// Драйвер SSD1306/SH1106 128×64 по I2C на Arduino Wire.
// Умеет отправлять только изменённые окна страниц — те, что отдаёт FaceEngine::dirty().
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "face/framebuffer.h"

class Ssd1306 {
 public:
  // addr = 0 — найти экран самому (0x3C или 0x3D). false — экран не ответил.
  bool begin(TwoWire& wire, uint8_t addr = 0, bool sh1106 = false);
  uint8_t address() const { return addr_; }
  bool sh1106() const { return sh1106_; }

  void contrast(uint8_t v);
  void power(bool on);
  // Отправить часть страницы: столбцы x0..x1 включительно.
  void send_window(const face::Framebuffer& fb, uint8_t page, uint8_t x0, uint8_t x1);
  void send_full(const face::Framebuffer& fb);

 private:
  void cmd(uint8_t c);
  void cmds(const uint8_t* c, size_t n);

  TwoWire* wire_ = nullptr;
  uint8_t addr_ = 0x3C;
  bool sh1106_ = false;
};
