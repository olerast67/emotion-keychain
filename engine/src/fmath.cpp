#include "face/fmath.h"

namespace face {

float fsin(float x) {
  // Приводим к [-pi, pi], затем отражаем в [-pi/2, pi/2].
  x -= kTau * (float)ifloor((x + kPi) / kTau);
  if (x > kPi * 0.5f) x = kPi - x;
  if (x < -kPi * 0.5f) x = -kPi - x;
  float x2 = x * x;
  return x * (1.0f - x2 * (1.0f / 6.0f - x2 * (1.0f / 120.0f - x2 * (1.0f / 5040.0f))));
}

float ftan(float x) {
  float c = fcos(x);
  if (fabs_(c) < 1e-6f) c = c < 0.0f ? -1e-6f : 1e-6f;
  return fsin(x) / c;
}

float fatan2(float y, float x) {
  float ax = fabs_(x), ay = fabs_(y);
  if (ax < 1e-9f && ay < 1e-9f) return 0.0f;
  float a, z;
  if (ax >= ay) {
    z = ay / ax;
    a = z * (kPi * 0.25f) + 0.273f * z * (1.0f - z);
  } else {
    z = ax / ay;
    a = kPi * 0.5f - (z * (kPi * 0.25f) + 0.273f * z * (1.0f - z));
  }
  if (x < 0.0f) a = kPi - a;
  if (y < 0.0f) a = -a;
  return a;
}

float apply_ease(Ease e, float t) {
  t = clampf(t, 0.0f, 1.0f);
  switch (e) {
    case Ease::Linear: return t;
    case Ease::In: return ease_in(t);
    case Ease::Out: return ease_out(t);
    case Ease::InOut: return ease_in_out(t);
    case Ease::OutBack: return ease_out_back(t);
  }
  return t;
}

}  // namespace face
