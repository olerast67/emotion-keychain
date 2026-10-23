// Тестовый стенд: ESP32 + SSD1306 128×64 по I2C и, по желанию, сенсорная кнопка TTP223
// и пассивная пищалка (голос брелока, sound.h).
//
// Режимы:
//   «живой»  — работает характер (Behavior): моргает, скучает, реагирует на кнопку;
//   «показ»  — по кругу все эмоции каталога, имя каждой печатается в Serial;
//   «ручной» — одна эмоция, выбранная командой из Serial.
//
// Кнопка: короткое касание — тап (в показе — следующая эмоция), два быстрых —
// двойной тап, удержание 1.5 с — переключить «живой» ↔ «показ».
// Команды Serial — `?` выводит список.
//
// Персонаж выводится из серийного номера чипа (identity.h), прогресс роста,
// тема и день рождения хранятся во флеше (Preferences) и переживают перезагрузку.
//
// millis() переполняется раз в 49.7 суток: сроки сравниваются только через
// time_reached, а часы и счёт дней идут по 64-битному времени работы.
#include <Arduino.h>
#include <Preferences.h>
#include <Wire.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face/behavior.h"
#include "face/engine.h"
#include "face/identity.h"
#include "face/progress.h"
#include "face/sound.h"
#include "ssd1306.h"

#ifndef PIN_SDA
#define PIN_SDA 21
#endif
#ifndef PIN_SCL
#define PIN_SCL 22
#endif
#ifndef PIN_BUTTON
#define PIN_BUTTON 4  // -1 — без кнопки
#endif
#ifndef PIN_BOOT
#define PIN_BOOT -1  // кнопка BOOT на плате: работает как вторая кнопка, без проводов
#endif
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW 0  // TTP223 по умолчанию даёт 1 при касании; 1 — обычная кнопка на GND
#endif
#ifndef I2C_HZ
#define I2C_HZ 400000
#endif
#ifndef PIN_BUZZER
#define PIN_BUZZER -1  // пассивная пищалка (пьезо) между выводом и GND; -1 — без неё
#endif

using namespace face;

namespace {

enum class Mode : uint8_t { Alive, Showcase, Manual };
const char* const kModeName[] = {"живой", "показ", "ручной"};

constexpr uint64_t kDayMs = 24ull * 3600000ull;
constexpr uint32_t kOledRetryMs = 2000;  // как часто искать пропавший экран

FaceEngine eng;
Behavior bh;
Voice voice;
Ssd1306 oled;
bool have_oled = false;
bool oled_full = false;  // прошлая отправка сорвалась: нужен весь кадр целиком
uint32_t oled_retry_at = 0;
bool use_sh1106 = false;
uint8_t contrast = 0x7F;  // яркость пользователя (b, +, -): её получает обычный уровень
bool auto_dim = true;  // ночью, во сне и в темноте экран тусклее
uint8_t shown_contrast = 0;

Mode mode = Mode::Alive;
int showcase_i = 0;
uint32_t showcase_ms = 3500;
uint32_t showcase_next = 0;
Expr shown = Expr::Count;

// Часов реального времени на стенде нет: стартуем с полудня, команда `t` переставляет.
uint64_t uptime = 0;  // время работы в мс, без переполнения
uint32_t clock_base_min = 12 * 60;
uint64_t clock_base_up = 0;

// Статистика за окно в 10 секунд.
bool verbose = false;
uint32_t stat_t0 = 0, stat_frames = 0, stat_bytes = 0, stat_render_us = 0, stat_send_us = 0;

char line[64];
size_t line_n = 0;
bool line_overflow = false;  // строка длиннее буфера: целиком пропускается

// Персонаж, рост и настройки.
Identity me;
uint64_t chip_serial = 0;
Progress progress;
Progress saved;  // что лежит во флеше
Preferences prefs;
uint32_t last_save = 0;
uint64_t day_t0 = 0;  // начало текущих «суток вместе» по времени работы
uint8_t bday_m = 0, bday_d = 0;

// Пищалка: частота меняется только на сменах нот. Тихий режим (ночь, скромник) — узкий импульс.
bool sound_on = true;
uint16_t buzzer_hz = 0;
bool buzzer_soft = false;
uint32_t sounds_seen = 0;

void buzzer(uint16_t hz, bool soft) {
  if (PIN_BUZZER < 0 || (hz == buzzer_hz && soft == buzzer_soft)) return;
  buzzer_hz = hz;
  buzzer_soft = soft;
  ledcWriteTone(PIN_BUZZER, hz);  // 0 — тишина
  if (hz && soft) ledcWrite(PIN_BUZZER, 128);  // скважность 1/8 вместо 1/2: примерно на 9 дБ тише
}

// millis() с учётом переполнений. Вызывается каждый проход цикла, так что ни одно не теряется.
uint64_t uptime_ms(uint32_t now) {
  static uint32_t last = 0, wraps = 0;
  if (now < last) ++wraps;
  last = now;
  return (uint64_t)wraps << 32 | now;
}

unsigned clock_minutes() { return (unsigned)((clock_base_min + (uptime - clock_base_up) / 60000u) % 1440u); }

// ---------------------------------------------------------------- экран

// Экран перестал отвечать: перестаём слать кадры и ищем его снова раз в пару секунд.
void display_lost(uint32_t now) {
  if (have_oled) Serial.println("Экран не отвечает — ищу его снова");
  have_oled = false;
  oled_retry_at = now + kOledRetryMs;
}

void push_frame() {
  DirtyWindow w[kPages];
  const int n = eng.dirty(w);
  const uint32_t t0 = micros();
  bool ok = true;
  for (int i = 0; i < n && ok; ++i) ok = oled.send_window(eng.frame(), w[i].page, w[i].x0, w[i].x1);
  stat_send_us += micros() - t0;
  stat_bytes += (uint32_t)dirty_bytes(w, n);
  ++stat_frames;
  if (!ok) oled_full = true;  // часть кадра могла не дойти — следом уйдёт весь кадр
}

void scan_i2c() {
  Serial.print("I2C:");
  int found = 0;
  for (uint8_t a = 1; a < 127; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      ++found;
    }
  }
  Serial.println(found ? "" : " никого нет — проверь провода VCC, GND, SCL, SDA");
}

