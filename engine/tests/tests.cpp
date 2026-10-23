// Тесты движка без внешних зависимостей: ctest или ./face_tests.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "face/behavior.h"
#include "face/engine.h"
#include "face/identity.h"
#include "face/progress.h"
#include "face/scenes.h"
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
    // Летучая мышь по задумке пролетает через весь экран.
    const bool crosses = expression(e).overlays & ov(Overlay::Bat);
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
    if (!crosses) CHECK(margin >= kSafeMargin - 1, "%s: до края %d px", expression(e).id, margin);
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
        q.pupil_scale > 1.31f || q.blink_rate < 0.79f || q.blink_rate > 1.31f ||
        q.cyber_eye != (id.rarity == Rarity::Legendary))
      ranges = false;
  }
  CHECK(ranges, "особенности внешности в своих пределах");
  for (int i = 0; i < (int)Archetype::Count; ++i)
    CHECK(arch[i] > n / 12, "архетип %s встречается: %d", archetype_id((Archetype)i), arch[i]);
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
    if (expression(e).overlays & ov(Overlay::Bat)) continue;
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
  test_growth_behavior();
  printf("%d проверок, %d ошибок\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
