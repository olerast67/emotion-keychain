// Симуляция брелоков rev B с настоящей прошивкой: координатор.
//
// Каждая плата — процесс keychain_board (board.cpp) с прошивкой firmware/src/keychain и
// моделями микросхем. Координатор ведёт общее время, эфир между платами (реклама →
// прослушивание с уровнем сигнала по расстоянию), батарею (ток из отчёта платы →
// заряд ячейки → напряжение), перезагрузки и выключения, и гонит сценарии с проверками.
// Итог — отчёт и GIF того, что показывали панели.
//
//   keychain_sim <keychain_board> <папка для файлов> [сценарий…]
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "gif_writer.h"

namespace {

std::string board_exe, outdir;
uint64_t g_ms = 0;  // общее время, мс
int checks = 0, failures = 0;
std::vector<std::string> report_lines;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
  char b[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  printf("%s\n", b);
  fflush(stdout);
  report_lines.push_back(b);
}

#define CHECK(c, ...)                         \
  do {                                        \
    ++checks;                                 \
    char m_[512];                             \
    snprintf(m_, sizeof m_, __VA_ARGS__);     \
    if (c)                                    \
      say("  ok    %s", m_);                  \
    else {                                    \
      ++failures;                             \
      say("  FAIL  %s  (%s:%d)", m_, __FILE__, __LINE__); \
    }                                         \
  } while (0)

uint32_t rnd_state = 2024;
uint32_t rnd() {
  rnd_state ^= rnd_state << 13, rnd_state ^= rnd_state >> 17, rnd_state ^= rnd_state << 5;
  return rnd_state;
}

// ---------------------------------------------------------------- батарея

const float kOcv[] = {3.30f, 3.50f, 3.60f, 3.68f, 3.74f, 3.79f, 3.85f, 3.92f, 4.00f, 4.08f, 4.18f};
float ocv(double pct) {
  if (pct <= 0) return 3.0f + (float)fmax(pct, -10) * 0.03f;
  if (pct >= 100) return 4.18f;
  const int i = (int)(pct / 10);
  return kOcv[i] + (kOcv[i + 1] - kOcv[i]) * (float)((pct - i * 10) / 10);
}

// ---------------------------------------------------------------- плата

struct Board {
  int id = 0;
  std::string name, flash, uicr;
  pid_t pid = -1;
  FILE* to = nullptr;
  FILE* from = nullptr;
  uint64_t boot_ms = 0;  // общее время последнего сброса
  bool alive = false, off = false, dead = false;
  bool button_wakes = true;  // сон с пробуждением от кнопки; без неё будит только USB
  // Загрузчик (прошивки не идёт): 1 — вошли по просьбе прошивки (dfu, кнопка при подключении
  // USB), без компьютера через 3 с запускает прошивку; 2 — кнопка была нажата при USB в момент
  // сброса, без компьютера — через 10 с. Компьютер находит диск KEYCHAIN, и тогда загрузчик
  // ждёт, пока USB не отключат. Прошивка стартует с той же причиной сброса, что и он.
  int boot_mode = 0;
  uint64_t boot_at = 0;
  uint32_t boot_reason = 0;
  bool boot_mounted = false;
  bool host = true;  // USB от компьютера; false — зарядка от розетки
  // Последние SET мира (свет, звук, температура, неисправности): новый процесс платы — та же
  // плата после сброса — получает их заново.
  std::map<std::string, std::string> world;
  double x = 0;  // положение, м
  // Мир
  bool vbus = false, button = false, charging = false, vbus_was = false;
  double cap_mah = 150, soc_mah = 120;
  float last_cell = 0;
  bool last_chg = false;
  // Отчёт
  std::map<std::string, std::string> st;
  std::vector<std::pair<uint64_t, std::string>> out;  // строки консоли с общим временем
  std::vector<std::pair<uint64_t, std::string>> events;
  std::vector<std::string> pending;
  double uams_total = 0, uams_window = 0;
  uint64_t window_ms = 0;
  uint64_t next_adv_ms = 0;
  int boots = 0;
  uint64_t t0_override = 0;
  double vib_max = 0;  // наибольшее напряжение на моторе, В
  double off_ua = 12;  // ток в System OFF по тому, что прошивка оставила включённым (offua)

