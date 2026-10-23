#include "face/raster.h"

namespace face {

namespace {

struct Box {
  int x0, y0, x1, y1;
  bool empty() const { return x0 > x1 || y0 > y1; }
};

Box clip_box(float minx, float miny, float maxx, float maxy) {
  Box b{ifloor(minx), ifloor(miny), iceil(maxx), iceil(maxy)};
  b.x0 = imax(b.x0, 0);
  b.y0 = imax(b.y0, 0);
  b.x1 = imin(b.x1, kWidth - 1);
  b.y1 = imin(b.y1, kHeight - 1);
  return b;
}

inline void put(Framebuffer& fb, int x, int y, const Paint& p) {
  if (p.pass(x, y)) fb.plot(x, y, p.ink);
}

// Неявное уравнение сердца (x²+y²−1)³ − x²y³ ≤ 0 в нормированных координатах.
inline bool in_heart(float dx, float dy, float hw, float hh) {
  if (hw <= 0.0f || hh <= 0.0f) return false;
  float x = dx / hw * 1.14f;
  float y = -(dy / hh) * 1.12f + 0.12f;
  float a = x * x + y * y - 1.0f;
  return a * a * a - x * x * y * y * y <= 0.0f;
}

}  // namespace

void fill_rrect(Framebuffer& fb, float cx, float cy, float hw, float hh, float r, Paint p) {
  if (hw <= 0.0f || hh <= 0.0f) return;
  r = clampf(r, 0.0f, fmin_(hw, hh));
  Box b = clip_box(cx - hw - 1, cy - hh - 1, cx + hw + 1, cy + hh + 1);
  for (int y = b.y0; y <= b.y1; ++y) {
    float dy = (float)y + 0.5f - cy;
    for (int x = b.x0; x <= b.x1; ++x) {
      if (in_rrect((float)x + 0.5f - cx, dy, hw, hh, r)) put(fb, x, y, p);
    }
  }
}

void fill_ellipse(Framebuffer& fb, float cx, float cy, float rx, float ry, Paint p) {
  if (rx <= 0.0f || ry <= 0.0f) return;
  Box b = clip_box(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1);
  for (int y = b.y0; y <= b.y1; ++y) {
    float dy = (float)y + 0.5f - cy;
    for (int x = b.x0; x <= b.x1; ++x) {
      if (in_ellipse((float)x + 0.5f - cx, dy, rx, ry)) put(fb, x, y, p);
    }
  }
}

void fill_circle(Framebuffer& fb, float cx, float cy, float r, Paint p) { fill_ellipse(fb, cx, cy, r, r, p); }

void ellipse_ring(Framebuffer& fb, float cx, float cy, float rx, float ry, float t, Paint p) {
  if (rx <= 0.0f || ry <= 0.0f) return;
  float irx = rx - t, iry = ry - t;
  bool hollow = irx > 0.0f && iry > 0.0f;
  Box b = clip_box(cx - rx - 1, cy - ry - 1, cx + rx + 1, cy + ry + 1);
  for (int y = b.y0; y <= b.y1; ++y) {
    float dy = (float)y + 0.5f - cy;
    for (int x = b.x0; x <= b.x1; ++x) {
      float dx = (float)x + 0.5f - cx;
      if (!in_ellipse(dx, dy, rx, ry)) continue;
      if (hollow && in_ellipse(dx, dy, irx, iry)) continue;
      put(fb, x, y, p);
    }
  }
}

void capsule(Framebuffer& fb, float x0, float y0, float x1, float y1, float thick, Paint p) {
  float r = fmax_(thick * 0.5f, 0.5f);
  float r2 = r * r;
  Box b = clip_box(fmin_(x0, x1) - r, fmin_(y0, y1) - r, fmax_(x0, x1) + r, fmax_(y0, y1) + r);
  for (int y = b.y0; y <= b.y1; ++y) {
    float py = (float)y + 0.5f;
    for (int x = b.x0; x <= b.x1; ++x) {
      if (seg_dist2((float)x + 0.5f, py, x0, y0, x1, y1) <= r2) put(fb, x, y, p);
    }
  }
}

void polyline(Framebuffer& fb, const Vec2* pts, int n, float thick, Paint p) {
  if (n == 1) capsule(fb, pts[0].x, pts[0].y, pts[0].x, pts[0].y, thick, p);
  for (int i = 0; i + 1 < n; ++i) capsule(fb, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, thick, p);
}

void fill_polygon(Framebuffer& fb, const Vec2* pts, int n, Paint p) {
  if (n < 3) return;
  float miny = pts[0].y, maxy = pts[0].y;
  for (int i = 1; i < n; ++i) {
    miny = fmin_(miny, pts[i].y);
    maxy = fmax_(maxy, pts[i].y);
  }
  int y0 = imax(ifloor(miny), 0), y1 = imin(iceil(maxy), kHeight - 1);
  float xs[32];
  for (int y = y0; y <= y1; ++y) {
    float yc = (float)y + 0.5f;
    int k = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      float ya = pts[j].y, yb = pts[i].y;
      if ((ya <= yc && yb > yc) || (yb <= yc && ya > yc)) {
        if (k < 32) xs[k++] = pts[j].x + (yc - ya) * (pts[i].x - pts[j].x) / (yb - ya);
      }
    }
    for (int a = 1; a < k; ++a) {
      float v = xs[a];
      int c = a - 1;
      while (c >= 0 && xs[c] > v) {
        xs[c + 1] = xs[c];
        --c;
      }
      xs[c + 1] = v;
    }
    for (int a = 0; a + 1 < k; a += 2) {
      int xa = iceil(xs[a] - 0.5f), xb = ifloor(xs[a + 1] - 0.5f);
      xa = imax(xa, 0);
      xb = imin(xb, kWidth - 1);
      for (int x = xa; x <= xb; ++x) put(fb, x, y, p);
    }
  }
}

