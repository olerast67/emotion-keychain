#!/usr/bin/env bash
# Собирает веб-симулятор: движок → face.wasm → один самодостаточный index.html.
# Нужны clang с целью wasm32 и wasm-ld (пакеты clang и lld), Python 3.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/sim/web/dist"
mkdir -p "$OUT"

CLANG="${CLANG:-clang++}"
"$CLANG" --target=wasm32 -std=c++20 -O2 -nostdlib -ffreestanding \
  -fno-exceptions -fno-rtti -fno-math-errno -fno-threadsafe-statics \
  -I"$ROOT/engine/include" \
  "$ROOT"/engine/src/*.cpp "$ROOT/sim/web/wasm_api.cpp" \
  -Wl,--no-entry -Wl,--export=__wasm_call_ctors -Wl,--strip-all \
  -o "$OUT/face.wasm"

python3 "$ROOT/tools/embed_wasm.py" "$ROOT/sim/web/index.html" "$OUT/face.wasm" "$OUT/index.html"
echo "Готово: $OUT/index.html ($(wc -c < "$OUT/face.wasm") байт wasm)"
