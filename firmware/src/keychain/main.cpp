// Прошивка брелока на своей плате rev B (nRF52840, hardware/rev-b).
//
// Тот же движок, что на стенде и в симуляторах. Вокруг него — настоящие датчики:
//   акселерометр — касания, тряска, падение, шаги, наклон взгляда;
//   микрофон — хлопок, речь, музыка, тишина;
//   датчик света — темно, светло, солнце; температура чипа — холодно и жарко;
//   батарея и USB — заряд, экорежим, выключение на пустой ячейке;
//   радио — встречи, дуэли и разговор шифром с другими брелоками (docs/encounters.md).
//
// Кнопка: нажатие — «кнопка» для характера; двойное — включить или выключить дуэли;
// удержание 3 с — выключить (проснётся от кнопки или USB).
// Консоль USB на 115200 — `?` выводит список команд.
#include <Arduino.h>
#include <Wire.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board.h"
#include "face/behavior.h"
#include "face/clock.h"
#include "face/encounter.h"
#include "face/engine.h"
#include "face/friends.h"
#include "face/identity.h"
#include "face/progress.h"
#include "face/sensing.h"
#include "face/sound.h"
#include "hal.h"
#include "radio.h"
#include "sensors.h"
#include "ssd1306.h"
#include "storage.h"

using namespace face;

namespace {

constexpr uint64_t kDayMs = 24ull * 3600000ull;
constexpr uint32_t kDarkScreenOffMs = 60000;  // в кармане дольше минуты — экран гаснет
constexpr uint32_t kLongPressMs = 3000;

// Настройки, которые переживают перезагрузку.
struct Settings {
  uint32_t magic = 0x4B435354u;  // «KCST»
  uint8_t version = 1;
  uint8_t theme = 0;
  uint8_t sound = 1;
  uint8_t duel_ready = 1;
  uint8_t ghost = 0;
  uint8_t contrast = 0x7F;
  uint8_t bday_m = 0, bday_d = 0;
  uint16_t year = 0;  // 0 — дата не задана
  uint8_t month = 0, day = 0;
  uint16_t clock_min = 12 * 60;
  uint8_t clock_known = 0;
  uint8_t reserved[5] = {};  // до 24 байт без выравнивания: memcmp не видит мусора
};
static_assert(sizeof(Settings) == 24, "настройки без скрытых байтов выравнивания");

FaceEngine eng;
Behavior bh;
Voice voice;
Encounters enc;
Ssd1306 oled;
Lis3dh acc;
Ltr303 als;
MotionSensor motion;
SoundSensor hearing;
LightSensor light;
TempSensor thermal;
BatterySensor battery;

Identity me;
uint64_t chip_serial = 0;
Progress progress, progress_saved;
Friends friends, friends_saved;
Settings cfg, cfg_saved;
uint32_t last_save = 0, last_save_try = 0;
bool save_failed = false;

bool have_oled = false, oled_full = false, screen_on = true;
uint8_t shown_contrast = 0;
uint32_t oled_fail_at = 0, oled_retry_ms = 2000;
bool oled_waiting = false;  // экран не ответил: ждём повтора с oled_fail_at
bool dark_timing = false;  // экран в темноте: с dark_since
uint32_t dark_since = 0;

uint64_t uptime = 0;
uint64_t clock_base_up = 0;
uint64_t day_t0 = 0;
uint16_t last_clock = 0;

uint32_t next_acc = 0, next_light = 0, next_temp = 0, next_batt = 0, next_steps = 0;
bool als_measuring = false;  // датчик света включён ради одного замера
uint32_t als_started = 0;
bool powering_off = false;
bool tap_wait = false;  // одиночное касание ждёт, не окажется ли оно двойным
uint32_t tap_wait_at = 0;
bool off_scene_seen = false;  // сцена «спокойной ночи» уже пошла
bool off_timing = false;
uint32_t off_at = 0;
uint32_t charge_t0 = 0;
int charge_pct0 = -1;
bool charged_full = false;  // с этого подключения USB ячейка уже заряжалась до конца

bool verbose = false;
char line[64];
size_t line_n = 0;
bool line_overflow = false;

// Время работы в мс без скачков. millis() ядра — тики FreeRTOS (1024 Гц) × 1000 / 1024 в
// 32 битах: через 48.5 суток он прыгает на ноль не на границе 2^32, и все сроки сбились
// бы разом. Здесь тики копятся в 64 битах; движку уходят младшие 32 бита — они
// переполняются ровно на 2^32, как ему и нужно.
uint64_t uptime_now() {
  static uint32_t last = 0;
  static uint64_t ticks = 0;
  const uint32_t t = xTaskGetTickCount();
  ticks += (uint32_t)(t - last);
  last = t;
  return ticks * 1000u / configTICK_RATE_HZ;
}

// Консоль не должна останавливать брелок: запись в USB ждёт, пока хост читает, и с открытым,
// но не читаемым портом цикл встал бы до сброса сторожевым таймером. Ждём места не дольше
// 50 мс, остаток строки теряется. После такого ожидания следующие строки не ждут вовсе,
// пока хост снова не начнёт читать, — иначе каждая строка стоила бы циклу 50 мс.
bool out_stalled = false;

void out_raw(const char* s, size_t n) {
  const uint32_t t0 = millis();
  while (n) {
    const int room = Serial.availableForWrite();
    if (room > 0) {
      out_stalled = false;
      const size_t k = (size_t)room < n ? (size_t)room : n;
      Serial.write((const uint8_t*)s, k);
      s += k, n -= k;
    } else if (!Serial || out_stalled || millis() - t0 > 50) {
      out_stalled = Serial ? true : false;
      return;
    } else {
      delay(1);
    }
  }
}

void out(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void out(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n > 0) out_raw(buf, (size_t)n < sizeof buf ? (size_t)n : sizeof buf - 1);
}

void out_line(const char* s) {
  out_raw(s, strlen(s));
  out_raw("\n", 1);
}

unsigned clock_minutes() { return (unsigned)((cfg.clock_min + (uptime - clock_base_up) / 60000u) % 1440u); }

void screen_power(bool on, uint32_t now);

void event(Event e, uint32_t now) {
  if (verbose) out("Событие: %s\n", event_id(e));
  // Касание или кнопка в темноте показывают лицо ещё на минуту. Одиночное касание на ходу
  // не в счёт: шаги в кармане детектор тоже принимает за касания.
  const bool walking_tap = e == Event::tap && motion.walking();
  if ((e == Event::tap || e == Event::double_tap || e == Event::button) && !walking_tap) {
    if (dark_timing) dark_since = now;
    if (!screen_on) screen_power(true, now);
  }
  // Взяли в руки в темноте — свет меряем сразу: может, достали из кармана.
  if (e == Event::pick_up && !screen_on && !als_measuring) next_light = now;
  bh.on_event(e, now);
}

// ---------------------------------------------------------------- флеш

// Всё, что переживает выключение, — одной записью журнала (storage.h).
struct Snapshot {
  Progress progress;
  Friends friends;
  Settings settings;
};
static_assert(sizeof(Snapshot) <= storage::kMaxRecord, "запись журнала");

void save_all(uint32_t now, bool force) {
  // Флеш выдерживает ограниченное число стираний, а встречи может подстроить кто угодно
  // по радио: обычная запись — не чаще раза в 10 минут. Сразу — новые эмоции (их немного:
  // награды за встречи ограничены за день), настройки с консоли и по force.
  const bool p_changed = memcmp(&progress, &progress_saved, offsetof(Progress, crc)) != 0;
  const bool f_changed = memcmp(&friends, &friends_saved, offsetof(Friends, crc)) != 0;
  const bool s_changed = memcmp(&cfg, &cfg_saved, sizeof cfg) != 0;
  if (!p_changed && !f_changed && !s_changed) return;
  const bool unlocks = progress.implant != progress_saved.implant || progress.flags != progress_saved.flags ||
                       memcmp(progress.extra, progress_saved.extra, sizeof progress.extra) != 0;
  // Часы меняют настройки каждые 10 минут — это обычная запись, остальное в настройках — сразу.
  Settings a = cfg, b = cfg_saved;
  a.clock_min = b.clock_min = 0;
  const bool user_settings = memcmp(&a, &b, sizeof a) != 0;
  const bool urgent = force || unlocks || user_settings;
  if (!urgent && now - last_save < 600000u) return;
  if (!force && save_failed && now - last_save_try < 60000u) return;  // сорвалась: повтор через минуту
  last_save_try = now;
  static Snapshot snap;
  progress.seal();
  friends.seal();
  snap.progress = progress;
  snap.friends = friends;
  snap.settings = cfg;
  save_failed = !storage::save(&snap, sizeof snap);
  if (!save_failed) {
    progress_saved = progress;
    friends_saved = friends;
    cfg_saved = cfg;
    last_save = now;
  }
}

int days_in_month(int y, int m) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (m < 1 || m > 12) return 31;
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : kDays[m - 1];
}

