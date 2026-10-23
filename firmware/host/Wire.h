// Заглушка Wire: вместо шины — эмулятор памяти контроллера SSD1306/SH1106.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define I2C_BUFFER_LENGTH 128

// Что контроллер «видит»: его GDDRAM и состояние адресации.
struct OledEmu {
  bool present = true;
  uint8_t addr = 0x3C;
  bool sh1106 = false;  // 132 столбца, только постраничная адресация
  uint8_t ram[8][132] = {};
  bool on = false;
  uint8_t contrast = 0;
  int col = 0, page = 0, col0 = 0, col1 = 127, page0 = 0, page1 = 7;
  int mode = 2;  // 0 — горизонтальная, 2 — постраничная (после сброса)
  uint8_t pend[4];
  int pend_n = 0, pend_need = 0;
  uint32_t transactions = 0, bytes = 0, overflows = 0, stray = 0;

  void command(uint8_t c);
  void data(uint8_t d);
};

class TwoWire {
 public:
  bool begin(int sda, int scl) { return sda >= 0 && scl >= 0; }
  bool setClock(uint32_t hz) { return hz_ = hz, true; }
  void beginTransmission(uint8_t a) { addr_ = a, n_ = 0; }
  size_t write(uint8_t b) {
    if (n_ >= I2C_BUFFER_LENGTH) return ++emu.overflows, 0;
    buf_[n_++] = b;
    return 1;
  }
  size_t write(const uint8_t* d, size_t n) {
    for (size_t i = 0; i < n; ++i) write(d[i]);
    return n;
  }
  uint8_t endTransmission(bool stop = true);

  OledEmu emu;

 private:
  uint32_t hz_ = 100000;
  uint8_t addr_ = 0;
  uint8_t buf_[I2C_BUFFER_LENGTH];
  size_t n_ = 0;
};
extern TwoWire Wire;
