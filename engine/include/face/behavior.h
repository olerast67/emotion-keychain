// Behavior — «характер»: превращает события датчиков в эмоции и клипы,
// ведёт настроение и сам придумывает, чем заняться, когда ничего не происходит.
//
//   Behavior b;  b.init(&engine, seed, now);
//   b.on_event(Event::DoubleTap, now);        // из обработчика прерывания IMU
//   b.update(now);                            // в главном цикле
//   спать до min(engine.next_update_ms(), b.next_update_ms())
#pragma once

#include <stdint.h>

#include "face/engine.h"

namespace face {

#define FACE_EVENT_LIST(X) \
  X(tap)                   \
  X(double_tap)            \
  X(shake)                 \
  X(shake_hard)            \
  X(free_fall)             \
  X(impact)                \
  X(pick_up)               \
  X(walk)                  \
  X(run)                   \
  X(still)                 \
  X(upside_down)           \
  X(upright)               \
  X(loud)                  \
  X(speech)                \
  X(music)                 \
  X(quiet)                 \
  X(dark)                  \
  X(light)                 \
  X(bright)                \
  X(cold)                  \
  X(hot)                   \
  X(temp_ok)               \
  X(charge_start)          \
  X(charge_full)           \
  X(charge_stop)           \
  X(battery_low)           \
  X(battery_critical)      \
  X(button)                \
  X(button_long)

enum class Event : uint8_t {
#define FACE_X(n) n,
  FACE_EVENT_LIST(FACE_X)
#undef FACE_X
      Count
};
constexpr int kEventCount = (int)Event::Count;
const char* event_id(Event e);
const char* event_name(Event e);  // по-русски

// Черты характера 0..1, задают вероятности реакций.
struct Personality {
  float playful = 0.6f;
  float grumpy = 0.3f;
  float sleepy = 0.4f;
  float shy = 0.4f;
};

struct Mood {
  float valence = 0.2f;    // −1 грусть … +1 радость
  float arousal = 0.3f;    // 0 вялый … 1 возбуждён
  float energy = 0.8f;     // 0 засыпает … 1 бодр
  float boredom = 0.0f;    // растёт, когда ничего не происходит
  float annoyance = 0.0f;  // растёт от тряски и частых тапов
  float affection = 0.3f;  // растёт от внимания
};

class Behavior {
 public:
  void init(FaceEngine* engine, uint32_t seed, uint32_t now_ms);
  void on_event(Event e, uint32_t now_ms);
  void update(uint32_t now_ms);
  uint32_t next_update_ms() const { return next_; }

  // Часы для суточного ритма: минуты от полуночи (0..1439).
  void set_clock(uint16_t minutes) { clock_min_ = minutes; }
  void set_auto(bool on) { auto_ = on; }
  bool auto_mode() const { return auto_; }
  // Ручной выбор эмоции из симулятора: поведение не мешает hold_ms.
  void hold_manual(uint32_t now_ms, uint32_t ms) { manual_until_ = now_ms + ms; }

  const Mood& mood() const { return mood_; }
  Personality& personality() { return pers_; }
  Expr base_expression() const { return base_; }
  const char* last_reason() const { return reason_; }

  // Состояние окружения (выставляется событиями, можно читать).
  bool dark = false, charging = false, upside = false, music_on = false, speech_on = false;
  bool cold = false, hot = false, moving = false, running = false;
  uint8_t battery = 2;  // 0 — критично, 1 — мало, 2 — норма

 private:
  struct Option {
    Expr expr;
    float weight;
  };
  void react(Expr e, uint32_t now, uint32_t ms, const char* why, Clip clip = Clip::None);
  // Взвешенный выбор из пула вариантов, недавние реакции выпадают реже.
  Expr pick(const Option* opts, int n);
  template <int N>
  Expr pick(const Option (&opts)[N]) {
    return pick(opts, N);
  }
  void decay(uint32_t now);
  Expr choose_base(uint32_t now);
  void idle_action(uint32_t now);
  bool night() const { return clock_min_ >= 23 * 60 || clock_min_ < 6 * 60; }

  FaceEngine* eng_ = nullptr;
  Rng rng_;
  Personality pers_;
  Mood mood_;
  bool auto_ = true;
  uint16_t clock_min_ = 12 * 60;

  Expr base_ = Expr::neutral;
  uint32_t react_until_ = 0;
  uint32_t manual_until_ = 0;
  uint32_t last_touch_ = 0;  // последнее внимание (тап, взяли в руки)
  uint32_t last_event_ = 0;  // последнее любое событие
  uint32_t last_decay_ = 0;
  uint32_t next_idle_ = 0;
  uint32_t next_eval_ = 0;
  uint32_t dark_since_ = 0;
  uint32_t upside_since_ = 0;
  uint32_t fall_at_ = 0;
  uint32_t falls_[3] = {};
  uint32_t loud_at_ = 0;
  uint32_t last_calling_ = 0;
  uint32_t last_cyclops_ = 0;
  Expr recent_react_[4] = {Expr::Count, Expr::Count, Expr::Count, Expr::Count};
  uint32_t tap_times_[8] = {};
  float shake_level_ = 0;
  uint8_t recent_idle_[4] = {255, 255, 255, 255};
  const char* reason_ = "";
  uint32_t next_ = 0;
};

}  // namespace face
