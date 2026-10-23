// Свой характер у каждого брелока. Из серийного номера чипа выводятся
// архетип, черты характера, мелкие отличия внешности и редкость.
// Одинаковый номер — всегда один и тот же персонаж, поэтому хранить ничего не нужно.
#pragma once

#include <stdint.h>

#include "face/behavior.h"
#include "face/engine.h"

namespace face {

#define FACE_ARCHETYPE_LIST(X) \
  X(playful)                   \
  X(grumpy)                    \
  X(sleepy)                    \
  X(shy)                       \
  X(curious)                   \
  X(chill)

enum class Archetype : uint8_t {
#define FACE_X(n) n,
  FACE_ARCHETYPE_LIST(FACE_X)
#undef FACE_X
      Count
};

enum class Rarity : uint8_t {
  Common,     // ~89%
  Rare,       // ~10%: заметная особенность внешности
  Legendary,  // ~1%: золотой кибер-глаз
};

struct Identity {
  uint32_t hash = 0;
  char code[8] = {};  // «позывной» для экрана и встреч, например «K7-3F2A»
  Archetype archetype = Archetype::playful;
  Rarity rarity = Rarity::Common;
  Personality personality;
  Quirks quirks;
};

Identity make_identity(uint64_t serial);
const char* archetype_id(Archetype a);
const char* archetype_name(Archetype a);  // по-русски
const char* rarity_name(Rarity r);        // по-русски

}  // namespace face
