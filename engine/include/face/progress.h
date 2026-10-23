// Рост брелока. На старте открыта база эмоций, остальные приходят «имплантами»
// за опыт: дни вместе, касания, шаги, встречи с другими брелоками и дуэли.
// Состояние — плоская структура постоянного размера: прошивка хранит её во флеше
// как есть, а load() понимает и старые версии записи.
#pragma once

#include <stdint.h>

#include "face/expressions.h"
#include "face/fmath.h"

namespace face {

// Имплант — набор эмоций, который открывается разом, когда набран опыт.
struct Implant {
  const char* tag;   // для экрана, латиницей: «EMPATHY.MOD»
  const char* name;  // по-русски, для Serial и документации
  uint32_t xp;       // сколько опыта нужно
};

int implant_count();
const Implant& implant(int i);
// К какому импланту относится эмоция (0 — база, открыта сразу).
int implant_of(Expr e);

// Сколько опыта за что дают. Касания и шаги ограничены за день, чтобы
// брелок рос от времени вместе, а не от того, что его трясут без остановки.
struct XpRules {
  uint16_t day = 60;   // новый день вместе
  uint16_t touch = 2;  // касание
  uint16_t touch_day_cap = 40;
  uint16_t steps_per_xp = 100;
  uint16_t steps_day_cap = 50;
  uint16_t friend_new = 40;    // новый знакомый брелок
  uint16_t friend_known = 10;  // старый знакомый, раз в день на каждого
  uint16_t duel_win = 25;
  uint16_t duel_lose = 10;
};

// Эмоция открыта, если её имплант уже установлен, или она в списке дополнительных
// (выигранные в дуэли, а потом и секретные), или открыто всё. Дополнительные хранятся
// по ключу от имени эмоции, а не по номеру в каталоге: новые эмоции и перестановки
// в каталоге не сбивают записанный прогресс.
//
// Как расширять: новое поле берётся из reserved (по умолчанию там нули), версия
// растёт на 1, а load() читает предыдущую версию и дописывает значения по умолчанию.
// Размер записи при этом не меняется.
struct Progress {
  static constexpr uint32_t kMagic = 0x4B43484Bu;  // «KCHK»
  static constexpr uint16_t kVersion = 2;
  static constexpr int kExtraSlots = 16;
  static constexpr uint8_t kAllUnlocked = 1;  // флаг: открыто всё

  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint16_t days = 0;  // дней вместе
  uint32_t xp = 0;
  uint32_t touches = 0;
  uint32_t steps = 0;
  uint16_t friends = 0;
  uint16_t duels_won = 0;
  uint16_t duels_lost = 0;
  uint16_t today_touch_xp = 0;
  uint16_t today_step_xp = 0;
  uint16_t step_rest = 0;  // шаги, которые ещё не превратились в опыт
  uint8_t implant = 0;  // сколько имплантов уже установлено (0 — только база)
  uint8_t flags = 0;    // kAllUnlocked
  uint8_t reserved[26] = {};         // запас под будущие поля: серии, достижения
  uint32_t extra[kExtraSlots] = {};  // ключи дополнительно открытых эмоций, 0 — пусто
  uint32_t crc = 0;

  // Чистое состояние нового брелока: открыта только база.
  void reset();
  bool is_unlocked(Expr e) const;
  // Открыть эмоцию досрочно. false — уже открыта или нет свободного места.
  bool unlock(Expr e);
  int unlocked_count() const;
  int extra_count() const;
  void unlock_all();

  // Каждое начисление возвращает номер импланта, который только что
  // установился, или −1. Если за раз набралось на несколько — вернётся последний.
  int add_xp(uint32_t xp);
  int new_day(const XpRules& r = {});
  int touch(const XpRules& r = {});
  int add_steps(uint32_t n, const XpRules& r = {});
  int met_friend(bool is_new, const XpRules& r = {});
  int duel(bool won, const XpRules& r = {});

  // Награда за дуэль: открыть одну эмоцию из ближайшего ещё не установленного
  // импланта раньше срока. Возвращает её или Expr::Count, если открывать нечего.
  Expr steal_one(Rng& rng);

  // Сколько опыта до следующего импланта (0 — всё открыто).
  uint32_t xp_to_next() const;

  // Контрольная сумма для хранения во флеше: при порче или смене формата — reset().
  uint32_t compute_crc() const;
  void seal() { crc = compute_crc(); }
  bool valid() const { return magic == kMagic && version == kVersion && crc == compute_crc(); }
  // Разобрать запись из флеша любой известной версии. false — запись чужая или испорчена.
  bool load(const void* data, uint32_t n);

  // Ключ эмоции для списка дополнительных: FNV-1a от её id, никогда не 0.
  static uint32_t key(Expr e);
};

static_assert(sizeof(Progress) == 128, "запись во флеше постоянного размера");

}  // namespace face