  long num(const char* k) const {
    auto it = st.find(k);
    return it == st.end() ? -1 : strtol(it->second.c_str(), nullptr, 0);
  }
  double dnum(const char* k) const {
    auto it = st.find(k);
    return it == st.end() ? -1 : atof(it->second.c_str());
  }
  std::string hex(const char* k) const {
    auto it = st.find(k);
    return it == st.end() ? "" : it->second;
  }
  void cmd(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char b[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    pending.push_back(b);
    remember(b);
  }
  void remember(const char* line) {
    if (strncmp(line, "SET ", 4)) return;
    std::string key(line + 4, strcspn(line + 4, " "));
    // vbus, button, cell, chg задаёт сам координатор; разовые события и состояния, которые
    // сброс платы не переживает, не повторяются.
    static const char* const kSkip[] = {"vbus", "button", "cell", "chg", "hang", "lisreset", "wedge", "sda", "osda"};
    for (const char* k : kSkip)
      if (key == k) return;
    if (key == "acc" || key == "walk" || key == "shake" || key == "still") key = "motion";
    world[key] = line;
  }
};

std::vector<Board*> boards;

void spawn(Board& b, uint32_t reason) {
  int a[2], c[2];
  if (pipe(a) || pipe(c)) abort();
  const pid_t pid = fork();
  if (pid == 0) {
    dup2(a[0], 0), dup2(c[1], 1);
    close(a[1]), close(c[0]);
    char id[16], rs[16], t0[32];
    snprintf(id, sizeof id, "%d", b.id);
    snprintf(rs, sizeof rs, "%u", reason);
    snprintf(t0, sizeof t0, "%llu", (unsigned long long)b.t0_override);
    setenv("ASAN_OPTIONS", "detect_leaks=0:abort_on_error=0", 1);
    execl(board_exe.c_str(), board_exe.c_str(), "--id", id, "--flash", b.flash.c_str(), "--uicr", b.uicr.c_str(),
          "--reset", rs, "--t0", t0, (char*)nullptr);
    _exit(127);
  }
  close(a[0]), close(c[1]);
  b.pid = pid;
  b.to = fdopen(a[1], "w");
  b.from = fdopen(c[0], "r");
  b.alive = true, b.off = false, b.dead = false;
  b.boot_ms = g_ms;
  ++b.boots;
  b.last_cell = 0, b.last_chg = !b.charging;
  // Мир — заново: новый процесс его не помнит.
  b.cmd("SET vbus %d", b.vbus ? 1 : 0);
  b.cmd("SET button %d", b.button ? 1 : 0);
  for (auto& [k, line] : b.world) b.pending.push_back(line);
}

// Загрузчик вместо прошивки (см. Board::boot_mode).
void enter_boot(Board& b, int mode, uint32_t reason) {
  b.boot_mode = mode, b.boot_at = g_ms, b.boot_reason = reason, b.boot_mounted = false;
  b.off = true;
  b.events.push_back({g_ms, mode == 1 ? "boot" : "boot_button"});
}

// Включение после сброса или из сна: сначала всегда загрузчик. Кнопка зажата при USB —
// он ждёт новую прошивку (BUTTON_DFU_NEEDS_USB), иначе сразу запускает прошивку.
void power_on(Board& b, uint32_t reason) {
  if (b.button && b.vbus)
    enter_boot(b, 2, reason);
  else
    spawn(b, reason);
}

void reap(Board& b) {
  if (b.to) fclose(b.to);
  if (b.from) fclose(b.from);
  b.to = b.from = nullptr;
  if (b.pid > 0) waitpid(b.pid, nullptr, 0);
  b.pid = -1;
  b.alive = false;
}

// Пропажа питания: процесс убит в любой момент, без отчёта.
void cut_power(Board& b) {
  if (!b.alive) return;
  kill(b.pid, SIGKILL);
  reap(b);
  b.dead = true;
}

constexpr uint32_t kOff = 1u << 16, kVbus = 1u << 20, kSreq = 1u << 2, kDog = 1u << 1;

void handle_event(Board& b, const std::string& ev) {
  b.events.push_back({g_ms, ev});
  reap(b);
  const uint64_t t = g_ms;
  if (ev == "reset") {
    power_on(b, kSreq);
  } else if (ev == "watchdog") {
    power_on(b, kDog);
  } else if (ev == "off") {
    b.off = true, b.button_wakes = true;
    b.off_ua = b.dnum("offua");
  } else if (ev == "off_vbus") {
    b.off = true, b.button_wakes = false;
    b.off_ua = b.dnum("offua");
  } else if (ev == "dfu") {
    enter_boot(b, 1, kSreq);  // программный сброс с просьбой к загрузчику
  } else {
    say("  !!    плата %s: %s в %llu мс", b.name.c_str(), ev.c_str(), (unsigned long long)t);
    b.dead = true;
  }
}

bool talk(Board& b, uint64_t local) {
  for (auto& p : b.pending) fprintf(b.to, "%s\n", p.c_str());
  b.pending.clear();
  fprintf(b.to, "STEP %llu\n", (unsigned long long)local);
  fflush(b.to);
  char line[8192];
  std::string ev;
  while (fgets(line, sizeof line, b.from)) {
    line[strcspn(line, "\n")] = 0;
    if (!strcmp(line, "OK")) break;
    if (!strncmp(line, "OUT ", 4)) {
      b.out.push_back({g_ms, line + 4});
    } else if (!strncmp(line, "EV ", 3)) {
      ev = line + 3;
    } else if (!strncmp(line, "ST ", 3)) {
      b.st.clear();
      for (char* tok = strtok(line + 3, " "); tok; tok = strtok(nullptr, " ")) {
        char* eq = strchr(tok, '=');
        if (eq) *eq = 0, b.st[tok] = eq + 1;
      }
      const double q = b.dnum("q");
      b.uams_total += q, b.uams_window += q;
      if (b.dnum("vibv") > b.vib_max) b.vib_max = b.dnum("vibv");
    }
  }
  if (feof(b.from) && ev.empty()) ev = "crash";
  if (!ev.empty()) {
    handle_event(b, ev);
    return false;
  }
  return true;
}

// Эфир: каждая реклама долетает до тех, кто слушает, с уровнем по расстоянию. air_loss — доля
// пакетов, которые теряются по дороге (помехи, Wi-Fi, другие устройства).
double air_loss = 0;

void air(uint64_t t0, uint64_t t1) {
  for (Board* s : boards) {
    if (!s->alive || s->num("adv") != 1) continue;
    const long iv = s->num("iv");
    const std::string data = s->hex("data");
    if (iv <= 0 || data == "-") continue;
    if (s->next_adv_ms < t0) s->next_adv_ms = t0 + rnd() % 10;
    while (s->next_adv_ms < t1) {
      for (Board* r : boards) {
        if (r == s || !r->alive) continue;
        const double d = fmax(0.2, fabs(r->x - s->x));
        const int rssi = (int)lround(-59 - 20 * log10(d) + (double)(rnd() % 9) - 4);
        if (rssi < -100) continue;
        if (air_loss > 0 && (double)(rnd() % 10000) < air_loss * 10000.0) continue;
        r->cmd("RX %d %s", rssi, data.c_str());
      }
      s->next_adv_ms += (uint64_t)(iv * 0.625) + rnd() % 10;  // advDelay 0–10 мс
    }
  }
}

void battery(Board& b, uint64_t dt_ms) {
  if (!dt_ms) return;
  const double ua = b.uams_window / (double)dt_ms;
  b.uams_window = 0;
  const double h = (double)dt_ms / 3600000.0;
  if (b.vbus && b.alive && b.num("chgen") == 1) {
    // Заряжает MCP73832, только когда прошивка опустила нижний конец R3 (CHG_EN).
    if (b.soc_mah >= b.cap_mah * 0.995) b.charging = false;
    else if (b.soc_mah < b.cap_mah * 0.96) b.charging = true;
    if (b.charging) b.soc_mah = fmin(b.cap_mah, b.soc_mah + 100.0 * h);
  } else if (b.vbus) {
    b.charging = false;
  } else {
    b.charging = false;
    // Без отчётов платы (сон, загрузчик) — ток по состоянию: в System OFF — по тому, что
    // прошивка оставила включённым (~12 мкА, когда датчики спят: остальное — зарядник, диод и
    // плата защиты ячейки, docs/power.md), загрузчик ~3 мА. Обрыв питания (dead) — ячейка
    // отключена.
    const double idle_ua = b.alive || b.dead ? 0 : b.boot_mode ? 3000 : b.off ? b.off_ua : 0;
    b.soc_mah -= (ua + idle_ua) / 1000.0 * h;
  }
  const float v = ocv(b.soc_mah / b.cap_mah * 100.0) - (float)(ua / 1000.0 * 0.0003);  // 0.3 Ом
  if (fabs(v - b.last_cell) > 0.002f) b.cmd("SET cell %.3f", v), b.last_cell = v;
  if (b.charging != b.last_chg) b.cmd("SET chg %d", b.charging ? 1 : 0), b.last_chg = b.charging;
  if (b.soc_mah <= 0 && !b.vbus && b.alive) {
    say("  !!    %s: ячейка пуста — плата защиты отключила питание", b.name.c_str());
    cut_power(b);
  }
}

void step(uint64_t dt) {
  const uint64_t t1 = g_ms + dt;
  air(g_ms, t1);
  for (Board* b : boards) {
    if (b->alive) {
      talk(*b, b->t0_override + t1 - b->boot_ms);
    } else if (b->boot_mode) {
      // Загрузчик: компьютер находит диск примерно за секунду.
      if (b->vbus && b->host && g_ms - b->boot_at >= 1000) b->boot_mounted = true;
      const uint64_t limit = b->boot_mode == 1 ? 3000 : 10000;
      if ((b->boot_mounted && !b->vbus) || (!b->boot_mounted && g_ms - b->boot_at >= limit)) {
        b->boot_mode = 0;
        spawn(*b, b->boot_reason);
      }
    } else if ((b->off || b->dead) && b->pid < 0) {
      // Выключена: просыпается от кнопки или подключения USB.
      if (b->button && (b->dead || b->button_wakes)) {
        power_on(*b, b->dead ? 0 : kOff);
      } else if (b->vbus && b->dead) {
        power_on(*b, 0);
      } else if (b->vbus && !b->vbus_was && b->off) {
        power_on(*b, kVbus);  // подключили USB к спящей плате — nRF52840 просыпается
      }
    }
    battery(*b, dt);
    b->vbus_was = b->vbus;
  }
  g_ms = t1;
}

void run(uint64_t ms, uint64_t dt = 10) {
  const uint64_t end = g_ms + ms;
  while (g_ms < end) step(std::min<uint64_t>(dt, end - g_ms));
}

// Строка консоли, появившаяся после момента since.
bool said(Board& b, const char* text, uint64_t since, uint64_t* at = nullptr) {
  for (auto& [t, s] : b.out)
    if (t >= since && s.find(text) != std::string::npos) {
      if (at) *at = t;
      return true;
    }
  return false;
}
int count_said(Board& b, const char* text, uint64_t since) {
  int n = 0;
  for (auto& [t, s] : b.out) n += t >= since && s.find(text) != std::string::npos;
  return n;
}
bool happened(Board& b, const char* ev, uint64_t since) {
  for (auto& [t, e] : b.events)
    if (t >= since && e == ev) return true;
  return false;
}
int count_events(Board& b, const char* ev, uint64_t since) {
  int n = 0;
  for (auto& [t, e] : b.events) n += t >= since && e == ev;
  return n;
}
// Процент заряда, который печатает команда sensors (−1 — не напечатан).
int sensors_pct(Board& b) {
  const uint64_t t = g_ms;
  b.cmd("IN sensors");
  run(500);
  int pct = -1;
  for (auto& [ms, l] : b.out)
    if (ms >= t && l.find("заряд ") != std::string::npos) sscanf(l.c_str() + l.find("заряд ") + strlen("заряд "), "%d", &pct);
  return pct;
}
bool any_event(Board& b, uint64_t since) {
  for (auto& [t, e] : b.events)
    if (t >= since) return true;
  return false;
}

// ---------------------------------------------------------------- запись экранов в GIF

struct Recorder {
  sim::GifWriter gif;
  std::vector<Board*> bs;
  bool open_ = false;
  void open(const std::string& name, std::vector<Board*> list) {
    bs = list;
    const int w = 128 * 2 * (int)bs.size() + 8 * ((int)bs.size() + 1), h = 64 * 2 + 16;
    open_ = gif.open(outdir + "/" + name, w, h, {{20, 22, 28}, {6, 6, 8}, {235, 240, 255}, {70, 200, 120}});
  }
  void frame(int delay_cs) {
    if (!open_) return;
    const int w = 128 * 2 * (int)bs.size() + 8 * ((int)bs.size() + 1), h = 64 * 2 + 16;
    std::vector<uint8_t> px((size_t)w * h, 0);
    for (size_t k = 0; k < bs.size(); ++k) {
      Board& b = *bs[k];
      const int ox = 8 + (int)k * (256 + 8), oy = 8;
      std::vector<uint8_t> ram(1024, 0);
      bool vis = false;
      if (b.alive) {
        fprintf(b.to, "DUMP\n");
        fflush(b.to);
        char line[4096];
        while (fgets(line, sizeof line, b.from)) {
          if (!strncmp(line, "OK", 2)) break;
          if (!strncmp(line, "FB ", 3)) {
            vis = strstr(line, "vis=1") != nullptr;
            const char* hx = strrchr(line, ' ') + 1;
            for (int i = 0; i < 1024; ++i) {
              unsigned v;
              sscanf(hx + 2 * i, "%2x", &v);
              ram[i] = (uint8_t)v;
            }
          }
        }
      }
      for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x) {
          const bool on = vis && (ram[(y / 8) * 128 + x] >> (y % 8) & 1);
          for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) px[(size_t)(oy + y * 2 + dy) * w + ox + x * 2 + dx] = on ? 2 : 1;
        }
      if (b.charging || b.vbus)  // полоска USB под экраном
        for (int x = 0; x < 20; ++x) px[(size_t)(oy + 130 + 2) * w + ox + x] = 3;
    }
    gif.frame(px, delay_cs);
  }
  void close() {
    if (open_) gif.close();
    open_ = false;
  }
};

// Прогон с записью кадров каждые 100 мс.
void run_rec(Recorder& r, uint64_t ms) {
  const uint64_t end = g_ms + ms;
  while (g_ms < end) {
    run(100);
    r.frame(10);
  }
}

Board* make_board(int id, const char* name, bool fresh = true) {
  Board* b = new Board;
  b->id = id;
  b->name = name;
  b->flash = outdir + "/flash" + std::to_string(id) + ".bin";
  b->uicr = outdir + "/uicr" + std::to_string(id) + ".bin";
  if (fresh) unlink(b->flash.c_str()), unlink(b->uicr.c_str());
  boards.push_back(b);
  return b;
}

// Плата больше не нужна сценарию: консоль — в файл, процесс завершается.
void drop_board(Board* b) {
  FILE* f = fopen((outdir + "/console-" + b->name + ".txt").c_str(), "w");
  if (f) {
    for (auto& [t, l] : b->out) fprintf(f, "%9.3f  %s\n", (double)t / 1000.0, l.c_str());
    for (auto& [t, e] : b->events) fprintf(f, "%9.3f  [%s]\n", (double)t / 1000.0, e.c_str());
    fclose(f);
  }
  if (b->alive) {
    fprintf(b->to, "QUIT\n");
    fflush(b->to);
    reap(*b);
  }
  boards.erase(std::find(boards.begin(), boards.end(), b));
  delete b;
}

void drop_boards() {
  while (!boards.empty()) drop_board(boards.front());
}

void health(Board& b, const char* what) {
  CHECK(b.num("mism") == 0, "%s: экран показывает ровно кадр движка (расхождений %ld)", what, b.num("mism"));
  CHECK(b.num("dark") == 0, "%s: экран не гаснет, когда должен гореть (%ld)", what, b.num("dark"));
  CHECK(b.num("i2c") == 0, "%s: ошибок шины и запрещённых записей нет (%ld)", what, b.num("i2c"));
}

// Подождать (шагами step мс, не дольше ms), пока не выполнится условие; время ожидания в мс
// или −1, если не дождались.
long wait_for(const std::function<bool()>& done, uint64_t ms, uint64_t step = 100) {
  const uint64_t t0 = g_ms;
  while (g_ms - t0 < ms) {
    if (done()) return (long)(g_ms - t0);
    run(step, step < 10 ? step : 10);
  }
  return done() ? (long)(g_ms - t0) : -1;
}

void press(Board& b, uint64_t ms) {
  b.button = true;
  b.cmd("SET button 1");
  run(ms);
  b.button = false;
  b.cmd("SET button 0");
}

