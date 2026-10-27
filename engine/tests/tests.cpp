// Тесты движка без внешних зависимостей: ctest или ./face_tests.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "face/behavior.h"
#include "face/encounter.h"
#include "face/engine.h"
#include "face/friends.h"
#include "face/identity.h"
#include "face/progress.h"
#include "face/render.h"
#include "face/scenes.h"
#include "face/sensing.h"
#include "face/sound.h"
#include "face/text.h"

using namespace face;

static int g_fail = 0, g_checks = 0;

#define CHECK(cond, ...)                                      \
  do {                                                        \
    ++g_checks;                                               \
    if (!(cond)) {                                            \
      ++g_fail;                                               \
      printf("FAIL %s:%d: %s — ", __FILE__, __LINE__, #cond); \
      printf(__VA_ARGS__);                                    \
      printf("\n");                                           \
    }                                                         \
  } while (0)

static void settle(FaceEngine& eng, Expr e, uint32_t& t) {
  eng.set_expression(e, t, 0);
  for (int i = 0; i < 25; ++i) eng.update(t += 33);
}

static void test_framebuffer() {
  Framebuffer fb;
  fb.clear();
  fb.set(0, 0);
  fb.set(127, 63);
  fb.set(5, 9);
  CHECK(fb.data[0] == 0x01, "пиксель (0,0) — младший бит первой страницы");
  CHECK(fb.data[7 * kWidth + 127] == 0x80, "пиксель (127,63) — старший бит последней страницы");
  CHECK(fb.get(5, 9) && !fb.get(5, 8), "get/set");
  CHECK(fb.count_lit() == 3, "count_lit = %d", fb.count_lit());
  fb.set(-1, 0);
  fb.set(0, 64);
  CHECK(fb.count_lit() == 3, "запись за краем игнорируется");

  Framebuffer r = fb;
  r.rotate180();
  CHECK(r.get(127, 63) && r.get(0, 0) && r.get(122, 54), "поворот на 180°");
  r.rotate180();
  CHECK(r.equals(fb), "два поворота возвращают исходный кадр");

  Framebuffer a, b;
  a.clear();
  b.clear();
  DirtyWindow w[kPages];
  CHECK(diff_frames(a, b, w) == 0, "одинаковые кадры — нет окон");
  b.set(10, 3);
  b.set(40, 5);
  b.set(64, 30);
  int n = diff_frames(a, b, w);
  CHECK(n == 2, "окон %d", n);
  CHECK(w[0].page == 0 && w[0].x0 == 10 && w[0].x1 == 40, "окно страницы 0: %d..%d", w[0].x0, w[0].x1);
  CHECK(w[1].page == 3 && w[1].x0 == 64 && w[1].x1 == 64, "окно страницы 3");
  CHECK(dirty_bytes(w, n) == 32, "байт %d", dirty_bytes(w, n));
}

static void test_catalog() {
  CHECK(kExprCount >= 80, "эмоций %d", kExprCount);
  for (int i = 0; i < kExprCount; ++i) {
    const Expression& x = expression((Expr)i);
    CHECK(strcmp(x.id, expr_id((Expr)i)) == 0, "порядок пресетов: %s != %s", x.id, expr_id((Expr)i));
    CHECK(x.name && x.name[0], "у %s нет названия", x.id);
    CHECK(x.when && x.when[0], "у %s не описано, когда она нужна", x.id);
    CHECK((int)x.next < kExprCount, "%s: next вне каталога", x.id);
    Expr back;
    CHECK(expr_from_id(x.id, &back) && back == (Expr)i, "поиск по id %s", x.id);
  }
}

// Каждая эмоция рисует заметную картинку, которая влезает в экран и не заливает его.
static void test_every_expression_renders() {
  for (int i = 0; i < kExprCount; ++i) {
    FaceEngine eng;
    eng.options().drift = false;
    eng.options().idle_motion = false;
    eng.init(3, 0);
    uint32_t t = 0;
    settle(eng, (Expr)i, t);
    int lit = eng.frame().count_lit();
    const char* id = expr_id((Expr)i);
    CHECK(lit > 80, "%s: слишком мало пикселей (%d)", id, lit);
    CHECK(lit < kWidth * kHeight * 45 / 100, "%s: залито %d пикселей", id, lit);
  }
}

// Симметричные эмоции должны быть зеркальными: иначе лицо «косит».
// На живой панели 0.96″ эмоции у самого края и однопиксельные линии плохо читаются.
// Прогоняем каждую эмоцию 12 с со всей анимацией и дрейфом.
static void test_safe_area_and_strokes() {
  for (int i = 0; i < kExprCount; ++i) {
    const Expr e = (Expr)i;
    FaceEngine eng;
    eng.init(1234 + i, 0);
    eng.set_expression(e, 0, 0);
    int margin = 99;
    long thin = 0, lit = 0;
    for (uint32_t t = 0; t < 12000; t += 33) {
      eng.update(t);
      if (eng.expression() != e) eng.set_expression(e, t, 0);
      const Framebuffer& f = eng.frame();
      for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) {
          if (!f.get(x, y)) continue;
          const int m = imin(imin(x, kWidth - 1 - x), imin(y, kHeight - 1 - y));
          if (m < margin) margin = m;
          if (t < 1000) continue;
          ++lit;
          if ((!f.get(x - 1, y) && !f.get(x + 1, y)) || (!f.get(x, y - 1) && !f.get(x, y + 1))) ++thin;
        }
    }
    CHECK(margin >= kSafeMargin - 1, "%s: до края %d px", expression(e).id, margin);
    CHECK(lit == 0 || thin * 100 < lit * 10, "%s: однопиксельных пикселей %ld%%", expression(e).id,
          lit ? thin * 100 / lit : 0);
  }
}

static void test_symmetry() {
  const Expr sym[] = {Expr::neutral, Expr::happy,     Expr::joy,     Expr::laugh,  Expr::love,    Expr::sad,
                      Expr::angry,   Expr::surprised, Expr::shocked, Expr::sleepy, Expr::sleeping};
  for (Expr e : sym) {
    Face f;
    expression_face(e, &f);
    // Зрачки смотрят к носу — это тоже зеркально.
    Framebuffer fb;
    fb.clear();
    render_face(fb, f, nullptr);
    int mismatches = 0;
    for (int y = 0; y < kHeight; ++y)
      for (int x = 0; x < kWidth; ++x)
        if (fb.get(x, y) != fb.get(kWidth - 1 - x, y)) ++mismatches;
    CHECK(mismatches == 0, "%s не зеркальна: %d пикселей", expr_id(e), mismatches);
  }
}

// После перехода кадр совпадает с тем, что рисуется напрямую: переходы не оставляют следов.
static void test_transitions_settle() {
  for (int a = 0; a < kExprCount; a += 7) {
    for (int b = 0; b < kExprCount; b += 5) {
      FaceEngine direct, via;
      for (FaceEngine* e : {&direct, &via}) {
        e->options().drift = false;
        e->options().idle_motion = false;
        e->init(5, 0);
      }
      uint32_t t1 = 0, t2 = 0;
      settle(via, (Expr)a, t2);
      via.set_expression((Expr)b, t2);
      for (int i = 0; i < 60; ++i) via.update(t2 += 33);
      const Expression& xb = expression((Expr)b);
      if (xb.hold_ms) continue;                                // сама уходит в другую эмоцию
      if (xb.motion != Motion::None || xb.overlays) continue;  // кадр зависит от фазы анимации
      settle(direct, (Expr)b, t1);
      CHECK(direct.frame().equals(via.frame()), "%s -> %s: кадр после перехода отличается", expr_id((Expr)a),
            expr_id((Expr)b));
    }
  }
}

// Статичное лицо не требует кадров: движок засыпает до следующего события.
static void test_idle_scheduling() {
  FaceEngine eng;
  eng.options().drift = false;
  eng.init(9, 0);
  uint32_t t = 0;
  eng.set_expression(Expr::neutral, t, 0);
  eng.update(t);
  // Минута без внешних событий: только моргания и взгляды.
  uint32_t wakes = 0;
  while (t < 60000) {
    t = eng.next_update_ms();
    eng.update(t);
    ++wakes;
  }
  const EngineStats& st = eng.stats();
  CHECK(st.frames < 60 * 12, "слишком много кадров в покое: %u", st.frames);
  CHECK(wakes < 60 * 14, "слишком много пробуждений: %u", wakes);
  CHECK(st.bytes < 60 * 2500, "слишком много байт в покое: %u", st.bytes);
  printf("  покой, нейтральное лицо: %u кадров/мин, %u пробуждений/мин, %u байт/мин\n", st.frames, wakes, st.bytes);
}

static void test_determinism() {
  FaceEngine a, b;
  a.init(42, 0);
  b.init(42, 0);
  bool same = true;
  for (uint32_t t = 0; t < 20000; t += 33) {
    if (t == 3000) {
      a.set_expression(Expr::laugh, t);
      b.set_expression(Expr::laugh, t);
    }
    if (t == 9000) {
      a.play_clip(Clip::sneeze, t);
      b.play_clip(Clip::sneeze, t);
    }
    a.update(t);
    b.update(t);
    if (!a.frame().equals(b.frame())) same = false;
  }
  CHECK(same, "одинаковый seed даёт одинаковую анимацию");
}

static void test_clips_finish() {
  for (int c = 0; c < kClipCount; ++c) {
    FaceEngine eng;
    eng.options().drift = false;
    eng.init(11, 0);
    uint32_t t = 0;
    settle(eng, Expr::neutral, t);
    eng.play_clip((Clip)c, t);
    for (uint32_t k = 0; k < clip_duration_ms((Clip)c) + 200u; k += 33) eng.update(t += 33);
    CHECK(eng.clip() == Clip::None, "клип %s не закончился", clip_id((Clip)c));
  }
}

// Поведение: ключевые ситуации дают ожидаемые эмоции.
static void test_behavior_reactions() {
  FaceEngine eng;
  Behavior bh;
  eng.init(1, 0);
  bh.init(&eng, 1, 0);
  uint32_t t = 1000;
  auto run = [&](uint32_t ms) {
    for (uint32_t end = t + ms; t < end; t += 20) {
      bh.update(t);
      eng.update(t);
    }
  };
  run(2000);
  bh.on_event(Event::double_tap, t);
  run(100);
  auto in = [&](std::initializer_list<Expr> pool) {
    for (Expr x : pool)
      if (eng.expression() == x) return true;
    return false;
  };
  CHECK(in({Expr::laugh, Expr::happy, Expr::playful, Expr::giggle, Expr::wink, Expr::tears_of_joy}),
        "двойной тап -> радостная реакция, а не %s", expr_id(eng.expression()));
  run(8000);
  for (int i = 0; i < 4; ++i) {
    bh.on_event(Event::shake, t);
    run(200);
  }
  CHECK(in({Expr::dizzy, Expr::hypnotized, Expr::screaming, Expr::soy_scream}), "тряска -> головокружение, а не %s",
        expr_id(eng.expression()));
  run(15000);
  bh.on_event(Event::dark, t);
  run(100);
  CHECK(in({Expr::pocket_peek, Expr::afraid_dark}), "темнота -> «кто выключил свет?», а не %s",
        expr_id(eng.expression()));
  run(15000);
  CHECK(eng.expression() == Expr::sleeping, "в темноте засыпает, а не %s", expr_id(eng.expression()));
  bh.on_event(Event::light, t);
  run(100);
  CHECK(in({Expr::curious, Expr::waking, Expr::squint}), "свет будит, а не %s", expr_id(eng.expression()));
  run(10000);
  bh.on_event(Event::charge_start, t);
  run(12000);
  CHECK(eng.expression() == Expr::charging, "на зарядке -> charging, а не %s", expr_id(eng.expression()));
  bh.on_event(Event::charge_stop, t);
  run(12000);
  CHECK(eng.expression() != Expr::charging, "после отключения зарядки лицо меняется");
}

// Сутки со случайными событиями: никаких падений и разумная частота кадров.
static void test_behavior_day() {
  FaceEngine eng;
  Behavior bh;
  eng.init(77, 0);
  bh.init(&eng, 77, 0);
  Rng r;
  r.seed(123);
  uint32_t t = 0, wakes = 0;
  uint32_t next_event = 5000;
  const uint32_t day = 24u * 3600u * 1000u;
  while (t < day) {
    if (t >= next_event) {
      bh.on_event((Event)r.irange(0, kEventCount - 1), t);
      next_event = t + 30000u + r.next() % 600000u;
    }
    bh.set_clock((uint16_t)((t / 60000u) % 1440u));
    bh.update(t);
    eng.update(t);
    ++wakes;
    uint32_t n = eng.next_update_ms();
    if (bh.next_update_ms() < n) n = bh.next_update_ms();
    if (next_event < n) n = next_event;
    t = n > t ? n : t + 1;
  }
  const double fps = eng.stats().frames / (day / 1000.0);
  printf("  сутки с поведением: %.2f кадра/с в среднем, %.1f пробуждений/с, %.1f отрисовок/с, %.0f байт/с\n", fps,
         wakes / (day / 1000.0), eng.stats().renders / (day / 1000.0), eng.stats().bytes / (day / 1000.0));
  CHECK(fps < 12.0, "в среднем %.2f кадра/с", fps);
}

static void test_identity() {
  const Identity a = make_identity(123456789ull), b = make_identity(123456789ull), c = make_identity(123456790ull);
  CHECK(a.hash == b.hash && a.archetype == b.archetype && a.quirks.spacing == b.quirks.spacing,
        "один номер — один персонаж");
  CHECK(a.hash != c.hash, "соседние номера — разные персонажи");
  CHECK(a.code[2] == '-' && a.code[7] == 0, "позывной вида XX-XXXX: %s", a.code);
  int arch[(int)Archetype::Count] = {}, rare = 0, legend = 0;
  const int n = 20000;
  bool ranges = true;
  for (int i = 0; i < n; ++i) {
    const Identity id = make_identity((uint64_t)i * 7919ull + 17ull);
    ++arch[(int)id.archetype];
    rare += id.rarity == Rarity::Rare;
    legend += id.rarity == Rarity::Legendary;
    const Quirks& q = id.quirks;
    if (fabs_(q.spacing) > 4.0f || q.eye_scale < 0.94f || q.eye_scale > 1.06f || q.pupil_scale < 0.84f ||
        q.pupil_scale > 1.31f || q.blink_rate < 0.59f || q.blink_rate > 1.45f ||
        q.cyber_eye != (id.rarity == Rarity::Legendary))
      ranges = false;
  }
  CHECK(ranges, "особенности внешности в своих пределах");
  // Каждый характер встречается не реже, чем в его доле весов, с запасом.
  float wsum = 0;
  for (int i = 0; i < kArchetypeCount; ++i) wsum += temperament((Archetype)i).weight;
  for (int i = 0; i < kArchetypeCount; ++i) {
    const float share = temperament((Archetype)i).weight / wsum;
    CHECK(arch[i] > (int)(n * share * 0.8f) && arch[i] < (int)(n * share * 1.2f), "архетип %s встречается: %d",
          archetype_id((Archetype)i), arch[i]);
  }
  // По хэшу из рекламного пакета сосед узнаёт тот же характер и позывной.
  const Identity peer = identity_from_hash(a.hash);
  CHECK(peer.archetype == a.archetype && !strcmp(peer.code, a.code) && peer.rarity == a.rarity,
        "характер соседа по хэшу совпадает");
  CHECK(legend > n / 200 && legend < n / 60, "легендарных около 1%%: %d из %d", legend, n);
  CHECK(rare > n / 14 && rare < n / 8, "редких около 10%%: %d из %d", rare, n);
}

static void test_progress() {
  Progress p;
  p.reset();
  const int base = p.unlocked_count();
  CHECK(base >= 38 && base <= 50, "на старте открыто около 40 эмоций: %d", base);
  CHECK(p.is_unlocked(Expr::sleeping) && p.is_unlocked(Expr::charging) && p.is_unlocked(Expr::low_battery),
        "без сна, зарядки и батареи персонаж не живёт");
  for (int i = 1; i < implant_count(); ++i) CHECK(implant(i).xp > implant(i - 1).xp, "импланты по возрастанию опыта");
  // Каждая эмоция ровно в одном импланте: по мере роста открывается ровно то, что в нём числится.
  int total = base;
  for (int i = 1; i < implant_count(); ++i) {
    const int before = p.unlocked_count();
    const int got = p.add_xp(implant(i).xp - p.xp);
    int listed = 0;
    for (int e = 0; e < kExprCount; ++e) listed += implant_of((Expr)e) == i;
    CHECK(got == i, "имплант %d установился (%d)", i, got);
    CHECK(p.unlocked_count() - before == listed, "имплант %s открыл %d из %d", implant(i).tag,
          p.unlocked_count() - before, listed);
    total += listed;
  }
  CHECK(total == kExprCount && p.unlocked_count() == kExprCount, "все %d эмоции открываются: %d", kExprCount,
        p.unlocked_count());
  CHECK(p.xp_to_next() == 0 && p.add_xp(1000) == -1, "после последнего импланта ставить нечего");

  // Касания и шаги ограничены за день.
  Progress q;
  q.reset();
  for (int i = 0; i < 500; ++i) q.touch();
  q.add_steps(100000);
  const XpRules r;
  CHECK(q.xp == (uint32_t)(r.touch_day_cap + r.steps_day_cap), "дневной предел: %u", q.xp);
  q.new_day();
  q.touch();
  CHECK(q.xp == (uint32_t)(r.touch_day_cap + r.steps_day_cap + r.day + r.touch), "новый день снимает предел");
  // Обычный день ≈ день + касания + шаги: полный набор — за несколько недель.
  const uint32_t per_day = r.day + r.touch_day_cap + r.steps_day_cap;
  const uint32_t days = implant(implant_count() - 1).xp / per_day;
  CHECK(days >= 21 && days <= 60, "всё открывается за %u дней активной носки", days);

  // Флеш: контрольная сумма ловит порчу.
  q.seal();
  CHECK(q.valid(), "целая запись проходит проверку");
  Progress broken = q;
  broken.xp ^= 1;
  CHECK(!broken.valid(), "испорченная запись не проходит");

  // Дуэль: досрочно открывается эмоция из ближайшего импланта.
  Progress d;
  d.reset();
  Rng rng;
  rng.seed(3);
  const Expr e = d.steal_one(rng);
  CHECK(e != Expr::Count && d.is_unlocked(e) && implant_of(e) == 1, "выигранная эмоция — из следующего импланта");
}

static void test_scenes() {
  for (int i = 0; i < kSceneCount; ++i) {
    const Scene s = (Scene)i;
    Scene back;
    CHECK(scene_from_id(scene_id(s), &back) && back == s, "id сцены %s", scene_id(s));
    FaceEngine eng;
    eng.init(5, 0);
    SceneParams p;
    p.set_text("K7-3F2A");
    eng.play_scene(s, 0, p);
    int margin = 99, frames = 0;
    uint32_t t = 0;
    for (; t < 20000 && eng.scene() != Scene::None; t += 20) {
      if (eng.update(t)) ++frames;
      const Framebuffer& f = eng.frame();
      for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
          if (f.get(x, y)) margin = imin(margin, imin(imin(x, kWidth - 1 - x), imin(y, kHeight - 1 - y)));
    }
    CHECK(eng.scene() == Scene::None && t <= scene_duration_ms(s) + 40u, "сцена %s заканчивается сама", scene_id(s));
    CHECK(frames > 3, "сцена %s анимирована: %d кадров", scene_id(s), frames);
    // В прятках лицо уходит за нижний край — так задумано.
    if (s != Scene::hide) CHECK(margin >= kSafeMargin - 1, "сцена %s: до края %d px", scene_id(s), margin);
  }
  CHECK(text_width("ABC") == 22 && text_height() == 10, "размер надписи");
}

static void test_cyber_theme_and_tilt() {
  // Кибер-тема и кибер-глаз не выводят эмоции за безопасное поле.
  Quirks q;
  q.cyber_eye = true;
  for (int i = 0; i < kExprCount; ++i) {
    const Expr e = (Expr)i;
    FaceEngine eng;
    eng.options().drift = false;
    eng.options().theme = Theme::Cyber;
    eng.init(77 + i, 0);
    eng.set_quirks(q, 0);
    eng.set_expression(e, 0, 0);
    int margin = 99;
    for (uint32_t t = 0; t < 6000; t += 50) {
      eng.update(t);
      if (eng.expression() != e) eng.set_expression(e, t, 0);
      const Framebuffer& f = eng.frame();
      for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x)
          if (f.get(x, y)) margin = imin(margin, imin(imin(x, kWidth - 1 - x), imin(y, kHeight - 1 - y)));
    }
    CHECK(margin >= kSafeMargin - 1, "кибер-тема, %s: до края %d px", expression(e).id, margin);
  }
  // Наклон: зрачки уходят в нижнюю сторону.
  FaceEngine eng;
  eng.options().idle_motion = false;
  eng.init(1, 0);
  eng.set_tilt(1.0f, 0.0f);
  for (uint32_t t = 0; t < 1000; t += 33) eng.update(t);
  CHECK(eng.face().gx > 0.6f, "наклон вправо — взгляд вправо: %.2f", eng.face().gx);
  eng.set_tilt(0.0f, 0.0f);
  for (uint32_t t = 1000; t < 2500; t += 33) eng.update(t);
  CHECK(fabs_(eng.face().gx) < 0.1f, "выровняли — взгляд вернулся: %.2f", eng.face().gx);
}

