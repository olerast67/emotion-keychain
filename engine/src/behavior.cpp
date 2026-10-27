// Поведение: события датчиков → эмоции. Пулы вариантов реакций, лестницы
// интенсивности, настроение, суточный ритм и фоновые занятия.
#include "face/behavior.h"

#include "face/identity.h"

namespace face {

namespace {

constexpr uint32_t kMin = 60000u;
constexpr uint32_t kDay = 24u * 60u * kMin;
// Сюрпризы — не реже раза в неделю: так любой срок остаётся меньше 2^31 мс.
constexpr uint32_t kMaxSurpriseMs = 7u * kDay;
constexpr uint32_t kMaxManualMs = 10u * kMin;
// Сколько показываем продолжение цепочки движка, прежде чем снова выбирать по настроению.
constexpr uint32_t kFollowMs = 4000u;
// Дольше этого после своего выбора цепочкам не уступаем: закрытая эмоция может
// подмениться той же самой (love → adoring → love), и цепочка не кончится никогда.
constexpr uint32_t kChainMaxMs = 20000u;
// Сколько ждём, пока движок сам перейдёт по цепочке, если он обновляется после поведения.
constexpr uint32_t kHoldSlackMs = 50u;
// Реплики «по желанию» (на тапы, от скуки) — не чаще этого.
constexpr uint32_t kSayGapMs = 1500u;
// Между сценами очереди — эмоция-развязка прошлой сцены. Пауза постоянная: два брелока,
// которые поставили в очередь одно и то же (дуэль → разговор), остаются синхронными.
constexpr uint32_t kBetweenScenesMs = 2500u;

const char* const kEventIds[] = {
#define FACE_X(n) #n,
    FACE_EVENT_LIST(FACE_X)
#undef FACE_X
};

const char* const kEventNames[] = {
    "Тап",
    "Двойной тап",
    "Встряхнули",
    "Сильно трясут",
    "Падение",
    "Удар",
    "Взяли в руки",
    "Идём",
    "Бежим",
    "Лежит неподвижно",
    "Вверх ногами",
    "Перевернули назад",
    "Громкий звук",
    "Речь рядом",
    "Музыка",
    "Тишина",
    "Темно",
    "Светло",
    "Очень ярко",
    "Холодно",
    "Жарко",
    "Температура в норме",
    "Зарядка подключена",
    "Заряжен",
    "Зарядку отключили",
    "Мало заряда",
    "Заряд почти ноль",
    "Кнопка",
    "Долгое нажатие",
    "Новый знакомый брелок",
    "Знакомый брелок рядом",
    "Победа в дуэли",
    "Поражение в дуэли",
};
static_assert(sizeof(kEventNames) / sizeof(kEventNames[0]) == kEventCount, "названия событий");

// День недели по григорианскому календарю: 0 — воскресенье (алгоритм Сакамото).
int weekday(int y, int m, int d) {
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int day_of_year(int y, int m, int d) {
  static const int before[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return before[m - 1] + d + (leap && m > 2 ? 1 : 0);
}

inline float clamp01(float v) { return clampf(v, 0.0f, 1.0f); }
inline float clamp11(float v) { return clampf(v, -1.0f, 1.0f); }

// Фоновые занятия, когда ничего не происходит.
struct IdleAct {
  Expr expr;
  Clip clip;
  uint16_t ms;
};

enum IdleId : uint8_t {
  kLookAround,
  kDoubleBlink,
  kWink,
  kYawn,
  kSigh,
  kSneeze,
  kDaydream,
  kSing,
  kThink,
  kCurious,
  kShy,
  kGiggle,
  kSmug,
  kZoned,
  kShrug,
  kNpc,
  kPoker,
  kHmm,
  kLenny,
  kMeGusta,
  kSwabCat,
  kChad,
  kSmugGlasses,
  kSoyWow,
  kTroll,
  kMoney,
  kRobot,
  kParty,
  kDerp,
  kCool,
  kDevil,
  kBoo,
  kCyclops,
  kFocused,
  kNervous,
  kUwu,
  kDoomer,
  kImDead,
  kNervousLaugh,
  kGuilty,
  kApologetic,
  kKiss,
  kIdleCount
};

const IdleAct kIdle[kIdleCount] = {
    {Expr::Count, Clip::look_around, 0},   {Expr::Count, Clip::blink, 0},          {Expr::wink, Clip::None, 1800},
    {Expr::Count, Clip::yawn, 0},          {Expr::Count, Clip::sigh, 0},           {Expr::Count, Clip::sneeze, 0},
    {Expr::daydreaming, Clip::None, 7000}, {Expr::singing, Clip::None, 5000},      {Expr::thinking, Clip::None, 4500},
    {Expr::curious, Clip::None, 4000},     {Expr::shy, Clip::None, 3000},          {Expr::giggle, Clip::None, 2500},
    {Expr::smug, Clip::None, 3500},        {Expr::zoned_out, Clip::None, 6000},    {Expr::shrug, Clip::None, 3500},
    {Expr::npc, Clip::None, 5000},         {Expr::poker_face, Clip::None, 4500},   {Expr::hmm_squint, Clip::None, 4500},
    {Expr::lenny, Clip::None, 4500},       {Expr::me_gusta, Clip::None, 4500},     {Expr::swab_cat, Clip::None, 5500},
    {Expr::chad_stare, Clip::None, 4500},  {Expr::smug_glasses, Clip::None, 4500}, {Expr::soy_wow, Clip::None, 3500},
    {Expr::troll_grin, Clip::None, 3500},  {Expr::money, Clip::None, 3000},        {Expr::robot, Clip::None, 5000},
    {Expr::party, Clip::None, 5000},       {Expr::derp, Clip::None, 3500},         {Expr::cool, Clip::None, 5000},
    {Expr::devilish, Clip::None, 4000},    {Expr::boo, Clip::None, 3000},          {Expr::cyclops, Clip::None, 3000},
    {Expr::focused, Clip::None, 4500},     {Expr::nervous, Clip::None, 3000},      {Expr::uwu, Clip::None, 3500},
    {Expr::doomer, Clip::None, 4500},      {Expr::im_dead, Clip::None, 3500},      {Expr::nervous_laugh, Clip::None, 3000},
    {Expr::guilty, Clip::None, 3000},      {Expr::apologetic, Clip::None, 3000},   {Expr::kiss, Clip::None, 2500},
};

}  // namespace

const char* event_id(Event e) { return (int)e < kEventCount ? kEventIds[(int)e] : "?"; }
const char* event_name(Event e) { return (int)e < kEventCount ? kEventNames[(int)e] : "?"; }

void Behavior::init(FaceEngine* engine, uint32_t seed, uint32_t now) {
  eng_ = engine;
  rng_.seed(seed ^ 0xA5A5F00Du);
  mood_ = Mood{};
  base_ = Expr::neutral;
  reacting_ = manual_ = follow_ = falling_ = false;
  react_until_ = manual_until_ = follow_until_ = 0;
  seen_t0_ = eng_ ? eng_->expression_started_ms() : now;
  chose_ms_ = now;
  last_touch_ = last_event_ = last_decay_ = last_now_ = now;
  dark_since_ = upside_since_ = fall_at_ = now;
  next_idle_ = now + 12000u + rng_.next() % 15000u;
  next_eval_ = now;
  shake_level_ = 0;
  for (uint32_t& t : tap_times_) t = 0;
  fed_ = vented_ = false;
  fed_at_ = vented_at_ = 0;
  moved_at_ = now - kDay;
  run_since_ = now;
  for (uint32_t& t : falls_) t = 0;
  for (uint8_t& r : recent_idle_) r = 255;
  for (Expr& r : recent_react_) r = Expr::Count;
  loud_at_ = 0;
  last_calling_ = last_cyclops_ = now;
  queue_n_ = shared_n_ = 0;
  queue_at_ = now;
  cur_scene_ = Scene::None;
  cur_shared_ = false;
  after_scene_ = Expr::Count;
  after_fresh_ = false;
  if (eco_ && eng_) eng_->options().frame_ms = eco_frame_ms_;
  eco_ = 0;
  saver_done_ = off_ = false;
  time_eggs_done_ = 0;
  last_clock_ = clock_min_;
  for (uint32_t& k : knocks_) k = 0;
  last_say_ = now - kSayGapMs;
  chat_peer_ = 0;
  schedule_surprise(now);
  holiday_done_ = 0;
  dark = charging = upside = music_on = speech_on = cold = hot = moving = running = false;
  battery = 2;
  reason_ = "";
  next_ = now;
  if (progress_) attach_progress(progress_);
  set_temperament(temp_);
}

void Behavior::set_temperament(const Temperament* t) {
  temp_ = t;
  if (!eng_) return;
  eng_->options().tempo = t ? t->tempo : 1.0f;
  eng_->options().glitches = t && t->glitches;
}

bool Behavior::usable(Expr e) const {
  return !progress_ || (int)e >= kExprCount || progress_->is_unlocked(e) || (temp_ && temperament_innate(*temp_, e));
}

Expr Behavior::flavor(Expr e) const {
  if (!temp_) return e;
  switch (e) {
    case Expr::neutral:
    case Expr::calm: return temp_->rest;
    case Expr::happy: return temp_->rest_happy;
    case Expr::bored: return temp_->rest_bored;
    default: return e;
  }
}

void Behavior::schedule_surprise(uint32_t now) {
  next_surprise_ = now + surprise_min_ + rng_.next() % (surprise_max_ - surprise_min_ + 1u);
}

void Behavior::hold_manual(uint32_t now, uint32_t ms) {
  manual_ = true;
  manual_until_ = now + (ms < kMaxManualMs ? ms : kMaxManualMs);
}

Brightness Behavior::brightness() const {
  if (dark) return Brightness::Dark;  // в кармане или в тёмной комнате хватает минимума
  const Expr cur = eng_ ? eng_->expression() : Expr::neutral;
  if (night() || cur == Expr::sleeping || cur == Expr::sleepy) return Brightness::Dim;
  return Brightness::Normal;
}

Expr Behavior::pick(const Option* opts, int n) {
  float w[16];
  float total = 0;
  n = n > 16 ? 16 : n;
  for (int i = 0; i < n; ++i) {
    w[i] = opts[i].weight > 0.0f ? opts[i].weight : 0.0f;
    if (!usable(opts[i].expr)) w[i] = 0.0f;
    for (Expr r : recent_react_)
      if (r == opts[i].expr) w[i] *= 0.35f;
    total += w[i];
  }
  Expr chosen = allowed(opts[0].expr);
  if (total > 0.0f) {
    float x = rng_.unit() * total;
    for (int i = 0; i < n; ++i) {
      x -= w[i];
      if (x <= 0.0f) {
        chosen = opts[i].expr;
        break;
      }
    }
  }
  for (int i = 3; i > 0; --i) recent_react_[i] = recent_react_[i - 1];
  recent_react_[0] = chosen;
  return chosen;
}

void Behavior::react(Expr e, uint32_t now, uint32_t ms, const char* why, Clip clip) {
  if (e != Expr::Count) show(allowed(e), now);
  if (clip != Clip::None) eng_->play_clip(clip, now);
  reacting_ = true;
  react_until_ = now + ms;
  next_eval_ = react_until_;
  manual_ = follow_ = false;
  reason_ = why;
  next_ = now;
}

void Behavior::show(Expr e, uint32_t now) {
  eng_->set_expression(e, now);
  seen_t0_ = eng_->expression_started_ms();
  chose_ms_ = now;
}

void Behavior::say(Cue c, uint32_t now, float chance) {
  if (!voice_ || c == Cue::Count) return;
  if (chance < 1.0f) {
    if (now - last_say_ < kSayGapMs) return;
    if (rng_.unit() >= chance * (temp_ ? temp_->talk : 0.7f)) return;
  }
  if (voice_->say(c, now)) last_say_ = now;
}

void Behavior::voice_event(Event e, uint32_t now) {
  // Реплика по эмоции, которую только что показали; у тапа и кнопки — хотя бы «бип».
  Cue c = cue_for(eng_->expression());
  float chance = 0.0f;
  switch (e) {
    case Event::tap:
      chance = 0.45f;
      if (c == Cue::Count) c = Cue::boop;
      break;
    case Event::double_tap:
      chance = 0.8f;
      if (c == Cue::Count) c = Cue::happy;
      break;
    case Event::button:
      chance = 0.9f;
      if (c == Cue::Count) c = Cue::boop;
      break;
    case Event::shake:
    case Event::shake_hard:
    case Event::impact:
    case Event::pick_up:
    case Event::loud:
    case Event::upside_down: chance = 0.7f; break;
    case Event::free_fall: chance = 0.95f; break;
    // Питание и выключение — всегда: это не болтовня, а сообщение хозяину.
    case Event::charge_start: c = Cue::charge_start, chance = 1.0f; break;
    case Event::charge_full: c = Cue::charge_full, chance = 1.0f; break;
    case Event::battery_low:
    case Event::battery_critical: c = Cue::battery_low, chance = 1.0f; break;
    case Event::button_long: c = Cue::goodbye, chance = 1.0f; break;
    default: break;
  }
  if (chance > 0.0f) say(c, now, chance);
}

void Behavior::age_marks(uint32_t now) {
  auto cap = [&](uint32_t& t) {
    if (now - t > kDay) t = now - kDay;
  };
  cap(last_touch_);
  cap(last_event_);
  cap(dark_since_);
  cap(upside_since_);
  cap(fall_at_);
  cap(last_calling_);
  cap(last_cyclops_);
  cap(chose_ms_);
  cap(moved_at_);
  cap(run_since_);
  if (fed_ && now - fed_at_ > kDay) fed_ = false;
  if (vented_ && now - vented_at_ > kDay) vented_ = false;
  // Отметки, где 0 значит «не было», просто забываются.
  for (uint32_t& t : tap_times_)
    if (t && now - t > kDay) t = 0;
  for (uint32_t& t : falls_)
    if (t && now - t > kDay) t = 0;
  if (loud_at_ && now - loud_at_ > kDay) loud_at_ = 0;
}

void Behavior::decay(uint32_t now) {
  age_marks(now);
  // Черты характера доступны снаружи по ссылке: держим их конечными и в 0..1.
  pers_.playful = sanef(pers_.playful, 0.6f, 0.0f, 1.0f);
  pers_.grumpy = sanef(pers_.grumpy, 0.3f, 0.0f, 1.0f);
  pers_.sleepy = sanef(pers_.sleepy, 0.4f, 0.0f, 1.0f);
  pers_.shy = sanef(pers_.shy, 0.4f, 0.0f, 1.0f);
  uint32_t dt_ms = now - last_decay_;
  last_decay_ = now;
  if (dt_ms > kDay) dt_ms = kDay;
  const float dt = (float)dt_ms * 0.001f;
  if (dt <= 0.0f) return;
  Mood& m = mood_;
  m.valence += (0.15f - m.valence) * clampf(dt * 0.01f, 0.0f, 1.0f);
  m.arousal += (0.25f - m.arousal) * clampf(dt * 0.03f, 0.0f, 1.0f);
  m.annoyance = fmax_(0.0f, m.annoyance - dt * 0.02f);
  m.affection += (0.2f - m.affection) * clampf(dt * 0.002f, 0.0f, 1.0f);
  if (now - last_event_ > kMin) m.boredom = clamp01(m.boredom + dt / 600.0f);
  const bool asleep = eng_ && eng_->expression() == Expr::sleeping;
  if (asleep || charging)
    m.energy = clamp01(m.energy + dt / 1800.0f);
  else
    m.energy = clamp01(m.energy - dt / 57600.0f * (night() ? 3.0f : 1.0f) * (0.5f + pers_.sleepy));
  shake_level_ = fmax_(0.0f, shake_level_ - dt * 0.6f);
}

void Behavior::on_event(Event e, uint32_t now) {
  if (!eng_) return;
  decay(now);
  last_now_ = now;
  eng_->note_activity(now);
  const uint32_t since_touch = now - last_touch_;
  last_event_ = now;
  mood_.boredom *= 0.3f;
  const Expr cur = eng_->expression();
  const bool asleep = cur == Expr::sleeping || cur == Expr::sleepy || cur == Expr::pocket_peek;
  const bool good = mood_.valence > 0.3f;

  switch (e) {
    case Event::tap:
    case Event::double_tap: {
      last_touch_ = now;
      off_ = false;
      if (progress_) grant(progress_->touch(), now);
      if (clock_in(3 * 60, 4 * 60)) earn(Ach::night_owl, now);
      if (clock_in(5 * 60, 6 * 60)) earn(Ach::early_bird, now);
      // Секретный стук «Shave and a haircut»: отвечает «two bits» и подмигивает.
      if (e == Event::tap && knock(now)) {
        react(pick_unlocked({Expr::playful, Expr::wink}), now, 3000, "секретный стук", Clip::wink);
        say(Cue::two_bits, now);
        // Сцена достижения — после ответа, чтобы его было слышно и видно.
        if (time_left(now, queue_at_) < 2000u) queue_at_ = now + 2000u;
        earn(Ach::knock, now);
        break;
      }
      const int add = e == Event::double_tap ? 2 : 1;
      for (int a = 0; a < add; ++a) {
        for (int i = kTapHistory - 1; i > 0; --i) tap_times_[i] = tap_times_[i - 1];
        tap_times_[0] = now;
      }
      int n = 0;
      for (uint32_t t : tap_times_)
        if (t && now - t < 3000u) ++n;
      if (asleep) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.15f);
        if (night() || n >= 3)
          react(Expr::grumpy, now, 3500, "разбудили ночью");
        else
          react(Expr::waking, now, 2600, "разбудили тапом", Clip::yawn);
        break;
      }
      mood_.affection = clamp01(mood_.affection + 0.04f);
      mood_.arousal = clamp01(mood_.arousal + 0.1f);
      if (since_touch > 2u * 60u * kMin && n <= 2) {
        react(Expr::touched, now, 3000, "тронули после долгой разлуки");
      } else if (n >= 9) {
        vented_ = true, vented_at_ = now;
        mood_.annoyance = clamp01(mood_.annoyance + 0.3f);
        const Option o[] = {{Expr::furious, 3}, {Expr::rage, 2}, {Expr::table_flip, 1}};
        react(pick(o), now, 4000, "тыкают без остановки");
      } else if (n >= 6) {
        vented_ = true, vented_at_ = now;
        mood_.annoyance = clamp01(mood_.annoyance + 0.2f);
        const Option o[] = {{Expr::angry, 3}, {Expr::twitchy, 1.5f}, {Expr::disapproval, 1}};
        react(pick(o), now, 3500, "слишком много тапов");
      } else if (n >= 4) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.12f);
        const Option o[] = {{Expr::annoyed, 3},   {Expr::unamused, 2},  {Expr::bruh, 1.5f},
                            {Expr::skeptical, 1}, {Expr::disgusted, 0.5f}};
        react(pick(o), now, 2600, "надоедают тапами", Clip::headshake);
      } else if (n == 3) {
        mood_.valence = clamp11(mood_.valence + 0.1f);
        const Option o[] = {{Expr::playful, 2 * pers_.playful},
                            {Expr::giggle, 2},
                            {Expr::tears_of_joy, good ? 1.0f : 0.2f},
                            {Expr::im_dead, good ? 0.5f : 0.1f},  // «умер со смеху»
                            {Expr::troll_grin, 0.3f}};
        react(pick(o), now, 2600, "тройной тап");
      } else if (n == 2) {
        mood_.valence = clamp11(mood_.valence + 0.15f);
        if (mood_.annoyance > 0.5f) {
          react(Expr::unamused, now, 2400, "двойной тап, но он не в духе");
        } else if (temp_) {
          const Option o[] = {{temp_->dtap[0], 3}, {temp_->dtap[1], 2}, {temp_->dtap[2], 1}};
          react(pick(o), now, 3000, "двойной тап");
        } else {
          const Option o[] = {{Expr::laugh, 3},
                              {Expr::happy, 2},
                              {Expr::playful, 1},
                              {Expr::giggle, 1},
                              {Expr::wink, 0.7f},
                              {Expr::tears_of_joy, good ? 0.6f : 0.0f},
                              {Expr::kiss, good ? 0.5f : 0.0f}};
          react(pick(o), now, 3000, "двойной тап");
        }
      } else if (mood_.annoyance > 0.5f) {
        const Option o[] = {{Expr::annoyed, 2}, {Expr::disapproval, 1}, {Expr::unamused, 1}};
        react(pick(o), now, 2000, "тап, но он не в духе");
      } else if (vented_ && now - vented_at_ < 3u * kMin && rng_.chance(0.7f)) {
        // Вспылил на тапы, злость почти прошла, а его гладят: неловко за вспышку.
        vented_ = false;
        const Option o[] = {{Expr::apologetic, 2}, {Expr::guilty, 1.5f}, {Expr::embarrassed, 1}};
        react(pick(o), now, 2500, "отошёл после вспышки, неловко");
      } else if (since_touch > 10u * kMin) {
        react(Expr::surprised, now, 1800, "тап после долгой паузы", Clip::startle);
      } else {
        // Самое частое событие: пул из нескольких вариантов, чтобы не надоедать.
        mood_.valence = clamp11(mood_.valence + 0.05f);
        if (temp_) {
          const Option o[] = {{temp_->tap[0], temp_->tap_w[0]},
                              {temp_->tap[1], temp_->tap_w[1]},
                              {temp_->tap[2], temp_->tap_w[2]},
                              {temp_->tap[3], temp_->tap_w[3]}};
          const Expr x = pick(o);
          react(x, now, x == Expr::booped ? 800 : 2000, "тап", Clip::boop);
          break;
        }
        const Option o[] = {{Expr::booped, 4},     {Expr::wink, good ? 1.2f : 0.3f},
                            {Expr::curious, 1},    {Expr::owo, good ? 0.6f : 0.2f},
                            {Expr::awkward, 0.4f}, {Expr::wtf, 0.2f}};
        const Expr x = pick(o);
        react(x, now, x == Expr::booped ? 800 : 2000, "тап", Clip::boop);
      }
      break;
    }
    case Event::shake:
    case Event::shake_hard: {
      shake_level_ += e == Event::shake_hard ? 3.0f : 1.0f;
      mood_.annoyance = clamp01(mood_.annoyance + (e == Event::shake_hard ? 0.15f : 0.06f));
      mood_.arousal = clamp01(mood_.arousal + 0.2f);
      if (shake_level_ >= 7.0f) {
        const Option o[] = {{Expr::sick, 2}, {Expr::queasy, 2}};
        react(pick(o), now, 4000, "укачало от тряски");
      } else if (shake_level_ >= 3.0f) {
        const Option o[] = {{Expr::dizzy, 4}, {Expr::hypnotized, 1}, {Expr::screaming, 0.6f}, {Expr::soy_scream, 0.3f}};
        react(pick(o), now, 4500, "сильно трясли");
      } else if (mood_.annoyance > 0.6f && pers_.grumpy > 0.4f) {
        react(Expr::angry, now, 3000, "трясут, когда он зол");
      } else {
        const Option o[] = {{Expr::surprised, 3}, {Expr::startled, 1}, {Expr::wtf, 0.6f}};
        react(pick(o), now, 1500, "встряхнули", Clip::shake_off);
      }
      break;
    }
    case Event::free_fall: {
      fall_at_ = now;
      falling_ = true;
      mood_.arousal = 1.0f;
      const Option o[] = {{Expr::terrified, 3}, {Expr::scared, 2}, {Expr::screaming, 1}};
      react(pick(o), now, 1500, "падаю!");
      break;
    }
    case Event::impact: {
      // Падением считается только удар вскоре после свободного падения.
      const bool fell = falling_ && now - fall_at_ < 2000u;
      falling_ = false;
      mood_.valence = clamp11(mood_.valence - (fell ? 0.35f : 0.12f));
      if (fell) {
        falls_[2] = falls_[1];
        falls_[1] = falls_[0];
        falls_[0] = now;
        if (progress_) progress_->fell();
        if (falls_[2] && now - falls_[2] < 10u * kMin) {
          react(Expr::game_over, now, 3500, "третье падение за 10 минут");
        } else if (falls_[1] && now - falls_[1] < 10u * kMin) {
          const Option o[] = {{Expr::crying, 3}, {Expr::sobbing, 1}};
          react(pick(o), now, 4000, "опять уронили");
        } else {
          react(Expr::knocked_out, now, 3500, "упал и ударился");
        }
      } else {
        const Option o[] = {{Expr::hurt, 3}, {Expr::cringe, 1}};
        react(pick(o), now, 1800, "стукнули");
      }
      break;
    }
    case Event::pick_up:
      last_touch_ = now;
      off_ = false;
      if (asleep) {
        react(Expr::waking, now, 2400, "взяли в руки сонного", Clip::yawn);
      } else if (since_touch > 2u * 60u * kMin) {
        react(Expr::touched, now, 3000, "взяли после долгой разлуки");
      } else if (since_touch > 10u * kMin) {
        react(Expr::hello, now, 2500, "взяли в руки после перерыва", Clip::hop);
      } else {
        const Option o[] = {{Expr::happy, good ? 3.0f : 1.0f},
                            {Expr::curious, 2},
                            {Expr::alert, 1},
                            {Expr::suspicious, 0.4f},
                            {Expr::kiss, good && mood_.affection > 0.35f ? 0.6f : 0.0f},
                            {Expr::uwu, good ? 0.3f : 0.0f}};
        react(pick(o), now, 2000, "взяли в руки");
      }
      break;
    case Event::walk: {
      // «Пошли гулять» — после настоящего перерыва, а не после каждого светофора: походка
      // кончается уже через 2.5 с без шагов.
      const bool first = !moving && now - moved_at_ > 2u * kMin;
      moving = true;
      moved_at_ = now;
      running = false;
      mood_.arousal = clamp01(mood_.arousal + 0.1f);
      if (first && mood_.energy > 0.5f && rng_.chance(0.5f))
        react(Expr::excited, now, 3000, "пошли гулять");
      else
        react(Expr::walking, now, 2500, "идём");
      break;
    }
    case Event::run: {
      if (!running) run_since_ = now;
      moving = running = true;
      moved_at_ = now;
      mood_.arousal = clamp01(mood_.arousal + 0.3f);
      mood_.energy = clamp01(mood_.energy - 0.03f);
      if (mood_.energy < 0.3f) {
        react(Expr::exhausted, now, 3500, "бежим, но сил мало");
      } else {
        const Option o[] = {{Expr::running, 3}, {Expr::challenge_accepted, 1}, {Expr::determined, 1}};
        react(pick(o), now, 3000, "бежим");
      }
      break;
    }
    case Event::still:
      if (moving) moved_at_ = now;  // перерыв в прогулке считается с остановки
      moving = running = false;
      next_eval_ = now;
      break;
    case Event::upside_down:
      upside = true;
      upside_since_ = now;
      react(Expr::upside_down, now, 2200, "перевернули вверх ногами");
      break;
    case Event::upright:
      if (!upside) break;  // и не был вверх ногами: возвращать нечего
      upside = false;
      if (now - upside_since_ > 5000u)
        react(Expr::dizzy, now, 2500, "долго висел вверх ногами");
      else
        react(Expr::relieved, now, 2200, "вернули как было");
      break;
    case Event::loud: {
      mood_.arousal = clamp01(mood_.arousal + 0.35f);
      const bool again = loud_at_ && now - loud_at_ < 10000u;
      loud_at_ = now;
      mood_.annoyance = clamp01(mood_.annoyance + (again ? 0.15f : 0.05f));
      if (asleep) {
        react(Expr::scared, now, 2500, "громкий звук разбудил");
      } else if (again) {
        const Option o[] = {{Expr::annoyed, 2}, {Expr::disapproval, 1}, {Expr::unamused, 1}, {Expr::disgusted, 0.5f}};
        react(pick(o), now, 2500, "опять шумят");
      } else {
        const Option o[] = {{Expr::startled, 4},
                            {Expr::wtf, 1},
                            {Expr::soy_scream, 0.4f},
                            {Expr::screaming, 0.3f},
                            {Expr::mind_blown, 0.15f}};
        react(pick(o), now, 1600, "громкий звук", Clip::startle);
      }
      break;
    }
    case Event::speech: {
      speech_on = true;
      const Option o[] = {{Expr::listening, 4}, {Expr::alert, 1}, {Expr::curious, 0.6f}};
      react(pick(o), now, 3500, "рядом говорят");
      break;
    }
    case Event::music: {
      music_on = true;
      mood_.valence = clamp11(mood_.valence + 0.1f);
      if (asleep) {
        react(Expr::singing, now, 5000, "музыка сквозь сон");
      } else {
        const Option o[] = {{Expr::music, 3}, {Expr::vibing, 3}, {Expr::soy_wow, 0.3f}, {Expr::me_gusta, 0.4f}};
        react(pick(o), now, 5000, "играет музыка");
      }
      break;
    }
    case Event::quiet:
      speech_on = music_on = false;
      next_eval_ = now;
      break;
    case Event::dark:
      if (!dark) dark_since_ = now;
      dark = true;
      if (!charging) {
        const Option o[] = {{Expr::pocket_peek, 3}, {Expr::afraid_dark, 1}};
        react(pick(o), now, 3000, "внезапно стало темно");
      }
      break;
    case Event::light:
      if (dark && now - dark_since_ > kMin)
        react(Expr::squint, now, 1500, "снова светло после долгой темноты");
      else if (dark)
        react(Expr::curious, now, 2200, "снова светло", Clip::peek);
      dark = false;
      break;
    case Event::bright: {
      dark = false;
      const Option o[] = {{Expr::dazzled, 3}, {Expr::squint, 1}, {Expr::cool, 0.4f}};
      react(pick(o), now, 2000, "резко очень ярко");
      break;
    }
    case Event::cold:
      cold = true;
      hot = false;
      react(Expr::cold, now, 4000, "холодно", rng_.chance(0.2f) ? Clip::sneeze : Clip::None);
      break;
    case Event::hot:
      hot = true;
      cold = false;
      react(Expr::hot, now, 4000, "жарко");
      break;
    case Event::temp_ok:
      if (cold || hot) react(Expr::relieved, now, 2500, "температура пришла в норму");
      cold = hot = false;
      break;
    case Event::charge_start: {
      charging = true;
      off_ = saver_done_ = false;
      // Сначала батарея наливается на экране, потом — реакция. Голодного покормили — опыт.
      Expr x = rng_.chance(0.15f) ? Expr::money : Expr::happy;
      const char* why = "подключили зарядку";
      if (battery == 0) x = Expr::relieved, why = "зарядку подключили в последний момент";
      if (battery == 1) x = Expr::grateful, why = "голодного покормили";
      // Покормить можно не чаще раза в 3 часа: иначе «голод» набивали бы, дёргая кабель
      // (после отключения датчик снова говорит «мало заряда», если ячейка не набралась).
      // Отметка о кормлении — и в записи роста по минутам суток: выключение и включение
      // кнопкой её не сбрасывают.
      if (battery <= 1 && progress_ && (!fed_ || now - fed_at_ >= 3u * 60u * kMin) &&
          progress_->may_feed(clock_min_)) {
        fed_ = true;
        fed_at_ = now;
        grant(progress_->feed(clock_min_), now);
      }
      set_eco(0);
      SceneParams p;
      p.value = battery_pct_;
      queue_scene(Scene::charge, p, allowed(x), now, why);
      break;
    }
    case Event::charge_full: {
      battery = 2;
      set_eco(0);
      const Option o[] = {{Expr::charged, 3}, {Expr::pog, 1}, {Expr::money, 0.5f}};
      react(pick(o), now, 3000, "зарядился");
      break;
    }
    case Event::charge_stop: {
      charging = false;
      // Недозарядили — расстроен. Заряд на зарядке прошивка оценивает сама; не знает — судим
      // по тому, был ли голоден до неё.
      const bool short_charge = battery_pct_ >= 0 ? battery_pct_ < 50 : battery < 2;
      react(short_charge ? Expr::disappointed : Expr::hello, now, 2200, "отключили зарядку");
      // Заряд снова меряется без USB: если ячейка ещё мало заряжена, датчик сам скажет
      // «мало заряда» после установления. Иначе голод держался бы до полной зарядки.
      battery = 2;
      break;
    }
    case Event::battery_low: {
      battery = 1;
      set_eco(1);
      const Option o[] = {{Expr::hungry, 2}, {Expr::pleading, 1}, {Expr::low_battery, 1}};
      react(pick(o), now, 4000, "мало заряда");
      break;
    }
    case Event::battery_critical:
      battery = 0;
      set_eco(2);
      react(Expr::critical_battery, now, 4000, "заряд почти закончился");
      break;
    case Event::button: {
      last_touch_ = now;
      off_ = false;
      if (asleep) {
        react(Expr::waking, now, 2400, "нажали кнопку", Clip::yawn);
      } else {
        const Option o[] = {{Expr::booped, 2}, {Expr::hello, 2}, {Expr::wink, 1}, {Expr::curious, 0.5f}};
        react(pick(o), now, 2000, "нажали кнопку", Clip::boop);
      }
      break;
    }
    case Event::button_long: react(Expr::goodbye, now, 2500, "выключают"); break;
    case Event::friend_new:
    case Event::friend_known: meet("??", e == Event::friend_new, now); break;
    case Event::duel_win:
    case Event::duel_lose: duel("??", e == Event::duel_win, now); break;
    case Event::Count: break;
  }
  voice_event(e, now);
  achieve(now);
}

