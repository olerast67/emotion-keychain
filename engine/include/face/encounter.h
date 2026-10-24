// Встречи брелоков по Bluetooth без соединения (docs/encounters.md).
//
// Каждый брелок рассылает короткий рекламный пакет и изредка слушает эфир. Здесь —
// всё, что не зависит от радио: формат пакета, подтверждение встречи (тот же сосед
// слышен дважды за минуту и достаточно близко), общий отсчёт до сцены и исход дуэли.
// Исход и ключ разговора оба брелока считают одинаково и независимо, поэтому сцены
// у них совпадают без обмена сообщениями. Прошивка только передаёт сюда пакеты из эфира,
// рассылает advert() и запускает сцены, которые отдаёт poll().
#pragma once

#include <stdint.h>

namespace face {

// Пакет — данные производителя (Manufacturer Specific Data), 15 байт.
constexpr int kAdvLen = 15;
constexpr uint16_t kAdvCompany = 0xFFFF;  // прототип; для продаж — свой ID из Bluetooth SIG
constexpr uint8_t kAdvVersion = 1;

// Флаги пакета.
constexpr uint8_t kAdvDuelReady = 1u << 0;   // хозяин разрешил дуэли
constexpr uint8_t kAdvCyber = 1u << 1;       // киберпанк-тема
constexpr uint8_t kAdvLegendary = 1u << 2;   // легендарный брелок
constexpr uint8_t kAdvDuelCalled = 1u << 3;  // объявленная сцена — дуэль, а не встреча

struct Advert {
  uint32_t hash = 0;       // позывной: Identity::hash
  uint8_t level = 0;       // установленный имплант
  uint8_t flags = 0;
  uint8_t mood = 128;      // настроение 0…255 (mood_byte)
  uint8_t countdown = 0;   // через сколько ×100 мс начнётся общая сцена; 0 — не объявлена
  uint16_t to = 0;         // младшие 16 бит позывного того, с кем сцена
};

// Пакет в байты и обратно. decode проверяет компанию, метку «KC» и версию.
void adv_encode(const Advert& a, uint8_t out[kAdvLen]);
bool adv_decode(const uint8_t* p, int n, Advert* out);
// Настроение (−1…+1) в байт пакета.
uint8_t mood_byte(float valence);

// Общий nonce пары для сцены: из обоих позывных и байта настроения того, кто объявил
// сцену. Оба брелока знают его из пакета с отсчётом.
uint32_t encounter_nonce(uint32_t hash_a, uint32_t hash_b, uint8_t announcer_mood);
// Исход дуэли для брелока me: одинаков на обоих брелоках при тех же уровнях и nonce.
// Уровень помогает (4 очка за имплант разницы), но не решает.
bool duel_won(uint32_t me, uint32_t peer, uint8_t my_level, uint8_t peer_level, uint32_t nonce);

enum class Rendezvous : uint8_t { None, Greet, Duel };

// Общая сцена с соседом, которую пора начинать.
struct Meeting {
  Rendezvous kind = Rendezvous::None;
  uint32_t peer = 0;        // его позывной
  uint8_t peer_level = 0;
  uint8_t peer_flags = 0;
  uint32_t nonce = 0;       // для Behavior::meet / duel: шёпот и ключ разговора
  bool won = false;         // только для дуэли
  int rssi = -127;
};

class Encounters {
 public:
  struct Config {
    int rssi_min = -70;              // дБм: примерно 1–3 метра
    uint32_t confirm_ms = 60000;     // второй пакет того же соседа — в пределах минуты
    uint32_t countdown_ms = 1500;    // отсчёт до общей сцены
    uint32_t cooldown_ms = 600000;   // с тем же соседом — не чаще раза в 10 минут
    uint32_t forget_ms = 180000;     // сосед пропал из эфира — забываем
  };

  void init(uint32_t my_hash, const Config& cfg);
  void init(uint32_t my_hash) { init(my_hash, Config{}); }
  // Что рассылать о себе: уровень, флаги (кроме kAdvDuelCalled) и настроение.
  void set_self(uint8_t level, uint8_t flags, uint8_t mood);
  // Новые сутки: дуэль с каждым соседом снова возможна (не больше kDueled дуэлей в сутки).
  void new_day();

  // Пакет из эфира с уровнем сигнала.
  void on_advert(const Advert& a, int rssi, uint32_t now_ms);
  // Пакет для рассылки сейчас: с отсчётом, пока объявленная сцена не началась.
  Advert advert(uint32_t now_ms) const;
  // Общая сцена, которую пора начинать (один раз на каждую).
  bool poll(uint32_t now_ms, Meeting* out);
  // Ждём ли общую сцену (прошивка в это время слушает эфир чаще).
  bool pending() const { return pending_.kind != Rendezvous::None; }
  // Сколько соседей сейчас в памяти.
  int nearby(uint32_t now_ms) const;
  // Стоит ли слушать эфир чаще: ждём сцену или рядом сосед, с которым встреча ещё
  // возможна (не в перерыве после прошлой).
  bool wants_busy(uint32_t now_ms) const;

 private:
  struct Peer {
    uint32_t hash = 0;
    uint32_t first_ms = 0, last_ms = 0;
    uint8_t hits = 0;
    uint8_t level = 0, flags = 0, mood = 128;
    int rssi = -127;
  };
  static constexpr int kPeers = 8;
  static constexpr int kDueled = 16;  // с кем сегодня уже была дуэль: живёт до new_day

  bool dueled_today(uint32_t hash) const;
  void mark_dueled(uint32_t hash);
  bool may_duel(uint32_t hash) const;
  struct Cool {
    uint32_t hash = 0, until = 0;  // с этим соседом до until — перерыв после встречи
  };
  static constexpr int kCools = 16;
  bool cooling(uint32_t hash, uint32_t now_ms) const;
  void cool(uint32_t hash, uint32_t now_ms);

  Peer* find(uint32_t hash);
  Peer* slot(uint32_t hash, uint32_t now_ms);
  void announce(Peer& p, uint32_t now_ms);

  Config cfg_;
  uint32_t me_ = 0;
  uint8_t level_ = 0, flags_ = 0, mood_ = 128;
  Peer peers_[kPeers];
  uint32_t dueled_[kDueled] = {};
  Cool cools_[kCools];
  int dueled_n_ = 0;
  Meeting pending_;
  uint32_t start_ms_ = 0;
  bool announcer_ = false;
  uint8_t announced_mood_ = 128;
};

}  // namespace face
