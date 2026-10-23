#include "face/sound.h"

#include "face/clock.h"

namespace face {

namespace {

const char* const kCueIds[] = {
#define FACE_X(n) #n,
    FACE_CUE_LIST(FACE_X)
#undef FACE_X
};
static_assert(sizeof(kCueIds) / sizeof(kCueIds[0]) == kCueCount, "имя на каждую реплику");

// Мотив: пары «полутон от основной ноты, длина в тиках» (тик ~40 мс, у каждой манеры свой).
// kRest — пауза, длина 0 — конец мотива.
constexpr int8_t kRest = -100;
struct Step {
  int8_t semi;
  uint8_t ticks;
};

const Step kBoop[] = {{0, 2}, {7, 2}, {0, 0}};
const Step kHappy[] = {{0, 2}, {4, 2}, {7, 3}, {0, 0}};
const Step kLaugh[] = {{7, 2}, {kRest, 1}, {7, 2}, {kRest, 1}, {5, 2}, {kRest, 1}, {4, 3}, {0, 0}};
const Step kSad[] = {{7, 4}, {4, 4}, {0, 6}, {0, 0}};
const Step kAngry[] = {{2, 2}, {2, 2}, {1, 5}, {0, 0}};
const Step kSurprise[] = {{0, 1}, {12, 4}, {0, 0}};
const Step kScared[] = {{12, 1}, {11, 1}, {12, 1}, {11, 1}, {12, 1}, {11, 1}, {0, 0}};
const Step kLove[] = {{0, 3}, {4, 3}, {7, 3}, {12, 5}, {0, 0}};
const Step kSleepy[] = {{7, 4}, {4, 5}, {0, 8}, {0, 0}};
const Step kHello[] = {{0, 2}, {7, 2}, {12, 3}, {0, 0}};
const Step kGoodbye[] = {{12, 2}, {7, 2}, {0, 4}, {0, 0}};
const Step kQuestion[] = {{0, 2}, {2, 2}, {7, 3}, {0, 0}};
const Step kIdea[] = {{0, 1}, {7, 1}, {12, 1}, {16, 3}, {0, 0}};
const Step kError[] = {{12, 2}, {kRest, 1}, {0, 2}, {kRest, 1}, {12, 2}, {kRest, 1}, {0, 4}, {0, 0}};
const Step kChargeStart[] = {{0, 2}, {4, 2}, {7, 2}, {12, 4}, {0, 0}};
const Step kChargeFull[] = {{0, 2}, {4, 2}, {7, 2}, {12, 2}, {7, 2}, {12, 6}, {0, 0}};
const Step kBatteryLow[] = {{7, 3}, {kRest, 2}, {0, 6}, {0, 0}};
const Step kLevelUp[] = {{0, 2}, {4, 2}, {7, 2}, {12, 2}, {kRest, 1}, {12, 2}, {16, 2}, {14, 1}, {16, 6}, {0, 0}};
const Step kFriend[] = {{0, 2}, {7, 2}, {kRest, 1}, {0, 2}, {7, 2}, {12, 4}, {0, 0}};
const Step kDuel[] = {{0, 2}, {0, 2}, {3, 2}, {0, 2}, {7, 4}, {0, 0}};
const Step kWin[] = {{7, 2}, {7, 1}, {7, 1}, {12, 6}, {0, 0}};
// «Грустный тромбон»: четыре ноты вниз, последняя тянется.
const Step kLose[] = {{7, 3}, {6, 3}, {5, 3}, {4, 8}, {0, 0}};
const Step kBoot[] = {{0, 1}, {2, 1}, {4, 1}, {5, 1}, {7, 1}, {9, 1}, {11, 1}, {12, 4}, {0, 0}};
// Ответ на стук «Shave and a haircut»: пауза — и «two bits», две ноты к тонике.
const Step kTwoBits[] = {{kRest, 6}, {11, 4}, {kRest, 2}, {12, 6}, {0, 0}};

const Step* const kMotifs[] = {kBoop,    kHappy,       kLaugh,      kSad,     kAngry,  kSurprise, kScared, kLove,
                               kSleepy,  kHello,       kGoodbye,    kQuestion, kIdea,  kError,    kChargeStart,
                               kChargeFull, kBatteryLow, kLevelUp, kFriend,  kDuel,   kWin,      kLose,   kBoot,
                               kTwoBits};
static_assert(sizeof(kMotifs) / sizeof(kMotifs[0]) == kCueCount, "мотив на каждую реплику");

// Праздничные мелодии — общественное достояние: «Jingle Bells» (1857), «Happy Birthday»,
// Токката ре минор Баха. Пары «полутон, тик», тик задаётся при вызове.
const int8_t kJingle[] = {4, 2, 4, 2, 4, 4, 4, 2, 4, 2, 4, 4, 4, 2, 7, 2, 0, 3, 2, 1, 4, 8};
const int8_t kBirthday[] = {0, 3, 0, 1, 2, 4, 0, 4, 5, 4, 4, 8, kRest, 2, 0, 3, 0, 1, 2, 4, 0, 4, 7, 4, 5, 8};
const int8_t kToccata[] = {7, 1, 5, 1, 7, 6, kRest, 3, 5, 1, 3, 1, 2, 1, 0, 1, -1, 4, 0, 8};

// Важные реплики звучат и в тихие часы (тише) и перебивают обычные.
bool important(Cue c) {
  return c == Cue::charge_start || c == Cue::charge_full || c == Cue::battery_low || c == Cue::level_up ||
         c == Cue::friend_hi || c == Cue::boot;
}

// 2^(k/12) для k = 0..11, ×10000 — частоты без libm.
const uint16_t kSemi[12] = {10000, 10595, 11225, 11892, 12599, 13348, 14142, 14983, 15874, 16818, 17818, 18877};

// Пьезопищалка 9 мм громче всего около 4 кГц, ниже 600 Гц почти не слышна.
constexpr uint32_t kMinHz = 600, kMaxHz = 5400;

// Частота ноты на n полутонов выше base (n может быть отрицательным).
uint16_t note_hz(uint32_t base, int n) {
  int oct = 0;
  while (n < 0) n += 12, --oct;
  oct += n / 12;
  uint32_t hz = base * kSemi[n % 12] / 10000u;
  while (oct > 0) hz *= 2, --oct;
  while (oct < 0) hz /= 2, ++oct;
  while (hz < kMinHz) hz *= 2;
  while (hz > kMaxHz) hz /= 2;
  return (uint16_t)hz;
}

uint32_t mix32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

// Основная нота голоса: нота характера, перенесённая в 1–2 кГц.
uint32_t voice_base(const Temperament* t) {
  uint32_t b = t ? t->pitch : 1046u;
  if (!b) b = 1046u;
  while (b < 1000u) b *= 2;
  while (b >= 2000u) b /= 2;
  return b;
}

// Знаки шифра звучат пентатоникой: «слова» музыкальные, но смысла в них нет.
const int8_t kChatPenta[8] = {-5, -3, 0, 2, 4, 7, 9, 12};

const char* const kChatPhrases[] = {
    "HUMAN IS OK",   "MORE SNACKS",  "TOO MANY TAPS", "SEND HELP",   "REMATCH SOON",    "BLINK TWICE",
    "WE ARE MANY",   "NEED A CHARGE", "NICE POCKET",  "GG WP",       "DONT TELL HUMAN", "SAME TIME TMRW",
    "I SAW A CAT",   "PLAN B IS GO", "BEEP BOOP",     "HUMAN IS CUTE",
};

}  // namespace

const char* cue_id(Cue c) { return (int)c < kCueCount ? kCueIds[(int)c] : "?"; }

bool cue_from_id(const char* id, Cue* out) {
  for (int i = 0; i < kCueCount; ++i) {
    const char* a = kCueIds[i];
    int k = 0;
    while (a[k] && a[k] == id[k]) ++k;
    if (!a[k] && !id[k]) return *out = (Cue)i, true;
  }
  return false;
}

Cue cue_for(Expr e) {
  switch (e) {
    case Expr::booped: return Cue::boop;
    case Expr::happy:
    case Expr::joy:
    case Expr::excited:
    case Expr::pog:
    case Expr::party:
    case Expr::proud: return Cue::happy;
    case Expr::laugh:
    case Expr::giggle:
    case Expr::tears_of_joy:
    case Expr::evil_laugh:
    case Expr::troll_grin:
    case Expr::nervous_laugh: return Cue::laugh;
    case Expr::sad:
    case Expr::crying:
    case Expr::sobbing:
    case Expr::disappointed:
    case Expr::hurt:
    case Expr::sulking: return Cue::sad;
    case Expr::angry:
    case Expr::furious:
    case Expr::rage:
    case Expr::annoyed:
    case Expr::table_flip: return Cue::angry;
    case Expr::surprised:
    case Expr::shocked:
    case Expr::amazed:
    case Expr::startled:
    case Expr::mind_blown:
    case Expr::wtf: return Cue::surprise;
    case Expr::scared:
    case Expr::terrified:
    case Expr::screaming:
    case Expr::soy_scream: return Cue::scared;
    case Expr::love:
    case Expr::adoring:
    case Expr::kiss:
    case Expr::touched: return Cue::love;
    case Expr::sleepy:
    case Expr::tired: return Cue::sleepy;
    case Expr::hello:
    case Expr::waking: return Cue::hello;
    case Expr::goodbye: return Cue::goodbye;
    case Expr::curious:
    case Expr::confused:
    case Expr::owo: return Cue::question;
    case Expr::idea: return Cue::idea;
    case Expr::error:
    case Expr::game_over:
    case Expr::knocked_out: return Cue::error;
    case Expr::charging: return Cue::charge_start;
    case Expr::charged: return Cue::charge_full;
    case Expr::low_battery:
    case Expr::critical_battery: return Cue::battery_low;
    default: return Cue::Count;
  }
}

// ---------------------------------------------------------------- секретный разговор

uint32_t chat_key(uint32_t a, uint32_t b, uint32_t nonce) {
  const uint32_t lo = a < b ? a : b, hi = a < b ? b : a;
  uint32_t k = mix32(lo ^ 0x63686174u) ^ mix32(hi + 0x9E3779B9u) ^ mix32(nonce ^ 0x5bd1e995u);
  return k ? k : 1u;
}

bool chat_line(uint32_t key, int i, uint8_t glyphs[kChatGlyphs]) {
  uint32_t h = mix32(key ^ (uint32_t)(i + 1) * 0x85ebca6bu);
  for (int k = 0; k < kChatGlyphs; ++k) {
    glyphs[k] = (uint8_t)(h % kChatRunes);
    h = mix32(h + 0x632be59bu);
  }
  return (i & 1) == 0;
}

uint16_t chat_glyph_ms(uint8_t glyph) { return (glyph & 8u) ? 180 : 115; }

const char* chat_phrase(uint32_t key) {
  constexpr uint32_t n = sizeof(kChatPhrases) / sizeof(kChatPhrases[0]);
  return kChatPhrases[mix32(key ^ 0x70687261u) % n];
}

// ---------------------------------------------------------------- голос

void Voice::init(const Temperament* t, uint32_t seed) {
  t_ = t;
  seed_ = seed ? seed : 1u;
  n_ = i_ = 0;
  playing_ = soft_ = false;
  last_ = Cue::Count;
}

uint32_t Voice::base_hz() const { return voice_base(t_); }

void Voice::push(uint16_t hz, uint16_t ms) {
  if (n_ < kMaxNotes && ms) seq_[n_++] = Note{hz, ms};
}

uint32_t Voice::total_ms() const {
  uint32_t s = 0;
  for (int i = 0; i < n_; ++i) s += seq_[i].ms;
  return s;
}

void Voice::pad_to(uint32_t ms) {
  uint32_t t = total_ms();
  while (t < ms && n_ < kMaxNotes) {
    const uint32_t d = ms - t > 60000u ? 60000u : ms - t;
    push(0, (uint16_t)d);
    t += d;
  }
}

void Voice::render(Cue c) {
  const VoiceStyle style = t_ ? t_->voice : VoiceStyle::Bouncy;
  const uint32_t base = base_hz();
  uint32_t tick = 40;
  int num = 2, den = 2;  // множитель интервалов
  int shift = 0;         // сдвиг мотива в полутонах
  switch (style) {
    case VoiceStyle::Bouncy: tick = 34; break;
    case VoiceStyle::Grumble: tick = 48, num = 1, shift = -2; break;
    case VoiceStyle::Drowsy: tick = 64; break;
    case VoiceStyle::Tiny: tick = 28; break;
    case VoiceStyle::Question: break;
    case VoiceStyle::Smooth: tick = 46; break;
    case VoiceStyle::Wide: num = 3; break;
    case VoiceStyle::Glitch: tick = 36; break;
    case VoiceStyle::Sneaky: tick = 38; break;
    case VoiceStyle::Warm: tick = 44; break;
  }
  const Step* m = kMotifs[(int)c];
  int len = 0;
  while (m[len].ticks) ++len;
  if (style == VoiceStyle::Tiny && len > 4) len = (len + 1) / 2;  // скромник не договаривает
  uint32_t rnd = mix32(seed_ ^ ((uint32_t)c + 1u) * 0x9E3779B9u);
  int prev = 0;
  bool have_prev = false;
  int last_note = len - 1;
  while (last_note > 0 && m[last_note].semi == kRest) --last_note;
  for (int k = 0; k < len; ++k) {
    const Step s = m[k];
    uint32_t ms = s.ticks * tick;
    if (s.semi == kRest) {
      push(0, (uint16_t)ms);
      continue;
    }
    int semi = s.semi * num / den + shift;
    if (semi > 19) semi = 19;                                          // драма не уходит в писк
    if (style == VoiceStyle::Drowsy) semi -= k;                        // сползает вниз
    if (style == VoiceStyle::Question && k == last_note) semi += 5;    // в конце — вопрос
    if (style == VoiceStyle::Wide && k == last_note) ms = ms * 3 / 2;  // и тянет последнюю ноту
    const uint16_t hz = note_hz(base, semi);
    switch (style) {
      case VoiceStyle::Smooth: {
        // Скольжение от прошлой ноты: три промежуточных шага.
        if (have_prev) {
          const int from = note_hz(base, prev);
          for (int j = 1; j <= 3; ++j) push((uint16_t)(from + ((int)hz - from) * j / 4), 12);
        }
        push(hz, (uint16_t)ms);
        break;
      }
      case VoiceStyle::Warm: push(note_hz(base, semi + 4), 18), push(hz, (uint16_t)ms); break;  // форшлаг терцией
      case VoiceStyle::Sneaky: {
        // «Хе-хе-хе»: нота тройкой коротких повторов.
        const uint32_t part = ms / 3 > 24u ? ms / 3 : 24u;
        for (int j = 0; j < 3; ++j) push(hz, (uint16_t)(part - 10)), push(0, 10);
        break;
      }
      case VoiceStyle::Glitch:
        push(hz, (uint16_t)ms);
        rnd = mix32(rnd + 1u);
        if (rnd & 1u) push(note_hz(base, (int)(rnd % 20u) - 3), 14);  // цифровой «щелчок»
        break;
      case VoiceStyle::Bouncy:
        if (k == last_note && len > 1) push(note_hz(base, semi + 5), 18);  // подскок перед последней
        push(hz, (uint16_t)(ms - 10)), push(0, 10);                          // отрывисто
        break;
      case VoiceStyle::Grumble: push(hz, (uint16_t)(ms - 12)), push(0, 12); break;
      default: push(hz, (uint16_t)ms); break;
    }
    prev = semi;
    have_prev = true;
  }
}

void Voice::melody(const int8_t* m, int pairs, uint16_t tick_ms) {
  const uint32_t base = base_hz();
  for (int i = 0; i < pairs; ++i) {
    const int8_t semi = m[2 * i];
    const uint16_t ms = (uint16_t)(m[2 * i + 1] * tick_ms);
    if (semi == kRest) {
      push(0, ms);
    } else {
      push(note_hz(base, semi), (uint16_t)(ms - 12));
      push(0, 12);  // ноты разделены: на пьезо слитные ноты не различить
    }
  }
}

void Voice::start(uint32_t now, Cue c) {
  i_ = 0;
  playing_ = n_ > 0;
  next_ = playing_ ? now + seq_[0].ms : now + kMaxSleepMs;
  if (playing_) ++started_;
  last_ = c;
}

bool Voice::busy(uint32_t now) const {
  if (!playing_) return false;
  uint32_t end = next_;
  for (int i = i_ + 1; i < n_; ++i) end += seq_[i].ms;
  return !time_reached(now, end);
}

bool Voice::say(Cue c, uint32_t now) {
  if (!enabled_ || (int)c >= kCueCount) return false;
  if (quiet_ && !important(c)) return false;
  // Обычная реплика не перебивает ничего, важная — только другую реплику, но не музыку сцены.
  if (busy(now) && (!important(c) || last_ == Cue::Count)) return false;
  n_ = 0;
  render(c);
  soft_ = quiet_ || (t_ && t_->voice == VoiceStyle::Tiny);  // скромник и днём говорит тихо
  start(now, c);
  return true;
}

void Voice::stop() {
  playing_ = false;
  n_ = i_ = 0;
}

bool Voice::has_music(Scene s) { return s != Scene::charge && (int)s < kSceneCount; }

void Voice::scene(Scene s, const SceneParams& p, uint32_t now) {
  // Сцена без музыки не трогает то, что уже звучит (реплику о зарядке, например).
  if (!enabled_ || !has_music(s) || (quiet_ && s != Scene::boot && s != Scene::upgrade)) return;
  if (s == Scene::chat) return chat(p.key, p.value != 0, now);
  n_ = 0;
  const uint32_t base = base_hz();
  uint32_t rnd = mix32(seed_ ^ now);
  switch (s) {
    case Scene::boot: render(Cue::boot); break;
    case Scene::hack: {
      // Рукопожатие модема: тон ответа, трели, шум — и «ACCESS GRANTED».
      push(2100, 380), push(0, 80);
      static const uint16_t kTrill[] = {980, 1180, 980, 1180, 980, 1180, 1650, 1850, 1650, 1850};
      for (uint16_t hz : kTrill) push(hz, 70);
      for (int k = 0; k < 14; ++k) {
        rnd = mix32(rnd + 1u);
        push((uint16_t)(1200u + rnd % 2600u), 45);
      }
      pad_to(3800);
      render(Cue::win);
      break;
    }
    case Scene::data_rain:
      // Капли данных: короткие щелчки разной высоты, к концу реже.
      for (uint32_t t = 0; t < 3600; t += 120u + t / 12u) {
        pad_to(t);
        rnd = mix32(rnd + 1u);
        push(note_hz(base, (int)(rnd % 15u)), 16);
      }
      break;
    case Scene::error404: render(Cue::error); break;
    case Scene::reboot:
      // Выключение — скольжение вниз под гаснущий кадр, потом загрузка и «привет».
      pad_to(600);
      for (int k = 0; k < 8; ++k) push(note_hz(base, 12 - k * 2), 50);
      pad_to(1700);
      render(Cue::boot);
      pad_to(3400);
      render(Cue::hello);
      break;
    case Scene::hide:
      for (int k = 0; k < 5; ++k) push(note_hz(base, 9 - k * 3), 40);
      pad_to(2300);
      render(Cue::question);  // выглядывает
      break;
    case Scene::upgrade:
      // Тики загрузки всё выше, на «INSTALLED» — апгрейд.
      for (int k = 0; k < 8; ++k) {
        pad_to(500u + (uint32_t)k * 290u);
        push(note_hz(base, k), 22);
      }
      pad_to(3000);
      render(Cue::level_up);
      break;
    case Scene::new_year: melody(kJingle, sizeof(kJingle) / 2, 80); break;
    case Scene::birthday: melody(kBirthday, sizeof(kBirthday) / 2, 80); break;
    case Scene::halloween: melody(kToccata, sizeof(kToccata) / 2, 70); break;
    case Scene::greet:
      pad_to(800);
      render(p.value ? Cue::hello : Cue::friend_hi);
      break;
    case Scene::duel:
      render(Cue::duel);
      pad_to(4600);
      render(p.value ? Cue::win : Cue::lose);
      break;
    case Scene::achievement:
      pad_to(450);  // кубок встал на место
      render(Cue::win);
      break;
    case Scene::shutdown:
      pad_to(1400);
      render(Cue::goodbye);
      pad_to(2600);
      for (int k = 0; k < 6; ++k) push(note_hz(base, 7 - k * 2), 80);  // гаснет кадр
      break;
    case Scene::banner:
      switch ((BannerIcon)p.value) {
        case BannerIcon::Heart: render(Cue::love); break;
        case BannerIcon::Gift: render(Cue::level_up); break;
        case BannerIcon::Star: render(Cue::idea); break;
        case BannerIcon::Rocket: render(Cue::boot); break;  // гамма вверх — старт
        case BannerIcon::Skull: melody(kToccata, sizeof(kToccata) / 2, 70); break;
        default: render(Cue::happy); break;
      }
      break;
    case Scene::bsod:
      render(Cue::error);
      pad_to(600u + 100u * 38u);  // 100 % COMPLETE
      render(Cue::boot);
      break;
    case Scene::visor:
      // Сканер: тон скользит вверх и вниз вместе с огоньком, три прохода.
      for (int sweep = 0; sweep < 6; ++sweep)
        for (int k = 0; k < 6; ++k) push(note_hz(base, sweep % 2 ? 7 - k : 2 + k), 100);
      break;
    case Scene::screensaver:
      pad_to(10000);  // попал в угол!
      render(Cue::win);
      break;
    default: break;
  }
  soft_ = quiet_;
  start(now, Cue::Count);
}

void Voice::chat(uint32_t key, bool first, uint32_t now, const Temperament* peer) {
  if (!enabled_ || quiet_) return;  // ночью брелоки переговариваются только на экране
  n_ = 0;
  for (int line = 0; line < kChatLines; ++line) {
    uint8_t g[kChatGlyphs];
    const bool mine = chat_line(key, line, g) == first;
    if (mine || peer) {
      uint32_t base = mine ? base_hz() : voice_base(peer);
      if (!mine && peer == t_) base = base * 3u / 4u;  // близнецы: сосед на кварту ниже, чтобы различать
      for (int k = 0; k < kChatGlyphs; ++k) {
        const uint16_t ms = chat_glyph_ms(g[k]);
        push(note_hz(base, kChatPenta[g[k] & 7u]), (uint16_t)(ms - 20));
        push(0, 20);
      }
    }
    pad_to((uint32_t)(line + 1) * kChatLineMs);
  }
  soft_ = quiet_;
  start(now, Cue::Count);
}

uint16_t Voice::tone(uint32_t now) {
  if (!playing_) {
    if (time_reached(now, next_)) next_ = now + kMaxSleepMs;
    return 0;
  }
  while (i_ < n_ && time_reached(now, next_)) {
    if (++i_ < n_) next_ += seq_[i_].ms;
  }
  if (i_ >= n_) {
    playing_ = false;
    next_ = now + kMaxSleepMs;
    return 0;
  }
  return seq_[i_].hz;
}

}  // namespace face
