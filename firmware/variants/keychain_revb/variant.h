// Плата брелока rev B: модуль Raytac MDBT50Q (nRF52840) без кварца 32 кГц.
//
// Номер вывода Arduino совпадает с номером ножки nRF52840: P0.xx — xx, P1.xx — 32 + xx.
// Так выводы в коде читаются прямо по схеме (hardware/rev-b/gen/netlist.py, GPIO).
#ifndef _VARIANT_KEYCHAIN_REVB_
#define _VARIANT_KEYCHAIN_REVB_

#define VARIANT_MCK (64000000ul)

// Кварца 32 кГц на плате нет: часы низкой частоты — от внутреннего RC с калибровкой.
#define USE_LFRC

#include "WVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINS_COUNT (48)
#define NUM_DIGITAL_PINS (48)
#define NUM_ANALOG_INPUTS (0)  // заряд меряется через VDDHDIV5, аналоговые входы не выведены
#define NUM_ANALOG_OUTPUTS (0)

// Светодиода у процессора нет: ядро и Bluefruit дёргают свободную ножку P0.31.
#define PIN_LED1 (31)
#define LED_BUILTIN PIN_LED1
#define LED_CONN PIN_LED1
#define LED_RED PIN_LED1
#define LED_BLUE PIN_LED1
#define LED_STATE_ON 1

// Кнопка на правом торце, на землю.
#define PIN_BUTTON1 (22)

// Шина датчиков (Wire): акселерометр LIS3DH 0x18 и датчик света LTR-303ALS 0x29.
#define WIRE_INTERFACES_COUNT 2
#define PIN_WIRE_SDA (27)
#define PIN_WIRE_SCL (1)
// Шина экрана (Wire1): модуль SSD1306 за ключом питания.
#define PIN_WIRE1_SDA (15)
#define PIN_WIRE1_SCL (13)

// Микрофон PDM; питание микрофона — с ножки P1.09.
#define PIN_PDM_DIN (8)
#define PIN_PDM_CLK (6)
#define PIN_PDM_PWR (41)

// SPI и Serial1 на плате не используются; ядру нужны номера — свободные ножки.
#define SPI_INTERFACES_COUNT 1
#define PIN_SPI_MISO (2)
#define PIN_SPI_MOSI (3)
#define PIN_SPI_SCK (28)
#define PIN_SERIAL1_RX (29)
#define PIN_SERIAL1_TX (30)

static const uint8_t SS = 10;
static const uint8_t MOSI = PIN_SPI_MOSI;
static const uint8_t MISO = PIN_SPI_MISO;
static const uint8_t SCK = PIN_SPI_SCK;

#define ADC_RESOLUTION 14

#ifdef __cplusplus
}
#endif

#endif
