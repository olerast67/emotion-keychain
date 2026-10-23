"""Схема rev B в формате KiCad 7 из того же netlist.py, что и плата.

Каждый вывод подписан меткой цепи — схема читается как таблица соединений, а
KiCad видит её как обычную: открывается, печатается, сверяется с платой.

    python3 hardware/rev-b/gen/schematic.py   # → keychain-rev-b.kicad_sch и out/keychain-rev-b-schematic.pdf
"""
import os
import re
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ET

import netlist

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.normpath(os.path.join(HERE, ".."))
NAME = "keychain-rev-b"
SCH = os.path.join(REV, NAME + ".kicad_sch")
OUT = os.path.join(REV, "out")
SYMDIR = os.environ.get("KICAD7_SYMBOL_DIR", "/usr/share/kicad/symbols")

GRID = 2.54
WIRE = 2.54  # короткий провод от вывода до метки


# ---------------------------------------------------------------- S-выражения

class Q(str):
    """Строка в кавычках (в отличие от голого атома)."""


def parse(text):
    tokens = re.findall(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+', text)
    pos = 0

    def node():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            out = []
            while tokens[pos] != ")":
                out.append(node())
            pos += 1
            return out
        if tok.startswith('"'):
            return Q(tok[1:-1].replace('\\"', '"').replace("\\\\", "\\"))
        return tok

    return node()


def dump(node, indent=0):
    if isinstance(node, list):
        simple = all(not isinstance(x, list) for x in node)
        if simple:
            return "(" + " ".join(dump(x) for x in node) + ")"
        pad = "  " * (indent + 1)
        head = []
        rest = []
        for x in node:
            (rest if isinstance(x, list) or rest else head).append(x)
        return ("(" + " ".join(dump(x) for x in head) + "\n" +
                "\n".join(pad + dump(x, indent + 1) for x in rest) + ")")
    if isinstance(node, Q):
        return '"' + node.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(node, float):
        return f"{node:.4f}".rstrip("0").rstrip(".")
    return str(node)


def items(node, key):
    return [x for x in node if isinstance(x, list) and x and x[0] == key]


def item(node, key):
    found = items(node, key)
    return found[0] if found else None


# ---------------------------------------------------------------- библиотеки символов

_libs = {}


def library(name):
    if name not in _libs:
        tree = parse(open(os.path.join(SYMDIR, name + ".kicad_sym")).read())
        _libs[name] = {str(s[1]): s for s in items(tree, "symbol")}
    return _libs[name]


def flat_symbol(lib_id):
    """Определение символа без extends, с именем «Библиотека:Символ», как его хранит схема."""
    lib, name = lib_id.split(":")
    sym = library(lib)[name]
    ext = item(sym, "extends")
    if ext:
        parent = flat_symbol(f"{lib}:{ext[1]}")
        pname = str(ext[1])
        props = {str(p[1]): p for p in items(sym, "property")}
        out = [parent[0], Q(lib_id)]
        for x in parent[2:]:
            if isinstance(x, list) and x[0] == "property" and str(x[1]) in props:
                out.append(props.pop(str(x[1])))
            elif isinstance(x, list) and x[0] == "symbol":
                unit = list(x)
                unit[1] = Q(str(x[1]).replace(pname, name, 1))
                out.append(unit)
            else:
                out.append(x)
        # свойства, которых нет у родителя, — перед блоками единиц
        first_unit = next(i for i, x in enumerate(out) if isinstance(x, list) and x[0] == "symbol")
        for p in props.values():
            out.insert(first_unit, p)
        return out
    out = list(sym)
    out[1] = Q(lib_id)
    return out


def symbol_pins(flat):
    """Выводы первой единицы: номер → (x, y, угол) в координатах символа (y вверх)."""
    pins = {}
    for unit in items(flat, "symbol"):
        m = re.search(r"_(\d+)_(\d+)$", str(unit[1]))
        if not m or m.group(1) not in ("0", "1") or m.group(2) not in ("0", "1"):
            continue
        for pin in items(unit, "pin"):
            at = item(pin, "at")
            num = str(item(pin, "number")[1])
            length = float(item(pin, "length")[1])
            pins[num] = (float(at[1]), float(at[2]), int(float(at[3])) if len(at) > 3 else 0, length)
    return pins


def symbol_bbox(flat, pins):
    xs, ys = [], []
    for x, y, a, ln in pins.values():
        xs.append(x)
        ys.append(y)
    for unit in items(flat, "symbol"):
        for rect in items(unit, "rectangle"):
            for key in ("start", "end"):
                p = item(rect, key)
                xs.append(float(p[1]))
                ys.append(float(p[2]))
        for poly in items(unit, "polyline"):
            for xy in items(item(poly, "pts"), "xy"):
                xs.append(float(xy[1]))
                ys.append(float(xy[2]))
        for circ in items(unit, "circle"):
            c = item(circ, "center")
            r = float(item(circ, "radius")[1])
            xs += [float(c[1]) - r, float(c[1]) + r]
            ys += [float(c[2]) - r, float(c[2]) + r]
    return min(xs), min(ys), max(xs), max(ys)


# ---------------------------------------------------------------- сборка листа

def uid():
    return Q(str(uuid.uuid4()))


def snap(v):
    return round(v / GRID) * GRID


def font(size=1.27, hide=False, justify=None):
    eff = ["effects", ["font", ["size", size, size]]]
    if justify:
        eff.append(["justify"] + justify.split())
    if hide:
        eff.append("hide")
    return eff


# Где на листе A3 (420 × 297 мм) стоит каждая группа: x, y, ширина ряда.
REGIONS = {
    "power": (15, 25, 200, "Питание: USB-C, зарядка Li-Po, разделение питания"),
    "mcu": (225, 25, 185, "nRF52840 (модуль Raytac MDBT50Q) и программатор"),
    "display": (15, 135, 200, "Экран: ключ питания и выбор порядка выводов"),
    "feedback": (225, 150, 185, "Пищалка, вибромотор, кнопка"),
    "sensors": (15, 185, 395, "Датчики: акселерометр, свет, микрофон"),
    "debug": (225, 236, 70, "Контрольные точки"),
}

NOTES = [
    "Брелок-компаньон Olerast, плата rev B. Сгенерировано из hardware/rev-b/gen/netlist.py — правки только там.",
    "Заряд: MCP73832, 100 мА (R3 = 10 кОм). Светодиод горит, пока идёт заряд.",
    "Питание системы VSYS: от USB через D1, от батареи через Q1. VDDH модуля = VSYS, VDD = 3.3 В от его REG0 (UICR.REGOUT0).",
    "Порядок питания экрана: R16+R18 — «GND VCC SCL SDA» (по умолчанию), R17+R19 — «VCC GND SCL SDA».",
    "Экран выключается ключом Q2: на время сна выводы DISP_* — в отключённый вход, чтобы не питать модуль через подтяжки.",
]


def build():
    parts = netlist.parts()
    root = uid()
    sch = ["kicad_sch", ["version", "20230121"], ["generator", "eeschema"], ["uuid", root], ["paper", Q("A3")],
           ["title_block", ["title", Q("Брелок-компаньон Olerast")], ["date", Q("2026-10-23")], ["rev", Q("B")],
            ["company", Q("Olerast — olerast.com")],
            ["comment", 1, Q("nRF52840 · SSD1306 · LIS3DH · LTR-303 · PDM-микрофон · Li-Po")]]]
    lib_symbols = ["lib_symbols"]
    used = {}
    body = []

    # группы по порядку, внутри — слева направо рядами
    cursor = {g: [REGIONS[g][0], REGIONS[g][1] + 12, 0.0] for g in REGIONS}
    for g, (x0, y0, w, title) in REGIONS.items():
        body.append(["text", Q(title), ["at", float(x0), float(y0), 0], font(2.0, justify="left bottom"), ["uuid", uid()]])

    connected = 0
    for p in parts:
        lib_id = p["symbol"]
        if lib_id not in used:
            used[lib_id] = flat_symbol(lib_id)
            lib_symbols.append(used[lib_id])
        flat = used[lib_id]
        pins = symbol_pins(flat)
        bx0, by0, bx1, by1 = symbol_bbox(flat, pins)
        # поля под метки: слева/справа — по длине самой длинной метки
        def label_room(side):
            names = [p["pins"].get(n, "") for n, (x, y, a, ln) in pins.items() if (a == 0 and side < 0) or (a == 180 and side > 0)]
            return (max((len(s) for s in names), default=0) * 0.9 + WIRE + 1.5) if names else 1.0

        def label_room_v(side):
            names = [p["pins"].get(n, "") for n, (x, y, a, ln) in pins.items() if (a == 270 and side > 0) or (a == 90 and side < 0)]
            return (max((len(s) for s in names), default=0) * 0.9 + WIRE + 1.0) if names else 1.0

        left, right = label_room(-1), label_room(1)
        top, bottom = label_room_v(1), label_room_v(-1)
        width = (bx1 - bx0) + left + right + 6
        height = (by1 - by0) + top + bottom + 6
        g = p["group"]
        x0, y0, w, _ = REGIONS[g]
        cx, cy, row_h = cursor[g]
        if cx + width > x0 + w and cx > x0:
            cx, cy, row_h = x0, cy + row_h, 0.0
        sx = snap(cx + left - bx0)
        sy = snap(cy + top + by1)
        cursor[g] = [cx + width, cy, max(row_h, height)]

        sym = ["symbol", ["lib_id", Q(lib_id)], ["at", sx, sy, 0], ["unit", 1], ["in_bom", "yes"],
               ["on_board", "yes"], ["dnp", "yes" if p["dnp"] else "no"], ["uuid", uid()]]
        ref_at = (sx + bx1 + 1.0, sy - by1 + 1.0) if (bx1 - bx0) < 8 else (sx + bx0, sy - by1 - 1.5)
        val_at = (ref_at[0], ref_at[1] + 2.0) if (bx1 - bx0) < 8 else (sx + bx0, sy - by0 + 2.5)
        props = [("Reference", p["ref"], ref_at, False), ("Value", p["value"], val_at, False),
                 ("Footprint", p["footprint"], (sx, sy), True), ("Datasheet", "~", (sx, sy), True)]
        if p["lcsc"]:
            props.append(("LCSC", p["lcsc"], (sx, sy), True))
        if p["mpn"]:
            props.append(("MPN", p["mpn"], (sx, sy), True))
        for name, value, at, hide in props:
            sym.append(["property", Q(name), Q(value), ["at", float(at[0]), float(at[1]), 0],
                        font(1.27, hide=hide, justify=None if hide else "left")])
        for num in pins:
            sym.append(["pin", Q(num), ["uuid", uid()]])
        sym.append(["instances", ["project", Q(NAME), ["path", Q("/" + root), ["reference", Q(p["ref"])], ["unit", 1]]]])
        body.append(sym)

        # метки цепей на каждом выводе, неподключённые — крестиком
        for num, (px, py, ang, ln) in pins.items():
            ex, ey = sx + px, sy - py  # конец вывода на листе (y вниз)
            net = p["pins"].get(num)
            if not net:
                body.append(["no_connect", ["at", ex, ey], ["uuid", uid()]])
                continue
            connected += 1
            dx, dy = {0: (-1, 0), 180: (1, 0), 90: (0, 1), 270: (0, -1)}[ang]
            wx, wy = ex + dx * WIRE, ey + dy * WIRE
            body.append(["wire", ["pts", ["xy", ex, ey], ["xy", wx, wy]],
                         ["stroke", ["width", 0], ["type", "default"]], ["uuid", uid()]])
            langle, just = {(1, 0): (0, "left bottom"), (-1, 0): (180, "right bottom"),
                            (0, -1): (90, "left bottom"), (0, 1): (270, "right bottom")}[(dx, dy)]
            body.append(["label", Q(net), ["at", wx, wy, langle], font(1.27, justify=just), ["uuid", uid()]])

    y = 250
    for line in NOTES:
        body.append(["text", Q(line), ["at", 20.0, float(y), 0], font(1.5, justify="left bottom"), ["uuid", uid()]])
        y += 5
    sch.append(lib_symbols)
    sch += body
    sch.append(["sheet_instances", ["path", Q("/"), ["page", Q("1")]]])
    with open(SCH, "w") as f:
        f.write(dump(sch) + "\n")
    return connected


def verify():
    """Сверяет соединения схемы (экспорт netlist из KiCad) с netlist.py."""
    xml = os.path.join(OUT, "work", NAME + "-sch.xml")
    os.makedirs(os.path.dirname(xml), exist_ok=True)
    subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadxml", "-o", xml, SCH],
                   check=True, capture_output=True)
    got = {}
    for net in ET.parse(xml).getroot().iter("net"):
        nodes = {(n.get("ref"), n.get("pin")) for n in net.iter("node")}
        name = net.get("name").lstrip("/")
        if len(nodes) > 1 or not name.startswith("unconnected"):
            got[name] = nodes
    want = {}
    for p in netlist.parts():
        for pin, net in p["pins"].items():
            want.setdefault(net, set()).add((p["ref"], pin))
    bad = 0
    for net, nodes in want.items():
        if got.get(net) != nodes:
            print("расхождение в цепи", net, "схема:", sorted(got.get(net, [])), "netlist:", sorted(nodes))
            bad += 1
    print(f"схема: {len(want)} цепей сверено, расхождений {bad}")
    return bad


def export_pdf():
    os.makedirs(OUT, exist_ok=True)
    pdf = os.path.join(OUT, NAME + "-schematic.pdf")
    subprocess.run(["kicad-cli", "sch", "export", "pdf", "-o", pdf, SCH], check=True, capture_output=True)
    return pdf


if __name__ == "__main__":
    n = build()
    print("выводов с метками:", n)
    bad = verify()
    print("PDF:", os.path.relpath(export_pdf(), REV))
    sys.exit(1 if bad else 0)
