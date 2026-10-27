// Модели «железа» платы rev B для симуляции (sim.h): время, ножки, шины и микросхемы.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "Arduino.h"
#include "InternalFileSystem.h"
#include "PDM.h"
#include "Wire.h"
#include "bluefruit.h"
#include "nrf.h"
#include "nrf_sdm.h"
#include "nrf_soc.h"
#include "oled_model.h"

namespace sim {

uint64_t now_us = 0;
void (*time_hook)() = nullptr;
uint8_t pin_mode[48] = {};
uint8_t pin_out[48] = {};
World world;
Power power;
Status status;

Uicr uicr;
Nvmc nvmc;
PowerRegs power_regs;
Wdt wdt;
Pwm pwm3, pwm2;
Ficr ficr;
Twim twim0, twim1;

namespace {
uint32_t rng_state = 12345;
uint32_t rnd() {
  rng_state ^= rng_state << 13, rng_state ^= rng_state >> 17, rng_state ^= rng_state << 5;
  return rng_state;
}
float noise(float a) { return ((float)(rnd() % 2001) / 1000.0f - 1.0f) * a; }
}  // namespace

// ---------------------------------------------------------------- LIS3DH

class Lis3dhModel : public I2cDevice {
 public:
  void write(const uint8_t* d, size_t n, bool) override {
    sample_until(now_us);
    if (!n) return;
    reg_ = d[0] & 0x7F;
    inc_ = (d[0] & 0x80) != 0;
    for (size_t i = 1; i < n; ++i) {
      regs_[reg_] = d[i];
      if (reg_ == 0x20) odr_since_ = now_us;
      if (inc_) ++reg_;
    }
  }
  size_t read(uint8_t* d, size_t n) override {
    sample_until(now_us);
    for (size_t i = 0; i < n; ++i) {
      d[i] = get(reg_);
      if (inc_ || n > 1) advance();
    }
    return n;
  }
  bool int1() {
    sample_until(now_us);
    return (regs_[0x22] & 0x80) && (click_ & 0x40);
  }
  bool active() const { return (regs_[0x20] >> 4) != 0; }
  void sync() { sample_until(now_us); }  // мир сейчас изменится: выборки до этого — по старому
  uint32_t overruns = 0;