// Настройки из флеша: чужая версия или значения вне диапазона — заводские для этого поля.
bool settings_ok(const Settings& s) { return s.magic == Settings{}.magic && s.version == Settings{}.version; }

void sanitize(Settings& s) {
  const Settings d;
  if (s.theme >= (uint8_t)Theme::Count) s.theme = d.theme;
  s.sound = s.sound ? 1 : 0;
  s.duel_ready = s.duel_ready ? 1 : 0;
  s.ghost = s.ghost ? 1 : 0;
  if (s.bday_m < 1 || s.bday_m > 12 || s.bday_d < 1 || s.bday_d > days_in_month(2000, s.bday_m))
    s.bday_m = s.bday_d = 0;
  if (s.year && (s.year < 2000 || s.year > 2099 || s.month < 1 || s.month > 12 || s.day < 1 ||
                 s.day > days_in_month(s.year, s.month)))
    s.year = 0, s.month = 0, s.day = 0;
  if (s.clock_min >= 1440) s.clock_min = d.clock_min, s.clock_known = 0;
  s.clock_known = s.clock_known ? 1 : 0;
}

void load_all() {
  storage::begin();
  static Snapshot snap;
  bool p_ok = false, f_ok = false, s_ok = false;
  if (storage::load(&snap, sizeof snap)) {
    p_ok = progress.load(&snap.progress, sizeof snap.progress);
    f_ok = friends.load(&snap.friends, sizeof snap.friends);
    s_ok = settings_ok(snap.settings);
    if (s_ok) cfg = snap.settings;
  } else {
    // Журнала ещё нет: записи прошлой прошивки в файлах LittleFS.
    uint8_t blob[sizeof(Progress)];
    const int n = storage::legacy_load("progress", blob, sizeof blob);
    p_ok = n > 0 && progress.load(blob, (uint32_t)n);
    uint8_t fblob[sizeof(Friends)];
    const int fn = storage::legacy_load("friends", fblob, sizeof fblob);
    f_ok = fn == (int)sizeof(Friends) && friends.load(fblob, (uint32_t)fn);
    Settings st;
    s_ok = storage::legacy_load("settings", &st, sizeof st) == (int)sizeof st && settings_ok(st);
    if (s_ok) cfg = st;
  }
  if (!p_ok) progress.reset();
  if (!f_ok) friends.reset();
  sanitize(cfg);
  progress_saved = progress;
  friends_saved = friends;
  cfg_saved = cfg;
}

