// FaceEngine — то, с чем работает прошивка и симуляторы.
//
//   engine.init(seed, now);
//   engine.set_expression(Expr::happy, now);
//   loop:
//     if (time_reached(now, engine.next_update_ms()) && engine.update(now))
//       отправить engine.dirty(...) на экран;
//     спать до engine.next_update_ms();
//
// Если лицо статично, next_update_ms() указывает на следующее запланированное
// событие (моргание, взгляд, дрейф), и до него можно спать: экран держит картинку сам.
// Дальше kMaxSleepMs вперёд движок не планирует, сравнивать время — только через time_reached.
#pragma once

#include <stdint.h>

#include "face/clock.h"
#include "face/expressions.h"
#include "face/fmath.h"
#include "face/framebuffer.h"
#include "face/params.h"
#include "face/render.h"
#include "face/scenes.h"

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

// Сколько пикселей движок оставляет между эмоцией и краем экрана.
constexpr int kSafeMargin = 4;

// Внешность конкретного брелока: небольшие отличия, по которым два брелока
// с одной эмоцией всё равно выглядят по-разному (см. identity.h).
struct Quirks {
  float spacing = 0;       // расстояние между глазами, ±px
  float eye_scale = 1;     // размер глаз
  float pupil_scale = 1;   // размер зрачков
  float blink_rate = 1;    // множитель интервала моргания: >1 — моргает реже
  float brow_lift = 0;     // брови выше (+) или ниже (−), px
  bool cyber_eye = false;  // правый глаз — кибер-глаз (редкий брелок)
};

enum class Theme : uint8_t { Classic, Cyber };

struct EngineOptions {
  bool drift = true;         // медленный дрейф лица против выгорания
  bool idle_motion = true;   // моргание и взгляд
  bool scanlines = false;    // ретро-режим: горит каждая вторая строка
  bool upside_down = false;  // повернуть кадр на 180°
  uint16_t frame_ms = 33;    // период кадров анимации (~30 fps), от 10
  Theme theme = Theme::Classic;
  // Через сколько мс без внимания (note_activity, взгляд, наклон) лицо успокаивается:
  // фирменные жесты втрое реже, взгляд вдвое реже. 0 — не успокаиваться.
  uint32_t calm_after_ms = 180000;
  // Характер брелока (temperament.h): темп переходов (меньше 1 — резче) и мелкие сбои
  // картинки в любой теме — у «глюка».
  float tempo = 1.0f;
  bool glitches = false;
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
  // Кто-то обратил внимание на брелок (датчик, кнопка, команда): лицо снова оживлённое.
  void note_activity(uint32_t now_ms);
  bool calm() const { return calm_; }

  // Фильтр эмоций: движок сам переходит по цепочкам (смех → радость) и через
  // фильтр не покажет то, что ещё не открыто. nullptr — без фильтра.
  using ExprFilter = Expr (*)(Expr e, const void* ctx);
  void set_expr_filter(ExprFilter f, const void* ctx) { filter_ = f, filter_ctx_ = ctx; }

  // Внешность конкретного брелока (identity.h). Применяется сразу.
  void set_quirks(const Quirks& q, uint32_t now_ms);
  const Quirks& quirks() const { return quirks_; }

  // Наклон из акселерометра: проекция силы тяжести на плоскость экрана, −1…1
  // (x вправо, y вниз). Зрачки плавно «скатываются» в нижнюю сторону.
  // Изменения меньше kTiltDeadband — шум датчика: они не будят движок.
  void set_tilt(float x, float y);
  // Резкое движение брелока: персонаж на миг смотрит в ту сторону.
  void jerk(float dx, float dy, uint32_t now_ms);
  static constexpr float kTiltDeadband = 0.05f;

  // Прицел кибер-темы: уголки съезжаются к лицу, держатся и гаснут, мигнув.
  // Сам включается на «цепких» эмоциях (сосредоточен, подозревает, решителен…) и
  // изредка как «сканирование»; снаружи — когда рядом замечен другой брелок.
  // В классической теме не рисуется.
  void scan(uint32_t now_ms, uint32_t ms = kReticleMs);
  bool reticle_visible() const { return reticle_shown_; }
  static constexpr uint32_t kReticleMs = 2200;

