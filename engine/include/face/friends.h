// Знакомые брелоки и уровень дружбы с каждым. Дружба растёт от числа дней,
// в которые брелоки встречались: раз в день на каждого, сколько бы раз они ни
// виделись. Как и Progress, плоская запись постоянного размера: прошивка хранит её
// во флеше как есть.
#pragma once

#include <stdint.h>

namespace face {

// Уровни: 0 — новый знакомый, дальше BUDDY, FRIENDS, BEST FRIENDS и BFF.
constexpr int kFriendLevels = 5;
const char* friend_level_tag(int level);   // для экрана
const char* friend_level_name(int level);  // по-русски
// Сколько дней встреч нужно для уровня.
uint16_t friend_level_days(int level);

struct FriendRecord {
  uint32_t hash = 0;      // Identity::hash соседа, 0 — пустое место
  uint16_t days = 0;      // в сколько разных дней виделись
  uint16_t last_day = 0;  // номер дня последней встречи (Progress::days)
};

struct Friends {
  static constexpr uint32_t kMagic = 0x4B434652u;  // «KCFR»
  static constexpr uint16_t kVersion = 1;
  static constexpr int kSlots = 16;

  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t reserved = 0;
  FriendRecord f[kSlots];
  uint32_t crc = 0;

  void reset() { *this = Friends{}; }
  // Встреча с брелоком hash в день day. Возвращает уровень после встречи; в *before —
  // уровень до неё или −1, если это новый знакомый; в *first_today — первая ли это встреча
  // с ним за день. Когда места нет, новый вытесняет того, с кем виделись меньше всего
  // дней (при равенстве — давнее всех).
  int meet(uint32_t hash, uint16_t day, int* before = nullptr, bool* first_today = nullptr);
  // Уровень дружбы; −1 — не знакомы.
  int level(uint32_t hash) const;
  const FriendRecord* find(uint32_t hash) const;
  int count() const;

  uint32_t compute_crc() const;
  void seal() { crc = compute_crc(); }
  bool valid() const { return magic == kMagic && version == kVersion && crc == compute_crc(); }
  bool load(const void* data, uint32_t n);
};

static_assert(sizeof(Friends) == 140, "запись во флеше постоянного размера");

}  // namespace face
