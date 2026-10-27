// Behavior — «характер»: превращает события датчиков в эмоции и клипы,
// ведёт настроение и сам придумывает, чем заняться, когда ничего не происходит.
//
//   Behavior b;  b.init(&engine, seed, now);
//   b.attach_voice(&voice);                   // по желанию: реплики пищалкой (sound.h)
//   b.on_event(Event::DoubleTap, now);        // из обработчика прерывания IMU
//   b.update(now);                            // в главном цикле, в любом режиме
//   спать до ближайшего из engine.next_update_ms(), b.next_update_ms() и voice.next_change_ms()
//
// Как и движок, поведение не планирует дальше kMaxSleepMs и сравнивает время через time_reached.
#pragma once

#include <stdint.h>

#include "face/engine.h"
#include "face/friends.h"
#include "face/progress.h"
#include "face/sound.h"
#include "face/temperament.h"

namespace face {

#define FACE_EVENT_LIST(X) \
  X(tap)                   \
  X(double_tap)            \
  X(shake)                 \
  X(shake_hard)            \
  X(free_fall)             \
  X(impact)                \
  X(pick_up)               \
  X(walk)                  \
  X(run)                   \
  X(still)                 \
  X(upside_down)           \
  X(upright)               \
  X(loud)                  \
  X(speech)                \
  X(music)                 \
  X(quiet)                 \
  X(dark)                  \
  X(light)                 \
  X(bright)                \
  X(cold)                  \
  X(hot)                   \
  X(temp_ok)               \
  X(charge_start)          \
  X(charge_full)           \
  X(charge_stop)           \
  X(battery_low)           \
  X(battery_critical)      \
  X(button)                \
  X(button_long)           \
  X(friend_new)            \
  X(friend_known)          \
  X(duel_win)              \
  X(duel_lose)

enum class Event : uint8_t {
#define FACE_X(n) n,
  FACE_EVENT_LIST(FACE_X)
#undef FACE_X
      Count
};
constexpr int kEventCount = (int)Event::Count;
const char* event_id(Event e);
const char* event_name(Event e);  // по-русски

// Черты характера 0..1, задают вероятности реакций.
struct Personality {
  float playful = 0.6f;
  float grumpy = 0.3f;
  float sleepy = 0.4f;
  float shy = 0.4f;
};

struct Mood {
  float valence = 0.2f;    // −1 грусть … +1 радость
  float arousal = 0.3f;    // 0 вялый … 1 возбуждён
  float energy = 0.8f;     // 0 засыпает … 1 бодр
  float boredom = 0.0f;    // растёт, когда ничего не происходит
  float annoyance = 0.0f;  // растёт от тряски и частых тапов
  float affection = 0.3f;  // растёт от внимания
};

// Сколько яркости нужно экрану сейчас. Прошивка переводит уровень в контраст.
enum class Brightness : uint8_t {
  Normal,  // обычная
  Dim,     // ночь или персонаж спит
  Dark,    // темно, брелок в кармане или в сумке
};

class Behavior {
 public:
  void init(FaceEngine* engine, uint32_t seed, uint32_t now_ms);
  void on_event(Event e, uint32_t now_ms);
  void update(uint32_t now_ms);
  uint32_t next_update_ms() const { return next_; }

  // Часы для суточного ритма: минуты от начала текущих суток (0..1439). Если часы известны —
  // это минуты от полуночи.
  void set_clock(uint16_t minutes) {
    clock_min_ = (uint16_t)(minutes % 1440);
    if (voice_) voice_->set_quiet(night());  // тихие часы — сразу, а не на следующем пробуждении
  }
  // Известно ли настоящее время (по умолчанию — да). Без часов минуты set_clock — только счёт
  // суток (лимит кормления), а всё «по часам» выключено: ночь (приглушённый экран, тихий
  // голос, сон без дела), недосып, пасхалки по времени и достижения за 3 и 5 утра. Темноту
  // брелок и без часов видит по датчику света.
  void set_clock_known(bool known) {
    clock_known_ = known;
    if (voice_) voice_->set_quiet(night());
  }
  bool clock_known() const { return clock_known_; }
  // Без автоматики поведение только ведёт настроение; включённое снова — сразу в деле.
  void set_auto(bool on) {
    auto_ = on;
    if (on) next_ = last_now_;
  }
  bool auto_mode() const { return auto_; }
  // Ручной выбор эмоции из симулятора: поведение не мешает ms (не дольше 10 минут).
  void hold_manual(uint32_t now_ms, uint32_t ms);
  // Какой яркости экрана хватает: в темноте — минимум, ночью и во сне — приглушённо.
  Brightness brightness() const;

