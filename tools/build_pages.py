#!/usr/bin/env python3
"""Собирает сайт GitHub Pages: веб-симулятор в корне.

    build_pages.py --sim sim/web/dist/index.html --out site/

Прошивка стенда из браузера — на olerast.com/flash, здесь её нет."""
import argparse
import pathlib
import shutil
import sys


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--sim", required=True, help="собранный sim/web/dist/index.html")
    ap.add_argument("--out", required=True, help="папка сайта")
    a = ap.parse_args()

    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(a.sim, out / "index.html")
    (out / ".nojekyll").write_text("")
    print(f"Готово: {out} (симулятор)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