// Прицел кибер-темы: только на «цепких» эмоциях, сам гаснет, в классике его нет.
static void test_reticle() {
  for (int theme = 0; theme < 2; ++theme) {
    FaceEngine eng;
    eng.options().theme = theme ? Theme::Cyber : Theme::Classic;
    eng.init(5, 0);
    bool seen = false;
    for (uint32_t t = 0; t < 5000; t += 50) {
      eng.update(t);
      seen |= eng.reticle_visible();
    }
    CHECK(!seen, "тема %d: на спокойном лице прицела нет", theme);
    eng.set_expression(Expr::focused, 5000, 0);
    eng.update(5000);
    eng.update(5400);
    CHECK(eng.reticle_visible() == (theme == 1), "тема %d: сосредоточенность берёт цель", theme);
    eng.update(5000 + FaceEngine::kReticleMs + 100);
    CHECK(!eng.reticle_visible(), "тема %d: прицел гаснет сам", theme);
  }
  // Изредка сканирует и без повода: раз в 3–8 минут.
  FaceEngine eng;
  eng.options().theme = Theme::Cyber;
  eng.init(6, 0);
  int scans = 0;
  uint32_t last_seen = 0;
  for (uint32_t t = 0; t < 30u * 60000u; t += 50) {
    eng.update(t);
    if (!eng.reticle_visible()) continue;
    if (!scans || t - last_seen > 1000u) ++scans;  // мигание в конце — тот же скан
    last_seen = t;
  }
  CHECK(scans >= 3 && scans <= 10, "за полчаса %d сканирований", scans);
}

// Кибер-глаз: правый зрачок квадратный, левый — круглый.
static void test_cyber_pupil() {
  Face f;
  expression_face(Expr::neutral, &f);
  Framebuffer a, b;
  a.clear(), b.clear();
  render_face(a, f, nullptr);
  f.eye[1].sq = true;
  render_face(b, f, nullptr);
  int left_diff = 0, right_diff = 0;
  for (int y = 0; y < kHeight; ++y)
    for (int x = 0; x < kWidth; ++x) (x < kWidth / 2 ? left_diff : right_diff) += a.get(x, y) != b.get(x, y);
  CHECK(right_diff > 8, "правый зрачок другой формы: %d пикселей", right_diff);
  CHECK(left_diff == 0, "левый глаз не меняется: %d пикселей", left_diff);
}

// Характеры заметно разные: у каждого своё лицо в покое и свои ответы на касания,
// даже у нового брелока, у которого открыта только база.
static void test_temperaments() {
  static float hist[kArchetypeCount][kExprCount];
  for (int a = 0; a < kArchetypeCount; ++a) {
    const Temperament& t = temperament((Archetype)a);
    FaceEngine eng;
    Behavior bh;
    Progress p;
    p.reset();
    eng.init(40 + a, 0);
    bh.init(&eng, 40 + a, 0);
    bh.attach_progress(&p);
    bh.set_temperament(&t);
    bh.set_surprise_interval(7u * 24u * 3600000u, 7u * 24u * 3600000u, 0);
    int rest_ms = 0, tap_hits = 0, taps = 0;
    for (uint32_t ms = 0; ms < 3u * 3600000u; ms += 500) {
      bh.set_clock((uint16_t)(10 * 60 + ms / 60000u));
      if (ms % 300000u == 150000u) {
        bh.on_event(Event::tap, ms);
        ++taps;
        bh.update(ms);
        eng.update(ms);
        for (Expr x : t.tap) tap_hits += eng.expression() == x;
      }
      bh.update(ms);
      eng.update(ms);
      hist[a][(int)eng.expression()] += 0.5f;
      rest_ms += eng.expression() == t.rest ? 500 : 0;
    }
    CHECK(rest_ms > 3600000 * 3 / 5, "%s: лицо в покое (%s) — %d%% времени", t.tag, expression(t.rest).id,
          rest_ms / 108000);
    CHECK(tap_hits * 10 >= taps * 6, "%s: касание отвечает по-своему %d из %d", t.tag, tap_hits, taps);
  }
  // Попарно: распределения эмоций пересекаются меньше чем наполовину.
  int same = 0;
  for (int a = 0; a < kArchetypeCount; ++a)
    for (int b = a + 1; b < kArchetypeCount; ++b) {
      float inter = 0, total = 0;
      for (int e = 0; e < kExprCount; ++e) inter += fmin_(hist[a][e], hist[b][e]), total += hist[a][e];
      if (inter > total * 0.5f) ++same;
    }
  CHECK(same == 0, "похожих пар характеров: %d", same);
}

// Встреча зависит от пары характеров: близнецы радуются, скромник смущается, ворчун ворчит.
static uint32_t hash_of(Archetype a) {
  for (uint32_t h = 1;; ++h)
    if (archetype_from_hash(h) == a) return h;
}

static void test_meet_pairs() {
  struct Case {
    Archetype me, other;
    Expr expect[3];
    const char* sub;
  };
  const Case cases[] = {
      {Archetype::shy, Archetype::grumpy, {Expr::flustered, Expr::embarrassed, Expr::shy}, "GRUMPY"},
      {Archetype::playful, Archetype::playful, {Expr::pog, Expr::excited, Expr::happy}, "TWIN!"},
      {Archetype::grumpy, Archetype::playful, {Expr::unamused, Expr::annoyed, Expr::Count}, "PLAYFUL"},
      {Archetype::sweet, Archetype::chill, {Expr::love, Expr::adoring, Expr::Count}, "CHILL"},
  };
  for (const Case& c : cases) {
    FaceEngine eng;
    Behavior bh;
    eng.init(3, 0);
    bh.init(&eng, 3, 0);
    bh.set_temperament(&temperament(c.me));
    bh.meet("AB-CDEF", true, 1000, hash_of(c.other));
    bool sub_ok = false, after_ok = false;
    for (uint32_t t = 1000; t < 9000; t += 50) {
      bh.update(t);
      eng.update(t);
      if (eng.scene() == Scene::greet) sub_ok |= !strcmp(eng.scene_params().sub, c.sub);
      if (eng.scene() == Scene::None && t > 4500)
        for (Expr x : c.expect) after_ok |= x == eng.expression();
    }
    CHECK(sub_ok, "%s встречает %s: подпись %s", temperament(c.me).tag, temperament(c.other).tag, c.sub);
    CHECK(after_ok, "%s встречает %s: эмоция после — %s", temperament(c.me).tag, temperament(c.other).tag,
          expression(eng.expression()).id);
  }
}

static void test_growth_behavior() {
  // С новым прогрессом персонаж сутки показывает только открытые эмоции.
  FaceEngine eng;
  Behavior bh;
  Progress p;
  p.reset();
  eng.init(9, 0);
  bh.init(&eng, 9, 0);
  bh.attach_progress(&p);
  bh.set_surprise_interval(3600000u, 7200000u, 0);
  bool ok = true;
  Expr bad = Expr::Count;
  int scenes = 0;
  Scene last = Scene::None;
  for (uint32_t t = 0; t < 24u * 3600000u; t += 1000) {
    bh.set_clock((uint16_t)((t / 60000u + 12 * 60) % 1440));
    if (t % 600000u == 0) bh.on_event(Event::tap, t);
    bh.update(t);
    eng.update(t);
    if (eng.scene() != last && eng.scene() != Scene::None) ++scenes;
    last = eng.scene();
    if (eng.scene() == Scene::None && !p.is_unlocked(eng.expression())) ok = false, bad = eng.expression();
  }
  CHECK(ok, "показана закрытая эмоция: %s", bad == Expr::Count ? "-" : expression(bad).id);
  CHECK(scenes >= 5, "за сутки были сюрпризы и апгрейды: %d сцен", scenes);
  CHECK(p.touches > 100 && p.xp > 0, "касания дают опыт: %u", p.xp);

  // Встреча и дуэль.
  bh.meet("K7-3F2A", true, 100000000u);
  eng.update(100000000u);
  CHECK(eng.scene() == Scene::greet, "встреча — сцена приветствия");
  const int before = p.unlocked_count();
  uint32_t t = 100000000u;
  for (; t < 100060000u; t += 50) {
    bh.update(t);
    eng.update(t);
  }
  bh.duel("K7-3F2A", true, t);
  for (uint32_t end = t + 30000u; t < end; t += 50) {
    bh.update(t);
    eng.update(t);
  }
  CHECK(p.unlocked_count() > before, "победа в дуэли открыла эмоцию: %d → %d", before, p.unlocked_count());
  CHECK(p.duels_won == 1 && p.friends == 1, "счётчики встреч и дуэлей");

  // Праздник — один раз за дату.
  FaceEngine e2;
  Behavior b2;
  e2.init(4, 0);
  b2.init(&e2, 4, 0);
  b2.set_date(2026, 12, 31);
  int ny = 0;
  Scene prev = Scene::None;
  for (uint32_t tt = 0; tt < 120000; tt += 100) {
    b2.update(tt);
    e2.update(tt);
    if (e2.scene() == Scene::new_year && prev != Scene::new_year) ++ny;
    prev = e2.scene();
  }
  CHECK(ny == 1, "Новый год показан ровно раз: %d", ny);
  b2.set_birthday(5, 14);
  b2.set_date(2027, 5, 14);
  bool bday = false;
  for (uint32_t tt = 120000; tt < 140000; tt += 100) {
    b2.update(tt);
    e2.update(tt);
    bday |= e2.scene() == Scene::birthday;
  }
  CHECK(bday, "день рождения хозяина");
}

// ---------------------------------------------------------------- время, батарея, устойчивость

// Цикл как в прошивке: опрос раз в 1 мс, а отправка изменённых окон по I2C на 400 кГц
// занимает ~23 мкс на байт плюс команды окна. Время 64-битное, в движок идёт uint32.
struct FirmwareLoop {
  FaceEngine* eng;
  Behavior* bh;
  uint64_t t;
  uint64_t last_eng = 0, last_bh = 0, max_eng_gap = 0, max_bh_gap = 0;
  uint32_t eng_updates = 0, bh_updates = 0, frames = 0, max_ahead = 0;
  void reset_stats() {
    last_eng = last_bh = t;
    max_eng_gap = max_bh_gap = 0;
    eng_updates = bh_updates = frames = 0;
  }
  void step() {
    const uint32_t now = (uint32_t)t;
    uint32_t cost_us = 1000;
    if (bh && time_reached(now, bh->next_update_ms())) {
      bh->update(now);
      ++bh_updates;
      if (t - last_bh > max_bh_gap) max_bh_gap = t - last_bh;
      last_bh = t;
      max_ahead = imax((int)max_ahead, (int)time_left(now, bh->next_update_ms()));
    }
    if (time_reached(now, eng->next_update_ms())) {
      if (eng->update(now)) {
        DirtyWindow w[kPages];
        const int n = eng->dirty(w);
        cost_us += (uint32_t)dirty_bytes(w, n) * 23u + (uint32_t)n * 300u;
        ++frames;
      }
      ++eng_updates;
      if (t - last_eng > max_eng_gap) max_eng_gap = t - last_eng;
      last_eng = t;
      max_ahead = imax((int)max_ahead, (int)time_left(now, eng->next_update_ms()));
    }
    t += (cost_us + 999u) / 1000u;
  }
  void run_until(uint64_t end) {
    while (t < end) step();
  }
};

// millis() переполняется раз в 49.7 суток. Лицо и характер живут дальше во всех состояниях:
// не замирают, не крутятся вхолостую по 1 мс перед переполнением и не засыпают на полтора месяца.
static void test_millis_wrap() {
  const uint64_t W = 1ull << 32;
  struct Case {
    const char* name;
    Event ev;
    bool manual;
  };
  const Case cases[] = {{"ручной режим", Event::Count, true},
                        {"на зарядке", Event::charge_start, false},
                        {"во сне в темноте", Event::dark, false},
                        {"бодрствует", Event::Count, false}};
  for (const Case& c : cases) {
    for (uint32_t phase = 0; phase < 4; ++phase) {
      FaceEngine eng;
      Behavior bh;
      const uint64_t t0 = W - 150000u - phase * 7919u;
      eng.init(11 + phase, (uint32_t)t0);
      bh.init(&eng, 11 + phase, (uint32_t)t0);
      bh.set_clock(c.ev == Event::dark ? 23 * 60 : 12 * 60);
      if (c.manual) {
        bh.set_auto(false);
        eng.set_expression(Expr::neutral, (uint32_t)t0);
      }
      if (c.ev != Event::Count) bh.on_event(c.ev, (uint32_t)t0);
      FirmwareLoop lp{&eng, &bh, t0};
      lp.run_until(W - 30000u);
      lp.reset_stats();
      lp.run_until(W);
      const uint32_t before = lp.eng_updates;
      lp.reset_stats();
      lp.run_until(W + 180000u);
      CHECK(before < 2000, "%s, фаза %u: перед переполнением %u обновлений за 30 с — холостой цикл", c.name, phase,
            before);
      CHECK(lp.max_eng_gap <= kMaxSleepMs + kWakeMergeMs + 50, "%s, фаза %u: лицо замерло на %.1f с после переполнения",
            c.name, phase, lp.max_eng_gap / 1000.0);
      CHECK(lp.frames >= 5, "%s, фаза %u: после переполнения всего %u кадров", c.name, phase, lp.frames);
      CHECK(lp.bh_updates >= 3 && lp.max_bh_gap <= kMaxSleepMs + 50,
            "%s, фаза %u: характер замер (%u обновлений, пауза %.1f с)", c.name, phase, lp.bh_updates,
            lp.max_bh_gap / 1000.0);
      CHECK(lp.max_ahead <= kMaxSleepMs + kWakeMergeMs, "%s, фаза %u: расписание на %u мс вперёд", c.name, phase,
            lp.max_ahead);
    }
  }

  // Сюрпризы рядом с переполнением идут по расписанию, а не подряд.
  FaceEngine eng;
  Behavior bh;
  const uint64_t t0 = W - 3u * 3600000u;
  eng.init(5, (uint32_t)t0);
  bh.init(&eng, 5, (uint32_t)t0);
  bh.set_surprise_interval(3600000u, 7200000u, (uint32_t)t0);
  int scenes = 0;
  Scene last = Scene::None;
  for (uint64_t t = t0; t < W + 3600000u;) {
    const uint32_t now = (uint32_t)t;
    bh.set_clock(12 * 60);
    if (time_reached(now, bh.next_update_ms())) bh.update(now);
    if (time_reached(now, eng.next_update_ms())) eng.update(now);
    if (eng.scene() != last && eng.scene() != Scene::None) ++scenes;
    last = eng.scene();
    const uint32_t wait = imin((int)time_left(now, bh.next_update_ms()), (int)time_left(now, eng.next_update_ms()));
    t += wait ? wait : 1;
  }
  CHECK(scenes >= 1 && scenes <= 4, "за 4 часа у переполнения %d сюрпризов (раз в 1–2 часа)", scenes);
}

static int lit_now(const FaceEngine& eng) { return eng.frame().count_lit(); }

// Чих, испуг и «тык» не отменяют моргание навсегда, даже если начались посреди него.
static void test_blink_survives_clips() {
  const Clip clips[] = {Clip::sneeze, Clip::startle, Clip::boop};
  int lost = 0, total = 0;
  for (uint32_t seed = 1; seed <= 30; ++seed) {
    for (Clip c : clips) {
      FaceEngine eng;
      eng.options().drift = false;
      eng.init(seed, 0);
      uint32_t t = 0;
      eng.set_expression(Expr::neutral, t, 0);
      eng.update(t);
      // Открытые глаза — самый «светлый» кадр последних секунд: в момент клипа
      // лицо может как раз моргать.
      int open = 0;
      while (t < 8000u + seed * 37u) {
        t = eng.next_update_ms();
        eng.update(t);
        if (t + 3000u > 8000u + seed * 37u) open = imax(open, lit_now(eng));
      }
      eng.play_clip(c, t);
      const uint32_t clip_end = t + clip_duration_ms(c) + 100u;
      bool blinked = false;
      while (t < clip_end + 20000u && !blinked) {
        t = eng.next_update_ms();
        eng.update(t);
        if (t > clip_end && lit_now(eng) < open * 6 / 10) blinked = true;
      }
      ++total;
      lost += !blinked;
    }
  }
  CHECK(lost == 0, "после клипа моргание пропало в %d случаях из %d", lost, total);
}

// NaN и бесконечности снаружи не ломают лицо: ни взгляд, ни наклон, ни внешность.
static void test_nonfinite_inputs() {
  const float nan = __builtin_nanf(""), inf = __builtin_inff();
  FaceEngine eng;
  eng.init(3, 0);
  uint32_t t = 0;
  settle(eng, Expr::neutral, t);
  auto run = [&](uint32_t ms) {
    for (const uint32_t end = t + ms; t < end; t += 33) eng.update(t);
  };
  eng.jerk(inf, 0.0f, t);
  eng.jerk(-inf, 1.0f, t);
  run(1500);
  CHECK(lit_now(eng) > 80 && finitef(eng.face().gx), "рывок с бесконечностью: %d пикселей", lit_now(eng));
  eng.set_tilt(nan, 0.5f);
  run(1500);
  CHECK(lit_now(eng) > 80 && finitef(eng.face().gx) && finitef(eng.face().gy), "наклон с NaN: %d пикселей",
        lit_now(eng));
  eng.look_at(nan, inf, t, 1000);
  run(1500);
  CHECK(lit_now(eng) > 80 && finitef(eng.face().gx), "взгляд с NaN");
  Quirks q;
  q.spacing = nan, q.eye_scale = inf, q.pupil_scale = -inf, q.blink_rate = nan, q.brow_lift = nan;
  eng.set_quirks(q, t);
  run(3000);
  CHECK(lit_now(eng) > 80 && finitef(eng.quirks().eye_scale) && finitef(eng.quirks().blink_rate), "внешность с NaN");
  eng.set_tilt(0.8f, 0.0f);
  run(1500);
  CHECK(eng.face().gx > 0.5f, "после NaN наклон снова работает: %.2f", eng.face().gx);

  Behavior bh;
  bh.init(&eng, 3, t);
  bh.personality().playful = nan;
  bh.personality().sleepy = inf;
  for (int i = 0; i < 20; ++i) {
    bh.on_event(Event::tap, t);
    bh.update(t);
    run(500);
  }
  CHECK(finitef(bh.mood().energy) && finitef(bh.personality().playful) && lit_now(eng) > 80, "характер с NaN");
}

static uint32_t tilt_renders_per_min(bool feed, float noise) {
  FaceEngine eng;
  eng.init(3, 0);
  uint32_t t = 0;
  eng.set_expression(Expr::neutral, t, 0);
  eng.update(t);
  Rng r;
  r.seed(99);
  uint32_t next_imu = 0;
  uint32_t renders0 = 0;
  bool started = false;
  while (t < 65000) {
    if (!started && t >= 5000) started = true, renders0 = eng.stats().renders;
    if (feed && time_reached(t, next_imu)) {
      eng.set_tilt(0.4f + r.range(-noise, noise), 0.3f + r.range(-noise, noise));
      next_imu = t + 20;  // акселерометр 50 Гц
    }
    if (time_reached(t, eng.next_update_ms())) eng.update(t);
    uint32_t n = imin((int)time_left(t, eng.next_update_ms()), feed ? (int)time_left(t, next_imu) : 1 << 30);
    t += n ? n : 1;
  }
  return eng.stats().renders - renders0;
}

// Шум акселерометра не будит движок и не дёргает зрачки, а настоящий наклон работает.
static void test_tilt_noise() {
  const uint32_t still = tilt_renders_per_min(false, 0.0f), noisy = tilt_renders_per_min(true, 0.01f);
  CHECK(noisy <= still * 13 / 10 + 30, "наклон с шумом 50 Гц: %u отрисовок/мин против %u без него", noisy, still);
  printf("  наклон с шумом ±0.01 на 50 Гц: %u отрисовок/мин, без наклона %u\n", noisy, still);

  // Рывок в ту же сторону только продлевает взгляд, не будит движок заново.
  FaceEngine eng;
  eng.init(3, 0);
  uint32_t t = 0;
  settle(eng, Expr::neutral, t);
  eng.jerk(1.0f, 0.0f, t);
  for (int i = 0; i < 5; ++i) eng.update(t += 33);
  const uint32_t next = eng.next_update_ms();
  eng.jerk(1.0f, 0.02f, t + 10);
  CHECK(eng.next_update_ms() == next, "повторный рывок в ту же сторону разбудил движок");
}

static uint32_t wakes_per_min(Expr e, uint32_t* renders = nullptr, uint32_t* frames = nullptr) {
  FaceEngine eng;
  eng.init(7, 0);
  uint32_t t = 0;
  eng.set_expression(e, t, 0);
  eng.update(t);
  while (t < 5000) {
    t = eng.next_update_ms();
    eng.update(t);
  }
  const EngineStats s0 = eng.stats();
  uint32_t wakes = 0;
  while (t < 65000) {
    t = eng.next_update_ms();
    eng.update(t);
    ++wakes;
  }
  if (renders) *renders = eng.stats().renders - s0.renders;
  if (frames) *frames = eng.stats().frames - s0.frames;
  return wakes;
}

// Дрожь от холода — приступами 1 с из 4, и между приступами процессор спит.
static void test_shiver_wakes() {
  const uint32_t w = wakes_per_min(Expr::cold);
  CHECK(w < 900, "холод: %u пробуждений в минуту", w);
}

