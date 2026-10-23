// Небольшая математика без libm: движок собирается и для МК, и для WebAssembly без libc.
#pragma once

#include <stdint.h>

namespace face {

constexpr float kPi = 3.14159265f;
constexpr float kTau = 6.28318531f;
constexpr float kDeg = kPi / 180.0f;

inline float fabs_(float x) { return x < 0.0f ? -x : x; }
inline float fmin_(float a, float b) { return a < b ? a : b; }
inline float fmax_(float a, float b) { return a > b ? a : b; }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float fsqrt(float x) { return x <= 0.0f ? 0.0f : __builtin_sqrtf(x); }

inline int ifloor(float x) {
  int i = (int)x;
  return (x < (float)i) ? i - 1 : i;
}
inline int iround(float x) { return ifloor(x + 0.5f); }
inline int iceil(float x) { return -ifloor(-x); }
inline int imin(int a, int b) { return a < b ? a : b; }
inline int imax(int a, int b) { return a > b ? a : b; }
inline int iclamp(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }

// Синус с ошибкой ~2e-4: приведение к [-pi/2, pi/2] и ряд Тейлора до x^7.
float fsin(float x);
inline float fcos(float x) { return fsin(x + kPi * 0.5f); }
float ftan(float x);
// atan2 с ошибкой ~0.005 рад; для спиралей и орбит этого достаточно.
float fatan2(float y, float x);

// Функции плавности, t в [0, 1].
inline float ease_linear(float t) { return t; }
inline float ease_in(float t) { return t * t * t; }
inline float ease_out(float t) {
  float u = 1.0f - t;
  return 1.0f - u * u * u;
}
inline float ease_in_out(float t) {
  if (t < 0.5f) return 4.0f * t * t * t;
  float u = -2.0f * t + 2.0f;
  return 1.0f - u * u * u * 0.5f;
}
// Перелёт с возвратом: «пружинка» при появлении.
inline float ease_out_back(float t) {
  const float c1 = 1.70158f, c3 = c1 + 1.0f;
  float u = t - 1.0f;
  return 1.0f + c3 * u * u * u + c1 * u * u;
}
// Колокол 0 → 1 → 0, удобен для одноразовых движений.
inline float bump(float t) { return t <= 0.0f || t >= 1.0f ? 0.0f : fsin(t * kPi); }
// Огибающая: быстро нарастает, держится, плавно спадает.
inline float envelope(float t, float attack, float release) {
  if (t <= 0.0f || t >= 1.0f) return 0.0f;
  if (t < attack) return ease_out(t / attack);
  if (t > 1.0f - release) return ease_in_out((1.0f - t) / release);
  return 1.0f;
}

enum class Ease : uint8_t { Linear, In, Out, InOut, OutBack };
float apply_ease(Ease e, float t);

// Детерминированный ГПСЧ (xorshift32): одинаковые анимации при одинаковом seed.
struct Rng {
  uint32_t s = 0x9E3779B9u;
  void seed(uint32_t v) { s = v ? v : 0x9E3779B9u; }
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  // [0, 1)
  float unit() { return (float)(next() >> 8) * (1.0f / 16777216.0f); }
  float range(float lo, float hi) { return lo + (hi - lo) * unit(); }
  int irange(int lo, int hi_incl) { return lo + (int)(next() % (uint32_t)(hi_incl - lo + 1)); }
  bool chance(float p) { return unit() < p; }
};

}  // namespace face
