// Сцены-сюрпризы. Все надписи — шрифтом 3×5 с клетками 2×2, все линии не тоньше
// 2 px, всё держится внутри безопасного поля kSafeMargin, как и эмоции.
#include "face/scenes.h"

#include "face/engine.h"
#include "face/raster.h"
#include "face/render.h"
#include "face/text.h"

namespace face {

namespace {

const char* const kIds[] = {
#define FACE_X(n) #n,
    FACE_SCENE_LIST(FACE_X)
#undef FACE_X
};

const char* const kNames[] = {"Загрузка «OLERAST LINK»", "Взлом",    "Дождь данных", "ERROR 404",
                              "Перезагрузка",           "Прятки",   "Апгрейд",      "Новый год",
                              "День рождения",          "Хэллоуин", "Встреча",      "Дуэль"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kSceneCount, "имена сцен");

const uint16_t kDuration[] = {3800, 5200, 5000, 3600, 4200, 5200, 4600, 6000, 6000, 4500, 3200, 6500};
static_assert(sizeof(kDuration) / sizeof(kDuration[0]) == kSceneCount, "длительности сцен");

constexpr int kL = kSafeMargin, kR = kWidth - 1 - kSafeMargin, kT = kSafeMargin, kB = kHeight - 1 - kSafeMargin;
constexpr int kCx = kWidth / 2;

uint32_t hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

float phase(uint32_t t, uint32_t t0, uint32_t len) {
  if (t <= t0) return 0.0f;
  if (t >= t0 + len) return 1.0f;
  return (float)(t - t0) / (float)len;
}

bool blink_on(uint32_t t, uint32_t period) { return (t / period) % 2u == 0u; }

void fill_box(Framebuffer& fb, int x0, int y0, int x1, int y1, Ink ink = Ink::Set) {
  for (int y = y0; y <= y1; ++y) fb.hspan(y, x0, x1, ink);
}

// Рамка толщиной 2 px.
void frame_box(Framebuffer& fb, int x0, int y0, int x1, int y1) {
  fill_box(fb, x0, y0, x1, y0 + 1);
  fill_box(fb, x0, y1 - 1, x1, y1);
  fill_box(fb, x0, y0, x0 + 1, y1);
  fill_box(fb, x1 - 1, y0, x1, y1);
}

void progress_bar(Framebuffer& fb, int x0, int y0, int x1, int y1, float k) {
  frame_box(fb, x0, y0, x1, y1);
  const int ia = x0 + 4, ib = x1 - 4;
  const int fill = ia + iround((float)(ib - ia) * clampf(k, 0.0f, 1.0f));
  if (fill > ia) fill_box(fb, ia, y0 + 4, fill, y1 - 4);
}

// Сдвиг полосы строк вбок — «сбой сигнала».
void glitch_band(Framebuffer& fb, uint32_t seed) {
  const uint32_t h = hash(seed);
  const int y0 = kT + (int)(h % (uint32_t)(kB - kT - 6)), len = 2 + (int)((h >> 8) % 4u);
  const int sh = (int)((h >> 16) % 9u) - 4;
  if (!sh) return;
  for (int y = y0; y < y0 + len && y < kHeight; ++y) {
    bool row[kWidth];
    for (int x = 0; x < kWidth; ++x) row[x] = fb.get(x, y);
    for (int x = 0; x < kWidth; ++x) {
      const int sx = x - sh;
      fb.plot(x, y, (sx >= 0 && sx < kWidth && row[sx]) ? Ink::Set : Ink::Clear);
    }
  }
}

void draw_face(Framebuffer& fb, Face f) {
  Anchors a;
  render_face(fb, f, &a);
}

Face with_eyes(const Face& face, float open) {
  Face f = face;
  for (int i = 0; i < 2; ++i) f.eye[i].squash *= clampf(open, 0.0f, 1.0f);
  return f;
}

// ---------------------------------------------------------------- сцены

void boot(Framebuffer& fb, uint32_t t) {
  if (t < 300) return;
  const char* title = "OLERAST LINK";
  draw_text_centered(fb, kCx, kT + 6, title, 2, Ink::Set, (int)((t - 300) / 90));
  if (t >= 1100) progress_bar(fb, 16, 28, 111, 38, ease_in_out(phase(t, 1100, 1700)));
  if (t >= 2900 && (t > 3500 || blink_on(t - 2900, 150))) draw_text_centered(fb, kCx, 46, "ONLINE");
}

void hack(Framebuffer& fb, uint32_t t) {
  if (t < 3800) {
    static const char* const kWords[] = {"BYPASS ICE", "ROOT ACCESS", "DECRYPT KEY", "PORT 22 OPEN", "INJECT.EXE"};
    static const char kHex[] = "0123456789ABCDEF";
    const int lines = (int)(t / 170u) + 1;
    for (int r = 0; r < 4; ++r) {
      const int idx = lines - 4 + r;
      if (idx < 0) continue;
      char buf[16];
      const uint32_t h = hash((uint32_t)idx * 2654435761u + 7u);
      if (idx % 4 == 3) {
        const char* w = kWords[h % 5u];
        int i = 0;
        for (; w[i] && i < 14; ++i) buf[i] = w[i];
        buf[i] = 0;
      } else {
        // «> 3FA9 C20E 7B»
        const char pat[] = "> #### #### ##";
        uint32_t v = h;
        for (int i = 0; i < 15; ++i) {
          buf[i] = pat[i];
          if (pat[i] == '#') {
            buf[i] = kHex[v & 15u];
            v = (v >> 4) | (v << 28);
          }
        }
      }
      draw_text(fb, kL + 2, kT + 2 + r * 13, buf);
    }
    return;
  }
  if (t > 4400 || blink_on(t - 3800, 150)) {
    draw_text_centered(fb, kCx, 14, "ACCESS");
    draw_text_centered(fb, kCx, 32, "GRANTED");
  }
}

void data_rain(Framebuffer& fb, uint32_t t, const Face& face) {
  // Поток данных: столбцы нулей и единиц тем же шрифтом, что и надписи, падают с разной
  // скоростью; цифры иногда меняются. Под конец поток редеет и проявляется лицо.
  const float fade = 1.0f - phase(t, 3400, 900);
  constexpr int kCols = 9, kPitch = 12;
  for (int c = 0; c < kCols; ++c) {
    const uint32_t h = hash((uint32_t)c * 977u + 13u);
    if ((float)(h % 1000u) / 1000.0f > fade) continue;
    const int x = kL + 2 + c * 13 + (int)((h >> 4) % 3u);
    const float v = 0.018f + (float)(h % 25u) * 0.001f;  // px/мс
    const int len = 2 + (int)((h >> 20) % 3u);            // цифр в струе
    const int span = (kB - kT) + len * kPitch + 10;
    const int head = kT + (int)((float)t * v + (float)(h >> 10) * 0.01f) % span;
    for (int k = 0; k < len; ++k) {
      const int y = head - k * kPitch;
      if (y < kT || y + text_height() - 1 > kB) continue;
      const uint32_t g = hash(h + (uint32_t)(y / kPitch) * 31u + (t / 400u) * 7u);
      const char digit[2] = {(char)('0' + (g & 1u)), 0};
      draw_text(fb, x, y, digit);
    }
  }
  if (t >= 4100) draw_face(fb, with_eyes(face, ease_out(phase(t, 4100, 600))));
}

void error404(Framebuffer& fb, uint32_t t) {
  if (t < 2600) {
    draw_text_centered(fb, kCx, 10, "ERROR 404", 3);
    draw_text_centered(fb, kCx, 36, "MOOD NOT FOUND");
    glitch_band(fb, t / 120u);
    if ((t / 120u) % 3u == 0u) glitch_band(fb, t / 120u + 101u);
    return;
  }
  if (blink_on(t - 2600, 250)) draw_text(fb, kL + 4, 46, "_");
}

void reboot(Framebuffer& fb, uint32_t t, const Face& face) {
  if (t < 600) return draw_face(fb, with_eyes(face, 1.0f - ease_in(phase(t, 0, 600))));
  if (t < 1100) {
    // Кадр гаснет как старый кинескоп: в линию, потом в точку.
    const int w = iround(100.0f * (1.0f - ease_in(phase(t, 600, 500)))) + 4;
    fill_box(fb, kCx - w / 2, 31, kCx + w / 2, 32);
    return;
  }
  if (t < 1300) return fill_box(fb, kCx - 2, 30, kCx + 1, 33);
  if (t < 1700) return;
  if (t < 3400) {
    draw_text_centered(fb, kCx, 14, "REBOOT");
    progress_bar(fb, 24, 32, 103, 42, ease_in_out(phase(t, 1700, 1500)));
    return;
  }
  draw_face(fb, with_eyes(face, ease_out(phase(t, 3400, 600))));
}

void hide(Framebuffer& fb, uint32_t t, const Face& face) {
  Face f = face;
  if (t < 900) {
    f.oy += 70.0f * ease_in(phase(t, 0, 900));
  } else if (t < 2300) {
    return;
  } else if (t < 3500) {
    // Выглядывает снизу и косится по сторонам.
    f.oy += 70.0f - 30.0f * ease_out(phase(t, 2300, 300));
    f.gx = fsin((float)(t - 2300) * 0.004f) * 0.9f;
    f.gy = -0.6f;
  } else if (t < 4400) {
    f.oy += 40.0f * (1.0f - ease_out(phase(t, 3500, 900)));
  }
  draw_face(fb, f);
}

void upgrade(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  if (t > 600 || blink_on(t, 150)) draw_text_centered(fb, kCx, kT + 2, "UPGRADE");
  draw_text_centered(fb, kCx, 20, p.text[0] ? p.text : "IMPLANT");
  if (t < 3000) {
    if (t >= 500) progress_bar(fb, 14, 36, 113, 46, ease_in_out(phase(t, 500, 2300)));
  } else if (t > 3600 || blink_on(t - 3000, 150)) {
    draw_text_centered(fb, kCx, 38, "INSTALLED");
  }
}

void new_year(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  const float tx = 32;
  const Vec2 tiers[3][3] = {{{tx, 12}, {tx - 14, 29}, {tx + 14, 29}},
                            {{tx, 20}, {tx - 19, 41}, {tx + 19, 41}},
                            {{tx, 30}, {tx - 24, 52}, {tx + 24, 52}}};
  for (const auto& tr : tiers) fill_polygon(fb, tr, 3);
  fill_box(fb, (int)tx - 3, 52, (int)tx + 2, kB);
  // Гирлянда: огоньки-дырки 2×2 перемигиваются.
  static const int kLights[][2] = {{28, 25}, {35, 22}, {24, 36}, {38, 33}, {31, 39}, {20, 47}, {30, 45}, {41, 48}};
  for (int i = 0; i < 8; ++i)
    if (((t / 400u) + (uint32_t)i) % 2u)
      fill_box(fb, kLights[i][0], kLights[i][1], kLights[i][0] + 1, kLights[i][1] + 1, Ink::Clear);
  const float tw = 1.0f + 0.25f * fsin((float)t * 0.006f);
  fill_star(fb, tx, 10.5f, 5.0f * tw, 2.2f * tw, 5, 0.0f);
  const int cx = 92;
  draw_text_centered(fb, cx, 12, "HAPPY");
  draw_text_centered(fb, cx, 26, "NEW");
  draw_text_centered(fb, cx, 40, p.text[0] ? p.text : "YEAR");
}

void birthday(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  fill_box(fb, 12, 37, 56, kB);
  fill_box(fb, 12, 43, 56, 44, Ink::Clear);
  fill_box(fb, 12, 51, 56, 52, Ink::Clear);
  fill_box(fb, 18, 28, 50, 35);
  fill_box(fb, 32, 17, 35, 26);
  const float fl = fsin((float)t * 0.017f) * 0.6f + fsin((float)t * 0.031f) * 0.4f;
  fill_ellipse(fb, 33.5f, 11.5f + fl * 0.5f, 2.6f + fl * 0.4f, 4.2f);
  const int cx = 92;
  draw_text_centered(fb, cx, 14, "HAPPY");
  draw_text_centered(fb, cx, 30, "B-DAY");
  if (p.text[0]) draw_text_centered(fb, cx, 46, p.text);
  const float s1 = 3.0f + 2.0f * (0.5f + 0.5f * fsin((float)t * 0.005f));
  const float s2 = 3.0f + 2.0f * (0.5f + 0.5f * fsin((float)t * 0.005f + 2.0f));
  fill_star(fb, 66.0f, 9.0f, s1, fmax_(s1 * 0.4f, 1.3f), 4, 0.0f);
  fill_star(fb, 118.0f, 52.0f, s2, fmax_(s2 * 0.4f, 1.3f), 4, 0.0f);
}

void halloween(Framebuffer& fb, uint32_t t) {
  fill_box(fb, 38, 10, 42, 16);
  fill_ellipse(fb, 40.0f, 36.0f, 26.0f, 20.0f);
  const Vec2 le[3] = {{28, 25}, {21, 34}, {35, 34}};
  const Vec2 re[3] = {{52, 25}, {45, 34}, {59, 34}};
  const Vec2 mouth[9] = {{23, 42}, {29, 46}, {35, 42}, {40, 46}, {45, 42}, {51, 46}, {57, 42}, {54, 50}, {26, 50}};
  const Paint cut{Ink::Clear};
  fill_polygon(fb, le, 3, cut);
  fill_polygon(fb, re, 3, cut);
  fill_polygon(fb, mouth, 9, cut);
  // Свеча внутри мерцает: нос то тёмный, то светится.
  if ((t / 350u) % 3u) {
    const Vec2 nose[3] = {{40, 35}, {37, 40}, {43, 40}};
    fill_polygon(fb, nose, 3, cut);
  }
  draw_text_centered(fb, 98, 24, "BOO!", 3);
}

void greet(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  draw_text_centered(fb, kCx, kT + 4, p.value ? "HI AGAIN" : "NEW FRIEND", 2, Ink::Set, (int)(t / 70u));
  if (t >= 800) {
    // Позывной другого брелока, вокруг — прицел: уголки съезжаются, «цель захвачена».
    const char* code = p.text[0] ? p.text : "--";
    draw_text_centered(fb, kCx, 26, code);
    const float pad = 3.0f + 8.0f * (1.0f - ease_out(phase(t, 800, 350)));
    const int hw = text_width(code) / 2;
    const int x0 = imax(iround((float)(kCx - hw) - 3.0f - pad), kL), x1 = imin(iround((float)(kCx + hw) + 2.0f + pad), kR);
    const int y0 = imax(iround(23.0f - pad), kT), y1 = imin(iround(37.0f + pad), kB);
    const int len = 4;
    fill_box(fb, x0, y0, x0 + len, y0 + 1);
    fill_box(fb, x0, y0, x0 + 1, y0 + len);
    fill_box(fb, x1 - len, y0, x1, y0 + 1);
    fill_box(fb, x1 - 1, y0, x1, y0 + len);
    fill_box(fb, x0, y1 - 1, x0 + len, y1);
    fill_box(fb, x0, y1 - len, x0 + 1, y1);
    fill_box(fb, x1 - len, y1 - 1, x1, y1);
    fill_box(fb, x1 - 1, y1 - len, x1, y1);
  }
  if (t >= 1400) {
    const float k = 1.0f + 0.15f * fsin((float)(t - 1400) * 0.012f);
    fill_heart(fb, (float)kCx, 49.0f, 8.0f * k, 7.0f * k);
  }
}

void duel(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  if (t > 800 || blink_on(t, 150)) draw_text_centered(fb, kCx, kT, "DUEL", 3);
  const bool win = p.value != 0;
  if (t < 4600) {
    const float u = ease_in_out(phase(t, 800, 3800));
    const float wob = 0.05f * fsin((float)t * 0.011f);
    const float me = win ? u : u * 0.82f + wob;
    const float op = win ? u * 0.8f - wob : u;
    char tag[3] = {p.text[0] ? p.text[0] : '?', p.text[1] ? p.text[1] : '?', 0};
    draw_text(fb, kL, 26, "ME");
    draw_text(fb, kL, 40, tag);
    progress_bar(fb, 26, 25, kR, 35, me);
    progress_bar(fb, 26, 39, kR, 49, op);
    return;
  }
  if (t > 5400 || blink_on(t - 4600, 150)) draw_text_centered(fb, kCx, 32, win ? "WIN!" : "LOST", 3);
}

}  // namespace

const char* scene_id(Scene s) { return (int)s < kSceneCount ? kIds[(int)s] : "none"; }
const char* scene_name(Scene s) { return (int)s < kSceneCount ? kNames[(int)s] : "—"; }

bool scene_from_id(const char* id, Scene* out) {
  for (int i = 0; i < kSceneCount; ++i) {
    const char* a = kIds[i];
    int k = 0;
    while (a[k] && a[k] == id[k]) ++k;
    if (!a[k] && !id[k]) return *out = (Scene)i, true;
  }
  return false;
}

uint16_t scene_duration_ms(Scene s) { return (int)s < kSceneCount ? kDuration[(int)s] : 0; }

uint16_t scene_frame_ms(Scene s, uint32_t t) {
  switch (s) {
    case Scene::boot: return t < 2900 ? 50 : 75;
    case Scene::hack: return t < 3800 ? 85 : 75;
    case Scene::data_rain: return 50;
    case Scene::error404: return 60;
    case Scene::reboot:
    case Scene::hide: return 40;
    case Scene::upgrade: return 50;
    case Scene::new_year: return 100;
    case Scene::birthday: return 80;
    case Scene::halloween: return 120;
    case Scene::greet: return 60;
    case Scene::duel: return 60;
    default: return 100;
  }
}

void render_scene(Framebuffer& fb, Scene s, uint32_t t, const SceneParams& p, const Face& face) {
  switch (s) {
    case Scene::boot: boot(fb, t); break;
    case Scene::hack: hack(fb, t); break;
    case Scene::data_rain: data_rain(fb, t, face); break;
    case Scene::error404: error404(fb, t); break;
    case Scene::reboot: reboot(fb, t, face); break;
    case Scene::hide: hide(fb, t, face); break;
    case Scene::upgrade: upgrade(fb, t, p); break;
    case Scene::new_year: new_year(fb, t, p); break;
    case Scene::birthday: birthday(fb, t, p); break;
    case Scene::halloween: halloween(fb, t); break;
    case Scene::greet: greet(fb, t, p); break;
    case Scene::duel: duel(fb, t, p); break;
    default: break;
  }
}

}  // namespace face