// Значки будят процессор, только когда меняется их картинка.
static void test_overlay_clocks() {
  uint32_t w = 0;
  CHECK(overlay_clock(Overlay::Ellipsis, 300, &w) == 289 && w == 577 - 300, "многоточие: шаги по точкам");
  CHECK(overlay_clock(Overlay::BatteryLow, 10, &w) == 1 && w == 771, "батарейка мигает дважды за цикл");
  CHECK(overlay_clock(Overlay::Exclaim, 700, &w) == 600 && w == 0, "«!» замирает после встряски");
  CHECK(overlay_clock(Overlay::Sweat, 12345, &w) == 0 && w == 0, "капля пота статична");
  CHECK(overlay_clock(Overlay::Zzz, 30u * 86400000u + 123u, &w) < 3200, "время Zzz не растёт дальше цикла");
  uint32_t r_low = 0, r_bored = 0, r_listen = 0;
  wakes_per_min(Expr::low_battery, &r_low);
  wakes_per_min(Expr::bored, &r_bored);
  wakes_per_min(Expr::listening, &r_listen);
  CHECK(r_low < 520, "мало заряда: %u отрисовок/мин", r_low);
  CHECK(r_bored < 650, "скука: %u отрисовок/мин", r_bored);
  CHECK(r_listen < 800, "слушает: %u отрисовок/мин", r_listen);
}

