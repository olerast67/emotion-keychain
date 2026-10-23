#include "face/behavior.h"

namespace face {

namespace {

constexpr uint32_t kNever = 0xFFFFFFFFu;

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
};
static_assert(sizeof(kEventNames) / sizeof(kEventNames[0]) == kEventCount, "названия событий");

inline float clamp01(float v) { return clampf(v, 0.0f, 1.0f); }

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
  kDaydream,
  kSing,
  kThink,
  kCurious,
  kShy,
  kGiggle,
  kSmug,
  kRobot,
  kParty,
  kDerp,
  kCool,
  kDevil,
  kIdleCount
};

const IdleAct kIdle[kIdleCount] = {
    {Expr::Count, Clip::look_around, 0}, {Expr::Count, Clip::blink, 0},      {Expr::wink, Clip::None, 1800},
    {Expr::Count, Clip::yawn, 0},        {Expr::Count, Clip::sigh, 0},       {Expr::daydreaming, Clip::None, 7000},
    {Expr::singing, Clip::None, 5000},   {Expr::thinking, Clip::None, 4500}, {Expr::curious, Clip::None, 4000},
    {Expr::shy, Clip::None, 3000},       {Expr::giggle, Clip::None, 2500},   {Expr::smug, Clip::None, 3500},
    {Expr::robot, Clip::None, 5000},     {Expr::party, Clip::None, 5000},    {Expr::derp, Clip::None, 3500},
    {Expr::cool, Clip::None, 5000},      {Expr::devilish, Clip::None, 4000},
};

}  // namespace

const char* event_id(Event e) { return (int)e < kEventCount ? kEventIds[(int)e] : "?"; }
const char* event_name(Event e) { return (int)e < kEventCount ? kEventNames[(int)e] : "?"; }

void Behavior::init(FaceEngine* engine, uint32_t seed, uint32_t now) {
  eng_ = engine;
  rng_.seed(seed ^ 0xA5A5F00Du);
  mood_ = Mood{};
  base_ = Expr::neutral;
  react_until_ = manual_until_ = 0;
  last_touch_ = last_event_ = last_decay_ = now;
  next_idle_ = now + 12000u + rng_.next() % 15000u;
  next_eval_ = now;
  shake_level_ = 0;
  for (uint32_t& t : tap_times_) t = 0;
  for (uint8_t& r : recent_idle_) r = 255;
  dark = charging = upside = music_on = speech_on = cold = hot = moving = false;
  battery = 2;
  reason_ = "";
  next_ = now;
}

void Behavior::react(Expr e, uint32_t now, uint32_t ms, const char* why, Clip clip) {
  if (e != Expr::Count) eng_->set_expression(e, now);
  if (clip != Clip::None) eng_->play_clip(clip, now);
  react_until_ = now + ms;
  next_eval_ = react_until_;
  manual_until_ = 0;
  reason_ = why;
  next_ = now;
}