 private:
  uint8_t get(uint8_t r) {
    if (r == 0x0F) return 0x33;
    if (r == 0x2F) {
      const int c = (int)count_;
      return (uint8_t)((c == 32 ? 0x40 : 0) | (c == 0 ? 0x20 : 0) | (c & 0x1F));
    }
    if (r == 0x39) {
      const uint8_t c = click_;
      click_ = 0;  // флаг держится до чтения (LIR_Click)
      return c;
    }
    if (r >= 0x28 && r <= 0x2D) {
      const int16_t* s = count_ ? fifo_[head_] : last_;
      const int k = (r - 0x28) / 2;
      const uint16_t v = (uint16_t)s[k];
      return (r & 1) ? (uint8_t)(v >> 8) : (uint8_t)(v & 0xFF);
    }
    return regs_[r];
  }
  void advance() {
    if (reg_ == 0x2D && fifo_on()) {
      // AN3308: в режиме FIFO после OUT_Z_H адрес возвращается на OUT_X_L, а выборка — следующая.
      if (count_) {
        if (getenv("SIM_TRACE_ACC"))
          fprintf(stderr, "%llu acc %d %d %d\n", (unsigned long long)(now_us / 1000), fifo_[head_][0] / 16,
                  fifo_[head_][1] / 16, fifo_[head_][2] / 16);
        memcpy(last_, fifo_[head_], sizeof last_);
        head_ = (head_ + 1) % 32, --count_;
      }
      reg_ = 0x28;
      return;
    }
    ++reg_;
  }
  bool fifo_on() const { return (regs_[0x24] & 0x40) && (regs_[0x2E] >> 6) == 2; }
  void sample_until(uint64_t t) {
    if (world.tap) {
      click_ = (uint8_t)(0x40 | (world.tap == 2 ? 0x20 : 0x10) | 0x04);
      world.tap = 0;
    }
    const int odr = regs_[0x20] >> 4;
    if (odr != 5) {
      next_ = t;
      return;
    }
    if (next_ < odr_since_) next_ = odr_since_ + 10000;
    while (next_ <= t) {
      int16_t s[3];
      sample(next_, s);
      if (fifo_on()) {
        if (count_ == 32) {
          head_ = (head_ + 1) % 32, --count_;  // поток: старая выборка теряется
          ++overruns;
        }
        memcpy(fifo_[(head_ + count_) % 32], s, sizeof s);
        ++count_;
      } else {
        memcpy(last_, s, sizeof s);
      }
      next_ += 10000;  // 100 Гц
    }
  }
  void sample(uint64_t t, int16_t out[3]) {
    float ax = world.ax, ay = world.ay, az = world.az;
    const double ts = (double)t / 1e6;
    if (world.motion == 1) {  // ходьба: толчок вверх в начале шага
      const double ph = fmod(ts * world.motion_hz, 1.0);
      if (ph < 0.2) ay += world.motion_amp;
    } else if (world.motion == 2) {  // тряска вдоль x
      ax += sin(2 * M_PI * world.motion_hz * ts) > 0 ? world.motion_amp : -world.motion_amp;
    }
    // Датчик на обороте платы: x и z перевёрнуты (board::kAccMap).
    const float g[3] = {-ax + noise(0.01f), ay + noise(0.01f), -az + noise(0.01f)};
    for (int k = 0; k < 3; ++k) {
      long v = lroundf(g[k] / 0.002f);
      if (v > 2047) v = 2047;
      if (v < -2048) v = -2048;
      out[k] = (int16_t)(v * 16);  // 12 бит, выровнено влево
    }
  }

  uint8_t regs_[0x40] = {};
  uint8_t reg_ = 0;
  bool inc_ = false;
  int16_t fifo_[32][3] = {}, last_[3] = {};
  uint32_t head_ = 0, count_ = 0;
  uint64_t next_ = 0, odr_since_ = 0;
  uint8_t click_ = 0;
};

// ---------------------------------------------------------------- LTR-303ALS

class Ltr303Model : public I2cDevice {
 public:
  bool powered() const override { return now_us >= 100000; }  // отвечает через 100 мс после питания
  void write(const uint8_t* d, size_t n, bool) override {
    update();
    if (!n) return;
    reg_ = d[0];
    for (size_t i = 1; i < n; ++i, ++reg_) {
      if (reg_ == 0x80) {
        const bool was = active();
        regs_[0x80] = d[i];
        if (!was && active()) next_ = now_us + 10000 + 100000;  // пробуждение и накопление
      } else {
        regs_[reg_] = d[i];
      }
    }
  }
  size_t read(uint8_t* d, size_t n) override {
    update();
    for (size_t i = 0; i < n; ++i, ++reg_) {
      if (reg_ == 0x86) d[i] = 0xA0;
      else if (reg_ == 0x8C) d[i] = (uint8_t)(fresh_ ? 0x04 : 0x00);
      else if (reg_ >= 0x88 && reg_ <= 0x8B) {
        d[i] = data_[reg_ - 0x88];
        if (reg_ == 0x8B) fresh_ = false;
      } else d[i] = regs_[reg_];
    }
    return n;
  }
  bool active() const { return regs_[0x80] & 1; }
  uint32_t measurements = 0;

