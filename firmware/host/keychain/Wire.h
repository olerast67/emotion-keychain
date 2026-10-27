// Шины I2C платы: Wire — датчики (LIS3DH, LTR-303), Wire1 — экран за ключом питания.
// Передача занимает время по скорости шины; устройство без питания не отвечает.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sim.h"

class TwoWire {
 public:
  explicit TwoWire(int bus) : bus_(bus) {}
  // Как в ядре: begin настраивает ножки входами с подтяжкой (открытый сток), end только
  // выключает TWIM — ножки остаются с подтяжкой. Пока TWIM включён, линиями управляет он.
  void begin();
  void end() { on_ = false; }
  bool enabled() const { return on_; }
  void setClock(uint32_t hz) { hz_ = hz; }
  void beginTransmission(uint8_t a) { addr_ = a, n_ = 0, tx_ = true; }
  size_t write(uint8_t b) {
    if (n_ >= sizeof buf_) return 0;  // буфер ядра — 64 байта
    buf_[n_++] = b;
    return 1;
  }
  size_t write(const uint8_t* d, size_t n) {
    size_t k = 0;
    while (k < n && write(d[k])) ++k;
    return k;
  }
  uint8_t endTransmission(bool stop = true);
  uint8_t requestFrom(uint8_t addr, uint8_t n);
  int available() const { return (int)(rx_n_ - rx_pos_); }
  int read() { return rx_pos_ < rx_n_ ? rx_[rx_pos_++] : -1; }

  uint32_t bytes = 0, transactions = 0;

 private:
  void spend(size_t n);
  bool bus_fault();
  int bus_;
  bool on_ = false, tx_ = false;
  uint32_t hz_ = 100000;
  uint8_t addr_ = 0;
  uint8_t buf_[64];
  size_t n_ = 0;
  uint8_t rx_[64];
  size_t rx_n_ = 0, rx_pos_ = 0;
};
extern TwoWire Wire, Wire1;
