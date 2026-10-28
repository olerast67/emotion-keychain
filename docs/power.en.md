English | [Русский](power.md)

# Power budget

The face is always on. The screen goes dark only when nobody can possibly see it: in a pocket, in a closed bag, in a dark room. With the face on, the OLED and the microphone draw almost all of the current, so battery life comes down to:

1. how many pixels are lit;
2. how bright they are;
3. how often the screen is touched at all;
4. how long the face is on versus how long the keychain sits in a pocket.

## Rev B board: current by mode

The numbers below come from the full-board simulation (`keychain_sim`, scenarios `pocket` and `day`): the real firmware `firmware/src/keychain` running against chip models, with current computed from each chip's state using typical datasheet values. This is an estimate and must be confirmed with a PPK2 measurement on a real board. The simulation also checks its own limits: pocket below 170 µA, keychain switched off below 15 µA, a normal day 10–16 mAh.

| Mode | When | Current | What draws it |
|---|---|---|---|
| Face on screen | daytime, in light | ≈2.3 mA idle, ≈2.7 mA with taps and walking | screen ~1.35 mA, microphone 0.8 mA, CPU, sensors and advertising the rest |
| Face in a dim room | 8 lx, daytime | ≈2.3 mA | same: normal brightness |
| Dimmed face (Dim) | at night with lights on, character asleep | ≈0.9 mA | contrast 1/4, microphone off |
| Pocket, screen off | dark for over a minute | ≈0.13 mA | CPU asleep, advertising every 10 s, light sensor every 2 s, accelerometer |
| Encounter nearby (Busy) | another keychain nearby, while an encounter is possible | +1.2 mA (calculated from the code) | 0.3 s receive window every 1.5 s and advertising 10 times per second; after an encounter the neighbor is ignored for 10 minutes |
| Fully off | button held 3 s, empty cell | 6–17 µA | sensors asleep; the rest is the charger, the diode and the cell's protection board; 12 µA in simulation |
| Charging | USB | from USB | the cell is not used |

Sound and vibration draw from a few to tens of milliamps, but only while they play: they barely affect the average current.

**Accelerometer SA0 is tied to VDD.** In the first layout this LIS3DH pin went to ground, while it has an internal pull-up to VDD. That pull-up would have drawn 0.11–0.16 mA all the time, even with the keychain off and in the bootloader: pocket current would double (0.13 → ~0.29 mA), and a switched-off keychain would drain a 150 mAh cell in 5–7 weeks. SA0 is now on VDD, so no current flows through the pull-up; the firmware also disables it (CTRL_REG0 = 0x90). The simulation never modeled this leak, so its numbers did not change.

### Battery life

