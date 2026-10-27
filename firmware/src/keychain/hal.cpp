#include "hal.h"

#include <PDM.h>
#include <Wire.h>
#include <math.h>
#include <nrf.h>
#include <nrf_soc.h>

#include "board.h"

namespace hal {

namespace {
void vib_on(bool on);
}  // namespace

// ---------------------------------------------------------------- питание

namespace {
uint32_t reset_cause = 0;
}

void power_init() {
  // Ядро читает и стирает причину сброса ещё до setup: берём сохранённую им копию.
  reset_cause = readResetReason();
  // REG0 модуля по умолчанию даёт 1.8 В: при нём экран и датчики не работают. Значение
  // хранится в UICR и пишется один раз; новое значение действует после перезагрузки.
  // Пишется только поверх заводского значения: бит во флеше без стирания можно лишь сбросить,
  // и записать 3.3 В поверх другого напряжения нельзя — иначе плата перезагружалась бы без конца.
  if ((NRF_UICR->REGOUT0 & UICR_REGOUT0_VOUT_Msk) == (UICR_REGOUT0_VOUT_DEFAULT << UICR_REGOUT0_VOUT_Pos)) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (!NRF_NVMC->READY) {
    }
    NRF_UICR->REGOUT0 = (NRF_UICR->REGOUT0 & ~UICR_REGOUT0_VOUT_Msk) | (UICR_REGOUT0_VOUT_3V3 << UICR_REGOUT0_VOUT_Pos);
    while (!NRF_NVMC->READY) {
    }
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    while (!NRF_NVMC->READY) {
    }
    NVIC_SystemReset();
  }
  // DC/DC для ядра (REG1): катушка стоит внутри модуля. REG0 работает линейно — вывод
  // DCCH на плате свободен, его DC/DC не включаем.
  NRF_POWER->DCDCEN = 1;
  // Без подтяжки: статус приходит через 10 кОм, и встроенная подтяжка (~13 кОм) не дала бы
  // опуститься ниже порога. Высокий уровень даёт светодиод заряда от VBUS, низкий — STAT.
  // Вход подключается только на время чтения (charging).
  nrf_gpio_cfg_default(board::kChgIo);
  // У делителя VDDH/5 большое сопротивление: замеру нужно время выборки побольше.
  analogSampleTime(40);
}

bool warm_reset() {
  const uint32_t warm = POWER_RESETREAS_DOG_Msk | POWER_RESETREAS_SREQ_Msk | POWER_RESETREAS_LOCKUP_Msk |
                        POWER_RESETREAS_RESETPIN_Msk;
  const uint32_t woke = POWER_RESETREAS_OFF_Msk | POWER_RESETREAS_LPCOMP_Msk | POWER_RESETREAS_DIF_Msk |
                        POWER_RESETREAS_VBUS_Msk;
  return (reset_cause & warm) && !(reset_cause & woke);
}

float vsys_volts() {
  // VDDH / 5 при опоре 0.6 В и усилении 1/6: полная шкала 3.6 В.
  analogReference(AR_INTERNAL);
  analogReadResolution(12);
  const uint32_t raw = analogReadVDDHDIV5();
  return (float)raw * 3.6f / 4096.0f * 5.0f;
}