// Нужный сейчас контраст: обычный уровень — яркость пользователя, приглушённый и
// тёмный — её доли. В показе, в ручном режиме и с выключенной автояркостью — как задано.
uint8_t wanted_contrast() {
  if (mode != Mode::Alive || !auto_dim) return contrast;
  switch (bh.brightness()) {
    case Brightness::Dim: return (uint8_t)(contrast / 4);
    case Brightness::Dark: return (uint8_t)(contrast / 16);
    default: return contrast;
  }
}

// Экран сначала (или снова) включается: инициализация, яркость и весь кадр.
bool start_oled() {
  have_oled = oled.begin(Wire, 0, use_sh1106);
  if (!have_oled) return false;
  shown_contrast = wanted_contrast();
  if (!oled.contrast(shown_contrast) || !oled.send_full(eng.frame())) {
    have_oled = false;
    return false;
  }
  oled_full = false;
  Serial.printf("Экран %s на 0x%02X\n", use_sh1106 ? "SH1106" : "SSD1306", oled.address());
  return true;
}

// Контраст уходит на шину только при смене уровня или яркости пользователя.
void apply_brightness(uint32_t now) {
  if (!have_oled) return;
  const uint8_t c = wanted_contrast();
  if (c == shown_contrast) return;
  if (oled.contrast(c))
    shown_contrast = c;
  else
    display_lost(now);
}

// ---------------------------------------------------------------- персонаж и флеш

void save_progress(uint32_t now, bool force) {
  // Сравниваем без контрольной суммы: она меняется только при записи.
  if (!force && memcmp(&progress, &saved, offsetof(Progress, crc)) == 0) return;
  const bool unlocks = progress.implant != saved.implant || progress.flags != saved.flags ||
                       memcmp(progress.extra, saved.extra, sizeof progress.extra) != 0;
  if (!force && !unlocks && now - last_save < 60000u) return;  // флеш бережём
  progress.seal();
  prefs.putBytes("progress", &progress, sizeof(progress));
  saved = progress;
  last_save = now;
}