Expr Behavior::choose_base(uint32_t now) {
  // Шаги не события (походку датчик сообщает, только когда она меняется): на ходу брелок не
  // «без дела», и ночью он не засыпает посреди прогулки.
  const uint32_t quiet = moving ? 0u : now - last_event_;
  const uint32_t alone = now - last_touch_;
  if (off_) return Expr::sleeping;
  if (charging) return Expr::charging;
  if (battery == 0) return Expr::critical_battery;
  if (dark && now - dark_since_ > 8000u) return Expr::sleeping;
  if (night() && quiet > 10u * kMin) return Expr::sleeping;
  if (night() && quiet > 2u * kMin) return Expr::sleepy;
  if (upside) return Expr::confused;
  if (cold) return Expr::cold;
  if (hot) return Expr::hot;
  if (music_on) return ((now / 15000u) & 1u) ? Expr::vibing : Expr::music;
  if (speech_on) return Expr::listening;
  if (battery == 1) return ((now / 20000u) & 1u) ? Expr::low_battery : Expr::hungry;
  // Бег дольше 5 минут или без сил — выдохся.
  if (running) return now - run_since_ > 5u * kMin || mood_.energy < 0.3f ? Expr::exhausted : Expr::running;
  if (mood_.annoyance > 0.6f) return Expr::grumpy;
  if (mood_.annoyance > 0.35f) return Expr::annoyed;
  if (mood_.energy < 0.12f) return Expr::sleepy;
  // С ним возятся во втором часу ночи и позже — недосып (без дела он к этому времени спит).
  if (clock_in(60, 6 * 60)) return Expr::sleep_deprived;
  if (mood_.energy < 0.25f) return Expr::tired;
  // Игнор, но брелок двигается: зовёт хозяина (не чаще раза в 20 минут).
  if (moving && alone > 60u * kMin && now - last_calling_ > 20u * kMin) {
    last_calling_ = now;
    return Expr::calling;
  }
  if (moving) return Expr::walking;
  if (alone > 4u * 60u * kMin) return Expr::sulking;
  if (alone > 2u * 60u * kMin) return Expr::lonely;
  if (mood_.boredom > 0.7f) return flavor(Expr::bored);
  if (mood_.valence < -0.4f) return Expr::sad;
  if (mood_.valence < -0.15f) return Expr::worried;
  if (mood_.valence > 0.55f) return flavor(Expr::happy);
  if (temp_) return temp_->rest;  // у характера своё лицо в покое
  if (mood_.valence > 0.35f) return Expr::content;
  if (clock_in(21 * 60, 22 * 60)) return Expr::content;
  if (quiet > 5u * kMin) return Expr::calm;
  return Expr::neutral;
}

