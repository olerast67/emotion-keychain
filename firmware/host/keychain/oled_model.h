// Модуль SSD1306 за ключом питания: память, адресация, включение панели и накачки.
#pragma once

#include <stdlib.h>
#include <string.h>

#include "sim.h"

namespace sim {

class OledModel : public I2cDevice {
 public:
  bool powered() const override;
  void on_power(bool p) {
    if (p == was_powered_) return;
    was_powered_ = p;
    if (p) {
      // Питание подали: контроллер после сброса, в памяти — что угодно.
      power_since = now_us;
      on = pump = false;
      contrast = 0x7F;
      mode = 2, col = page = 0, col0 = 0, col1 = 127, page0 = 0, page1 = 7;
      pend_need = pend_n = 0;
      for (auto& row : ram)
        for (auto& b : row) b = (uint8_t)rand();
      ++power_cycles;
    } else {
      on = pump = false;
    }
  }
  void write(const uint8_t* d, size_t n, bool) override {
    if (!n) return;
    const bool data = (d[0] & 0x40) != 0;
    for (size_t i = 1; i < n; ++i) data ? put(d[i]) : command(d[i]);
    if (!data) ++cmd_packets;
  }
  size_t read(uint8_t* d, size_t n) override {
    memset(d, 0, n);
    return n;
  }
  bool visible() const { return powered() && on && pump; }
  int lit() const {
    int k = 0;
    for (auto& row : ram)
      for (int x = 0; x < 128; ++x) k += __builtin_popcount(row[x]);
    return k;
  }

  uint8_t ram[8][128] = {};
  bool on = false, pump = false;
  uint8_t contrast = 0x7F;
  uint64_t power_since = 0;
  uint32_t power_cycles = 0, cmd_packets = 0, stray = 0;

 private:
  void command(uint8_t c) {
    if (pend_need) {
      pend_[pend_n++] = c;
      if (pend_n < pend_need) return;
      switch (pend_[0]) {
        case 0x20: mode = pend_[1] & 3; break;
        case 0x21: col0 = col = pend_[1] & 127, col1 = pend_[2] & 127; break;
        case 0x22: page0 = page = pend_[1] & 7, page1 = pend_[2] & 7; break;
        case 0x81: contrast = pend_[1]; break;
        case 0x8D: pump = (pend_[1] & 0x04) != 0; break;
        default: break;
      }
      pend_need = pend_n = 0;
      return;
    }
    switch (c) {
      case 0x21:
      case 0x22: pend_[0] = c, pend_n = 1, pend_need = 3; return;
      case 0x20:
      case 0x81:
      case 0x8D:
      case 0xA8:
      case 0xD3:
      case 0xD5:
      case 0xD9:
      case 0xDA:
      case 0xDB: pend_[0] = c, pend_n = 1, pend_need = 2; return;
      case 0xAE: on = false; return;
      case 0xAF: on = true; return;
      default: break;
    }
    if (c <= 0x0F)
      col = (col & 0xF0) | c;
    else if (c <= 0x1F)
      col = (col & 0x0F) | (c & 0x0F) << 4;
    else if (c >= 0xB0 && c <= 0xB7)
      page = c & 7;
  }
  void put(uint8_t d) {
    if (col < 128) ram[page][col] = d;
    if (mode == 0) {
      if (++col > col1) {
        col = col0;
        if (++page > page1) page = page0;
      }
    } else if (col < 127) {
      ++col;
    }
  }
  bool was_powered_ = false;
  int mode = 2, col = 0, page = 0, col0 = 0, col1 = 127, page0 = 0, page1 = 7;
  uint8_t pend_[4];
  int pend_n = 0, pend_need = 0;
};

extern OledModel oled_dev;
bool oled_powered();
uint32_t flash_failure_count();

}  // namespace sim
