English | [Русский](README.md)

# Firmware

There are two firmwares here, both on the same engine:

- **Rev B board** (`src/keychain/`, environment `keychain`) — the keychain's own nRF52840 board ([`hardware/rev-b`](../hardware/rev-b/README.en.md)): all sensors, voice, Bluetooth encounters, sleep and power-off. Described [below](#rev-b-board).
- **Stand** (`src/main.cpp`, `esp32*` environments) — an ESP32 board, a 0.96″ SSD1306 module and, optionally, a TTP223 touch button and a passive buzzer. The point of the stand is to see all 139 emotions on a real panel without the custom board. The stand also does Bluetooth encounters: two stands on a desk greet each other, duel and chat just like two keychains, and a stand and a rev B keychain do the same with each other ([encounters on the stand](#bluetooth-encounters)).

The engine is the same as in the simulators, unchanged.

# ESP32 stand

## What you need

- **Board**, any of:
  - ESP32-C5-DevKitC-1;
  - an ESP-WROOM-32 DevKit;
  - ESP32-C3 SuperMini or DevKitM;
  - ESP32-S3 DevKitC.
- **Display module:** SSD1306 128×64, I2C, 4 pins.
- **Button** (optional): TTP223 touch sensor.
- **Buzzer** (optional): passive, without a built-in oscillator. A piezo disc, a 9–12 mm piezo buzzer or a KY-006 module will do. An active buzzer with an oscillator won't work: it beeps at a single pitch.
- Dupont wires and a USB cable with data lines. A "charge-only" cable won't work.

**ESP8266 (Wemos D1 mini) is not supported.** The emotion catalog takes about 65 KB of constants. On the ESP8266 these live in RAM by default, and only about 45 KB of RAM is free.

## Wiring

| Display module | ESP32-C5 | ESP32 DevKit | ESP32-C3 | ESP32-S3 |
|---|---|---|---|---|
| VCC | 3V3 | 3V3 | 3V3 | 3V3 |
| GND | GND | GND | GND | GND |
| SCL | GPIO 1 | GPIO 22 | GPIO 9 | GPIO 9 |
| SDA | GPIO 0 | GPIO 21 | GPIO 8 | GPIO 8 |
| **TTP223 button** | | | | |
| VCC | 3V3 | 3V3 | 3V3 | 3V3 |
| GND | GND | GND | GND | GND |
| I/O | GPIO 6 | GPIO 4 | GPIO 3 | GPIO 4 |
| **Buzzer** | | | | |
| + | GPIO 8 | GPIO 25 | GPIO 4 | GPIO 5 |
| − | GND | GND | GND | GND |

- **Wire by the labels on the module, not by pin order.** Some modules have `VCC GND SCL SDA`, others `GND VCC SCL SDA`. Swap the power pins and the module burns out.
- **Power it from 3.3 V.** The modules also run on 5 V, but then the module's pull-ups pull SDA and SCL to 5 V, and the ESP32 inputs are not rated for that.
- **ESP32-C5-DevKitC-1:**
  - 3V3, GND, GPIO 0, 1 and 6 are on the same header (J1);
  - the board has two USB-C ports: the stand's messages go through the **UART** port, so plug the cable into that one;
  - the **BOOT** button on the board itself works as the stand's button, no wires needed.
- **The TTP223 needs three wires, not one:** VCC, GND and I/O. Without power the module stays silent. Every touch is printed to Serial: «Кнопка: тап» ("Button: tap").
- **Address** 0x3C or 0x3D (jumper on the back of the module). The firmware finds it on its own.
- **1.3″ modules** usually use an SH1106. If the picture is shifted or there is garbage along an edge, enter the `h` command.
- **The buzzer** can be connected straight to the pin, preferably through a 100–220 Ω resistor. Without a buzzer the stand works the same, just silently.
- **Other pins** are set in `platformio.ini`: `-DPIN_SDA`, `-DPIN_SCL`, `-DPIN_BUTTON`, `-DPIN_BUZZER`.
- **A regular push button** instead of the touch sensor goes between the pin and GND. Then add `-DBUTTON_ACTIVE_LOW=1` to `build_flags`.

## How to flash

### From the browser, nothing to install

1. Take the ready-made file for your board from [`release/`](release/README.en.md): `stand-esp32c5.bin`, `stand-esp32dev.bin`, `stand-esp32c3.bin` or `stand-esp32s3.bin`. Your own build comes from PlatformIO (below): the file `.pio/build/<env>/firmware.factory.bin`. CI puts the same files into the **stand-firmware** artifact in the Actions tab on GitHub, but only when Actions are running ([why they aren't right now](#where-to-get-the-files)).
2. Open the web flasher [olerast.com/flash](https://olerast.com/flash) in desktop Chrome or Edge, connect the board and pick its port.
3. Flash your board's file at address `0x0`.
4. Open the console at 115200: messages appear there and you can type commands.

Fallback: Espressif's flasher [espressif.github.io/esptool-js](https://espressif.github.io/esptool-js/): **Connect** → file at address `0x0` → **Program** → **Disconnect** → in the **Console** section set 115200 baud and press **Start**.

### VS Code + PlatformIO

1. Install the **PlatformIO IDE** extension.
2. Open the `firmware/` folder.
3. Pick the environment at the bottom of the window: `esp32c5`, `esp32dev`, `esp32c3` or `esp32s3`.
4. Click **Upload**, then **Serial Monitor**.

The first build downloads the core and the compiler and takes a few minutes.

### From the terminal

```sh
pip install platformio
cd firmware
pio run -e esp32c5 -t upload      # or -e esp32dev, -e esp32c3, -e esp32s3
pio device monitor                # 115200, exit with Ctrl+C
```

If the port doesn't show up, install the driver for the board's USB bridge: CH340 or CP210x. If flashing doesn't start, hold **BOOT**, press **RESET** and release **BOOT**.

## What the stand can do

| Mode | What happens |
|---|---|
| **live** (at startup) | The personality runs: blinks, looks around, gets bored, falls asleep at night, reacts to the button |
| **showcase** | All emotions in a loop, 3.5 s each. Number and name are printed to Serial |
| **manual** | A single emotion chosen by command |

**Button:**

| Action | Result |
|---|---|
| Touch | In live mode — tap. In showcase and manual mode — next emotion |
| Two touches in a row | Double tap |
| Hold 1.5 s | Toggle "live" ↔ "showcase" |

**Serial commands** (one per line, then Enter):

| Command | What it does |
|---|---|
| `laugh` or `42` | show an emotion by id or by number from the [catalog](../docs/emotions.en.md) |
| `n` / `p` | next or previous emotion |
| `s` or `s 1500` | showcase of all emotions, optionally with ms per emotion |
| `a` | live mode |
| `e tap`, `e shake`, `e music`, `e dark` … | an event for the personality, as if a sensor fired |
| `c blink`, `c sneeze`, `c glitch` … | a gesture |
| `+` / `-` / `b 200` | brightness: brighter, dimmer, exact value 0–255 |
| `dim` | auto-brightness on/off: in live mode the screen is 4× dimmer at night and during sleep, 16× dimmer in the dark |
| `u` / `r` / `d` | flip the screen / retro scanlines / anti-burn-in drift |
| `h` | toggle SSD1306 ↔ SH1106 |
| `t 23:30` | time of day for the personality (the stand has no clock, it starts at 12:00) |
| `l` / `i` / `?` | list of emotions, clips and events / status / help |
| `v` | every 10 s print frames per second, bytes and time per frame |

**Personality, growth and scenes:**

| Command | What it does |
|---|---|
| `me` | who am I: callsign, personality, rarity, day, XP, implants, unlocked emotions |
| `theme classic` / `cyber` / `retro` | visual theme, saved to flash |
| `x hack`, `x new_year`, `x duel` … | scene: `boot hack data_rain error404 reboot hide upgrade new_year birthday halloween greet duel chat achievement charge shutdown banner bsod visor screensaver` |
| `surprise` | a random surprise right now |
| `xp 200` / `day` / `steps 3000` | XP, new day, steps — to test implants and the upgrade scene |
| `meet` / `meet old` | a new or a familiar keychain nearby |
| `meet again` | the same neighbor as last time. Friendship grows by days met: `day`, `meet again`, `day`, `meet again` — and you are pals |
| `meet mentor` | an older keychain: gives an emotion from the next implant |
| `friends` | known keychains: days met and friendship level, stored in flash |
| `ach` | 24 achievements: which you have and what the others are for |
| `knock` | the stand itself knocks "Shave and a haircut", and the stand's character answers "two bits" (on the rev B board knocking doesn't trigger yet) |
| `bat 47` | charge in percent for the charging scene (`e charge_start`) |
| `off` | power off: GOODNIGHT, sleep until the button |
| `duel win` / `duel lose` | a duel with a made-up neighbor: a win unlocks an emotion early, then comes the secret chat |
| `date 2026-12-31` | date for holidays and Easter eggs: `2026-03-14` — Pi Day, `2026-04-01` — prank, `2026-04-12` — POEKHALI!, `2026-02-13` — Friday the 13th (the stand has no clock) |
| `t 13:37` | Easter-egg times: 13:37, 11:11, 12:34, 04:04 |
| `bday 05-14` | your birthday, saved to flash |
| `tilt 1 0` / `jerk -1 0` | tilt: pupils roll to the side / a sudden jerk |
| `serial 75` / `serial` | view another keychain (number 75 is legendary) / go back to your own. The callsign broadcast over the air is that of the one being viewed |
| `unlock all` / `reset yes` | unlock all emotions / restart growth from scratch |

**Sound** ([the keychain's voice](../docs/sound.en.md)):

| Command | What it does |
|---|---|
| `snd` | buzzer on/off, saved to flash (on the rev B board a triple press does the same; the stand has no triple press) |
| `say laugh` | a phrase: `boop happy laugh sad angry surprise scared love sleepy hello goodbye question idea error charge_start charge_full battery_low level_up friend_hi duel win lose boot` |
| `chat` | a secret coded chat with a made-up neighbor: you hear both sides (with a real neighbor each buzzer plays only its own lines) |
| `x birthday`, `x hack` … | scenes play their own music |

Each personality has its own voice. The keychain doesn't respond to touches and events every time, but according to how talkative it is. From 23:00 to 6:00 it only talks about important things, and quietly. Right after `duel win` or `duel lose` comes the secret chat. Each phrase is printed to Serial: «Звук: laugh» ("Sound: laugh").

The character is derived from the chip's serial number: every board has its own. Growth, known keychains, theme, birthday, duels and ghost mode are stored in flash and survive a reboot. A growth record from an older version is migrated on the first run of new firmware. The OLERAST LINK boot animation plays at power-on. In manual mode (`laugh`, `42`) you can show any emotion, even one not yet unlocked.

A command with a wrong number or format (`t 25:99`, `xp -1`, `b 300`) is not executed; the stand prints the expected format instead. A line longer than 63 characters is skipped entirely.

The screen can be disconnected and reconnected on the fly. The stand notices it is gone from I2C errors and looks for the module every 2 s. Once found, it re-initializes it and redraws the whole frame. If a transfer failed because of interference, the whole frame is sent next.

`millis()` overflows every 49.7 days. The stand compares deadlines only through `time_reached`, and keeps the clock and day count on a 64-bit uptime.

## Bluetooth encounters

The stand meets others over the radio just like a rev B keychain: the same advertising packet, the same schedule, signal threshold, limits and table of known keychains. The encounter logic is shared, from the engine (`face/encounter.h`); how it works is in [keychain encounters](../docs/encounters.en.md). So these can meet:

- two stands — any of the four boards in any combination;
- a stand and a rev B keychain.

Encounters only happen in **live mode**. In showcase and manual mode the stand neither transmits nor listens: there would be no one to play the scene with the neighbor. At night (`t 23:30`), during sleep and in the dark (`e dark`) the stand, like the keychain, only broadcasts its packet rarely and doesn't listen.

| Command | What it does |
|---|---|
| `radio` | radio status: stack, mode (normal, frequent, night, off), neighbors nearby, how many keychain packets were heard, free memory |
| `duels` | duels with neighbors on/off, saved to flash; on by default (on the keychain a double press does the same) |
| `ghost` | ghost mode: the stand only listens and meets no one; saved to flash |
| `me` | at the end — radio stack, duels and ghost mode |

An encounter is printed to the console just like on the keychain: «Встреча с K7-3F2A (<характер>), новый, уровень 0 (сигнал −48 дБм)» ("Encounter with K7-3F2A (<personality>), new, level 0 (signal −48 dBm)") or «Дуэль с K7-3F2A: победа (сигнал −48 дБм)» ("Duel with K7-3F2A: win (signal −48 dBm)"). Scenes and sounds follow, as after `meet` and `duel`.

**How to test with two boards.**

1. Flash both boards. Each has its own callsign: it comes from the chip's serial number, and `me` shows it.
2. Open two consoles (two `pio device monitor -p <port>` windows). At startup the stand prints «Радио: NimBLE…» or «Радио: Bluedroid…» ("Radio: …"). If it says «Bluetooth не включился» ("Bluetooth failed to start"), encounters work only via commands.
3. Put the boards next to each other, half a meter apart. The −70 dBm signal threshold means 1–3 m for a keychain. ESP32 boards transmit louder than the keychain, so two stands hear each other even farther away.
4. Wait. The first encounter usually comes within 1–3 minutes, occasionally up to 5–7: while no neighbor is heard, the stand listens for 150 ms every 30 s. Once a neighbor is heard, `radio` shows the "frequent" mode.
5. A first encounter with duels on for both is usually a duel: one wins, the other loses, then a coded chat on two buzzers. With `duels` turned off on one of the stands, both show a NEW FRIEND encounter.
6. A repeat with the same neighbor comes after 10 minutes; on consecutive encounters — after 20, 40, 80, 160. A duel with them — once a day. To skip the wait: `day` on both stands (a new day: a duel is possible again) or RESET on the board (cooldowns are forgotten, known keychains stay in flash). Friendship grows by days: `day` on both — and another encounter.
7. `ghost` on one stand — no encounters for either of them.

With a stand and a rev B keychain it's the same. The keychain doesn't listen in the dark, during sleep or at night by its own clock: keep it in the light and with an awake face.

**Limitations.**

- **Boards:** ESP32-C5, ESP32-C3 and ESP32-S3 run the NimBLE stack, the plain ESP32 (DevKit) runs Bluedroid from the Arduino-ESP32 3.3 core. With Bluedroid the plain ESP32 firmware doesn't fit into the standard 1.25 MB partition, so the `esp32dev` environment uses the `huge_app` partition table: app up to 3 MB, no over-the-air updates. Records in flash (NVS) stay where they are. ESP32-S2 and ESP8266 have no Bluetooth and are not supported.
- **Memory:** the Bluetooth stack takes tens of kilobytes of heap; free memory is shown by the startup message and by `radio`. The ESP32-C3 has the least, but even there it is enough. If Bluetooth fails to start, the stand keeps working without encounters. Build without radio: `-DSTAND_BLE=0` in `build_flags`.
- **No clock:** a stand's day is 24 hours of uptime or the `day` command. Cooldowns after encounters live in RAM and are forgotten after RESET; the keychain does the same.
- **No vibration;** in a coded chat with a real neighbor each buzzer plays only its own lines.
- **The BLE address** of the stand is the chip's MAC, not a random one as on the keychain: the stand is a test board.
- **Interference:** boards with on-board antennas hear each other differently; in a noisy environment (lots of Wi‑Fi and Bluetooth nearby) the first encounter can take longer.

## What to check on a real panel

- [ ] **The whole catalog** (`s`), from 30–50 cm and from the distance of a keychain on a backpack. Note the numbers of emotions that don't read well.
- [ ] **Small icons:** Zzz, sweat drop, notes, hearts, stars, question mark. Check that they don't blur together.
- [ ] **Look-alike pairs:**
  - `scared` and `surprised`;
  - `sad` and `tired`;
  - `angry` and `annoyed`.

  Can you tell them apart without a caption?
- [ ] **Smoothness.** No stutter in transitions and gestures. The `v` command shows bus time per frame.
- [ ] **Brightness** `b 30`, `b 127` and `b 255`: at which level the face reads indoors and outdoors.
- [ ] **Retro mode** (`r`): how it looks and whether readability suffers.
- [ ] **Auto-brightness:** `t 23:30` and `e dark` — the screen dims, `e light` and `t 12:00` — it comes back. Is the face visible enough at the minimum?
- [ ] **Voice:** `say laugh`, `x birthday`, `duel win`. Are the notes distinguishable, is it not grating, can you hear it in a noisy room? At night (`t 23:30`) phrases are quieter.
- [ ] **Retro theme** (`theme retro`): do emotions read with big pixels?
- [ ] **New scenes:** `x achievement`, `x charge`, `x bsod`, `x visor`, `x screensaver` (wait 10 s for the corner), `x banner`. Is the small text readable?
- [ ] **Bluetooth encounters** ([how](#bluetooth-encounters)): two stands meet and duel, the outcomes are opposite, scenes and chat run simultaneously; a stand and a rev B keychain — the same. How many minutes until the first encounter, and from what distance.

Emotions that don't read well are easiest to report with an [issue template](https://github.com/olerast67/emotion-keychain/issues/new/choose).

## How it works

| File | What's inside |
|---|---|
| `src/main.cpp` | modes, button, Serial commands, statistics, radio encounters |
| `src/stand_radio.*` | the stand's Bluetooth: connectionless advertising of the keychain packet, listening in windows on the keychain's schedule, its own packet parser with length checks, a queue of heard packets. NimBLE on C3, C5, S3 and Bluedroid on the plain ESP32 |
| `src/ssd1306.*` | SSD1306/SH1106 driver. Sends only changed windows using `0x21`/`0x22` commands in 64-byte chunks; on the SH1106 — page by page, with a 2-column offset |
| `platformio.ini` | environments `esp32c5`, `esp32dev`, `esp32c3` and `esp32s3` with pins for each; `esp32dev` uses the `huge_app` partition table |
| `host/` | firmware testing on a PC: Arduino stubs and a controller memory emulator |

The build uses the Arduino-ESP32 3.3 core via the [pioarduino](https://github.com/pioarduino/platform-espressif32) platform. The official PlatformIO platform is stuck on core 2.0, which has no ESP32-C5. Besides the regular firmware, the build produces a merged file `.pio/build/<env>/firmware.factory.bin` — this is what you flash from the browser at address `0x0`.

The PC test is run by `ctest` from the repository root, together with the engine tests. The test runs the real `main.cpp` on virtual time. It checks:
- the button, all commands and invalid input;
- the showcase of the whole catalog;
- SH1106 and address 0x3D;
- auto-brightness;
- bus interference and screen reconnection;
- migration of an old growth record;
- `millis()` overflow in manual mode, while charging and during sleep;
- buzzer: startup melody, phrases, on/off switch, quiet hours, chat after a duel;
- themes, charging with a percentage, the secret knock, achievements, friendship by days, power-off, the 13:37 Easter egg;
- radio encounters with a keychain nearby: instead of Bluetooth — the test's simulated air, and the keychain is the same engine encounter logic. A duel on both at the same time with a single winner, the chat after it, an encounter with duels off, a known keychain in flash, a weak signal, ghost mode, night and showcase.

After every loop pass the "screen" memory must match the engine's frame.

# Rev B board

Firmware for the custom board: a Raytac MDBT50Q module (nRF52840) with SoftDevice S140, Adafruit's Arduino core. The board definition for the build is [`boards/keychain_revb.json`](boards/keychain_revb.json) and [`variants/keychain_revb`](variants/keychain_revb).

## Features

| What | How |
|---|---|
| **Face** | the whole engine: 139 emotions, personality from the chip's serial number, growth, achievements, themes, Easter eggs |
| **Touch** | the LIS3DH hardware detector: tap and double tap on the case |
| **Motion** | shake, hard shake, fall and impact, "picked up", rest, upside down, walking and running, steps into XP; pupils roll with tilt and lag behind on a jerk |
| **Sound** | PDM microphone: clap, speech, music, silence. Listens only while the keychain is awake. The microphone doesn't hear its own buzzer and motor: while they run and for 150 ms after, sound doesn't count. Kicks from its own motor are not touches: while it runs and for 150 ms after, shake, impact, fall and touches don't count |
| **Light** | LTR-303ALS: dark, light, sun; screen brightness by time of day and light |
| **Temperature** | the chip's sensor: cold, hot; it also decides whether charging is allowed |
| **Voice** | buzzer driven in antiphase on two pins: phrases, scene music, coded chat |
| **Vibration** | short patterns for encounters and duels |
| **Encounters** | connectionless BLE advertising and scanning: greeting, friendship by days, a gift from an older keychain, a duel and coded chat simultaneously on two keychains ([encounters](../docs/encounters.en.md)) |
| **Battery** | charge in percent, charging scene, eco mode on low charge, shutdown on an empty cell. Charging only at 10–42 °C measured on the nRF die ([charging](#charging)) |
| **Sleep** | the CPU sleeps between frames; the screen turns off in a pocket after a minute; full power-off with the button |
| **Storage** | growth, known keychains and settings — one record in a journal spread over three dedicated flash pages (0xEA000–0xECFFF, reserved by the bootloader). Records are appended without erasing, a record is 320 bytes, a page is erased once every 12 records; a power cut in the middle of a write leaves the previous record intact (test `storage_tests`: 3000 power cuts). The record has a header with the lengths of its parts: firmware in which they have grown can still read an old record. A regular save happens every 10 minutes, new emotions are saved immediately, console settings 3 s after the last change and no more than once every 10 s. While the buzzer is sounding or the motor is running, a regular save waits: erasing a page stalls the loop for a fraction of a second |
| **Reliability** | 8 s watchdog from the first lines of `setup`. A slave holding SDA low is released with SCL clocks, while a bus stuck hard is left alone by the firmware — both the sensor bus and the display bus: any transfer on it would hang until reset, and the keychain would reboot in a loop with no face, console, charging or button power-off. Before the bus starts, the lines are also checked for a short between them (an SCL–SDA bridge on adjacent J2 pins is invisible at idle, but a transfer with it hangs). A screen with a stuck bus is powered off and retried less and less often, down to once a minute; if the display controller pulls SDA low in the middle of operation, the firmware sees it between transfers and power-cycles it. Sensors that stop responding or lose their settings are restarted: checked every 10 s, a failed restart is retried less and less often, down to once every 5 minutes. A screen that doesn't respond is powered off for a second with SDA and SCL held low — that way its supply drops below 1 V and a hung controller resets. Time without a jump at 48.5 days, the console doesn't block the loop |

## Button

| Press | What it does |
|---|---|
| Short | "button" for the personality; wakes a dark screen |
| Double | duels on/off (banner DUELS ON / DUELS OFF) |
| Triple | sound on/off (banner SOUND ON / SOUND OFF), same as the `snd` command; saved to flash |
| Hold 3 s | "good night" and full power-off; wakes up from the button or from USB. The goodbye starts immediately, even in the middle of another scene (an encounter or chat is cut short), and the screen goes dark about 7 s after the press. While charging — the screen, sensors and radio turn off, charging and the clock keep going; the button turns it back on (full sleep on USB is impossible: on USB the button enters the bootloader) |
| Hold and plug in USB | bootloader: the KEYCHAIN drive appears. Plug in USB within 10 s of the press: the screen goes dark after about 7 s — plug in right away ([all methods](#bootloader-and-usb-updates)) |

Presses form one series as long as the next one starts no later than 400 ms after the previous release, however long it was held. One and two presses are resolved 400 ms after the last release, three — immediately. A 3 s hold after short presses only powers off: the series doesn't count.

The button used to wake the keychain is often held "until it turns on". Such a press counts neither as a short press nor as a power-off hold. While charging (powered off on USB) the keychain turns on when the button is released; holding longer than 10 s enters the bootloader.

## Where to get the files

1. **Ready-made files are in [`release/`](release/README.en.md).** It also has their version, checksums and how to find out which firmware is on a keychain. These files are for prototypes: they carry Adafruit's IDs. Keychains for sale are flashed with your own build (item 2).
2. **Your own build.** You need Python and PlatformIO (`pip install platformio`). From the repository root:

   ```sh
   cd firmware
   pio run -e keychain
   ```

   The files appear in `firmware/.pio/build/keychain/`. There they are named differently than in `release/`:

   | In `release/` and in CI | In your build | What it is |
   |---|---|---|
   | `keychain-revb-factory.hex` | `firmware-factory.hex` | everything at once — for a programmer or the factory |
   | `keychain-revb.uf2` | `firmware.uf2` | for the KEYCHAIN drive |
   | `keychain-revb-dfu.zip` | `firmware.zip` | DFU over USB |
   | `keychain-revb.hex` (CI only) | `firmware.hex` | firmware only, without bootloader and UICR; not for writing to a new board with a programmer |

   Your build's version is `git describe` of the commit it was built from; `-dirty` at the end means the working copy had uncommitted changes. A build for sale: your own IDs in the `[sale]` section of `platformio.ini` and a rebuilt bootloader ([how](bootloader/README.en.md#how-to-rebuild)), then a commit and a tag (for example, `git tag sale-1.0`) — and only then `pio run -e keychain`.
3. **CI** builds the same files on every push — the `keychain-firmware` artifact in the Actions tab on GitHub. Right now GitHub Actions doesn't run jobs: the account's payment failed or the Actions spending limit is exhausted. This is fixed in the GitHub account settings (Settings → Billing and plans), not in code. The latest artifacts there are built from commit `fd8ec8e`, before the fixes — don't use them. Once Actions work again, take the artifact of the run on the commit you need and check the version.

**Old firmware doesn't charge on the final rev B board.** Files built before commit `098681e` don't enable charging (CHG_EN on P0.24) and look for the accelerometer at address 0x18 instead of 0x19. Firmware from before this round of fixes doesn't know the `ver` command at all. So after every flash, check the version with the `ver` command in the console ([how to open the console](release/README.en.md#which-firmware-is-on-the-keychain)). The power-on greeting also names the version, but it usually doesn't reach the terminal. If `ver` answers «Не знаю такую команду» ("Unknown command"), the firmware is old — flash a new one.

## First flash: bootloader over SWD

A new module is blank, so a programmer is needed once. It writes the Adafruit bootloader together with SoftDevice S140 — the ready-made file [`bootloader/emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex`](bootloader) — or everything at once together with the firmware (`keychain-revb-factory.hex`, below). After that everything is updated over USB.

**Power and logic levels — read before connecting.**
- **The board is powered from its own USB-C**, at the factory too. The cell doesn't have to be installed at this step. On an unflashed board the charge-status pin sits slightly above its limit until the first flash; this is harmless ([board README](../hardware/rev-b/README.en.md#power-and-charging)).
- **On a blank chip the pins run at 1.8 V.** The module's REG0 outputs 1.8 V by default, and 3.3 V is enabled only after a reset once a value has been written to UICR (REGOUT0; written by the factory file or by the bootloader on its first run). The whole programmer session on a new board runs at VDD = 1.8 V.
- **J3, pad 1 — VTref (this is the board's VDD).** The programmer or level shifter only *senses* the board voltage on it and adjusts its levels to match. Do not feed power into it and do not connect it to the programmer's 3.3 V output — on the Pico that is 3V3 (pin 36): pad 1 is the output of the module's regulator.
- **There are no resistors on SWDIO and SWDCLK on the board:** next to the module there was no room left to route them. If you connect a 3.3 V programmer directly to a blank board (1.8 V), current will flow through the protection diodes of the module's pins. Don't do this. The Pico only outputs 3.3 V, so with it a level shifter is mandatory. The J-Link EDU Mini V2 adapts to VTref on its own; the J-Link EDU Mini V1 works only at 3.3 V and is not suitable.

### Main programmer: Raspberry Pi Pico

A Raspberry Pi Pico with the official [debugprobe](https://github.com/raspberrypi/debugprobe) firmware and a TXS0108E level-shifter module. This covers everything the keychain needs over SWD: the first flash of the bootloader together with the application, erase and unlock, reflashing later (for example, a new bootloader). After the first flash, updates go over USB (`.uf2` to the KEYCHAIN drive, [below](#bootloader-and-usb-updates)): the programmer is no longer needed.

> **The Pico + TXS0108E combination has not yet been tested on this board.** Flash one board with it first and check `ver` in the console.

**What to buy:**

| What | Price | Why |
|---|---|---|
| Raspberry Pi Pico or Pico 2 — the plain one, not W: debugprobe release files are built for these | ~$4–6 | programmer |
| TXS0108E level-shifter module, 8 channels (pins VA or VCCA, VB or VCCB, OE, GND, A1–A8, B1–B8) | ~$1–2 | side A — keychain levels (1.8 V before the first flash, then 3.3 V), side B — the Pico's 3.3 V |
| Micro-USB data cable, Dupont wires up to 10 cm, header pins for the Pico and the module if they don't have them | — | Pico to computer, connections |
| Tag-Connect TC2030-CTX-NL cable and an adapter from the 10-pin 1.27 mm (Cortex Debug) plug to 2.54 mm pins — or thin wires soldered to the J3 pads | — | to J3 ([connection](#connecting-to-j3)) |

**Flashing debugprobe onto the Pico** (once):
1. Download from [github.com/raspberrypi/debugprobe/releases](https://github.com/raspberrypi/debugprobe/releases) the file `debugprobe_on_pico.uf2` (for Pico) or `debugprobe_on_pico2.uf2` (for Pico 2). `debugprobe.uf2` is for the official Debug Probe, not for the Pico.
2. Hold the BOOTSEL button on the Pico and connect it to the computer. A `RPI-RP2` drive appears (`RP2350` on the Pico 2); you can release the button.
3. Copy the file to that drive. The Pico reboots and the drive disappears: it is now a CMSIS-DAP programmer (shown in the system as "Debugprobe on Pico"). Its SWD pins are GP2 (SWCLK) and GP3 (SWDIO).

**Wiring.** Side A of the module goes to the keychain, side B to the Pico:

| TXS0108E module | To | Why |
|---|---|---|
| VA (VCCA) | J3, pad 1 — VTref (keychain VDD) | powers the keychain side: 1.8 V or 3.3 V from the keychain itself |
| VB (VCCB) | Pico, 3V3 (pin 36) | powers the Pico side |
| OE | VA on the module itself | enables the module while VA is present |
| GND | Pico GND (for example, pin 3) and J3, pad 5 | common ground |
| A1 | J3, pad 2 — SWDIO | |
| B1 | Pico GP3 (pin 5) — SWDIO | |
| A2 | J3, pad 4 — SWDCLK | |
| B2 | Pico GP2 (pin 4) — SWCLK | |

- **Never connect the Pico's 3V3 to J3 pad 1** — neither directly nor through VA. The keychain is powered only from its own USB-C; the Pico from its own USB to the computer.
- Leave the module's other channels (A3–A8, B3–B8) unconnected.
- **Keep the wires short, no longer than 10–15 cm:** the TXS0108E detects signal direction on its own and misbehaves on long wires.
- TC2030-CTX-NL cable through an adapter: the adapter's contacts are labeled VTref (VCC), SWDIO, SWCLK, GND; connect by the labels the same way as the J3 pads in the table.

**What to install on the computer** (once): Python 3 and pyOCD — `pip install pyocd`. No driver is needed on Windows and macOS. On Linux the programmer is visible without root only if you install udev rules — they are in the pyOCD documentation. Check: `pyocd list` shows the connected Pico.

### Alternative: J-Link EDU Mini V2

The J-Link EDU Mini V2 adapts its levels to VTref on its own, no level shifter needed. Under SEGGER's license it is for non-commercial use only; it is fine for keychains given as gifts. The TC2030-CTX-NL cable plugs straight into it. Other CMSIS-DAP programmers with levels from 1.2 to 5 V (for example, NXP MCU-Link Pro with pyOCD) are also untested on this board; don't enable powering the board from the programmer.

- **What to install:** from the SEGGER website, the "J-Link Software and Documentation Pack" ([segger.com/downloads/jlink](https://www.segger.com/downloads/jlink/)): without it the computer doesn't see the programmer. Then from the Nordic website — nRF Command Line Tools, which include `nrfjprog`. Nordic no longer updates them, but they can still be downloaded. Check: `nrfjprog --version` prints the nrfjprog and JLinkARM DLL versions, and `nrfjprog --ids` prints the serial number of the connected programmer.
- **Instead of `nrfjprog`** you can use nRF Util — the `nrfutil` program from the Nordic website (not `adafruit-nrfutil`); it also needs the SEGGER pack: `nrfutil install device`, then `nrfutil device recover` and `nrfutil device program --firmware firmware/release/keychain-revb-factory.hex --options chip_erase_mode=ERASE_ALL,verify=VERIFY_READ,reset=RESET_SYSTEM`.

### Connecting to J3

J3 is a set of pads on the back of the board for a Tag-Connect TC2030 cable:
- the **TC2030-CTX-NL** cable has a 10-pin 1.27 mm (Cortex Debug) plug. It plugs straight into the J-Link EDU Mini V2; to the Pico it goes through an adapter to 2.54 mm pins and the TXS0108E module ([wiring](#main-programmer-raspberry-pi-pico));
- **the NL cable has no latches:** J3 on the board has no holes for retaining clips. Insert the plug's three pins into the three J3 holes and press it against the board evenly, without tilting, for the whole time both commands below are running (for pyOCD — both `erase` and `flash`; for J-Link — both `--recover` and `--program … --verify`). If you let go too early or a command aborted with an error, press harder and repeat both commands. To avoid holding it by hand, there is a Tag-Connect retaining clip for NL cables (TC2030-CLIP; check the model for your cable on the Tag-Connect website): it clips onto the tips of the pins on the front side while the screen and cell are not yet installed;
- **or thin wires soldered to the pads:** 1 — VTref, 2 — SWDIO, 4 — SWDCLK, 5 — GND. Pads 3 and 6 are not connected. With the Pico this is even simpler: the wires go straight to the TXS0108E module, no cable or adapter needed. Desolder the wires before installing the cell and the screen.

### Commands

From the repository root, with a file from `firmware/release/` ([where to get it](#where-to-get-the-files)). For your own build: instead of `firmware/release/keychain-revb-factory.hex` use `firmware/.pio/build/keychain/firmware-factory.hex`. Keychains for sale are flashed only with your own build with your own IDs in the `[sale]` section: the `release/` files are for prototypes.

```sh
# Pico with debugprobe via TXS0108E: pyOCD; keep the cable pressed to J3 the whole time
pip install pyocd
pyocd erase --chip -t nrf52840 -f 100k
pyocd flash -t nrf52840 -f 100k firmware/release/keychain-revb-factory.hex

# or J-Link EDU Mini V2: nrfjprog from nRF Command Line Tools
nrfjprog -f nrf52 --recover
nrfjprog -f nrf52 --program firmware/release/keychain-revb-factory.hex --verify --reset
```

`-f 100k` is slow SWD: through a level shifter at 1.8 V a fast clock doesn't always get through. If it fails, shorten the wires and repeat both commands with `-f 50k`.

**Debug protection.** On new nRF52840 revisions it (APPROTECT) is enabled from the factory. On connect, pyOCD checks it itself through the Nordic chip's CTRL-AP port and, if it is enabled, erases the whole chip through the same port — this removes the protection (pyOCD option `auto_unlock`, on by default). `pyocd erase --chip` erases all flash memory together with UICR; `nrfjprog --recover` does the same on J-Link. That is why the factory file is written in full, including UICR.

**`keychain-revb-factory.hex`** (in your own build — `firmware-factory.hex`). It contains:
- the bootloader and SoftDevice;
- the application;
- a "valid application" mark for the bootloader (0xFF000 = 1, like `flash-skip-crc` in its Makefile);
- **UICR.REGOUT0 = 0xFFFFFFFD (3.3 V).** The value takes effect from the reset that ends flashing: the bootloader and firmware run at 3.3 V from the very first start, with no extra reboot to write UICR.

After the programmer the keychain immediately shows a face (if the screen is already fitted), and `ver` in the console reports the version. The same file goes to the factory if the assembler flashes the boards itself (PCBWay — "Firmware loading"): then you don't need J3 at all. For keychains for sale, both the factory and the programmer get `firmware-factory.hex` from your own build with your own IDs in the `[sale]` section; `keychain-revb-factory.hex` from `release/` is for prototypes only. The bootloader alone, without firmware, is written the same way, with the file from `bootloader/`; it sets 3.3 V on REG0 itself on first run and reboots.

After that, a keychain connected over USB shows up as the **KEYCHAIN** drive (if only the bootloader was written) or shows a face right away.

## Bootloader and USB updates

**Drag and drop a file.** Copy `keychain-revb.uf2` from [`release/`](release/README.en.md) (in your own build — `firmware.uf2`) to the KEYCHAIN drive. The keychain reboots with the new firmware. Growth, known keychains and settings are preserved. Then check the version with `ver`. Keychains that have been sold get only `firmware.uf2` from your own build for sale: `keychain-revb.uf2` would give them back the prototype USB IDs (239A:810B). It doesn't affect encounters: the packet is the same in all firmwares.

**How to enter the bootloader.** The bootloader looks at the button only at the moment of reset, so on a running keychain the gestures are caught by the firmware itself.

| How | When it works |
|---|---|
| Hold the button and, without releasing, plug in USB | the keychain is running or was just woken with this same button. The button was pressed no earlier than 10 s before USB — including when the hold is already powering the keychain off: the goodbye scene has played, the screen is dark, and the button is still held. The goodbye starts right after a 3 s hold, and the screen goes dark about 7 s after the press — leaving about 3 s before the 10 s are up |
| The `dfu` command in the console | firmware is running, console is open |
| On a keychain powered off while charging, hold the button longer than 10 s | after 10 s the firmware reboots with the button held, and the bootloader sees it |
| Firmware hung: hold the button with USB connected | the watchdog resets the board within 8 s, the bootloader sees the button and USB |
| Do nothing | there is no firmware or it is corrupted |

- **A powered-off keychain on battery** is first woken by the button press: without USB the bootloader doesn't notice the button. Keep holding it and plug in USB — the first row applies.
- **If the button is held longer than 10 s** (stuck, or used to wake the keychain and not released), plugging in USB does not enter the bootloader: otherwise a keychain with a stuck button would never charge. If the KEYCHAIN drive appears every time you connect a powered-off keychain, the button is stuck.
- **How long the bootloader waits.** If the computer has found the drive — until USB is disconnected. On a wall charger — 3 s after `dfu` or the gesture, 10 s after a reset with the button held. Then the firmware starts.
- **The clock** continues after the gesture and `dfu`: before entering the bootloader the firmware saves it to the minute and enters it via a software reset. Time spent in the bootloader isn't counted.
- **The cell doesn't charge in the bootloader** ([charging](#charging)).

**PlatformIO:**

```sh
cd firmware
pio run -e keychain                                # build only
pio run -e keychain -t upload && pio device monitor  # build and upload over USB
```

`-t upload` works only on a keychain that already has the bootloader: PlatformIO switches it into the bootloader itself and uploads `firmware.zip`. A new board has no port, and the upload after the build stops with an error — the files are already built by then. Without `-e keychain` PlatformIO builds the stand firmware for the ESP32-C5.

Before the build, `tools/version.py` takes the version from `git describe` (tag or short commit hash, `-dirty` with uncommitted changes; without git — `unknown`). It is printed by the console greeting and the `ver` command: that is how you see which build is on the keychain. The build puts `firmware.hex`, `firmware.zip` (DFU over USB), `firmware.uf2` and `firmware-factory.hex` (everything for a programmer) into `.pio/build/keychain/`; what they are called in `release/` and in CI is [above](#where-to-get-the-files). After the build, `tools/uf2.py` checks that the firmware ends before the journal (0xEA000); the size limit in the board definition is the same 802,816 bytes starting at 0x26000.

**USB VID/PID — borrowed, for prototypes only.** The firmware shows up as 239A:810B, the bootloader as 239A:010B: these are Adafruit's IDs for the Raytac MDBT50Q-RX board. Before selling you need your own: a PID under someone else's VID via sublicense, or your own VID from USB-IF (or free from [pid.codes](https://pid.codes): they give PIDs to open projects under OSI licenses, and this project is now open — code under Apache-2.0, board under CERN-OHL-W-2.0). In the firmware they are in one place, the `[sale]` section of [`platformio.ini`](platformio.ini): `usb_vid`, `usb_pid`, `usb_pid_boot`. Before the build, `tools/sale_ids.py` substitutes them for `hwids` from `boards/keychain_revb.json` (the first pair is the firmware's, the second the bootloader's). Nothing needs to be bought for encounters: keychains recognize each other by their own UUID in the packet ([packet](../docs/encounters.en.md#advertising-packet)). A build with the default IDs is byte-for-byte identical to one without the section. The bootloader takes its IDs not from there but from `bootloader/emotion_keychain_revb/board.h` (`USB_DESC_VID`, `USB_DESC_UF2_PID`, `USB_DESC_CDC_ONLY_PID`): you set the same ones there, rebuild the bootloader ([how](bootloader/README.en.md#how-to-rebuild)) and write it with a programmer; the board stays the same.

## Charging

- **The charger is enabled by the firmware.** The bottom end of R3 (PROG of the MCP73832) is on pin P0.24 (CHG_EN). Low — charging at 100 mA; pin released — the charger is off, and the keychain runs from USB through a diode.
- **Charging happens at an nRF die temperature of 10 to 42 °C.** If during charging it gets colder than 7 °C or hotter than 45 °C, charging stops. The check runs immediately when USB is connected and then every 30 s. A lithium cell must not be charged in freezing temperatures: lithium plates onto the anode and the cell degrades. The MCP73832 has no thermistor input, so the firmware watches the temperature.
- **The console** prints «Заряд не идёт: 5 °C, заряжается от 10 до 42 °C» ("Not charging: 5 °C, charges from 10 to 42 °C"), «Заряд остановлен: …» ("Charging stopped: …") and «Заряд снова разрешён (24 °C)» ("Charging allowed again (24 °C)"). While charging is blocked, the charge percentage is unknown, and the charging scene doesn't start on connection.
- **This is the die temperature, not the cell's.** Per the datasheet, the nRF52840 sensor is accurate to ±5 °C (that accuracy is with the 64 MHz crystal; on USB the firmware keeps it running), and on USB the CPU is also a few degrees warmer than the surrounding air and the cell. That's why the cold side has a big margin: charging starts at 10 °C on the die. This check cannot guarantee that a cell near 0 °C won't be charged — only a thermistor next to the cell can do that (rev C). Warm up a keychain brought in from the cold first. On the hot side the die errs on the safe side.
- **End of charge on weak USB.** The firmware reads the charger status via the CHG_IO pin, and "charge complete" reads reliably only if VBUS is at least ~4.6 V. On a weak source the keychain may keep showing the charging scene and 99 % after charging has ended; the charger itself terminates charging correctly ([board README](../hardware/rev-b/README.en.md#power-and-charging)).
- **Without firmware the keychain doesn't charge:** on a blank board, in the bootloader and until the firmware has started, CHG_EN is released: the red charge LED doesn't light brightly, it only glows faintly (50–70 µA flows through R5 and R20 — this is by design). A new board is charged only after flashing.
- **After «Ячейка почти пуста — выключаюсь»** ("Cell almost empty — shutting down") about 3.6 mAh remain before the cell's protection board cuts off. A powered-off keychain draws 6–17 µA — that's 9–25 days. Charge it within a week, and for long-term storage leave it about half charged. A button press on an empty cell shows an empty battery for 1.5 s and powers the keychain off again without fully booting. After USB is connected this rule is lifted.

## Console

USB, 115200. Open it with any terminal: the Arduino IDE serial monitor, `pio device monitor -b 115200`, PuTTY or `screen` ([how](release/README.en.md#which-firmware-is-on-the-keychain)). The `?` command prints the list. The power-on greeting is printed once and usually doesn't reach the terminal: the port appears when the keychain has already started. `ver` and `sensors` show the version and sensors at any time.

| Command | What it does |
|---|---|
| `me` | who am I: personality, growth, steps, duels |
| `sensors` | acceleration per axis, light, sound, temperature, power and charge, flash erase count |
| `v`, `v on`, `v off` | print sensor events: tap, shake, music, dark… (`v` toggles) |
| `t` | show the time or «Время не задано» ("Time not set") |
| `t 23:30`, `date 2026-12-31`, `bday 05-14` | time, date and birthday — for the daily rhythm and holidays |
| `theme cyber` | theme: classic, cyber or retro |
| `snd`, `say laugh` | sound on/off, a phrase |
| `duel`, `ghost` | duels on/off; ghost mode — listen only |
| `ach`, `friends` | achievements, known keychains. `ach` marks KNOCK KNOCK as «на брелоке пока не работает» ("doesn't work on the keychain yet"): the rev B board can't hear the secret knock |
| `e music`, `x hack` | an event or scene manually |
| `b 0…255` | brightness |
| `off`, `dfu` | power off; bootloader for new firmware |
| `ver` | firmware version (also printed by the power-on greeting) |
| `reset yes` | restart growth: growth, achievements and known keychains; settings and clock stay |
| `factory yes` | before giving it away (or selling) — factory state: growth, achievements, known keychains, all settings (theme, sound, duels, ghost mode, brightness, birthday, date) and clock, then power-off (on USB — until the cable is unplugged). The buyer is the one who turns the keychain on. Without `yes` — just a hint |

Unprompted messages: «Заряд не идёт / остановлен / снова разрешён» ("Not charging / Charging stopped / Charging allowed again"), «Ячейка почти пуста — выключаюсь» ("Cell almost empty — shutting down"), «Акселерометр перезапущен» ("Accelerometer restarted"), «Датчик света перезапущен» ("Light sensor restarted"), «Шина датчиков зажата: SDA или SCL в нуле» ("Sensor bus stuck: SDA or SCL low"), «Шина экрана зажата: SDA или SCL в нуле или замкнуты между собой» ("Display bus stuck: SDA or SCL low or shorted together"), «Шина экрана зажата посреди работы: экран перезапускается» ("Display bus stuck during operation: restarting screen").

**Clock.** There is no real-time clock on the board.
- After a reboot (watchdog, `dfu`, bootloader, new firmware) time and date continue from the saved values. Before `dfu`, the bootloader gesture and turning on from power-off on USB they are saved to the minute; after a watchdog reset the time may lag by 10–20 minutes: on its own it is saved every 10 minutes.
- After power-off with the button, an empty cell or the very first power-up, time and date are reset: how long the keychain slept is unknown. The console reminds you to set them again with the `t` and `date` commands.
- **Without a set clock there is no clock-based night.** A dimmed screen, rare radio (encounters aren't listened for), a quiet voice, idle sleep, sleep deprivation, time-based Easter eggs and the 3 a.m. and 5 a.m. achievements — all only by the time set with `t`. Without it the keychain dims and falls asleep only in the dark, by the light sensor.
- **Without a clock a day is 24 hours of uptime, and the count survives power-off.** A keychain that is turned off at night still reaches a new day, and the daily XP limits reset: three sessions of 8–9 hours make a new day. Time while the keychain is off isn't counted.

## Whole-board simulation

Before ordering boards, the firmware can be run on a PC the way it will run on the board. The code from `src/keychain` is built unchanged; models replace the Adafruit core (`host/keychain`):

- **Sensors:** LIS3DH with a 32-sample FIFO (burst reads only with the auto-increment bit, scale and resolution from CTRL1 and CTRL4, overflow), a tap flag on INT1 (the scenario defines the tap itself: a single first, for a double the second after a given delay) and registers that reset on a power dip; LTR-303 with one-shot measurements (wake-up and 100 ms integration); PDM microphone in 32 ms blocks: after power-on — the library's zeros and a startup transient, every other time in a different order (zero, transient, zero: the library hands over the buffer one block late, and which buffer comes first depends on the previous power-on).
- **I2C buses:** a chip can stop responding and come back, a slave can hold SDA low (it releases after SCL clocks while TWIM is off — as on the board, where an enabled TWIM drives the pins itself), SCL can get stuck low (the transfer hangs, as in the core, until the watchdog). Display bus: SCL low or shorted to SDA (the transfer hangs too), a controller holding SDA low (the transfer "succeeds" but never reaches the screen). The Wire buffer is 63 bytes, as in the core; an extra byte is an error.
- **Screen:** SSD1306 behind a power switch. After power is applied its memory holds garbage, and without initialization it doesn't turn on. The module's supply decays slowly after power-off (about 1 V after a second, and within tens of milliseconds with SDA and SCL low), the pull-ups on the lines back-feed it; the controller resets only if the supply dropped below 1 V. The controller can "hang" and stay silent or hold SDA low until it is powered off this way.
- **Power and flash:** asynchronous SoftDevice flash (word-by-word writes, a cut in the middle leaves an incomplete record; erase 85 ms; "busy"; failures during radio scanning); watchdog, UICR, reset reason, core time with tick overflow; USB with a power dip through the diode after disconnection, a charger with the CHG_EN pin, the button, a battery discharging at the real current, including when the keychain is off: ~12 µA plus whatever the firmware left on before System OFF.
- **Bootloader:** button with USB at reset and `dfu` — it waits; computer found the drive — until USB is disconnected, otherwise 3 or 10 s; then the firmware with the same reset reason. No charging in the bootloader.
- **Radio between boards:** advertising arrives with a signal level based on distance; some packets can be dropped.

Each board is its own process. A reboot is a new process, and the journal flash is a file at the same address 0xEA000, so it survives reboots and even a power cut in the middle of a write. Light, sound, temperature and faults set by the scenario are delivered anew to the new process. After every loop pass the emulated screen memory is compared with the engine's frame. Current is computed from the state of the chips.

```sh
cmake -B build && cmake --build build -j
./build/keychain_sim ./build/keychain_board build/sim             # all scenarios (6–17 min, depending on the machine)
./build/keychain_sim ./build/keychain_board build/sim meeting pocket   # selected ones
```

Any scenario can also be run on its own: `boot` always runs before it. Neighbor B from `meeting` and `attack` doesn't take part in the other scenarios.

| Scenario | What it checks |
|---|---|
| `boot` | first power-on: writing 3.3 V to UICR and reboot, screen and sensors respond |
| `sensors`, `sound` | tap and double tap (and a slow double tap — without an extra single), shake, 60 steps, flipping over, fall and impact, frost; clap, music, silence, speech |
| `pocket` | darkness, the screen turns off after a minute, current in a pocket (below 170 µA), a tap and light bring the face back; the microphone after the pocket without false events |
| `charge` | start, end and disconnection of charging; after disconnecting at full — no "low charge", charge ≥ 90 %; on USB the 64 MHz crystal is requested both after a microphone restart and after USB bouncing between loop passes (TinyUSB drops the request on disconnect); a cable for 1.9–2 s — the percentage doesn't drop |
| `chargetemp` | frost −5 °C — charger off and the console explains why; 24 °C — charging on; 50 °C — stopped; thresholds 10/7 and 42/45 °C on the die from both sides of the hysteresis band; no false "charged"; the USB charge estimate holds while charging is blocked |
| `meeting` | two keychains a meter apart: first one hears the other (the scenario waits for the first packet with a margin of up to 15 minutes; usually it's 1–3 minutes: scan windows are rare, and search time is a matter of luck), then a duel on both simultaneously, one winner, vibration — about 3 V on the motor; while the motor buzzes, its kick and a loud sound are not events; 10-minute cooldown |
| `lossy` | an encounter with 40 % of packets lost: the same scene on both, simultaneously |
| `attack` | 4000 corrupted packets, spoofed advertisements and a flood of made-up keychains |
| `i2c` | the accelerometer and light sensor disappear and come back, LIS3DH settings reset, SDA held low, SCL stuck (watchdog reset, then a face without boot loops), no sensors at power-on |
| `oled` | a hung display controller is reset by powering it off (supply below 1 V); no screen for 2 minutes — the keychain works, the screen is back — face; the controller holds SDA low at power-on and in the middle of operation — the screen restarts |
| `dbus` | display bus: SCL low, SCL shorted to SDA — no more than one watchdog reset, console, charging and button power-off work; bus released — face |
| `clock` | clock lost after power-off at 23:30 — normal brightness during the day, radio listens; a new board for 11.5 h without a clock — not night |
| `off`, `offusb` | power-off with the button on battery and while charging, power-on, growth and clock preserved; a hold used to wake doesn't power off; button + USB — bootloader, including 8 s after a press in the middle of a long scene: goodbye right after a 3 s hold, screen dark before 8 s; USB later than 10 s after the press — power-off on USB; powered-off current |
| `dfu` | when the bootloader is entered (gesture, `dfu`, 10 s of button while charging) and when not (USB without the button, booting already on USB, button held longer than 10 s); clock intact afterwards. The bootloader's own rules (button needed together with USB, 3 and 10 s waits, no charging) are a model; only the first board verifies them |
| `stuck` | button stuck when powered off while charging: USB disconnected — sleep without the button, only USB wakes it |
| `watchdog`, `console` | loop hang and watchdog reset; a console nobody reads |
| `buttons` | press series: one — exactly one `button` event and nothing toggles, two — duels, three — sound, and slow ones (180–250 ms presses, 220–250 ms gaps); 450 and 700 ms gaps — counted one by one; two presses and a hold — power-off only |
| `factory` | `factory yes`: growth, achievements, known keychains, settings and clock — factory defaults, the keychain powers off, presses during the goodbye aren't saved; after power-on — day one, classic theme, sound and duels on, ghost mode off, brightness 127, time not set |
| `cuts` | 30 power cuts, 10 of them in the middle of a flash write or erase |
| `empty` | the cell discharges — the keychain powers itself off before the protection cutoff; the button on an empty cell doesn't fully boot it; after charging — back to normal |
| `wrap` | 48.5 days of uptime: core tick overflow |
| `days` | no clock: 25 h of uptime with two power-offs — a new day after exactly 24 h of uptime |
| `day` | consumption over a day by mode, from the morning (turned off at night, cell charged): 10–16 mAh |

Results go to the given folder: `report.txt`, each board's console with timestamps (`console-A.txt`…) and GIFs of what the panels showed (`meeting.gif`, `sensors.gif`, `pocket.gif`). In CI the run is the `keychain_sim` test, under AddressSanitizer and UBSan.

What the simulation doesn't replace: the analog part (checked in SPICE, see [`hardware/rev-b`](../hardware/rev-b/README.en.md)), real radio timing, the behavior of a specific display module, leakage current, the tap threshold (a tap in a scenario is a ready-made CLICK_SRC flag, not samples) and kicks from its own motor seen by the accelerometer, the orientation of the accelerometer axes on the board, the level on CHG_IO at low VBUS (the charger status in the model is pure logic) and external chips during an nRF reset: in the model every reset starts the LIS3DH, the screen and the bus slave from scratch, while on the board they keep their state (the LIS3DH keeps its settings, FIFO and tap flag). That's why the first board still goes through the checklist below.

## What to check on the first board

- [ ] **Power.** About 1.8 V on the 3V3 test point while the board is unflashed, and about 3.3 V after flashing and reset. SYS is the cell voltage, and from USB — VBUS minus 0.15–0.4 V across the diode, up to ~5.4 V.
- [ ] **Bootloader and firmware.** After the programmer, the KEYCHAIN drive is visible over USB (only the bootloader written) or a face appears right away. The version from `ver` matches the one in [`release/`](release/README.en.md), and for your own build for sale — your tag, without `-dirty`. `sensors`: the first line is `Ускорение: x …` ("Acceleration: x …") or «Ускорение: нет новых выборок» ("Acceleration: no new samples"; repeat), not «Акселерометр не отвечает» ("Accelerometer not responding"); the second starts with `Свет: есть` ("Light: present"). Firmware installs from the drive and via `pio run -e keychain -t upload`. The "hold the button and plug in USB" gesture and the `dfu` command open the drive; unplug USB — the face is back.
- [ ] **Screen.** Boot animation, then the face. `b 255` and `b 10` — brightness changes.
- [ ] **Accelerometer axes** (`sensors`). Hanging straight — `y` about +1; right edge down — `x` about −1; lying face up — `z` about +1. If a sign or axis is wrong, fix `kAccMap` in [`board.h`](src/keychain/board.h).
- [ ] **Touch.** Tap and double tap on the case: `v` prints `tap` and `double_tap`. The threshold is `CLICK_THS` in [`sensors.cpp`](src/keychain/sensors.cpp).
- [ ] **Motion.** Shake, walk 10 steps (`me` — steps go up), turn upside down. The FIFO is read in bursts of 10 samples (the address wraps back to OUT_X_L, AN3308). If the gaze tilt jumps or `sensors` prints garbage, set `kBurst = 1` in [`sensors.cpp`](src/keychain/sensors.cpp).
- [ ] **Light.** `b 255`, then cover the sensor with your palm for 8 s — `dark` (after 4–7 s: the sensor must not mistake the screen's own glow for light). A flashlight — `bright`, also after 4–7 s. The sensor measures one-shot, about every 2 s, and the next measurement confirms the new state. From dark to light — after 2–4 s; if the keychain is picked up with the screen dark — after about 2 s.
- [ ] **Sound.** A clap — `loud`, music nearby for 5–10 s — `music`, speech — `speech`. `v on` and `say laugh`: its own buzzer produces no events. Take the keychain out of a pocket in a quiet room — neither `loud` nor `music`.
- [ ] **Buzzer and vibration.** `say laugh`; an encounter of two keychains — vibration, and the motor kicks don't become `tap`.
- [ ] **Charging.** Plug in USB — the console stays silent or reports the temperature, the red LED is on, `sensors` — «заряжается да» ("charging yes"). Unplug — the scene ends, the percentage doesn't drop.
- [ ] **Charging in cold and heat.** Cool the keychain to 0 °C (in a bag against condensation, in a freezer) and plug in USB: «Заряд не идёт», the LED doesn't light brightly (a faint glow is normal). Warm it up — at 10 °C on the die, «Заряд снова разрешён». With a hair dryer from a distance, up to ~50 °C on the case, no higher: «Заряд остановлен». A keychain in the bootloader (KEYCHAIN drive) doesn't charge — the LED doesn't light brightly (a faint glow is normal).
- [ ] **Encounter.** Two keychains in the light, 1 m apart, neither in a pocket nor asleep. Usually within 1–3 minutes, occasionally up to 5–7: before the first encounter the keychain listens in a 150 ms window every 30 s. The scene runs on both at the same time. Out of the box duels are on for both, so the first scene is a duel (DUEL, then WIN! or LOST, vibration) followed right away by the coded chat, without the NEW FRIEND greeting. The greeting appears if you turn duels off on one keychain with a double press. With the same neighbor — again no sooner than 10 minutes later. Nothing within 5 minutes — check the light, the distance and `me` on both: «невидимка выкл» ("ghost off").
- [ ] **Power-off.** A 3 s hold — GOODNIGHT immediately, even in the middle of a scene, the screen goes dark about 7 s after the press, then sleep; a press — boot again. Waking it by holding the button for 5 s doesn't power it off again.
- [ ] **Powered-off current** — below 25 µA, on every board before the cell is soldered: PPK2 on the BT1 pads, keychain powered off with a button hold, USB disconnected ([how](../hardware/rev-b/README.en.md#first-power-on)). If higher — look for a leak: the accelerometer's SA0 pull-up (SA0 to VDD, the firmware writes CTRL_REG0 = 0x90), D1 and R6 near VBUS.
- [ ] **Current with the screen** — on the measurement board, without a cell: PPK2 as a 3.7 V source on the SYS and GND test points on the back ([measurement board](../hardware/rev-b/README.en.md#on-the-first-boards)). Average current with a face and during sleep — estimates in the [power budget](../docs/power.en.md). In a pocket with the screen off — about 0.13 mA. If noticeably more than 0.3 mA, first check whether the 64 MHz crystal is released after the microphone starts (`mic_begin` in [`hal.cpp`](src/keychain/hal.cpp)). Powered off with the screen — also below 25 µA; more than this board drew before the screen — leakage through the screen pins with the switch off.
- [ ] **Screen on an almost empty cell** — on the same measurement board. Over USB, with the PPK2 disconnected, send `b 255` and wait 10 s: the brightness will be saved to flash. Unplug USB and only then turn on the PPK2 source — 3.45 V, then 3.40 V, a minute at each: the face is steady, without flicker or reboots. Then 3.35 V: the face holds until the keychain powers itself off after a minute («Ячейка почти пуста»). If it goes dark or flickers earlier, the display module has a weak regulator: don't use that module. After this measurement disconnect the PPK2 and plug in USB once: otherwise a keychain that shut down on an empty cell keeps showing an empty battery.
- [ ] **The board will be given away after testing** — last of all, over USB, `factory yes` ([console](#console)): the tests left brightness, a known neighbor, XP and a duel on it. The keychain says goodbye and goes dark; unplug USB — it is off. Don't turn it on again — the new owner will.

## How it works

| File | What's inside |
|---|---|
| `src/keychain/main.cpp` | loop: sensors → personality → screen and voice; button, encounters, charging, sleep, console |
| `src/keychain/sensors.*` | LIS3DH (FIFO, taps, settings check) and LTR-303ALS |
| `src/keychain/hal.*` | power and REG0, charging and CHG_EN, temperature, buzzer on PWM3, vibration, PDM microphone, watchdog, I2C bus recovery, power-off |
| `src/keychain/radio.*` | scheduled BLE advertising and scanning |
| `src/keychain/storage.*` | flash journal; PC test — `host/storage_test.cpp` |
| `src/keychain/board.h` | board pins and accelerometer axes |
| `tools/uf2.py` | after the build: checks where the firmware ends, `firmware.uf2` and `firmware-factory.hex` with REGOUT0 |
| `tools/sale_ids.py` | before the build: IDs from the `[sale]` section in `platformio.ini` — the firmware's USB VID/PID (replacing the board definition's `hwids`) |
| `host/keychain/` | board simulation: chip models (`models.cpp`), board process (`board.cpp`), coordinator and scenarios (`keychain_sim.cpp`) |
| `bootloader/` | the Adafruit bootloader variant for the board and the ready-made `.hex` ([how to rebuild](bootloader/README.en.md)) |

Sensor processing (`face/sensing.h`) and encounter handling (`face/encounter.h`) live in the engine and are tested on a PC. The firmware only reads the hardware and passes numbers along.