void Behavior::idle_action(uint32_t now) {
  const Expr cur = eng_->expression();
  if (cur == Expr::sleeping || cur == Expr::charging || cur == Expr::critical_battery || cur == Expr::music ||
      cur == Expr::vibing || cur == Expr::listening || cur == Expr::calling)
    return;
  float w[kIdleCount];
  const Mood& m = mood_;
  const bool happy = m.valence > 0.2f;
  // Свои занятия характера: с вероятностью own_idle — из его списка.
  if (temp_ && rng_.unit() < temp_->own_idle) {
    Expr opts[4];
    int n = 0;
    for (Expr x : temp_->idle)
      if (x != cur && usable(x)) opts[n++] = x;
    if (n) {
      const Expr x = opts[rng_.next() % (uint32_t)n];
      react(x, now, 3500, "сам по себе, по характеру");
      say(cue_for(x), now, 0.15f);
      return;
    }
  }
  // мемы — приправа, а не основа
  const float meme = 0.12f * pers_.playful * (temp_ ? temp_->meme : 1.0f);
  w[kLookAround] = 1.0f;
  w[kDoubleBlink] = 0.6f;
  w[kWink] = 0.25f * pers_.playful;
  w[kYawn] = (1.0f - m.energy) * 1.2f + (night() ? 0.6f : 0.0f);
  w[kSigh] = m.boredom + (m.valence < 0.0f ? 0.5f : 0.0f);
  w[kSneeze] = cold ? 0.4f : 0.04f;
  w[kDaydream] = happy ? 0.4f : 0.1f;
  w[kSing] = (happy && !night()) ? 0.35f * pers_.playful : 0.0f;
  w[kThink] = 0.4f;
  w[kCurious] = 0.5f;
  w[kShy] = 0.15f * pers_.shy;
  w[kGiggle] = m.valence > 0.3f ? 0.25f * pers_.playful : 0.0f;
  w[kSmug] = 0.1f;
  w[kZoned] = m.boredom * 0.5f;
  w[kShrug] = meme;
  w[kNpc] = meme * (0.5f + m.boredom);
  w[kPoker] = meme;
  w[kHmm] = meme;
  w[kLenny] = meme * 0.7f;
  w[kMeGusta] = happy ? meme : 0.0f;
  w[kSwabCat] = meme * 0.6f;
  w[kChad] = moving ? meme : meme * 0.4f;
  w[kSmugGlasses] = meme * 0.6f;
  w[kSoyWow] = happy ? meme * 0.5f : 0.0f;
  w[kTroll] = meme * 0.5f * pers_.playful;
  w[kMoney] = meme * 0.3f;
  w[kRobot] = w[kParty] = w[kDerp] = w[kCool] = w[kDevil] = 0.03f;
  w[kBoo] = night() ? 0.08f : 0.0f;
  // Циклоп — не чаще раза в 2 часа.
  w[kCyclops] = (happy && now - last_cyclops_ > 120u * kMin) ? 0.05f : 0.0f;
  // Эмоции из имплантов, которые иначе встречались бы только в сценах наград.
  w[kFocused] = night() ? 0.0f : 0.15f;
  w[kNervous] = m.annoyance * 0.3f + 0.05f * pers_.shy;
  w[kUwu] = happy ? meme * 0.6f : 0.0f;
  w[kDoomer] = (m.valence < 0.0f || m.boredom > 0.5f) ? meme * 0.8f : 0.0f;  // хандра от скуки
  w[kImDead] = happy ? meme * 0.4f : 0.0f;
  w[kNervousLaugh] = 0.08f * pers_.shy;
  // Недавно вспылил на тапы — неловко за это.
  const bool sorry = vented_ && now - vented_at_ < 3u * kMin;
  w[kGuilty] = sorry ? 0.4f : 0.0f;
  w[kApologetic] = sorry ? 0.2f + 0.3f * pers_.shy : 0.0f;
  w[kKiss] = m.affection > 0.4f ? 0.15f : 0.0f;
  float total = 0;
  for (int i = 0; i < kIdleCount; ++i) {
    if (kIdle[i].expr != Expr::Count && !usable(kIdle[i].expr)) w[i] = 0.0f;
    for (uint8_t r : recent_idle_)
      if (r == i) w[i] *= 0.15f;  // не повторяться
    total += w[i];
  }
  float x = rng_.unit() * total;
  int chosen = 0;
  for (int i = 0; i < kIdleCount; ++i) {
    x -= w[i];
    if (x <= 0.0f) {
      chosen = i;
      break;
    }
  }
  for (int i = 3; i > 0; --i) recent_idle_[i] = recent_idle_[i - 1];
  recent_idle_[0] = (uint8_t)chosen;
  if (chosen == kCyclops) last_cyclops_ = now;
  if (chosen == kGuilty || chosen == kApologetic) vented_ = false;
  const IdleAct& a = kIdle[chosen];
  if (a.expr != Expr::Count) {
    react(a.expr, now, a.ms, "сам по себе");
    say(cue_for(a.expr), now, 0.1f);
  } else {
    eng_->play_clip(a.clip, now);
    reason_ = "сам по себе";
  }
}

