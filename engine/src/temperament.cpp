#include "face/temperament.h"

namespace face {

namespace {

using E = Expr;

const char* const kIds[] = {
#define FACE_X(n) #n,
    FACE_ARCHETYPE_LIST(FACE_X)
#undef FACE_X
};

// Десять характеров. Ноты голоса — из шестой-пятой октавы: на пьезопищалке 9 мм
// они звучат громче всего (резонанс около 4 кГц, ниже — тише).
const Temperament kTemperaments[] = {
    {.tag = "PLAYFUL", .name = "игрун",
     .rest = E::happy, .rest_happy = E::joy, .rest_bored = E::playful,
     .tap = {E::booped, E::giggle, E::wink, E::playful}, .tap_w = {3, 2, 1.5f, 1},
     .dtap = {E::laugh, E::joy, E::tears_of_joy},
     .idle = {E::wink, E::giggle, E::party, E::derp}, .own_idle = 0.45f,
     .activity = 1.4f, .tempo = 0.8f, .blink = 1.15f, .meme = 1.3f,
     .playful = 0.9f, .grumpy = 0.1f, .sleepy = 0.3f, .shy = 0.2f,
     .fav = Scene::hide, .glitches = false, .pitch = 1568, .voice = VoiceStyle::Bouncy, .talk = 0.9f, .weight = 1.2f},
    {.tag = "GRUMPY", .name = "ворчун",
     .rest = E::unamused, .rest_happy = E::smug, .rest_bored = E::poker_face,
     .tap = {E::unamused, E::annoyed, E::skeptical, E::booped}, .tap_w = {3, 2, 1.5f, 0.5f},
     .dtap = {E::disapproval, E::bruh, E::hmm_squint},
     .idle = {E::hmm_squint, E::disapproval, E::skeptical, E::poker_face}, .own_idle = 0.5f,
     .activity = 0.7f, .tempo = 1.1f, .blink = 0.9f, .meme = 0.9f,
     .playful = 0.3f, .grumpy = 0.85f, .sleepy = 0.4f, .shy = 0.2f,
     .fav = Scene::error404, .glitches = false, .pitch = 523, .voice = VoiceStyle::Grumble, .talk = 0.5f, .weight = 0.9f},
    {.tag = "SLEEPY", .name = "соня",
     .rest = E::tired, .rest_happy = E::bliss, .rest_bored = E::sleepy,
     .tap = {E::waking, E::booped, E::sleepy, E::content}, .tap_w = {2, 2, 1, 1},
     .dtap = {E::waking, E::tired, E::bliss},
     .idle = {E::daydreaming, E::zoned_out, E::sleepy, E::bliss}, .own_idle = 0.4f,
     .activity = 0.5f, .tempo = 1.4f, .blink = 0.7f, .meme = 0.6f,
     .playful = 0.35f, .grumpy = 0.3f, .sleepy = 0.9f, .shy = 0.4f,
     .fav = Scene::reboot, .glitches = false, .pitch = 784, .voice = VoiceStyle::Drowsy, .talk = 0.35f, .weight = 0.9f},
    {.tag = "SHY", .name = "скромник",
     .rest = E::shy, .rest_happy = E::content, .rest_bored = E::lonely,
     .tap = {E::embarrassed, E::flustered, E::shy, E::booped}, .tap_w = {3, 2, 2, 1},
     .dtap = {E::flustered, E::embarrassed, E::touched},
     .idle = {E::shy, E::innocent, E::pocket_peek, E::embarrassed}, .own_idle = 0.45f,
     .activity = 0.8f, .tempo = 1.0f, .blink = 1.2f, .meme = 0.5f,
     .playful = 0.4f, .grumpy = 0.2f, .sleepy = 0.45f, .shy = 0.9f,
     .fav = Scene::hide, .glitches = false, .pitch = 1319, .voice = VoiceStyle::Tiny, .talk = 0.3f, .weight = 0.9f},
    {.tag = "CURIOUS", .name = "любопытный",
     .rest = E::curious, .rest_happy = E::excited, .rest_bored = E::thinking,
     .tap = {E::curious, E::owo, E::booped, E::idea}, .tap_w = {3, 2, 1.5f, 1},
     .dtap = {E::excited, E::amazed, E::idea},
     .idle = {E::thinking, E::idea, E::processing, E::alert}, .own_idle = 0.45f,
     .activity = 1.3f, .tempo = 0.9f, .blink = 1.0f, .meme = 1.0f,
     .playful = 0.7f, .grumpy = 0.2f, .sleepy = 0.3f, .shy = 0.3f,
     .fav = Scene::data_rain, .glitches = false, .pitch = 1175, .voice = VoiceStyle::Question, .talk = 0.8f, .weight = 1.1f},
    {.tag = "CHILL", .name = "пофигист",
     .rest = E::calm, .rest_happy = E::vibing, .rest_bored = E::zoned_out,
     .tap = {E::cool, E::content, E::smug, E::booped}, .tap_w = {2, 2, 1, 1},
     .dtap = {E::cool, E::vibing, E::not_bad},
     .idle = {E::vibing, E::cool, E::npc, E::zoned_out}, .own_idle = 0.45f,
     .activity = 0.55f, .tempo = 1.3f, .blink = 0.85f, .meme = 1.2f,
     .playful = 0.5f, .grumpy = 0.3f, .sleepy = 0.55f, .shy = 0.15f,
     .fav = Scene::None, .glitches = false, .pitch = 659, .voice = VoiceStyle::Smooth, .talk = 0.45f, .weight = 1.0f},
    {.tag = "DRAMA", .name = "драма",
     .rest = E::neutral, .rest_happy = E::tears_of_joy, .rest_bored = E::pleading,
     .tap = {E::shocked, E::tears_of_joy, E::touched, E::startled}, .tap_w = {2, 2, 1.5f, 1},
     .dtap = {E::love, E::tears_of_joy, E::mind_blown},
     .idle = {E::pleading, E::touched, E::speechless, E::shocked}, .own_idle = 0.45f,
     .activity = 1.2f, .tempo = 0.7f, .blink = 1.1f, .meme = 1.0f,
     .playful = 0.6f, .grumpy = 0.4f, .sleepy = 0.3f, .shy = 0.3f,
     .fav = Scene::reboot, .glitches = false, .pitch = 988, .voice = VoiceStyle::Wide, .talk = 1.0f, .weight = 0.8f},
    {.tag = "GLITCH", .name = "глюк",
     .rest = E::robot, .rest_happy = E::owo, .rest_bored = E::processing,
     .tap = {E::error, E::processing, E::booped, E::robot}, .tap_w = {2, 2, 1.5f, 1},
     .dtap = {E::error, E::twitchy, E::owo},
     .idle = {E::processing, E::error, E::twitchy, E::zoned_out}, .own_idle = 0.5f,
     .activity = 1.0f, .tempo = 0.85f, .blink = 1.0f, .meme = 1.1f,
     .playful = 0.6f, .grumpy = 0.3f, .sleepy = 0.3f, .shy = 0.3f,
     .fav = Scene::hack, .glitches = true, .pitch = 1046, .voice = VoiceStyle::Glitch, .talk = 0.7f, .weight = 0.6f},
    {.tag = "TRICKSTER", .name = "хитрец",
     .rest = E::smug, .rest_happy = E::mischievous, .rest_bored = E::lenny,
     .tap = {E::devilish, E::wink, E::booped, E::troll_grin}, .tap_w = {2, 2, 1.5f, 1},
     .dtap = {E::evil_laugh, E::troll_grin, E::mischievous},
     .idle = {E::lenny, E::smug_glasses, E::mischievous, E::troll_grin}, .own_idle = 0.5f,
     .activity = 1.1f, .tempo = 0.9f, .blink = 1.0f, .meme = 1.5f,
     .playful = 0.75f, .grumpy = 0.35f, .sleepy = 0.3f, .shy = 0.15f,
     .fav = Scene::hack, .glitches = false, .pitch = 880, .voice = VoiceStyle::Sneaky, .talk = 0.75f, .weight = 0.8f},
    {.tag = "SWEET", .name = "добряк",
     .rest = E::content, .rest_happy = E::adoring, .rest_bored = E::daydreaming,
     .tap = {E::love, E::adoring, E::happy, E::booped}, .tap_w = {2, 2, 1.5f, 1},
     .dtap = {E::love, E::grateful, E::bliss},
     .idle = {E::adoring, E::grateful, E::bliss, E::love}, .own_idle = 0.45f,
     .activity = 1.0f, .tempo = 1.05f, .blink = 1.0f, .meme = 0.7f,
     .playful = 0.6f, .grumpy = 0.1f, .sleepy = 0.4f, .shy = 0.4f,
     .fav = Scene::hide, .glitches = false, .pitch = 1397, .voice = VoiceStyle::Warm, .talk = 0.7f, .weight = 1.0f},
};
static_assert(sizeof(kTemperaments) / sizeof(kTemperaments[0]) == kArchetypeCount, "темперамент на каждый архетип");

}  // namespace

const Temperament& temperament(Archetype a) { return kTemperaments[(int)a < kArchetypeCount ? (int)a : 0]; }

Archetype archetype_from_hash(uint32_t hash) {
  // Своё перемешивание хэша, чтобы архетип не зависел от позывного (младших бит).
  uint32_t x = hash ^ 0x61726368u;  // «arch»
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  float total = 0;
  for (const Temperament& t : kTemperaments) total += t.weight;
  float u = (float)(x >> 8) / (float)(1u << 24) * total;
  for (int i = 0; i < kArchetypeCount; ++i) {
    u -= kTemperaments[i].weight;
    if (u <= 0) return (Archetype)i;
  }
  return (Archetype)(kArchetypeCount - 1);
}
const char* archetype_id(Archetype a) { return (int)a < kArchetypeCount ? kIds[(int)a] : "?"; }
const char* archetype_name(Archetype a) { return (int)a < kArchetypeCount ? kTemperaments[(int)a].name : "?"; }

bool temperament_innate(const Temperament& t, Expr e) {
  if (e == t.rest || e == t.rest_happy || e == t.rest_bored) return true;
  for (Expr x : t.tap)
    if (x == e) return true;
  for (Expr x : t.dtap)
    if (x == e) return true;
  for (Expr x : t.idle)
    if (x == e) return true;
  return false;
}

}  // namespace face
