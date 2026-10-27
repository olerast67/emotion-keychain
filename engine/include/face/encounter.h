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
constexpr uint8_t kAdvVersion = 2;  // 2: ответ «принял» и отсчёт 4 с — со старой прошивкой встреч нет

// Флаги пакета.
constexpr uint8_t kAdvDuelReady = 1u << 0;   // хозяин разрешил дуэли
constexpr uint8_t kAdvCyber = 1u << 1;       // киберпанк-тема
constexpr uint8_t kAdvLegendary = 1u << 2;   // легендарный брелок
constexpr uint8_t kAdvDuelCalled = 1u << 3;  // сцена — дуэль: у объявившего — объявил, у ответившего — принял
constexpr uint8_t kAdvAck = 1u << 4;         // «принял объявление»: ответ того, с кем сцена

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
    // Отсчёт до общей сцены. За него второй брелок должен услышать объявление и ответить
    // «принял», а объявивший — услышать ответ: без ответа объявивший сцену снимает, и она
    // не играет у него одного. Если потерялся сам ответ, сцена будет только у второго:
    // без соединения этого не исключить, бывает редко (docs/encounters.md).
    uint32_t countdown_ms = 4000;
    // Объявление, до сцены которого осталось меньше этого, второй брелок не принимает:
    // объявивший уже не успел бы услышать «принял», и сцена была бы только у второго даже
    // без потерь в эфире. Запас — на период частого прослушивания прошивки (1.5 с), один
    // пакет ответа (0.1 с) и задержку цикла. Объявивший повторит попытку после перерыва.
    uint32_t accept_min_ms = 2000;
    // С тем же соседом — не чаще раза в 10 минут. Встречи подряд (брелоки лежат рядом)
    // удлиняют перерыв вдвое, до 16 раз: 10, 20, 40, 80, 160 минут. Серия забывается,
    // если после конца перерыва series_ms встречи с ним не было (были порознь), и с новыми
    // сутками.
    uint32_t cooldown_ms = 600000;
    uint32_t series_ms = 3u * 3600000u;
    uint32_t forget_ms = 180000;     // сосед пропал из эфира — забываем
    // Сосед слышен вблизи дважды, а сцена с ним не началась (он не слушает эфир — темно или
    // ночь, слышит нас слабее порога, у него лимит встреч): столько эфир слушается часто,
    // дальше сосед в перерыве 1, 2, 4, 8 минут. Иначе такой сосед держал бы частое
    // прослушивание бесконечно. Окно — с запасом на прослушивание соседа раз в 30 с.
    uint32_t wait_ms = 40000;
    // Встреч всего — не чаще раза в 20 с и не больше 6 за 15 минут: поддельные позывные
    // иначе устроили бы сцену с вибрацией каждые пару секунд.
    uint32_t min_gap_ms = 20000;
    uint32_t burst_window_ms = 900000;
  };

  void init(uint32_t my_hash, const Config& cfg);
  void init(uint32_t my_hash) { init(my_hash, Config{}); }
  // Друзья не тратят общий лимит встреч: иначе поток поддельных «новых» соседей выбирал бы
  // его, и друг ждал бы до 15 минут. Прошивка считает другом того, с кем дружба не ниже
  // BUDDY, то есть виделись хотя бы в три разных дня (friend_level_days(1)): позывной,
  // выдуманный сегодня, в друзья не попадает.
  using KnownFn = bool (*)(uint32_t hash, const void* ctx);
  void set_known(KnownFn fn, const void* ctx) { known_ = fn, known_ctx_ = ctx; }
  // Что рассылать о себе: уровень, флаги (кроме kAdvDuelCalled) и настроение.
  void set_self(uint8_t level, uint8_t flags, uint8_t mood);
  // Новые сутки: дуэль с каждым соседом снова возможна (не больше kDueled дуэлей в сутки),
  // серии встреч подряд начинаются заново (идущие перерывы доживают свой срок).
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
  // возможна (не в перерыве после прошлой и ждём её не дольше wait_ms).
  bool wants_busy(uint32_t now_ms) const;

 private:
  struct Peer {
    uint32_t hash = 0;
    uint32_t first_ms = 0, last_ms = 0;
    uint32_t ready_ms = 0;  // с этого момента ждём сцену: слышен дважды и не в перерыве
    uint8_t hits = 0;
    uint8_t level = 0, flags = 0, mood = 128;
    int rssi = -127;
  };
  static constexpr int kPeers = 8;
  static constexpr int kDueled = 16;  // с кем сегодня уже была дуэль: живёт до new_day

  bool dueled_today(uint32_t hash) const;
  void mark_dueled(uint32_t hash);
  bool may_duel(uint32_t hash) const;
  // Перерыв с соседом: dur мс от at. Хранится начало, а не конец, — сравнение «прошло ли
  // dur» верно при любом переполнении часов.
  struct Cool {
    uint32_t hash = 0, at = 0, dur = 0;
    uint8_t fails = 0;  // сколько попыток подряд не дошли до сцены (нет ответа, не дождались)
    uint8_t met = 0;    // сколько встреч подряд: от этого длина перерыва после следующей
  };
  static constexpr int kCools = 16;
  static constexpr uint32_t kRetryMs = 60000;  // после неудачной попытки: 1, 2, 4, 8 мин
  static constexpr uint8_t kSeriesMax = 4;     // перерыв растёт до cooldown_ms × 2^4
  bool cooling(uint32_t hash, uint32_t now_ms) const;
  Cool* cool_of(uint32_t hash);
  // Попытка не дошла до сцены: перерыв 1, 2, 4, 8 минут и соседа слушаем заново.
  void retry_later(uint32_t hash, uint32_t now_ms);
  bool rate_ok(uint32_t hash, uint32_t now_ms) const;
  KnownFn known_ = nullptr;
  const void* known_ctx_ = nullptr;
  static constexpr int kBurst = 6;
  uint32_t met_at_[kBurst] = {};
  int met_n_ = 0;
  Cool& cool(uint32_t hash, uint32_t now_ms, uint32_t dur);

  Peer* find(uint32_t hash);
  Peer* slot(uint32_t hash, uint32_t now_ms);
  Peer* hear(const Advert& a, int rssi, uint32_t now_ms);  // обычный пакет соседа вблизи
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
  bool acked_ = false;  // объявивший услышал «принял»
  uint8_t announced_mood_ = 128;
  // Свой уровень на время отсчёта: по нему считают исход дуэли оба брелока. Новый имплант
  // посреди отсчёта иначе дал бы им разные исходы.
  uint8_t pending_level_ = 0;
};

}  // namespace face
