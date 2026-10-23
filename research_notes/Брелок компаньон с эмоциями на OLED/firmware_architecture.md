# Firmware architecture and firmware-level optimization (архитектура прошивки и оптимизация)

Scope: software only (frameworks, libraries, rendering pipeline, animation and behavior engines, data formats, sleep-aware code patterns). Hardware choice, power numbers, animation art and general pitfalls are covered by other researchers.

Method note: docs.espressif.com, docs.zephyrproject.org, docs.lvgl.io, docs.nordicsemi.com and similar doc hosts could not be reached from this research environment. Most claims below were therefore checked against the **documentation sources and code in the projects' GitHub repositories** (the .rst/.md files that the official doc sites are built from, plus driver source code). GitHub links are given; the rendered pages on the doc sites have the same content. The web-search budget ran out partway through, so a few points were left unverified; they are listed under Gaps. Library versions are as of the repository state read on 2026-10-23.

---

## Q1. Frameworks and toolchains: Arduino vs ESP-IDF vs PlatformIO vs Zephyr/NCS vs Pico SDK vs MicroPython/CircuitPython vs Rust

### Takeaway
For a battery keychain that should idle in automatic light sleep while the face is static, the best fits are **ESP-IDF (C/C++) on ESP32-C3/C6/S3** or **Zephyr/nRF Connect SDK on nRF52840**. Both have tickless idle and automatic low-power entry built in. The stock Arduino-ESP32 core ships with the power-management/tickless options off. Use it for prototyping, or run Arduino as an ESP-IDF component so you can change menuconfig. Rust/Embassy is a credible, modern option: its async executor sleeps the core automatically. MicroPython and CircuitPython are good for quick face design but slow for per-frame rendering and give weaker sleep control.