void load_settings(uint32_t now) {
  prefs.begin("keychain", false);
  // Запись любой известной версии; старая сразу переписывается в текущем формате.
  uint8_t blob[sizeof(Progress)];
  const size_t n = prefs.getBytesLength("progress");
  const bool ok =
      n > 0 && n <= sizeof(blob) && prefs.getBytes("progress", blob, n) == n && progress.load(blob, (uint32_t)n);
  if (!ok) {
    progress.reset();
    Serial.println("Новый брелок: открыта база эмоций");
  }
  saved = progress;
  if (ok && n != sizeof(Progress)) save_progress(now, true);
  eng.options().theme = prefs.getUChar("theme", 0) ? Theme::Cyber : Theme::Classic;
  sound_on = prefs.getUChar("sound", 1) != 0;
  voice.set_enabled(sound_on);
  bday_m = prefs.getUChar("bday_m", 0);
  bday_d = prefs.getUChar("bday_d", 0);
  if (bday_m) bh.set_birthday(bday_m, bday_d);
}

void apply_identity(uint64_t serial, uint32_t now) {
  me = make_identity(serial);
  bh.personality() = me.personality;
  bh.set_temperament(&temperament(me.archetype));
  bh.set_identity_hash(me.hash);
  voice.init(&temperament(me.archetype), me.hash);
  voice.set_enabled(sound_on);
  eng.set_quirks(me.quirks, now);
}

void print_me() {
  const Personality& p = me.personality;
  const Temperament& t = temperament(me.archetype);
  Serial.printf("Я %s: %s (%s), %s%s\n", me.code, t.name, t.tag, rarity_name(me.rarity),
                me.quirks.cyber_eye ? ", золотой кибер-глаз" : "");
  Serial.printf("В покое — %s, рад — %s, скучает — %s; на касание — %s, %s…\n", expression(t.rest).id,
                expression(t.rest_happy).id, expression(t.rest_bored).id, expression(t.tap[0]).id,
                expression(t.tap[1]).id);
  Serial.printf("Характер: игривость %.2f, ворчливость %.2f, сонливость %.2f, застенчивость %.2f\n", p.playful,
                p.grumpy, p.sleepy, p.shy);
  const Implant& cur = implant(progress.implant);
  Serial.printf("Рост: день %u, опыт %u, имплант %d/%d «%s», открыто %d/%d эмоций", progress.days,
                (unsigned)progress.xp, progress.implant, implant_count() - 1, cur.name, progress.unlocked_count(),
                kExprCount);
  if (progress.xp_to_next())
    Serial.printf(", до «%s» ещё %u опыта\n", implant(progress.implant + 1).name, (unsigned)progress.xp_to_next());
  else
    Serial.println(", открыто всё");
  Serial.printf("Касаний %u, шагов %u, друзей %u, дуэлей %u/%u, тема %s\n", (unsigned)progress.touches,
                (unsigned)progress.steps, progress.friends, progress.duels_won,
                progress.duels_won + progress.duels_lost,
                eng.options().theme == Theme::Cyber ? "киберпанк" : "классика");
}

// ---------------------------------------------------------------- эмоции

void print_expr(Expr e) {
  const Expression& x = expression(e);
  Serial.printf("[%3d/%d] %-16s %s · %s\n", (int)e + 1, kExprCount, x.id, x.name, category_name(x.cat));
}

void show(Expr e, uint32_t now) {
  eng.set_expression(e, now);
  showcase_i = (int)e;
}

void set_mode(Mode m, uint32_t now) {
  mode = m;
  bh.set_auto(m == Mode::Alive);
  if (m == Mode::Showcase) {
    show((Expr)showcase_i, now);
    showcase_next = now + showcase_ms;
  }
  Serial.printf("Режим: %s\n", kModeName[(int)m]);
}

void step(int d, uint32_t now) {
  showcase_i = (showcase_i + d + kExprCount) % kExprCount;
  if (mode == Mode::Alive) set_mode(Mode::Manual, now);
  show((Expr)showcase_i, now);
  showcase_next = now + showcase_ms;
}

bool find_clip(const char* s, Clip* out) {
  for (int i = 0; i < kClipCount; ++i)
    if (!strcmp(s, clip_id((Clip)i))) return *out = (Clip)i, true;
  return false;
}

bool find_event(const char* s, Event* out) {
  for (int i = 0; i < kEventCount; ++i)
    if (!strcmp(s, event_id((Event)i))) return *out = (Event)i, true;
  return false;
}

