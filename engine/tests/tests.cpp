// Тесты движка без внешних зависимостей: ctest или ./face_tests.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "face/behavior.h"
#include "face/engine.h"
#include "face/identity.h"
#include "face/progress.h"
#include "face/render.h"
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
  printf("%d проверок, %d ошибок\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