static uint32_t crc32_ref(const uint8_t* p, uint32_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < n; ++i) {
    c ^= p[i];
    for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

// Запись роста не зависит от порядка каталога и переносится со старой версии.
static void test_progress_format() {
  bool keys_ok = true;
  for (int i = 0; i < kExprCount; ++i) {
    if (!Progress::key((Expr)i)) keys_ok = false;
    for (int j = 0; j < i; ++j)
      if (Progress::key((Expr)i) == Progress::key((Expr)j)) keys_ok = false;
  }
  CHECK(keys_ok, "ключи эмоций разные и не нулевые");

  // Дуэль кладёт эмоцию в список дополнительных, имплант потом освобождает место.
  Progress p;
  p.reset();
  Rng rng;
  rng.seed(7);
  const Expr won = p.steal_one(rng);
  CHECK(won != Expr::Count && p.is_unlocked(won) && p.extra_count() == 1, "выигранная эмоция в списке дополнительных");
  p.add_xp(implant(1).xp);
  CHECK(p.is_unlocked(won) && p.extra_count() == 0, "имплант открыл её и освободил место");
  int filled = 0;
  for (int i = 0; i < kExprCount; ++i)
    if (!p.is_unlocked((Expr)i) && p.unlock((Expr)i)) ++filled;
  CHECK(filled == Progress::kExtraSlots && p.steal_one(rng) == Expr::Count, "в список влезает %d, не больше", filled);
  Progress all;
  all.reset();
  all.unlock_all();
  CHECK(all.unlocked_count() == kExprCount, "«открыть всё» — флагом");

  // Своя версия: запись → чтение без потерь; порча и чужая версия не проходят.
  p.seal();
  Progress back;
  CHECK(back.load(&p, sizeof p) && memcmp(&back, &p, sizeof p) == 0, "запись читается без потерь");
  Progress bad = p;
  bad.xp ^= 1;
  CHECK(!back.load(&bad, sizeof bad), "испорченная запись не читается");
  Progress future = p;
  future.version = Progress::kVersion + 1;
  future.seal();
  CHECK(!back.load(&future, sizeof future), "запись более новой версии не читается");

  // Версия 1: битовая карта по номерам каталога. Сверх имплантов открыто «рыдание» — выиграно в дуэли.
  uint8_t v1[56] = {};
  auto put16 = [&](int o, uint32_t v) { v1[o] = (uint8_t)v, v1[o + 1] = (uint8_t)(v >> 8); };
  auto put32 = [&](int o, uint32_t v) { put16(o, v & 0xFFFF), put16(o + 2, v >> 16); };
  put32(0, Progress::kMagic), put16(4, 1), put16(6, 17), put32(8, 700), put32(12, 321), put32(16, 54321);
  put16(20, 3), put16(22, 2), put16(24, 1), put16(26, 8), put16(28, 10), put16(30, 42), v1[32] = 3;
  for (int i = 0; i < kExprCount; ++i)
    if (implant_of((Expr)i) <= 3) v1[34 + i / 8] |= (uint8_t)(1u << (i % 8));
  v1[34 + (int)Expr::sobbing / 8] |= (uint8_t)(1u << ((int)Expr::sobbing % 8));
  put32(52, crc32_ref(v1, 52));
  Progress m;
  CHECK(m.load(v1, sizeof v1) && m.valid() && m.version == Progress::kVersion, "запись версии 1 переносится");
  CHECK(m.days == 17 && m.xp == 700 && m.touches == 321 && m.steps == 54321 && m.friends == 3 && m.implant == 3 &&
            m.step_rest == 42,
        "поля версии 1 на месте");
  CHECK(m.is_unlocked(Expr::sobbing) && !m.is_unlocked(Expr::furious) && m.extra_count() == 1,
        "выигранная в версии 1 эмоция осталась открытой");
  v1[10] ^= 1;
  CHECK(!m.load(v1, sizeof v1), "испорченная запись версии 1 не читается");
}

// Цепочка движка «смех → радость» доигрывает: радость видна, характер её не перебивает.
// А цепочка, которая замкнулась на себя (закрытая эмоция подменилась той же), не вечна.
static void test_chain_follow_up() {
  FaceEngine eng;
  Behavior bh;
  eng.init(9, 0);
  bh.init(&eng, 9, 0);
  uint64_t t = 1000;
  FirmwareLoop lp{&eng, &bh, t};
  lp.run_until(20000);
  bh.queue_scene(Scene::boot, SceneParams{}, Expr::laugh, (uint32_t)lp.t, "тест");
  bool laughed = false;
  uint32_t happy_ms = 0;
  for (const uint64_t end = lp.t + 16000; lp.t < end;) {
    const uint64_t before = lp.t;
    lp.step();
    if (eng.scene() != Scene::None) continue;
    if (eng.expression() == Expr::laugh) laughed = true;
    if (laughed && eng.expression() == Expr::happy) happy_ms += (uint32_t)(lp.t - before);
  }
  CHECK(laughed && happy_ms >= 2000, "после смеха радость была видна %u мс", happy_ms);

  Progress fresh;
  fresh.reset();
  bh.attach_progress(&fresh);
  CHECK(!fresh.is_unlocked(Expr::adoring) && bh.allowed(Expr::adoring) == Expr::love,
        "love → adoring замыкается на love");
  bh.queue_scene(Scene::boot, SceneParams{}, Expr::love, (uint32_t)lp.t, "тест");
  lp.run_until(lp.t + 6000);
  bool left = false;
  for (const uint64_t end = lp.t + 40000; lp.t < end; lp.step()) left |= eng.expression() != Expr::love;
  CHECK(left, "замкнутая цепочка love держится вечно");
}

// Значок появляется с «пружинкой», даже если лицо перед этим долго стояло.
static void test_overlay_pop_after_idle() {
  FaceEngine eng;
  eng.options().drift = false;
  eng.options().idle_motion = false;
  eng.init(2, 0);
  eng.set_expression(Expr::neutral, 0, 0);
  eng.update(0);
  eng.set_expression(Expr::nervous, 3000, 0);  // капля пота
  eng.update(3000);
  const int first = lit_now(eng);
  for (uint32_t t = 3033; t < 3500; t += 33) eng.update(t);
  CHECK(first < lit_now(eng), "капля появилась сразу целиком: %d → %d пикселей", first, lit_now(eng));
}

// Движение и значки через месяц на одной эмоции идут так же, как в первые секунды.
static void test_long_hold_precision() {
  FaceEngine eng;
  eng.init(4, 0);
  eng.set_expression(Expr::charging, 0, 0);
  auto frames = [&](uint32_t from) {
    uint32_t t = from;
    eng.update(t);
    const uint32_t f0 = eng.stats().frames;
    while (t < from + 10000) {
      t = eng.next_update_ms();
      eng.update(t);
    }
    return eng.stats().frames - f0;
  };
  const uint32_t early = frames(5000);
  uint32_t t = 15000;
  for (; t < 30u * 86400000u; t += 59000u) eng.update(t);
  const uint32_t late = frames(t);
  CHECK(late * 4 >= early * 3 && late * 4 <= early * 5, "кадров за 10 с: в начале %u, через месяц %u", early, late);
}

// Удар без падения — не падение; «перевернули обратно» без переворота — не головокружение.
static void test_behavior_fresh_state() {
  FaceEngine eng;
  Behavior bh;
  eng.init(1, 1000);
  bh.init(&eng, 1, 1000);
  bh.on_event(Event::impact, 1500);
  CHECK(strcmp(bh.last_reason(), "стукнули") == 0, "удар сразу после старта: «%s»", bh.last_reason());
  bh.on_event(Event::upright, 30000);
  CHECK(eng.expression() != Expr::dizzy && strcmp(bh.last_reason(), "стукнули") == 0,
        "upright без переворота: %s, «%s»", expr_id(eng.expression()), bh.last_reason());
  bh.on_event(Event::free_fall, 40000);
  bh.on_event(Event::impact, 40500);
  CHECK(strcmp(bh.last_reason(), "упал и ударился") == 0, "падение и удар: «%s»", bh.last_reason());

  // Автоматика выключена надолго, потом включена — характер сразу в деле.
  for (uint32_t tt = 50000; tt < 50000u + 3600000u; tt += 20000) {
    if (tt == 50000) bh.set_auto(false);
    bh.update(tt);
  }
  bh.set_auto(true);
  CHECK(time_reached(50000u + 3600000u, bh.next_update_ms()), "после включения автоматики поведение ждёт");
}

// Оскал рисуется без однопиксельных линий и щелей.
static void test_teeth_strokes() {
  for (int i = 0; i < kExprCount; ++i) {
    const Expression& x = expression((Expr)i);
    if (x.mouth.kind != MouthKind::Teeth) continue;
    Face f;
    expression_face((Expr)i, &f);
    for (int k = 0; k < 2; ++k) f.eye[k] = no_eye(), f.brow[k].len = 0;
    Framebuffer fb;
    fb.clear();
    render_face(fb, f, nullptr);
    int lonely = 0;
    for (int y = 0; y < kHeight; ++y)
      for (int x2 = 0; x2 < kWidth; ++x2) {
        if (!fb.get(x2, y)) continue;
        bool in_block = false;
        for (int dy = -1; dy <= 0 && !in_block; ++dy)
          for (int dx = -1; dx <= 0 && !in_block; ++dx)
            in_block = fb.get(x2 + dx, y + dy) && fb.get(x2 + dx + 1, y + dy) && fb.get(x2 + dx, y + dy + 1) &&
                       fb.get(x2 + dx + 1, y + dy + 1);
        lonely += !in_block;
      }
    CHECK(lonely == 0, "%s: в оскале %d однопиксельных пикселей", x.id, lonely);
  }
}

// Щетина привязана к лицу: сдвиг лица сдвигает точки, в том числе левее нуля.
static void test_stubble_follows_face() {
  Face f;
  expression_face(Expr::soy_scream, &f);
  Framebuffer a, b;
  a.clear(), b.clear();
  Anchors an;
  f.ox = -3.0f;
  render_face(a, f, &an);
  a.clear();
  render_overlay(a, Overlay::Stubble, an, 1.0f, 0.0f);
  f.ox = 0.0f;
  render_face(b, f, &an);
  b.clear();
  render_overlay(b, Overlay::Stubble, an, 1.0f, 0.0f);
  int diff = 0, lit = 0;
  for (int y = 0; y < kHeight; ++y)
    for (int x = 12; x < 112; ++x) diff += a.get(x - 3, y) != b.get(x, y), lit += b.get(x, y);
  CHECK(lit > 20 && diff == 0, "щетина не следует за лицом: %d расхождений из %d", diff, lit);
}

// Пробуждения, выпавшие ближе 20 мс друг к другу, сливаются; а дальше минуты движок не спит.
static void test_wake_merge_and_horizon() {
  FaceEngine eng;
  eng.init(6, 0);
  eng.options().drift = true;
  uint32_t t = 0;
  eng.set_expression(Expr::neutral, t, 0);
  eng.update(t);
  int idle_short = 0, wakes = 0;
  bool prev_changed = true;
  uint32_t longest = 0;
  while (t < 600000) {
    const uint32_t n = eng.next_update_ms();
    if (n - t < kWakeMergeMs && !prev_changed) ++idle_short;
    longest = n - t > longest ? n - t : longest;
    t = n;
    prev_changed = eng.update(t);
    ++wakes;
  }
  CHECK(idle_short == 0, "%d пробуждений ближе %u мс без анимации", idle_short, kWakeMergeMs);
  CHECK(longest <= kMaxSleepMs + kWakeMergeMs, "движок проспал %u мс", longest);
}

// Через несколько минут без внимания лицо успокаивается: жесты реже, кадров меньше.
static void test_calm_breathing() {
  FaceEngine eng;
  eng.init(8, 0);
  uint32_t t = 0;
  eng.set_expression(Expr::neutral, t, 0);
  eng.update(t);
  auto frames = [&](uint32_t to) {
    const uint32_t f0 = eng.stats().frames;
    while (t < to) {
      t = eng.next_update_ms();
      eng.update(t);
    }
    return eng.stats().frames - f0;
  };
  frames(10000);
  const uint32_t lively = frames(70000);
  frames(250000);
  const uint32_t calm = frames(310000);
  CHECK(eng.calm() && calm * 10 < lively * 7, "спокойное лицо: %u кадров/мин против %u", calm, lively);
  eng.note_activity(t);
  CHECK(!eng.calm(), "внимание возвращает живость");
  printf("  нейтральное лицо: %u кадров/мин, успокоившись — %u\n", lively, calm);
}

// Яркость: темно — минимум, ночь и сон — приглушённо, иначе обычная.
static void test_brightness_levels() {
  FaceEngine eng;
  Behavior bh;
  eng.init(1, 0);
  bh.init(&eng, 1, 0);
  bh.set_clock(12 * 60);
  CHECK(bh.brightness() == Brightness::Normal, "днём — обычная яркость");
  bh.on_event(Event::dark, 1000);
  CHECK(bh.brightness() == Brightness::Dark, "в темноте — минимум");
  bh.on_event(Event::light, 2000);
  CHECK(bh.brightness() == Brightness::Normal, "свет — снова обычная");
  bh.set_clock(23 * 60 + 30);
  CHECK(bh.brightness() == Brightness::Dim, "ночью — приглушённо");
  bh.set_clock(12 * 60);
  eng.set_expression(Expr::sleeping, 3000);
  CHECK(bh.brightness() == Brightness::Dim, "во сне — приглушённо");
}

// Часы не заданы (после выключения у платы их нет): минуты — только счёт суток, и ничего
// «по часам» не включается, что бы в них ни было.
static void test_clock_unknown() {
  FaceEngine eng;
  Behavior bh;
  Voice voice;
  Progress pr;
  pr.reset();
  eng.init(6, 0);
  bh.init(&eng, 6, 0);
  voice.init(nullptr, 6);
  bh.attach_voice(&voice);
  bh.attach_progress(&pr);
  bh.set_clock_known(false);
  bh.set_clock(2 * 60);
  CHECK(bh.brightness() == Brightness::Normal && !voice.quiet(), "часы не заданы, в счёте 02:00 — не ночь");
  uint32_t t = 1000;
  int night_faces = 0, eggs = 0;
  auto run_for = [&](uint32_t ms) {
    for (const uint32_t end = t + ms; t < end; t += 500) {
      bh.update(t), eng.update(t);
      const Expr b = bh.base_expression();
      night_faces += b == Expr::sleepy || b == Expr::sleeping || b == Expr::sleep_deprived;
      eggs += eng.scene() == Scene::banner || eng.scene() == Scene::error404;
      night_faces += bh.brightness() != Brightness::Normal && eng.expression() != Expr::sleepy &&
                     eng.expression() != Expr::sleeping;
    }
  };
  bh.on_event(Event::tap, t);
  run_for(15u * 60000u);  // без дела 15 минут «ночью»
  bh.set_clock(3 * 60);  // «3 AM» и 03:00–04:00 для NIGHT OWL
  bh.on_event(Event::tap, t);
  run_for(60000);
  bh.set_clock(4 * 60 + 4);
  run_for(60000);
  bh.set_clock(5 * 60 + 30);
  bh.on_event(Event::double_tap, t);
  run_for(60000);
  CHECK(night_faces == 0, "без часов нет сна без дела, недосыпа и ночной яркости (%d)", night_faces);
  CHECK(eggs == 0 && !pr.earned(Ach::night_owl) && !pr.earned(Ach::early_bird),
        "без часов нет «3 AM», «04:04» и достижений за 3 и 5 утра");
  // Часы задали — ночь снова по ним.
  bh.set_clock_known(true);
  bh.set_clock(23 * 60 + 30);
  CHECK(bh.brightness() == Brightness::Dim && voice.quiet(), "часы задали — ночью снова приглушённо и тихо");
}

// Ночная прогулка при свете: шаги — не события, но на ходу брелок не засыпает.
static void test_night_walk() {
  for (const uint16_t clock : {(uint16_t)(23 * 60 + 30), (uint16_t)(1 * 60 + 30)}) {
    FaceEngine eng;
    Behavior bh;
    MotionSensor m;
    eng.init(8, 0);
    bh.init(&eng, 8, 0);
    bh.set_clock(clock);
    Event ev[kSenseEvents];
    int asleep = 0, walking = 0, samples = 0;
    uint32_t next_step = 1000, bump_end = 0;
    for (uint32_t t = 0; t < 12u * 60000u; t += 10) {
      if (t >= next_step) bump_end = t + 80, next_step += 500;  // 2 шага в секунду
      const int n = m.feed(0.0f, t < bump_end ? 1.4f : 1.0f, 0.0f, t, ev);
      for (int i = 0; i < n; ++i) bh.on_event(ev[i], t);
      if (t % 50 == 0) bh.update(t), eng.update(t);
      if (t > 20000 && t % 1000 == 0) {
        ++samples;
        const Expr b = bh.base_expression();
        asleep += b == Expr::sleepy || b == Expr::sleeping;
        walking += b == Expr::walking || b == Expr::sleep_deprived;
      }
    }
    CHECK(asleep == 0 && walking == samples, "%02u:%02u, 12 минут ходьбы при свете — не засыпает (сон %d, шаг %d из %d)",
          clock / 60u, clock % 60u, asleep, walking, samples);
  }
}

// Встреча с легендарным брелоком: после приветствия — звёзды в глазах у любого характера.
static void test_legend_greeting() {
  uint32_t legend = 1;
  while (identity_from_hash(legend).rarity != Rarity::Legendary) ++legend;
  int ok = 0;
  for (int a = 0; a < kArchetypeCount; ++a) {
    FaceEngine eng;
    Behavior bh;
    eng.init(3, 0);
    bh.init(&eng, 3, 0);  // без роста: открыто всё, и pick_unlocked берёт первый вариант
    bh.set_temperament(&temperament((Archetype)a));
    bh.set_clock(12 * 60);
    bh.meet("LG-0001", true, 1000, legend);
    bool greeted = false;
    Expr after = Expr::Count;
    for (uint32_t t = 1000; t < 40000 && after == Expr::Count; t += 20) {
      eng.update(t);
      if (eng.scene() == Scene::greet) greeted = true;
      bh.update(t);
      if (greeted && eng.scene() != Scene::greet) after = eng.expression();
    }
    ok += greeted && after == Expr::starstruck;
    if (after != Expr::starstruck)
      printf("  %s: после приветствия легенды %s\n", temperament((Archetype)a).tag,
             after == Expr::Count ? "—" : expression(after).id);
  }
  CHECK(ok == kArchetypeCount, "легенда рядом — после приветствия звёзды в глазах у всех характеров (%d из %d)", ok,
        kArchetypeCount);
}

// ---------------------------------------------------------------- звук

static uint32_t voice_ms(const Voice& v) {
  uint32_t s = 0;
  for (int i = 0; i < v.length(); ++i) s += v.note(i).ms;
  return s;
}

static void test_sound_cues() {
  for (int c = 0; c < kCueCount; ++c) {
    Cue back;
    CHECK(cue_from_id(cue_id((Cue)c), &back) && back == (Cue)c, "id реплики %s", cue_id((Cue)c));
  }
  for (int a = 0; a < kArchetypeCount; ++a) {
    const Temperament& t = temperament((Archetype)a);
    CHECK(t.talk > 0.0f && t.talk <= 1.0f, "%s: разговорчивость %.2f", t.tag, t.talk);
    for (int c = 0; c < kCueCount; ++c) {
      Voice v;
      v.init(&t, 7);
      CHECK(v.say((Cue)c, 0), "%s говорит %s", t.tag, cue_id((Cue)c));
      bool audible = true, sound = false;
      for (int i = 0; i < v.length(); ++i) {
        const Note& n = v.note(i);
        if (n.hz) audible &= n.hz >= 600 && n.hz <= 5400, sound = true;
      }
      const uint32_t ms = voice_ms(v);
      CHECK(sound && audible, "%s/%s: ноты в слышимом для пьезо диапазоне", t.tag, cue_id((Cue)c));
      CHECK(ms >= 60 && ms <= 2500, "%s/%s: длина %u мс", t.tag, cue_id((Cue)c), (unsigned)ms);
    }
  }
  // Одна реплика у разных характеров звучит по-разному.
  int distinct = 0;
  for (int a = 1; a < kArchetypeCount; ++a) {
    Voice x, y;
    x.init(&temperament((Archetype)0), 7);
    y.init(&temperament((Archetype)a), 7);
    x.say(Cue::laugh, 0);
    y.say(Cue::laugh, 0);
    bool same = x.length() == y.length();
    for (int i = 0; same && i < x.length(); ++i) same = x.note(i).hz == y.note(i).hz && x.note(i).ms == y.note(i).ms;
    distinct += !same;
  }
  CHECK(distinct == kArchetypeCount - 1, "смех у всех характеров свой: %d из %d", distinct, kArchetypeCount - 1);
  // Скромник не договаривает и говорит тихо.
  Voice shy, play;
  shy.init(&temperament(Archetype::shy), 1);
  play.init(&temperament(Archetype::playful), 1);
  shy.say(Cue::level_up, 0);
  play.say(Cue::level_up, 0);
  CHECK(voice_ms(shy) < voice_ms(play) && shy.soft() && !play.soft(), "скромник короче и тише");
  CHECK(cue_for(Expr::laugh) == Cue::laugh && cue_for(Expr::calm) == Cue::Count, "реплики по эмоциям");
}

static void test_sound_playback() {
  Voice v;
  v.init(&temperament(Archetype::curious), 3);
  const uint32_t t0 = 0xFFFFFF00u;  // и через переполнение millis()
  CHECK(v.say(Cue::hello, t0), "говорит");
  CHECK(v.tone(t0) == v.note(0).hz && v.next_change_ms() == t0 + v.note(0).ms, "первая нота и её конец");
  uint32_t at = t0;
  for (int i = 0; i < v.length(); ++i) {
    CHECK(v.tone(at) == v.note(i).hz, "нота %d звучит вовремя", i);
    at += v.note(i).ms;
  }
  CHECK(!v.busy(at) && v.tone(at) == 0, "после конца — тишина");
  CHECK(time_left(at, v.next_change_ms()) == kMaxSleepMs, "тихий голос не будит процессор раньше горизонта");
  // Пока идёт реплика, обычная не перебивает, важная — перебивает.
  v.say(Cue::laugh, 1000);
  CHECK(!v.say(Cue::boop, 1010), "обычная реплика ждёт");
  CHECK(v.say(Cue::charge_start, 1020) && v.last_cue() == Cue::charge_start, "зарядка перебивает болтовню");
  // Ночью — только важное, и тихо.
  v.stop();
  v.set_quiet(true);
  CHECK(!v.say(Cue::laugh, 5000), "ночью не смеётся вслух");
  CHECK(v.say(Cue::battery_low, 5000) && v.soft(), "о батарее скажет и ночью, но тихо");
  v.set_quiet(false);
  v.set_enabled(false);
  CHECK(!v.say(Cue::battery_low, 9000) && v.tone(9000) == 0, "выключенная пищалка молчит");
}

static void test_scene_music() {
  for (int a = 0; a < kArchetypeCount; ++a) {
    for (int i = 0; i < kSceneCount; ++i) {
      Voice v;
      v.init(&temperament((Archetype)a), 5);
      SceneParams p;
      p.value = 1;
      p.key = 12345;
      v.scene((Scene)i, p, 0);
      CHECK(voice_ms(v) <= scene_duration_ms((Scene)i), "%s/%s: музыка %u мс длиннее сцены", temperament((Archetype)a).tag,
            scene_id((Scene)i), (unsigned)voice_ms(v));
      for (int k = 0; k < v.length(); ++k)
        CHECK(!v.note(k).hz || (v.note(k).hz >= 600 && v.note(k).hz <= 5400), "%s: нота %u Гц",
              scene_id((Scene)i), v.note(k).hz);
    }
  }
  // Исход дуэли звучит, когда он появляется на экране (4.6 с).
  Voice v;
  v.init(&temperament(Archetype::playful), 5);
  SceneParams p;
  p.value = 0;
  v.scene(Scene::duel, p, 0);
  uint32_t at = 0;
  int first_after = -1;
  for (int i = 0; i < v.length(); ++i) {
    if (at >= 4600 && v.note(i).hz && first_after < 0) first_after = (int)at;
    at += v.note(i).ms;
  }
  CHECK(first_after >= 4600 && first_after < 4700, "проигрыш звучит вместе с LOST: %d мс", first_after);
}

static void test_chat_sync() {
  const uint32_t ha = 0x1234567u, hb = 0x89ABCDEFu, nonce = 77;
  const uint32_t key = chat_key(ha, hb, nonce);
  CHECK(key == chat_key(hb, ha, nonce) && key != chat_key(ha, hb, nonce + 1), "ключ общий и зависит от сеанса");
  CHECK(chat_first(ha, hb) != chat_first(hb, ha), "начинает ровно один");
  Voice a, b;
  a.init(&temperament(Archetype::grumpy), 1);
  b.init(&temperament(Archetype::sweet), 2);
  a.chat(key, chat_first(ha, hb), 0);
  b.chat(key, chat_first(hb, ha), 0);
  CHECK(voice_ms(a) <= kChatMs && voice_ms(b) <= kChatMs, "разговор укладывается в %u мс", (unsigned)kChatMs);
  int overlap = 0, silent_lines = 0;
  for (int line = 0; line < kChatLines; ++line) {
    int a_on = 0, b_on = 0;
    for (uint32_t t = line * kChatLineMs; t < (line + 1) * kChatLineMs; t += 5) {
      const bool x = a.tone(t) != 0, y = b.tone(t) != 0;
      a_on += x, b_on += y, overlap += x && y;
    }
    silent_lines += !a_on && !b_on;
    uint8_t g[kChatGlyphs];
    const bool by_first = chat_line(key, line, g);
    const bool a_says = by_first == chat_first(ha, hb);
    CHECK(a_says ? (a_on > 0 && b_on == 0) : (b_on > 0 && a_on == 0), "реплику %d говорит только свой брелок", line);
  }
  CHECK(!overlap && !silent_lines, "брелоки не перебивают друг друга: %d, пустых реплик %d", overlap, silent_lines);
  // Знаки на экране появляются вместе с нотами: число нот реплики = числу знаков.
  uint8_t g[kChatGlyphs];
  chat_line(key, 0, g);
  uint32_t sum = 0;
  for (uint8_t x : g) {
    sum += chat_glyph_ms(x);
    CHECK(x < kChatRunes, "знак %u", x);
  }
  CHECK(sum < kChatLineMs, "реплика короче паузы между репликами");
  CHECK(chat_phrase(key)[0] && text_width(chat_phrase(key)) <= kWidth - 2 * kSafeMargin, "фраза влезает");
  // Для стенда с одним брелоком: обе стороны одной пищалкой.
  Voice both;
  both.init(&temperament(Archetype::grumpy), 1);
  both.chat(key, true, 0, &temperament(Archetype::sweet));
  int lines_heard = 0;
  for (int line = 0; line < kChatLines; ++line) {
    bool heard = false;
    for (uint32_t t = line * kChatLineMs; t < line * kChatLineMs + 1200; t += 5) heard |= both.tone(t) != 0;
    lines_heard += heard;
  }
  CHECK(lines_heard == kChatLines, "демо: слышны все %d реплики", lines_heard);
}

static void test_behavior_voice() {
  FaceEngine eng;
  Behavior bh;
  Voice v;
  eng.init(4, 0);
  bh.init(&eng, 4, 0);
  bh.set_temperament(&temperament(Archetype::dramatic));
  v.init(&temperament(Archetype::dramatic), 4);
  bh.attach_voice(&v);
  bh.set_clock(12 * 60);
  bh.update(0);
  bh.on_event(Event::charge_start, 1000);
  CHECK(v.started() == 1 && v.last_cue() == Cue::charge_start, "зарядка — всегда вслух");
  // Тапы: иногда отвечает, но не чаще раза в 1.5 с.
  uint32_t said = v.started(), t = 10000;
  for (int i = 0; i < 40; ++i, t += 400) {
    bh.on_event(Event::tap, t);
    bh.update(t);
    eng.update(t);
    v.tone(t);
  }
  const uint32_t talk = v.started() - said;
  CHECK(talk >= 2 && talk <= 11, "на 40 тапов за 16 с — %u реплик", (unsigned)talk);
  // Ночью — тишина, кроме важного.
  bh.set_clock(2 * 60);
  bh.update(t);
  said = v.started();
  for (int i = 0; i < 10; ++i, t += 2000) bh.on_event(Event::double_tap, t);
  CHECK(v.started() == said, "ночью на тапы молчит");
  bh.on_event(Event::battery_low, t += 3000);
  CHECK(v.started() == said + 1 && v.soft(), "ночью о батарее — тихо");
}

static void test_duel_then_chat() {
  // Два брелока после дуэли: разговор начинается у обоих в одно время, ключ общий, начинает один.
  const uint32_t ha = hash_of(Archetype::trickster), hb = hash_of(Archetype::shy);
  FaceEngine ea, eb;
  Behavior a, b;
  Voice va, vb;
  ea.init(1, 0), eb.init(2, 0);
  a.init(&ea, 1, 0), b.init(&eb, 2, 0);
  a.set_temperament(&temperament(Archetype::trickster)), b.set_temperament(&temperament(Archetype::shy));
  a.set_identity_hash(ha), b.set_identity_hash(hb);
  va.init(&temperament(Archetype::trickster), 1), vb.init(&temperament(Archetype::shy), 2);
  a.attach_voice(&va), b.attach_voice(&vb);
  a.duel("SH-0001", true, 1000, hb, 99);
  b.duel("TR-0002", false, 1000, ha, 99);
  uint32_t chat_a = 0, chat_b = 0, chat_end = 0;
  Expr after_chat = Expr::Count;
  for (uint32_t t = 1000; t < 30000; t += 20) {
    // Касание посреди паузы не сдвигает разговор: у соседа его не было.
    if (t == 8600) a.on_event(Event::tap, t);
    a.update(t), b.update(t), ea.update(t), eb.update(t);
    if (!chat_a && ea.scene() == Scene::chat) chat_a = t;
    if (!chat_b && eb.scene() == Scene::chat) chat_b = t;
    // Эмоция сразу после разговора (позже персонаж может заняться своим).
    if (chat_a && !chat_end && ea.scene() == Scene::None) chat_end = t;
    if (chat_end && after_chat == Expr::Count && t >= chat_end + 100) after_chat = ea.expression();
  }
  CHECK(chat_a && chat_a == chat_b, "разговор начался одновременно: %u и %u", (unsigned)chat_a, (unsigned)chat_b);
  CHECK(chat_a >= 1000 + scene_duration_ms(Scene::duel), "сначала дуэль");
  CHECK(ea.scene_params().key == eb.scene_params().key && ea.scene_params().value != eb.scene_params().value,
        "ключ общий, начинает один");
  CHECK(after_chat == Expr::innocent || after_chat == Expr::smug || after_chat == Expr::mischievous,
        "после разговора — невинное лицо: %s", after_chat == Expr::Count ? "?" : expression(after_chat).id);
}

// ---------------------------------------------------------------- рост 2: серии, достижения, друзья

static void test_streak_and_achievements() {
  Progress p;
  p.reset();
  p.new_day();
  CHECK(p.streak == 0, "первый день ещё не серия");
  for (int d = 0; d < 3; ++d) {
    p.touch();
    p.new_day();
  }
  CHECK(p.streak == 3 && p.best_streak == 3, "три дня с касаниями — серия 3: %u", p.streak);
  p.new_day();  // день без хозяина
  CHECK(p.streak == 0 && p.best_streak == 3, "день без касаний обнуляет серию, рекорд остаётся");
  // Достижения за счётчики — по одному, каждое один раз.
  Progress q;
  q.reset();
  CHECK(q.next_achievement() == Ach::Count, "у нового брелока достижений нет");
  q.met_friend(true);
  CHECK(q.next_achievement() == Ach::first_friend && q.next_achievement() == Ach::Count, "первый друг — один раз");
  for (int i = 0; i < 100; ++i) q.touch();
  CHECK(q.next_achievement() == Ach::petted, "сто касаний");
  CHECK(q.earn(Ach::knock) && !q.earn(Ach::knock) && q.earned(Ach::knock), "достижение за событие — один раз");
  CHECK(q.achievement_count() == 3, "всего %d", q.achievement_count());
  for (int i = 0; i < kAchCount; ++i) {
    const Achievement& a = achievement((Ach)i);
    CHECK(a.tag[0] && text_width(a.tag) <= kWidth - 2 * kSafeMargin && a.xp > 0, "достижение %s влезает", a.id);
  }
  // Голодного покормили: счётчик каждый раз, опыт раз в день.
  Progress f;
  f.reset();
  const uint32_t xp0 = f.xp;
  f.feed();
  f.feed();
  CHECK(f.feeds == 2 && f.xp == xp0 + XpRules{}.care, "опыт за заботу раз в день: %u", f.xp - xp0);
  f.new_day();
  f.feed();
  CHECK(f.xp == xp0 + 2u * XpRules{}.care + XpRules{}.day, "на следующий день — снова");
}

static void test_progress_v2_migration() {
  // Запись версии 2 — та же раскладка с нулями в запасе. После чтения — версия 3, всё на месте.
  Progress v2;
  v2.reset();
  v2.days = 12, v2.xp = 640, v2.friends = 4, v2.implant = 3;
  v2.version = 2;
  v2.seal();
  Progress m;
  CHECK(m.load(&v2, sizeof v2) && m.version == Progress::kVersion && m.valid(), "запись версии 2 переносится");
  CHECK(m.days == 12 && m.xp == 640 && m.friends == 4 && m.implant == 3 && m.streak == 0 && !m.achievements,
        "поля версии 2 на месте, новые — нули");
  v2.xp ^= 1;
  CHECK(!m.load(&v2, sizeof v2), "испорченная запись версии 2 не читается");
}

static void test_friends_table() {
  Friends f;
  f.reset();
  int before = 7;
  bool first = false;
  CHECK(f.meet(0xABCu, 1, &before, &first) == 0 && before == -1 && first, "новый знакомый");
  CHECK(f.meet(0xABCu, 1, &before, &first) == 0 && before == 0 && !first, "в тот же день дружба не растёт");
  int lvl = 0;
  for (uint16_t d = 2; d <= 30; ++d) lvl = f.meet(0xABCu, d);
  CHECK(lvl == kFriendLevels - 1 && !strcmp(friend_level_tag(lvl), "BFF"), "30 дней встреч — BFF");
  CHECK(f.level(0xABCu) == 4 && f.level(0x123u) == -1, "уровень знакомого и незнакомого");
  for (uint32_t h = 1; h <= (uint32_t)Friends::kSlots + 3; ++h) f.meet(0x10000u + h, 31);
  CHECK(f.count() == Friends::kSlots && f.level(0xABCu) == 4, "места кончились, лучший друг не вытеснен");
  for (int l = 1; l < kFriendLevels; ++l) CHECK(friend_level_days(l) > friend_level_days(l - 1), "уровни по возрастанию");
  f.seal();
  Friends g;
  CHECK(g.load(&f, sizeof f) && g.level(0xABCu) == 4, "таблица переживает флеш");
  Friends bad = f;
  bad.f[0].days ^= 1;
  CHECK(!g.load(&bad, sizeof bad), "испорченная таблица не читается");
}

static void test_retro_theme() {
  Theme t;
  CHECK(theme_from_id("retro", &t) && t == Theme::Retro && !strcmp(theme_id(Theme::Cyber), "cyber"), "темы по имени");
  FaceEngine eng;
  eng.init(9, 0);
  eng.options().theme = Theme::Retro;
  eng.options().drift = false;
  uint32_t now = 0;
  int blocky = 0;
  for (int i = 0; i < kExprCount; ++i) {
    settle(eng, (Expr)i, now);
    const Framebuffer& f = eng.frame();
    bool ok = true;
    int margin = 99;
    for (int y = 1; y + 1 < kHeight; y += 2)
      for (int x = 1; x + 1 < kWidth; x += 2) {
        const bool a = f.get(x, y);
        ok &= a == f.get(x + 1, y) && a == f.get(x, y + 1) && a == f.get(x + 1, y + 1);
      }
    for (int y = 0; y < kHeight; ++y)
      for (int x = 0; x < kWidth; ++x)
        if (f.get(x, y)) margin = imin(margin, imin(imin(x, kWidth - 1 - x), imin(y, kHeight - 1 - y)));
    blocky += ok;
    CHECK(margin >= kSafeMargin - 1, "ретро %s: до края %d px", expression((Expr)i).id, margin);
    CHECK(f.count_lit() > 40, "ретро %s: лицо не пропало", expression((Expr)i).id);
  }
  CHECK(blocky == kExprCount, "все эмоции — квадратами 2×2: %d из %d", blocky, kExprCount);
}

static void test_badge() {
  FaceEngine eng;
  eng.init(2, 0);
  uint32_t now = 0;
  settle(eng, Expr::happy, now);
  eng.badge("NEW", now);
  eng.update(now);
  CHECK(eng.badge_visible(), "надпись видна сразу");
  bool seen_off = false;
  for (uint32_t t = now; t < now + 600; t += 20) {
    eng.update(t);
    seen_off |= !eng.badge_visible();
  }
  CHECK(seen_off, "сначала мигает");
  eng.update(now + 1500);
  CHECK(eng.badge_visible() && eng.frame().get(kSafeMargin + 1, kSafeMargin + 1), "потом горит в углу");
  eng.update(now + FaceEngine::kBadgeMs + 50);
  CHECK(!eng.badge_visible(), "и гаснет сама");
  eng.badge("NEW", now + 4000);
  eng.play_scene(Scene::boot, now + 4000);
  eng.update(now + 4100);
  CHECK(!eng.badge_visible(), "поверх сцены не рисуется");
}

// Прогнать поведение и движок до момента t и собрать, какие сцены показались.
struct SceneLog {
  Scene seen[16];
  SceneParams params[16];
  int n = 0;
  bool has(Scene s) const {
    for (int i = 0; i < n; ++i)
      if (seen[i] == s) return true;
    return false;
  }
  const SceneParams* find(Scene s) const {
    for (int i = 0; i < n; ++i)
      if (seen[i] == s) return &params[i];
    return nullptr;
  }
};

static void run_log(Behavior& bh, FaceEngine& eng, uint32_t from, uint32_t to, SceneLog& log, uint32_t step = 20) {
  Scene last = Scene::None;
  for (uint32_t t = from; t < to; t += step) {
    bh.update(t);
    eng.update(t);
    if (eng.scene() != last) {
      last = eng.scene();
      if (last != Scene::None && log.n < 16) log.seen[log.n] = last, log.params[log.n++] = eng.scene_params();
    }
  }
}

static void test_date_and_time_eggs() {
  struct Case {
    uint16_t y;
    uint8_t m, d;
    const char* text;
  };
  const Case cases[] = {{2026, 3, 14, "PI DAY"},      {2026, 5, 4, "MAY THE 4TH"},  {2026, 4, 12, "POEKHALI!"},
                        {2026, 5, 25, "DONT PANIC"},  {2026, 2, 13, "FRIDAY 13"},   {2028, 2, 29, "FEB 29"},
                        {2026, 9, 13, "HELLO WORLD"}, {2028, 9, 12, "HELLO WORLD"}, {2026, 4, 1, "GOTCHA!"}};
  for (const Case& c : cases) {
    FaceEngine eng;
    Behavior bh;
    eng.init(1, 0);
    bh.init(&eng, 1, 0);
    bh.set_clock(12 * 60);
    bh.set_date(c.y, c.m, c.d);
    SceneLog log;
    run_log(bh, eng, 0, 20000, log);
    const SceneParams* p = log.find(Scene::banner);
    CHECK(p && !strcmp(p->text, c.text), "%04u-%02u-%02u: пасхалка «%s»", c.y, c.m, c.d, c.text);
    if (c.m == 4 && c.d == 1) CHECK(log.n >= 2 && log.seen[0] == Scene::bsod, "1 апреля сначала «падает»");
  }
  // Обычный день — без пасхалки; та же дата второй раз — тоже.
  {
    FaceEngine eng;
    Behavior bh;
    eng.init(1, 0);
    bh.init(&eng, 1, 0);
    bh.set_date(2026, 6, 17);
    SceneLog log;
    run_log(bh, eng, 0, 8000, log);
    CHECK(!log.has(Scene::banner), "17 июня — без пасхалки");
  }
  // Время: 13:37 — «1337» и достижение, 04:04 — ERROR 404 «TIME NOT FOUND».
  FaceEngine eng;
  Behavior bh;
  Progress pr;
  pr.reset();
  eng.init(1, 0);
  bh.init(&eng, 1, 0);
  bh.attach_progress(&pr);
  bh.set_clock(13 * 60 + 37);
  SceneLog log;
  run_log(bh, eng, 0, 15000, log);
  const SceneParams* p = log.find(Scene::banner);
  CHECK(p && !strcmp(p->text, "1337") && pr.earned(Ach::leet) && log.has(Scene::achievement), "13:37 — 1337 и хакер");
  SceneLog again;
  run_log(bh, eng, 15000, 30000, again);
  CHECK(!again.has(Scene::banner), "в ту же минуту второй раз не показывается");
  bh.set_clock(4 * 60 + 4);
  SceneLog e404;
  run_log(bh, eng, 30000, 40000, e404);
  const SceneParams* q = e404.find(Scene::error404);
  CHECK(q && !strcmp(q->sub, "TIME NOT FOUND"), "04:04 — время не найдено");
}

static void test_knock_code() {
  FaceEngine eng;
  Behavior bh;
  Voice v;
  Progress pr;
  pr.reset();
  eng.init(3, 0);
  bh.init(&eng, 3, 0);
  bh.attach_progress(&pr);
  bh.set_temperament(&temperament(Archetype::playful));
  v.init(&temperament(Archetype::playful), 3);
  bh.attach_voice(&v);
  bh.set_clock(12 * 60);
  bh.update(0);
  // «Та, та-та, та, та» с восьмой 250 мс.
  const uint32_t t0 = 20000, at[5] = {0, 500, 750, 1000, 1500};
  for (uint32_t a : at) {
    bh.on_event(Event::tap, t0 + a);
    bh.update(t0 + a);
    eng.update(t0 + a);
  }
  CHECK(pr.earned(Ach::knock) && v.last_cue() == Cue::two_bits, "на секретный стук — «two bits»: %s",
        cue_id(v.last_cue()));
  // Просто пять быстрых тапов — не стук.
  Progress p2;
  p2.reset();
  bh.attach_progress(&p2);
  for (int i = 0; i < 5; ++i) bh.on_event(Event::tap, 40000u + (uint32_t)i * 200u);
  CHECK(!p2.earned(Ach::knock), "ровные тапы — не стук");
}

static void test_battery_care() {
  FaceEngine eng;
  Behavior bh;
  Progress pr;
  pr.reset();
  eng.init(5, 0);
  bh.init(&eng, 5, 0);
  bh.attach_progress(&pr);
  bh.set_clock(12 * 60);
  const uint16_t frame0 = eng.options().frame_ms;
  bh.on_event(Event::battery_low, 1000);
  CHECK(bh.eco() && eng.options().frame_ms >= 50, "мало заряда — экорежим: кадр %u мс", eng.options().frame_ms);
  bh.set_battery_percent(12);
  const uint32_t xp0 = pr.xp;
  bh.on_event(Event::charge_start, 5000);
  CHECK(!bh.eco() && eng.options().frame_ms == frame0, "на зарядке экорежим снят");
  CHECK(eng.scene() == Scene::charge && eng.scene_params().value == 12, "сцена зарядки с процентом");
  CHECK(pr.feeds == 1 && pr.xp >= xp0 + XpRules{}.care, "голодного покормили — опыт");
  // Долго на зарядке без внимания — заставка, один раз.
  SceneLog log;
  run_log(bh, eng, 5000, 5000 + 12u * 60000u, log, 50);
  CHECK(log.has(Scene::screensaver), "на зарядке — заставка");
  // Выключение: сцена, потом сон до касания.
  bh.power_off(800000);
  SceneLog off;
  run_log(bh, eng, 800000, 820000, off, 50);
  CHECK(off.has(Scene::shutdown) && eng.expression() == Expr::sleeping, "выключился и спит: %s",
        expression(eng.expression()).id);
  bh.on_event(Event::charge_stop, 820000);
  bh.on_event(Event::button, 821000);
  run_log(bh, eng, 821000, 830000, off, 50);
  CHECK(eng.expression() != Expr::sleeping, "кнопка будит: %s", expression(eng.expression()).id);
}

static void test_friendship_and_gifts() {
  FaceEngine eng;
  Behavior bh;
  Progress pr;
  Friends fr;
  pr.reset();
  fr.reset();
  eng.init(6, 0);
  bh.init(&eng, 6, 0);
  bh.attach_progress(&pr);
  bh.attach_friends(&fr);
  bh.set_clock(12 * 60);
  const uint32_t peer = 0x5151u;
  uint32_t t = 1000;
  SceneLog log;
  for (int day = 0; day < 7; ++day) {
    bh.new_day(t);
    bh.meet("PE-0001", day == 0, t, peer);
    run_log(bh, eng, t, t + 40000, log, 50);
    t += 40000;
  }
  bool buddy = false, friends = false;
  for (int i = 0; i < log.n; ++i)
    if (log.seen[i] == Scene::banner) {
      buddy |= !strcmp(log.params[i].text, "BUDDY");
      friends |= !strcmp(log.params[i].text, "FRIENDS");
    }
  CHECK(buddy && friends && fr.level(peer) == 2, "дружба растёт по дням: BUDDY и FRIENDS (уровень %d)", fr.level(peer));
  // Старший брелок дарит эмоцию: сцена GIFT, после неё в углу — NEW.
  const int unlocked = pr.unlocked_count();
  bh.meet("OL-0002", true, t, 0x7777u, (uint8_t)(pr.implant + 3));
  bool gift = false, badge = false;
  for (uint32_t u = t; u < t + 60000; u += 20) {
    bh.update(u);
    eng.update(u);
    gift |= eng.scene() == Scene::banner && !strcmp(eng.scene_params().text, "GIFT");
    badge |= gift && eng.badge_visible();
  }
  CHECK(gift && badge && pr.unlocked_count() > unlocked, "подарок старшего: GIFT и NEW, открыто +%d",
        pr.unlocked_count() - unlocked);
}

static void test_whisper_sync() {
  // Старые знакомые шепчутся, если так решил общий ключ: у обоих одинаково, и шёпот сразу за приветствием.
  const uint32_t ha = hash_of(Archetype::curious), hb = hash_of(Archetype::chill);
  uint32_t nonce = 1;
  while (chat_key(ha, hb, nonce) % 4u) ++nonce;
  FaceEngine ea, eb;
  Behavior a, b;
  ea.init(1, 0), eb.init(2, 0);
  a.init(&ea, 1, 0), b.init(&eb, 2, 0);
  a.set_identity_hash(ha), b.set_identity_hash(hb);
  a.meet("CH-0001", false, 1000, hb, 0, nonce);
  b.meet("CU-0002", false, 1000, ha, 0, nonce);
  uint32_t ca = 0, cb = 0;
  for (uint32_t t = 1000; t < 20000; t += 20) {
    a.update(t), b.update(t), ea.update(t), eb.update(t);
    if (!ca && ea.scene() == Scene::chat) ca = t;
    if (!cb && eb.scene() == Scene::chat) cb = t;
  }
  CHECK(ca && ca == cb, "шёпот у обоих одновременно: %u и %u", (unsigned)ca, (unsigned)cb);
  FaceEngine ec;
  Behavior c;
  ec.init(3, 0);
  c.init(&ec, 3, 0);
  c.set_identity_hash(ha);
  uint32_t other = nonce + 1;
  while (chat_key(ha, hb, other) % 4u == 0) ++other;
  c.meet("CH-0001", false, 1000, hb, 0, other);
  bool chat = false;
  for (uint32_t t = 1000; t < 20000; t += 20) {
    c.update(t), ec.update(t);
    chat |= ec.scene() == Scene::chat;
  }
  CHECK(!chat, "в другой сеанс ключ решил иначе — без шёпота");
}

// ---------------------------------------------------------------- встречи по BLE

static void test_encounter_packet() {
  Advert a;
  a.hash = 0xA1B2C3D4u, a.level = 7, a.flags = kAdvDuelReady | kAdvCyber, a.mood = 200, a.countdown = 12;
  a.to = 0xBEEF;
  uint8_t buf[kAdvLen];
  adv_encode(a, buf);
  CHECK(buf[2] == 'K' && buf[3] == 'C' && buf[4] == kAdvVersion, "метка и версия в пакете");
  Advert b;
  CHECK(adv_decode(buf, kAdvLen, &b), "пакет читается");
  CHECK(b.hash == a.hash && b.level == 7 && b.flags == a.flags && b.mood == 200 && b.countdown == 12 && b.to == 0xBEEF,
        "все поля пакета доходят без потерь");
  buf[2] = 'X';
  CHECK(!adv_decode(buf, kAdvLen, &b), "чужой пакет без метки KC отбрасывается");
  CHECK(!adv_decode(buf, 5, &b), "короткий пакет отбрасывается");
  CHECK(mood_byte(-1.0f) == 0 && mood_byte(1.0f) == 255 && mood_byte(0.0f) == 128, "настроение в байт");
  CHECK(mood_byte(0.0f / 0.0f) == 0 && mood_byte(5.0f) == 255, "NaN и выход за пределы");
}

static void test_encounter_duel_fair() {
  int a_wins = 0, n = 0, bonus_wins = 0;
  for (uint32_t i = 1; i <= 2000; ++i) {
    const uint32_t a = i * 2654435761u | 1u, b = (i * 40503u) ^ 0x5A5A5A5Au;
    if (a == b) continue;
    const uint32_t nonce = encounter_nonce(a, b, (uint8_t)i);
    const bool wa = duel_won(a, b, 3, 3, nonce), wb = duel_won(b, a, 3, 3, nonce);
    CHECK(wa != wb, "ровно один победитель (пара %u)", (unsigned)i);
    a_wins += wa;
    bonus_wins += duel_won(a, b, 8, 3, nonce);
    ++n;
  }
  CHECK(a_wins > n * 4 / 10 && a_wins < n * 6 / 10, "при равных уровнях шансы около половины: %d из %d", a_wins, n);
  CHECK(bonus_wins > a_wins + n / 10, "старший имплант помогает: %d против %d", bonus_wins, a_wins);
  CHECK(bonus_wins < n * 9 / 10, "но не решает: %d из %d", bonus_wins, n);
  CHECK(encounter_nonce(5, 9, 100) == encounter_nonce(9, 5, 100), "nonce не зависит от порядка позывных");
}

// Два брелока в эфире: каждый рассылает пакет раз в period мс, второй слышит его.
static void encounter_run(Encounters& x, Encounters& y, uint32_t& t, uint32_t ms, int rssi, Meeting* mx, Meeting* my,
                          uint32_t* tx, uint32_t* ty) {
  for (uint32_t end = t + ms; t < end; t += 50) {
    if (t % 2000 == 0) y.on_advert(x.advert(t), rssi, t);
    if ((t + 1000) % 2000 == 0) x.on_advert(y.advert(t), rssi, t);
    Meeting m;
    if (x.poll(t, &m) && mx && mx->kind == Rendezvous::None) *mx = m, *tx = t;
    if (y.poll(t, &m) && my && my->kind == Rendezvous::None) *my = m, *ty = t;
  }
}

static void test_encounter_two_keychains() {
  const uint32_t ha = 0x10002000u, hb = 0x90003000u;  // у A позывной меньше: он объявляет сцену
  Encounters a, b;
  a.init(ha);
  b.init(hb);
  a.set_self(4, kAdvDuelReady, 180);
  b.set_self(2, kAdvDuelReady, 60);
  uint32_t t = 0, ta = 0, tb = 0;
  Meeting ma, mb;
  encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
  CHECK(ma.kind == Rendezvous::Duel && mb.kind == Rendezvous::Duel, "оба готовы к дуэли — дуэль у обоих (%d, %d)",
        (int)ma.kind, (int)mb.kind);
  CHECK(ma.peer == hb && mb.peer == ha, "каждый знает соседа");
  CHECK(ma.nonce == mb.nonce && ma.nonce, "общий nonce одинаковый: разговор шифром совпадёт");
  CHECK(ma.won != mb.won, "победитель один");
  CHECK((ta > tb ? ta - tb : tb - ta) <= 150, "сцена начинается почти одновременно: %u и %u мс", (unsigned)ta,
        (unsigned)tb);
  CHECK(ma.peer_level == 2 && mb.peer_level == 4, "уровень соседа из пакета");

  // Сразу после — тишина: тот же сосед не чаще раза в 10 минут.
  Meeting again_a, again_b;
  encounter_run(a, b, t, 120000, -60, &again_a, &again_b, &ta, &tb);
  CHECK(again_a.kind == Rendezvous::None && again_b.kind == Rendezvous::None, "повторной сцены сразу нет");

  // Через 10 минут — снова встреча, но дуэль с ним сегодня уже была: приветствие.
  t += 600000;
  Meeting ga, gb;
  encounter_run(a, b, t, 20000, -60, &ga, &gb, &ta, &tb);
  CHECK(ga.kind == Rendezvous::Greet && gb.kind == Rendezvous::Greet, "вторая встреча за день — приветствие (%d, %d)",
        (int)ga.kind, (int)gb.kind);

  // Новые сутки — дуэль снова возможна. Вторая встреча подряд дала перерыв 20 минут: он
  // доживает свой срок и в новых сутках.
  a.new_day();
  b.new_day();
  t += 1200000;
  Meeting da, db;
  encounter_run(a, b, t, 20000, -60, &da, &db, &ta, &tb);
  CHECK(da.kind == Rendezvous::Duel && db.kind == Rendezvous::Duel, "на следующий день снова дуэль");
}

static void test_encounter_rules() {
  Encounters a, b;
  a.init(0x100u);
  b.init(0x200u);
  a.set_self(1, kAdvDuelReady, 128);
  b.set_self(1, 0, 128);  // B не хочет дуэлей
  uint32_t t = 0, ta = 0, tb = 0;
  Meeting ma, mb;
  encounter_run(a, b, t, 20000, -85, &ma, &mb, &ta, &tb);
  CHECK(ma.kind == Rendezvous::None && mb.kind == Rendezvous::None, "далеко (−85 дБм) — не встреча");
  encounter_run(a, b, t, 20000, -55, &ma, &mb, &ta, &tb);
  CHECK(ma.kind == Rendezvous::Greet && mb.kind == Rendezvous::Greet, "без согласия обоих — только приветствие");

  // Один пакет прохожего — не встреча: нужен второй за минуту.
  Encounters c;
  c.init(0x50u);
  Advert p;
  p.hash = 0x900u;
  c.on_advert(p, -50, 1000);
  Meeting m;
  bool any = false;
  for (uint32_t tt = 1000; tt < 10000; tt += 100) any |= c.poll(tt, &m);
  CHECK(!any && !c.pending(), "один пакет — прохожий");
  c.on_advert(p, -50, 70000);  // второй — через 69 с
  CHECK(!c.pending(), "второй пакет позже минуты — встреча не подтверждена");
  c.on_advert(p, -50, 75000);
  CHECK(c.pending(), "два пакета за минуту — встреча");
  CHECK(c.advert(75000).countdown > 0 && c.advert(75000).to == 0x900u, "в пакете объявлен отсчёт для соседа");
  c.on_advert(p, -50, 75100);
  CHECK(c.nearby(75100) == 1, "сосед в памяти");
}


// Чужой передатчик шлёт какие угодно пакеты: сцены, награды и режим эфира от этого
// не должны зависеть сильнее, чем от настоящего соседа.
static void test_encounter_attacks() {
  const uint32_t me = 0x80000000u;
  Advert fake;
  fake.hash = 1;  // всегда меньше: «объявляет» он
  fake.countdown = 30;
  fake.to = (uint16_t)(me & 0xFFFF);
  fake.flags = kAdvDuelReady | kAdvDuelCalled;
  {
    Encounters e;
    e.init(me);
    e.set_self(0, 0, 128);  // дуэли выключены
    for (uint32_t t = 0; t < 20000; t += 200) {
      fake.hash = 1 + t / 200;  // каждый раз новый позывной
      e.on_advert(fake, -40, t);
    }
    CHECK(!e.pending(), "объявление сцены от неслыханного соседа не принимается");
  }
  {
    Encounters e;
    e.init(me);
    e.set_self(0, 0, 128);
    fake.hash = 7;
    Advert plain = fake;
    plain.countdown = 0, plain.to = 0;
    e.on_advert(plain, -60, 0);  // сначала обычные пакеты вблизи — дважды
    e.on_advert(plain, -60, 300);
    e.on_advert(fake, -60, 500);
    Meeting m;
    bool got = false;
    for (uint32_t t = 500; t < 5000; t += 50) got |= e.poll(t, &m);
    CHECK(got && m.kind == Rendezvous::Greet, "дуэль без моего согласия не начинается — только приветствие (%d)",
          (int)m.kind);
  }
  {
    Encounters e;
    e.init(me);
    e.set_self(0, kAdvDuelReady, 128);
    Advert plain;
    plain.hash = 9;
    e.on_advert(plain, -60, 0);
    e.on_advert(plain, -60, 50);
    Advert cd = plain;
    cd.to = (uint16_t)(me & 0xFFFF);
    cd.countdown = 255;
    e.on_advert(cd, -60, 100);
    CHECK(!e.pending(), "отсчёт на 25 с — подделка, не принимается");
    cd.countdown = 15;
    e.on_advert(cd, -60, 100);
    CHECK(!e.pending(), "объявление за 1.5 с до сцены — поздно: «принял» объявивший уже не услышит");
    cd.countdown = 30;
    e.on_advert(cd, -60, 100);
    CHECK(e.pending(), "обычный отсчёт принимается");
    Meeting m;
    bool got = false;
    uint32_t at = 0;
    for (uint32_t t = 100; t < 6000; t += 50) {
      cd.countdown = 30;  // пытается откладывать начало
      e.on_advert(cd, -60, t);
      if (!got && e.poll(t, &m)) got = true, at = t;
    }
    CHECK(got && at <= 3150, "начало нельзя отложить повторными пакетами: %u мс", (unsigned)at);
    CHECK(!e.wants_busy(at + 100), "после встречи сосед в перерыве — эфир не слушаем чаще");
    // 20 выдуманных соседей не сбрасывают перерыв настоящему.
    for (uint32_t i = 0; i < 20; ++i) {
      Advert f;
      f.hash = 1000 + i;
      e.on_advert(f, -50, at + 200 + i * 10);
    }
    bool again = false;
    for (uint32_t t = at + 1000; t < at + 30000; t += 1000) {
      e.on_advert(plain, -60, t);
      again |= e.poll(t, &m) && m.peer == 9;
    }
    CHECK(!again, "поток выдуманных позывных не сбрасывает перерыв соседу");
  }
  {
    Encounters e;
    e.init(me);
    Advert big;
    big.hash = 5;
    big.level = 255;
    e.on_advert(big, -50, 0);
    e.on_advert(big, -50, 1000);
    CHECK(e.nearby(1000) == 0, "уровень больше числа имплантов — пакет отбрасывается");
  }
}


// У обоих брелоков сцена должна быть одна и та же, даже если один перезагрузился или
// его память соседей забита выдуманными позывными.
static void test_encounter_sync() {
  const uint32_t ha = 0x10002000u, hb = 0x90003000u;
  {
    Encounters a, b;
    a.init(ha), b.init(hb);
    a.set_self(1, kAdvDuelReady, 100), b.set_self(1, kAdvDuelReady, 100);
    uint32_t t = 0, ta = 0, tb = 0;
    Meeting ma, mb;
    encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
    b.init(hb);  // B перезагрузился: не помнит, что сегодня уже дрались
    b.set_self(1, kAdvDuelReady, 100);
    t += 600000;
    Meeting ga, gb;
    encounter_run(a, b, t, 20000, -60, &ga, &gb, &ta, &tb);
    CHECK(ga.kind == gb.kind && ga.kind == Rendezvous::Greet, "после перезагрузки соседа сцена одна и та же (%d, %d)",
          (int)ga.kind, (int)gb.kind);
  }
  {
    Encounters a, b;
    a.init(ha), b.init(hb);
    a.set_self(1, kAdvDuelReady, 100), b.set_self(1, kAdvDuelReady, 100);
    uint32_t t = 0, ta = 0, tb = 0;
    // Память B забита соседями, которых слышно вблизи.
    for (uint32_t i = 0; i < 8; ++i) {
      Advert f;
      f.hash = 0xA0000000u + i;
      b.on_advert(f, -50, t);
    }
    Meeting ma, mb;
    encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
    CHECK(ma.kind == Rendezvous::Duel && mb.kind == Rendezvous::Duel, "забитая память соседей не мешает встрече (%d, %d)",
          (int)ma.kind, (int)mb.kind);
  }
  {
    // 16 дуэлей за сутки у B: он перестаёт показывать готовность, и обе сцены — приветствие.
    Encounters a, b;
    a.init(ha), b.init(hb);
    a.set_self(1, kAdvDuelReady, 100), b.set_self(1, kAdvDuelReady, 100);
    for (uint32_t i = 0; i < 16; ++i) {
      Encounters x;
      x.init(0x00000100u + i);  // меньше B: объявляет
      x.set_self(1, kAdvDuelReady, 100);
      uint32_t t = 0, tx = 0, tb = 0;
      Meeting mx, mbb;
      encounter_run(x, b, t, 20000, -60, &mx, &mbb, &tx, &tb);
    }
    CHECK(!(b.advert(0).flags & kAdvDuelReady), "дуэлей на сегодня хватит — без флага готовности");
    uint32_t t = 0, ta = 0, tb = 0;
    Meeting ma, mb;
    encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
    CHECK(ma.kind == Rendezvous::Greet && mb.kind == Rendezvous::Greet, "лимит дуэлей у одного — приветствие у обоих (%d, %d)",
          (int)ma.kind, (int)mb.kind);
  }
  Progress p;
  p.reset();
  int xp = 0;
  for (int i = 0; i < 10; ++i) {
    const uint32_t before = p.xp;
    p.duel(true);
    xp += p.xp > before;
  }
  CHECK(xp == XpRules{}.duels_xp_day_cap && p.duels_won == XpRules{}.duels_xp_day_cap,
        "дуэлей с опытом за день — по лимиту (%d, %u)", xp, (unsigned)p.duels_won);
}

static void test_encounter_ack() {
  // Объявивший начинает сцену, только услышав «принял»: иначе она была бы у него одного.
  Encounters a;
  a.init(0x100u);
  a.set_self(1, 0, 128);
  Advert b;
  b.hash = 0x900u;
  a.on_advert(b, -50, 0);
  a.on_advert(b, -50, 500);
  CHECK(a.pending() && a.advert(600).countdown > 0, "объявил сцену соседу");
  Meeting m;
  bool any = false;
  for (uint32_t t = 600; t < 6000; t += 100) any |= a.poll(t, &m);
  CHECK(!any && !a.pending(), "сосед не ответил — сцены нет и объявление снято");
  // Повтор — не сразу: сосед мог сыграть сцену, не услышав нас, и теперь в перерыве.
  a.on_advert(b, -50, 7000);
  a.on_advert(b, -50, 7500);
  CHECK(!a.pending(), "сразу после отмены — без нового объявления");
  // Через минуту сосед ответил «принял» — сцена есть.
  a.on_advert(b, -50, 66000);
  a.on_advert(b, -50, 66500);
  CHECK(a.pending(), "через минуту — объявил снова");
  Advert ack = b;
  ack.flags = kAdvAck, ack.to = 0x100u, ack.countdown = 30;
  a.on_advert(ack, -50, 67000);
  for (uint32_t t = 66500; t < 72000; t += 100) any |= a.poll(t, &m);
  CHECK(any && m.peer == 0x900u, "ответил — сцена начинается");

  // Повторы без ответа — всё реже: 1, 2, 4 минуты.
  Encounters c;
  c.init(0x100u);
  c.set_self(1, 0, 128);
  Advert d;
  d.hash = 0x700u;
  uint32_t t = 0, announced[4] = {};
  int n = 0;
  for (; t < 900000 && n < 4; t += 500) {
    c.on_advert(d, -50, t);
    if (c.pending() && (!n || t - announced[n - 1] > 5000)) announced[n++] = t;
    c.poll(t, &m);
  }
  CHECK(n == 4, "объявлений: %d", n);
  CHECK(n == 4 && announced[1] - announced[0] >= 60000 && announced[2] - announced[1] >= 120000 &&
            announced[3] - announced[2] >= 240000,
        "повторы реже: %u, %u, %u мс", announced[1] - announced[0], announced[2] - announced[1],
        announced[3] - announced[2]);

  // Ответ «принял» не считается объявлением: третий брелок с теми же младшими битами
  // позывного иначе сыграл бы сцену один.
  Encounters e;
  e.init(0x00010100u);
  e.set_self(1, 0, 128);
  Advert third;
  third.hash = 0x50u;  // меньше: путь второго брелока
  e.on_advert(third, -50, 0);
  e.on_advert(third, -50, 500);
  Advert third_ack = third;
  third_ack.flags = kAdvAck, third_ack.to = 0x0100u, third_ack.countdown = 30;
  e.on_advert(third_ack, -50, 1000);
  CHECK(!e.pending(), "чужой ответ «принял» — не объявление");

  // Дуэль, принятая как встреча (у второго дуэли выключены), у обоих становится встречей.
  Encounters f;
  f.init(0x100u);
  f.set_self(1, kAdvDuelReady, 128);
  Advert g;
  g.hash = 0x900u, g.flags = kAdvDuelReady;
  f.on_advert(g, -50, 0);
  f.on_advert(g, -50, 500);
  CHECK(f.pending() && (f.advert(600).flags & kAdvDuelCalled), "объявил дуэль");
  Advert g_ack = g;
  g_ack.flags = kAdvAck, g_ack.to = 0x100u, g_ack.countdown = 30;  // принял без флага дуэли
  f.on_advert(g_ack, -50, 1000);
  any = false;
  for (uint32_t u = 1000; u < 6000; u += 100) any |= f.poll(u, &m);
  CHECK(any && m.kind == Rendezvous::Greet && !m.won, "второй принял встречу — у объявившего тоже встреча");
  // И наоборот: принял дуэль — дуэль.
  Encounters h;
  h.init(0x100u);
  h.set_self(1, kAdvDuelReady, 128);
  Advert k;
  k.hash = 0x901u, k.flags = kAdvDuelReady;
  h.on_advert(k, -50, 0);
  h.on_advert(k, -50, 500);
  Advert k_ack = k;
  k_ack.flags = kAdvAck | kAdvDuelCalled, k_ack.to = 0x100u, k_ack.countdown = 30;
  h.on_advert(k_ack, -50, 1000);
  any = false;
  for (uint32_t u = 1000; u < 6000; u += 100) any |= h.poll(u, &m);
  CHECK(any && m.kind == Rendezvous::Duel, "второй принял дуэль — дуэль");
  // Второй в ответе показывает, что принял: встречу или дуэль.
  Encounters fol;
  fol.init(0x900u);
  fol.set_self(1, 0, 128);  // дуэли выключены
  Advert an;
  an.hash = 0x100u, an.flags = kAdvDuelReady;
  fol.on_advert(an, -50, 0);
  fol.on_advert(an, -50, 500);
  Advert call = an;
  call.flags = kAdvDuelReady | kAdvDuelCalled, call.to = 0x900u, call.countdown = 30;
  fol.on_advert(call, -50, 1000);
  CHECK(fol.pending() && (fol.advert(1100).flags & kAdvAck) && !(fol.advert(1100).flags & kAdvDuelCalled),
        "второй с выключенными дуэлями отвечает «принял встречу»");
}

static void test_encounter_cooldown_wrap() {
  // Перерыв с соседом не «оживает», когда часы уходят за 2^31 мс (24.8 суток без сброса).
  Encounters a, b;
  a.init(0x100u), b.init(0x900u);
  a.set_self(1, 0, 128), b.set_self(1, 0, 128);
  uint32_t t = 1000, ta = 0, tb = 0;
  Meeting ma, mb;
  encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
  CHECK(ma.kind == Rendezvous::Greet && mb.kind == Rendezvous::Greet, "первая встреча");
  // Брелоки лежат порознь 24.9 суток: каждый живёт своей жизнью, poll идёт.
  for (uint32_t u = t; u < t + 2151360000u; u += 60000) {
    Meeting m;
    a.poll(u, &m), b.poll(u, &m);
  }
  t += 2151360000u;
  ma = mb = Meeting{};
  encounter_run(a, b, t, 20000, -60, &ma, &mb, &ta, &tb);
  CHECK(ma.kind == Rendezvous::Greet && mb.kind == Rendezvous::Greet, "через 24.9 суток встреча снова есть (%d, %d)",
        (int)ma.kind, (int)mb.kind);
}

static void test_encounter_rate_limit() {
  // Поток поддельных соседей: каждые 4 с новый позывной, дважды вблизи и с объявлением.
  Encounters e;
  const uint32_t me = 0x80000000u;
  e.init(me);
  e.set_self(1, kAdvDuelReady, 128);
  int scenes = 0;
  Meeting m;
  for (uint32_t t = 0, i = 0; t < 600000; t += 4000, ++i) {
    Advert f;
    f.hash = 1000 + i;
    e.on_advert(f, -50, t);
    e.on_advert(f, -50, t + 100);
    Advert cd = f;
    cd.countdown = 30, cd.to = (uint16_t)(me & 0xFFFF), cd.flags = kAdvDuelReady | kAdvDuelCalled;
    e.on_advert(cd, -50, t + 200);
    for (uint32_t k = t; k < t + 4000; k += 100) scenes += e.poll(k, &m);
  }
  CHECK(scenes <= 6, "поток подделок: за 10 минут не больше 6 сцен (%d)", scenes);
  // Знакомый брелок лимит не тратит: после потока подделок встреча с ним проходит.
  static const uint32_t kFriend = 77;
  e.set_known([](uint32_t h, const void*) { return h == kFriend; }, nullptr);
  Advert fr;
  fr.hash = kFriend;
  const uint32_t t0 = 600000;
  e.on_advert(fr, -50, t0);
  e.on_advert(fr, -50, t0 + 100);
  Advert cd = fr;
  cd.countdown = 30, cd.to = (uint16_t)(me & 0xFFFF);
  e.on_advert(cd, -50, t0 + 200);
  bool met = false;
  for (uint32_t k = t0; k < t0 + 4000; k += 100) met |= e.poll(k, &m) && m.peer == kFriend;
  CHECK(met, "знакомый встречается и сразу после потока подделок");
}

static void test_encounter_rewards_capped() {
  Progress p;
  p.reset();
  int counted = 0;
  for (int i = 0; i < 20; ++i) counted += p.met_friend(true) >= -1 && p.friends > (uint16_t)counted;
  CHECK(p.friends == XpRules{}.new_friends_day_cap, "новых друзей в счётчике за день не больше лимита: %u",
        (unsigned)p.friends);
  Rng rng(7);
  int prizes = 0, gifts = 0;
  for (int i = 0; i < 10; ++i) {
    prizes += p.duel_prize(rng) != Expr::Count;
    gifts += p.gift(rng) != Expr::Count;
  }
  CHECK(prizes == XpRules{}.duel_prizes_day_cap && gifts == XpRules{}.gifts_day_cap,
        "эмоций из дуэлей и подарков за день — по лимиту (%d, %d)", prizes, gifts);
  p.new_day();
  CHECK(p.duel_prize(rng) != Expr::Count, "на следующий день награды снова есть");
  p.seal();
  Progress q;
  CHECK(q.load(&p, sizeof p) && q.today_duel_prizes == 1, "счётчики наград переживают перезагрузку");

  Friends f;
  f.reset();
  for (uint32_t i = 0; i < Friends::kSlots; ++i) {
    f.meet(100 + i, 1);
    f.meet(100 + i, 2);  // крепкие друзья: два дня
  }
  for (uint32_t i = 0; i < 50; ++i) f.meet(5000 + i, 3);
  int kept = 0;
  for (uint32_t i = 0; i < Friends::kSlots; ++i) kept += f.level(100 + i) >= 0;
  CHECK(kept == Friends::kSlots, "поток новых брелоков не вытесняет крепких друзей: осталось %d", kept);
  f.meet(9999, 40);  // через месяц старых друзей можно вытеснить
  CHECK(f.level(9999) >= 0, "через месяц без встреч место освобождается");
}


// ---------------------------------------------------------------- датчики

static int sense_count(const Event* ev, int n, Event e) {
  int k = 0;
  for (int i = 0; i < n; ++i) k += ev[i] == e;
  return k;
}

static void test_motion_sensing() {
  Event ev[kSenseEvents];
  {
    MotionSensor m;
    int other = 0, still = 0;
    for (uint32_t t = 0; t < 70000; t += 40) {
      const int n = m.feed(0.0f, 1.0f, 0.0f, t, ev);
      still += sense_count(ev, n, Event::still);
      other += n - sense_count(ev, n, Event::still);
    }
    CHECK(still == 1 && other == 0, "висит спокойно: только «покой» через минуту (%d, %d)", still, other);
    int pick = 0;
    for (uint32_t t = 70000; t < 70400; t += 40) {
      const int n = m.feed(0.3f, 1.2f, 0.1f, t, ev);
      pick += sense_count(ev, n, Event::pick_up);
    }
    CHECK(pick == 1, "после покоя движение — взяли в руки");
  }
  {
    MotionSensor m;
    int shake = 0, hard = 0;
    for (uint32_t t = 0; t < 1200; t += 20) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    for (uint32_t t = 1200; t < 2400; t += 20) {
      const float x = ((t / 100) & 1) ? 1.5f : -1.5f;
      const int n = m.feed(x, 1.0f, 0.0f, t, ev);
      shake += sense_count(ev, n, Event::shake);
      hard += sense_count(ev, n, Event::shake_hard);
    }
    CHECK(shake + hard >= 1, "трясут — тряска");
    CHECK(hard == 0, "умеренная тряска — не сильная");
  }
  {
    MotionSensor m;
    int fall = 0, impact = 0;
    for (uint32_t t = 0; t < 1000; t += 20) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    for (uint32_t t = 1000; t < 1300; t += 20) {
      const int n = m.feed(0.02f, 0.05f, 0.0f, t, ev);
      fall += sense_count(ev, n, Event::free_fall);
    }
    for (uint32_t t = 1300; t < 1400; t += 20) {
      const int n = m.feed(0.5f, 3.6f, 0.2f, t, ev);
      impact += sense_count(ev, n, Event::impact);
    }
    CHECK(fall == 1 && impact == 1, "падение и удар (%d, %d)", fall, impact);
  }
  {
    MotionSensor m;
    int walk = 0, run = 0;
    for (uint32_t t = 0; t < 8000; t += 20) {
      const uint32_t ph = t % 500;  // шаг раз в 0.5 с
      const float bump = ph < 100 ? 0.4f : 0.0f;
      const int n = m.feed(0.0f, 1.0f + bump, 0.0f, t, ev);
      walk += sense_count(ev, n, Event::walk);
      run += sense_count(ev, n, Event::run);
    }
    const uint32_t steps = m.take_steps();
    CHECK(walk == 1 && run == 0, "шаги 2 в секунду — ходьба (%d, %d)", walk, run);
    CHECK(steps >= 13 && steps <= 17, "за 8 с около 16 шагов: %u", (unsigned)steps);
    CHECK(m.take_steps() == 0, "шаги отдаются один раз");
  }
  {
    MotionSensor m;
    int up = 0;
    for (uint32_t t = 0; t < 1000; t += 20) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    for (uint32_t t = 1000; t < 4000; t += 20) up += sense_count(ev, m.feed(0.0f, -1.0f, 0.0f, t, ev), Event::upside_down);
    CHECK(up == 1, "перевернули — вверх ногами один раз");
    m.feed(0.6f, 0.8f, 0.0f, 4000, ev);
    for (uint32_t t = 4000; t < 6000; t += 20) m.feed(0.6f, 0.75f, 0.0f, t, ev);
    CHECK(m.tilt_x() > 0.5f, "наклон вправо — зрачки вправо: %.2f", m.tilt_x());
    CHECK(m.feed(0.0f / 0.0f, 1.0f, 0.0f, 6000, ev) == 0, "NaN с датчика отбрасывается");
  }
}

static void test_hearing() {
  Event ev[kSenseEvents];
  SoundSensor h;
  int loud = 0, music = 0, speech = 0, quiet = 0;
  uint32_t t = 0;
  for (; t < 5000; t += 30) h.feed(-62.0f, t, ev);
  for (int i = 0; i < 3; ++i, t += 30) loud += sense_count(ev, h.feed(i == 1 ? -8.0f : -62.0f, t, ev), Event::loud);
  CHECK(loud == 1, "хлопок — громко");
  for (uint32_t end = t + 9000; t < end; t += 30) {
    const int n = h.feed(-30.0f, t, ev);
    music += sense_count(ev, n, Event::music);
    speech += sense_count(ev, n, Event::speech);
  }
  CHECK(music == 1 && speech == 0, "ровный звук без пауз — музыка (%d, %d)", music, speech);
  for (uint32_t end = t + 12000; t < end; t += 30) quiet += sense_count(ev, h.feed(-62.0f, t, ev), Event::quiet);
  CHECK(quiet == 1, "стихло — тишина");
  for (uint32_t end = t + 9000; t < end; t += 30) {
    const bool syl = (t % 450) < 200;  // слоги с паузами
    speech += sense_count(ev, h.feed(syl ? -35.0f : -62.0f, t, ev), Event::speech);
  }
  CHECK(speech == 1, "речь: всплески с паузами");
}

static void test_light_and_heat() {
  Event ev[kSenseEvents];
  LightSensor l;
  int n = l.feed(400.0f, 0, ev);
  CHECK(n == 1 && ev[0] == Event::light, "первое измерение — обычный свет");
  int dark = 0;
  for (uint32_t t = 0; t < 1500; t += 100) dark += sense_count(ev, l.feed(1.0f, t, ev), Event::dark);
  for (uint32_t t = 1500; t < 2000; t += 100) l.feed(400.0f, t, ev);
  CHECK(dark == 0, "тень от руки на секунду — не темнота");
  for (uint32_t t = 2000; t < 6000; t += 100) dark += sense_count(ev, l.feed(1.0f, t, ev), Event::dark);
  CHECK(dark == 1, "в кармане — темно");
  int bright = 0;
  for (uint32_t t = 6000; t < 10000; t += 100) bright += sense_count(ev, l.feed(20000.0f, t, ev), Event::bright);
  CHECK(bright == 1, "на солнце — ярко");

  TempSensor th;
  CHECK(th.feed(22.0f, 0, ev) == 0, "комнатная температура — без события");
  int cold = 0;
  for (uint32_t t = 0; t < 40000; t += 1000) cold += sense_count(ev, th.feed(-3.0f, t, ev), Event::cold);
  CHECK(cold == 1, "мороз — холодно");
}

static void test_battery_sensing() {
  CHECK(BatterySensor::percent_of(4.2f) == 100 && BatterySensor::percent_of(3.2f) == 0, "края шкалы заряда");
  int prev = -1;
  bool mono = true;
  for (float v = 3.2f; v < 4.25f; v += 0.01f) {
    const int p = BatterySensor::percent_of(v);
    mono &= p >= prev;
    prev = p;
  }
  CHECK(mono, "проценты растут с напряжением");
  Event ev[kSenseEvents];
  BatterySensor b;
  b.feed(3.9f, false, false, 0, ev);
  int start = 0, full = 0, stop = 0;
  start += sense_count(ev, b.feed(3.95f, true, true, 1000, ev), Event::charge_start);
  full += sense_count(ev, b.feed(4.2f, true, false, 2000, ev), Event::charge_full);
  stop += sense_count(ev, b.feed(4.15f, false, false, 3000, ev), Event::charge_stop);
  CHECK(start == 1 && full == 1 && stop == 1, "зарядка: начало, полная, отключили (%d %d %d)", start, full, stop);
  int low = 0, crit = 0;
  bool early = false;
  uint32_t below_at = 0;
  for (uint32_t t = 4000; t < 300000; t += 1000) {
    const float v = t < 200000 ? 3.75f - (float)(t - 4000) / 196000.0f * 0.5f : 3.25f;
    const int n = b.feed(v, false, false, t, ev);
    low += sense_count(ev, n, Event::battery_low);
    crit += sense_count(ev, n, Event::battery_critical);
    if (!below_at && b.volts() < 3.35f) below_at = t;
    if (b.empty() && t - below_at < 60000) early = true;
  }
  CHECK(low == 1 && crit == 1, "разряд: «мало» и «почти пусто» по разу (%d, %d)", low, crit);
  CHECK(b.empty(), "ниже 3.35 В дольше минуты — пора выключаться");
  CHECK(!early, "пустая ячейка — только через минуту ниже порога, не сразу");
  {
    // Чётные метки времени: ожидание не должно проскакивать (метка «now | 1» была в будущем).
    BatterySensor q;
    q.feed(3.30f, false, false, 1000, ev);
    q.feed(3.30f, false, false, 2000, ev);
    CHECK(!q.empty(), "провал напряжения на секунду — не выключение");
  }
  const int before = b.percent();
  b.feed(4.9f, true, true, 300000, ev);
  CHECK(!b.empty(), "подключили USB — не выключаемся");
  CHECK(b.percent() == before, "от USB напряжение USB не принимается за заряд ячейки");
}
static void test_sensing_timing() {
  Event ev[kSenseEvents];
  {
    MotionSensor m;
    for (uint32_t t = 0; t < 1000; t += 10) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    int fall = sense_count(ev, m.feed(0.0f, 0.2f, 0.0f, 2000, ev), Event::free_fall);
    fall += sense_count(ev, m.feed(0.0f, 1.0f, 0.0f, 2010, ev), Event::free_fall);
    CHECK(fall == 0, "один отсчёт невесомости при чётном времени — не падение");
    int up = 0;
    up += sense_count(ev, m.feed(0.0f, -1.0f, 0.0f, 3000, ev), Event::upside_down);
    for (uint32_t t = 3010; t < 3200; t += 10) up += sense_count(ev, m.feed(0.0f, -1.0f, 0.0f, t, ev), Event::upside_down);
    CHECK(up == 0, "перевернули на 0.2 с — ещё не вверх ногами");
  }
  {
    // Медленная ходьба: шаг раз в 0.9 с. Каждый шаг считается один раз, ходьба не «мигает».
    MotionSensor m;
    int walk = 0;
    uint32_t t = 0;
    for (; t < 1000; t += 10) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    for (int step = 0; step < 60; ++step)
      for (uint32_t end = t + 900; t < end; t += 10) {
        const float bump = (end - t) > 800 ? 0.4f : 0.0f;
        walk += sense_count(ev, m.feed(0.0f, 1.0f + bump, 0.0f, t, ev), Event::walk);
      }
    for (uint32_t end = t + 5000; t < end; t += 10) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    const uint32_t steps = m.take_steps();
    CHECK(steps >= 58 && steps <= 60 && walk == 1, "60 медленных шагов — 60 шагов и одна ходьба (%u, %d)",
          (unsigned)steps, walk);
  }
  {
    // Тряска 3 Гц — не шаги; сильная тряска — сильная.
    MotionSensor m;
    for (uint32_t t = 0; t < 1000; t += 10) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    int hard = 0, impact = 0;
    for (uint32_t t = 1000; t < 5000; t += 10) {
      const float x = ((t / 167) & 1) ? 2.0f : -2.0f;
      const int n = m.feed(x, 1.0f, 0.0f, t, ev);
      hard += sense_count(ev, n, Event::shake_hard);
      impact += sense_count(ev, n, Event::impact);
    }
    CHECK(m.take_steps() == 0, "тряска не считается шагами");
    CHECK(hard >= 1, "тряска ±2 g — сильная");
    CHECK(impact <= 1, "сильная тряска — не серия ударов: %d", impact);
  }
  {
    MotionSensor m;
    for (uint32_t t = 0; t < 2000; t += 10) m.feed(0.0f, 1.0f, 0.0f, t, ev);
    int odd = 0;
    for (uint32_t t = 2000; t < 2200; t += 10) odd += m.feed(0.3f, 1.2f, 0.0f, t, ev);
    odd += sense_count(ev, m.feed(0.0f, 1.0f, 0.0f, 2170, ev), Event::still);  // метка из прошлой пачки
    CHECK(sense_count(ev, m.feed(0.0f, 1.0f, 0.0f, 2180, ev), Event::still) == 0, "время назад — не «покой»");
  }
  {
    // Ровная музыка две минуты остаётся музыкой.
    SoundSensor h;
    uint32_t t = 0;
    for (; t < 5000; t += 32) h.feed(-62.0f, t, ev);
    int music = 0, speech = 0, quiet = 0;
    for (uint32_t end = t + 120000; t < end; t += 32) {
      const float db = -30.0f + (float)((t / 32) % 9) - 4.0f;
      const int n = h.feed(db, t, ev);
      music += sense_count(ev, n, Event::music);
      speech += sense_count(ev, n, Event::speech);
      quiet += sense_count(ev, n, Event::quiet);
    }
    CHECK(music == 1 && speech == 0 && quiet == 0, "две минуты музыки — музыка (%d, %d, %d)", music, speech, quiet);
    // Микрофон выключили на час: после включения не «тишина» и не «музыка» сразу.
    t += 3600000;
    int n = h.feed(-60.0f, t, ev);
    CHECK(sense_count(ev, n, Event::music) == 0 && sense_count(ev, n, Event::speech) == 0,
          "после перерыва старое окно не в счёт");
  }
  {
    SoundSensor h;
    uint32_t t = 0;
    for (; t < 5000; t += 32) h.feed(-60.0f, t, ev);
    int loud = 0;
    loud += sense_count(ev, h.feed(-30.0f, t, ev), Event::loud);
    loud += sense_count(ev, h.feed(-12.0f, t + 32, ev), Event::loud);
    CHECK(loud == 1, "хлопок на стыке двух блоков — громко");
  }
}

// ---------------------------------------------------------------- регрессии предрелизного аудита

static void test_battery_settle() {
  Event ev[kSenseEvents];
  {
    // Отключили USB с полной ячейки: первый замер — в провале через диод (3.55 В вместо 4.15).
    BatterySensor b;
    b.feed(4.9f, true, true, 0, ev);
    b.feed(4.9f, true, false, 60000, ev);  // зарядился
    int low = 0, crit = 0;
    const int n0 = b.feed(3.55f, false, false, 120000, ev);
    const int stop = sense_count(ev, n0, Event::charge_stop);
    low += sense_count(ev, n0, Event::battery_low);
    for (uint32_t t = 130000; t < 720000; t += 10000) {
      const int n = b.feed(4.15f, false, false, t, ev);
      low += sense_count(ev, n, Event::battery_low);
      crit += sense_count(ev, n, Event::battery_critical);
    }
    CHECK(stop == 1, "отключили зарядку — событие");
    CHECK(low == 0 && crit == 0, "провал сразу после отключения — не «мало заряда» (%d, %d)", low, crit);
    CHECK(b.percent() >= 90, "заряд после отключения с полной: %d %%", b.percent());
  }
  {
    // Включили без USB, первый замер — под нагрузкой: процент — по лучшему замеру первых 30 с.
    BatterySensor b;
    b.feed(3.70f, false, false, 0, ev);
    b.feed(3.95f, false, false, 10000, ev);
    CHECK(b.percent() == BatterySensor::percent_of(3.95f), "первые 30 с — по наибольшему замеру: %d %%", b.percent());
  }
  {
    // Ячейка правда почти пуста: «мало заряда» — после установления и один раз.
    BatterySensor b;
    b.feed(4.9f, true, true, 0, ev);
    int low = 0;
    uint32_t low_at = 0;
    for (uint32_t t = 10000; t < 200000; t += 10000) {
      const int n = b.feed(3.54f, false, false, t, ev);
      if (sense_count(ev, n, Event::battery_low)) {
        ++low;
        if (!low_at) low_at = t;
      }
    }
    CHECK(low == 1 && low_at >= 40000, "почти пустая — «мало» один раз и не раньше 30 с (%d, %u мс)", low,
          (unsigned)low_at);
  }
}

// После неполной зарядки голод не держится до следующей полной; не набралась — вернётся.
static void test_battery_face_recovers() {
  for (int crit = 0; crit < 2; ++crit) {
    FaceEngine eng;
    Behavior bh;
    eng.init(8, 0);
    bh.init(&eng, 8, 0);
    bh.set_clock(12 * 60);
    uint32_t t = 1000;
    bh.on_event(crit ? Event::battery_critical : Event::battery_low, t);
    bh.set_battery_percent(crit ? 3 : 12);
    for (const uint32_t end = t + 30000; t < end; t += 50) bh.update(t), eng.update(t);
    const Expr hungry = bh.base_expression();
    CHECK(crit ? hungry == Expr::critical_battery : (hungry == Expr::low_battery || hungry == Expr::hungry),
          "мало заряда — голодное лицо: %s", expression(hungry).id);
    bh.on_event(Event::charge_start, t);
    for (const uint32_t end = t + 60000; t < end; t += 50) bh.update(t), eng.update(t);
    bh.set_battery_percent(60);  // оценка прошивки на зарядке
    bh.on_event(Event::charge_stop, t);
    CHECK(eng.expression() == Expr::hello, "отключили на 60 %% — не расстроен: %s", expression(eng.expression()).id);
    for (const uint32_t end = t + 60000; t < end; t += 50) bh.update(t), eng.update(t);
    const Expr after = bh.base_expression();
    CHECK(after != Expr::low_battery && after != Expr::hungry && after != Expr::critical_battery && !bh.eco(),
          "после неполной зарядки лицо обычное: %s", expression(after).id);
  }
  // Вместе с датчиком: ячейка набралась до 3.85 В — голода нет; почти пустая — «мало» снова.
  for (int full = 0; full < 2; ++full) {
    FaceEngine eng;
    Behavior bh;
    BatterySensor s;
    Event ev[kSenseEvents];
    eng.init(9, 0);
    bh.init(&eng, 9, 0);
    bh.set_clock(12 * 60);
    uint32_t t = 0;
    auto feed = [&](float v, bool usb) {
      const int n = s.feed(v, usb, usb, t, ev);
      for (int i = 0; i < n; ++i) bh.on_event(ev[i], t);
      for (const uint32_t end = t + 10000; t < end; t += 100) bh.update(t), eng.update(t);
    };
    for (int i = 0; i < 6; ++i) feed(3.55f, false);
    CHECK(bh.battery == 1, "почти пустая — мало заряда");
    for (int i = 0; i < 30; ++i) feed(4.9f, true);
    for (int i = 0; i < 10; ++i) feed(full ? 3.85f : 3.55f, false);
    CHECK(bh.battery == (full ? 2 : 1), "%s: battery %d", full ? "подзарядили до 60 %" : "не набралась",
          (int)bh.battery);
  }
}

// Дуэль — тоже встреча: дружба растёт с баннером и опытом, как от приветствий.
static void friendship_days(bool duels, int days, int* banners, Progress& pr, Friends& fr, uint32_t peer) {
  FaceEngine eng;
  Behavior bh;
  pr.reset();
  fr.reset();
  eng.init(6, 0);
  bh.init(&eng, 6, 0);
  bh.attach_progress(&pr);
  bh.attach_friends(&fr);
  bh.set_clock(12 * 60);
  *banners = 0;
  uint32_t t = 1000;
  for (int day = 0; day < days; ++day) {
    bh.new_day(t);
    if (duels)
      bh.duel("PE-0001", day % 2 == 0, t, peer, 100u + (uint32_t)day);
    else
      bh.meet("PE-0001", day == 0, t, peer);
    SceneLog log;
    run_log(bh, eng, t, t + 60000, log, 50);
    for (int i = 0; i < log.n; ++i)
      if (log.seen[i] == Scene::banner && log.params[i].value == (int)BannerIcon::Heart) ++*banners;
    t += 60000;
  }
}

static void test_duel_friendship() {
  const uint32_t peer = 0x5151u;
  Progress pm, pd;
  Friends fm, fd;
  int bm = 0, bd = 0;
  friendship_days(false, 15, &bm, pm, fm, peer);
  friendship_days(true, 15, &bd, pd, fd, peer);
  CHECK(bm == 3 && bd == 3, "BUDDY, FRIENDS, BEST FRIENDS — и от встреч, и от дуэлей (%d, %d)", bm, bd);
  CHECK(fd.level(peer) == 3 && pd.earned(Ach::best_friend), "дуэли каждый день — лучшие друзья (уровень %d)",
        fd.level(peer));
  CHECK(pm.earned(Ach::best_friend), "встречи каждый день — лучшие друзья");
  CHECK(pd.today_known_friends == 1, "дуэль со старым знакомым — его дневной опыт (%u)",
        (unsigned)pd.today_known_friends);
  // Рост дружбы от дуэли даёт 15 × уровень опыта, как от встречи.
  {
    FaceEngine eng;
    Behavior bh;
    Progress pr;
    Friends fr;
    pr.reset();
    fr.reset();
    eng.init(6, 0);
    bh.init(&eng, 6, 0);
    bh.attach_progress(&pr);
    bh.attach_friends(&fr);
    for (int d = 0; d < 2; ++d) {
      bh.new_day(1000);
      fr.meet(peer, pr.days);
    }
    bh.new_day(1000);  // третий день встреч — BUDDY
    const uint32_t xp0 = pr.xp;
    bh.duel("PE-0001", false, 2000, peer, 7);
    CHECK(pr.xp >= xp0 + XpRules{}.duel_lose + 15u + XpRules{}.friend_known, "опыт за дуэль, BUDDY и знакомого: +%u",
          (unsigned)(pr.xp - xp0));
  }
  // Близнец, легенда и подарок старшего — и после дуэли (короткая встреча бывает только дуэлью).
  {
    FaceEngine eng;
    Behavior bh;
    Progress pr;
    Friends fr;
    pr.reset();
    fr.reset();
    eng.init(7, 0);
    bh.init(&eng, 7, 0);
    bh.attach_progress(&pr);
    bh.attach_friends(&fr);
    bh.set_temperament(&temperament(Archetype::curious));
    uint32_t legend = 1;
    while (identity_from_hash(legend).rarity != Rarity::Legendary) ++legend;
    bh.duel("LG-0001", true, 1000, legend, 5);
    bh.duel("TW-0002", false, 2000, hash_of(Archetype::curious), 6, (uint8_t)(pr.implant + 3));
    bool gift = false;
    for (uint32_t t = 2000; t < 120000; t += 50) {
      bh.update(t);
      eng.update(t);
      gift |= eng.scene() == Scene::banner && !strcmp(eng.scene_params().text, "GIFT");
    }
    CHECK(pr.earned(Ach::legend) && pr.earned(Ach::twin), "после дуэли — легенда и близнец");
    CHECK(gift, "старший соперник дарит эмоцию и после дуэли");
  }
}

// «Тыкают без остановки» — от 9 тапов за 3 с; когда злость почти прошла, ласковый тап — неловко.
static void test_tap_frenzy() {
  int rage = 0, furious = 0, flip = 0, sorry = 0;
  for (uint32_t seed = 1; seed <= 16; ++seed) {
    FaceEngine eng;
    Behavior bh;
    eng.init(seed, 0);
    bh.init(&eng, seed, 0);
    bh.set_clock(12 * 60);
    uint32_t t = 5000;
    for (int i = 0; i < 5; ++i, t += 450) bh.on_event(Event::double_tap, t);  // 10 касаний за 1.8 с
    const Expr x = eng.expression();
    rage += x == Expr::rage, furious += x == Expr::furious, flip += x == Expr::table_flip;
    CHECK(x == Expr::rage || x == Expr::furious || x == Expr::table_flip, "10 касаний за 2 с — ярость: %s",
          expression(x).id);
    t += 25000;
    bh.update(t);
    bh.on_event(Event::tap, t);
    const Expr y = eng.expression();
    sorry += y == Expr::apologetic || y == Expr::guilty || y == Expr::embarrassed;
  }
  CHECK(rage && furious && flip, "ярость, FFFUUU и переворот стола выпадают все (%d, %d, %d)", rage, furious, flip);
  CHECK(sorry >= 4, "после вспышки ласковый тап — неловко: %d из 16", sorry);
}

// Походка: остановился — «покой» сразу, с бега на шаг — «идём»; у границы бега не мигает.
static void test_gait_events() {
  Event ev[kSenseEvents];
  MotionSensor m;
  uint32_t t = 0;
  int walk = 0, run = 0, still = 0;
  uint32_t still_at = 0, last_step = 0, next_step = 0, bump_end = 0;
  auto go = [&](uint32_t ms, uint32_t period) {
    if (period && (int32_t)(next_step - t) < 0) next_step = t;
    for (const uint32_t end = t + ms; t < end; t += 10) {
      if (period && t >= next_step) bump_end = t + 80, last_step = t, next_step += period;  // шаг без скачка фазы
      const bool bump = t < bump_end;
      const int n = m.feed(0.0f, bump ? 1.4f : 1.0f, 0.0f, t, ev);
      walk += sense_count(ev, n, Event::walk);
      run += sense_count(ev, n, Event::run);
      if (sense_count(ev, n, Event::still) && !still_at) still_at = t;
      still += sense_count(ev, n, Event::still);
    }
  };
  go(1000, 0);
  go(8000, 500);  // 2 шага в секунду
  CHECK(walk == 1 && run == 0, "ходьба (%d, %d)", walk, run);
  go(5000, 0);
  CHECK(still == 1 && still_at - last_step <= 3000, "остановился — «покой» через %u мс после шага",
        (unsigned)(still_at - last_step));
  walk = run = 0;
  go(8000, 300);  // 3.3 шага в секунду
  CHECK(run == 1 && walk == 0, "бег (%d, %d)", walk, run);
  go(10000, 500);
  CHECK(run == 1 && walk == 1, "с бега на шаг — «идём» (%d, %d)", walk, run);
  go(8000, 300);
  walk = run = 0;
  go(20000, 385);  // 2.6 шага в секунду — у самой границы
  go(20000, 370);
  CHECK(walk + run == 0, "у границы бег и шаг не мигают (%d, %d)", walk, run);

  // Поведение: остановился — лицо уже не «идёт»; бег не прячет голод; долгий бег — выдохся.
  FaceEngine eng;
  Behavior bh;
  eng.init(4, 0);
  bh.init(&eng, 4, 0);
  bh.set_clock(12 * 60);
  uint32_t u = 1000;
  auto run_for = [&](uint32_t ms) {
    for (const uint32_t end = u + ms; u < end; u += 50) bh.update(u), eng.update(u);
  };
  bh.on_event(Event::walk, u);
  run_for(10000);
  CHECK(bh.base_expression() == Expr::walking, "идёт: %s", expression(bh.base_expression()).id);
  bh.on_event(Event::still, u);
  run_for(10000);
  CHECK(bh.base_expression() != Expr::walking, "остановился: %s", expression(bh.base_expression()).id);
  bh.on_event(Event::run, u);
  run_for(60000);
  CHECK(bh.base_expression() == Expr::running, "бежит: %s", expression(bh.base_expression()).id);
  run_for(5u * 60000u);
  CHECK(bh.base_expression() == Expr::exhausted, "бежит дольше 5 минут — выдохся: %s",
        expression(bh.base_expression()).id);
  bh.on_event(Event::battery_low, u);
  run_for(30000);
  CHECK(bh.base_expression() == Expr::low_battery || bh.base_expression() == Expr::hungry,
        "на бегу мало заряда — голодное лицо: %s", expression(bh.base_expression()).id);

  // «Пошли гулять» — после перерыва больше 2 минут, а не на каждом светофоре долгой прогулки.
  int again = 0, fresh = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed) {
    FaceEngine e2;
    Behavior b2;
    e2.init(seed, 0);
    b2.init(&e2, seed, 0);
    b2.set_clock(12 * 60);
    uint32_t v = 1000;
    b2.on_event(Event::walk, v);
    for (const uint32_t end = v + 30u * 60000u; v < end; v += 1000) b2.update(v), e2.update(v);
    b2.on_event(Event::still, v);
    v += 10000;
    b2.on_event(Event::walk, v);
    again += !strcmp(b2.last_reason(), "пошли гулять");
    b2.on_event(Event::still, v += 1000);
    v += 3u * 60000u;
    b2.on_event(Event::walk, v);
    fresh += !strcmp(b2.last_reason(), "пошли гулять");
  }
  CHECK(again == 0 && fresh > 0, "постоял 10 с — не «пошли гулять» (%d); после 3 минут — бывает (%d)", again, fresh);
}

