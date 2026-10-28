English | [Русский](README.md)

# Rev B board bootloader

The prebuilt file `emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex` is the [Adafruit nRF52 bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader) 0.11.0 (commit `c67f0bc`) with a small patch, together with SoftDevice S140 6.1.1. It is written once with a programmer, as described in the [firmware README](../README.en.md#first-flash-bootloader-over-swd). Usually it is not written separately: it is already inside `keychain-revb-factory.hex` ([prebuilt files](../release/README.en.md)).

How it differs from the bootloaders of off-the-shelf boards (`emotion_keychain_revb/board.h`):

- **Bootloader button** — the keychain button, P0.22. The bootloader looks at it only at reset: if it is held while on USB, the bootloader waits for firmware and shows the KEYCHAIN drive. A reset with the button held on USB happens if you hold the button for more than 10 s on a keychain that is switched off while charging, or if the firmware hung and the watchdog reset it. On a running keychain the same gesture, "hold the button and plug in USB", is caught by the firmware itself, which jumps to the bootloader with a software reset ([firmware README](../README.en.md#bootloader-and-usb-updates)).
- **The `button-dfu-needs-usb.patch` patch.** The same button wakes the keychain from deep sleep, and waking up is a reset, with the button still pressed at that moment. Without the patch the bootloader would enter firmware-update mode with no timeout, and a keychain on battery would hang with a dark screen. With the patch the button only works when powered from USB. If the computer does not respond within 10 s, e.g. the keychain is charging from a wall adapter, the firmware starts. If the computer has found the drive, the bootloader waits until USB is unplugged and then starts the firmware.
- **REG0 = 3.3 V** is written to UICR on the bootloader's first start (followed by an immediate reboot): the display and sensors already work in the bootloader. `keychain-revb-factory.hex` already contains this value, so there is no extra reboot after the programmer.
- **No charging.** The low end of R3 (PROG) goes to pin P0.24 (CHG_EN), and only the firmware turns it on, after checking the temperature. In the bootloader, and on a board without firmware, the charger is off; the keychain is powered from USB meanwhile.
- **No LED:** the LED pin is the unused pin P0.31.
- **No 32 kHz crystal needed:** the Adafruit bootloader always runs from the internal RC.
- **USB VID/PID** are taken from the Raytac MDBT50Q-RX board (from the Adafruit set): bootloader 239A:010B, firmware 239A:810B (the `[sale]` section in `firmware/platformio.ini`; `boards/keychain_revb.json` only has defaults). That is fine for your own prototypes. Before selling you need your own IDs: in the bootloader these are `USB_DESC_VID`, `USB_DESC_UF2_PID` and `USB_DESC_CDC_ONLY_PID` in `emotion_keychain_revb/board.h`, followed by a rebuild using the steps below. The board does not change for this.

## How to rebuild

You need `arm-none-eabi-gcc`, `make`, Python and git. Run the commands from the repository root: the first one changes to the bootloader folder, and all paths after that are relative to it.

The bootloader USB VID/PID (`USB_DESC_VID`, `USB_DESC_UF2_PID`, `USB_DESC_CDC_ONLY_PID` in `emotion_keychain_revb/board.h`) must match `usb_vid` and `usb_pid_boot` in the `[sale]` section of `firmware/platformio.ini`. The firmware does not change the bootloader's IDs: after editing, the bootloader is rebuilt and written with a programmer.

```sh
cd firmware/bootloader
git clone --recurse-submodules https://github.com/adafruit/Adafruit_nRF52_Bootloader.git
cp -r emotion_keychain_revb Adafruit_nRF52_Bootloader/src/boards/
cd Adafruit_nRF52_Bootloader
git checkout c67f0bc
git submodule update --init --recursive   # the submodules c67f0bc had, not those of the latest master
git apply ../button-dfu-needs-usb.patch
make BOARD=emotion_keychain_revb all
# output file: _build/build-emotion_keychain_revb/emotion_keychain_revb_bootloader-*_s140_6.1.1.hex
```

- **Do not commit the `Adafruit_nRF52_Bootloader/` folder:** it is someone else's repository, and the clone is only needed for the build. You can delete it afterwards.
- **New bootloader into the factory file.** `tools/uf2.py` assembles `keychain-revb-factory.hex` from the file named `emotion_keychain_revb_bootloader-0.11.0-keychain_s140_6.1.1.hex` in this folder (`BOOTLOADER_HEX` in `tools/uf2.py`). Replace it with the new file under the same name. Commit the changes (`board.h`, the new `.hex`, the `[sale]` section) and add a tag (e.g. `git tag sale-1.0`), otherwise the firmware version will be `-dirty`. Then rebuild the firmware: `cd firmware && pio run -e keychain`. The new `firmware-factory.hex` in `firmware/.pio/build/keychain/` already contains the new bootloader.
- **Keychains that are already flashed** get the new bootloader with the same SWD programmer (J3 on the back of the board), as in the [first flash](../README.en.md#first-flash-bootloader-over-swd). Erasing before writing also erases the log: the keychain's growth, acquaintances and settings will be lost.
