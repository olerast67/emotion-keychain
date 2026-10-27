// Датчики на шине Wire: акселерометр LIS3DH и датчик света LTR-303ALS.
#pragma once

#include <Arduino.h>
#include <Wire.h>

// LIS3DH: 100 Гц, ±4 g, выборки копятся в FIFO (32 штуки), касание и двойное касание —
// аппаратным детектором на INT1. Процессор забирает FIFO раз в 100–300 мс и спит.
class Lis3dh {
 public:
  bool begin(TwoWire& wire, uint8_t addr);
  bool ok() const { return ok_; }
  // Выборки из FIFO в g (оси датчика). Возвращает их число (до max).
  int read_fifo(float (*xyz)[3], int max);
  // Касания с прошлого вызова: 1 — одиночное, 2 — двойное, 0 — не было.
  int take_click();
  // Сон с минимальным током или работа.
  void power(bool on);
  // Отвечает ли датчик и целы ли его настройки; false — пора перезапустить (begin).
  bool healthy();

 private:
  bool write(uint8_t reg, uint8_t v);
  bool read(uint8_t reg, uint8_t* buf, size_t n);
  TwoWire* w_ = nullptr;
  uint8_t addr_ = 0x19;
  bool ok_ = false;
  bool on_ = true;
};

// LTR-303ALS: освещённость, замер 100 мс раз в 500 мс.
class Ltr303 {
 public:
  bool begin(TwoWire& wire, uint8_t addr);
  bool ok() const { return ok_; }
  // Люксы; −1 — нет свежего замера или датчик не ответил.
  float lux();
  void power(bool on);

 private:
  bool write(uint8_t reg, uint8_t v);
  TwoWire* w_ = nullptr;
  uint8_t addr_ = 0x29;
  bool ok_ = false;
};
