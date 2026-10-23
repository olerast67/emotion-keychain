// Заглушка Arduino для проверки прошивки на ПК (см. stand_test.cpp).
// Время виртуальное: delay() и передача по I2C двигают часы.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

namespace host {
extern uint64_t now_us;
extern int pins[64];
}  // namespace host

inline uint32_t millis() { return (uint32_t)(host::now_us / 1000); }
inline uint32_t micros() { return (uint32_t)host::now_us; }
inline void delay(uint32_t ms) { host::now_us += (uint64_t)ms * 1000; }
inline uint32_t esp_random() { return 0x12345678u; }

struct EspClass {
  uint64_t efuse_mac = 0xA1B2C3D4E5F6ull;
  uint64_t getEfuseMac() const { return efuse_mac; }
};
extern EspClass ESP;

constexpr int LOW = 0, HIGH = 1;
constexpr int INPUT = 0, INPUT_PULLUP = 1, INPUT_PULLDOWN = 2;
inline void pinMode(int pin, int mode) { host::pins[pin] = mode == INPUT_PULLUP ? HIGH : LOW; }
inline int digitalRead(int pin) { return host::pins[pin]; }

// Serial: вход подаёт тест, выход копится в строку.
class HostSerial {
 public:
  void begin(unsigned long) {}
  explicit operator bool() const { return true; }
  int available() const { return (int)(in.size() - in_pos); }
  int read() { return in_pos < in.size() ? (unsigned char)in[in_pos++] : -1; }
  size_t print(const char* s) { return out += s, 0; }
  size_t println(const char* s = "") { return out += s, out += '\n', 0; }
  size_t printf(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    out += buf;
    return 0;
  }

  std::string in, out;
  size_t in_pos = 0;
};
extern HostSerial Serial;