  // Характер брелока (temperament.h): лицо в покое, ответ на касания, свои занятия,
  // темп и сбои картинки. nullptr — «средний» персонаж, как в симуляторе.
  void set_temperament(const Temperament* t);
  const Temperament* temperament_ptr() const { return temp_; }
  // Хэш своего брелока (identity.h): по нему с соседом считается ключ секретного разговора.
  void set_identity_hash(uint32_t h) { self_hash_ = h; }

  // Голос: реплики на касания и события (чаще у разговорчивых характеров), музыка сцен,
  // ночью — только важное. nullptr — молчит. Характер голосу задаёт сам Voice::init.
  void attach_voice(Voice* v) { voice_ = v; }
  Voice* voice() const { return voice_; }
  // Стенд и симулятор с одним брелоком: в секретном разговоре звучат обе стороны.
  void set_chat_both_sides(bool on) { chat_both_ = on; }

  // Рост: без прогресса открыто всё (симулятор, тесты). С прогрессом персонаж
  // показывает только открытые эмоции, а касания, шаги и встречи дают опыт.
  void attach_progress(Progress* p);
  Progress* progress() const { return progress_; }
  // Знакомые брелоки (friends.h): уровни дружбы, подарки наставника. nullptr — без памяти о знакомых.
  void attach_friends(Friends* f) { friends_ = f; }
  Friends* friends() const { return friends_; }
  // Закрытая эмоция подменяется ближайшей открытой (furious → angry и т. п.).
  Expr allowed(Expr e) const;
  void new_day(uint32_t now_ms);
  void add_xp(uint32_t xp, uint32_t now_ms);
  void add_steps(uint32_t steps, uint32_t now_ms);

  // Другой брелок рядом (BLE): приветствие и опыт. code — его позывной, peer_hash — хэш
  // из его рекламного пакета: по нему виден характер соседа, и встреча идёт по-разному
  // для разных пар (близнецы, ворчун и игрун, скромник с кем угодно…). 0 — неизвестен.
  // С таблицей знакомых новизну решает она, а дружба растёт по дням встреч. peer_level —
  // его имплант: старший на два и больше раз в день дарит эмоцию. С nonce сеанса старые
  // знакомые иногда шепчутся шифром: решение из общего ключа одинаково у обоих, а «старый
  // ли знакомый» каждый решает по своей таблице (обычно у соседа она та же).
  void meet(const char* code, bool is_new, uint32_t now_ms, uint32_t peer_hash = 0, uint8_t peer_level = 0,
            uint32_t nonce = 0);
  // Хакерская дуэль с другим брелоком: победа открывает эмоцию досрочно. С peer_hash
  // и общим nonce сеанса BLE после дуэли брелоки «переговариваются» шифром (сцена chat):
  // оба ставят её в очередь одинаково, поэтому разговор идёт у них одновременно. Дуэль —
  // тоже встреча: день дружбы, подарок старшего (peer_level), близнец и легенда — как в meet.
  void duel(const char* code, bool won, uint32_t now_ms, uint32_t peer_hash = 0, uint32_t nonce = 0,
            uint8_t peer_level = 0);
  // Секретный разговор с соседом без дуэли.
  void chat(const char* code, uint32_t peer_hash, uint32_t nonce, uint32_t now_ms);

  // Питание. Процент заряда показывает сцена зарядки (−1 — неизвестен). При малом заряде
  // поведение само переходит в экорежим: реже кадры и занятия, без сюрпризов.
  void set_battery_percent(int pct) { battery_pct_ = pct < -1 ? -1 : pct > 100 ? 100 : pct; }
  bool eco() const { return eco_; }
  // Выключение: «LOW BATTERY → GOODNIGHT» и сон; прошивка гасит всё по eng.scene_ends_ms().
  // low_battery — выключение из-за пустой ячейки: сцена сначала показывает батарейку.
  void power_off(uint32_t now_ms, bool low_battery = false);

