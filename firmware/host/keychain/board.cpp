// Одна плата rev B с настоящей прошивкой: процесс, которым управляет координатор
// (keychain_sim.cpp) по строкам на stdin/stdout.
//
//   SET <поле> <значения>   мир вокруг платы (ускорение, свет, звук, USB, кнопка…) и
//                           неисправности: lis|ltr|oled 0|1 — микросхема не отвечает и снова
//                           отвечает (после сброса по питанию), lisreset — сброс LIS3DH,
//                           wedge — контроллер экрана завис, sda <такты> и scl 0|1 — шина
//                           датчиков зажата
//   TAP 1|2                 касание или двойное касание (детектор LIS3DH)
//   RX <rssi> <hex>         пакет из эфира — Bluefruit отдаст его прошивке, если слушает
//   IN <текст>              строка в консоль USB
//   HOSTREAD 0|1            читает ли хост консоль (0 — порт открыт, но не читается)
//   STEP <мс>               гонять loop() до этого времени с последнего сброса
//   DUMP                    память экрана: FB <1024 байта hex> и что сейчас видно
//
// Ответ на STEP — строки OUT (консоль), EV (сброс, выключение, загрузчик), ST (состояние)
// и OK. Флеш журнала — файл, отображённый на 0xEA000, как на плате: он переживает
// перезапуск процесса и даже его убийство посреди записи.
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/keychain/main.cpp"
#include "bluefruit.h"
#include "oled_model.h"

namespace sim {
bool lis_active();
bool ltr_active();
uint32_t lis_overruns();
uint32_t ltr_measurements();
void world_changing();
}  // namespace sim

