// Тестовый стенд: ESP32 + SSD1306 128×64 по I2C и, по желанию, сенсорная кнопка TTP223.
//
// Режимы:
//   «живой»  — работает характер (Behavior): моргает, скучает, реагирует на кнопку;
//   «показ»  — по кругу все эмоции каталога, имя каждой печатается в Serial;
//   «ручной» — одна эмоция, выбранная командой из Serial.
//
// Кнопка: короткое касание — тап (в показе — следующая эмоция), два быстрых —
// двойной тап, удержание 1.5 с — переключить «живой» ↔ «показ».
// Команды Serial — `?` выводит список.
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "face/behavior.h"
#include "face/engine.h"
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
#ifndef BUTTON_ACTIVE_LOW
#define BUTTON_ACTIVE_LOW 0  // TTP223 по умолчанию даёт 1 при касании; 1 — обычная кнопка на GND
#endif
#ifndef I2C_HZ
#define I2C_HZ 400000
#endif

using namespace face;

namespace {

enum class Mode : uint8_t { Alive, Showcase, Manual };
const char* const kModeName[] = {"живой", "показ", "ручной"};

FaceEngine eng;
Behavior bh;
Ssd1306 oled;
bool have_oled = false;
bool use_sh1106 = false;
uint8_t contrast = 0x7F;

Mode mode = Mode::Alive;
int showcase_i = 0;
uint32_t showcase_ms = 3500;
uint32_t showcase_next = 0;
Expr shown = Expr::Count;

// Часов реального времени на стенде нет: стартуем с полудня, команда `t` переставляет.
uint32_t clock_base_min = 12 * 60;
uint32_t clock_base_ms = 0;

// Статистика за окно в 10 секунд.
bool verbose = false;
uint32_t stat_t0 = 0, stat_frames = 0, stat_bytes = 0, stat_render_us = 0, stat_send_us = 0;

char line[64];
size_t line_n = 0;

// ---------------------------------------------------------------- экран

void push_frame() {
  DirtyWindow w[kPages];
  const int n = eng.dirty(w);
  const uint32_t t0 = micros();
  for (int i = 0; i < n; ++i) oled.send_window(eng.frame(), w[i].page, w[i].x0, w[i].x1);
  stat_send_us += micros() - t0;
  stat_bytes += (uint32_t)dirty_bytes(w, n);
  ++stat_frames;
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

bool start_oled() {
  have_oled = oled.begin(Wire, 0, use_sh1106);
  if (!have_oled) return false;
  oled.contrast(contrast);
  oled.send_full(eng.frame());
  Serial.printf("Экран %s на 0x%02X\n", use_sh1106 ? "SH1106" : "SSD1306", oled.address());
  return true;
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
      "  u            перевернуть, r — ретро-строки, d — дрейф\n"
      "  h            переключить SSD1306 ↔ SH1106 (для модулей 1.3\")\n"
      "  t ЧЧ:ММ      время суток для характера\n"
      "  l            список эмоций, клипов и событий\n"
      "  v            раз в 10 с печатать кадры, байты и время\n"
      "  i            что сейчас на экране");
}

void info(uint32_t now) {
  print_expr(eng.expression());
  const Mood& m = bh.mood();
  Serial.printf("Режим %s, яркость %u, время %02u:%02u, причина: %s\n", kModeName[(int)mode], contrast,
                (unsigned)(clock_base_min + (now - clock_base_ms) / 60000) / 60 % 24,
                (unsigned)(clock_base_min + (now - clock_base_ms) / 60000) % 60, bh.last_reason());
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
  }
  if (!*s) return;

  if (!strcmp(s, "?") || !strcmp(s, "help")) return help();
  if (!strcmp(s, "l")) return list_all();
  if (!strcmp(s, "i")) return info(now);
  if (!strcmp(s, "n")) return step(1, now);
  if (!strcmp(s, "p")) return step(-1, now);
  if (!strcmp(s, "a")) return set_mode(Mode::Alive, now);
  if (!strcmp(s, "s")) {
    if (arg && atoi(arg) >= 500) showcase_ms = (uint32_t)atoi(arg);
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
    int v = contrast;
    if (*s == '+')
      v = v < 16 ? 16 : v * 2;
    else if (*s == '-')
      v /= 2;
    else if (arg)
      v = atoi(arg);
    contrast = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    if (have_oled) oled.contrast(contrast);
    Serial.printf("Яркость %u\n", contrast);
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
    if (!start_oled()) Serial.println("Экран не ответил");
    return;
  }
  if (!strcmp(s, "t")) {
    int hh = 0, mm = 0;
    if (!arg || sscanf(arg, "%d:%d", &hh, &mm) < 1) return (void)Serial.println("Формат: t 23:30");
    clock_base_min = (uint32_t)((hh % 24) * 60 + mm % 60);
    clock_base_ms = now;
    Serial.printf("Время %02d:%02d\n", hh % 24, mm % 60);
    return;
  }
  if (!strcmp(s, "v")) {
    verbose = !verbose;
    stat_t0 = now, stat_frames = stat_bytes = stat_render_us = stat_send_us = 0;
    return;
  }

  // Иначе это эмоция: по id или по номеру из списка.
  Expr e;
  const int num = atoi(s);
  if (num >= 1 && num <= kExprCount)
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
      if (line_n) command(line, now);
      line_n = 0;
    } else if (line_n + 1 < sizeof(line)) {
      line[line_n++] = ch;
    }
  }
}

// ---------------------------------------------------------------- кнопка

bool btn_down = false;
uint32_t btn_t0 = 0, btn_change = 0, tap_pending_at = 0;
bool long_fired = false, tap_pending = false;

void on_short(uint32_t now, bool twice) {
  if (mode == Mode::Alive) {
    bh.on_event(twice ? Event::double_tap : Event::tap, now);
  } else {
    step(1, now);
  }
}

void poll_button(uint32_t now) {
  if (PIN_BUTTON < 0) return;
  const bool raw = digitalRead(PIN_BUTTON) == (BUTTON_ACTIVE_LOW ? LOW : HIGH);
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
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(I2C_HZ);

  const uint32_t now = millis();
  const uint32_t seed = (uint32_t)esp_random();
  eng.init(seed, now);
  bh.init(&eng, seed ^ 0x9E3779B9u, now);
  clock_base_ms = now;

  Serial.printf("\nБрелок с эмоциями — стенд. SDA %d, SCL %d, кнопка %d, I2C %d кГц\n", PIN_SDA, PIN_SCL, PIN_BUTTON,
                I2C_HZ / 1000);
  if (!start_oled()) {
    Serial.println("Экран не найден.");
    scan_i2c();
  }
  help();
  stat_t0 = now;
}

void loop() {
  const uint32_t now = millis();

  if (!have_oled) {
    static uint32_t retry = 0;
    if (now - retry > 2000) {
      retry = now;
      if (!start_oled()) scan_i2c();
    }
  }

  poll_serial(now);
  poll_button(now);

  bh.set_clock((uint16_t)((clock_base_min + (now - clock_base_ms) / 60000) % 1440));
  if (mode == Mode::Alive && now >= bh.next_update_ms()) bh.update(now);
  if (mode == Mode::Showcase && now >= showcase_next) {
    showcase_next = now + showcase_ms;
    show((Expr)((showcase_i + 1) % kExprCount), now);
  }

  // Как и движок, сравниваем время напрямую: kNever = 0xFFFFFFFF значит «не будить».
  if (now >= eng.next_update_ms()) {
    const uint32_t t0 = micros();
    const bool changed = eng.update(now);
    stat_render_us += micros() - t0;
    if (changed && have_oled) push_frame();
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