bool usb_present() {
  uint32_t st = 0;
  uint8_t sd = 0;
  sd_softdevice_is_enabled(&sd);
  if (sd)
    sd_power_usbregstatus_get(&st);
  else
    st = NRF_POWER->USBREGSTATUS;
  return (st & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

bool charging() {
  // Без USB зарядник молчит, а R20 держит ножку в нуле. Вход подключаем только на время
  // чтения: остальное время ножка отключена и не тратит ток.
  if (!usb_present()) return false;
  pinMode(board::kChgIo, INPUT);
  delayMicroseconds(20);
  const bool low = digitalRead(board::kChgIo) == LOW;
  nrf_gpio_cfg_default(board::kChgIo);
  return low;
}

float temperature_c() { return readCPUTemperature(); }

void watchdog_start() {
  NRF_WDT->CONFIG = WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos;  // считает и во сне; при отладке — пауза
  NRF_WDT->CRV = 8u * 32768u;
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}

void watchdog_feed() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

void i2c_recover(uint8_t sda, uint8_t scl) {
  // Датчик, сброшенный посреди чтения, может держать SDA в нуле: шина зависла бы навсегда.
  // До девяти тактов SCL выпускают его, потом — стоп.
  pinMode(sda, INPUT_PULLUP);
  pinMode(scl, INPUT_PULLUP);
  delayMicroseconds(10);
  if (digitalRead(sda) == HIGH) return;
  pinMode(scl, OUTPUT);
  for (int i = 0; i < 9 && digitalRead(sda) == LOW; ++i) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
  }
  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);  // стоп: SDA вверх при высоком SCL
  delayMicroseconds(5);
  pinMode(sda, INPUT);
  pinMode(scl, INPUT);
}

void i2c_390k(int bus) {
  // Errata 219 nRF52840: на 400 кГц TWIM нарушает тайминги I2C; обход — 390 кГц.
  (bus ? NRF_TWIM1 : NRF_TWIM0)->FREQUENCY = 0x06200000UL;
}

void system_off() {
  // Кнопку ещё держат: ждём, пока отпустят, иначе она сразу разбудит плату. Не дольше 10 с:
  // залипшая кнопка не должна держать брелок включённым (на пустой ячейке — тем более).
  bool stuck = false;
  for (int i = 0; digitalRead(board::kButton) == LOW; ++i) {
    if (i >= 1000) {
      stuck = true;
      break;
    }
    watchdog_feed();
    delay(10);
  }
  buzzer(0, false);
  vib_on(false);
  digitalWrite(board::kVibEn, LOW);
  Wire1.end();
  nrf_gpio_cfg_default(PIN_WIRE1_SDA);  // экран обесточен: ножки отключены, не висят входами
  nrf_gpio_cfg_default(PIN_WIRE1_SCL);
  digitalWrite(board::kDispEn, LOW);
  mic_end();
  delay(20);
  // Кнопка будит из полного сна; подключение USB — тоже (событие VBUS у nRF52840).
  if (!stuck) systemOff(board::kButton, LOW);
  // Кнопка залипла: сон без неё, разбудит только USB.
  nrf_gpio_cfg_default(board::kButton);
  uint8_t sd = 0;
  sd_softdevice_is_enabled(&sd);
  if (sd)
    sd_power_system_off();
  else
    NRF_POWER->SYSTEMOFF = 1;
  while (true) {
  }
}

void enter_bootloader() {
  enterUf2Dfu();  // загрузчик с диском KEYCHAIN: прошивку можно перетащить файлом .uf2
  while (true) {
  }
}

// ---------------------------------------------------------------- пищалка

namespace {
uint16_t seq[4];  // значения каналов PWM: память должна быть в RAM и жить всё время
uint16_t cur_hz = 0;
bool cur_soft = false;
}  // namespace

