// Символические значки поверх лица. Каждый рисуется процедурно и привязан
// к глазам или рту, поэтому едет вместе с лицом.
#include "face/raster.h"
#include "face/render.h"

namespace face {

namespace {

inline float frac(float x) { return x - (float)ifloor(x); }
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

// Крошечный шрифт 3×5 для надписей-пасхалок (только нужные буквы).
void text3x5(Framebuffer& fb, int x, int y, const char* s) {
  struct G {
    char c;
    uint8_t rows[5];
  };
  static const G kGlyphs[] = {{'G', {3, 4, 5, 5, 3}}, {'A', {2, 5, 7, 5, 5}}, {'M', {5, 7, 7, 5, 5}},
                              {'E', {7, 4, 6, 4, 7}}, {'O', {2, 5, 5, 5, 2}}, {'V', {5, 5, 5, 5, 2}},
                              {'R', {6, 5, 6, 5, 5}}, {'H', {5, 5, 7, 5, 5}}, {'I', {7, 2, 2, 2, 7}},
                              {'U', {5, 5, 5, 5, 7}}, {'P', {6, 5, 6, 4, 4}}, {'0', {7, 5, 5, 5, 7}},
                              {'1', {2, 6, 2, 2, 7}}, {'4', {5, 5, 7, 1, 1}}};
  for (int i = 0; s[i]; ++i) {
    for (const G& g : kGlyphs) {
      if (g.c != s[i]) continue;
      for (int r = 0; r < 5; ++r)
        for (int c = 0; c < 3; ++c)
          if (g.rows[r] & (4 >> c)) fb.set(x + i * 4 + c, y + r);
    }
  }
}

void sparkle_at(Framebuffer& fb, float x, float y, float r) {
  if (r < 1.0f) return;
  fill_star(fb, x, y, r, fmax_(r * 0.32f, 0.8f), 4, 0.0f);
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

uint16_t overlay_frame_ms(Overlay o) {
  // Длительные состояния (сон, усталость, мало заряда) должны быть дешёвыми:
  // их значки статичны или обновляются редко.
  switch (o) {
    case Overlay::BlushLines:
    case Overlay::BlushDither:
    case Overlay::Shades:
    case Overlay::Horns:
    case Overlay::Gloom:
    case Overlay::Sweat:
    case Overlay::Glasses:
    case Overlay::Nose:
    case Overlay::Stubble:
    case Overlay::EyeBags:
    case Overlay::ShrugArms:
    case Overlay::GameOver:
    case Overlay::QueasyLines: return 0;
    case Overlay::Bat: return 66;
    case Overlay::Explosion: return 50;
    case Overlay::Stick: return 120;
    case Overlay::TearStream:
    case Overlay::DizzyStars:
    case Overlay::SpeedLines:
    case Overlay::Vein:
    case Overlay::Rays: return 50;
    case Overlay::Sparkles:
    case Overlay::SoundWaves:
    case Overlay::Steam:
    case Overlay::Tear: return 66;
    case Overlay::Hearts:
    case Overlay::Notes: return 80;
    case Overlay::Exclaim:
    case Overlay::Question:
    case Overlay::Bulb:
    case Overlay::Bolt: return 100;
    case Overlay::Zzz: return 200;
    default: return 150;
  }
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
          capsule(fb, x - 1.5f * s * side, by + 2.5f * s, x + 1.5f * s * side, by - 2.5f * s, 2.0f);
        }
      }
      return;
    }
    case Overlay::BlushDither: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float bx = a.ex[i] - side * (a.ew[i] * 0.42f), by = a.ey[i] + a.eh[i] * 0.5f + 4.0f;
        fill_ellipse(fb, bx, by, 7.0f * s, 3.5f * s, Paint{Ink::Set, 0.5f});
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
        for (int y = iround(y0); y < kHeight; ++y) {
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
        const float x = cx + side * (52.0f + 2.0f * i) + 2.5f * fsin(kTau * (ph * 1.4f + i * 0.3f));
        const float y = 56.0f - ph * 48.0f;
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
        const float ph = frac(t * 1.4f);
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
        const float x = cx + side * (50.0f - ph * 4.0f) + 2.0f * fsin(kTau * ph * 2.0f);
        const float y = 54.0f - ph * 40.0f;
        const float k = s * (ph > 0.85f ? (1.0f - ph) / 0.15f : 1.0f);
        if (k > 0.3f) note(fb, x, y, k);
      }
      return;
    }
    case Overlay::Question: {
      const float bob = fsin(t * kTau * 0.8f) * 1.5f;
      const float x = rx + rw * 0.5f + 9.0f, y = ry - rh * 0.5f + 1.0f + bob;
      const float k = s;
      arc(fb, x, y - 4.0f * k, 3.6f * k, 3.6f * k, kPi * 1.0f, kPi * 2.45f, 2.0f);
      capsule(fb, x + 0.5f * k, y, x + 0.5f * k, y + 2.5f * k, 2.0f);
      fill_circle(fb, x + 0.5f * k, y + 6.5f * k, 1.4f * k);
      return;
    }
    case Overlay::Exclaim: {
      const float jig = fsin(t * kTau * 3.0f) * (t < 0.6f ? 1.0f : 0.0f);
      const float x = lx - lw * 0.5f - 9.0f + jig, y = ly - lh * 0.5f + 2.0f;
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
      const int xa = iround(lx - lw * 0.5f), xb = iround(rx + rw * 0.5f);
      const int ytop = iround(fmin_(ly - lh * 0.5f, ry - rh * 0.5f));
      int k = 0;
      for (int x = xa; x <= xb; x += 4, ++k) {
        const int len = iround((float)(ytop + 3) * s * ((k & 1) ? 0.6f : 1.0f));
        for (int y = 0; y < len; ++y) fb.set(x, y);
      }
      return;
    }
    case Overlay::DizzyStars: {
      const float ccx = cx, ccy = fmin_(ly - lh * 0.5f, ry - rh * 0.5f) - 4.0f;
      for (int i = 0; i < 3; ++i) {
        const float ang = t * 3.2f + kTau * i / 3.0f;
        const float depth = 0.5f + 0.5f * fsin(ang);
        const float x = ccx + 30.0f * fcos(ang);
        const float y = ccy + 3.5f * fsin(ang);
        const float r = (2.2f + 1.6f * depth) * s;
        fill_star(fb, x, fmax_(y, r), r, r * 0.45f, 5, ang);
      }
      return;
    }
    case Overlay::Bulb: {
      const float x = rx + rw * 0.5f + 8.0f, y = ry - rh * 0.5f - 1.0f;
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
      capsule(fb, 0.0f, top + 1.0f, lx - lw * 0.5f - 3.0f, top + 1.0f, 2.0f);
      capsule(fb, rx + rw * 0.5f + 3.0f, top + 1.0f, (float)kWidth, top + 1.0f, 2.0f);
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
      const float pts[2][2] = {{lx - lw * 0.5f - 8.0f, 9.0f}, {rx + rw * 0.5f + 8.0f, 14.0f}};
      for (int i = 0; i < 2; ++i) {
        const float x = pts[i][0], y = pts[i][1] + 2.0f * fsin(t * 1.5f + i);
        const float r = 4.0f * s;
        for (int k = 0; k < 3; ++k) {
          float ang = t * 0.8f + kPi * (float)k / 3.0f;
          capsule(fb, x - r * fcos(ang), y - r * fsin(ang), x + r * fcos(ang), y + r * fsin(ang), 1.5f);
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
      const float x = cx, y = 5.0f;
      const int xa = iround(x - 8 * s), xb = iround(x + 6 * s), ya = iround(y - 3.5f * s), yb = iround(y + 3.5f * s);
      for (int xx = xa; xx <= xb; ++xx) {
        fb.set(xx, ya);
        fb.set(xx, yb);
      }
      for (int yy = ya; yy <= yb; ++yy) {
        fb.set(xa, yy);
        fb.set(xb, yy);
      }
      for (int yy = ya + 2; yy <= yb - 2; ++yy) {
        fb.set(xb + 1, yy);
        fb.set(xb + 2, yy);
        fb.hspan(yy, xa + 2, xa + 3, Ink::Set);
      }
      return;
    }
    case Overlay::SpeedLines: {
      const float ph = frac(t * 2.5f);
      for (int k = 0; k < 3; ++k) {
        const float y = 18.0f + 13.0f * k;
        const float off = frac(ph + k * 0.37f) * 10.0f;
        capsule(fb, 1.0f + off, y, 8.0f + off * 0.5f, y, 1.5f);
        capsule(fb, 126.0f - off, y, 119.0f - off * 0.5f, y, 1.5f);
      }
      return;
    }
    case Overlay::Bubble: {
      const float ph = frac(t / 3.0f);
      const float r = (2.0f + 6.0f * ease_in_out(ph < 0.5f ? ph * 2.0f : 2.0f - ph * 2.0f)) * s;
      const float x = cx + 7.0f + r * 0.6f, y = (ly + ry) * 0.5f + 9.0f;
      ellipse_ring(fb, x, y, r, r, 1.5f);
      return;
    }
    case Overlay::Halo: {
      const float y = 3.5f + fsin(t * kTau * 0.5f);
      ellipse_ring(fb, cx, y, 14.0f * s, 3.2f * s, 1.6f);
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
      capsule(fb, 0.0f, by - 2.0f, a.ex[0] - a.ew[0] * 0.5f - 5.0f * s, by, 2.0f);
      capsule(fb, a.ex[1] + a.ew[1] * 0.5f + 5.0f * s, by, (float)kWidth, by - 2.0f, 2.0f);
      return;
    }
    case Overlay::Nose: {
      const float nx = cx + 1.0f, ny1 = a.my - 6.0f, ny0 = ny1 - 8.0f * s;
      bezier(fb, Vec2{nx + 1.0f, ny0}, Vec2{nx - 5.0f * s, ny1 - 1.0f}, Vec2{nx - 2.0f, ny1}, 2.0f);
      capsule(fb, nx - 2.0f, ny1, nx + 2.0f * s, ny1 - 1.0f, 2.0f);
      return;
    }
    case Overlay::Stubble: {
      const int ox = iround(a.ox), oy = iround(a.oy);
      const float ecx = a.mx, ecy = a.my - 16.0f;
      for (int y = imax(iround(a.my - 8.0f), 0); y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
          const float dx = (float)x - ecx, dy = (float)y - ecy;
          const bool jaw = in_ellipse(dx, dy, 60.0f, 34.0f) && !in_ellipse(dx, dy, 44.0f, 18.0f);
          const bool chin = fabs_(dx) < 16.0f && (float)y > a.my + 7.0f;
          if (!jaw && !chin) continue;
          if (fabs_((float)x - a.mx) < a.mw * 0.5f + 4.0f && fabs_((float)y - a.my) < 7.0f) continue;
          // Щетина — редкие короткие вертикальные «волоски» по хэшу, без регулярной штриховки.
          const int xr = x - ox, yr = y - oy;
          if ((yr & 1) || (xr & 1)) continue;
          uint32_t hsh = (uint32_t)(xr * 73856093) ^ (uint32_t)(yr * 19349663);
          hsh ^= hsh >> 13;
          hsh *= 0x5bd1e995u;
          hsh ^= hsh >> 15;
          if ((hsh & 255u) < (uint32_t)(70.0f * s)) {
            fb.set(x, y);
            fb.set(x + ((hsh >> 9) & 1u), y + 1);
          }
        }
      }
      return;
    }
    case Overlay::Stick: {
      const float sway = 1.5f * fsin(t * 2.2f);
      const float x0 = a.mx + 1.0f, y0 = a.my + 1.0f, x1 = x0 + sway, y1 = fmin_(y0 + 12.0f * s, 57.0f);
      capsule(fb, x0, y0, x1, y1, 2.5f);
      fill_ellipse(fb, x1, y1 + 2.5f, 3.2f * s, 4.2f * s);
      return;
    }
    case Overlay::EyeBags: {
      for (int i = 0; i < 2; ++i) {
        arc(fb, a.ex[i], a.ey[i] + a.eh[i] * 0.5f + 1.0f, a.ew[i] * 0.32f * s, 2.5f * s, 0.15f * kPi, 0.85f * kPi,
            1.5f);
      }
      return;
    }
    case Overlay::ShrugArms: {
      for (int i = 0; i < 2; ++i) {
        const int side = i == 0 ? 1 : -1;
        const float edge = side > 0 ? 1.0f : (float)kWidth - 2.0f;
        const float yt = fmin_(a.ey[0], a.ey[1]) - 4.0f * s, yb = a.my + 1.0f;
        Vec2 p[4] = {
            {edge, yt}, {edge + side * 8.0f * s, yt}, {edge + side * 17.0f * s, yb}, {edge + side * 25.0f * s, yb}};
        polyline(fb, p, 4, 2.0f);
      }
      return;
    }
    case Overlay::Explosion: {
      const float bx = cx, by = 4.0f;
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
      const float ph = frac(t / 3.0f);
      const float x = -10.0f + ph * 148.0f, y = 7.0f + 2.0f * fsin(t * 6.0f);
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
      text3x5(fb, iround(cx) - 17, kHeight - 6, "GAME OVER");
      return;
    }
    case Overlay::QueasyLines: {
      for (int i = 0; i < 2; ++i) {
        const float top = a.ey[i] - a.eh[i] * 0.5f - 11.0f * s;
        for (int k = -1; k <= 1; ++k) {
          const float x = a.ex[i] + k * 6.0f;
          Vec2 p[5];
          for (int j = 0; j < 5; ++j) p[j] = Vec2{x + ((j & 1) ? 1.5f : -1.5f), top + j * 2.0f * s};
          polyline(fb, p, 5, 1.5f);
        }
      }
      return;
    }
    case Overlay::Count: return;
  }
}

}  // namespace face