void Behavior::update(uint32_t now) {
  if (!eng_) return;
  decay(now);
  last_now_ = now;
  if (voice_) voice_->set_quiet(night());
  // Срок по умолчанию — горизонт сна; ветки ниже приближают его.
  next_ = now + kMaxSleepMs;
  auto wake = [&](uint32_t t) {
    if (time_left(now, t) < time_left(now, next_)) next_ = t;
  };
  if (manual_) {
    if (!time_reached(now, manual_until_)) {
      next_ = manual_until_;
      return;
    }
    manual_ = false;
  }
  if (!auto_) {
    // Автоматика выключена: сроки держим свежими, чтобы после включения всё пошло сразу.
    reacting_ = follow_ = false;
    next_eval_ = now;
    next_idle_ = now + 12000u;
    if (time_reached(now, next_surprise_)) schedule_surprise(now);
    return;
  }
  // Пока идёт сцена, персонаж не отвлекается; после неё — эмоция-развязка,
  // а если в очереди есть ещё сцены — следующая через постоянную паузу.
  if (eng_->scene() != Scene::None) {
    wake(eng_->scene_ends_ms());
    return;
  }
  if (after_scene_ != Expr::Count) {
    const Expr e = after_scene_;
    after_scene_ = Expr::Count;
    react(e, now, queue_n_ ? kBetweenScenesMs : 3500u, after_why_);
    if (after_fresh_) eng_->badge("NEW", now);  // только что открытая эмоция
    after_fresh_ = false;
    queue_at_ = now + kBetweenScenesMs;
    if (queue_n_) wake(queue_at_);
    return;
  }
  if (queue_n_ && time_reached(now, queue_at_) && run_queue(now)) return;
  if (reacting_) {
    if (!time_reached(now, react_until_)) {
      next_ = react_until_;
      if (queue_n_) wake(queue_at_);
      return;
    }
    reacting_ = false;
  }
  // Эмоцию сменил сам движок (цепочка «смех → радость»): даём её продолжению побыть на экране.
  const uint32_t started = eng_->expression_started_ms();
  const bool chain_ok = now - chose_ms_ < kChainMaxMs;
  if (started != seen_t0_) {
    seen_t0_ = started;
    follow_ = chain_ok;
    follow_until_ = started + kFollowMs;
  }
  const Expr curx = eng_->expression();
  const bool awake = curx != Expr::sleeping && curx != Expr::sleepy && curx != Expr::pocket_peek && !dark;
  // Праздники и пасхалки по дате — раз за дату, по времени суток — раз в сутки, когда не спит.
  if (awake && (date_eggs(now) || time_eggs(now))) return;
  // На зарядке, когда давно никто не трогал, — заставка: логотип ищет угол.
  if (awake && charging && !saver_done_ && now - last_event_ > 10u * kMin) {
    saver_done_ = true;
    queue_scene(Scene::screensaver, SceneParams{}, pick_unlocked({Expr::pog, Expr::excited, Expr::happy}), now,
                "заставка на зарядке попала в угол");
    return;
  }
  if (time_reached(now, next_surprise_)) {
    if (awake && !charging && !eco_) {
      surprise(now);
      return;
    }
    next_surprise_ = now;  // время пришло во сне или на зарядке — покажем, когда проснётся
  }
  // Не перебиваем цепочку эмоции (например, смех → радость), пока она не доиграла.
  // Небольшой запас — на случай, если движок обновится в этом цикле уже после поведения.
  const Expression& cur = expression(curx);
  if (cur.hold_ms && chain_ok && now - started <= cur.hold_ms + kHoldSlackMs) {
    const uint32_t end = started + cur.hold_ms + 1u;
    next_ = time_reached(now, end) ? now + 1u : end;
    return;
  }
  if (follow_) {
    if (chain_ok && !time_reached(now, follow_until_)) {
      next_ = follow_until_;
      return;
    }
    follow_ = false;
  }
  if (time_reached(now, next_eval_)) {
    const Expr want = allowed(choose_base(now));
    if (want != eng_->expression()) {
      show(want, now);
      reason_ = "настроение и обстановка";
    }
    base_ = want;
    next_eval_ = now + 4000u;
  }
  if (time_reached(now, next_idle_)) {
    idle_action(now);
    const bool sleepy = mood_.energy < 0.3f;
    const float activity = temp_ ? sanef(temp_->activity, 1.0f, 0.2f, 4.0f) : 1.0f;
    next_idle_ =
        now + (uint32_t)((float)((12000u + rng_.next() % 28000u) * (sleepy ? 2u : 1u) * (eco_ ? 2u : 1u)) / activity);
  }
  if (reacting_) wake(react_until_);
  if (queue_n_) wake(queue_at_);
  wake(next_eval_);
  wake(next_idle_);
  if (!time_reached(now, next_surprise_)) wake(next_surprise_);
}

