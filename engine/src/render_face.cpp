// Глаза, брови и рот.
#include "face/raster.h"
#include "face/render.h"

namespace face {

namespace {

constexpr float kCx = kWidth * 0.5f;

// Горизонтальная чёрточка закрытого глаза.
void closed_line(Framebuffer& fb, float cx, float cy, float w, float thick) {
  float hw = fmax_(w * 0.5f - thick * 0.5f, 1.0f);
  capsule(fb, cx - hw, cy, cx + hw, cy, thick);
}

void draw_rrect_eye(Framebuffer& fb, const Eye& e, float cx, float cy, int side, float gx, float gy) {
  const float w = e.w * e.sx * e.stretch;
  const float h = e.h * e.sy * e.squash;
  const float hw = w * 0.5f, hh = h * 0.5f;
  const float stroke = e.t;
  if (h < 3.5f) {
    closed_line(fb, cx, cy, w, stroke >= 3.0f ? 3.0f : 2.0f);
    return;
  }
  const float r = fmin_(fmin_(e.r * fmax_(e.sx, e.sy), hw), hh);
  const float ihw = hw - stroke, ihh = hh - stroke;
  const bool hollow = ihw > 0.75f && ihh > 0.75f;
  const float ir = fmin_(fmin_(fmax_(r - stroke, 0.0f), ihw), ihh);

  const bool has_up = e.lid_cov > 0.001f || fabs_(e.lid_ang) > 0.1f || fabs_(e.lid_bend) > 0.001f;
  const float slope = ftan(e.lid_ang * kDeg) * hw / hh;
  const bool has_low = e.low_cov > 0.001f || fabs_(e.low_bend) > 0.001f;
  const float edge = hollow ? clampf(stroke, 2.0f, 4.0f) / hh : 0.0f;

  // Зрачок или блик смещаются в сторону взгляда.
  const float room = hollow ? fmax_(fmin_(ihw, ihh) - e.pr - 0.5f, 0.0f) : fmax_(fmin_(hw, hh) - e.pr - 2.0f, 0.0f);
  const float follow = hollow ? 1.0f : 0.45f;
  const float pcx = cx + side * e.px * e.sx + gx * room * follow;
  float pcy = cy + e.py * e.sy * e.squash + gy * room * follow;
  if (hollow && e.pr > 0.0f) {
    // Зрачок не прячется целиком под веком: выглядывает из-под него, как у живого глаза.
    if (has_up) {
      const float lid_y = cy + (-1.0f + 2.0f * e.lid_cov - e.lid_bend) * hh;
      pcy = fmax_(pcy, lid_y + clampf(stroke, 2.0f, 4.0f) + e.pr * 0.35f);
    }
    if (has_low) {
      const float low_y = cy + (1.0f - 2.0f * e.low_cov - e.low_bend) * hh;
      pcy = fmin_(pcy, low_y - clampf(stroke, 2.0f, 4.0f) - e.pr * 0.35f);
    }
  }
  const float prx = e.pr * e.sx, pry = e.pr * e.sy * e.squash;
  const float g2x = cx + side * e.gx * e.sx + gx * room * follow;
  const float g2y = cy + e.gy * e.sy * e.squash + gy * room * follow;
  const float g2rx = e.gr * e.sx, g2ry = e.gr * e.sy * e.squash;

  const int x0 = imax(ifloor(cx - hw - 1), 0), x1 = imin(iceil(cx + hw + 1), kWidth - 1);
  const int y0 = imax(ifloor(cy - hh - 1), 0), y1 = imin(iceil(cy + hh + 1), kHeight - 1);
  for (int y = y0; y <= y1; ++y) {
    const float py = (float)y + 0.5f;
    const float dy = py - cy;
    const float v = dy / hh;
    for (int x = x0; x <= x1; ++x) {
      const float px = (float)x + 0.5f;
      const float dx = px - cx;
      if (!in_rrect(dx, dy, hw, hh, r)) continue;
      const float u = dx / hw;
      float lid_v = -2.0f, low_v = 2.0f;
      if (has_up) {
        lid_v = -1.0f + 2.0f * e.lid_cov + slope * (u * side) - e.lid_bend * (1.0f - u * u);
        if (v < lid_v) continue;
      }
      if (has_low) {
        low_v = 1.0f - 2.0f * e.low_cov - e.low_bend * (1.0f - u * u);
        if (v > low_v) continue;
      }
      bool on;
      if (!hollow) {
        on = true;
        if (e.pr > 0.0f && in_ellipse(px - pcx, py - pcy, prx, pry)) on = false;
        if (e.gr > 0.0f && in_ellipse(px - g2x, py - g2y, g2rx, g2ry)) on = false;
      } else {
        on = !in_rrect(dx, dy, ihw, ihh, ir);
        if (!on && has_up && v < lid_v + edge) on = true;
        if (!on && has_low && v > low_v - edge) on = true;
        if (!on && e.pr > 0.0f && in_ellipse(px - pcx, py - pcy, prx, pry)) on = true;
      }
      if (on) fb.set(x, y);
    }
  }
}

void draw_spiral(Framebuffer& fb, float cx, float cy, float hw, float hh, float t, float rot_deg) {
  const float R = fmax_(fmin_(hw, hh), 1.0f);
  const float turns = 2.3f;
  const float a = R / (turns * kTau);
  const float ht = t * 0.5f + 0.15f;
  const float rot = rot_deg * kDeg;
  const int x0 = imax(ifloor(cx - hw - 1), 0), x1 = imin(iceil(cx + hw + 1), kWidth - 1);
  const int y0 = imax(ifloor(cy - hh - 1), 0), y1 = imin(iceil(cy + hh + 1), kHeight - 1);
  for (int y = y0; y <= y1; ++y) {
    float dy = ((float)y + 0.5f - cy) * (hw / fmax_(hh, 0.5f));
    for (int x = x0; x <= x1; ++x) {
      float dx = (float)x + 0.5f - cx;
      float rho = fsqrt(dx * dx + dy * dy);
      if (rho > R + ht) continue;
      if (rho <= ht) {
        fb.set(x, y);
        continue;
      }
      float th = fatan2(dy, dx) - rot;
      th -= kTau * (float)ifloor(th / kTau);
      float s = rho / a - th;
      int k = iround(s / kTau);
      if (k < 0) k = 0;
      float rk = a * (th + kTau * (float)k);
      if (fabs_(rho - rk) <= ht && rk <= R + 0.5f) fb.set(x, y);
    }
  }
}

void draw_star_eye(Framebuffer& fb, float cx, float cy, float rx, float ry, float inner, int points, float rot_deg) {
  Vec2 v[24];
  int n = points * 2;
  for (int i = 0; i < n; ++i) {
    float a = rot_deg * kDeg - kPi * 0.5f + kPi * (float)i / (float)points;
    float k = (i & 1) ? inner : 1.0f;
    v[i] = Vec2{cx + rx * k * fcos(a), cy + ry * k * fsin(a)};
  }
  fill_polygon(fb, v, n);
}

void draw_eye(Framebuffer& fb, const Eye& e, float cx, float cy, int side, float gx, float gy) {
  const float w = e.w * e.sx * e.stretch;
  const float h = e.h * e.sy * e.squash;
  const float t = e.t;
  switch (e.kind) {
    case EyeKind::None: return;
    case EyeKind::RRect: draw_rrect_eye(fb, e, cx, cy, side, gx, gy); return;
    case EyeKind::ArcUp: {
      if (h < 3.0f) {
        closed_line(fb, cx, cy, w, t);
        return;
      }
      float rx = fmax_(w * 0.5f - t * 0.5f, 1.0f), ry = fmax_(h - t * 0.5f, 1.0f);
      arc(fb, cx, cy + h * 0.5f - t * 0.5f, rx, ry - t * 0.5f, kPi, kTau, t);
      return;
    }
    case EyeKind::ArcDown: {
      if (h < 3.0f) {
        closed_line(fb, cx, cy, w, t);
        return;
      }
      float rx = fmax_(w * 0.5f - t * 0.5f, 1.0f), ry = fmax_(h - t * 0.5f, 1.0f);
      arc(fb, cx, cy - h * 0.5f + t * 0.5f, rx, ry - t * 0.5f, 0.0f, kPi, t);
      return;
    }
    case EyeKind::Chevron: {
      float hx = fmax_(w * 0.5f - t * 0.5f, 1.0f), hy = fmax_(h * 0.5f - t * 0.5f, 0.0f);
      Vec2 p[3] = {{cx - side * hx, cy - hy}, {cx + side * hx, cy}, {cx - side * hx, cy + hy}};
      polyline(fb, p, 3, t);
      return;
    }
    case EyeKind::Heart:
      if (h < 3.5f) {
        closed_line(fb, cx, cy, w, 3.0f);
      } else if (t * 2.0f >= fmin_(w, h)) {
        fill_heart(fb, cx, cy, w * 0.5f, h * 0.5f);
      } else {
        heart_ring(fb, cx, cy, w * 0.5f, h * 0.5f, t);
      }
      return;
    case EyeKind::Star:
      if (h < 3.5f) {
        closed_line(fb, cx, cy, w, 3.0f);
        return;
      }
      draw_star_eye(fb, cx, cy, w * 0.5f, h * 0.5f, 0.45f, 5, e.rot);
      return;
    case EyeKind::Sparkle:
      if (h < 3.5f) {
        closed_line(fb, cx, cy, w, 3.0f);
        return;
      }
      draw_star_eye(fb, cx, cy, w * 0.5f, h * 0.5f, 0.3f, 4, e.rot);
      return;
    case EyeKind::Spiral:
      if (h < 3.5f) {
        closed_line(fb, cx, cy, w, 3.0f);
        return;
      }
      draw_spiral(fb, cx, cy, w * 0.5f, h * 0.5f, t, e.rot * (float)side);
      return;
    case EyeKind::Cross: {
      float hx = fmax_(w * 0.5f - t * 0.5f, 1.0f), hy = fmax_(h * 0.5f - t * 0.5f, 0.0f);
      capsule(fb, cx - hx, cy - hy, cx + hx, cy + hy, t);
      capsule(fb, cx - hx, cy + hy, cx + hx, cy - hy, t);
      return;
    }
  }
}

void draw_brow(Framebuffer& fb, const Brow& b, const Eye& e, float ex, float ey, int side) {
  if (b.len < 1.0f || b.t < 0.5f) return;
  const float top = ey - e.h * e.sy * 0.5f;
  const float cx = ex + side * b.dx;
  const float cy = top - b.gap - b.t * 0.5f;
  const float half = b.len * 0.5f;
  const float a = b.ang * kDeg;
  const float ca = fcos(a), sa = fsin(a);
  Vec2 inner{cx + side * half * ca, cy - half * sa};
  Vec2 outer{cx - side * half * ca, cy + half * sa};
  Vec2 ctrl{(inner.x + outer.x) * 0.5f, (inner.y + outer.y) * 0.5f - 2.0f * b.bend};
  bezier(fb, inner, ctrl, outer, b.t, Paint{}, 8);
}

void draw_curve_mouth(Framebuffer& fb, const Mouth& m, float mx, float my) {
  const float hw = fmax_(m.w * m.sx * 0.5f, 1.0f);
  const float t = m.t;
  const float c = m.curve, o = fmax_(m.open, 0.0f);
  const float lo = fmin_(c, 0.0f) - t - fabs_(m.tilt) - 1.0f;
  const float hi = fmax_(c + o, 0.0f) + t + fabs_(m.tilt) + 1.0f;
  const int x0 = imax(ifloor(mx - hw - t), 0), x1 = imin(iceil(mx + hw + t), kWidth - 1);
  const int y0 = imax(ifloor(my + lo), 0), y1 = imin(iceil(my + hi), kHeight - 1);
  const float cap_r2 = t * t * 0.25f;
  for (int x = x0; x <= x1; ++x) {
    const float dx = (float)x + 0.5f - mx;
    // Верхняя кромка — парабола, нижняя у открытого рта — полуэллипс: «D» получается круглым.
    const float u = dx / hw, q = fmax_(1.0f - u * u, 0.0f);
    const float base = my + m.tilt * u;
    const float top = base + c * q - t * 0.5f;
    const float bot = base + c * q + o * fsqrt(q) + t * 0.5f;
    for (int y = y0; y <= y1; ++y) {
      const float py = (float)y + 0.5f;
      bool on = false;
      if (fabs_(dx) <= hw) {
        on = py >= top && py <= bot;
      } else {
        const float ex = dx > 0 ? hw : -hw;
        const float ey = my + (dx > 0 ? m.tilt : -m.tilt);
        const float ddx = dx - ex, ddy = py - ey;
        on = ddx * ddx + ddy * ddy <= cap_r2;
      }
      if (on) fb.set(x, y);
    }
  }
}

void draw_mouth(Framebuffer& fb, const Mouth& m, float mx, float my) {
  const float w = m.w * m.sx;
  const float hw = w * 0.5f;
  const float t = m.t;
  switch (m.kind) {
    case MouthKind::None: return;
    case MouthKind::Curve: draw_curve_mouth(fb, m, mx, my); return;
    case MouthKind::Ring: ellipse_ring(fb, mx, my, hw, m.h * m.sx * 0.5f, t); return;
    case MouthKind::Wavy: {
      Vec2 p[17];
      const float waves = 4.0f;
      for (int i = 0; i <= 16; ++i) {
        float u = (float)i / 16.0f * 2.0f - 1.0f;
        p[i] = Vec2{mx + u * hw, my + m.h * fsin(kPi * waves * (u + 1.0f) * 0.5f)};
      }
      polyline(fb, p, 17, t);
      return;
    }
    case MouthKind::Cat: {
      const float q = w * 0.25f, hh = m.h * m.sx;
      arc(fb, mx - q, my - hh * 0.5f, q, hh, 0.0f, kPi, t);
      arc(fb, mx + q, my - hh * 0.5f, q, hh, 0.0f, kPi, t);
      return;
    }
    case MouthKind::Pucker: {
      const float q = m.h * m.sx * 0.25f;
      arc(fb, mx - q * 0.5f, my - q, q, q, -kPi * 0.5f, kPi * 0.5f, t);
      arc(fb, mx - q * 0.5f, my + q, q, q, -kPi * 0.5f, kPi * 0.5f, t);
      return;
    }
    case MouthKind::Teeth: {
      const float hh = m.h * m.sx * 0.5f;
      const float x0 = mx - hw, x1 = mx + hw;
      const int xa = iround(x0), xb = iround(x1) - 1;
      const int ya = iround(my - hh), yb = iround(my + hh) - 1;
      for (int x = xa; x <= xb; ++x) {
        fb.set(x, ya);
        fb.set(x, ya + 1);
        fb.set(x, yb);
        fb.set(x, yb - 1);
      }
      for (int y = ya; y <= yb; ++y) {
        fb.set(xa, y);
        fb.set(xa + 1, y);
        fb.set(xb, y);
        fb.set(xb - 1, y);
      }
      const int mid = iround(my) - 0;
      fb.hspan(mid, xa, xb, Ink::Set);
      for (int x = xa + 4; x < xb - 2; x += 4) {
        for (int y = ya; y <= yb; ++y) fb.set(x, y);
      }
      return;
    }
    case MouthKind::Tongue: {
      Mouth line = m;
      line.kind = MouthKind::Curve;
      line.open = 0;
      draw_curve_mouth(fb, line, mx, my);
      const float tx = mx + hw * 0.2f;
      const float ty = my + m.curve * 0.9f + t * 0.5f;
      const float rx = fmax_(w * 0.2f, 3.0f), ry = m.h * m.sx;
      for (int y = iround(ty); y <= iround(ty + ry); ++y) {
        for (int x = ifloor(tx - rx); x <= iceil(tx + rx); ++x) {
          if (in_ellipse((float)x + 0.5f - tx, (float)y + 0.5f - ty, rx, ry)) fb.set(x, y);
        }
      }
      // Ложбинка посередине языка.
      for (int y = iround(ty + 1); y <= iround(ty + ry * 0.55f); ++y) fb.plot(iround(tx - 0.5f), y, Ink::Clear);
      return;
    }
    case MouthKind::Triangle: {
      const float hh = m.h * m.sx * 0.5f;
      Vec2 p[4] = {{mx - hw, my - hh}, {mx + hw, my - hh}, {mx + hw * 0.35f, my + hh}, {mx - hw * 0.35f, my + hh}};
      fill_polygon(fb, p, 4);
      return;
    }
  }
}

}  // namespace

void render_face(Framebuffer& fb, const Face& f, Anchors* out) {
  const Layout& L = f.layout;
  Anchors a;
  for (int i = 0; i < 2; ++i) {
    const Eye& e = f.eye[i];
    const int side = i == 0 ? 1 : -1;
    // Глаз на стороне взгляда чуть крупнее — иллюзия поворота головы.
    const float base_x = kCx - side * L.spacing * 0.5f + side * e.dx;
    const float base_y = L.eye_y + e.dy + (i == 0 ? -L.tilt : L.tilt);
    a.ex[i] = (float)iround(base_x + f.ox + f.gx * 5.0f);
    a.ey[i] = (float)iround(base_y + f.oy + f.gy * 3.0f);
    a.ew[i] = e.w * e.sx;
    a.eh[i] = e.h * e.sy;
  }
  a.mx = (float)iround(kCx + f.mouth.dx + f.ox + f.gx * 3.0f);
  a.my = (float)iround(L.mouth_y + f.mouth.dy + f.oy + f.gy * 2.0f);
  a.mw = f.mouth.w * f.mouth.sx;
  a.ox = f.ox;
  a.oy = f.oy;

  for (int i = 0; i < 2; ++i) {
    const int side = i == 0 ? 1 : -1;
    Eye e = f.eye[i];
    const float look = f.gx * (float)(-side);  // >0, если взгляд в сторону этого глаза
    e.sx *= 1.0f + 0.05f * look;
    e.sy *= 1.0f + 0.05f * look - 0.04f * fmax_(f.gy, 0.0f);
    draw_eye(fb, e, a.ex[i], a.ey[i], side, f.gx, f.gy);
    draw_brow(fb, f.brow[i], f.eye[i], a.ex[i], a.ey[i], side);
  }
  draw_mouth(fb, f.mouth, a.mx, a.my);
  if (out) *out = a;
}

}  // namespace face
