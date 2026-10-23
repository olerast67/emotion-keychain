// Темперамент — чем характеры отличаются на деле: лицо в покое, ответ на касания,
// занятия от скуки, темп, моргание, любимый сюрприз и голос пищалки.
// Архетип выводится из серийного номера (identity.h), темперамент — из архетипа.
#pragma once

#include <stdint.h>

#include "face/expressions.h"
#include "face/scenes.h"

namespace face {

#define FACE_ARCHETYPE_LIST(X) \
  X(playful)                   \
  X(grumpy)                    \
  X(sleepy)                    \
  X(shy)                       \
  X(curious)                   \
  X(chill)                     \
  X(dramatic)                  \
  X(glitchy)                   \
  X(trickster)                 \
  X(sweet)

enum class Archetype : uint8_t {
#define FACE_X(n) n,
  FACE_ARCHETYPE_LIST(FACE_X)
#undef FACE_X
      Count
};
constexpr int kArchetypeCount = (int)Archetype::Count;

// Манера голоса: как пищалка складывает ноты в «слова» (sound.h).
enum class VoiceStyle : uint8_t {
  Bouncy,    // скачет вверх-вниз
  Grumble,   // низко и коротко
  Drowsy,    // медленно сползает вниз
  Tiny,      // тихие короткие писки
  Question,  // всё время с вопросом вверх
  Smooth,    // плавные скольжения
  Wide,      // широкие театральные скачки
  Glitch,    // случайные цифровые трели
  Sneaky,    // «хе-хе» тройками
  Warm,      // мягкие терции
};

struct Temperament {
  const char* tag;   // латиницей для экрана, до 9 знаков
  const char* name;  // по-русски
  // Лицо: в покое, в хорошем настроении, от скуки.
  Expr rest, rest_happy, rest_bored;
  // Ответ на касание (вместо общего «тык») и на двойное касание.
  Expr tap[4];
  float tap_w[4];
  Expr dtap[3];
  // Свои занятия от скуки и как часто он выбирает их, а не общие (0..1).
  Expr idle[4];
  float own_idle;
  float activity;  // как часто сам что-то делает: 1 — как все, больше — чаще
  float tempo;     // переходы между эмоциями: меньше 1 — резче, больше — плавнее
  float blink;     // частота моргания
  float meme;      // тяга к мемам
  // Черты характера 0..1 (Personality): игривость, ворчливость, сонливость, застенчивость.
  float playful, grumpy, sleepy, shy;
  Scene fav;     // любимый сюрприз; Scene::None — без любимого
  bool glitches;  // мелкие сбои картинки и в классической теме
  // Голос: основная нота и манера.
  uint16_t pitch;  // Гц
  VoiceStyle voice;
  float weight;  // как часто встречается среди брелоков
};

const Temperament& temperament(Archetype a);
// Архетип по хэшу брелока (identity.h) — так же, как его выводит сам брелок.
Archetype archetype_from_hash(uint32_t hash);
const char* archetype_id(Archetype a);
const char* archetype_name(Archetype a);  // по-русски
// Своя эмоция характера: он показывает её с первого дня, даже если её имплант ещё не стоит.
bool temperament_innate(const Temperament& t, Expr e);

}  // namespace face