// ---------------------------------------------------------------- рост, сцены, встречи

void Behavior::attach_progress(Progress* p) {
  progress_ = p;
  FaceEngine::ExprFilter f = [](Expr e, const void* self) { return ((const Behavior*)self)->allowed(e); };
  if (eng_) eng_->set_expr_filter(p ? f : nullptr, this);
}

Expr Behavior::allowed(Expr e) const {
  if (usable(e)) return e;
  struct Fb {
    Expr from, to;
  };
  static const Fb kFallback[] = {
      {Expr::furious, Expr::angry},      {Expr::rage, Expr::angry},
      {Expr::disgusted, Expr::annoyed},  {Expr::sobbing, Expr::crying},
      {Expr::exhausted, Expr::tired},    {Expr::knocked_out, Expr::dizzy},
      {Expr::terrified, Expr::scared},   {Expr::shocked, Expr::surprised},
      {Expr::amazed, Expr::surprised},   {Expr::startled, Expr::surprised},
      {Expr::excited, Expr::happy},      {Expr::starstruck, Expr::love},
      {Expr::adoring, Expr::love},       {Expr::kiss, Expr::love},
      {Expr::joy, Expr::happy},          {Expr::giggle, Expr::laugh},
      {Expr::tears_of_joy, Expr::laugh}, {Expr::vibing, Expr::music},
      {Expr::singing, Expr::music},      {Expr::sleep_deprived, Expr::tired},
      {Expr::sick, Expr::tired},         {Expr::queasy, Expr::dizzy},
      {Expr::smug, Expr::wink},          {Expr::playful, Expr::wink},
      {Expr::mischievous, Expr::wink},   {Expr::evil_laugh, Expr::laugh},
      {Expr::nervous, Expr::worried},    {Expr::embarrassed, Expr::shy},
      {Expr::flustered, Expr::shy},      {Expr::guilty, Expr::worried},
      {Expr::apologetic, Expr::worried}, {Expr::disappointed, Expr::sad},
      {Expr::hurt, Expr::sad},           {Expr::pleading, Expr::sad},
      {Expr::focused, Expr::thinking},   {Expr::idea, Expr::thinking},
      {Expr::skeptical, Expr::confused}, {Expr::suspicious, Expr::annoyed},
      {Expr::unamused, Expr::annoyed},   {Expr::bruh, Expr::annoyed},
      {Expr::alert, Expr::surprised},    {Expr::calling, Expr::lonely},
      {Expr::afraid_dark, Expr::scared}, {Expr::processing, Expr::thinking},
      {Expr::daydreaming, Expr::calm},   {Expr::party, Expr::happy},
      {Expr::proud, Expr::happy},        {Expr::spooky, Expr::scared},
      {Expr::boo, Expr::surprised},
  };
  Expr to = Expr::Count;
  for (const Fb& f : kFallback)
    if (f.from == e) to = f.to;
  if (to == Expr::Count) {
    switch (expression(e).cat) {
      case Category::Positive:
      case Category::Social:
      case Category::Fun: to = Expr::happy; break;
      case Category::Negative: to = Expr::sad; break;
      case Category::Surprise: to = Expr::surprised; break;
      case Category::Cognitive: to = Expr::thinking; break;
      default: to = Expr::neutral; break;
    }
  }
  if (usable(to)) return to;
  return temp_ ? temp_->rest : Expr::neutral;
}