 private:
  void update() {
    if (!active()) return;
    while (next_ <= now_us) {
      // Обычный свет: CH1/(CH0+CH1) = 0.3 — ветка 1.7743·CH0 + 1.1059·CH1.
      const double ch0 = world.lux / (1.7743 + 1.1059 * 0.4286);
      const double ch1 = ch0 * 0.4286;
      const uint16_t c0 = (uint16_t)fmin(65535, ch0), c1 = (uint16_t)fmin(65535, ch1);
      data_[0] = c1 & 0xFF, data_[1] = c1 >> 8, data_[2] = c0 & 0xFF, data_[3] = c0 >> 8;
      fresh_ = true;
      ++measurements;
      next_ += 500000;  // MEAS_RATE 0x03: раз в 500 мс
    }
  }
  uint8_t regs_[256] = {};
  uint8_t reg_ = 0;
  uint8_t data_[4] = {};
  bool fresh_ = false;
  uint64_t next_ = 0;
};

Lis3dhModel lis;
Ltr303Model ltr;
OledModel oled_dev;

bool lis_active() { return lis.active(); }
void world_changing() { lis.sync(); }
bool ltr_active() { return ltr.active(); }
uint32_t lis_overruns() { return lis.overruns; }
uint32_t ltr_measurements() { return ltr.measurements; }

I2cDevice* i2c_device(int bus, uint8_t addr) {
  if (bus == 0 && addr == 0x19) return &lis;  // SA0 на VDD
  if (bus == 0 && addr == 0x29) return &ltr;
  if (bus == 1 && addr == 0x3C) return &oled_dev;
  return nullptr;
}

bool oled_powered() { return pin_mode[19] == kOutput && pin_out[19]; }
bool OledModel::powered() const { return oled_powered() && now_us >= power_since + 30000; }

// ---------------------------------------------------------------- флеш SoftDevice

namespace {
struct FlashOp {
  int kind = 0;  // 0 — нет, 1 — запись, 2 — стирание
  uint32_t* dst = nullptr;
  const uint32_t* src = nullptr;
  uint32_t words = 0, page = 0;
  uint64_t done_us = 0;
} flash_op;
uint32_t flash_failures = 0;

void finish_flash() {
  FlashOp op = flash_op;
  if (getenv("SIM_TRACE_FLASH"))
    fprintf(stderr, "%llu flash op %d page %x dst %p words %u\n", (unsigned long long)(now_us / 1000), op.kind, op.page,
            (void*)op.dst, op.words);
  flash_op = FlashOp{};
  // Окно для флеша SoftDevice даёт между радиособытиями; при сплошном прослушивании
  // операция может не успеть — событие ошибки, память не тронута.
  if (Bluefruit.Scanner.isRunning() && rnd() % 4 == 0) {
    ++flash_failures;
    return;
  }
  if (op.kind == 1) {
    for (uint32_t i = 0; i < op.words; ++i) op.dst[i] &= op.src[i];
  } else {
    memset((void*)(uintptr_t)(op.page * 4096u), 0xFF, 4096);
  }
}
}  // namespace

uint32_t flash_failure_count() { return flash_failures; }

// ---------------------------------------------------------------- время

void advance_us(uint64_t us) {
  const uint64_t target = now_us + us;
  while (now_us < target) {
    uint64_t next = target;
    if (flash_op.kind && flash_op.done_us < next) next = flash_op.done_us;
    const uint64_t pdm_at = PDM.next_tick();
    if (pdm_at && pdm_at < next) next = pdm_at;
    if (status.wdt_started) {
      const uint64_t dead = status.wdt_fed_us + status.wdt_timeout_us;
      if (dead < next) next = dead;
    }
    if (next < now_us) next = now_us;
    now_us = next;
    if (flash_op.kind && now_us >= flash_op.done_us) finish_flash();
    PDM.tick(now_us);
    if (status.wdt_started && now_us >= status.wdt_fed_us + status.wdt_timeout_us) throw Reset{ResetKind::Watchdog};
  }
  if (time_hook) time_hook();
}

// ---------------------------------------------------------------- ножки

int read_pin(int pin) {
  const uint8_t m = pin_mode[pin];
  if (m == kOutput) return pin_out[pin];
  if (m == kDefault) return (int)(rnd() & 1);  // вход отключён: читать нечего
  if (pin == 22) return world.button ? LOW : (m == kInputPullup ? HIGH : (int)(rnd() & 1));
  if (pin == 4) return lis.int1() ? HIGH : LOW;
  if (pin == 20) {
    // MCP73832: STAT в нуле — заряд; иначе высокий уровень даёт светодиод от VBUS, без
    // USB ножка висит в воздухе.
    if (!world.vbus) return (int)(rnd() & 1);
    return world.charging ? LOW : HIGH;
  }
  return m == kInputPullup ? HIGH : LOW;
}

UicrReg& UicrReg::operator=(uint32_t x) {
  if (nvmc.CONFIG.v != NVMC_CONFIG_WEN_Wen) {
    ++status.i2c_errors;  // запись UICR без разрешения NVMC не проходит
    return *this;
  }
  status.uicr_regout0 &= x;  // флеш: биты только сбрасываются
  return *this;
}

}  // namespace sim

