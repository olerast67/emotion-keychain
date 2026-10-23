#include "oled_look.h"

namespace sim {

std::vector<Rgb> oled_palette() {
  return {
      {0x05, 0x06, 0x08},  // kBg
      {0x12, 0x14, 0x18},  // kOff
      {0xF4, 0xF6, 0xF8},  // kLit
      {0x9C, 0xA3, 0xAD},  // kLitEdge
      {0x7C, 0x86, 0x94},  // kText
      {0xE8, 0xEC, 0xF2},  // kTextHi
      {0x1E, 0x22, 0x29},  // kBezel
      {0x5A, 0x2A, 0x2A},  // kDirty
      {0x1D, 0x3F, 0x8F},  // kPcb
      {0x14, 0x2F, 0x6C},  // kPcbEdge
      {0xC7, 0xA3, 0x5A},  // kPin
      {0xE8, 0xEE, 0xF7},  // kSilk
      {0x03, 0x04, 0x05},  // kGlass
  };
}

int module_w(int scale) { return (face::kWidth + 18) * scale; }
int module_h(int scale) { return (face::kHeight + 25) * scale; }

void draw_module(Canvas& cv, const face::Framebuffer& fb, int ox, int oy, int s, bool raw) {
  const int w = module_w(s), h = module_h(s);
  const int r = 2 * s;
  // Плата со скруглёнными углами и тёмной кромкой.
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      const int cx = x < r ? r - x : (x >= w - r ? x - (w - r - 1) : 0);
      const int cy = y < r ? r - y : (y >= h - r ? y - (h - r - 1) : 0);
      if (cx * cx + cy * cy > r * r) continue;
      const bool edge = x < 2 || y < 2 || x >= w - 2 || y >= h - 2 || cx * cx + cy * cy > (r - 2) * (r - 2);
      cv.set(ox + x, oy + y, edge ? kPcbEdge : kPcb);
    }
  }
  // Крепёжные отверстия с позолоченным ободком.
  const int hr = (int)(2.2f * s), hc = (int)(3.6f * s);
  const int holes[4][2] = {{hc, hc}, {w - hc, hc}, {hc, h - hc}, {w - hc, h - hc}};
  for (const auto& p : holes) {
    for (int y = -hr - 1; y <= hr + 1; ++y)
      for (int x = -hr - 1; x <= hr + 1; ++x) {
        const int d = x * x + y * y;
        if (d <= hr * hr) cv.set(ox + p[0] + x, oy + p[1] + y, d <= (hr - s) * (hr - s) ? kBg : kPin);
      }
  }
  // Пины GND VCC SCL SDA с подписями.
  const char* labels[4] = {"GND", "VCC", "SCL", "SDA"};
  const int ts = s >= 4 ? s / 2 : 1;
  const int step = 7 * s, x0 = w / 2 - step * 3 / 2;
  for (int i = 0; i < 4; ++i) {
    const int px = ox + x0 + i * step, py = oy + 2 * s;
    cv.rect(px - s, py, 2 * s, 2 * s, kPin);
    const std::string lb = labels[i];
    draw_text(cv, px - text_width(lb, ts) / 2, py + 3 * s, lb, ts, kSilk);
  }
  // Стекло и активная область.
  const int gx = ox + 6 * s, gy = oy + 11 * s;
  cv.rect(gx, gy, (face::kWidth + 6) * s, (face::kHeight + 6) * s, kGlass);
  draw_oled(cv, fb, gx + 3 * s, gy + 3 * s, s, raw);
}

