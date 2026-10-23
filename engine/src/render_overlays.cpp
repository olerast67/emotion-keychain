// Символические значки поверх лица. Каждый рисуется процедурно и привязан
// к глазам или рту, поэтому едет вместе с лицом.
#include "face/engine.h"
#include "face/raster.h"
#include "face/render.h"
#include "face/text.h"

namespace face {

namespace {

inline float frac(float x) { return x - (float)ifloor(x); }

// Края безопасного поля для значков, привязанных к экрану.
constexpr float kL = (float)kSafeMargin, kR = (float)(kWidth - kSafeMargin), kT = (float)kSafeMargin,
                kB = (float)(kHeight - kSafeMargin);
inline float pop(float a) { return a >= 1.0f ? 1.0f : ease_out_back(clampf(a, 0.0f, 1.0f)); }

// Капля: круг снизу, остриё сверху.
void drop(Framebuffer& fb, float cx, float cy, float s, Paint p = {}) {
  if (s < 0.2f) return;
  float r = 2.6f * s;
  fill_circle(fb, cx, cy + r * 0.4f, r, p);
  Vec2 tri[3] = {{cx - r * 0.92f, cy + r * 0.1f}, {cx + r * 0.92f, cy + r * 0.1f}, {cx, cy - r * 1.9f}};
  fill_polygon(fb, tri, 3, p);
}

void zed(Framebuffer& fb, float x, float y, float s, float t) {
  Vec2 p[4] = {{x - s, y - s}, {x + s, y - s}, {x - s, y + s}, {x + s, y + s}};
  polyline(fb, p, 4, t);
}

void note(Framebuffer& fb, float x, float y, float s) {
  fill_ellipse(fb, x, y, 2.4f * s, 1.8f * s);
  float sx = x + 1.8f * s;
  capsule(fb, sx, y, sx, y - 7.0f * s, 1.6f);
  capsule(fb, sx, y - 7.0f * s, sx + 3.0f * s, y - 4.5f * s, 1.6f);
}

void sparkle_at(Framebuffer& fb, float x, float y, float r) {
  if (r < 1.0f) return;
  fill_star(fb, x, y, r, fmax_(r * 0.36f, 1.3f), 4, 0.0f);
}

}  // namespace

const char* overlay_id(Overlay o) {
  static const char* const kIds[] = {
      "blush_lines", "blush_dither", "sweat",     "vein",     "tear",      "tear_stream", "zzz",     "hearts",
      "sparkles",    "sound_waves",  "notes",     "question", "exclaim",   "ellipsis",    "steam",   "gloom",
      "dizzy_stars", "bulb",         "shades",    "bolt",     "snow",      "drool",       "rays",    "battery_low",
      "speed_lines", "bubble",       "halo",      "horns",    "glasses",   "nose",        "stubble", "stick",
      "eye_bags",    "shrug_arms",   "explosion", "bat",      "game_over", "queasy_lines"};
  static_assert(sizeof(kIds) / sizeof(kIds[0]) == (int)Overlay::Count, "overlay ids");
  return (int)o < (int)Overlay::Count ? kIds[(int)o] : "?";
}

bool overlay_screen_space(Overlay o) {
  switch (o) {
    case Overlay::TearStream:
    case Overlay::Hearts:
    case Overlay::Notes:
    case Overlay::Gloom:
    case Overlay::Snow:
    case Overlay::BatteryLow:
    case Overlay::SpeedLines:
    case Overlay::Halo:
    case Overlay::Stubble:
    case Overlay::ShrugArms:
    case Overlay::Explosion:
    case Overlay::Bat:
    case Overlay::GameOver: return true;
    default: return false;
  }
}

namespace {

// Как значок меняется во времени:
//   cycle — через сколько мс анимация повторяется (0 — не повторяется);
//   step  — шаг кадров плавного движения, мс: примерно на пиксель сдвига;
//   still — с этого момента значок неподвижен (0 — не замирает);
//   edges — у мигающих значков: первая мс каждого состояния внутри цикла.
//           Картинка между границами одна и та же, кадры нужны только на них.
// У статичных значков всё по нулям: кадр нужен только при появлении.
// Длительные состояния (сон, скука, мало заряда, зарядка) поэтому почти не будят процессор.
struct Timing {
  uint16_t cycle, step, still;
  uint16_t edges[5];
};

Timing timing(Overlay o) {
  switch (o) {
    case Overlay::Vein: return {500, 50, 0, {}};
    case Overlay::Tear: return {1300, 65, 0, {}};
    case Overlay::TearStream: return {225, 45, 0, {}};
    case Overlay::Zzz: return {3200, 200, 0, {}};
    case Overlay::Hearts: return {2400, 80, 0, {}};
    case Overlay::Sparkles: return {10000, 66, 0, {}};
    case Overlay::SoundWaves: return {714, 0, 0, {1, 179, 358, 465, 643}};
    case Overlay::Notes: return {2200, 80, 0, {}};
    case Overlay::Question: return {1250, 125, 0, {}};
    case Overlay::Exclaim: return {0, 50, 600, {}};
    case Overlay::Ellipsis: return {1600, 0, 0, {1, 289, 577, 865}};
    case Overlay::Steam: return {1100, 66, 0, {}};
    case Overlay::DizzyStars: return {2000, 50, 0, {}};
    case Overlay::Bulb: return {400, 0, 0, {1, 241}};
    case Overlay::Bolt: return {2000, 125, 0, {}};
    case Overlay::Snow: return {4000, 200, 0, {}};
    case Overlay::Drool: return {2400, 200, 0, {}};
    case Overlay::Rays: return {400, 50, 0, {}};
    case Overlay::BatteryLow: return {1200, 0, 0, {1, 781}};
    case Overlay::SpeedLines: return {400, 50, 0, {}};
    case Overlay::Bubble: return {3000, 120, 0, {}};
    case Overlay::Halo: return {2000, 250, 0, {}};
    case Overlay::Stick: return {2800, 140, 0, {}};
    case Overlay::Explosion: return {600, 50, 0, {}};
    case Overlay::Bat: return {6000, 66, 0, {}};
    default: return {0, 0, 0, {}};  // румянец, капля, очки, нос, щетина и другие статичные
  }
}

}  // namespace

uint32_t overlay_clock(Overlay o, uint32_t t, uint32_t* wait) {
  const Timing tm = timing(o);
  *wait = 0;
  if (tm.still && t >= tm.still) return tm.still;  // значок замер
  if (!tm.cycle && !tm.step) return 0;             // статичный значок
  const uint32_t c = tm.cycle ? t % tm.cycle : t;
  if (tm.step) {
    const uint32_t q = c - c % tm.step;
    const uint32_t end = tm.cycle ? tm.cycle : tm.still;
    const uint32_t next = q + tm.step < end ? q + tm.step : end;
    *wait = next - c;
    return q;
  }
  int i = 0;
  while (i + 1 < 5 && tm.edges[i + 1] && tm.edges[i + 1] <= c) ++i;
  const uint32_t next = i + 1 < 5 && tm.edges[i + 1] ? tm.edges[i + 1] : tm.cycle;
  *wait = next - c;
  return tm.edges[i];
}

void render_overlay(Framebuffer& fb, Overlay o, const Anchors& a, float alpha, float t) {
  if (alpha <= 0.0f) return;
  const float s = pop(alpha);
  const float lx = a.ex[0], ly = a.ey[0], rx = a.ex[1], ry = a.ey[1];
  const float lw = a.ew[0], lh = a.eh[0], rw = a.ew[1], rh = a.eh[1];
  const float cx = kWidth * 0.5f + a.ox;
  switch (o) {
    case Overlay::BlushLines: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float ex = a.ex[i], ey = a.ey[i], ew = a.ew[i], eh = a.eh[i];
        const float bx = ex - side * (ew * 0.5f + 3.0f), by = ey + eh * 0.5f + 5.0f;
        for (int k = -1; k <= 1; ++k) {
          float x = bx + k * 4.0f * s;
          capsule(fb, x - 1.5f * s * side, by + 2.5f * s, x + 1.5f * s * side, by - 2.5f * s, 2.6f);
        }
      }
      return;
    }
    case Overlay::BlushDither: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float bx = a.ex[i] - side * (a.ew[i] * 0.42f), by = a.ey[i] + a.eh[i] * 0.5f + 4.0f;
        // Шашка клетками 2×2: читается как румянец и не рассыпается в однопиксельный шум.
        const float hx = 7.0f * s, hy = 3.5f * s;
        for (int y = iround(by - hy) - 1; y <= iround(by + hy) + 1; ++y)
          for (int x = iround(bx - hx) - 1; x <= iround(bx + hx) + 1; ++x)
            if ((((x >> 1) + (y >> 1)) & 1) && in_ellipse((float)x + 0.5f - bx, (float)y + 0.5f - by, hx, hy))
              fb.set(x, y);
      }
      return;
    }
    case Overlay::Sweat: {
      const float x = rx + rw * 0.5f + 7.0f, y = ry - rh * 0.5f + 5.0f;
      drop(fb, x, y, 1.25f * s);
      fb.plot(iround(x - 1.0f), iround(y + 1.5f), Ink::Clear);
      return;
    }
    case Overlay::Vein: {
      const float x = rx + rw * 0.5f + 6.0f, y = ry - rh * 0.5f - 1.0f;
      const float k = s * (0.9f + 0.15f * fsin(t * kTau * 2.0f));
      const float d = 4.2f * k, rad = 3.8f * k;
      for (int q = 0; q < 4; ++q) {
        const float sxq = (q == 0 || q == 3) ? 1.0f : -1.0f;
        const float syq = (q < 2) ? -1.0f : 1.0f;
        const float ccx = x + sxq * d, ccy = y + syq * d;
        // Дуга, обращённая к центру значка.
        float a0 = fatan2(-syq, -sxq) - kPi * 0.25f;
        arc(fb, ccx, ccy, rad, rad, a0, a0 + kPi * 0.5f, 2.0f);
      }
      return;
    }
    case Overlay::Tear: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float ph = frac(t / 1.3f + 0.45f * i);
        const float x = a.ex[i] - side * a.ew[i] * 0.18f;
        const float y = a.ey[i] + a.eh[i] * 0.5f + 2.0f + ph * 12.0f;
        drop(fb, x, y, 0.9f * s * (ph < 0.15f ? ph / 0.15f : 1.0f));
      }
      return;
    }
    case Overlay::TearStream: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float x = a.ex[i] - side * a.ew[i] * 0.12f;
        const float y0 = a.ey[i] + a.eh[i] * 0.25f;
        const float hw = 2.0f * s;
        const float scroll = t * 40.0f;
        for (int y = iround(y0); y < (int)kB; ++y) {
          float k = frac(((float)y - scroll) / 9.0f);
          if (k > 0.72f) continue;
          fb.hspan(y, iround(x - hw), iround(x + hw) - 1, Ink::Set);
        }
      }
      return;
    }
    case Overlay::Zzz: {
      const float x0 = rx + rw * 0.35f, y0 = ry - rh * 0.5f + 2.0f;
      for (int i = 0; i < 3; ++i) {
        const float ph = frac(t / 3.2f + i / 3.0f);
        if (ph < 0.06f || ph > 0.94f) continue;
        const float zs = (1.5f + 3.0f * ph) * s;
        zed(fb, x0 + ph * 16.0f, y0 - ph * 18.0f, zs, zs > 2.5f ? 2.0f : 1.5f);
      }
      return;
    }
    case Overlay::Hearts: {
      for (int i = 0; i < 3; ++i) {
        const float ph = frac(t / 2.4f + i / 3.0f);
        const float side = (i & 1) ? 1.0f : -1.0f;
        const float x =
            clampf(cx + side * (50.0f + 2.0f * i) + 2.0f * fsin(kTau * (ph * 1.4f + i * 0.3f)), kL + 4.0f, kR - 4.0f);
        const float y = kB - 4.0f - ph * 44.0f;
        const float k = s * (ph > 0.8f ? (1.0f - ph) / 0.2f : 1.0f);
        if (k > 0.25f) fill_heart(fb, x, y, 4.0f * k, 3.6f * k);
      }
      return;
    }
    case Overlay::Sparkles: {
      const float pts[4][3] = {{lx - lw * 0.5f - 8.0f, ly - lh * 0.4f, 0.0f},
                               {rx + rw * 0.5f + 8.0f, ry - rh * 0.3f, 0.35f},
                               {lx - lw * 0.5f - 5.0f, ly + lh * 0.45f, 0.6f},
                               {rx + rw * 0.5f + 5.0f, ry + rh * 0.5f, 0.85f}};
      for (int i = 0; i < 4; ++i) {
        const float tw = 0.5f + 0.5f * fsin(kTau * (t * 1.1f + pts[i][2]));
        sparkle_at(fb, pts[i][0], pts[i][1], (2.0f + 3.5f * tw) * s);
      }
      return;
    }
    case Overlay::SoundWaves: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float ccx = a.ex[i] - side * (a.ew[i] * 0.5f - 1.0f), ccy = a.ey[i];
        const float base = side > 0 ? kPi : 0.0f;
        const float ph = frac(t / 0.714f);
        for (int k = 0; k < 3; ++k) {
          // Волны приходят снаружи: сначала дальняя, потом ближе.
          float vis = ph * 4.0f - (2 - k);
          if (vis < 0.0f || vis > 2.6f) continue;
          float rad = (6.0f + 5.0f * k) * s;
          arc(fb, ccx, ccy, rad, rad, base - 0.55f, base + 0.55f, 2.0f);
        }
      }
      return;
    }
    case Overlay::Notes: {
      for (int i = 0; i < 2; ++i) {
        const float ph = frac(t / 2.2f + i * 0.5f);
        const float side = i ? 1.0f : -1.0f;
        const float x = clampf(cx + side * (48.0f - ph * 4.0f) + 2.0f * fsin(kTau * ph * 2.0f), kL + 3.0f, kR - 7.0f);
        const float y = kB - 4.0f - ph * 36.0f;
        const float k = s * (ph > 0.85f ? (1.0f - ph) / 0.15f : 1.0f);
        if (k > 0.3f) note(fb, x, y, k);
      }
      return;
    }
    case Overlay::Question: {
      // Знак висит у правого глаза, но с поля не уходит, даже когда глаз уехал за взглядом.
      const float bob = fsin(t * kTau * 0.8f) * 1.5f;
      const float x = fmin_(rx + rw * 0.5f + 9.0f, kR - 5.5f), y = fmax_(ry - rh * 0.5f + 1.0f + bob, kT + 9.0f);
      const float k = s;
      arc(fb, x, y - 4.0f * k, 3.6f * k, 3.6f * k, kPi * 1.0f, kPi * 2.45f, 2.0f);
      capsule(fb, x + 0.5f * k, y, x + 0.5f * k, y + 2.5f * k, 2.0f);
      fill_circle(fb, x + 0.5f * k, y + 6.5f * k, 1.4f * k);
      return;
    }
    case Overlay::Exclaim: {
      const float jig = fsin(t * kTau * 3.0f) * (t < 0.6f ? 1.0f : 0.0f);
      const float x = fmax_(lx - lw * 0.5f - 9.0f + jig, kL + 2.5f), y = fmax_(ly - lh * 0.5f + 2.0f, kT + 8.0f);
      const float k = s;
      capsule(fb, x, y - 6.0f * k, x, y + 2.0f * k, 3.0f);
      fill_circle(fb, x, y + 6.5f * k, 1.7f * k);
      return;
    }
    case Overlay::Ellipsis: {
      const float ph = frac(t / 1.6f);
      for (int i = 0; i < 3; ++i) {
        if (ph < 0.18f * (i + 1)) continue;
        fill_circle(fb, rx + rw * 0.5f - 2.0f + i * 6.0f, ry - rh * 0.5f - 6.0f, 1.6f * s);
      }
      return;
    }
    case Overlay::Steam: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float ph = frac(t / 1.1f + 0.5f * i);
        const float x = a.ex[i] - side * (a.ew[i] * 0.5f + 4.0f) - side * ph * 4.0f;
        const float y = a.ey[i] - a.eh[i] * 0.5f - 2.0f - ph * 8.0f;
        // Облачко тает, уменьшаясь, — без шумного дизеринга.
        const float k = s * (ph < 0.7f ? 1.0f : (1.0f - ph) / 0.3f);
        if (k < 0.2f) continue;
        fill_circle(fb, x, y, (2.5f + ph * 1.5f) * k);
        fill_circle(fb, x + 3.0f * k, y + 1.0f, (2.0f + ph) * k);
        fill_circle(fb, x - 3.0f * k, y + 1.0f, (2.0f + ph) * k);
      }
      return;
    }
    case Overlay::Gloom: {
      // Тучка уныния, как в манге: по три вертикальные черты у верхних углов,
      // снаружи от глаз. Над глазами им места нет — там брови.
      // Длины разные и заметно длиннее ширины промежутков: так черты не складываются в букву.
      static const float kLen[3] = {19.0f, 13.0f, 16.0f};
      for (int side = 0; side < 2; ++side) {
        const float edge = side == 0 ? lx - lw * 0.5f - 4.0f : rx + rw * 0.5f + 4.0f;
        for (int k = 0; k < 3; ++k) {
          const int x = iround(side == 0 ? edge - 2.0f - (float)k * 6.0f : edge + (float)k * 6.0f);
          if (x < (int)kL || x + 1 > (int)kR - 1) continue;
          const int len = iround(kLen[k] * s);
          for (int y = (int)kT; y < (int)kT + len; ++y) fb.hspan(y, x, x + 1, Ink::Set);
        }
      }
      return;
    }
    case Overlay::DizzyStars: {
      const float ccx = cx, ccy = fmin_(ly - lh * 0.5f, ry - rh * 0.5f) - 4.0f;
      for (int i = 0; i < 3; ++i) {
        const float ang = t * kPi + kTau * i / 3.0f;  // оборот за 2 с
        const float depth = 0.5f + 0.5f * fsin(ang);
        const float x = ccx + 30.0f * fcos(ang);
        const float y = ccy + 3.5f * fsin(ang);
        const float r = (2.8f + 1.6f * depth) * s;
        fill_star(fb, x, fmax_(y, kT + r), r, r * 0.5f, 5, ang);
      }
      return;
    }
    case Overlay::Bulb: {
      // Лампочка с лучами крупная: её место у правого глаза, но не ближе поля к краю.
      const float x = fmin_(rx + rw * 0.5f + 8.0f, kR - 10.0f), y = fmax_(ry - rh * 0.5f - 1.0f, kT + 9.5f);
      const float k = s;
      fill_circle(fb, x, y, 4.0f * k);
      capsule(fb, x - 1.5f * k, y + 5.0f * k, x + 1.5f * k, y + 5.0f * k, 1.5f);
      capsule(fb, x - 1.0f * k, y + 7.0f * k, x + 1.0f * k, y + 7.0f * k, 1.5f);
      fb.plot(iround(x - 1.5f), iround(y - 1.5f), Ink::Clear);
      if (frac(t * 2.5f) < 0.6f) {
        for (int i = 0; i < 5; ++i) {
          float ang = kPi + kPi * (float)i / 4.0f;
          capsule(fb, x + 6.5f * k * fcos(ang), y + 6.5f * k * fsin(ang), x + 8.5f * k * fcos(ang),
                  y + 8.5f * k * fsin(ang), 1.5f);
        }
      }
      return;
    }
    case Overlay::Shades: {
      const float top = fmin_(ly, ry) - 9.0f * s;
      for (int i = 0; i < 2; ++i) {
        const float ex = a.ex[i], ew = a.ew[i];
        const float hw = (ew * 0.5f + 4.0f) * s;
        Vec2 lens[6] = {{ex - hw, top},
                        {ex + hw, top},
                        {ex + hw, top + 4.0f},
                        {ex + hw * 0.6f, top + 16.0f * s},
                        {ex - hw * 0.6f, top + 16.0f * s},
                        {ex - hw, top + 4.0f}};
        fill_polygon(fb, lens, 6);
        // Блик на стекле.
        capsule(fb, ex - hw * 0.45f, top + 9.0f * s, ex - hw * 0.1f, top + 3.0f * s, 1.5f, Paint{Ink::Clear});
      }
      capsule(fb, lx + lw * 0.5f, top + 2.0f, rx - rw * 0.5f, top + 2.0f, 2.5f);
      capsule(fb, fmax_(kL, lx - lw * 0.5f - 12.0f), top + 1.0f, lx - lw * 0.5f - 3.0f, top + 1.0f, 2.5f);
      capsule(fb, rx + rw * 0.5f + 3.0f, top + 1.0f, fmin_(kR - 1.0f, rx + rw * 0.5f + 12.0f), top + 1.0f, 2.5f);
      return;
    }
    case Overlay::Bolt: {
      const float x = cx, y = fmin_(ly - lh * 0.5f, ry - rh * 0.5f) + 4.0f;
      const float k = s * (0.92f + 0.08f * fsin(t * kTau * 1.5f));
      Vec2 p[6] = {{x + 1.5f * k, y - 7.0f * k}, {x - 4.5f * k, y + 1.0f * k}, {x - 0.2f * k, y + 1.0f * k},
                   {x - 1.5f * k, y + 7.0f * k}, {x + 4.5f * k, y - 1.0f * k}, {x + 0.2f * k, y - 1.0f * k}};
      fill_polygon(fb, p, 6);
      return;
    }
    case Overlay::Snow: {
      const float pts[2][2] = {{fmax_(kL + 5.0f, lx - lw * 0.5f - 8.0f), kT + 7.0f},
                               {fmin_(kR - 5.0f, rx + rw * 0.5f + 8.0f), kT + 12.0f}};
      // Покачивание — за 4 с, поворот снежинки — на 60° за 4/3 с: картинка повторяется ровно через 4 с.
      for (int i = 0; i < 2; ++i) {
        const float x = pts[i][0], y = pts[i][1] + 2.0f * fsin(t * kTau * 0.25f + i);
        const float r = 4.0f * s;
        for (int k = 0; k < 3; ++k) {
          float ang = t * kTau * 0.125f + kPi * (float)k / 3.0f;
          capsule(fb, x - r * fcos(ang), y - r * fsin(ang), x + r * fcos(ang), y + r * fsin(ang), 2.0f);
        }
      }
      return;
    }
    case Overlay::Drool: {
      const float x = a.mx + a.mw * 0.32f, y = a.my + 2.0f;
      const float len = 1.0f + 4.0f * ease_in_out(frac(t / 2.4f));
      capsule(fb, x, y, x, y + len * s, 2.0f);
      fill_circle(fb, x, y + len * s + 1.0f, 1.6f * s);
      return;
    }
    case Overlay::Rays: {
      const float pulse = 0.85f + 0.15f * fsin(t * kTau * 2.5f);
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float ccx = a.ex[i], ccy = a.ey[i];
        const float rad0 = fmax_(a.ew[i], a.eh[i]) * 0.5f + 3.0f;
        for (int k = 0; k < 3; ++k) {
          float ang = (side > 0 ? kPi * 1.25f : kPi * 1.75f) + side * (k - 1) * 0.42f;
          float r0 = rad0, r1 = rad0 + 5.0f * s * pulse;
          capsule(fb, ccx + r0 * fcos(ang), ccy + r0 * fsin(ang), ccx + r1 * fcos(ang), ccy + r1 * fsin(ang), 2.0f);
        }
      }
      return;
    }
    case Overlay::BatteryLow: {
      if (frac(t / 1.2f) > 0.65f) return;
      const float x = cx, y = kT + 4.0f;
      const int xa = iround(x - 9 * s), xb = iround(x + 7 * s), ya = iround(y - 4.0f * s), yb = iround(y + 4.0f * s);
      // Корпус батарейки с обводкой в 2 px, контакт и один оставшийся сегмент.
      for (int yy = ya; yy <= yb; ++yy) {
        const bool edge = yy <= ya + 1 || yy >= yb - 1;
        if (edge) {
          fb.hspan(yy, xa, xb, Ink::Set);
        } else {
          fb.hspan(yy, xa, xa + 1, Ink::Set);
          fb.hspan(yy, xb - 1, xb, Ink::Set);
          fb.hspan(yy, xa + 3, xa + 5, Ink::Set);
        }
        if (yy >= ya + 2 && yy <= yb - 2) fb.hspan(yy, xb + 1, xb + 2, Ink::Set);
      }
      return;
    }
    case Overlay::SpeedLines: {
      const float ph = frac(t * 2.5f);
      for (int k = 0; k < 3; ++k) {
        const float y = 18.0f + 13.0f * k;
        const float off = frac(ph + k * 0.37f) * 10.0f;
        capsule(fb, kL + off, y, kL + 7.0f + off * 0.5f, y, 2.5f);
        capsule(fb, kR - 1.0f - off, y, kR - 8.0f - off * 0.5f, y, 2.5f);
      }
      return;
    }
    case Overlay::Bubble: {
      const float ph = frac(t / 3.0f);
      const float r = (2.0f + 6.0f * ease_in_out(ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f)) * s;
      const float x = cx + 7.0f + r * 0.6f, y = (ly + ry) * 0.5f + 9.0f;
      ellipse_ring(fb, x, y, r, r, 2.0f);
      return;
    }
    case Overlay::Halo: {
      const float y = kT + 4.0f + fsin(t * kTau * 0.5f);
      ellipse_ring(fb, cx, y, 14.0f * s, 3.5f * s, 2.2f);
      return;
    }
    case Overlay::Horns: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float bx = a.ex[i] - side * a.ew[i] * 0.32f, by = a.ey[i] - a.eh[i] * 0.5f - 3.0f;
        Vec2 p[3] = {{bx - 4.0f * s, by + 1.0f}, {bx + 4.0f * s, by + 1.0f}, {bx - side * 4.0f * s, by - 8.0f * s}};
        fill_polygon(fb, p, 3);
      }
      return;
    }
    case Overlay::Glasses: {
      for (int i = 0; i < 2; ++i) {
        const float ex = a.ex[i], ey = a.ey[i] + 1.0f;
        const float hw = (a.ew[i] * 0.5f + 5.0f) * s, hh = (a.ew[i] + 10.0f) * 0.3f * s;
        const int xa = iround(ex - hw), xb = iround(ex + hw) - 1, ya = iround(ey - hh), yb = iround(ey + hh) - 1;
        for (int y = ya; y <= yb; ++y) {
          for (int x = xa; x <= xb; ++x) {
            const float dx = (float)x + 0.5f - ex, dy = (float)y + 0.5f - ey;
            if (in_rrect(dx, dy, hw, hh, 3.0f) && !in_rrect(dx, dy, hw - 2.0f, hh - 2.0f, 1.0f)) fb.set(x, y);
          }
        }
      }
      const float by = fmin_(a.ey[0], a.ey[1]) - 2.0f;
      capsule(fb, a.ex[0] + a.ew[0] * 0.5f + 5.0f * s, by, a.ex[1] - a.ew[1] * 0.5f - 5.0f * s, by, 2.0f);
      const float lend = a.ex[0] - a.ew[0] * 0.5f - 5.0f * s, rend = a.ex[1] + a.ew[1] * 0.5f + 5.0f * s;
      capsule(fb, fmax_(kL, lend - 10.0f), by - 2.0f, lend, by, 2.5f);
      capsule(fb, rend, by, fmin_(kR - 1.0f, rend + 10.0f), by - 2.0f, 2.5f);
      return;
    }
    case Overlay::Nose: {
      const float nx = cx + 1.0f, ny1 = a.my - 6.0f, ny0 = ny1 - 8.0f * s;
      bezier(fb, Vec2{nx + 1.0f, ny0}, Vec2{nx - 5.0f * s, ny1 - 1.0f}, Vec2{nx - 2.0f, ny1}, 2.0f);
      capsule(fb, nx - 2.0f, ny1, nx + 2.0f * s, ny1 - 1.0f, 2.0f);
      return;
    }
    case Overlay::Stubble: {
      // Щетина — ровный пунктир точками 2×2 по линии челюсти: две дуги под ртом от щеки
      // до щеки, внутренняя со сдвигом на полшага. Регулярный узор читается как борода,
      // а не как пыль на экране, и едет вместе с лицом.
      const float jx = a.mx, jy = a.my - 7.0f;
      for (int row = 0; row < 2; ++row) {
        const float ax = 40.0f - 7.0f * (float)row, ay = 17.0f - 5.0f * (float)row;
        const int n = 16 - 3 * row;
        for (int i = 0; i <= n; ++i) {
          const float k = ((float)i + 0.5f * (float)row) / (float)n;
          if (k > 1.0f) continue;
          const float ang = kPi * (0.06f + 0.88f * k);
          const int x = iround(jx + ax * s * fcos(ang)), y = iround(jy + ay * s * fsin(ang));
          if (x < (int)kL || x + 1 > (int)kR - 1 || y < (int)kT || y + 1 > (int)kB - 1) continue;
          if (fabs_((float)x - a.mx) < a.mw * 0.5f + 3.0f && fabs_((float)y - a.my) < 7.0f) continue;  // рот
          fb.set(x, y);
          fb.set(x + 1, y);
          fb.set(x, y + 1);
          fb.set(x + 1, y + 1);
        }
      }
      return;
    }
    case Overlay::Stick: {
      const float sway = 1.5f * fsin(t * kTau / 2.8f);
      const float x0 = a.mx + 1.0f, y0 = a.my + 1.0f, x1 = x0 + sway, y1 = fmin_(y0 + 12.0f * s, kB - 8.0f);
      capsule(fb, x0, y0, x1, y1, 2.5f);
      fill_ellipse(fb, x1, y1 + 2.5f, 3.2f * s, 4.2f * s);
      return;
    }
    case Overlay::EyeBags: {
      for (int i = 0; i < 2; ++i) {
        arc(fb, a.ex[i], a.ey[i] + a.eh[i] * 0.5f + 1.0f, a.ew[i] * 0.32f * s, 2.5f * s, 0.15f * kPi, 0.85f * kPi,
            2.5f);
      }
      return;
    }
    case Overlay::ShrugArms: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float edge = side > 0 ? kL + 1.0f : kR - 2.0f;
        const float yt = fmin_(a.ey[0], a.ey[1]) - 4.0f * s, yb = a.my + 1.0f;
        Vec2 p[4] = {
            {edge, yt}, {edge + side * 8.0f * s, yt}, {edge + side * 17.0f * s, yb}, {edge + side * 25.0f * s, yb}};
        polyline(fb, p, 4, 2.5f);
      }
      return;
    }
    case Overlay::Explosion: {
      const float bx = cx, by = kT + 9.0f;
      const float ph = frac(t / 0.6f);
      for (int k = 0; k < 9; ++k) {
        const float ang = kPi + kPi * (float)k / 8.0f;
        const float r0 = 4.0f + 2.0f * ph, r1 = r0 + (4.0f + 3.0f * ((k & 1) ? 0.5f : 1.0f)) * s;
        capsule(fb, bx + r0 * 2.2f * fcos(ang), by + r0 * fsin(ang) + 4.0f, bx + r1 * 2.2f * fcos(ang),
                by + r1 * fsin(ang) + 4.0f, 2.0f);
      }
      return;
    }
    case Overlay::Bat: {
      // Летает туда и обратно за 6 с. Крылья (7·s по бокам) не заходят в поле у краёв.
      const float ph = frac(t / 6.0f), there = ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f;
      const float x = kL + 9.0f + (kR - kL - 18.0f) * there, y = kT + 7.0f + 2.0f * fsin(t * kTau);
      const bool up = frac(t * 4.0f) < 0.5f;
      fill_ellipse(fb, x, y, 2.0f * s, 2.5f * s);
      const float wy = up ? -4.0f : 3.0f;
      Vec2 l[3] = {{x - 1.0f, y - 1.0f}, {x - 7.0f * s, y + wy * s}, {x - 1.0f, y + 1.5f}};
      Vec2 r[3] = {{x + 1.0f, y - 1.0f}, {x + 7.0f * s, y + wy * s}, {x + 1.0f, y + 1.5f}};
      fill_polygon(fb, l, 3);
      fill_polygon(fb, r, 3);
      return;
    }
    case Overlay::GameOver: {
      if (s < 0.5f) return;
      draw_text_centered(fb, iround(cx), (int)kB - 10, "GAME OVER");
      return;
    }
    case Overlay::QueasyLines: {
      for (int i = 0; i < 2; ++i) {
        const float top = a.ey[i] - a.eh[i] * 0.5f - 11.0f * s;
        for (int k = -1; k <= 1; ++k) {
          const float x = a.ex[i] + k * 6.0f;
          Vec2 p[5];
          for (int j = 0; j < 5; ++j) p[j] = Vec2{x + ((j & 1) ? 1.5f : -1.5f), top + j * 2.0f * s};
          polyline(fb, p, 5, 2.5f);
        }
      }
      return;
    }
    case Overlay::Count: return;
  }
}