// ---------------------------------------------------------------- экран

uint8_t wanted_contrast() {
  switch (bh.brightness()) {
    case Brightness::Dim: return (uint8_t)(cfg.contrast / 4);
    case Brightness::Dark: return (uint8_t)(cfg.contrast / 16);
    default: return cfg.contrast;
  }
}

bool start_oled() {
  digitalWrite(board::kDispEn, HIGH);
  delay(40);  // ключ включается плавно (C11), модулю нужно время на старт
  Wire1.begin();
  Wire1.setClock(400000);
  hal::i2c_390k(1);
  have_oled = oled.begin(Wire1, 0, false);
  if (have_oled) {
    shown_contrast = wanted_contrast();
    have_oled = oled.contrast(shown_contrast) && oled.send_full(eng.frame());
  }
  oled_full = false;
  return have_oled;  // не ответил — повторы раз в 2 с в начале loop
}

void screen_power(bool on, uint32_t now) {
  if (on == screen_on) return;
  if (on) {
    screen_on = true;
    oled_retry_ms = 2000;  // прошлые сбои не в счёт: первый повтор — через 2 с
    start_oled();          // не ответил — повторы в loop
  } else {
    if (have_oled) oled.power(false);
    // Шина экрана отпускается: иначе подтяжки ножек питали бы обесточенный модуль через SDA и SCL.
    Wire1.end();
    nrf_gpio_cfg_default(PIN_WIRE1_SDA);
    nrf_gpio_cfg_default(PIN_WIRE1_SCL);
    digitalWrite(board::kDispEn, LOW);  // модуль обесточен целиком
    screen_on = false;
  }
}

void push_frame() {
  DirtyWindow w[kPages];
  const int n = eng.dirty(w);
  bool ok = true;
  for (int i = 0; i < n && ok; ++i) ok = oled.send_window(eng.frame(), w[i].page, w[i].x0, w[i].x1);
  if (!ok) oled_full = true;
}

// ---------------------------------------------------------------- персонаж

void apply_identity(uint32_t now) {
  me = make_identity(chip_serial);
  bh.personality() = me.personality;
  bh.set_temperament(&temperament(me.archetype));
  bh.set_identity_hash(me.hash);
  voice.init(&temperament(me.archetype), me.hash);
  voice.set_enabled(cfg.sound != 0);
  eng.set_quirks(me.quirks, now);
  enc.init(me.hash);
  // Лимит встреч не тратят друзья — те, с кем виделись в разные дни (уровень ≥ 1): позывной,
  // выдуманный сегодня, так не обходит лимит.
  enc.set_known([](uint32_t h, const void* f) { return ((const Friends*)f)->level(h) >= 1; }, &friends);
}

void print_me() {
  const Temperament& t = temperament(me.archetype);
  out("Я %s: %s (%s), %s%s\n", me.code, t.name, t.tag, rarity_name(me.rarity),
                me.quirks.cyber_eye ? ", золотой кибер-глаз" : "");
  out("Рост: день %u, опыт %u, имплант %d/%d, открыто %d/%d эмоций, достижений %d/%d\n", progress.days,
                (unsigned)progress.xp, progress.implant, implant_count() - 1, progress.unlocked_count(), kExprCount,
                progress.achievement_count(), kAchCount);
  out("Шагов %u, касаний %u, друзей %u, дуэлей %u/%u; дуэли %s, невидимка %s\n", (unsigned)progress.steps,
                (unsigned)progress.touches, progress.friends, progress.duels_won,
                progress.duels_won + progress.duels_lost, cfg.duel_ready ? "вкл" : "выкл", cfg.ghost ? "вкл" : "выкл");
}

void set_duel_ready(bool on, uint32_t now) {
  cfg.duel_ready = on ? 1 : 0;
  SceneParams p;
  p.set_text(on ? "DUELS ON" : "DUELS OFF");
  p.set_sub(on ? "READY TO HACK" : "PEACE MODE");
  p.value = (int)(on ? BannerIcon::Skull : BannerIcon::Heart);
  bh.queue_scene(Scene::banner, p, eng.expression(), now, on ? "дуэли включены" : "дуэли выключены", true);
  out_line(on ? "Дуэли включены" : "Дуэли выключены");
}

// ---------------------------------------------------------------- часы и сутки

void advance_date() {
  if (!cfg.year) return;
  if (++cfg.day > days_in_month(cfg.year, cfg.month)) {
    cfg.day = 1;
    if (++cfg.month > 12) cfg.month = 1, ++cfg.year;
  }
}