// Целое число из команды: без мусора в хвосте и в пределах [lo, hi].
bool parse_int(const char* s, long lo, long hi, long* out) {
  if (!s || !*s) return false;
  char* end = nullptr;
  const long v = strtol(s, &end, 10);
  if (end == s) return false;
  while (*end == ' ') ++end;
  if (*end || v < lo || v > hi) return false;
  *out = v;
  return true;
}

void list_all() {
  for (int i = 0; i < kExprCount; ++i) print_expr((Expr)i);
  Serial.print("Клипы:");
  for (int i = 0; i < kClipCount; ++i) Serial.printf(" %s", clip_id((Clip)i));
  Serial.print("\nСобытия:");
  for (int i = 0; i < kEventCount; ++i) Serial.printf(" %s", event_id((Event)i));
  Serial.println();
}

void help() {
  Serial.println(
      "Команды:\n"
      "  laugh        показать эмоцию по id (или по номеру: 42)\n"
      "  n / p        следующая / предыдущая эмоция\n"
      "  s [мс]       показ всех эмоций по кругу, можно задать время на каждую\n"
      "  a            живой режим (характер сам выбирает эмоции)\n"
      "  e <событие>  событие для характера: e tap, e shake, e music, e dark …\n"
      "  c <клип>     жест: c blink, c sneeze, c glitch …\n"
      "  + / -        яркость, b <0..255> — точное значение\n"
      "  dim          автояркость: ночью, во сне и в темноте тусклее (вкл/выкл)\n"
      "  u            перевернуть, r — ретро-строки, d — дрейф\n"
      "  h            переключить SSD1306 ↔ SH1106 (для модулей 1.3\")\n"
      "  t ЧЧ:ММ      время суток для характера\n"
      "  l            список эмоций, клипов и событий\n"
      "  v            раз в 10 с печатать кадры, байты и время\n"
      "  i            что сейчас на экране\n"
      "Характер и рост:\n"
      "  me           кто я: характер, редкость, опыт, импланты\n"
      "  theme cyber  тема оформления: cyber или classic\n"
      "  x <сцена>    сцена: boot hack data_rain error404 reboot hide upgrade\n"
      "               new_year birthday halloween greet duel chat\n"
      "  surprise     случайный сюрприз прямо сейчас\n"
      "  xp 200       добавить опыт; day — новый день; steps 3000 — шаги\n"
      "  meet [old]   рядом другой брелок (новый или знакомый)\n"
      "  duel win     дуэль с другим брелоком: win или lose\n"
      "  date 2026-12-31   дата для праздников; bday 05-14 — твой день рождения\n"
      "Звук:\n"
      "  snd          пищалка вкл/выкл (сохраняется)\n"
      "  say laugh    реплика: boop happy laugh sad angry surprise scared love sleepy\n"
      "               hello goodbye question idea error charge_start charge_full\n"
      "               battery_low level_up friend_hi duel win lose boot\n"
      "  chat         секретный разговор шифром с соседом (слышны обе стороны)\n"
      "  tilt 1 0     наклон: зрачки скатываются в сторону; jerk -1 0 — рывок\n"
      "  serial 42    посмотреть другой брелок; serial — вернуть свой\n"
      "  unlock all   открыть всё; reset yes — начать рост заново");
}

void info() {
  print_expr(eng.expression());
  const Mood& m = bh.mood();
  const unsigned mins = clock_minutes();
  Serial.printf("Режим %s, яркость %u (на экране %u), время %02u:%02u, причина: %s\n", kModeName[(int)mode], contrast,
                shown_contrast, mins / 60, mins % 60, bh.last_reason());
  Serial.printf("Настроение: радость %.2f, возбуждение %.2f, бодрость %.2f, скука %.2f, раздражение %.2f\n", m.valence,
                m.arousal, m.energy, m.boredom, m.annoyance);
}

// ---------------------------------------------------------------- Serial

