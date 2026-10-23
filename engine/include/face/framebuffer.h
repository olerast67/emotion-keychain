// Кадровый буфер 128×64 в родном формате SSD1306: 8 страниц по 8 строк,
// байт = вертикальный столбик из 8 пикселей, младший бит сверху.
// Такой буфер уходит в GDDRAM контроллера без перепаковки.
#pragma once

#include <stdint.h>

namespace face {

constexpr int kWidth = 128;
constexpr int kHeight = 64;
constexpr int kPages = kHeight / 8;
constexpr int kFrameBytes = kWidth * kPages;

enum class Ink : uint8_t { Set, Clear, Invert };

struct Framebuffer {
  uint8_t data[kFrameBytes];

  void clear();
  void fill(uint8_t pattern);
  void copy_from(const Framebuffer& other);

  inline bool get(int x, int y) const {
    if ((unsigned)x >= (unsigned)kWidth || (unsigned)y >= (unsigned)kHeight) return false;
    return (data[(y >> 3) * kWidth + x] >> (y & 7)) & 1u;
  }
  inline void plot(int x, int y, Ink ink) {
    if ((unsigned)x >= (unsigned)kWidth || (unsigned)y >= (unsigned)kHeight) return;
    uint8_t& b = data[(y >> 3) * kWidth + x];
    const uint8_t m = (uint8_t)(1u << (y & 7));
    switch (ink) {
      case Ink::Set: b |= m; break;
      case Ink::Clear: b &= (uint8_t)~m; break;
      case Ink::Invert: b ^= m; break;
    }
  }
  inline void set(int x, int y) { plot(x, y, Ink::Set); }

  // Горизонтальный отрезок [x0, x1] включительно, с обрезкой по краям.
  void hspan(int y, int x0, int x1, Ink ink);
  int count_lit() const;
  bool equals(const Framebuffer& other) const;
  // Поворот на 180° (брелок вверх ногами). У SSD1306 то же самое делают
  // команды 0xA0/0xC0, но в симуляторе удобнее программно.
  void rotate180();
};

// Изменённый участок одной страницы: столбцы [x0, x1] включительно.
// Драйвер шлёт его командами 0x21 (столбцы) и 0x22 (страницы).
struct DirtyWindow {
  uint8_t page;
  uint8_t x0;
  uint8_t x1;
};

// Сравнивает кадры и возвращает число окон (не больше kPages).
int diff_frames(const Framebuffer& prev, const Framebuffer& next, DirtyWindow out[kPages]);
int dirty_bytes(const DirtyWindow* windows, int count);

// Порог упорядоченного дизеринга Байера 4×4: пиксель рисуется, если alpha выше порога.
bool dither_pass(int x, int y, float alpha);

}  // namespace face
