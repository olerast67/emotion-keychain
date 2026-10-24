#include "variant.h"

#include "nrf.h"
#include "wiring_constants.h"
#include "wiring_digital.h"

// Вывод Arduino N — ножка nRF52840 N.
const uint32_t g_ADigitalPinMap[] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
};

void initVariant() {
  // Ядро заранее ставит на всех ножках высокий уровень выхода. Ключи экрана, мотора,
  // микрофона и выводы пищалки сначала гасятся и только потом становятся выходами,
  // чтобы при старте ничего не дёрнулось.
  static const uint8_t kOff[] = {19 /*DISP_EN*/, 21 /*VIB_EN*/, 41 /*MIC_PWR*/, 25 /*BUZ_A*/, 34 /*BUZ_B*/,
                                 PIN_LED1};
  for (uint8_t p : kOff) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
  }
}
