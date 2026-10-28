English | [Русский](plan.md)

# Development plan

## Principles

- **The face is always on screen.** The screen goes dark only when nobody can see it: in a pocket, in a bag, in the dark.
- **Our own face engine**: `engine/`, with no third-party face code or animations.
- **Transparent case.** An acrylic "sandwich": the boards are visible right through it, and the cell is enclosed on all sides ([hardware](hardware.en.md), [case](../hardware/rev-b/case/README.en.md)).
- **Straight to our own rev B board.** We skipped the XIAO carrier board (variant A): the rev B firmware is fully verified by the [full-board simulation](../firmware/README.en.md#whole-board-simulation), and faces on a real panel by the ESP32 stand.

## Stages

| Stage | Status | Result | Milestone |
|---|---|---|---|
| 1. Engine and simulator | **done** | 139 emotions, transitions, gestures, behavior, simulators on PC and in the browser | **M1: the face lives on PC** |
| 0. Purchasing and test stand | in progress | ESP32-C5 stand built, emotions checked on a real screen | — |
| 1b. Personality and surprises | **done** (software) | Each keychain has its own personality, growth and implants, scenes, cyberpunk theme, tilt, encounters | — |
| 2. Live prototype | ahead | Face on the rev B board, first current measurements | **M2: face on screen** |
| 3. Power for an always-on face | estimate done | Current by mode in the board simulation; PPK2 measurements and discharge test ahead | — |
| 4. Sensors and behavior on hardware | firmware done | Thresholds and a day of wear without false triggers, on a real board | — |
| 5. Emotions on a real panel | partial | Catalog on the stand; check from keychain distance ahead | — |
| 6. Variant A | not done | Went straight to rev B; XIAO is the fallback | — |
| 7. Mechanics | case ready for cutting | Case S, M, L; drop, pull and splash tests ahead | **M3: a week of wear** |
| 8. Variant B: rev B board | **ready to order** | Own board, firmware, bootloader, case | **M4: target battery life** |
| 9. Reliability and polish | partial | Flash log, watchdog, USB update; long-term tests ahead | — |

### 1. Engine and simulator — done

- [x] Framebuffer in SSD1306 format, dirty-window detection.
- [x] Rasterizer: rings, rounded rectangles, ellipses, thick lines and arcs, polygons, icons.
- [x] Parametric face: eyes, eyelids, eyebrows, 12 mouth types.
- [x] Smooth transitions; shape changes are hidden behind a blink.
- [x] Blinking, saccades, anti-burn-in drift, a signature gesture for each emotion.
- [x] 139 emotions in 10 categories, 38 icons, 17 clips.
- [x] Behavior: sensor events → reactions from pools, mood, daily rhythm, idle activities.
- [x] Power saving: render skipping by hash, its own frame period for each motion.
- [x] Simulators: `facegen`, `facesim` (SDL2), web version (WebAssembly).
- [x] Tests (1000+ checks) and CI.

### 1b. Personality and surprises — done in the engine and in the board firmware

- [x] Personality from the serial number: 10 archetypes, appearance traits, 10 % rare, 1 % legendary with a cyber eye.
- [x] Growth: 44 emotions at start, 11 implants earned with experience (days, taps, steps, encounters, duels), all within ~a month.
- [x] Growth saved to flash with a checksum.
- [x] Scenes: OLERAST LINK boot, hack, data rain, ERROR 404, reboot, hide-and-seek, upgrade, New Year, birthday, Halloween, encounter, duel, secret conversation.
- [x] Surprises every 1–2.5 days, holidays once per date.
- [x] Cyberpunk theme: targeting reticle when there is a reason (intense emotions, scanning, encounter), rare signal glitches.
- [x] Pupils roll when tilted; a jerk makes the gaze shift in the direction of motion.
- [x] Encounters over BLE on nRF52840 ([protocol](encounters.en.md)); the simulation runs two boards and the air between them. On real boards, stage 2.
- [x] Tilt, shake and steps from the accelerometer in the board firmware; thresholds on a real board (stage 4).
- [x] Voice: buzzer phrases with each personality's own manner, scene music, quiet hours, a ciphered secret conversation after a duel ([keychain voice](sound.en.md)).
- [x] Day streak and 24 achievements, NEW on an unlocked emotion, progress format version 3 with migration of old records.
- [x] Keychain friendship by days of encounters, a gift from the elder, whispers of old acquaintances, shared scenes at the front of the queue.
- [x] Battery: charging scene with percentage, eco mode, experience for care, shutdown.
- [x] Retro theme; easter eggs by date and time, a secret knock (on the stand and in the simulator; not yet on the rev B board, stage 4), blue screen, visor, charging screensaver.
- [x] Vibration on encounters and duels.

### 0. Purchasing and test stand

- [x] Stand firmware for ESP32-C5, ESP32, C3 and S3 + SSD1306 and a touch button: live mode, catalog display, Serial commands ([firmware/](../firmware/README.en.md)).
- [x] Build the stand on ESP32-C5 and go through the [live panel checklist](../firmware/README.en.md#what-to-check-on-a-real-panel): emotions are readable; fixed proximity to the edge and thin lines.
- [ ] Buy parts from the [list](hardware.en.md#shopping-list-for-assembly) (assembly purchases).
- [ ] Measure screen modules from different sellers: dimensions, holes, component height on the back, controller, pinout, LDO.
- [ ] Measure 402025, 502025 and 602025 cells from the batch: thickness, width, length, wire exit.

### 2. Live prototype — on the rev B board

- [ ] SSD1306 driver over I2C:
  - [x] initialization, contrast, window transfer, display off, SH1106 in the driver, address detection 0x3C/0x3D;
  - [ ] VCOMH and precharge from measurements;
  - [ ] autodetection of SSD1306, SSD1315 or SH1106.
- [x] Engine in the board firmware (`firmware/src/keychain`), frames are checked against the screen in simulation.
- [ ] First board per the ["What to check"](../firmware/README.en.md#what-to-check-on-the-first-board) list: power, bootloader, screen, accelerometer axes, taps, sound, charging, encounter, shutdown.
- [ ] Measure frame render and transfer time on nRF52840.
- [ ] PPK2 measurements:
  - face at different contrasts;
  - static frame and animation;
  - pocket with the screen off (estimate 0.13 mA);
  - keychain switched off (limit 25 µA).
- [ ] Replace estimates in the [power budget](power.en.md) with measurements.

### 3. Power for an always-on face

- [x] Sleep between frames, frames only when the face changes.
- [x] Light sensor: adaptive brightness, darkness detection with hysteresis, screen unpowered in a pocket.
- [x] Current by mode in the board simulation: face ~2.7 mA, dimmed ~0.9 mA, pocket ~0.13 mA ([power budget](power.en.md)).
- [x] Core for rev B: Adafruit's Arduino core with SoftDevice S140.
- [ ] Microphone in windows, contrast by lux, I2C with the core sleeping, event-driven loop: list in the [power budget](power.en.md).
- [ ] Experiments with contrast, VCOMH, precharge and retro mode on a real panel.
- [ ] Discharge test of S, M, L cells: "16 h face + 8 h dark".

### 4. Sensors and behavior on hardware

- [x] Accelerometer in firmware: hardware tap and double tap, shake, fall, flip, steps; jolts from our own motor produce no events.
- [x] Microphone: clap, speech, music; the microphone does not hear our own buzzer and motor.
- [x] Charging and battery level: scene, eco mode, shutdown before cutoff, charging only at 10–42 °C by the nRF die temperature.
- [ ] Record data from a backpack (`v on`): walking, running, stairs, transport. Tune thresholds, filter out false taps.
- [ ] Check whether the accelerometer and microphone hear the motor and buzzer through the case.
- [ ] Slow double tap on a real board: the single-tap window in firmware is 0.5 s, while the LIS3DH double-tap window is ~0.72 s. A second tap later than ~0.4 s may produce both a tap and a double tap. Measure first, then adjust `TIME_WINDOW` or the firmware window.
- [ ] Secret knock on the rev B board: it does not trigger yet, and the KNOCK KNOCK achievement cannot be earned (23 of 24 on the keychain). A single tap reaches the engine only after waiting 0.5 s for a double tap, and the sensor itself merges a "knock-knock" into a double tap. The engine needs the time of each sensor click, based on measurements on a real board, plus a knock scenario in the board simulation.
- [ ] Simulation: a tap detector working on samples instead of a flag from the scenario, and a "motor → accelerometer" coupling based on a measurement on a real board.
- [ ] A day of wear: false-tap counter, "does it get annoying".

### 5. Emotions on a real panel

- [ ] Contact sheet on the panel from 30–50 cm and from keychain-on-a-bag distance.
- [ ] Fix small icons that are not readable on 0.96″.
- [ ] Compare similar pairs: fear and surprise, sadness and tiredness, anger and irritation.

### 6. Variant A: XIAO carrier board — not done

We went straight to our own rev B board. The XIAO nRF52840 Sense remains a fallback in case the first rev B batch is delayed.

### 7. Mechanics

- [x] Acrylic "sandwich" case for laser cutting, three versions S, M, L for 402025, 502025, 602025 cells ([case](../hardware/rev-b/case/README.en.md)).
- [ ] Drop from 1–1.5 m onto tile, on every face, ×10.
- [ ] Pull on the lanyard eyelet at 5–10 kg, abrasion by a key ring over a week.
- [ ] Splashes, heat in a car.
- [ ] Decide: conformal coating on the back, a bumper for the glass edges.
- [ ] A week of wear on a backpack.

### 8. Variant B: our own board

**Rev B is ready to order** (`hardware/rev-b`, 30 × 46 mm plus eyelet, 4 layers):

- [x] Schematic: MDBT50Q module (nRF52840), MCP73832 charger enabled by firmware, USB/battery power separation, LIS3DH, LTR-303ALS, PDM microphone, buzzer and vibration motor on switches, USB-C with CC resistors, TVS and ESD, Tag-Connect pads for SWD, screen power switch.
- [x] Layout: ground on L2, power rails and slow signals on L3, antenna at the edge with no copper; DRC: 0 errors, 0 unconnected, 0 warnings.
- [x] Gerber, BOM and CPL for JLCPCB, BOM and placement for PCBWay, assembly drawings, hand-assembly list; a 2-board assembly panel (`hardware/rev-b/out/panel`).
- [x] Fixes from the pre-release audit: accelerometer SA0 to VDD, REG0 capacitance, screen jumpers, temperature-gated charging, charge status levels, VBUS protection ([board README](../hardware/rev-b/README.en.md#pre-order-review), pre-order check).
- [x] Board firmware: all sensors, voice, vibration, BLE encounters, sleep and shutdown, flash log, bootloader with a UF2 drive, factory file with bootloader and UICR (`firmware/src/keychain`, `firmware/bootloader`).
- [ ] Order from JLCPCB: PCB, 5 panels (the minimum), of which 3 assembled = 6 boards (PCBA requires at least 2 panels); 76 × 72 mm panel, 2 boards, from `hardware/rev-b/out/panel` ([panel](../hardware/rev-b/README.en.md#assembly-panel), [order](../hardware/rev-b/README.en.md#ordering-from-jlcpcb)). Order number: "Order Number (Specify Position)"; it goes onto the label under the cell. By hand: screen module, cell, vibration motor.
- [ ] Bring-up on a real board: checklists in the [board README](../hardware/rev-b/README.en.md#first-power-on) (first power-on) and the [firmware README](../firmware/README.en.md#what-to-check-on-the-first-board) (what to check on the first board).
- [ ] Measurements: off ≤25 µA, pocket ~0.13 mA, face ≤2.7 mA; discharge test.
- [ ] USB hot-plug: oscilloscope on VBUS and VSYS, short cable, 10 plug-ins ([how](../hardware/rev-b/README.en.md#on-the-first-boards), section on the first boards). According to the ngspice model, ringing briefly exceeds the module's 5.8 V limit; TVS D3 does not suppress it. Record the peak and the time above 5.8 V: this is data for the rev C damper. For our own rev B prototypes the risk is accepted; before any sale the measurement is mandatory. The pass criterion and what to do if it fails are in the [board README](../hardware/rev-b/README.en.md#on-the-first-boards).
- [ ] Charging temperature sensor: keychain in the fridge for an hour (+4…6 °C), then straight to USB: `sensors` shows a temperature within 2 °C of a kitchen thermometer next to it. From the freezer: the console prints «Заряд не идёт» ("Not charging"), the LED does not light brightly. Repeat on several keychains from each batch.
- [ ] Radio inside the case: encounter range with two keychains, −70 dBm threshold ([encounters](encounters.en.md)).

**Before selling rev B**: only if the keychains are ever sold; not needed for gifts:

- [x] Encounter packet without a Company ID: our own free UUID in the service data ([encounters](encounters.en.md#advertising-packet), advertising packet).
- [ ] Our own USB VID/PID instead of Adafruit's numbers: in the `[sale]` section of `firmware/platformio.ini`, rebuild the bootloader. Build from a tag, without `-dirty`. The `firmware/release/` files are for prototypes only ([bootloader](../firmware/bootloader/README.en.md#how-to-rebuild), how to rebuild; [encounters](encounters.en.md#advertising-packet), advertising packet).
- [ ] Cell datasheet (charge current from 100 mA) and screen module check on the stand ([board README](../hardware/rev-b/README.en.md#what-to-check-before-ordering), what to verify before ordering).
- [ ] Tests before the first sale: USB ringing ([measurement](../hardware/rev-b/README.en.md#on-the-first-boards)) and a pilot batch (below). Optional for gifts; the most useful and free test is to carry one keychain for a week or two before giving away the rest.

  **Pilot batch**: for example, 5 keychains, at least one of each version you will sell:

  | Test | How | Passes if |
  |---|---|---|
  | Drop | from 1–1.5 m onto tile, on each face, 10 times total | the case has not cracked at the screws or the eyelet, the cell is not visible, the face and the button work |
  | Eyelet pull | a weight or luggage scale at 5–10 kg on the ring | no cracks at the eyelet hole |
  | Cold charging | an hour in the fridge (+4…6 °C), then straight to USB and `sensors`; then the same from the freezer | the temperature in `sensors` is within 2 °C of a thermometer next to it; from the freezer the console prints «Заряд не идёт», the LED does not light brightly |
  | Empty cell | discharge fully | the keychain shows LOW BATTERY and GOODNIGHT by itself and goes dark; a button press briefly shows an empty battery; works after charging |
  | Current | PPK2 ([how to measure](power.en.md#how-to-measure)) | off below 25 µA, pocket no more than 0.3 mA, face in light no more than 2.7 mA |
  | Run time | full cell, 16 h of face in light and 8 h dark per day, until shutdown | record the days for each version; use these days in the guide and listings instead of the calculation |
  | Charge cycles | 50 cycles: full charge, then discharge to shutdown with the face in light (about 2 days per cycle for S). This takes months, so start on the very first assembled keychain | the cell has not swollen, the lid and bottom are not separating, run time has not dropped noticeably |
  | A week of wear | on a backpack, with keys | no cracks, the button does not press itself, the face has no stripes |

- [x] Owner's guide: [user-guide.en.md](user-guide.en.md).
- [ ] Guide for the box: print the [card](user-guide.en.md#card-for-the-box).
- [ ] Every keychain before shipping: the checks from [first power-on](../hardware/rev-b/README.en.md#first-power-on), with `factory yes` last ([console](../firmware/README.en.md#console)).

**Rev C: fully factory-assembled.** Rev B remains the working version; rev C is developed alongside in `hardware/rev-c`:

- [ ] Screen without its own board: a bare SSD1306 glass with a flex cable and an FPC connector placed by JLCPCB; the screen plugs into the connector. Controller support parts (charge pump capacitors, VCOMH, IREF) go on our board.
- [ ] A keyed battery connector (JST or similar): the cell plugs in, with no soldering and no risk of reversing it. Also needed for the EU: from 18.02.2027 the buyer must be able to replace the cell with ordinary tools (Regulation (EU) 2023/1542, Article 11). The cell must be removable without removing the screen, which the FPC-connected screen helps with.
- [ ] Reverse polarity protection for the cell: a P-MOSFET or an ideal diode in VBAT. Right now only the cell's own protection board saves it from a reversed cell.
- [ ] Cell temperature: an NTC under the cell and a charger with a thermistor (TS) input instead of enabling charging by the nRF die temperature.
- [ ] Vibration motor: find a surface-mount model at JLCPCB/LCSC; if there is none, the wire pads stay.
- [ ] USB-C shell tabs outside the cell outline: right now their solder comes out on the front side directly under the cell.
- [ ] Larger cell polarity marks, with "+ VBAT / − GND, red = +", on the back too.
- [ ] D1 with low leakage in heat: a hot diode slightly raises VBUS without USB. The rev B firmware does not check for this and detects USB by VBUSDETECT alone. A false "USB" is possible only with a very hot D1 and goes away by itself at VBAT ≈ 3.35 V; the worst outcome is a late shutdown on an empty cell. Optionally in firmware: in `poll_battery`, count USB only when VDDH ≥ 3.8 V.
- [ ] A damper on VBUS at connector J1: an RC network or a lossy capacitor (~0.5–1 Ω in series, ≥10 µF). On hot-plug with a short cable, per the ngspice model, VBUS and VSYS ring above the module's 5.8 V limit (VBUS and VDDH), and TVS D3, a pulse suppressor with 6.4–7.1 V breakdown, does not suppress the ringing. 10 Ω alone in front of the module's VBUS pin is not enough: it does not protect VDDH (which is VSYS) or the charger. Verify the circuit in ngspice and commit it to the repository.
- [ ] CHG_IO charge status: pull the "1" level up to VDD, not from VBUS through the LED. Then the level is correct both on a board without firmware (VDD 1.8 V) and on weak USB below 4.6 V.
- [ ] 100–330 Ω in series in SWDIO and SWDCLK: before the first flashing the board runs at 1.8 V, and a programmer with a fixed 3.3 V pushes current through the pins' protection diodes.
- [ ] Antenna keep-out lower along the right edge (down to y ≈ 16 mm): in rev B that is the only path for signals from the module to SWD and the button.
- [ ] Component-free strips along two opposite board edges, for rails or panel tabs. In rev B the tabs are on the left and top edges plus one short tab on the bottom, between the USB-C and the right screw. There are none on the right edge (button and module with antenna).
- [ ] Move button SW1 outward by 0.35–0.4 mm (or cut out the board edge at the button): right now the plunger extends past the edge by only 0.17 mm with 0.2 mm of travel, and the actuator needs wings under the board.
- [ ] Clearance for thick USB-C plugs: in rev B the lower cell frame sits directly above the plug (a 6.5 mm plug, the standard's limit, touches it by 0.02 mm). In rev C: a lower receptacle or the cell end further from it, with at least 1.5 mm of acrylic between the plug window and the cell.
- [ ] Buzzer sound output to the side: right now it sounds into the bottom through a Ø3 mm hole, and on your back or in a bag the keychain is quieter.
- [ ] Re-placement and routing with the same pipeline (`build.py place --force`, `route`, `fab`), DRC 0/0/0. `place` without `--force` will not overwrite a routed board; with it, the board stays without traces until `route`. The 0.5 mm rule between holes of different nets is already in the DRC (`keychain-rev-b.kicad_dru`); keep it.
- [ ] A new case for the thinner stack: no screen module and no stack of frames under it.
- [ ] A 20 × 30 mm cell (402030–602030) if the board under it grows: the "16 h of face ≥5 days" target is currently met only by the L version ([power budget](power.en.md)).
- [ ] Screen switch: R7 and R9 at 1 MΩ instead of 100 kΩ (−60 µA with the screen on, soft start remains).
- [ ] 32 kHz crystal: a more accurate clock and −1–2 µA.

### 9. Reliability and polish

- [ ] 4 weeks of the face 24/7, then inspection on a white screen.
- [ ] 50+ charge cycles, low-battery behavior, charging in cold and heat on a real board.
- [x] Watchdog, I2C recovery, data protection in flash (log, power-loss test).
- [x] Firmware update over USB: UF2 bootloader, entered by holding the button while plugging in USB; a hung firmware is reset by the watchdog, and the bootloader sees the button again.
- [ ] Emotion packs over USB.

## Risks

| Risk | Symptom | What we do | When we check |
|---|---|---|---|
| Battery life below target | Face above 2.7 mA, pocket above 0.3 mA | Microphone in windows, contrast by lux, L cell | Stages 2–3 |
| Current of a switched-off keychain | Above 25 µA | Look for the leak: SA0, D1 and R6, screen switch | Stage 2 |
| Screen burn-in | Marks on a white screen | Drift, gestures, contrast ≤0x7F | Stage 9 |
| False taps while walking | Reactions for no reason | Hardware tap with windows, backpack recordings | Stage 4 |
| Variation among "SSD1306" modules | Offset, garbage, does not turn on | Several sellers, a module from each batch on the stand before soldering, power-sequence jumpers, controller autodetection | Stages 0–2 |
| Cell thicker than nominal | The screen module does not sit on the frame tabs | Measure the batch, use the next case version up | Stage 0 |
| Solder bridge at the screen pins | Short on the screen supply | Continuity check before power-on, jumpers moved away | Stage 2 |
| Fragile screen flex cable | Stripes after impacts | Kapton and UV glue, then variant C | Stage 7 |
| Cell damage | Swelling, punctures | Cell between the boards in a frame, protection board, cutoff, charging only when warm | Stage 7 |
| Face not visible in sunlight | Unreadable outdoors | Maximum contrast in bright light | Stage 7 |
| Eyelet breaks | Cracks at the hole | FR4 1.6 mm, material margin, pull test | Stage 7 |
| Radio worse inside the case | Encounters only at very close range | Nylon M2 × 25 screws and nuts, RSSI threshold from measurements | Stage 2 |
| Ringing on USB hot-plug | VBUS or VSYS above 5.8 V on the oscilloscope | Rev B: risk accepted for prototypes, measurement before sale; if it fails, recommend only a tested charger and cable or wait for rev C with a damper at J1 | Stage 2, before the first sale |
| Charging in the cold | In the fridge, `sensors` differs from the thermometer by more than 2 °C | 10 °C die threshold with margin, a "warm it up first after the cold" note in the guide; rev C: NTC | Stage 2 |

## Decisions

1. **Darkness:** the screen is powered down after a minute of darkness; at night with lights on, a dimmed face (night is determined only by a set time: there is no battery-backed clock).
2. **Cell:** three case versions: 402025, 502025, 602025.
3. **Bluetooth:** connectionless keychain encounters ([protocol](encounters.en.md)); advertising and short listening add a little to the average current. Updates go over USB.
4. **Back side:** transparent case bottom, the board is visible.
5. **Charging:** only when enabled by firmware and at 10–42 °C by die temperature (starts at 10 °C, stops below 7 °C and above 45 °C); rev C: NTC.
