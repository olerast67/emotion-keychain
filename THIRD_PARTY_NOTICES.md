English | [Русский](#сторонние-компоненты-кратко)

# Third-party notices

This project's own code is under Apache-2.0, its hardware files under CERN-OHL-W-2.0 and its documentation under CC-BY-4.0 ([NOTICE](NOTICE), [REUSE.toml](REUSE.toml)). The firmware files and the web pages built from it also contain or load third-party components. Each one keeps its own licence. This file lists them, with their copyright notices and the licence texts or where to find them.

Licence texts referenced below are in [`LICENSES/`](LICENSES): [MIT](LICENSES/MIT.txt), [BSD-3-Clause](LICENSES/BSD-3-Clause.txt), [LGPL-2.1-or-later](LICENSES/LGPL-2.1-or-later.txt), [Apache-2.0](LICENSES/Apache-2.0.txt), [Nordic licence](LICENSES/LicenseRef-Nordic-5-Clause.txt), [Arm object code licence](LICENSES/LicenseRef-Arm-Object-Code-1.0.txt).

**Source code.** The complete source of this project, including the exact build configuration of every binary in [`firmware/release/`](firmware/release), is public at <https://github.com/olerast67/emotion-keychain>. Versions of the third-party packages are pinned in [`firmware/platformio.ini`](firmware/platformio.ini).

## Rev B keychain firmware (nRF52840)

Files: `firmware/release/keychain-revb-factory.hex`, `keychain-revb.uf2`, `keychain-revb-dfu.zip`, and any `pio run -e keychain` build. Built with PlatformIO `nordicnrf52@11.0.0`, which brings the Adafruit nRF52 Arduino core 1.7.0.

| Component | Version | Licence | Copyright | Upstream |
|---|---|---|---|---|
| Adafruit nRF52 Arduino core (core, `Wire`, `SPI`, `PDM`) | 1.7.0 | LGPL-2.1-or-later | © 2015 Arduino LLC, © 2016 Sandeep Mistry, © 2017 Adafruit Industries | <https://github.com/adafruit/Adafruit_nRF52_Arduino> |
| Bluefruit52Lib (BLE) | 0.22.0 | MIT | © 2016 Adafruit Industries | same repository, `libraries/Bluefruit52Lib` |
| Adafruit TinyUSB Arduino (USB serial) | 3.7.0 | MIT | © 2019 Ha Thach (tinyusb.org) for Adafruit Industries | <https://github.com/adafruit/Adafruit_TinyUSB_Arduino> |
| Adafruit LittleFS, InternalFileSystem | 0.11.0 | MIT | © 2019 Ha Thach for Adafruit Industries | core repository, `libraries/` |
| littlefs | 1.7 | BSD-3-Clause | © 2017 Arm Limited | <https://github.com/littlefs-project/littlefs> |
| Adafruit nRFCrypto | 0.1.2 | MIT | © 2020 Adafruit Industries | core repository, `libraries/Adafruit_nRFCrypto` |
| nrf_cc310 (CryptoCell library, object code) | 0.9.13 | Arm Object Code and Header Files Licence 1.0 (below) | © Arm Limited | shipped inside Adafruit nRFCrypto |
| FreeRTOS kernel | V10.0.1 | MIT | © Amazon.com, Inc. or its affiliates | <https://www.freertos.org> |
| nrfx (Nordic drivers and MDK) | from the core | BSD-3-Clause | © 2017–2020 Nordic Semiconductor ASA | <https://github.com/NordicSemiconductor/nrfx> |
| CMSIS-Core | 5.7.0 | Apache-2.0 | © 2009–2020 Arm Limited | <https://github.com/ARM-software/CMSIS_5> |
| Nordic SoftDevice S140 (BLE stack, binary) | 6.1.1 | Nordic licence (below) | © 2007–2018 Nordic Semiconductor ASA | <https://www.nordicsemi.com/Products/Development-software/S140> |
| Adafruit nRF52 bootloader (in the factory hex) | 0.11.0, see below | MIT | see below | see below |
| GCC runtime and newlib (from the Arm GNU toolchain) | 12.3 | GPL-3.0 with the GCC Runtime Library Exception; newlib — BSD-style licences | © Free Software Foundation and others | <https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads> |

### LGPL-2.1: getting the source and relinking

The Adafruit nRF52 Arduino core is linked statically into the firmware. Section 6 of the LGPL-2.1 lets you replace it with a modified version and relink. Everything you need is public:

1. The core source: <https://github.com/adafruit/Adafruit_nRF52_Arduino/tree/1.7.0> (PlatformIO package `framework-arduinoadafruitnrf52` 1.10700.0).
2. The complete source of this firmware and its build recipe: this repository, `firmware/` and `engine/` ([build instructions](firmware/README.md#плата-rev-b)).
3. To relink with a changed core: point `platform_packages` in `firmware/platformio.ini` at your copy of the core (for example `framework-arduinoadafruitnrf52 @ symlink:///path/to/your/core`), then run `cd firmware && pio run -e keychain`. The new `firmware.uf2` installs over USB without a programmer.

The release binaries are offered from the same place as this source, which is how LGPL-2.1 section 6(d) is met. If you hand out or sell keychains with this firmware, give the recipients this repository link (the [user guide](docs/user-guide.md#открытый-код) has it), so that they can get the same source.

## Bootloader (nRF52840)

File: `firmware/bootloader/emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex` (also merged into `keychain-revb-factory.hex`). It is the [Adafruit nRF52 bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) 0.11.0 (commit `c67f0bc`) with this project's board files and patch ([how it is built](firmware/bootloader/README.md)), merged with the SoftDevice S140 6.1.1 and the Nordic MBR.

| Component | Licence | Copyright |
|---|---|---|
| Adafruit nRF52 bootloader | MIT | © 2016–2024 Adafruit Industries |
| TinyUSB | MIT | © 2018 hathach (tinyusb.org) |
| Microsoft UF2 | MIT | © Microsoft Corporation |
| nrfx | BSD-3-Clause | © Nordic Semiconductor ASA |
| Nordic nRF5 SDK 11 bootloader/DFU modules | Nordic Semiconductor standard software licence (free, non-transferable use; the notice must be kept) | © 2013 Nordic Semiconductor ASA |
| SoftDevice S140 6.1.1, MBR 2.4.1 | Nordic licence (below) | © 2007–2019 Nordic Semiconductor ASA |

## ESP32 test stand firmware

Files: `firmware/release/stand-esp32c5.bin`, `stand-esp32dev.bin`, `stand-esp32c3.bin`, `stand-esp32s3.bin`, and any `pio run -e esp32*` build. Built with the pioarduino platform 55.03.312.

| Component | Version | Licence | Upstream |
|---|---|---|---|
| Arduino-ESP32 core (`Wire`, `Preferences` and, when used, the BLE library) | 3.3.x | LGPL-2.1-or-later | <https://github.com/espressif/arduino-esp32> |
| ESP-IDF (bootloader, FreeRTOS port, drivers, NimBLE / Bluedroid when BLE is used) | 5.5.x | Apache-2.0, with components under MIT and BSD-3-Clause | <https://github.com/espressif/esp-idf> |
| Espressif precompiled radio and PHY libraries | from ESP-IDF | Apache-2.0 (Espressif) | <https://github.com/espressif/esp32-wifi-lib> |

The LGPL source and relinking notes above apply the same way: the core source is at the Arduino-ESP32 repository (tag matching the platform version), and the full stand source is `firmware/src/main.cpp` and `engine/` in this repository; rebuild with `cd firmware && pio run -e esp32c5`. ESP-IDF component licences are listed in each component's `LICENSE` file in ESP-IDF.

## Web pages (browser simulator and 3D case viewer)

| Component | Licence | How it is used |
|---|---|---|
| three.js r128 and its `OrbitControls` | MIT, © 2010–2021 three.js authors | loaded from cdnjs.cloudflare.com and cdn.jsdelivr.net by `hardware/rev-b/case/viewer.html` and the generated `keychain-3d.html`; not copied into this repository |
| Google Fonts: Unbounded, IBM Plex Sans, IBM Plex Mono, JetBrains Mono, Silkscreen | SIL Open Font License 1.1 | loaded remotely from fonts.googleapis.com by `sim/web/index.html` and the case viewer; not copied into this repository |

The browser simulator itself (`sim/web`) is this project's code compiled to WebAssembly and contains no other third-party code.

## Hardware files

The board in `hardware/rev-b` uses footprints and symbols from the official KiCad libraries, copied into the board and schematic files. They are licensed under CC-BY-SA-4.0 with an exception: designs that use them can be distributed under any licence (<https://www.kicad.org/libraries/license/>). The board's own footprints are in `hardware/rev-b/Keychain.pretty` and are part of this project (CERN-OHL-W-2.0).

## Licence texts that must travel with the binaries

### Nordic Semiconductor licence (SoftDevice S140 6.1.1, MBR)

```
Copyright (c) 2007 - 2018, Nordic Semiconductor ASA
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form, except as embedded into a Nordic
   Semiconductor ASA integrated circuit in a product or a software update for
   such product, must reproduce the above copyright notice, this list of
   conditions and the following disclaimer in the documentation and/or other
   materials provided with the distribution.

3. Neither the name of Nordic Semiconductor ASA nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

4. This software, with or without modification, must only be used with a
   Nordic Semiconductor ASA integrated circuit.

5. Any software provided in binary form under this license must not be reverse
   engineered, decompiled, modified and/or disassembled.

THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

Clause 4 means the SoftDevice, and so the keychain firmware, may only run on Nordic chips. This is the reason the release binaries are not under Apache-2.0 alone.

### Arm Object Code and Header Files Licence 1.0 (nrf_cc310)

```
ARM Object Code and Header Files License
Version 1.0

Redistribution.

Redistribution and use of object code, header files, and
documentation, without modification, are permitted provided that the following
conditions are met:
1) Redistributions must reproduce the above copyright notice and the
   following disclaimer in the documentation and/or other materials
   provided with the distribution.
2) Unless to the extent explicitly permitted by law, no reverse
   engineering, decompilation, or disassembly of is permitted.
3) Redistribution and use is permitted solely for the purpose of
   developing or executing applications that are targeted for use
   on an ARM-based product.

 DISCLAIMER. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 CONTRIBUTORS "AS IS." ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT
 NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, NON-INFRINGEMENT,
 AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

### MIT, BSD-3-Clause, LGPL-2.1, Apache-2.0

The MIT and BSD-3-Clause components above are distributed under the standard texts in [`LICENSES/MIT.txt`](LICENSES/MIT.txt) and [`LICENSES/BSD-3-Clause.txt`](LICENSES/BSD-3-Clause.txt), with the copyright holders named in the tables. The LGPL-2.1 text is in [`LICENSES/LGPL-2.1-or-later.txt`](LICENSES/LGPL-2.1-or-later.txt), the Apache-2.0 text in [`LICENSES/Apache-2.0.txt`](LICENSES/Apache-2.0.txt).

If you ship keychains, include this file (or a link to it) with them, for example on the card in the box: see the [user guide](docs/user-guide.md#открытый-код).

---

[English](#third-party-notices) | Русский

## Сторонние компоненты кратко

Свой код проекта — Apache-2.0, файлы платы и корпуса — CERN-OHL-W-2.0, документация — CC-BY-4.0. В файлах прошивки и на страницах есть чужие компоненты со своими лицензиями; полный список с авторами и версиями — в английской части выше.

- **Прошивка брелока rev B:** ядро Adafruit nRF52 для Arduino (LGPL-2.1), Bluefruit52Lib, Adafruit TinyUSB, LittleFS, nRFCrypto (MIT), littlefs и nrfx (BSD-3-Clause), FreeRTOS (MIT), CMSIS (Apache-2.0), nrf_cc310 (объектный код Arm по лицензии Arm), SoftDevice S140 6.1.1 (лицензия Nordic: только на чипах Nordic, без дизассемблирования).
- **Загрузчик:** загрузчик Adafruit nRF52 0.11.0 (MIT) с TinyUSB и UF2 (MIT), nrfx (BSD-3-Clause), модулями nRF5 SDK 11 (лицензия Nordic) и SoftDevice S140 с MBR (лицензия Nordic).
- **Стенд на ESP32:** Arduino-ESP32 (LGPL-2.1) и ESP-IDF (Apache-2.0, отдельные части — MIT и BSD-3-Clause).
- **Страницы:** three.js r128 (MIT) и шрифты Google Fonts (SIL OFL 1.1) грузятся из сети, в репозитории их копий нет.
- **LGPL-2.1 и пересборка.** Ядро Arduino вкомпоновано в прошивку статически. Исходники ядра — в его репозитории (версия 1.7.0), полный исходный код прошивки и рецепт сборки — в этом репозитории: <https://github.com/olerast67/emotion-keychain>. Чтобы собрать прошивку со своим вариантом ядра, укажите его в `platform_packages` файла `firmware/platformio.ini` и выполните `cd firmware && pio run -e keychain`.
