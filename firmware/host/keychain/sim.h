// Симуляция платы rev B: состояние «железа» одного брелока.
//
// Прошивка (firmware/src/keychain) собирается на ПК без изменений, а заголовки ядра
// Adafruit подменены этими заглушками. Время виртуальное: delay() и обмен по шинам
// двигают его, и вместе с ним живут модели микросхем — акселерометр копит FIFO,
// датчик света меряет, микрофон присылает блоки, сторожевой таймер ждёт кормления.
// Перезагрузка, выключение и загрузчик бросают Reset: процесс платы завершается, а
// координатор (keychain_sim.cpp) запускает его заново, как настоящую плату.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace sim {

// Чем кончилась работа прошивки.
// SystemOff — сон с пробуждением от кнопки (systemOff ядра), SystemOffUsb — без кнопки:
// sd_power_system_off или SYSTEMOFF напрямую, будит только VBUS.
enum class ResetKind { Soft = 1, SystemOff = 2, Dfu = 3, Watchdog = 4, Hang = 5, SystemOffUsb = 6 };
struct Reset {
  ResetKind kind;
};

extern uint64_t now_us;  // время с последнего сброса
void advance_us(uint64_t us);
// Зовётся после каждого шага времени: процесс платы отдаёт управление координатору.
extern void (*time_hook)();
inline uint32_t now_ms() { return (uint32_t)(now_us / 1000); }

// Ножки: режим и выход, что задаёт прошивка; что читается — решает мир.
enum PinMode : uint8_t { kDefault, kInput, kInputPullup, kOutput, kPeriph };
extern uint8_t pin_mode[48];
extern uint8_t pin_out[48];
int read_pin(int pin);

// Мир вокруг платы — его задаёт координатор.
struct World {
  float ax = 0, ay = 1, az = 0;  // ускорение в осях брелока, g (висит ровно)
  int motion = 0;                // 0 — как задано, 1 — ходьба, 2 — тряска
  float motion_hz = 2, motion_amp = 0.4f;
  float lux = 300;
  float mic_db = -62;
  int mic_pattern = 0;  // 0 — ровно, 1 — речь (слоги с паузами)
  float temp_c = 24;
  bool vbus = false;
  uint64_t vbus_off_us = 0;  // когда отключили USB (провал VSYS через диод Q1)
  bool button = false;
  float cell_v = 3.9f;  // расчётное напряжение ячейки (координатор ведёт заряд)
  bool charging = false;
  int tap = 0;           // 1 — касание, 2 — двойное: модель LIS3DH взводит CLICK
  uint32_t reset_reason = 0;
  uint64_t device_id = 0;
};
extern World world;

// Учёт тока: мкА·мс по статьям, чтобы координатор вёл заряд и считал автономность.
struct Power {
  double cpu_us = 0;  // время работы ядра (обмен по шинам, кадры, проходы цикла)
  double adv_events = 0;
  double scan_ms = 0;
  double charge_uams = 0;  // накоплено с прошлого отчёта
  void add(double ua, double ms) { charge_uams += ua * ms; }
};
extern Power power;

// Что прошивка делает с «железом» — для отчёта координатору.
struct Status {
  bool hfclk_requested = false;
  bool pdm_running = false;
  uint16_t tone_hz = 0;
  float vib_duty = 0;    // доля периода, когда ключ мотора открыт (по биту 15 полярности PWM)
  float vib_peak_v = 0;  // наибольшее среднее напряжение на моторе с прошлого отчёта
  bool wdt_started = false;
  uint64_t wdt_fed_us = 0;
  uint32_t wdt_timeout_us = 0;
  uint32_t uicr_regout0 = 0xFFFFFFFFu;  // по умолчанию — стёрто (1.8 В)
  uint32_t i2c_errors = 0;
  uint32_t loops = 0;
  uint32_t frames = 0;
};
extern Status status;

// Хуки шины I2C для моделей устройств.
struct I2cDevice {
  virtual ~I2cDevice() = default;
  virtual bool powered() const { return true; }
  virtual void write(const uint8_t* d, size_t n, bool stop) = 0;  // байты после адреса
  virtual size_t read(uint8_t* d, size_t n) = 0;
};
I2cDevice* i2c_device(int bus, uint8_t addr);

}  // namespace sim
