// Превью кадра «как на живом OLED»: крупные пиксели с зазором, белое свечение.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "face/framebuffer.h"
#include "gif_writer.h"

namespace sim {

enum Pal : uint8_t {
  kBg = 0,       // фон вокруг экрана
  kOff = 1,      // погашенный пиксель
  kLit = 2,      // горящий пиксель
  kLitEdge = 3,  // край горящего пикселя (скругление)
  kText = 4,     // подписи
  kTextHi = 5,
  kBezel = 6,    // рамка экрана
  kDirty = 7,    // подсветка изменённых окон
  kPcb = 8,      // плата модуля
  kPcbEdge = 9,  // кромка платы
  kPin = 10,     // позолота пинов и отверстий
  kSilk = 11,    // шелкография
  kGlass = 12,   // стекло вокруг активной области
};

std::vector<Rgb> oled_palette();

// Холст с индексами палитры.
struct Canvas {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  void init(int width, int height, uint8_t fill = kBg) {
    w = width;
    h = height;
    px.assign((size_t)w * h, fill);
  }
  void set(int x, int y, uint8_t c) {
    if (x >= 0 && y >= 0 && x < w && y < h) px[(size_t)y * w + x] = c;
  }
  void rect(int x, int y, int rw, int rh, uint8_t c) {
    for (int j = 0; j < rh; ++j)
      for (int i = 0; i < rw; ++i) set(x + i, y + j, c);
  }
};

// Модуль SSD1306 целиком: синяя плата, отверстия, пины с подписями, стекло.
// Размер: 146×89 при scale = 1 (умножается на scale).
int module_w(int scale);
int module_h(int scale);
void draw_module(Canvas& cv, const face::Framebuffer& fb, int ox, int oy, int scale, bool raw = false);

// raw = true — чистые квадраты без сетки (для чёткого просмотра).
void draw_oled(Canvas& cv, const face::Framebuffer& fb, int ox, int oy, int scale, bool raw = false);
// Подпись шрифтом 3×5 (латиница, цифры, _ - . : /), масштаб scale.
void draw_text(Canvas& cv, int x, int y, const std::string& s, int scale, uint8_t color);
int text_width(const std::string& s, int scale);

}  // namespace sim
