#!/usr/bin/env python3
"""Собирает сайт GitHub Pages: веб-симулятор в корне и прошивальщик стенда в flash/.

    build_pages.py --sim sim/web/dist/index.html --bins out/ --out site/ [--version abc1234]

В flash/ кладутся страница sim/flash/index.html, файлы stand-<среда>.bin из --bins и
manifest.json для ESP Web Tools: по сборке на каждую найденную плату, тип чипа
определяется при подключении."""
import argparse
import json
import pathlib
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

# Среда PlatformIO → chipFamily в манифесте ESP Web Tools (как CHIP_NAME у esptool-js).
CHIPS = {
    "esp32c5": "ESP32-C5",
    "esp32dev": "ESP32",
    "esp32c3": "ESP32-C3",
    "esp32s3": "ESP32-S3",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim", required=True, help="собранный sim/web/dist/index.html")
    ap.add_argument("--bins", required=True, help="папка с stand-<среда>.bin")
    ap.add_argument("--out", required=True, help="папка сайта")
    ap.add_argument("--version", default="dev", help="версия прошивки для манифеста")
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    flash = out / "flash"
    flash.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(a.sim, out / "index.html")
    shutil.copyfile(ROOT / "sim/flash/index.html", flash / "index.html")
    (out / ".nojekyll").write_text("")

    builds = []
    for env, chip in CHIPS.items():
        src = pathlib.Path(a.bins) / f"stand-{env}.bin"
        if not src.is_file():
            print(f"нет {src}: {chip} в прошивальщике не будет", file=sys.stderr)
            continue
        shutil.copyfile(src, flash / src.name)
        builds.append({"chipFamily": chip, "parts": [{"path": src.name, "offset": 0}]})
    if not builds:
        print("ни одного stand-*.bin: прошивальщику нечего прошивать", file=sys.stderr)
        return 1
    manifest = {
        "name": "Emotion keychain ESP32 stand",
        "version": a.version,
        "new_install_prompt_erase": True,
        "builds": builds,
    }
    (flash / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Готово: {out} (симулятор, прошивальщик: {', '.join(b['chipFamily'] for b in builds)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
