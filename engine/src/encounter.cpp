#include "face/encounter.h"

#include "face/clock.h"
#include "face/progress.h"

namespace face {

namespace {

uint32_t mix(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

uint32_t rotl(uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

}  // namespace

void adv_encode(const Advert& a, uint8_t out[kAdvLen]) {
  for (int i = 0; i < 16; ++i) out[i] = kAdvUuid[i];
  uint8_t* f = out + 16;
  f[0] = kAdvVersion;
  for (int i = 0; i < 4; ++i) f[1 + i] = (uint8_t)(a.hash >> (8 * i));
  f[5] = a.level;
  f[6] = a.flags;
  f[7] = a.mood;
  f[8] = a.countdown;
  f[9] = (uint8_t)(a.to & 0xFF);
  f[10] = (uint8_t)(a.to >> 8);
}

bool adv_decode(const uint8_t* p, int n, Advert* out) {
  if (!p || n < kAdvLen) return false;
  for (int i = 0; i < 16; ++i)
    if (p[i] != kAdvUuid[i]) return false;
  const uint8_t* f = p + 16;
  if (f[0] != kAdvVersion) return false;
  Advert a;
  a.hash = (uint32_t)f[1] | (uint32_t)f[2] << 8 | (uint32_t)f[3] << 16 | (uint32_t)f[4] << 24;
  if (!a.hash) return false;
  a.level = f[5];
  a.flags = f[6];
  a.mood = f[7];
  a.countdown = f[8];
  a.to = (uint16_t)(f[9] | f[10] << 8);
  *out = a;
  return true;
}

uint8_t mood_byte(float valence) {
  if (!(valence > -1.0f)) valence = -1.0f;  // и NaN
  if (valence > 1.0f) valence = 1.0f;
  return (uint8_t)((valence + 1.0f) * 127.5f + 0.5f);
}

uint32_t encounter_nonce(uint32_t a, uint32_t b, uint8_t announcer_mood) {
  const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
  return mix(lo ^ rotl(hi, 13) ^ (uint32_t)announcer_mood * 0x9E3779B9u);
}

bool duel_won(uint32_t me, uint32_t peer, uint8_t my_level, uint8_t peer_level, uint32_t nonce) {
  // a — меньший позывной; бросок и бонус считаются от его лица на обоих брелоках.
  const bool i_am_a = me < peer;
  const uint32_t a = i_am_a ? me : peer, b = i_am_a ? peer : me;
  const int la = i_am_a ? my_level : peer_level, lb = i_am_a ? peer_level : my_level;
  const int roll = (int)(mix(a ^ rotl(b, 13) ^ nonce) % 100u);
  const bool a_wins = roll + (la - lb) * 4 >= 50;
  return i_am_a ? a_wins : !a_wins;
}

void Encounters::init(uint32_t my_hash, const Config& cfg) {
  *this = Encounters{};
  cfg_ = cfg;
  me_ = my_hash;
}

void Encounters::set_self(uint8_t level, uint8_t flags, uint8_t mood) {
  level_ = level;
  flags_ = (uint8_t)(flags & ~kAdvDuelCalled);
  mood_ = mood;
}

void Encounters::new_day() {
  for (uint32_t& h : dueled_) h = 0;
  dueled_n_ = 0;
  for (Cool& c : cools_) c.met = 0;
}

bool Encounters::dueled_today(uint32_t hash) const {
  for (uint32_t h : dueled_)
    if (h == hash) return true;
  return false;
}

void Encounters::mark_dueled(uint32_t hash) {
  // Список полон — новых дуэлей до new_day нет: вытеснение старых записей позволило бы
  // второй раз за день сразиться с тем же соседом.
  if (dueled_today(hash) || dueled_n_ >= kDueled) return;
  dueled_[dueled_n_++] = hash;
}

bool Encounters::may_duel(uint32_t hash) const { return dueled_n_ < kDueled && !dueled_today(hash); }

bool Encounters::rate_ok(uint32_t hash, uint32_t now) const {
  if (known_ && known_(hash, known_ctx_)) return true;
  if (!met_n_) return true;
  const int last = (met_n_ - 1) % kBurst;
  if (now - met_at_[last] < cfg_.min_gap_ms) return false;
  if (met_n_ < kBurst) return true;
  return now - met_at_[met_n_ % kBurst] >= cfg_.burst_window_ms;  // самая старая из шести
}

bool Encounters::cooling(uint32_t hash, uint32_t now) const {
  for (const Cool& c : cools_)
    if (c.hash == hash) return now - c.at < c.dur;
  return false;
}

Encounters::Cool* Encounters::cool_of(uint32_t hash) {
  for (Cool& c : cools_)
    if (c.hash == hash) return &c;
  return nullptr;
}

void Encounters::retry_later(uint32_t hash, uint32_t now) {
  // Каждая неудача подряд — перерыв вдвое длиннее. Соседа слушаем заново: после перерыва
  // ему снова нужно быть слышным дважды.
  if (Peer* p = find(hash)) p->hits = 0;
  const Cool* c = cool_of(hash);
  uint8_t fails = c ? c->fails : 0;
  if (fails < 4) ++fails;
  cool(hash, now, kRetryMs << (fails - 1)).fails = fails;
}

Encounters::Cool& Encounters::cool(uint32_t hash, uint32_t now, uint32_t dur) {
  // Своя таблица перерывов, отдельно от соседей: поток выдуманных позывных вытесняет из
  // памяти соседей, но не перерыв настоящего. Место — его прежнее, пустое или того,
  // чей перерыв кончится раньше всех.
  Cool* best = &cools_[0];
  auto left = [now](const Cool& c) { return now - c.at < c.dur ? c.dur - (now - c.at) : 0u; };
  for (Cool& c : cools_) {
    if (c.hash == hash) {
      best = &c;
      break;
    }
    if (!c.hash) {
      if (best->hash) best = &c;
      continue;
    }
    if (best->hash && left(c) < left(*best)) best = &c;
  }
  if (best->hash != hash) *best = Cool{};
  best->hash = hash;
  best->at = now;
  best->dur = dur;
  return *best;
}

Encounters::Peer* Encounters::find(uint32_t hash) {
  for (Peer& p : peers_)
    if (p.hash == hash) return &p;
  return nullptr;
}

Encounters::Peer* Encounters::slot(uint32_t hash, uint32_t now) {
  if (Peer* p = find(hash)) return p;
  // Пустое место; иначе — услышанный один раз (прохожий или подделка), потом давнее всех
  // слышанный. Того, с кем ждём сцену, не вытесняем.
  Peer* best = nullptr;
  for (Peer& p : peers_) {
    if (!p.hash) {
      best = &p;
      break;
    }
    if (pending() && pending_.peer == p.hash) continue;
    const bool once = p.hits <= 1, best_once = best && best->hits <= 1;
    if (!best || (once && !best_once) || (once == best_once && now - p.last_ms > now - best->last_ms)) best = &p;
  }
  if (!best) return nullptr;
  *best = Peer{};
  best->hash = hash;
  best->first_ms = now;
  return best;
}

int Encounters::nearby(uint32_t now) const {
  int n = 0;
  for (const Peer& p : peers_) n += p.hash && now - p.last_ms < cfg_.forget_ms;
  return n;
}

bool Encounters::wants_busy(uint32_t now) const {
  if (pending()) return true;
  // Сосед, с которым встреча ещё может случиться: слышен недавно, не в перерыве и ждём
  // его объявления не дольше wait_ms (дальше poll даёт ему перерыв). Два брелока, которые
  // уже поздоровались и лежат рядом, эфир чаще не слушают.
  for (const Peer& p : peers_) {
    if (!p.hash || !p.hits || now - p.last_ms >= cfg_.confirm_ms || cooling(p.hash, now)) continue;
    if (p.hits >= 2 && now - p.ready_ms >= cfg_.wait_ms) continue;
    return true;
  }
  return false;
}

Encounters::Peer* Encounters::hear(const Advert& a, int rssi, uint32_t now) {
  Peer* p = slot(a.hash, now);
  if (!p) return nullptr;
  if (p->hits && now - p->last_ms > cfg_.confirm_ms) {
    p->hits = 0;  // прошлый раз был давно: встреча начинается заново
    p->first_ms = now;
  }
  p->last_ms = now;
  p->level = a.level, p->flags = a.flags, p->mood = a.mood, p->rssi = rssi;
  if (p->hits < 255) ++p->hits;
  if (p->hits == 2) p->ready_ms = now;  // встреча возможна: ждём сцену не дольше wait_ms
  return p;
}

void Encounters::announce(Peer& p, uint32_t now) {
  const bool duel = (flags_ & kAdvDuelReady) && (p.flags & kAdvDuelReady) && may_duel(p.hash);
  pending_ = Meeting{};
  pending_.kind = duel ? Rendezvous::Duel : Rendezvous::Greet;
  pending_.peer = p.hash;
  pending_.peer_level = p.level;
  pending_.peer_flags = p.flags;
  pending_.rssi = p.rssi;
  announced_mood_ = mood_;
  pending_.nonce = encounter_nonce(me_, p.hash, announced_mood_);
  pending_level_ = level_;
  // Исход по его уровню из обычного пакета — предварительный: окончательный считается по
  // уровню из его ответа «принял», по которому считал и он сам.
  if (duel) pending_.won = duel_won(me_, p.hash, pending_level_, p.level, pending_.nonce);
  start_ms_ = now + cfg_.countdown_ms;
  announcer_ = true;
  acked_ = false;
}

void Encounters::on_advert(const Advert& a, int rssi, uint32_t now) {
  if (!a.hash || a.hash == me_) return;
  // Пакеты не нашей прошивки или подделка: уровней столько, сколько имплантов.
  if (a.level >= implant_count()) return;
  // Ответ на моё объявление: сосед принял сцену. Дуэль он принимает, только если сам их
  // разрешил; принял встречу вместо дуэли — сцена у обоих будет встречей.
  if ((a.flags & kAdvAck) && a.to == (uint16_t)(me_ & 0xFFFF) && pending() && announcer_ && pending_.peer == a.hash) {
    acked_ = true;
    pending_.peer_level = a.level;  // в ответе — уровень, по которому он считал исход
    if (pending_.kind == Rendezvous::Duel && !(a.flags & kAdvDuelCalled)) {
      pending_.kind = Rendezvous::Greet;
      pending_.won = false;
    }
    if (pending_.kind == Rendezvous::Duel)
      pending_.won = duel_won(me_, a.hash, pending_level_, a.level, pending_.nonce);
  }
  // Сосед объявил сцену со мной: ставим её на тот же момент, даже если сигнал слабее порога.
  // Ответ «принял» третьего брелока объявлением не считается.
  if (a.countdown && !(a.flags & kAdvAck) && a.to == (uint16_t)(me_ & 0xFFFF) && a.hash < me_) {
    // Свой отсчёт никто не объявляет дольше countdown_ms; длиннее — подделка, которая
    // держала бы встречи занятыми.
    if ((uint32_t)a.countdown * 100u > cfg_.countdown_ms + 500u) return;
    // Сцену принимаем только от соседа, которого слышали вблизи дважды: объявление одним
    // пакетом издалека не проходит. Объявление вблизи — тоже его пакет: обычный мы могли
    // услышать лишь раз, тогда сцену примем по следующему пакету отсчёта.
    Peer* p = rssi >= cfg_.rssi_min ? hear(a, rssi, now) : find(a.hash);
    if (!p || p->hits < 2 || now - p->last_ms > cfg_.confirm_ms) return;
    if (rssi < cfg_.rssi_min - 15) return;  // совсем издалека — не сосед
    p->last_ms = now;
    p->level = a.level, p->flags = a.flags, p->mood = a.mood, p->rssi = rssi;
    const uint32_t start = now + (uint32_t)a.countdown * 100u;
    const bool same = pending() && pending_.peer == a.hash && !announcer_;
    if (same) {
      // Последний пакет точнее, но начало можно только приблизить.
      if (time_reached(start, start_ms_)) return;  // новый момент не раньше прежнего
      start_ms_ = start;
      return;
    }
    if (pending()) return;  // уже ждём другую сцену
    // Услышали объявление слишком поздно: ответ «принял» до объявившего уже не дойдёт.
    if ((uint32_t)a.countdown * 100u < cfg_.accept_min_ms) return;
    if (cooling(a.hash, now) || !rate_ok(a.hash, now)) return;
    // Дуэль — только если хозяин сам их разрешил. Сколько дуэлей сегодня, решает объявивший:
    // мой список мог пропасть с перезагрузкой, а сцена у обоих должна быть одна. Свой полный
    // список я показываю в пакете — без флага готовности (advert).
    const bool duel = (a.flags & kAdvDuelCalled) && (flags_ & kAdvDuelReady) && dueled_n_ < kDueled;
    pending_ = Meeting{};
    pending_.kind = duel ? Rendezvous::Duel : Rendezvous::Greet;
    pending_.peer = a.hash;
    pending_.peer_level = a.level;
    pending_.peer_flags = a.flags;
    pending_.rssi = rssi;
    pending_.nonce = encounter_nonce(me_, a.hash, a.mood);
    pending_level_ = level_;  // его и показываем в ответе «принял» до начала сцены
    if (duel) pending_.won = duel_won(me_, a.hash, pending_level_, a.level, pending_.nonce);
    announcer_ = false;
    start_ms_ = start;
    return;
  }
  if (rssi < cfg_.rssi_min) return;
  Peer* p = hear(a, rssi, now);
  if (!p) return;  // все места заняты соседями в перерыве
  if (p->hits < 2 || pending()) return;
  if (cooling(a.hash, now) || !rate_ok(a.hash, now)) return;
  // Сцену объявляет брелок с меньшим позывным, второй подстраивается по его пакету.
  if (me_ < a.hash) announce(*p, now);
}

Advert Encounters::advert(uint32_t now) const {
  Advert a;
  a.hash = me_;
  a.level = level_;
  a.flags = flags_;
  if (dueled_n_ >= kDueled) a.flags &= (uint8_t)~kAdvDuelReady;  // на сегодня дуэлей хватит
  a.mood = mood_;
  if (pending() && !time_reached(now, start_ms_)) {
    const uint32_t left = time_left(now, start_ms_);
    a.countdown = (uint8_t)((left + 99u) / 100u > 255u ? 255u : (left + 99u) / 100u);
    a.to = (uint16_t)(pending_.peer & 0xFFFF);
    a.level = pending_level_;  // уровень замер до начала сцены: на нём держится исход дуэли
    if (announcer_)
      a.mood = announced_mood_;  // на нём держится общий nonce
    else
      a.flags |= kAdvAck;  // принял: объявивший начнёт сцену, только услышав это
    if (pending_.kind == Rendezvous::Duel) a.flags |= kAdvDuelCalled;
  }
  return a;
}

bool Encounters::poll(uint32_t now, Meeting* out) {
  // Забываем пропавших соседей и кончившиеся перерывы: старая запись через 24.8 суток
  // часов снова выглядела бы свежей.
  for (Peer& p : peers_)
    if (p.hash && now - p.last_ms > cfg_.forget_ms && !(pending() && pending_.peer == p.hash)) p = Peer{};
  // Счёт неудачных попыток и серия встреч живут дольше самого перерыва, иначе повторы не
  // удлинялись бы.
  for (Cool& c : cools_) {
    if (!c.hash) continue;
    uint32_t keep = c.fails ? 16 * kRetryMs : 0u;
    if (c.met && cfg_.series_ms > keep) keep = cfg_.series_ms;
    const uint32_t age = now - c.at;
    if (age >= c.dur && age - c.dur >= keep) c = Cool{};
  }
  // Сосед слышен дважды, а сцены с ним всё нет: тот, кто должен объявить, нас не слышит или
  // не может объявить. Частое прослушивание тут не поможет — перерыв, как после объявления
  // без ответа. Окно ожидания отсчитывается от конца перерыва и не идёт, пока сцена объявлена.
  for (Peer& p : peers_) {
    if (!p.hash || p.hits < 2) continue;
    if ((pending() && pending_.peer == p.hash) || cooling(p.hash, now)) {
      p.ready_ms = now;
      continue;
    }
    if (now - p.ready_ms >= cfg_.wait_ms) retry_later(p.hash, now);
  }
  if (!pending() || !time_reached(now, start_ms_)) return false;
  if (announcer_ && !acked_) {
    // Сосед не ответил «принял» (не услышал, занят другой сценой, отказал): сцены нет у нас.
    // Повтор — после короткого перерыва, каждый раз вдвое длиннее: если он всё же ответил
    // и сыграл сцену, а мы ответ не услышали, он теперь в перерыве и отвечать не будет.
    retry_later(pending_.peer, now);
    pending_ = Meeting{};
    announcer_ = false;
    return false;
  }
  *out = pending_;
  {
    // Перерыв по серии встреч подряд: 10, 20, 40, 80, 160 минут.
    const Cool* prev = cool_of(pending_.peer);
    const uint8_t met = prev ? prev->met : 0;
    uint32_t dur = cfg_.cooldown_ms;
    for (uint8_t i = 0; i < met && dur < 0x40000000u; ++i) dur <<= 1;
    Cool& c = cool(pending_.peer, now, dur);
    c.fails = 0;
    c.met = met < kSeriesMax ? (uint8_t)(met + 1) : kSeriesMax;
  }
  // Друзья общий лимит не тратят (rate_ok их пропускает) и в нём не считаются.
  if (!(known_ && known_(pending_.peer, known_ctx_))) {
    met_at_[met_n_ % kBurst] = now;
    ++met_n_;
  }
  if (Peer* p = find(pending_.peer)) p->hits = 0;
  if (pending_.kind == Rendezvous::Duel) mark_dueled(pending_.peer);
  pending_ = Meeting{};
  announcer_ = false;
  return true;
}

}  // namespace face