  // Календарь: Новый год, Хэллоуин, день рождения хозяина и пасхалки — день π, 4 мая,
  // 1 апреля, 12 апреля, день полотенца, день программиста, пятница 13-е, 29 февраля.
  void set_date(uint16_t year, uint8_t month, uint8_t day);
  void set_birthday(uint8_t month, uint8_t day) { bday_m_ = month, bday_d_ = day; }
  // Сюрпризы — взлом, прятки, перезагрузка, ERROR 404: раз в min…max мс бодрствования.
  void set_surprise_interval(uint32_t min_ms, uint32_t max_ms, uint32_t now_ms);
  void surprise(uint32_t now_ms);
  // Сцена с последующей эмоцией: очередь на несколько штук (встреча → апгрейд).
  // fresh — эмоция после сцены только что открыта: в углу мигает «NEW».
  void queue_scene(Scene s, const SceneParams& p, Expr after, uint32_t now_ms, const char* why, bool fresh = false);
  // Общая сцена с соседом (встреча, дуэль, разговор): встаёт в начало очереди за другими
  // общими и начинается сразу, прерывая свою сцену — та покажется после. Так у двух
  // брелоков общие сцены идут одновременно, что бы у каждого ни стояло в очереди.
  void queue_shared(Scene s, const SceneParams& p, Expr after, uint32_t now_ms, const char* why, bool fresh = false);

  const Mood& mood() const { return mood_; }
  Personality& personality() { return pers_; }
  Expr base_expression() const { return base_; }
  const char* last_reason() const { return reason_; }

  // Состояние окружения (выставляется событиями, можно читать).
  bool dark = false, charging = false, upside = false, music_on = false, speech_on = false;
  bool cold = false, hot = false, moving = false, running = false;
  uint8_t battery = 2;  // 0 — критично, 1 — мало, 2 — норма

 private:
  struct Option {
    Expr expr;
    float weight;
  };
  void react(Expr e, uint32_t now, uint32_t ms, const char* why, Clip clip = Clip::None);
  // Сказать реплику: chance < 1 — с такой вероятностью (умноженной на разговорчивость характера)
  // и не чаще раза в kSayGapMs; chance = 1 — всегда, если пищалка свободна.
  void say(Cue c, uint32_t now, float chance = 1.0f);
  void voice_event(Event e, uint32_t now);
  // Показать эмоцию и запомнить, что её выбрало поведение (а не цепочка движка).
  void show(Expr e, uint32_t now);
  void schedule_surprise(uint32_t now);
  // Отметки «когда было» старше суток прижимаются к суткам: разность uint32 верна лишь до 49.7 суток.
  void age_marks(uint32_t now);
  // Взвешенный выбор из пула вариантов, недавние реакции выпадают реже.
  Expr pick(const Option* opts, int n);
  template <int N>
  Expr pick(const Option (&opts)[N]) {
    return pick(opts, N);
  }
  void decay(uint32_t now);
  Expr choose_base(uint32_t now);
  void idle_action(uint32_t now);
  void grant(int installed, uint32_t now);
  // Достижения: за счётчики — после каждого события, за события — earn().
  void achieve(uint32_t now);
  void earn(Ach a, uint32_t now);
  void reward(Ach a, uint32_t now);
  void set_eco(int level);
  // Пасхалки по дате и времени суток: раз за дату и раз в сутки на каждое время.
  bool date_eggs(uint32_t now);
  bool time_eggs(uint32_t now);
  bool knock(uint32_t now);
  void banner(const char* big, const char* sub, BannerIcon icon, Expr after, uint32_t now, const char* why);
  // Эмоция после приветствия: зависит от своего характера и характера соседа.
  Expr greet_after(const Temperament* other, bool is_new);
  bool run_queue(uint32_t now);
  // После встречи или дуэли (Friends::meet уже засчитал день): баннер роста дружбы с опытом
  // и достижением, подарок старшего, близнец, легенда, опыт за нового или старого знакомого.
  void friendship(const char* code, uint32_t peer_hash, bool is_new, int before, int level, bool first_today,
                  uint8_t peer_level, uint32_t now);
  // Первая открытая из вариантов; если закрыты все — ближайшая замена первой.
  Expr pick_unlocked(const Expr* opts, int n);
  template <int N>
  Expr pick_unlocked(const Expr (&opts)[N]) {
    return pick_unlocked(opts, N);
  }
  // Время суток в [from, to) по настоящим часам; без часов — никогда.
  bool clock_in(int from, int to) const { return clock_known_ && clock_min_ >= from && clock_min_ < to; }
  bool night() const { return clock_known_ && (clock_min_ >= 23 * 60 || clock_min_ < 6 * 60); }
  // Эмоция доступна: открыта ростом или своя у характера.
  bool usable(Expr e) const;
  // Подмена через характер: «в покое» — его лицо, «рад» — его радость и т. д.
  Expr flavor(Expr e) const;