// Кормить можно раз в 3 часа: дёргать кабель ради опыта и CARING HUMAN бесполезно.
static void test_feed_cooldown() {
  FaceEngine eng;
  Behavior bh;
  Progress pr;
  pr.reset();
  eng.init(9, 0);
  bh.init(&eng, 9, 0);
  bh.attach_progress(&pr);
  bh.set_clock(12 * 60);
  uint32_t t = 1000;
  auto at = [&](Event e, uint32_t ms) {
    for (const uint32_t end = t + ms; t < end; t += ms > 600000 ? 1000 : 100) bh.update(t), eng.update(t);
    bh.on_event(e, t);
  };
  for (int i = 0; i < 6; ++i) {
    at(Event::battery_low, 10000);  // после отключения датчик снова говорит «мало»
    at(Event::charge_start, 10000);
    at(Event::charge_stop, 60000);
  }
  CHECK(pr.feeds == 1 && !pr.earned(Ach::caring), "шесть раз дёрнули кабель — покормили один раз (%u)",
        (unsigned)pr.feeds);
  at(Event::battery_low, 3u * 3600000u);
  bh.set_clock(15 * 60 + 10);  // прошивка ведёт минуты суток
  at(Event::charge_start, 10000);
  CHECK(pr.feeds == 2, "через 3 часа — снова (%u)", (unsigned)pr.feeds);

  // Выключили кнопкой и включили: поведение и датчик батареи начинают заново, а запись роста —
  // та же. Отметка о кормлении в ней, и перезагрузки не кормят.
  Progress saved = pr;
  for (int i = 0; i < 5; ++i) {
    saved.seal();
    Progress loaded;
    CHECK(loaded.load(&saved, sizeof saved), "запись роста читается");
    FaceEngine e2;
    Behavior b2;
    e2.init(20 + (uint32_t)i, 0);
    b2.init(&e2, 20 + (uint32_t)i, 0);
    b2.attach_progress(&loaded);
    b2.set_clock((uint16_t)(15 * 60 + 20 + i * 2));  // минуты суток идут дальше с того же места
    b2.on_event(Event::battery_low, 31000);
    b2.on_event(Event::charge_start, 41000);
    saved = loaded;
  }
  CHECK(saved.feeds == 2 && !saved.earned(Ach::caring), "пять перезагрузок с кабелем — не кормление (%u)",
        (unsigned)saved.feeds);
  CHECK(!saved.may_feed(18 * 60 + 5) && saved.may_feed(18 * 60 + 15), "снова — не раньше чем через 3 часа");
  saved.new_day();
  CHECK(saved.may_feed(0), "новые сутки — снова можно");
}

