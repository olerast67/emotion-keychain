English | [Русский](README.md)

# Prebuilt firmware files

> **These files are for prototypes:** they carry Adafruit USB IDs (239A:810B for the firmware, 239A:010B for the bootloader), i.e. the `[sale]` section of `firmware/platformio.ini` as it is in the repository. Keychains for sale are flashed with your own build that has your own IDs in `[sale]` — both at assembly and when updating keychains already sold (your own `firmware.uf2`, not `keychain-revb.uf2`).

This folder holds prebuilt firmware for the keychain (rev B board) and for the ESP32 stand. All of it is built from one commit; there is nothing to build. Other places to get the firmware are listed in the [firmware README](../README.en.md#where-to-get-the-files).

| | |
|---|---|
| Version (`git describe`) | `d7b6eee` |
| Source commit | `d7b6eeeb37effdea758321ccc8af845f8489d534` |

The keychain reports the same version for the `ver` command ([how to check](#which-firmware-is-on-the-keychain)).

## Files

| File | Contents | Where and how |
|---|---|---|
| `keychain-revb-factory.hex` | Everything at once: Adafruit bootloader with SoftDevice S140, keychain firmware, the "firmware is valid" mark for the bootloader, and UICR.REGOUT0 = 3.3 V | With a programmer over SWD onto a new board ([first flash](../README.en.md#first-flash-bootloader-over-swd)) or to the factory for "Firmware loading". Prototypes only: keychains for sale get `firmware-factory.hex` from your own build. After writing, the keychain starts the firmware right away: a face if the display is already fitted; without a display, check with `ver` and `sensors` in the console |
| `keychain-revb.uf2` | Keychain firmware only | Drag onto the KEYCHAIN drive ([USB update](../README.en.md#bootloader-and-usb-updates)). The bootloader must already be on the board. Keychains already sold get not this file but `firmware.uf2` from your own sale build |
| `keychain-revb-dfu.zip` | Keychain firmware only, DFU package | Via `adafruit-nrfutil` over USB while the keychain is in the bootloader. Only needed if the drive will not do, e.g. for your own script |
| `stand-esp32c5.bin` | Stand for ESP32-C5-DevKitC-1 | Browser flasher, at address `0x0` ([stand](../README.en.md#from-the-browser-nothing-to-install)) |
| `stand-esp32dev.bin` | Stand for a DevKit with ESP-WROOM-32 | same |
| `stand-esp32c3.bin` | Stand for ESP32-C3 SuperMini or DevKitM | same |
| `stand-esp32s3.bin` | Stand for ESP32-S3 DevKitC | same |
| `SHA256SUMS` | File checksums: SHA-256 sum and file name, one line per file | [Verification](#verifying-the-files) |
| `README.md` | This file (Russian; English: `README.en.md`) | |

The bootloader without firmware is kept separately in [`../bootloader/`](../bootloader/README.en.md). You normally do not need it: it is already inside `keychain-revb-factory.hex`.

The rev B keychain is flashed only with `keychain-revb-*` files. Stand files do not work on the keychain, and vice versa. The stand does not print its version: it is the same as at the top of this file.

The DFU package, if you need it: put the keychain into the bootloader (button + USB, or the `dfu` command), then

```sh
pip install adafruit-nrfutil
adafruit-nrfutil --verbose dfu serial --package keychain-revb-dfu.zip -p <bootloader port> -b 115200 --singlebank
```

The bootloader port is `/dev/ttyACM0` on Linux, `/dev/cu.usbmodem…` on macOS, `COM…` on Windows; in the bootloader its number may differ from that of the running keychain.

## Verifying the files

Check the sums before flashing, especially if you downloaded the files one by one in a browser. If a sum does not match, the file is corrupted or it is the wrong file: download it again.

On Linux:

```sh
cd firmware/release
sha256sum -c SHA256SUMS
```

On macOS:

```sh
cd firmware/release
shasum -a 256 -c SHA256SUMS
```

Every file should show `OK`. If the command says a file is missing and you did not download it, that is not an error.

On Windows (PowerShell, in the folder with the files):

```powershell
Get-FileHash keychain-revb-factory.hex -Algorithm SHA256
```

Compare the 64 characters with the first column of that file's line in `SHA256SUMS`. Letter case does not matter.

Git delivers the files in this folder byte for byte without changing line endings, even on Windows with `core.autocrlf` (this is set in `.gitattributes`). So a file from a clone must match too. If it does not, the file is corrupted or from a different version: download it again (on GitHub, the "Download raw file" button) and check once more. If it still does not match, do not flash it.

## Which firmware is on the keychain

The keychain console is USB, 115200 baud. Connect the keychain with a cable that has data lines (a "charge-only" cable will not give you a port) and open the port in any terminal at 115200:

- **Arduino IDE:** Tools → Port — the keychain's port, then Tools → Serial Monitor; at the right of the monitor window choose 115200 and any line ending except "No Line Ending".
- **PlatformIO:** `pio device monitor -b 115200` from the `firmware/` folder; exit with Ctrl+C.
- **Windows:** PuTTY, connection type Serial, port `COM…` (the number is in Device Manager under "Ports"), speed 115200.
- **Linux and macOS:** `screen /dev/ttyACM0 115200` (on macOS `/dev/cu.usbmodem…`); exit with Ctrl+A, then K and Y.

Type a command and press Enter.

- **The `ver` command** works at any time. It replies `Прошивка <версия>, плата rev B` ("Firmware <version>, board rev B").
- **`sensors`** — the sensors: the first line is `Ускорение: x …` ("Acceleration: x …") or «Ускорение: нет новых выборок» ("Acceleration: no new samples"; repeat the command), but not «Акселерометр не отвечает» ("Accelerometer not responding"); the second starts with `Свет: есть` ("Light: present"), not `Свет: нет ответа` ("Light: no response").
- **Power-on greeting:** `Брелок с эмоциями, плата rev B, прошивка <версия>. Экран есть, акселерометр есть, свет есть` ("Emotion keychain, board rev B, firmware <version>. Display present, accelerometer present, light present"). It is printed once, right after power-on, and usually never reaches the terminal: the port appears when the keychain has already started. Not seeing the greeting is not an error; check with `ver` and `sensors`.

The version must match the "Version" line at the top of this file. For a keychain for sale, it must match the tag of your sale build. What the rest means:

- **`-dirty` at the end** — the file was built from sources with uncommitted changes. Such firmware is not put on keychains for sale.
- **`unknown`** — the file was built without git, e.g. from a source archive. There is no way to tell which version it is.
- **The keychain answers `ver` with «Не знаю такую команду»** ("Unknown command") — the firmware is old, built before the version was added. Flash a new one. Firmware older than commit `098681e` on the final rev B board also does not charge the cell (it does not enable CHG_EN on P0.24) and does not see the accelerometer (it looks for it at address 0x18, while on the board it is 0x19): its `sensors` prints «Акселерометр не отвечает».