void command(char* s, uint32_t now) {
  while (*s == ' ') ++s;
  char* arg = strchr(s, ' ');
  if (arg) {
    *arg++ = 0;
    while (*arg == ' ') ++arg;
    if (!*arg) arg = nullptr;
  }
  if (!*s) return;
  eng.note_activity(now);  // команда — тоже внимание: лицо снова оживлённое

  if (!strcmp(s, "?") || !strcmp(s, "help")) return help();
  if (!strcmp(s, "l")) return list_all();
  if (!strcmp(s, "i")) return info();
  if (!strcmp(s, "n")) return step(1, now);
  if (!strcmp(s, "p")) return step(-1, now);
  if (!strcmp(s, "a")) return set_mode(Mode::Alive, now);
  if (!strcmp(s, "s")) {
    long ms = 0;
    if (arg && !parse_int(arg, 500, 600000, &ms)) return (void)Serial.println("Формат: s 1500 (500…600000 мс)");
    if (arg) showcase_ms = (uint32_t)ms;
    return set_mode(Mode::Showcase, now);
  }
  if (!strcmp(s, "e")) {
    Event ev;
    if (!arg || !find_event(arg, &ev)) return (void)Serial.println("Нет такого события, список — l");
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    bh.on_event(ev, now);
    return;
  }
  if (!strcmp(s, "c")) {
    Clip c;
    if (!arg || !find_clip(arg, &c)) return (void)Serial.println("Нет такого клипа, список — l");
    eng.play_clip(c, now);
    return;
  }
  if (!strcmp(s, "+") || !strcmp(s, "-") || !strcmp(s, "b")) {
    long v = contrast;
    if (*s == '+')
      v = v < 16 ? 16 : v * 2;
    else if (*s == '-')
      v /= 2;
    else if (!arg || !parse_int(arg, 0, 255, &v))
      return (void)Serial.println("Формат: b 0…255");
    contrast = (uint8_t)(v > 255 ? 255 : v);
    apply_brightness(now);
    Serial.printf("Яркость %u\n", contrast);
    return;
  }
  if (!strcmp(s, "dim")) {
    auto_dim = arg ? !strcmp(arg, "on") : !auto_dim;
    apply_brightness(now);
    Serial.println(auto_dim ? "Автояркость включена" : "Автояркость выключена");
    return;
  }
  if (!strcmp(s, "u")) {
    eng.options().upside_down = !eng.options().upside_down;
    Serial.println(eng.options().upside_down ? "Вверх ногами" : "Нормально");
    return;
  }
  if (!strcmp(s, "r")) {
    eng.options().scanlines = !eng.options().scanlines;
    Serial.println(eng.options().scanlines ? "Ретро-строки включены" : "Ретро-строки выключены");
    return;
  }
  if (!strcmp(s, "d")) {
    eng.options().drift = !eng.options().drift;
    Serial.println(eng.options().drift ? "Дрейф включён" : "Дрейф выключен");
    return;
  }
  if (!strcmp(s, "h")) {
    use_sh1106 = !use_sh1106;
    if (!start_oled()) {
      Serial.println("Экран не ответил");
      oled_retry_at = now + kOledRetryMs;
    }
    return;
  }
  if (!strcmp(s, "t")) {
    int hh = -1, mm = -1;
    char tail = 0;
    if (!arg || sscanf(arg, "%d:%d%c", &hh, &mm, &tail) != 2 || hh < 0 || hh > 23 || mm < 0 || mm > 59)
      return (void)Serial.println("Формат: t 23:30");
    clock_base_min = (uint32_t)(hh * 60 + mm);
    clock_base_up = uptime;
    Serial.printf("Время %02d:%02d\n", hh, mm);
    return;
  }
  if (!strcmp(s, "v")) {
    verbose = !verbose;
    stat_t0 = now, stat_frames = stat_bytes = stat_render_us = stat_send_us = 0;
    return;
  }

  if (!strcmp(s, "me")) return print_me();
  if (!strcmp(s, "theme")) {
    const bool cyber = arg && !strcmp(arg, "cyber");
    eng.options().theme = cyber ? Theme::Cyber : Theme::Classic;
    prefs.putUChar("theme", cyber ? 1 : 0);
    eng.set_expression(eng.expression(), now);
    Serial.println(cyber ? "Тема: киберпанк" : "Тема: классика");
    return;
  }
  if (!strcmp(s, "x")) {
    Scene sc;
    if (!arg || !scene_from_id(arg, &sc)) return (void)Serial.println("Нет такой сцены, список — ?");
    SceneParams p;
    const Identity peer = make_identity(now);
    if (sc == Scene::upgrade)
      p.set_text(implant(progress.implant + 1 < implant_count() ? progress.implant + 1 : 1).tag);
    if (sc == Scene::greet || sc == Scene::duel || sc == Scene::chat) p.set_text(peer.code);
    if (sc == Scene::duel) p.value = (int)(now & 1u);
    if (sc == Scene::chat) {
      p.key = chat_key(me.hash, peer.hash, now);
      p.value = chat_first(me.hash, peer.hash) ? 1 : 0;
    }
    eng.play_scene(sc, now, p);
    if (sc == Scene::chat)
      voice.chat(p.key, p.value != 0, now, &temperament(peer.archetype));
    else
      voice.scene(sc, p, now);
    Serial.printf("Сцена: %s\n", scene_name(sc));
    return;
  }
  if (!strcmp(s, "surprise")) {
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    return bh.surprise(now);
  }
  if (!strcmp(s, "xp") || !strcmp(s, "day") || !strcmp(s, "steps")) {
    long n = *s == 'x' ? 100 : 1000;
    if (*s == 'x' && arg && !parse_int(arg, 1, 100000, &n)) return (void)Serial.println("Формат: xp 200 (1…100000)");
    if (*s == 's' && arg && !parse_int(arg, 1, 1000000, &n))
      return (void)Serial.println("Формат: steps 3000 (1…1000000)");
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    if (*s == 'x')
      bh.add_xp((uint32_t)n, now);
    else if (*s == 'd')
      bh.new_day(now);
    else
      bh.add_steps((uint32_t)n, now);
    save_progress(now, true);
    return print_me();
  }
  if (!strcmp(s, "meet")) {
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    const Identity peer = make_identity(now);
    bh.meet(peer.code, !(arg && !strcmp(arg, "old")), now, peer.hash);
    save_progress(now, true);
    return;
  }
  if (!strcmp(s, "duel")) {
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    // После дуэли — разговор шифром: на стенде соседа нет, его реплики играет та же пищалка.
    const Identity peer = make_identity(now ^ 0x55u);
    bh.duel(peer.code, !(arg && !strcmp(arg, "lose")), now, peer.hash, now);
    save_progress(now, true);
    return;
  }
  if (!strcmp(s, "chat")) {
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    const Identity peer = make_identity(now ^ 0xC4A7u);
    Serial.printf("Разговор с %s (%s)\n", peer.code, archetype_name(peer.archetype));
    bh.chat(peer.code, peer.hash, now, now);
    return;
  }
  if (!strcmp(s, "snd")) {
    sound_on = arg ? !strcmp(arg, "on") : !sound_on;
    voice.set_enabled(sound_on);
    prefs.putUChar("sound", sound_on ? 1 : 0);
    Serial.println(PIN_BUZZER < 0 ? (sound_on ? "Звук включён, но пищалки нет: задайте -DPIN_BUZZER"
                                              : "Звук выключен")
                                  : (sound_on ? "Звук включён" : "Звук выключен"));
    return;
  }
  if (!strcmp(s, "say")) {
    Cue c;
    if (!arg || !cue_from_id(arg, &c)) return (void)Serial.println("Нет такой реплики, список — ?");
    if (!voice.say(c, now)) Serial.println(sound_on ? "Пищалка занята или тихие часы" : "Звук выключен: snd");
    return;
  }
  if (!strcmp(s, "date")) {
    int y = 0, mo = 0, d = 0;
    char tail = 0;
    if (!arg || sscanf(arg, "%d-%d-%d%c", &y, &mo, &d, &tail) != 3 || y < 2000 || y > 2099 || mo < 1 || mo > 12 ||
        d < 1 || d > 31)
      return (void)Serial.println("Формат: date 2026-12-31");
    if (mode != Mode::Alive) set_mode(Mode::Alive, now);
    bh.set_date((uint16_t)y, (uint8_t)mo, (uint8_t)d);
    Serial.printf("Дата %04d-%02d-%02d\n", y, mo, d);
    return;
  }
  if (!strcmp(s, "bday")) {
    int mo = 0, d = 0;
    char tail = 0;
    if (!arg || sscanf(arg, "%d-%d%c", &mo, &d, &tail) != 2 || mo < 1 || mo > 12 || d < 1 || d > 31)
      return (void)Serial.println("Формат: bday 05-14");
    bday_m = (uint8_t)mo, bday_d = (uint8_t)d;
    bh.set_birthday(bday_m, bday_d);
    prefs.putUChar("bday_m", bday_m);
    prefs.putUChar("bday_d", bday_d);
    Serial.printf("День рождения: %02d-%02d\n", mo, d);
    return;
  }
  if (!strcmp(s, "tilt") || !strcmp(s, "jerk")) {
    // NaN и бесконечности движок отбрасывает сам.
    float x = 0, y = 0;
    if (!arg || sscanf(arg, "%f %f", &x, &y) != 2) return (void)Serial.println("Формат: tilt 1 0 или jerk -1 0");
    if (*s == 't')
      eng.set_tilt(x, y);
    else
      eng.jerk(x, y, now);
    return;
  }
  if (!strcmp(s, "serial")) {
    uint64_t serial = chip_serial;
    if (arg) {
      char* end = nullptr;
      serial = (uint64_t)strtoull(arg, &end, 10);
      if (end == arg || *end) return (void)Serial.println("Формат: serial 42");
    }
    apply_identity(serial, now);
    return print_me();
  }
  if (!strcmp(s, "unlock") && arg && !strcmp(arg, "all")) {
    progress.unlock_all();
    save_progress(now, true);
    return print_me();
  }
  if (!strcmp(s, "reset") && arg && !strcmp(arg, "yes")) {
    progress.reset();
    save_progress(now, true);
    return print_me();
  }

  // Иначе это эмоция: по id или по номеру из списка.
  Expr e;
  long num = 0;
  if (parse_int(s, 1, kExprCount, &num))
    e = (Expr)(num - 1);
  else if (!expr_from_id(s, &e))
    return (void)Serial.println("Не знаю такую команду или эмоцию, помощь — ?");
  if (mode != Mode::Manual) set_mode(Mode::Manual, now);
  show(e, now);
}