Usable capacity, down to the firmware cutoff at 3.35 V, is about 95 % of nominal: ~142, ~190 and ~237 mAh for the S, M and L cells ([case](../hardware/rev-b/case/README.en.md#three-versions-s-m-l), section "Three versions: S, M, L").

| Day | Daily consumption | S, 402025, 150 mAh | M, 502025, 200 mAh | L, 602025, 250 mAh |
|---|---|---|---|---|
| Normal: 3 h face, 1 h in dim light, 20 h in a pocket and at night | ≈12.9 mAh | ≈11 days | ≈15 days | ≈18 days |
| Heavy: 8 h face, 16 h in a pocket | ≈23.5 mAh | ≈6 days | ≈8 days | ≈10 days |
| Target from [hardware](hardware.en.md#goals) (Goals): 16 h face + 8 h dark | ≈44 mAh | ≈3 days | ≈4.3 days | ≈5.4 days |

- **Normal day** comes straight from the `day` scenario (one hour per mode, taps and walking during the day): face 2.69 mA, dim light 2.27 mA, pocket and night 0.127 mA.
- **Heavy day and target** are calculated from the same currents: face 2.69 mA, the rest of the time 0.127 mA.
- Encounters (+1.2 mA while a neighbor is nearby), long scenes and sound are not included. The capacity printed on cheap cells is often inflated: the real value is checked by a discharge test.

The "16 h of face, ≥5 days" target is met only by the L version. What remains, ordered by gain:

1. **Microphone in windows** (listen 1 s out of 4, or only while the keychain is in hand): ≈−0.5 mA in face mode. Changes behavior: a clap is heard only within a window.
2. **Contrast by lux** (dimmer indoors, brighter in sunlight) plus VCOMH/precharge: ≈−0.3–0.5 mA. Needs a readability check on our own panel.
3. **I2C with the core sleeping** during transfers instead of busy-waiting in Wire: ≈−0.15 mA in face mode.
4. **Event-driven loop** instead of a pass every 10 ms, accelerometer FIFO-level interrupt, 50 Hz instead of 100: tens of µA.
5. **Rev C:** screen switch resistors R7 and R9 at 1 MΩ instead of 100 kΩ (−60 µA with the screen on), a 32 kHz crystal, a 20 × 30 mm cell if the board under it grows ([plan](plan.en.md)).

With items 1–3 the face draws about 1.8 mA, and the target is met already with a 200 mAh cell.

### Switched-off keychain and storage

A switched-off keychain draws 6–17 µA by typical datasheet values: nRF52840 sleep, the charger, the diode on VBUS, the cell's protection board, sensors asleep. The limit for the first-board check is 25 µA ([first power-on](../hardware/rev-b/README.en.md#first-power-on)).

| What | 6 µA | 12 µA | 17 µA | 25 µA |
|---|---|---|---|---|
| ~3.6 mAh reserve left after "Battery almost empty" (S; slightly more for M and L) | 25 days | 12 days | 9 days | 6 days |
| S cell charged to half (~71 mAh) | ~16 months | ~8 months | ~6 months | ~4 months |

The cell's own self-discharge (a few percent per month) comes on top. Therefore:

- a keychain that shut down on an empty cell must be charged within a week: after that the cell's protection board disconnects it, and deep discharge damages the cell;
- for long-term storage, leave the keychain charged to about half and top it up every 2–3 months.

## What the firmware does

- **A frame is rendered only when the face changes.** The SSD1306 holds a static image on its own while the CPU sleeps. If the hash of the face parameters has not changed, the engine skips rendering entirely.
- **Only changed page windows are sent to the screen.** A neutral face produces about 1.2 KB/s over I2C, roughly 3 % of a 400 kHz bus; once calmed down, about 0.8 KB/s.
- **A frame happens exactly when the picture changes.** Motion moves the face by whole pixels, and the CPU wakes at the moment of the shift. Blinking icons wake it only on picture changes: the battery twice per 1.2 s, the ellipsis 4 times per 1.6 s. Zzz is 5 times per second. Events closer than 20 ms are merged into one wakeup.
- **Long states are cheap:** sleep, tiredness, low battery. Shivering from cold comes in bouts, 1 s out of 4, and the CPU sleeps between bouts. After 3 minutes without attention the face calms down: signature gestures three times less often, gaze shifts half as often.
- **Accelerometer noise does not wake the CPU.** Tilt below 0.05 is ignored, and the pupils move in steps.
- **Ring eyes instead of filled ones:** fewer lit pixels, less panel burn-in.
- **Contrast adapts to the surroundings.** Behavior outputs a brightness level: normal; dimmed at night and during sleep; minimum in the dark. The firmware maps it to contrast: the user's brightness, 1/4 of it, and 1/16. Contrast is sent to the screen only when the level changes. VCOMH and precharge are the next step: VCOMH `0x00` and a short precharge give a dimmer picture than contrast alone. We measure the effect on our own panel.
- **In the dark the screen is switched off rather than drawing a black frame.** A black but powered screen still draws 0.5–1.4 mA for the charge pump and scanning. After a minute of darkness the firmware cuts power to the screen module with a switch; with the screen off, frames run at most once per second.
- **Microphone and radio only when needed.** The microphone listens while the face is at normal brightness; at night, in a pocket and in eco mode its power is off. The 64 MHz crystal that the microphone requests at start is released right away on battery (~0.25 mA); on USB, USB itself holds the crystal, and it is also used to measure the temperature for charging. Radio scanning runs in 150 ms windows every 30 s, not at all at night or in a pocket, and advertising goes out every 10 s.
- **Optional retro mode:** every other line is lit, with an alternating offset. Less burn-in and slightly lower current.
- **Eco mode on low battery.** Behavior switches to it by itself on the `battery_low` event. Frames are less frequent: 50 ms, 80 ms on critical charge. Boredom activities are half as frequent, no surprises. Everything returns to normal on the charger.
- **Shutdown** on an empty battery: `Behavior::power_off(now, true)` shows LOW BATTERY → GOODNIGHT, then the character sleeps. The firmware turns off the screen and enters system off at `scene_ends_ms()`, with margin before the cell's protection board cutoff.
- **Sound and vibration** only while they play. At night the keychain does not chatter ([keychain voice](sound.en.md)).

`./build/facegen bench` shows the load for each emotion: frame time, frames per second, bytes per second, bus utilization.

## Burn-in protection

A static frame burns into an OLED. A white panel ages more slowly than a blue one, but protection is still needed.

- The whole face drifts slowly: 1 px every 20–40 s, within ±3 px.
- Blinks, gaze and signature gestures move the lit pixels. No frame stays for more than a few seconds.
- Indoor contrast is `0x40`–`0x7F`, not `0xFF`.
- Test: one unit runs with the face on 24/7 for 4 weeks, then a white screen is shown on it and checked for marks.

## How to measure

- **Instrument:** a Nordic PPK2 in source mode powers the board at 3.7–3.8 V in place of the cell. Record each state for at least 60 s. A µCurrent or other ammeters go only in series between a 3.7 V source and the board, not in place of the cell.
- **Where to connect.** The BT1 pads are covered by the cell and the screen module, so:
  - **without the screen module**: on the BT1 pads, before the cell is soldered in (the assembly step before the cell). The firmware runs without a screen;
  - **with the screen module**: on a separate measurement board assembled without a cell: PPK2 on test points SYS (+) and GND (−) on the back ([measurement board](../hardware/rev-b/README.en.md#on-the-first-boards), section on the first boards). Without USB, SYS is connected to VBAT through Q1, which is on, so this is the same as powering from the cell. The points are accessible until the back frame and the bottom are installed. A cell cannot be soldered into such a board later without removing the screen.
  - **Do not connect USB while the PPK2 is connected:** USB puts more than 3.7 V on SYS, and current will flow into the PPK2 output.
- **Screen current:** the difference between measurements "with module" and "without module", on the measurement board or before and after installing the module.
- **Switched-off keychain:** hold the button 3 s, USB disconnected, record for at least 60 s. Above 25 µA, look for a leak ([first power-on](../hardware/rev-b/README.en.md#first-power-on)); the screen pins with the switch off can only be checked with the module installed.
- **Common measurement mistakes:**
  - USB connected: the keychain runs from it, and the charge LED glows faintly;
  - an encounter nearby: another keychain on the table adds +1.2 mA;
  - a screen module from a different seller: its LDO and panel have their own current; compare on the same module.
- **Final check:** discharge a real cell down to cutoff in the "16 h face + 8 h dark" mode.
