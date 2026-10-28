English | [Русский](README.md)

# Rev B case: acrylic "sandwich"

Laser-cut acrylic layers clamped by four M2 screws. The board and the display are visible through the clear lid and bottom; the frames close the sides. Everything is computed from the same board file `keychain-rev-b.kicad_pcb`, so after changing the placement you only need to rerun the scripts. This page also covers [the full keychain assembly](#assembly), from the board as it arrives from the factory to the finished keychain.

```sh
cd hardware/rev-b/case
python3 case.py                               # checks and cut files for S, M, L → out/, out/m/, out/l/
python3 case.py --sheet 3=2.86 --sheet 4=4.05 # the same for your sheets (measured with calipers)
python3 selftest.py                           # tests the checks: deliberate breakages get caught
python3 viewer.py                             # 3D view of all three versions → out/keychain-3d.html
python3 viewer.py --shots ../../../docs/img   # case-3d.png and case-exploded.png for the README
python3 viewer.py --render ../../../docs/img/pcb-rev-b.png   # board picture for the README
```

**Ready-made cut files and `keychain-3d.html` are already in `out/`**, for nominal sheets. You do not need to run the scripts to get parts cut. Run them if you changed the board or `OLED` in `gen/footprints.py`, or want to recompute everything for your own sheets: `case.py` then rewrites `out/`.

**What the scripts need.** They all run in Python with the `pcbnew` module from KiCad 7, like the [board scripts](../README.en.md) (on Linux that is the system `python3`). `viewer.py` also needs `kicad-cli`, Pillow and cairosvg: `pip install pillow cairosvg`, or another path in the `CAIROSVG` variable; `rsvg-convert` works too. `viewer.py` takes the face for the display from `build/facegen` in the repository root (the engine build, see the [README](../../../README.md)) or from the path in `FACEGEN`. `--shots` needs Node.js with Playwright (`shots.js`); if the browser cannot reach the CDN, point `THREE_DIR` at an unpacked npm package `three@0.128.0`.

## Three versions: S, M, L

The board, the firmware and the board outline are the same for all of them. What differs is the cell, the upper cell frame and, with it, the height under the display module, the microphone tube and the display pin header. The keychain is 35 × 51 mm, with the tab for the keyring sticking out at the top. The button is on the right: there the back frame juts out as a 0.8 mm boss, and the plunger sticks out another 0.5 mm.

| Version | Cell | Typical day | Thickness | Under module | Cell frames | Screws | Display header | Files |
|---|---|---|---|---|---|---|---|---|
| S | 402025, ~150 mAh | ~11 days | 18.8 mm | 6 mm | 3 + 3 mm | M2 × 25 | standard | `out/` |
| M | 502025, ~200 mAh | ~15 days | 19.8 mm | 7 mm | 3 + 4 mm | M2 × 25 | long, 15 mm or more | `out/m/` |
| L | 602025, ~250 mAh | ~18 days | 20.8 mm | 8 mm | 3 + 5 mm | M2 × 25 | long, 15 mm or more | `out/l/` |

- **No standoffs under the module.** The display module rests on the tabs of the two cell frames, and the screws pass through them. The stack of frames is the spacer: its height always equals the height of the frames, and there are no spacers to buy.
- **Only common acrylic thicknesses:** 2, 3 and 4 mm for S and M, plus 5 mm for L.
- **Screws are M2 × 25 nylon for all versions:** nylon M2 × 22 is hardly sold. The excess is clipped off after assembly ([what to buy](#what-to-buy)).
- **The charger delivers 100 mA:** for all three cells at nominal capacity that is no more than 0.7C. The cell's datasheet must allow this ([choosing a cell](#choosing-a-cell)).

## Choosing a cell

**Thickness.** The cell lies on the board under the display module. The free height above it is "under module" minus the tallest part on the back of the module. "Under module" is the two cell frames stacked: 6, 7 or 8 mm nominal. The part on the back of the module is usually a SOT-23 regulator 1.1 mm tall, but it can be a 0805 capacitor 1.45 mm tall: measure your module. The cell fits if:

> cell thickness + 0.3 mm (Kapton and tape under it) + 0.1 mm (Kapton on the back of the module above it) + 0.3 mm (swelling margin) ≤ free height

| Version | Free under module | Cell no thicker than |
|---|---|---|
| S | 4.55 … 4.9 mm | 3.85 … 4.2 mm |
| M | 5.55 … 5.9 mm | 4.85 … 5.2 mm |
| L | 6.55 … 6.9 mm | 5.85 … 6.2 mm |

The first number is for a 1.45 mm module part, the second for 1.1 mm.

**The table is for nominal sheets, and acrylic sheets vary in thickness by ±0.1–0.3 mm** (cast acrylic even more). So before buying a cell, measure the sheets you will cut the cell frames from (3 mm and 3, 4 or 5 mm) with calipers in several places and recompute: `python3 case.py --sheet 3=2.86 --sheet 4=4.05` prints the free height, the cell limit, and the tube and screw lengths for your sheets, and rewrites the cut files in `out/` for them (afterwards `python3 viewer.py` updates the 3D view). Without recomputing: for every 0.1 mm by which the two cell frames together are thinner than 6, 7 or 8 mm, the cell limit drops by 0.1 mm.

Cells are rarely thinner than nominal and more often thicker: a 402025 can be up to 4.5 mm per its datasheet. A nominal 402025 (4.0 mm) fits in S only if the parts on the back of the module are no taller than 1.3 mm and the two cell frames together are no thinner than 6.0 mm. If it does not fit, use the M case. Measure the cell thickness with calipers at its thickest point, at the protection board.

**Width:** no more than 20.7 mm including the wrap. The pocket in the frames is 21.0 mm wide. The frame tabs open toward the cell as a fork, so its edges bear against the pocket walls, not against the tabs and screws.

**Length:** no more than 25.0 mm including the protection board. The pocket is 0.6 mm longer; the cell is placed tight against its lower wall, 0.3 mm beyond the lower edge of the board. Above it is the plastic of the display header: with a 25.0 mm cell about 0.5 mm remains.

**Leads.** You need a cell with a protection board and thin wires (about 30 AWG) coming out of the end with the protection board. That end goes up, toward the display header, with the wires at the right corner: they run down along the right side of the cell to the BT1 pads through a channel in the frames.

**Cell datasheet.** Ask the seller for the datasheet and check:

- a regular 3.7 V Li-Po, 4.2 V charge voltage, protection board (PCM) already on the cell;
- maximum charge current at least 100 mA: the keychain always charges at 100 mA. For a 150 mAh cell that is 0.7C; if the datasheet says "max charge 0.5C", that cell is not suitable — take a larger capacity (M or L);
- charge temperature: the range includes 0–45 °C (as usual for Li-Po; at minimum 7–45 °C): the keychain starts charging at 10–42 °C according to the sensor in the processor and stops below 7 °C and above 45 °C.

Keep the datasheet with the cell supplier's documents (UN38.3 and SDS).

## Layers from face to back

Heights z are measured from the front side of the board, for version S. In M the upper cell frame is 1 mm thicker and everything above it is 1 mm higher; in L, 2 mm.

| Sheet | Parts | z, mm | What it does |
|---|---|---|---|
| 2 mm, clear | lid | 9.2 … 11.2 | 0.6 mm gap above the glass; holes for the screws and Ø 1.2 mm above the microphone tube |
| 2 mm, clear | 4 washers | 7.2 … 9.2 | between the module board and the lid; the lower two have a flat on the glass side |
| 3 mm | display frame | 6 … 9 | walls around the module with a 0.5 mm gap, tube holder; 0.2 mm below the lid |
| 3 mm (M: 4, L: 5) | upper cell frame | 3 … 6 | tabs under the module, tube holder |
| 3 mm | lower cell frame | 0 … 3 | tabs, cutout around the nRF52840 module; no window for the USB-C plug — the end of the cell is closed |
| — | board 1.6 mm | −1.6 … 0 | |
| 4 mm | two-piece back frame, plunger | −5.6 … −1.6 | tabs under the screws, plunger pocket and boss at the button, window for the USB-C plug; both pieces glued to the bottom |
| 2 mm, clear | bottom | −7.6 … −5.6 | cutout for the USB-C plug, Ø 3 mm hole opposite the buzzer, charge LED visible |

**Button.** The actuator of button SW1 sticks out past the board edge by only 0.17 mm, and its travel is 0.2 mm, so the fully pressed plunger goes under the board edge. To keep it from rising toward the board and hitting its edge, the plunger has wings on both sides of the button: at rest they lie 1.2 mm under the board, the board holds them from above and the bottom from below. The plunger's ledge in the pocket is 1.5 mm, like all frame bridges, so the pocket wall sticks out as a 0.8 mm boss. The finger stops on the boss. The actuator slot and the USB-C window cut the back frame into two pieces, and the lower right one, with half of the boss, is held by a single screw. That is why both pieces are glued to the bottom with thin film (step 16).

**Microphone.** It sits on the back and listens through a hole in the board. To keep it from listening through the whole case cavity (which would cut off sound above ~1 kHz), a 1 × 2 mm silicone tube runs from the hole to the lid. Two frames hold it. The tube is 0.6 mm longer than the board-to-lid gap, and both ends are compressed: that way there is no gap even with sheets thicker than nominal. The tube is offset 0.15 mm from capacitor C4; the hole in the board stays inside its bore.

**Buzzer** BZ1 sits on the back and sounds away from the board, into the bottom. That is why the bottom has a Ø 3 mm hole opposite it: do not tape it over. A keychain lying on its back or in a bag sounds quieter.

**USB-C plug.** There is a window for it only in the bottom and the back frame: for a standard plug body, up to 12.35 × 6.5 mm, with 0.5 mm clearance at the sides. The lower cell frame has no window: it closes the end of the cell, so nothing can reach the cell from outside. The receptacle axis is 3.23 mm below the board face, and the frame lies on the board face, so a plug up to 6.46 mm thick passes under the frame.

- **Ordinary plugs, up to ~6.4 mm, fit.** A plug at the standard's limit, 6.5 mm, touches the frame by 0.02 mm. `case.py` accepts this as a touch tolerance of up to 0.05 mm, not as an error: a plug in the receptacle has about 0.15 mm of vertical play and takes up such a touch, but may not go all the way in.
- **A cable with a thick plug will not go all the way in.** On cables with thick braiding the plug body can be thicker than 6.5 mm. Use an ordinary cable and test it on the assembled keychain (step 18).
- **The limit is for a 1.6 mm board.** The receptacle is on the back and the frame on the face, so a board 0.1 mm thinner reduces the plug limit by 0.2 mm.

## Cut files

For each thickness there is `case-acrylic-2mm.svg`, `…-3mm.svg` and so on, plus matching `.dxf` (R12, millimeters). The parts of each sheet are laid out in rows; a sheet is no wider than 125 mm. Cut lines are red, 0.05 mm wide. In the SVG each part has its own named group (`<title>`).

| Sheet | S | M | L |
|---|---|---|---|
| 2 mm | lid, bottom, 4 washers, 2 spare lower washers | same | same |
| 3 mm | display frame, both cell frames | display frame, lower cell frame | display frame, lower cell frame |
| 4 mm | back frame (2 pieces), plunger | upper cell frame, back frame, plunger | back frame, plunger |
| 5 mm | — | — | upper cell frame |

**Kerf.** The outlines are nominal, without kerf compensation. Ask the shop to compensate for the kerf, i.e. offset the line by half the kerf. Without compensation the parts come out smaller and the holes larger by the kerf width, 0.1–0.3 mm. The case will still assemble. At rest the plunger will sit farther from the button, up to 0.55 mm, but pressed down to the boss it will still actuate it: `case.py` checks this for kerf from 0 to 0.3 mm, the overtravel comes out at 0.05–0.2 mm, and the plunger wings stay under the board.

**The lower washers are fragile:** there is only 0.5 mm of acrylic between the hole and the flat, 0.2 mm without kerf compensation. That is why the 2 mm sheet has two spares. A cracked lower washer works just as well as a horseshoe: put it with the break toward the glass.

## What to buy

- **Acrylic:** 2 mm clear (lid, bottom, washers) and 3 and 4 mm for the frames, plus 5 mm for L. The frames can be colored. A 125 × 60 mm sheet of each thickness is enough.
- **M2 × 25 nylon (polyamide) screws and M2 nuts, also nylon,** 4 of each, for any version.
  - **Head:** cheese or pan head, Ø no more than 4 mm, **not countersunk:** a countersunk head in the lid's straight hole wedges the acrylic apart.
  - **Where to get them.** Nylon M2 pan-head screws are usually sold only up to 20 mm, which is too short. M2 × 25 exists with a cheese head (DIN 84), e.g. Essentra 50M020040D025 (polyamide 6.6, head Ø 3.8), but more often in bulk packs. Nylon M2 nuts are sold everywhere.
  - **Why nylon.** The top right screw, looking at the display, is only 7.7 mm below the antenna zone of the nRF module ([board README](../README.en.md#layout-by-block)), and metal near the antenna shortens the encounter range.
  - **Length.** The screw protrudes from the nut by 4.6 mm (S), 3.6 mm (M) and 2.6 mm (L). After final tightening, not before, clip the end with flush cutters, leaving about 1 mm above the nut. The margin is calculated like this: the screw protrudes from the nut by at least 1.2 mm on nominal sheets, which is +0.2 mm for each of the six sheets under the screw. Shorter is not allowed.
  - **Could not find nylon M2 × 25?** Steel ones will do (stainless is better): M2 × 22 for S, M2 × 25 for M and L, with nylon nuts all the same. The case assembles the same way, only the radio gets a bit weaker; check the encounter threshold on assembled keychains ([what to check](#what-to-check)). This is only for your own prototypes: keychains for sale use nylon only. Nylon shorter than 25 mm does not work: M2 × 20 does not pass through the nut in any version.
- **Cell** with a protection board, per the rules above.
- **Display pin header.** The pins pass through the board and must protrude from it by at least 0.5 mm. A standard 2.54 header has 8.5 mm under the module (2.5 plastic + 6 tail). You need "under module + 1.6 + 0.5": 8.1 mm for S, 9.1 for M, 10.1 for L.
  - S: a module with a standard header already soldered in works; the pins will protrude 0.9 mm from the board.
  - M and L: a standard header will not come out of the board. Get a module without a soldered header and a long 2.54 4-pin header, 15 mm or more (3 + 2.5 + 9.5). The pins will protrude 3.4 mm (M) and 2.4 mm (L); clip the excess after soldering, leaving about 1 mm.
- **Silicone tube 1 × 2 mm** (inner × outer): a 9.8 mm piece (S), 10.8 mm (M) or 11.8 mm (L) on nominal sheets. Longer is fine, it compresses; shorter is not, there will be a gap.
- **Kapton tape** (USB-C legs, cell solder joints, back of the module above the cell, display flex) **and thin double-sided tape** 0.1–0.15 mm, not foam. UV glue for the flex is optional.
- **Adhesive transfer tape without a carrier, up to 0.06 mm** (e.g. 3M 467MP): to glue both back frame pieces to the bottom (step 16). Do not use ordinary double-sided tape here: it is thicker and lifts the bottom.
- **USB-C cable with an ordinary plug:** plug body no thicker than 6.4 mm ([why](#layers-from-face-to-back)). A data cable, not charge-only: the console runs over it.
- **Electronics fitted by hand** ([purchasing](../../../docs/hardware.en.md#shopping-list-for-assembly)):
  - 0.96″ SSD1306 display module, I2C, 4 pins, white, with LDO; for M and L without a soldered header. The keychain firmware drives the display only as an SSD1306: 1.3″ modules usually use the SH1106 and do not work ([what to check](../README.en.md#what-to-check-before-ordering));
  - 0820 coin vibration motor, 3 V;
  - for a `VCC GND` module: two 0 Ω 0402 jumpers (R17 and R19), or move the removed R16 and R18 (step 2).
- **Tools:**
  - soldering iron with a fine tip, solder, flux, desoldering braid, tweezers and a magnifier;
  - multimeter, calipers and a ruler;
  - flush cutters, a needle file and sandpaper on a flat sheet;
  - a sponge or foam block to rest the board and the stack on;
  - an SWD programmer, if the board arrived without firmware: a Raspberry Pi Pico with debugprobe firmware and a TXS0108E level shifter module, wires up to 10–15 cm; to J3, a Tag-Connect TC2030-CTX-NL cable via an adapter to 2.54 mm pins, or thin wires soldered to the pads ([which and how](../README.en.md#programmer-swd)). Another option is a J-Link EDU Mini V2; the cable plugs straight into it. The cable has no latches, so it is held against J3 by hand for the whole flashing;
  - Nordic PPK2 for measuring current (step 6).

## Assembly

This is the complete keychain assembly: electronics first, then the case. The cell lies under the display module and covers its pads, so the order is: firmware, display jumpers, motor, cell, display, case. Everything that gets soldered on the board near the cell is done before it: once the cell is on, no hot air near the board. Before cutting, measure the display module and the sheets ([what to check](#what-to-check)).

1. **Firmware**, if the board arrived without it: over SWD, with the board powered from its own USB-C, no cell and no display yet. Programmer: Pico with debugprobe through a TXS0108E: VA of the module to pad 1 of J3 (VTref), VB to the Pico's 3V3; never connect the Pico's 3V3 to J3 ([wiring and commands](../../../firmware/README.en.md#main-programmer-raspberry-pi-pico), [J3](../README.en.md#programmer-swd)). The file for prototypes is `keychain-revb-factory.hex` from [`firmware/release/`](../../../firmware/release/README.en.md). For keychains for sale, `firmware-factory.hex` from your own build with your own IDs (the `[sale]` section in `firmware/platformio.ini`). 3V3 is ~1.8 V before flashing and ~3.3 V after. Do the first check from [first power-on](../README.en.md#first-power-on) right here: USB-C only, no cell; in the console `ver` shows the firmware version, `sensors` shows that the accelerometer and light sensor respond.
2. **Display power pin order — before the motor and the cell.** 0.96″ modules come as `GND VCC SCL SDA` or `VCC GND SCL SDA`: check the labels on your module now. Unplug USB-C — the board has neither power nor a cell.
   - The factory fits R16 and R18: pin 1 is GND, pin 2 is VCC.
   - Only for a `VCC GND` module, remove R16 and R18 and fit R17 and R19 (0 Ω 0402) **in their place**, or move the removed R16 and R18. Use a fine-tip soldering iron and tweezers, not hot air. The G and V labels are on the back above the jumpers, between them and the display pins.
   - **Never fit both pairs:** R16 with R17, or R18 with R19, short the display supply to ground.
   - **Only now.** After step 6 the jumpers lie right behind the cell's protection board, on the other side of the 1.6 mm board, and cannot be soldered. If you fitted the wrong pair and the cell is already on, desolder the cell first.
3. **Vibration motor** — on the back, in the circle labeled VIBRO: peel the film off the adhesive layer and press the motor inside the circle, leads toward the "+" and "−" pads next to the circle. Red wire to "+", the other to "−". Trim the wires to length so they lie flat and do not extend past the board edge.
4. **The USB-C legs** come through to the front side under the cell. Clip them and file them nearly flush, no higher than 0.1 mm, then cover with Kapton. Under the cell 0.3 mm is allowed for Kapton and tape. If the board arrived in a panel, its edges have leftover breakaway tab stubs — bumps up to ~0.3 mm. Sand them flush before the board goes into the frames. Where they are, looking at the board face (button on the right):
   - on the left edge: 3.25–8.25, 24.5–29.5 and 35.2–38.2 mm below the top edge of the board (not counting the tab);
   - on the top edge left of the tab: 2.5–6.5 mm from the left edge;
   - on the bottom edge: 22.0–25.0 mm from the left edge (between USB-C and the right screw).

   There are none on the right edge or on the tab ([where they are](../README.en.md#assembly-panel)).
5. **Display module.** For M and L, solder the long header into the module: short end through the module, plastic under the module. Cover the back of the module above the future cell with a layer of Kapton so the corners of the SOT-23 and 0805 do not dent the cell's wrap. Leave 3.3 mm radius circles around the module holes uncovered: the frame tabs will rest on them. Cover the glass flex, folded around the lower edge of the module, with a narrow strip of Kapton: it protects it from kinking and from the edge of the display frame. Do not bend or pull the flex. UV glue is optional: a thin bead along the edge of the glass at the flex, no higher than the glass.
6. **Cell — only with a protection board (PCM).** The board has no reverse-polarity protection: a reversed cell forward-biases the internal diode of charger U2, and the current is limited only by the cell's protection board. The cell's position is set by the lower cell frame, used as a template: its pocket is only 0.3–1.0 mm wider than the cell.
   - First, while the BT1 pads are free, measure the current of the switched-off keychain: PPK2 on the BT1 pads, keychain switched off with the button, less than 25 µA ([how](../README.en.md#first-power-on)). Then desolder the PPK2 wires.
   - Double-sided tape goes on the bottom side of the cell; do not peel the liner yet.
   - Put the board on the sponge face up and insert two M2 screws from the back into the two lower board holes. Slip the lower cell frame (the one with the cutout around the nRF52840 module, no tube holder) over them: its fork tabs open toward each other, and on two screws the frame cannot shift sideways or lengthwise. Leave the upper holes free: the wires will run by the top right one.
   - Peel the liner and place the cell in the frame pocket: protection board end up, wires at the right corner, pushed down against the lower wall of the pocket. Press it down.
   - Remove the frame and take out the screws: you cannot solder next to acrylic, and the BT1 pads are only 0.6 mm from the frame wall.
   - Without the template: cell centered on the board, gaps to the left and right board edges equal to (30 − cell width) / 2 — for a 20.0 mm cell, 5.0 mm each. Measure with calipers.
   - Bend the wires at the corner of the cell and route them down, pressed against its right side, to the BT1 pads. Hold the bend down with Kapton: the loop must not rise toward the top right screw hole.
   - Cut off the connector and the excess wire length to fit, **one wire at a time**: cutters that snip both at once short the cell. Strip the ends by 1–2 mm and tin them.
   - Check the polarity with a multimeter before soldering, not by wire color. Plus (+) goes to the upper of the two pads at the right edge, "+", minus to "−". Cover the joints with Kapton.
   - From now on, no hot air near the board: the cell is on it.
7. **Switch the keychain off** by holding the button for 3 s. With firmware it has powered up from the cell, and the display power switch must be off while the pins are being soldered. On a board without firmware it is off anyway.
8. **Cell frames** — onto the board from the face side: first the lower one (with the cutout around the nRF52840 module), then the upper one (with the tube holder). The tabs sit over the screw holes, the cell wires go into the channel on the right.
9. **Module** — pins into the board; the module rests on the frame tabs. While soldering, clamp it to the tabs with the screws:
   - place the four 2 mm washers on the module holes, the lower two with the flat toward the glass;
   - insert the screws from the top through the washers, module, frames and board and start the nuts on the back;
   - tighten finger-tight, only enough for the module to sit on the tabs.

   Do not clamp without the washers, and do not put nuts on the face: at the lower holes the screw head or nut overlaps the edge of the glass. Solder the pins from the back and clip them, leaving about 1 mm. Remove the screws and washers: the washers go back in step 13.
10. **Continuity check before power-on.** The keychain is off (step 7). Check all adjacent module pins with a multimeter: 1–2, 2–3 and 3–4 (on a `GND VCC` module these are GND–VCC, VCC–SCL and SCL–SDA), plus SCL–GND and SDA–GND. There must be no shorts.
    - A short beep on VCC–GND is the capacitors charging, not a short. Between SCL and SDA the meter shows a few kilohms: those are the module's two pull-ups (together usually 9–20 kΩ), also not a short.
    - The pads of adjacent pins are only 0.84 mm apart. A solder blob on GND–VCC is a short on the display supply, and switch Q2 passes it straight from the cell, with no current limit. A blob on VCC–SCL puts the display supply (up to ~5.4 V from USB) straight onto an nRF pin: the firmware cannot protect against that. A shorted SCL–SDA, or SCL to GND, the firmware detects itself — «Шина экрана зажата…» ("Display bus stuck…") in the console — and runs without the display.
    - The R16–R19 jumpers are almost 2 mm away from the pins, but check them too.

    Only then press the button: boot and a face. The rest follows [first power-on](../README.en.md#first-power-on).
11. **Microphone tube** — through the holder of the upper cell frame, lower end over the board hole next to C4.
12. **Display frame** — around the module, with the tube passing into its holder. Put strips of thin tape on the top face of the frame (see "Stack height" below).
13. **Washers** — on the module holes, the lower two with the flat toward the glass. The tube should stick out about 0.6 mm (0.4–0.8) above the washers: if longer, trim it; if shorter, cut a new one.
14. **Lid** — with its hole over the tube. Insert the four screws from the top through everything.
15. **Turn it over**, holding the stack, onto the sponge or foam: the screw heads sink into it.
16. **Back.**
    - The two-piece back frame, tabs onto the screws.
    - Plunger: ledge into the pocket, wings under the board, actuator into the slot in the boss. It is clamped between the board and the bottom, so it must not be thicker than the back frame: measure both with calipers. If it is thicker or the same, sand its flat faces on a flat sheet of sandpaper until it is slightly thinner than the frame.
    - Hold a ruler against the right side of the case below the boss: the right piece of the back frame (the one with one hole) and the cell frames must line up, with no step you can feel, and the gap around the actuator must be the same above and below.
    - **Film.** The right piece is held by one screw, and it carries the lower half of the boss the finger stops on. Without glue, presses will rotate it around the screw over time, and it will start pressing the button. So apply strips of carrier-free transfer tape to the exposed side of both back frame pieces. Not on the plunger, not in the pocket and not in the wing pockets. Solvent, superglue and acrylic cement are not suitable: they wick under the plunger and fog the clear bottom.
    - On top, the bottom, with its hole over the buzzer. Lower it straight down without shifting the right piece, and press: now both pieces are held by the bottom, and the bottom by all four screws.
    - Start the nuts without tightening. Check with the ruler again, now together with the bottom, and hold it like that while tightening.
17. **Nuts.** Tighten without force, otherwise the acrylic cracks. Do not use threadlocker: it cracks acrylic too. After tightening, the plunger must move just as freely and return by itself; if it binds, loosen the nuts and sand it (step 16). Then clip the screw ends, leaving about 1 mm above the nut, and dress them with a file.
18. **Check:**
    - the button clicks when the plunger is pressed down to the boss — with the keychain face up, face down, and hanging from the ring. If it does not click, loosen the nuts, shift the back frame together with the bottom a hair away from the button toward the middle, and tighten again. If the keychain switches itself off or ignores presses, as if the button were held, shift toward the button;
    - then press the button hard about 50 times and hold it once for 3 s (the keychain switches off; switch it back on), and check the button again;
    - the USB-C plug of the cable you will charge with goes all the way in. If it hits the lower cell frame, the cable has a thick plug; use an ordinary one ([USB-C plug](#layers-from-face-to-back));
    - there are no pressure marks on the display;
    - buzzer and microphone: per "What to check" below.

A keychain that will be given as a gift gets `factory yes` in the console as the last step ([commands](../../../firmware/README.en.md#console)): it returns to the factory state and switches off, and the new owner switches it on.

## What to check

- **Display module, before cutting.** Modules differ from seller to seller, and the case is computed from `footprints.OLED`:
  - the tallest part on the back: it sets the free height above the cell;
  - no parts on the back within 3.3 mm circles around the hole centers: the frame tabs rest there;
  - at least 1.9 mm from the center of the lower holes to the glass: otherwise the washers need a different flat;
  - module board 27.3 × 27.8 mm ±0.2: the display frame gives 0.5 mm clearance;
  - module board thickness 1.2 mm; if different, recompute with `--module`, e.g. `python3 case.py --module 1.6`;
  - the pins stick out above the module board by no more than 1.8 mm: there is 0.2 mm above them to the lid;
  - the glass flex folds around the lower edge of the module by no more than 0.7 mm, within a 13 mm band in the center;
  - the pin order, `GND VCC` or `VCC GND`: it determines the jumpers in step 2, before the motor and the cell.

  If something is off, correct `OLED` in `gen/footprints.py` and rerun `case.py`: the checks will show what does not fit.
- **Acrylic sheets, before buying the cell and the tube.** Measure each sheet with calipers in several places and recompute: `python3 case.py --sheet 2=1.95 --sheet 3=2.86 --sheet 4=4.05` (nominal=measured). From the measurements `case.py` gives the height above the cell, the tube and screw lengths and cut files in `out/` for your sheets, and shows what to adjust during assembly as a warning: for example, sanding down the display frame.
- **Stack height.** The display frame (3 mm) must be 0.1–0.3 mm lower than "module board + washer". With sheet measurements `case.py` checks this and tells you what thickness to sand it down to. If there is a gap, the tape on the display frame takes it up and the frame does not rattle. If the frame is higher, stick it without tape or sand it down: otherwise the lid rests on the frame instead of on the washers, and the module will be loose.
- **Screw play.** The sheets are held only by the screws, and the holes are 2.3 mm in the acrylic for a 2.0 screw, 2.2 in the board. Each sheet can shift by 0.15–0.3 mm, as much as the button overtravel (0.05–0.2 mm). That is why the back frame is aligned with a ruler (step 16) and the button is checked after assembly (step 18). The lower right piece of the back frame is held by one screw, so it is glued to the bottom (step 16). There is no room in the frame for a bridge to the other piece: the parts on the back are in the way. If the keychain later switches itself off or ignores presses, re-glue it, aligning it again.
- **USB-C plug.** The end of the cell is closed by the lower cell frame, which a 6.5 mm plug at the standard's limit touches ([USB-C plug](#layers-from-face-to-back)). Test the cables you will recommend to buyers on an assembled keychain.
- **Buzzer.** After assembly, compare `say laugh` in the firmware console in the case and on the bare board, and check that the voice can be heard in a noisy room. The buzzer hole in the bottom must not be taped over.
- **Microphone.** After assembly, check claps and speech (`v on` in the firmware console): the volume thresholds were tuned without a case.
- **The tab holds the ring by the board alone.** There is enough FR4 around the hole, but a yank on the ring must be tested (stage 7 of the plan). The wall cutout above the tab is shallow: the ring moves in the upper three quarters of the hole and below that hits the acrylic.

## Checks in case.py

`case.py` writes no files if even one check fails, and prints a list of errors:

- parts in the same layer do not overlap in height (the plunger may touch the back frame);
- clearances to board parts (by courtyards, 0.25 mm), the module board (0.4 mm), the glass (0.25 mm), the header, the flex, the cell, the screws, the microphone tube (holders: 0.03 mm) and the USB-C plug body (0.5 mm for the standard size in the bottom and the back frame);
- the lower cell frame rests on a USB-C plug at the standard's limit by no more than 0.05 mm (touch tolerance, currently 0.02 mm);
- the cell is enclosed on all sides: in every frame at its height the cutout for it is closed, and no window of the outline reaches it;
- the cell frames do not extend under parts on the back of the module, except for the 3.3 mm circles at the holes;
- frame and plunger bridges are no thinner than 1.5 mm, walls around the screw and buzzer holes no thinner than 0.9 mm, 0.45 mm for the washers;
- each sheet under a screw holds it with a ring, not an edge;
- the tube covers the microphone hole in the board and does not land on the C4 and C6 pads;
- the plunger actuates the button with kerf from 0 to 0.3 mm;
- at any kerf the plunger wings lie under the board on both sides of the button, by at least 0.3 mm with the board edge at ±0.2 mm and over a strip no shorter than 0.8 mm. Without them the plunger would rise toward the board and hit its edge before actuating the button; this is checked separately;
- over the whole travel to the boss and with play in y, the plunger stays at least 0.25 mm from the button body and 0.1 mm from its pads and does not touch parts on the back, and the pocket lets it travel to the boss;
- the cell wires along its right edge lie in the frame cutout and no closer than 0.2 mm to the screws;
- the bottom has a hole opposite the buzzer, and the back frame is no closer than 0.5 mm to it;
- sheets are only common thicknesses, the display frame is 0.1–0.3 mm below the lid, there is at least 0.5 mm above the glass, and the version's cell fits with 1.1 mm module parts;
- a screw of the right length exists.

`case.py` picks the screw length itself: the shortest common screw that protrudes from the nut by at least 1.2 mm (on nominal sheets or your measurements). With measurements, if there is none, the longest one that protrudes by at least 0.6 mm, with a warning. Common sizes are taken to be nylon M2 × 16, 20 and 25 mm and steel 16, 18, 20, 22, 25 and 30 mm.

`selftest.py` tests the checks themselves: it breaks the model in memory (plunger without wings or with a thin ledge, a wing on a button pad, no buzzer hole, a frame on the cell wires, a USB-C window in the lower cell frame, a back frame or bottom without the plug cutout, no long screws) and expects its own error from `check()` for each breakage. It also recomputes for sheet measurements, thin and thick, and expects warnings without errors. It writes no files.

With sheet measurements (`--sheet`, `--module`), anything that is fixed during assembly is not an error but a warning with dimensions, and the model is built already corrected:

- display frame higher than "module board + washer": sand it to the stated thickness; lower than needed: thicker tape;
- pins above the module close to the lid: shorten to the stated length;
- the version's cell does not fit, washers too thin for the glass;
- the screw protrudes from the nut by less than 1.2 mm (0.6 is enough), or nylon of the needed length is not sold — then steel.

Even with measurements, only what cannot be fixed during assembly remains an error.