void tick_clock(uint32_t now) {
  const uint16_t m = (uint16_t)clock_minutes();
  bh.set_clock(m);
  // Новые сутки: в полночь, если часы заданы, иначе каждые 24 часа работы.
  bool new_day = false;
  if (cfg.clock_known) {
    if (m < last_clock) new_day = true;
  } else if (uptime - day_t0 >= kDayMs) {
    day_t0 += kDayMs;
    new_day = true;
  }
  last_clock = m;
  if (new_day) {
    advance_date();
    if (cfg.year) bh.set_date(cfg.year, cfg.month, cfg.day);
    bh.new_day(now);
    enc.new_day();
  }
  // Часы переживают перезагрузку приблизительно: сохраняем их раз в 10 минут.
  if (cfg.clock_known && m % 10 == 0 && cfg.clock_min != m) {
    cfg.clock_min = m;
    clock_base_up = uptime - (uptime - clock_base_up) % 60000u;
  }
}

// ---------------------------------------------------------------- датчики

void poll_motion(uint32_t now) {
  if (!time_reached(now, next_acc)) return;
  // FIFO датчика — 32 выборки, 320 мс при 100 Гц: в экорежиме опрос с запасом.
  next_acc = now + (bh.eco() ? 200 : 100);
  static float xyz[32][3];
  const int n = acc.read_fifo(xyz, 32);
  Event ev[kSenseEvents];
  for (int i = 0; i < n; ++i) {
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = xyz[i][board::kAccMap[k][0]] * board::kAccMap[k][1];
    const uint32_t t = now - (uint32_t)(n - 1 - i) * 10u;  // 100 Гц
    const int ne = motion.feed(d[0], d[1], d[2], t, ev);
    for (int j = 0; j < ne; ++j) event(ev[j], now);
  }
  if (n) eng.set_tilt(motion.tilt_x(), motion.tilt_y());
  float jx, jy;
  if (motion.take_jerk(&jx, &jy)) eng.jerk(jx, jy, now);
  if (digitalRead(board::kAccInt1) == HIGH) {
    const int c = acc.take_click();
    // Двойное касание датчик сначала отмечает как одиночное: одиночное ждёт окно 0.5 с.
    if (c == 2) {
      tap_wait = false;
      eng.note_activity(now);
      event(Event::double_tap, now);
    } else if (c == 1 && !tap_wait) {
      tap_wait = true;
      tap_wait_at = now;
    }
  }
  if (tap_wait && now - tap_wait_at >= 500) {
    tap_wait = false;
    eng.note_activity(now);
    event(Event::tap, now);
  }
  if (time_reached(now, next_steps)) {
    next_steps = now + 10000;
    const uint32_t s = motion.take_steps();
    if (s) bh.add_steps(s, now);
  }
}

void poll_environment(uint32_t now) {
  Event ev[kSenseEvents];
  // Свет — разовыми замерами раз в 2 с: датчик включается на ~130 мс (пробуждение и
  // накопление 100 мс), остальное время спит. Постоянно включённый он брал бы ~0.2 мА.
  if (time_reached(now, next_light)) {
    if (!als_measuring) {
      als.power(true);
      als_measuring = true;
      als_started = now;
      next_light = now + 130;
    } else {
      const float lux = als.lux();
      if (lux < 0 && now - als_started < 400) {
        next_light = now + 30;  // замер ещё не готов
      } else {
        als.power(false);
        als_measuring = false;
        next_light = now + 2000;
        if (lux >= 0) {
          const int n = light.feed(lux, now, ev);
          for (int i = 0; i < n; ++i) event(ev[i], now);
        }
      }
    }
  }
  if (time_reached(now, next_temp)) {
    next_temp = now + 60000;
    const int n = thermal.feed(hal::temperature_c(), now, ev);
    for (int i = 0; i < n; ++i) event(ev[i], now);
  }
  float db;
  if (hal::mic_take(&db)) {
    const int n = hearing.feed(db, now, ev);
    for (int i = 0; i < n; ++i) event(ev[i], now);
  }
  // Микрофон слушает, пока брелок бодрствует; в темноте, во сне и в экорежиме — выключен.
  const bool want_mic = !bh.eco() && bh.brightness() == Brightness::Normal && !powering_off;
  if (want_mic && !hal::mic_on())
    hal::mic_begin();
  else if (!want_mic && hal::mic_on())
    hal::mic_end();
}

void poll_battery(uint32_t now) {
  static bool usb_was = false;
  const bool usb = hal::usb_present();
  if (!time_reached(now, next_batt) && usb == usb_was) return;
  usb_was = usb;
  next_batt = now + 10000;
  const bool chg = hal::charging();
  Event ev[kSenseEvents];
  const int n = battery.feed(hal::vsys_volts(), usb, chg, now, ev);
  if (!usb) charged_full = false, charge_pct0 = -1;
  for (int i = 0; i < n; ++i) {
    if (ev[i] == Event::charge_full) charged_full = true;
    // Подзарядка после полного заряда начинается почти со 100 %, а не с заряда до USB.
    // Включились уже на USB — заряд до него неизвестен.
    if (ev[i] == Event::charge_start && !charged_full) charge_t0 = now, charge_pct0 = battery.percent();
    event(ev[i], now);
  }
  // От USB заряд ячейки не измерить: оценка по времени заряда током 100 мА.
  int pct = battery.percent();
  if (usb) {
    if (charged_full)
      pct = 100;
    else if (chg && charge_pct0 >= 0) {
      const int est = charge_pct0 + (int)((now - charge_t0) / 60000u * 2u / 3u);  // ~0.7 % в минуту
      pct = est > 99 ? 99 : est;
    } else {
      pct = -1;  // заряд неизвестен (или статус зарядника ещё не установился)
    }
  }
  bh.set_battery_percent(pct);
  if (battery.empty() && !powering_off) {
    out_line("Ячейка почти пуста — выключаюсь");
    powering_off = true;
    bh.power_off(now);
  }
}

