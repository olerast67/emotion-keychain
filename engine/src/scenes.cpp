// Сцены-сюрпризы. Все надписи — шрифтом 3×5 с клетками 2×2, все линии не тоньше
// 2 px, всё держится внутри безопасного поля kSafeMargin, как и эмоции.
#include "face/scenes.h"

#include "face/engine.h"
#include "face/raster.h"
#include "face/render.h"
#include "face/sound.h"
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
                              "День рождения",          "Хэллоуин", "Встреча",      "Дуэль",
                              "Секретный разговор",     "Достижение", "Зарядка",    "Выключение",
                              "Пасхалка",               "Синий экран", "Визор",     "Заставка"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == kSceneCount, "имена сцен");

// Разговор: четыре реплики, «расшифровка» и отказ в доступе.
constexpr uint32_t kChatDecryptMs = kChatMs, kChatFlashMs = kChatMs + 900, kChatDeniedMs = kChatMs + 1650;
constexpr uint16_t kChatSceneMs = (uint16_t)(kChatMs + 2600);

// Заставка: логотип отскакивает от краёв и к концу попадает точно в угол.
constexpr uint32_t kSaverHitMs = 10000, kSaverMs = 12000;

const uint16_t kDuration[] = {3800, 5200, 5000, 3600, 4200, 5200, 4600, 6000, 6000, 4500, 3200, 6500, kChatSceneMs,
                              4200, 3200, 3600, 4800, 5200, 3600, (uint16_t)kSaverMs};
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