// День рождения хозяина — важнее праздника в тот же день; новые сутки без часов открывают
// пасхалки времени заново.
static void test_birthday_first() {
  const uint8_t days[][2] = {{1, 1}, {12, 31}, {10, 31}, {2, 14}, {4, 1}};
  for (const auto& d : days) {
    FaceEngine eng;
    Behavior bh;
    eng.init(2, 0);
    bh.init(&eng, 2, 0);
    bh.set_clock(12 * 60);
    bh.set_birthday(d[0], d[1]);
    bh.set_date(2026, d[0], d[1]);
    SceneLog log;
    run_log(bh, eng, 0, 20000, log, 50);
    CHECK(log.has(Scene::birthday) && !log.has(Scene::new_year) && !log.has(Scene::halloween) && !log.has(Scene::bsod),
          "день рождения %02u.%02u — торт, а не праздник", d[1], d[0]);
  }
  FaceEngine eng;
  Behavior bh;
  eng.init(2, 0);
  bh.init(&eng, 2, 0);
  bh.set_clock(13 * 60 + 37);
  SceneLog a, b;
  run_log(bh, eng, 0, 15000, a, 50);
  bh.new_day(15000);  // сутки по счётчику, а часы полночь не проходили
  run_log(bh, eng, 15000, 30000, b, 50);
  CHECK(a.has(Scene::banner) && b.has(Scene::banner), "13:37 на следующие сутки — снова 1337");
}