// Выключить удержанием кнопки 3 с и дождаться сна; true — уснула.
bool power_off(Board& b) {
  const uint64_t t = g_ms;
  press(b, 3500);
  wait_for([&] { return !b.alive && happened(b, "off", t); }, 25000);
  return !b.alive && happened(b, "off", t);
}

// Разбудить спящую кнопкой (короткое нажатие); true — прошивка загрузилась.
bool wake(Board& b) {
  const uint64_t t = g_ms;
  b.button = true;
  run(100);
  b.cmd("SET button 1");
  run(300);
  b.button = false;
  b.cmd("SET button 0");
  wait_for([&] { return b.alive && said(b, "Экран ", t); }, 5000);
  return b.alive && said(b, "Экран ", t);
}

// Сосед B в метре от A (сценарии встречи и атаки).
Board& neighbour() {
  for (Board* b : boards)
    if (b->name == "B") return *b;
  Board& b = *make_board(1, "B");
  b.x = 1.0;
  spawn(b, 0);
  run(4000);
  return b;
}

// ---------------------------------------------------------------- сценарии

void sc_first_boot() {
  say("\n== Первое включение новой платы ==");
  Board& a = *make_board(0, "A");
  spawn(a, 0);
  run(4000);
  CHECK(happened(a, "reset", 0), "прошивка записала 3.3 В в UICR и перезагрузилась");
  CHECK(a.hex("uicr") == "fffffffd", "REGOUT0 = 3.3 В (uicr=%s)", a.hex("uicr").c_str());
  CHECK(a.boots == 2, "загрузок: %d (запись UICR и рабочая)", a.boots);
  CHECK(said(a, "Экран есть, акселерометр есть, свет есть", 0), "экран, акселерометр и датчик света отвечают");
  CHECK(said(a, "плата rev B, прошивка ", 0), "в приветствии — версия прошивки");
  a.cmd("IN ver");
  run(300);
  CHECK(said(a, "Прошивка ", 3500), "команда ver — версия прошивки");
  CHECK(a.num("vis") == 1 && a.num("lit") > 200, "на экране лицо: горит %ld пикселей", a.num("lit"));
  CHECK(a.num("adv") == 1 && a.num("iv") == 1600, "реклама в эфире раз в 1 с");
  CHECK(a.hex("data").find("ffff4b4302") != std::string::npos, "пакет: компания 0xFFFF, метка KC, версия 2");
  CHECK(a.num("hf") == 0, "кварц 64 МГц отпущен после старта микрофона");
  CHECK(a.num("mic") == 1 && a.num("acc") == 1, "микрофон и акселерометр работают");
  health(a, "старт");
  run(20000);
  CHECK(!any_event(a, 4000), "20 с работы без сбросов");
}

void sc_sensors() {
  say("\n== Датчики и реакции ==");
  Board& a = *boards[0];
  Recorder rec;
  rec.open("sensors.gif", {&a});
  a.cmd("IN v on");
  run(300);
  uint64_t t = g_ms;
  a.cmd("TAP 1");
  run_rec(rec, 1500);
  CHECK(said(a, "Событие: tap", t), "касание — событие tap");
  t = g_ms;
  a.cmd("TAP 2");
  run_rec(rec, 1500);
  CHECK(said(a, "Событие: double_tap", t) && !said(a, "Событие: tap", t), "двойное касание — только double_tap");
  // Датчик сначала отмечает одиночное касание, двойное — после второго: и при медленном
  // двойном (0.35 с между касаниями) одиночного события нет.
  t = g_ms;
  a.cmd("TAP 2 350");
  run_rec(rec, 1500);
  CHECK(said(a, "Событие: double_tap", t) && !said(a, "Событие: tap", t),
        "медленное двойное касание — тоже только double_tap");
  t = g_ms;
  a.cmd("SET shake 5 2.2");
  run_rec(rec, 3000);
  a.cmd("SET still");
  run_rec(rec, 2000);
  CHECK(said(a, "Событие: shake", t), "тряска — shake / shake_hard");
  CHECK(!said(a, "Событие: walk", t), "тряска не принята за ходьбу");
  const long steps0 = a.num("steps");
  t = g_ms;
  a.cmd("SET walk 2 0.4");
  run(31000);
  a.cmd("SET still");
  run(12000);
  const long ds = a.num("steps") - steps0;
  CHECK(said(a, "Событие: walk", t), "ходьба — walk");
  CHECK(ds >= 55 && ds <= 64, "30 с ходьбы по 2 шага/с — шагов %ld (около 60)", ds);
  t = g_ms;
  a.cmd("SET acc 0 -1 0");
  run_rec(rec, 2500);
  CHECK(said(a, "Событие: upside_down", t), "перевернули — upside_down");
  a.cmd("SET acc 0 1 0");
  run_rec(rec, 2500);
  t = g_ms;
  a.cmd("SET acc 0 0.05 0");
  run(300);
  a.cmd("SET acc 0 3.5 0");
  run(60);
  a.cmd("SET acc 0 1 0");
  run_rec(rec, 2500);
  CHECK(said(a, "Событие: free_fall", t) && said(a, "Событие: impact", t), "падение и удар");
  t = g_ms;
  a.cmd("SET temp -5");
  run(130000, 20);
  CHECK(said(a, "Событие: cold", t), "мороз — cold");
  a.cmd("SET temp 24");
  run(130000, 20);
  rec.close();
  health(a, "датчики");
}

void sc_sound() {
  say("\n== Звук ==");
  Board& a = *boards[0];
  a.cmd("IN v on");  // события печатаются и без сценария sensors
  a.cmd("SET mic -62 0");
  run(6000);
  uint64_t t = g_ms;
  a.cmd("SET mic -8 0");
  run(40);
  a.cmd("SET mic -62 0");
  run(1500);
  CHECK(said(a, "Событие: loud", t), "хлопок — loud");
  t = g_ms;
  a.cmd("SET mic -30 0");
  run(10000);
  CHECK(said(a, "Событие: music", t), "ровный звук — music");
  t = g_ms;
  a.cmd("SET mic -62 0");
  run(13000);
  CHECK(said(a, "Событие: quiet", t), "стихло — quiet");
  t = g_ms;
  a.cmd("SET mic -35 1");
  run(10000);
  a.cmd("SET mic -62 0");
  run(12000);
  CHECK(said(a, "Событие: speech", t), "речь — speech");
}

void sc_light_and_pocket() {
  say("\n== Свет, карман и экран ==");
  Board& a = *boards[0];
  Recorder rec;
  rec.open("pocket.gif", {&a});
  a.cmd("IN v on");  // события печатаются и без сценария sensors
  run(300);
  uint64_t t = g_ms;
  a.cmd("SET lux 0.3");
  run_rec(rec, 12000);
  CHECK(said(a, "Событие: dark", t), "в кармане — dark");
  run(60000);
  CHECK(a.num("disp") == 0, "через минуту темноты экран обесточен");
  CHECK(a.num("mic") == 0, "микрофон в темноте выключен");
  const double q0 = a.uams_total;
  const uint64_t t0 = g_ms;
  run(120000, 50);
  const double pocket_ua = (a.uams_total - q0) / (double)(g_ms - t0);
  CHECK(pocket_ua < 170, "ток в кармане %.0f мкА (docs/power.md: ≈0.13 мА)", pocket_ua);
  say("        ток в кармане: %.0f мкА", pocket_ua);
  t = g_ms;
  a.cmd("TAP 1");
  run_rec(rec, 2000);
  CHECK(a.num("disp") == 1 && a.num("vis") == 1, "касание в темноте показывает лицо");
  health(a, "экран после включения");
  run_rec(rec, 62000 - 2000);
  run(5000);
  CHECK(a.num("disp") == 0, "и через минуту снова гаснет");
  t = g_ms;
  a.cmd("SET lux 300");
  run_rec(rec, 8000);
  CHECK(said(a, "Событие: light", t) && a.num("disp") == 1 && a.num("vis") == 1, "достали из кармана — свет и лицо");
  health(a, "из кармана");
  // Микрофон включается заново: первые блоки (нули библиотеки и выход на режим) пропускаются,
  // иначе обычная тишина комнаты выглядела бы громкой после «полной тишины». Звук выключен:
  // своя пищалка при выходе из кармана заглушила бы эти блоки и без пропуска.
  a.cmd("IN snd");
  a.cmd("SET lux 0.3");
  run(15000);
  CHECK(a.num("mic") == 0, "снова в кармане — микрофон выключен");
  t = g_ms;
  a.cmd("SET lux 300");
  run_rec(rec, 8000);
  CHECK(a.num("mic") == 1 && !said(a, "Событие: loud", t) && !said(a, "Событие: music", t) &&
            !said(a, "Событие: speech", t),
        "микрофон снова включён: ложных loud, music и speech нет");
  a.cmd("IN snd");
  rec.close();
  const double q1 = a.uams_total;
  const uint64_t t1 = g_ms;
  run(120000, 20);
  say("        ток с лицом на экране: %.2f мА", (a.uams_total - q1) / (double)(g_ms - t1) / 1000.0);
}

void sc_charge() {
  say("\n== Зарядка ==");
  Board& a = *boards[0];
  a.cmd("IN v on");
  a.soc_mah = 60;
  uint64_t t = g_ms;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(15000);
  CHECK(said(a, "Событие: charge_start", t), "USB — начало зарядки");
  a.cmd("IN sensors");
  run(500);
  CHECK(said(a, "USB да, заряжается да", t), "консоль видит USB и зарядку");
  CHECK(a.num("hf") == 1, "на USB кварц 64 МГц запрошен (USBD и замер температуры для заряда)");
  // Микрофон перезапускается на USB (карман и обратно): его старт не снимает запрос кварца.
  a.cmd("SET lux 0.3");
  run(15000);
  a.cmd("SET lux 300");
  run(8000);
  CHECK(a.num("mic") == 1 && a.num("hf") == 1, "микрофон перезапущен на USB — кварц по-прежнему запрошен");
  t = g_ms;
  a.soc_mah = a.cap_mah * 0.994;
  run(15000);
  CHECK(said(a, "Событие: charge_full", t), "зарядилась — charge_full");
  t = g_ms;
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(15000);
  CHECK(said(a, "Событие: charge_stop", t), "USB отключили — charge_stop");
  // Сразу после отключения система ~0.1 с питается через диод P-MOSFET: замер тогда показал
  // бы почти пустую ячейку.
  CHECK(!said(a, "Событие: battery_low", t) && !said(a, "Событие: battery_critical", t),
        "после отключения от полной зарядки — не «мало заряда»");
  t = g_ms;
  a.cmd("IN sensors");
  run(500);
  int pct = -1;
  for (auto& [ms, l] : a.out)
    if (ms >= t && l.find("заряд ") != std::string::npos) sscanf(l.c_str() + l.find("заряд ") + strlen("заряд "), "%d", &pct);
  CHECK(pct >= 90, "заряд после отключения от полной зарядки: %d%%", pct);
  CHECK(a.num("hf") == 0, "без USB кварц отпущен");
  CHECK(!any_event(a, t), "без сбросов");
  // Кабель вставили и почти сразу выдернули (плохой контакт): провал питания через диод сразу
  // после отключения не попадает в замер, и процент не падает до следующей зарядки.
  a.soc_mah = 120;
  run(300000, 50);  // процент устанавливается по новому напряжению ячейки
  int worst = 0;
  for (const int hold : {1900, 1950, 1980}) {
    const int p0 = sensors_pct(a);
    a.vbus = true;
    a.cmd("SET vbus 1");
    run((uint64_t)hold, 10);
    a.vbus = false;
    a.cmd("SET vbus 0");
    run(60000, 20);
    const int p1 = sensors_pct(a);
    if (p0 - p1 > worst) worst = p0 - p1;
    say("        USB на %d мс: заряд %d%% → %d%%", hold, p0, p1);
  }
  CHECK(worst <= 2, "USB на 1.9–2 с — процент не падает (худшее падение %d)", worst);
  CHECK(!said(a, "Событие: battery_low", t), "и без ложного «мало заряда»");
  a.soc_mah = 120;
}

