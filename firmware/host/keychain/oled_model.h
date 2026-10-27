// Модуль SSD1306 за ключом питания: память, адресация, включение панели и накачки.
#pragma once

#include <stdlib.h>
#include <string.h>

#include "sim.h"

namespace sim {

// Питание модуля за ключом (DISP_VCC) после выключения DISP_EN спадает не сразу: конденсаторы
// модуля разряжает только его собственный ток (~20 мкА, τ ≈ 0.85 с: около 1 В через секунду).
// SDA и SCL, прижатые к нулю, разряжают его через подтяжки модуля (τ ≈ 0.1 с на линию), а
// подтяжки nRF или единица на линиях, наоборот, питают модуль через них (~2.6 В). Контроллер
// сбрасывается, только если питание опускалось ниже ~1 В; иначе после включения он помнит
// прежнее, в том числе «зависание» (wedge): не отвечает на адрес, пока его не обесточат.
// Другое зависание (osda): после следующего включения (osda 1) или сразу (osda 2) контроллер
// держит SDA в нуле — такты его не отпускают, только сброс по питанию. Ноль на SDA читается как подтверждение: обмен
// будто бы проходит, а до контроллера ничего не доходит.
class OledModel : public I2cDevice {
 public:
  bool powered() const override;
  bool acks() const override;
  void on_power(bool p) {
    if (p == was_powered_) return;
    track_vcc();
    was_powered_ = p;
    if (p) {
      last_off_min = vcc_min;
      power_since = now_us;
      if (vcc_min < kPorV) {
        // Сброс по питанию: контроллер с нуля, в памяти — что угодно.
        on = pump = false;
        wedged = false;
        sda_hold = false;
        contrast = 0x7F;
        mode = 2, col = page = 0, col0 = 0, col1 = 127, page0 = 0, page1 = 7;
        pend_need = pend_n = 0;
        for (auto& row : ram)
          for (auto& b : row) b = (uint8_t)rand();
        ++resets;
      }
      if (sda_arm) sda_hold = true, sda_arm = false;
      vcc = vcc_min = 3.3f;
      ++power_cycles;
    } else {
      vcc_min = vcc;
    }
  }
  // Досчитать напряжение питания до сейчас по тому, как настроены ножки (models.cpp).
  void track_vcc();
  void wedge() { wedged = true; }
  // now — держит SDA сразу (завис посреди работы), иначе — после следующего включения.
  void arm_sda_hold(bool now) {
    if (now)
      sda_hold = true;
    else
      sda_arm = true;
  }
  bool holds_sda() const;  // держит SDA сейчас (под питанием)
  void write(const uint8_t* d, size_t n, bool) override {
    if (!n || wedged) return;
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

  static constexpr float kPorV = 1.0f;
  uint8_t ram[8][128] = {};
  bool on = false, pump = false, wedged = false;
  bool sda_hold = false, sda_arm = false;
  uint8_t contrast = 0x7F;
  uint64_t power_since = 0;
  uint32_t power_cycles = 0, resets = 0, cmd_packets = 0, stray = 0;
  float vcc = 0, vcc_min = 0;  // питание модуля и его минимум за последнее выключение
  float last_off_min = 0;      // минимум перед последним включением
  uint64_t vcc_t = 0;

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
