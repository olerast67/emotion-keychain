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
const Slot kSlots[] = {
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

uint32_t crc32(const uint8_t* p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

}  // namespace

int implant_count() { return kImplantCount; }
const Implant& implant(int i) { return kImplants[i < 0 ? 0 : i >= kImplantCount ? kImplantCount - 1 : i]; }

int implant_of(Expr e) {
  for (const Slot& s : kSlots)
    if (s.expr == e) return s.implant;
  return kImplantCount - 1;  // эмоция без места в таблице открывается последней (тест это ловит)
}

void Progress::reset() {
  *this = Progress{};
  for (const Slot& s : kSlots)
    if (s.implant == 0) unlock(s.expr);
}

int Progress::unlocked_count() const {
  int n = 0;
  for (int i = 0; i < kExprCount; ++i) n += is_unlocked((Expr)i);
  return n;
}

void Progress::unlock_all() {
  for (int i = 0; i < kExprCount; ++i) unlock((Expr)i);
  implant = (uint8_t)(kImplantCount - 1);
}

int Progress::add_xp(uint32_t n) {
  xp += n;
  int installed = -1;
  while (implant + 1 < kImplantCount && xp >= kImplants[implant + 1].xp) {
    ++implant;
    for (const Slot& s : kSlots)
      if (s.implant == implant) unlock(s.expr);
    installed = implant;
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
    unlock(e);
    return e;
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

}  // namespace face