using namespace sim;

// ---------------------------------------------------------------- Arduino

HostSerial Serial;
TwoWire Wire(0), Wire1(1);
PDMClass PDM;
AdafruitBluefruit Bluefruit;
Adafruit_LittleFS_Namespace::InternalFileSystemHost InternalFS;

void pinMode(uint32_t pin, uint32_t mode) {
  pin_mode[pin] = mode == OUTPUT || mode == OUTPUT_S0D1 ? kOutput : mode == INPUT_PULLUP ? kInputPullup : kInput;
  if (pin == 19) oled_dev.on_power(oled_powered());
}
void digitalWrite(uint32_t pin, uint32_t v) {
  pin_out[pin] = v ? 1 : 0;
  if (pin == 19) oled_dev.on_power(oled_powered());
}
int digitalRead(uint32_t pin) { return read_pin((int)pin); }

namespace {
int adc_bits = 10;
uint8_t adc_tacq = 3;
bool adc_internal = false;
}  // namespace
void analogReference(eAnalogReference r) { adc_internal = r == AR_INTERNAL; }
void analogReadResolution(int bits) { adc_bits = bits; }
void analogSampleTime(uint8_t us) { adc_tacq = us; }
uint32_t analogReadVDDHDIV5() {
  // VDDH: от USB — через диод около 4.7 В, иначе ячейка. Первые ~100 мс после отключения USB
  // система питается через диод в P-MOSFET (пока VBUS не спадёт): на 0.55 В ниже ячейки.
  float vddh = world.vbus ? 4.7f : world.cell_v;
  if (!world.vbus && world.vbus_off_us && now_us - world.vbus_off_us < 100000) vddh -= 0.55f;
  float v = vddh / 5.0f;
  if (adc_tacq < 40) v *= 0.9f;  // делитель VDDH/5 не успевает зарядить выборку
  const float full = adc_internal ? 3.6f : 3.6f;
  long raw = lroundf(v / full * (float)(1 << adc_bits) + noise(1.5f));
  if (raw < 0) raw = 0;
  if (raw > (1 << adc_bits) - 1) raw = (1 << adc_bits) - 1;
  return (uint32_t)raw;
}
float readCPUTemperature() { return world.temp_c + 3.0f + noise(0.25f); }
uint32_t readResetReason() { return world.reset_reason; }
void systemOff(uint32_t, uint8_t) { throw Reset{ResetKind::SystemOff}; }
void enterUf2Dfu() { throw Reset{ResetKind::Dfu}; }
void NVIC_SystemReset() { throw Reset{ResetKind::Soft}; }

// ---------------------------------------------------------------- регистры с действием

