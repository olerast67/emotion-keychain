#include "face/identity.h"

namespace face {

namespace {

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

const char* rarity_name(Rarity r, Lang l) {
  const bool en = l == Lang::En;
  switch (r) {
    case Rarity::Common: return en ? "common" : "обычный";
    case Rarity::Rare: return en ? "rare" : "редкий";
    case Rarity::Legendary: return en ? "legendary" : "легендарный";
  }
  return "?";
}
const char* rarity_name(Rarity r) { return rarity_name(r, lang()); }

Identity make_identity(uint64_t serial) {
  uint64_t s = serial ^ 0x6B65796368616E21ull;  // «keychan!»: свой поток даже для номера 0
  const uint64_t h = mix(s);
  return identity_from_hash((uint32_t)(h ^ (h >> 32)));
}

Identity identity_from_hash(uint32_t hash) {
  Identity id;
  id.hash = hash;

  // Позывной: 6 знаков без похожих друг на друга (нет O/0, I/1).
  static const char kAlpha[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  uint32_t c = id.hash;
  for (int i = 0; i < 7; ++i) {
    if (i == 2) {
      id.code[i] = '-';
      continue;
    }
    id.code[i] = kAlpha[c & 31u];
    c >>= 5;
  }
  id.code[7] = 0;

  // Остальное — из потока бит, засеянного хэшем.
  uint64_t s = (uint64_t)hash * 0x9E3779B97F4A7C15ull ^ 0x6F6C657261737421ull;  // «olerast!»
  id.archetype = archetype_from_hash(hash);
  const Temperament& t = temperament(id.archetype);

  // Черты — от темперамента с небольшим разбросом: два игруна тоже не близнецы.
  Personality& p = id.personality;
  p.playful = clampf(t.playful + range(s, -0.08f, 0.08f), 0.05f, 0.95f);
  p.grumpy = clampf(t.grumpy + range(s, -0.08f, 0.08f), 0.05f, 0.95f);
  p.sleepy = clampf(t.sleepy + range(s, -0.08f, 0.08f), 0.05f, 0.95f);
  p.shy = clampf(t.shy + range(s, -0.08f, 0.08f), 0.05f, 0.95f);

  Quirks& q = id.quirks;
  q.spacing = (float)((int)(mix(s) % 5u) - 2);  // −2…+2 px
  q.eye_scale = range(s, 0.95f, 1.05f);
  q.pupil_scale = range(s, 0.85f, 1.2f);
  q.blink_rate = range(s, 0.85f, 1.2f) * t.blink;
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
