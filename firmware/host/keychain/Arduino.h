// Заглушка Arduino-ядра Adafruit nRF52 для симуляции платы rev B (см. sim.h).
#pragma once

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "nrf.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "sim.h"

// Выводы платы — как в variants/keychain_revb/variant.h.
#define PINS_COUNT (48)
#define PIN_LED1 (31)
#define LED_BUILTIN PIN_LED1
#define PIN_BUTTON1 (22)
#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE_SDA (27)
#define PIN_WIRE_SCL (1)
#define PIN_WIRE1_SDA (15)
#define PIN_WIRE1_SCL (13)
#define PIN_PDM_DIN (8)
#define PIN_PDM_CLK (6)
#define PIN_PDM_PWR (41)

constexpr int LOW = 0, HIGH = 1;
constexpr int INPUT = 0, OUTPUT = 1, INPUT_PULLUP = 2, INPUT_PULLDOWN = 3, OUTPUT_S0D1 = 0xD;
enum eAnalogReference { AR_DEFAULT, AR_INTERNAL };

// Как в ядре: millis() — тики FreeRTOS (1024 Гц) × 1000 / 1024, тики 32-битные. Через
// 48.5 суток тики переполняются, и millis() прыгает на ноль не на границе 2^32.
inline uint32_t xTaskGetTickCount() { return (uint32_t)(sim::now_us * 1024 / 1000000); }
inline uint32_t millis() { return (uint32_t)((uint64_t)xTaskGetTickCount() * 1000 / 1024); }
inline uint32_t micros() { return (uint32_t)sim::now_us; }
inline void delay(uint32_t ms) { sim::advance_us((uint64_t)ms * 1000); }
inline void delayMicroseconds(uint32_t us) { sim::advance_us(us); }
inline void yield() { sim::advance_us(50); }

void pinMode(uint32_t pin, uint32_t mode);
void digitalWrite(uint32_t pin, uint32_t v);
int digitalRead(uint32_t pin);

void analogReference(eAnalogReference);
void analogReadResolution(int bits);
void analogSampleTime(uint8_t us);
uint32_t analogReadVDDHDIV5();
inline void analogCalibrateOffset() {}
float readCPUTemperature();
uint32_t readResetReason();
[[noreturn]] void systemOff(uint32_t pin, uint8_t wake_logic);
[[noreturn]] void enterUf2Dfu();
[[noreturn]] void NVIC_SystemReset();

// FreeRTOS: тики 1024 Гц, как в ядре.
#define configTICK_RATE_HZ 1024

// USB CDC: вход подаёт координатор, выход копится и уходит в отчёт.
class HostSerial {
 public:
  void begin(unsigned long) {}
  explicit operator bool() const { return connected; }
  int available() const { return (int)(in.size() - in_pos); }
  int read() { return in_pos < in.size() ? (unsigned char)in[in_pos++] : -1; }
  int availableForWrite() const { return connected ? (int)(fifo_cap - fifo_used) : 0; }
  size_t write(const uint8_t* d, size_t n) {
    if (!connected) return 0;
    const size_t k = n < fifo_cap - fifo_used ? n : fifo_cap - fifo_used;
    out.append((const char*)d, k);
    if (!host_reads) fifo_used += k;  // хост не читает — FIFO заполняется
    return k;
  }
  size_t write(uint8_t c) { return write(&c, 1); }
  void flush() {}

  std::string in, out;
  size_t in_pos = 0;
  bool connected = true;
  bool host_reads = true;
  size_t fifo_cap = 256, fifo_used = 0;
};
extern HostSerial Serial;

inline void nrf_gpio_cfg_default(uint32_t pin) {
  sim::pin_changing((int)pin);
  sim::pin_mode[pin] = sim::kDefault;
  sim::pin_changed((int)pin);
}
