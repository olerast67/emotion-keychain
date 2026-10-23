#include "face/text.h"

namespace face {

namespace {

struct Glyph {
  char c;
  uint8_t rows[5];  // сверху вниз; бит 4 — левый столбец, бит 1 — правый
};

// clang-format off
const Glyph kGlyphs[] = {
    {'A', {2, 5, 7, 5, 5}}, {'B', {6, 5, 6, 5, 6}}, {'C', {3, 4, 4, 4, 3}}, {'D', {6, 5, 5, 5, 6}},
    {'E', {7, 4, 6, 4, 7}}, {'F', {7, 4, 6, 4, 4}}, {'G', {3, 4, 5, 5, 3}}, {'H', {5, 5, 7, 5, 5}},
    {'I', {7, 2, 2, 2, 7}}, {'J', {1, 1, 1, 5, 2}}, {'K', {5, 5, 6, 5, 5}}, {'L', {4, 4, 4, 4, 7}},
    {'M', {5, 7, 7, 5, 5}}, {'N', {6, 5, 5, 5, 5}}, {'O', {2, 5, 5, 5, 2}}, {'P', {6, 5, 6, 4, 4}},
    {'Q', {2, 5, 5, 6, 3}}, {'R', {6, 5, 6, 5, 5}}, {'S', {3, 4, 2, 1, 6}}, {'T', {7, 2, 2, 2, 2}},
    {'U', {5, 5, 5, 5, 7}}, {'V', {5, 5, 5, 5, 2}}, {'W', {5, 5, 7, 7, 5}}, {'X', {5, 5, 2, 5, 5}},
    {'Y', {5, 5, 2, 2, 2}}, {'Z', {7, 1, 2, 4, 7}},
    {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}}, {'2', {6, 1, 2, 4, 7}}, {'3', {6, 1, 2, 1, 6}},
    {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 6, 1, 6}}, {'6', {3, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}},
    {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 6}},
    {':', {0, 2, 0, 2, 0}}, {'.', {0, 0, 0, 0, 2}}, {',', {0, 0, 0, 2, 4}}, {'!', {2, 2, 2, 0, 2}},
    {'?', {6, 1, 2, 0, 2}}, {'-', {0, 0, 7, 0, 0}}, {'+', {0, 2, 7, 2, 0}}, {'=', {0, 7, 0, 7, 0}},
    {'/', {1, 1, 2, 4, 4}}, {'%', {5, 1, 2, 4, 5}}, {'>', {4, 2, 1, 2, 4}}, {'<', {1, 2, 4, 2, 1}},
    {'_', {0, 0, 0, 0, 7}}, {'#', {5, 7, 5, 7, 5}}, {'*', {5, 2, 7, 2, 5}}, {'[', {6, 4, 4, 4, 6}},
    {']', {3, 1, 1, 1, 3}}, {'\'', {2, 2, 0, 0, 0}},
};
// clang-format on

const Glyph* find(char c) {
  if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
  for (const Glyph& g : kGlyphs)
    if (g.c == c) return &g;
  return nullptr;
}

}  // namespace

int text_width(const char* s, int scale) {
  if (scale < 2) scale = 2;
  int n = 0;
  while (s[n]) ++n;
  return n ? (n * 4 - 1) * scale : 0;
}

void draw_text(Framebuffer& fb, int x, int y, const char* s, int scale, Ink ink, int visible) {
  if (scale < 2) scale = 2;
  for (int i = 0; s[i] && (visible < 0 || i < visible); ++i) {
    const Glyph* g = find(s[i]);
    if (!g) continue;
    for (int r = 0; r < 5; ++r)
      for (int c = 0; c < 3; ++c) {
        if (!(g->rows[r] & (4 >> c))) continue;
        const int px = x + (i * 4 + c) * scale, py = y + r * scale;
        for (int dy = 0; dy < scale; ++dy) fb.hspan(py + dy, px, px + scale - 1, ink);
      }
  }
}

void draw_text_centered(Framebuffer& fb, int cx, int y, const char* s, int scale, Ink ink, int visible) {
  draw_text(fb, cx - text_width(s, scale) / 2, y, s, scale, ink, visible);
}

}  // namespace face
