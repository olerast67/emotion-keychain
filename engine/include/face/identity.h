// Свой характер у каждого брелока. Из серийного номера чипа выводится хэш, а из него —
// архетип с темпераментом (temperament.h), черты характера, мелкие отличия внешности
// и редкость. Одинаковый номер — всегда один и тот же персонаж, хранить ничего не нужно.
#pragma once

#include <stdint.h>

#include "face/behavior.h"
#include "face/engine.h"
#include "face/temperament.h"

namespace face {

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
// Всё о брелоке по его позывному-хэшу: так брелок узнаёт характер соседа по рекламному
// пакету BLE, где передаётся только хэш.
Identity identity_from_hash(uint32_t hash);
const char* rarity_name(Rarity r);  // по-русски

}  // namespace face
