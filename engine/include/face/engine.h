// FaceEngine — то, с чем работает прошивка и симуляторы.
//
//   engine.init(seed, now);
//   engine.set_expression(Expr::happy, now);
//   loop:
//     if (engine.update(now)) отправить engine.dirty(...) на экран;
//     спать до engine.next_update_ms();
//
// Если лицо статично, next_update_ms() указывает на следующее запланированное
// событие (моргание, взгляд, дрейф), и до него можно спать: экран держит картинку сам.
#pragma once

#include <stdint.h>

#include "face/expressions.h"
#include "face/fmath.h"
#include "face/framebuffer.h"
#include "face/params.h"
#include "face/render.h"

namespace face {

#define FACE_CLIP_LIST(X) \
  X(blink)                \
  X(wink)                 \
  X(nod)                  \
  X(headshake)            \
  X(startle)              \
  X(yawn)                 \
  X(double_take)          \
  X(sneeze)               \
  X(look_around)          \
  X(hop)                  \
  X(shiver)               \
  X(peek)                 \
  X(glitch)               \
  X(eye_roll)             \
  X(sigh)                 \
  X(shake_off)            \
  X(boop)

enum class Clip : uint8_t {
#define FACE_X(n) n,
  FACE_CLIP_LIST(FACE_X)
#undef FACE_X
      Count,
  None = 255
};

constexpr int kClipCount = (int)Clip::Count;
const char* clip_id(Clip c);
uint16_t clip_duration_ms(Clip c);

struct EngineOptions {
  bool drift = true;         // медленный дрейф лица против выгорания
  bool idle_motion = true;   // моргание и взгляд
  bool scanlines = false;    // ретро-режим: горит каждая вторая строка
  bool upside_down = false;  // повернуть кадр на 180°
  uint16_t frame_ms = 33;    // период кадров во время анимации (~30 fps)
};

struct EngineStats {
  uint32_t frames = 0;  // сколько раз кадр реально изменился
  uint32_t bytes = 0;  // сколько байт ушло бы на экран (только изменённые окна)
  uint32_t renders = 0;  // сколько раз движок рисовал кадр (пропуски по хэшу не считаются)
};

class FaceEngine {
 public:
  void init(uint32_t seed, uint32_t now_ms);

  // transition_ms < 0 — взять длительность из пресета.
  void set_expression(Expr e, uint32_t now_ms, int transition_ms = -1);
  Expr expression() const { return expr_; }
  void play_clip(Clip c, uint32_t now_ms);
  Clip clip() const { return clip_active_ ? clip_ : Clip::None; }
  // Внешнее управление взглядом (например, «смотрит туда, откуда звук»).
  void look_at(float gx, float gy, uint32_t now_ms, uint32_t hold_ms);

  EngineOptions& options() { return opt_; }
  const EngineOptions& options() const { return opt_; }

  // Продвигает анимацию и рисует кадр. true — кадр изменился.
  bool update(uint32_t now_ms);
  uint32_t next_update_ms() const { return next_update_; }
  const Framebuffer& frame() const { return front_; }
  int dirty(DirtyWindow out[kPages]) const;
  const Face& face() const { return composed_; }
  const EngineStats& stats() const { return stats_; }
  uint32_t expression_started_ms() const { return expr_t0_; }

 private:
  void compose(uint32_t now);
  void update_blink(uint32_t now, uint32_t& period);
  void update_gaze(uint32_t now, uint32_t& period);
  void schedule_blink(uint32_t now);
  void schedule_gaze(uint32_t now);
  void apply_motion(uint32_t now, uint32_t& period);
  void apply_accent(uint32_t now, uint32_t& period);
  void schedule_accent(uint32_t from);
  void apply_clip(uint32_t now, uint32_t& period);
  void overlays_update(uint32_t now, uint32_t& period);

  EngineOptions opt_;
  EngineStats stats_;
  Rng rng_;

  Expr expr_ = Expr::neutral;
  const Expression* preset_ = nullptr;
  uint32_t expr_t0_ = 0;

  // Переход между пресетами.
  Face from_, to_, base_;
  uint32_t tr_t0_ = 0;
  uint16_t tr_ms_ = 0;
  bool tr_active_ = false;
  bool eye_swap_[2] = {false, false};
  bool mouth_swap_ = false;

  Face composed_;

  // Моргание.
  uint32_t blink_next_ = 0, blink_t0_ = 0;
  uint16_t blink_ms_ = 180;
  bool blink_on_ = false;
  bool blink_double_ = false;

  // Взгляд.
  float g_from_x_ = 0, g_from_y_ = 0, g_to_x_ = 0, g_to_y_ = 0, gx_ = 0, gy_ = 0;
  uint32_t g_t0_ = 0, g_next_ = 0, g_ext_until_ = 0;
  uint16_t g_ms_ = 80;
  bool g_moving_ = false;

  // Фирменное движение эмоции.
  uint32_t accent_next_ = 0, accent_t0_ = 0;
  bool accent_on_ = false;
  float accent_sign_ = 1;

  // Клип.
  Clip clip_ = Clip::None;
  uint32_t clip_t0_ = 0;
  bool clip_active_ = false;
  float glitch_ = 0;

  // Дрейф против выгорания.
  int drift_x_ = 0, drift_y_ = 0;
  uint32_t drift_next_ = 0;

  // Оверлеи.
  float ov_alpha_[(int)Overlay::Count] = {};
  uint32_t ov_t0_[(int)Overlay::Count] = {};
  uint64_t ov_target_ = 0;
  uint32_t ov_last_ = 0;

  Framebuffer front_, back_;
  uint32_t frame_hash_ = 0;
  bool has_frame_ = false;
  DirtyWindow dirty_[kPages];
  int dirty_n_ = 0;
  uint32_t last_now_ = 0;
  uint32_t next_update_ = 0;
};

}  // namespace face