void buzzer_begin() {
  NRF_PWM3->PSEL.OUT[0] = board::kBuzA;  // P0.25
  NRF_PWM3->PSEL.OUT[1] = (1u << 5) | (board::kBuzB - 32);  // P1.02
  NRF_PWM3->PSEL.OUT[2] = 0xFFFFFFFFu;
  NRF_PWM3->PSEL.OUT[3] = 0xFFFFFFFFu;
  NRF_PWM3->MODE = PWM_MODE_UPDOWN_Up;
  NRF_PWM3->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_4;  // 4 МГц: от 123 Гц до 2 МГц
  NRF_PWM3->DECODER = (PWM_DECODER_LOAD_Individual << PWM_DECODER_LOAD_Pos) |
                      (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
  NRF_PWM3->LOOP = 0;
  NRF_PWM3->SEQ[0].PTR = (uint32_t)seq;
  NRF_PWM3->SEQ[0].CNT = 4;
  NRF_PWM3->SEQ[0].REFRESH = 0;
  NRF_PWM3->SEQ[0].ENDDELAY = 0;
}

void buzzer(uint16_t hz, bool soft) {
  // BUZ_B — P1.02, ножка «только для низких частот» (до 10 кГц, рядом с радио).
  if (hz > 10000) hz = 10000;
  if (hz == cur_hz && soft == cur_soft) return;
  cur_hz = hz, cur_soft = soft;
  if (!hz) {
    NRF_PWM3->TASKS_STOP = 1;
    NRF_PWM3->ENABLE = 0;  // выводы возвращаются к GPIO: оба в нуле
    return;
  }
  const uint32_t top = 4000000u / hz;
  NRF_PWM3->COUNTERTOP = top > 32767u ? 32767u : top < 3u ? 3u : top;  // допустимо 3…32767
  const uint16_t half = (uint16_t)(NRF_PWM3->COUNTERTOP / 2);
  // Бит 15 значения — полярность: 1 — с начала периода высокий до сравнения, потом низкий
  // (так пишет analogWrite ядра), 0 — наоборот.
  if (soft) {
    seq[0] = (uint16_t)(NRF_PWM3->COUNTERTOP / 8) | 0x8000;  // узкий импульс на одном выводе
    seq[1] = 0x8000;                                         // второй вывод всё время в нуле
  } else {
    seq[0] = half | 0x8000;  // A: сначала высокий, спад на половине периода
    seq[1] = half;           // B: сначала низкий — противофаза
  }
  seq[2] = seq[3] = 0;
  NRF_PWM3->ENABLE = 1;
  NRF_PWM3->TASKS_SEQSTART[0] = 1;  // после конца последовательности PWM держит последние значения
}

// ---------------------------------------------------------------- вибро

namespace {
const uint16_t* vib_p = nullptr;
int vib_n = 0, vib_i = 0;
uint32_t vib_until = 0;
bool vib_started = false;
uint16_t vib_seq[4];  // значение для PWM2: память в RAM на всё время работы
constexpr uint16_t kVibTop = 800;  // 16 МГц / 800 = 20 кГц: выше слышимого

// Мотор на 3 В стоит прямо на VSYS (до 4.2 В от ячейки и ~4.7 В от USB): ключ работает ШИМом
// со скважностью 3 В / VSYS — ресурс и шум как у мотора на своём напряжении.
void vib_on(bool on) {
  if (!on) {
    NRF_PWM2->TASKS_STOP = 1;
    NRF_PWM2->ENABLE = 0;  // вывод возвращается к GPIO: выход в нуле
    return;
  }
  const float v = vsys_volts();
  float duty = v > 3.0f ? 3.0f / v : 1.0f;
  if (duty < 0.5f) duty = 0.5f;
  vib_seq[0] = (uint16_t)(kVibTop * duty) | 0x8000;  // бит 15 = 1: высокий (ключ открыт) duty периода
  vib_seq[1] = vib_seq[2] = vib_seq[3] = 0;
  NRF_PWM2->ENABLE = 1;
  NRF_PWM2->TASKS_SEQSTART[0] = 1;
}
}  // namespace

void vibro_begin() {
  digitalWrite(board::kVibEn, LOW);
  pinMode(board::kVibEn, OUTPUT);
  NRF_PWM2->PSEL.OUT[0] = board::kVibEn;
  NRF_PWM2->PSEL.OUT[1] = 0xFFFFFFFFu;
  NRF_PWM2->PSEL.OUT[2] = 0xFFFFFFFFu;
  NRF_PWM2->PSEL.OUT[3] = 0xFFFFFFFFu;
  NRF_PWM2->MODE = PWM_MODE_UPDOWN_Up;
  NRF_PWM2->PRESCALER = PWM_PRESCALER_PRESCALER_DIV_1;
  NRF_PWM2->COUNTERTOP = kVibTop;
  NRF_PWM2->DECODER = (PWM_DECODER_LOAD_Common << PWM_DECODER_LOAD_Pos) |
                      (PWM_DECODER_MODE_RefreshCount << PWM_DECODER_MODE_Pos);
  NRF_PWM2->LOOP = 0;
  NRF_PWM2->SEQ[0].PTR = (uint32_t)vib_seq;
  NRF_PWM2->SEQ[0].CNT = 1;
  NRF_PWM2->SEQ[0].REFRESH = 0;
  NRF_PWM2->SEQ[0].ENDDELAY = 0;
}

void vibrate(const uint16_t* pattern, int n) {
  if (!pattern || n <= 0) return;
  vib_p = pattern, vib_n = n, vib_i = 0;
  vib_started = false;
}

void vibro_update(uint32_t now) {
  if (!vib_p) return;
  if (!vib_started) {
    vib_started = true;
    vib_until = now + vib_p[0];
    vib_on(true);
    return;
  }
  if ((int32_t)(now - vib_until) < 0) return;
  if (++vib_i >= vib_n) {
    vib_on(false);
    vib_p = nullptr;
    return;
  }
  vib_on(!(vib_i & 1));
  vib_until = now + vib_p[vib_i];
}

bool vibro_busy() { return vib_p != nullptr; }

void vibro_stop() {
  vib_p = nullptr;
  vib_on(false);
}

// ---------------------------------------------------------------- микрофон

namespace {
int16_t mic_buf[512];
volatile float mic_level = -90.0f;
volatile bool mic_fresh = false;
bool mic_running = false;

void on_pdm() {
  // Обработчик прерывания: только средний квадрат блока, без тяжёлой работы.
  const int n = PDM.available();
  const int got = PDM.read(mic_buf, n > (int)sizeof(mic_buf) ? (int)sizeof(mic_buf) : n) / 2;
  if (got <= 0) return;
  int32_t mean = 0;
  for (int i = 0; i < got; ++i) mean += mic_buf[i];
  mean /= got;  // постоянная составляющая
  float acc = 0.0f;
  for (int i = 0; i < got; ++i) {
    const float s = (float)(mic_buf[i] - mean);
    acc += s * s;
  }
  const float ms = acc / (float)got / (32768.0f * 32768.0f);
  mic_level = ms > 1e-9f ? 10.0f * log10f(ms) : -90.0f;
  mic_fresh = true;
}
}  // namespace

bool mic_begin() {
  if (mic_running) return true;
  PDM.onReceive(on_pdm);
  PDM.setBufferSize((int)sizeof(mic_buf));
  mic_running = PDM.begin(1, 16000) == 1;
  // PDM.begin просит у SoftDevice кварц 64 МГц, а PDM.end его не отпускает: кварц (~0.25 мА)
  // работал бы до выключения, в том числе в кармане. Для уровня в дБ хватает внутреннего
  // генератора; радио SoftDevice включает кварц само, когда он ему нужен.
  uint8_t sd = 0;
  sd_softdevice_is_enabled(&sd);
  if (sd) sd_clock_hfclk_release();
  return mic_running;
}

void mic_end() {
  if (!mic_running) return;
  PDM.end();
  mic_running = false;
  digitalWrite(PIN_PDM_PWR, LOW);  // PDM.end оставляет вывод питания висеть входом
  pinMode(PIN_PDM_PWR, OUTPUT);
  nrf_gpio_cfg_default(PIN_PDM_CLK);  // ножки обесточенного микрофона — отключены
  nrf_gpio_cfg_default(PIN_PDM_DIN);
}

bool mic_on() { return mic_running; }

bool mic_take(float* dbfs) {
  if (!mic_fresh) return false;
  mic_fresh = false;
  *dbfs = mic_level;
  return true;
}

}  // namespace hal
