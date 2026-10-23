#!/usr/bin/env python3
"""Встраивает face.wasm в HTML-шаблон (base64 вместо маркера), чтобы страница
открывалась одним файлом без сервера: embed_wasm.py template.html face.wasm out.html"""
import base64
import sys

MARKER = "__FACE_WASM_BASE64__"


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    template, wasm, out = sys.argv[1:]
    with open(template, encoding="utf-8") as f:
        html = f.read()
    if MARKER not in html:
        print(f"В шаблоне нет маркера {MARKER}")
        return 1
    with open(wasm, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    with open(out, "w", encoding="utf-8") as f:
        f.write(html.replace(MARKER, b64))
    return 0


if __name__ == "__main__":
    sys.exit(main())