// ---------------------------------------------------------------- встречи по радио

void poll_radio(uint32_t now) {
  uint8_t flags = 0;
  if (cfg.duel_ready) flags |= kAdvDuelReady;
  if (eng.options().theme == Theme::Cyber) flags |= kAdvCyber;
  if (me.rarity == Rarity::Legendary) flags |= kAdvLegendary;
  enc.set_self(progress.implant, flags, mood_byte(bh.mood().valence));

  Advert a;
  int rssi = 0;
  // Невидимка не встречается: его не слышат, и сцена была бы только у него.
  while (radio::take(&a, &rssi))
    if (!cfg.ghost) enc.on_advert(a, rssi, now);

  radio::Pace pace = radio::Pace::Normal;
  if (powering_off)
    pace = radio::Pace::Off;
  else if (enc.wants_busy(now))
    pace = radio::Pace::Busy;
  else if (bh.brightness() != Brightness::Normal)
    pace = radio::Pace::Night;
  radio::set_pace(pace);
  radio::set_advert(enc.advert(now));
  radio::update(now);

  Meeting m;
  if (!enc.poll(now, &m)) return;
  const Identity peer = identity_from_hash(m.peer);
  eng.note_activity(now);
  if (m.kind == Rendezvous::Duel) {
    out("Дуэль с %s: %s\n", peer.code, m.won ? "победа" : "поражение");
    // Вибро — только если дуэль пошла в счёт (дневные лимиты): поток подделок не жужжит.
    const uint8_t duels = progress.today_duels;
    bh.duel(peer.code, m.won, now, m.peer, m.nonce);
    static const uint16_t kBuzz[] = {60, 80, 60, 80, 200};
    if (progress.today_duels != duels) hal::vibrate(kBuzz, 5);
  } else {
    const bool is_new = friends.level(m.peer) < 0;
    out("Встреча с %s (%s), %s\n", peer.code, archetype_name(peer.archetype), is_new ? "новый" : "знакомый");
    // Вибро — только если встреча пошла в счёт: новый знакомый или первая за день встреча
    // со старым, в пределах дневных лимитов. Друг, который весь день рядом, не жужжит.
    const uint8_t new_n = progress.today_new_friends, known_n = progress.today_known_friends;
    bh.meet(peer.code, is_new, now, m.peer, m.peer_level, m.nonce);
    static const uint16_t kHello[] = {80, 120, 80};
    if (progress.today_new_friends != new_n || progress.today_known_friends != known_n) hal::vibrate(kHello, 3);
  }
  // Без force: встречу может подстроить кто угодно, запись — по общему правилу save_all.
  save_all(now, false);
}

// ---------------------------------------------------------------- кнопка

bool btn_down = false, long_fired = false, tap_pending = false;
uint32_t btn_t0 = 0, btn_change = 0, tap_at = 0;

void poll_button(uint32_t now) {
  const bool raw = digitalRead(board::kButton) == LOW;
  if (raw != btn_down && now - btn_change > 25) {
    btn_down = raw;
    btn_change = now;
    if (raw) {
      btn_t0 = now;
      long_fired = false;
      eng.note_activity(now);
      if (!screen_on) screen_power(true, now);
    } else if (!long_fired) {
      if (tap_pending && now - tap_at < 400) {
        tap_pending = false;
        set_duel_ready(!cfg.duel_ready, now);
      } else {
        tap_pending = true;
        tap_at = now;
      }
    }
  }
  if (btn_down && !long_fired && now - btn_t0 >= kLongPressMs) {
    long_fired = true;
    tap_pending = false;
    event(Event::button_long, now);
    powering_off = true;
    bh.power_off(now);
  }
  if (tap_pending && !btn_down && now - tap_at >= 400) {
    tap_pending = false;
    event(Event::button, now);
  }
}

// ---------------------------------------------------------------- консоль

bool parse_int(const char* s, long lo, long hi, long* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  const long v = strtol(s, &end, 10);
  if (end == s || *end || v < lo || v > hi) return false;
  *out = v;
  return true;
}

void help() {
  out_line(
      "Команды:\n"
      "  me           кто я: характер, рост, шаги, дуэли\n"
      "  sensors      показания: ускорение, свет, звук, температура, батарея\n"
      "  ach          достижения; friends — знакомые брелоки\n"
      "  t ЧЧ:ММ      время; date 2026-12-31 — дата; bday 05-14 — день рождения\n"
      "  theme cyber  тема: classic, cyber или retro\n"
      "  snd          звук вкл/выкл; say laugh — реплика\n"
      "  duel         дуэли вкл/выкл (как двойное нажатие); ghost — невидимка в эфире\n"
      "  b 0..255     яркость\n"
      "  e <событие>  событие для характера: e tap, e music …\n"
      "  x <сцена>    сцена: hack birthday visor bsod …\n"
      "  v [on|off]   печатать события датчиков\n"
      "  off          выключиться; dfu — загрузчик для новой прошивки\n"
      "  reset yes    начать рост заново");
}