void sc_meeting() {
  say("\n== Встреча двух брелоков ==");
  Board& a = *boards[0];
  Board& b = neighbour();  // метр от A
  a.cmd("IN v on");
  b.cmd("IN v on");
  Recorder rec;
  rec.open("meeting.gif", {&a, &b});
  a.cmd("IN snd");  // у A без звука: во время вибро микрофон глушит только мотор
  const uint64_t t = g_ms;
  a.vib_max = b.vib_max = 0;
  uint64_t ta = 0, tb = 0, t_inj = 0;
  bool inj_vib = false;
  for (int i = 0; i < 1800 && !(said(a, "Дуэль с", t, &ta) && said(b, "Дуэль с", t, &tb)); ++i) {
    run(100);
    if (said(a, "Дуэль с", t) || said(b, "Дуэль с", t)) rec.frame(10);
    if (!t_inj && said(a, "Дуэль с", t)) {
      // Мотор жужжит: толчок, похожий на касание, и громкий звук — не события.
      t_inj = g_ms;
      inj_vib = a.num("vb") == 1;
      a.cmd("TAP 1");
      a.cmd("SET mic -8 0");
      run(40);
      a.cmd("SET mic -62 0");
    }
  }
  run_rec(rec, 12000);
  rec.close();
  CHECK(ta && tb, "дуэль у обоих (через %.1f с)", ta ? (double)(ta - t) / 1000.0 : -1.0);
  CHECK((ta > tb ? ta - tb : tb - ta) <= 150, "начало одновременно: разница %lld мс", (long long)ta - (long long)tb);
  const bool wa = said(a, "победа", t), wb = said(b, "победа", t);
  CHECK(wa != wb, "победитель один (A %s, B %s)", wa ? "победил" : "проиграл", wb ? "победил" : "проиграл");
  CHECK(a.num("fr") == 1 && b.num("fr") == 1, "каждый записал другого в знакомые");
  // Мотор на 3 В стоит на VSYS: ШИМ должен давать ему около 3 В, а не VSYS − 3 В.
  CHECK(a.vib_max > 2.6 && a.vib_max < 3.3 && b.vib_max > 2.6 && b.vib_max < 3.3,
        "вибро за дуэль, на моторе ~3 В (A %.2f, B %.2f В)", a.vib_max, b.vib_max);
  CHECK(t_inj && inj_vib && !said(a, "Событие: tap", t_inj) && !said(a, "Событие: loud", t_inj),
        "пока жужжит свой мотор, касание и громкий звук не события");
  a.cmd("IN snd");
  const uint64_t t2 = g_ms;
  run(300000, 20);
  CHECK(!said(a, "Дуэль с", t2) && !said(a, "Встреча с", t2), "5 минут рядом — повторной сцены нет (перерыв 10 мин)");
  health(a, "встреча A");
  health(b, "встреча B");
}

void sc_attack() {
  say("\n== Атака по эфиру ==");
  Board& a = *boards[0];
  const long xp0 = a.num("xp");
  const uint64_t attack_t0 = g_ms;
  // Свой позывной A — в его пакете (байты 5–8 данных производителя).
  const std::string d = a.hex("data");
  const size_t k = d.find("ffff4b4302");
  const std::string hash_hex = k != std::string::npos ? d.substr(k + 10, 8) : "01020304";
  const std::string to_hex = hash_hex.substr(0, 4);
  uint64_t t = g_ms;
  // 1) Мусор: поля любой длины, обрезанные пакеты, нулевые длины.
  for (int i = 0; i < 4000; ++i) {
    const int n = (int)(rnd() % 32);
    std::string h;
    char c[4];
    for (int j = 0; j < n; ++j) snprintf(c, sizeof c, "%02x", (unsigned)(rnd() & 0xFF)), h += c;
    if (i % 3 == 0 && n > 4) h.replace(0, 4, "ffff");  // испорченная длина поля
    a.cmd("RX -40 %s", h.empty() ? "00" : h.c_str());
    if (i % 40 == 0) run(10);
  }
  run(2000);
  CHECK(a.alive && !any_event(a, t), "4000 испорченных пакетов — плата работает (без сбоев памяти)");
  // 2) Объявление дуэли от «брелока» с позывным 1, которого раньше не слышали: одним пакетом
  // вблизи и потоком издалека. Сцену принимают только от соседа, услышанного вблизи дважды
  // (поток объявлений вблизи — это уже сосед, его пакеты не отличить от настоящего).
  t = g_ms;
  char ann[128];
  snprintf(ann, sizeof ann, "02010410ffffff4b430201000000%02x%02x%02x01%s", 0, 0x09, 128, to_hex.c_str());
  a.cmd("RX -40 %s", ann);
  run(3000);
  for (int i = 0; i < 200; ++i) {
    a.cmd("RX -95 %s", ann);
    run(50);
  }
  run(3000);
  CHECK(!said(a, "Дуэль с", t) && !said(a, "Встреча с", t),
        "объявление сцены от неслыханного брелока не принято: один пакет вблизи, поток издалека");
  // 3) Поток выдуманных брелоков вблизи: каждый слышен дважды.
  t = g_ms;
  for (int i = 0; i < 60; ++i) {
    char h[128];
    const uint32_t fake = 0xF0000000u + (uint32_t)i * 7919u;
    snprintf(h, sizeof h, "02010410ffffff4b4302%02x%02x%02x%02x%02x%02x80000000", fake & 0xFF, fake >> 8 & 0xFF,
             fake >> 16 & 0xFF, fake >> 24, 255, 0x01);
    a.cmd("RX -40 %s", h);
    run(300);
    a.cmd("RX -40 %s", h);
    run(2500);
  }
  run(5000);
  a.cmd("IN me");
  run(500);
  const int meets = count_said(a, "Встреча с", t) + count_said(a, "Дуэль с", t);
  say("        сцен с выдуманными брелоками: %d (уровень в пакете 255 — пакеты отброшены)", meets);
  CHECK(meets == 0, "пакеты с уровнем больше числа имплантов отброшены");
  t = g_ms;
  for (int i = 0; i < 40; ++i) {
    char h[128];
    const uint32_t fake = 0xF1000000u + (uint32_t)i * 7919u;
    snprintf(h, sizeof h, "02010410ffffff4b4302%02x%02x%02x%02x%02x%02x80000000", fake & 0xFF, fake >> 8 & 0xFF,
             fake >> 16 & 0xFF, fake >> 24, 9, 0x01);
    a.cmd("RX -40 %s", h);
    run(300);
    a.cmd("RX -40 %s", h);
    run(2500);
  }
  run(5000);
  const long gained = a.num("xp") - xp0;
  say("        опыт за 40 выдуманных «старших» брелоков: +%ld", gained);
  CHECK(gained <= 5 * 40 + 3 * 25 + 400, "опыт ограничен дневными лимитами (+%ld)", gained);
  CHECK(a.num("fr") <= 16, "таблица знакомых не переполнена (%ld)", a.num("fr"));
  // Настоящая встреча после атаки всё ещё работает: B рядом, перерыв уже прошёл.
  neighbour();
  run(240000, 20);
  CHECK(count_said(a, "Встреча с", attack_t0) + count_said(a, "Дуэль с", attack_t0) >= 1,
        "во время и после атаки настоящий сосед встречается (сцен с ним: %d)",
        count_said(a, "Встреча с", attack_t0) + count_said(a, "Дуэль с", attack_t0));
  health(a, "атака");
}

void sc_power_off_battery() {
  say("\n== Выключение кнопкой на батарее ==");
  Board& a = *boards[0];
  a.cmd("IN t 10:15");
  run(500);
  const long xp = a.num("xp");
  uint64_t t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(3500);
  a.button = false;
  a.cmd("SET button 0");
  run(16000);
  CHECK(happened(a, "off", t), "удержание 3 с — сцена и полный сон (System OFF)");
  CHECK(!a.alive, "плата спит");
  CHECK(a.off_ua < 15, "ток выключенного %.1f мкА: экран, датчики и микрофон выключены", a.off_ua);
  run(5000);
  t = g_ms;
  a.button = true;
  run(100);
  a.cmd("SET button 1");
  run(300);
  a.button = false;
  a.cmd("SET button 0");
  run(4000);
  CHECK(a.alive && said(a, "Экран есть", t), "кнопка будит: загрузка");
  CHECK(a.num("xp") >= xp, "рост сохранён (опыт %ld, был %ld)", a.num("xp"), xp);
  CHECK(said(a, "время и дата сброшены", t), "после выключения время сброшено и консоль просит задать");
  // Решает это загрузчик (BUTTON_DFU_NEEDS_USB — правило модели в power_on), прошивка тоже
  // не уходит в него сама.
  CHECK(!happened(a, "dfu", t) && !happened(a, "boot_button", t),
        "кнопка без USB не включает загрузчик (модель загрузчика и прошивка)");
  // Будят, держа кнопку 6 с, «пока не включится»: это не удержание для выключения.
  run(5000);
  a.button = true;
  a.cmd("SET button 1");
  run(3500);
  a.button = false;
  a.cmd("SET button 0");
  run(16000);
  CHECK(!a.alive, "снова выключен");
  t = g_ms;
  a.button = true;
  run(100);
  a.cmd("SET button 1");
  run(6000);
  a.button = false;
  a.cmd("SET button 0");
  run(10000);
  CHECK(a.alive && !happened(a, "off", t), "долгое нажатие, которым разбудили, не выключает снова");
  // Зажали кнопку на работающем брелоке и подключили USB — загрузчик, как написано в README.
  a.cmd("IN t 11:00");
  run(500);
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(500);
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(1500);
  CHECK(happened(a, "dfu", t) && a.boot_mode == 1, "кнопка + USB — загрузчик для новой прошивки");
  a.button = false;
  run(3000);
  CHECK(a.boot_mode == 1 && !a.alive, "компьютер видит диск KEYCHAIN: загрузчик ждёт, пока USB подключён");
  t = g_ms;
  a.vbus = false;
  wait_for([&] { return a.alive && said(a, "Экран есть", t); }, 5000);
  CHECK(a.alive && said(a, "Экран есть", t), "USB отключили — загрузчик запускает прошивку");
  a.cmd("IN t");
  run(300);
  CHECK(said(a, "Время 11:0", t) && !said(a, "не задано", t) && !said(a, "время и дата сброшены", t),
        "часы пережили загрузчик: в него уходят программным сбросом");
}