  FaceEngine* eng_ = nullptr;
  Rng rng_;
  Personality pers_;
  const Temperament* temp_ = nullptr;
  Mood mood_;
  bool auto_ = true;
  uint16_t clock_min_ = 12 * 60;
  bool clock_known_ = true;

  Expr base_ = Expr::neutral;
  // Сроки с флагом «взведён»: устаревший срок не путается с будущим после переполнения millis().
  uint32_t react_until_ = 0;
  bool reacting_ = false;
  uint32_t manual_until_ = 0;
  bool manual_ = false;
  // Эмоция, которую поставила цепочка движка (смех → радость): её не перебиваем до follow_until_.
  // chose_ms_ — когда поведение само выбрало эмоцию в последний раз.
  uint32_t seen_t0_ = 0;
  uint32_t follow_until_ = 0;
  uint32_t chose_ms_ = 0;
  bool follow_ = false;
  uint32_t last_touch_ = 0;  // последнее внимание (тап, взяли в руки)
  uint32_t last_event_ = 0;  // последнее любое событие
  uint32_t last_decay_ = 0;
  uint32_t last_now_ = 0;
  uint32_t next_idle_ = 0;
  uint32_t next_eval_ = 0;
  uint32_t dark_since_ = 0;
  uint32_t upside_since_ = 0;
  uint32_t fall_at_ = 0;
  bool falling_ = false;  // было свободное падение, ждём удара
  uint32_t falls_[3] = {};
  uint32_t loud_at_ = 0;
  uint32_t last_calling_ = 0;
  uint32_t last_cyclops_ = 0;
  Expr recent_react_[4] = {Expr::Count, Expr::Count, Expr::Count, Expr::Count};
  static constexpr int kTapHistory = 12;  // «тыкают без остановки» — от 9 тапов за 3 с
  uint32_t tap_times_[kTapHistory] = {};
  bool fed_ = false;  // покормили (зарядили голодного) в fed_at_
  uint32_t fed_at_ = 0;
  bool vented_ = false;  // вспылил на тапы в vented_at_: потом неловко
  uint32_t vented_at_ = 0;
  uint32_t moved_at_ = 0;  // когда последний раз шёл или остановился
  uint32_t run_since_ = 0;  // бежит с этого момента
  float shake_level_ = 0;
  uint8_t recent_idle_[4] = {255, 255, 255, 255};
  const char* reason_ = "";
  uint32_t next_ = 0;

  Progress* progress_ = nullptr;
  struct Queued {
    Scene scene;
    SceneParams params;
    Expr after;
    const char* why;
    bool fresh;
  };
  Queued queue_[8];
  int queue_n_ = 0;
  int shared_n_ = 0;               // сколько общих сцен в начале очереди ещё не начато
  Scene cur_scene_ = Scene::None;  // сцена, которую запустила очередь
  bool cur_shared_ = false;        // и она общая
  bool after_fresh_ = false;
  uint32_t queue_at_ = 0;  // следующая сцена очереди — не раньше (после эмоции-развязки прошлой)
  Expr after_scene_ = Expr::Count;
  const char* after_why_ = "";
  uint32_t surprise_min_ = 24u * 3600000u, surprise_max_ = 60u * 3600000u;  // не больше недели
  uint32_t next_surprise_ = 0;
  uint16_t year_ = 0;
  uint8_t month_ = 0, day_ = 0, bday_m_ = 0, bday_d_ = 0;
  uint32_t holiday_done_ = 0;  // ключ даты, для которой праздник уже показан

  Voice* voice_ = nullptr;
  Friends* friends_ = nullptr;
  int battery_pct_ = -1;
  int eco_ = 0;  // 0 — нет, 1 — мало заряда, 2 — критично
  uint16_t eco_frame_ms_ = 0;  // период кадров движка до экорежима
  bool saver_done_ = false;    // заставка уже была на этой зарядке
  bool off_ = false;           // «выключен»: спит до касания, кнопки или зарядки
  uint8_t time_eggs_done_ = 0;
  uint16_t last_clock_ = 0;
  uint32_t knocks_[5] = {};
  uint32_t last_say_ = 0;
  uint32_t self_hash_ = 0;
  uint32_t chat_peer_ = 0;  // хэш соседа для сцены chat в очереди
  bool chat_both_ = false;
};

}  // namespace face