void command(char* s, uint32_t now) {
  while (*s == ' ') ++s;
  char* arg = strchr(s, ' ');
  if (arg) {
    *arg++ = 0;
    while (*arg == ' ') ++arg;
    if (!*arg) arg = nullptr;
  }
  if (!*s) return;
  eng.note_activity(now);
  if (!strcmp(s, "?") || !strcmp(s, "help")) return help();
  if (!strcmp(s, "me")) return print_me();
  if (!strcmp(s, "v")) {
    verbose = arg ? !strcmp(arg, "on") : !verbose;
    out_line(verbose ? "События датчиков печатаются" : "События датчиков не печатаются");
    return;
  }
  if (!strcmp(s, "sensors")) {
    static float xyz[32][3];
    const int n = acc.read_fifo(xyz, 32);
    if (n) {
      float d[3];
      for (int k = 0; k < 3; ++k) d[k] = xyz[n - 1][board::kAccMap[k][0]] * board::kAccMap[k][1];
      out("Ускорение: x %.2f, y %.2f, z %.2f g (висит ровно — около 0, +1, 0)\n", d[0], d[1], d[2]);
    } else {
      out_line(acc.ok() ? "Ускорение: нет новых выборок" : "Акселерометр не отвечает");
    }
    out("Свет: %s; звук: фон %.0f дБ; температура %.1f °C\n", als.ok() ? "есть" : "нет ответа",
                  hearing.floor_db(), hal::temperature_c());
    out("Питание %.2f В, USB %s, заряжается %s, заряд %d%%; шагов всего %u\n", hal::vsys_volts(),
                  hal::usb_present() ? "да" : "нет", hal::charging() ? "да" : "нет", battery.percent(),
                  (unsigned)motion.steps_total());
    out("Флеш: стираний страниц с включения %u%s\n", (unsigned)storage::erases(),
                  save_failed ? ", последняя запись не удалась" : "");
    return;
  }
  if (!strcmp(s, "ach")) {
    for (int i = 0; i < kAchCount; ++i) {
      const Achievement& a = achievement((Ach)i);
      out("  [%c] %-14s %s — %s\n", progress.earned((Ach)i) ? 'x' : ' ', a.tag, a.name, a.how);
    }
    return;
  }
  if (!strcmp(s, "friends")) {
    for (const FriendRecord& r : friends.f) {
      if (!r.hash) continue;
      const Identity id = identity_from_hash(r.hash);
      const int lvl = friends.level(r.hash);
      out("  %s %-10s дней %u, %s\n", id.code, archetype_name(id.archetype), r.days, friend_level_tag(lvl));
    }
    return;
  }
  if (!strcmp(s, "t")) {
    int hh = -1, mm = -1;
    char tail = 0;
    if (!arg || sscanf(arg, "%d:%d%c", &hh, &mm, &tail) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59)
      return (void)out_line("Формат: t 23:30");
    cfg.clock_min = (uint16_t)(hh * 60 + mm);
    cfg.clock_known = 1;
    clock_base_up = uptime;
    last_clock = cfg.clock_min;
    out("Время %02d:%02d\n", hh, mm);
    return;
  }
  if (!strcmp(s, "date")) {
    int y = 0, mo = 0, d = 0;
    char tail = 0;
    if (!arg || sscanf(arg, "%d-%d-%d%c", &y, &mo, &d, &tail) != 3 || y < 2000 || y > 2099 || mo < 1 || mo > 12 ||
        d < 1 || d > days_in_month(y, mo))
      return (void)out_line("Формат: date 2026-12-31");
    cfg.year = (uint16_t)y, cfg.month = (uint8_t)mo, cfg.day = (uint8_t)d;
    bh.set_date(cfg.year, cfg.month, cfg.day);
    out("Дата %04d-%02d-%02d\n", y, mo, d);
    return;
  }
  if (!strcmp(s, "bday")) {
    int mo = 0, d = 0;
    char tail = 0;
    if (!arg || sscanf(arg, "%d-%d%c", &mo, &d, &tail) != 2 || mo < 1 || mo > 12 || d < 1 ||
        d > days_in_month(2000, mo))
      return (void)out_line("Формат: bday 05-14");
    cfg.bday_m = (uint8_t)mo, cfg.bday_d = (uint8_t)d;
    bh.set_birthday(cfg.bday_m, cfg.bday_d);
    out("День рождения: %02d-%02d\n", mo, d);
    return;
  }
  if (!strcmp(s, "theme")) {
    Theme th;
    if (!arg || !theme_from_id(arg, &th)) return (void)out_line("Формат: theme classic | cyber | retro");
    eng.options().theme = th;
    cfg.theme = (uint8_t)th;
    eng.set_expression(eng.expression(), now);
    return;
  }
  if (!strcmp(s, "snd")) {
    cfg.sound = cfg.sound ? 0 : 1;
    voice.set_enabled(cfg.sound != 0);
    out_line(cfg.sound ? "Звук включён" : "Звук выключен");
    return;
  }
  if (!strcmp(s, "say")) {
    Cue c;
    if (!arg || !cue_from_id(arg, &c)) return (void)out_line("Нет такой реплики");
    if (!voice.say(c, now)) out_line("Пищалка занята, тихие часы или звук выключен");
    return;
  }
  if (!strcmp(s, "duel")) return set_duel_ready(!cfg.duel_ready, now);
  if (!strcmp(s, "ghost")) {
    cfg.ghost = cfg.ghost ? 0 : 1;
    radio::set_ghost(cfg.ghost != 0);
    out_line(cfg.ghost ? "Невидимка: только слушаю эфир" : "Снова виден другим брелокам");
    return;
  }
  if (!strcmp(s, "b")) {
    long v = 0;
    if (!parse_int(arg, 0, 255, &v)) return (void)out_line("Формат: b 0…255");
    cfg.contrast = (uint8_t)v;
    return;
  }
  if (!strcmp(s, "e")) {
    for (int i = 0; arg && i < kEventCount; ++i)
      if (!strcmp(arg, event_id((Event)i))) return event((Event)i, now);
    return (void)out_line("Нет такого события");
  }
  if (!strcmp(s, "x")) {
    Scene sc;
    if (!arg || !scene_from_id(arg, &sc)) return (void)out_line("Нет такой сцены");
    SceneParams p;
    eng.play_scene(sc, now, p);
    voice.scene(sc, p, now);
    return;
  }
  if (!strcmp(s, "off")) {
    powering_off = true;
    bh.power_off(now);
    return;
  }
  if (!strcmp(s, "dfu")) {
    save_all(now, true);
    out_line("Перезагрузка в загрузчик");
    Serial.flush();
    delay(100);
    hal::enter_bootloader();
  }
  if (!strcmp(s, "reset") && arg && !strcmp(arg, "yes")) {
    progress.reset();
    friends.reset();
    save_all(now, true);
    return print_me();
  }
  out_line("Не знаю такую команду, помощь — ?");
}