void sc_power_off_usb() {
  say("\n== Выключение на зарядке ==");
  Board& a = *boards[0];
  a.cmd("IN t 10:15");
  run(500);
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(3000);
  uint64_t t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(3500);
  a.button = false;
  a.cmd("SET button 0");
  run(20000);
  CHECK(!happened(a, "off", t) && a.alive, "на USB не уходит в System OFF (кнопка при USB включила бы загрузчик)");
  CHECK(a.num("disp") == 0 && a.num("adv") == 0 && a.num("mic") == 0, "экран, эфир и микрофон выключены");
  run(120000, 50);
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(200);
  a.button = false;
  a.cmd("SET button 0");
  run(5000);
  CHECK(happened(a, "reset", t) && a.alive && said(a, "Экран есть", t), "кнопка включает (перезагрузкой)");
  CHECK(!said(a, "время и дата сброшены", t), "часы пережили выключение на USB");
  a.cmd("IN me");
  run(300);
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  // Выключают на батарее и не отпускают кнопку, а пока system_off ждёт отпускания, подключают
  // USB: он возвращается, и выключение идёт на USB (экран уже погашен, его шина выключена).
  // USB — позже 10 с от нажатия: это уже не жест загрузчика.
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  const long w = wait_for([&] { return a.num("disp") == 0; }, 25000);
  CHECK(w >= 0 && a.alive && !happened(a, "off", t), "кнопку держат: сцена прощания, экран погас, ждёт отпускания");
  if (g_ms < t + 10600) run(t + 10600 - g_ms);
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(1000);
  a.button = false;
  a.cmd("SET button 0");
  run(5000);
  CHECK(a.alive && !any_event(a, t) && a.num("disp") == 0 && a.num("adv") == 0,
        "USB подключили через 10.6 с, пока ждали отпускания, — выключение на USB, без сбросов");
  health(a, "выключение с USB во время ожидания");
  t = g_ms;
  a.vbus = false;
  a.cmd("SET vbus 0");
  wait_for([&] { return happened(a, "off", t); }, 5000);
  CHECK(happened(a, "off", t) && !a.alive, "USB отключили — полный сон с пробуждением от кнопки");
  CHECK(wake(a), "кнопка будит");
  // Жест загрузчика и после прощания: кнопку, зажатую для выключения, держат, экран уже погас,
  // и через 8 с от нажатия подключают USB — README обещает 10 с.
  run(3000);
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  wait_for([&] { return a.num("disp") == 0; }, 25000);
  if (g_ms < t + 8000) run(t + 8000 - g_ms);
  const bool dark = a.alive && a.num("disp") == 0;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(500);
  CHECK(dark && happened(a, "dfu", t) && a.boot_mode == 1, "экран погас, USB через 8 с от нажатия — загрузчик");
  a.button = false;
  run(2000);
  t = g_ms;
  a.vbus = false;
  wait_for([&] { return a.alive && said(a, "Экран есть", t); }, 5000);
  CHECK(a.alive && said(a, "Экран есть", t), "USB отключили — загрузчик запускает прошивку");
}

void sc_stuck_button() {
  say("\n== Залипшая кнопка при выключении на зарядке ==");
  Board& a = *boards[0];
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(3000);
  uint64_t t = g_ms;
  a.button = true;  // зажали, чтобы выключить, — и кнопка залипла
  a.cmd("SET button 1");
  run(30000);
  CHECK(a.alive && !happened(a, "off", t) && a.num("disp") == 0, "на USB выключился и ждёт, кнопка всё ещё зажата");
  t = g_ms;
  a.vbus = false;
  a.cmd("SET vbus 0");
  uint64_t off_at = 0;
  for (int i = 0; i < 300 && !off_at; ++i) {
    run(100);
    for (auto& e : a.events)
      if (e.first >= t && e.second == "off_vbus") off_at = e.first;
  }
  CHECK(off_at && !a.alive, "USB отключили — полный сон без кнопки (через %.1f с)",
        off_at ? (double)(off_at - t) / 1000.0 : -1.0);
  run(60000, 50);
  CHECK(!a.alive, "залипшая кнопка не будит и не держит включённым");
  a.button = false;
  run(1000);
  t = g_ms;
  a.vbus = true;
  run(100);
  a.cmd("SET vbus 1");
  run(5000);
  CHECK(a.alive && said(a, "Экран есть", t), "USB будит после сна без кнопки");
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
}

void sc_watchdog() {
  say("\n== Зависание и сторожевой таймер ==");
  Board& a = *boards[0];
  const long xp = a.num("xp");
  uint64_t t = g_ms;
  a.cmd("SET hang 1");
  run(12000);
  CHECK(happened(a, "watchdog", t), "зависание цикла — сброс сторожевым таймером за 8 с");
  run(4000);
  CHECK(a.alive && said(a, "Экран есть", t), "после сброса — снова работает");
  CHECK(!said(a, "время и дата сброшены", t), "часы пережили сброс сторожевым таймером");
  CHECK(a.num("xp") >= xp - 100, "рост на месте");
}

void sc_console_stuck() {
  say("\n== Консоль открыта, но не читается ==");
  Board& a = *boards[0];
  a.cmd("HOSTREAD 0");
  a.cmd("IN v on");
  const long loops0 = a.num("loops");
  const uint64_t t = g_ms;
  for (int i = 0; i < 20; ++i) {
    a.cmd("TAP 1");
    run(1000);
  }
  CHECK(!any_event(a, t) && a.num("loops") - loops0 > 1500, "цикл идёт (%ld проходов за 20 с), без сброса",
        a.num("loops") - loops0);
  a.cmd("HOSTREAD 1");
  a.cmd("IN v off");
  run(500);
}

// Серии нажатий: одно — «кнопка» для характера, два — дуэли вкл/выкл, три — звук вкл/выкл.
void sc_buttons() {
  say("\n== Нажатия кнопки: одно, два, три ==");
  Board& a = *boards[0];
  auto taps = [&](int n) {
    for (int i = 0; i < n; ++i) {
      press(a, 120);
      run(150);
    }
    run(1500);
  };
  uint64_t t = g_ms;
  taps(1);
  CHECK(!count_said(a, "Дуэли", t) && !count_said(a, "Звук", t), "одно нажатие ничего не переключает");
  t = g_ms;
  taps(2);
  CHECK(count_said(a, "Дуэли выключены", t) == 1 && !count_said(a, "Звук", t), "два нажатия — дуэли выключены");
  t = g_ms;
  taps(3);
  CHECK(count_said(a, "Звук выключен", t) == 1 && !count_said(a, "Дуэли", t), "три нажатия — звук выключен");
  t = g_ms;
  taps(3);
  CHECK(count_said(a, "Звук включён", t) == 1, "ещё три — звук включён");
  t = g_ms;
  taps(2);
  CHECK(count_said(a, "Дуэли включены", t) == 1, "ещё два — дуэли включены");
  // Нажатия с паузой больше 400 мс — по одному, без переключений.
  t = g_ms;
  for (int i = 0; i < 3; ++i) {
    press(a, 120);
    run(700);
  }
  run(1000);
  CHECK(!count_said(a, "Дуэли", t) && !count_said(a, "Звук", t), "редкие нажатия — по одному");
}

void sc_power_cuts() {
  say("\n== Обрывы питания в любой момент ==");
  Board& a = *boards[0];
  int ok_boots = 0, lost = 0;
  long saved = -1;  // опыт, который точно записан
  int torn = 0;  // обрывов, пришедшихся на идущую запись или стирание
  for (int i = 0; i < 30; ++i) {
    // Настройка с консоли пишется через 3 с после изменения и не раньше 10 с после прошлой
    // записи (и после включения) — вместе с ростом.
    a.cmd("IN theme %s", i % 2 ? "cyber" : "retro");
    const bool mid_write = i % 3 == 2;  // каждый третий обрыв — прямо во время записи
    const long ls0 = a.num("ls");
    if (mid_write) {
      // Ждём начала операции флеша и обрываем через 0–2 мс: запись ~5 мс, стирание 85 мс.
      wait_for([&] { return a.num("fop") == 1; }, 20000, 2);
      run(rnd() % 3, 1);
      torn += a.num("fop") == 1;
    } else {
      wait_for([&] { return a.num("ls") != ls0; }, 20000);
      saved = a.num("sxp");  // опыт, который точно записан
      run(rnd() % 400);
    }
    cut_power(a);
    a.button = true;
    run(50);
    a.button = false;
    a.cmd("SET button 0");
    const uint64_t t = g_ms;
    run(3500);
    if (a.alive && said(a, "Экран есть", t)) ++ok_boots;
    a.cmd("IN me");
    run(300);
    if (saved >= 0 && a.num("xp") < saved) {
      ++lost;
      say("        обрыв %d: опыт %ld, а записано было %ld", i, a.num("xp"), saved);
    }
  }
  CHECK(ok_boots == 30, "30 обрывов питания — плата каждый раз загружается (%d)", ok_boots);
  CHECK(torn >= 5, "обрывов посреди записи или стирания флеша: %d из 10", torn);
  CHECK(lost == 0, "записанный рост ни разу не потерян, и при обрыве посреди записи (%d)", lost);
}

