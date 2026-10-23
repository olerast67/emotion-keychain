// Каталог эмоций: идентификаторы, оверлеи, движения и сами пресеты.
#pragma once

#include <stdint.h>

#include "face/params.h"

namespace face {

// Символические значки поверх лица (манга-«манпу»).
enum class Overlay : uint8_t {
  BlushLines,   // румянец «///»
  BlushDither,  // румянец-овал через дизеринг
  Sweat,        // капля пота
  Vein,         // «💢» — вздувшаяся вена
  Tear,         // слезинки
  TearStream,   // ручьи слёз
  Zzz,
  Hearts,      // всплывающие сердечки
  Sparkles,    // искры
  SoundWaves,  // звуковые волны у глаз
  Notes,       // ноты
  Question,    // «?»
  Exclaim,     // «!»
  Ellipsis,    // «...»
  Steam,       // пар от злости
  Gloom,       // линии уныния
  DizzyStars,  // звёздочки по орбите
  Bulb,        // лампочка-идея
  Shades,      // тёмные очки
  Bolt,        // молния зарядки
  Snow,        // снежинки
  Drool,       // слюнка
  Rays,        // линии испуга
  BatteryLow,  // значок батареи
  SpeedLines,  // линии скорости
  Bubble,      // пузырь из носа во сне
  Halo,        // нимб
  Horns,       // рожки
  Count
};

constexpr uint32_t ov() { return 0; }
template <typename... Rest>
constexpr uint32_t ov(Overlay first, Rest... rest) {
  return (1u << (uint32_t)first) | ov(rest...);
}

// Непрерывное движение всего лица, пока держится эмоция.
enum class Motion : uint8_t {
  None,
  Bounce,   // подпрыгивает
  Bob,      // плавно покачивается вверх-вниз
  Tremble,  // дрожит
  Shake,    // трясёт головой
  Sway,     // покачивается из стороны в сторону, с наклоном
  Shiver,   // мелко дрожит от холода
  Pulse,    // глаза пульсируют, как сердце
  Float,    // мечтательно плывёт
  Nod,      // кивает в ритм
  Wobble,   // качается, как неваляшка
};

// Как ведёт себя взгляд.
enum class Gaze : uint8_t {
  Idle,    // обычные саккады: чаще в центр, иногда по сторонам
  Still,   // почти неподвижен
  Around,  // осматривается
  Up,
  Down,
  Left,
  Right,
  Avert,   // отводит взгляд вниз-вбок
  Dart,    // нервно бегает
  UpSide,  // вверх и вбок — задумался
};

enum class Blink : uint8_t { None, Rare, Normal, Often, Slow };

enum class Category : uint8_t { Positive, Negative, Surprise, Cognitive, Social, State, System, Fun, Count };

// Список эмоций. Порядок совпадает с таблицей пресетов в expressions.cpp.
#define FACE_EXPR_LIST(X) \
  X(neutral)              \
  X(calm)                 \
  X(happy)                \
  X(joy)                  \
  X(laugh)                \
  X(giggle)               \
  X(love)                 \
  X(adoring)              \
  X(excited)              \
  X(starstruck)           \
  X(proud)                \
  X(wink)                 \
  X(playful)              \
  X(kiss)                 \
  X(grateful)             \
  X(relieved)             \
  X(content)              \
  X(sad)                  \
  X(crying)               \
  X(sobbing)              \
  X(disappointed)         \
  X(lonely)               \
  X(hurt)                 \
  X(angry)                \
  X(furious)              \
  X(annoyed)              \
  X(grumpy)               \
  X(disgusted)            \
  X(sulking)              \
  X(embarrassed)          \
  X(guilty)               \
  X(surprised)            \
  X(shocked)              \
  X(amazed)               \
  X(scared)               \
  X(terrified)            \
  X(nervous)              \
  X(worried)              \
  X(startled)             \
  X(curious)              \
  X(confused)             \
  X(thinking)             \
  X(idea)                 \
  X(focused)              \
  X(skeptical)            \
  X(suspicious)           \
  X(listening)            \
  X(mischievous)          \
  X(determined)           \
  X(daydreaming)          \
  X(processing)           \
  X(hello)                \
  X(goodbye)              \
  X(pleading)             \
  X(shy)                  \
  X(smug)                 \
  X(innocent)             \
  X(devilish)             \
  X(apologetic)           \
  X(sleepy)               \
  X(sleeping)             \
  X(waking)               \
  X(bored)                \
  X(tired)                \
  X(exhausted)            \
  X(dizzy)                \
  X(knocked_out)          \
  X(hungry)               \
  X(cold)                 \
  X(hot)                  \
  X(sick)                 \
  X(dazzled)              \
  X(afraid_dark)          \
  X(charging)             \
  X(charged)              \
  X(low_battery)          \
  X(critical_battery)     \
  X(error)                \
  X(music)                \
  X(singing)              \
  X(cool)                 \
  X(uwu)                  \
  X(owo)                  \
  X(derp)                 \
  X(robot)                \
  X(party)

enum class Expr : uint8_t {
#define FACE_X(n) n,
  FACE_EXPR_LIST(FACE_X)
#undef FACE_X
      Count
};

constexpr int kExprCount = (int)Expr::Count;

struct Expression {
  const char* id;
  const char* name;  // по-русски, для симулятора и документации
  Category cat = Category::Positive;
  EyePair eyes = both(ring(30, 4));
  BrowPair brows = {};
  Mouth mouth = line(18);
  Layout lay = {};
  uint32_t overlays = 0;
  Motion motion = Motion::None;
  float motion_amp = 0;  // пиксели (для Pulse — проценты)
  float motion_hz = 0;
  Gaze gaze = Gaze::Idle;
  Blink blink = Blink::Normal;
  uint16_t in_ms = 260;       // длительность перехода в эмоцию
  uint16_t hold_ms = 0;       // сколько держится (0 — пока не сменят)
  Expr next = Expr::neutral;  // во что переходит после hold_ms
  const char* when = "";      // в каких ситуациях
};

const Expression& expression(Expr e);
const char* expr_id(Expr e);
const char* category_name(Category c);
bool expr_from_id(const char* id, Expr* out);
// Базовая форма лица для эмоции (без процедурных слоёв).
void expression_face(Expr e, Face* out);

}  // namespace face