void poll_serial(uint32_t now) {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      line[line_n] = 0;
      if (!line_overflow && line_n) command(line, now);
      line_n = 0;
      line_overflow = false;
    } else if (line_n + 1 < sizeof line) {
      line[line_n++] = ch;
    } else {
      line_overflow = true;
    }
  }
}


// ---------------------------------------------------------------- выключение на USB

// Выключились на зарядке. Полный сон тут не годится: проснуться можно только кнопкой, а
// кнопка при USB включает загрузчик. Поэтому экран, датчики и эфир выключены, часы идут,
// зарядка идёт. Кнопка — включиться (перезагрузкой, часы сохраняются), USB отключили —
// полный сон.
// Ждёт, пока отпустят кнопку: не дольше 10 с и только пока есть USB. Залипшая кнопка не
// держит брелок в этом цикле — дальше её обходит system_off.
bool wait_release() {
  const uint32_t t0 = millis();
  while (digitalRead(board::kButton) == LOW) {
    if (!hal::usb_present() || millis() - t0 > 10000) return false;
    hal::watchdog_feed();
    delay(10);
  }
  return true;
}

void off_on_usb() {
  // Кнопку, которой выключали, ещё держат: иначе она тут же включила бы брелок обратно.
  wait_release();
  screen_power(false, millis());
  hal::mic_end();
  hal::buzzer(0, false);
  hal::vibro_stop();
  radio::set_pace(radio::Pace::Off);
  radio::update(millis());
  // Включает только новое нажатие: кнопка, которая так и не отпустилась, — нет.
  bool armed = digitalRead(board::kButton) == HIGH;
  for (;;) {
    hal::watchdog_feed();
    uptime = uptime_now();
    const uint32_t now = (uint32_t)uptime;
    tick_clock(now);
    save_all(now, false);
    if (!hal::usb_present()) return;
    if (digitalRead(board::kButton) == HIGH) {
      armed = true;
    } else if (armed) {
      delay(30);
      if (digitalRead(board::kButton) == LOW) {
        wait_release();
        // Часы — с точностью до минуты: после программного сброса они продолжатся.
        cfg.clock_min = (uint16_t)clock_minutes();
        save_all(now, true);
        NVIC_SystemReset();
      }
    }
    delay(50);
  }
}

}  // namespace

// ---------------------------------------------------------------- setup / loop