void sc_millis_wrap() {
  say("\n== 48.5 суток без выключения (переполнение тиков ядра) ==");
  Board& w = *make_board(2, "W");
  w.x = 50;  // далеко от остальных
  // Тики FreeRTOS 32-битные: 2^32 тиков при 1024 Гц ≈ 4 194 304 000 мс. Старт за 90 с до них.
  w.t0_override = 4194304000ull - 90000ull;
  spawn(w, 0);
  run(5000);
  run(3000);
  w.cmd("IN v on");
  run(80000, 20);
  const uint64_t t = g_ms;
  for (int i = 0; i < 6; ++i) {
    w.cmd("TAP 1");
    run(2000);
  }
  run(60000, 20);
  CHECK(count_said(w, "Событие: tap", t) == 6, "после скачка millis() касания доходят (%d из 6)",
        count_said(w, "Событие: tap", t));
  // Сроки после 24.8 суток: «давний» срок не должен стать сроком в будущем.
  const uint64_t tr = g_ms;
  w.cmd("SET lisreset 1");
  const long fix = wait_for([&] { return said(w, "Акселерометр перезапущен", tr); }, 25000, 200);
  CHECK(fix >= 0, "после 48.5 суток сброс настроек акселерометра исправлен за %.1f с", fix / 1000.0);
  CHECK(!any_event(w, 0) || w.events.size() == 1, "без зависаний и сбросов");
  health(w, "после 48.5 суток");
  drop_board(&w);
}

void sc_empty_cell() {
  say("\n== Разряд до отсечки ==");
  Board& a = *boards[0];
  a.soc_mah = a.cap_mah * 0.025;
  const uint64_t t = g_ms;
  for (int i = 0; i < 16; ++i) {
    run(30000, 20);
    a.cmd("IN sensors");
    run(200);
    if (getenv("SIM_DEBUG")) say("        %.2f мА·ч, ячейка %.3f В", a.soc_mah, a.last_cell);
  }
  CHECK(happened(a, "off", t), "ячейка почти пуста — брелок сам выключился");
  CHECK(a.soc_mah > 0, "раньше, чем плата защиты отключила бы питание (осталось %.2f мА·ч)", a.soc_mah);
  CHECK(said(a, "почти пуста", t), "консоль сообщает причину");
  // Кнопка на пустой ячейке: секунда с пустой батареей на экране и снова сон, без загрузки.
  const double before = a.soc_mah;
  const uint64_t tp = g_ms;
  for (int i = 0; i < 3; ++i) {
    a.button = true;
    run(100);
    a.cmd("SET button 1");
    run(300);
    a.button = false;
    a.cmd("SET button 0");
    run(5000);
  }
  CHECK(!a.alive && !said(a, "Брелок с эмоциями", tp), "кнопка на пустой ячейке не загружает брелок целиком");
  CHECK(before - a.soc_mah < 0.05, "три нажатия стоили %.3f мА·ч", before - a.soc_mah);
  a.soc_mah = 120;
  const uint64_t tw = g_ms;
  a.vbus = true;
  run(5000);
  CHECK(a.alive && said(a, "Экран есть", tw), "подключили USB — проснулся");
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  // Зарядили — отметка о пустой ячейке снята: кнопка снова будит брелок целиком.
  CHECK(power_off(a), "выключили кнопкой");
  const uint64_t tf = g_ms;
  CHECK(wake(a) && said(a, "Брелок с эмоциями", tf), "после зарядки кнопка снова включает брелок целиком");
}

void sc_day() {
  say("\n== Расход за типичный день (ускоренно: по часу на режим) ==");
  Board& a = *boards[0];
  // День — с утра: на ночь брелок выключали, ячейка заряжена. Иначе после длинных сценариев
  // (сутки работы в days) бодрость на нуле, лицо сонное и приглушённое, эфир редкий, и расход
  // зависел бы от того, что шло перед этим сценарием.
  power_off(a);
  a.soc_mah = a.cap_mah * 0.8;
  wake(a);
  a.cmd("SET lux 300");
  a.cmd("SET mic -55 0");
  run(40000);  // датчик батареи устанавливается 30 с
  CHECK(a.num("con") == 127 && a.num("mic") == 1 && a.num("bpct") >= 60,
        "с утра: обычная яркость, микрофон слушает, заряд %ld%%", a.num("bpct"));
  struct Mode {
    const char* name;
    float lux;
    double hours_per_day;
    double ua = 0;
  } modes[] = {{"лицо на экране", 300, 3}, {"лицо в тусклом свете", 8, 1}, {"карман и ночь", 0.3, 20}};
  for (Mode& m : modes) {
    a.cmd("SET lux %.1f", m.lux);
    run(70000, 50);  // войти в режим
    const double q0 = a.uams_total;
    const uint64_t t0 = g_ms;
    for (int min = 0; min < 30; ++min) {
      if (m.lux > 100 && min % 5 == 0) a.cmd("TAP 1");
      if (m.lux > 100 && min % 10 == 3) a.cmd("SET walk 2 0.4");
      if (m.lux > 100 && min % 10 == 5) a.cmd("SET still");
      run(60000, 50);
    }
    m.ua = (a.uams_total - q0) / (double)(g_ms - t0);
    say("        %-22s %.3f мА", m.name, m.ua / 1000.0);
  }
  double mah = 0;
  for (Mode& m : modes) mah += m.ua / 1000.0 * m.hours_per_day;
  say("        за сутки: %.1f мА·ч → %.1f дня на 150 мА·ч (до отсечки 3.35 В — ~142 мА·ч)", mah, 142.0 / mah);
  CHECK(mah > 10 && mah < 16, "расход за обычный день %.1f мА·ч (в docs/power.md ≈12.9)", mah);
}

void sc_charge_temp() {
  say("\n== Заряд только в тепле: 10–42 °C по температуре кристалла ==");
  Board& a = *boards[0];
  a.cmd("IN v on");
  a.soc_mah = 60;
  a.cmd("SET temp -5");  // кристалл ≈ −2 °C
  run(2000);
  uint64_t t = g_ms;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(5000);
  CHECK(a.num("chgen") == 0 && !a.charging, "мороз −5 °C: USB есть, а зарядник выключен (CHG_EN отпущен)");
  CHECK(said(a, "Заряд не идёт", t) && said(a, "заряжается от 10 до 42 °C", t), "консоль объясняет: заряжается от 10 до 42 °C");
  const double soc0 = a.soc_mah;
  run(60000, 50);
  CHECK(a.soc_mah <= soc0 + 0.001 && !said(a, "Событие: charge_start", t),
        "минуту на морозе ячейка не заряжается (%.2f → %.2f мА·ч)", soc0, a.soc_mah);
  t = g_ms;
  a.cmd("SET temp 24");
  run(35000, 50);
  CHECK(a.num("chgen") == 1 && a.charging, "потеплело до 24 °C — заряд включился (проверка раз в 30 с)");
  CHECK(said(a, "Заряд снова разрешён", t) && said(a, "Событие: charge_start", t), "консоль и событие charge_start");
  CHECK(!said(a, "Событие: charge_full", t), "включение заряда не принято за «зарядилась»");
  t = g_ms;
  a.cmd("SET temp 50");  // кристалл ≈ 53 °C
  run(35000, 50);
  CHECK(a.num("chgen") == 0 && !a.charging && said(a, "Заряд остановлен", t), "жара 50 °C — заряд остановлен");
  CHECK(!said(a, "Событие: charge_full", t), "остановка по температуре — не «зарядилась»");
  t = g_ms;
  const double soc1 = a.soc_mah;
  a.cmd("SET temp 24");
  run(35000, 50);
  CHECK(a.num("chgen") == 1 && a.charging && a.soc_mah > soc1 && !said(a, "Событие: charge_full", t),
        "остыл — заряд продолжается без ложного «зарядилась»");
  // Пороги и запас от дребезга: температура кристалла задана прямо (без +3 °C модели), шаги по
  // 35 с — проверка раз в 30 с; шум датчика ±0.25 °C, до порога — 0.5 °C.
  a.cmd("SET dieoffset 0");
  struct Step {
    float die;
    int chgen;
  } const steps[] = {{7.5f, 1}, {6.5f, 0}, {9.5f, 0}, {10.5f, 1}, {43.5f, 1}, {45.5f, 0}, {43.5f, 0}, {41.5f, 1}};
  for (const Step& s : steps) {
    a.cmd("SET temp %.1f", s.die);
    run(35000, 50);
    CHECK(a.num("chgen") == s.chgen, "кристалл %.1f °C: заряд %s (chgen=%ld)", s.die, s.chgen ? "идёт" : "запрещён",
          a.num("chgen"));
  }
  CHECK(!said(a, "Событие: charge_full", t), "у порогов — ни одного ложного «зарядилась»");
  // Оценка заряда на USB стоит, пока заряд запрещён: перерыв — не зарядка.
  a.cmd("SET temp 24");
  run(6u * 60000u, 50);
  const long pct1 = a.num("bpct");
  a.cmd("SET temp 50");
  run(30u * 60000u, 100);
  a.cmd("SET temp 24");
  run(35000, 50);
  const long pct2 = a.num("bpct");
  CHECK(pct1 >= 0 && pct2 >= pct1 && pct2 - pct1 <= 3,
        "оценка заряда на USB: %ld%% до перерыва, %ld%% после 30 минут без заряда", pct1, pct2);
  a.cmd("SET dieoffset 3");
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  CHECK(a.num("chgen") == 0, "USB отключили — CHG_EN отпущен");
  CHECK(!any_event(a, t), "без сбросов");
  a.soc_mah = 120;
}

void sc_lossy_meeting() {
  say("\n== Встреча, когда теряется 40 %% пакетов ==");
  Board& p = *make_board(6, "P");
  p.x = 3000;  // далеко от остальных
  spawn(p, 0);
  Board& q = *make_board(7, "Q");
  q.x = 3001;
  spawn(q, 0);
  run(4000);
  p.cmd("IN v on");
  q.cmd("IN v on");
  air_loss = 0.4;
  const uint64_t t = g_ms;
  uint64_t tp = 0, tq = 0;
  auto scene = [&](Board& b, uint64_t* at) { return said(b, "Дуэль с", t, at) || said(b, "Встреча с", t, at); };
  wait_for([&] { return scene(p, &tp) && scene(q, &tq); }, 300000);
  CHECK(tp && tq, "сцена у обоих (через %.1f с)", tp ? (double)(tp - t) / 1000.0 : -1.0);
  CHECK(said(p, "Дуэль с", t) == said(q, "Дуэль с", t), "у обоих одна и та же сцена");
  CHECK((tp > tq ? tp - tq : tq - tp) <= 300, "начало почти одновременно: разница %lld мс", (long long)tp - (long long)tq);
  run(60000, 20);
  CHECK(p.num("fr") == 1 && q.num("fr") == 1, "каждый записал другого в знакомые");
  const int np = count_said(p, "Дуэль с", t) + count_said(p, "Встреча с", t);
  const int nq = count_said(q, "Дуэль с", t) + count_said(q, "Встреча с", t);
  CHECK(np == 1 && nq == 1, "сцена одна, без сцены только у одного (P %d, Q %d)", np, nq);
  health(p, "встреча с потерями P");
  health(q, "встреча с потерями Q");
  air_loss = 0;
  drop_board(&q);
  drop_board(&p);
}