namespace {

const char* uicr_path = nullptr;
uint32_t mismatches = 0, dark_errors = 0;
double charge_uams = 0;
uint64_t last_acct_us = 0;
double last_cpu_us = 0;
uint32_t last_frame_hash = 0;
uint32_t frame_changes = 0;
double scan_ms = 0, adv_events = 0;

void map_flash(const char* path) {
  const int fd = open(path, O_RDWR | O_CREAT, 0644);
  if (fd < 0) _exit(20);
  struct stat st;
  fstat(fd, &st);
  if (st.st_size < 3 * 4096) {
    static uint8_t ff[3 * 4096];
    memset(ff, 0xFF, sizeof ff);
    if (pwrite(fd, ff, sizeof ff, 0) != (ssize_t)sizeof ff) _exit(21);
  }
  void* p = mmap((void*)0xEA000, 3 * 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
  if (p != (void*)0xEA000) {
    fprintf(stderr, "не удалось отобразить флеш на 0xEA000\n");
    _exit(22);
  }
}

void save_uicr() {
  if (!uicr_path) return;
  FILE* f = fopen(uicr_path, "wb");
  if (!f) return;
  fwrite(&sim::status.uicr_regout0, 4, 1, f);
  fclose(f);
}

void load_uicr() {
  FILE* f = fopen(uicr_path, "rb");
  if (!f) return;
  if (fread(&sim::status.uicr_regout0, 4, 1, f) != 1) sim::status.uicr_regout0 = 0xFFFFFFFFu;
  fclose(f);
}

uint32_t frame_hash() {
  uint32_t h = 2166136261u;
  const uint8_t* d = eng.frame().data;
  for (int i = 0; i < kPages * kWidth; ++i) h = (h ^ d[i]) * 16777619u;
  return h;
}

// Ток сейчас, мкА, по состоянию микросхем (типовые значения даташитов; docs/power.md).
double state_ua() {
  double ua = 3 + 1 + 10;  // сон nRF с RAM, REG0, зарядник и плата защиты ячейки
  if (sim::oled_powered()) {
    ua += 67;  // делитель и затвор ключа экрана
    if (sim::oled_dev.visible())
      ua += 500 + 24000.0 * sim::oled_dev.lit() / 8192.0 * sim::oled_dev.contrast / 255.0;
    else
      ua += 5;
  }
  ua += sim::lis_active() ? 20 : 0.5;
  ua += sim::ltr_active() ? 220 : 5;
  if (sim::status.pdm_running) ua += 800;
  if (sim::status.hfclk_requested) ua += 250;
  if (Bluefruit.Scanner.isRunning()) ua += 5500;
  if (sim::status.tone_hz) ua += 4000;
  // Мотор: ~60 мА на 3 В, ток — по среднему напряжению на нём (скважность × VSYS).
  if (sim::status.vib_duty > 0) ua += 60000.0 * sim::status.vib_duty * (sim::world.vbus ? 4.7 : sim::world.cell_v) / 3.0;
  return ua;
}

void account() {
  const double dt_ms = (double)(sim::now_us - last_acct_us) / 1000.0;
  if (dt_ms <= 0) return;
  charge_uams += state_ua() * dt_ms;
  if (Bluefruit.Scanner.isRunning()) scan_ms += dt_ms;
  if (Bluefruit.Advertising.running && Bluefruit.Advertising.interval) {
    const double ev = dt_ms / (Bluefruit.Advertising.interval * 0.625);
    adv_events += ev;
    charge_uams += ev * 9000.0;  // ~9 мкКл на рекламу на трёх каналах при 0 дБм
  }
  const double cpu = sim::power.cpu_us - last_cpu_us;
  last_cpu_us = sim::power.cpu_us;
  charge_uams += cpu / 1000.0 * 3300.0;
  last_acct_us = sim::now_us;
}

// Экран показывает ровно кадр движка? Сверка после каждого прохода цикла.
void check_display() {
  if (!(screen_on && have_oled && !oled_full)) return;
  if (!sim::oled_dev.visible()) {
    ++dark_errors;
    return;
  }
  for (int p = 0; p < kPages; ++p)
    if (memcmp(sim::oled_dev.ram[p], &eng.frame().data[p * kWidth], kWidth) != 0) {
      ++mismatches;
      return;
    }
}

void flush_serial() {
  size_t pos = 0;
  std::string& o = Serial.out;
  while (true) {
    const size_t nl = o.find('\n', pos);
    if (nl == std::string::npos) break;
    printf("OUT %.*s\n", (int)(nl - pos), o.c_str() + pos);
    pos = nl + 1;
  }
  o.erase(0, pos);
}

void report() {
  flush_serial();
  const auto& adv = Bluefruit.Advertising;
  std::string hex;
  char b[4];
  for (uint8_t c : adv.data) snprintf(b, sizeof b, "%02x", c), hex += b;
  // chgen — нижний конец R3 (P0.23) в нуле: зарядник включён. sxp — опыт в последней записи
  // журнала, fop — операция флеша идёт, oled/accok/alsok/sbus — прошивка считает экран,
  // акселерометр, датчик света и шину датчиков рабочими; ls — время последней удачной записи,
  // dm — минуты текущих суток в настройках; ovcc — минимум питания экрана перед
  // последним включением, por — сколько раз контроллер экрана сбрасывался по питанию.
  printf("ST t=%u adv=%d iv=%u scan=%d disp=%d vis=%d con=%u lit=%d tone=%u vib=%d mic=%d als=%d acc=%d hf=%d "
         "vibv=%.2f chgen=%d q=%.0f loops=%u mism=%u dark=%u i2c=%u ffail=%u ovr=%u frames=%u xp=%u sxp=%u fr=%u "
         "steps=%u days=%u dm=%u ls=%u fop=%d oled=%d accok=%d alsok=%d sbus=%d ovcc=%.2f por=%u scanms=%.0f adv_ev=%.1f "
         "uicr=%x data=%s\n",
         sim::now_ms(), adv.running ? 1 : 0, adv.interval, Bluefruit.Scanner.isRunning() ? 1 : 0,
         sim::oled_powered() ? 1 : 0, sim::oled_dev.visible() ? 1 : 0, sim::oled_dev.contrast, sim::oled_dev.lit(),
         sim::status.tone_hz, NRF_PWM2->ENABLE.v ? 1 : 0, sim::status.pdm_running ? 1 : 0, sim::ltr_active() ? 1 : 0,
         sim::lis_active() ? 1 : 0, sim::status.hfclk_requested ? 1 : 0, sim::status.vib_peak_v,
         sim::pin_mode[23] == sim::kOutput && !sim::pin_out[23] ? 1 : 0, charge_uams, sim::status.loops, mismatches,
         dark_errors, sim::status.i2c_errors, sim::flash_failure_count(), sim::lis_overruns(), frame_changes,
         (unsigned)progress.xp, (unsigned)progress_saved.xp, (unsigned)friends.count(), (unsigned)progress.steps,
         (unsigned)progress.days, (unsigned)cfg.day_min, (unsigned)last_save, sim::flash_busy() ? 1 : 0, have_oled ? 1 : 0, acc.ok() ? 1 : 0,
         als.ok() ? 1 : 0, sensor_bus ? 1 : 0, sim::oled_dev.last_off_min, sim::oled_dev.resets, scan_ms, adv_events,
         sim::status.uicr_regout0, hex.empty() ? "-" : hex.c_str());
  charge_uams = 0;
  sim::status.vib_peak_v = sim::status.vib_duty * (sim::world.vbus ? 4.7f : sim::world.cell_v);
  save_uicr();
}

void on_alarm(int) {
  const char msg[] = "EV hang\nOK\n";
  if (write(1, msg, sizeof msg - 1) < 0) {
  }
  _exit(15);
}

std::vector<std::pair<int, std::vector<uint8_t>>> pending_rx;
bool hang = false;  // цикл «завис»: время идёт, сторожевой таймер никто не кормит
uint32_t step_target = 0;
bool serving = false;

int hexval(char c);
void serve();

// Время дошло до шага координатора: отчёт и следующие команды. Зовётся изнутри delay() и
// любого другого ожидания, поэтому длинные циклы прошивки тоже отдают управление.
void on_time() {
  if (serving || sim::now_ms() < step_target) return;
  account();
  alarm(0);
  report();
  printf("OK\n");
  fflush(stdout);
  serve();
}

int hexval(char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; }

void serve() {
  serving = true;
  char line[4096];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    char* arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    if (!strcmp(line, "SET") && arg) {
      sim::world_changing();
      char key[32];
      float a = 0, b = 0, c = 0;
      const int n = sscanf(arg, "%31s %f %f %f", key, &a, &b, &c);
      auto& w = sim::world;
      if (!strcmp(key, "acc") && n == 4) w.ax = a, w.ay = b, w.az = c, w.motion = 0;
      if (!strcmp(key, "walk")) w.motion = 1, w.motion_hz = a, w.motion_amp = b;
      if (!strcmp(key, "shake")) w.motion = 2, w.motion_hz = a, w.motion_amp = b;
      if (!strcmp(key, "still")) w.motion = 0;
      if (!strcmp(key, "lux")) w.lux = a;
      if (!strcmp(key, "mic")) w.mic_db = a, w.mic_pattern = (int)b;
      if (!strcmp(key, "temp")) w.temp_c = a;
      if (!strcmp(key, "vbus")) {
        if (w.vbus && a == 0) w.vbus_off_us = sim::now_us;
        w.vbus = a != 0;
      }
      if (!strcmp(key, "chg")) w.charging = a != 0;
      if (!strcmp(key, "cell")) w.cell_v = a;
      if (!strcmp(key, "button")) w.button = a != 0;
      if (!strcmp(key, "usbhost")) Serial.connected = a != 0;
      if (!strcmp(key, "hang")) hang = a != 0;
      // Микросхема пропала и вернулась: вернулась она после сброса по питанию.
      if (!strcmp(key, "lis")) {
        if (!w.lis_ok && a != 0) sim::lis_glitch();
        w.lis_ok = a != 0;
      }
      if (!strcmp(key, "ltr")) {
        if (!w.ltr_ok && a != 0) sim::ltr_glitch();
        w.ltr_ok = a != 0;
      }
      if (!strcmp(key, "oled")) w.oled_ok = a != 0;
      if (!strcmp(key, "lisreset")) sim::lis_glitch();
      if (!strcmp(key, "wedge")) sim::oled_wedge();
      if (!strcmp(key, "sda")) w.sda_stuck = (int)a;
      if (!strcmp(key, "scl")) w.scl_stuck = a != 0;
    } else if (!strcmp(line, "TAP") && arg) {
      sim::world.tap = atoi(arg);
    } else if (!strcmp(line, "HOSTREAD") && arg) {
      Serial.host_reads = atoi(arg) != 0;
      if (Serial.host_reads) Serial.fifo_used = 0;
    } else if (!strcmp(line, "IN") && arg) {
      Serial.in += arg;
      Serial.in += '\n';
    } else if (!strcmp(line, "RX") && arg) {
      char* sp = strchr(arg, ' ');
      if (sp && Bluefruit.started) {
        const int rssi = atoi(arg);
        uint8_t d[64];
        int n = 0;
        for (const char* h = sp + 1; h[0] && h[1] && n < 64; h += 2) d[n++] = (uint8_t)(hexval(h[0]) << 4 | hexval(h[1]));
        pending_rx.push_back({rssi, std::vector<uint8_t>(d, d + n)});
      }
    } else if (!strcmp(line, "STEP") && arg) {
      step_target = (uint32_t)strtoul(arg, nullptr, 10);
      alarm(60);
      break;
    } else if (!strcmp(line, "DUMP")) {
      printf("FB vis=%d con=%u ", sim::oled_dev.visible() ? 1 : 0, sim::oled_dev.contrast);
      for (auto& row : sim::oled_dev.ram)
        for (uint8_t c : row) printf("%02x", c);
      printf("\nOK\n");
      fflush(stdout);
    } else if (!strcmp(line, "QUIT")) {
      exit(0);
    }
  }
  if (feof(stdin)) exit(0);
  serving = false;
  // Пакеты из эфира — уже после разбора команд, как приходят от задачи BLE.
  for (auto& [rssi, d] : pending_rx) Bluefruit.Scanner.deliver(d.data(), (uint16_t)d.size(), (int8_t)rssi);
  pending_rx.clear();
}


}  // namespace

