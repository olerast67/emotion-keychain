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
#include "snapshot.h"
#include "ssd1306.h"
#include "storage.h"

// Версия прошивки: git describe при сборке (tools/version.py); без git — «unknown». Видна в
// приветствии консоли и по команде ver: так видно, какая сборка залита в брелок.
#ifndef FW_VERSION
#define FW_VERSION "unknown"
#endif

using namespace face;

namespace {

constexpr uint64_t kDayMs = 24ull * 3600000ull;
constexpr uint32_t kDarkScreenOffMs = 60000;  // в кармане дольше минуты — экран гаснет
constexpr uint32_t kLongPressMs = 3000;
// Загрузчик по кнопке: USB подключили, пока кнопку держат, и зажали её не раньше чем за 10 с.
// Кнопка, которую держат дольше (залипла или зажата с самого включения), загрузчик не включает.
constexpr uint32_t kDfuPressMs = 10000;

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
bool oled_bus_bad = false;  // шина экрана зажата: обмен по ней не начинается
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
int battery_shown = -1;  // процент заряда, который видит характер (на USB — оценка по времени)
bool charged_full = false;  // с этого подключения USB ячейка уже заряжалась до конца
bool charge_allowed = false;  // заряд разрешён по температуре (ножка CHG_EN)
uint32_t vib_end = 0, voice_end = 0;  // когда кончились вибро и звук — датчики их не слышат
bool usb_seen = false;        // USB на прошлом проходе: подключили с зажатой кнопкой — загрузчик
uint32_t oled_off_at = 0;     // экран обесточен для перезапуска с этого момента
bool oled_cycling = false;
uint32_t next_health = 0;
int als_fail = 0;
bool sensor_bus = false;  // шина датчиков свободна и запущена (Wire)
uint32_t next_fix = 0, fix_retry_ms = 10000;  // перезапуск датчиков: когда и через сколько повтор
Settings cfg_seen;
uint32_t cfg_changed_at = 0;

// Кнопка (poll_button): нажата ли после подавления дребезга и с какого момента.
bool btn_down = false, long_fired = false, tap_pending = false;
uint32_t btn_t0 = 0, btn_change = 0, tap_at = 0;

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

// Минуты с начала текущих суток: по часам, если они заданы, иначе по счёту работы. day_t0
// может быть «до включения» (сутки продолжаются с прошлого раза): разность беззнаковая и верна
// по модулю 2^64.
uint16_t day_minutes() {
  return (uint16_t)(cfg.clock_known ? clock_minutes() : (unsigned)((uptime - day_t0) / 60000u % 1440u));
}

// Часы и счёт суток в настройках — с точностью до минуты: перед записью, после которой брелок
// перезагрузится или уснёт. Начало отсчёта сдвигается вместе с clock_min, иначе прошедшие
// минуты посчитались бы дважды, если работа всё-таки продолжится.
void sync_clock() {
  if (cfg.clock_known) {
    const uint16_t m = (uint16_t)clock_minutes();
    clock_base_up = uptime - (uptime - clock_base_up) % 60000u;
    cfg.clock_min = m;
  }
  cfg.day_min = day_minutes();
}

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

// Всё, что переживает выключение, — одной записью журнала (snapshot.h).
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
  // Часы и счёт суток меняют настройки каждые 10 минут — это обычная запись. Остальное в
  // настройках — скоро, но не на каждое изменение: через 3 с после последнего и не чаще раза
  // в 10 с, иначе скрипт, гоняющий команды консоли, протёр бы флеш за часы.
  Settings a = cfg, b = cfg_saved;
  a.clock_min = b.clock_min = 0;
  a.day_min = b.day_min = 0;
  if (memcmp(&cfg, &cfg_seen, sizeof cfg) != 0) cfg_seen = cfg, cfg_changed_at = now;
  const bool user_settings = memcmp(&a, &b, sizeof a) != 0 && now - cfg_changed_at >= 3000u &&
                             now - last_save >= 10000u;
  const bool urgent = force || unlocks || user_settings;
  if (!urgent && now - last_save < 600000u) return;
  // Стирание страницы останавливает цикл на доли секунды: посреди звука или вибро это слышно.
  if (!force && (voice.busy(now) || hal::vibro_busy())) return;
  if (!force && save_failed && now - last_save_try < 60000u) return;  // сорвалась: повтор через минуту
  last_save_try = now;
  static Snapshot snap;
  progress.seal();
  friends.seal();
  snap.head = SnapHead{};
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

// Настройки из флеша: значения вне диапазона — заводские для этого поля (чужую версию
// отбрасывает settings_ok).
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
  s.empty_off = s.empty_off ? 1 : 0;
  if (s.day_min >= 1440) s.day_min = 0;
}

