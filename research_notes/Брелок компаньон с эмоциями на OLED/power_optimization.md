# Оптимизация энергопотребления и времени работы брелка-компаньона с OLED (SSD1306 128x64, LiPo 100–500 мА·ч)

> Notes for the report writer. Language: Russian with English technical terms. Research date: 2026-10-23.
> Method caveat: in this session WebFetch could only open github.com pages; most other sites (espressif.com, nordicsemi.com, hsel.co.uk, bitbank blog, raspberrypi.com, etc.) were unreachable (DNS/proxy), and the web-search budget ran out near the end. Many numbers therefore come from search-engine summaries of those pages, not from my own reading of the full page. Where that is the case it is marked "(по сводке поиска)". Primary sources opened in full: the ESP-IDF docs on GitHub, the NimBLE power_save README, the InfiniTime discussion, the Bangle.js 2 docs on GitHub, the jorne wiki table (a copy of Bitbank's data), and the ThingPulse and Adafruit GitHub issues.
> Source ages: hsel.co.uk is from 2018, Bitbank from 2019, the ThingPulse issue from 2018–2019, the Adafruit issue from 2017, robdobson from 2023-11, the STM32U0 announcement from 2024-03, the InfiniTime discussion from 2022–2023. The ESP-IDF docs are master as of 2026, and the Bangle.js docs are current.

## 1. Энергопотребление OLED SSD1306 / SH1106 / SSD1315: пиксели, контраст, charge pump, sleep, VCOMH/precharge, модуль

### Takeaway
A 0.96" SSD1306 module showing a face with about 10% of pixels lit draws roughly **1.5–2.6 mA** (depending on contrast). That is about 8–14× less than full white at maximum contrast (about 20 mA). A "black but switched on" screen still draws **0.47–1.4 mA**, because the charge pump and panel scanning keep running. After `0xAE` (+ `0x8D,0x10`) the module drops to **about 14–26 µA**. On an always-on device the OLED is usually the largest consumer, so the main levers are fewer lit pixels, lower contrast/VCOMH and switching the panel off.

### Cited Findings
**Current vs lit pixels and contrast (0.96" 128x64, 3.3 V)**
- Bitbank table (multimeter between the display's Vcc and a 3.3 V supply, ±10%) for the 128x64 0.96" module: Display Off **26 µA**; On but black **469 µA**; contrast 31 / 50% pixels **5.6 mA**; contrast 127 / 50% **8.1 mA**; contrast 255 / 50% **11.0 mA**; contrast 31 / 100% **10.3 mA**; contrast 127 / 100% **15.1 mA**; contrast 255 / 100% **20.7 mA** — [jorne wiki, copy of Bitbank's table](https://github.com/joric/jorne/wiki/Displays); original: [Bitbank, "How much current do OLED displays use?" (2019)](https://bitbanksoftware.blogspot.com/2019/06/how-much-current-do-oled-displays-use.html) (по сводке поиска: method "multimeter probes between Vcc of the display and a 3.3V power source", accuracy ±10%).
- The same table for the 1.3" 128x64 (usually SH1106): Off **490 µA**, On black 490 µA, contrast 127 / 50% **26.1 mA**, contrast 255 / 100% **39.1 mA**. For the 0.91" 128x32: Off 4–6 µA, contrast 255 / 100% 36–38.7 mA. For the 0.49" 64x32: contrast 255 / 100% **5.17 mA** — [jorne wiki / Bitbank](https://github.com/joric/jorne/wiki/Displays).
- hsel.co.uk (2018, shunt resistor, STM32F0 Discovery, static image with no bus traffic): current is **linear in % fill**. I(mA) = 0.054124·Fill% + 1.444 at contrast 0x7F, and I(mA) = 0.047538·Fill% + 1.397 at 0x01. Quiescent current is almost the same at both contrast values, and the panel clock frequency affects current less than fill does — [hsel.co.uk](https://hsel.co.uk/2018/12/04/ssd1306-power-consumption/) (по сводке поиска).
- TTN forum, 9 I2C OLED modules on an MCU-independent Li-Ion supply: about **1.4 mA** with the display on and all pixels off, **8.7 mA** at 50%, **11.4 mA** at 100%, **13.7 µA** with the display off. The display library made no difference to consumption — [TTN forum, Big ESP32+SX127x topic part 2 #646](https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-2/11973/646) (по сводке поиска).
- Lasse (nerdcamp): at maximum contrast and 13.65% of pixels lit, measurements were taken across settings. A **fully black screen draws 0.4–1.7 mA depending on the initialization settings** — [lasse.nerdcamp.net](http://lasse.nerdcamp.net/static/ssd1306-oled-current-consumption/index.html) (по сводке поиска).
- Vendor figures: about 20 mA "depending on active pixels" — [Soldered](https://soldered.com/products/display-oled-i2c-white-0-96-ssd1306); Adafruit gives about 20 mA on average from 3.3 V, depending on how much of the screen is lit — [Adafruit Learn: Power Requirements](https://learn.adafruit.com/monochrome-oled-breakouts/power-requirements); Waveshare FAQ: about 25 mA fully lit and 1.5 mA "fully off" — [Waveshare 0.96inch OLED (A)](https://www.waveshare.com/wiki/0.96inch_OLED_(A)) (по сводке поиска); a DFRobot 0.91" module: about 25 mA fully lit and about 2 mA "screen off" — (по сводке поиска, [robu.in listing](https://robu.in/product/dfrobot-fermion-monochrome-0-91-128x32-i2c-oled-display-breakout)).
- Crystalfontz: a black-and-white checkerboard draws 50% less than a fully white screen, so current is proportional to lit pixels — [Crystalfontz Low Power Displays](https://www.crystalfontz.com/products/low-power-lcds-oleds-and-epaper.php) (по сводке поиска).
- **Conflict:** hsel found contrast barely matters (0x01 vs 0x7F at 100%: about 6.2 vs 6.9 mA by the formula). Bitbank found contrast 31 vs 255 at 100% gives 10.3 vs 20.7 mA, a 2× difference. The likely causes are different panels, VCOMH/precharge settings and measurement methods (see the VCOMH/precharge findings below).

**SSD1306 controller (datasheet)**
- VDD = 1.65–3.3 V (logic), VCC = 7–15 V (panel drive). Segment max source current is 100 µA, common max sink current is 15 mA, and contrast control has 256 steps — [SSD1306 datasheet (Adafruit copy)](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf) (по сводке поиска).
- Datasheet of the DD-9616BE-2A module (Farnell): sleep current IDD,SLEEP and ICC,SLEEP **1–5 µA**. Operating current is specified at VDD = 2.8 V, VCC = 7.25 V, 50% of the area on — [Farnell DD-9616BE-2A](https://www.farnell.com/datasheets/609753.pdf) (по сводке поиска).

**Display off / charge pump / leaks through the lines**
- `0xAE` = display off (the controller logic stays powered), `0xAF` = on. The charge pump is disabled with `0x8D, 0x10` (enabled with `0x8D, 0x14`) — [Adafruit forum: Safely sleeping a SSD1306](https://forums.adafruit.com/viewtopic.php?t=45520) (по сводке поиска).
- Adafruit forum: with the display off and the MCU asleep, **about 650 µA flowed through the Data line**. After adding a transistor that cut that path, the OLED board's draw in sleep was "0 µA" — [Adafruit forum t=41332](https://forums.adafruit.com/viewtopic.php?t=41332) (по сводке поиска).
- Arduino forum: on the 3.3 V version without a regulator, deep-sleep current is "<1 µA" — [Arduino forum: SSD1306 reduce power consumption](https://forum.arduino.cc/t/ssd1306-reduce-power-consumption/554925) (по сводке поиска).

**VCOMH (0xDB) and precharge (0xD9): dimming below what contrast alone gives**
- Datasheet VCOMH levels: `0x00` ≈ 0.65·VCC, `0x20` ≈ 0.77·VCC (reset), `0x30` ≈ 0.83·VCC. The `0x40` common in drivers (u8glib and others) is undocumented. One tester (2 SPI and 4 I2C modules) found 0x40 gives maximum brightness and values up to 0x70 give no further gain — [Adafruit_SSD1306 issue #42](https://github.com/adafruit/Adafruit_SSD1306/issues/42).
- Lowering VCOMH "makes the display much dimmer at low contrast settings without affecting the brightest settings". The useful range is 0x00–0x40 — [lasse.nerdcamp.net](http://lasse.nerdcamp.net/static/ssd1306-oled-current-consumption/index.html) (по сводке поиска).
- ThingPulse esp8266-oled-ssd1306: "any setContrast 0–255 value barely changes the visible brightness". The proposed fix is `setContrast(contrast, precharge=0xF1, comdetect=0x40)` with very dim settings `setContrast(10, 5, 0)`, i.e. precharge 1–0x1F and VCOMH 0. No measured current is given — [ThingPulse issue #134](https://github.com/ThingPulse/esp8266-oled-ssd1306/issues/134).
- A short precharge means pixels do not reach full brightness, but it saves some energy — (по сводке поиска; [Adafruit forum t=38569](https://forums.adafruit.com/viewtopic.php?t=38569) / [iotexpert](https://iotexpert.com/debugging-ssd1306-display-problems/)).

**The module's onboard LDO (662K = XC6206P332MR)**
- XC6206: Iq ≈ **1 µA** (typ) — [Torex XC6206](https://product.torexsemi.com/en/series/xc6206); [Torex USA](https://www.torex-usa.com/products/voltage-regulators/low-iq/xc6206/).
- Modules are sold as "3.3–5.5 V" — [Newegg listing](https://www.newegg.com/p/3C6-079E-000J2) (по сводке поиска).

### Inferences
- **A typical face (5–15% of pixels lit)**, interpolating linearly in Bitbank's table (0.469 mA black → 50% value):
  - contrast 31: 5% ≈ 1.0 mA, 10% ≈ **1.5 mA**, 15% ≈ 2.0 mA;
  - contrast 127: 10% ≈ **2.0 mA** (matches the hsel formula: 0.054·10+1.44 = 1.98 mA);
  - contrast 255: 10% ≈ **2.6 mA**.
  - For comparison, full white is 6.9–20.7 mA depending on the source. **Face vs full white is about 8–14×.**
- **"Black but on" (0.47–1.4 mA) is a floor that pixels and contrast cannot remove.** It covers the charge pump, the oscillator and the COM/SEG scan. Even an "empty" sleeping face at 1 fps therefore costs about 0.8–1.6 mA, while `0xAE` + `0x8D 0x10` costs about 14–26 µA. That is about 30–100× less.
- A static image costs the MCU nothing: the SSD1306 refreshes the panel from its own GDDRAM. The hsel measurements were made with "buffer filled, no further data". Frames that do not change do not need to be sent.
- Because the floor is fixed, dimming with VCOMH 0x00 + low precharge + low contrast mainly reduces the "per-pixel" part of the current. On a dark face with 10% fill, the expected saving is around 0.5–1 mA out of 1.5–2.6 mA. **This needs measuring on your own panel; no measured "contrast+VCOMH+precharge" combination with current figures was found.**
- Vendor figures of "1.5–2 mA when off" most likely mean the "on, black screen" state, not `0xAE` with the charge pump off.
- The 1.3" SH1106 module draws about 2–3× more than the 0.96" and has 490 µA in "off" (by Bitbank). For a keychain, the 0.96" SSD1306 is preferable, or the module's sleep current must be checked.
- The XC6206 LDO itself (≈1 µA) is not worth removing for Iq. The reasons to bypass it are dropout at a low LiPo voltage and the option to power the OLED from a switched 3.3 V rail. The real leaks are the I2C pull-ups and back-powering through SDA/SCL (650 µA in the example above). At 4.7 kΩ to 3.3 V, one line held low draws 3.3/4.7k ≈ 0.7 mA (calculation).

### Gaps
- No exact SSD1306 chip datasheet values for IDD/ICC sleep (the "10 µA" figure is often quoted, but the official table was not confirmed). The SSD1306 PDF could not be opened.
- No current-vs-mode numbers for the SSD1315 or SH1106 in their datasheets.
- No measurements of how the panel oscillator frequency and frame rate (`0xD5`) affect current. hsel only says the effect is "smaller than fill".
- No quantitative "current vs precharge/VCOMH" measurements.

## 2. Режимы питания МК и сон между кадрами (ESP32-C3/C6/S3, nRF52840, RP2040, STM32L4/U0); fps, частота I2C, частичные обновления

### Takeaway
Pushing a full frame over I2C at 400 kHz takes about **23 ms** (1024 B × 9 bit × 2.5 µs). At 20 fps that keeps the MCU awake about half the time. On an ESP32 that means about 15–20 mA, because the I2C driver holds the APB lock and light sleep is impossible during the transfer. **Partial updates** (only the eye region), a **low frame rate**, and sleeping between frames bring the ESP32-C3 down to about 1.5 mA on average. An nRF52840 or STM32U0 (µA-level sleep, 52 µA/MHz) brings the MCU share down to about 0.1–0.2 mA, at which point the OLED dominates. The RP2040 is the worst fit: dormant ≈ 0.18 mA on the chip and ≈ 0.8 mA on a Pico board.

### Cited Findings
**ESP32-C3**
- Datasheet / Espressif book: Modem-sleep with the CPU running is **20 mA @160 MHz** and **15 mA @80 MHz**. Light-sleep **130 µA**, Deep-sleep **5 µA** — [ESP32-C3 Wireless Adventure, 12.2.4](https://espressif.github.io/esp32-c3-book-en/chapter_12/12.2/12.2.4.html); deep sleep 5 µA also in [ESP32-C3 datasheet v2.4](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf) (по сводке поиска).
- Another datasheet revision: Modem-sleep @160 MHz **23 mA** (peripheral clocks off) / 28 mA (on); @80 MHz **17 / 22 mA** — [ESP32-C3 datasheet, Mouser copy](https://www.mouser.com/datasheet/2/744/Seed_113991054-3159716.pdf) (по сводке поиска).
- Measured (Qoitech, XIAO ESP32-C3): light-sleep **193 µA**, modem-sleep **17.0 mA**. Seeed's board spec gives 4 mA for light sleep, and the validated measurements came out 3–21× lower than the board spec — [Qoitech](https://www.qoitech.com/blog/esp32-s3-c3-c6-sleep-power-consumption/) (по сводке поиска).
- Rob Dobson (2023-11): calling light sleep manually from the CPU loop gave **2.2 mA** on average with periodic spikes — [robdobson.com](https://robdobson.com/2023/11/investigating-esp32-c3-power-management/) (по сводке поиска).
- NimBLE power_save README (ESP-IDF master), ESP32-C3: modem sleep **12 mA**, light sleep with main XTAL **2.3 mA**, light sleep with 32 kHz XTAL **140 µA**. Max current 262 mA — [esp-idf power_save README](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md).

**ESP32-C6 / S3 (same README and datasheets)**
- C6: modem sleep 22 mA, light sleep main XTAL 3.3 mA, light sleep 32k XTAL **34 µA**. S3: modem sleep 17.9 mA, light sleep main XTAL 3.3 mA, **230 µA** with 32k. H2: 24 µA. C2: 169 µA — [esp-idf power_save README](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md).
- C6 datasheet: light sleep **180 µA** (35 µA with peripherals powered down), deep sleep **7 µA** — [ESP32-C6 datasheet v1.5](https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf) (по сводке поиска). Measured on a XIAO ESP32-C6: light-sleep about 220 µA, deep-sleep about 16.9 µA — (по сводке поиска; [Seeed XIAO ESP32C6 wiki](https://wiki.seeedstudio.com/xiao_esp32c6_getting_started/) / [Qoitech](https://www.qoitech.com/blog/esp32-s3-c3-c6-sleep-power-consumption/); I could not confirm which of the two pages it came from).
- C6 LP core ≈ 245 µA @20 MHz, a third-party estimate, unverified — [blog.cyril.by](https://blog.cyril.by/en/free-speech/esp32-c6-power-consumption) (по сводке поиска).
- C6 bug: on an ESP32-C6-Mini-1 devkit, deep sleep drew >1 mA instead of 7 µA. The cause was a pending interrupt that prevented the LP core from entering sleep. It was fixed in ESP-IDF master/v5.1/v5.2 (12.2023) — [esp-idf issue #12651](https://github.com/espressif/esp-idf/issues/12651).
- S3: deep sleep **7 µA**, light sleep **240 µA** (datasheet), modem sleep about 27.6 mA (Qoitech data at 160 MHz, CPU idle). On a XIAO S3, light sleep measured **2.88 mA**, because ESP-IDF keeps the external flash powered by default. Third-party figures put 240 MHz at about 47.6 mA idle and 65.9 mA under load — [Qoitech](https://www.qoitech.com/blog/esp32-s3-c3-c6-sleep-power-consumption/); [ESP32-S3 datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf); [luisllamas](https://www.luisllamas.es/en/esp32-power-consumption/) (по сводке поиска).

**ESP-IDF automatic light sleep: mechanics and pitfalls** (official docs, master)
- It needs `CONFIG_PM_ENABLE` + `CONFIG_FREERTOS_USE_TICKLESS_IDLE`; without tickless, `esp_pm_configure()` returns `ESP_ERR_NOT_SUPPORTED`. The chip sleeps only when no locks are held, and the sleep length is set by the nearest FreeRTOS timeout or `esp_timer` event — [ESP-IDF Power Management](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst).
- **The SPI master, I2C, SDMMC and I2S drivers hold `ESP_PM_APB_FREQ_MAX` during a transaction.** I2S with APLL holds `ESP_PM_NO_LIGHT_SLEEP`. Bluetooth usually holds `NO_LIGHT_SLEEP` too — [ESP-IDF Power Management](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst).
- During light sleep, peripherals are clock-gated and GPIO and peripheral interrupts are not generated. Wake-up has to be configured through the sleep wakeup sources. DFS adds interrupt latency from about 0.2 µs to about 40 µs. If the actual wake overhead is larger than predicted, `vTaskStepTick()` may assert (the `CONFIG_PM_LIGHTSLEEP_TICK_OVERFLOW_PROTECTION` option trades this for tick accuracy). With `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`, I2C/LEDC/UART and others are saved and restored through REG_DMA retention, and IOs must be held with `gpio_hold_en` — [ESP-IDF Power Management](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst).
- Recommended auto-light-sleep configuration for C3/C6/S3: tick 1000 Hz, idle-before-sleep = 3 ticks, RTC clock "Internal 150 kHz OSC", MAC/BB PD ON, CPU PD ON, Flash PD OFF. ESP32-C6 needs `CONFIG_ESP_PHY_MAC_BB_PD` for light sleep to work correctly. Auto light sleep is "for cases where real-time response to external events is not required" — [ESP-IDF low-power-mode-soc](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-soc.rst); [power_save README](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md).
- Flash stays powered in light sleep by default (SPI flash standby is about 10–30 µA, Deep Power-Down <1 µA). Powering flash off is not recommended, since a full power-down can take more than 300 ms. The recommended alternative is `CONFIG_ESP_SLEEP_SET_FLASH_DPD`. A GPIO wake pulse must last at least 3 RTC slow-clock cycles (about 7.4 µs each on RC_SLOW, about 30.5 µs on XTAL32K). In deep sleep the internal pull-ups stay on (use `rtc_gpio_isolate`), and the CPU context is lost on wake (the bootloader runs) — [ESP-IDF Sleep Modes](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/sleep_modes.rst); [low-power-mode-soc](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-soc.rst).
- Auto light sleep often fails to engage because idle time is too short, typically due to excessive logging or continuous scanning — [ESP-IDF low-power-mode-ble FAQ](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-ble.rst).

**nRF52840**
- CPU: **52 µA/MHz** running CoreMark from flash, i.e. about **3.3 mA @64 MHz** (DCDC, 3 V; the "3.3" in the table is CoreMark/MHz, and the current is 52 µA/MHz × 64) — [nRF52840 PS v1.11](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6469/NRF52840-DK.pdf) (по сводке поиска).
- System ON idle with RTC wake: **≈1.5 µA** (no RAM retention). System OFF **≈0.4 µA** — [nRF52840 PS v1.11](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6469/NRF52840-DK.pdf). The SoC brief gives 0.97 µA without retention and **2.35 µA** with full 256 kB retention — [nRF52840 SoC brief v3.0](https://www.mouser.com/datasheet/2/297/nrf52840_soc_v3_0-2942478.pdf); the old brief gives about 1 µA plus about 30 nA per 4 kB retained — [nRF52840 PB v1.0](https://www.mouser.com/pdfdocs/Nordic_nRF52840_PB.pdf) (по сводке поиска).
- Real boards: about 3.5 µA on a PCA10056 DK against a 2.35 µA target (board leakage). On a BMD-340 the floor was 1.9 µA against 0.97 µA in the spec — [DevZone 111812](https://devzone.nordicsemi.com/f/nordic-q-a/111812/nrf52840-system-on-low-power-almost-reached-but-1-a-comes-from-nowhere?pifragment-684=2); [DevZone 78793](https://devzone.nordicsemi.com/f/nordic-q-a/78793/nrf52840-high-current-about-2-5ua-while-idle) (по сводке поиска).

**RP2040**
- Datasheet (section 5.7, по сводке поиска): **Dormant ≈ 0.18 mA**, Sleep ≈ 0.39 mA. Dormant stops all clocks and PLLs, and wake-up comes from a GPIO or an RTC clocked from an external pin. Other sources give 0.8 mA (dormant) and 1.3 mA (sleep) for the Pico board — [Core Electronics forum](https://forum.core-electronics.com.au/t/pi-pico-sleep-dormant-states/12584); [HN discussion](https://news.ycombinator.com/item?id=30815114) (по сводке поиска).
- rp-rs: WFE/WFI with interrupts disabled ≈ **20 mA** on a Pico, timer sleep ≈ 9 mA, early "deep sleep" attempts >8 mA (the clocks were not stopped), BOOTSEL ≈ 14 mA. The RTC must be clocked from an external pin to wake from dormant — [rp-hal issue #659](https://github.com/rp-rs/rp-hal/issues/659).
- Floating inputs and pulls that fight external levels leak current in dormant, so unused GPIOs should be put into a "pulls off, input buffer off, output off" state — [pico_battery_op](https://github.com/elehobica/pico_battery_op).

**STM32**
- STM32U0 (Cortex-M0+, 2024): Run **52 µA/MHz** (LDO), **Stop 2 with RTC 630 nA** (U031) / **825 nA** (U073), Standby with RTC 160 nA, Shutdown 16 nA — [CNX Software](https://www.cnx-software.com/2024/03/21/stmicro-ultra-low-power-stm32u0-mcu-18nm-fd-soi-process-stm32-microcontrollers/); [ST eStore U073](https://estore.st.com/en/stm32u073mbt6-cpn.html) (по сводке поиска).
- STM32L4: Stop 2 ≈ **1.26 µA** @25 °C, 3.6 V — [ST wiki: Getting started with PWR](https://wiki.stmicroelectronics.cn/stm32mcu/wiki/Getting_started_with_PWR) (по сводке поиска). STM32U5 for reference: 18.5–19.5 µA/MHz, Stop 2 4–17.5 µA depending on SRAM — [STM32U5A5 DS](https://www.st.com/resource/en/datasheet/stm32u5a5aj.pdf) (по сводке поиска).

**I2C speed and frame rate**
- A full frame takes 1024 B × 9 bit × 2.5 µs ≈ **23 ms at 400 kHz**, and about 100 ms (≈10 fps) at 100 kHz. Bitbank measured about **23 fps** at 400 kHz. The SSD1306 is specified only up to 400 kHz, and nobody reliably confirmed that 1 MHz works — [Arduino forum: U8g2 framerate](https://forum.arduino.cc/t/ssd1306-u8g2lib-framerate/522803); [Bitbank: Practical animation on I2C SSD1306](https://bitbanksoftware.blogspot.com/2018/05/practical-animation-on-i2c-ssd1306.html); [qsantos.fr](https://qsantos.fr/2026/01/24/asynchronous-i%C2%B2c-in-c/) (по сводке поиска).

### Inferences
- **Average MCU current model:** I_avg = f·t_awake·I_active + (1 − f·t_awake)·I_sleep, where t_awake = render time + I2C transfer time (on the ESP32 the I2C transfer = awake time because of the APB lock). Assumptions for the examples: render time about 2 ms (not measured, an estimate); ESP32-C3 at 80 MHz ≈ 17 mA; light sleep ≈ 0.19 mA (XIAO measurement).
  - 20 fps, full frame @400 kHz (≈27 ms): duty 54% → **≈9.3 mA**;
  - 10 fps, full frame @400 kHz: duty 27% → **≈4.7 mA**;
  - 10 fps, partial eye region of about 256 B (≈5.8 ms + 2 ms): duty 8% → **≈1.5 mA**;
  - 20 fps, partial @1 MHz (≈2.3 + 2 ms): duty 8.6% → **≈1.6 mA**. Note: 1 MHz is out of spec;
  - 2 fps, partial (a "static" face with rare blinks): duty 1.6% → **≈0.46 mA**;
  - busy loop with no sleep at 160 MHz → **≈20–28 mA** all the time.
- **Conclusion:** at the same 10 fps, dirty-rectangle / partial updates cut the ESP32-C3's MCU share about 3× (4.7 → 1.5 mA). Combined with dropping from 20 to 10 fps, the cut is about 6× (9.3 → 1.5 mA). Going from 400 kHz to 1 MHz gives about 2.5× on transfer time, but it is a risk, and partial updates give the same or more within spec.
- On the nRF52840 (TWIM with EasyDMA lets the CPU sleep during the transfer, from general knowledge of the architecture), 10 fps partial: render 2 ms × 10 × 3.3 mA ≈ 0.07 mA, plus the transfer (current with HFCLK+TWIM running is unknown; at an assumed ~1 mA that adds about 0.06 mA), plus 2 µA idle → **≈0.1–0.2 mA**. The STM32U0 at 16 MHz (≈0.83 mA running) would be in the same order of magnitude.
- The RP2040 has no cheap "light sleep with RAM and a fast wake" between frames: sleep/dormant is 0.18–0.39 mA on the chip and 0.8–1.3 mA on a Pico, and running with WFI is about 20 mA. It will never win on battery life for this device.
- The ESP32-S3 makes sense only if PSRAM or a large display is needed. Its light sleep on real boards can be mA-level because of flash/PSRAM, and its active current is higher.

### Gaps
- No measured ESP32 light-sleep entry/exit time (the ESP-IDF docs do not give a number) and no current during the transition. At 10–20 wakes per second this could be noticeable.
- No current figure for nRF52840 TWIM/EasyDMA and HFCLK during an I2C transfer.
- No measured render time for a face frame on each MCU (the 2 ms figure is an estimate).
- The STM32U0/L4 I2C+DMA path cannot work in Stop 2, so the MCU stays in Sleep/Run during the transfer. No current figures were found for that.
- No datasheet values for RP2040 active current vs clock speed (e.g. 125 vs 48 MHz).

## 3. Стоимость радио: BLE vs Wi-Fi; радио выключено по умолчанию

### Takeaway
On the nRF52840, BLE advertising costs **≈6 µA at a 2 s interval** and about 30 µA at 200 ms. That is negligible next to the OLED. On the ESP32-C3, an untuned BLE setup costs **tens of mA** (35–42 mA when advertising with modem sleep). Getting down to hundreds of µA requires auto light sleep, a 32 kHz crystal and the right sdkconfig. Wi-Fi peaks at 240–262 mA. On an ESP32 the radio should be off by default and switched on briefly, on demand.

### Cited Findings
- nRF52840 advertising averages: **164 µA @30 ms**, **30.5 µA @200 ms**, **5.9 µA @2000 ms** (from Nordic's nRF52840 vs nRF54L comparison table) — [Nordic Developer Academy, nRF54L express course](https://academy.nordicsemi.com/courses/nrf54l-series-express-course/lessons/lesson-1-architecture-overview/topic/processing-performance-and-power-consumption/) (по сводке поиска; check the conditions).
- PineTime (nRF52832, InfiniTime 1.12 → dev branch): BLE fast advertising 1.5 mA → **820 µA**, slow advertising 890 → **280 µA**, BLE connected 1.3 mA → **380 µA**, BLE off 860 → **250 µA**. These are whole-watch figures in sleep — [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420).
- Bangle.js 2: BLE connected in high-bandwidth mode **0.75 mA** for the whole watch (vs 0.3 mA idle at 12.5 Hz accelerometer polling) — [Bangle.js2 docs](https://github.com/espruino/EspruinoDocs/blob/master/boards/Bangle.js2.md).
- ESP32-C3 + NimBLE (Rob Dobson, 2023): advertising with modem sleep at a 1 s interval **35.0 mA** average, 42.2 mA at the default interval, **97.2 mA** without modem sleep — [robdobson.com](https://robdobson.com/2023/11/investigating-esp32-c3-power-management/) (по сводке поиска).
- Espressif's NimBLE power_save example: the C3 light-sleep floor with a 32 kHz XTAL is **140 µA**, and with the main XTAL 2.3 mA. The S3 is 230 µA / 3.3 mA, the C6 34 µA / 3.3 mA. Bluetooth modem sleep and "Power down MAC and baseband" must be enabled — [power_save README](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md).
- ESP BLE low power: the main XTAL stays on in light sleep and raises current. **If the 32 kHz crystal is not detected at BLE init, the system silently falls back to the main XTAL** (check the log line "Using main XTAL as clock source"). On C3/S3 the 136 kHz RC is good only for legacy advertising/scanning, not for connections. Sleep clock accuracy must be ≤500 ppm — [ESP-IDF low-power-mode-ble](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-ble.rst).
- Unverified claims of "about 60 µA for ESP32-C3 advertising at 1 s" and "about 0.8 mA light-sleep advertising" do not match the bench measurements — [Hubble comparison](https://hubble.com/community/comparisons/stm32wba-vs-esp32-for-ble-which-low-cost-mcu-fits-your-product/); [Hubble BLE guide](https://hubble.com/community/guides/esp32-power-consumption-in-ble-mode-what-to-expect-from-advertising-scanning-and-connected-states/) (по сводке поиска; low reliability).
- ESP32 peak current is about 240 mA when transmitting over Wi-Fi — (по сводке поиска; [JLCPCB blog](https://jlcpcb.com/blog/esp32-power-consumption-explained)). The C3 "max current" is 262 mA — [power_save README](https://github.com/espressif/esp-idf/blob/master/examples/bluetooth/nimble/power_save/README.md).
- Watchy (ESP32 + e-paper): 5–7 days when only keeping time, **2–3 days** with Wi-Fi data fetches — [Watchy docs: Battery Life](https://watchy.sqfmi.com/docs/battery-life/) (по сводке поиска).

### Inferences
- nRF52840 + advertising at 1–2 s ≈ +6–15 µA ≈ +0.15–0.35 mA·h/day, i.e. <1% of a 240 mA·h budget per day. The radio can be kept in "always discoverable" mode.
- ESP32-C3: even an ideal 140 µA (light sleep with 32k) is already about the same as the C3's whole sleep floor. With an untuned stack (35 mA), BLE would drain a 240 mA·h cell in about 7 h. Recommendation: radio OFF by default; turn it on with a gesture or button for one sync session (≤ 1–2 min); install a 32.768 kHz crystal if BLE is needed.
- Wi-Fi on a keychain should be used only for OTA or sync, and preferably while charging.

### Gaps
- No primary measurement of ESP32-C3 BLE advertising average current with a properly configured auto light sleep + 32k XTAL at intervals of 1 s / 2 s.
- No nRF52840 connection-interval current figures (only the PineTime system-level numbers).

## 4. Стоимость датчиков: IMU wake-on-motion, микрофон, датчик света

### Takeaway
A hardware wake-on-motion IMU costs **≈0.4–2 µA** (LIS2DW12 / LIS2DH12 / BMA400), which is negligible. Polling the accelerometer from the MCU costs much more, because of the MCU wake-ups. "Always listening" through an MCU (PDM/I2S) on an ESP32 means the MCU never sleeps (mA). A mic with built-in acoustic activity detection (Vesper VM3011, ZPL) costs **≈10 µA** and wakes the MCU with a GPIO edge.

### Cited Findings
- BMA400: sleep ≈ **200 nA**. Low-power mode at a fixed 25 Hz ≈ **850 nA** (800–1200 nA depending on osr_lp). The wake-up interrupt works from low-power mode — [BMA400 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bma400-ds000.pdf) (по сводке поиска).
- LIS2DH12: "ultra-low power consumption down to 2 µA", ODR 1 Hz–5.3 kHz, wake-up interrupt on two pins — [LIS2DH12 datasheet](https://www.st.com/resource/en/datasheet/lis2dh12.pdf). ST design tip DT0098: in wake-up configuration the LIS2DH12 draws **2 µA @2.5 V** and the LIS2DW12 **0.38 µA @1.8 V** — [ST DT0098](https://www.st.com/resource/en/design_tip/dt0098-setting-up-wakeup-recognition-and-absoluterelative-references-with-sts-mems-accelerometers-stmicroelectronics.pdf) (по сводке поиска).
- Bangle.js 2 (the MCU polls the accelerometer): idle **0.3 mA** at 12.5 Hz vs **0.15 mA** at 1.25 Hz (the default after about 120 s without movement). Whole-watch figures — [Bangle.js2 docs](https://github.com/espruino/EspruinoDocs/blob/master/boards/Bangle.js2.md).
- Vesper VM3011 (piezo MEMS, PDM): ZeroPower Listening (no clock) **≈10 µA**. PDM <1 MHz ≈ 400 µA, PDM 1 MHz ≈ **700 µA**. On wake-on-sound DOUT gives a "threshold exceeded" flag, and the threshold is adaptive at 6–18 dB above background (set over I2C). Reaction time about 200 µs. Vesper claims ">90% of the time hibernating, >10× longer battery life". The predecessor VM1010 draws 3 µA in listening mode — [Vesper VM3011](https://vespermems.com/products/vm3011/); [audioXpress](https://audioxpress.com/news/new-vesper-vm3011-piezo-mems-microphone-with-adaptive-zeropower-listening-extends-battery-in-voice-activated-designs); [VM3011 datasheet (SparkFun)](https://cdn.sparkfun.com/assets/learn_tutorials/1/7/9/1/Vesper_Technologies_VM3011_Datasheet_5b5_5d-1893207.pdf) (по сводке поиска; vendor-stated figures).
- On ESP32, I2S with APLL holds `ESP_PM_NO_LIGHT_SLEEP`, and I2S normally holds `ESP_PM_APB_FREQ_MAX` — [ESP-IDF Power Management](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst).

### Inferences
- **Wake-on-motion should be done in hardware**: an IMU INT pin to an MCU GPIO wake pin (on ESP32, `gpio_wakeup_enable` for light sleep or EXT1/RTC GPIO for deep sleep). Bangle shows that halving the polling rate saves ~0.15 mA, which is entirely MCU cost.
- **Always-on mic on ESP32 through I2S/PDM**: the MCU stays awake (~12–20 mA by the modem-sleep figures) plus the mic (~0.4–0.7 mA). That is incompatible with multi-day battery life. Options: (a) VM3011 at ≈10 µA → wake → short PDM capture; (b) an analog mic sampled by the MCU's ADC in bursts (e.g. 50 ms every 1–2 s), with a duty-cycled awake cost; (c) listen only in the "active" state.
- A light sensor (a phototransistor/LDR with a divider powered from a GPIO and measured once every 5–30 s) is negligible if the divider is switched off between measurements (see the divider section).

### Gaps
- No sourced figures for ALS chips (VEML7700, LTR-303, etc.) or for a phototransistor solution.
- No PDM current figures for typical MEMS mics (Knowles SPH0641, etc.) in low-power mode, and no current for the nRF52840 PDM peripheral.
- No figures for analog MEMS mic + op-amp + ADC sampling.

## 5. Стратегии: поведенческие состояния, выключение дисплея, «спящее лицо», адаптивный fps, минимизация I2C

### Takeaway
The biggest win comes from a **state machine**: ACTIVE (10–20 fps, partial updates) → IDLE (1–2 fps, rare blinks, low contrast) → DROWSY ("sleeping face" at minimum contrast with VCOMH 0x00) → SLEEP (`0xAE` + charge pump off, MCU in light/deep sleep, wake on tap/shake/button). The panel holds the image itself, so a static frame costs the MCU nothing. The floor for "screen on" is ≈0.5–1.4 mA, so in the dark or in a bag the panel must be off, not showing black.

### Cited Findings
- Bangle.js 2 automatically lowers its accelerometer polling to 1.25 Hz after about 120 s without movement, which halves idle draw (0.3 → 0.15 mA). A clock updating once a minute gives about a month; once a second gives about 2 weeks — [Bangle.js2 docs](https://github.com/espruino/EspruinoDocs/blob/master/boards/Bangle.js2.md).
- InfiniTime (PineTime) went from 3–5 days to >10 days on firmware 1.13 thanks to power-management optimizations. After the June 2023 optimizations it reached "a bit more than 16 days" — [ncartron.org](https://www.ncartron.org/infinitime-113---improved-heart-rate-algorithm-and-battery-life-and-more.html) (по сводке поиска); [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420).
- PineTime: touching the display raised current from 1.1 mA to 3.6 mA for about 2 s, even with wake options disabled — [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420).
- OLED: black but on = 0.47–1.4 mA; off = 14–26 µA — [jorne wiki/Bitbank](https://github.com/joric/jorne/wiki/Displays); [TTN forum](https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-2/11973/646) (по сводке поиска).
- Static current with no bus traffic was measured with a static image — [hsel.co.uk](https://hsel.co.uk/2018/12/04/ssd1306-power-consumption/) (по сводке поиска).
- Auto light sleep fails to engage when idle time is short or there is too much logging — [ESP-IDF low-power-mode-ble](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-ble.rst).

### Inferences
Recommended states (numbers come from the calculations in sections 1–2 and 8; ESP32-C3 / nRF52840):

| State | Display | fps / updates | Contrast | MCU | Estimated I_avg (C3 / nRF) |
|---|---|---|---|---|---|
| ACTIVE (moved, tap, sound) | on | 10–20 fps, dirty-rect | 0x40–0x7F | auto light sleep between frames | ≈3.5 / ≈2.2 mA |
| IDLE (still for 30–60 s) | on | 1–2 fps, blink every 3–6 s, otherwise static | 0x10–0x20, VCOMH 0x00 | sleep | ≈2.0 / ≈1.55 mA |
| DROWSY (still for 3–5 min) | "sleeping face" ~3% pixels | 0.5–1 fps "Zzz" | minimum + VCOMH 0x00 + short precharge | sleep | ≈1.15 / ≈0.83 mA |
| SLEEP (still >10–15 min, darkness/bag, night) | `0xAE` + `0x8D 0x10` | 0 | — | light sleep (C3) / System ON idle (nRF), wake on IMU INT/button | ≈0.23 / ≈0.045 mA |
| DEEP (storage, battery <10%) | off | 0 | — | deep sleep / System OFF | ≈0.05 / ≈0.045 mA (≈0.02 mA if the OLED power is cut with a load switch) |

- **Darkness = put away (bag or pocket)**: if the light sensor reads dark and the IMU sees no movement → SLEEP right away (no 15-minute wait). If dark but shaking (walking with the keychain in a bag) → still SLEEP, since nobody can see the screen.
- **Night dimming**: in low ambient light the face can be dimmer (lower contrast and VCOMH), which also saves current and reduces glare.
- **Adaptive fps**: in IDLE send only changed regions (eyelids during a blink = 1–2 pages × the eye width). With no changes, do not touch the bus at all. "Breathing" and "eye-shifting" animations can be done by shifting with `0xD3` (display offset) or the start line (`0x40–0x7F`) using 2-byte commands instead of resending the buffer (an idea based on the SSD1306 command set; its power effect was not measured).
- **Do not use `delay()` loops**: on the ESP32 the CPU should block in `vTaskDelayUntil`/`esp_timer` so tickless idle can put the chip into light sleep. Disable logging/UART in production.
- **Wake on tap/shake**: IMU in hardware wake-up (≈1–2 µA); in SLEEP the MCU sleeps until the INT edge. On ESP32 deep sleep, the wake = a reboot with the bootloader. The "waking up" face animation can hide that delay.
- **Avoid "dark but on"**: it costs as much as a dim face (≈0.5–1.4 mA), so in SLEEP the panel must really be off.
- **Burn-in and charm**: this belongs to the animation-design researchers, but power-wise it supports the same thing: fewer lit pixels and shorter time lit.

### Gaps
- No public measurements of a "dim sleeping face" vs `0xAE` on the same module.
- No data on how long the SSD1306 takes to come back from `0xAE` + charge pump off to a visible image. The charge pump may need on the order of ~100 ms (unconfirmed, needs measuring).

## 6. Потери в цепи питания: Iq LDO, зарядник, делитель напряжения, DW01, светодиоды

### Takeaway
On a well-designed board, the static losses (LDO ≈1 µA + DW01 ≈3 µA + TP4056 ≤2–6 µA + a 1 MΩ divider ≈4 µA) total about 10–15 µA. That does not matter for an always-on face but **is 25–35% of the floor in deep sleep**. The real killers are dev boards (LEDs, USB-UART, ordinary LDOs: mA-level), I2C back-powering (hundreds of µA) and low-value dividers (18.5 µA at 200 kΩ).

### Cited Findings
- XC6206 (662K): Iq ≈ 1 µA typ — [Torex](https://product.torexsemi.com/en/series/xc6206).
- DW01A: about 3 µA in standby — [best-microcontroller-projects: DW01A](https://www.best-microcontroller-projects.com/dw01a.html) (по сводке поиска; secondary source, the datasheet was not opened).
- TP4056: battery current in standby (Vbat = 4.2 V) is at most −6 µA; another source says <2 µA after input power is removed; the TP4056H datasheet says <1 µA — [best-microcontroller-projects: TP4056](https://www.best-microcontroller-projects.com/tp4056.html); [watelectronics](https://www.watelectronics.com/tp4056-ic/); [TP4056H datasheet, toppwr](https://www.toppwr.com/uploadfile/file/20260320/69bcb732c9d26.pdf) (по сводке поиска). An unverified claim of 20–80 µA for whole modules — [Alibaba guide](https://electronics.alibaba.com/question/tp4056-lithium-battery-charger-module-guide) (low reliability).
- Battery divider: a 200 kΩ divider on a XIAO ESP32-C6 leaked **≈18.5 µA**, about doubling the sleep current. Fix: higher resistances or switch the divider only during the measurement — [Seeed forum](https://forum.seeedstudio.com/t/current-leak-using-a-voltage-divider-with-the-xiao-esp32-c6/284369) (по сводке поиска). Typical practice is 0.5–1 MΩ plus 10–100 nF on the lower resistor — [EEVblog forum](https://www.eevblog.com/forum/projects/battery-monitoring-voltage-divider/) (по сводке поиска). If only the top of the divider is switched off, about 1.9 V remains on the ADC pin and leaks through the lower resistor. A ground-side N-MOSFET with a gate pull-down, or a P+N pair, is better. MOSFET off-state leakage can be tens to hundreds of µA, higher than the divider itself (DigiKey forum) — [DigiKey forum](https://forum.digikey.com/t/turning-off-a-resistive-voltage-divider-to-save-power/34986); [allaboutcircuits](https://forum.allaboutcircuits.com/threads/how-to-isolate-voltage-divider-both-from-power-and-ground.175820/) (по сводке поиска).
- A typical dev board can draw about 6.5 mA in deep sleep (regulator, USB-serial, power LED) vs about 10 µA on the bare ESP32 — [Hubble: ESP32 deep sleep current](https://hubble.com/community/guides/esp32-deep-sleep-current-what-the-datasheet-says-vs-what-you-ll-actually-measure/) (по сводке поиска; secondary source).
- ESP32 deep sleep: internal pull-ups stay on and can add current through external circuits (`rtc_gpio_isolate`) — [ESP-IDF Sleep Modes](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/sleep_modes.rst).
- Back-powering the OLED through SDA: ≈650 µA — [Adafruit forum t=41332](https://forums.adafruit.com/viewtopic.php?t=41332) (по сводке поиска).
- PineTime: the gap between the nRF52832's chip floor (System ON + RTC ≈1.9 µA) and the board's sleep current (≈60–180 µA) comes from the other parts (SPI flash, charger, sensors). In sleep mode the LCD alone drew about 800 µA (forum) — [int-circuit.eu](https://blog.int-circuit.eu/posts/pinetime/); [Pine64 forum](https://forum.pine64.org/showthread.php?tid=9769) (по сводке поиска); [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420).

### Inferences
- **Divider calculation:** I = V_bat/(R1+R2). At 4.2 V: 200 kΩ → 21 µA; 1 MΩ → 4.2 µA; 2 MΩ → 2.1 µA; 10 MΩ → 0.42 µA (the ADC needs a 100 nF capacitor on the lower leg and a reading after settling). Better options: measure through a ground-side switched divider, or use the MCU's own measurement (nRF52 SAADC can measure VDD/VDDH internally; general knowledge, no source found in this session).
- **Budget table for the "power path" (best case):** LDO 1 µA + DW01 3 µA + TP4056 1–6 µA + divider 1 MΩ 4 µA + IMU 1–2 µA ≈ **10–16 µA** ≈ 0.24–0.38 mA·h/day.
- For an always-on face (1.5–3 mA) the power path is <1%. For SLEEP on an nRF52840 (≈45 µA), it is ≈30%.
- Practical rules: no power LED on the main rail (a charge LED from the charger is fine, since it is lit only during charging); the OLED's pull-ups should be powered from the same rail as the OLED, or the OLED's power should be cut by a load switch together with its pull-ups; in sleep, set SDA/SCL to "input/high-Z" or hold them high, never low; do not use dev boards with a CH340/CP2102 and an AMS1117 for measurements.
- To cut the OLED's power completely (≈0 µA instead of 14–26 µA), use a P-MOSFET or load switch. The gain is ~0.5 mA·h/day, which matters only for very long storage. The cost is a full re-init and a "blink" of the face on power-up.

### Gaps
- No primary DW01A datasheet figure, and no measurement of a whole "TP4056+DW01+FS8205" module from the battery side.
- No sourced Iq values for common alternative LDOs (ME6211, RT9080, TPS7A02, MCP1700) or for buck-boost converters (TPS63802 etc.). Search budget ran out.
- No LiPo self-discharge data for 100–500 mA·h cells.

## 7. Измерения: PPK2, Joulescope, uCurrent, INA219/INA226; методика и типичные ошибки

### Takeaway
For µA-level sleep with mA-level pulses (frames, BLE) you need an instrument that integrates the current with a high sampling rate: the **Nordic PPK2** (~$89, 100 ksps, from ~100–200 nA up to 1 A, can also act as the supply) or the **Joulescope JS220** (0.5 nA, 2 Msps, ~€939). A multimeter only shows a "snapshot". Typical mistakes: measuring through USB or a dev board, back-powering through signal lines, flash left on in light sleep, BLE silently falling back to the main XTAL.

### Cited Findings
- PPK2: 100 ksps; resolution from 100 nA (Nordic) / 0.2 µA (reviews) to 1 mA depending on range; measures from sub-µA to 1 A; source mode 0.8–5.0 V up to 1 A; average-current accuracy claimed better than ±20% (one roadtest got ±6% in its own scenarios); price ≈ $89 — [Nordic PPK2](https://www.nordicsemi.com/Products/Development-hardware/Power-Profiler-Kit-2); [Nordic news 2020](https://www.nordicsemi.com/Nordic-news/2020/12/Power-Profiler-Kit-II); [element14 roadtest](https://community.element14.com/products/roadtest/rv/roadtest_reviews/1880/nordic-power-profiler-kit-ii-ppk2-roadtest); [circuitdigest review](https://circuitdigest.com/review/nordics-power-profiler-kit-2-a-must-have-tool-for-embedded-engineers) (по сводке поиска).
- Joulescope JS220: resolution **0.5 nA**, 2 Msps for current and voltage, 300 kHz bandwidth, shunt voltage ≤20 mV (total drop 25 mV @1 A), price ≈ €939 — [JS220 description](https://download.joulescope.com/products/JS220/JS220-K000/description.html); [JS220 datasheet](https://www.welectron.com/mediafiles/datasheets/jetperch/Jetperch_Joulescope-JS220_Datasheet.pdf); [welectron](https://www.welectron.com/Jetperch-Joulescope-JS220-Precision-DC-Energy-Analyzer) (по сводке поиска).
- InfiniTime: measurements with a PPK2 on an nRF52DK and a PineTime devkit. Results for the "same" sleep differed by measurement point (66 µA vs 0.18 mA, 72 µA average on the PPK) — [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420).
- rp-rs: the developer "could not see the current at all" until they got a proper instrument; after that the results matched the datasheet — [rp-hal issue #659](https://github.com/rp-rs/rp-hal/issues/659).
- Qoitech (Otii): board specs vs validated measurements differ 3–21× — [Qoitech](https://www.qoitech.com/blog/esp32-s3-c3-c6-sleep-power-consumption/) (по сводке поиска).
- Bitbank: cheap multimeter, accuracy assumed ±10% — [Bitbank 2019](https://bitbanksoftware.blogspot.com/2019/06/how-much-current-do-oled-displays-use.html) (по сводке поиска).
- DevZone: the nRF52840 DK adds about 1 µA of leakage to the 2.35 µA target — [DevZone 111812](https://devzone.nordicsemi.com/f/nordic-q-a/111812/nrf52840-system-on-low-power-almost-reached-but-1-a-comes-from-nowhere?pifragment-684=2) (по сводке поиска).

### Inferences
- Method: power the device from the PPK2 in Source Meter mode at 3.7–3.8 V (a "nominal LiPo", with the battery disconnected) → record each state for ≥60 s → take the average current from the logger → put it in the budget table. Measure every state separately (ACTIVE / IDLE / DROWSY / SLEEP) and the transitions (wake + init).
- Separate the OLED and MCU shares: either power the OLED from a separate channel (a second PPK2 / an INA226 on the OLED rail), or measure the "OLED disconnected" vs "OLED connected" difference.
- Typical mistakes checklist: (1) USB connected, so the USB-JTAG/CP210x and the charger are drawing current; (2) a dev board with an LED or AMS1117; (3) SDA/SCL/pull-ups back-powering an unpowered OLED; (4) a 100–200 kΩ divider; (5) flash/PSRAM kept on in light sleep (ESP32-S3); (6) BLE fell back to the main XTAL with no 32k crystal; (7) the UART console or logging blocks auto light sleep; (8) a multimeter on the µA range has a large burden voltage and causes brown-out/reset on a current spike (general knowledge, no sources found); (9) GPIOs left floating in sleep.

### Gaps
- No sourced specs for the uCurrent Gold (burden, ranges) or for the INA219/INA226 (resolution, sampling for µA). The search budget ran out. From general knowledge: the INA219/INA226 suit logging mA-level averages but not sub-µA sleep. This needs a source.

## 8. Рабочие примеры расчёта времени работы (явная математика)

### Takeaway
On a 300 mA·h cell (80% usable = 240 mA·h): an **always-on face** gives about **3 days on an ESP32-C3** and about **6 days on an nRF52840**. A **naive** ESP32 build (no sleep, full frames) gives about **10 hours**, which matches the Dasai Mochi's "up to 8 h of screen-on". **Face only during interaction (2 h/day) + sleep** gives about **3–8 weeks**. A realistic mixed day gives about **7–10 days**. In every efficient scenario the OLED is 50–90% of the budget.

### Cited Findings (inputs)
- OLED: 10% face ≈ 1.5 mA (contrast 31) / 2.0 mA (contrast 127) / 2.6 mA (contrast 255); black but on 0.47–1.4 mA; off 14–26 µA — [jorne wiki/Bitbank](https://github.com/joric/jorne/wiki/Displays); [hsel.co.uk](https://hsel.co.uk/2018/12/04/ssd1306-power-consumption/); [TTN forum](https://www.thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-2/11973/646) (interpolations are mine; the sources are cited above).
- ESP32-C3: active 80 MHz ≈ 15–17 mA; 160 MHz 20–28 mA; light sleep 130 µA (chip) / 193 µA (XIAO); deep sleep 5 µA — [Espressif book](https://espressif.github.io/esp32-c3-book-en/chapter_12/12.2/12.2.4.html); [Mouser datasheet copy](https://www.mouser.com/datasheet/2/744/Seed_113991054-3159716.pdf); [Qoitech](https://www.qoitech.com/blog/esp32-s3-c3-c6-sleep-power-consumption/).
- nRF52840: 52 µA/MHz (≈3.3 mA @64 MHz), System ON idle 1.5–2.35 µA (≈3.5 µA on real boards) — [nRF52840 PS](https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6469/NRF52840-DK.pdf); [SoC brief](https://www.mouser.com/datasheet/2/297/nrf52840_soc_v3_0-2942478.pdf); [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/111812/nrf52840-system-on-low-power-almost-reached-but-1-a-comes-from-nowhere?pifragment-684=2).
- I2C full frame at 400 kHz ≈ 23 ms — [Arduino forum](https://forum.arduino.cc/t/ssd1306-u8g2lib-framerate/522803).
- IMU wake-up ≈ 0.4–2 µA — [ST DT0098](https://www.st.com/resource/en/design_tip/dt0098-setting-up-wakeup-recognition-and-absoluterelative-references-with-sts-mems-accelerometers-stmicroelectronics.pdf).
- Power path ≈ 10–16 µA (sum of section 6's components) — [Torex](https://product.torexsemi.com/en/series/xc6206); [DW01A](https://www.best-microcontroller-projects.com/dw01a.html); [TP4056](https://www.best-microcontroller-projects.com/tp4056.html).
- Dasai Mochi 3: "up to 8 hours of screen-on time" — [dasai.com.au Mochi 3](https://dasai.com.au/pages/mochi3) (по сводке поиска).

### Inferences (all calculations are mine; the assumptions are explicit)
**Basic formula:** T[h] = C_usable[mA·h] / I_avg[mA]; C_usable = C_nom × 0.8. For 150 / 300 / 500 mA·h: 120 / 240 / 400 mA·h. For a mixed day: E_day = Σ(I_i × t_i), days = C_usable / E_day.
Assumptions: render ≈2 ms/frame; partial update ≈256 B (two eyes of ~32×32 px); ESP32-C3 active at 80 MHz = 17 mA, light sleep = 0.19 mA; nRF52840 TWIM transfer ≈1 mA (unconfirmed estimate); power path + IMU = 0.017 mA; LiPo self-discharge and capacity loss at low temperature are ignored.

**Scenario A — face always on, 24/7, 10 fps, partial updates, contrast ≈31 (OLED 1.5 mA)**
- ESP32-C3: MCU = 10×(5.8+2) ms = 7.8% × 17 + 92.2% × 0.19 = 1.33 + 0.18 = 1.51 mA; total = 1.5 + 1.51 + 0.017 ≈ **3.03 mA** → 240/3.03 ≈ **79 h ≈ 3.3 days** (150 mA·h: 40 h; 500 mA·h: 132 h ≈ 5.5 days).
- nRF52840: MCU ≈ 10×2 ms×3.3 mA (0.066) + 10×5.8 ms×1 mA (0.058) + 0.002 ≈ 0.13 mA; total ≈ 1.5 + 0.13 + 0.017 ≈ **1.65 mA** → 240/1.65 ≈ **145 h ≈ 6 days** (150: 73 h; 500: 242 h ≈ 10 days).
- The same at contrast 255 (OLED 2.6 mA): C3 ≈ 4.13 mA → 58 h; nRF ≈ 2.75 mA → 87 h. **Contrast alone changes battery life by 1.4–1.7×.**
- **A "naive" build** (ESP32-C3 at 160 MHz without sleep, `delay()`, full frames, contrast 0x7F): ≈ 22 + 2.0 = **24 mA** → 240/24 = **10 h** (150 mA·h: 5 h). This is the Dasai Mochi range ("up to 8 h screen-on"; users report 2–4 h).
- ESP32-C3, 20 fps full frames at 400 kHz with light sleep: MCU ≈ 9.3 mA + OLED 2.0 → **11.3 mA** → **21 h**.

**Scenario B — face only when moved or looked at (2 h/day at scenario A rates), the other 22 h asleep (OLED `0xAE`+CP off, wake on IMU INT)**
- ESP32-C3 + light sleep: I_sleep = 0.026 (OLED off) + 0.19 + 0.017 ≈ 0.233 mA. E_day = 2×3.03 + 22×0.233 = 6.06 + 5.13 = **11.2 mA·h/day** → 240/11.2 ≈ **21 days**.
- ESP32-C3 + deep sleep (chip 5 µA; boards are often higher): I_sleep ≈ 0.026 + 0.005 + 0.017 = 0.048 mA → E_day = 6.06 + 1.06 = **7.1 mA·h/day** → **≈34 days**. Here the OLED off current (26 µA) and the power path (17 µA) are about 90% of the sleep current. But the 2 h of active face (6.06 mA·h) already dominate the day, so cutting the OLED's power (I_sleep ≈ 0.022 mA → 6.5 mA·h/day) gains only about 34 → 37 days. In this scenario the lever is the length and cost of the active phase.
- nRF52840: I_sleep ≈ 0.026 + 0.003 + 0.017 = 0.046 mA → E_day = 2×1.65 + 22×0.046 = 3.3 + 1.01 = **4.3 mA·h/day** → **≈56 days**.

**Scenario C — a realistic mixed day:** 3 h ACTIVE (10 fps, partial, contrast 127: OLED 2.0 mA) + 9 h IDLE (2 fps, partial, contrast 31: OLED 1.5 mA) + 4 h DROWSY (sleeping face ~3%, minimum contrast: OLED ≈0.8 mA, 1 fps) + 8 h night or bag (off).
- ESP32-C3: ACTIVE 2.0+1.51+0.017 = 3.53 mA × 3 h = 10.6; IDLE 1.5+0.46+0.017 = 1.98 × 9 = 17.8; DROWSY 0.8+0.33+0.017 = 1.15 × 4 = 4.6; NIGHT 0.233 × 8 = 1.9 → **34.9 mA·h/day → 6.9 days** (150 mA·h: 3.4 days; 500 mA·h: 11.5 days).
- nRF52840: ACTIVE 2.15 × 3 = 6.45; IDLE 1.55 × 9 = 14.0; DROWSY 0.83 × 4 = 3.3; NIGHT 0.046 × 8 = 0.37 → **24.1 mA·h/day → 10 days** (150: 5 days; 500: 16.6 days).
- The OLED share here (2×3 + 1.5×9 + 0.8×4 + 0.026×8 ≈ 22.9 mA·h) is about 66% of daily energy on the C3 and about 95% on the nRF. The strongest next levers are fewer lit pixels, lower contrast and more hours in SLEEP.

**Scenario D — the cost of night or a bag (8 h)**
- Keep the sleeping face on (≈1.15 mA, C3): 9.2 mA·h/night; panel off + light sleep (0.233 mA): 1.9 mA·h; off + deep sleep (0.048 mA): 0.4 mA·h.
- The difference of ≈7.3 mA·h/night ≈ 3% of a 240 mA·h budget per night, ≈ 21% per week. Turning the panel off "in the dark, when still" is a must.

**Scenario E — radio**
- nRF52840 advertising at 2 s (+5.9 µA) adds +0.14 mA·h/day to scenario B: 4.3 → 4.44 (−3% battery life). At 200 ms (+30.5 µA) it adds +0.73 → 5.03 mA·h/day (−15%).
- ESP32-C3 with an untuned BLE setup (≈35 mA) → 240/35 ≈ 7 h. The radio must be off by default.

**Comparison by MCU (scenario C, 300 mA·h):** nRF52840 ≈ 10 days > STM32U0 (no radio; probably similar to the nRF, since run is 52 µA/MHz and Stop 2 <1 µA; estimate) > ESP32-C6 (light sleep with 32k ≈34 µA, so the night share is lower than the C3; active is similar) ≈ ESP32-C3 ≈ 7 days > ESP32-S3 (light sleep on boards up to 2.9 mA if flash/PSRAM is not handled; higher active) > RP2040 (sleep 0.4–1.3 mA, active ≈20 mA; estimated ≤3–4 days even with tricks).

### Gaps
- All scenarios are calculations from datasheet and hobbyist figures, not measurements of a real prototype. ESP32 sleep-transition overhead, render time and nRF52 TWIM current are assumptions.
- The usable-capacity coefficient (0.8) is taken from the brief. No sources were found for LiPo self-discharge or for the effect of temperature and cutoff voltage.

## 9. Как коммерческие и open-source устройства добиваются многодневной работы (Dasai Mochi, Tamagotchi, Bangle.js, PineTime, Watchy)

### Takeaway
Multi-day battery life is reached in only two ways: **a display that holds the image at ≈0 power** (Bangle.js 2 memory LCD, Watchy e-paper) or **an aggressive "screen off" policy** (PineTime/InfiniTime: screen and LCD sleep, BLE optimizations). An OLED companion that shows a face constantly (Dasai Mochi) lives for **hours** ("up to 8 h screen-on"). For our keychain that means behavioral states plus "screen off in the dark or when still" are mandatory if the goal is days or weeks.

### Cited Findings
- **Dasai Mochi 3**: "up to 8 hours of screen-on time". Owners report 2–3 h of heavy use, and a Mochi Futures buyer reported 3–4 h. No official mAh rating found. Mochi 3 uses a "Full Colour OLED" — [dasai.com.au Mochi 3](https://dasai.com.au/pages/mochi3); [dasai.com.au product](https://dasai.com.au/products/mochi-3); [Futures Mochi](https://dasai.com.au/products/futures-mochi) (по сводке поиска). DIY clones use a 16340/CR123 1300 mA·h cell + TP4056 and a 1.3" 128×64 OLED — [Thingiverse 7255174](https://www.thingiverse.com/thing:7255174); [GitHub maraulsav/Dasai-Mochi](https://github.com/maraulsav/Dasai-Mochi) (по сводке поиска).
- **Bangle.js 2** (nRF52840, always-on memory LCD, 175 mA·h): idle **0.15 mA** (accelerometer at 1.25 Hz, the default after ~120 s still), 0.3 mA at 12.5 Hz, a clock updating every second 0.5 mA, touch unlocked 2.8 mA, **backlight 17 mA**, 100% CPU JS 4 mA, GPS 26 mA, off 0.02–0.03 mA. "4 weeks standby"; about a month with a clock updating once a minute, about 2 weeks with one updating every second — [Bangle.js2 docs](https://github.com/espruino/EspruinoDocs/blob/master/boards/Bangle.js2.md). The shop page lists 200 mA·h (a discrepancy) — [Espruino shop](https://shop.espruino.com/banglejs2) (по сводке поиска).
- **PineTime** (nRF52832, LCD, 170–180 mA·h): LCD backlight 1.8–12.3 mA depending on brightness; the wiki recommends putting the LCD into sleep — [Pine64 wiki](https://wiki.pine64.org/wiki/PineTime) (по сводке поиска). InfiniTime: sleep 1.1 mA in a test; after optimizations BLE off 250 µA and slow advertising 280 µA; "a bit more than 16 days" after the June 2023 merge — [InfiniTime discussion #1420](https://github.com/InfiniTimeOrg/InfiniTime/discussions/1420); firmware 1.13 went from 3–5 days to >10 days — [ncartron.org](https://www.ncartron.org/infinitime-113---improved-heart-rate-algorithm-and-battery-life-and-more.html) (по сводке поиска).
- **Watchy** (ESP32 + e-paper): wakes every 60 s (RTC + accelerometer, display refresh), then deep sleep. 5–7 days for timekeeping only, 2–3 days with Wi-Fi — [Watchy docs: Battery Life](https://watchy.sqfmi.com/docs/battery-life/) (по сводке поиска).
- e-paper and memory LCD draw almost nothing between updates ("<0.1 mA on refresh" per the jorne wiki) — [jorne wiki](https://github.com/joric/jorne/wiki/Displays).

### Inferences
- Average current of reference devices (calculated): Bangle.js 2: 175 mA·h / (28 days × 24 h) ≈ **0.26 mA** average; PineTime: ~175 mA·h / (16 × 24) ≈ **0.46 mA**; Dasai Mochi: unknown capacity, but "8 h" means tens of mA on average, i.e. the screen and MCU are always on with no deep sleep.
- Our OLED face at ≈1.5–2 mA "just to stay on" is already about 6–8× more than Bangle's whole average budget. Matching smartwatch-class battery life is possible only if the screen is off most of the time (scenario B) or if the device moves to a memory LCD / e-paper (a different product).
- Strategies worth borrowing: Bangle — lower the sensor polling rate after N seconds still; InfiniTime — put the display into sleep, minimize BLE activity, keep touch from waking; Watchy — wake briefly and sleep the rest of the time.

### Gaps
- **Tamagotchi**: no sources found on battery life or currents (the search budget ran out). From general knowledge, classic Tamagotchis run on a 4-bit Epson/Bandai MCU with a segment LCD and CR2032 cells for months, but this needs sources.
- No official Dasai Mochi battery capacity, MCU or display controller details.
- The Watchy battery capacity and measured sleep current were not confirmed.
