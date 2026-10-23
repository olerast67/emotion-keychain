# Аппаратная часть брелока-компаньона: выбор компонентов и BOM

Область: какие модули и чипы выбрать (MCU, дисплей, сенсоры, питание, готовые платы, кнопки), с цифрами и ссылками, плюс 2–3 рекомендуемые сборки. Детальный расчёт энергобюджета, прошивка, анимации и подводные камни исследуются отдельно. Здесь только то, что нужно для выбора деталей.

Общая оговорка по источникам: часть сайтов (espressif.com docs, seeedstudio.com, thethingsnetwork.org, hubble.com, qoitech.com) не открылась через fetch-инструмент (DNS-ошибка). Их цифры взяты из выдачи поиска, где показаны фрагменты страниц. Ссылки даны на исходные страницы, но для критичных цифр их стоит перепроверить вручную.

---

## 1. Микроконтроллеры: сон, активный режим, память, радио, удобство разработки

### Takeaway
Для брелока, который почти всё время спит и просыпается от прерывания IMU или кнопки, лучшее семейство — Nordic nRF52/nRF54. Ток сна с RTC и **сохранением RAM** у них порядка 1.5–3 µA, BLE есть, Arduino/Zephyr поддерживаются. Самый дешёвый и простой путь — ESP32-C3/C6: глубокий сон 5–7 µA у чипа, но **без сохранения основной RAM** (после пробуждения программа перезапускается), а у готовых плат реальный сон 40–700 µA. RP2040/RP2350 в сне потребляют сотни µA и больше, для батарейного брелока они плохо подходят. STM32U0 и CH32V003 подходят как дешёвый «немой» контроллер без радио.

