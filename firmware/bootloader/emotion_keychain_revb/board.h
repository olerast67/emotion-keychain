// Загрузчик Adafruit nRF52 для платы брелока rev B (hardware/rev-b).
// Кладётся в src/boards/emotion_keychain_revb загрузчика, см. firmware/bootloader/README.md.
#ifndef EMOTION_KEYCHAIN_REVB_H
#define EMOTION_KEYCHAIN_REVB_H

// Светодиода у процессора нет: свободная ножка P0.31.
#define LEDS_NUMBER     1
#define LED_PRIMARY_PIN PINNUM(0, 31)
#define LED_STATE_ON    1

// Кнопка на правом торце (на землю): зажата при подключённом USB — загрузчик ждёт прошивку.
// Та же кнопка будит брелок из сна, поэтому без USB она загрузчик не включает, а если
// компьютер не ответил за 10 с (зарядка от розетки) — запускается прошивка. Нужен патч
// button-dfu-needs-usb.patch (README).
#define BUTTON_DFU     PINNUM(0, 22)
#define BUTTON_DFU_NEEDS_USB
#define BUTTON_DFU_TIMEOUT_MS 10000
// Обновление по Bluetooth — свободная ножка с подтяжкой, то есть никогда.
#define BUTTON_DFU_OTA PINNUM(0, 30)
#define BUTTON_PULL    NRF_GPIO_PIN_PULLUP

// REG0 модуля — 3.3 В: экран и датчики работают уже в загрузчике.
#define UICR_REGOUT0_VALUE UICR_REGOUT0_VOUT_3V3

#define BLEDIS_MANUFACTURER "olerast"
#define BLEDIS_MODEL        "Emotion Keychain rev B"

// USB: VID/PID платы Raytac MDBT50Q-RX — только для прототипа.
#define USB_DESC_VID          0x239A
#define USB_DESC_UF2_PID      0x010B
#define USB_DESC_CDC_ONLY_PID 0x010B

#define UF2_PRODUCT_NAME "Emotion Keychain rev B"
#define UF2_VOLUME_LABEL "KEYCHAIN"
#define UF2_BOARD_ID     "nRF52840-EmotionKeychain-revB"
#define UF2_INDEX_URL    "https://github.com/olerast67/emotion-keychain"

#endif