void load_all() {
  storage::begin();
  static uint8_t raw[storage::kMaxRecord];
  const int n = storage::load_any(raw, sizeof raw);
  const SnapLoaded got = snapshot_load(raw, n, &progress, &friends, &cfg);
  bool p_ok = got.progress, f_ok = got.friends, s_ok = got.settings;
  if (!got.found && n < 0) {
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
  nrf_gpio_cfg_default(PIN_WIRE1_SDA);
  nrf_gpio_cfg_default(PIN_WIRE1_SCL);
  digitalWrite(board::kDispEn, HIGH);
  delay(40);  // ключ включается плавно (C11), модулю нужно время на старт
  // Шина экрана зажата: SCL в нуле (замыкание, неисправный модуль), SCL и SDA замкнуты между
  // собой (соседние выводы J2) или SDA так и держит зависший контроллер. Обмен по такой шине в
  // ядре ждёт без срока либо не доходит до экрана, а сторожевой таймер перезагружал бы брелок
  // по кругу — без консоли, зарядки и выключения кнопкой. Её не трогаем: экрана нет, модуль
  // обесточен, повтор с перезапуском питания — в loop, всё реже.
  if (!hal::i2c_recover(PIN_WIRE1_SDA, PIN_WIRE1_SCL)) {
    nrf_gpio_cfg_default(PIN_WIRE1_SDA);
    nrf_gpio_cfg_default(PIN_WIRE1_SCL);
    digitalWrite(board::kDispEn, LOW);
    if (!oled_bus_bad) out_line("Шина экрана зажата: SDA или SCL в нуле или замкнуты между собой");
    oled_bus_bad = true;
    have_oled = oled_full = false;
    return false;
  }
  oled_bus_bad = false;
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
    oled_cycling = false;
    start_oled();          // не ответил — повторы в loop
  } else {
    oled_cycling = false;
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
  // Движку — минуты текущих суток: по часам, если они заданы, иначе по счёту работы (по ним
  // он ведёт лимит кормления). Ночь, недосып и пасхалки по времени — только по заданным часам:
  // после выключения часы потеряны, и «ночь» по старым минутам шла бы днём — тусклый экран,
  // эфир не слушается, лицо засыпает. В кармане и в темноте брелок и так гаснет по свету.
  bh.set_clock_known(cfg.clock_known != 0);
  bh.set_clock(day_minutes());
  // Новые сутки: в полночь, если часы заданы, иначе каждые 24 часа работы.
  bool new_day = false;
  if (cfg.clock_known) {
    if (m < last_clock) new_day = true;
  } else if (uptime - day_t0 >= kDayMs) {
    day_t0 += kDayMs;
    new_day = true;
  }
  last_clock = m;
  // Без часов сутки — 24 часа работы, и отсчёт переживает выключение: минуты с начала
  // текущих суток сохраняются раз в 10 минут (и точно — перед выключением). Иначе брелок,
  // который выключают на ночь, никогда не дожил бы до новых суток, и дневные лимиты не
  // сбрасывались бы. С часами это минуты с полуночи: после выключения часы теряются, и сутки
  // продолжаются по счёту работы с того же места.
  const uint16_t dm = day_minutes();
  if (new_day || dm / 10 != cfg.day_min / 10) cfg.day_min = dm;
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
  // Свой вибромотор трясёт датчик: пока он работает и 150 мс после, толчки — не события.
  if (hal::vibro_busy()) vib_end = now;
  const bool buzzing = hal::vibro_busy() || now - vib_end < 150;
  for (int i = 0; i < n; ++i) {
    float d[3];
    for (int k = 0; k < 3; ++k) d[k] = xyz[i][board::kAccMap[k][0]] * board::kAccMap[k][1];
    const uint32_t t = now - (uint32_t)(n - 1 - i) * 10u;  // 100 Гц
    const int ne = motion.feed(d[0], d[1], d[2], t, ev);
    for (int j = 0; j < ne; ++j) {
      const Event e = ev[j];
      if (buzzing && (e == Event::shake || e == Event::shake_hard || e == Event::impact || e == Event::free_fall))
        continue;
      event(e, now);
    }
  }
  if (n) eng.set_tilt(motion.tilt_x(), motion.tilt_y());
  float jx, jy;
  if (motion.take_jerk(&jx, &jy)) eng.jerk(jx, jy, now);
  if (digitalRead(board::kAccInt1) == HIGH) {
    int c = acc.take_click();
    if (buzzing) c = 0;  // флаг касания снят, а само касание — от мотора
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
          als_fail = 0;
          const int n = light.feed(lux, now, ev);
          for (int i = 0; i < n; ++i) event(ev[i], now);
        } else {
          ++als_fail;  // три замера подряд без ответа — перезапуск (check_sensors)
        }
      }
    }
  }
  if (time_reached(now, next_temp)) {
    next_temp = now + 60000;
    const float t = hal::temperature_c();
    hal::adc_follow_temperature(t);
    const int n = thermal.feed(t, now, ev);
    for (int i = 0; i < n; ++i) event(ev[i], now);
  }
  float db;
  // Своя пищалка и свой мотор — не звук вокруг: пока они работают и 150 мс после, уровень не
  // учитывается.
  if (voice.busy(now)) voice_end = now;
  if (hal::vibro_busy()) vib_end = now;
  if (hal::mic_take(&db) && !voice.busy(now) && now - voice_end >= 150 && !hal::vibro_busy() &&
      now - vib_end >= 150) {
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

// Заряд — только в разумной температуре: у MCP73832 нет входа термистора, а литиевая ячейка
// на морозе от заряда портится. Температура — кристалла nRF: по документации она верна лишь
// до ±5 °C, а на USB кристалл ещё и теплее ячейки (его греют зарядник и сам nRF). Поэтому у
// холода запас большой: заряд разрешается с 10 °C и запрещается ниже 7 °C — так ячейку около
// нуля не заряжает и датчик с худшей ошибкой (гарантию даст только термистор у ячейки, rev C).
// У жары кристалл ошибается в безопасную сторону: разрешено до 42 °C, запрещено выше 45 °C.
// Проверка — сразу после подключения USB и дальше раз в 30 с.
constexpr float kChargeOnC = 10.0f, kChargeOffC = 7.0f, kChargeHotOnC = 42.0f, kChargeHotOffC = 45.0f;

// Можно ли заряжать при температуре кристалла t: с запасом от дребезга у порогов.
bool charge_temp_ok(bool allowed, float t) {
  return allowed ? (t >= kChargeOffC && t <= kChargeHotOffC) : (t >= kChargeOnC && t <= kChargeHotOnC);
}

// true — заряд только что разрешён: статус зарядника ещё не установился.
bool update_charge_gate(bool usb) {
  static uint32_t next = 0;
  static bool started = false;
  static bool blocked = false;  // запрещён по температуре — консоль скажет, когда снова можно
  const uint32_t now = (uint32_t)uptime;
  if (!usb) {
    if (charge_allowed) hal::charge_enable(false), charge_allowed = false;
    started = blocked = false;
    return false;
  }
  if (started && !time_reached(now, next)) return false;
  started = true;
  next = now + 30000;
  const float t = hal::temperature_c();
  const bool ok = charge_temp_ok(charge_allowed, t);
  if (ok == charge_allowed) {
    if (!ok && !blocked) {
      blocked = true;
      out("Заряд не идёт: %.0f °C, заряжается от %.0f до %.0f °C\n", t, kChargeOnC, kChargeHotOnC);
    }
    return false;
  }
  charge_allowed = ok;
  hal::charge_enable(ok);
  if (!ok) {
    blocked = true;
    out("Заряд остановлен: %.0f °C, заряжается от %.0f до %.0f °C\n", t, kChargeOnC, kChargeHotOnC);
  } else if (blocked) {
    blocked = false;
    out("Заряд снова разрешён (%.0f °C)\n", t);
  }
  return ok;
}

void set_battery_shown(int pct) {
  battery_shown = pct;
  bh.set_battery_percent(pct);
}

void poll_battery(uint32_t now) {
  static bool usb_was = false;
  static bool holding = false;  // замер отложен до hold_until
  static uint32_t hold_until = 0;
  const bool usb = hal::usb_present();
  // Подключили USB, пока держат кнопку, — загрузчик для новой прошивки (как при сбросе).
  // Только для свежего нажатия: на брелоке с залипшей кнопкой зарядка не включала бы его.
  if (usb && !usb_seen && btn_down && now - btn_t0 < kDfuPressMs) {
    sync_clock();
    save_all(now, true);
    hal::enter_bootloader();
  }
  if (usb != usb_seen) hal::usb_crystal(usb);
  const bool usb_prev = usb_seen;
  usb_seen = usb;
  const bool opened = update_charge_gate(usb);
  if (cfg.empty_off && usb) cfg.empty_off = 0;  // зарядили — снова обычное включение
  // Замер откладывается. Сразу после отключения USB система ~0.1 с питается через диод в
  // P-MOSFET, на 0.5 В ниже ячейки: замер показал бы почти пустую ячейку — ждём 1.5 с. После
  // разрешения заряда статус MCP73832 устанавливается не сразу, а «USB есть, не заряжается»
  // после зарядки значило бы «зарядилась до конца» — ждём 2 с. Отключение видно по USB на
  // прошлом проходе: кабель, выдернутый посреди ожидания после подключения, тоже ждёт 1.5 с.
  if (usb_prev && !usb) holding = true, hold_until = now + 1500;
  if (opened) holding = true, hold_until = now + 2000;
  if (holding) {
    if (!time_reached(now, hold_until)) return;
    holding = false;
    next_batt = now;
  }
  if (!time_reached(now, next_batt) && usb == usb_was) return;
  usb_was = usb;
  next_batt = now + 10000;
  // Заряд запрещён по температуре: зарядник молчит, и это не «зарядился до конца». Оценка
  // заряда по времени стоит: перерыв — не зарядка (заряженное до него в ней остаётся).
  if (usb && !charge_allowed) {
    if (charge_pct0 >= 0 && !charged_full) {
      const int est = charge_pct0 + (int)((now - charge_t0) / 60000u * 2u / 3u);
      charge_pct0 = est > 99 ? 99 : est;
      charge_t0 = now;
    }
    set_battery_shown(-1);
    return;
  }
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
  set_battery_shown(pct);
  if (battery.empty() && !powering_off) {
    out_line("Ячейка почти пуста — выключаюсь");
    cfg.empty_off = 1;
    powering_off = true;
    bh.power_off(now, true);
  }
}

// Шина датчиков заново: Wire выключается (пока TWIM включён, ножками управляет он, и такты
// i2c_recover до шины не дошли бы), ведомый, зажавший SDA, отпускается тактами. Если SDA или
// SCL так и остались в нуле, Wire не запускается, а датчики считаются потерянными: обмен по
// зажатой шине в ядре ждёт без срока — до сброса сторожевым таймером.
bool start_sensor_bus() {
  Wire.end();
  sensor_bus = hal::i2c_recover(PIN_WIRE_SDA, PIN_WIRE_SCL);
  if (!sensor_bus) {
    acc.lost();
    als.lost();
    return false;
  }
  Wire.begin();
  Wire.setClock(400000);
  hal::i2c_390k(0);
  return true;
}

// Датчики, переставшие отвечать или потерявшие настройки (сброс питания, сбой шины), —
// перезапуск, а не тишина до выключения. Проверка раз в 10 с; неудачный перезапуск
// повторяется всё реже, до раза в 5 минут: датчика, которого нет совсем, батарея не замечает.
// Перезапуск держит цикл до ~0.2 с, поэтому не посреди звука и вибро.
void check_sensors(uint32_t now) {
  if (!time_reached(now, next_health)) return;
  if (voice.busy(now) || hal::vibro_busy()) return;  // проверим на следующем проходе
  next_health = now + 10000;
  const bool acc_bad = !acc.healthy();
  const bool als_bad = !als.ok() || als_fail >= 3;
  if (!acc_bad && !als_bad) {
    // Срок держится свежим: «давний» срок через 24.8 суток работы стал бы сроком в будущем.
    fix_retry_ms = 10000;
    next_fix = now;
    return;
  }
  if (!time_reached(now, next_fix)) return;
  bool fixed = start_sensor_bus();
  if (sensor_bus && acc_bad) {
    if (acc.begin(Wire, board::kLis3dhAddr))
      out_line("Акселерометр перезапущен");
    else
      fixed = false;
  }
  if (sensor_bus && als_bad) {
    als_fail = 0;
    als_measuring = false;
    if (als.begin(Wire, board::kLtr303Addr))
      out_line("Датчик света перезапущен");
    else
      fixed = false;
  }
  if (!sensor_bus) out_line("Шина датчиков зажата: SDA или SCL в нуле");
  fix_retry_ms = fixed ? 10000 : fix_retry_ms < 150000 ? fix_retry_ms * 2 : 300000;
  next_fix = now + fix_retry_ms;
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
  // Ночью и в кармане эфир не слушается, а реклама — раз в 10 с, даже если рядом сосед:
  // часто — только когда общая сцена уже объявлена и её надо довести до конца.
  if (powering_off)
    pace = radio::Pace::Off;
  else if (enc.pending())
    pace = radio::Pace::Busy;
  else if (bh.brightness() != Brightness::Normal)
    pace = radio::Pace::Night;
  else if (enc.wants_busy(now))
    pace = radio::Pace::Busy;
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
    // Дуэль — тоже встреча: уровень соседа нужен для подарка старшего, как в meet.
    bh.duel(peer.code, m.won, now, m.peer, m.nonce, m.peer_level);
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
      "  t [ЧЧ:ММ]    время (без числа — показать); date 2026-12-31 — дата; bday 05-14 — день рождения\n"
      "  theme cyber  тема: classic, cyber или retro\n"
      "  snd          звук вкл/выкл; say laugh — реплика\n"
      "  duel         дуэли вкл/выкл (как двойное нажатие); ghost — невидимка в эфире\n"
      "  b 0..255     яркость\n"
      "  e <событие>  событие для характера: e tap, e music …\n"
      "  x <сцена>    сцена: hack birthday visor bsod …\n"
      "  v [on|off]   печатать события датчиков\n"
      "  off          выключиться; dfu — загрузчик для новой прошивки; ver — версия прошивки\n"
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
  if (!strcmp(s, "ver")) return (void)out("Прошивка %s, плата rev B\n", FW_VERSION);
  if (!strcmp(s, "t") && !arg) {
    if (!cfg.clock_known) return (void)out_line("Время не задано: t ЧЧ:ММ");
    const unsigned m = clock_minutes();
    out("Время %02u:%02u\n", m / 60, m % 60);
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
    bh.set_clock_known(true);  // ночь и пасхалки по часам — сразу
    bh.set_clock(cfg.clock_min);
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
    sync_clock();  // загрузчик вернётся в прошивку программным сбросом: часы продолжатся
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
    const bool usb = hal::usb_present();
    update_charge_gate(usb);
    if (!usb) return;
    if (digitalRead(board::kButton) == HIGH) {
      armed = true;
    } else if (armed) {
      delay(30);
      if (digitalRead(board::kButton) == LOW) {
        // Держат дольше 10 с — сброс с зажатой кнопкой при USB: загрузчик ждёт прошивку.
        wait_release();
        sync_clock();  // после программного сброса часы продолжатся
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

  // Шина датчиков зажата (SDA или SCL в нуле) — датчики не трогаем: обмен по ней висел бы до
  // сторожевого таймера, и брелок перезагружался бы по кругу без лица. Повторит check_sensors.
  start_sensor_bus();
  load_all();
  // Часов реального времени у платы нет: после выключения неизвестно, сколько прошло.
  const bool clock_lost = !hal::warm_reset() && (cfg.clock_known || cfg.year);
  if (!hal::warm_reset())
    cfg.clock_known = 0, cfg.clock_min = Settings{}.clock_min, cfg.year = 0, cfg.month = 0, cfg.day = 0;

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
  day_t0 = uptime - (uint64_t)cfg.day_min * 60000u;  // сутки без часов продолжаются с прошлого раза
  last_clock = (uint16_t)clock_minutes();
  bh.set_clock_known(cfg.clock_known != 0);
  bh.set_clock(day_minutes());
  if (progress.days == 0) bh.new_day(now);

  // Выключились на пустой ячейке, и с тех пор не заряжали: кнопка показывает пустую батарею
  // на полторы секунды и снова выключает — полная загрузка с экраном, эфиром и микрофоном
  // съела бы остаток, а через минуту брелок всё равно выключился бы. Экран гасит system_off.
  // Подключили USB, пока ждали кнопку, — system_off возвращается, и загрузка идёт дальше как
  // обычно: start_oled ниже заново включает экран (часы движка при этом на 2 с впереди — это
  // сдвигает только ближайшие моргания).
  if (cfg.empty_off && !hal::usb_present() && hal::vsys_volts() < 3.5f) {
    eng.set_expression(Expr::critical_battery, now, 0);
    eng.update(now + 2000);  // значки на лице уже проявились
    if (start_oled())
      for (int i = 0; i < 15; ++i) hal::watchdog_feed(), delay(100);
    hal::system_off();
  }

  if (sensor_bus) {
    acc.begin(Wire, board::kLis3dhAddr);
    als.begin(Wire, board::kLtr303Addr);
  }
  radio::begin();
  radio::set_ghost(cfg.ghost != 0);

  start_oled();
  next_acc = next_light = next_temp = next_batt = next_steps = now;
  next_health = now + 10000;
  next_fix = now;
  usb_seen = hal::usb_present();
  if (usb_seen) hal::usb_crystal(true);
  eng.play_scene(Scene::boot, now);
  voice.scene(Scene::boot, SceneParams{}, now);
  hal::watchdog_feed();

  out("\nБрелок с эмоциями, плата rev B, прошивка %s. Экран %s, акселерометр %s, свет %s\n", FW_VERSION,
      have_oled ? "есть" : "нет", acc.ok() ? "есть" : "нет", als.ok() ? "есть" : "нет");
  print_me();
  if (clock_lost) out_line("После выключения время и дата сброшены: задайте их командами t и date");
  if (!sensor_bus) out_line("Шина датчиков зажата: SDA или SCL в нуле");
  // Кнопку, которой разбудили, ещё держат: это не новое нажатие и не удержание для выключения.
  if (digitalRead(board::kButton) == LOW) {
    btn_down = long_fired = true;
    btn_change = btn_t0 = (uint32_t)uptime_now();
  }
}

void loop() {
  uptime = uptime_now();
  const uint32_t now = (uint32_t)uptime;
  hal::watchdog_feed();

  if (screen_on && !have_oled) {
    // Экран не отвечает: повторы всё реже (до минуты) — каждый стоит 40 мс и тайм-ауты шины.
    // Перед повтором модуль обесточивается на секунду: питание за ключом (R7·C11 и
    // конденсаторы модуля) спадает медленно, и только так зависший контроллер сбрасывается.
    // SDA и SCL в это время в нуле: через подтяжки модуля они помогают разрядить его питание.
    if (!oled_waiting) oled_waiting = true, oled_fail_at = now;
    if (!oled_cycling && now - oled_fail_at >= oled_retry_ms) {
      Wire1.end();
      digitalWrite(board::kDispEn, LOW);
      digitalWrite(PIN_WIRE1_SDA, LOW);
      digitalWrite(PIN_WIRE1_SCL, LOW);
      pinMode(PIN_WIRE1_SDA, OUTPUT);
      pinMode(PIN_WIRE1_SCL, OUTPUT);
      oled_cycling = true;
      oled_off_at = now;
    } else if (oled_cycling && now - oled_off_at >= 1000) {
      oled_cycling = false;
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
  if (!powering_off) check_sensors(now);
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
  // Контроллер, зажавший SDA посреди работы, по обмену не виден: экран только пишут, а ноль на
  // SDA читается как подтверждение. Между обменами обе линии шины в единице (вход ножки при
  // TWIM подключён); ноль две проверки подряд (раз в секунду) — экран считается пропавшим, и
  // повтор в начале loop перезапускает его питание.
  static uint32_t next_bus_check = 0;
  static uint8_t bus_low_n = 0;
  if (have_oled && screen_on && time_reached(now, next_bus_check)) {
    next_bus_check = now + 1000;
    const bool low = digitalRead(PIN_WIRE1_SDA) == LOW || digitalRead(PIN_WIRE1_SCL) == LOW;
    bus_low_n = low ? (uint8_t)(bus_low_n + 1) : 0;
    if (bus_low_n >= 2) {
      bus_low_n = 0;
      have_oled = false;
      out_line("Шина экрана зажата посреди работы: экран перезапускается");
    }
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
    sync_clock();  // счёт суток без часов продолжится со следующего включения
    save_all(now, true);
    acc.power(false);
    als.power(false);
    // Экран гаснет здесь, пока шина экрана работает: system_off выключает Wire1, и команда
    // экрану после него ждала бы в ядре без срока.
    screen_power(false, now);
    // Кнопку, которой выключали, держат и после прощания и подключают USB — это всё ещё жест
    // загрузчика: все 10 с от нажатия, как и на работающем брелоке. Часы и запись сохранены
    // выше. USB, который был и до этого, — не жест: тогда выключение идёт на зарядке.
    if (btn_down && !usb_seen) {
      while (digitalRead(board::kButton) == LOW && (uint32_t)uptime_now() - btn_t0 < kDfuPressMs) {
        hal::watchdog_feed();
        if (hal::usb_present()) hal::enter_bootloader();
        delay(10);
      }
    }
    // Выключение на USB и полный сон по очереди: пока ждали кнопку, USB могли подключить
    // (system_off тогда возвращается), а вернётся off_on_usb, только когда USB отключат.
    for (;;) {
      if (hal::usb_present()) off_on_usb();
      hal::system_off();
    }
  }
  delay(10);  // процессор спит до следующего прохода
}