// Сосед с меньшим позывным слышен, но сцену не объявляет (в темноте или ночью не слушает эфир,
// слышит нас слабее порога): частое прослушивание кончается через wait_ms, повторы — всё реже.
static void test_encounter_busy_bound() {
  const Encounters::Config cfg;
  const uint32_t lo = 0x10002000u, hi = 0x90003000u;
  {
    Encounters b;
    b.init(hi);
    b.set_self(1, kAdvDuelReady, 128);
    Advert a;
    a.hash = lo, a.level = 1, a.flags = kAdvDuelReady;
    uint32_t busy_ms = 0, first_off = 0, on_at[10] = {};
    int ons = 0, scenes = 0;
    bool was = false;
    Meeting m;
    for (uint32_t t = 0; t < 3600000u; t += 100) {
      if (t % 1000 == 0) b.on_advert(a, -60, t);
      scenes += b.poll(t, &m);
      const bool busy = b.wants_busy(t);
      busy_ms += busy ? 100 : 0;
      if (busy && !was && ons < 10) on_at[ons++] = t;
      if (!busy && was && !first_off) first_off = t;
      was = busy;
    }
    CHECK(scenes == 0, "сосед не объявляет — сцен нет");
    CHECK(first_off && first_off <= 1000 + cfg.wait_ms + 1000, "часто слушает не дольше %u с: %u мс",
          (unsigned)(cfg.wait_ms / 1000), (unsigned)first_off);
    // Окно ожидания — от конца перерыва; шаг опроса здесь 100 мс.
    const uint32_t w = cfg.wait_ms - 200;
    CHECK(ons >= 5 && on_at[1] - on_at[0] >= w + 60000 && on_at[2] - on_at[1] >= w + 120000 &&
              on_at[3] - on_at[2] >= w + 240000 && on_at[4] - on_at[3] >= w + 480000 &&
              on_at[4] - on_at[3] < cfg.wait_ms + 490000,
          "повторы через 1, 2, 4, 8 минут: %u, %u, %u, %u с", (unsigned)((on_at[1] - on_at[0]) / 1000),
          (unsigned)((on_at[2] - on_at[1]) / 1000), (unsigned)((on_at[3] - on_at[2]) / 1000),
          (unsigned)((on_at[4] - on_at[3]) / 1000));
    CHECK(busy_ms < 3600000u / 8, "за час часто слушает меньше 1/8 времени: %u с", (unsigned)(busy_ms / 1000));
  }
  {
    // Несимметричный сигнал: A слышит B слабее порога, B слышит A — встреч нет, а B не залипает.
    Encounters a, b;
    a.init(lo), b.init(hi);
    a.set_self(1, kAdvDuelReady, 128), b.set_self(1, kAdvDuelReady, 128);
    uint32_t busy_a = 0, busy_b = 0;
    int scenes = 0;
    Meeting m;
    for (uint32_t t = 0; t < 3600000u; t += 100) {
      if (t % 1000 == 0) b.on_advert(a.advert(t), -65, t);
      if (t % 1000 == 500) a.on_advert(b.advert(t), -75, t);
      scenes += a.poll(t, &m) + b.poll(t, &m);
      busy_a += a.wants_busy(t) ? 100 : 0;
      busy_b += b.wants_busy(t) ? 100 : 0;
    }
    CHECK(scenes == 0 && busy_a == 0, "A нас не слышит: сцен нет, сам эфир не слушает (%d, %u)", scenes,
          (unsigned)busy_a);
    CHECK(busy_b < 3600000u / 8, "B часто слушает меньше 1/8 часа: %u с", (unsigned)(busy_b / 1000));
  }
  {
    // Объявляющий слушает эфир раз в 30 с и услышал нас поздно: встреча всё равно у обоих.
    Encounters a, b;
    a.init(lo), b.init(hi);
    a.set_self(1, 0, 128), b.set_self(1, 0, 128);
    Meeting m;
    int ga = 0, gb = 0;
    for (uint32_t t = 0; t < 120000; t += 50) {
      if (t % 1000 == 0) b.on_advert(a.advert(t), -60, t);
      if (t >= 31000 && t % 500 == 250) a.on_advert(b.advert(t), -60, t);
      ga += a.poll(t, &m);
      gb += b.poll(t, &m);
    }
    CHECK(ga == 1 && gb == 1, "поздно услышал — встреча у обоих (%d, %d)", ga, gb);
  }
  {
    // После перерыва без сцены сосед, который начал объявлять, встречается сразу.
    Encounters a, b;
    a.init(lo), b.init(hi);
    a.set_self(1, 0, 128), b.set_self(1, 0, 128);
    Meeting m;
    int ga = 0, gb = 0;
    for (uint32_t t = 0; t < 300000; t += 50) {
      if (t % 1000 == 0) b.on_advert(a.advert(t), -60, t);
      if (t >= 110000 && t % 500 == 250) a.on_advert(b.advert(t), -60, t);  // A вышел из темноты
      ga += a.poll(t, &m);
      gb += b.poll(t, &m);
    }
    CHECK(ga == 1 && gb == 1, "вышел из темноты после перерыва — встреча у обоих (%d, %d)", ga, gb);
  }
}

