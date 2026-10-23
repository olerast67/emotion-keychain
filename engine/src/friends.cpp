#include "face/friends.h"

namespace face {

namespace {

const char* const kTags[kFriendLevels] = {"NEW FRIEND", "BUDDY", "FRIENDS", "BEST FRIENDS", "BFF"};
const char* const kNames[kFriendLevels] = {"новый знакомый", "приятель", "друзья", "лучшие друзья",
                                           "друзья навсегда"};
const uint16_t kDays[kFriendLevels] = {1, 3, 7, 15, 30};

int level_of(uint16_t days) {
  int l = 0;
  while (l + 1 < kFriendLevels && days >= kDays[l + 1]) ++l;
  return l;
}

uint32_t crc32(const uint8_t* p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

}  // namespace

const char* friend_level_tag(int level) { return kTags[level < 0 ? 0 : level >= kFriendLevels ? kFriendLevels - 1 : level]; }
const char* friend_level_name(int level) {
  return kNames[level < 0 ? 0 : level >= kFriendLevels ? kFriendLevels - 1 : level];
}
uint16_t friend_level_days(int level) { return kDays[level < 0 ? 0 : level >= kFriendLevels ? kFriendLevels - 1 : level]; }

const FriendRecord* Friends::find(uint32_t hash) const {
  if (!hash) return nullptr;
  for (const FriendRecord& r : f)
    if (r.hash == hash) return &r;
  return nullptr;
}

int Friends::level(uint32_t hash) const {
  const FriendRecord* r = find(hash);
  return r ? level_of(r->days) : -1;
}

int Friends::count() const {
  int n = 0;
  for (const FriendRecord& r : f) n += r.hash != 0;
  return n;
}

int Friends::meet(uint32_t hash, uint16_t day, int* before, bool* first_today) {
  if (before) *before = -1;
  if (first_today) *first_today = false;
  if (!hash) return 0;
  FriendRecord* r = (FriendRecord*)find(hash);
  if (r) {
    if (before) *before = level_of(r->days);
    if (r->last_day != day) {
      r->days = (uint16_t)(r->days + (r->days < 0xFFFF));
      r->last_day = day;
      if (first_today) *first_today = true;
    }
    return level_of(r->days);
  }
  if (first_today) *first_today = true;
  // Новый знакомый: пустое место или тот, с кем дружба слабее всего.
  FriendRecord* slot = &f[0];
  for (FriendRecord& x : f) {
    if (!x.hash) {
      slot = &x;
      break;
    }
    if (x.days < slot->days || (x.days == slot->days && (uint16_t)(day - x.last_day) > (uint16_t)(day - slot->last_day)))
      slot = &x;
  }
  *slot = FriendRecord{hash, 1, day};
  return 0;
}

uint32_t Friends::compute_crc() const {
  return crc32((const uint8_t*)this, (uint32_t)((const uint8_t*)&crc - (const uint8_t*)this));
}

bool Friends::load(const void* data, uint32_t n) {
  if (!data || n != sizeof(Friends)) return false;
  Friends q;
  const uint8_t* p = (const uint8_t*)data;
  uint8_t* dst = (uint8_t*)&q;
  for (uint32_t i = 0; i < n; ++i) dst[i] = p[i];
  if (!q.valid()) return false;
  *this = q;
  return true;
}

}  // namespace face
