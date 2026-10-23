#include "face/progress.h"

namespace face {

namespace {

using E = Expr;

const Implant kImplants[] = {
    {"BASE", "База", 0},
    {"EMPATHY.MOD", "Модуль эмпатии", 150},
    {"REFLEX.SYS", "Рефлексы", 350},
    {"SOCIAL.LIB", "Общение", 600},
    {"MEME.DRV", "Мем-драйвер", 900},
    {"DRAMA.EXE", "Драма", 1250},
    {"SARCASM.DLL", "Сарказм", 1650},
    {"MEME.DRV2", "Мем-драйвер 2", 2100},
    {"PARTY.PKG", "Вечеринка", 2600},
    {"GLITCH.CORE", "Глитч-ядро", 3150},
    {"CHAOS.BIN", "Хаос", 3750},
    {"GHOST.SHELL", "Призрак в оболочке", 4400},
};
constexpr int kImplantCount = (int)(sizeof(kImplants) / sizeof(kImplants[0]));

struct Slot {
  E expr;
  uint8_t implant;
};

// Кому что. База — то, без чего персонаж не может жить с датчиками:
// сон, зарядка, батарея, холод, базовые радость, грусть и злость.
constexpr Slot kSlots[] = {
    // База
    {E::neutral, 0},
    {E::calm, 0},
    {E::happy, 0},
    {E::laugh, 0},
    {E::love, 0},
    {E::wink, 0},
    {E::content, 0},
    {E::sad, 0},
    {E::crying, 0},
    {E::angry, 0},
    {E::annoyed, 0},
    {E::grumpy, 0},
    {E::surprised, 0},
    {E::scared, 0},
    {E::worried, 0},
    {E::curious, 0},
    {E::confused, 0},
    {E::thinking, 0},
    {E::listening, 0},
    {E::hello, 0},
    {E::goodbye, 0},
    {E::shy, 0},
    {E::sleepy, 0},
    {E::sleeping, 0},
    {E::waking, 0},
    {E::bored, 0},
    {E::tired, 0},
    {E::dizzy, 0},
    {E::hungry, 0},
    {E::cold, 0},
    {E::hot, 0},
    {E::charging, 0},
    {E::charged, 0},
    {E::low_battery, 0},
    {E::critical_battery, 0},
    {E::error, 0},
    {E::music, 0},
    {E::booped, 0},
    {E::walking, 0},
    {E::running, 0},
    {E::upside_down, 0},
    {E::pocket_peek, 0},
    {E::lonely, 0},
    {E::sulking, 0},
    // Модуль эмпатии
    {E::joy, 1},
    {E::giggle, 1},
    {E::adoring, 1},
    {E::grateful, 1},
    {E::relieved, 1},
    {E::disappointed, 1},
    {E::hurt, 1},
    {E::embarrassed, 1},
    {E::guilty, 1},
    {E::apologetic, 1},
    // Рефлексы
    {E::excited, 2},
    {E::startled, 2},
    {E::shocked, 2},
    {E::amazed, 2},
    {E::nervous, 2},
    {E::terrified, 2},
    {E::alert, 2},
    {E::focused, 2},
    {E::calling, 2},
    // Общение
    {E::kiss, 3},
    {E::playful, 3},
    {E::proud, 3},
    {E::pleading, 3},
    {E::innocent, 3},
    {E::daydreaming, 3},
    {E::suspicious, 3},
    {E::skeptical, 3},
    {E::idea, 3},
    {E::processing, 3},
    // Мем-драйвер
    {E::shrug, 4},
    {E::poker_face, 4},
    {E::npc, 4},
    {E::bruh, 4},
    {E::unamused, 4},
    {E::lenny, 4},
    {E::not_bad, 4},
    {E::awkward, 4},
    // Драма
    {E::sobbing, 5},
    {E::furious, 5},
    {E::disgusted, 5},
    {E::exhausted, 5},
    {E::knocked_out, 5},
    {E::sick, 5},
    {E::afraid_dark, 5},
    {E::dazzled, 5},
    {E::starstruck, 5},
    // Сарказм
    {E::smug, 6},
    {E::mischievous, 6},
    {E::determined, 6},
    {E::disapproval, 6},
    {E::cringe, 6},
    {E::chad_stare, 6},
    {E::wtf, 6},
    {E::squint, 6},
    // Мем-драйвер 2
    {E::hmm_squint, 7},
    {E::me_gusta, 7},
    {E::swab_cat, 7},
    {E::smug_glasses, 7},
    {E::soy_wow, 7},
    {E::soy_scream, 7},
    {E::doomer, 7},
    {E::pog, 7},
    // Вечеринка
    {E::party, 8},
    {E::singing, 8},
    {E::vibing, 8},
    {E::cool, 8},
    {E::uwu, 8},
    {E::owo, 8},
    {E::money, 8},
    {E::tears_of_joy, 8},
    // Глитч-ядро
    {E::robot, 9},
    {E::derp, 9},
    {E::hypnotized, 9},
    {E::zoned_out, 9},
    {E::twitchy, 9},
    {E::sleep_deprived, 9},
    {E::queasy, 9},
    {E::speechless, 9},
    // Хаос
    {E::rage, 10},
    {E::table_flip, 10},
    {E::troll_grin, 10},
    {E::screaming, 10},
    {E::evil_laugh, 10},
    {E::devilish, 10},
    {E::mind_blown, 10},
    {E::challenge_accepted, 10},
    // Призрак в оболочке
    {E::spooky, 11},
    {E::boo, 11},
    {E::im_dead, 11},
    {E::game_over, 11},
    {E::flustered, 11},
    {E::bliss, 11},
    {E::touched, 11},
    {E::nervous_laugh, 11},
    {E::cyclops, 11},
};

// Номер импланта для каждой эмоции — таблицей: is_unlocked зовут часто.
// Эмоция без места в kSlots открывается последней (тест это ловит).
struct ImplantTable {
  uint8_t of[kExprCount];
};
constexpr ImplantTable make_implant_table() {
  ImplantTable t{};
  for (uint8_t& v : t.of) v = (uint8_t)(kImplantCount - 1);
  for (const Slot& s : kSlots) t.of[(int)s.expr] = s.implant;
  return t;
}
constexpr ImplantTable kImplantOf = make_implant_table();

// Ключ эмоции — FNV-1a от её id: не зависит от места в каталоге.
constexpr uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  for (; *s; ++s) h = (h ^ (uint8_t)*s) * 16777619u;
  return h ? h : 1u;
}
constexpr uint32_t kKeys[] = {
#define FACE_X(n) fnv1a(#n),
    FACE_EXPR_LIST(FACE_X)
#undef FACE_X
};
static_assert(sizeof(kKeys) / sizeof(kKeys[0]) == kExprCount, "ключи эмоций");

uint32_t crc32(const uint8_t* p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

uint16_t read16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t read32(const uint8_t* p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// Версия 1 (только платы разработки): те же поля до implant включительно, затем
// битовая карта открытых эмоций по номерам тогдашнего каталога из 139 эмоций
// и CRC на смещении 52. Перенос по возможности: каталог с тех пор мог измениться.
constexpr uint32_t kV1Size = 56, kV1Crc = 52, kV1Bits = 34;
constexpr int kV1ExprCount = 139;

bool from_v1(Progress& out, const uint8_t* p, uint32_t n) {
  if (n != kV1Size || crc32(p, kV1Crc) != read32(p + kV1Crc)) return false;
  out.reset();
  out.days = read16(p + 6);
  out.xp = read32(p + 8);
  out.touches = read32(p + 12);
  out.steps = read32(p + 16);
  out.friends = read16(p + 20);
  out.duels_won = read16(p + 22);
  out.duels_lost = read16(p + 24);
  out.today_touch_xp = read16(p + 26);
  out.today_step_xp = read16(p + 28);
  out.step_rest = read16(p + 30);
  out.implant = p[32] < kImplantCount ? p[32] : (uint8_t)(kImplantCount - 1);
  // Открытое сверх имплантов (выигранное в дуэлях) переезжает в список дополнительных.
  for (int i = 0; i < kV1ExprCount && i < kExprCount; ++i)
    if ((p[kV1Bits + (i >> 3)] >> (i & 7)) & 1u) out.unlock((Expr)i);
  out.seal();
  return true;
}

}  // namespace

int implant_count() { return kImplantCount; }
const Implant& implant(int i) { return kImplants[i < 0 ? 0 : i >= kImplantCount ? kImplantCount - 1 : i]; }

int implant_of(Expr e) { return (int)e < kExprCount ? kImplantOf.of[(int)e] : kImplantCount - 1; }

uint32_t Progress::key(Expr e) { return (int)e < kExprCount ? kKeys[(int)e] : 0u; }

void Progress::reset() { *this = Progress{}; }

bool Progress::is_unlocked(Expr e) const {
  if ((int)e >= kExprCount) return false;
  if ((flags & kAllUnlocked) || kImplantOf.of[(int)e] <= implant) return true;
  const uint32_t k = kKeys[(int)e];
  for (uint32_t x : extra)
    if (x == k) return true;
  return false;
}

bool Progress::unlock(Expr e) {
  if ((int)e >= kExprCount || is_unlocked(e)) return false;
  for (uint32_t& x : extra) {
    if (x) continue;
    x = kKeys[(int)e];
    return true;
  }
  return false;
}

int Progress::unlocked_count() const {
  int n = 0;
  for (int i = 0; i < kExprCount; ++i) n += is_unlocked((Expr)i);
  return n;
}

int Progress::extra_count() const {
  int n = 0;
  for (uint32_t x : extra) n += x != 0;
  return n;
}

void Progress::unlock_all() {
  flags |= kAllUnlocked;
  implant = (uint8_t)(kImplantCount - 1);
}

int Progress::add_xp(uint32_t n) {
  xp += n;
  int installed = -1;
  while (implant + 1 < kImplantCount && xp >= kImplants[implant + 1].xp) {
    ++implant;
    installed = implant;
  }
  if (installed >= 0) {
    // Что теперь открыл имплант, освобождает место в списке дополнительных.
    // Ключи эмоций, которых нет в этом каталоге (из будущих прошивок), не трогаем.
    for (uint32_t& x : extra) {
      if (!x) continue;
      for (int i = 0; i < kExprCount; ++i) {
        if (kKeys[i] != x) continue;
        if (kImplantOf.of[i] <= implant) x = 0;
        break;
      }
    }
  }
  return installed;
}

int Progress::new_day(const XpRules& r) {
  ++days;
  today_touch_xp = today_step_xp = 0;
  return add_xp(r.day);
}

int Progress::touch(const XpRules& r) {
  ++touches;
  if (today_touch_xp >= r.touch_day_cap) return -1;
  const uint16_t g = (uint16_t)imin(r.touch, r.touch_day_cap - today_touch_xp);
  today_touch_xp = (uint16_t)(today_touch_xp + g);
  return add_xp(g);
}

int Progress::add_steps(uint32_t n, const XpRules& r) {
  steps += n;
  uint32_t pool = step_rest + n;
  const uint32_t g = pool / r.steps_per_xp;
  step_rest = (uint16_t)(pool % r.steps_per_xp);
  const uint32_t room = r.steps_day_cap > today_step_xp ? r.steps_day_cap - today_step_xp : 0;
  const uint32_t give = g < room ? g : room;
  today_step_xp = (uint16_t)(today_step_xp + give);
  return give ? add_xp(give) : -1;
}

int Progress::met_friend(bool is_new, const XpRules& r) {
  if (is_new) ++friends;
  return add_xp(is_new ? r.friend_new : r.friend_known);
}

int Progress::duel(bool won, const XpRules& r) {
  if (won)
    ++duels_won;
  else
    ++duels_lost;
  return add_xp(won ? r.duel_win : r.duel_lose);
}

Expr Progress::steal_one(Rng& rng) {
  for (int imp = implant + 1; imp < kImplantCount; ++imp) {
    Expr cand[16];
    int n = 0;
    for (const Slot& s : kSlots)
      if (s.implant == imp && !is_unlocked(s.expr) && n < 16) cand[n++] = s.expr;
    if (!n) continue;
    const Expr e = cand[rng.next() % (uint32_t)n];
    return unlock(e) ? e : Expr::Count;  // список дополнительных полон — открывать некуда
  }
  return Expr::Count;
}

uint32_t Progress::xp_to_next() const {
  if (implant + 1 >= kImplantCount) return 0;
  const uint32_t need = kImplants[implant + 1].xp;
  return need > xp ? need - xp : 0;
}

uint32_t Progress::compute_crc() const {
  return crc32((const uint8_t*)this, (uint32_t)((const uint8_t*)&crc - (const uint8_t*)this));
}

bool Progress::load(const void* data, uint32_t n) {
  const uint8_t* p = (const uint8_t*)data;
  if (!p || n < 8 || read32(p) != kMagic) return false;
  switch (read16(p + 4)) {
    case 1: return from_v1(*this, p, n);
    case kVersion: {
      if (n != sizeof(Progress)) return false;
      Progress q;
      uint8_t* dst = (uint8_t*)&q;
      for (uint32_t i = 0; i < n; ++i) dst[i] = p[i];
      if (!q.valid()) return false;
      *this = q;
      return true;
    }
    default: return false;  // запись более новой прошивки или чужая
  }
}

}  // namespace face
