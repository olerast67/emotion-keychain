// Поведение: события датчиков → эмоции. Пулы вариантов реакций, лестницы
// интенсивности, настроение, суточный ритм и фоновые занятия.
#include "face/behavior.h"

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
  for (uint32_t& t : falls_) t = 0;
  for (uint8_t& r : recent_idle_) r = 255;
  for (Expr& r : recent_react_) r = Expr::Count;
  loud_at_ = 0;
  last_calling_ = last_cyclops_ = now;
  queue_n_ = 0;
  after_scene_ = Expr::Count;
  schedule_surprise(now);
  holiday_done_ = 0;
  dark = charging = upside = music_on = speech_on = cold = hot = moving = running = false;
  battery = 2;
  reason_ = "";
  next_ = now;
  if (progress_) attach_progress(progress_);
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
    if (progress_ && !progress_->is_unlocked(opts[i].expr)) w[i] = 0.0f;
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
      if (progress_) grant(progress_->touch(), now);
      const int add = e == Event::double_tap ? 2 : 1;
      for (int a = 0; a < add; ++a) {
        for (int i = 7; i > 0; --i) tap_times_[i] = tap_times_[i - 1];
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
        mood_.annoyance = clamp01(mood_.annoyance + 0.3f);
        const Option o[] = {{Expr::furious, 3}, {Expr::rage, 2}, {Expr::table_flip, 1}};
        react(pick(o), now, 4000, "тыкают без остановки");
      } else if (n >= 6) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.2f);
        const Option o[] = {{Expr::angry, 3}, {Expr::twitchy, 1.5f}, {Expr::disapproval, 1}};
        react(pick(o), now, 3500, "слишком много тапов");
      } else if (n >= 4) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.12f);
        const Option o[] = {{Expr::annoyed, 3}, {Expr::unamused, 2}, {Expr::bruh, 1.5f}, {Expr::skeptical, 1}};
        react(pick(o), now, 2600, "надоедают тапами", Clip::headshake);
      } else if (n == 3) {
        mood_.valence = clamp11(mood_.valence + 0.1f);
        const Option o[] = {{Expr::playful, 2 * pers_.playful},
                            {Expr::giggle, 2},
                            {Expr::tears_of_joy, good ? 1.0f : 0.2f},
                            {Expr::troll_grin, 0.3f}};
        react(pick(o), now, 2600, "тройной тап");
      } else if (n == 2) {
        mood_.valence = clamp11(mood_.valence + 0.15f);
        if (mood_.annoyance > 0.5f) {
          react(Expr::unamused, now, 2400, "двойной тап, но он не в духе");
        } else {
          const Option o[] = {{Expr::laugh, 3},  {Expr::happy, 2},   {Expr::playful, 1},
                              {Expr::giggle, 1}, {Expr::wink, 0.7f}, {Expr::tears_of_joy, good ? 0.6f : 0.0f}};
          react(pick(o), now, 3000, "двойной тап");
        }
      } else if (mood_.annoyance > 0.5f) {
        const Option o[] = {{Expr::annoyed, 2}, {Expr::disapproval, 1}, {Expr::unamused, 1}};
        react(pick(o), now, 2000, "тап, но он не в духе");
      } else if (since_touch > 10u * kMin) {
        react(Expr::surprised, now, 1800, "тап после долгой паузы", Clip::startle);
      } else {
        // Самое частое событие: пул из нескольких вариантов, чтобы не надоедать.
        mood_.valence = clamp11(mood_.valence + 0.05f);
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
        if (falls_[2] && now - falls_[2] < 10u * kMin)
          react(Expr::game_over, now, 3500, "третье падение за 10 минут");
        else if (falls_[1] && now - falls_[1] < 10u * kMin)
          react(Expr::crying, now, 4000, "опять уронили");
        else
          react(Expr::knocked_out, now, 3500, "упал и ударился");
      } else {
        const Option o[] = {{Expr::hurt, 3}, {Expr::cringe, 1}};
        react(pick(o), now, 1800, "стукнули");
      }
      break;
    }
    case Event::pick_up:
      last_touch_ = now;
      if (asleep) {
        react(Expr::waking, now, 2400, "взяли в руки сонного", Clip::yawn);
      } else if (since_touch > 2u * 60u * kMin) {
        react(Expr::touched, now, 3000, "взяли после долгой разлуки");
      } else if (since_touch > 10u * kMin) {
        react(Expr::hello, now, 2500, "взяли в руки после перерыва", Clip::hop);
      } else {
        const Option o[] = {
            {Expr::happy, good ? 3.0f : 1.0f}, {Expr::curious, 2}, {Expr::alert, 1}, {Expr::suspicious, 0.4f}};
        react(pick(o), now, 2000, "взяли в руки");
      }
      break;
    case Event::walk: {
      const bool first = !moving;
      moving = true;
      running = false;
      mood_.arousal = clamp01(mood_.arousal + 0.1f);
      if (first && mood_.energy > 0.5f && rng_.chance(0.5f))
        react(Expr::excited, now, 3000, "пошли гулять");
      else
        react(Expr::walking, now, 2500, "идём");
      break;
    }
    case Event::run: {
      moving = running = true;
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
        const Option o[] = {{Expr::annoyed, 2}, {Expr::disapproval, 1}, {Expr::unamused, 1}};
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
    case Event::charge_start:
      charging = true;
      if (battery == 0)
        react(Expr::relieved, now, 2200, "зарядку подключили в последний момент");
      else if (battery == 1)
        react(Expr::grateful, now, 2200, "голодного покормили");
      else
        react(rng_.chance(0.15f) ? Expr::money : Expr::happy, now, 2200, "подключили зарядку");
      break;
    case Event::charge_full: {
      battery = 2;
      const Option o[] = {{Expr::charged, 3}, {Expr::pog, 1}, {Expr::money, 0.5f}};
      react(pick(o), now, 3000, "зарядился");
      break;
    }
    case Event::charge_stop:
      charging = false;
      react(battery < 2 ? Expr::disappointed : Expr::hello, now, 2200, "отключили зарядку");
      break;
    case Event::battery_low: {
      battery = 1;
      const Option o[] = {{Expr::hungry, 2}, {Expr::pleading, 1}, {Expr::low_battery, 1}};
      react(pick(o), now, 4000, "мало заряда");
      break;
    }
    case Event::battery_critical:
      battery = 0;
      react(Expr::critical_battery, now, 4000, "заряд почти закончился");
      break;
    case Event::button: {
      last_touch_ = now;
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
}

Expr Behavior::choose_base(uint32_t now) {
  const uint32_t quiet = now - last_event_;
  const uint32_t alone = now - last_touch_;
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
  if (running) return Expr::running;
  if (battery == 1) return ((now / 20000u) & 1u) ? Expr::low_battery : Expr::hungry;
  if (mood_.annoyance > 0.6f) return Expr::grumpy;
  if (mood_.annoyance > 0.35f) return Expr::annoyed;
  if (mood_.energy < 0.12f) return Expr::sleepy;
  if (mood_.energy < 0.25f) return clock_min_ >= 60 && clock_min_ < 6 * 60 ? Expr::sleep_deprived : Expr::tired;
  // Игнор, но брелок двигается: зовёт хозяина (не чаще раза в 20 минут).
  if (moving && alone > 60u * kMin && now - last_calling_ > 20u * kMin) {
    last_calling_ = now;
    return Expr::calling;
  }
  if (moving) return Expr::walking;
  if (alone > 4u * 60u * kMin) return Expr::sulking;
  if (alone > 2u * 60u * kMin) return Expr::lonely;
  if (mood_.boredom > 0.7f) return Expr::bored;
  if (mood_.valence < -0.4f) return Expr::sad;
  if (mood_.valence < -0.15f) return Expr::worried;
  if (mood_.valence > 0.55f) return Expr::happy;
  if (mood_.valence > 0.35f) return Expr::content;
  if (clock_min_ >= 21 * 60 && clock_min_ < 22 * 60) return Expr::content;
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
  const float meme = 0.12f * pers_.playful;  // мемы — приправа, а не основа
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
  float total = 0;
  for (int i = 0; i < kIdleCount; ++i) {
    if (progress_ && kIdle[i].expr != Expr::Count && !progress_->is_unlocked(kIdle[i].expr)) w[i] = 0.0f;
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
  const IdleAct& a = kIdle[chosen];
  if (a.expr != Expr::Count) {
    react(a.expr, now, a.ms, "сам по себе");
  } else {
    eng_->play_clip(a.clip, now);
    reason_ = "сам по себе";
  }
}

void Behavior::update(uint32_t now) {
  if (!eng_) return;
  decay(now);
  last_now_ = now;
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
  // Пока идёт сцена, персонаж не отвлекается; после неё — эмоция-развязка.
  if (eng_->scene() != Scene::None) {
    wake(eng_->scene_ends_ms());
    return;
  }
  if (run_queue(now)) return;
  if (after_scene_ != Expr::Count) {
    const Expr e = after_scene_;
    after_scene_ = Expr::Count;
    react(e, now, 3500, after_why_);
    return;
  }
  if (reacting_) {
    if (!time_reached(now, react_until_)) {
      next_ = react_until_;
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
  // Праздник показываем один раз за дату, когда персонаж бодрствует.
  if (awake && year_ && holiday_done_ != (uint32_t)year_ * 400u + month_ * 32u + day_) {
    holiday_done_ = (uint32_t)year_ * 400u + month_ * 32u + day_;
    SceneParams p;
    if ((month_ == 12 && day_ == 31) || (month_ == 1 && day_ == 1)) {
      char y[6];
      const uint16_t yy = month_ == 12 ? (uint16_t)(year_ + 1) : year_;
      y[0] = (char)('0' + yy / 1000 % 10), y[1] = (char)('0' + yy / 100 % 10), y[2] = (char)('0' + yy / 10 % 10);
      y[3] = (char)('0' + yy % 10), y[4] = 0;
      p.set_text(y);
      queue_scene(Scene::new_year, p, pick_unlocked({Expr::party, Expr::happy}), now, "Новый год");
      return;
    }
    if (bday_m_ && month_ == bday_m_ && day_ == bday_d_) {
      queue_scene(Scene::birthday, p, pick_unlocked({Expr::party, Expr::love}), now, "день рождения хозяина");
      return;
    }
    if (month_ == 10 && day_ == 31) {
      queue_scene(Scene::halloween, p, pick_unlocked({Expr::spooky, Expr::boo, Expr::scared}), now, "Хэллоуин");
      return;
    }
  }
  if (time_reached(now, next_surprise_)) {
    if (awake && !charging) {
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
    next_idle_ = now + (12000u + rng_.next() % 28000u) * (sleepy ? 2u : 1u);
  }
  if (reacting_) wake(react_until_);
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
  if (!progress_ || (int)e >= kExprCount || progress_->is_unlocked(e)) return e;
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
  return progress_->is_unlocked(to) ? to : Expr::neutral;
}

Expr Behavior::pick_unlocked(const Expr* opts, int n) {
  for (int i = 0; i < n; ++i)
    if (!progress_ || progress_->is_unlocked(opts[i])) return opts[i];
  return allowed(opts[0]);
}

void Behavior::queue_scene(Scene s, const SceneParams& p, Expr after, uint32_t now, const char* why) {
  if (queue_n_ < 4) queue_[queue_n_++] = Queued{s, p, after, why};
  manual_ = false;
  reason_ = why;
  next_ = now;
  if (eng_->scene() == Scene::None && after_scene_ == Expr::Count) run_queue(now);
}

bool Behavior::run_queue(uint32_t now) {
  if (!queue_n_ || eng_->scene() != Scene::None) return false;
  const Queued q = queue_[0];
  for (int i = 1; i < queue_n_; ++i) queue_[i - 1] = queue_[i];
  --queue_n_;
  eng_->play_scene(q.scene, now, q.params);
  after_scene_ = q.after;
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
  queue_scene(Scene::upgrade, p, show, now, "новый имплант");
}

void Behavior::new_day(uint32_t now) {
  if (progress_) grant(progress_->new_day(), now);
}

void Behavior::add_xp(uint32_t xp, uint32_t now) {
  if (progress_) grant(progress_->add_xp(xp), now);
}

void Behavior::add_steps(uint32_t steps, uint32_t now) {
  if (progress_) grant(progress_->add_steps(steps), now);
}

void Behavior::meet(const char* code, bool is_new, uint32_t now) {
  last_event_ = last_touch_ = now;
  if (eng_) eng_->note_activity(now);
  mood_.valence = clamp11(mood_.valence + 0.2f);
  mood_.affection = clamp01(mood_.affection + 0.05f);
  SceneParams p;
  p.set_text(code);
  p.value = is_new ? 0 : 1;
  queue_scene(Scene::greet, p, is_new ? pick_unlocked({Expr::excited, Expr::hello}) : pick_unlocked({Expr::hello}), now,
              is_new ? "новый знакомый брелок" : "встретил знакомого");
  if (progress_) grant(progress_->met_friend(is_new), now);
}

void Behavior::duel(const char* code, bool won, uint32_t now) {
  last_event_ = now;
  if (eng_) eng_->note_activity(now);
  SceneParams p;
  p.set_text(code);
  p.value = won ? 1 : 0;
  Expr after =
      won ? pick_unlocked({Expr::proud, Expr::smug, Expr::happy}) : pick_unlocked({Expr::sulking, Expr::grumpy});
  const char* why = won ? "выиграл дуэль" : "проиграл дуэль";
  if (won && progress_) {
    const Expr stolen = progress_->steal_one(rng_);
    if (stolen != Expr::Count) {
      after = stolen;
      why = "выиграл в дуэли новую эмоцию";
    }
  }
  mood_.valence = clamp11(mood_.valence + (won ? 0.25f : -0.15f));
  mood_.annoyance = clamp01(mood_.annoyance + (won ? 0.0f : 0.15f));
  queue_scene(Scene::duel, p, after, now, why);
  if (progress_) grant(progress_->duel(won), now);
}

void Behavior::set_date(uint16_t year, uint8_t month, uint8_t day) {
  year_ = year;
  month_ = month;
  day_ = day;
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
  const Opt opts[] = {{Scene::hide, cyber ? 1.0f : 3.0f},
                      {Scene::reboot, 1.5f},
                      {Scene::error404, cyber ? 2.0f : 1.0f},
                      {Scene::hack, cyber ? 3.0f : 1.0f},
                      {Scene::data_rain, cyber ? 2.0f : 0.7f}};
  float total = 0;
  for (const Opt& o : opts) total += o.w;
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
    default: break;
  }
  queue_scene(s, SceneParams{}, after, now, "сюрприз");
}

}  // namespace face
