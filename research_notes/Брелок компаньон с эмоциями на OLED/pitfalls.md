# Pitfalls, failure modes and engineering risks ("подводные камни") of a battery-powered OLED "emotion companion" keychain — prototype to small production run

> Research method note (read first): In this session WebFetch could only reach GitHub-hosted pages (raw ESP-IDF docs, u8g2 FAQ). Every other host (forum.arduino.cc, esp32.com, eevblog, adafruit, ti.com, eur-lex, etc.) was refused by the egress proxy, so most findings below come from search-engine result summaries of those pages, not full reads. The shared web-search budget also ran out before a few planned queries (IMU tap app notes, MEMS mic noise, CH1116, JLCPCB rules, the final text of the 2026 battery-removability delegated act). Claims marked "(snippet)" were seen only in a search summary. Facts from my own background knowledge are kept under **Inferences**, never under Cited Findings.

---

## 1. OLED display (SSD1306/SH1106/SSD1315 modules and bare panels)

### Takeaway
The cheap "0.96 inch SSD1306" module is not one product. Listings ship different controllers (SSD1306, SSD1315, sometimes SH1106 under the SSD1306 name), different pin orders (VCC/GND swapped) and different on-board parts. Brightness is low (about 60 cd/m²), and there is no TE/vsync pin. A static face burns in. The panel's row-multiplexed scan can beat with phone cameras. Lock down one verified source, write code that tolerates a different controller, and design the face animation around burn-in and camera capture from day one.