  // Полноэкранная сцена вместо лица (scenes.h). Лицо продолжает жить под ней.
  void play_scene(Scene s, uint32_t now_ms, const SceneParams& p = {});
  Scene scene() const { return scene_; }
  const SceneParams& scene_params() const { return scene_p_; }
  uint32_t scene_ends_ms() const { return scene_ == Scene::None ? 0 : scene_t0_ + scene_duration_ms(scene_); }

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
  void update_blink(uint32_t now, uint32_t& period);
  void update_gaze(uint32_t now, uint32_t& period);
  void schedule_blink(uint32_t now);
  void schedule_gaze(uint32_t now);
  void apply_motion(uint32_t now);
  void apply_accent(uint32_t now, uint32_t& period);
  void schedule_accent(uint32_t from);
  void apply_clip(uint32_t now, uint32_t& period);
  // Клип ещё идёт и сам управляет глазами (моргание ждёт).
  bool clip_running(uint32_t now) const;
  bool clip_holds_eyes(uint32_t now) const;
  void apply_tilt(uint32_t now, uint32_t& period);
  void overlays_update(uint32_t now, uint32_t& period);
  void compute_fit();
  uint32_t frame_ms() const { return opt_.frame_ms < 10 ? 10u : opt_.frame_ms; }

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

  // Моргание. blink_armed_ — следующее моргание запланировано на blink_next_.
  uint32_t blink_next_ = 0, blink_t0_ = 0;
  uint16_t blink_ms_ = 180;
  bool blink_on_ = false;
  bool blink_armed_ = false;
  bool blink_double_ = false;

  // Взгляд. g_ext_ — взгляд задан снаружи (look_at) до g_ext_until_.
  float g_from_x_ = 0, g_from_y_ = 0, g_to_x_ = 0, g_to_y_ = 0, gx_ = 0, gy_ = 0;
  uint32_t g_t0_ = 0, g_next_ = 0, g_ext_until_ = 0;
  uint16_t g_ms_ = 80;
  bool g_moving_ = false;
  bool g_ext_ = false;

  // Фирменное движение эмоции.
  uint32_t accent_next_ = 0, accent_t0_ = 0;
  bool accent_on_ = false;
  float accent_sign_ = 1;

  // Внимание: когда его долго нет, лицо успокаивается (calm_).
  uint32_t active_ms_ = 0;
  bool calm_ = false;

  // Движение пресета: через сколько мс лицо сдвинется на пиксель (0 — не сдвинется)
  // и на сколько этот шаг можно отложить ради уже запланированного кадра.
  uint32_t motion_wake_ = 0, motion_slack_ = 0;

  // Клип.
  Clip clip_ = Clip::None;
  uint32_t clip_t0_ = 0;
  bool clip_active_ = false;
  float glitch_ = 0;

  // Дрейф против выгорания.
  int drift_x_ = 0, drift_y_ = 0;
  uint32_t drift_next_ = 0;

  // Безопасное поле: сдвиг, который держит эмоцию не ближе kSafeMargin к краю,
  // и сколько места остаётся на дрейф в каждую сторону.
  float fit_from_x_ = 0, fit_from_y_ = 0, fit_to_x_ = 0, fit_to_y_ = 0, fit_x_ = 0, fit_y_ = 0;
  int drift_lo_x_ = -3, drift_hi_x_ = 3, drift_lo_y_ = -2, drift_hi_y_ = 2;

  ExprFilter filter_ = nullptr;
  const void* filter_ctx_ = nullptr;

  // Внешность, тема, сцена, наклон.
  Quirks quirks_;
  Scene scene_ = Scene::None;
  uint32_t scene_t0_ = 0;
  SceneParams scene_p_;
  // Наклон: цель от датчика, сглаженное значение и его ступени, которыми он рисуется.
  float tilt_x_ = 0, tilt_y_ = 0, tilt_cx_ = 0, tilt_cy_ = 0, tilt_qx_ = 0, tilt_qy_ = 0;
  uint32_t tilt_last_ = 0;
  uint32_t micro_glitch_next_ = 0, micro_glitch_until_ = 0;
  bool micro_glitch_on_ = false;
  uint32_t reticle_t0_ = 0, reticle_ms_ = 0, scan_next_ = 0;
  bool reticle_on_ = false, reticle_shown_ = false;
  void apply_quirks(Face& f) const;

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