void poll_serial(uint32_t now) {
  while (Serial.available()) {
    const char ch = (char)Serial.read();
    if (ch == '\r' || ch == '\n') {
      line[line_n] = 0;
      if (line_overflow)
        Serial.printf("Строка длиннее %u символов — пропускаю\n", (unsigned)(sizeof(line) - 1));
      else if (line_n)
        command(line, now);
      line_n = 0;
      line_overflow = false;
    } else if (line_n + 1 < sizeof(line)) {
      line[line_n++] = ch;
    } else {
      line_overflow = true;  // хвост не дописываем: обрезанная команда могла бы значить другое
    }
  }
}

// ---------------------------------------------------------------- кнопка

bool btn_down = false;
uint32_t btn_t0 = 0, btn_change = 0, tap_pending_at = 0;
bool long_fired = false, tap_pending = false;

void on_short(uint32_t now, bool twice) {
  Serial.println(twice ? "Кнопка: двойной тап" : "Кнопка: тап");
  eng.note_activity(now);
  if (mode == Mode::Alive) {
    bh.on_event(twice ? Event::double_tap : Event::tap, now);
  } else {
    step(1, now);
  }
}

void poll_button(uint32_t now) {
  if (PIN_BUTTON < 0 && PIN_BOOT < 0) return;
  bool raw = PIN_BUTTON >= 0 && digitalRead(PIN_BUTTON) == (BUTTON_ACTIVE_LOW ? LOW : HIGH);
  if (PIN_BOOT >= 0 && digitalRead(PIN_BOOT) == LOW) raw = true;
  if (raw != btn_down && now - btn_change > 25) {  // антидребезг
    btn_down = raw;
    btn_change = now;
    if (raw) {
      btn_t0 = now;
      long_fired = false;
    } else if (!long_fired) {
      // Ждём второе касание, чтобы отличить двойной тап.
      if (tap_pending && now - tap_pending_at < 350) {
        tap_pending = false;
        on_short(now, true);
      } else {
        tap_pending = true;
        tap_pending_at = now;
      }
    }
  }
  if (btn_down && !long_fired && now - btn_t0 >= 1500) {
    long_fired = true;
    tap_pending = false;
    eng.note_activity(now);
    set_mode(mode == Mode::Alive ? Mode::Showcase : Mode::Alive, now);
  }
  if (tap_pending && !btn_down && now - tap_pending_at >= 350) {
    tap_pending = false;
    on_short(now, false);
  }
}

}  // namespace