void Behavior::decay(uint32_t now) {
  const float dt = (float)(now - last_decay_) * 0.001f;
  last_decay_ = now;
  if (dt <= 0.0f) return;
  Mood& m = mood_;
  m.valence += (0.15f - m.valence) * clampf(dt * 0.01f, 0.0f, 1.0f);
  m.arousal += (0.25f - m.arousal) * clampf(dt * 0.03f, 0.0f, 1.0f);
  m.annoyance = fmax_(0.0f, m.annoyance - dt * 0.02f);
  m.affection += (0.2f - m.affection) * clampf(dt * 0.002f, 0.0f, 1.0f);
  if (now - last_event_ > 60000u) m.boredom = clamp01(m.boredom + dt / 600.0f);
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
  const uint32_t since_touch = now - last_touch_;
  last_event_ = now;
  mood_.boredom *= 0.3f;
  const Expr cur = eng_->expression();
  const bool asleep = cur == Expr::sleeping || cur == Expr::sleepy;

  switch (e) {
    case Event::tap:
    case Event::double_tap: {
      last_touch_ = now;
      for (int i = 7; i > 0; --i) tap_times_[i] = tap_times_[i - 1];
      tap_times_[0] = now;
      if (e == Event::double_tap) {
        for (int i = 7; i > 0; --i) tap_times_[i] = tap_times_[i - 1];
        tap_times_[0] = now;
      }
      int n = 0;
      for (uint32_t t : tap_times_)
        if (t && now - t < 2500u) ++n;
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
      if (n >= 9) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.3f);
        react(Expr::furious, now, 4000, "тыкают без остановки");
      } else if (n >= 6) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.2f);
        react(Expr::angry, now, 3500, "слишком много тапов");
      } else if (n >= 4) {
        mood_.annoyance = clamp01(mood_.annoyance + 0.12f);
        react(Expr::annoyed, now, 2500, "надоедают тапами", Clip::headshake);
      } else if (n == 3) {
        mood_.valence = clamp01(mood_.valence + 0.1f);
        react(rng_.chance(pers_.playful) ? Expr::playful : Expr::giggle, now, 2500, "тройной тап");
      } else if (n == 2) {
        mood_.valence = clampf(mood_.valence + 0.15f, -1.0f, 1.0f);
        react(mood_.annoyance > 0.5f ? Expr::annoyed : Expr::laugh, now, 3000, "двойной тап");
      } else if (mood_.annoyance > 0.5f) {
        react(Expr::annoyed, now, 2000, "тап, но он не в духе");
      } else if (since_touch > 600000u) {
        react(Expr::surprised, now, 1800, "тап после долгой паузы", Clip::startle);
      } else if (mood_.valence > 0.35f && rng_.chance(pers_.playful)) {
        Expr opts[3] = {Expr::giggle, Expr::wink, Expr::happy};
        react(opts[rng_.next() % 3], now, 2200, "лёгкий тап в хорошем настроении", Clip::hop);
      } else {
        mood_.valence = clampf(mood_.valence + 0.05f, -1.0f, 1.0f);
        react(Expr::surprised, now, 1600, "тап");
      }
      break;
    }
    case Event::shake:
    case Event::shake_hard: {
      shake_level_ += e == Event::shake_hard ? 3.0f : 1.0f;
      mood_.annoyance = clamp01(mood_.annoyance + (e == Event::shake_hard ? 0.15f : 0.06f));
      mood_.arousal = clamp01(mood_.arousal + 0.2f);
      if (shake_level_ >= 7.0f)
        react(Expr::sick, now, 4000, "укачало от тряски");
      else if (shake_level_ >= 3.0f)
        react(Expr::dizzy, now, 4500, "сильно трясли");
      else if (mood_.annoyance > 0.6f && pers_.grumpy > 0.4f)
        react(Expr::angry, now, 3000, "трясут, когда он зол");
      else
        react(Expr::surprised, now, 1500, "встряхнули", Clip::shake_off);
      break;
    }
    case Event::free_fall:
      fall_at_ = now;
      mood_.arousal = 1.0f;
      react(Expr::terrified, now, 1500, "падаю!");
      break;
    case Event::impact:
      mood_.valence = clampf(mood_.valence - (now - fall_at_ < 2000u ? 0.35f : 0.12f), -1.0f, 1.0f);
      if (now - fall_at_ < 2000u)
        react(Expr::knocked_out, now, 3500, "упал и ударился");
      else
        react(Expr::hurt, now, 1800, "стукнули");
      break;
    case Event::pick_up:
      last_touch_ = now;
      if (asleep)
        react(Expr::waking, now, 2400, "взяли в руки сонного", Clip::yawn);
      else if (since_touch > 600000u)
        react(Expr::hello, now, 2500, "взяли в руки после перерыва", Clip::hop);
      else
        react(mood_.valence > 0.2f ? Expr::happy : Expr::curious, now, 2000, "взяли в руки");
      break;
    case Event::walk:
      moving = true;
      mood_.arousal = clamp01(mood_.arousal + 0.1f);
      if (mood_.energy > 0.5f && rng_.chance(0.5f))
        react(Expr::excited, now, 3000, "пошли гулять");
      else
        react(Expr::determined, now, 3000, "идём");
      break;
    case Event::run:
      moving = true;
      mood_.arousal = clamp01(mood_.arousal + 0.3f);
      mood_.energy = clamp01(mood_.energy - 0.03f);
      react(mood_.energy > 0.4f ? Expr::excited : Expr::exhausted, now, 3500, "бежим");
      break;
    case Event::still: moving = false; break;
    case Event::upside_down:
      upside = true;
      upside_since_ = now;
      react(Expr::confused, now, 3000, "перевернули вверх ногами", Clip::double_take);
      break;
    case Event::upright:
      upside = false;
      if (now - upside_since_ > 5000u)
        react(Expr::dizzy, now, 2500, "долго висел вверх ногами");
      else
        react(Expr::relieved, now, 2200, "вернули как было");
      break;
    case Event::loud:
      mood_.arousal = clamp01(mood_.arousal + 0.35f);
      mood_.annoyance = clamp01(mood_.annoyance + 0.05f);
      if (asleep)
        react(Expr::scared, now, 2500, "громкий звук разбудил");
      else
        react(Expr::startled, now, 1600, "громкий звук", Clip::startle);
      break;
    case Event::speech:
      speech_on = true;
      react(Expr::listening, now, 3500, "рядом говорят");
      break;
    case Event::music:
      music_on = true;
      mood_.valence = clampf(mood_.valence + 0.1f, -1.0f, 1.0f);
      react(asleep ? Expr::singing : Expr::music, now, 5000, "играет музыка");
      break;
    case Event::quiet:
      speech_on = music_on = false;
      next_eval_ = now;
      break;
    case Event::dark:
      if (!dark) dark_since_ = now;
      dark = true;
      if (!charging) react(Expr::afraid_dark, now, 2500, "внезапно стало темно");
      break;
    case Event::light:
      if (dark && now - dark_since_ > 60000u)
        react(Expr::waking, now, 2600, "снова светло после долгой темноты", Clip::yawn);
      else if (dark)
        react(Expr::curious, now, 2200, "снова светло", Clip::peek);
      dark = false;
      break;
    case Event::bright:
      dark = false;
      react(Expr::dazzled, now, 2000, "резко очень ярко");
      break;
    case Event::cold:
      cold = true;
      hot = false;
      react(Expr::cold, now, 4000, "холодно");
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
      react(battery < 2 ? Expr::grateful : Expr::happy, now, 2200, "подключили зарядку");
      break;
    case Event::charge_full:
      battery = 2;
      react(Expr::charged, now, 3000, "зарядился");
      break;
    case Event::charge_stop:
      charging = false;
      react(battery < 2 ? Expr::disappointed : Expr::wink, now, 2200, "отключили зарядку");
      break;
    case Event::battery_low:
      battery = 1;
      react(Expr::hungry, now, 4000, "мало заряда");
      break;
    case Event::battery_critical:
      battery = 0;
      react(Expr::critical_battery, now, 4000, "заряд почти закончился");
      break;
    case Event::button:
      last_touch_ = now;
      if (asleep)
        react(Expr::waking, now, 2400, "нажали кнопку", Clip::yawn);
      else
        react(rng_.chance(0.5f) ? Expr::wink : Expr::hello, now, 2000, "нажали кнопку");
      break;
    case Event::button_long: react(Expr::goodbye, now, 2500, "выключают"); break;
    case Event::Count: break;
  }
}

