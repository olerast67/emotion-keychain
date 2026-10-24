# Загрузчик платы rev B

Готовый файл `emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex` — [загрузчик Adafruit nRF52](https://github.com/adafruit/Adafruit_nRF52_Bootloader) 0.11.0 (коммит `c67f0bc`) с небольшим патчем, вместе с SoftDevice S140 6.1.1. Записывается программатором один раз, как описано в [README прошивки](../README.md#первая-прошивка-загрузчик-по-swd).

Чем он отличается от загрузчиков готовых плат (`emotion_keychain_revb/board.h`):

- **Кнопка загрузчика** — кнопка брелока P0.22. Зажата при подключении USB — загрузчик ждёт прошивку и показывает диск KEYCHAIN.
- **Патч `button-dfu-needs-usb.patch`.** Та же кнопка будит брелок из полного сна, а пробуждение — это сброс, и кнопка в этот момент ещё нажата. Без патча загрузчик входил бы в режим прошивки без таймаута, и брелок на батарее висел бы с тёмным экраном. С патчем кнопка действует только при питании от USB. Если компьютер не ответил за 10 с, например брелок на зарядке от розетки, запускается прошивка.
- **REG0 = 3.3 В** записывается в UICR сразу: экран и датчики работают уже в загрузчике.
- **Светодиода нет:** вывод LED — свободная ножка P0.31.
- **Кварц 32 кГц не нужен:** загрузчик Adafruit всегда работает от внутреннего RC.
- **USB VID/PID** взяты у платы Raytac MDBT50Q-RX. Для прототипа это нормально, для продаж нужны свои.

## Как пересобрать

Нужны `arm-none-eabi-gcc`, `make` и Python.

```sh
git clone --recurse-submodules https://github.com/adafruit/Adafruit_nRF52_Bootloader.git
cp -r emotion_keychain_revb Adafruit_nRF52_Bootloader/src/boards/
cd Adafruit_nRF52_Bootloader
git checkout c67f0bc
git apply ../button-dfu-needs-usb.patch
make BOARD=emotion_keychain_revb all
# готовый файл: _build/build-emotion_keychain_revb/emotion_keychain_revb_bootloader-*_s140_6.1.1.hex
```
