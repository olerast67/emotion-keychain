// Минимальный кодировщик GIF89a (палитра до 256 цветов, LZW, анимация с циклом).
// Каждый кадр кодируется только по изменившемуся прямоугольнику, поэтому
// анимации лица весят единицы килобайт.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace sim {

struct Rgb {
  uint8_t r, g, b;
};

class GifWriter {
 public:
  // palette — до 256 цветов. loop = true — бесконечный повтор.
  bool open(const std::string& path, int w, int h, const std::vector<Rgb>& palette, bool loop = true);
  // pixels — индексы палитры, w*h байт. delay_cs — задержка в сотых долях секунды.
  void frame(const std::vector<uint8_t>& pixels, int delay_cs);
  bool close();

 private:
  void flush_pending();
  void write_image(const std::vector<uint8_t>& px, int x0, int y0, int x1, int y1, int delay_cs);
  void put(uint8_t b) { fputc(b, f_); }
  void put16(int v) {
    put((uint8_t)(v & 0xFF));
    put((uint8_t)((v >> 8) & 0xFF));
  }

  FILE* f_ = nullptr;
  int w_ = 0, h_ = 0, bits_ = 1;
  std::vector<uint8_t> prev_, pending_;
  int pending_delay_ = 0;
  bool has_prev_ = false, has_pending_ = false;
  int px0_ = 0, py0_ = 0, px1_ = 0, py1_ = 0;
};

}  // namespace sim