int main(int argc, char** argv) {
  int id = 0;
  const char* flash = "flash.bin";
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!strcmp(argv[i], "--id")) id = atoi(argv[i + 1]);
    if (!strcmp(argv[i], "--flash")) flash = argv[i + 1];
    if (!strcmp(argv[i], "--uicr")) uicr_path = argv[i + 1];
    if (!strcmp(argv[i], "--reset")) sim::world.reset_reason = (uint32_t)strtoul(argv[i + 1], nullptr, 0);
    if (!strcmp(argv[i], "--t0")) sim::now_us = strtoull(argv[i + 1], nullptr, 0) * 1000ull;
  }
  map_flash(flash);
  if (uicr_path) load_uicr();
  const uint64_t dev = 0x5EED000000000000ull ^ ((uint64_t)(id + 1) * 0x9E3779B97F4A7C15ull);
  sim::ficr.DEVICEID[0] = (uint32_t)dev, sim::ficr.DEVICEID[1] = (uint32_t)(dev >> 32);
  srand((unsigned)id * 7 + 1);
  signal(SIGALRM, on_alarm);
  setvbuf(stdout, nullptr, _IOFBF, 1 << 16);

  sim::time_hook = on_time;
  try {
    serve();
    last_acct_us = sim::now_us;
    setup();
    for (;;) {
      if (hang) sim::advance_us(10000000);
      loop();
      ++sim::status.loops;
      sim::power.cpu_us += 40;  // пробуждение и проход цикла
      const uint32_t h = frame_hash();
      if (h != last_frame_hash) {
        last_frame_hash = h;
        ++frame_changes;
        sim::power.cpu_us += 2500;  // отрисовка кадра на M4F 64 МГц (оценка)
      }
      check_display();
    }
  } catch (sim::Reset& r) {
    alarm(0);
    serving = true;
    account();
    const char* k = r.kind == sim::ResetKind::Soft        ? "reset"
                    : r.kind == sim::ResetKind::SystemOff ? "off"
                    : r.kind == sim::ResetKind::SystemOffUsb ? "off_vbus"
                    : r.kind == sim::ResetKind::Dfu       ? "dfu"
                                                          : "watchdog";
    printf("EV %s\n", k);
    report();
    printf("OK\n");
    fflush(stdout);
    msync((void*)0xEA000, 3 * 4096, MS_SYNC);
    return 10 + (int)r.kind;
  }
  return 0;
}