namespace {

void corner_rect(Framebuffer& fb, int x0, int y0, int x1, int y1) {
  for (int y = y0; y <= y1; ++y) fb.hspan(y, x0, x1, Ink::Set);
}

}  // namespace

void render_theme(Framebuffer& fb, const Anchors& a, float reticle, bool cyber_eye, bool glint) {
  const int lo_x = kSafeMargin, hi_x = kWidth - 1 - kSafeMargin, lo_y = kSafeMargin, hi_y = kHeight - 1 - kSafeMargin;
  if (reticle >= 0.0f) {
    // Прицел вокруг всего лица: уголки съезжаются к нему — «цель захвачена».
    const float pad = 3.0f + 10.0f * (1.0f - clampf(reticle, 0.0f, 1.0f));
    float fx0 = a.mx - a.mw * 0.5f, fx1 = a.mx + a.mw * 0.5f, fy0 = a.my, fy1 = a.my + 6.0f;
    for (int i = 0; i < 2; ++i) {
      fx0 = fmin_(fx0, a.ex[i] - a.ew[i] * 0.5f), fx1 = fmax_(fx1, a.ex[i] + a.ew[i] * 0.5f);
      fy0 = fmin_(fy0, a.ey[i] - fmax_(a.eh[i] * 0.5f, 6.0f) - 6.0f);  // место под брови
    }
    const int x0 = imax(iround(fx0 - pad - 6.0f), lo_x), x1 = imin(iround(fx1 + pad + 6.0f), hi_x);
    const int y0 = imax(iround(fy0 - pad), lo_y), y1 = imin(iround(fy1 + pad), hi_y);
    const int len = 7;
    corner_rect(fb, x0, y0, x0 + len, y0 + 1);
    corner_rect(fb, x0, y0, x0 + 1, y0 + len);
    corner_rect(fb, x1 - len, y0, x1, y0 + 1);
    corner_rect(fb, x1 - 1, y0, x1, y0 + len);
    corner_rect(fb, x0, y1 - 1, x0 + len, y1);
    corner_rect(fb, x0, y1 - len, x0 + 1, y1);
    corner_rect(fb, x1 - len, y1 - 1, x1, y1);
    corner_rect(fb, x1 - 1, y1 - len, x1, y1);
  }
  if (cyber_eye) {
    // Кибер-глаз: квадратный зрачок рисует само лицо, здесь — вспышка раз в 5 с
    // и риски вокруг глаза, пока он «сканирует» вместе с прицелом.
    const float ex = a.ex[1], ey = a.ey[1];
    const float rx = a.ew[1] * 0.5f + 3.0f, ry = fmax_(a.eh[1] * 0.5f, 5.0f) + 3.0f;
    if (reticle >= 0.0f) {
      const int cx = iround(ex), cy = iround(ey);
      corner_rect(fb, cx - 1, imax(iround(ey - ry) - 3, lo_y), cx, iround(ey - ry));
      corner_rect(fb, imax(iround(ex - rx) - 3, lo_x), cy - 1, iround(ex - rx), cy);
      corner_rect(fb, iround(ex + rx), cy - 1, imin(iround(ex + rx) + 3, hi_x), cy);
    }
    if (glint)
      sparkle_at(fb, fmin_(ex + rx + 2.0f, (float)hi_x - 4.0f), fmax_(ey - ry - 1.0f, (float)lo_y + 4.0f), 4.0f);
  }
}

}  // namespace face
