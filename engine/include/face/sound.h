// Голос брелока для пьезопищалки: короткие «реплики» из нот на эмоции и события,
// музыка к сценам и секретный разговор двух брелоков после дуэли.
// Движок звука выдаёт только частоту и длительность — прошивка играет их ШИМом
// (ESP32 — LEDC, nRF52840 — PWM). Голос у каждого характера свой (temperament.h):
// та же реплика у игруна скачет вверх, у ворчуна бурчит, у сони сползает вниз.
//
//   Voice v;  v.init(&temperament(me.archetype), seed);
//   behavior.attach_voice(&v);                // реплики на касания, события и сцены
//   каждый проход: hz = v.tone(now) → пищалка (0 — тишина); спать до v.next_change_ms()
#pragma once

#include <stdint.h>

#include "face/expressions.h"
#include "face/scenes.h"
#include "face/temperament.h"

namespace face {

#define FACE_CUE_LIST(X) \
  X(boop)                \
  X(happy)               \
  X(laugh)               \
  X(sad)                 \
  X(angry)               \
  X(surprise)            \
  X(scared)              \
  X(love)                \
  X(sleepy)              \
  X(hello)               \
  X(goodbye)             \
  X(question)            \
  X(idea)                \
  X(error)               \
  X(charge_start)        \
  X(charge_full)         \
  X(battery_low)         \
  X(level_up)            \
  X(friend_hi)           \
  X(duel)                \
  X(win)                 \
  X(lose)                \
  X(boot)                \
  X(two_bits)

enum class Cue : uint8_t {
#define FACE_X(n) n,
  FACE_CUE_LIST(FACE_X)
#undef FACE_X
      Count
};
constexpr int kCueCount = (int)Cue::Count;
const char* cue_id(Cue c);
bool cue_from_id(const char* id, Cue* out);

struct Note {
  uint16_t hz;  // 0 — пауза
  uint16_t ms;
};

// Какая реплика подходит эмоции. Cue::Count — эмоция молчит (спокойные и сонные лица
// не пищат каждый раз, когда их показывают).
Cue cue_for(Expr e);

// ---- Секретный разговор ----
// Два брелока после дуэли «переговариваются» пищалками: четыре реплики по очереди,
// по шесть знаков шифра в каждой. Оба считают разговор из общего ключа, поэтому
// играют одно и то же, каждый — свои реплики, а на экранах видны одни и те же знаки.
constexpr int kChatLines = 4;
constexpr int kChatGlyphs = 6;
constexpr int kChatRunes = 16;      // разных знаков шифра
constexpr uint32_t kChatLineMs = 1600;  // реплика и пауза после неё
constexpr uint32_t kChatMs = kChatLines * kChatLineMs;
// Ключ одинаков у обоих брелоков: от их хэшей (identity.h) в любом порядке и общего
// числа сеанса (nonce из BLE). Начинает тот, у кого хэш меньше.
uint32_t chat_key(uint32_t hash_a, uint32_t hash_b, uint32_t nonce);
inline bool chat_first(uint32_t my_hash, uint32_t peer_hash) { return my_hash < peer_hash; }
// Реплика i: знаки шифра (0..kChatRunes-1). true — её говорит тот, кто начинает.
bool chat_line(uint32_t key, int i, uint8_t glyphs[kChatGlyphs]);
// Сколько звучит знак (вместе с паузой после него) — по нему же сцена печатает знаки.
uint16_t chat_glyph_ms(uint8_t glyph);
// «Расшифровка», которая на миг мелькает на экране в конце разговора.
const char* chat_phrase(uint32_t key);

class Voice {
 public:
  static constexpr int kMaxNotes = 96;

  void init(const Temperament* t, uint32_t seed);
  void set_temperament(const Temperament* t) { t_ = t; }
  // Сказать реплику. false — пищалка молчит: выключена, тихие часы или идёт другая реплика.
  // Важные (зарядка, батарея, апгрейд, встреча, загрузка) говорятся и в тихие часы, но тише.
  bool say(Cue c, uint32_t now_ms);
  // Музыка сцены, по времени совпадает с картинкой: «INSTALLED» апгрейда, исход дуэли,
  // праздничные мелодии. Перебивает реплику. Сцены без музыки молчат.
  void scene(Scene s, const SceneParams& p, uint32_t now_ms);
  // Секретный разговор (см. выше): свои реплики, на месте чужих — тишина, их играет второй брелок.
  // peer — характер соседа: тогда звучат обе стороны (для стенда и симулятора).
  void chat(uint32_t key, bool first, uint32_t now_ms, const Temperament* peer = nullptr);
  void stop();
  // У сцены есть музыка (у «зарядки» — нет: за неё говорит реплика charge_start).
  static bool has_music(Scene s);

  // Что звучит сейчас: частота в Гц, 0 — тишина.
  uint16_t tone(uint32_t now_ms);
  // Ближайшая смена ноты; когда тихо — kMaxSleepMs вперёд от последнего tone().
  uint32_t next_change_ms() const { return next_; }
  bool busy(uint32_t now_ms) const;
  // Сколько реплик и мелодий уже начато — по нему прошивка видит, что пищалка заговорила.
  uint32_t started() const { return started_; }
  Cue last_cue() const { return last_; }
  int length() const { return n_; }
  const Note& note(int i) const { return seq_[i]; }

  void set_enabled(bool on) {
    enabled_ = on;
    if (!on) stop();
  }
  bool enabled() const { return enabled_; }
  // Тихие часы (ночь): молчат все реплики, кроме важных.
  void set_quiet(bool quiet) { quiet_ = quiet; }
  bool quiet() const { return quiet_; }
  // Сейчас нужна тихая громкость (тихие часы): прошивка гонит пищалку с одной ножки, а не мостом.
  bool soft() const { return soft_; }

 private:
  void push(uint16_t hz, uint16_t ms);
  // Пауза до момента ms от начала мелодии.
  void pad_to(uint32_t ms);
  uint32_t total_ms() const;
  void start(uint32_t now_ms, Cue c);
  void render(Cue c);
  // Мелодия без манеры характера (праздники), в тональности его голоса.
  void melody(const int8_t* m, int n, uint16_t tick_ms);
  uint32_t base_hz() const;

  const Temperament* t_ = nullptr;
  uint32_t seed_ = 1;
  Note seq_[kMaxNotes];
  int n_ = 0, i_ = 0;
  uint32_t next_ = 0;
  uint32_t started_ = 0;
  Cue last_ = Cue::Count;
  bool playing_ = false, enabled_ = true, quiet_ = false, soft_ = false;
};

}  // namespace face