void sc_i2c() {
  say("\n== Шина I2C: датчики пропадают, зажимают шину и возвращаются ==");
  Board& a = *boards[0];
  a.cmd("SET lux 300");
  a.cmd("IN v on");
  run(1000);
  // 1) LIS3DH перестал отвечать посреди работы и вернулся (после сброса по питанию).
  uint64_t t = g_ms;
  a.cmd("SET lis 0");
  run(25000);
  CHECK(a.alive && !any_event(a, t) && a.num("accok") == 0, "акселерометр не отвечает — брелок работает дальше");
  t = g_ms;
  a.cmd("SET lis 1");
  long w = wait_for([&] { return said(a, "Акселерометр перезапущен", t); }, 90000, 500);
  CHECK(w >= 0 && a.num("accok") == 1, "вернулся — перезапущен через %.1f с", w / 1000.0);
  t = g_ms;
  a.cmd("TAP 1");
  run(1500);
  CHECK(said(a, "Событие: tap", t), "касания снова доходят");
  // 2) Сброс LIS3DH по питанию: отвечает, но настройки стёрты — FIFO молчит.
  t = g_ms;
  a.cmd("SET lisreset 1");
  w = wait_for([&] { return said(a, "Акселерометр перезапущен", t); }, 15000, 200);
  CHECK(w >= 0, "сброс настроек датчика замечен и исправлен за %.1f с", w / 1000.0);
  const long steps0 = a.num("steps");
  a.cmd("SET walk 2 0.4");
  run(21000);
  a.cmd("SET still");
  run(12000);
  CHECK(a.num("steps") - steps0 >= 30, "шаги снова считаются (%ld за 20 с)", a.num("steps") - steps0);
  // 3) LTR-303 пропал и вернулся.
  t = g_ms;
  a.cmd("SET ltr 0");
  run(20000);
  CHECK(a.num("alsok") == 0 && !any_event(a, t), "датчик света не отвечает — брелок работает дальше");
  t = g_ms;
  a.cmd("SET ltr 1");
  w = wait_for([&] { return said(a, "Датчик света перезапущен", t); }, 90000, 500);
  CHECK(w >= 0 && a.num("alsok") == 1, "вернулся — перезапущен через %.1f с", w / 1000.0);
  t = g_ms;
  a.cmd("SET lux 0.3");
  run(12000);
  CHECK(said(a, "Событие: dark", t), "свет снова меряется: темно — dark");
  a.cmd("SET lux 300");
  run(8000);
  // 4) Ведомый зажал SDA: такты i2c_recover (при выключенном TWIM) его отпускают.
  t = g_ms;
  a.cmd("SET sda 5");
  w = wait_for([&] { return said(a, "Акселерометр перезапущен", t); }, 25000, 200);
  CHECK(w >= 0 && !said(a, "Шина датчиков зажата", t), "SDA зажата — отпущена тактами, датчики перезапущены за %.1f с",
        w / 1000.0);
  CHECK(!any_event(a, t), "без сбросов");
  // 5) SCL в нуле: обмен в ядре ждёт без срока, сбрасывает сторожевой таймер. После сброса
  // шина не трогается: лицо на экране и без перезагрузок по кругу.
  t = g_ms;
  a.cmd("SET scl 1");
  run(25000);
  CHECK(happened(a, "watchdog", t), "SCL в нуле: обмен завис — сброс сторожевым таймером");
  CHECK(a.alive && said(a, "Шина датчиков зажата", t) && a.num("vis") == 1 && a.num("sbus") == 0,
        "после сброса — лицо на экране, зажатая шина не трогается");
  const uint64_t t2 = g_ms;
  run(40000);
  CHECK(a.alive && !any_event(a, t2), "без перезагрузок по кругу");
  t = g_ms;
  a.cmd("SET scl 0");
  w = wait_for([&] { return said(a, "Акселерометр перезапущен", t) && said(a, "Датчик света перезапущен", t); },
               330000, 1000);
  CHECK(w >= 0 && a.num("sbus") == 1, "шину отпустили — оба датчика снова работают (через %.0f с)", w / 1000.0);
  // 6) Датчиков нет при включении: брелок с лицом, датчики подхватываются, когда появятся.
  a.cmd("SET lis 0");
  a.cmd("SET ltr 0");
  cut_power(a);
  t = g_ms;
  a.button = true;
  run(50);
  a.button = false;
  a.cmd("SET button 0");
  run(3500);
  CHECK(a.alive && said(a, "Экран есть, акселерометр нет, свет нет", t), "включился без датчиков — с лицом");
  t = g_ms;
  a.cmd("SET lis 1");
  a.cmd("SET ltr 1");
  w = wait_for([&] { return said(a, "Акселерометр перезапущен", t) && said(a, "Датчик света перезапущен", t); },
               330000, 1000);
  CHECK(w >= 0, "датчики появились — подхвачены через %.0f с", w / 1000.0);
  health(a, "после неисправностей шины");
}

void sc_oled() {
  say("\n== Экран завис или пропал ==");
  Board& a = *boards[0];
  a.cmd("SET lux 300");
  run(3000);
  // 1) Контроллер завис: не отвечает, пока его не обесточат. Питание за ключом спадает
  // медленно — сбросит его только выключение дольше, чем нужно на спад ниже 1 В.
  uint64_t t = g_ms;
  const long por0 = a.num("por");
  a.cmd("SET wedge 1");
  long w = wait_for([&] { return a.num("oled") == 1 && a.num("por") > por0 && a.num("vis") == 1; }, 15000);
  CHECK(w >= 0, "контроллер завис — прошивка обесточила экран, лицо снова на экране через %.1f с", w / 1000.0);
  CHECK(a.dnum("ovcc") >= 0 && a.dnum("ovcc") < 1.0, "питание экрана при перезапуске опустилось до %.2f В (сброс — ниже 1 В)",
        a.dnum("ovcc"));
  CHECK(!any_event(a, t), "без сбросов платы");
  run(2000);
  health(a, "экран после зависания");
  // 2) Экран пропал совсем (оторвался шлейф): брелок работает, повторы всё реже.
  t = g_ms;
  a.cmd("SET oled 0");
  run(120000, 50);
  CHECK(a.alive && !any_event(a, t) && a.num("oled") == 0, "экрана нет 2 минуты — брелок работает без сбросов");
  t = g_ms;
  a.cmd("SET oled 1");
  w = wait_for([&] { return a.num("oled") == 1 && a.num("vis") == 1; }, 70000);
  CHECK(w >= 0, "экран вернулся — лицо через %.0f с (повторы не реже раза в минуту)", w / 1000.0);
  run(2000);
  health(a, "экран вернулся");
  // 3) Контроллер после включения держит SDA: «подтверждение» читается нулём, обмен будто бы
  // идёт, а экран тёмный. Шина не запускается, и следующий перезапуск питания его сбрасывает.
  t = g_ms;
  const long por1 = a.num("por");
  a.cmd("SET osda 1");
  a.cmd("SET wedge 1");  // перезапуск питания из-за зависания — и при включении SDA зажата
  w = wait_for([&] { return a.num("oled") == 1 && a.num("por") >= por1 + 2 && a.num("vis") == 1; }, 25000);
  CHECK(w >= 0 && said(a, "Шина экрана зажата", t),
        "контроллер зажал SDA — шина не запущена, после нового перезапуска питания лицо через %.1f с", w / 1000.0);
  CHECK(!any_event(a, t), "без сбросов платы");
  run(2000);
  health(a, "экран после зажатой SDA");
  // 4) Контроллер зажал SDA посреди работы: обмен по-прежнему «проходит», но линия в нуле и между
  // обменами — прошивка видит это сама и перезапускает питание экрана.
  t = g_ms;
  const long por2 = a.num("por");
  a.cmd("SET osda 2");
  const long seen = wait_for([&] { return said(a, "Шина экрана зажата посреди работы", t); }, 10000);
  CHECK(seen >= 0 && seen <= 2500, "контроллер зажал SDA посреди работы — прошивка заметила через %.1f с", seen / 1000.0);
  w = wait_for([&] { return a.num("oled") == 1 && a.num("por") > por2 && a.num("vis") == 1; }, 15000);
  CHECK(w >= 0, "питание экрана перезапущено — лицо снова на экране ещё через %.1f с", w / 1000.0);
  CHECK(!any_event(a, t), "без сбросов платы");
  run(2000);
  health(a, "экран после SDA, зажатой посреди работы");
}

// Шина экрана зажата неисправностью платы: SCL в нуле или SCL замкнута с SDA (перемычка на J2).
// Обмен по ней в ядре ждёт без срока — один сброс сторожевым таймером, дальше брелок работает
// без экрана: консоль, зарядка и выключение кнопкой.
void sc_display_bus() {
  say("\n== Шина экрана зажата: SCL в нуле или замкнута с SDA ==");
  Board& a = *boards[0];
  a.cmd("SET lux 300");
  run(2000);
  for (const char* fault : {"dscl", "dbridge"}) {
    uint64_t t = g_ms;
    a.cmd("SET %s 1", fault);
    run(60000);
    const int dogs = count_events(a, "watchdog", t);
    CHECK(dogs <= 1 && a.alive, "%s: сбросов сторожевым таймером за минуту — %d (не по кругу)", fault, dogs);
    CHECK(said(a, "Брелок с эмоциями", t) && said(a, "Шина экрана зажата", t) && a.num("oled") == 0 && a.num("disp") == 0,
          "%s: загрузка дошла до консоли, экран обесточен и не трогается", fault);
    t = g_ms;
    a.cmd("IN me");
    run(500);
    CHECK(said(a, "Рост: день", t), "%s: консоль отвечает", fault);
    a.vbus = true;
    a.cmd("SET vbus 1");
    run(5000);
    CHECK(a.num("chgen") == 1, "%s: на USB при 24 °C заряд разрешён", fault);
    a.vbus = false;
    a.cmd("SET vbus 0");
    run(3000);
    CHECK(power_off(a), "%s: удержание кнопки выключает", fault);
    CHECK(wake(a) && said(a, "Экран нет", t), "%s: кнопка будит, загрузка без экрана", fault);
    t = g_ms;
    a.cmd("SET %s 0", fault);
    const long w = wait_for([&] { return a.num("oled") == 1 && a.num("vis") == 1; }, 70000);
    CHECK(w >= 0 && !any_event(a, t), "%s: шину отпустили — лицо снова на экране через %.0f с", fault, w / 1000.0);
  }
  health(a, "шина экрана");
}