Expr Behavior::pick_unlocked(const Expr* opts, int n) {
  for (int i = 0; i < n; ++i)
    if (usable(opts[i])) return opts[i];
  return allowed(opts[0]);
}

void Behavior::queue_scene(Scene s, const SceneParams& p, Expr after, uint32_t now, const char* why, bool fresh) {
  if (queue_n_ < (int)(sizeof(queue_) / sizeof(queue_[0]))) queue_[queue_n_++] = Queued{s, p, after, why, fresh};
  manual_ = false;  // причину сцена покажет, когда начнётся (run_queue)
  next_ = now;
  if (eng_->scene() == Scene::None && after_scene_ == Expr::Count && time_reached(now, queue_at_)) run_queue(now);
}

void Behavior::queue_shared(Scene s, const SceneParams& p, Expr after, uint32_t now, const char* why, bool fresh) {
  const Scene playing = eng_->scene();
  if (playing != Scene::None && !(cur_shared_ && playing == cur_scene_)) {
    // Своя сцена уступает: доиграет после общих (если её запускала очередь).
    if (playing == cur_scene_ && queue_n_ < (int)(sizeof(queue_) / sizeof(queue_[0])))
      queue_[queue_n_++] = Queued{playing, eng_->scene_params(), after_scene_, after_why_, after_fresh_};
    eng_->stop_scene(now);
    after_scene_ = Expr::Count;
    after_fresh_ = false;
    cur_scene_ = Scene::None;
    queue_at_ = now;
  } else if (playing == Scene::None && !(cur_shared_ && after_scene_ != Expr::Count)) {
    // Между своими сценами паузу не ждём: общая начинается сейчас.
    after_scene_ = Expr::Count;
    queue_at_ = now;
  }
  constexpr int kMax = (int)(sizeof(queue_) / sizeof(queue_[0]));
  if (queue_n_ == kMax) --queue_n_;  // очередь полна: уходит последняя своя
  for (int i = queue_n_; i > shared_n_; --i) queue_[i] = queue_[i - 1];
  queue_[shared_n_] = Queued{s, p, after, why, fresh};
  ++shared_n_;
  ++queue_n_;
  manual_ = false;
  next_ = now;
  if (eng_->scene() == Scene::None && after_scene_ == Expr::Count && time_reached(now, queue_at_)) run_queue(now);
}

bool Behavior::run_queue(uint32_t now) {
  if (!queue_n_ || eng_->scene() != Scene::None) return false;
  const Queued q = queue_[0];
  for (int i = 1; i < queue_n_; ++i) queue_[i - 1] = queue_[i];
  --queue_n_;
  cur_shared_ = shared_n_ > 0;
  if (shared_n_) --shared_n_;
  cur_scene_ = q.scene;
  eng_->play_scene(q.scene, now, q.params);
  if (voice_) {
    if (q.scene == Scene::chat && chat_both_ && chat_peer_)
      voice_->chat(q.params.key, q.params.value != 0, now, &temperament(archetype_from_hash(chat_peer_)));
    else
      voice_->scene(q.scene, q.params, now);
  }
  after_scene_ = q.after;
  after_fresh_ = q.fresh;
  after_why_ = q.why;
  reason_ = q.why;
  next_ = eng_->scene_ends_ms();
  return true;
}

void Behavior::grant(int installed, uint32_t now) {
  if (installed < 0 || !progress_) return;
  // Показываем апгрейд и сразу хвастаемся одной из новых эмоций.
  SceneParams p;
  p.set_text(implant(installed).tag);
  Expr show = Expr::happy;
  int seen = 0;
  for (int i = 0; i < kExprCount; ++i) {
    if (implant_of((Expr)i) != installed) continue;
    if (rng_.next() % (uint32_t)(++seen) == 0) show = (Expr)i;
  }
  queue_scene(Scene::upgrade, p, show, now, "новый имплант", true);
}

void Behavior::reward(Ach a, uint32_t now) {
  SceneParams p;
  p.set_text(achievement(a).tag);
  queue_scene(Scene::achievement, p, pick_unlocked({Expr::proud, Expr::happy}), now, "достижение");
  grant(progress_->add_xp(achievement(a).xp), now);
}

void Behavior::achieve(uint32_t now) {
  if (!progress_) return;
  for (int i = 0; i < 2; ++i) {  // не больше двух за раз: остальные — на следующем событии
    const Ach a = progress_->next_achievement();
    if (a == Ach::Count) return;
    reward(a, now);
  }
}

void Behavior::earn(Ach a, uint32_t now) {
  if (progress_ && progress_->earn(a)) reward(a, now);
}

void Behavior::set_eco(int level) {
  if (!eng_ || level == eco_) return;
  if (!eco_) eco_frame_ms_ = eng_->options().frame_ms;
  eco_ = level;
  const uint16_t slow = level == 1 ? 50 : 80;
  eng_->options().frame_ms = level == 0 ? eco_frame_ms_ : (eco_frame_ms_ > slow ? eco_frame_ms_ : slow);
}

void Behavior::power_off(uint32_t now, bool low_battery) {
  off_ = true;
  SceneParams p;
  p.value = low_battery ? 1 : 0;
  queue_scene(Scene::shutdown, p, Expr::sleeping, now, low_battery ? "выключается: ячейка пуста" : "выключается");
}

void Behavior::banner(const char* big, const char* sub, BannerIcon icon, Expr after, uint32_t now, const char* why) {
  SceneParams p;
  p.set_text(big);
  p.set_sub(sub);
  p.value = (int)icon;
  queue_scene(Scene::banner, p, after, now, why);
}