Expr Behavior::choose_base(uint32_t now) {
  const uint32_t quiet = now - last_event_;
  const uint32_t alone = now - last_touch_;
  if (charging) return Expr::charging;
  if (battery == 0) return Expr::critical_battery;
  if (dark && now - dark_since_ > 8000u) return Expr::sleeping;
  if (night() && quiet > 600000u) return Expr::sleeping;
  if (night() && quiet > 120000u) return Expr::sleepy;
  if (upside) return Expr::confused;
  if (cold) return Expr::cold;
  if (hot) return Expr::hot;
  if (music_on) return Expr::music;
  if (speech_on) return Expr::listening;
  if (battery == 1) return ((now / 20000u) & 1u) ? Expr::low_battery : Expr::hungry;
  if (mood_.annoyance > 0.6f) return Expr::grumpy;
  if (mood_.annoyance > 0.35f) return Expr::annoyed;
  if (mood_.energy < 0.12f) return Expr::sleepy;
  if (mood_.energy < 0.25f) return Expr::tired;
  if (alone > 7200000u) return Expr::lonely;
  if (mood_.boredom > 0.7f) return Expr::bored;
  if (mood_.valence < -0.4f) return Expr::sad;
  if (mood_.valence < -0.15f) return Expr::worried;
  if (mood_.valence > 0.55f) return Expr::happy;
  if (mood_.valence > 0.35f) return Expr::content;
  if (quiet > 300000u) return Expr::calm;
  return Expr::neutral;
}

