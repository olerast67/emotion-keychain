#include "face/identity.h"

namespace face {

namespace {

const char* const kArchIds[] = {
#define FACE_X(n) #n,
    FACE_ARCHETYPE_LIST(FACE_X)
#undef FACE_X
};

const char* const kArchNames[] = {"игрун", "ворчун", "соня", "скромник", "любопытный", "пофигист"};
static_assert(sizeof(kArchNames) / sizeof(kArchNames[0]) == (int)Archetype::Count, "имена архетипов");

// Черты характера каждого архетипа: playful, grumpy, sleepy, shy.
const Personality kBase[] = {
    {0.85f, 0.15f, 0.30f, 0.25f},  // игрун
    {0.35f, 0.80f, 0.40f, 0.30f},  // ворчун
    {0.40f, 0.30f, 0.85f, 0.40f},  // соня
    {0.40f, 0.20f, 0.45f, 0.85f},  // скромник
    {0.70f, 0.25f, 0.30f, 0.30f},  // любопытный
    {0.50f, 0.35f, 0.55f, 0.20f},  // пофигист
};

// splitmix64: из серийного номера получаем хорошо перемешанный поток бит.
uint64_t mix(uint64_t& s) {
  uint64_t z = (s += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

float unit(uint64_t& s) { return (float)(mix(s) >> 40) / (float)(1ull << 24); }
float range(uint64_t& s, float lo, float hi) { return lo + (hi - lo) * unit(s); }

}  // namespace

const char* archetype_id(Archetype a) { return (int)a < (int)Archetype::Count ? kArchIds[(int)a] : "?"; }
const char* archetype_name(Archetype a) { return (int)a < (int)Archetype::Count ? kArchNames[(int)a] : "?"; }
const char* rarity_name(Rarity r) {
  switch (r) {
    case Rarity::Common: return "обычный";
    case Rarity::Rare: return "редкий";
    case Rarity::Legendary: return "легендарный";
  }
  return "?";
}

Identity make_identity(uint64_t serial) {
  Identity id;
  uint64_t s = serial ^ 0x6B65796368616E21ull;  // «keychan!»: свой поток даже для номера 0
  const uint64_t h = mix(s);
  id.hash = (uint32_t)(h ^ (h >> 32));

  // Позывной: 6 знаков без похожих друг на друга (нет O/0, I/1).
  static const char kAlpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  uint32_t c = id.hash;
  for (int i = 0, k = 0; i < 7; ++i) {
    if (i == 2) {
      id.code[i] = '-';
      continue;
    }
    id.code[i] = kAlpha[c & 31u];
    c >>= 5;
    ++k;
  }
  id.code[7] = 0;

  // Архетип: игруны и любопытные чуть чаще, ворчуны и соньки реже — так веселее.
  static const float kWeights[] = {1.3f, 0.8f, 0.8f, 0.9f, 1.2f, 1.0f};
  float total = 0;
  for (float w : kWeights) total += w;
  float x = unit(s) * total;
  int arch = 0;
  for (int i = 0; i < (int)Archetype::Count; ++i) {
    x -= kWeights[i];
    if (x <= 0) {
      arch = i;
      break;
    }
  }
  id.archetype = (Archetype)arch;

  Personality p = kBase[arch];
  p.playful = clampf(p.playful + range(s, -0.1f, 0.1f), 0.05f, 0.95f);
  p.grumpy = clampf(p.grumpy + range(s, -0.1f, 0.1f), 0.05f, 0.95f);
  p.sleepy = clampf(p.sleepy + range(s, -0.1f, 0.1f), 0.05f, 0.95f);
  p.shy = clampf(p.shy + range(s, -0.1f, 0.1f), 0.05f, 0.95f);
  id.personality = p;

  Quirks& q = id.quirks;
  q.spacing = (float)((int)(mix(s) % 5u) - 2);  // −2…+2 px
  q.eye_scale = range(s, 0.95f, 1.05f);
  q.pupil_scale = range(s, 0.85f, 1.2f);
  q.blink_rate = range(s, 0.8f, 1.3f);
  q.brow_lift = (float)((int)(mix(s) % 3u) - 1);  // −1…+1 px

  const uint32_t roll = (uint32_t)(mix(s) % 1000u);
  if (roll < 10) {
    id.rarity = Rarity::Legendary;
    q.cyber_eye = true;
  } else if (roll < 110) {
    // Редкий: заметная, но не уродующая особенность — большие зрачки или широкая посадка глаз.
    id.rarity = Rarity::Rare;
    if (roll & 1)
      q.pupil_scale = 1.3f;
    else
      q.spacing = (roll & 2) ? 4.0f : -4.0f;
  }
  return id;
}

}  // namespace face