// ---------------------------------------------------------------- setup / loop

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 1500) delay(10);  // USB CDC на C3/S3 появляется не сразу

  if (PIN_BUTTON >= 0) pinMode(PIN_BUTTON, BUTTON_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  if (PIN_BOOT >= 0) pinMode(PIN_BOOT, INPUT_PULLUP);
  if (PIN_BUZZER >= 0) ledcAttach(PIN_BUZZER, 2000, 10);
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ);

  const uint32_t now = millis();
  uptime = uptime_ms(now);
  const uint32_t seed = (uint32_t)esp_random();
  eng.init(seed, now);
  bh.init(&eng, seed ^ 0x9E3779B9u, now);
  bh.attach_voice(&voice);
  bh.set_chat_both_sides(true);  // на стенде один брелок: реплики соседа играет та же пищалка
  clock_base_up = uptime;
  day_t0 = uptime;

  load_settings(now);
  bh.attach_progress(&progress);
  chip_serial = ESP.getEfuseMac();
  apply_identity(chip_serial, now);
  if (progress.days == 0) bh.new_day(now);  // первый день вместе
  eng.play_scene(Scene::boot, now);
  voice.scene(Scene::boot, SceneParams{}, now);

  Serial.printf("\nБрелок с эмоциями — стенд. SDA %d, SCL %d, кнопка %d, BOOT %d, пищалка %d, I2C %d кГц\n", PIN_SDA,
                PIN_SCL, PIN_BUTTON, PIN_BOOT, PIN_BUZZER, I2C_HZ / 1000);
  if (!start_oled()) {
    Serial.println("Экран не найден.");
    scan_i2c();
    oled_retry_at = now + kOledRetryMs;
  }
  help();
  print_me();
  stat_t0 = now;
}