bool Behavior::date_eggs(uint32_t now) {
  if (!year_) return false;
  const uint32_t key = (uint32_t)year_ * 400u + month_ * 32u + day_;
  if (holiday_done_ == key) return false;
  holiday_done_ = key;
  const int m = month_, d = day_;
  // День рождения хозяина важнее любого праздника в тот же день.
  if (bday_m_ && m == bday_m_ && d == bday_d_) {
    queue_scene(Scene::birthday, SceneParams{}, pick_unlocked({Expr::party, Expr::love}), now, "день рождения хозяина");
    return true;
  }
  if ((m == 12 && d == 31) || (m == 1 && d == 1)) {
    char y[6];
    const uint16_t yy = m == 12 ? (uint16_t)(year_ + 1) : year_;
    y[0] = (char)('0' + yy / 1000 % 10), y[1] = (char)('0' + yy / 100 % 10), y[2] = (char)('0' + yy / 10 % 10);
    y[3] = (char)('0' + yy % 10), y[4] = 0;
    SceneParams p;
    p.set_text(y);
    queue_scene(Scene::new_year, p, pick_unlocked({Expr::party, Expr::happy}), now, "Новый год");
    return true;
  }
  if (m == 10 && d == 31) {
    queue_scene(Scene::halloween, SceneParams{}, pick_unlocked({Expr::spooky, Expr::boo, Expr::scared}), now,
                "Хэллоуин");
    return true;
  }
  if (d == 13 && weekday(year_, m, d) == 5) {
    banner("FRIDAY 13", "BE CAREFUL", BannerIcon::Skull, pick_unlocked({Expr::spooky, Expr::scared}), now,
           "пятница, 13-е");
    return true;
  }
  if (m == 4 && d == 1) {
    // Розыгрыш: «упал» синим экраном — и «попался!».
    queue_scene(Scene::bsod, SceneParams{}, pick_unlocked({Expr::innocent, Expr::neutral}), now, "1 апреля");
    banner("GOTCHA!", "APRIL 1", BannerIcon::Sparkles, pick_unlocked({Expr::troll_grin, Expr::laugh}), now,
           "1 апреля: розыгрыш");
    return true;
  }
  struct Egg {
    uint8_t m, d;
    const char *big, *sub;
    BannerIcon icon;
    Expr after[2];
    const char* why;
  };
  static const Egg kEggs[] = {
      {2, 14, "LOVE DAY", "<3", BannerIcon::Heart, {Expr::love, Expr::adoring}, "день всех влюблённых"},
      {2, 29, "FEB 29", "BONUS DAY", BannerIcon::Sparkles, {Expr::party, Expr::happy}, "29 февраля: лишний день"},
      {3, 14, "PI DAY", "3.14159265", BannerIcon::Pi, {Expr::idea, Expr::thinking}, "день числа пи"},
      {4, 12, "POEKHALI!", "APRIL 12", BannerIcon::Rocket, {Expr::excited, Expr::happy}, "день космонавтики"},
      {5, 4, "MAY THE 4TH", "BE WITH YOU", BannerIcon::Star, {Expr::cool, Expr::smug}, "4 мая"},
      {5, 25, "DONT PANIC", "TOWEL DAY 42", BannerIcon::Sparkles, {Expr::calm, Expr::cool}, "день полотенца"},
  };
  for (const Egg& g : kEggs) {
    if (g.m != m || g.d != d) continue;
    banner(g.big, g.sub, g.icon, pick_unlocked(g.after), now, g.why);
    return true;
  }
  if (day_of_year(year_, m, d) == 256) {
    banner("HELLO WORLD", "DAY 256", BannerIcon::Sparkles, pick_unlocked({Expr::robot, Expr::cool}), now,
           "день программиста");
    return true;
  }
  return false;
}

bool Behavior::time_eggs(uint32_t now) {
  if (!clock_known_) return false;  // без часов «13:37» и «3 AM» показывались бы когда попало
  if (clock_min_ < last_clock_) time_eggs_done_ = 0;  // часы прошли полночь
  last_clock_ = clock_min_;
  auto once = [&](int bit) {
    if (time_eggs_done_ & (1u << bit)) return false;
    time_eggs_done_ = (uint8_t)(time_eggs_done_ | 1u << bit);
    return true;
  };
  switch (clock_min_) {
    case 13 * 60 + 37:
      if (!once(0)) break;
      banner("1337", "H4X0R MODE", BannerIcon::Sparkles, pick_unlocked({Expr::smug_glasses, Expr::cool}), now,
             "13:37");
      earn(Ach::leet, now);
      return true;
    case 11 * 60 + 11:
      if (!once(1)) break;
      banner("11:11", "MAKE A WISH", BannerIcon::Star, pick_unlocked({Expr::daydreaming, Expr::bliss}), now, "11:11");
      return true;
    case 12 * 60 + 34:
      if (!once(2)) break;
      banner("12:34", "1 2 3 4!", BannerIcon::Sparkles, pick_unlocked({Expr::happy}), now, "12:34");
      return true;
    case 3 * 60:
      if (!once(3)) break;
      banner("3 AM", "GO TO SLEEP", BannerIcon::Skull, pick_unlocked({Expr::sleepy, Expr::tired}), now,
             "три часа ночи, а он не спит");
      return true;
    case 4 * 60 + 4: {
      if (!once(4)) break;
      SceneParams p;
      p.set_sub("TIME NOT FOUND");
      queue_scene(Scene::error404, p, pick_unlocked({Expr::shrug, Expr::confused}), now, "04:04");
      return true;
    }
    default: break;
  }
  return false;
}

bool Behavior::knock(uint32_t now) {
  // Ритм «та, та-та, та, та»: четверть, две восьмые, две четверти. Восьмая — 120–450 мс,
  // каждый промежуток с допуском ±35 %.
  for (int i = 0; i < 4; ++i) knocks_[i] = knocks_[i + 1];
  knocks_[4] = now;
  if (!knocks_[0]) return false;
  uint32_t d[4];
  for (int i = 0; i < 4; ++i) d[i] = knocks_[i + 1] - knocks_[i];
  const uint32_t e = (d[1] + d[2]) / 2;
  if (e < 120u || e > 450u) return false;
  auto near = [](uint32_t a, uint32_t b) { return a * 100u >= b * 65u && a * 100u <= b * 135u; };
  if (!near(d[1], d[2]) || !near(d[0], 2u * e) || !near(d[3], 2u * e)) return false;
  for (uint32_t& k : knocks_) k = 0;
  return true;
}

void Behavior::new_day(uint32_t now) {
  time_eggs_done_ = 0;  // без часов полночь не видна: новые сутки открывают пасхалки заново
  if (!progress_) return;
  grant(progress_->new_day(), now);
  achieve(now);
}

void Behavior::add_xp(uint32_t xp, uint32_t now) {
  if (!progress_) return;
  grant(progress_->add_xp(xp), now);
  achieve(now);
}

void Behavior::add_steps(uint32_t steps, uint32_t now) {
  if (!progress_) return;
  grant(progress_->add_steps(steps), now);
  achieve(now);
}

Expr Behavior::greet_after(const Temperament* other, bool is_new) {
  const Temperament* me = temp_;
  if (!me || !other) return is_new ? pick_unlocked({Expr::excited, Expr::hello}) : pick_unlocked({Expr::hello});
  if (me == other) return pick_unlocked({Expr::pog, Expr::excited, Expr::happy});  // близнецы
  const Archetype a = (Archetype)(me - &temperament((Archetype)0));
  const Archetype b = (Archetype)(other - &temperament((Archetype)0));
  switch (a) {
    case Archetype::grumpy:
      if (b == Archetype::playful || b == Archetype::dramatic) return pick_unlocked({Expr::unamused, Expr::annoyed});
      if (b == Archetype::sweet) return pick_unlocked({Expr::flustered, Expr::smug});  // растаял, но не признаётся
      return pick_unlocked({Expr::not_bad, Expr::skeptical});
    case Archetype::shy: return pick_unlocked({Expr::flustered, Expr::embarrassed, Expr::shy});
    case Archetype::sweet: return pick_unlocked({Expr::love, Expr::adoring});
    case Archetype::trickster:
      if (b == Archetype::trickster || b == Archetype::glitchy) return pick_unlocked({Expr::evil_laugh, Expr::devilish});
      return pick_unlocked({Expr::devilish, Expr::smug});
    case Archetype::curious: return pick_unlocked({Expr::curious, Expr::idea, Expr::excited});
    case Archetype::dramatic: return pick_unlocked({Expr::tears_of_joy, Expr::touched, Expr::love});
    case Archetype::glitchy: return pick_unlocked({Expr::processing, Expr::owo});
    case Archetype::chill: return pick_unlocked({Expr::cool, Expr::not_bad});
    case Archetype::sleepy: return pick_unlocked({Expr::waking, Expr::content});
    case Archetype::playful:
      if (b == Archetype::grumpy) return pick_unlocked({Expr::troll_grin, Expr::playful});  // подразнить ворчуна
      return pick_unlocked({Expr::party, Expr::excited, Expr::happy});
    default: return pick_unlocked({Expr::hello});
  }
}

