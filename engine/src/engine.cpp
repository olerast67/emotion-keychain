#include "face/engine.h"

namespace face {

namespace {

constexpr uint32_t kNever = 0xFFFFFFFFu;

inline float frac(float x) { return x - (float)ifloor(x); }

void lerp_eye(const Eye& a, const Eye& b, float k, Eye& o) {
  o = k < 0.5f ? a : b;
  o.w = lerpf(a.w, b.w, k);
  o.h = lerpf(a.h, b.h, k);
  o.r = lerpf(a.r, b.r, k);
  o.t = lerpf(a.t, b.t, k);
  o.px = lerpf(a.px, b.px, k);
  o.py = lerpf(a.py, b.py, k);
  o.pr = lerpf(a.pr, b.pr, k);
  o.gx = lerpf(a.gx, b.gx, k);
  o.gy = lerpf(a.gy, b.gy, k);
  o.gr = lerpf(a.gr, b.gr, k);
  o.lid_cov = lerpf(a.lid_cov, b.lid_cov, k);
  o.lid_ang = lerpf(a.lid_ang, b.lid_ang, k);
  o.lid_bend = lerpf(a.lid_bend, b.lid_bend, k);
  o.low_cov = lerpf(a.low_cov, b.low_cov, k);
  o.low_bend = lerpf(a.low_bend, b.low_bend, k);
  o.dx = lerpf(a.dx, b.dx, k);
  o.dy = lerpf(a.dy, b.dy, k);
  o.sx = lerpf(a.sx, b.sx, k);
  o.sy = lerpf(a.sy, b.sy, k);
  o.rot = lerpf(a.rot, b.rot, k);
  o.squash = lerpf(a.squash, b.squash, k);
  o.stretch = lerpf(a.stretch, b.stretch, k);
}

void lerp_brow(Brow a, Brow b, float k, Brow& o) {
  // Брови появляются «из точки» и так же исчезают.
  if (a.len < 0.5f && b.len >= 0.5f) {
    a = b;
    a.len = 0;
  } else if (b.len < 0.5f && a.len >= 0.5f) {
    b = a;
    b.len = 0;
  }
  o.len = lerpf(a.len, b.len, k);
  o.t = lerpf(a.t, b.t, k);
  o.dx = lerpf(a.dx, b.dx, k);
  o.gap = lerpf(a.gap, b.gap, k);
  o.ang = lerpf(a.ang, b.ang, k);
  o.bend = lerpf(a.bend, b.bend, k);
}

void lerp_mouth(const Mouth& a, const Mouth& b, float k, Mouth& o) {
  o = k < 0.5f ? a : b;
  o.w = lerpf(a.w, b.w, k);
  o.curve = lerpf(a.curve, b.curve, k);
  o.open = lerpf(a.open, b.open, k);
  o.t = lerpf(a.t, b.t, k);
  o.tilt = lerpf(a.tilt, b.tilt, k);
  o.h = lerpf(a.h, b.h, k);
  o.dx = lerpf(a.dx, b.dx, k);
  o.dy = lerpf(a.dy, b.dy, k);
  o.sx = lerpf(a.sx, b.sx, k);
}

// Хэш FNV-1a по всем параметрам, от которых зависит картинка.
struct Hasher {
  uint32_t h = 2166136261u;
  void byte(uint8_t b) {
    h ^= b;
    h *= 16777619u;
  }
  void u32(uint32_t v) {
    for (int i = 0; i < 4; ++i) byte((uint8_t)(v >> (8 * i)));
  }
  void f(float v) {
    union {
      float f;
      uint32_t u;
    } c{v};
    u32(c.u);
  }
  void eye(const Eye& e) {
    byte((uint8_t)e.kind);
    const float vals[] = {e.w,  e.h,  e.r,       e.t,       e.px,       e.py,      e.pr,       e.gx,
                          e.gy, e.gr, e.lid_cov, e.lid_ang, e.lid_bend, e.low_cov, e.low_bend, e.dx,
                          e.dy, e.sx, e.sy,      e.rot,     e.squash,   e.stretch};
    for (float v : vals) f(v);
  }
  void face(const Face& fc) {
    for (int i = 0; i < 2; ++i) {
      eye(fc.eye[i]);
      const Brow& b = fc.brow[i];
      const float bv[] = {b.len, b.t, b.dx, b.gap, b.ang, b.bend};
      for (float v : bv) f(v);
    }
    const Mouth& m = fc.mouth;
    byte((uint8_t)m.kind);
    const float mv[] = {m.w, m.curve, m.open, m.t, m.tilt, m.h, m.dx, m.dy, m.sx};
    for (float v : mv) f(v);
    const float lv[] = {
        fc.layout.spacing, fc.layout.eye_y, fc.layout.mouth_y, fc.layout.tilt, fc.ox, fc.oy, fc.gx, fc.gy};
    for (float v : lv) f(v);
  }
};

inline void want(uint32_t& period, uint32_t ms) {
  if (ms < period) period = ms;
}

bool blinkable(EyeKind k) {
  return k == EyeKind::RRect || k == EyeKind::Heart || k == EyeKind::Star || k == EyeKind::Sparkle;
}

uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

}  // namespace

const char* clip_id(Clip c) {
  static const char* const kIds[] = {
#define FACE_X(n) #n,
      FACE_CLIP_LIST(FACE_X)
#undef FACE_X
  };
  return (int)c < kClipCount ? kIds[(int)c] : "none";
}

uint16_t clip_duration_ms(Clip c) {
  switch (c) {
    case Clip::blink: return 200;
    case Clip::wink: return 520;
    case Clip::nod: return 820;
    case Clip::headshake: return 760;
    case Clip::startle: return 620;
    case Clip::yawn: return 2200;
    case Clip::double_take: return 1000;
    case Clip::sneeze: return 1600;
    case Clip::look_around: return 2300;
    case Clip::hop: return 760;
    case Clip::shiver: return 1000;
    case Clip::peek: return 1500;
    case Clip::glitch: return 700;
    case Clip::eye_roll: return 1300;
    case Clip::sigh: return 1500;
    case Clip::shake_off: return 650;
    default: return 0;
  }
}

void FaceEngine::init(uint32_t seed, uint32_t now_ms) {
  rng_.seed(seed);
  has_frame_ = false;
  front_.clear();
  back_.clear();
  dirty_n_ = 0;
  stats_ = EngineStats{};
  for (int i = 0; i < (int)Overlay::Count; ++i) ov_alpha_[i] = 0;
  ov_target_ = 0;
  clip_active_ = false;
  glitch_ = 0;
  drift_x_ = drift_y_ = 0;
  drift_next_ = now_ms + 20000u + (rng_.next() % 20000u);
  gx_ = gy_ = g_from_x_ = g_from_y_ = g_to_x_ = g_to_y_ = 0;
  g_moving_ = false;
  g_ext_until_ = 0;
  expression_face(Expr::neutral, &base_);
  last_now_ = now_ms;
  set_expression(Expr::neutral, now_ms, 0);
}

void FaceEngine::set_expression(Expr e, uint32_t now_ms, int transition_ms) {
  if ((int)e >= kExprCount) return;
  preset_ = &face::expression(e);
  expr_ = e;
  expr_t0_ = now_ms;
  from_ = base_;
  expression_face(e, &to_);
  for (int i = 0; i < 2; ++i) eye_swap_[i] = from_.eye[i].kind != to_.eye[i].kind;
  mouth_swap_ = from_.mouth.kind != to_.mouth.kind;
  tr_ms_ = (uint16_t)(transition_ms < 0 ? preset_->in_ms : transition_ms);
  tr_t0_ = now_ms;
  tr_active_ = tr_ms_ > 0;
  if (!tr_active_) base_ = to_;
  ov_target_ = preset_->overlays;
  blink_on_ = false;
  schedule_blink(now_ms + tr_ms_);
  g_moving_ = false;
  schedule_gaze(now_ms + tr_ms_ / 2);
  next_update_ = now_ms;
}

void FaceEngine::play_clip(Clip c, uint32_t now_ms) {
  if ((int)c >= kClipCount) return;
  if (c == Clip::blink) {
    blink_on_ = true;
    blink_t0_ = now_ms;
    blink_ms_ = 180;
    next_update_ = now_ms;
    return;
  }
  clip_ = c;
  clip_t0_ = now_ms;
  clip_active_ = true;
  next_update_ = now_ms;
}

void FaceEngine::look_at(float gx, float gy, uint32_t now_ms, uint32_t hold_ms) {
  g_from_x_ = gx_;
  g_from_y_ = gy_;
  g_to_x_ = clampf(gx, -1.0f, 1.0f);
  g_to_y_ = clampf(gy, -1.0f, 1.0f);
  g_t0_ = now_ms;
  g_ms_ = 90;
  g_moving_ = true;
  g_ext_until_ = now_ms + hold_ms;
  g_next_ = g_ext_until_;
  next_update_ = now_ms;
}

int FaceEngine::dirty(DirtyWindow out[kPages]) const {
  for (int i = 0; i < dirty_n_; ++i) out[i] = dirty_[i];
  return dirty_n_;
}

void FaceEngine::schedule_blink(uint32_t from) {
  if (!preset_) return;
  uint32_t lo, hi;
  switch (preset_->blink) {
    case Blink::None: blink_next_ = kNever; return;
    case Blink::Rare: lo = 6000, hi = 14000; break;
    case Blink::Often: lo = 900, hi = 2600; break;
    case Blink::Slow: lo = 2500, hi = 6000; break;
    case Blink::Normal:
    default: lo = 2500, hi = 6500; break;
  }
  blink_next_ = from + lo + rng_.next() % (hi - lo);
}

void FaceEngine::update_blink(uint32_t now, uint32_t& period) {
  if (!opt_.idle_motion && !blink_on_) return;
  if (!blink_on_ && blink_next_ != kNever && now >= blink_next_ && !tr_active_) {
    blink_on_ = true;
    blink_t0_ = now;
    blink_ms_ = preset_->blink == Blink::Slow ? 560 : (preset_->blink == Blink::Often ? 150 : 180);
    blink_next_ = kNever;  // следующее моргание планируется, когда закончится это
  }
  if (!blink_on_) return;
  float k = (float)(now - blink_t0_) / (float)blink_ms_;
  if (k >= 1.0f) {
    blink_on_ = false;
    float double_p = preset_->blink == Blink::Often ? 0.3f : (preset_->blink == Blink::Slow ? 0.0f : 0.15f);
    if (!blink_double_ && rng_.chance(double_p)) {
      blink_double_ = true;
      blink_next_ = now + 110;
    } else {
      blink_double_ = false;
      schedule_blink(now);
    }
    return;
  }
  float sq;
  if (k < 0.38f)
    sq = 1.0f - ease_in(k / 0.38f);
  else if (k < 0.52f)
    sq = 0.0f;
  else
    sq = ease_out((k - 0.52f) / 0.48f);
  float st = 1.0f + 0.10f * bump(k);
  for (int i = 0; i < 2; ++i) {
    if (!blinkable(composed_.eye[i].kind)) continue;
    composed_.eye[i].squash *= sq;
    composed_.eye[i].stretch *= st;
  }
  want(period, opt_.frame_ms);
}

void FaceEngine::schedule_gaze(uint32_t from) {
  if (!preset_) return;
  uint32_t lo, hi;
  switch (preset_->gaze) {
    case Gaze::Still: lo = 3000, hi = 7000; break;
    case Gaze::Around: lo = 700, hi = 1800; break;
    case Gaze::Dart: lo = 250, hi = 700; break;
    case Gaze::Up:
    case Gaze::Down:
    case Gaze::UpSide: lo = 1500, hi = 3500; break;
    case Gaze::Left:
    case Gaze::Right:
    case Gaze::Avert: lo = 1800, hi = 4000; break;
    case Gaze::Idle:
    default: lo = 1200, hi = 3600; break;
  }
  g_next_ = from + lo + rng_.next() % (hi - lo);
}

void FaceEngine::update_gaze(uint32_t now, uint32_t& period) {
  if (opt_.idle_motion && !g_moving_ && now >= g_next_ && now >= g_ext_until_) {
    float tx = 0, ty = 0;
    float r = rng_.unit();
    switch (preset_->gaze) {
      case Gaze::Idle:
        if (r < 0.55f)
          tx = rng_.range(-0.25f, 0.25f), ty = rng_.range(-0.2f, 0.2f);
        else if (r < 0.88f)
          tx = rng_.range(-0.8f, 0.8f), ty = rng_.range(-0.5f, 0.4f);
        else
          tx = rng_.chance(0.5f) ? -0.95f : 0.95f, ty = rng_.range(-0.3f, 0.3f);
        break;
      case Gaze::Still: tx = rng_.range(-0.12f, 0.12f), ty = rng_.range(-0.1f, 0.1f); break;
      case Gaze::Around: tx = rng_.range(-1.0f, 1.0f), ty = rng_.range(-0.7f, 0.5f); break;
      case Gaze::Up: tx = rng_.range(-0.35f, 0.35f), ty = rng_.range(-0.95f, -0.75f); break;
      case Gaze::Down: tx = rng_.range(-0.4f, 0.4f), ty = rng_.range(0.7f, 0.95f); break;
      case Gaze::Left: tx = rng_.range(-1.0f, -0.85f), ty = rng_.range(-0.15f, 0.15f); break;
      case Gaze::Right: tx = rng_.range(0.85f, 1.0f), ty = rng_.range(-0.15f, 0.15f); break;
      case Gaze::Avert: tx = (gx_ > 0 ? -0.8f : 0.8f), ty = rng_.range(0.5f, 0.75f); break;
      case Gaze::Dart: tx = rng_.range(-1.0f, 1.0f), ty = rng_.range(-0.6f, 0.6f); break;
      case Gaze::UpSide: tx = rng_.range(0.6f, 0.9f) * (r < 0.8f ? 1.0f : -1.0f), ty = rng_.range(-0.9f, -0.65f); break;
    }
    g_from_x_ = gx_, g_from_y_ = gy_;
    g_to_x_ = tx, g_to_y_ = ty;
    float dist = fabs_(tx - gx_) + fabs_(ty - gy_);
    g_ms_ = (uint16_t)(50.0f + 45.0f * dist);
    g_t0_ = now;
    g_moving_ = dist > 0.04f;
    schedule_gaze(now);
  }
  float squash = 1.0f;
  if (g_moving_) {
    float k = (float)(now - g_t0_) / (float)g_ms_;
    if (k >= 1.0f) {
      gx_ = g_to_x_, gy_ = g_to_y_;
      g_moving_ = false;
    } else {
      float e = ease_out(k);
      gx_ = lerpf(g_from_x_, g_to_x_, e);
      gy_ = lerpf(g_from_y_, g_to_y_, e);
      float dist = fabs_(g_to_x_ - g_from_x_) + fabs_(g_to_y_ - g_from_y_);
      if (dist > 0.5f) squash = 1.0f - 0.12f * bump(k);
      want(period, opt_.frame_ms);
    }
  }
  composed_.gx = gx_;
  composed_.gy = gy_;
  if (squash < 1.0f) {
    for (int i = 0; i < 2; ++i)
      if (blinkable(composed_.eye[i].kind)) composed_.eye[i].squash *= squash;
  }
}

void FaceEngine::apply_motion(uint32_t now, uint32_t& period) {
  if (!preset_ || preset_->motion == Motion::None) return;
  const float t = (float)(now - expr_t0_) * 0.001f;
  const float A = preset_->motion_amp, f = preset_->motion_hz;
  Face& c = composed_;
  switch (preset_->motion) {
    case Motion::None: break;
    case Motion::Bounce: c.oy -= A * fabs_(fsin(kPi * f * t)); break;
    case Motion::Bob: c.oy += A * fsin(kTau * f * t); break;
    case Motion::Tremble: {
      uint32_t h = hash32((uint32_t)(t * f) * 2654435761u ^ 0x5bd1e995u);
      int span = (int)A * 2 + 1;
      c.ox += (float)((int)(h % (uint32_t)span) - (int)A);
      c.oy += (float)((int)((h >> 8) % (uint32_t)span) - (int)A);
      break;
    }
    case Motion::Shake: c.ox += A * fsin(kTau * f * t); break;
    case Motion::Sway:
      c.ox += A * fsin(kTau * f * t);
      c.layout.tilt += A * 0.5f * fsin(kTau * f * t);
      break;
    case Motion::Shiver:
      // Дрожь приступами: 1 с из 4, иначе холод съест батарею.
      if (frac(t / 4.0f) < 0.25f) c.ox += fsin(kTau * f * t) > 0.0f ? A : -A;
      break;
    case Motion::Pulse: {
      float p = frac(t * f);
      float k = bump(clampf(p / 0.16f, 0.0f, 1.0f)) + 0.7f * bump(clampf((p - 0.22f) / 0.16f, 0.0f, 1.0f));
      float s = 1.0f + A * 0.01f * k;
      for (int i = 0; i < 2; ++i) {
        c.eye[i].sx *= s;
        c.eye[i].sy *= s;
      }
      break;
    }
    case Motion::Float:
      c.oy += A * fsin(kTau * f * t);
      c.ox += A * 0.6f * fsin(kPi * f * t);
      break;
    case Motion::Nod: c.oy += A * fabs_(fsin(kPi * f * t)); break;
    case Motion::Wobble:
      c.layout.tilt += A * fsin(kTau * f * t);
      c.ox += A * 0.4f * fsin(kTau * f * t);
      break;
  }
  // Кадры нужны не чаще, чем лицо реально сдвигается на пиксель.
  uint32_t p;
  switch (preset_->motion) {
    case Motion::Tremble: p = (uint32_t)(1000.0f / fmax_(f, 1.0f)); break;
    case Motion::Shiver: p = (uint32_t)(500.0f / fmax_(f, 1.0f)); break;
    case Motion::Pulse: p = 40; break;
    default: {
      const float speed = kTau * f * fmax_(A, 0.5f);  // пикс/с на пике
      p = (uint32_t)clampf(1000.0f / (speed * 1.5f), (float)opt_.frame_ms, 250.0f);
    }
  }
  want(period, p);
}

void FaceEngine::apply_clip(uint32_t now, uint32_t& period) {
  glitch_ = 0;
  if (!clip_active_) return;
  const uint32_t dur = clip_duration_ms(clip_);
  const float ms = (float)(now - clip_t0_);
  if (ms >= (float)dur) {
    clip_active_ = false;
    return;
  }
  want(period, opt_.frame_ms);
  const float k = ms / (float)dur;
  Face& c = composed_;
  switch (clip_) {
    case Clip::blink:
    case Clip::Count:
    case Clip::None: break;
    case Clip::wink: {
      float env = envelope(k, 0.18f, 0.3f);
      c.eye[1].squash *= 1.0f - env;
      c.eye[0].low_cov = fmax_(c.eye[0].low_cov, 0.12f * env);
      c.mouth.curve += 2.5f * env;
      break;
    }
    case Clip::nod: c.oy += 3.0f * fabs_(fsin(kTau * k)) * (1.0f - k * 0.3f); break;
    case Clip::headshake: c.ox += 4.0f * fsin(kTau * 3.0f * k) * envelope(k, 0.1f, 0.3f); break;
    case Clip::startle: {
      float env = envelope(k, 0.1f, 0.6f);
      for (int i = 0; i < 2; ++i) {
        c.eye[i].sx *= 1.0f + 0.25f * env;
        c.eye[i].sy *= 1.0f + 0.25f * env;
      }
      c.oy -= 3.0f * bump(clampf(k * 2.0f, 0.0f, 1.0f));
      blink_on_ = false;
      break;
    }
    case Clip::yawn: {
      float env = envelope(k, 0.32f, 0.28f);
      if (env > 0.08f) {
        float yw = 10.0f + 5.0f * env, yh = 4.0f + 14.0f * env;
        c.mouth.kind = MouthKind::Ring;
        c.mouth.w = yw;
        c.mouth.h = yh;
        c.mouth.t = 3.0f;
        c.mouth.sx = 1.0f;
        c.mouth.dy += env * 2.0f;
      }
      for (int i = 0; i < 2; ++i) {
        if (blinkable(c.eye[i].kind)) c.eye[i].squash *= 1.0f - 0.85f * env;
      }
      c.oy -= 2.0f * env;
      break;
    }
    case Clip::double_take: {
      float gx;
      if (ms < 220)
        gx = ease_out(ms / 220.0f) * 0.9f;
      else if (ms < 400)
        gx = 0.9f * (1.0f - ease_in_out((ms - 220) / 180.0f));
      else if (ms < 520)
        gx = 0.95f * ease_out((ms - 400) / 120.0f);
      else if (ms < 850)
        gx = 0.95f;
      else
        gx = 0.95f * (1.0f - (ms - 850) / 150.0f);
      c.gx = gx;
      if (ms > 400 && ms < 850) {
        float e = bump((ms - 400) / 450.0f);
        for (int i = 0; i < 2; ++i) {
          c.eye[i].sx *= 1.0f + 0.22f * e;
          c.eye[i].sy *= 1.0f + 0.22f * e;
        }
      }
      break;
    }
    case Clip::sneeze: {
      if (ms < 800) {
        float a = ease_in_out(ms / 800.0f);
        for (int i = 0; i < 2; ++i)
          if (blinkable(c.eye[i].kind)) c.eye[i].squash *= 1.0f - 0.55f * a;
        c.mouth.kind = MouthKind::Ring;
        c.mouth.w = 6.0f + 4.0f * a;
        c.mouth.h = 5.0f + 5.0f * a;
        c.mouth.t = 2.5f;
        c.mouth.sx = 1.0f;
        c.oy -= 2.5f * a;
      } else if (ms < 1000) {
        float b = (ms - 800) / 200.0f;
        for (int i = 0; i < 2; ++i) {
          const float dx0 = c.eye[i].dx;
          c.eye[i] = chevron(22, 18, 4);
          c.eye[i].dx = dx0;
        }
        c.mouth = open(20, 0, 9);
        c.oy += 4.0f * bump(b);
        c.ox += 2.0f * fsin(b * kTau * 2.0f);
      } else {
        float r = (ms - 1000) / 600.0f;
        for (int i = 0; i < 2; ++i)
          if (blinkable(c.eye[i].kind)) c.eye[i].squash *= 0.4f + 0.6f * ease_out(r);
      }
      blink_on_ = false;
      break;
    }
    case Clip::look_around: {
      float gx = 0, gy = 0;
      if (ms < 120)
        gx = -ease_out(ms / 120.0f);
      else if (ms < 750)
        gx = -1.0f;
      else if (ms < 900)
        gx = -1.0f + 2.0f * ease_in_out((ms - 750) / 150.0f);
      else if (ms < 1600)
        gx = 1.0f;
      else if (ms < 1750)
        gx = 1.0f - ease_in_out((ms - 1600) / 150.0f), gy = -0.5f * ease_out((ms - 1600) / 150.0f);
      else if (ms < 2100)
        gx = 0, gy = -0.5f;
      else
        gy = -0.5f * (1.0f - (ms - 2100) / 200.0f);
      c.gx = gx;
      c.gy = gy;
      break;
    }
    case Clip::hop: {
      float h = fabs_(fsin(kPi * 2.0f * k));
      c.oy -= 7.0f * h;
      if (h < 0.2f && k > 0.05f)
        for (int i = 0; i < 2; ++i) c.eye[i].squash *= 0.85f;
      break;
    }
    case Clip::shiver:
      c.ox += ((int)(ms / 40.0f) & 1) ? 1.0f : -1.0f;
      for (int i = 0; i < 2; ++i) c.eye[i].squash *= 0.92f;
      break;
    case Clip::peek: {
      if (ms < 700)
        c.oy += 44.0f * (1.0f - ease_out_back(ms / 700.0f));
      else if (ms < 1000)
        c.gx = -0.9f * bump((ms - 700) / 300.0f);
      else if (ms < 1300)
        c.gx = 0.9f * bump((ms - 1000) / 300.0f);
      break;
    }
    case Clip::glitch: glitch_ = 1.0f - k; break;
    case Clip::eye_roll: {
      if (ms < 900) {
        float th = kPi + kPi * ease_in_out(ms / 900.0f);
        c.gx = fcos(th);
        c.gy = 0.85f * fsin(th);
      } else {
        c.gx = 1.0f - (ms - 900) / 400.0f;
      }
      for (int i = 0; i < 2; ++i) c.eye[i].lid_cov = fmax_(c.eye[i].lid_cov, 0.3f * envelope(k, 0.2f, 0.2f));
      break;
    }
    case Clip::sigh: {
      float env = envelope(k, 0.35f, 0.35f);
      for (int i = 0; i < 2; ++i) c.eye[i].lid_cov = fmax_(c.eye[i].lid_cov, 0.45f * env);
      c.oy += 2.0f * env;
      if (env > 0.5f) {
        c.mouth.kind = MouthKind::Ring;
        c.mouth.w = 6.0f;
        c.mouth.h = 6.0f;
        c.mouth.t = 2.0f;
        c.mouth.sx = 1.0f;
      }
      break;
    }
    case Clip::shake_off: {
      c.ox += 5.0f * fsin(kTau * ms / 110.0f) * (1.0f - k);
      for (int i = 0; i < 2; ++i)
        if (blinkable(c.eye[i].kind)) c.eye[i].squash *= 0.75f + 0.25f * k;
      break;
    }
  }
}

void FaceEngine::overlays_update(uint32_t now, uint32_t& period) {
  const float dt = (float)(now - ov_last_);
  ov_last_ = now;
  const float step = dt / 180.0f;
  for (int i = 0; i < (int)Overlay::Count; ++i) {
    const bool on = (ov_target_ >> i) & 1u;
    float& a = ov_alpha_[i];
    if (on && a < 1.0f) {
      if (a <= 0.0f) ov_t0_[i] = now;
      a = fmin_(1.0f, a + fmax_(step, 0.0f));
      want(period, opt_.frame_ms);
    } else if (!on && a > 0.0f) {
      a = fmax_(0.0f, a - fmax_(step, 0.0f));
      want(period, opt_.frame_ms);
    }
    if (a > 0.0f) {
      const uint16_t ms = overlay_frame_ms((Overlay)i);
      if (ms) want(period, ms);
    }
  }
}

bool FaceEngine::update(uint32_t now) {
  if (!preset_) init(1, now);
  if (preset_->hold_ms && now - expr_t0_ >= preset_->hold_ms) set_expression(preset_->next, now);

  uint32_t period = kNever;  // когда нужен следующий кадр; kNever — лицо статично

  if (tr_active_) {
    float k = (float)(now - tr_t0_) / (float)tr_ms_;
    if (k >= 1.0f) {
      base_ = to_;
      tr_active_ = false;
    } else {
      const float e = ease_in_out(k);
      lerp_mouth(from_.mouth, to_.mouth, e, base_.mouth);
      for (int i = 0; i < 2; ++i) {
        if (eye_swap_[i]) {
          // Смена формы глаза прячется за морганием: закрыть — подменить — открыть.
          Eye pos;
          lerp_eye(from_.eye[i], to_.eye[i], e, pos);
          if (k < 0.5f) {
            base_.eye[i] = from_.eye[i];
            base_.eye[i].squash = from_.eye[i].squash * (1.0f - ease_in(k * 2.0f));
          } else {
            base_.eye[i] = to_.eye[i];
            base_.eye[i].squash = ease_out(k * 2.0f - 1.0f);
          }
          base_.eye[i].dx = pos.dx;
          base_.eye[i].dy = pos.dy;
        } else {
          lerp_eye(from_.eye[i], to_.eye[i], e, base_.eye[i]);
        }
        lerp_brow(from_.brow[i], to_.brow[i], e, base_.brow[i]);
      }
      if (mouth_swap_) {
        const Mouth& src = k < 0.5f ? from_.mouth : to_.mouth;
        base_.mouth = src;
        base_.mouth.sx =
            src.sx * (k < 0.5f ? 1.0f - 0.85f * ease_in(k * 2.0f) : 0.15f + 0.85f * ease_out(k * 2.0f - 1.0f));
        base_.mouth.dx = lerpf(from_.mouth.dx, to_.mouth.dx, e);
        base_.mouth.dy = lerpf(from_.mouth.dy, to_.mouth.dy, e);
      }
      base_.layout.spacing = lerpf(from_.layout.spacing, to_.layout.spacing, e);
      base_.layout.eye_y = lerpf(from_.layout.eye_y, to_.layout.eye_y, e);
      base_.layout.mouth_y = lerpf(from_.layout.mouth_y, to_.layout.mouth_y, e);
      base_.layout.tilt = lerpf(from_.layout.tilt, to_.layout.tilt, e);
      want(period, opt_.frame_ms);
    }
  }

  composed_ = base_;
  composed_.ox = composed_.oy = 0;
  for (int i = 0; i < 2; ++i) {
    // Спирали крутятся, пока держится эмоция.
    if (composed_.eye[i].kind == EyeKind::Spiral) {
      composed_.eye[i].rot += (float)(now - expr_t0_) * 0.3f;
      want(period, 50);
    }
  }
  apply_motion(now, period);
  update_gaze(now, period);
  update_blink(now, period);
  apply_clip(now, period);

  if (opt_.drift && now >= drift_next_) {
    // Шаг дрейфа на 1 px раз в 20–40 с: глаза не стоят часами на одних пикселях.
    int nx = drift_x_ + rng_.irange(-1, 1), ny = drift_y_ + rng_.irange(-1, 1);
    drift_x_ = iclamp(nx, -3, 3);
    drift_y_ = iclamp(ny, -2, 2);
    drift_next_ = now + 20000u + rng_.next() % 20000u;
  }
  if (opt_.drift) {
    composed_.ox += (float)drift_x_;
    composed_.oy += (float)drift_y_;
  }

  overlays_update(now, period);

  // Время значков квантуется их собственным периодом: если кадр рисуется
  // ради другого движения, значок не обновляется лишний раз.
  uint32_t ov_local[(int)Overlay::Count];
  Hasher hs;
  hs.face(composed_);
  for (int i = 0; i < (int)Overlay::Count; ++i) {
    ov_local[i] = 0;
    if (ov_alpha_[i] <= 0.0f) continue;
    uint32_t local = now - ov_t0_[i];
    const uint16_t p = overlay_frame_ms((Overlay)i);
    local = p ? local - local % p : 0;
    ov_local[i] = local;
    hs.byte((uint8_t)i);
    hs.f(ov_alpha_[i]);
    hs.u32(local);
  }
  hs.f(glitch_);
  hs.u32(glitch_ > 0.0f ? now / 50u : 0u);
  hs.byte((uint8_t)(opt_.scanlines | opt_.upside_down << 1));
  hs.u32(opt_.scanlines ? (expr_t0_ / 1000u + now / 20000u) & 1u : 0u);

  // Ничего не изменилось — не рисуем вовсе (на МК это экономит процессорное время).
  const bool same = has_frame_ && hs.h == frame_hash_;
  frame_hash_ = hs.h;
  has_frame_ = true;
  if (same) {
    dirty_n_ = 0;
  } else {
    back_.clear();
    Anchors anchors;
    render_face(back_, composed_, &anchors);
    for (int i = 0; i < (int)Overlay::Count; ++i) {
      if (ov_alpha_[i] > 0.0f) render_overlay(back_, (Overlay)i, anchors, ov_alpha_[i], (float)ov_local[i] * 0.001f);
    }

    if (glitch_ > 0.0f) {
      // Сбой: сдвигаем полосы строк и инвертируем одну из них.
      uint32_t h = hash32((now / 50u) * 2246822519u);
      int bands = 2 + (int)(glitch_ * 4.0f);
      for (int b = 0; b < bands; ++b) {
        h = hash32(h + (uint32_t)b);
        int y0 = (int)(h % kHeight), len = 2 + (int)((h >> 8) % 6u), sh = (int)((h >> 16) % 13u) - 6;
        for (int y = y0; y < y0 + len && y < kHeight; ++y) {
          bool row[kWidth];
          for (int x = 0; x < kWidth; ++x) row[x] = back_.get(x, y);
          for (int x = 0; x < kWidth; ++x) {
            int sx = x - sh;
            back_.plot(x, y, (sx >= 0 && sx < kWidth && row[sx]) ? Ink::Set : Ink::Clear);
          }
        }
        if (b == 0) {
          int yy = (int)((h >> 4) % kHeight);
          back_.hspan(yy, 0, kWidth - 1, Ink::Invert);
        }
      }
    }
    if (opt_.scanlines) {
      const int phase = (int)((expr_t0_ / 1000u + now / 20000u) & 1u);
      for (int y = phase; y < kHeight; y += 2) back_.hspan(y, 0, kWidth - 1, Ink::Clear);
    }
    if (opt_.upside_down) back_.rotate180();

    ++stats_.renders;
    dirty_n_ = diff_frames(front_, back_, dirty_);
    if (dirty_n_) {
      front_.copy_from(back_);
      ++stats_.frames;
      stats_.bytes += (uint32_t)dirty_bytes(dirty_, dirty_n_);
    }
  }

  uint32_t next = period == kNever ? kNever : now + period;
  if (opt_.idle_motion) {
    if (blink_next_ > now && blink_next_ < next) next = blink_next_;
    uint32_t g = g_next_ > g_ext_until_ ? g_next_ : g_ext_until_;
    if (g > now && g < next) next = g;
  }
  if (opt_.drift && drift_next_ < next) next = drift_next_;
  if (preset_->hold_ms) {
    uint32_t h = expr_t0_ + preset_->hold_ms;
    if (h < next) next = h;
  }
  if (opt_.scanlines) {
    uint32_t s = (now / 20000u + 1u) * 20000u;
    if (s < next) next = s;
  }
  next_update_ = next <= now ? now + 1 : next;
  last_now_ = now;
  return dirty_n_ > 0;
}

}  // namespace face