void setup() {
  hal::power_init();  // при первом запуске записывает 3.3 В и перезагружается
  pinMode(board::kButton, INPUT_PULLUP);
  pinMode(board::kAccInt1, INPUT);
  digitalWrite(board::kDispEn, LOW);
  pinMode(board::kDispEn, OUTPUT);
  hal::vibro_begin();
  hal::buzzer_begin();
  // Сторожевой таймер — сразу: вывода сброса на плате нет, и зависание дальше (шина I2C,
  // флеш, экран) иначе оставило бы плату мёртвой до разряда ячейки.
  hal::watchdog_start();
  Serial.begin(115200);

  hal::i2c_recover(PIN_WIRE_SDA, PIN_WIRE_SCL);
  Wire.begin();
  Wire.setClock(400000);
  hal::i2c_390k(0);
  load_all();
  // Часов реального времени у платы нет: после выключения неизвестно, сколько прошло.
  const bool clock_lost = !hal::warm_reset() && (cfg.clock_known || cfg.year);
  if (!hal::warm_reset()) cfg.clock_known = 0, cfg.year = 0, cfg.month = 0, cfg.day = 0;

  uptime = uptime_now();
  const uint32_t now = (uint32_t)uptime;
  chip_serial = (uint64_t)NRF_FICR->DEVICEID[1] << 32 | NRF_FICR->DEVICEID[0];
  const uint32_t seed = (uint32_t)chip_serial ^ (uint32_t)(chip_serial >> 32) ^ micros();
  eng.init(seed, now);
  eng.options().theme = cfg.theme < (uint8_t)Theme::Count ? (Theme)cfg.theme : Theme::Classic;
  bh.init(&eng, seed ^ 0x9E3779B9u, now);
  bh.attach_voice(&voice);
  bh.attach_progress(&progress);
  bh.attach_friends(&friends);
  apply_identity(now);
  if (cfg.bday_m) bh.set_birthday(cfg.bday_m, cfg.bday_d);
  if (cfg.year) bh.set_date(cfg.year, cfg.month, cfg.day);
  clock_base_up = uptime;
  day_t0 = uptime;
  last_clock = (uint16_t)clock_minutes();
  if (progress.days == 0) bh.new_day(now);

  acc.begin(Wire, board::kLis3dhAddr);
  als.begin(Wire, board::kLtr303Addr);
  radio::begin();
  radio::set_ghost(cfg.ghost != 0);

  start_oled();
  next_acc = next_light = next_temp = next_batt = next_steps = now;
  eng.play_scene(Scene::boot, now);
  voice.scene(Scene::boot, SceneParams{}, now);
  hal::watchdog_feed();

  out("\nБрелок с эмоциями, плата rev B. Экран %s, акселерометр %s, свет %s\n", have_oled ? "есть" : "нет",
                acc.ok() ? "есть" : "нет", als.ok() ? "есть" : "нет");
  print_me();
  if (clock_lost) out_line("После выключения время и дата сброшены: задайте их командами t и date");
}

void loop() {
  uptime = uptime_now();
  const uint32_t now = (uint32_t)uptime;
  hal::watchdog_feed();

  if (screen_on && !have_oled) {
    // Экран не отвечает: повторы всё реже (до минуты) — каждый стоит 40 мс и тайм-ауты шины.
    // Перед повтором модуль обесточивается: зависший контроллер так сбрасывается.
    if (!oled_waiting) oled_waiting = true, oled_fail_at = now;
    if (now - oled_fail_at >= oled_retry_ms) {
      Wire1.end();
      nrf_gpio_cfg_default(PIN_WIRE1_SDA);
      nrf_gpio_cfg_default(PIN_WIRE1_SCL);
      digitalWrite(board::kDispEn, LOW);
      delay(30);
      if (start_oled()) {
        oled_retry_ms = 2000;
      } else {
        oled_retry_ms = oled_retry_ms < 30000 ? oled_retry_ms * 2 : 60000;
        oled_fail_at = now;
      }
    }
  } else {
    oled_waiting = false;
  }
  poll_serial(now);
  poll_button(now);
  if (!powering_off) {
    poll_motion(now);
    poll_environment(now);
  }
  poll_battery(now);
  poll_radio(now);
  tick_clock(now);
  save_all(now, false);

  if (time_reached(now, bh.next_update_ms())) bh.update(now);
  // Экран обесточен — кадры не рисуются чаще раза в секунду: сцены и сроки идут, а
  // растеризация впустую не тратит батарею. Включится экран — уйдёт полный кадр.
  static uint32_t dark_frame_last = 0;
  if (time_reached(now, eng.next_update_ms()) && (screen_on || now - dark_frame_last >= 1000u)) {
    dark_frame_last = now;
    if (eng.update(now) && have_oled && screen_on) push_frame();
  }
  if (have_oled && screen_on && oled_full) {
    if (oled.send_full(eng.frame()))
      oled_full = false;
    else
      have_oled = false;
  }
  if (have_oled && screen_on) {
    const uint8_t c = wanted_contrast();
    if (c != shown_contrast && oled.contrast(c)) shown_contrast = c;
  }
  // В кармане дольше минуты — экран обесточен; свет, касание или кнопка его вернут.
  if (bh.brightness() == Brightness::Dark && eng.scene() == Scene::None) {
    if (!dark_timing) dark_timing = true, dark_since = now;
    if (screen_on && now - dark_since > kDarkScreenOffMs) screen_power(false, now);
  } else {
    dark_timing = false;
    if (!screen_on) screen_power(true, now);
  }

  hal::buzzer(voice.tone(now), voice.soft());
  hal::vibro_update(now);

  // Выключение: дождаться конца сцены «спокойной ночи», сохранить всё и уснуть.
  if (powering_off && !off_timing) off_timing = true, off_at = now;
  // Ждём именно сцену выключения: если выключили посреди другой сцены, сначала доиграет она.
  if (powering_off && eng.scene() == Scene::shutdown) off_scene_seen = true;
  if (powering_off && ((off_scene_seen && eng.scene() == Scene::None && !voice.busy(now)) || now - off_at > 15000)) {
    save_all(now, true);
    acc.power(false);
    als.power(false);
    if (hal::usb_present()) off_on_usb();  // вернётся, только когда USB отключат
    hal::system_off();
  }
  delay(10);  // процессор спит до следующего прохода
}