void loop() {
  const uint32_t now = millis();
  uptime = uptime_ms(now);

  // Экрана нет или он пропал: ищем его раз в пару секунд, нашёлся — инициализация и весь кадр.
  if (!have_oled && time_reached(now, oled_retry_at)) {
    oled_retry_at = now + kOledRetryMs;
    start_oled();
  }

  poll_serial(now);
  poll_button(now);

  bh.set_clock((uint16_t)clock_minutes());
  // Часов нет: «день вместе» — каждые сутки работы.
  if (uptime - day_t0 >= kDayMs) {
    day_t0 += kDayMs;
    bh.new_day(now);
  }
  save_progress(now, false);
  // Поведение обновляется в любом режиме: без автоматики оно только ведёт настроение
  // и держит свои сроки свежими, а включённое снова — сразу в деле.
  if (time_reached(now, bh.next_update_ms())) bh.update(now);
  if (mode == Mode::Showcase && time_reached(now, showcase_next)) {
    showcase_next = now + showcase_ms;
    show((Expr)((showcase_i + 1) % kExprCount), now);
  }

  if (time_reached(now, eng.next_update_ms())) {
    const uint32_t t0 = micros();
    const bool changed = eng.update(now);
    stat_render_us += micros() - t0;
    if (changed && have_oled) push_frame();
  }
  if (have_oled && oled_full) {
    // Прошлая отправка сорвалась: шлём кадр целиком. Не вышло и так — экран пропал.
    if (oled.send_full(eng.frame()))
      oled_full = false;
    else
      display_lost(now);
  }
  apply_brightness(now);
  buzzer(voice.tone(now), voice.soft());
  if (voice.started() != sounds_seen) {
    sounds_seen = voice.started();
    const Cue c = voice.last_cue();
    Serial.printf("Звук: %s%s\n", c == Cue::Count ? "музыка сцены" : cue_id(c), voice.soft() ? " (тихо)" : "");
  }

  static Scene shown_scene = Scene::None;
  if (eng.scene() != shown_scene) {
    shown_scene = eng.scene();
    if (shown_scene != Scene::None) Serial.printf("Сцена: %s\n", scene_name(shown_scene));
  }
  if (eng.expression() != shown) {
    shown = eng.expression();
    print_expr(shown);
  }

  if (now - stat_t0 >= 10000) {
    if (verbose) {
      const float s = (now - stat_t0) / 1000.0f;
      Serial.printf("За %.0f с: %.1f кадр/с, %.0f байт/с, движок %.2f мс/кадр, шина %.2f мс/кадр\n", s, stat_frames / s,
                    stat_bytes / s, stat_frames ? stat_render_us / 1000.0f / stat_frames : 0.0f,
                    stat_frames ? stat_send_us / 1000.0f / stat_frames : 0.0f);
    }
    stat_t0 = now, stat_frames = stat_bytes = stat_render_us = stat_send_us = 0;
  }

  delay(1);
}
