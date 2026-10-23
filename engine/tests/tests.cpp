// Тесты движка без внешних зависимостей: ctest или ./face_tests.
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "face/behavior.h"
#include "face/engine.h"

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
    CHECK(lit > 120, "%s: слишком мало пикселей (%d)", id, lit);
    CHECK(lit < kWidth * kHeight * 45 / 100, "%s: залито %d пикселей", id, lit);
  }
}

// Симметричные эмоции должны быть зеркальными: иначе лицо «косит».
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
  CHECK(eng.expression() == Expr::laugh, "двойной тап -> смех, а не %s", expr_id(eng.expression()));
  run(8000);
  for (int i = 0; i < 4; ++i) {
    bh.on_event(Event::shake, t);
    run(200);
  }
  CHECK(eng.expression() == Expr::dizzy, "тряска -> головокружение, а не %s", expr_id(eng.expression()));
  run(15000);
  bh.on_event(Event::dark, t);
  run(100);
  CHECK(eng.expression() == Expr::afraid_dark, "темнота -> испуг, а не %s", expr_id(eng.expression()));
  run(15000);
  CHECK(eng.expression() == Expr::sleeping, "в темноте засыпает, а не %s", expr_id(eng.expression()));
  bh.on_event(Event::light, t);
  run(100);
  CHECK(eng.expression() == Expr::curious || eng.expression() == Expr::waking, "свет будит, а не %s",
        expr_id(eng.expression()));
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

int main() {
  test_framebuffer();
  test_catalog();
  test_every_expression_renders();
  test_symmetry();
  test_transitions_settle();
  test_idle_scheduling();
  test_determinism();
  test_clips_finish();
  test_behavior_reactions();
  test_behavior_day();
  printf("%d проверок, %d ошибок\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