void Behavior::idle_action(uint32_t now) {
  const Expr cur = eng_->expression();
  if (cur == Expr::sleeping || cur == Expr::charging || cur == Expr::critical_battery || cur == Expr::music ||
      cur == Expr::listening)
    return;
  float w[kIdleCount];
  const Mood& m = mood_;
  const bool happy = m.valence > 0.2f;
  w[kLookAround] = 1.0f;
  w[kDoubleBlink] = 0.6f;
  w[kWink] = 0.25f * pers_.playful;
  w[kYawn] = (1.0f - m.energy) * 1.2f + (night() ? 0.6f : 0.0f);
  w[kSigh] = m.boredom + (m.valence < 0.0f ? 0.5f : 0.0f);
  w[kDaydream] = happy ? 0.4f : 0.1f;
  w[kSing] = (happy && !night()) ? 0.35f * pers_.playful : 0.0f;
  w[kThink] = 0.4f;
  w[kCurious] = 0.5f;
  w[kShy] = 0.15f * pers_.shy;
  w[kGiggle] = m.valence > 0.3f ? 0.25f * pers_.playful : 0.0f;
  w[kSmug] = 0.1f;
  w[kRobot] = w[kParty] = w[kDerp] = w[kCool] = w[kDevil] = 0.03f;
  float total = 0;
  for (int i = 0; i < kIdleCount; ++i) {
    for (uint8_t r : recent_idle_)
      if (r == i) w[i] *= 0.15f;  // не повторяться
    total += w[i];
  }
  float pick = rng_.unit() * total;
  int chosen = 0;
  for (int i = 0; i < kIdleCount; ++i) {
    pick -= w[i];
    if (pick <= 0.0f) {
      chosen = i;
      break;
    }
  }
  for (int i = 3; i > 0; --i) recent_idle_[i] = recent_idle_[i - 1];
  recent_idle_[0] = (uint8_t)chosen;
  const IdleAct& a = kIdle[chosen];
  if (a.expr != Expr::Count)
    react(a.expr, now, a.ms, "сам по себе");
  else {
    eng_->play_clip(a.clip, now);
    reason_ = "сам по себе";
  }
}

void Behavior::update(uint32_t now) {
  if (!eng_) return;
  decay(now);
  if (now < manual_until_) {
    next_ = manual_until_;
    return;
  }
  if (!auto_) {
    next_ = kNever;
    return;
  }
  if (now < react_until_) {
    next_ = react_until_;
    return;
  }
  // Не перебиваем цепочку эмоции (например, смех → радость), пока она не доиграла.
  const Expression& cur = expression(eng_->expression());
  const uint32_t started = eng_->expression_started_ms();
  if (cur.hold_ms && now - started < cur.hold_ms) {
    next_ = started + cur.hold_ms;
    return;
  }
  if (now >= next_eval_) {
    const Expr want = choose_base(now);
    if (want != eng_->expression()) {
      eng_->set_expression(want, now);
      reason_ = "настроение и обстановка";
    }
    base_ = want;
    next_eval_ = now + 4000u;
  }
  if (now >= next_idle_) {
    idle_action(now);
    const bool sleepy = mood_.energy < 0.3f;
    next_idle_ = now + (12000u + rng_.next() % 28000u) * (sleepy ? 2u : 1u);
  }
  next_ = next_eval_ < next_idle_ ? next_eval_ : next_idle_;
  if (react_until_ > now && react_until_ < next_) next_ = react_until_;
}

}  // namespace face