void draw_oled(Canvas& cv, const face::Framebuffer& fb, int ox, int oy, int scale, bool raw) {
  for (int y = 0; y < face::kHeight; ++y) {
    for (int x = 0; x < face::kWidth; ++x) {
      const bool on = fb.get(x, y);
      const int px = ox + x * scale, py = oy + y * scale;
      if (raw || scale < 3) {
        cv.rect(px, py, scale, scale, on ? kLit : kOff);
        continue;
      }
      const int cell = scale - 1;  // зазор в 1 px справа и снизу
      cv.rect(px, py, cell, cell, on ? kLit : kOff);
      if (on && scale >= 5) {
        cv.set(px, py, kLitEdge);
        cv.set(px + cell - 1, py, kLitEdge);
        cv.set(px, py + cell - 1, kLitEdge);
        cv.set(px + cell - 1, py + cell - 1, kLitEdge);
      }
    }
  }
}

namespace {

struct Glyph {
  char c;
  uint8_t rows[5];
};

// Строки по 3 бита, старший бит слева.
const Glyph kFont[] = {
    {'a', {2, 5, 7, 5, 5}}, {'b', {6, 5, 6, 5, 6}}, {'c', {3, 4, 4, 4, 3}}, {'d', {6, 5, 5, 5, 6}},
    {'e', {7, 4, 6, 4, 7}}, {'f', {7, 4, 6, 4, 4}}, {'g', {3, 4, 5, 5, 3}}, {'h', {5, 5, 7, 5, 5}},
    {'i', {7, 2, 2, 2, 7}}, {'j', {1, 1, 1, 5, 2}}, {'k', {5, 5, 6, 5, 5}}, {'l', {4, 4, 4, 4, 7}},
    {'m', {5, 7, 7, 5, 5}}, {'n', {6, 5, 5, 5, 5}}, {'o', {2, 5, 5, 5, 2}}, {'p', {6, 5, 6, 4, 4}},
    {'q', {2, 5, 5, 6, 3}}, {'r', {6, 5, 6, 5, 5}}, {'s', {3, 4, 2, 1, 6}}, {'t', {7, 2, 2, 2, 2}},
    {'u', {5, 5, 5, 5, 7}}, {'v', {5, 5, 5, 5, 2}}, {'w', {5, 5, 7, 7, 5}}, {'x', {5, 5, 2, 5, 5}},
    {'y', {5, 5, 2, 2, 2}}, {'z', {7, 1, 2, 4, 7}}, {'0', {7, 5, 5, 5, 7}}, {'1', {2, 6, 2, 2, 7}},
    {'2', {6, 1, 2, 4, 7}}, {'3', {6, 1, 2, 1, 6}}, {'4', {5, 5, 7, 1, 1}}, {'5', {7, 4, 6, 1, 6}},
    {'6', {3, 4, 7, 5, 7}}, {'7', {7, 1, 2, 2, 2}}, {'8', {7, 5, 7, 5, 7}}, {'9', {7, 5, 7, 1, 6}},
    {'_', {0, 0, 0, 0, 7}}, {'-', {0, 0, 7, 0, 0}}, {'.', {0, 0, 0, 0, 2}}, {':', {0, 2, 0, 2, 0}},
    {'/', {1, 1, 2, 4, 4}}, {'%', {5, 1, 2, 4, 5}}, {'(', {2, 4, 4, 4, 2}}, {')', {2, 1, 1, 1, 2}},
    {'=', {0, 7, 0, 7, 0}}, {'+', {0, 2, 7, 2, 0}}, {' ', {0, 0, 0, 0, 0}},
};

const Glyph* find(char c) {
  if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
  for (const Glyph& g : kFont)
    if (g.c == c) return &g;
  return nullptr;
}

}  // namespace

int text_width(const std::string& s, int scale) { return (int)s.size() * 4 * scale - scale; }

void draw_text(Canvas& cv, int x, int y, const std::string& s, int scale, uint8_t color) {
  for (size_t i = 0; i < s.size(); ++i) {
    const Glyph* g = find(s[i]);
    if (!g) continue;
    for (int r = 0; r < 5; ++r)
      for (int c = 0; c < 3; ++c)
        if (g->rows[r] & (4 >> c)) cv.rect(x + ((int)i * 4 + c) * scale, y + r * scale, scale, scale, color);
  }
}

}  // namespace sim