namespace {
void wdt_start(uint32_t v) {
  if (!v) return;
  status.wdt_started = true;
  status.wdt_fed_us = now_us;
  status.wdt_timeout_us = (uint32_t)((uint64_t)wdt.CRV.v * 1000000u / 32768u);
}
void wdt_feed(uint32_t v) {
  if (v == WDT_RR_RR_Reload) status.wdt_fed_us = now_us;
}
void pwm_update(uint32_t) {
  if (!pwm3.ENABLE.v) {
    status.tone_hz = 0;
    return;
  }
  const uint32_t top = pwm3.COUNTERTOP.v;
  status.tone_hz = top >= 3 ? (uint16_t)(4000000u / top) : 0;
}
void pwm_stop(uint32_t v) {
  if (v) status.tone_hz = 0;
}
// Мотор: значение последовательности PWM2. Указатель прошивка пишет 32-битным, как на nRF;
// старшие биты на ПК — те же, что у любой статической переменной этой программы.
void vib_update(uint32_t) {
  status.vib_duty = 0;
  if (!pwm2.ENABLE.v || !pwm2.COUNTERTOP.v || !pwm2.SEQ[0].PTR.v) return;
  const auto* seq = (const uint16_t*)(((uintptr_t)&pwm2 & ~(uintptr_t)0xFFFFFFFFu) | pwm2.SEQ[0].PTR.v);
  const uint16_t v = seq[0];
  const uint32_t top = pwm2.COUNTERTOP.v, cmp = (v & 0x7FFFu) < top ? (v & 0x7FFFu) : top;
  // Бит 15 = 1: высокий с начала периода до сравнения; 0 — низкий до сравнения, потом высокий.
  const float f = (float)cmp / (float)top;
  status.vib_duty = (v & 0x8000u) ? f : 1.0f - f;
  const float vsys = world.vbus ? 4.7f : world.cell_v;
  if (status.vib_duty * vsys > status.vib_peak_v) status.vib_peak_v = status.vib_duty * vsys;
}
void vib_stop(uint32_t v) {
  if (v) status.vib_duty = 0;
}
void power_off_reg(uint32_t v) {
  if (v) throw Reset{ResetKind::SystemOffUsb};
}
struct RegHooks {
  RegHooks() {
    power_regs.SYSTEMOFF.on_write = power_off_reg;
    wdt.TASKS_START.on_write = wdt_start;
    wdt.RR[0].on_write = wdt_feed;
    pwm3.TASKS_SEQSTART[0].on_write = pwm_update;
    pwm3.ENABLE.on_write = pwm_update;
    pwm3.TASKS_STOP.on_write = pwm_stop;
    pwm2.TASKS_SEQSTART[0].on_write = vib_update;
    pwm2.ENABLE.on_write = vib_update;
    pwm2.TASKS_STOP.on_write = vib_stop;
  }
} reg_hooks;
}  // namespace

// ---------------------------------------------------------------- I2C

void TwoWire::spend(size_t n) {
  const double us = (double)(n + 1) * 9.0 * 1e6 / (double)hz_ + 15.0;
  power.cpu_us += us;  // ядро ждёт конца передачи в Wire
  bytes += (uint32_t)n;
  ++transactions;
  advance_us((uint64_t)us);
}

uint8_t TwoWire::endTransmission(bool stop) {
  if (!on_) {
    ++status.i2c_errors;  // шина не запущена: в ядре это зависание или ошибка
    return 4;
  }
  spend(n_);
  I2cDevice* d = i2c_device(bus_, addr_);
  if (!d || !d->powered()) return 2;  // адрес без ответа
  d->write(buf_, n_, stop);
  return 0;
}

uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t n) {
  rx_n_ = rx_pos_ = 0;
  if (!on_) {
    ++status.i2c_errors;
    return 0;
  }
  if (n > sizeof rx_) n = sizeof rx_;
  spend(n);
  I2cDevice* d = i2c_device(bus_, addr);
  if (!d || !d->powered()) return 0;
  rx_n_ = d->read(rx_, n);
  return (uint8_t)rx_n_;
}

// ---------------------------------------------------------------- PDM

