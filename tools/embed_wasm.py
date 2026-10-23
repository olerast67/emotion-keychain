#!/usr/bin/env python3
"""Встраивает face.wasm в HTML-шаблон (base64 вместо маркера), чтобы страница
открывалась одним файлом без сервера.

    embed_wasm.py template.html face.wasm out.html [--standalone]

Шаблон — это содержимое страницы без <html>/<head>/<body> (так его принимает
публикация артефактов). С --standalone добавляется полноценный каркас документа,
чтобы файл открывался в браузере напрямую."""
import base64
import sys

MARKER = "__FACE_WASM_BASE64__"


def main() -> int:
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    standalone = "--standalone" in sys.argv
    if len(args) != 3:
        print(__doc__)
        return 2
    template, wasm, out = args
    with open(template, encoding="utf-8") as f:
        html = f.read()
    if MARKER not in html:
        print(f"В шаблоне нет маркера {MARKER}")
        return 1
    with open(wasm, "rb") as f:
        b64 = base64.b64encode(f.read()).decode("ascii")
    html = html.replace(MARKER, b64)
    if standalone:
        html = (
            '<!doctype html>\n<html lang="ru">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n'
            "</head>\n<body>\n" + html + "\n</body>\n</html>\n"
        )
    with open(out, "w", encoding="utf-8") as f:
        f.write(html)
    return 0


if __name__ == "__main__":
    sys.exit(main())