### Cited Findings
**ESP32 (Espressif)**
- ESP32-C3: потребление в Deep-sleep по даташиту 5 µA, RTC-память при этом питается. — [ESP32-C3 datasheet](https://documentation.espressif.com/api/resource/doc/file/aYwvqd1B/FILE/esp32-c3_datasheet_en.pdf)
- ESP32-C3 Light-sleep: 130 µA. Цифру приводит пользователь форума со ссылкой на раздел 4.4 даташита; в выдаче её не было видно напрямую. Тот же пользователь намерил на ESP32-C3-DevKit-M1 1.1 mA в light sleep и 750 µA в deep sleep (вклад платы, USB-UART). — [esp32.com](https://esp32.com/viewtopic.php?p=83633)
- ESP-IDF: в light sleep цифровая периферия, большая часть RAM и CPU тактируются с clock gating, напряжение снижается, состояние сохраняется. В deep sleep CPU, большая часть RAM и вся периферия на APB_CLK отключены, остаются RTC-контроллер и RTC-периферия. — [ESP-IDF Sleep Modes (ESP32-C3)](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/sleep_modes.html)
- ESP32-C3 будят из deep sleep только IO из домена VDD3P3_RTC, через `esp_deep_sleep_enable_gpio_wakeup()` (пин и уровень). — [ESP-IDF v4.3 Sleep Modes C3](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32c3/api-reference/system/sleep_modes.html). На Arduino-ESP32 2.0.2 был отчёт, что GPIO0/GPIO1 не будили C3, а GPIO2/3 будили. — [arduino-esp32 issue #6656](https://github.com/espressif/arduino-esp32/issues/6656)
- ESP32-C6: Deep-sleep 7 µA (даташит v1.3/v1.5). — [ESP32-C6 datasheet](https://documentation.espressif.com/esp32-c6_datasheet_en.pdf)
- ESP32-H2: Deep-sleep 7 µA, LP-память остаётся под питанием. — [ESP32-H2 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-h2_datasheet_en.pdf)
- ESP32-S3: light sleep около 240 µA. — [Espressif ESP Techpedia, Low Power Introduction](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/performance/lowpower/lowpower-fundamental.html). На реальной плате S3 в light sleep намеряли ~700 µA – 1.9 mA, причина — утечка во flash; после отключения питания flash ток приблизился к даташиту. — [esp32.com](https://esp32.com/viewtopic.php?p=135260)
- ESP32-C5: в даташите (Table 6-9) light sleep 250 µA с отключённым периферийным тактированием. Пользователь намерил ~175 µA, после отключения консоли UART0 — ~125 µA. Цифру deep sleep для C5/C61 найти не удалось. — [esp32.com, ESP32-C5 Light-Sleep](https://esp32.com/viewtopic.php?p=149869)

**Nordic**
- nRF52840 PS: System ON без сохранения RAM с пробуждением по RTC ~1.5 µA @3 V; System OFF без RAM ~0.4 µA @3 V. — [nRF52840 PS v1.8](https://infocenter.nordicsemi.com/pdf/nRF52840_PS_v1.8.pdf)
- По словам разработчика на DevZone, System ON с сохранением RAM по даташиту 2.35 µA; на DK он намерил ~3.5 µA. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/111812/nrf52840-system-on-low-power-almost-reached-but-1-a-comes-from-nowhere/489011)
- nRF52840 в активном режиме: 52 µA/MHz (CoreMark из flash), то есть ≈3.3 mA при 64 MHz с HFXO и DC/DC. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/42266/how-to-measure-current-on-the-nrf52840-preview-dk/164698). Частота CPU в nRF52 фиксирована, 64 MHz. DC/DC можно включать только при наличии индуктивностей на плате. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/117329/how-to-change-nrf52840-cpu-clock-to-32mhz-or-even-lower/515177)
- nRF52832: System ON с полным сохранением RAM и пробуждением по любому событию 1.5 µA; без RAM, с пробуждением по RTC 1.9 µA. — [nRF52832 PS, PMU](https://docs.nordicsemi.com/r/bundle/ps_nrf52832/page/pmu.html)
- nRF54L15 (Nordic Academy): в System OFF без сохранения памяти и без периодического пробуждения до ~0.7 µA; с пробуждением по GRTC 0.9 µA. Есть варианты сохранения 96/192/256 KB RAM. — [Nordic Academy, memory retention and sleep modes](https://academy.nordicsemi.com/courses/designing-low-power-bluetooth-le-products/lessons/lesson-5-soc-specific-power-optimization-i/topic/memory-retention-and-sleep-modes/)
- nRF54L15 System ON Sleep ≈1.8 µA (вторичный источник со ссылкой на даташит). Автор намерил ~350 µA на дефолтной сборке NCS: по умолчанию включены UART-логи, полное сохранение RAM и LDO-режим. — [Hubble guide](https://hubble.com/community/guides/getting-the-most-battery-life-out-of-the-nrf54l15/). Каждые 4 с видны пики тока — калибровка RC-генератора. — [DevZone nRF54L15 System OFF peaks](https://devzone.nordicsemi.com/f/nordic-q-a/123511/nrf54l15---system-off-strange-current-peaks/544970)

**Raspberry Pi RP2040/RP2350**
- RP2040: минимальное потребление 0.18 mA в Dormant и 0.39 mA в Sleep. — [TinyGo low power](https://tinygo.org/docs/concepts/low-power/)
- Pico SDK, грубые цифры по VSYS: Dormant на Pico 0.95 mA, на Pico 2 (RP2350) 3.3 mA. У RP2350 таймер в dormant работает от LPOSC, а clk_ref продолжает идти. — [pico-sdk commit #3031](https://pigweed.googlesource.com/third_party/github/raspberrypi/pico-sdk/+/9d5da1ca93c85bdb78c63c177663df53d901da1e%5E%21/). Это противоречит измерению пользователя (~1.4 mA RP2350 против ~1.8 mA Pico 2/2W) и его замечанию о неточности LPOSC. — [Pimoroni forum](https://forums.pimoroni.com/t/rp2350-low-power-modes/27235)

**STM32 / WCH**
- STM32U0: Standby с RTC 160 nA; Stop 2 с RTC 630 nA при полном сохранении памяти. — [ST blog STM32U0](https://blog.st.com/stm32u0/). На форуме по STM32U073 намерили ~20 µA в Stop 2 с RTC-пробуждением; ST ответил, что без RTC 695 nA. — [ST Community](https://stcommunity.st.com/t5/stm32-mcus-products/stm32u073cct6-stop-mode-2-power-consumption/td-p/863116)
- CH32V003: в standby ~10 µA (пример ch32v003fun). Автопробуждение AWU от LSI 128 kHz, максимум 30 с. — [ch32v003fun standby_autowake README](https://git.c3pb.de/ahorn/ch32v003fun/-/raw/e44c7f40c297c76ed6d457cb85977f01742cab8a/examples/standby_autowake/README.md). В другом проекте намерили 7–8 µA. Из standby чип нельзя перепрошить обычным образом, wlink может стереть flash. — [atomic14 blog](https://blog.atomic14.com/2025/10/12/CH32V003-music.html). Даташит: в standby ядро обесточено, HSI/HSE выключены, пробуждение по EXTI, AWU, NRST, IWDG. — [CH32V003 datasheet (chipdip)](https://static.chipdip.ru/lib/164/DOC045164018.pdf)

**Измерения: общее**
- В режиме амперметра PPK2 с подключённым USB шум на измерительном разъёме добавляет 5–10 µA. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/81722/regrading-sleep-current-in-nrf52840)
- Ещё одна цифра с форума: на nRF52840 с простым WFE-циклом пользователь намерил ~10 µA, тот же код на nRF52832 дал ~1 µA. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/22651/nrf52840-power-consumption)

### Inferences
- Кадровый буфер 128×64 при 1 бит/пиксель занимает 1024 байта (расчёт). RAM хватает любому кандидату, кроме CH32V003: у него, по общеизвестным данным, 2 KB SRAM (в этом раунде не проверено). Для хранения анимаций важнее объём flash. Около 1 KB на кадр без сжатия (≈100 кадров на 100 KB) помещается в 1 MB nRF52840 или во внешние 4 MB у типичных ESP32-C3 (объёмы памяти ESP32 здесь не проверялись).
- **Сохранение RAM при пробуждении от GPIO/IMU.** nRF52/nRF54 в System ON: да, с прерыванием GPIOTE/PORT, ~1.5–3 µA. STM32U0 в Stop 2: да, с EXTI, ~0.6 µA. ESP32 в light sleep: да, но 130–240 µA и больше. ESP32 в deep sleep: нет, сохраняется только RTC/LP-память, программа перезапускается. CH32V003 в standby: ядро обесточено, это фактически перезапуск (по описанию даташита). RP2040 dormant: тактирование останавливается, но и в нём ~180 µA.
- Для «живого» лица с BLE (смартфон, уведомления) и долгим сном оптимальны nRF52840 и nRF54L15. ESP32-C3/C6 оправдан, если нужен Wi-Fi (NTP-время, погода, OTA) или важны цена и экосистема Arduino.
- Если радио не нужно, хорошая пара — STM32U0 (или STM32L0/L4: их цифры здесь не собирались) с LIS2DW12. Но порог входа выше, чем у Arduino на nRF/ESP.
- Активный ток ESP32-C3 при 160 MHz (обычно цитируют ~20–25 mA) и RP2040 (~20 mA) в этом раунде источниками не подтверждены, см. Gaps.

### Gaps
- Не найдены первичные цифры: deep sleep ESP32-S3 (вторичные блоги пишут ~7 µA), ESP32-C5/C61, активный ток ESP32-C3/C6/S3 и RP2040/RP2350, точный ток nRF52840 System ON с полным сохранением RAM (по памяти ~3.16 µA, не подтверждено), System ON Idle для nRF54L15 из PS.
- STM32L0/L4/U5, ATtiny/AVR (например ATtiny1616 в power-down) и CH32V203 не исследовались из-за ограничения по числу запросов.
- Объёмы SRAM/flash ESP32-C3/C6/S3, RP2040 (264 KB), RP2350 (520 KB), CH32V003 приведены по памяти, источниками не проверены.

---

## 2. Дисплеи: OLED SSD1306 и альтернативы

### Takeaway
Синий/белый SSD1306 0.96" 128×64 — правильный выбор для «лица». Он потребляет ~1.4 mA при чёрном экране, ~8.7 mA при 50 % засветке и ~11.4 mA при 100 %, а в sleep (0xAE) ~14 µA. Лицо из глаз на чёрном фоне засвечивает малую долю пикселей, поэтому реальный ток невелик. Для плавной полноэкранной анимации лучше SPI-вариант (полный кадр ~0.8 ms против ~23 ms по I2C 400 kHz). Если нужен «вечно видимый» экран за микроватты, подходит Sharp Memory LCD. Цветные TFT и AMOLED потребляют на порядок больше и для брелока на маленьком LiPo неоптимальны.

### Cited Findings
**SSD1306 0.96" 128×64, потребление**
- Измерение шести модулей 0.96" 128×64 SSD1306 (питание 3.7 V Li-ion → стабилизатор 3.3 V, прецизионный амперметр): пиксели выключены 1.4 mA, 50 % — 8.7 mA, 100 % — 11.4 mA; дисплей выключен — 13.7 µA. Выбор библиотеки на потребление не влиял. — [TTN forum, Big ESP32 + SX127x topic](https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-2/11973/646); та же серия измерений — [preamp.org](https://preamp.org/static/ssd1306-oled-current-consumption/index.html)
- Документация Soldered: плата потребляет около 20 mA в зависимости от числа активных пикселей. — [Soldered SSD1306](https://docs.soldered.com/ssd1306/overview/)
- Fab Academy: 15 mA с включённым дисплеем, 0 mA с выключенным. — [Fab Academy 2026](https://fabacademy.org/2026/labs/puebla/students/anasofia-tellez/assignments/week10.html)
- zbotic: 25–30 mA при полностью белом экране, но данные внутренне противоречивы (0.4–0.7 mA vs 0.5 mA idle), источник менее надёжен. — [zbotic](https://zbotic.in/ssd1306-power-consumption-oled-vs-lcd-for-solar-iot-nodes/)
- Команда 0xAE (display off) — режим сна, содержимое GDDRAM сохраняется. Флаг external_vcc выбирает внешний VCC или внутренний charge pump. — [OpenMV ssd1306 docs](https://docs.openmv.io/dev/library/omv.ssd1306.html)

**I2C или SPI: скорость**
- Полный кадр 1024 байта по I2C 400 kHz занимает ~23 ms (≤30 fps с накладными расходами), по SPI 10 MHz ~0.8 ms. — [displaymodule.com: SPI vs I2C for OLED](https://www.displaymodule.com/blogs/knowledge/spi-vs-i2c-for-oled-speed-pin-count)
- Бенчмарк MicroPython: 0.087 с/кадр по I2C против 0.066 с/кадр по SPI. Разница всего ×1.3, потому что доминируют накладные расходы Python. — [MicroPython forum](https://forum.micropython.org/viewtopic.php?p=1766)
- U8g2 на SSD1306 128×64 даёт от 2 до 60 fps в зависимости от режима буфера и конструктора. — [Arduino forum](https://forum.arduino.cc/t/ssd1306-u8g2lib-framerate/522803)
- Автор u8glib утверждал, что SSD1306 не держит больше 100 kHz I2C (~11 fps). Ему возражали: по даташиту минимальный период такта 2.5 µs, то есть 400 kHz. — [Arduino forum](https://forum.arduino.cc/t/u8glib-sdd1306-oled-slow/267988)
- Пример: 100 fps на OLED через SPI. — [atrium wiki: oled-100fps](https://github.com/maierkomor/atrium/wiki/oled-100fps)

**Совместимые контроллеры и размеры**
- SH1106 (обычно в модулях 1.3"): внутренняя RAM 132×64 при видимой области 128×64, поэтому библиотеки SSD1306 дают сдвиг изображения. Нет команд SetColumnAddress/SetPageAddress, страницы нужно адресовать вручную. — [zbotic SH1106 vs SSD1306](https://zbotic.in/sh1106-vs-ssd1306-difference-between-128x64-oled-drivers/); [universal-solder](https://www.universal-solder.ca/displays-oled/)
- SSD1315 считается совместимым с SSD1306, драйверы и библиотеки поддерживают оба. — [docs.rs ssd1315](https://docs.rs/crate/ssd1315/0.1.1); [Arduino ssd1306xled](https://www.arduino.cc/reference/en/libraries/ssd1306xled)
- 0.42" 72×40 SSD1306 (I2C): модуль 12×11 мм. — [Elecrow](https://www.elecrow.com/0-42inch-tiny-oled-display-4-pin-screen-module-with-ssd1306-driver-and-iic-interface.html). Контроллер SSD1306B. — [BuyDisplay 0.42"](https://www.buydisplay.com/i2c-white-0-42-inch-oled-display-module-72x40-arduino-raspberry-pi)
- 0.49" 64×32: модуль 15×16 мм. — [BuyDisplay 0.49"](https://www.buydisplay.com/i2c-white-0-49-inch-oled-display-module-64x32-arduino-raspberry-pi)
- 0.66" 64×48: панель 18.46×18.1×1.21 мм, активная область 13.42×10.06 мм. — [Global Sources](https://www.globalsources.com/product/oled-display-modules_1226136781f.htm)

**Голая панель (FPC) для своей платы**
- Встроенному charge pump SSD1306 нужны конденсаторы между C1P/C1N и C2P/C2N, конденсатор на VCC и подключённый VBAT (3.3–4.2 V). Если преобразователь не используется, VBAT соединяют с VDD. Charge pump включается командой 0x8D, 0x14. — [DEP 096016B-W datasheet](https://cdn.soselectronic.com/productdata/e3/cc/8632cd9a/dep-096016b-w.pdf); [Unisystem WEO009616B](https://download.unisystem.com/OLED-displays/WEO009616BWPP3N0Y000/WEO009616BWPP3N0Y000.pdf)
- Типичные ошибки на своих платах: закороченный конденсатор charge pump даёт тусклый экран, не те номиналы конденсаторов, забытые подтяжки RES/CS. — [Fab Academy 2020](https://fabacademy.org/2020/labs/opendot/students/sol-bekic/log/11/); [EEVblog](https://eevblog.com/forum/beginners/cant-get-ssd1306-oled-working-on-custom-pcb)
- Открытая референсная схема и разводка модуля 1.3" SSD1306 (EagleCAD) от Adafruit. — [GitHub Adafruit](https://github.com/adafruit/Adafruit-1.3inch-128x64-Mono-OLED-PCB)

**Альтернативы**
- Sharp Memory LCD LS013B7DH03 (1.28" 128×128): на статичном изображении от 2 µA, при обновлении раз в секунду меньше 5 µA (маркетинг Mikroe), питание 3.3 V, инверсия VCOM не реже 1 Гц. — [LCD Mono Click (thedebugstore)](https://thedebugstore.com/products/lcd-mono-click-board-mikroe-3789-uk)
- Sharp LS011B7DH03: 160×68, ч/б, без подсветки, около 25 µW typ. — [Sharp lineup](https://global.sharp/products/device/lineup/selection/lcd/mobile/index.html). DigiKey: 1.08", активный статус, 28 недель срок поставки. — [DigiKey](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS011B7DH03/25953418). Data-Modul указывает 1.10" и 160×72, это расходится с Sharp. — [Data-Modul](https://data-modul.com/en/product-finder/compare-list/9708947)
- Adafruit SHARP Memory 1.3" 168×144 (LS013B7DH05): $24.95. — [Adafruit #3502](https://www.adafruit.com/product/3502)
- GC9A01 1.28" круглый 240×240: 3.3 V, рабочий ток 20 mA, подсветка из двух белых LED параллельно. — [Elecrow GC9A01](https://www.elecrow.com/1-28-inch-round-lcd-module-gc9a01-240x240-lcd-display.html?r=YWxzb2Fk). Сам контроллер TFT потребляет ~1–5 mA, основное — подсветка (вторичный блог). — [zbotic backlight](https://zbotic.in/lcd-backlight-dimming-pwm-control-for-power-saving-projects/)
- Waveshare ESP32-S3-Touch-AMOLED-1.43: ESP32-S3-PICO-1-N8R8, 466×466, CO5300 по QSPI, тач CST820, 8 MB PSRAM / 16 MB flash, RTC PCF85063, разъём LiPo MX1.25. Цифр тока производитель не даёт. — [Waveshare](https://www.waveshare.com/product/esp32-s3-touch-amoled-1.43.htm)

**Цены на SSD1306**
- 0.96" SSD1306 I2C: ~$9 за штуку в западной рознице, AZDelivery 3 шт. за ~$13. — [microcontrollerslab 2026](https://microcontrollerslab.com/best-oled-displays-for-microcontroller-embedded-projects-buying-guide/). BuyDisplay ¥22.10 за 1 шт. — [BuyDisplay](https://www.buydisplay.com/chinese/i2c-white-0-96-inch-oled-display-module-128x64-arduino-raspberry-pi). Geekworm ~$5. — [Geekworm](https://geekworm.com/en-au/products/0-96-inch-oled)
- Готовый набор ESP32-C3 + 0.96" SSD1306 (продаётся как «AI-робот» с динамиком и батареей): $13.63. — [eBay](https://www.ebay.de/itm/297717611810)

### Inferences
- Ток OLED примерно линеен по засветке: 1.4 mA + ~0.15 mA на каждый % пикселей (интерполяция по данным TTN). Лицо из двух глаз — порядка 10–25 % пикселей, то есть ~3–5 mA. Это главный потребитель в активной фазе, MCU тратит меньше.
- Модули с 4 пинами (GND/VCC/SCL/SDA) — это I2C. Модули с 7 пинами (GND/VCC/D0/D1/RES/DC/CS) — SPI, их можно перепаять на I2C. Для 30+ fps без рывков лучше брать SPI-модуль или своё подключение голой панели по SPI.
- Сон через 0xAE оставляет ~14 µA на модуле с LDO и подтяжками. Для режима «спит часами» лучше отключать VCC модуля load switch’ем (раздел 4). Но тогда при пробуждении нужна повторная инициализация и перезапись кадра.
- Двухцветные «жёлто-синие» модули — это жёлтая полоса из верхних 16 строк и синий остальной экран. Для лица это мешает: глаза разрываются на два цвета. Для брелока лучше монохромный белый или синий (по общему знанию, не проверено источником).
- Sharp LS011B7DH03 (160×68, ~1.08") по формату и размеру ближе всего к 0.96" OLED и может показывать лицо постоянно при ~25 µW. Но он отражательный (без подсветки, в темноте не виден) и не «светится», как OLED.
- 1.43" AMOLED и GC9A01 физически крупнее (Ø~32–36 мм) и требуют десятки mA. Это «премиум»-вариант, уже не крошечный брелок.

### Gaps
- Не найдено официальных цифр SSD1306 из даташита Solomon Systech (ток sleep, ток charge pump), данных SSD1315 и SSD1309 2.42" (тока), измерений для 0.91" 128×32 и 1.3" SH1106.
- Нет подтверждённых измеренных токов для 1.43" AMOLED (CO5300/SH8601), ST7789 и GC9A01 в реальном режиме. Нет данных по e-paper (частичное обновление, энергия на кадр).
- Цены голых 30-пиновых панелей SSD1306 на AliExpress/LCSC не найдены.

---

## 3. Сенсоры для «реакции на мир»: акселерометр/IMU, микрофон, свет, касание

### Takeaway
Для «встряхнуть, наклонить, постучать, подбросить» нужен акселерометр с аппаратным wake-up/tap и прерыванием на MCU. Лучшие по току: ADXL362 (270 nA в wake-up режиме) и LIS2DW12 (0.38–1 µA, на практике ~4 µA при 25 Гц). Классика LIS2DH12/LIS3DH (от 2 µA) дешёвая и есть везде. 6-осевые IMU (LSM6DSO/X, BMI270, QMI8658) потребляют на порядок больше, их стоит брать, только если нужен гироскоп или готовые жесты. Микрофон для «слушает»: PDM T5838 с аппаратным Acoustic Activity Detect (~20 µA). Свет: VEML7700 (0.5 µA в shutdown) или фототранзистор на АЦП. Касание: TTP223 в low-power режиме (~1.5–2 µA).

### Cited Findings
**Акселерометры**
- LIS2DW12: Low-Power Mode 1, 12 бит — ~1 µA; при 1.6 Гц — 0.38 µA. — [ST blog, LIS2DW12 «Femto» accelerometer](https://blog.st.com/?p=45925)
- Реальное измерение LIS2DW12: ~4.1 µA @3.3 V при 25 Гц против ожидаемых по таблице даташита 3 µA @1.8 V. BMA400 в sleep ~850 nA @1.8 V, но BMA400 снят с производства. — [kriswiner/LIS2DW12 (GitHub)](https://github.com/kriswiner/LIS2DW12)
- ST на форуме рекомендовал LIS2DW12 вместо LIS3DH для энергоэффективного double-tap. — [ST Community](https://community.st.com/mems-sensors-48/lis3dh-single-double-tap-detection-current-consumption-56812)
- ADXL362: пробуждение по движению от 270 nA при ~6 Гц, <2 µA при 100 Гц ODR (1.8 µA @100 Гц, 3.0 µA @400 Гц при 2.0 V), standby 10 nA. Прерывания обрабатываются автономно, без MCU. Не использует duty-cycling: полная полоса на всех ODR. — [ADXL362 datasheet (Mouser)](https://www.mouser.com/datasheet/2/609/ADXL362_MI-1551392.pdf)
- LIS2DH12: потребление от 2 µA, ODR 1 Гц–5.3 кГц, два независимых прерывания wake-up/free-fall, функции sleep-to-wake и return-to-sleep. — [LIS2DH12 datasheet](https://cdn.sparkfun.com/assets/a/e/e/3/9/lis2dh12_datasheet.pdf). LIS3DH: от 2 µA, ODR 1 Гц–5 кГц, те же два прерывания. — [LIS3DH datasheet](https://ece-classes.usc.edu/ee459/library/datasheets/LIS3DH.pdf)
- Цена LIS2DH12TR на LCSC (C110926): $0.36–0.56 от 1000 шт., ~$0.61–0.92 за 1 шт. (разные снимки кэша, запас 0–19 520 шт.). — [LCSC C110926](https://www.lcsc.com/product-image/C110926.html). LIS2DW12TR на LCSC: C189624. — [LCSC C189624](https://www.lcsc.com/product-image/C189624.html)

**6-осевые IMU**
- BMI270: ~685 µA на полном ODR, suspend 3.5 µA, акселерометр в low-power 25 Гц ~210 µA (по даташиту). Bosch на странице продукта пишет «10 µA accelerometer only», это противоречие. Распознавание жестов/пробуждение в ультранизкопотребляющем домене ~30 µA. — [BMI270 datasheet](https://cdn.sparkfun.com/assets/5/c/0/0/d/bst-bmi270-ds000.pdf); [Bosch BMI270](https://www.bosch-sensortec.com/products/motion-sensors/imus/bmi270/)
- LSM6DSOX: 0.55 mA в high-performance режиме. Аппаратно: free-fall, 6D-ориентация, single/double tap, wake-up. — [LSM6DSOX datasheet](https://cdn.sparkfun.com/assets/6/f/b/6/b/lsm6dsox.pdf)
- QMI8658: tap-детекция работает только при включённом акселерометре. Цифр тока не найдено. — [RIOT OS qmi8658 driver](https://api.riot-os.org/group__drivers__qmi8658.html)
- XIAO nRF52840 Sense имеет на борту 6-осевой LSM6DS3TR-C и PDM-микрофон MSM261D3526H1CPM. — [Seeed wiki XIAO BLE](https://wiki.seeedstudio.com/XIAO_BLE/); [Zephyr xiao_ble](https://docs.zephyrproject.org/latest/boards/seeed/xiao_ble/doc/index.html)

**Микрофон**
- TDK T5838 (PDM): Acoustic Activity Detect (wake-on-sound, будит SoC) в трёх режимах, от 20 µA. Ultra-low-power режим 130 µA, High Quality 330 µA. — [TDK InvenSense press release](https://invensense.tdk.com/news-media/tdk-introduces-worlds-lowest-power-PDM-microphone-with-acoustic-activity-detect)
- Дистрибьютор приводит другие цифры: HQ 310 µA, LP 120 µA, ultrasonic 500 µA, sleep 9 µA. Их происхождение не подтверждено. — [MEV Elektronik T5838](https://shop.mev-elektronik.com/product/t5838/)

**Свет**
- VEML7700: shutdown 0.5 µA typ @3.3 V. В режиме энергосбережения ~2 µA (интервал 4.1 с), ~8 µA (600 ms), ~45 µA без PSM. — [Vishay VEML7700 datasheet](https://cdn.sparkfun.com/assets/c/a/5/0/6/VEML7700_Datasheet.pdf); [Vishay app note](https://www.mouser.lt/pdfdocs/designingveml7700.pdf)

**Касание**
- TTP223: low-power режим 1.5 µA typ / 3.0 µA max @3 V (SLRFTB=1), 2.0/4.0 µA при SLRFTB=0. Вариант TTP223E: 2.0/4.0 µA. Время отклика ~220 ms в low-power и ~60 ms в fast. — [TTP223 datasheet (comet.bg)](https://store.comet.bg/download-file.php?id=25921); [TTP223E-HA6 (Makers Electronics)](https://makerselectronics.com/product/ttp223e-ha6-smd-ic-sot-23-6-touch-screen-controller/)

### Inferences
- Сценарий «стук = удивление, тряска = головокружение, наклон = взгляд в сторону, переворот = сон». Хватает акселерометра с tap/double-tap, wake-up, 6D/4D-ориентацией и free-fall. Всё это есть у LIS2DH12/LIS3DH/LIS2DW12. У ADXL362 есть activity/inactivity, но нет аппаратного tap (по общему знанию, не проверено). Гироскоп для брелока избыточен.
- Рекомендация. Своя плата: LIS2DW12, лучший баланс µA, tap и цены. ADXL362 — если важен каждый µA, но tap придётся делать программно. Прототип на модулях: LIS3DH/LIS2DH12-breakout или встроенный LSM6DS3TR-C на XIAO Sense.
- «Слушает» с долгим автономным режимом — это T5838 с AAD (~20 µA), который будит MCU по звуку. Постоянно включённый обычный PDM-микрофон с обработкой на MCU стоит сотни µA плюс время CPU. Аналоговые MAX4466/MAX9814 в этом раунде не измерялись. Они дают простую огибающую на АЦП, но постоянно потребляют порядка сотен µA (по общему знанию, не проверено).
- Для «реакции на свет» хватит фототранзистора или светодиода в режиме фотодиода на АЦП, включаемого на миллисекунды раз в N секунд: почти ноль µA в среднем. VEML7700 даёт калиброванный люкс при 0.5 µA в shutdown.
- ESP32-S3 имеет встроенные touch-пины, ESP32-C3 и nRF52 — нет (по общему знанию, не проверено). На nRF/C3 касание проще сделать через TTP223.

### Gaps
- Не найдено: точные цифры LIS3DH/LIS2DH12 при 1/10 Гц, MC3479, BMA456, LSM6DSO/DS3TR-C (low-power accel), QMI8658 (ток); токи MSM261, SPH0645, MAX4466, MAX9814; LTR-303, BH1750 (power-down); датчики температуры/влажности.
- Нет данных по вибромоторам (ток пуска ~50–100 mA по общему знанию) и пьезо-пищалкам.

---

## 4. Питание: аккумулятор, зарядка, защита, стабилизаторы, отключение дисплея, топливомер, USB-C

### Takeaway
За модулем 0.96" (~27×27 мм) помещается плоский LiPo 20×30 мм. 402030 (~150–200 мАч реально), 502030 (~250 мАч) и 602030 (~300 мАч) отличаются в основном толщиной: 4, 5 и 6 мм. Заявленные ёмкости дешёвых ячеек часто завышены. Для своей платы выгоднее всего BQ25180 (I2C, ship mode 15 nA) или BQ25120A (зарядка, 700 nA, buck и LDO в одном чипе). Простой вариант — MCP73831 (утечка с батареи 0.15 µA). Стабилизатор 3.3 V: TPS7A02 (25 nA), XC6206 (~1 µA) или HT7333 (~3.5 µA), а не ME6211 (50 µA) и не AMS1117 (мА). Дисплей обесточивать load switch’ем TPS22916 (10 nA off). MAX17048 потребляет 3 µA в hibernate. Это сопоставимо со всем остальным бюджетом сна, поэтому делитель на АЦП с ключом экономнее.

### Cited Findings
**LiPo-ячейки**
- 502030: 20×30×5 мм, 250 мАч, 6 г, разъём JST PHR-2, длительный разряд 0.25 A, пиковый 0.75 A. — [UNIT Electronics 502030](https://uelectronics.com/producto/bateria-lipo-3-7v-250mah-502030/?add-to-cart=86654). Такой же размер, 5 г. — [eBay LP502030](https://www.ebay.de/itm/172913751631)
- 602030: 6×20×30 мм, 7 г, 300 мАч в таблице (в описании 500 мАч, противоречие), 1.11 Wh. — [Makers Electronics 602030](https://makerselectronics.com/product/rechargeable-602030-lipo-battery-cell-3-7v-300mah-wire-solder/)
- 602035: 35×20×6 мм, 400 мАч, 8 г, длительный разряд 0.40 A. — [UNIT Electronics 602035](https://uelectronics.com/producto/bateria-lipo-3-7v-400mah-602035/?add-to-cart=310123)
- 402030: ~4×20×30 мм. Заявленная ёмкость у разных продавцов 200, 220, 320 и 350 мАч. Один продавец прямо пишет: «500mAh (Actual Capacity 140mAh)». — [Makers Electronics 402030 «actual 140mAh»](https://makerselectronics.com/product/rechargeable-402030-lipo-battery-cell-3-7v-500mah-wire-solder-actual-capacity-140mah/); [Besomi 402030 200 мАч](https://besomi.com/product/402030-besomi-200mah-3-7v-lithium-battery); [Hubtronics 402030 320 мАч](https://hubtronics.in/battery-cells/402030-3.7v-320mah-lipo-battery). Карточка производителя LP402030: 190 мАч, 20.5×31×4.3 мм. Привязка к URL сделана по сводке поиска, нужна проверка. — [EEMB](https://www.eemb.com/product-128)

**Зарядные ИС**
- BQ25180 (I2C, 1-cell linear): ток покоя от батареи ~3–3.5 µA typ без функции кнопки, 4–5 µA с ней. Ship mode с кнопкой 3.2 µA typ (4.5 max). Shutdown (VIN=0, adapter-sense wake) 15 nA. Вход в ship mode по I2C или долгим нажатием кнопки (по умолчанию 10 с), выход — кнопкой или подачей входного питания. — [BQ25180 datasheet SLUSE99](https://ti.com/lit/pdf/SLUSE99); [TI E2E](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1456013/bq25180-bq25180); [TI SLYT842 (shelf life)](https://ti.com/document-viewer/lit/html/SLYT842/GUID-420F3B4A-4432-4FA7-A743-80A5AD7CDC85)
- BQ25120A (PMIC для носимых устройств): Iq 700 nA с включённым buck без нагрузки. Buck 300 mA (по умолчанию 1.8 V), LDO 100 mA или load switch (по умолчанию load switch), ток заряда 5–300 mA, ship mode <50 nA, WCSP 2.5×2.5 мм, 6 внешних компонентов, настройка по I2C. — [TI BQ25120A](https://ti.com/product/BQ25120A). Есть в каталоге chipdip. — [chipdip BQ25120AYFPT](https://chipdip.ru/product0/8004995045)
- MCP73831: ток разряда батареи (IDISCHARGE) 0.15 µA typ / 2 µA max при плавающем PROG или VDD<VSTOP, 0.25 µA при плавающем VDD. — [MCP73831 datasheet](https://cdn.sparkfun.com/assets/2/c/c/5/a/MCP73831.pdf)
- TP4056: без входного питания ток с батареи <2 µA. Блокирующий диод не нужен благодаря внутреннему PMOSFET. — [TP4056 datasheet](https://mikrocontroller.net/attachment/250976/TP4056.pdf). Модули TP4056+DW01 отключают нагрузку примерно при 2.4 V. — [ShillehTek TP4056 manual](https://shillehtek.com/blogs/shillehtek-product-manuals/18650-tp4056-1a-3-7-4-2v-lipo-battery-charging-board-micro-usb-with-current-protection-manual); про DW01A — [best-microcontroller-projects](https://best-microcontroller-projects.com/dw01a.html)
- На XIAO nRF52840 стоит зарядник BQ25101, ток заряда 50/100 mA. — [RobotShop XIAO nRF52840](https://jp.robotshop.com/en/products/seeed-studio-xiao-nrf52840)

**Стабилизаторы 3.3 V**
- TPS7A02: Iq <25 nA, shutdown 3 nA. — [Embedded Computing: TI TPS7A02](https://embeddedcomputing.com/technology/analog-and-power/ti-claims-lowest-quiescent-current-ultra-small-ldo-linear-regulator). LCSC: TPS7A0212PDBVR (1.2 V) $0.298 @1k; TPS7A0225PDBVR $0.45 @1k; ~$0.58–1.08 за 1 шт. — [LCSC C5220172](https://lcsc.com/product-detail/image/TPS7A0212PDBVR_C5220172.html); [LCSC C1693617](https://www.lcsc.com/product-image/C1693617.html)
- XC6206P332: 1.0 µA typ. — [embeddedrelated XC6206P332MR-G](https://www.embeddedrelated.com/parts/p/XC6206P332MR-G)
- MCP1700: 1.6 µA typ; та же карточка в таблице пишет 4 µA (противоречие). — [embeddedrelated MCP1700-330](https://www.embeddedrelated.com/parts/p/MCP1700-330)
- HT73xx: ~3.5 µA typ. Ссылка на PDF взята по сводке поиска, нужна проверка. — [HT73xx datasheet (chipdip)](https://static.chipdip.ru/lib/704/DOC043704740.pdf)
- ME6211: 50 µA typ, standby 0.1 µA, dropout 100 mV при 100 mA, до 500 mA. — [ME6211 datasheet (LCSC)](https://wmsc.lcsc.com/wmsc/upload/file/pdf/v2/lcsc/2201121200_MICRONE-Nanjing-Micro-One-Elec-ME6211C25M5G_C2835813.pdf)
- RT9013: 25 µA, 500 mA, dropout 250 mV (таблица с форума). — [esp8266.com](https://www.esp8266.com/viewtopic.php?p=82463)
- LM1117/AMS1117: Iq 5–10 mA, для батарейных устройств не подходит. — [mhightower83 wiki: LDO Regulators](https://github-wiki-see.page/m/mhightower83/Arduino-ESP8266-misc/wiki/Power,-LDO-Regulators); пример высокого Iq ASM1117 на ESP32-PICO-KIT — [esp32.com](https://esp32.com/viewtopic.php?p=76989)

**Импульсные преобразователи**
- TPS62840: buck 750 mA, Iq 60 nA typ, 120 nA в режиме 100 % duty, Vin 1.8–6.5 V, 16 выходных напряжений задаются пином VSET (включая 3.3 V). — [TI TPS62840](https://www.ti.com/product/TPS62840). На chipdip есть TPS62842. — [chipdip](https://www.chipdip.ru/product0/8017659753)
- TPS63802 (buck-boost): ориентир по Iq 11 µA (тред на TI E2E, где измерения пользователя). — [TI E2E TPS63802](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1101467/tps63802-feed-forward-capacitor-experiment)

**Load switch**
- TPS22916: в выключенном состоянии 10 nA typ / 100 nA max, во включённом 0.5 µA typ, обратная утечка ≤300 nA. На пине ON pull-down ~750 kΩ. — [Mouser TPS22916](https://www.mouser.in/new/texas-instruments/ti-tps22916-load-switch/); [TI TPS22916](https://www.ti.com/product/de-de/TPS22916); [LCSC C3230033](https://www.lcsc.com/product-detail/C3230033.html)

**Топливомер**
- MAX17048: 23 µA в активном режиме, 3 µA в hibernate. ModelGauge обходится без шунта и без циклов обучения. — [Analog Devices MAX17048](https://www.analog.com/en/products/max17048). В одной статье указано 4 µA в hibernate, это противоречит даташиту. — [eepower](https://eepower.com/new-industry-products/micropower-fuel-gauge-from-maxim-reduces-quiescent-current-by-4x-and-size-by-3x/)

### Inferences
- **Размер.** 502030 и 602030 (20×30 мм) по ширине влезают за модуль ~27×27 мм, но по длине выступают на ~3 мм. 402535 и 503035 (25×35 мм) по размеру уже не помещаются за 0.96". При своей плате 27×27 мм под ячейку 20×30 корпус придётся сделать немного длиннее, например с ушком для кольца сверху. Толщина пакета: модуль OLED ~4 мм (оценка) + плата ~1–1.6 мм + LiPo 4–6 мм, итого ~10–12 мм.
- **Сравнение ёмкостей.** 402030 ≈ 150–200 мАч реально, 502030 ≈ 250 мАч, 602030 ≈ 300 мАч. Вывод: выбирать по толщине и честному поставщику, а не по цифре на этикетке.
- **Бюджет сна на своей плате** (оценка): nRF52840 System ON ~1.5–3 µA + LIS2DW12 ~1 µA + TPS7A02 0.025 µA + BQ25180 ~3 µA (или MCP73831 ~0.15 µA) + OLED, отключённый TPS22916, 0.01 µA. Итого ~3–7 µA. При 250 мАч это теоретически годы, на практике ограничивает саморазряд LiPo.
- В этой оценке зарядник BQ25180 становится сопоставимым по току с MCU. Это аргумент за MCP73831 (если не нужна кнопка ship-mode и I2C) или за BQ25120A (700 nA с buck, ship <50 nA).
- MAX17048 в hibernate (3 µA) удваивает бюджет сна. Делитель 2×1 MΩ на АЦП, подключаемый ключом на время измерения, почти ничего не потребляет. Модель «процент заряда» можно сделать по кривой напряжения LiPo.
- Готовые модули TP4056+DW01 удобны для прототипа, но ток заряда 1 A по умолчанию слишком велик для ячейки 200–300 мАч. Нужно менять резистор PROG, обычно до ~0.2–0.3 C. Значения резистора не проверены, см. даташит TP4056. DW01 отключает нагрузку только около 2.4 V — для LiPo это глубокий разряд. Порог отключения стоит продублировать в прошивке, около 3.3 V.
- USB-C только для зарядки: два резистора 5.1 kΩ от CC1/CC2 на землю (Rd по спецификации USB Type-C). Без них зарядники C-to-C не дают 5 V. В этом раунде источником не подтверждено, см. Gaps.

### Gaps
- Нет надёжных данных по 302030 и 402535/503035 (ёмкость, размеры), а также по ценам LiPo на AliExpress и в РФ.
- Не найдены Iq TP4054, LTC4054, BQ21040, BQ25100, а также ток утечки модуля TP4056+DW01+FS8205 целиком (его нужно мерить).
- Не найден первичный даташит TPS63802 (11 µA взято из треда пользователя). Нет цифр XC6220, BQ27441.
- Требование 5.1 kΩ на CC (USB Type-C Rd) не подтверждено ссылкой в этом раунде.

---

## 5. Готовые dev-платы с MCU, зарядником LiPo и низким током сна

### Takeaway
Лучшая готовая база для прототипа — Seeed XIAO nRF52840 **Sense**: на ней есть IMU, PDM-микрофон, зарядник BQ25101, Arduino/Zephyr, ~5 µA в System OFF по спецификации. Но реальный ток сна сильно зависит от прошивки и обвязки: на форумах от 2.4 µA до 10 mA. Новые XIAO nRF54L15 ($9.90, Sense $15.99) — вариант «на будущее» с System OFF ~0.6–0.8 µA. XIAO ESP32-C3/C6/S3 дают 14–44 µA в deep sleep. Клоны ESP32-C3 SuperMini дешевле всего, ~43 µA, но версии с WS2812 потребляют 0.6–1.5 mA, и у SuperMini, по-видимому, нет зарядника.

### Cited Findings
- **XIAO nRF52840:** ~5 µA в deep sleep (System OFF), standby <5 µA, зарядник BQ25101 50/100 mA, 3.3 V @200 mA. — [RobotShop](https://jp.robotshop.com/en/products/seeed-studio-xiao-nrf52840); ~5 µA в System OFF, регулятор ~150 mA (противоречит 200 mA) — [ShillehTek manual](https://shillehtek.com/blogs/shillehtek-product-manuals/xiao-seeed-nrf52840-pre-soldered-with-usb-c-cable-manual)
- **XIAO nRF52840, реальные проблемы.** Пользователь с PPK2 видел ~10 mA во сне; в ответах это объяснили цепями зарядника и регулятора на BAT. После установки P-канальных MOSFET сон снизился до 2.4 µA. С LED и BLE ~390 µA. Тот же код: ~600 µA на XIAO против ~7 µA на nRF52840 DK. — [Seeed forum: 10mA in sleep](https://forum.seeedstudio.com/t/xiao-nrf52840-10ma-power-usage-when-in-sleep/295403); [Seeed forum: Deep Sleep vs Light Sleep](https://forum.seeedstudio.com/t/sleep-current-of-xiao-nrf52840-deep-sleep-vs-light-sleep/271841?page=2)
- **XIAO nRF52840 Sense:** LSM6DS3TR-C, PDM-микрофон MSM261D3526H1CPM, nRF52840 Cortex-M4F 64 MHz, 256 KB RAM, 1 MB flash на чипе + 2 MB QSPI flash. — [Seeed wiki](https://wiki.seeedstudio.com/XIAO_BLE/); [Zephyr board doc](https://docs.zephyrproject.org/latest/boards/seeed/xiao_ble/doc/index.html). Pimoroni больше не продаёт Sense. — [Pimoroni](https://shop.pimoroni.com/products/seeed-xiao-ble-nrf52840-sense)
- **Цены XIAO:** XIAO nRF52840 у TME ~10.6–11 USD за 1 шт.; XIAO ESP32S3 Sense ~21–22 USD. — [TME Seeed catalog](https://www.tme.com/us/en-us/katalog/p,seeed-studio_1325/). Switch Science: XIAO nRF52840 Sense ¥3,069, XIAO ESP32C3 ¥1,122 (с налогом). — [Switch Science XIAO](https://www.switch-science.com/collections/all/cat-%E3%83%96%E3%83%A9%E3%83%B3%E3%83%89%E5%B0%8F%E5%88%86%E9%A1%9E_xiao?page=2)
- **XIAO nRF54L15 / Sense:** System OFF 0.6 µA, с GRTC-пробуждением 0.8 µA (из таблицы в блоге; к какой колонке относятся цифры, не до конца ясно). Цена при запуске в 2025: $9.90 и $15.99 (Sense). — [Seeed blog 2025-07-30](https://www.seeedstudio.com/blog/2025/07/30/meet-seeed-studio-xiao-nrf54l15-sense-the-ultra-low-power-platform-with-multi-protocol-connectivity/); Pi Hut £9.60 — [The Pi Hut](https://thepihut.com/collections/all/products/seeed-studio-xiao-nrf54l15)
- **XIAO nRF54LM20A (новее):** deep sleep 4.76 µA, Ship Mode 0.33 µA, цена ~$9.90–15.90. — [The Pi Hut XIAO nRF54LM20A Sense](https://thepihut.com/products/seeed-studio-xiao-nrf54lm20a-sense)
- **XIAO ESP32C3:** 44 µA в deep sleep по тест-таблице Seeed (AT-прошивка, RTC от кварца 32.768 kHz). Дистрибьюторы пишут «>44 µA». — [Seeed Low Power Consumption PDF (Core Electronics)](https://core-electronics.com.au/attachments/localcontent/Low_Power_Consumption_1412147b09b.pdf); [Elektor](https://elektor.com/products/seeed-studio-xiao-esp32c3)
- **XIAO ESP32S3:** ~14 µA в deep sleep, зарядник LiPo, ток заряда 50 mA. — [CNX Software](https://www.cnx-software.com/2023/03/22/xiao-esp32s3-tiny-esp32-s3-wifi-4-and-ble-5-0-module-for-iot-ai-robotics/)
- **XIAO ESP32C6:** ~15 µA в deep sleep, зарядка LiPo. — [RIOT OS board doc](https://doc.riot-os.org/boards/seeedstudio-xiao-esp32c6). Qoitech намерил 13.4 µA (S3) и 16.9 µA (C6); light sleep у Seeed завышен. — [Qoitech](https://www.qoitech.com/?p=19961). У Seeed light sleep C6 указан как 3.1 mA, но с активным Wi-Fi. — [esp32.com / сводка](https://esp32.com/viewtopic.php?p=152323)
- **Waveshare ESP32-C6-Zero:** ~667 µA в deep sleep. — [esp32.com](https://esp32.com/viewtopic.php?p=152323)
- **ESP32-C3 SuperMini:** ~43 µA в deep sleep для стандартной версии, ~600 µA – 1.5 mA для Plus (WS2812B). — [mischianti SuperMini Plus](https://mischianti.org/esp32-c3-supermini-plus-v2-high-resolution-pinout-datasheet-and-specs/). На другой плате после удаления WS2812 ток упал до 61 µA; кастомная плата на C3 потребляла ~300 µA. — [esp32.com](https://esp32.com/viewtopic.php?p=106434)
- **SuperMini nRF52840** — клон nice!nano в формате Pro Micro, поддерживается CircuitPython. — [circuitpython.org](https://circuitpython.org/board/supermini_nrf52840). nice!nano v2: Iq снижен на ~70 % (заявление продавца), внешнее питание отключается встроенным MOSFET (LED могут тянуть по 1 mA). — [dailyclack nice!nano v2](https://dailyclack.com/products/nice-nano-v2-0)
- **Adafruit Feather nRF52840:** ~1 mA в deep sleep по жалобе на DevZone. — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/126622/adafruit-feather-nrf52840-high-power-consumption-1ma-in-deep-sleep/559341)
- **Готовая альтернатива с цветным экраном:** Waveshare ESP32-S3-Touch-AMOLED-1.43, см. раздел 2. — [Waveshare](https://www.waveshare.com/product/esp32-s3-touch-amoled-1.43.htm)

### Inferences
- **XIAO nRF52840 Sense — лучший кандидат для сборки (a):** всё нужное для «реакции на мир» (IMU + микрофон), зарядник и BLE на плате размером с ноготь. Минусы: цена выше ESP32, нестабильный по форумам ток сна, IMU и микрофон надо явно выключать.
- **ESP32-C3 SuperMini + модули** — самый дешёвый путь. Но нужен отдельный TP4056/MCP73831-модуль: у SuperMini, по-видимому, нет LiPo-зарядника, в этом раунде не проверено. Также нужно учитывать перезапуск после deep sleep.
- Модули LOLIN C3 mini, Waveshare ESP32-C3-Zero, Adafruit QT Py, SparkFun Pro nRF52840 Mini в этом раунде не измерялись.

### Gaps
- Нет официальных цифр Seeed по XIAO nRF52840 (wiki не открылся). Нет измерений nice!nano и SuperMini nRF52840 в System ON/OFF, LOLIN C3 mini, ESP32-C3-Zero, QT Py ESP32-C3, SparkFun Pro nRF52840 Mini.
- Не подтверждён состав сенсоров XIAO nRF54L15 Sense. По памяти — IMU и микрофон.

---

## 6. Входы/выходы: кнопки, выключатель, светодиод, вибро

### Takeaway
Самый экономичный «выключатель» — soft-power через ship-mode зарядника: у BQ25180 вход в ship mode долгим нажатием и выход по нажатию или подаче USB, у BQ25120A ship <50 nA. Механический ползунковый выключатель в разрыве батареи надёжен, но занимает место. Для «тапа» по лицу проще использовать акселерометр, чем сенсорную площадку.

### Cited Findings
- BQ25180: функция кнопки (/MR). Долгое нажатие (по умолчанию 10 с) переводит в ship mode, короткое или подача VIN выводит из него. Ship mode с кнопкой 3.2 µA, shutdown 15 nA. — [BQ25180 datasheet](https://ti.com/lit/pdf/SLUSE99); [TI E2E](https://e2e.ti.com/support/power-management-group/power-management/f/power-management-forum/1456013/bq25180-bq25180)
- BQ25120A: ship mode <50 nA, load switch/LDO для отключения периферии. — [TI BQ25120A](https://ti.com/product/BQ25120A)
- На nice!nano внешнее питание отключается MOSFET, потому что даже выключенные LED могут тянуть ~1 mA. — [dailyclack](https://dailyclack.com/products/nice-nano-v2-0)
- Платы ESP32-C3 SuperMini Plus с WS2812 в сне потребляют 0.6–1.5 mA из-за питания контроллера WS2812. — [mischianti](https://mischianti.org/esp32-c3-supermini-plus-v2-high-resolution-pinout-datasheet-and-specs/)
- Внутренние подтяжки IO могут быть включены по умолчанию. Внешняя цепь, тянущая пин в deep sleep, повышает ток. — [ESP-IDF Sleep Modes C3](https://docs.espressif.com/projects/esp-idf/en/release-v4.3/esp32c3/api-reference/system/sleep_modes.html)

### Inferences
- Рекомендуемая конфигурация входов. Одна боковая тактовая кнопка на прерывании MCU: короткое нажатие — смена эмоции, долгое — сон/ship. Акселерометр с tap/double-tap даёт «постучать по лицу». Опционально TTP223 на площадку на корпусе (1.5–2 µA).
- Светодиод не нужен: экран сам служит индикатором. Индикацию заряда можно показывать на OLED. Если LED есть, его нужно питать через ключ.
- Для бокового монтажа подходят SMD-кнопки с боковым нажатием (side-push, 3×4 мм или 4×2 мм). Конкретные модели и цены в этом раунде не искались.

### Gaps
- Не исследованы конкретные боковые тактовые кнопки (TS-xxx side-push, Alps SKRT), мини-выключатели (MSK-12C02), вибромоторы (1027 coin), пьезо: размеры, токи, цены.

---

## 7. Рекомендуемые сборки, ориентировочный BOM и доступность в РФ/СНГ

### Takeaway
(a) **Быстрый прототип:** XIAO nRF52840 Sense + 0.96" SSD1306 I2C + LiPo 402030/502030 + кнопка/выключатель. Около $25–35, без своей платы, IMU и микрофон уже есть. Бюджетный вариант: ESP32-C3 SuperMini/XIAO ESP32-C3 + SSD1306 + LIS3DH/LIS2DH12 + TP4056-модуль, около $10–15. (b) **Оптимизированная своя плата:** nRF52832/52840 (или nRF54L15) + LIS2DW12 + T5838 (опционально) + BQ25180 или MCP73831 + TPS7A02 + TPS22916 для отключения OLED + SSD1306 по SPI + USB-C + 502030. Сон ~5 µA. (c) **Премиум:** либо «всегда видимое лицо» на Sharp Memory LCD с nRF54L15, либо «цветное» на 1.43" AMOLED + ESP32-S3. Второй вариант крупнее и сильно прожорливее.

### Cited Findings (цены и наличие)
- XIAO nRF52840 ~10.6–11 USD (TME). — [TME](https://www.tme.com/us/en-us/katalog/p,seeed-studio_1325/). XIAO nRF52840 Sense ¥3,069, XIAO ESP32C3 ¥1,122 (Switch Science). — [Switch Science](https://www.switch-science.com/collections/all/cat-%E3%83%96%E3%83%A9%E3%83%B3%E3%83%89%E5%B0%8F%E5%88%86%E9%A1%9E_xiao?page=2)
- XIAO nRF54L15 $9.90, Sense $15.99. — [Seeed blog](https://www.seeedstudio.com/blog/2025/07/30/meet-seeed-studio-xiao-nrf54l15-sense-the-ultra-low-power-platform-with-multi-protocol-connectivity/)
- 0.96" SSD1306 ~$4–9 в рознице, ¥22.10 у BuyDisplay. — [microcontrollerslab](https://microcontrollerslab.com/best-oled-displays-for-microcontroller-embedded-projects-buying-guide/); [BuyDisplay](https://www.buydisplay.com/chinese/i2c-white-0-96-inch-oled-display-module-128x64-arduino-raspberry-pi)
- LIS2DH12TR $0.36–0.56 @1k, ~$0.61–0.92 @1 (LCSC). — [LCSC](https://www.lcsc.com/product-image/C110926.html)
- TPS7A02xx $0.30–0.45 @1k, ~$0.58–1.08 @1 (LCSC). — [LCSC](https://lcsc.com/product-detail/image/TPS7A0212PDBVR_C5220172.html)
- Adafruit Sharp Memory 1.3" $24.95. — [Adafruit](https://www.adafruit.com/product/3502)
- Готовый комплект ESP32-C3 + 0.96" OLED: $13.63 (eBay). — [eBay](https://www.ebay.de/itm/297717611810)
- В каталоге chipdip.ru есть BQ25120A и TPS62842, цены в выдаче не показаны. — [chipdip BQ25120A](https://chipdip.ru/product0/8004995045); [chipdip TPS62842](https://www.chipdip.ru/product0/8017659753). CH32V003F4U6 есть в каталоге Compel. — [compel.ru](https://www.compel.ru/infosheet/WCH/CH32V003F4U6)
- Sharp LS011B7DH03: срок поставки на DigiKey 28 недель. — [DigiKey](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS011B7DH03/25953418)

### Inferences (сборки и BOM; цены — оценка автора, если без ссылки)

**(a1) Прототип «nRF-Sense», без своей платы**
- XIAO nRF52840 Sense: MCU + BLE + LSM6DS3TR-C + PDM-микрофон + BQ25101. ~$15–20, по Switch Science ¥3,069.
- SSD1306 0.96" 128×64 I2C, белый или синий. $2–5 на AliExpress (оценка), $4–9 в западной рознице.
- LiPo 402030 (~150–200 мАч) или 502030 (~250 мАч) с JST 1.25 или проводами. $2–4 (оценка, без источника).
- Боковая тактовая кнопка + ползунковый выключатель в разрыве BAT+ (опционально). <$1.
- Корпус на 3D-принтере, кольцо. Итого ≈ **$22–30**.
- Пробуждение: INT1 LSM6DS3TR-C → GPIO nRF52840 (System ON sleep, RAM сохраняется).
- Риски: ток сна по форумам непредсказуем (2.4 µA – 10 mA), IMU и микрофон нужно выключать явно, OLED через 0xAE даёт ~14 µA.

**(a2) Бюджетный прототип «ESP32-C3»**
- ESP32-C3 SuperMini (без RGB) или XIAO ESP32-C3 (с зарядником, ¥1,122).
- SSD1306 0.96" I2C или SPI.
- Модуль LIS3DH или LIS2DH12. Сам чип у LCSC $0.6–0.9, модуль ~$1–3 (оценка).
- Модуль TP4056+DW01 с уменьшенным током заряда (для SuperMini) + LiPo 502030.
- Итого ≈ **$10–15**.
- Плюсы: Wi-Fi (NTP-время, OTA), огромная Arduino-экосистема.
- Минусы: deep sleep = перезапуск (состояние держать в RTC-памяти), сон платы ~43–700 µA, пробуждение только от RTC-GPIO.

**(b) Оптимизированная своя плата (~27×27 мм, двухсторонний монтаж)**
- MCU: nRF52832 или nRF52840 (модуль или чип) либо nRF54L15. System ON ~1.5–3 µA с RAM, BLE.
- Акселерометр: LIS2DW12 (~1 µA, tap/double-tap/wake/6D, LCSC C189624) или ADXL362 (270 nA).
- Микрофон (опционально): TDK T5838 с AAD (~20 µA в режиме ожидания звука). Без микрофона «слушание» можно имитировать по вибрации или звуку через акселерометр.
- Свет: фототранзистор на АЦП через ключ или VEML7700 (0.5 µA в shutdown).
- Зарядка: BQ25180 (I2C, кнопка ship/reset, 3 µA) или MCP73831 (0.15 µA утечки, проще). Вариант «всё в одном»: BQ25120A (зарядка + buck + LDO/ключ, 700 nA).
- 3.3 V: TPS7A02-33 (25 nA) или TPS62840 (60 nA buck, выше КПД при токе OLED ~5 mA).
- Отключение OLED: TPS22916 на VCC модуля или голой панели (10 nA off).
- Дисплей: SSD1306 0.96" по SPI. Либо модуль, либо голая 30-pin FPC-панель с конденсаторами charge pump на своей плате, что даёт минимальную толщину.
- USB-C 16-pin только для питания + 2×5.1 kΩ на CC (+ ESD). LiPo 502030 или 602030.
- Ориентировочная себестоимость компонентов при заказе в JLCPCB/LCSC небольшой партией: **$8–15 на штуку без корпуса**. Оценка автора: точные цены nRF52832/52840, BQ25180, T5838 и FPC-панели не найдены.
- Ожидаемый сон ≈ 3–7 µA. Активный режим с лицом на экране ≈ 4–6 mA (OLED 10–25 % засветки + MCU), то есть ~40–60 ч непрерывной анимации на 250 мАч. Расчёт грубый, детально — в отчёте по энергобюджету.

**(c1) Премиум «всегда на связи» (вечный дисплей)**
- nRF54L15 (XIAO nRF54L15 $9.90 или чип) + Sharp LS011B7DH03 (1.08" 160×68, ~25 µW) или LS013B7DH03 (1.28" 128×128, 2–5 µA) + LIS2DW12 + T5838 + BQ25180.
- Лицо видно всегда без пробуждения экрана, автономность — месяцы.
- Минусы: отражательный экран без свечения (в темноте не виден без фронт-подсветки), 28 недель поставки и высокая цена Sharp, другой визуальный стиль по сравнению с OLED.

**(c2) Премиум «цветное лицо»**
- Waveshare ESP32-S3-Touch-AMOLED-1.43 (466×466, 16.7M цветов, тач, 8 MB PSRAM, разъём LiPo) или GC9A01 1.28" + ESP32-S3.
- Красиво, но это уже «часы на брелоке» (Ø ~35–40 мм) с током порядка десятков mA. На 300–500 мАч это часы работы, а не дни.

**(c3) Премиум на OLED (компромисс)**
- XIAO nRF54L15 Sense или своя плата (b) + SSD1309/SSD1306 по SPI на 60 fps + T5838 + вибромотор через MOSFET для «смеха».
- Плюс USB-C, BQ25180 с soft-power кнопкой и анодированный или полимерный корпус.

**Доступность в РФ/СНГ** (вывод по косвенным признакам)
- Seeed XIAO, SSD1306, LiPo, TP4056-модули и ESP32-C3 SuperMini массово продаются на AliExpress и Ozon.
- TI-чипы (BQ25120A, TPS62842) и прочее есть в каталогах chipdip и compel. LCSC и JLCPCB доставляют в РФ. Конкретных рублёвых цен и сроков на октябрь 2026 найти не удалось.

### Gaps
- Не найдены рублёвые цены на chipdip, Ozon и AliExpress-RU на октябрь 2026, цены XIAO nRF52840 Sense в USD у Seeed, цены LiPo 402030/502030, BQ25180, T5838, nRF52832/nRF52840 (чип/модуль), голой панели SSD1306.
- Не проверены санкционные или логистические ограничения на поставку TI/Nordic в РФ в 2026 году.
- BOM-цены с пометкой «оценка автора» — ориентир, а не данные. Перед публикацией отчёта их нужно сверить с живыми страницами магазинов.
