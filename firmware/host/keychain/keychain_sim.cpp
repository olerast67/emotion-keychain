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
    spawn(b, kSreq);
  } else if (ev == "watchdog") {
    spawn(b, kDog);
  } else if (ev == "off") {
    b.off = true, b.button_wakes = true;
  } else if (ev == "off_vbus") {
    b.off = true, b.button_wakes = false;
  } else if (ev == "dfu") {
    b.off = true;  // загрузчик: ждёт новую прошивку
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

// Эфир: каждая реклама долетает до тех, кто слушает, с уровнем по расстоянию.
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
  if (b.vbus) {
    if (b.soc_mah >= b.cap_mah * 0.995) b.charging = false;
    else if (b.soc_mah < b.cap_mah * 0.96) b.charging = true;
    if (b.charging) b.soc_mah = fmin(b.cap_mah, b.soc_mah + 100.0 * h);
  } else {
    b.charging = false;
    b.soc_mah -= ua / 1000.0 * h;
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
    } else if ((b->off || b->dead) && b->pid < 0) {
      // Выключена: просыпается от кнопки или подключения USB.
      if (b->button && (b->dead || b->button_wakes)) {
        spawn(*b, b->dead ? 0 : kOff);
      } else if (b->vbus && b->dead) {
        spawn(*b, 0);
      } else if (b->vbus && !b->vbus_was && b->off) {
        spawn(*b, kVbus);  // подключили USB к спящей плате — nRF52840 просыпается
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

void drop_boards() {
  for (Board* b : boards) {
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
    delete b;
  }
  boards.clear();
}

void health(Board& b, const char* what) {
  CHECK(b.num("mism") == 0, "%s: экран показывает ровно кадр движка (расхождений %ld)", what, b.num("mism"));
  CHECK(b.num("dark") == 0, "%s: экран не гаснет, когда должен гореть (%ld)", what, b.num("dark"));
  CHECK(b.num("i2c") == 0, "%s: ошибок шины и запрещённых записей нет (%ld)", what, b.num("i2c"));
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
  CHECK(pocket_ua < 300, "ток в кармане %.0f мкА (оценка по коду ~150)", pocket_ua);
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
  rec.close();
  const double q1 = a.uams_total;
  const uint64_t t1 = g_ms;
  run(120000, 20);
  say("        ток с лицом на экране: %.2f мА", (a.uams_total - q1) / (double)(g_ms - t1) / 1000.0);
}

void sc_charge() {
  say("\n== Зарядка ==");
  Board& a = *boards[0];
  a.soc_mah = 60;
  uint64_t t = g_ms;
  a.vbus = true;
  a.cmd("SET vbus 1");
  run(15000);
  CHECK(said(a, "Событие: charge_start", t), "USB — начало зарядки");
  a.cmd("IN sensors");
  run(500);
  CHECK(said(a, "USB да, заряжается да", t), "консоль видит USB и зарядку");
  t = g_ms;
  a.soc_mah = a.cap_mah * 0.994;
  run(15000);
  CHECK(said(a, "Событие: charge_full", t), "зарядилась — charge_full");
  t = g_ms;
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(15000);
  CHECK(said(a, "Событие: charge_stop", t), "USB отключили — charge_stop");
  CHECK(!any_event(a, t), "без сбросов");
  a.soc_mah = 120;
}

void sc_meeting() {
  say("\n== Встреча двух брелоков ==");
  Board& a = *boards[0];
  Board& b = *make_board(1, "B");
  b.x = 1.0;  // метр от A
  spawn(b, 0);
  run(4000);
  a.cmd("IN v on");
  b.cmd("IN v on");
  Recorder rec;
  rec.open("meeting.gif", {&a, &b});
  const uint64_t t = g_ms;
  a.vib_max = b.vib_max = 0;
  uint64_t ta = 0, tb = 0;
  for (int i = 0; i < 1800 && !(said(a, "Дуэль с", t, &ta) && said(b, "Дуэль с", t, &tb)); ++i) {
    run(100);
    if (said(a, "Дуэль с", t) || said(b, "Дуэль с", t)) rec.frame(10);
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
  // 2) Объявление дуэли одним пакетом от «брелока» с позывным 1.
  t = g_ms;
  for (int i = 0; i < 200; ++i) {
    char h[128];
    snprintf(h, sizeof h, "02010410ffffff4b430201000000%02x%02x%02x01%s", 0, 0x09, 128, to_hex.c_str());
    a.cmd("RX -40 %s", h);
    run(50);
  }
  run(3000);
  CHECK(!said(a, "Дуэль с", t) && !said(a, "Встреча с", t), "объявление сцены от неслыханного брелока не принято");
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
  run(240000, 20);
  const Board& nb = *boards[1];
  (void)nb;
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
  CHECK(!happened(a, "dfu", t), "кнопка без USB не включает загрузчик");
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

void sc_power_cuts() {
  say("\n== Обрывы питания в любой момент ==");
  Board& a = *boards[0];
  int ok_boots = 0, lost = 0;
  long saved = -1;  // опыт, который точно записан
  for (int i = 0; i < 30; ++i) {
    // Настройка с консоли пишется сразу, вместе с ростом.
    a.cmd("IN theme %s", i % 2 ? "cyber" : "retro");
    const bool mid_write = i % 3 == 2;  // каждый третий обрыв — прямо во время записи
    if (mid_write) {
      run(10 + rnd() % 6);
    } else {
      run(600);
      saved = a.num("xp");
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
  CHECK(!any_event(w, 0) || w.events.size() == 1, "без зависаний и сбросов");
  health(w, "после 48.5 суток");
  fprintf(w.to, "QUIT\n");
  fflush(w.to);
  reap(w);
  boards.pop_back();
  delete &w;
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
  a.soc_mah = 120;
  const uint64_t tw = g_ms;
  a.vbus = true;
  run(5000);
  CHECK(a.alive && said(a, "Экран есть", tw), "подключили USB — проснулся");
  a.vbus = false;
  a.cmd("SET vbus 0");
  run(3000);
}

void sc_day() {
  say("\n== Расход за типичный день (ускоренно: по часу на режим) ==");
  Board& a = *boards[0];
  a.cmd("SET lux 300");
  a.cmd("SET mic -55 0");
  run(10000);
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
  CHECK(mah > 1 && mah < 25, "расход за обычный день %.1f мА·ч (оценка в docs/power.md ~14)", mah);
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
  std::vector<std::pair<const char*, void (*)()>> all = {
      {"boot", sc_first_boot},         {"sensors", sc_sensors},     {"sound", sc_sound},
      {"pocket", sc_light_and_pocket}, {"charge", sc_charge},       {"meeting", sc_meeting},
      {"attack", sc_attack},           {"off", sc_power_off_battery}, {"offusb", sc_power_off_usb},
      {"stuck", sc_stuck_button},
      {"watchdog", sc_watchdog},       {"console", sc_console_stuck}, {"cuts", sc_power_cuts},
      {"empty", sc_empty_cell},        {"wrap", sc_millis_wrap},      {"day", sc_day},
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