void Behavior::meet(const char* code, bool is_new, uint32_t now, uint32_t peer_hash, uint8_t peer_level,
                    uint32_t nonce) {
  last_event_ = last_touch_ = now;
  if (eng_) eng_->note_activity(now);
  mood_.valence = clamp11(mood_.valence + 0.2f);
  mood_.affection = clamp01(mood_.affection + 0.05f);
  const Temperament* other = peer_hash ? &temperament(archetype_from_hash(peer_hash)) : nullptr;
  int before = -1, level = 0;
  bool first_today = true;
  if (friends_ && peer_hash) {
    level = friends_->meet(peer_hash, progress_ ? progress_->days : 0, &before, &first_today);
    is_new = before < 0;
  }
  SceneParams p;
  p.set_text(code);
  p.value = is_new ? 0 : 1;
  if (other) p.set_sub(other == temp_ ? "TWIN!" : other->tag);
  // Легендарный брелок рядом — звёзды в глазах, кто бы ни встретил.
  const bool legend = peer_hash && identity_from_hash(peer_hash).rarity == Rarity::Legendary;
  queue_shared(Scene::greet, p,
               legend ? pick_unlocked({Expr::starstruck, Expr::amazed, Expr::excited}) : greet_after(other, is_new), now,
               other == temp_ && other ? "встретил такого же" : is_new ? "новый знакомый брелок" : "встретил знакомого");
  // Ворчун при виде брелока с золотым глазом, глюк с глюком — данные текут рекой.
  if (other && temp_ && other == temp_ && temp_->glitches)
    queue_shared(Scene::data_rain, SceneParams{}, pick_unlocked({Expr::hypnotized, Expr::zoned_out}), now,
                 "два глюка синхронизировались");
  // Старые знакомые иногда шепчутся. Решение — из общего ключа, у соседа оно то же, и шёпот
  // стоит в очереди сразу за приветствием: дальше у каждого свои сцены.
  if (!is_new && peer_hash && nonce && chat_key(self_hash_, peer_hash, nonce) % 4u == 0) {
    chat(code, peer_hash, nonce, now);
    reason_ = "старые знакомые шепчутся";
  }
  friendship(code, peer_hash, is_new, before, level, first_today, peer_level, now);
  achieve(now);
}

void Behavior::friendship(const char* code, uint32_t peer_hash, bool is_new, int before, int level,
                          bool first_today, uint8_t peer_level, uint32_t now) {
  if (friends_ && before >= 0 && level > before) {
    banner(friend_level_tag(level), code, BannerIcon::Heart, pick_unlocked({Expr::love, Expr::happy}), now,
           "дружба крепнет");
    if (progress_) grant(progress_->add_xp(15u * (uint32_t)level), now);
    if (level >= 3) earn(Ach::best_friend, now);
  }
  // Старший брелок (имплант на два выше) раз в день дарит эмоцию из следующего импланта.
  if (progress_ && first_today && peer_level < implant_count() && peer_level >= progress_->implant + 2) {
    const Expr gift = progress_->gift(rng_);
    if (gift != Expr::Count) {
      SceneParams g;
      g.set_text("GIFT");
      g.set_sub(code);
      g.value = (int)BannerIcon::Gift;
      queue_scene(Scene::banner, g, gift, now, "подарок от старшего брелока", true);
    }
  }
  const Temperament* other = peer_hash ? &temperament(archetype_from_hash(peer_hash)) : nullptr;
  if (other && other == temp_) earn(Ach::twin, now);
  if (peer_hash && identity_from_hash(peer_hash).rarity == Rarity::Legendary) earn(Ach::legend, now);
  if (progress_ && (is_new || first_today)) grant(progress_->met_friend(is_new), now);  // старый знакомый — раз в день
}

void Behavior::duel(const char* code, bool won, uint32_t now, uint32_t peer_hash, uint32_t nonce,
                    uint8_t peer_level) {
  last_event_ = now;
  if (eng_) eng_->note_activity(now);
  SceneParams p;
  p.set_text(code);
  p.value = won ? 1 : 0;
  // Из пула, а не первую открытую: иначе звёзды в глазах и рыдания не выпадали бы никогда.
  const Option win[] = {{Expr::proud, 3}, {Expr::smug, 2}, {Expr::starstruck, 1.5f}, {Expr::happy, 1}};
  const Option lose[] = {{Expr::sulking, 3}, {Expr::grumpy, 2}, {Expr::sobbing, 1}};
  Expr after = won ? pick(win) : pick(lose);
  const char* why = won ? "выиграл дуэль" : "проиграл дуэль";
  bool fresh = false;
  if (won && progress_) {
    const Expr stolen = progress_->duel_prize(rng_);
    if (stolen != Expr::Count) {
      after = stolen;
      fresh = true;
      why = "выиграл в дуэли новую эмоцию";
    }
  }
  mood_.valence = clamp11(mood_.valence + (won ? 0.25f : -0.15f));
  mood_.annoyance = clamp01(mood_.annoyance + (won ? 0.0f : 0.15f));
  queue_shared(Scene::duel, p, after, now, why, fresh);  // выигранная эмоция — с «NEW»
  // Сразу за дуэлью — разговор шифром: у соседа очередь та же, и он начнётся одновременно.
  if (peer_hash) chat(code, peer_hash, nonce, now);
  if (progress_) grant(progress_->duel(won), now);
  // Дуэль — тоже встреча: день дружбы засчитывается, новый соперник становится знакомым
  // (без своей сцены приветствия — её место заняли дуэль и разговор).
  // Рост дружбы, подарок старшего, близнец, легенда и опыт за знакомство — как после встречи:
  // первая встреча дня с готовым к дуэлям соседом и есть дуэль, а короткая встреча — только она.
  int before = -1, level = 0;
  bool first_today = false;
  if (friends_ && peer_hash) level = friends_->meet(peer_hash, progress_ ? progress_->days : 0, &before, &first_today);
  if (peer_hash) friendship(code, peer_hash, friends_ && before < 0, before, level, first_today, peer_level, now);
  achieve(now);
}

void Behavior::chat(const char* code, uint32_t peer_hash, uint32_t nonce, uint32_t now) {
  SceneParams p;
  p.set_text(code);
  p.key = chat_key(self_hash_, peer_hash, nonce);
  p.value = chat_first(self_hash_, peer_hash) ? 1 : 0;
  chat_peer_ = peer_hash;
  // После разговора — невинное лицо: «мы ничего не обсуждали».
  queue_shared(Scene::chat, p, pick_unlocked({Expr::innocent, Expr::smug, Expr::mischievous}), now,
               "секретный разговор с соседом");
  if (progress_) progress_->chatted();
}

void Behavior::set_date(uint16_t year, uint8_t month, uint8_t day) {
  year_ = year;
  month_ = month;
  day_ = day;
  next_ = last_now_;  // праздник проверится на ближайшем проходе, а не через минуту
}

void Behavior::set_surprise_interval(uint32_t min_ms, uint32_t max_ms, uint32_t now) {
  surprise_max_ = max_ms < kMaxSurpriseMs ? max_ms : kMaxSurpriseMs;
  surprise_min_ = min_ms < surprise_max_ ? min_ms : surprise_max_;
  schedule_surprise(now);
}

void Behavior::surprise(uint32_t now) {
  schedule_surprise(now);
  const bool cyber = eng_->options().theme == Theme::Cyber;
  struct Opt {
    Scene s;
    float w;
  };
  const bool retro = eng_->options().theme == Theme::Retro;
  Opt opts[] = {{Scene::hide, cyber ? 1.0f : 3.0f},
                {Scene::reboot, retro ? 2.5f : 1.5f},
                {Scene::error404, cyber ? 2.0f : 1.0f},
                {Scene::hack, cyber ? 3.0f : 1.0f},
                {Scene::data_rain, cyber ? 2.0f : 0.7f},
                {Scene::bsod, retro ? 1.5f : 0.6f},
                {Scene::visor, cyber ? 2.0f : 0.6f}};
  float total = 0;
  for (Opt& o : opts) {
    if (temp_ && o.s == temp_->fav) o.w *= 3.0f;  // любимый сюрприз характера
    total += o.w;
  }
  float x = rng_.unit() * total;
  Scene s = Scene::hide;
  for (const Opt& o : opts) {
    x -= o.w;
    if (x <= 0) {
      s = o.s;
      break;
    }
  }
  Expr after = Expr::neutral;
  switch (s) {
    case Scene::hack: after = pick_unlocked({Expr::troll_grin, Expr::smug, Expr::wink}); break;
    case Scene::data_rain: after = pick_unlocked({Expr::hypnotized, Expr::zoned_out, Expr::dizzy}); break;
    case Scene::error404: after = pick_unlocked({Expr::shrug, Expr::confused}); break;
    case Scene::reboot: after = Expr::waking; break;
    case Scene::hide: after = pick_unlocked({Expr::playful, Expr::giggle, Expr::laugh}); break;
    case Scene::bsod: after = pick_unlocked({Expr::dizzy, Expr::shrug}); break;
    case Scene::visor: after = pick_unlocked({Expr::cool, Expr::robot}); break;
    default: break;
  }
  queue_scene(s, SceneParams{}, after, now, "сюрприз");
}

}  // namespace face