int PDMClass::begin(int, long rate) {
  if (rate != 16000) return 0;
  sd_clock_hfclk_request();  // как в ядре: кварц для PDM
  running_ = true;
  status.pdm_running = true;
  next_us_ = now_us + 32000;
  return 1;
}
void PDMClass::end() {
  running_ = false;
  have_ = false;
  status.pdm_running = false;  // кварц ядро не отпускает
}
uint64_t PDMClass::next_tick() const { return running_ ? next_us_ : 0; }
void PDMClass::tick(uint64_t t) {
  if (!running_ || t < next_us_) return;
  next_us_ += 32000;
  float db = world.mic_db;
  if (world.mic_pattern == 1 && (t / 1000) % 450 >= 200) db = -62;  // речь: слоги и паузы
  const float amp = 32768.0f * powf(10.0f, db / 20.0f) * 1.41421f;
  for (int i = 0; i < 512; ++i) {
    const float s = amp * sinf((float)(phase_++) * 0.39f) + noise(30.0f) + 40.0f;
    block_[i] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, s));
  }
  have_ = true;
  if (cb_) cb_();  // обработчик «прерывания»
}
int PDMClass::read(void* dst, int bytes) {
  if (!have_) return 0;
  const int n = bytes < (int)sizeof block_ ? bytes : (int)sizeof block_;
  memcpy(dst, block_, (size_t)n);
  have_ = false;
  return n;
}

// ---------------------------------------------------------------- Bluefruit и SoftDevice

bool AdafruitBluefruit::begin(int, int) { return started = true; }

bool BLEScanner::start(uint16_t timeout) {
  running_ = true, paused_ = false;
  until_us_ = timeout ? now_us + (uint64_t)timeout * 10000u : ~0ull;
  return true;
}
bool BLEScanner::isRunning() {
  if (running_ && now_us >= until_us_) running_ = false;
  return running_;
}
bool BLEScanner::deliver(const uint8_t* d, uint16_t n, int8_t rssi) {
  if (!isRunning() || paused_ || !cb_) return false;
  static uint8_t buf[31];
  if (n > sizeof buf) n = sizeof buf;
  memcpy(buf, d, n);
  ble_gap_evt_adv_report_t r{{buf, n}, rssi};
  paused_ = true;  // до resume() из обработчика
  cb_(&r);
  return true;
}

uint32_t sd_softdevice_is_enabled(uint8_t* en) {
  *en = Bluefruit.started ? 1 : 0;
  return NRF_SUCCESS;
}
uint32_t sd_power_usbregstatus_get(uint32_t* st) {
  *st = world.vbus ? POWER_USBREGSTATUS_VBUSDETECT_Msk : 0;
  return NRF_SUCCESS;
}
uint32_t sd_clock_hfclk_request() {
  status.hfclk_requested = true;
  return NRF_SUCCESS;
}
uint32_t sd_power_system_off() { throw Reset{ResetKind::SystemOffUsb}; }
uint32_t sd_clock_hfclk_release() {
  status.hfclk_requested = false;
  return NRF_SUCCESS;
}
uint32_t sd_flash_write(uint32_t* dst, const uint32_t* src, uint32_t words) {
  if (flash_op.kind) return NRF_ERROR_BUSY;
  const uintptr_t a = (uintptr_t)dst;
  if (a < 0xEA000 || a + words * 4 > 0xED000 || (a & 3)) abort();  // мимо журнала — ошибка прошивки
  flash_op = FlashOp{1, dst, src, words, 0, now_us + 50 + words * 41};
  return NRF_SUCCESS;
}
uint32_t sd_flash_page_erase(uint32_t page) {
  if (flash_op.kind) return NRF_ERROR_BUSY;
  if (page < 0xEA || page > 0xEC) abort();
  flash_op = FlashOp{2, nullptr, nullptr, 0, page, now_us + 85000};
  return NRF_SUCCESS;
}