### Cited Findings
**ESP-IDF power management (automatic light sleep = DFS + tickless idle)**
- ESP-IDF PM can scale the APB and CPU frequency and put the chip into Light-sleep automatically. Components state their needs with PM locks: `ESP_PM_CPU_FREQ_MAX`, `ESP_PM_APB_FREQ_MAX` and `ESP_PM_NO_LIGHT_SLEEP` — [ESP-IDF power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst) (rendered: [docs.espressif.com](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c3/api-reference/system/power_management.html))
- Enable it with `CONFIG_PM_ENABLE`, then call `esp_pm_configure()` with an `esp_pm_config_t {max_freq_mhz, min_freq_mhz, light_sleep_enable}`. "Automatic Light-sleep is based on FreeRTOS Tickless Idle functionality". If `CONFIG_FREERTOS_USE_TICKLESS_IDLE` is off, `esp_pm_configure()` returns `ESP_ERR_NOT_SUPPORTED` — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- The sleep length is set by "FreeRTOS tasks blocked with finite timeouts" and by "Timers registered with esp_timer". An `esp_timer` created with `skip_unhandled_events = true` will not wake the system — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- PM costs interrupt latency: at least 0.2 µs (240 MHz, no DFS) and up to 40 µs (DFS switching from 40 to 80 MHz on interrupt entry) — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- "In Light-sleep, peripherals are clock gated, and interrupts (from GPIOs and internal peripherals) will not be generated". Wake sources from sleep_modes must be used, e.g. GPIO — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- Debug helpers: `esp_pm_dump_locks()`, `esp_pm_get_lock_stats_all()` and `CONFIG_PM_PROFILING` show which component is blocking sleep — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- The SPI master, I2C and I2S drivers hold `ESP_PM_APB_FREQ_MAX` for the length of a transaction. GPTimer, SPI slave, PCNT and the Bluetooth controller hold locks while they are enabled. On non-ESP32 chips, BT holds `ESP_PM_NO_LIGHT_SLEEP` even with modem sleep (see Q9 for the BLE clock-source caveat) — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- On chips with `SOC_PM_SUPPORT_TOP_PD`, for example ESP32-C6, peripherals can be powered down in Light-sleep with register-context retention (`CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP`). I2C, SPI, GPTimer, LEDC and UART are on the retention list — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst); ESP32-C6 has `SOC_PM_SUPPORT_TOP_PD` and `SOC_I2C_SUPPORT_SLEEP_RETENTION` — [esp32c6 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c6/include/soc/soc_caps.h)
- Kconfig defaults: `PM_ENABLE` is `default n`, and `FREERTOS_USE_TICKLESS_IDLE` "depends on PM_ENABLE" and is `default n` — [esp_pm/Kconfig](https://github.com/espressif/esp-idf/blob/master/components/esp_pm/Kconfig), [freertos/Kconfig](https://github.com/espressif/esp-idf/blob/master/components/freertos/Kconfig)
- ESP-IDF versions: the `release/v6.0` and `release/v6.1` branches exist. `master` reports 6.2.0, i.e. the development line — [esp_idf_version.h (release/v6.1)](https://github.com/espressif/esp-idf/blob/release/v6.1/components/esp_common/include/esp_idf_version.h), [esp_idf_version.h (master)](https://github.com/espressif/esp-idf/blob/master/components/esp_common/include/esp_idf_version.h)

**Arduino-ESP32 / PlatformIO**
- The Arduino-ESP32 core on `master` is version 3.3.12 — [arduino-esp32 platform.txt](https://github.com/espressif/arduino-esp32/blob/master/platform.txt). The pioarduino PlatformIO platform states "currently espressif Arduino 3.3.12 and IDF v5.5.5". So the Arduino core still sits on IDF 5.5.x, not on 6.x — [pioarduino/platform-espressif32 README](https://github.com/pioarduino/platform-espressif32/blob/main/README.md)
- The prebuilt Arduino libraries come from esp32-arduino-lib-builder configs. `defconfig.common` sets `CONFIG_FREERTOS_HZ=1000` and does not set `CONFIG_PM_ENABLE` or `CONFIG_FREERTOS_USE_TICKLESS_IDLE`. The `defconfig.esp32c3` and `defconfig.esp32c6` files don't set them either, so the Kconfig default of n applies. C3 uses NimBLE (`CONFIG_BT_NIMBLE_ENABLED=y`) — [defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common), [defconfig.esp32c3](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.esp32c3), [defconfig.esp32c6](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.esp32c6)
- Arduino can run as an ESP-IDF component, configured with `idf.py menuconfig`. It requires `CONFIG_FREERTOS_HZ` = 1000 Hz — [arduino-esp32 docs: esp-idf_component.rst](https://github.com/espressif/arduino-esp32/blob/master/docs/en/esp-idf_component.rst)
- pioarduino adds its own "Hybrid compile" feature, for example to support ESP32-C2/C61/solo1 under Arduino — [pioarduino README](https://github.com/pioarduino/platform-espressif32/blob/main/README.md)

**Zephyr / nRF Connect SDK (nRF52840)**
- `CONFIG_TICKLESS_KERNEL` is `default y if TICKLESS_CAPABLE` ("fully event driven kernel. Periodic system clock interrupt generation would be stopped") — [zephyr kernel/Kconfig](https://github.com/zephyrproject-rtos/zephyr/blob/main/kernel/Kconfig)
- System PM: when it has nothing to schedule, the kernel idle thread calls the PM subsystem (`CONFIG_PM`). A policy picks a power state. The default residency policy enters a state if `time_to_next_scheduled_event >= min_residency_us + exit_latency`. Custom policies go through `pm_policy_next_state()` — [zephyr doc/services/pm/system.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/pm/system.rst)
- Device runtime PM (`CONFIG_PM_DEVICE_RUNTIME`) suspends unused devices by usage count, independent of the system state — [device_runtime.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/pm/device_runtime.rst)
- Versions: Zephyr `main` VERSION = 4.5.0-rc3 — [zephyr VERSION](https://github.com/zephyrproject-rtos/zephyr/blob/main/VERSION). sdk-nrf `main` VERSION = 3.4.99, meaning the dev line after 3.4 — [sdk-nrf VERSION](https://github.com/nrfconnect/sdk-nrf/blob/main/VERSION)

**Raspberry Pi Pico SDK (RP2040/RP2350)**
- Sleep and dormant APIs live in **pico-extras** (`pico_sleep`), not in the core SDK. Dormant stops ALL clocks. Sources are XOSC, ROSC and LPOSC (RP2350 only). Functions include `sleep_goto_sleep_for(delay_ms, cb)` and `sleep_goto_sleep_until()`. On RP2040 the RTC must be driven by an external clock for timed dormant — [pico-extras pico/sleep.h](https://github.com/raspberrypi/pico-extras/blob/master/src/rp2_common/pico_sleep/include/pico/sleep.h)

**MicroPython / CircuitPython**
- MicroPython `machine.lightsleep([ms])` keeps full RAM and state and resumes where it stopped. `deepsleep` may lose RAM and restarts from main. Behaviour "is highly dependent on the underlying hardware" — [micropython docs/library/machine.rst](https://github.com/micropython/micropython/blob/master/docs/library/machine.rst)
- CircuitPython `alarm.light_sleep_until_alarms()` / `exit_and_deep_sleep_until_alarms()`. "If CircuitPython is connected to a host computer, the connection will be maintained, and the microcontroller may not actually go into a light sleep". Deep sleep restarts `code.py`, and `alarm.sleep_memory` persists across it — [circuitpython shared-bindings/alarm/__init__.c](https://github.com/adafruit/circuitpython/blob/main/shared-bindings/alarm/__init__.c)
- A MicroPython SSD1306 benchmark on the MicroPython forum: about 2 ms bus time per frame on SPI @16 MHz vs about 23 ms on I2C @400 kHz, with about 64 ms of *compute* per frame in both cases. Interpreter rendering, not the bus, was the bottleneck. (This comes from a search-result summary; the page itself could not be fetched) — [forum.micropython.org](https://forum.micropython.org/viewtopic.php?p=1766)

**Rust (Embassy, esp-hal)**
- Embassy: async tasks "transformed at compile time into state machines that get run cooperatively". "Requires no dynamic memory allocation and runs on a single stack, so no per-task stack size tuning". "The async executor automatically puts the core to sleep when there's no work to do". HALs exist for STM32, nRF52/53/54/91 and RP2040/RP23xx, and for ESP32 via esp-hal. BLE comes from `trouble` (nRF52, rp2040, esp32…) or `nrf-softdevice`. `embassy-boot` provides power-fail-safe DFU with rollback — [embassy README](https://github.com/embassy-rs/embassy/blob/main/README.md)
- esp-hal: a `no_std` HAL for ESP32, C2/C3/C5/C6/C61, H2, P4 and S2/S3. It also programs the LP RISC-V core of the C6/S2/S3 (esp-lp-hal). It is past 1.0, and the README recommends pinning `esp-hal = "~1.1"` — [esp-hal README](https://github.com/esp-rs/esp-hal/blob/main/README.md)

### Inferences
- **Recommended stack (ESP32-C3/C6):** ESP-IDF 5.5.x/6.x in C++, with `CONFIG_PM_ENABLE=y`, `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y`, `esp_pm_configure({max=160, min=40 (XTAL), light_sleep_enable=true})`. Alternatively Arduino-as-component, if Arduino libraries such as U8g2 or RoboEyes-like code are wanted. Under the stock Arduino core (tick 1000 Hz, no tickless), automatic light sleep is unavailable. Only manual `esp_light_sleep_start()` is possible, so the device would wake 1000 times a second for the tick unless code sleeps manually.
- **Recommended stack (nRF52840):** NCS/Zephyr with the SSD1306 driver from Zephyr's display subsystem (Q2), or a custom 1 KB-framebuffer flush. You get tickless idle "for free", plus mature BLE (SoftDevice Controller) and MCUboot/SMP DFU (Q9).
- **Development speed vs power:** prototype the face in Arduino or Wokwi (Q10). Keep the face/behavior engine as portable C++ with no Arduino dependency, then port the platform layer (display flush, timers, sleep, input) to ESP-IDF or Zephyr. Use MicroPython only for design exploration. Its ~64 ms/frame compute (above) rules out smooth 20–30 fps procedural animation in pure Python.
- **Rust** suits a team that already knows it. The async/await model maps naturally onto "sleep until next frame OR sensor IRQ OR BLE event". Embassy does not give ESP32 the IDF-grade automatic light sleep with BLE, though; that needs checking (Gap).

### Gaps
- Whether pioarduino's "custom sdkconfig / hybrid compile" lets you set `CONFIG_PM_ENABLE` and tickless idle under `framework = arduino` was not verified (search budget ran out).
- Not checked: whether the official PlatformIO `platform-espressif32` (not pioarduino) supports Arduino-ESP32 3.x as of Oct 2026.
- The latest MicroPython/CircuitPython releases and the latest LVGL patch version were not verified.
- STM32L (STM32CubeL4/U5 LPM, Stop2, Zephyr STM32 PM) was not researched.
- Embassy + esp-hal light-sleep support (automatic, with BLE) was not verified.

---

## Q2. Display drivers and graphics libraries; bus speed and achievable FPS

### Takeaway
SSD1306 GDDRAM is 8 pages × 128 columns of vertical bytes, so a full frame is 1024 bytes. Over I2C at the rated 400 kHz, a full frame takes ~23 ms on the wire (theoretical ceiling ~43 fps; measured ~30 fps). Over SPI at 8–10 MHz it takes ~1 ms. For a sleepy battery device, the bus time matters more than the library. The usual choices are **U8g2 (full-buffer `_F_`, HW I2C/SPI)** or a **custom thin driver** with dirty-region flush. Adafruit_SSD1306 always sends the whole frame. LVGL works with 1-bpp displays but is overkill for a face.

### Cited Findings
**SSD1306 facts (datasheet)**
- GDDRAM is "a bit mapped static RAM holding the bit pattern to be displayed". It is 128×64 bits, divided into eight pages PAGE0–PAGE7 (COM0–7 … COM56–63) — [SSD1306 datasheet Rev 1.1, §8.7](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf) (mirror of [Adafruit copy](https://cdn-shop.adafruit.com/datasheets/SSD1306.pdf))
- Command 0x20 sets the addressing mode (00b horizontal, 01b vertical, 10b page). 0x21 "Set Column Address" and 0x22 "Set Page Address" are triple-byte commands that set start/end and reset the pointer. In horizontal mode the pointer wraps from the end column to the start column and goes to the next page, so a rectangular window can be streamed in one burst — [SSD1306 datasheet §10.1.4–10.1.5](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)
- I2C: after the slave address comes a control byte with Co and D/C# bits. Co=0 means only data bytes follow, and D/C#=1 means the bytes go to GDDRAM with an auto-incrementing column pointer. So one transaction `[addr][0x40][N data bytes]` streams a window — [SSD1306 datasheet §8.1.5](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)
- Timing: I2C clock cycle time min **2.5 µs** (i.e. 400 kHz). 4-wire SPI clock cycle time min **100 ns** (10 MHz) — [SSD1306 datasheet AC characteristics](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf); same conclusion in [nopnop2002/esp-idf-ssd1306 README](https://github.com/nopnop2002/esp-idf-ssd1306)
- Sleep (0xAE, display OFF) current: ICC,SLEEP ≤ 10 µA and IDD,SLEEP ≤ 10 µA. Display ON with all pixels on at contrast 0xFF: ICC typ 430 µA / max 780 µA (VCC=12 V, no panel load) — [SSD1306 datasheet DC characteristics](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)
- The panel refresh rate is set inside the controller: FFRM = Fosc / (D × K × MUX), with K = 54 at power-on reset and MUX = 64. D comes from command 0xD5 — [SSD1306 datasheet §8.3](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)
- Hardware scroll commands 0x26/0x27 (horizontal) and 0x2E (deactivate) exist — [SSD1306 datasheet §10.2](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)

**Measured or reported frame rates**
- ESP8266 + Adafruit_SSD1306: ~10 fps at 100 kHz, ~30 fps at 400 kHz, ~60 fps at 800 kHz (`Wire.setClock(800000L)` was the maximum that worked). Another user's draw time went from 112 ms to 30 ms — [Adafruit_SSD1306 issue #80](https://github.com/adafruit/Adafruit_SSD1306/issues/80)
- ESP32-C5 project: stable 30 FPS at 400 kHz I2C for 128×64 (1024 bytes/frame, ~9 clocks per byte incl. ACK), using triple buffering for network input (from a search-result summary) — [DFRobot community article](https://mc.dfrobot.com.cn/article-384894.html)
- Arduino Uno + U8g2 measured ~7.7 fps (search-result summary) — [Arduino forum thread](https://forum.arduino.cc/t/ssd1306-u8g2lib-framerate/522803)
- MicroPython: SPI@16 MHz ~2 ms/frame vs I2C@400 kHz ~23 ms/frame (search-result summary) — [forum.micropython.org](https://forum.micropython.org/viewtopic.php?p=1766)

**MCU bus limits**
- nRF52 TWIM frequencies: 100/250/400 kbps. 1000 kbps exists only where `NRF_TWIM_HAS_1000_KHZ_FREQ` is set — [nrfx hal/nrf_twim.h](https://github.com/NordicSemiconductor/nrfx/blob/master/hal/nrf_twim.h). A Nordic DevZone answer says the nRF52840 TWI maximum is 400 kbit/s (search-result summary) — [DevZone](https://devzone.nordicsemi.com/f/nordic-q-a/37606/twi---easy-dma/158651). nRF54 TWIM pages list 1000 kbps — [Nordic nRF54L TWIM](https://docs.nordicsemi.com/r/bundle/ps_nrf54lm20a/page/twim.html)
- nRF SPIM: 125 kbps up to 8 Mbps. 16 and 32 Mbps exist only where `NRF_SPIM_HAS_16_MHZ_FREQ` / `_32_MHZ_FREQ` are set — [nrfx hal/nrf_spim.h](https://github.com/NordicSemiconductor/nrfx/blob/master/hal/nrf_spim.h)
- RP2040 I2C: no High-speed/Ultra-fast mode; "the fastest operation being fast mode plus at up to 1000Kb/s". DMA is paced by `I2C_DREQ_NUM(i2c, is_tx)` — [pico-sdk hardware/i2c.h](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_i2c/include/hardware/i2c.h)
- ESP32-C3 has one HP I2C controller (`SOC_I2C_NUM 1`). ESP32-C6 has `SOC_I2C_NUM 2` and an LP I2C — [esp32c3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c3/include/soc/soc_caps.h), [esp32c6 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c6/include/soc/soc_caps.h)

**Libraries (versions/maintenance as of Oct 2026)**
- **U8g2** (U8g2_Arduino `version=2.37.1`) — [library.properties](https://github.com/olikraus/U8g2_Arduino/blob/master/library.properties)
  - Constructor `U8G2_SSD1306_128X64_NONAME_F_HW_I2C` / `_F_4W_HW_SPI`. "F" keeps a full frame buffer in RAM; use `clearBuffer()`/`sendBuffer()`. "1"/"2" keep 1 or 2 pages and need a `firstPage()/nextPage()` loop. "2" is "up to two times faster than 1" — [u8g2setupcpp wiki](https://github.com/olikraus/u8g2/wiki/u8g2setupcpp)
  - FAQ: "F" is faster and needs more RAM. HW I2C/SPI is "much faster" than SW emulation. "For HW I2C the default speed for some controllers is set 100KHz", so try `u8g2.setBusClock(400000)` — [u8g2 doc/faq.txt](https://github.com/olikraus/u8g2/blob/master/doc/faq.txt); `setBusClock` doc suggests 200–400 kHz for I2C and 1–8 MHz for SPI — [u8g2reference](https://github.com/olikraus/u8g2/wiki/u8g2reference)
  - `updateDisplayArea(tx, ty, tw, th)` sends only a rectangle in 8×8-pixel tiles (full-buffer mode only). `getBufferPtr()` gives direct buffer access. `setPowerSave(1)` turns the display off with RAM kept — [u8g2reference](https://github.com/olikraus/u8g2/wiki/u8g2reference)
- **Adafruit_SSD1306** (`version=2.5.17`, depends on Adafruit GFX) — [library.properties](https://github.com/adafruit/Adafruit_SSD1306/blob/master/library.properties)
  - `display()` always sets PAGEADDR 0..0xFF and COLUMNADDR 0..127 and streams the **entire** buffer. I2C goes out in `WIRE_MAX`-byte chunks (32 by default unless the core defines `BUFFER_LENGTH`/`I2C_BUFFER_LENGTH`), and each chunk restarts the transaction and re-sends the 0x40 control byte. The default I2C clock during transfer is 400 kHz (`clkDuring`), restored to 100 kHz afterwards (`clkAfter`). The buffer is `malloc`'ed in `begin()`. Default SPI is 8 MHz — [Adafruit_SSD1306.cpp](https://github.com/adafruit/Adafruit_SSD1306/blob/master/Adafruit_SSD1306.cpp)
- **ThingPulse esp8266-oled-ssd1306** (`version=4.6.2`): SSD1306/SH1106 over I2C or SPI on ESP8266/ESP32/mbed — [README](https://github.com/ThingPulse/esp8266-oled-ssd1306). `SSD1306Wire` defaults to **700 kHz** I2C. With `OLEDDISPLAY_DOUBLE_BUFFER` it compares `buffer` with `buffer_back`, computes one bounding box of the changed bytes, and returns early if nothing changed — [src/SSD1306Wire.h](https://github.com/ThingPulse/esp8266-oled-ssd1306/blob/master/src/SSD1306Wire.h)
- **ESP-IDF native:** `esp_lcd_new_panel_ssd1306()` over an `esp_lcd` I2C panel-IO — [i2c_lcd.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/lcd/i2c_lcd.rst). The official example `peripherals/lcd/i2c_oled` uses it with LVGL — [example README](https://github.com/espressif/esp-idf/blob/master/examples/peripherals/lcd/i2c_oled/README.md). Third-party `nopnop2002/esp-idf-ssd1306` (SSD1306/SH1106) defaults to I2C 400 kHz and SPI 1 MHz, configurable up to 10 MHz — [README](https://github.com/nopnop2002/esp-idf-ssd1306)
- **Zephyr:** `display_ssd1306.c` supports SSD1306 and SH1106 (I2C/SPI). It uses horizontal addressing and writes arbitrary windows with 0x21/0x22, reports `SCREEN_INFO_MONO_VTILED` and `PIXEL_FORMAT_MONO01/MONO10`, and maps `blanking_on/off` to suspend/resume — [zephyr drivers/display/display_ssd1306.c](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/display/display_ssd1306.c). Zephyr also has a "Monochrome Character Framebuffer" (CFB) API — [zephyr display docs index](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/hardware/peripherals/display/index.rst)
- **LVGL (v9.x):** `LV_COLOR_DEPTH 1` / `LV_COLOR_FORMAT_I1` means 1 bpp, **horizontally** mapped (bits of a byte are adjacent pixels in a row, MSB first). Buffers need +8 bytes for a 2×4-byte palette (`px_map += 8` in flush). `lv_draw_sw_i1_convert_to_vtiled()` converts to the column-wise (vtiled) layout that SSD1306-like controllers need — [LVGL color_format.rst (release/v9.3)](https://github.com/lvgl/lvgl/blob/release/v9.3/docs/src/details/main-modules/display/color_format.rst). LVGL 9.5 requirements: Flash > 64 kB (>180 kB recommended), static RAM ~2 kB, stack > 2 kB (8 kB recommended), heap > 2 kB (48 kB recommended) (search-result summary) — [LVGL 9.5 requirements](https://docs.lvgl.io/9.5/introduction/requirements.html)
- **MicroPython** `ssd1306.py` subclasses `framebuf.FrameBuffer` with `framebuf.MONO_VLSB`, which is the SSD1306 native vertical-byte layout, and `show()` writes via `i2c.writeto` — [micropython-lib ssd1306.py](https://github.com/micropython/micropython-lib/blob/master/micropython/drivers/display/ssd1306/ssd1306.py)
- **Rust:** the `ssd1306` crate (0.10.0 on master) has `into_buffered_graphics_mode()` + `flush()` and an optional `async` feature (embedded-hal-async) — [ssd1306 README](https://github.com/rust-embedded-community/ssd1306), [Cargo.toml](https://github.com/rust-embedded-community/ssd1306/blob/master/Cargo.toml). `embedded-graphics` is `no_std`, needs no allocator and is iterator-based, "without using any buffers". It provides lines, rectangles, circles, ellipses, arcs and sectors — [embedded-graphics README](https://github.com/embedded-graphics/embedded-graphics)
- **Wokwi** supports Adafruit SSD1306, U8g2/U8x8, lcdgfx, SSD1306Ascii and Tiny4kOLED for its SSD1306 part (Q10) — [wokwi-docs board-ssd1306](https://github.com/wokwi/wokwi-docs/blob/main/docs/parts/board-ssd1306.md)
- TFT_eSPI targets colour TFT controllers. esp32-eyes notes it switched *from* TFT_eSPI (original ESP32_Faces) *to* U8g2 for the OLED — [playfultechnology/esp32-eyes README](https://github.com/playfultechnology/esp32-eyes)

### Inferences
- **Wire-time math (my calculation; I2C = 9 SCL clocks per byte incl. ACK, SPI = 8):**

| Transfer | Bytes on wire | 400 kHz I2C | 800 kHz I2C* | 1 MHz I2C* | 8 MHz SPI | 10 MHz SPI |
|---|---|---|---|---|---|---|
| Full frame, one transaction ([addr][0x40][1024]) | 1026 | **23.1 ms** (≤43 fps) | 11.5 ms | 9.2 ms | **1.02 ms** | 0.82 ms |
| Full frame, Adafruit 32-B chunks (34 × (addr+0x40) extra) | ~1094 | ~24.6 ms + gaps | ~12.3 ms | ~9.8 ms | n/a | n/a |
| Two eye windows 40 col × 4 pages each + 2× window cmds | ~340 | ~7.7 ms | ~3.8 ms | ~3.1 ms | ~0.34 ms | ~0.27 ms |
| Blink-only update (eyes shrink inside same windows) | same as above | | | | | |
| Static face | 0 | 0 | 0 | 0 | 0 | 0 |

  *Above the 400 kHz datasheet rating. It often works (ThingPulse defaults to 700 kHz; 800 kHz measured on ESP8266), but it is not guaranteed and depends on pull-ups and wiring. A product should be validated at its chosen speed across temperature and voltage, or should use SPI.
- **Power implication:** at 30 fps full-frame on 400 kHz I2C, the bus is busy ~23 of every 33 ms (~70% duty). During that time the ESP-IDF driver holds an APB lock (no light sleep) and the CPU is often awake. Smooth 30 fps on I2C is therefore incompatible with deep idling. Mitigations, in order of impact: (1) send nothing when the face is static, because GDDRAM holds the image by itself; (2) dirty-window updates; (3) a lower frame rate for slow idle motion; (4) SPI if the module exposes it; (5) out-of-spec I2C overclock.
- **Library choice:**
  - *Prototype:* U8g2 `_F_HW_I2C` (portable, many primitives, `updateDisplayArea`, `writeBufferXBM` for screenshots), or RoboEyes on Adafruit GFX for an instant face (GPL-3.0, Q4).
  - *Product:* your own 1 KB buffer in **SSD1306-native page layout**, a small set of integer primitives (filled rounded rect, filled ellipse spans, triangles/half-planes for lids, masked sprite blit), and a platform-specific flush (dirty spans, DMA/async). U8g2 can still be used as the drawing back end, since `getBufferPtr()` gives its native buffer.
  - *Avoid for this product:* Adafruit_SSD1306's `display()` (always full frame, small chunks, and switches the bus clock back to 100 kHz, which would slow other devices like the IMU if they rely on the default). Also avoid LVGL unless rich UI menus are planned. LVGL needs ≥64 kB flash and tens of kB of heap, I1 is row-major so every flush needs a vtiled conversion, and widgets don't help draw a parametric face.
- Since nRF52840 TWIM tops out at 400 kHz, an nRF52840 build gains the most from an SPI OLED module or aggressive dirty-window updates.

### Gaps
- No primary measurement of U8g2 `sendBuffer()` time on ESP32-C3/C6 at 400 kHz / 1 MHz was found. Run U8g2's FPS example on the target.
- Whether specific cheap SSD1306 modules reliably accept 1 MHz I2C was not verified (no source).
- Hardware scroll (0x26/0x27) as a zero-CPU "look sideways" effect is untested, and how it interacts with partial writes is unknown.
- The SSD1306 has no tear-effect output on typical modules (not verified). Possible tearing while streaming at 30 fps was not researched.

---

## Q3. Rendering pipeline: framebuffer, dirty pages/windows, DMA/async transfers, double buffering, frame pacing

### Takeaway
Keep one 1 KB framebuffer in SSD1306 page layout plus a 1 KB shadow copy of what is on the panel. Diff the two to get per-page dirty column spans. Send only those spans using 0x21/0x22 windows in horizontal addressing mode, without memcpy and asynchronously where the platform allows. Advance animation by **time**, not frames. When nothing animates, block on an event with a timeout until the next scheduled micro-event (blink, saccade), so the RTOS can enter automatic light sleep.

### Cited Findings
- SSD1306 byte layout: PAGE0–PAGE7 × 128 columns. With 0x20=00 (horizontal), 0x21 col start/end and 0x22 page start/end define a window. The pointer wraps column → next page inside the window — [SSD1306 datasheet §8.7, §10.1](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf)
- Zephyr's SSD1306 driver already writes arbitrary `(x, y, desc)` windows with `SET_MEM_ADDRESSING_MODE`, `SET_COLUMN_ADDRESS` and `SET_PAGE_ADDRESS`, then a data burst — [display_ssd1306.c](https://github.com/zephyrproject-rtos/zephyr/blob/main/drivers/display/display_ssd1306.c)
- ThingPulse's double-buffer mode diffs the current buffer against the previous one and sends one bounding box, or nothing — [SSD1306Wire.h](https://github.com/ThingPulse/esp8266-oled-ssd1306/blob/master/src/SSD1306Wire.h)
- U8g2's `updateDisplayArea()` sends a tile-aligned (8×8) rectangle — [u8g2reference](https://github.com/olikraus/u8g2/wiki/u8g2reference)
- **ESP-IDF I2C master:**
  - `i2c_master_multi_buffer_transmit()` sends several buffers (e.g. control byte, commands, pixel data) as one transaction without concatenation.
  - Async (non-blocking) operation comes from registering `i2c_master_register_event_callbacks()` and setting `trans_queue_depth`. "On the same bus, only one device can adopt asynchronous operation", and async is "still an experimental feature" (memory problems with very large transfers).
  - The driver takes `ESP_PM_APB_FREQ_MAX` during transfers when it is clocked from APB. With `I2C_CLK_SRC_XTAL` it installs no PM lock ("more suitable for a low power application").
  - `allow_pd` lets the peripheral power down in light sleep with context backup.
  — [ESP-IDF i2c.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/i2c.rst)
- **ESP-IDF SPI master:** "transparent handling of DMA transfers". Interrupt transactions block the caller "allowing the CPU to run other tasks" (queued with `spi_device_queue_trans`). Polling transactions avoid ISR overhead for short transfers — [spi_master.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/spi_master.rst)
- **RP2040:** I2C TX can be DMA-paced via `I2C_DREQ_NUM(i2c, true)` — [pico-sdk hardware/i2c.h](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_i2c/include/hardware/i2c.h)
- **nRF52:** TWIM is "I2C compatible two-wire interface controller with EasyDMA" — [Nordic TWIM doc](https://docs.nordicsemi.com/r/bundle/ps_nrf52810/page/twim.html)
- ESP-IDF auto light sleep is timed from blocked tasks and esp_timers, and `skip_unhandled_events` timers don't wake the chip — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- RoboEyes paces frames with `if (millis() - fpsTimer >= frameInterval)` and defaults to `frameInterval = 20` ms (50 fps) — [FluxGarage_RoboEyes.h](https://github.com/FluxGarage/RoboEyes/blob/main/src/FluxGarage_RoboEyes.h)

### Inferences
- **Per-page dirty spans beat one bounding box** for two separated eyes. For example, if only the left eye changed, the box shrinks to one eye. If both eyes changed, two windows avoid sending the gap between them. Each window costs ~8 command bytes over I2C (control byte + 6 command bytes), so merge spans whose gap is under ~8 columns.
- **Reference flush (ESP-IDF, I2C), sketch:**

```cpp
// fb[8][128] = what we drew; shadow[8][128] = what the panel shows
struct Span { uint8_t page, x0, x1; };
static int diff_spans(const uint8_t fb[8][128], uint8_t sh[8][128], Span* out) {
  int n = 0;
  for (uint8_t p = 0; p < 8; ++p) {
    int x0 = -1, x1 = -1;
    for (int x = 0; x < 128; ++x) if (fb[p][x] != sh[p][x]) { if (x0 < 0) x0 = x; x1 = x; }
    if (x0 >= 0) { out[n++] = {p, (uint8_t)x0, (uint8_t)x1}; memcpy(&sh[p][x0], &fb[p][x0], x1 - x0 + 1); }
  }
  return n;   // optionally merge vertically-adjacent spans with similar x-range into one 0x22 window
}
static void send_span(i2c_master_dev_handle_t dev, const uint8_t fb[8][128], Span s) {
  const uint8_t cmd[] = {0x00, 0x21, s.x0, s.x1, 0x22, s.page, s.page};   // Co=0,D/C#=0 then 6 cmd bytes
  i2c_master_transmit(dev, cmd, sizeof cmd, 10);
  static const uint8_t ctl = 0x40;                                       // Co=0, D/C#=1 → data
  i2c_master_transmit_multi_buffer_info_t b[2] = {{(uint8_t*)&ctl, 1}, {(uint8_t*)&fb[s.page][s.x0], (size_t)(s.x1 - s.x0 + 1)}};
  i2c_master_multi_buffer_transmit(dev, b, 2, 20);                        // zero-copy from framebuffer
}
```

- **Double buffering:** with async/DMA flush, render frame N+1 into buffer B while A is in flight. Two 1 KB buffers plus a shadow = 3 KB RAM, trivial on every candidate MCU. With a blocking flush, a single buffer plus shadow is enough.
- **Frame pacing pattern:** a variable-rate loop where *time* drives animation:

```cpp
void render_task(void*) {
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    uint32_t now = esp_timer_get_time() / 1000;
    bool active = face.update(now);               // tweens use absolute ms; returns true while anything moves
    if (face.changed()) { face.draw(fb); flush_dirty(fb); }
    if (active) {
      vTaskDelayUntil(&wake, pdMS_TO_TICKS(1000 / FPS_ACTIVE));   // e.g. 25–30 fps during motion
    } else {                                      // idle: sleep until next blink/saccade or an event
      uint32_t ms = face.ms_to_next_scheduled_event(now);          // e.g. 2000–6000 ms
      ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ms)); // sensor/BLE/behavior task notifies to wake early
      wake = xTaskGetTickCount();
    }
  }
}
```

  With tickless idle + `light_sleep_enable`, the idle branch becomes automatic light sleep for the whole wait.
- **Make sure no sleep happens mid-transfer:** with APB-clocked I2C the driver's lock prevents light sleep during the transaction. If you switch to the XTAL clock source to avoid the lock, take an explicit `ESP_PM_NO_LIGHT_SLEEP` lock around each frame flush, or check this behaviour first (see Gaps).
- **Cheap visual tricks that cost no frames:**
  - contrast (0x81) to dim for "sleepy" or night;
  - display OFF (0xAE, ≤10 µA controller sleep current) for real sleep;
  - optionally hardware scroll for drifting zzz/ticker effects.

### Gaps
- Not verified: whether ESP-IDF's new I2C master driver prevents automatic light sleep during a blocking transfer when `I2C_CLK_SRC_XTAL` is selected and no PM lock exists.
- The I2C FIFO size and whether ESP32-C3 I2C uses DMA (it appears to be FIFO+ISR only) were not confirmed.
- No measurement was found of the CPU cost of diffing 1 KB per frame on ESP32-C3. My estimate: a few µs at 160 MHz, negligible next to the bus time.

---

## Q4. Animation engine architecture: procedural/parametric face vs bitmap sequences vs hybrid; existing implementations; fixed-point math

### Takeaway
Use the **hybrid** approach. A procedural, parametric face (two eyes as rounded rects or ellipses plus eyelid masks) is tweened between small keyframe "expression" presets with easing; this covers blink, look-around, surprised, sleepy, angry, happy and squint. 1-bpp sprite overlays are composited on top: tears, hearts, zzz, sound waves, sweat, dizzy spirals. All the open-source eye libraries are procedural. Anki's Cozmo/Vector ProceduralFace is the reference parameter set. On ESP32-C3/C6 (no FPU), do the tween math in fixed point with easing lookup tables.

### Cited Findings
**FluxGarage RoboEyes** (header V1.1.1; library.properties 1.1.2; GPL-3.0; Adafruit GFX based)
- API: `begin(w, h, fps)`, `update()` (frame-limited), `setWidth/Height/Borderradius/Spacebetween`, `setMood(DEFAULT|TIRED|ANGRY|HAPPY)`, `setPosition(N…NW)`, `setCuriosity`, `setCyclops`, `setH/VFlicker`, `setSweat`, `open/close/blink`, `anim_confused/anim_laugh`, `setAutoblinker(on, interval, variation)`, `setIdleMode(on, interval, variation)` — [RoboEyes README](https://github.com/FluxGarage/RoboEyes), [library.properties](https://github.com/FluxGarage/RoboEyes/blob/main/library.properties)
- Drawing: eyes are `fillRoundRect`. Tired/angry eyelids are `fillTriangle` drawn in the *background* colour over the eye. Happy is a background-coloured `fillRoundRect` covering the lower part of the eye — [FluxGarage_RoboEyes.h](https://github.com/FluxGarage/RoboEyes/blob/main/src/FluxGarage_RoboEyes.h)
- Tweening is per-frame halving, e.g. `eyeLheightCurrent = (eyeLheightCurrent + eyeLheightNext + offset)/2` and `eyeLx = (eyeLx + eyeLxNext)/2`. That is exponential smoothing tied to frame rate — [FluxGarage_RoboEyes.h](https://github.com/FluxGarage/RoboEyes/blob/main/src/FluxGarage_RoboEyes.h)
- Defaults: 50 fps. Autoblink every `1 s + random(0..4) s`. Idle repositioning every `1 s + random(0..3) s` to `random(screenConstraint)`. Laugh and confused last 500 ms — [FluxGarage_RoboEyes.h](https://github.com/FluxGarage/RoboEyes/blob/main/src/FluxGarage_RoboEyes.h)

**playfultechnology/esp32-eyes** (U8g2, based on Luis Llamas' ESP32_Faces; source headers carry **AGPL-3.0**)
- Structure: `Face` = LeftEye + RightEye + Behavior + Expression + LookAssistant + BlinkAssistant, chained each frame. There are 18 emotions: Normal, Angry, Glee, Happy, Sad, Worried, Focused, Annoyed, Surprised, Skeptic, Frustrated, Unimpressed, Sleepy, Suspicious, Squint, Furious, Scared, Awe. Behaviour holds weights and does **roulette-wheel random selection**. Emotions are not blended; one is shown at a time — [esp32-eyes README](https://github.com/playfultechnology/esp32-eyes), [FaceEmotions.hpp](https://github.com/playfultechnology/esp32-eyes/blob/main/FaceEmotions.hpp)
- Per-eye parameters (`EyeConfig`): OffsetX/Y, Height, Width, Slope_Top/Bottom (float), Radius_Top/Bottom, Inverse_Radius_Top/Bottom, Inverse_Offset_Top/Bottom. Presets are const structs, e.g. `Preset_Glee {Height 8, Width 40, Radius_Top 8, Inverse_Radius_Bottom 5…}` — [EyeConfig.h](https://github.com/playfultechnology/esp32-eyes/blob/main/EyeConfig.h), [EyePresets.h](https://github.com/playfultechnology/esp32-eyes/blob/main/EyePresets.h)
- The transition is a 500 ms `RampAnimation` returning float t. It applies `Origin = Origin*(1-t) + Destin*t` to every field — [EyeTransition.cpp](https://github.com/playfultechnology/esp32-eyes/blob/main/EyeTransition.cpp), [Animations.h](https://github.com/playfultechnology/esp32-eyes/blob/main/Animations.h)
- Per-eye operator chain: `Config → Transformation → Variation1 → Variation2 → BlinkTransformation → FinalConfig`. Variations are 800 ms multi-segment animations — [Eye.cpp](https://github.com/playfultechnology/esp32-eyes/blob/main/Eye.cpp). The blink timer defaults to 3500 ms and the look timer to 4000 ms. `Face::Draw()` does `clearBuffer()` → draw eyes → `sendBuffer()` (full frame every update) — [BlinkAssistant.cpp](https://github.com/playfultechnology/esp32-eyes/blob/main/BlinkAssistant.cpp), [LookAssistant.cpp](https://github.com/playfultechnology/esp32-eyes/blob/main/LookAssistant.cpp), [Face.cpp](https://github.com/playfultechnology/esp32-eyes/blob/main/Face.cpp)
- `LookAt(x, y)` takes x, y in −1.0…1.0 — [esp32-eyes README](https://github.com/playfultechnology/esp32-eyes)

**intellar/oled_eye_display** (the brief's "intellar" reference; the repo is `oled_eye_display`, not "esp32-eyes")
- An Arduino Nano driving animated eyes, "listening to command over usb connection", with a Python host example. It supports U8g2 (`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`) and Adafruit_SSD1306 through a `display_wrapper.h` abstraction — [intellar/oled_eye_display README](https://github.com/intellar/oled_eye_display), [display_wrapper.h](https://github.com/intellar/oled_eye_display/blob/main/control_display/display_wrapper.h)
- Animations (`blink`, `sleep`, `wakeup`, `happy_eye`, `saccade(dx, dy)`, `move_big_eye`) are blocking loops with `delay(1)`/`delay(1000)`. Eyes are rounded boxes, with a `calculate_safe_radius()` helper for U8g2 `drawRBox` constraints — [control_display.ino](https://github.com/intellar/oled_eye_display/blob/main/control_display/control_display.ino)

**Anki Cozmo/Vector ProceduralFace** (open-sourced by Digital Dream Labs)
- Per-eye parameter array (`ProceduralEyeParameter`):
  - geometry: EyeCenterX/Y, EyeScaleX/Y, EyeAngle;
  - corner radii: Upper/Lower × Inner/Outer RadiusX/Y (8 params);
  - lids: UpperLidY, UpperLidAngle, UpperLidBend, LowerLidY, LowerLidAngle, LowerLidBend;
  - colour/glow: Saturation, Lightness, GlowSize, GlowLightness, HotSpotCenterX/Y.

  Face-level parameters are face angle, face center X/Y and face scale X/Y — [proceduralFace.cpp](https://github.com/digital-dream-labs/vector/blob/main/cannedAnimLib/proceduralFace/proceduralFace.cpp), [proceduralFace.h](https://github.com/digital-dream-labs/vector/blob/main/cannedAnimLib/proceduralFace/proceduralFace.h)
- `Interpolate(face1, face2, blendFraction)` blends every parameter of both eyes. Params flagged `isAngle` use an angle-aware blend, and blendFraction ≈0 or ≈1 short-circuits to a copy. `LookAt(x, y, …, lookUpMaxScale = 1.1, lookDownMinScale = 0.85, outerEyeScaleIncrease = 0.1)` makes eyes bigger looking up, smaller looking down, and the outer eye taller (a perspective cue). `Combine()` composes layered faces. There is optional scanline distortion — [proceduralFace.h](https://github.com/digital-dream-labs/vector/blob/main/cannedAnimLib/proceduralFace/proceduralFace.h), [proceduralFace.cpp](https://github.com/digital-dream-labs/vector/blob/main/cannedAnimLib/proceduralFace/proceduralFace.cpp)

**FPU / math cost**
- ESP-IDF speed guide, for chips without FPU: "Avoid using floating point arithmetic float. On {chip} these calculations are emulated in software and are very slow. If possible, use fixed point representations". `double` is always emulated — [ESP-IDF speed.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/performance/speed.rst)
- `SOC_CPU_HAS_FPU` is defined for ESP32-S3 but not for ESP32-C3 or ESP32-C6 — [esp32s3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/include/soc/soc_caps.h), [esp32c3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c3/include/soc/soc_caps.h), [esp32c6 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c6/include/soc/soc_caps.h)

### Inferences
- **Parameter set to adopt.** A trimmed Vector-style set fits 1-bpp 128×64 and keeps keyframes tiny:

```cpp
struct Eye {            // all integers; positions in 1/16 px (Q4) for sub-pixel smooth motion
  int16_t cx, cy;       // center (Q4)
  uint8_t w, h;         // size px
  uint8_t r_top, r_bot; // corner radii (rounded rect → ellipse when r = min(w,h)/2)
  int8_t  lid_top_y, lid_bot_y;      // eyelid coverage, 0..h
  int8_t  lid_top_ang, lid_bot_ang;  // eyelid slope (angry/sad), small int, e.g. ±32 = ±45°
  int8_t  pupil_dx, pupil_dy;        // optional pupil/highlight (black dot inside white eye)
};
struct Face { Eye L, R; int8_t dx, dy; uint8_t scale_q7; uint8_t overlay_id, overlay_phase; };
// one expression keyframe ≈ 2×14 + 5 ≈ 33 bytes in flash → 30 expressions ≈ 1 KB (≈ one bitmap frame)
```

- **Rendering primitives (integer only):**
  - Rounded rect = span fill per row. Precompute a quarter-circle profile per radius (LUT of ≤32 entries), or reuse U8g2 `drawRBox` / GFX `fillRoundRect`.
  - Lids = clip each row's span against a line `y = y0 + slope·(x−x0)`, i.e. a half-plane mask, instead of drawing background triangles. That is cheaper and composes better.
  - "Happy" (^ ^) = subtract an offset ellipse or rounded rect from the bottom, as RoboEyes does.
  - Mirror the right eye from the left, as esp32-eyes does with `IsMirrored`.
- **Tweening done right:**
  - (a) Time-based tweens: `t = (now − t0) * 1024 / dur` in Q10, eased via LUT, then integer lerp.
  - (b) For "chasing" behaviour such as the RoboEyes halving, use a frame-rate-independent exponential `x += (target − x) * k(dt)` with `k(dt) = 1 − 2^(−dt/half_life)` from a small LUT indexed by dt in ms. RoboEyes' `(cur + next)/2` per frame changes speed when FPS changes, and that matters once FPS varies adaptively.
  - (c) Per-parameter tracks (blink overrides lid_y, look overrides cx/cy, expression drives the shape), combined additively or by priority. This is the same layering idea as esp32-eyes' operator chain and Vector's `Combine()`.

```cpp
// Q10 easing LUT (65 entries, ease-in-out cubic), linear interp between entries
static const uint16_t EASE_IO[65] = { /* generated offline: round(1024*easeInOutCubic(i/64.0)) */ };
static inline uint16_t ease_q10(uint16_t t) {           // t: 0..1024
  uint16_t i = t >> 4, f = t & 15;                        // 64 segments
  return EASE_IO[i] + (((EASE_IO[i+1] - EASE_IO[i]) * f) >> 4);
}
static inline int16_t lerp_q10(int16_t a, int16_t b, uint16_t t) { return a + (int16_t)(((int32_t)(b - a) * t) >> 10); }
// Keep 1–2 extra LUTs: ease-out-back (overshoot for "surprised" pop), and a blink curve (fast close, slower open)
```

- **Idle "aliveness" generators:**
  - blink scheduler: next = now + 2000..6000 ms jittered, with occasional double-blink;
  - saccade scheduler: small random gaze jumps of 150–250 ms tween, then hold;
  - micro-drift (±1 px slow noise);
  - breathing (±1 px height sine at 0.2 Hz from a LUT).

  RoboEyes' and esp32-eyes' defaults (1+0..4 s blink; 3.5 s blink / 4 s look) are useful starting points. The animation-design researcher owns the exact timings.
- **Overlays (hybrid):**
  - store sprites as 1-bpp with a mask;
  - blit at byte-aligned y (page-aligned) when possible, which is a pure OR/AND-NOT on bytes;
  - otherwise do shifted blits across two pages.

  Sprite animation = a few small frames (e.g. heart 16×16 = 32 B/frame) or procedural motion such as rising zzz on a path.
- **Licensing for a commercial product:** RoboEyes is GPL-3.0 and esp32-eyes code is AGPL-3.0, so copying their code into closed firmware is not allowed. Re-implement from the ideas (parameter sets, layering), which are not copyrightable. Vector's code licence was not checked (Gap).
- **Bitmap sequences** (prerecorded frames) remain useful for a few "hero" animations that are hard to do procedurally, such as laughing with a mouth, dizzy spirals, or a stylised heart-eyes morph. Store them as XOR-delta + RLE (Q5) and play them through the same dirty-span flush.

### Gaps
- The Vector parameter default values and the full `kEyeParamInfoLUT` ranges were not extracted. The ranges would help calibrate a 128×64 mapping.
- Not verified: Cozmo's OLED (128×64, 1-bpp with interlaced scanlines) and how Anki adapted ProceduralFace to monochrome.
- The Vector/victor source licence terms were not checked.

---

## Q5. Storing many animations: flash budget, bitmap formats, compression, placement, filesystems, downloadable packs

### Takeaway
Procedural expressions cost bytes, not kilobytes. Only overlays and "hero" bitmap clips need real storage. Store clips in **SSD1306-native page format**, compressed as **XOR-delta vs previous frame + RLE** (or heatshrink for keyframes). The decoder then yields dirty spans for free. Keep built-in assets as `const` data in flash (DROM/XIP). Put downloadable packs in a LittleFS partition with a small versioned container format.

### Cited Findings
- On ESP-IDF, constant data is placed by default in DROM (flash mapped through the MMU cache), and cached flash access "is as fast as accessing other types of internal memory" — [ESP-IDF memory-types.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/memory-types.rst)
- Frame formats:
  - SSD1306 GDDRAM is page-major vertical bytes — [SSD1306 datasheet §8.7](https://github.com/rm-hull/luma.oled/blob/main/doc/tech-spec/SSD1306.pdf);
  - MicroPython calls this `MONO_VLSB` — [micropython-lib ssd1306.py](https://github.com/micropython/micropython-lib/blob/master/micropython/drivers/display/ssd1306/ssd1306.py);
  - LVGL I1 is row-major MSB-first and needs `lv_draw_sw_i1_convert_to_vtiled()` for SSD1306 — [LVGL color_format.rst](https://github.com/lvgl/lvgl/blob/release/v9.3/docs/src/details/main-modules/display/color_format.rst);
  - U8g2 `drawXBM/drawXBMP` takes XBM 1-bit bitmaps (GIMP can export XBM), and since 2.15 draws them solid by default (`setBitmapMode(1)` for transparent) — [u8g2reference](https://github.com/olikraus/u8g2/wiki/u8g2reference).
- heatshrink: LZSS-based compression for embedded/real-time use. "Low memory usage (as low as 50 bytes)", "useful for many general cases with < 300 bytes". "Incremental, bounded CPU use". Static or dynamic allocation (`HEATSHRINK_DYNAMIC_ALLOC 0`). `window_sz2` between 4 and 15 (2^8 = 256 B, 2^10 = 1024 B). ISC licence — [heatshrink README](https://github.com/atomicobject/heatshrink)
- littlefs: "Power-loss resilience", "Dynamic wear leveling", "Bounded RAM/ROM" (RAM use does not grow with the filesystem) — [littlefs README](https://github.com/littlefs-project/littlefs). esp_littlefs was ported "because SPIFFS was too slow" and is used like the IDF SPIFFS API — [joltwallet/esp_littlefs README](https://github.com/joltwallet/esp_littlefs)
- NVS is for "a moderate, relatively stable set of small values" and not for "frequently rewritten datasets" (wear, fragmentation). For large blobs use a filesystem — [ESP-IDF nvs_flash.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/storage/nvs_flash.rst)
- Zephyr MCUmgr includes "File system management" and "Image management" groups, i.e. it can upload files and firmware over SMP transports — [zephyr mcumgr.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/device_mgmt/mcumgr.rst)
- ESP-IDF OTA needs ota_0/ota_1 + otadata partitions (Q9). Custom data partitions (e.g. a `graphics, data, spiffs` row) are declared in partitions.csv, as in the esp_littlefs README example — [ESP-IDF ota.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/ota.rst), [esp_littlefs README](https://github.com/joltwallet/esp_littlefs)

### Inferences
- **Budget arithmetic (mine):**
  - one raw full frame = 1024 B; a 2-second clip at 20 fps = 40 KB raw;
  - 50 such clips = 2 MB raw, which does not fit next to two OTA app slots in a 4 MB flash;
  - procedural expressions: ~33 B per keyframe (Q4), so a full emotion library is under 2 KB;
  - an overlay sprite set (hearts, tears, zzz, notes, sweat, spiral, "!"/"?" marks; ~10 sprites × 4 frames × 32–128 B) is ~2–5 KB.
- **Delta + RLE codec (fast, tiny, synergistic with dirty flush):**

```
Clip file:  "EMO1" | u8 version | u8 w_pages | u8 h_cols | u16 n_frames | u16 fps | u32 frame_offset[n] | frames…
Frame:      u8 type (K=keyframe, D=delta) | payload
Payload D:  sequence of (u16 skip, u8 run_len, run_len bytes XOR-mask) in page-major order — skipped bytes unchanged
Decode:     for each run: fb[pos..] ^= data;  mark span dirty  → feed directly to flush_dirty()
Keyframe K: heatshrink(window 2^8, lookahead 2^4) of full 1 KB page buffer (or raw if larger)
```

  Because consecutive eye frames differ in few bytes, delta payloads are typically a small fraction of 1 KB. That is an expectation, not measured here; benchmark on real clips. The run list also *is* the dirty-region list, so no separate diff pass is needed.
- **Placement:**
  - built-in assets: `static const uint8_t clip_laugh[] = {…}` (DROM on ESP32; `const` lands in flash on nRF/RP2040/STM32 as well; `PROGMEM` only matters on AVR);
  - downloadable packs: files on a LittleFS partition (`/packs/<name>.emo`), with a manifest (JSON or CBOR, or a fixed binary header) listing clip IDs mapped to emotion triggers;
  - verify a CRC32 or SHA-256 per file before activating it.
- **Pack transfer:**
  - nRF/Zephyr: the MCUmgr FS management group over BLE SMP (already supported);
  - ESP32: a custom GATT "asset upload" characteristic (write-without-response chunks, offset + CRC). Write to a temp file, then rename (atomic on littlefs).
  - Over USB: a CDC/serial protocol or USB MSC (CircuitPython-style) during development.
- Avoid NVS for anything over a few hundred bytes, or anything written often (Q6).

### Gaps
- No measured compression ratios for XOR+RLE vs heatshrink vs LZ4 on real 128×64 eye clips were found. LZ4/QOI-like options were not researched (search budget).
- Typical flash sizes of the chosen module are a hardware question; this assumes 4 MB.

---

## Q6. Behavior engine: FSM/HSM vs behavior trees vs utility AI; mood/emotion model; idle randomness; sensor events; persistence across sleep

### Takeaway
Use a **small hierarchical state machine (HSM)** for device modes (Awake/Idle, Reacting, Listening, Drowsy, Asleep, BLE-session). Add a **mood vector** that decays over time and takes impulses from sensor events. Use **weighted-random (utility/roulette) selection** of idle behaviors and expressions from the current mood. Full behavior trees are overkill on this hardware. Keep the mood live in RAM, mirror it into RTC/retention memory before deep sleep, and write it to flash rarely (NVS/settings/littlefs), because NVS is not meant for frequent rewrites.

### Cited Findings
- esp32-eyes "Behaviour" keeps an array of weighted emotions and picks the next one by "roulette-wheel selection" at a configurable interval. The same emotion can be re-picked, and weights are selection probabilities, not blend weights — [esp32-eyes README](https://github.com/playfultechnology/esp32-eyes)
- Anki Vector's emotion dimensions: `EmotionType { Happy, Confident, Social, Stimulated, Trust }` — [vector clad/types/emotionTypes.clad](https://github.com/digital-dream-labs/vector/blob/main/clad/src/clad/types/emotionTypes.clad)
- RoboEyes idle randomness: blink every `blinkInterval + random(variation)` seconds, gaze repositioning every `idleInterval + random(variation)` s — [FluxGarage_RoboEyes.h](https://github.com/FluxGarage/RoboEyes/blob/main/src/FluxGarage_RoboEyes.h)
- The intellar project triggers animations by index from serial commands (`launch_animation_with_index()` switch: WAKEUP, RESET, MOVE_RIGHT_BIG, BLINK_LONG, HAPPY, SLEEP, SACCADE_RANDOM…) with blocking playback — [control_display.ino](https://github.com/intellar/oled_eye_display/blob/main/control_display/control_display.ino)
- **ESP32 persistence:**
  - `RTC_NOINIT_ATTR` places data in RTC slow memory that "keep their value after waking from deep sleep" — [ESP-IDF memory-types.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/memory-types.rst);
  - ESP32-C3 has only RTC *fast* memory (`SOC_RTC_FAST_MEM_SUPPORTED`, no `SOC_RTC_SLOW_MEM_SUPPORTED`), while S3 has both — [esp32c3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c3/include/soc/soc_caps.h), [esp32s3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/include/soc/soc_caps.h).
- NVS can survive power-off at any point (only the in-flight write may be lost). On weak or unstable supplies, such as batteries, failed erases can exhaust pages (`ESP_ERR_NVS_NO_FREE_PAGES`). It is not for frequently rewritten data — [ESP-IDF nvs_flash.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/storage/nvs_flash.rst)
- MicroPython lightsleep keeps full RAM and state. Deepsleep may not — [machine.rst](https://github.com/micropython/micropython/blob/master/docs/library/machine.rst). CircuitPython has `alarm.sleep_memory` for deep sleep — [alarm/__init__.c](https://github.com/adafruit/circuitpython/blob/main/shared-bindings/alarm/__init__.c)

### Inferences
- **Mood model (compact, integer):**

```cpp
struct Mood {               // all int8 in [-127..127] or uint8 [0..255]
  int8_t valence;           // unhappy ↔ happy
  int8_t arousal;           // sleepy ↔ excited
  uint8_t energy;           // drains while awake/animating, refills while asleep/charging
  uint8_t boredom;          // rises with no interaction, drops on events
  uint8_t affection;        // rises with petting/BLE "hello", slow decay
};
// decay toward baseline every behavior tick (e.g. 1 Hz): v += (base - v) >> 5;
// event impulses (table-driven):
//   TAP        → arousal +40, boredom -30           → expression SURPRISED (short) then CURIOUS
//   DOUBLE_TAP → valence +20, affection +10         → HAPPY/LAUGH
//   SHAKE      → arousal +60, valence -10           → DIZZY (spiral overlay)
//   LOUD_SOUND → arousal +50                        → STARTLED; repeated speech-like energy → LISTENING (sound-wave overlay)
//   DARK (lux< t, >N s) → arousal -30               → DROWSY → ASLEEP (zzz, display dim/off)
//   PET (touch held) → valence +30, affection +20   → LOVE (heart overlay)
//   BLE notify  → arousal +20                        → ALERT/“message” glyph
```

  Vector's five dimensions (Happy, Confident, Social, Stimulated, Trust) are a richer alternative. A 2D valence/arousal plus 2–3 Tamagotchi-style needs is enough to choose among ~15 expressions.
- **Selection = utility + randomness:** each idle behavior (look around, blink-double, yawn, hum/"listening" glance, sigh, bored eye-roll, sleepy nod) has `score = base_weight × f(mood)`. Pick by roulette wheel, as esp32-eyes does, with a cooldown per behavior so the same thing doesn't repeat. Expression from mood: nearest prototype in (valence, arousal) space, with hysteresis so it doesn't flicker between neighbours.
- **HSM skeleton:**

```
Root
 ├─ Awake
 │   ├─ Idle        (procedural idle gen; render only during micro-animations; deep idle → auto light sleep)
 │   ├─ Reacting    (one-shot reaction clip/tween; returns to Idle)
 │   └─ Listening   (mic envelope stream; sound-wave overlay)
 ├─ Drowsy          (slower blinks, half-lids, dim contrast; timer → Asleep)
 ├─ Asleep          (display OFF/very dim; MCU deep sleep or long light sleep; wake on IMU INT / button)
 └─ Session         (BLE connected: notifications, pack upload, OTA; overlays Awake)
```

  Event-driven: ISR → event queue → HSM dispatch. Timers are events too, so nothing polls.
- **Persistence policy:**
  - live mood in RAM;
  - before deep sleep, copy it to `RTC_DATA_ATTR`/`RTC_NOINIT_ATTR` (ESP32) or a `.noinit`/retained RAM section (nRF/STM32), with a checksum and version;
  - write to flash (NVS / Zephyr settings / littlefs) only on significant change, e.g. at most every 30–60 min, at "bedtime", or on low battery. That respects NVS guidance and limits wear and the risk of failed erases.
  - On cold boot, restore from flash and apply "time away" decay computed from an RTC timestamp, if time is known (Q9 time sync).
- A behavior tree adds node/tick machinery that pays off only with many conditional multi-step behaviors. For ~10 behaviors, an HSM plus a utility table is smaller and easier to tune and test on the desktop (Q10).

### Gaps
- No primary source was fetched for the valence-arousal (circumplex) model or for Tamagotchi need mechanics. Those statements are design inference.
- Not verified: nRF52840 RAM retention in System OFF (retained RAM sections) and its specifics in Zephyr (`retained_mem`/`retention` drivers).
- Not retrieved: Vector's mood decay and event config (moodManager); the file path was not found.

---

## Q7. RTOS usage: FreeRTOS tasks/queues vs super-loop; power impact

### Takeaway
On ESP-IDF or Zephyr, use **3–4 event-driven tasks**: input/sensors (ISR-deferred), behavior (event and timer driven), render (active only while something animates), plus the BLE stack's own tasks. Each one blocks on a queue, notification or event group with a finite timeout, never busy-waits or `delay()`-loops. Tickless idle then turns all waiting time into light sleep. A cooperative super-loop is fine under Arduino or bare-metal, but only if it ends each iteration with a real sleep call (`esp_light_sleep_start`, `__WFI`, Embassy await).

### Cited Findings
- ESP-IDF automatic light sleep duration is set by tasks "blocked with finite timeouts" and esp_timers. `skip_unhandled_events` timers avoid wakeups. Locks held by drivers (I2C/SPI during a transaction, GPTimer while enabled, BT) block light sleep — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst)
- `FREERTOS_USE_TICKLESS_IDLE`: "FreeRTOS will be able to put the system into light sleep mode when no tasks need to run for a number of ticks", set by `FREERTOS_IDLE_TIME_BEFORE_SLEEP`. esp_timer timers may prevent sleep — [freertos/Kconfig](https://github.com/espressif/esp-idf/blob/master/components/freertos/Kconfig)
- The I2C master driver is thread-safe through a bus semaphore. ISR callbacks must use only `…FromISR` APIs. `CONFIG_I2C_ISR_IRAM_SAFE` keeps the ISR working while cache is disabled (flash writes) — [i2c.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/i2c.rst)
- The Arduino-ESP32 builds use `CONFIG_FREERTOS_HZ=1000` with no tickless idle configured — [lib-builder defconfig.common](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.common)
- Zephyr: tickless kernel by default on capable SoCs, and the idle thread enters PM states by residency policy — [kernel/Kconfig](https://github.com/zephyrproject-rtos/zephyr/blob/main/kernel/Kconfig), [pm/system.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/pm/system.rst)
- Embassy: cooperative async tasks on one stack. The executor sleeps the core when idle. Multiple executors at different priorities give pre-emption — [embassy README](https://github.com/embassy-rs/embassy/blob/main/README.md)
- Blocking `delay()`-loop animation (intellar) ties up the CPU for the whole animation, with `delay(1000)` holds — [control_display.ino](https://github.com/intellar/oled_eye_display/blob/main/control_display/control_display.ino)

### Inferences
- **Task layout (ESP-IDF):**

| Task | Prio | Stack | Wakes on | Notes |
|---|---|---|---|---|
| `input` | high | 2–3 KB | GPIO ISR (IMU INT1/2, button, touch) → `xTaskNotifyFromISR` | reads IMU source regs over I2C, debounces, posts `Event{type, ts}` to behavior queue |
| `audio` (optional) | mid | 3–4 KB | I2S DMA buffer ready | runs only in Listening state; computes envelope; posts LOUD/VOICE events; otherwise I2S disabled |
| `behavior` | mid | 3 KB | event queue or next mood tick (1 Hz) | HSM + mood; sends `FaceCmd` (target expression, overlay, one-shot) to render |
| `render` | low-mid | 3 KB | FaceCmd / frame period while active | tween + draw + dirty flush; idles with timeout = time to next micro-event |
| BLE host (NimBLE) | system | — | radio | GATT handlers post to behavior queue |

- Use one I2C bus mutex. The driver provides it, but OLED flushes and IMU reads interleave. Keep OLED flushes in small windows so IMU reads aren't starved, and remember only one device per bus can use async mode.
- Prefer task notifications or event groups for "wake now" signals and queues for typed events. Avoid periodic esp_timers that don't do useful work, or mark them `skip_unhandled_events`.
- **Super-loop alternative (Arduino):** `loop() { handle_events(); if (face.update(millis())) flush(); sleep_until(next_deadline); }`, with `esp_sleep_enable_timer_wakeup()` + `esp_sleep_enable_gpio_wakeup()` + `esp_light_sleep_start()` on idle. That is functional, but every peripheral must be re-checked after a manual light sleep, and BLE usually rules manual light sleep out.

### Gaps
- No measured comparison of idle current, tickless vs 1 kHz tick, on ESP32-C3 was found. That belongs to the power-budget researcher.

---

## Q8. Sensor processing at minimal power: IMU interrupts and FIFO; microphone envelope/VAD/clap; light sensor; touch

### Takeaway
Let the sensors do the detecting. Configure the accelerometer's built-in **single/double-tap, wake-up (shake), 6D/orientation and free-fall** detectors and route them to INT pins wired to MCU wake-capable GPIOs. The MCU reads the source register only after an interrupt. Run the microphone only in specific states. On ESP32-C3 it gives raw PDM only (no hardware PDM→PCM), so prefer an analog/I2S MEMS mic or keep sample rates and windows small. On ESP32-S3 the hardware PDM-to-PCM filter is available, and nRF52 has a dedicated PDM peripheral with hardware decimation.

### Cited Findings
- ST's LIS2DW12 single-tap reference config: full scale ±2 g, `CONT_LOW_PWR_LOW_NOISE_12bit` power mode, ODR 400 Hz, tap on X/Y/Z, threshold 9 on each axis, quiet = 1, shock = 2, `ONLY_SINGLE` mode, INT1 routed to `int1_single_tap`. A separate wake-up (motion) example exists — [STMems lis2dw12_tap_single.c](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lis2dw12_STdC/examples/lis2dw12_tap_single.c), [lis2dw12_wake_up.c](https://github.com/STMicroelectronics/STMems_Standard_C_drivers/blob/master/lis2dw12_STdC/examples/lis2dw12_wake_up.c)
- **ESP32 I2S PDM:**
  - with a PDM-to-PCM converter, "Only the RX direction of I2S0 can convert received PDM data to PCM";
  - without one, "it can only read raw PDM data. Convert the data in software";
  - the PDM bit-clock is normally 1.024–6.144 MHz, and the data unit is always 16-bit.

  — [ESP-IDF i2s.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/peripherals/i2s.rst)
- `SOC_I2S_SUPPORTS_PDM_RX` is defined for ESP32-C3, C6 and S3, but `SOC_I2S_SUPPORTS_PDM2PCM` only for S3 among these — [esp32c3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c3/include/soc/soc_caps.h), [esp32c6 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c6/include/soc/soc_caps.h), [esp32s3 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32s3/include/soc/soc_caps.h)
- nRF PDM peripheral has configurable decimation ratio (`NRF_PDM_HAS_RATIO_CONFIG`, ratio 32/64 symbols) — [nrfx hal/nrf_pdm.h](https://github.com/NordicSemiconductor/nrfx/blob/master/hal/nrf_pdm.h). Zephyr's DMIC API delivers PCM after filtering and decimation (`dmic_configure`, `dmic_read`) — [zephyr dmic.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/hardware/peripherals/audio/dmic.rst)
- ESP32-C6 LP core: "can access the Low-power (LP) SRAM and peripherals when the HP system is in sleep mode", including LP I2C, and can wake the main CPU — [ESP-IDF ulp-lp-core.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/ulp-lp-core.rst). ESP32-C6 has `SOC_LP_CORE_SUPPORTED` and `SOC_LP_I2C_SUPPORTED` — [esp32c6 soc_caps.h](https://github.com/espressif/esp-idf/blob/master/components/soc/esp32c6/include/soc/soc_caps.h)
- Light-sleep wakeup must use sleep wake sources such as GPIO, because peripheral interrupts are not generated in light sleep. Deep-sleep wake via ext1 RTC GPIOs (`esp_sleep_enable_ext1_wakeup_io`) works on chips with `SOC_PM_SUPPORT_EXT1_WAKEUP` — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst), [sleep_modes.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/sleep_modes.rst)

### Inferences
- **IMU pattern:**
  - Map INT1 to tap/double-tap, and INT2 to wake-up/6D/free-fall (latched).
  - On the MCU: GPIO wakeup → `input` task → read `ALL_INT_SRC` once (clears the latch) → post `TAP`, `DOUBLE_TAP`, `SHAKE` or `ORIENT(face_down…)`.
  - "Dizzy": wake-up threshold events counted inside a window, e.g. ≥4 in 1.5 s, or the FIFO in stream mode at ODR 100–200 Hz briefly after the first shake, with the RMS computed once.
  - "Tilt to look": 6D orientation changes drive the gaze direction, with no continuous polling.
  - In deep sleep, keep only the wake-up (motion) interrupt enabled at the lowest ODR.
- **Microphone pattern (ESP32-C3):**
  - Option A: analog MEMS mic + ADC continuous mode at 8 kHz, computing only an envelope (|x − DC| low-pass), only in Awake.
  - Option B: an I2S (not PDM) MEMS mic at 16 kHz/16-bit.
  - Option C: raw PDM with a 64× CIC decimator in software. That is CPU-heavy at a 1–3 MHz bit-clock, so avoid it on the C3.

  Detection by state:
  - *Loud/startle:* short-term energy (10–20 ms) > noise floor (slow EMA) + 15–20 dB.
  - *Clap:* a startle with fast rise (<5 ms) and decay below threshold within ~100 ms; double-clap = two within 150–600 ms.
  - *Voice-ish "listening":* energy above floor sustained for 200–1500 ms with a modulation of 3–8 Hz in the envelope (syllable rate). Optional zero-crossing-rate gate.

  These thresholds are design starting points with no source, to be tuned on hardware.
- **ESP32-S3** can use PDM mics with hardware PDM→PCM. **nRF52840** PDM + EasyDMA at 16 kHz is the cleanest low-CPU path, using Zephyr DMIC.
- **Light sensor:** sample every 5–30 s in Idle via a one-shot ADC read or the sensor's own threshold-interrupt mode, if the part has one. "Dark for N minutes" → Drowsy. Never poll faster than the behavior needs.
- **ESP32-C6 option:** the LP core polls cheap I2C sensors (light, IMU status) while the HP core sleeps, waking the HP core only on meaningful change.

### Gaps
- Not verified: the exact LIS2DW12 / LIS3DH / BMI270 / BMA4xx interrupt capabilities and currents; hardware selection belongs to another researcher.
- No sources were collected for clap or VAD algorithms on MCUs (search budget).
- Not checked: ESP-IDF ADC continuous-mode light-sleep compatibility.

---

## Q9. Companion features: BLE GATT, OTA/DFU, time sync

### Takeaway
BLE turns the keychain into a connected toy: a mood/notification characteristic, an asset upload path and OTA. On ESP32, BLE and automatic light sleep conflict unless the BLE low-power clock is set up correctly: an external 32 kHz crystal is best, and main-XTAL-on works at higher sleep current. ESP-IDF OTA uses dual app slots with rollback. Zephyr/NCS uses MCUboot + MCUmgr (SMP over BLE), which also covers file upload. Embassy has embassy-boot.

### Cited Findings
- **ESP BLE low-power clock options** (from ESP-IDF's BLE low-power-mode guide):
  - Main XTAL: "the main XTAL remains powered on during light-sleep, resulting in higher current consumption".
  - External 32 kHz crystal: `CONFIG_BT_*_LOW_POWER_CLOCK = External 32 kHz` + `CONFIG_RTC_CLK_SRC_EXT_CRYS`. "Even if 32 kHz is selected… the system will fall back to the main XTAL if the external crystal is not detected".
  - Internal RC clock: >500 PPM sleep clock accuracy, with interoperability limits.

  — [ESP-IDF low-power-mode-ble.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/low-power-mode/low-power-mode-ble.rst)
- The PM doc says that on non-ESP32 chips the BT controller holds `ESP_PM_NO_LIGHT_SLEEP` even with modem sleep — [power_management.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/power_management.rst). This *appears to conflict* with the BLE low-power guide above, which describes light sleep with BLE. Check the exact Kconfig (`CONFIG_BT_CTRL_MODEM_SLEEP`, LPCLK source) on the target chip and IDF version.
- Arduino-ESP32 on C3 is built with NimBLE (`CONFIG_BT_NIMBLE_ENABLED=y`, Bluedroid off) — [defconfig.esp32c3](https://github.com/espressif/esp32-arduino-lib-builder/blob/master/configs/defconfig.esp32c3)
- **ESP-IDF OTA:**
  - at least two OTA app partitions (`ota_0`, `ota_1`) plus an OTA data partition;
  - app rollback (`CONFIG_BOOTLOADER_APP_ROLLBACK`) is enabled by default;
  - a new image starts as `ESP_OTA_IMG_PENDING_VERIFY` and is confirmed either automatically at startup (default) or by the app (`…CONFIRM_BY_APP`).

  — [ESP-IDF ota.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/ota.rst)
- **Zephyr MCUmgr** offers OS, Image, Statistics, Settings, File-system and Shell management groups over SMP — [mcumgr.rst](https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/services/device_mgmt/mcumgr.rst)
- **Embassy:** `embassy-boot` is "a lightweight bootloader supporting firmware application upgrades in a power-fail-safe way, with trial boots and rollbacks". BLE comes from `trouble` or `nrf-softdevice` — [embassy README](https://github.com/embassy-rs/embassy/blob/main/README.md)

### Inferences
- **Minimal GATT design (custom 128-bit service "Companion"):**

| Characteristic | Props | Payload |
|---|---|---|
| `mood` | read/notify | `{valence, arousal, energy, boredom, affection, state}` 6–8 B |
| `command` | write | `{op, arg…}`: show_expression(id, ms), play_clip(id), set_overlay, poke(event) |
| `notify_in` | write | phone pushes "you have a message / call / timer done" → behavior event |
| `time` | write | u32 epoch + i16 tz offset (phone sets on connect) |
| `asset_ctrl` / `asset_data` | write / write-no-rsp | pack upload: begin(name, size, crc) / chunk(offset, ≤ MTU−3 bytes) / commit |
| `settings` | read/write | brightness, night hours, sound sensitivity, enabled reactions |

  Use long advertising intervals (≥1 s) when not connected, and connection parameters with large intervals plus slave latency once connected. Disconnect when idle and let the app reconnect on demand.
- **Time sync:** the phone writes epoch time on each connect, and the device keeps time with the RTC/LP timer across light and deep sleep. That enables "time away" mood decay, night mode and greeting behaviors. The standard Current Time Service (CTS) is an alternative to a custom characteristic (not researched).
- **OTA over BLE on ESP32:** NimBLE GATT chunks go to `esp_ota_begin/write/end`. Gate on battery level, and confirm the new app only after the display, IMU and BLE all initialised (`CONFIRM_BY_APP`). On nRF, use MCUboot + SMP and the Nordic nRF Connect Device Manager app (not researched here).
- Asset packs share the transfer path but write to LittleFS (Q5), so art can update without a firmware OTA.

### Gaps
- The apparent conflict between "BT holds NO_LIGHT_SLEEP" (PM doc) and "BLE in light sleep with main XTAL / 32 kHz" (BLE low-power guide) for ESP32-C3/C6 in IDF 5.5/6.x was not resolved.
- Not researched: Espressif BLE OTA components (esp-iot-solution `ble_ota`), the Nordic Device Manager app, and CTS details (search budget).
- Not covered: BLE security and pairing UX for a keychain without input.

---

## Q10. Development workflow: simulators, desktop builds, unit tests, GIF capture

### Takeaway
Iterate on faces **without hardware**:
- Build the portable face/behavior core for the desktop with an SDL window (U8g2's SDL back end, or LVGL's PC simulator project if LVGL is used).
- Run the full firmware in **Wokwi**, which simulates ESP32-C3/C6/S3 + SSD1306, with **Wokwi CI** automation in GitHub Actions.
- Unit-test the logic on the host (PlatformIO `native`, or ESP-IDF linux target + CMock).
- Export frames as PBM/XBM (U8g2 has `writeBufferPBM/XBM`) or straight from your framebuffer, and convert them to GIF for design review.

### Cited Findings
- Wokwi simulates ESP32, ESP32-C3, ESP32-S2, ESP32-S3, ESP32-C5 (alpha), ESP32-C6, ESP32-C61, ESP32-H2 and others, including ESP32-C3-DevKitM-1 and ESP32-C6-DevKitC-1 boards — [wokwi-docs guides/esp32.md](https://github.com/wokwi/wokwi-docs/blob/main/docs/guides/esp32.md). Its `board-ssd1306` part is 128×64 I2C at address 0x3c, changeable via the `i2cAddress` attribute, and works with Adafruit SSD1306, U8g2/U8x8, lcdgfx, SSD1306Ascii and Tiny4kOLED — [wokwi-docs board-ssd1306.md](https://github.com/wokwi/wokwi-docs/blob/main/docs/parts/board-ssd1306.md)
- Wokwi CI runs firmware on the same simulation engine in the cloud from CI (GitHub Actions, GitLab). Automation scenarios can "push buttons, change the state of the sensors, and check the serial output" ("Wokwi in the Loop") — [wokwi-docs wokwi-ci/getting-started.md](https://github.com/wokwi/wokwi-docs/blob/main/docs/wokwi-ci/getting-started.md)
- U8g2 ships an SDL display back end for desktop builds (`sys/sdl`, e.g. `u8x8_d_sdl_128x64.c` with a configurable pixel multiplier) — [u8g2 sys/sdl](https://github.com/olikraus/u8g2/blob/master/sys/sdl/common/u8x8_d_sdl_128x64.c)
- U8g2 `writeBufferPBM/XBM(Print&)` dumps the current buffer as ASCII PBM/XBM "to implement a screenshot/screencapture feature" — [u8g2reference](https://github.com/olikraus/u8g2/wiki/u8g2reference)
- The LVGL VSCode PC simulator runs LVGL on Windows/Linux/macOS with SDL2, optionally with FreeRTOS — [lvgl/lv_port_pc_vscode README](https://github.com/lvgl/lv_port_pc_vscode)
- ESP-IDF host apps can run on Linux/macOS with the FreeRTOS POSIX/Linux simulator, or fully mocked with CMock — [ESP-IDF host-apps.rst](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/host-apps.rst). PlatformIO Unit Testing can run the same tests on the local host (native) and on targets/simulators — [platformio-docs unit-testing](https://github.com/platformio/platformio-docs/blob/develop/advanced/unit-testing/index.rst)

### Inferences
- **Repo layout:**

```
/core        (pure C++17, no Arduino/IDF includes)
  face/      params, presets (const tables), tween+easing LUTs, rasterizer → uint8_t fb[8][128]
  anim/      clip decoder (XOR-RLE/heatshrink), overlays
  behavior/  HSM, mood, utility selector, event types
/platform
  esp-idf/   i2c/spi flush (dirty spans), pm config, gpio wake, nimble gatt, nvs/littlefs
  zephyr/    display or raw i2c flush, pm, bt, settings
  desktop/   SDL window (scale ×6), keyboard → events (t=tap, s=shake, d=dark, l=loud), PBM/GIF export
/tests       host unit tests: tween endpoints, LUT monotonicity, HSM transitions, mood decay, codec round-trip
/tools       png→native-page converter, clip packer, LUT generator (Python)
```

- **GIF pipeline:** the desktop build writes `frame_%04d.pbm` from `fb` at a fixed virtual time step, e.g. 33 ms per frame. Then `ffmpeg -framerate 30 -i frame_%04d.pbm -vf scale=512:256:flags=neighbor face.gif`, or ImageMagick. A deterministic virtual clock and seeded RNG make the GIFs reproducible and usable for golden-image regression tests (hash per frame).
- **On-device capture:** a debug serial command dumps `fb` (1024 B, base64) so the same converter makes GIFs from real devices, e.g. to verify the dirty flush matches.
- **Wokwi** is for integration (I2C driver, timing, buttons, IMU-less events via scenario-driven GPIO). Note it cannot validate real current draw or OLED overclock tolerance.

### Gaps
- Not verified: whether Wokwi simulates ESP32 light-sleep timing faithfully, and which IMU or microphone parts it offers.
- No ready-made SSD1306 "GIF recorder" library was found. The pipeline above is a suggestion.