### Cited Findings
**SH1106 vs SSD1306 (the 132-column offset)**
- Symptom: the image is shifted about 2 px, with noise or garbage in the edge columns. Cause: SH1106 RAM is 132 columns wide but the glass shows 128. SSD1306 code starts at column 0, which is hidden on SH1106. — [SparkFun forum](https://forum.sparkfun.com/viewtopic.php?p=196668); [Zbotic](https://zbotic.in/?p=597789)
- Even after shifting the image, one user still saw junk in the last two columns, because the unused RAM columns were never cleared. — [SparkFun community](https://community.sparkfun.com/t/sh1106-oled-display-issue/33567)
- SH1106 supports only page addressing, with no horizontal auto-increment. Each page needs its own column-address reset, so SSD1306 "horizontal addressing + one burst" drivers break. — [Zbotic SH1106 vs SSD1306](https://zbotic.in/sh1106-vs-ssd1306-difference-between-128x64-oled-drivers/); [Universal-Solder](https://www.universal-solder.ca/displays-oled/)
- u8g2 FAQ, verbatim answer to "x-offset on SSD1306 128x64": "This is not a SSD1306 OLED, instead use the SH1106 constructor." — [u8g2 FAQ](https://github.com/olikraus/u8g2/blob/master/doc/faq.txt)
- Sources disagree on whether the hidden columns are 2 on each side or 4 in total. — [search summary across the above](https://controllerstech.com/interface-sh1106-oled-display-with-stm32/)
- The Linux DRM ssd130x driver needed a dedicated patch series to support SH1106, which confirms the two are not drop-in compatible. — [LKML patch](https://lkml.iu.edu/hypermail/linux/kernel/2204.0/10114.html)

**Clone and alternative controllers (SSD1315, CH1116)**
- A module sold as SSD1306 was marked SSD1315. Adafruit_SSD1306 showed only noise, while u8g2 worked. A reply said user registers match SSD1306 but some system registers may need different values. — [Arduino forum "SSD1315"](https://forum.arduino.cc/t/ssd1315/695280) (snippet)
- A later poster in the same thread compared the datasheets and reported a different oscillator frequency and a different K (DCLK per row) value. That may explain flicker seen on SSD1315 but not on SSD1306. — [Arduino forum SSD1315 p.2](https://forum.arduino.cc/t/ssd1315/695280?page=2) (snippet, not verified against the datasheets)
- The Rust `ssd1315` crate says it is "compatible with SSD1306" (earlier versions said "theoretically compatible") and notes SSD1315 has no 6800/8080 parallel bus. — [docs.rs ssd1315](https://docs.rs/crate/ssd1315/0.2.0)
- Flowcode users report SSD1315 "not fully compatible" with their SSD1306 component: left-edge offset and garbled text. — [Flowcode forum](https://www.flowcode.co.uk/forums/viewtopic.php?p=18933)
- Waveshare sells the same 0.96" module in SSD1306 and SSD1315 variants, so even branded vendors swap controllers. — [Waveshare 0.96inch OLED (C)](https://www.waveshare.com/product/0.96inch-oled-module-c.htm)

**Pinout and power-pin swaps between batches**
- Some 0.96" I2C modules are GND-VCC-SCL-SDA and others VCC-GND-SCL-SDA. The reversed ones had a different PCB, with oblong holes. Posters disagreed on whether the reversed boards were SH1106. — [Arduino forum "0.96 I2C OLED power inputs"](https://forum.arduino.cc/t/0-96-i2c-oled-power-inputs/561341)
- On an ESP8266 forum: "several of these displays have a pinout of Vcc-GND-SCL-SDA and only one is GND-Vcc-SCL-SDA". This matters most when the module is soldered to a custom PCB. — [esp8266.com](https://esp8266.com/viewtopic.php?p=67836)
- Reference pinouts from Waveshare and The Pi Hut put VCC on pin 1 and GND on pin 2. — [Waveshare wiki](https://www.waveshare.com/wiki/0.96inch_OLED_(A)); [The Pi Hut](https://thepihut.com/collections/all/products/0-96inch-oled-display-module)

**On-module regulator, pull-ups and 3.0–4.2 V operation**
- Waveshare's 0.96" module schematic (Rev 1.1) shows an RT9193-33 3.3 V LDO, 10 k and 4.7 k resistors, and 0 Ω jumpers that select SPI or I2C. — [Waveshare schematic PDF](https://files.waveshare.com/upload/8/8f/0.96inch_OLED_Module_Schematic.pdf)
- Most modules accept 3.3–5 V and many carry an onboard 3.3 V regulator. Common addresses are 0x3C/0x3D (7-bit). — [Hackaday.io SSD1306 project](https://hackaday.io/project/206427-interfacing-an-ssd1306-oled-display-with-arduino)
- SSD1306 charge-pump input: the Solomon overview gives "VBAT = 3.3 V – 4.2 V for charge pump regulator circuit". One panel datasheet gives 3.0–4.2 V and a charge-pump output (panel VCC) of 7.5 V typical, 7.0 V minimum. — [Solomon Systech](https://www.solomon-systech.com/?p=5534); [Display Elektronik DEP 096016B datasheet](https://cdn.soselectronic.com/productdata/e3/cc/8632cd9a/dep-096016b-w.pdf)
- DigiKey support: the SSD1306BZ "is optimized for a single cell li-po battery". — [DigiKey forum](https://forum.digikey.com/t/mcot064032a1v-yi-vbat/70314)
- SparkFun Micro OLED ties VBAT to VDD by default, in which case VDD "should be around 3.3 V". — [SparkFun Micro OLED Breakout PDF](https://www.electrokit.com/upload/product/41013/41013355/Micro-OLED-Breakout.pdf)
- The charge pump is enabled with 0x8D followed by 0x14 (0x10 disables it). — [mbed SSD1306 driver](https://os.mbed.com/teams/OULOUTECH/code/SSD1306//rev/e479b0296757)
- Custom-PCB bring-up failure: no image because the charge-pump capacitor voltages were low. The same poster had also forgotten the I2C pull-ups. — [EEVblog "Can't get SSD1306 OLED working on custom PCB"](https://eevblog.com/forum/beginners/cant-get-ssd1306-oled-working-on-custom-pcb/msg2323845)

**Sleep current of the module itself**
- Nine I2C OLED modules (six SSD1306) were measured. They drew mA with the display on and about 7–32 µA with the display off or in power-save, and the module regulators may have inflated the readings. An ESP32 with an SSD1306 reached about 7.7 µA total in sleep. — [TTN forum, big ESP32+SX127x topic p.31](https://thethingsnetwork.org/forum/t/big-esp32-sx127x-topic-part-2/11973?page=31)
- An ESP32-C3 board spent most of its time in light sleep yet lasted only about 1.5 days on 500 mAh. Replies: measure the sleep current and switch off peripheral and display power. — [Arduino forum "ESP32-C3 runtime on battery"](https://forum.arduino.cc/t/esp32-c3-runtime-on-battery/1348051)

**Burn-in and lifetime (PMOLED)**
- Hobbyist estimate for SSD1306/SSD1309/SH1106-class modules: T50 (half brightness) of about 5,000–10,000 h at full brightness, rising to about 15,000–25,000 h with contrast at 128. On monochrome modules, every lit pixel ages at the same rate, so uneven use of the panel is the risk. Image retention (temporary) is described as the early warning of permanent burn-in. — [Zbotic burn-in guide](https://zbotic.in/oled-display-burn-in-how-to-prevent-pixel-degradation/)
- PMOLED vendor claims: a UI static for more than 48 h can leave a visible ghost. Limiting brightness to about 50 % adds about 30 % life. Screen-off timeouts raise life from about 10k to more than 15k h. These are vendor claims, not independent tests. — [DisplayModule knowledge base](https://www.displaymodule.com/blogs/knowledge/extending-pmoled-lifespan-screen-savers-current-control-storage)
- Another write-up gives 20–100 kh to half brightness, depending on color. — [msalamon.pl](https://msalamon.pl/en/?p=4335). A 2011 1.3" 128x64 SSD1306 PMOLED was marketed at "50,000 hours", with the end-of-life criterion undefined. — [OLED-Info](https://oled-info.com/osd-introduces-new-50000-hours-13-pmoled-module). The figures conflict, and no manufacturer T50 for a generic AliExpress panel was found.
- In PMOLED lifetime testing, the blue emitter had the shortest lifetime. This is relevant because the common modules are blue or white. — [ÉTS thesis (Fortier)](https://espace.etsmtl.ca/id/eprint/1907/1/FORTIER_Fran%C3%A7ois-Xavier.pdf)
- Burn-in reports exist on the Arduino forum. — ["OLED display and serious burn in"](https://forum.arduino.cc/t/oled-display-and-serious-burn-in/605031); ["SSD1306 OLED how to extend life time"](https://forum.arduino.cc/t/ssd1306-oled-how-to-extend-life-time-and-reduce-consumption/386861) (titles only; contents not read)
- A pixel-shift patent notes that once shift compensation is introduced it "must thereafter always be used" and it increases power consumption. — [USPTO 10475417](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10475417). Samsung OLED monitors ship with pixel shift on by default for static content. — [Samsung support](https://www.samsung.com/sg/support/displays/how-to-solve-image-shift-issues-on-your-samsung-oled-monitor/)

**Brightness and sunlight**
- DFRobot's 0.96" SSD1306 module: brightness "60 (Typ) cd/m²". — [DFRobot DFR0486 wiki](https://wiki.dfrobot.com/DFR0486). A 1.04" SSD1306 panel from Densitron is rated 100 cd/m². — [Densitron DD-2832WE-4A](https://densitron.com/products/product/dd-2832we-4ak)
- SSD1306 has 256-step contrast control, which scales drive current but does not raise the panel's peak luminance. — [Solomon Systech](https://www.solomon-systech.com/?p=5534)

**Frame rate, tearing, flicker**
- Register 0xD5: low nibble is the DCLK divide ratio (1–16, reset value 1), high nibble is the oscillator frequency. — [Zephyr SSD1306 binding](https://nrfconnectdocs.nordicsemi.com/ncs/latest/zephyr/build/dts/api/bindings/display/solomon,ssd1306b-spi.html)
- Crystalfontz: frame rate depends on init. Their demo writes 0x80 to 0xD5 and measured about 177 Hz, against a nominal of about 105 Hz. — [Crystalfontz forum](https://forum.crystalfontz.com/threads/i2c-operating-frequency-of-cfal12832c0-091b-w.5820)
- Practical full-screen update rate over I2C: about 23.5 FPS measured with u8g2 in one test. In another report, 100 kHz I2C gives about 7.7 FPS and 400 kHz gets closer to 30 FPS. — [Arduino forum "SSD1306 U8g2lib framerate"](https://forum.arduino.cc/t/ssd1306-u8g2lib-framerate/522803) (snippet)
- With optimized code one developer pushed an SSD1306 to 151.5 FPS. — [Hackaday "Push It To The Limit: SSD1306 At 150 FPS"](https://hackaday.com/category/arduino-hacks/page/134/)
- u8g2 HW I2C defaults to 100 kHz for some controllers. The FAQ suggests `u8g2.setBusClock(400000)`. Full-buffer "F" mode is faster but needs 1 KB RAM for 128x64. Page modes "1"/"2" save RAM but redraw slower. Software I2C conflicts with other I2C devices on the same pins. — [u8g2 FAQ](https://github.com/olikraus/u8g2/blob/master/doc/faq.txt)
- u8g2 FAQ: "Please connect the reset input of the display with a normal GPIO pin of your board" (not the MCU reset). — [u8g2 FAQ](https://github.com/olikraus/u8g2/blob/master/doc/faq.txt)
- Users report visible flicker and tearing when redrawing continuously, and partial updates did not fully cure it. — [Arduino forum "OLED SSD1306 flickering"](https://forum.arduino.cc/t/oled-ssd1306-flickering/1040319); [MakerForums "tearing artifacts in oled modules"](https://forum.makerforums.info/t/does-anyone-have-any-experience-with-tearing-artifacts-in-oled-modules/61547) (title only)
- Camera banding comes from the display's pulsed emission sampled by a rolling shutter. Changing the shutter speed usually mitigates it, and distance also helps. — [DXOMARK "Flicker, the display affliction"](https://www.dxomark.com/flicker-the-display-affliction/)

**Bicolor (yellow/blue) panels**
- The top 16 rows are yellow and the bottom 48 blue, with a physical break between the zones. It is effectively two displays, and the gap swallows a pixel row band. — [Core Electronics](https://core-electronics.com.au/dual-colour-oled-display-128x64-yellow-and-blue.html); [Embedded Adventures](https://embeddedadventures.com/oled_display_128x64_oled-12864-yb.html); Waveshare lists a "1/4 yellow, 3/4 blue" split — [Waveshare](https://www.waveshare.com/product/0.96inch-oled-module-c.htm)

**Fragile glass and FPC**
- A ribbon crimped and pulled at an angle killed a screen after a slight bend. The ribbon is itself a flex PCB and must not be flexed near its components. Peeling the protective film too fast at a bad angle also caused failures. — [Pimoroni forum "Did I break it?"](https://forums.pimoroni.com/t/did-i-break-it/17295)
- A module died after 1 h because the flex was not making contact, and resoldering fixed it. — [Zbotic product review](https://zbotic.in/?p=4081)
- The bare 0.96" panel FPC uses a non-standard 0.7 mm pitch, so no off-the-shelf mating connector exists. The panel is meant to be soldered. A heat gun used to desolder it damaged the screen. — [element14 thread](https://community.element14.com/technologies/experts/f/forum/11430/need-some-help-choosing-a-0-96-oled-and-a-connector)

### Inferences
- **I2C address confusion:** module silkscreens often print "0x78/0x7A" (8-bit address including the R/W bit), while Arduino/ESP-IDF want 0x3C/0x3D (7-bit). Moving the SA0 resistor flips it. In firmware, scan the bus at boot and accept either address. (Background knowledge; the u8g2 FAQ fetched did not cover addressing.)
- **Controller auto-detect:** SSD1306 and SH1106 can't be reliably told apart over I2C, since the GRAM can't be read in serial mode. Practical defense: (a) buy from one vetted supplier and receive-inspect every lot with a test pattern that lights columns 0–1 and 126–127; (b) keep an "SH1106 mode" in firmware selectable via NVS or a button combo at boot, so a mis-shipped batch can be rescued without reflashing.
- **Burn-in on a face device is the main display risk.** The eyes sit at fixed coordinates for hours, while the 1–2 px outlines around them are rarely lit, so they age differentially. Mitigations: (1) idle "drift" of the whole face by ±2–4 px every few minutes; (2) blinking, glancing and occasional position changes as part of character behavior (these double as liveliness); (3) contrast around 0x40–0x80 indoors (0x81 command) instead of 0xFF; (4) display off (0xAE) when the IMU says the bag is still, or the light sensor says it is dark (inside the bag); (5) avoid large filled areas, preferring outline eyes. The blue/white emitter ages fastest.
- **Camera flicker for TikTok:** a PMOLED is row-multiplexed, so each of the 64 rows is lit only about 1/64 of the frame. A phone's rolling shutter at short exposure catches only a few lit rows, which shows up as rolling dark bands. Mitigations to test: raise the oscillator frequency via 0xD5 (e.g. 0xF0) so the frame rate is well above 100 Hz; lock the camera exposure to an integer multiple of the frame period (≥ 1/60 s, not 1/1000 s); avoid slow-motion modes; keep contrast high while filming. Measure it — no source confirmed exact numbers for SSD1306.
- **No TE pin:** the I2C transfer (about 1 KB per frame) is not synchronized to the panel scan, so fast horizontal motion (blinks) can tear mid-frame. Mitigations: animate in small dirty rectangles; keep eye motion mostly vertical and fast (a blink of 2–3 frames looks fine); use 400 kHz–1 MHz I2C to shorten the window, or prefer SPI modules.
- **Module regulator at LiPo voltage:** a module with an onboard 3.3 V LDO fed from the LiPo (3.0–4.2 V) sits near or at dropout below about 3.4 V. Logic then runs slightly under 3.3 V (fine, since VDD goes down to 1.65 V on SSD1306), but the charge pump input falls below the 3.3 V spec minimum, risking dim or uneven brightness near empty. The LDO's quiescent current also adds to sleep current. Power the module from a switchable rail (load switch or P-FET) for true zero, and if you do, re-init the display after every power-up. Module pull-ups (often 4.7–10 kΩ to the module's VCC) appear in parallel with any other module's pull-ups.
- **Gaps in bicolor panels** break a face design: eyes crossing row 16 get split. Use mono-white or mono-blue for a face, or design the yellow band as "eyebrows/status".
- Sunlight: at about 60 cd/m², the face will be barely visible outdoors in daylight. Design expectations (and marketing videos) for indoor or shade use, and add an auto-brightness boost (max contrast in bright light) accepting faster aging.

### Gaps
- No primary manufacturer T50 or lifetime spec was found for the generic AliExpress 0.96" panels, and the hobbyist and vendor numbers conflict (5k–100k h).
- No source covering CH1116 (or other Chinese SSD1306 clones) compatibility was found; search budget ran out.
- No measured data on SSD1306 vs phone-camera flicker or the effect of 0xD5 settings on video; the camera flicker advice above is inference.
- The SSD1306 datasheet itself (frame-rate formula, I2C tCYCLE) could not be fetched — host blocked.

---

## 2. Battery and power (LiPo, charging, protection, regulators)

### Takeaway
The classic hobby chain "TP4056 module + DW01 + direct LiPo + AMS1117/HT7333" fails in several ways on a 100–500 mAh keychain. Charge current is about 10× too high, there is no power-path, the protection cuts off too low, the LDO drops out and browns out under radio peaks, divider leakage drains the cell, USB-C won't charge from C-to-C cables without CC resistors, and JST polarity is a lottery. Each of these has a cheap fix if designed in early.

### Cited Findings
**Charge current versus small cells**
- The default 1.2 kΩ R_PROG on TP4056 modules sets about 1 A. — [CircuitMess community](https://community.circuitmess.com/t/tp4056-module-current/1639); a reference design lists about 900 mA — [Flux TP4056 design](https://www.flux.ai/vasy_skral/tp4056-module). For a 100 mAh cell that is about 10C.
- Standard LiPo charge is 0.5C, with 1C as "fast". — [Arduino forum TP4056 thread](https://forum.arduino.cc/t/tp4056-to-charge-3-7v-li-ion-battery/516175) (snippet)
- A TP4056-family datasheet lists trickle current (V_BAT < V_TRIKL) at about 120 mA for R_PROG = 1.4 kΩ, which is above 1C for a 100 mAh cell even in pre-charge. — [TP4056 datasheet (LCSC)](https://atta.szlcsc.com/upload/public/pdf/source/20250619/330E91595E0A8AAAC6AB5B71C0FB02E2.pdf) (snippet)

**No load sharing / power path**
- TP4056 terminates when current falls to about C/10 in CV mode. A load on the battery during charge can keep the current from ever falling to C/10, so charging never terminates and the cell can be held at 4.2 V or overcharged. Fix: a P-MOSFET + Schottky "load sharing" circuit. — [best-microcontroller-projects TP4056 p.2](https://www.best-microcontroller-projects.com/tp4056-page2.html); [EEVblog TP4056 thread](https://www.eevblog.com/forum/projects/tp4056-li-ion-charging-and-protection-board)
- The TP4056 apparently lacks the safety timer that some charger ICs have (time-out if C/10 is never reached). — [Arduino forum "Solar tp4056"](https://forum.arduino.cc/t/solar-tp4056-esp01-led-tp4056-blocked/913719) (snippet; forum claim)
- "Nothing should be connected to the battery while you [charge]." — [Arduino forum "TP4056 custom board can't charge"](https://forum.arduino.cc/t/tp4056-custom-board-cant-charge/1372972)

**DW01A protection thresholds and fakes**
- DW01A over-discharge detection is 2.4 V ±0.1 V, with release at 3.0 V. — [best-microcontroller-projects DW01A](https://www.best-microcontroller-projects.com/dw01a.html). Another DW01 datasheet gives 2.50 V detection and 2.90 V release. — [DW01 datasheet (LCSC)](https://atta.szlcsc.com/upload/public/pdf/source/20220427/B6F1961EDEAC0A32E768F65C5E4790F9.pdf)
- A designer argues 2.4 V is too low for cell longevity and that guidelines suggest about 3.0 V. They also report a part sold as a 3.0 V-cutoff chip that still cut at 2.4 V (counterfeit or mislabeled). — [Tindie "The conservative LiPo protection circuit"](https://www.tindie.com/products/xlfe/the-conservative-lipo-protection-circuit/)
- Alternatives with higher thresholds: FS312F (about 2.9 V). — [Arduino forum "Low voltage cut off – the DW01"](https://forum.arduino.cc/t/low-voltage-cut-off-the-dw01/691421)

**Over-discharge and swelling**
- Over-discharge dissolves the anode's copper current collector, which redeposits on the cathode. The gas released can puff up a pouch cell, and copper dendrites can form on the next charge and damage the SEI. — [TI application brief SBAA709](https://www.ti.com/lit/pdf/sbaa709); [CALCE (UMD)](https://calce.umd.edu/news/story/understanding-the-nature-of-copper-dissolution-in-overdischarged-lithiumion-batteries)
- Over-discharge raises internal-short risk via copper dissolution and re-precipitation. — [UMD DRUM thesis](https://drum.lib.umd.edu/handle/1903/25178). An ECS abstract states over-discharge is a risk that "passive protection devices cannot prevent". — [ECS 228 abstract](https://ecs.confex.com/ecs/228/webprogram/Paper59451.html)
- Slowly "recovering" deeply discharged cells is considered dangerous by some practitioners, who say to recycle them instead. This is opinion, with no validated recovery procedure found. — [esk8 forum](https://forum.esk8.news/t/what-is-the-reason-for-the-discharge/65677)

**Charging temperature**
- Charging below about 0 °C plates metallic lithium on the anode, which is permanent, and most manufacturers stop charging below roughly 0 °C. — [Grepow](https://www.grepow.com/blog/charge-and-discharge-design-of-low-temperature-battery.html); [USPTO 10923775](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10923775)
- Typical charge window: +5 °C to +45 °C. The commonly quoted −30…+80 °C applies to discharge only. — [REDARC support](https://us.support.redarcelectronics.com/hc/en-us/articles/13856244101007-Why-you-should-not-charge-a-lithium-battery-below-0-C-or-32-F); [Cadex/ELFA PDF](https://www.elfa.nl/wp-content/uploads/2023/08/Charging-at-high-and-low-temperatures.pdf)

**Brownout from radio peaks and LDO dropout**
- ESP32 Wi-Fi TX peaks are about 400–500 mA. An AMS1117 needs about 1.1 V headroom, so a 3.7 V LiPo can't hold 3.3 V for long, and the brownout detector resets the chip. — [espboards.dev](https://www.espboards.dev/troubleshooting/issues/power/esp32-battery-low-voltage/)
- Bulk capacitance of 100–470 µF on 3.3 V is the common fix. — [PlatformIO community](https://community.platformio.org/t/esp32-platformio-no-work/5812); [esp32.com](https://esp32.com/viewtopic.php?p=52452). But a big capacitor recharging after each spike can itself overload a weak LDO. — [esp32.com "Starting wifi trips brownout"](https://esp32.com/viewtopic.php?p=21162)
- Disabling the brownout detector hides the cause and is inadvisable. — [Universal-Solder](https://www.universal-solder.ca/power-instability/)
- AMS1117 (about 1.2 V dropout) is "not appropriate for single-cell lithiums". ME6211 is about 100 mV nominal at its rating, "really somewhat higher". — [esp32.com LiPo + LDO thread](https://esp32.com/viewtopic.php?p=28801)
- Espressif staff on HT7333 with a LiPo: it can only supply 250 mA while RF peaks need ≥ 500 mA, and its dropout "can easily reach > 1 V (@250 mA), which can brown out flash chips". — [esp32.com](https://esp32.com/viewtopic.php?p=28802)
- ESP32 dev kit plus LiPo brownout reports are common. — [esp32.com "ESP32 Devkit with LiPo battery triggers brownout"](https://esp32.com/viewtopic.php?p=44289)

**Battery-voltage divider leakage**
- A 10 k divider draws about 210 µA continuously, which drains a small node's battery in months. A low-side N-FET to "switch off" the divider exposes the ADC pin to full battery voltage and leaks backwards through the MCU's ESD diodes. Fix: a high-side P-FET with an N-FET level shifter. — [EveryCircuit "switched battery voltage sensing"](https://everycircuit.com/circuit/6308045315506176/switched-battery-voltage-sensing)
- With high-value dividers (MΩ), add a capacitor at the ADC pin, or buffer it, because ADC input impedance can be as low as 10–50 kΩ on some MCUs. Forum opinions differ. — [EEVblog "Battery Monitoring – Voltage Divider"](https://www.eevblog.com/forum/projects/battery-monitoring-voltage-divider/msg2524116/)

**USB-C charging**
- Without 5.1 kΩ pull-downs on both CC1 and CC2, a USB-C source (C-to-C cable or charger) will not enable VBUS. C-to-A cables work regardless, because legacy sources always supply 5 V. — [Flux TP4056 USB-C design notes](https://www.flux.ai/mcprince96/tp4056-usb-c-with-battery-protection); [Arduino forum](https://forum.arduino.cc/t/tp4056-custom-board-cant-charge/1372972) ("You need those resistors or USB-C won't work")
- Classic symptom thread: a board works with C-to-A but not C-to-C even though pull-downs are fitted, so check the wiring and solder joints of the CC resistors. — [esp32.com](https://esp32.com/viewtopic.php?p=129526)

**JST connector polarity**
- There is no polarity standard for JST-PH 2.0. Adafruit, SparkFun and Particle use the same orientation, while many RC/drone and Amazon batteries are reversed. — [Particle community](https://community.particle.io/t/why-isnt-battery-polarity-always-the-same-in-jst-pins/44474); [Blues forum](https://discuss.blues.com/t/board-heat-from-lipo/405); [LowPowerLab](https://lowpowerlab.com/forum/moteino-m0/lipo-battery-recommendations/)
- "JST connectors themselves have no indication which terminal should be … positive". The crimper decides. — [RobotShop tutorial](https://community.robotshop.com/tutorials/show/reversing-the-polarity-on-lipo-battery-jst-connectors)
- A reversed drone battery made a SparkFun OpenLog Artemis smoke. — [SparkFun community](https://community.sparkfun.com/t/problems-with-lipo-battery-on-openlog-artemis/41035). In one review, a battery even had the black wire not negative. — [desertcart review](https://www.desertcart.com.ar/products/88899527)

### Inferences
- **TP4056 R_PROG** (datasheet table from background knowledge, verify): I ≈ 1200 V / R_PROG, so 10 kΩ ≈ 130 mA, 5 kΩ ≈ 250 mA, 1.2 kΩ ≈ 1 A. For 100–150 mAh use ≥ 10–20 kΩ (60–120 mA). For 300–500 mAh, 2.7–5 kΩ. Trickle (pre-charge) and termination scale with it.
- **Better charger choice for a product:** use a small charger with power-path and NTC input (e.g. linear chargers with JEITA/TS pin, or a PMIC), or at minimum add the P-FET load-sharing circuit. A keychain gets charged in cold cars, winter pockets and sun-heated bags, so wire the TP4056's TEMP pin to an NTC (or the equivalent on the chosen IC) instead of tying it to GND as most modules do.
- **Protection:** for a pouch cell carried on a bag, buy cells with their own PCM (protection board) fitted, and set a firmware cutoff around 3.3–3.4 V under load (deep sleep, display off, show "sleepy/hungry" face). Treat the DW01's 2.4 V as a last-resort fuse only. Shelf storage: total sleep draw must be ≤ a few µA or a 100 mAh cell goes flat in months. Ship at about 40–60 % SoC with a "shipping mode" (latching load switch) so the cell isn't drained between factory and customer.
- **LDO choice:** pick a ≤ 100–200 mV-dropout, low-Iq LDO (ME6211/RT9080-class, Iq 1–40 µA) or a buck-boost. Expect usable capacity to end around 3.4–3.5 V with an LDO. A LiPo's remaining energy below 3.5 V is a small fraction, so this is acceptable. BLE-only (no Wi-Fi) peaks are much lower (tens of mA), which greatly reduces brownout risk — another argument for nRF52/ESP32-C3 BLE-only firmware.
- **USB-C:** use two separate 5.1 kΩ resistors (one per CC pin). A single shared resistor for both CC pins is a well-known wrong design that fails with e-marked or compliant C-to-C cables. Test with a C-to-C cable and a USB-PD charger (e.g., a MacBook charger), not just C-to-A.
- **Puncture risk on a keychain:** the carabiner, keys and ring hit the device constantly. Keep the cell away from the eyelet and from any screw boss. Add a rigid shell layer or plate between cell and outer face, and leave about 5–10 % swelling space around the pouch.
- **JST:** for production, avoid detachable JST entirely. Solder the cell tabs or wires directly, or use a keyed connector whose polarity you specify to the cell vendor in writing, and check it with incoming inspection.

### Gaps
- Battery University pages could not be fetched (host blocked) for primary-source charge-temperature or capacity-versus-cutoff data.
- No source on mechanical abuse (puncture or crush) of small LiPo pouches in wearables or keychains specifically.
- No validated deep-discharge recovery threshold or procedure was found. The advice is "don't".

---

## 3. MCU and firmware (ESP32-C3/C6/S3, nRF52840, RP2040, STM32L)

### Takeaway
Most "datasheet says 5 µA, I measure 300 µA" stories are board-level: LDO Iq, USB-UART bridge, LEDs, pull-ups, and an always-on OLED. ESP32-C3-specific traps include the USB-Serial/JTAG port vanishing in sleep, the GPIO2/8/9 strapping pins, no FPU, and RC-clock drift. Generic firmware traps include I2C lockups, NVS wear from frequent saves, and OTA without rollback. RP2040 is a poor fit for a coin-sized battery device. nRF52840 has its own DC/DC-inductor trap.

### Cited Findings
**Dev-board sleep current versus datasheet**
- ESP32-C3 thread: an LM1117 LDO on the DevKit-M1 (high Iq) and the USB-serial chip add current. A bare module measured about 80–90 µA, and a custom board about 300 µA, against a datasheet figure of about 5 µA. The OP's culprit was an incompletely removed resistor feeding the smart LED. — [esp32.com "ESP32-C3 power consumption doesn't match the datasheet"](https://esp32.com/viewtopic.php?p=83688)

**ESP32-C3 USB-Serial/JTAG**
- Deep sleep: the USB-Serial/JTAG controller and PHY lose power, so the host sees a disconnect. Light sleep: clocks are gated, the host may report the device as faulty, and an unplug or replug may be needed. If the app reconfigures the USB pins (GPIO18/19) or disables the controller, the device "disappears". Recovery: hold the boot pin (GPIO9 on C3/C6, GPIO0 on S3) low and reset. With no terminal open, output stalls for about 50 ms once when the buffer fills. — [ESP-IDF docs (GitHub source)](https://github.com/espressif/esp-idf/blob/master/docs/en/api-guides/usb-serial-jtag-console.rst); [Espressif docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-guides/usb-serial-jtag-console.html)

**Strapping pins (ESP32-C3)**
- GPIO8 = 0 together with GPIO9 = 0 is an invalid strap combination and causes unexpected boot behavior. Recommendation: pull GPIO2 and GPIO8 up, and leave GPIO9 floating (grounded only for download). An EN RC delay works only if the supply has a well-defined rise time. With slow or unstable supplies, use a voltage supervisor. — [esp32.com strapping thread](https://esp32.com/viewtopic.php?p=145832); [esptool issue #774](https://github.com/espressif/esptool/issues/774)

**GPIO and peripherals in deep sleep (ESP-IDF)**
- "IOs have internal pullups or pulldowns, which are enabled by default". Combined with external pulls, they raise sleep current (the WROVER GPIO12 example). Use `rtc_gpio_isolate()` before deep sleep, and use HOLD to keep pulls during sleep. — [ESP-IDF sleep modes (GitHub)](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/sleep_modes.rst)
- Wi-Fi and BT must be disabled before sleep, and connections are not maintained in deep or light sleep. Deep sleep always powers down SPI flash, while powering flash down in light sleep is discouraged. Deep-sleep GPIO wake works only on VDD3P3_RTC-domain pins. — [same](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/system/sleep_modes.rst)

**I2C bus lockup**
- If the MCU resets mid-transfer, a slave can hold SDA low while waiting for clocks. Recovery per NXP UM10204: switch SCL/SDA to GPIO, clock SCL 9 times, issue STOP, then restore I2C. If SCL itself is stuck, power-cycle the slave. Run recovery at init when the problem recurs. — [NXP community (AN4803 routine)](https://community.nxp.com/t5/S32K/S32K-IIC-Communication-Error-Recovery/m-p/1619032); [Total Phase blog](https://www.totalphase.com/blog/2022/10/clearing-stuck-i2c-bus-using-aardvark-adapter-promira-serial-platform/)
- Pitfalls: recovery can fail (SDA still low after 9 clocks). — [TI E2E China](https://e2echina.ti.com/support/processors/f/processors-forum/579068/tda4vm-i2c-bus-recovery-function). Stopping early when SDA reads high is questionable, because a slave could be sending 0xFF. — [EmbeddedRelated](https://embeddedrelated.com/showthread/comp.arch.embedded/277057-1.php)

**I2C pull-ups and bus capacitance**
- TI SLVA689: Rp(max) = t_r / (0.8473 × C_b); Fast-mode limits t_r = 300 ns, C_b = 400 pF. Rp(min) comes from the 3 mA sink current at V_OL. — [TI SLVA689](https://ti.com/lit/SLVA689). Measuring rise time 10–90 % instead of 30–70 % overstates it (534 ns versus 193 ns in one test). — [element14 Workbench Wednesdays](https://community.element14.com/challenges-projects/element14-presents/workbenchwednesdays/w/documents/28424/if-and-when-do-i2c-pull-up-values-matter----workbench-wednesdays-83)

**Flash wear (NVS)**
- NVS is append-only with wear leveling "by design", and "works best for storing a moderate, relatively stable set of small values". Continuously growing or frequently rewritten data "increases flash wear", so use a filesystem instead. Data never rewritten pins pages out of wear leveling. NVS is power-loss safe except for the pair being written. Partitions are "difficult to resize … in the field", so size generously. RAM cost is about 22 KB per 1 MB of partition. — [ESP-IDF NVS docs (GitHub)](https://github.com/espressif/esp-idf/blob/master/docs/en/api-reference/storage/nvs_flash.rst)
- Forum math: about 126 32-byte entries per 4 KB page, 100k erase cycles. Default NVS is about 0x5000 (20 KB). A 2017 estimate: writing once per second wears out a 16 KB NVS in under a year. — [esp32.com "NVS read/write over usage"](https://esp32.com/viewtopic.php?p=18021). Meshtastic reports no flash wear-out after about 4 years in the field. — [Meshtastic discourse](https://meshtastic.discourse.group/t/on-chip-flash-endurance/9939)

**RTC drift**
- The default internal 150 kHz RC varies with temperature, so time drifts in deep and light sleep. An external 32 kHz crystal costs about +1 µA, and the 8.5 MHz/256 option about +5 µA. — [ESP-IDF system time docs (v4.1, GitHub)](https://github.com/espressif/esp-idf/blob/v4.1/docs/en/api-reference/system/system_time.rst). Forum: ±5 %, "2 min fast in 10 h". — [esp32.com](https://esp32.com/viewtopic.php?p=25836). On the C6, Espressif staff recommend an external precise clock rather than another RC. — [esp32.com C6](https://esp32.com/viewtopic.php?p=128004)

**No FPU on ESP32-C3**
- Espressif: on ESP32-C3 "floating-point math is executed in software". — [Espressif blog "cores with FPU" (Oct 2025)](https://developer.espressif.com/blog/2025/10/cores_with_fpu/). ArduinoFFT runs about 6× slower on S2 than on ESP32 and another about 2× slower on C3. — [ArduinoFFT issue #77](https://github.com/kosme/arduinofft/issues/77)

**OTA bricking**
- ESP-IDF rollback needs `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`, and the new app must call `esp_ota_mark_app_valid_cancel_rollback()` after self-test. If it doesn't, or crashes or loses power on first boot, the device rolls back. The factory partition is not a rollback target. Anti-rollback (eFuse security version) is a separate feature. — [ESP-IDF OTA docs (C3)](https://docs.espressif.com/projects/esp-idf/en/v5.2/esp32c3/api-reference/system/ota.html)

**nRF52840 DC/DC trap**
- Enabling DC/DC (in code, not only in devicetree) without the inductor fitted starves the regulator, leaving the chip in a power-on-reset loop (no permanent damage). Recovery is a looped `nrfjprog --recover`. In REG0 normal (3 V) mode the REG0 inductor is irrelevant. — [Nordic DevZone 76380](https://devzone.nordicsemi.com/f/nordic-q-a/76380/enabling-dc-dc-converter-for-reg0-stage-nrf52840/315866); [DevZone 125533](https://devzone.nordicsemi.com/f/nordic-q-a/125533/unable-to-use-ldo-power-on-nrf52840). On an nRF5340 the loop recovery failed after hundreds of tries. — [DevZone 108869](https://devzone.nordicsemi.com/f/nordic-q-a/108869/unable-to-recover-nrf5340-flashed-with-invalid-dcdc-configuration/473708)

**RP2040 sleep current**
- RP2040 minimums are about 0.18 mA (dormant) and 0.39 mA (sleep), versus a few µA for nRF52 with RAM retention. — [TinyGo low-power docs](https://tinygo.org/docs/concepts/low-power/). Pico boards measured 6–9 mA in "sleep" with board overhead. — [pico-sdk commit](https://pigweed.googlesource.com/third_party/github/raspberrypi/pico-sdk/+/9d5da1ca93c85bdb78c63c177663df53d901da1e%5E%21/). Waking from dormant with the RTC needs an external clock source. — [rp-hal issue #659](https://github.com/rp-rs/rp-hal/issues/659)

### Inferences
- **Keychain-specific strapping trap:** if the single user button is placed on GPIO9 (as on many C3 boards, "BOOT"), a user holding the button while plugging in USB, or during a brownout reset, boots into download mode. The device looks "dead" until the next reset. Put the user button on a non-strapping RTC-capable pin, or detect and handle the case with a watchdog-driven reset.
- **Flashing a sleeping C3:** add a "stay awake for N seconds after boot or USB-VBUS detect" rule, and a "hold button at boot = no sleep" rule. Detect VBUS (divider to a GPIO) to skip deep sleep while charging. Expose GPIO9 and EN as test pads on the PCB for production rescue.
- **I2C:** on a keychain, the OLED and IMU share the bus and the MCU often resets from brownout mid-transfer, so run bus recovery on every boot. Module pull-ups (4.7–10 kΩ) from the OLED plus the IMU breakout pull-ups in parallel can drop to about 2–3 kΩ. That is fine for 400 kHz, but each low-driven line draws about 1–1.5 mA at 3.3 V, so never leave SDA/SCL driven low in sleep. If the display rail is switched off while the MCU's pins are pulled up, the module is back-powered through the pull-ups and ESD diodes ("phantom" current, half-alive display).
- **Saving "mood" or "pet state":** don't write NVS on every emotion change. Keep state in RTC memory (survives deep sleep) and commit to NVS every N minutes, on low battery, or when charging starts. Size the NVS partition generously from day one, since it is hard to resize later.
- **Float math:** on the C3, do animation easing with fixed-point or integer lookup tables. On S3, nRF52840 (Cortex-M4F) and STM32L4 (M4F) single-precision float is hardware.
- **Watchdog and heap:** long blocking full-frame I2C writes (about 25 ms at 400 kHz) inside tight loops can trip the task WDT on ESP-IDF. Use yields, DMA or partial updates. Avoid Arduino `String` and per-frame allocations, preferring static frame buffers, to avoid heap fragmentation over weeks of uptime. (Background knowledge; not source-verified in this session.)
- **OTA:** if BLE OTA is offered, always enable rollback, refuse OTA below about 30–40 % battery, and keep a factory or recovery image. A brownout mid-OTA on a 100 mAh cell is likely.

### Gaps
- No sources were gathered on task-watchdog resets, heap fragmentation, or Arduino-core library bloat; the search budget was exhausted.
- No source on STM32L-specific pitfalls (e.g., debugger disconnect in Stop mode, the need for low-power debug configuration).
- The ESP32-C3 datasheet strapping table could not be fetched.

---

## 4. Sensors (IMU, microphone, light sensor) on a constantly moving keychain

### Takeaway
A keychain is the worst case for motion-based input. It swings with every step and knocks against keys and the bag, and inside the bag it is dark and muffled. Plain accelerometer thresholds produce constant false "taps" and "shakes". Design input to need deliberate, structured gestures (double or triple tap with timing windows, plus orientation hold), and use sensors mainly for context (moving or still, dark or light) rather than for precise commands.

### Cited Findings
- Accelerometer-only tap detection "cannot distinguish finger tap and periodic motion" (e.g., a rough road). A device in a pocket "comes into contact with an object" and triggers taps unintentionally. Walking produces repeated motion erroneously detected as taps if the threshold is low. Raising the threshold makes real taps get missed, and users then disable the feature. — [US8326569B2 Tap detection](https://patents.google.com/patent/US8326569); [USPTO 9710048 false wake](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/9710048); [US 20160139169](https://patents.justia.com/patent/20160139169)
- Discrimination techniques in patents: use jerk (derivative of acceleration) and peak timing; double-tap latency and window timers; confirm on multiple axes; fuse with another sensor. Earbuds fuse an optical proximity pattern, and audio+motion fusion rejects claps and wind. One patent targets a false-positive rate below 5 % with detection above 95 %, and says conventional accelerometer methods fall short. — [USPTO 8442797 directional tap](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/8442797); [USPTO 11579710 double-tap](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11579710); [USPTO 10291975 earbuds](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/10291975); [USPTO 11302342 acoustic tap](https://image-ppubs.uspto.gov/dirsearch-public/print/downloadPdf/11302342)

### Inferences
- **Distinguish intentional from ambient motion:** (1) gate gestures on a "held in hand" state: the device is lifted and the screen faces up (gravity vector stable for 300–500 ms) after a period of swinging. (2) Require double or triple tap with tight timing (e.g., 2nd tap 80–400 ms after the 1st, quiet before and after), not single tap. (3) Treat swinging (0.5–2 Hz pendulum around the carabiner pivot) and walking (1.5–2.5 Hz steps) as context ("walking with you" animation), never as commands. (4) Use the IMU's built-in tap, activity and inactivity engines (LIS2DW12, LIS3DH, BMA4xx and similar) so the MCU sleeps, but tune them with real-world recordings from a bag on a commute. Log raw IMU data during field tests and build a labeled dataset before setting thresholds.
- **Button versus IMU:** a physical button is the only fully reliable input on a keychain. Use the IMU for "wake to face" and "mood from motion" only.
- **Microphone:** in a bag or on a strap the mic hears fabric rustle, wind and handling impacts (keys hitting the case transmit structurally), all of which look like "loud sound" events. Use band-limited energy (e.g., speech band) with hysteresis, ignore events coincident with IMU shocks, and expect wind to dominate outdoors. Privacy-wise, prefer on-device level or "clap" detection with no recording (see section 9).
- **Light sensor:** inside a bag it reads "dark" most of the day. Use it as a feature ("it's dark here — sleep"), which also saves battery and reduces OLED aging. Behind a tinted window the reading is attenuated and spectrally shifted, so calibrate with the final enclosure. Avoid placing it where the OLED's own light leaks into it, or sample with the display momentarily off.
- **Sensors behind the enclosure:** a mic needs an acoustic port (with mesh or membrane for water). A light sensor needs a clear (not tinted) window. IMU orientation axes must be mapped to the final mounting.

### Gaps
- No vendor application-note data (e.g., ST AN for LIS2DW12 tap configuration) or academic measurements of false-tap rates for keychains or dangling objects were retrieved; the search budget ran out.
- No sources gathered on MEMS microphone wind or rustle noise in wearables, or on light-sensor behavior in bags.

---

## 5. Mechanical and enclosure (keychain abuse, drops, ingress, antenna)

### Takeaway
The keychain is a hammer. It gets swung on a carabiner, hit against keys and dropped on concrete. The weak points are the eyelet (a stress concentrator in a layered 3D print), the OLED glass and FPC, the unprotected display window, and the antenna next to the battery and metal ring. PETG/PC/nylon or SLA "tough" resins beat PLA and standard resin. The antenna must be tested in the final enclosure with the battery and keys attached.

### Cited Findings
- Keychain design should use a print orientation that prevents "snapping along layer lines". Standard PLA is "too brittle" for daily carry, so PETG is recommended. — [Siraya Tech 3D printed keychain guide](https://siraya.tech/blogs/news/3d-printed-keychain)
- PETG is "a bit tougher than plain PLA", and PLA fobs "soften and deform if left in the car". — [Maslow CNC forum](https://forums.maslowcnc.com/t/3d-printed-anchor-point/20113?page=2)
- Antennas: ceramic chip antennas assume a clearance zone with "no ground plane, no fill, no traces on any layer". A pour under the antenna can shift resonance "clean out of band". — [Hubble antenna layout guide](https://hubble.com/community/guides/kyocera-s-new-antenna-tool-picks-the-part-but-it-won-t-fix-your-ble-range-without-these-layout-rules/)
- An ESP32 module soldered flush to a carrier board was "completely detuned" by the board copper or substrate, and an air gap fixed it (anecdote). — [mischianti forum](https://mischianti.org/forums/reply/35898/)
- XIAO ESP32-C3 guidance: keep the antenna away from large batteries, ground planes and metal enclosure walls, and test RSSI and connection time with the complete enclosure, battery and wiring. — [OpenELAB XIAO ESP32-C3 guide](https://openelab.io/cs/blogs/learn/seeed-studio-xiao-esp32-c3-guide)
- Espressif's ESP32-C3 PCB layout guidelines (official) cover antenna and RF layout. — [Espressif HW design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c3/pcb-layout-design.html)
- OLED handling fragility. — see Section 1 ([Pimoroni](https://forums.pimoroni.com/t/did-i-break-it/17295), [element14](https://community.element14.com/technologies/experts/f/forum/11430/need-some-help-choosing-a-0-96-oled-and-a-connector))

### Inferences
- **Eyelet:** never hang the device from the PCB or from a thin printed tab. Use a through-loop molded into the shell's thickest wall, printed so the load path runs along layers (loop in the XY plane), or embed a metal insert or ring that carries the load into a larger area. Round all internal corners. Pull-test (e.g., 5–10 kg static plus repeated jerk) and drop-test (1–1.5 m onto concrete, multiple orientations) every revision.
- **Display window:** the OLED glass needs a separate cover, recessed 0.5–1 mm below the shell rim so keys don't touch it. PMMA (acrylic) is more scratch-resistant but brittle. PC is impact-tough but scratches, so hard-coated PC is the compromise. Leave a gap or foam gasket between cover and OLED glass so impacts don't transfer, and add a strain relief or adhesive anchor for the FPC so it never flexes near the glass.
- **Ingress:** rain on a bag and wet hands are expected. Gasket the shell seam, use a sealed tact switch or a membrane over the button, and use USB-C receptacles rated IPx7 (or a silicone plug). Don't claim an IP rating without testing.
- **Thickness stack:** OLED module (about 3–4 mm with header pins, or 1.4–1.6 mm for a bare panel) + PCB (0.8–1.0 mm) + components (about 1.5 mm) + LiPo (100 mAh about 3.5–4.5 mm thick) + walls (2 × 1.2–1.6 mm) + air gap gives about 12–15 mm. Bare panel on a custom PCB and a thin cell are the main levers.
- **Antenna:** the carabiner and keys are a moving metal object near the antenna, so RSSI will vary by orientation. Point the antenna end of the PCB away from the eyelet and battery, and keep metal hardware (inserts, screws) out of the keep-out.
- **Heat:** a bag in summer sun or a car dashboard can exceed 60 °C. PLA softens (~55–60 °C) and LiPo charging must stop above 45 °C, which argues for PETG/ASA/PC shells and NTC-gated charging.
- **3D print tolerances:** printed press fits vary between PLA and PETG and between printers. Lock material and printer for the run, and design snap-fits or screws with test coupons.

### Gaps
- No controlled data on 3D-printed keyring-hole strength by orientation and material, or on resin shell drop performance.
- No source on display-window material choice (PMMA vs PC) for keychains, or on IP ratings for small consumer gadgets.

---

## 6. Electrical and PCB design

### Takeaway
Exposed USB-C, buttons and a metal carabiner make ESD a real field-failure source. The OLED charge pump needs its specified capacitors placed close. Antenna keep-out must be honored. The bare 0.7 mm-pitch OLED FPC is a soldered part with no standard connector, which complicates PCBA.

### Cited Findings
- Any user-exposed connector needs IEC 61000-4-2 protection. For USB-C, use DC-tolerant TVS on CC and VBUS, not basic ESD diodes. — [TI SSZTAL6](https://www.ti.com/lit/pdf/ssztal6). Example part: TI ESD122, rated 17 kV contact and air. — [TI ESD122](https://www.ti.com/product/ESD122). Place the ESD part as close as possible to the connector pin. — [TDK TVS product brief](https://www.tdk-electronics.tdk.com/download/3170758/3f9a15e9abc0cedb96fa7fb69796ad3d/tvs-diode-pb.pdf); [Semtech app note SI21-03](https://semtech.com/uploads/design-support/TVS_App_Notes-SI21-03-ESD_Protection_of_USB_Type-C_Interfaces.pdf)
- Custom-PCB SSD1306 failures were traced to charge-pump capacitor problems and missing I2C pull-ups. — [EEVblog](https://eevblog.com/forum/beginners/cant-get-ssd1306-oled-working-on-custom-pcb/msg2323845)
- The 0.7 mm-pitch 0.96" panel FPC has no standard mating connector and is designed to be soldered. Desoldering with a heat gun damaged the panel. — [element14](https://community.element14.com/technologies/experts/f/forum/11430/need-some-help-choosing-a-0-96-oled-and-a-connector)
- Antenna keep-out: no copper on any layer. — [Hubble guide](https://hubble.com/community/guides/kyocera-s-new-antenna-tool-picks-the-part-but-it-won-t-fix-your-ble-range-without-these-layout-rules/)
- Strapping-pin biasing and EN RC timing. — [esp32.com](https://esp32.com/viewtopic.php?p=145832)

### Inferences
- **ESD on a keychain:** the user touches the device after walking on carpet, and the metal ring and keys can discharge into the eyelet area. Put TVS on USB D+/D−, CC1/CC2 and VBUS, and on the button line. Keep button traces short and away from the I2C and OLED lines. Ensure no exposed metal (screws, inserts) connects directly to internal nets. Bring each shell seam's internal ground away from the edge or use creepage.
- **OLED charge pump:** for a bare panel, copy the panel datasheet's reference circuit exactly. Typical capacitors are C1P/C1N and C2P/C2N flying caps (about 1 µF), VCC (about 2.2–4.7 µF, rated ≥ 16 V), VCOMH (about 2.2–4.7 µF) and IREF resistor (about 390–910 kΩ, panel-specific). Wrong IREF gives wrong brightness, and missing caps give a dim or dead display. Use X5R/X7R caps with voltage derating for the 7–7.5 V VCC rail.
- **FPC assembly:** most PCBA houses (including JLCPCB-class) place SMT parts by reflow, but a hand-placed OLED FPC usually needs manual hot-bar or iron soldering as a post-reflow step. Plan a jig, use the panel vendor's recommended land pattern, and consider ordering panels with a ZIF-compatible FPC tail (0.5 mm pitch variants exist from some suppliers) to make the display a connectorized part. Alternatively, keep the 4-pin module for small runs and accept the thickness.
- **Test points:** expose 3V3, GND, EN, GPIO9 (boot), TX/RX or USB D+/D−, and battery+ as pogo pads for flashing and production test.

### Gaps
- JLCPCB-specific constraints (e.g., whether they hot-bar FPCs, minimum pitch for hand-soldered parts, LiPo handling in assembly) were not researched; search budget exhausted.
- No primary SSD1306 bare-panel reference schematic was fetched (hosts blocked).

---

## 7. Manufacturing and sourcing (AliExpress batches, counterfeits, scale-up)

### Takeaway
For a small run, the biggest supply risk is that the "same" AliExpress listing ships different parts between orders: controller, pin order, regulator, DW01 variant. Counterfeit or remarked ICs also come from untraceable sellers. Buy everything you can from traceable distributors (LCSC, DigiKey, Mouser), buy all OLEDs and cells for the run in one lot, and do incoming inspection. Crowdfunded-hardware post-mortems point to premature inventory commitment, underestimated tooling and packaging work, and supplier strain as the classic schedule killers.

### Cited Findings
- Sourcing advice from EEVblog: "LCSC is an excellent supplier. Taobao/Ali and other stuff like this with individual sellers and no traceability are problematic." — [EEVblog counterfeit thread](https://eevblog.com/forum/microcontrollers/counterfeit-integrated-circuits-a-rising-threat/msg3559396)
- Counterfeit types: remarked or relabeled, reclaimed, out-of-spec overproduction, clones, empty packages. Removing marks and re-printing a different part number is "a simple matter". — [Electronic Design](https://www.electronicdesign.com/technologies/test-measurement/article/21185936/saelig-detecting-counterfeit-ics); [SGS white paper](https://sgs.com/en/-/media/sgscorp/documents/corporate/white-papers/sgs-cp-recognition-of-component-counterfeits-white-paper-en); [Industrial Monitor Direct](https://industrialmonitordirect.com/blogs/knowledgebase/detecting-counterfeit-ics-in-industrial-electronics)
- A "3.0 V cutoff" protection IC that actually cut at 2.4 V (suspected fake). — [Tindie](https://www.tindie.com/products/xlfe/the-conservative-lipo-protection-circuit/)
- OLED batch variance: VCC/GND order swaps ([Arduino forum](https://forum.arduino.cc/t/0-96-i2c-oled-power-inputs/561341)), SSD1315 shipped as SSD1306 ([Arduino forum](https://forum.arduino.cc/t/ssd1315/695280)), and vendors offering both controllers ([Waveshare](https://www.waveshare.com/product/0.96inch-oled-module-c.htm)).
- Crowdfunded-hardware lessons: committing to inventory too early, then finding design flaws; needing supply-chain and shipping expertise; tooling, packaging, backer communication and component search adding up to more work than a small team can handle (the Twine case); suppliers renegotiating or failing at new quantity. — [Fast Company "Why your Kickstarter project is late"](https://www.fastcompany.com/3004024/why-your-kickstarter-project-late); [VentureBeat "How my Kickstarter blew up my life"](https://venturebeat.com/business/how-my-kickstarter-blew-up-my-life); [GrabCAD blog](https://blog.grabcad.com/?p=24174)
- A Chalmers thesis on delayed crowdfunded hardware found design-for-manufacturability got "surprisingly little regard" compared with traditionally funded development. — [Chalmers ODR](https://odr.chalmers.se/handle/20.500.12380/301564)

### Inferences
- **Incoming QC jig:** a pogo-pin fixture that powers each OLED from a current-limited supply, runs an I2C scan, draws a border test pattern (catches SH1106 offset and dead columns) and checks current. Check every LiPo for voltage, polarity and internal resistance before assembly.
- **Lot discipline:** order the full run's OLEDs plus 10–15 % spares from one seller in one order, and photograph the back (controller marking, regulator, resistor positions) as the "golden sample". Re-qualify any reorder.
- **Cost creep:** the BOM is cheap, but the real per-unit cost is dominated by enclosure (printing or resin post-processing time), hand assembly of OLED and battery, packaging, battery-compliant shipping, certifications and returns. Budget certification and shipping as fixed costs before pricing.
- **Lead times:** LiPo cells with a custom connector or polarity and with UN38.3 paperwork, and custom-tail OLED panels, often have MOQs and multi-week lead times. Commit early to cells that already have UN38.3 summaries.

### Gaps
- No data on actual AliExpress batch-variance rates or counterfeit incidence for SSD1306 modules specifically.
- Recent (2025–2026) small-hardware post-mortems were not found; the sources above are mostly 2013–2016 and a 2020 thesis.

---

## 8. Legal, certification and shipping (EU, US, EAEU/Russia; status as of October 2026)

### Takeaway
A BLE or Wi-Fi keychain sold commercially is a radio product. In the EU it falls under RED (including the cybersecurity delegated act applicable since 1 Aug 2025 if internet-connectable, even via a phone), GPSR (since 13 Dec 2024, with an EU responsible person), the Battery Regulation (CE on batteries since Aug 2024, the crossed-out bin since Aug 2025, a QR code from 18 Feb 2027, and end-user removability from 18 Feb 2027 unless an exemption applies), WEEE/EPR registration per country, and the CRA (vulnerability reporting from 11 Sep 2026, full application 11 Dec 2027). Pre-certified radio modules reduce but do not eliminate testing: CE does not transfer from module to product at all. The FCC requires Part 15B for the host. For the EAEU, expect a TR CU 020/2011 EMC declaration and FSB notification for Bluetooth/Wi-Fi (encryption). Lithium shipping limits postal and air options.

### Cited Findings
**Radio certification with modules**
- EU/UK: module approvals "do not transfer at all as a matter of law". The whole product must be assessed, and RED includes immunity testing. FCC/ISED: modular approval lets the host skip radio certification, but the host must still meet EMC (Part 15B unintentional radiator) and should run limited transmitter verification. Grant conditions (antenna, co-location) must be kept. — [TÜV SÜD](https://www.tuvsud.com/en-gb/resource-centre/blogs/uk/testing-and-certification-blog/regulatory-compliance-for-radio-enabled-iot-devices-using-pre-certified-radio-modules); [InCompliance EU](https://incompliancemag.com/?p=34620); [InCompliance North America](https://incompliancemag.com/?p=34568); [Nemko on FCC KDB 996369](https://www.nemko.com/hubfs/Radio%20-%20Testing%20Strategies%20and%20best%20practice%20for%20using%20approved%20modules%20per%20FCC%20KDB99369.pdf)

**RED cybersecurity (Delegated Regulation (EU) 2022/30)**
- Mandatory from 1 August 2025 (deferred by Delegated Reg. 2023/2444). It covers Art. 3(3)(d)(e)(f), with harmonised standards EN 18031-1/-2/-3:2024 (with restrictions in the OJ listing). It applies to radio equipment "directly or indirectly" internet-connectable. — [IoT Security Foundation brief](https://iotsecurityfoundation.org/wp-content/uploads/2025/09/EUROPEAN-UNION-RADIO-EQUIPMENT-DIRECTIVE-EU-RED-EXECUTIVE-BRIEF-pages-2-and-3-of-9.pdf); [BSI FAQ](https://www.bsigroup.com/siteassets/pdf/en/products-and-services/bsi_radio_equipment_directive-cybersecurity_faq_flyer_a4_241014.pdf); [Spanish ministry EN 18031 guide](https://digital.gob.es/content/dam/portal-mtdfp/avance-digital/telecomunicacion-e-infraestructuras-digitales/areas_interes/equipos-telecomunicacion/documents/Guia-aplicacion-EN-18031.pdf); [InCompliance March 2026](https://digital.incompliancemag.com/issue/march-2026/reds-cybersecurity-requirements-update-en-18031-x2024/)

**Cyber Resilience Act (Regulation (EU) 2024/2847)**
- Reporting of actively exploited vulnerabilities and severe incidents starts 11 Sep 2026 (24 h early warning, 72 h notification, final report), including for products already on the market. Full application (essential requirements, CE) is 11 Dec 2027. — [PwC Switzerland](https://www.pwc.ch/en/insights/regulation/eu-cyber-resilience-act.html); [Finite State](https://finitestate.io/blog/cyber-resilience-act-timeline-2026-2027); Espressif published ESP32-specific CRA guidance in Sep 2026 — [Espressif developer blog](https://developer.espressif.com/blog/2026/09/esp32-cra-obligations-and-deadlines/) (not read)

**Common charger (Directive (EU) 2022/2380)**
- Applies from 28 Dec 2024 (laptops from 28 Apr 2026) to listed categories: phones, tablets, digital cameras, headphones, headsets, handheld consoles, portable speakers, e-readers, keyboards, mice, portable navigation, earbuds, laptops. Secondary lists differ slightly, and Annex Ia Part I is authoritative. — [ComReg](https://www.comreg.ie/industry/product-safety/common-charger-directive-ccd/); [UL](https://www.ul.com/services/common-charger); [Commission guidance on EUR-Lex](https://eur-lex.europa.eu/eli/C/2024/2997/oj/eng)

**EU Battery Regulation (EU) 2023/1542**
- Art. 11 end-user removability and replaceability applies from 18 Feb 2027. "Readily removable" means removable with commercially available tools, without proprietary tools, heat or solvents. Exemptions (e.g., wet environments) are not automatic and must be documented, and exempt products must still allow replacement by independent professionals. — [ComplianceGate](https://www.compliancegate.com/removable-and-replaceable-battery-requirements-european-union/); [CDX system](https://public.cdxsystem.com/web/cdx/w/battery-regulation-new-guidelines-on-removability-requirements)
- On 14 Jul 2026 the Commission reportedly adopted a delegated act adding six product categories to the derogations, including smartwatches and fitness trackers. It was not yet in force at the time of reporting, and the category lists differ between reports. — [REACH24H](https://en.reach24h.com/news/industry-news/sustainability/eu-portable-battery-removability-exemptions-2026) (snippet); earlier draft analysis — [Cooley (May 2026)](https://products.cooley.com/2026/05/15/eu-batteries-regulation-european-commission-proposes-new-exemptions-from-end-user-battery-removability-requirements/); [Vienna.at](https://www.vienna.at/new-decision-by-the-eu-not-all-devices-need-removable-batteries/10325174)
- Labelling: CE marking on batteries required since 18 Aug 2024, and the crossed-out wheeled bin since 18 Aug 2025. The general label nominally from 18 Aug 2026 may slip, because the implementing act on format was overdue. The QR code is required from 18 Feb 2027. Declared capacity must come from recognised test methods (IEC 61960-3). — [CMS law](https://cms.law/en/svk/legal-updates/eu-sustainable-batteries-regulation-where-are-we-now); [Pryor Marking](https://www.pryormarking.com/?p=23818); [Label Service](https://labelservice.co.uk/?p=19069)

**GPSR (Regulation (EU) 2023/988)**
- Applies from 13 Dec 2024. Non-EU sellers need an EU-based responsible person, products need batch or serial identifiers, and online listings must show safety info. Connected products may need software and cyber risk assessed over their lifetime. A small seller selling under their own brand is treated as the manufacturer. — [ecommerceGermany GPSR guide](https://ecommercegermany.com/blog/gpsr-general-product-safety-regulation/); [Mayer Brown](https://www.mayerbrown.com/en/insights/publications/2023/06/eu-general-product-safety-regulation-entered-into-force); [HCR Law](https://hcrlaw.com/news-and-insights/the-eu-general-product-safety-regulation)

**Toy safety (if marketed to children)**
- The new Toy Safety Regulation (EU) 2025/2509 was published in the OJ on 12 Dec 2025 and entered into force 1 Jan 2026. It applies from 1 Aug 2030 (repealing 2009/48/EC) and introduces a digital product passport. Its electrical requirements cover shock, overheating and battery ingestion. ChemRadar/CIRS state 2026 for application, which looks erroneous. — [Bureau Veritas](https://www.cps.bureauveritas.com/newsroom/eu-toy-safety-regulation-20252509-published); [Eurofins](https://www.eurofins.com/toys-hardlines/resources/industry-newsletter/tech-watch-eu-adopts-new-regulation-to-strengthen-toy-safety-across-europe/)

**WEEE/battery EPR (Germany example)**
- ElektroG and the battery law require registration with Stiftung EAR, and packaging requires LUCID. Since 1 Jul 2023, marketplaces must verify sellers' EAR registration and block unregistered electrical products. Foreign sellers need an authorised representative. — [Conrad seller center](https://platform.conrad.de/en/seller/competence-center/recht_compliance/erweiterte_herstellerverantwortung.html); [Kaufland Global Marketplace](https://www.kauflandglobalmarketplace.com/en/seller-university/formal-framework/epr-guidelines/epr-general-information/); [Lappa](https://lappa.org/guides/epr/germany-epr/)

**EAEU / Russia**
- TR CU 020/2011 (EMC) requires conformity documents for listed product groups (household electrical appliances, PC-connected devices) at customs. An example 1d-scheme declaration exists for a wireless device. — [Kongsberg TR CU 020/2011 declaration](https://kongsberg.com/globalassets/maritime/documents/certificates/product/sentry-wireless-temperature-monitoring/eac-tp-tc/tp-tc-020-2011-emc-gb-200-gb-300-valid-to-27-05-2026.pdf); [Alta-Soft](https://www.alta.ru/laws_news/27959/)
- FSB notification (encryption) is required for goods with Wi-Fi, Bluetooth or GSM modules that contain encryption, under EEC Commission Decision No. 30. — [IFCG "Нотификация ФСБ"](https://www.ifcg.ru/publications/federal-security-service-notification/); [IFCG FAQ](https://www.ifcg.ru/publications/what-do-you-know-about-fsb-notification)

**Lithium battery shipping**
- Air: Section II "small" Li-ion is ≤ 20 Wh per cell and ≤ 100 Wh per battery. Since 1 Apr 2022, Section II relief is gone for batteries shipped alone (PI 965), but still usable for batteries packed with or in equipment. Section IB covers more than 8 cells or more than 2 batteries shipped alone. — [APTIM](https://www.aptim.com/?p=12991); [Labelmaster/LION on ICAO rules](https://www.lion.com/Lion-News/March-2012/ICAOs-New-Rules-for-Lithium-Battery-Air-Shipments)
- Post: the UPU allowed, from 1 Jan 2013, international mail with up to 4 cells or 2 batteries installed in equipment. Packages must be protected from damage and short circuits. Rules are set by each postal operator and may have changed since. — [UPU news 2012](https://www.upu.int/en/news/2012/5/new-mailing-rule-to-allow-transport-of-lithium-batteries); [USPS Postal Bulletin](https://about.usps.com/postal-bulletin/2012/pb22336/html/updt_010.htm); [Post&Parcel](https://postandparcel.info/?p=47790)
- Logistics providers ask for a UN38.3 test summary for batteries and equipment containing them. — [MyLionTech](https://myliontech.com/en/un38-3-mini-ups-lithium-battery-shipping/); [Sennheiser transport info](https://www.sennheiser.com/globalassets/digizuite/44219-en-transportinformation_liion-batteries_en_20210425.pdf)

### Inferences
- **Practical route for a small run:** use a pre-certified module (ESP32-C3-MINI-1, or nRF52840 modules with FCC/CE reports) and follow its antenna and keep-out rules exactly, rather than going chip-down. Chip-down needs full radio testing. Budget an accredited lab for RED (radio EN 300 328, EMC EN 301 489-1/-17, safety EN 62368-1, RF exposure EN 62479) and FCC Part 15B. Ask the lab about EN 18031 if the device pairs with a phone app that reaches the internet.
- **Common charger:** a keychain toy or companion is not one of the listed categories, so USB-C is not legally mandated. Still, USB-C plus correct CC resistors is the market expectation and future-proof. If it is listed as "earbuds-like" or a similar category, it applies.
- **Battery removability (Feb 2027):** a glued-in LiPo in a sealed shell probably does not meet "readily removable by the end user" unless an exemption applies. Design for a screw-closed shell (standard Phillips or Torx) with a connector-attached cell and a printed instruction, or confirm with a compliance consultant whether a final 2026 delegated-act category (e.g., "wearables") covers a keychain. This is the most important design-time legal decision.
- **RoHS/REACH:** require RoHS declarations from every supplier (OLED module, cell, PCB) and keep them in the technical file. Cheap AliExpress modules rarely come with them, which is another reason to move to traceable distributors.
- **Russia/EAEU:** if sold there, budget an EAC declaration (TR CU 020/2011; TR CU 004/2011 does not apply below 50 V AC / 75 V DC) and FSB notification for BLE. Many module vendors already hold notifications, so check before filing.
- **Shipping:** a 100–500 mAh cell is about 0.37–1.85 Wh, well within Section II "in equipment" limits. But Section II still needs a lithium-battery mark on packages (for more than 2 packages or more than 4 cells per consignment, as applicable) and a cell that passed UN38.3, which the cell vendor must document. Spare cells shipped alone are much harder. Many national posts (and AliExpress-style consolidators) use specific "battery lines". Check the destination postal operator's current rules.

### Gaps
- The final text and categories of the July 2026 battery-removability delegated act could not be verified (host blocked, search budget exhausted). Its entry-into-force date is unknown.
- No primary EUR-Lex text was read for any EU act; dates come from law-firm or vendor summaries (consistent across sources).
- No current (2025–2026) UPU or national postal rules on lithium in equipment were found, and no Russia Post rules.
- RoHS, UKCA and US CPSC specifics were not researched.

---

## 9. UX and product (novelty, annoyance, privacy, daylight, battery anxiety, on/off)

### Takeaway
Companion-device engagement typically drops sharply within the first two weeks. Long-term success depends on variety, appropriate proactivity, and the device not becoming annoying or creepy. A microphone on a bag-hung gadget invites the same privacy backlash AI pendants got. Battery status, charging and "off" should be expressed in character, so the device never looks broken.

### Cited Findings
- In a home study, most children stopped using a domestic social robot after about 2 weeks, or used it only every few weeks. The authors attributed this to competition with other toys and devices and low motivation, not mainly to novelty wearing off. — [University of Amsterdam (ASCoR) 2022](https://ascor.uva.nl/shared/faculteiten/en/faculteit-der-maatschappij-en-gedragswetenschappen/news/2022/07/social-robots-and-children.html)
- In a 70-household study of robots in homes for up to 6 months, evaluations dropped initially but later rose with longer use (mere-exposure effect). — [Utrecht University research portal](https://research-portal.uu.nl/en/publications/long-term-evaluation-of-a-social-robot-in-real-homes/)
- Roomba household studies found novelty effects "typically wear off after a short period of time". — [Georgia Tech (Sung/Grinter/Christensen)](https://faculty.cc.gatech.edu/~hic/hic-papers/SORO_2010.pdf). HRI literature suggests long-term studies need ≥ 2 months before participants get accustomed. — [Georgia Tech IJSR paper](https://faculty.cc.gatech.edu/~beki/j16.pdf) (snippet; exact attribution not verified)
- MIT older-adult home study (1–12 months): proactivity and movement, meeting or not meeting expectations, the robot's persona, and even feelings of guilt drive continued use. — [MIT Media Lab](https://www-prod.media.mit.edu/publications/mixed-method-long-term-robot-usage-older-adults-lived-experience-of-social-robots)
- Always-listening wearables drew public backlash. Friend pendant ads were defaced in New York ("AI is not your friend"). Critics note bystanders never consented to being recorded or analyzed. Friend claimed recordings aren't stored. — [Tom's Guide](https://tomsguide.com/ai/friend-is-a-dollar99-ai-necklace-that-wants-to-help-you-remake-the-movie-her); [Futurism](https://futurism.com/ai-necklace-listens-constantly-data-complain.md); [Spyglass](https://spyglass.org/ai-friend-wearable/); [SF Standard Dec 2025](https://sfstandard.com/pacific-standard-time/2025/12/10/wearable-ai-accessories/)
- Meta's reported always-listening AI pendant (2026) is logged as a privacy-concern incident by the OECD AI incident monitor. — [OECD.AI](https://oecd.ai/en/incidents/2026-05-31-2268); [Digital Trends](https://digitaltrends.com/wearables/after-smart-glasses-meta-apparently-wants-you-to-wear-its-all-listening-ai-pendant)
- Display luminance of about 60 cd/m² limits daylight visibility. — [DFRobot](https://wiki.dfrobot.com/DFR0486)

### Inferences
- **Novelty decay:** plan content and behaviors for weeks 2–8, not only the unboxing. Unlockable expressions, day-of-week and time-of-day moods, "memories" of interactions (counts of taps or steps), rare "easter egg" animations with low probability, and seasonal updates via BLE all help. Persistent state that grows ("it got happier because you carried it daily") leverages the mere-exposure finding.
- **Annoyance budget:** cap unsolicited reactions (e.g., at most N per hour). Don't react to every bump (see Section 4). Quiet hours at night (light sensor plus RTC). Let users pick a "calm" or "lively" personality. A face constantly reacting while the bag swings looks glitchy, not alive.
- **Privacy:** if a mic exists, process on-device only (level or clap detection, no audio leaves the device). State "no recording" on the packaging. Consider omitting the mic entirely, since it adds BOM, power, legal review (wiretap-law concerns are being raised in the US about AI listening devices — not verified here) and trust cost for little UX gain.
- **Battery anxiety and states in character:** low battery shows as a "sleepy or hungry" face before the firmware cutoff. Charging shows an "eating" or "sleeping with Zzz" animation (and doubles as burn-in-safe content). Full charge shows a happy face then display off. Critical battery shows a single icon flash, then deep sleep. Always show something on button press, even if it is only "I'm asleep, charge me", so the device never looks dead.
- **On/off:** users need a clear "off" (long-press) and a "shipping or storage" mode. Bags get stored for weeks, and flat LiPos get damaged. A true off (load switch) protects the cell.
- **Daylight:** auto-max contrast in bright light and minimal contrast in dark. Set marketing expectations honestly (shade or indoors). Film promo videos indoors and in shade with locked camera exposure (see Section 1).

### Gaps
- No studies specific to keychain or Tamagotchi-style wearable companions' retention curves were found; robot studies are a proxy.
- No sources on user tolerance for reaction frequency ("annoyance") in ambient companion devices.
- The Oct 2026 US wiretap-law discussion around AI listening devices was seen only in a search summary attributed to CNBC and is not verified.