void error404(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  if (t < 2600) {
    draw_text_centered(fb, kCx, 10, "ERROR 404", 3);
    draw_text_centered(fb, kCx, 36, p.sub[0] ? p.sub : "MOOD NOT FOUND");
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
    if (p.sub[0]) {
      // Характер соседа: «SHY», «GRUMPY»… или «TWIN!», если такой же, как ты.
      if (t > 1900 || blink_on(t - 1400, 120)) draw_text_centered(fb, kCx, 44, p.sub);
    } else {
      const float k = 1.0f + 0.15f * fsin((float)(t - 1400) * 0.012f);
      fill_heart(fb, (float)kCx, 49.0f, 8.0f * k, 7.0f * k);
    }
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

// Знаки шифра 5×5: не похожи на буквы шрифта 3×5, рисуются клетками 2×2.
// clang-format off
const uint8_t kRunes[kChatRunes][5] = {
    {0x04, 0x0A, 0x11, 0x0A, 0x04}, {0x04, 0x04, 0x1F, 0x04, 0x0A}, {0x15, 0x15, 0x1F, 0x04, 0x04},
    {0x1C, 0x04, 0x07, 0x04, 0x1C}, {0x04, 0x04, 0x1F, 0x11, 0x11}, {0x10, 0x10, 0x1F, 0x01, 0x01},
    {0x0E, 0x11, 0x15, 0x11, 0x0E}, {0x01, 0x02, 0x1F, 0x08, 0x10}, {0x1B, 0x0A, 0x04, 0x0A, 0x1B},
    {0x04, 0x0E, 0x15, 0x04, 0x04}, {0x1F, 0x0A, 0x04, 0x0A, 0x1F}, {0x1F, 0x15, 0x15, 0x15, 0x00},
    {0x15, 0x00, 0x15, 0x00, 0x15}, {0x04, 0x0A, 0x11, 0x1F, 0x00}, {0x10, 0x18, 0x14, 0x12, 0x1F},
    {0x0E, 0x0A, 0x1B, 0x0A, 0x0E},
};
// clang-format on

constexpr int kRunePx = 10, kRuneStep = 12;
constexpr int kBubbleW = kChatGlyphs * kRuneStep - 2 + 10, kBubbleH = 20;

void draw_rune(Framebuffer& fb, int x, int y, uint8_t g) {
  const uint8_t* r = kRunes[g % kChatRunes];
  for (int row = 0; row < 5; ++row)
    for (int col = 0; col < 5; ++col)
      if (r[row] & (0x10 >> col)) fill_box(fb, x + col * 2, y + row * 2, x + col * 2 + 1, y + row * 2 + 1);
}

// Пузырь реплики со скруглёнными углами; shown — сколько знаков уже «сказано».
void bubble(Framebuffer& fb, int x0, int y0, const uint8_t* g, int shown) {
  const int x1 = x0 + kBubbleW - 1, y1 = y0 + kBubbleH - 1;
  fill_box(fb, x0 + 2, y0, x1 - 2, y0 + 1);
  fill_box(fb, x0 + 2, y1 - 1, x1 - 2, y1);
  fill_box(fb, x0, y0 + 2, x0 + 1, y1 - 2);
  fill_box(fb, x1 - 1, y0 + 2, x1, y1 - 2);
  fill_box(fb, x0 + 1, y0 + 1, x0 + 2, y0 + 2);
  fill_box(fb, x1 - 2, y0 + 1, x1 - 1, y0 + 2);
  fill_box(fb, x0 + 1, y1 - 2, x0 + 2, y1 - 1);
  fill_box(fb, x1 - 2, y1 - 2, x1 - 1, y1 - 1);
  for (int k = 0; k < shown && k < kChatGlyphs; ++k) draw_rune(fb, x0 + 5 + k * kRuneStep, y0 + 5, g[k]);
}

void chat(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  const bool we_first = p.value != 0;
  if (t < kChatDecryptMs) {
    // Сверху — кто с кем: сосед слева, мы справа. Реплики идут пузырями, знаки появляются
    // вместе с нотами пищалки (те же длительности, что у Voice::chat).
    draw_text(fb, kL, kT, p.text[0] ? p.text : "??");
    draw_text(fb, kR + 1 - text_width("ME"), kT, "ME");
    const int line = (int)(t / kChatLineMs);
    const uint32_t lt = t - (uint32_t)line * kChatLineMs;
    for (int row = 0; row < 2; ++row) {
      const int i = line - 1 + row;
      if (i < 0) continue;
      uint8_t g[kChatGlyphs];
      const bool ours = chat_line(p.key, i, g) == we_first;
      int shown = kChatGlyphs;
      if (i == line) {
        uint32_t at = 0;
        shown = 0;
        while (shown < kChatGlyphs && lt >= at) at += chat_glyph_ms(g[shown++]);
      }
      bubble(fb, ours ? kR + 1 - kBubbleW : kL, row ? 39 : 17, g, shown);
    }
    return;
  }
  if (t < kChatFlashMs) {
    draw_text_centered(fb, kCx, 14, "DECRYPT");
    progress_bar(fb, 16, 32, 111, 42, ease_in(phase(t, kChatDecryptMs, kChatFlashMs - kChatDecryptMs)));
    return;
  }
  if (t < kChatDeniedMs) {
    // На миг проступает «расшифровка»: буквы по одной выходят из шума — и тут же «доступ запрещён».
    static const char kNoise[] = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789#%*";
    const char* phrase = chat_phrase(p.key);
    char buf[16];
    int n = 0;
    while (phrase[n] && n < 15) ++n;
    const int resolved = iround((float)n * phase(t, kChatFlashMs, 350));
    for (int i = 0; i < n; ++i) {
      const uint32_t h = hash(p.key + (uint32_t)i * 977u + (t / 40u) * 131u);
      buf[i] = (i < resolved || phrase[i] == ' ') ? phrase[i] : kNoise[h % (sizeof(kNoise) - 1)];
    }
    buf[n] = 0;
    draw_text_centered(fb, kCx, 27, buf);
    return;
  }
  if (t > kChatDeniedMs + 500 || blink_on(t - kChatDeniedMs, 120)) {
    draw_text_centered(fb, kCx, 14, "ACCESS");
    draw_text_centered(fb, kCx, 32, "DENIED");
  }
}

// Четырёхлучевая искра, мерцает со своей фазой.
void sparkle(Framebuffer& fb, float x, float y, uint32_t t, uint32_t phase_ms) {
  const float k = 0.5f + 0.5f * fsin((float)(t + phase_ms) * 0.009f);
  if (k > 0.15f) fill_star(fb, x, y, 2.0f + 3.0f * k, 1.3f, 4, 0.0f);
}

void achievement(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  if (t > 700 || blink_on(t, 140)) draw_text_centered(fb, kCx, kT, "ACHIEVEMENT");
  // Кубок вырастает из точки с лёгким перелётом и встаёт на место.
  const float u = phase(t, 0, 450);
  const float k = t < 450 ? 0.3f + 0.8f * ease_out(u) : 1.0f + 0.1f * fsin(fmin_(1.0f, (float)(t - 450) / 250.0f) * 3.1416f);
  auto P = [&](float x, float y) { return Vec2{kCx + (x - kCx) * k, 27.0f + (y - 27.0f) * k}; };
  const Vec2 hl = P(52, 21), hr = P(76, 21);
  ellipse_ring(fb, hl.x, hl.y, 5 * k, 5 * k, 2.2f);
  ellipse_ring(fb, hr.x, hr.y, 5 * k, 5 * k, 2.2f);
  const Vec2 bowl[6] = {P(53, 17), P(75, 17), P(73, 26), P(68, 30), P(60, 30), P(55, 26)};
  fill_polygon(fb, bowl, 6);
  const Vec2 star = P(64, 22.5f);
  fill_star(fb, star.x, star.y, 4.2f * k, 1.8f * k, 5, 0.0f, Paint{Ink::Clear});
  const Vec2 stem[4] = {P(62, 30), P(66, 30), P(66, 35), P(62, 35)};
  fill_polygon(fb, stem, 4);
  const Vec2 base[4] = {P(56, 35), P(72, 35), P(72, 38), P(56, 38)};
  fill_polygon(fb, base, 4);
  if (t >= 650) {
    sparkle(fb, 40, 18, t, 0);
    sparkle(fb, 88, 16, t, 400);
    sparkle(fb, 36, 34, t, 900);
    sparkle(fb, 92, 32, t, 1300);
  }
  if (t >= 900) draw_text_centered(fb, kCx, 44, p.text[0] ? p.text : "UNLOCKED", 2, Ink::Set, (int)((t - 900) / 60u));
}

void battery_body(Framebuffer& fb, int x0, int y0, int x1, int y1) {
  frame_box(fb, x0, y0, x1, y1);
  const int my = (y0 + y1) / 2;
  fill_box(fb, x1 + 1, my - 5, x1 + 4, my + 5);
}

void charge(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  battery_body(fb, 26, 14, 96, 42);
  const int pct = p.value < 0 ? -1 : imin(p.value, 100);
  // Заряд наливается до своего уровня, верхний край мигает: идёт зарядка.
  const float shown = (pct < 0 ? 60.0f : (float)pct) * ease_out(phase(t, 150, 1300));
  const int ia = 30, ib = 92;
  int fill = ia + iround((float)(ib - ia) * shown / 100.0f);
  if (t > 1450 && blink_on(t - 1450, 300)) fill = imin(fill + 6, ib);
  if (fill > ia) fill_box(fb, ia, 18, fill, 38);
  const Vec2 bolt[7] = {{67, 18}, {57, 29}, {63, 29}, {59, 38}, {71, 26}, {65, 26}, {69, 18}};
  fill_polygon(fb, bolt, 7, Paint{Ink::Invert});
  if (pct >= 0) {
    char buf[5];
    int n = 0;
    if (pct >= 100) buf[n++] = '1';
    if (pct >= 10) buf[n++] = (char)('0' + pct / 10 % 10);
    buf[n++] = (char)('0' + pct % 10);
    buf[n++] = '%';
    buf[n] = 0;
    draw_text_centered(fb, kCx, 47, buf);
  } else {
    draw_text_centered(fb, kCx, 47, "CHARGING");
  }
}

void shutdown(Framebuffer& fb, uint32_t t) {
  if (t < 1400) {
    if (blink_on(t, 220)) draw_text_centered(fb, kCx, 8, "LOW BATTERY");
    battery_body(fb, 40, 26, 84, 46);
    if (blink_on(t, 350)) fill_box(fb, 44, 30, 47, 42);
    return;
  }
  if (t < 2600) return draw_text_centered(fb, kCx, 27, "GOODNIGHT", 2, Ink::Set, (int)((t - 1400) / 80u));
  if (t < 3200) {
    const int w = iround(96.0f * (1.0f - ease_in(phase(t, 2600, 600)))) + 4;
    return fill_box(fb, kCx - w / 2, 31, kCx + w / 2, 32);
  }
  if (t < 3400) fill_box(fb, kCx - 2, 30, kCx + 1, 33);
}

void banner_icon(Framebuffer& fb, BannerIcon icon, uint32_t t) {
  const float cx = kCx, cy = 50;
  switch (icon) {
    case BannerIcon::Heart: {
      const float k = 1.0f + 0.15f * fsin((float)t * 0.012f);
      fill_heart(fb, cx, cy, 8.0f * k, 7.0f * k);
      break;
    }
    case BannerIcon::Gift:
      fill_box(fb, 54, 46, 73, 57);
      fill_box(fb, 52, 42, 75, 45);
      fill_box(fb, 62, 42, 65, 57, Ink::Clear);
      ellipse_ring(fb, 59, 39, 3.5f, 2.5f, 2.0f);
      ellipse_ring(fb, 68, 39, 3.5f, 2.5f, 2.0f);
      break;
    case BannerIcon::Star: {
      const float k = 1.0f + 0.2f * fsin((float)t * 0.008f);
      fill_star(fb, cx, cy, 8.0f * k, 3.5f * k, 5, 0.0f);
      break;
    }
    case BannerIcon::Rocket: {
      // Ракета стартует: чуть дрожит, пламя мерцает.
      const float j = (t / 60u) % 2u ? 0.5f : -0.5f;
      const Vec2 nose[3] = {{64 + j, 38}, {69 + j, 44}, {59 + j, 44}};
      fill_polygon(fb, nose, 3);
      fill_box(fb, 60 + iround(j), 44, 68 + iround(j), 53);
      fill_circle(fb, 64.5f + j, 47.5f, 1.8f, Paint{Ink::Clear});
      const Vec2 fl[3] = {{60 + j, 49}, {55 + j, 55}, {60 + j, 53}};
      const Vec2 fr[3] = {{69 + j, 49}, {74 + j, 55}, {69 + j, 53}};
      fill_polygon(fb, fl, 3);
      fill_polygon(fb, fr, 3);
      const float h = 3.0f + 2.0f * (0.5f + 0.5f * fsin((float)t * 0.05f));
      const Vec2 flame[3] = {{61 + j, 54}, {68 + j, 54}, {64.5f + j, 54 + h}};
      fill_polygon(fb, flame, 3);
      break;
    }
    case BannerIcon::Pi:
      fill_box(fb, 52, 42, 76, 44);
      fill_box(fb, 57, 45, 59, 57);
      capsule(fb, 69, 45, 69, 54, 3.0f);
      capsule(fb, 69, 54, 73, 57, 2.6f);
      break;
    case BannerIcon::Skull:
      fill_ellipse(fb, cx, 47, 9, 7);
      fill_box(fb, 58, 50, 69, 57);
      fill_circle(fb, 60, 47, 2.2f, Paint{Ink::Clear});
      fill_circle(fb, 68, 47, 2.2f, Paint{Ink::Clear});
      fill_box(fb, 61, 54, 62, 57, Ink::Clear);
      fill_box(fb, 65, 54, 66, 57, Ink::Clear);
      break;
    case BannerIcon::Sparkles:
    default:
      sparkle(fb, 34, 50, t, 0);
      sparkle(fb, 64, 52, t, 500);
      sparkle(fb, 94, 50, t, 1000);
      break;
  }
}

void banner(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  // Крупная строка печатается, под ней — мелкая, ещё ниже — значок.
  const char* big = p.text[0] ? p.text : "!!!";
  const int scale = text_width(big, 3) <= kR - kL ? 3 : 2;
  draw_text_centered(fb, kCx, scale == 3 ? 6 : 8, big, scale, Ink::Set, (int)((t + 70) / 70u));
  if (t >= 900 && p.sub[0]) draw_text_centered(fb, kCx, 26, p.sub, 2, Ink::Set, (int)((t - 900) / 60u));
  if (t >= 1400) banner_icon(fb, (BannerIcon)p.value, t);
}

void bsod(Framebuffer& fb, uint32_t t) {
  // «:(» и проценты, которые медленно ползут к 100.
  fill_box(fb, 8, 10, 13, 15);
  fill_box(fb, 8, 24, 13, 29);
  arc(fb, 33, 20, 10, 14, 1.9f, 4.4f, 3.0f);
  draw_text(fb, 44, 6, "YOUR");
  draw_text(fb, 44, 18, "KEYCHAIN");
  draw_text(fb, 44, 30, "CRASHED");
  if (t < 600) return;
  const int pct = imin((int)((t - 600) / 38u), 100);
  char buf[16];
  int n = 0;
  if (pct >= 100) buf[n++] = '1';
  if (pct >= 10) buf[n++] = (char)('0' + pct / 10 % 10);
  buf[n++] = (char)('0' + pct % 10);
  const char* tail = "% COMPLETE";
  for (int i = 0; tail[i]; ++i) buf[n++] = tail[i];
  buf[n] = 0;
  draw_text(fb, kL + 4, 46, buf);
}

void visor(Framebuffer& fb, uint32_t t) {
  // Сканер-визор: огонёк бегает туда-обратно, за ним гаснет след.
  frame_box(fb, 6, 22, 121, 41);
  constexpr int kW = 14, kX0 = 10, kSpan = 117 - kW - kX0;
  auto pos = [&](uint32_t tt) {
    const uint32_t m = tt % 1200u;
    const float u = m < 600u ? (float)m / 600.0f : (float)(1200u - m) / 600.0f;
    return kX0 + iround(ease_in_out(u) * (float)kSpan);
  };
  for (int k = 3; k >= 1; --k) {
    if (t < (uint32_t)(k * 60)) continue;
    const int x = pos(t - (uint32_t)(k * 60));
    fill_rrect(fb, (float)x + kW / 2.0f, 31.5f, kW / 2.0f, 5.5f, 2.0f, Paint{Ink::Set, 0.75f - 0.2f * (float)k});
  }
  const int x = pos(t);
  fill_rrect(fb, (float)x + kW / 2.0f, 31.5f, kW / 2.0f, 5.5f, 2.0f);
}

int fold(int s, int r) {
  const int m = s % (2 * r);
  return m <= r ? m : 2 * r - m;
}

void screensaver(Framebuffer& fb, uint32_t t, const SceneParams& p) {
  // Логотип летит под 45° и отскакивает от краёв. Пути подобраны так, что к концу он
  // попадает точно в угол: диапазоны 64 и 36 px встречаются через 576 px пути.
  const char* logo = p.text[0] ? p.text : "OLERAST";
  const int w = text_width(logo), rx = imin(64, kR - kL + 1 - w), ry = 36;
  const int s = 576 - iround((float)(kSaverHitMs - imin((int)t, (int)kSaverHitMs)) * 0.05f);
  const int x = kL + fold(s, rx), y = kT + fold(s, ry);
  if (t < kSaverHitMs || blink_on(t - kSaverHitMs, 160)) draw_text(fb, x, y, logo);
  if (t >= kSaverHitMs) {
    // Попал в угол! Искры вокруг.
    sparkle(fb, (float)(x + 8), (float)(y + 18), t, 0);
    sparkle(fb, (float)(x + w / 2), (float)(y + 22), t, 300);
    sparkle(fb, (float)(x + w - 8), (float)(y + 18), t, 600);
    draw_text_centered(fb, kCx, 46, "CORNER!");
  }
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
    case Scene::chat: return t < kChatDecryptMs ? 20 : 60;
    case Scene::achievement: return t < 900 ? 30 : 60;
    case Scene::charge: return 60;
    case Scene::shutdown: return 40;
    case Scene::banner: return 60;
    case Scene::bsod: return 100;
    case Scene::visor: return 30;
    case Scene::screensaver: return 40;
    default: return 100;
  }
}

void render_scene(Framebuffer& fb, Scene s, uint32_t t, const SceneParams& p, const Face& face) {
  switch (s) {
    case Scene::boot: boot(fb, t); break;
    case Scene::hack: hack(fb, t); break;
    case Scene::data_rain: data_rain(fb, t, face); break;
    case Scene::error404: error404(fb, t, p); break;
    case Scene::reboot: reboot(fb, t, face); break;
    case Scene::hide: hide(fb, t, face); break;
    case Scene::upgrade: upgrade(fb, t, p); break;
    case Scene::new_year: new_year(fb, t, p); break;
    case Scene::birthday: birthday(fb, t, p); break;
    case Scene::halloween: halloween(fb, t); break;
    case Scene::greet: greet(fb, t, p); break;
    case Scene::duel: duel(fb, t, p); break;
    case Scene::chat: chat(fb, t, p); break;
    case Scene::achievement: achievement(fb, t, p); break;
    case Scene::charge: charge(fb, t, p); break;
    case Scene::shutdown: shutdown(fb, t); break;
    case Scene::banner: banner(fb, t, p); break;
    case Scene::bsod: bsod(fb, t); break;
    case Scene::visor: visor(fb, t); break;
    case Scene::screensaver: screensaver(fb, t, p); break;
    default: break;
  }
}

}  // namespace face
