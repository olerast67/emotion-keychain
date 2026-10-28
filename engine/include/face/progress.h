// Рост брелока. На старте открыта база эмоций, остальные приходят «имплантами»
// за опыт: дни вместе, касания, шаги, встречи с другими брелоками и дуэли.
// Сверху — серия дней с хозяином и достижения: каждое один раз, с опытом и сценой.
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
  uint16_t care = 15;  // зарядили, когда был голоден: раз в день
  // Встречи приходят по радио, и выдумать их легче, чем касания: награды за них тоже
  // ограничены за день.
  uint8_t new_friends_day_cap = 5;  // новых знакомых с опытом и в счётчике друзей
  uint8_t gifts_day_cap = 1;        // подарков эмоций от старших брелоков
  uint8_t duel_prizes_day_cap = 2;  // эмоций, выигранных в дуэлях
  uint8_t duels_xp_day_cap = 3;     // дуэлей с опытом и в счётчиках побед
  uint8_t known_friends_day_cap = 10;  // старых знакомых с опытом за день
};

// Достижения. Номера битов в Progress::achievements — по порядку списка, поэтому
// новые дописываются только в конец.
#define FACE_ACHIEVEMENT_LIST(X) \
  X(first_friend)                \
  X(social)                      \
  X(first_win)                   \
  X(champion)                    \
  X(good_loser)                  \
  X(petted)                      \
  X(loved)                       \
  X(walker)                      \
  X(marathon)                    \
  X(streak3)                     \
  X(streak7)                     \
  X(streak30)                    \
  X(day100)                      \
  X(full_upgrade)                \
  X(whisperer)                   \
  X(caring)                      \
  X(survivor)                    \
  X(twin)                        \
  X(legend)                      \
  X(night_owl)                   \
  X(early_bird)                  \
  X(knock)                       \
  X(leet)                        \
  X(best_friend)

enum class Ach : uint8_t {
#define FACE_X(n) n,
  FACE_ACHIEVEMENT_LIST(FACE_X)
#undef FACE_X
      Count
};
constexpr int kAchCount = (int)Ach::Count;
static_assert(kAchCount <= 32, "достижения — биты одного uint32");

struct Achievement {
  const char* id;
  const char* tag;   // для экрана, латиницей, до 15 знаков
  const char* name;  // по-русски
  const char* how;   // за что
  uint16_t xp;
};
const Achievement& achievement(Ach a);

// Эмоция открыта, если её имплант уже установлен, или она в списке дополнительных
// (выигранные в дуэли, а потом и секретные), или открыто всё. Дополнительные хранятся
// по ключу от имени эмоции, а не по номеру в каталоге: новые эмоции и перестановки
// в каталоге не сбивают записанный прогресс.
//
// Как расширять: новое поле берётся из reserved (по умолчанию там нули), версия
// растёт на 1, а load() читает предыдущую версию и дописывает значения по умолчанию.
// Размер записи при этом не меняется. Поле, у которого значение по умолчанию — ноль, версию
// не меняет: в запасе старых записей и так нули, а прошлая прошивка новую запись читает.
struct Progress {
  static constexpr uint32_t kMagic = 0x4B43484Bu;  // «KCHK»
  static constexpr uint16_t kVersion = 3;
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
  // Версия 3: серия, достижения и счётчики для них (в версии 2 здесь были нули).
  uint16_t streak = 0;        // дней подряд, когда хозяин был рядом (касался)
  uint32_t achievements = 0;  // биты Ach
  uint16_t best_streak = 0;
  uint16_t chats = 0;   // секретных разговоров
  uint16_t feeds = 0;   // зарядили, когда был голоден
  uint16_t falls = 0;   // пережитых падений
  uint8_t touched_today = 0;
  uint8_t fed_today = 0;
  // Сколько наград за встречи уже выдано сегодня (в старых записях здесь был запас нулями).
  uint8_t today_new_friends = 0;
  uint8_t today_gifts = 0;
  uint8_t today_duel_prizes = 0;
  uint8_t today_duels = 0;
  uint8_t today_known_friends = 0;
  // Когда сегодня кормили: минута текущих суток / 10 с округлением вверх, плюс 1; 0 — не
  // кормили. Из запаса, без новой версии: в старых записях здесь ноль.
  uint8_t fed_at = 0;
  uint8_t reserved[4] = {};          // запас под будущие поля
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
  // Зарядили голодного: счётчик растёт всегда, опыт — раз в день.
  int feed(const XpRules& r = {});
  // То же с отметкой времени: day_min — минута текущих суток (Behavior::set_clock). Отметка
  // хранится в записи, поэтому может ли брелок «поесть» снова (may_feed: не раньше чем через
  // 3 часа в те же сутки), перезагрузка не сбрасывает; сбрасывают новые сутки.
  int feed(uint16_t day_min, const XpRules& r = {});
  bool may_feed(uint16_t day_min) const { return !fed_at || day_min / 10 >= fed_at - 1 + 18; }
  void fell() { falls = (uint16_t)(falls + 1); }
  void chatted() { chats = (uint16_t)(chats + 1); }

  bool earned(Ach a) const { return (int)a < kAchCount && (achievements >> (int)a) & 1u; }
  // Отметить достижение. true — новое (опыт за него начисляет вызывающий через add_xp).
  bool earn(Ach a);
  // Достижение за счётчики (друзья, победы, касания, шаги, серия…), которое только что
  // заработано: отмечает его и возвращает; Ach::Count — нового нет.
  Ach next_achievement();
  int achievement_count() const;

  // Награда за дуэль: открыть одну эмоцию из ближайшего ещё не установленного
  // импланта раньше срока. Возвращает её или Expr::Count, если открывать нечего.
  Expr steal_one(Rng& rng);
  // Те же награды с дневным лимитом: выигрыш в дуэли и подарок старшего брелока.
  Expr duel_prize(Rng& rng, const XpRules& r = {});
  Expr gift(Rng& rng, const XpRules& r = {});

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
