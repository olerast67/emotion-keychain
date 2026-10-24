#include "face/encounter.h"

#include "face/clock.h"

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
  out[0] = (uint8_t)(kAdvCompany & 0xFF);
  out[1] = (uint8_t)(kAdvCompany >> 8);
  out[2] = 'K';
  out[3] = 'C';
  out[4] = kAdvVersion;
  for (int i = 0; i < 4; ++i) out[5 + i] = (uint8_t)(a.hash >> (8 * i));
  out[9] = a.level;
  out[10] = a.flags;
  out[11] = a.mood;
  out[12] = a.countdown;
  out[13] = (uint8_t)(a.to & 0xFF);
  out[14] = (uint8_t)(a.to >> 8);
}

bool adv_decode(const uint8_t* p, int n, Advert* out) {
  if (!p || n < kAdvLen) return false;
  if (p[0] != (kAdvCompany & 0xFF) || p[1] != (kAdvCompany >> 8) || p[2] != 'K' || p[3] != 'C' ||
      p[4] != kAdvVersion)
    return false;
  Advert a;
  a.hash = (uint32_t)p[5] | (uint32_t)p[6] << 8 | (uint32_t)p[7] << 16 | (uint32_t)p[8] << 24;
  if (!a.hash) return false;
  a.level = p[9];
  a.flags = p[10];
  a.mood = p[11];
  a.countdown = p[12];
  a.to = (uint16_t)(p[13] | p[14] << 8);
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
}

bool Encounters::dueled_today(uint32_t hash) const {
  for (uint32_t h : dueled_)
    if (h == hash) return true;
  return false;
}

void Encounters::mark_dueled(uint32_t hash) {
  if (dueled_today(hash)) return;
  dueled_[dueled_n_ % kDueled] = hash;  // переполнение — вытесняется самый старый
  ++dueled_n_;
}

Encounters::Peer* Encounters::find(uint32_t hash) {
  for (Peer& p : peers_)
    if (p.hash == hash) return &p;
  return nullptr;
}

Encounters::Peer* Encounters::slot(uint32_t hash, uint32_t now) {
  if (Peer* p = find(hash)) return p;
  Peer* best = &peers_[0];
  for (Peer& p : peers_) {
    if (!p.hash) {
      best = &p;
      break;
    }
    if (now - p.last_ms > now - best->last_ms) best = &p;  // давнее всех слышанный
  }
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

void Encounters::announce(Peer& p, uint32_t now) {
  const bool duel = (flags_ & kAdvDuelReady) && (p.flags & kAdvDuelReady) && !dueled_today(p.hash);
  pending_ = Meeting{};
  pending_.kind = duel ? Rendezvous::Duel : Rendezvous::Greet;
  pending_.peer = p.hash;
  pending_.peer_level = p.level;
  pending_.peer_flags = p.flags;
  pending_.rssi = p.rssi;
  announced_mood_ = mood_;
  pending_.nonce = encounter_nonce(me_, p.hash, announced_mood_);
  if (duel) pending_.won = duel_won(me_, p.hash, level_, p.level, pending_.nonce);
  start_ms_ = now + cfg_.countdown_ms;
  announcer_ = true;
}

void Encounters::on_advert(const Advert& a, int rssi, uint32_t now) {
  if (!a.hash || a.hash == me_) return;
  // Сосед объявил сцену со мной: ставим её на тот же момент, даже если сигнал слабее порога.
  if (a.countdown && a.to == (uint16_t)(me_ & 0xFFFF) && a.hash < me_) {
    Peer* p = slot(a.hash, now);
    p->last_ms = now;
    p->level = a.level, p->flags = a.flags, p->mood = a.mood, p->rssi = rssi;
    const bool same = pending() && pending_.peer == a.hash && !announcer_;
    if (!same && pending()) return;  // уже ждём другую сцену
    if (!same) {
      if (p->cooled && !time_reached(now, p->cooldown_until)) return;
      pending_ = Meeting{};
      pending_.kind = (a.flags & kAdvDuelCalled) ? Rendezvous::Duel : Rendezvous::Greet;
      pending_.peer = a.hash;
      pending_.peer_level = a.level;
      pending_.peer_flags = a.flags;
      pending_.rssi = rssi;
      pending_.nonce = encounter_nonce(me_, a.hash, a.mood);
      if (pending_.kind == Rendezvous::Duel) pending_.won = duel_won(me_, a.hash, level_, a.level, pending_.nonce);
      announcer_ = false;
    }
    start_ms_ = now + (uint32_t)a.countdown * 100u;  // последний пакет точнее
    return;
  }
  if (rssi < cfg_.rssi_min) return;
  Peer* p = slot(a.hash, now);
  if (p->hits && now - p->last_ms > cfg_.confirm_ms) {
    p->hits = 0;  // прошлый раз был давно: встреча начинается заново
    p->first_ms = now;
  }
  p->last_ms = now;
  p->level = a.level, p->flags = a.flags, p->mood = a.mood, p->rssi = rssi;
  if (p->hits < 255) ++p->hits;
  if (p->hits < 2 || pending()) return;
  if (p->cooled && !time_reached(now, p->cooldown_until)) return;
  // Сцену объявляет брелок с меньшим позывным, второй подстраивается по его пакету.
  if (me_ < a.hash) announce(*p, now);
}

Advert Encounters::advert(uint32_t now) const {
  Advert a;
  a.hash = me_;
  a.level = level_;
  a.flags = flags_;
  a.mood = mood_;
  if (pending() && announcer_ && !time_reached(now, start_ms_)) {
    const uint32_t left = time_left(now, start_ms_);
    a.countdown = (uint8_t)((left + 99u) / 100u > 255u ? 255u : (left + 99u) / 100u);
    a.to = (uint16_t)(pending_.peer & 0xFFFF);
    a.mood = announced_mood_;  // на нём держится общий nonce
    if (pending_.kind == Rendezvous::Duel) a.flags |= kAdvDuelCalled;
  }
  return a;
}

bool Encounters::poll(uint32_t now, Meeting* out) {
  // Забываем пропавших соседей.
  for (Peer& p : peers_)
    if (p.hash && now - p.last_ms > cfg_.forget_ms && !(p.cooled && !time_reached(now, p.cooldown_until)) &&
        !(pending() && pending_.peer == p.hash))
      p = Peer{};
  if (!pending() || !time_reached(now, start_ms_)) return false;
  *out = pending_;
  if (Peer* p = find(pending_.peer)) {
    p->cooled = true;
    p->cooldown_until = now + cfg_.cooldown_ms;
    p->hits = 0;
  } else {
    Peer* q = slot(pending_.peer, now);
    q->last_ms = now;
    q->cooled = true;
    q->cooldown_until = now + cfg_.cooldown_ms;
  }
  if (pending_.kind == Rendezvous::Duel) mark_dueled(pending_.peer);
  pending_ = Meeting{};
  announcer_ = false;
  return true;
}

}  // namespace face