void arc(Framebuffer& fb, float cx, float cy, float rx, float ry, float a0, float a1, float thick, Paint p) {
  float span = fabs_(a1 - a0);
  float len = span * fmax_(rx, ry);
  int segs = iclamp((int)(len / 2.5f) + 1, 3, 40);
  Vec2 pts[41];
  for (int i = 0; i <= segs; ++i) {
    float a = a0 + (a1 - a0) * (float)i / (float)segs;
    pts[i] = Vec2{cx + rx * fcos(a), cy + ry * fsin(a)};
  }
  polyline(fb, pts, segs + 1, thick, p);
}

void bezier(Framebuffer& fb, Vec2 a, Vec2 c, Vec2 b, float thick, Paint p, int segments) {
  segments = iclamp(segments, 1, 32);
  Vec2 pts[33];
  for (int i = 0; i <= segments; ++i) {
    float t = (float)i / (float)segments, u = 1.0f - t;
    pts[i] = Vec2{u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y};
  }
  polyline(fb, pts, segments + 1, thick, p);
}

void fill_star(Framebuffer& fb, float cx, float cy, float r_out, float r_in, int points, float rot, Paint p) {
  points = iclamp(points, 3, 12);
  Vec2 v[24];
  int n = points * 2;
  for (int i = 0; i < n; ++i) {
    float a = rot - kPi * 0.5f + kPi * (float)i / (float)points;
    float r = (i & 1) ? r_in : r_out;
    v[i] = Vec2{cx + r * fcos(a), cy + r * fsin(a)};
  }
  fill_polygon(fb, v, n, p);
}

void fill_heart(Framebuffer& fb, float cx, float cy, float hw, float hh, Paint p) {
  Box b = clip_box(cx - hw - 1, cy - hh - 1, cx + hw + 1, cy + hh + 1);
  for (int y = b.y0; y <= b.y1; ++y) {
    float dy = (float)y + 0.5f - cy;
    for (int x = b.x0; x <= b.x1; ++x) {
      if (in_heart((float)x + 0.5f - cx, dy, hw, hh)) put(fb, x, y, p);
    }
  }
}

void heart_ring(Framebuffer& fb, float cx, float cy, float hw, float hh, float t, Paint p) {
  Box b = clip_box(cx - hw - 1, cy - hh - 1, cx + hw + 1, cy + hh + 1);
  float ihw = hw - t * 1.1f, ihh = hh - t * 1.1f;
  for (int y = b.y0; y <= b.y1; ++y) {
    float dy = (float)y + 0.5f - cy;
    for (int x = b.x0; x <= b.x1; ++x) {
      float dx = (float)x + 0.5f - cx;
      if (!in_heart(dx, dy, hw, hh)) continue;
      if (ihw > 0.0f && ihh > 0.0f && in_heart(dx, dy + t * 0.15f, ihw, ihh)) continue;
      put(fb, x, y, p);
    }
  }
}

}  // namespace face