// Брелоки лежат рядом весь день: перерыв с тем же соседом растёт 10, 20, 40, 80, 160 минут.
// Порознь дольше серии или новые сутки — снова 10 минут.
static void test_encounter_cooldown_series() {
  const Encounters::Config cfg;
  Encounters a, b;
  a.init(0x100u), b.init(0x900u);
  a.set_self(1, 0, 128), b.set_self(1, 0, 128);
  uint32_t t = 0, times[16] = {};
  int na = 0, nb = 0;
  auto together = [&](uint32_t ms) {
    for (const uint32_t end = t + ms; t < end; t += 50) {
      if (t % 2000 == 0) b.on_advert(a.advert(t), -60, t);
      if (t % 2000 == 1000) a.on_advert(b.advert(t), -60, t);
      Meeting m;
      if (a.poll(t, &m) && na < 16) times[na++] = t;
      nb += b.poll(t, &m);
    }
  };
  together(8u * 3600000u);
  const uint32_t expect[] = {10, 20, 40, 80, 160, 160};
  bool ok = na == 7 && nb == 7;
  for (int i = 0; ok && i < 6; ++i) {
    const uint32_t gap = times[i + 1] - times[i], want = expect[i] * 60000u;
    ok = gap >= want && gap < want + 60000u;
  }
  CHECK(ok, "8 часов рядом: %d и %d встреч, перерывы %u, %u, %u, %u, %u, %u мин", na, nb,
        (unsigned)((times[1] - times[0]) / 60000), (unsigned)((times[2] - times[1]) / 60000),
        (unsigned)((times[3] - times[2]) / 60000), (unsigned)((times[4] - times[3]) / 60000),
        (unsigned)((times[5] - times[4]) / 60000), (unsigned)((times[6] - times[5]) / 60000));
  // Порознь дольше перерыва и серии: серия забыта.
  for (const uint32_t end = t + 160u * 60000u + cfg.series_ms + 60000u; t < end; t += 60000) {
    Meeting m;
    a.poll(t, &m), b.poll(t, &m);
  }
  na = nb = 0;
  together(25u * 60000u);
  CHECK(na == 2 && nb == 2 && times[1] - times[0] < 11u * 60000u, "после разлуки — снова через 10 минут (%d, %d)", na,
        nb);
  // Новые сутки: идущий перерыв (20 минут) доживает, следующий — снова 10 минут.
  a.new_day(), b.new_day();
  na = nb = 0;
  together(30u * 60000u);
  CHECK(na == 2 && nb == 2 && times[1] - times[0] < 11u * 60000u, "новые сутки — серия заново (%d, %d)", na, nb);
}

// Исход дуэли одинаков у обоих, даже если у кого-то посреди отсчёта встал новый имплант:
// уровни на время отсчёта замирают, объявивший пересчитывает исход по ответу «принял».
static void test_encounter_level_frozen() {
  int n = 0, same = 0, levels = 0;
  for (int mood = 0; mood < 256; mood += 3) {
    Encounters a, b;
    a.init(0x10002000u), b.init(0x90003000u);
    a.set_self(1, kAdvDuelReady, (uint8_t)mood), b.set_self(3, kAdvDuelReady, 128);
    Meeting ma, mb;
    bool ga = false, gb = false, a_up = false, b_mid = false, b_up = false;
    for (uint32_t t = 0; t < 20000; t += 50) {
      if (t % 1000 == 0) b.on_advert(a.advert(t), -60, t);
      if (t % 1000 == 500) a.on_advert(b.advert(t), -60, t);
      if (a.pending() && !a_up) a.set_self(9, kAdvDuelReady, (uint8_t)mood), a_up = true;  // новый имплант
      // У B имплант встал после последнего обычного пакета, который слышал A, но до того, как B
      // принял объявление: A считал исход по уровню 3, а B — по 10. Исход у A — по ответу.
      if (a.pending() && !b.pending() && !b_mid) b.set_self(10, kAdvDuelReady, 128), b_mid = true;
      if (b.pending() && !b_up) b.set_self(11, kAdvDuelReady, 128), b_up = true;  // посреди отсчёта
      Meeting m;
      if (!ga && a.poll(t, &m)) ma = m, ga = true;
      if (!gb && b.poll(t, &m)) mb = m, gb = true;
    }
    if (!(ga && gb && ma.kind == Rendezvous::Duel && mb.kind == Rendezvous::Duel)) continue;
    ++n;
    same += ma.won == mb.won;
    levels += ma.peer_level == 10 && mb.peer_level == 1;
  }
  CHECK(n == 86 && same == 0, "дуэлей %d, исход совпал у обоих в %d (должен быть ровно один победитель)", n, same);
  CHECK(levels == n, "уровень соседа — каким он был, когда отсчёт приняли (%d из %d)", levels, n);
}

// ---------------------------------------------------------------- все эмоции достижимы

// Жизнь брелока без прошивки: события по сценарию, время идёт, как спит прошивка (до срока
// поведения или конца сцены). Отмечает каждую показанную эмоцию, а развязку сцены — отдельно:
// так видно, какие эмоции бывают только после сцен.
struct Life {
  FaceEngine eng;
  Behavior bh;
  Rng rng;
  uint32_t t = 1000;
  bool* any;
  bool* outside;
  Expr last = Expr::Count;
  bool scene_on = false, after = false;

  Life(int arch, uint32_t seed, bool* any_, bool* outside_) : rng(seed), any(any_), outside(outside_) {
    eng.init(seed, 0);
    bh.init(&eng, seed, 0);
    if (arch >= 0) bh.set_temperament(&temperament((Archetype)arch));
    bh.set_identity_hash(arch >= 0 ? hash_of((Archetype)arch) : 0x1234u);
    bh.set_clock(12 * 60);
    bh.set_date(2026, 6, 17);  // будний день без праздников
  }
  void look(bool from_scene) {
    const Expr e = eng.expression();
    if (e == last) return;
    last = e;
    any[(int)e] = true;
    if (!from_scene) outside[(int)e] = true;
  }
  void ev(Event e) {
    bh.on_event(e, t);
    look(false);
  }
  void wait(uint32_t ms) {
    for (const uint32_t end = t + ms; time_left(t, end);) {
      bh.update(t);
      look(after);  // первая смена после конца сцены — её развязка
      after = false;
      eng.update(t);
      look(false);  // цепочка движка (смех → радость)
      const bool on = eng.scene() != Scene::None;
      if (scene_on && !on) after = true;
      scene_on = on;
      uint32_t nx = t + 1000;
      auto near = [&](uint32_t x) {
        if (time_left(t, x) && time_left(t, x) < nx - t) nx = x;
      };
      near(bh.next_update_ms());
      if (on) near(eng.scene_ends_ms());
      near(end);
      t = nx;
    }
  }
  void taps(int n, uint32_t gap) {
    for (int i = 0; i < n; ++i) ev(Event::tap), wait(gap);
  }
  void dtaps(int n, uint32_t gap) {
    for (int i = 0; i < n; ++i) ev(Event::double_tap), wait(gap);
  }
  // Касания всех видов: свой пул характера, двойные, тройные, серии и «неловко» после вспышки.
  void touches() {
    for (int i = 0; i < 16; ++i) ev(Event::tap), wait(25000 + rng.next() % 20000u);
    wait(11u * 60000u);
    ev(Event::tap);  // после долгой паузы
    wait(20000);
    for (int i = 0; i < 14; ++i) ev(Event::double_tap), wait(20000);
    for (int i = 0; i < 12; ++i) taps(3, 300), wait(20000);
    for (int k = 4; k <= 6; ++k)  // 5, 8 и 10 касаний подряд, потом ласковый тап
      for (int i = 0; i < 6; ++i) {
        if (k == 4) taps(5, 250);
        if (k == 5) dtaps(4, 450);
        if (k == 6) dtaps(5, 450);
        wait(25000);
        ev(Event::tap);
        wait(40000);
      }
  }
  // Занятия от скуки: внимание то есть, то нет.
  void idle(uint32_t minutes) {
    for (uint32_t m = 0; m < minutes; m += 10) {
      if (m % 60 < 30) {
        ev(Event::double_tap);
        wait(3000);
        ev(Event::pick_up);
        wait(10u * 60000u - 3000u);
      } else {
        wait(10u * 60000u);
      }
    }
  }
  void meetings() {
    uint32_t legend = 1;
    while (identity_from_hash(legend).rarity != Rarity::Legendary) ++legend;
    for (int a = 0; a < kArchetypeCount; ++a) {
      bh.meet("AB-0001", true, t, hash_of((Archetype)a), 0, 0);
      wait(20000);
      bh.meet("AB-0001", false, t, hash_of((Archetype)a), 0, 4);  // старые знакомые иногда шепчутся
      wait(30000);
    }
    bh.meet("LG-0001", true, t, legend);
    wait(20000);
    for (uint32_t i = 0; i < 8; ++i) {
      bh.duel("DU-0001", true, t, 0x5000u + i, 11 + i);
      wait(30000);
      bh.duel("DU-0002", false, t, 0x6000u + i, 31 + i);
      wait(30000);
    }
  }
  // Всё остальное: тряска, падения, звук, свет, температура, прогулки, батарея, кнопка, ночь,
  // праздники, пасхалки и сюрпризы.
  void world() {
    for (int i = 0; i < 6; ++i) ev(Event::shake), wait(20000);
    for (int i = 0; i < 8; ++i) ev(Event::shake_hard), wait(300), ev(Event::shake_hard), wait(20000);
    for (int i = 0; i < 6; ++i) {
      for (int k = 0; k < 3; ++k) ev(Event::shake_hard), wait(300);
      wait(30000);
    }
    for (int r = 0; r < 4; ++r) {
      for (int k = 0; k < 3; ++k) ev(Event::free_fall), wait(300), ev(Event::impact), wait(120000);
      wait(15u * 60000u);
    }
    for (int i = 0; i < 10; ++i) ev(Event::impact), wait(20000);
    for (int i = 0; i < 16; ++i) dtaps(1, 2000), ev(Event::pick_up), wait(20000);
    wait(15u * 60000u);
    ev(Event::pick_up);
    wait(20000);
    for (int i = 0; i < 3; ++i) {
      ev(Event::upside_down), wait(2000), ev(Event::upright), wait(10000);
      ev(Event::upside_down), wait(7000), ev(Event::upright), wait(10000);
    }
    for (int i = 0; i < 60; ++i) ev(Event::loud), wait(11000 + rng.next() % 9000u);
    for (int i = 0; i < 12; ++i) ev(Event::loud), wait(3000), ev(Event::loud), wait(15000);
    for (int i = 0; i < 8; ++i) ev(Event::speech), wait(10000), ev(Event::quiet), wait(10000);
    for (int i = 0; i < 10; ++i) ev(Event::music), wait(40000), ev(Event::quiet), wait(10000);
    for (int i = 0; i < 10; ++i) {
      ev(Event::dark), wait(3000), ev(Event::light), wait(10000);
      ev(Event::dark), wait(70000), ev(Event::light), wait(10000);
      ev(Event::bright), wait(10000), ev(Event::light), wait(5000);
    }
    ev(Event::dark), wait(20000), ev(Event::loud), wait(5000);  // спит — испугался
    wait(20000), ev(Event::music), wait(10000), ev(Event::quiet), ev(Event::light), wait(10000);
    for (int i = 0; i < 2; ++i) {
      ev(Event::cold), wait(60000), ev(Event::temp_ok), wait(10000);
      ev(Event::hot), wait(60000), ev(Event::temp_ok), wait(10000);
    }
    for (int i = 0; i < 6; ++i) {
      wait(3u * 60000u), ev(Event::walk), wait(60000), ev(Event::still);
      ev(Event::run), wait(30000), ev(Event::still);
    }
    ev(Event::run), wait(6u * 60000u), ev(Event::still), wait(10000);
    for (int i = 0; i < 2; ++i) {
      ev(Event::battery_low), wait(60000);
      bh.set_battery_percent(12);
      ev(Event::charge_start), wait(12u * 60000u);  // заставка на зарядке
      ev(Event::charge_full), wait(30000), ev(Event::charge_stop), wait(30000);
      ev(Event::battery_critical), wait(30000), ev(Event::charge_start), wait(60000);
      bh.set_battery_percent(30);
      ev(Event::charge_stop), wait(30000);
      bh.set_battery_percent(80);
    }
    for (int i = 0; i < 12; ++i)  // зарядили до конца: заряжен, «пог», $_$
      ev(Event::charge_start), wait(20000), ev(Event::charge_full), wait(10000), ev(Event::charge_stop), wait(10000);
    for (int i = 0; i < 8; ++i) ev(Event::button), wait(15000);
    ev(Event::button_long), wait(10000), ev(Event::button), wait(10000);
    // Ночь: с ним возятся в два часа, потом он засыпает; разбудили — ворчит; в 3:00 — пасхалка.
    bh.set_clock(2 * 60);
    taps(6, 30000);
    wait(3u * 60000u);
    wait(9u * 60000u);
    ev(Event::tap), wait(10000);
    bh.set_clock(3 * 60), wait(20000);
    bh.set_clock(23 * 60 + 30), idle(30);
    for (const uint16_t c : {4 * 60 + 4, 11 * 60 + 11, 12 * 60 + 34, 13 * 60 + 37}) bh.set_clock(c), wait(20000);
    bh.set_clock(12 * 60);
    const uint8_t dates[][2] = {{10, 31}, {12, 31}, {4, 1}, {2, 14}, {3, 14}, {4, 12}, {5, 4}, {5, 25}, {9, 13}, {11, 13}};
    for (const auto& d : dates) bh.set_date(2026, d[0], d[1]), wait(25000);
    bh.set_birthday(7, 1), bh.set_date(2026, 7, 1), wait(25000);
    bh.set_date(2028, 2, 29), wait(25000);
    bh.set_date(2026, 6, 17);
    bh.set_surprise_interval(60000, 120000, t);
    for (int i = 0; i < 30; ++i) ev(Event::tap), wait(90000);
    bh.set_surprise_interval(24u * 3600000u, 48u * 3600000u, t);
  }
  // Долго один: одиночество, обида, «эй!» на ходу; вернулся — растроган.
  void alone() {
    wait(270u * 60000u);
    ev(Event::tap), wait(20000);
    ev(Event::walk), wait(65u * 60000u), ev(Event::still), wait(10000);
  }
};

static void test_emotion_reachability() {
  bool any[kExprCount] = {}, outside[kExprCount] = {};
  {
    Life l(-1, 1, any, outside);  // «средний» персонаж симулятора
    l.touches(), l.meetings(), l.world(), l.idle(180), l.alone();  // циклоп — не раньше 2 часов
  }
  for (int a = 0; a < kArchetypeCount; ++a) {
    Life l(a, 100 + (uint32_t)a, any, outside);
    l.touches(), l.meetings(), l.idle(120);
    if (a == (int)Archetype::sleepy) l.world();  // свои лица в покое, радости и скуке — и в обстановке
  }
  // Только после сцен — нарочно: гордость и звёзды в глазах — за победу в дуэли, достижение
  // и встречу с легендой; призрак — на Хэллоуин и в пятницу 13-е. Всё остальное характер
  // показывает и сам: на касания, события, по обстановке и от скуки.
  const Expr scene_only[] = {Expr::proud, Expr::starstruck, Expr::spooky};
  int missing = 0, wrong = 0;
  for (int i = 0; i < kExprCount; ++i) {
    bool listed = false;
    for (Expr x : scene_only) listed |= (int)x == i;
    if (!any[i]) {
      ++missing;
      CHECK(false, "эмоцию %s характер не показывает никогда", expr_id((Expr)i));
    } else if (listed == outside[i]) {
      ++wrong;
      CHECK(false, "%s: %s", expr_id((Expr)i),
            listed ? "в списке «только после сцен», а бывает и сама" : "бывает только после сцен — добавь в список или в реакции");
    }
  }
  CHECK(missing == 0 && wrong == 0, "достижимы все %d эмоций, только после сцен — %d", kExprCount,
        (int)(sizeof scene_only / sizeof scene_only[0]));
}

int main() {
  test_framebuffer();
  test_catalog();
  test_every_expression_renders();
  test_symmetry();
  test_safe_area_and_strokes();
  test_transitions_settle();
  test_idle_scheduling();
  test_determinism();
  test_clips_finish();
  test_behavior_reactions();
  test_behavior_day();
  test_identity();
  test_progress();
  test_scenes();
  test_cyber_theme_and_tilt();
  test_reticle();
  test_cyber_pupil();
  test_growth_behavior();
  test_temperaments();
  test_meet_pairs();
  test_millis_wrap();
  test_blink_survives_clips();
  test_nonfinite_inputs();
  test_tilt_noise();
  test_shiver_wakes();
  test_overlay_clocks();
  test_progress_format();
  test_chain_follow_up();
  test_overlay_pop_after_idle();
  test_long_hold_precision();
  test_behavior_fresh_state();
  test_teeth_strokes();
  test_stubble_follows_face();
  test_wake_merge_and_horizon();
  test_calm_breathing();
  test_brightness_levels();
  test_clock_unknown();
  test_sound_cues();
  test_sound_playback();
  test_scene_music();
  test_chat_sync();
  test_encounter_packet();
  test_encounter_duel_fair();
  test_encounter_two_keychains();
  test_encounter_rules();
  test_encounter_attacks();
  test_encounter_rewards_capped();
  test_encounter_rate_limit();
  test_encounter_ack();
  test_encounter_cooldown_wrap();
  test_encounter_sync();
  test_motion_sensing();
  test_hearing();
  test_light_and_heat();
  test_battery_sensing();
  test_sensing_timing();
  test_behavior_voice();
  test_duel_then_chat();
  test_streak_and_achievements();
  test_progress_v2_migration();
  test_friends_table();
  test_retro_theme();
  test_badge();
  test_date_and_time_eggs();
  test_knock_code();
  test_battery_care();
  test_friendship_and_gifts();
  test_whisper_sync();
  test_battery_settle();
  test_battery_face_recovers();
  test_duel_friendship();
  test_tap_frenzy();
  test_gait_events();
  test_night_walk();
  test_feed_cooldown();
  test_birthday_first();
  test_encounter_busy_bound();
  test_encounter_cooldown_series();
  test_encounter_level_frozen();
  test_legend_greeting();
  test_emotion_reachability();
  printf("%d проверок, %d ошибок\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