// Часы задают только с консоли, и после выключения кнопкой они потеряны. По старым минутам
// «ночь» шла бы днём: тусклый экран, эфир не слушается, лицо засыпает.
void sc_clock_unknown() {
  say("\n== Часы не заданы: ночи по старым минутам нет ==");
  Board& a = *boards[0];
  const double x0 = a.x;
  a.x = 5000;  // подальше от соседей: встреча сменила бы темп эфира
  a.cmd("SET lux 300");
  a.cmd("IN t 23:30");
  run(3000);
  CHECK(a.num("con") < 127 && a.num("iv") == 16000, "часы заданы, 23:30 — ночь: экран приглушён (%ld), эфир редкий",
        a.num("con"));
  CHECK(power_off(a), "выключили кнопкой в 23:3x");
  run(5000);
  uint64_t t = g_ms;
  CHECK(wake(a), "включили");
  run(10000);
  a.cmd("IN t");
  run(300);
  CHECK(said(a, "Время не задано", t), "консоль: время не задано");
  long scan0 = a.num("scanms");
  run(120000, 20);
  CHECK(a.num("con") == 127 && a.num("iv") != 16000 && a.num("mic") == 1 && a.num("scanms") > scan0,
        "часы потеряны: днём обычная яркость (%ld), эфир слушается (%ld мс за 2 мин), микрофон включён", a.num("con"),
        a.num("scanms") - scan0);
  // Новая плата, часы не задавали ни разу, проработала 11.5 ч (в кармане — так считается
  // быстрее): «12:00 + время работы» — 23:30. Достали на свет — день как день.
  Board& n = *make_board(8, "N");
  n.x = 4000;  // далеко от остальных
  n.cmd("SET lux 0.3");
  a.cmd("SET lux 0.3");  // A эти часы тоже в кармане: спит, а не устаёт к следующим сценариям
  spawn(n, 0);
  for (const uint64_t end = g_ms + (uint64_t)(11.5 * 3600000.0); g_ms < end;) run(1000, 1000);
  n.cmd("SET lux 300");
  a.cmd("SET lux 300");
  run(15000);
  scan0 = n.num("scanms");
  run(120000, 20);
  CHECK(n.alive && n.num("con") == 127 && n.num("iv") != 16000 && n.num("mic") == 1 && n.num("scanms") > scan0,
        "11.5 ч работы без часов — не ночь: яркость %ld, эфир слушается", n.num("con"));
  health(n, "без часов");
  drop_board(&n);
  a.x = x0;
}

void sc_dfu() {
  say("\n== Загрузчик: когда включается, а когда нет ==");
  Board& a = *boards[0];
  // 1) USB к работающему брелоку без кнопки — обычная зарядка.
  uint64_t t = g_ms;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(3000);
  CHECK(a.alive && !happened(a, "dfu", t), "USB без кнопки — зарядка, не загрузчик");
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  // 2) Выключили кнопкой, USB к спящему: просыпается прошивка, загрузчика нет.
  CHECK(power_off(a), "выключили кнопкой");
  t = g_ms;
  a.vbus = true;
  run(4000);
  CHECK(a.alive && said(a, "Экран есть", t) && !a.boot_mode, "USB будит спящий брелок: прошивка, не загрузчик");
  run(15000);
  CHECK(a.alive && !happened(a, "dfu", t), "загрузились уже на USB без кнопки — загрузчик не включается");
  // 3) Выключили на USB и держат кнопку дольше 10 с: сброс с зажатой кнопкой — загрузчик.
  press(a, 3500);
  run(20000);
  CHECK(a.alive && a.num("disp") == 0, "выключился на USB");
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(12500);
  CHECK(happened(a, "boot_button", t) && a.boot_mode == 2, "кнопку держат 10 с на USB — загрузчик ждёт прошивку");
  a.button = false;
  run(5000);
  CHECK(a.boot_mode == 2 && !a.alive, "компьютер видит диск: загрузчик ждёт, прошивка не идёт");
  t = g_ms;
  a.vbus = false;
  wait_for([&] { return a.alive && said(a, "Экран есть", t); }, 5000);
  CHECK(a.alive && said(a, "Экран есть", t), "USB отключили — загрузчик запускает прошивку");
  // 4) Зарядка от розетки: тот же жест, но компьютера нет — через 10 с прошивка сама.
  a.host = false;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(3000);
  press(a, 3500);
  run(20000);
  t = g_ms;
  a.button = true;
  a.cmd("SET button 1");
  run(12500);
  a.button = false;
  CHECK(a.boot_mode == 2, "кнопку держат 10 с на зарядке — загрузчик");
  t = g_ms;
  wait_for([&] { return a.alive && said(a, "Экран есть", t); }, 15000);
  // 10 с — правило модели загрузчика (BUTTON_DFU_TIMEOUT_MS), проверяется здесь только то, что
  // прошивка после него стартует и работает.
  CHECK(a.alive && !a.boot_mode && said(a, "Экран есть", t), "компьютера нет — загрузчик сам запускает прошивку");
  a.host = true;
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  // 5) Кнопка зажата давно (залипла или ею будили и не отпускают): USB загрузчик не включает.
  CHECK(power_off(a), "выключили кнопкой");
  t = g_ms;
  a.button = true;
  run(100);
  a.cmd("SET button 1");
  run(12000);
  CHECK(a.alive && !happened(a, "off", t), "разбудили кнопкой и держат 12 с — брелок работает");
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(3000);
  CHECK(a.alive && !happened(a, "dfu", t), "USB при кнопке, зажатой дольше 10 с, — не загрузчик");
  press(a, 100);  // отпустили
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
  // 6) Команда dfu: загрузчик, после него часы на месте.
  a.vbus = true;
  a.cmd("SET vbus 1");
  a.cmd("IN t 09:30");
  run(2000);
  t = g_ms;
  a.cmd("IN dfu");
  run(2000);
  CHECK(happened(a, "dfu", t) && a.boot_mode == 1, "команда dfu — загрузчик");
  t = g_ms;
  a.vbus = false;
  wait_for([&] { return a.alive && said(a, "Экран есть", t); }, 5000);
  a.cmd("IN t");
  run(300);
  CHECK(a.alive && said(a, "Время 09:3", t) && !said(a, "не задано", t), "после загрузчика прошивка с теми же часами");
}

void sc_days() {
  say("\n== Сутки без часов: счёт работы переживает выключение ==");
  Board& d = *make_board(5, "D");
  d.x = 2000;  // далеко от остальных
  d.cmd("SET lux 0.3");  // в кармане: экран выключен, считается быстрее
  spawn(d, 0);
  run(5000);
  const long day0 = d.num("days");
  double on_ms = 0;  // сколько плата проработала
  double new_day_on = -1;
  auto session = [&](double hours) {
    const uint64_t end = g_ms + (uint64_t)(hours * 3600000.0);
    while (g_ms < end) {
      run(1000, 1000);
      if (d.alive) on_ms += 1000;
      if (new_day_on < 0 && d.num("days") > day0) new_day_on = on_ms;
    }
  };
  session(8.5);
  CHECK(power_off(d), "8.5 ч работы — выключили кнопкой");
  run(3600000, 1000);
  CHECK(wake(d), "через час разбудили");
  session(8.5);
  CHECK(power_off(d), "ещё 8.5 ч — выключили");
  run(3600000, 1000);
  CHECK(wake(d), "разбудили");
  CHECK(d.num("days") == day0, "17 ч работы в двух включениях — те же сутки (день %ld)", d.num("days"));
  session(8.0);
  CHECK(d.num("days") == day0 + 1, "за 25 ч работы с двумя выключениями — новые сутки (день %ld → %ld)", day0,
        d.num("days"));
  CHECK(new_day_on > 0 && fabs(new_day_on - 24 * 3600000.0) < 15 * 60000.0,
        "новые сутки — после %.2f ч работы (24 ч, выключения не обнуляют счёт)", new_day_on / 3600000.0);
  CHECK(!happened(d, "watchdog", 0) && !happened(d, "crash", 0), "без сбоев");
  drop_board(&d);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    fprintf(stderr, "keychain_sim <keychain_board> <папка> [сценарии]\n");
    return 2;
  }
  board_exe = argv[1];
  outdir = argv[2];
  mkdir(outdir.c_str(), 0755);
  signal(SIGPIPE, SIG_IGN);
  // Каждый сценарий можно запустить и отдельно: перед первым всегда идёт boot (плата A).
  std::vector<std::pair<const char*, void (*)()>> all = {
      {"boot", sc_first_boot},         {"sensors", sc_sensors},       {"sound", sc_sound},
      {"pocket", sc_light_and_pocket}, {"charge", sc_charge},         {"chargetemp", sc_charge_temp},
      {"meeting", sc_meeting},         {"lossy", sc_lossy_meeting},   {"attack", sc_attack},
      {"i2c", sc_i2c},                 {"oled", sc_oled},             {"dbus", sc_display_bus},
      {"clock", sc_clock_unknown},     {"off", sc_power_off_battery},
      {"dfu", sc_dfu},                 {"offusb", sc_power_off_usb},  {"stuck", sc_stuck_button},
      {"watchdog", sc_watchdog},       {"console", sc_console_stuck}, {"buttons", sc_buttons},
      {"cuts", sc_power_cuts},
      {"empty", sc_empty_cell},        {"wrap", sc_millis_wrap},      {"days", sc_days},
      {"day", sc_day},
  };
  auto wanted = [&](const char* n) {
    if (argc == 3) return true;
    for (int i = 3; i < argc; ++i)
      if (!strcmp(argv[i], n)) return true;
    return false;
  };
  for (auto& [n, f] : all) {
    if (strcmp(n, "boot") && boards.empty()) sc_first_boot();
    if (wanted(n) && !(strcmp(n, "boot") == 0 && !boards.empty())) f();
  }
  drop_boards();
  say("\nИтог: %d проверок, %d не прошли", checks, failures);
  FILE* f = fopen((outdir + "/report.txt").c_str(), "w");
  if (f) {
    for (auto& l : report_lines) fprintf(f, "%s\n", l.c_str());
    fclose(f);
  }
  return failures ? 1 : 0;
}
