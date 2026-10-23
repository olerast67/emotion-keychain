// Растровые примитивы для 1-битного экрана. Все проверки идут по центрам
// пикселей (x + 0.5, y + 0.5), поэтому фигуры с целым центром симметричны.
#pragma once

#include "face/fmath.h"
#include "face/framebuffer.h"

namespace face {

struct Vec2 {
  float x, y;
};

// Что рисовать: обычная заливка или дизеринг для полупрозрачности.
struct Paint {
  Ink ink = Ink::Set;
  float alpha = 1.0f;  // < 1 включает дизеринг Байера
  inline bool pass(int x, int y) const { return alpha >= 1.0f || dither_pass(x, y, alpha); }
};

// Точка внутри скруглённого прямоугольника с полуосями hw, hh и радиусом r (r <= min(hw, hh)).
inline bool in_rrect(float dx, float dy, float hw, float hh, float r) {
  float ax = fabs_(dx) - (hw - r);
  float ay = fabs_(dy) - (hh - r);
  if (ax > r || ay > r) return false;
  if (ax <= 0.0f || ay <= 0.0f) return true;
  return ax * ax + ay * ay <= r * r;
}

inline bool in_ellipse(float dx, float dy, float rx, float ry) {
  if (rx <= 0.0f || ry <= 0.0f) return false;
  float u = dx / rx, v = dy / ry;
  return u * u + v * v <= 1.0f;
}

// Квадрат расстояния от точки до отрезка.
inline float seg_dist2(float px, float py, float ax, float ay, float bx, float by) {
  float vx = bx - ax, vy = by - ay;
  float wx = px - ax, wy = py - ay;
  float l2 = vx * vx + vy * vy;
  float h = l2 > 0.0f ? clampf((wx * vx + wy * vy) / l2, 0.0f, 1.0f) : 0.0f;
  float dx = wx - vx * h, dy = wy - vy * h;
  return dx * dx + dy * dy;
}

void fill_rrect(Framebuffer& fb, float cx, float cy, float hw, float hh, float r, Paint p = {});
void fill_ellipse(Framebuffer& fb, float cx, float cy, float rx, float ry, Paint p = {});
void fill_circle(Framebuffer& fb, float cx, float cy, float r, Paint p = {});
// Кольцо эллипса толщиной t (если t >= min(rx, ry), получается заливка).
void ellipse_ring(Framebuffer& fb, float cx, float cy, float rx, float ry, float t, Paint p = {});
// Толстая линия со скруглёнными концами.
void capsule(Framebuffer& fb, float x0, float y0, float x1, float y1, float thick, Paint p = {});
void polyline(Framebuffer& fb, const Vec2* pts, int n, float thick, Paint p = {});
// Заливка многоугольника по правилу чёт-нечет (подходит и для невыпуклых: звёзды, молнии).
void fill_polygon(Framebuffer& fb, const Vec2* pts, int n, Paint p = {});
// Дуга эллипса от угла a0 до a1 (радианы, 0 = вправо, рост по часовой на экране).
void arc(Framebuffer& fb, float cx, float cy, float rx, float ry, float a0, float a1, float thick, Paint p = {});
// Квадратичная кривая Безье толщиной thick.
void bezier(Framebuffer& fb, Vec2 a, Vec2 ctrl, Vec2 b, float thick, Paint p = {}, int segments = 10);

// Готовые значки, которые нужны и глазам, и оверлеям.
void fill_star(Framebuffer& fb, float cx, float cy, float r_out, float r_in, int points, float rot, Paint p = {});
void fill_heart(Framebuffer& fb, float cx, float cy, float hw, float hh, Paint p = {});
void heart_ring(Framebuffer& fb, float cx, float cy, float hw, float hh, float t, Paint p = {});

}  // namespace face
