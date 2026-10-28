English | [Русский](hardware.md)

# Hardware and mechanical design

The rev B keychain is a custom 30 × 46 mm board with a lug for a key ring, a 0.96″ display module above it resting on the tabs of the acrylic frames, and a Li-Po cell in between. The whole thing is enclosed in a clear acrylic "sandwich". This page covers what is on the board and why. How to assemble and order the board is in the [board README](../hardware/rev-b/README.en.md), the case is in the [case README](../hardware/rev-b/case/README.en.md), current draw and battery life are in the [power budget](power.en.md).

| Variant | What's inside | Status |
|---|---|---|
| **A** | Standard SSD1306 module + carrier board with a Seeed XIAO nRF52840 Sense | early prototype plan; not built, fallback option |
| **B** | Standard SSD1306 module + custom board: nRF52840 module, charger, sensors | **rev B is ready to order** — [`hardware/rev-b`](../hardware/rev-b/README.en.md) |
| **C** | A single custom board with a bare display panel on an FPC connector | rev C in the [plan](plan.en.md): fully factory-assembled |

Variant A was skipped: the full rev B firmware is verified by the [full-board simulation](../firmware/README.en.md#whole-board-simulation), and the faces on a real panel by the ESP32 stand.

## Rev B board

![Rev B board: front and back](img/pcb-rev-b.png)

| Parameter | Rev B |
|---|---|
| Size | 30 × 46 mm plus an R4.5 lug with a 3.2 mm non-plated hole, 4 layers, 1.6 mm, ENIG, black solder mask |
| Layers | L1 and L4: components and signals, L2: solid ground, L3: 0.4/0.3 mm power rails and slow signals |
| Microcontroller | Raytac MDBT50Q-1MV2 module (nRF52840) with a chip antenna at the right edge, no copper under it |
| Power | MCP73832 (100 mA, charging enabled by firmware), USB/battery power path with a diode and a P-MOSFET, 3.3 V from the module's REG0 |
| USB protection | SMF5.0A TVS on VBUS (against surges and ESD), USBLC6 on D+/D− |
| Sensors and feedback | LIS3DH, LTR-303ALS, SPH0641 PDM microphone, PKMCS0909E buzzer, switched vibration motor, side button |
| Connectivity and debug | USB-C with CC resistors, Tag-Connect TC2030 pads for SWD |
| Display | 0.96″ I2C module above the cell, on the case frame tabs, power through a switch |
| Cell | 402025, 502025 or 602025 with a protection board — case versions S, M, L |
| Verification | DRC: 0 errors, 0 unconnected, 0 warnings; the firmware runs in the full-board simulation |

### Goals

| Goal | Current status |
|---|---|
| Battery life ≥5 days with 16 h of face and 8 h of darkness per day | simulated ≈3, ≈4.3 and ≈5.4 days for versions S, M, L; a typical day: 11–18 days ([power budget](power.en.md)) |
| Charging from USB-C at no more than 1C | 100 mA, no more than 0.7C at a nominal capacity of 150–250 mAh, only at 10–42 °C |
| 1–1.5 m drop, 5–10 kg pull on the lug, splashes | tests ahead (stage 7 of the [plan](plan.en.md)) |

### Pins and buses

| Signal | Pin | Goes to |
|---|---|---|
| SENS_SDA, SENS_SCL | P0.27, P0.01 | sensor bus (Wire): LIS3DH 0x19, LTR-303 0x29, 10 kΩ pull-ups |
| ACC_INT1, ACC_INT2 | P0.04, P0.17 | LIS3DH interrupts: tap (CLICK) and spare |
| ALS_INT | P0.00 | LTR-303 interrupt |
| PDM_CLK, PDM_DATA, MIC_PWR | P0.06, P0.08, P1.09 | microphone; powered from a pin, switched off completely |
| DISP_SCL, DISP_SDA | P0.13, P0.15 | dedicated display bus (Wire1), SSD1306 at 0x3C or 0x3D |
| DISP_EN | P0.19 | display power switch, high = on |
| VIB_EN | P0.21 | motor switch, 20 kHz PWM |
| CHG_IO | P0.20 | charger status, 0 = charging |
| CHG_EN | P0.24 | charge enable: 0 = charging, released = not charging |
| BTN | P0.22 | button to ground |
| BUZ_A, BUZ_B | P0.25, P1.02 | buzzer in a bridge through 1 kΩ |

Both I2C buses run at 390 kHz: at 400 kHz the nRF52840 TWIM violates timing (errata 219). There is no 32 kHz crystal; the clock runs from the internal RC oscillator, so P0.00 and P0.01 are ordinary pins. The pin numbers are recorded in `GPIO` in [`netlist.py`](../hardware/rev-b/gen/netlist.py) and in the firmware's [`board.h`](../firmware/src/keychain/board.h).

## Microcontroller: nRF52840

The face is always on, while the CPU spends nearly all its time waiting for the next frame or event. So idle current matters most, not peak performance.

- **nRF52840:** idle with a timer and RAM retention is 1.5–2.4 µA, running at 64 MHz about 3.3 mA. It has an FPU, a PDM interface for the microphone and BLE for keychain encounters.
- **ESP32-C3** draws 130–190 µA in light sleep, 50–100 times more. It has no FPU. It remains a fallback in case Wi-Fi is ever needed.
- **A module, not a bare chip.** The MDBT50Q-1MV2 already includes the antenna, a 32 MHz crystal and the regulator components. A custom antenna on a 30 mm board is an unnecessary risk.
- **Module power.** VDDH takes the system supply (VSYS, 2.5–5.5 V), and the built-in REG0 regulator provides 3.3 V on VDD for the module and the sensors. No separate LDO is needed: the load is 3–4 mA against REG0's 25 mA limit. On a blank chip REG0 outputs 1.8 V; 3.3 V is written to UICR by the factory firmware file or by the bootloader.
- **Capacitance on VDD** — 4.7 µF and four 100 nF, 5.1 µF in total: Nordic allows no more than ~5.5 µF for REG0.
- **USB** — built into the nRF52840: the KEYCHAIN drive for firmware updates, a console. The module detects that USB is connected through its VBUS pin.

## Display

- **White SSD1306 0.96″, a standard 4-pin I2C module.** A white panel ages more slowly than a blue one.
- **Why I2C rather than SPI.** 4-pin modules are the most common, and the engine sends only changed windows to the display. At 400 kHz a full frame (1 KB) takes ≈23 ms, a window covering both eyes (~256 B) ≈6 ms, which is enough for a face. It cannot go above 400 kHz: neither the nRF52840 interface nor the display controller supports it.
- **Dedicated bus and power switch.** The display has its own Wire1 bus, and its power goes through P-MOSFET Q2 from VSYS. The firmware switches the display off completely rather than just blanking it: a black but powered display draws 0.5–1.4 mA. Before turning the switch off, the firmware releases SDA and SCL so the module is not back-powered through the pull-ups. A capacitor between Q2's gate and drain turns the switch on gradually, without a large inrush current.
- **Marketplace modules are not all the same.**
  - Modules sold as SSD1306 may actually be SH1106 (shifted by 2 columns, garbage at the edge) or SSD1315. The board firmware expects an SSD1306; it finds the 0x3C or 0x3D address on its own.
  - The power pin order on the header may be `GND VCC` or `VCC GND`. It is selected by 0 Ω jumpers R16–R19 on the back: the factory fits the `GND VCC` option.
  - The module must have an LDO on board (662K / XC6206): it receives VSYS, which is up to ~5.4 V from USB.
  - So buy from 2–3 sellers and check dimensions, controller and pinout before soldering. You cannot tell the controller from the markings: modules are checked on the ESP32 stand ([how](../hardware/rev-b/README.en.md#what-to-check-before-ordering)).
- **Brightness is about 60 cd/m²**; in sunlight the face is harder to read. The firmware sets the contrast from the level given by the behavior: normal, 1/4 at night and during sleep, 1/16 in the dark.

## Sensors

| What | Part | How it is connected | What for |
|---|---|---|---|
| Motion | LIS3DH, ±4 g accelerometer | sensor bus, 0x19 (SA0 to VDD), taps on INT1 | tap and double tap, shaking, falls, flipping, steps, gaze tilt |
| Sound | SPH0641LM4H-1, bottom-port PDM microphone | sound through a 0.5 mm hole in the board and a 1.2 mm hole in the case lid; powered from a pin | "listening", startle at a clap, music, speech |
| Light | LTR-303ALS | sensor bus, 0x29, facing forward | display brightness, darkness in a pocket, night |
| Button | Omron B3U-3100P, side-actuated | on the right edge, plunger in the case | power on, power off, duels (two presses), sound (three presses), bootloader |
| Temperature | nRF52840 die sensor | inside the module | cold, hot; whether charging is allowed |
| Battery | VDDHDIV5 divider inside the nRF52840 | no external parts | charge percentage, eco mode, shutdown on an empty cell |

- **The accelerometer's SA0 goes to VDD.** This pin has an internal pull-up to VDD. Tied to ground, it would constantly draw 0.11–0.16 mA through that pull-up, even with the keychain off and in the bootloader — as much as the whole keychain in a pocket. The firmware also disables the pull-up (CTRL_REG0 = 0x90).
- **The light sensor faces forward**, the same way as the display: on a bag, a rear-facing sensor would see only fabric. It takes one-shot readings every 2 s.
- **The microphone** listens only while the keychain is awake: in a pocket, at night and in eco mode its power is off. The microphone does not hear the keychain's own buzzer and motor: while they run and for 150 ms after, sound is ignored. Jolts from its own motor are not touches: while it runs and for 150 ms after, shaking, impacts, falls and touches are ignored.

### Sound and vibration

| What | Part | How it is connected | Current |
|---|---|---|---|
| Buzzer | PKMCS0909E 9×9 mm piezo, no built-in oscillator | bridged across two pins through 1 kΩ: loud in antiphase, quiet from one pin | 0 at idle, a few milliamps while sounding |
| Vibration motor | 0820 coin motor (Ø8 × 2 mm, 3 V), on an adhesive pad | AO3400A switch with a diode from VSYS, 20 kHz PWM with a duty cycle of 3 V / VSYS | 0 at idle, ~60 mA in 50–200 ms pulses |

Sound and vibration are short and rare and barely affect the average current. The engine outputs notes (`sound.h`), the firmware plays them with PWM. Details are in [the keychain's voice](sound.en.md).

## Power

- **20 × 25 mm Li-Po cell with a protection board (PCM):**
  - 402025 — ~150 mAh, 4 mm, version S;
  - 502025 — ~200 mAh, 5 mm, version M;
  - 602025 — ~250 mAh, 6 mm, version L.

  The label capacity of cheap cells is often inflated, and the thickness is greater than nominal. Before buying a batch, measure a sample and check the capacity with a discharge test. The cell's datasheet must specify a maximum charge current of at least 100 mA ([cell datasheet](../hardware/rev-b/case/README.en.md#choosing-a-cell)). The thickness rule is in the [board README](../hardware/rev-b/README.en.md#stack-and-dimensions). The cell wires are soldered to pads; there is no connector. There is no reverse-polarity protection on the board either, so use only cells with a protection board.
- **Charging — MCP73832, 4.2 V, 100 mA:** at a nominal capacity of 150–250 mAh that is no more than 0.7C.
  - The MCP73832 has no thermistor input. So the bottom end of the PROG resistor goes to an nRF pin (CHG_EN), and charging is enabled by the firmware: only at a die temperature of 10–42 °C, stopping below 7 °C and above 45 °C. The check happens when USB is connected and every 30 s after that.
  - Why the margin on the cold side is so large. A lithium cell must not be charged below freezing: lithium plates onto the anode and the cell degrades. And according to Nordic's documentation the die sensor can be off by up to ±5 °C, and on USB the die is also a few degrees warmer than the cell: the charger and the nRF itself heat it. A 10 °C threshold on the die keeps the cell farther from zero even with the worst-case error. Only a thermistor at the cell would guarantee it, and that is in rev C. On the hot side the die errs in the safe direction: it is warmer than the cell.
  - Without firmware there is no charging, but the keychain still runs from USB.
  - The charge status reaches the pin through a 10 kΩ / 47 kΩ divider: a "1" stays at or below 3.3 V with VBUS up to 5.5 V.
  - Weak USB, below ~4.6 V on VBUS: the "1" may not reach the pin threshold, and the firmware reads a finished charge as "charging". This is only cosmetic: the "charged" scene will not appear, but the charger itself terminates the charge correctly. The end of charge is visible on the LED.
  - A board on USB without firmware (VDD 1.8 V): the "1" is above the pin limit of VDD + 0.3 V, and up to ~0.13 mA flows through R5 into its protection diode. This lasts only until the first firmware flash, which sets 3.3 V, and does not harm the pin; just don't leave an unflashed board on USB for long.
- **USB and battery power path.** From USB the system is powered through a Schottky diode (VSYS up to ~5.4 V), from the battery through a fully on P-MOSFET with almost no loss. There is no separate load switch or LDO: the display turns off its own switch, and REG0 provides 3.3 V.
- **USB-C:** 5.1 kΩ resistors on CC, USBLC6 against ESD on the data lines.
  - **SMF5.0A TVS on VBUS** — against surges and ESD: breakdown at 6.4–7.1 V.
  - **It does not damp hot-plug ringing.** The inductance of a short cable together with the ceramic capacitance on VBUS rings, and according to an ngspice model VBUS and VSYS briefly exceed the module's 5.8 V limit (VBUS and VDDH). This happens with a short, thick cable from a charger that always has 5 V on its socket (USB-A); a USB-C charger with a C–C cable turns on 5 V gradually and only after connection. The TVS starts conducting above that limit. The fix is a damper at the connector: an RC snubber or a lossy capacitor of ~0.5–1 Ω and ≥10 µF; it is in the rev C plan. A 10 Ω resistor in front of the module's VBUS pin alone is not enough: it does not protect VDDH or the charger.
  - **Check on the first board:** an oscilloscope on VBUS and VSYS, hot-plugging with a short cable ([how](../hardware/rev-b/README.en.md#on-the-first-boards)). For our own rev B prototypes the risk is accepted; before any sale the measurement is mandatory.
- **Shutdown is complete:** System OFF, sensors asleep, microphone and display unpowered. In this state the keychain draws 6–17 µA based on typical datasheet values; the limit when testing the first board is 25 µA.

## Mechanical design

### Stack, side view (front to back)

```
  ┌─ lug: board extension above the display module, light sensor on its front side
  ▼
[ lid: clear acrylic ]
[ OLED glass ]                                       ← 0.96" module, ≈4 mm including its board
[ module board ]
   ║ 2.54 mm pins through the board   ║ tabs of the two cell frames: 6, 7 or 8 mm (S, M, L)
[ cell 402025 / 502025 / 602025 on thin double-sided tape ]   ← in an acrylic frame
[ rev B board, FR4 1.6 mm, black mask, ENIG ]
[ back-side parts: USB-C on the bottom edge, button on the right, buzzer, motor ]   ← in the back frame
[ bottom: clear acrylic, Ø3 mm hole above the buzzer ]
```

The case is 35 × 51 mm (at the button the back frame has a 0.8 mm boss, the plunger sticks out another 0.5 mm); the acrylic layers are clamped together by four M2 × 25 nylon screws. The thickness and cutting layout of each version are in the [case README](../hardware/rev-b/case/README.en.md).

### Details that matter for durability

- **Lug.**
  - An extension of the 1.6 mm FR4 board, an R4.5 semicircle, a 3.2 mm hole for a split ring, not plated: the ring bears on the fiberglass itself, with ≈2.9 mm of material to the edge.
  - There is no copper on any layer within a 3 mm radius around the hole, so the ring cannot short or wear through anything.
  - Test: a 5–10 kg pull (stage 7 of the [plan](plan.en.md)).
- **The cell** is enclosed on all sides: the board behind, the display module and the clear lid in front, the case frames on the sides. At the USB-C connector, its end is covered by the lower cell frame: the frame has no window for the plug, and a plug up to ~6.4 mm passes under the frame ([USB-C plug](../hardware/rev-b/case/README.en.md#layers-from-face-to-back)). There must be no sharp leads nearby: the USB-C legs come through to the front side right under the cell, so they are clipped, filed almost flush and covered with Kapton.
- **The display flex cable** at the bottom edge of the module is the most fragile spot. The display frame does not clamp it. During assembly the flex is covered with a narrow strip of Kapton ([assembly step 5](../hardware/rev-b/case/README.en.md#assembly)); a drop of UV glue along the edge of the glass is optional. Whether more is needed will be decided after drop tests (stage 7 of the [plan](plan.en.md)). In variant C the flex goes under the board.
- **The button** is on the right edge (side-push), pressed by an acrylic plunger with wings under the board: the board keeps it from lifting and catching on the board edge. **USB-C** is on the bottom edge; the connector shell protrudes ~0.7 mm past the board edge into a cutout in the case.
- **The buzzer** sounds into the bottom, so the bottom has a Ø3 mm hole opposite it. A keychain lying on its back sounds quieter.
- **Finish:** R2 rounded corners, black matte mask, ENIG plating. A conformal coating on the back side (except the connector, button, microphone hole and programming pads) and a bumper for the glass edges will be decided after splash and drop tests.

## How the plan changed

The early plan was drawn up before the custom board existed. What turned out differently on the rev B board:

| In the plan | On the rev B board | What follows from it |
|---|---|---|
| 20 × 30 mm cell, 250–400 mAh; keychain ≈27 × 33 mm | 20 × 25 mm cell, 150–250 mAh; board 30 × 46 mm, case 35 × 51 mm | a 20 × 25 cell fits under the 27 × 28 mm display module, while the nRF module, sensors and the ring took the top 18 mm. Battery life is lower than in the early tables; see the [power budget](power.en.md) |
| LIS2DW12, T5838 microphone with its own sound detector, NTC on the cell | LIS3DH, SPH0641 PDM microphone powered from a pin, nRF die temperature | the microphone has no sound detector, so it is off in a pocket and at night. Nobody measures the cell temperature: charging is enabled by the die temperature via CHG_EN. An NTC and a charger with a thermistor input are in rev C |
| BQ25180 or MCP73831, TPS7A02 LDO, TPS22916 switch | MCP73832, the module's REG0, display switch on AO3401A and AO3400A | 3.3 V comes from REG0 (3–4 mA load out of 25); the switch transistors are JLCPCB basic parts |
| SPI display, 7 pins | standard 4-pin I2C module | full frame ≈23 ms instead of ≈1 ms; the engine sends only changed windows, and that is enough |
| Display transfers via EasyDMA with the core asleep | Wire transfers via TWIM (EasyDMA), but the core waits for the transfer to finish | sleeping the core during transfers is among the improvements in the [power budget](power.en.md) |
| Plated hole or brass bushing in the lug | 3.2 mm non-plated hole, no copper around it | the fiberglass holds it; the pull test is still ahead |
| Bare boards without a case | acrylic "sandwich" S, M, L | the cell is enclosed, the boards are visible through it |

## Shopping list for assembly

| What | How many | What for |
|---|---|---|
| Assembled rev B boards (JLCPCB or PCBWay) | at JLCPCB: PCB — 5 panels (the minimum), 3 of them assembled = 6 boards (PCBA from 2 panels) | assembly is ordered in panels: 2 boards on a 76 × 72 mm panel, "Panel by Customer" from `hardware/rev-b/out/panel`; the order number goes in "Order Number (Specify Position)", it ends up under the cell ([panel](../hardware/rev-b/README.en.md#assembly-panel), [order](../hardware/rev-b/README.en.md#ordering-from-jlcpcb)) |
| 0.96″ SSD1306 OLED module, I2C, 4 pins, **white**, with LDO; pin order `GND VCC` (`VCC GND` also works, then R16/R18 are moved, [assembly step 2](../hardware/rev-b/case/README.en.md#assembly)); for M and L without a pre-soldered header | 2–3 each from 2–3 sellers | displays, checking module variation |
| 402025, 502025 or 602025 cells with a protection board | 2 per keychain | to match the case version; measure the thickness before buying a batch |
| 0820 coin vibration motor, 3 V | 1 per keychain | vibration |
| M2 × 25 nylon cheese-head screws (DIN 84) and M2 nylon nuts for all versions; if nylon M2 × 25 cannot be found, steel M2 × 22 (S) or M2 × 25 (M, L) with nylon nuts; keychains for sale use nylon only | 4 per keychain | stack and case; [what to buy](../hardware/rev-b/case/README.en.md#what-to-buy) |
| 2.54 header (long, from 15 mm, for M and L), 1 × 2 mm silicone tube | per the [case list](../hardware/rev-b/case/README.en.md#what-to-buy) | display and microphone |
| Acrylic for laser cutting | per the [case list](../hardware/rev-b/case/README.en.md) | case |
| Raspberry Pi Pico or Pico 2 (not W) as a programmer with debugprobe firmware, ~$4–6 | 1 | first flash over SWD if the factory does not flash the boards; [how](../firmware/README.en.md#main-programmer-raspberry-pi-pico) |
| TXS0108E level shifter module, ~$1–2; Dupont wires up to 10 cm, a micro-USB data cable | 1 | between the Pico (3.3 V) and the board (1.8 V before the first flash) — required |
| Tag-Connect TC2030-CTX-NL cable and an adapter from 10-pin 1.27 mm to 2.54 mm pins, or thin wires soldered to J3; optionally a TC2030-CLIP retainer for NL cables | 1 | connecting to J3 |
| **Nordic Power Profiler Kit II** | 1 | current measurement: without it the power budget is an estimate |
| Calipers, ruler, fine-tip soldering iron, solder, flux, desoldering braid, tweezers, magnifier, multimeter, flush cutters, needle file and sandpaper, a sponge or foam | — | assembly and measurements |
| 0402 0 Ω jumpers | 2 per keychain, only for `VCC GND` modules | R17 and R19 instead of R16 and R18; the removed R16 and R18 can be reused |
| Carrierless adhesive transfer film up to 0.06 mm (e.g. 3M 467MP) | — | both parts of the back frame to the bottom ([step 16](../hardware/rev-b/case/README.en.md#assembly)) |
| USB-C data cable with a regular plug, plug shell no thicker than 6.4 mm | 1–2 | charging and console; a thick plug hits the lower cell frame |
| Kapton, thin double-sided tape; UV glue optional | — | mounting the cell, protecting the flex |
| Split rings and carabiners | a few | mechanical tests |

**Programmer.** Before its first flash the board runs at 1.8 V ([board README](../hardware/rev-b/README.en.md#programmer-swd)).

- **The main option is a Raspberry Pi Pico with the official [debugprobe](https://github.com/raspberrypi/debugprobe) firmware** (`debugprobe_on_pico.uf2` or `debugprobe_on_pico2.uf2` from the releases page, written via BOOTSEL) and pyOCD on the computer. The Pico outputs only 3.3 V, so a TXS0108E module goes between it and J3: side A is powered from the board's VTref (J3, pad 1), side B from the Pico's 3V3. Do not connect the Pico's 3V3 to J3 pad 1; the keychain is powered from its own USB-C. The Pico does everything: the first flash of the bootloader with the application, erasing and removing protection, reflashing later; after the first flash, updates go over USB. This setup has not been tested on this board yet, so start with one board. Wiring and commands are in the [firmware README](../firmware/README.en.md#main-programmer-raspberry-pi-pico).
- **Another option is a J-Link EDU Mini V2:** it handles levels via VTref by itself, and the TC2030-CTX-NL cable plugs straight into it. Under SEGGER's license it is for non-commercial use only, which is fine for keychains given as gifts.
- **The NL cable has no latches:** J3 has no holes for retaining clips. Hold the plug against J3 by hand for the whole flash, or use a TC2030-CLIP retainer ([how](../firmware/README.en.md#connecting-to-j3)). With a Pico you can do without the cable: thin wires soldered to the J3 pads.
- **Without your own programmer:** the factory flashes the boards itself (PCBWay: "Firmware loading"). The file for prototypes is `keychain-revb-factory.hex` from `firmware/release/`. For keychains for sale, use only a `firmware-factory.hex` from your own build with your own VID/PID (the `[sale]` section in `firmware/platformio.ini`); the files in `release/` carry Adafruit's IDs ([firmware README](../firmware/README.en.md#first-flash-bootloader-over-swd)).

The ESP32 stand has its own list in the [firmware README](../firmware/README.en.md#what-you-need).
