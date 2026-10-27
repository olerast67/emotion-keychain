"""Корпус-«бутерброд» для платы rev B: пластины акрила под лазерную резку.

Слои считаются из той же платы (keychain-rev-b.kicad_pcb): вырезы рамок обходят
дворы деталей, отверстия стоят на отверстиях модуля экрана. Пишет в out/:

- по SVG и DXF на каждый лист (толщина акрила), детали разложены рядом;
- case-model.json — слои с высотами для 3D-просмотра (viewer/).

Стек от лица к обороту (z — от лицевой стороны платы, мм):

  крышка 2 мм      8.2 … 10.2   прозрачная, винты, отверстие микрофона
  шайбы 2 мм       6.2 … 8.2    между крышкой и модулем экрана
  рамка 3 мм       5 … 8        стенки вокруг модуля экрана
  рамка 5 мм       0 … 5        стенки вокруг ячейки и деталей лица
  плата 1.6 мм     −1.6 … 0
  рамка 4 мм       −5.6 … −1.6  стенки вокруг деталей оборота, толкатель кнопки
  шайбы 4 мм       там же       под винтами
  дно 2 мм         −7.6 … −5.6  прозрачное, вырез под вилку USB-C

Это версия S (ячейка 402025). Версии M и L (502025, 602025) поднимают всё над рамкой
ячейки на 1 и 2 мм: проставки M2 × 6 и × 7, рамка 6 мм или 4 + 3 мм (VARIANTS).
Модуль экрана стоит на гладких проставках (не резьбовых), всё стягивают
четыре винта M2 с гайками на обороте.
"""
import json
import math
import os
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(REV, "gen"))
import footprints  # noqa: E402
import netlist  # noqa: E402

PCB = os.path.join(REV, "keychain-rev-b.kicad_pcb")
OUT = os.path.join(HERE, "out")

W, H, R = netlist.BOARD_W, netlist.BOARD_H, 2.0
WALL = 2.5        # стенка корпуса за краем платы
INSET = 1.0       # на сколько рамки заходят на край платы
GAP = 0.3         # зазор рамок вокруг дворов деталей
SCREW_D = 2.3     # отверстие под M2
WASHER_D = 4.2
MIC_D = 1.2       # отверстие для звука над микрофоном
TAB_NOTCH = 5.5   # полуширина выреза стенки под ушко платы
USB_NOTCH = 6.6   # полуширина выреза под вилку USB-C (корпус вилки ~12.4 мм)
BTN_Y = 25.4      # ось кнопки на правом торце

# Три версии под три ячейки 20 × 25 мм: плата, прошивка и раскрой платы те же, меняются
# только проставки под экраном и рамка вокруг ячейки. Под модулем экрана свободно примерно
# проставка − 1.1 мм: ячейка с плёнкой должна быть не толще.
VARIANTS = {
    "s": {"cell": "402025", "mah": 150, "standoff": 5.0, "mid": [5.0], "screw": 20},
    "m": {"cell": "502025", "mah": 200, "standoff": 6.0, "mid": [6.0], "screw": 22},
    "l": {"cell": "602025", "mah": 250, "standoff": 7.0, "mid": [4.0, 3.0], "screw": 22},
}


def sheets_for(v):
    """Листы: имя, толщина, z нижней грани, цвет и прозрачность для просмотра; и из какого
    листа акрила режется каждая деталь."""
    h = VARIANTS[v]["standoff"]
    sheets = {
        "cover": {"t": 2.0, "z": h + 3.2, "name": "крышка", "color": "#dff3ff", "opacity": 0.28},
        "washer_front": {"t": 2.0, "z": h + 1.2, "name": "шайбы 2 мм", "color": "#dff3ff", "opacity": 0.5},
        "frame_top": {"t": 3.0, "z": h, "name": "рамка 3 мм", "color": "#ff7a3d", "opacity": 0.55},
        "frame_back": {"t": 4.0, "z": -5.6, "name": "рамка 4 мм", "color": "#ff7a3d", "opacity": 0.55},
        "washer_back": {"t": 4.0, "z": -5.6, "name": "шайбы 4 мм", "color": "#ff7a3d", "opacity": 0.7},
        "button": {"t": 4.0, "z": -5.6, "name": "толкатель", "color": "#ffd23d", "opacity": 0.9},
        "bottom": {"t": 2.0, "z": -7.6, "name": "дно", "color": "#dff3ff", "opacity": 0.28},
    }
    # Рамка вокруг ячейки — одним листом или стопкой из двух.
    z = 0.0
    for i, t in enumerate(VARIANTS[v]["mid"]):
        key = "frame_mid" if i == 0 else f"frame_mid{i + 1}"
        sheets[key] = {"t": t, "z": z, "name": f"рамка {t:g} мм", "color": "#ff7a3d", "opacity": 0.55}
        z += t
    return sheets


def MM(v):
    return pcbnew.FromMM(v)


def poly(pts):
    s = pcbnew.SHAPE_POLY_SET()
    s.NewOutline()
    for x, y in pts:
        s.Append(MM(x), MM(y))
    return s


def rect(x0, y0, x1, y1):
    return poly([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])


def rounded(x0, y0, x1, y1, r, n=10):
    pts = []
    for cx, cy, a0 in ((x1 - r, y0 + r, -90), (x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180)):
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return poly(pts)


def union(*sets):
    out = pcbnew.SHAPE_POLY_SET()
    for s in sets:
        out.BooleanAdd(s, pcbnew.SHAPE_POLY_SET.PM_FAST)
    return out


def minus(a, *bs):
    out = pcbnew.SHAPE_POLY_SET(a)
    for b in bs:
        out.BooleanSubtract(b, pcbnew.SHAPE_POLY_SET.PM_FAST)
    return out


def inflated(s, d):
    out = pcbnew.SHAPE_POLY_SET(s)
    out.Inflate(MM(d), 12)
    return out


def outlines(s):
    """[[внешний контур, отверстие, …], …] в мм."""
    res = []
    s = pcbnew.SHAPE_POLY_SET(s)
    for i in range(s.OutlineCount()):
        rings = [s.COutline(i)] + [s.CHole(i, j) for j in range(s.HoleCount(i))]
        part = []
        for ring in rings:
            part.append([(round(pcbnew.ToMM(ring.CPoint(k).x), 3), round(pcbnew.ToMM(ring.CPoint(k).y), 3))
                         for k in range(ring.PointCount())])
        res.append(part)
    return res


def board_data():
    board = pcbnew.LoadBoard(PCB)
    front, back = [], []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref in ("H1",):
            continue
        layer = pcbnew.B_CrtYd if fp.IsFlipped() else pcbnew.F_CrtYd
        c = fp.GetCourtyard(layer)
        if not c.OutlineCount():
            continue
        bb = c.BBox()
        box = (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))
        (back if fp.IsFlipped() else front).append((ref, box))
    j2 = board.FindFootprintByReference("J2")
    screws = sorted((round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3))
                    for p in j2.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH)
    u6 = board.FindFootprintByReference("U6")
    mic = next((pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)) for p in u6.Pads()
               if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH)
    return front, back, screws, mic


def build():
    front, back, screws, mic = board_data()
    body = rounded(0, 0, W, H, R)
    outer = rounded(-WALL, -WALL, W + WALL, H + WALL, R + WALL)
    tab_notch = rect(W / 2 - TAB_NOTCH, -WALL - 1, W / 2 + TAB_NOTCH, 0)
    outer = minus(outer, tab_notch)
    inner_body = inflated(body, -INSET)

    # Ячейка под модулем экрана: 20 × 25 по центру BT1.
    side, bx, by, _ = netlist.PLACE["BT1"]
    cell = rect(bx - footprints.CELL["w"] / 2, by - footprints.CELL["h"] / 2,
                bx + footprints.CELL["w"] / 2, by + footprints.CELL["h"] / 2)
    f_cut = union(inner_body, inflated(cell, GAP), *[inflated(rect(*b), GAP) for _, b in front])

    sw = dict(back)["SW1"]
    btn_pocket = rect(sw[0] - GAP, BTN_Y - 2.8, sw[2] + 0.9, BTN_Y + 2.8)
    btn_slot = rect(sw[2] + 0.8, BTN_Y - 1.3, W + WALL + 1, BTN_Y + 1.3)
    usb = rect(W / 2 - USB_NOTCH, H - 0.2, W / 2 + USB_NOTCH, H + WALL + 1)
    b_cut = union(inner_body, btn_pocket, btn_slot, usb, *[inflated(rect(*b), GAP) for _, b in back])

    holes = [(x, y, SCREW_D) for x, y in screws]
    parts = {
        "cover": {"shape": outer, "circles": holes + [(mic[0], mic[1], MIC_D)]},
        "frame_top": {"shape": minus(outer, f_cut), "circles": []},
        "frame_mid": {"shape": minus(outer, f_cut), "circles": []},
        "frame_mid2": {"shape": minus(outer, f_cut), "circles": []},
        "frame_back": {"shape": minus(outer, b_cut), "circles": []},
        "bottom": {"shape": minus(outer, rect(W / 2 - USB_NOTCH, H + 0.3, W / 2 + USB_NOTCH, H + WALL + 1)),
                   "circles": holes},
    }
    # Шайбы под винтами: по одной на винт, на лице и на обороте.
    washers = []
    for x, y in screws:
        c = [(x + WASHER_D / 2 * math.cos(2 * math.pi * k / 32), y + WASHER_D / 2 * math.sin(2 * math.pi * k / 32))
             for k in range(32)]
        washers.append((poly(c), (x, y, SCREW_D)))
    parts["washer_front"] = {"shape": union(*[w for w, _ in washers]), "circles": [h for _, h in washers]}
    parts["washer_back"] = {"shape": union(*[w for w, _ in washers]), "circles": [h for _, h in washers]}
    # Толкатель кнопки: полка внутри кармана держит его, стержень выходит из стенки на 0.8 мм.
    x0 = sw[2] + 0.05
    plunger = union(rect(x0, BTN_Y - 2.4, x0 + 0.8, BTN_Y + 2.4),
                    rect(x0 + 0.8, BTN_Y - 1.1, W + WALL + 0.8, BTN_Y + 1.1))
    parts["button"] = {"shape": plunger, "circles": []}
    return parts


# ---------------------------------------------------------------- вывод

def write_svg(path, items, w, h, title):
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{h:.2f}mm" '
           f'viewBox="0 0 {w:.3f} {h:.3f}">', f"<title>{title}</title>",
           '<g fill="none" stroke="#ff0000" stroke-width="0.05">']
    for rings, circles, dx, dy in items:
        for part in rings:
            for ring in part:
                d = " ".join(f"{x + dx:.3f},{y + dy:.3f}" for x, y in ring)
                out.append(f'<polygon points="{d}"/>')
        for x, y, dia in circles:
            out.append(f'<circle cx="{x + dx:.3f}" cy="{y + dy:.3f}" r="{dia / 2:.3f}"/>')
    out.append("</g></svg>")
    with open(path, "w") as f:
        f.write("\n".join(out))


def write_dxf(path, items):
    """DXF R12: контуры — LINE, отверстия — CIRCLE. Y перевёрнут (в DXF ось вверх)."""
    lines = ["0", "SECTION", "2", "ENTITIES"]
    for rings, circles, dx, dy in items:
        for part in rings:
            for ring in part:
                for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
                    lines += ["0", "LINE", "8", "CUT", "10", f"{x0 + dx:.4f}", "20", f"{-(y0 + dy):.4f}",
                              "11", f"{x1 + dx:.4f}", "21", f"{-(y1 + dy):.4f}"]
        for x, y, dia in circles:
            lines += ["0", "CIRCLE", "8", "CUT", "10", f"{x + dx:.4f}", "20", f"{-(y + dy):.4f}", "40", f"{dia / 2:.4f}"]
    lines += ["0", "ENDSEC", "0", "EOF"]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def write_variant(v, parts):
    out = OUT if v == "s" else os.path.join(OUT, v)
    os.makedirs(out, exist_ok=True)
    sheets_def = sheets_for(v)
    model = {"board": {"w": W, "h": H, "t": 1.6}, "layers": []}
    sheets = {}
    for key, p in parts.items():
        if key not in sheets_def:
            continue
        rings = outlines(p["shape"])
        sheet = sheets_def[key]
        model["layers"].append({"id": key, "name": sheet["name"], "z": sheet["z"], "t": sheet["t"],
                                "color": sheet["color"], "opacity": sheet["opacity"],
                                "shapes": rings, "circles": p["circles"]})
        sheets.setdefault(sheet["t"], []).append((key, rings, p["circles"]))
    with open(os.path.join(out, "case-model.json"), "w") as f:
        json.dump(model, f, ensure_ascii=False)

    cfg = VARIANTS[v]
    top = max(sh["z"] + sh["t"] for sh in sheets_def.values())
    print(f"Версия {v.upper()}: ячейка {cfg['cell']} (~{cfg['mah']} мА·ч), проставки M2 × {cfg['standoff']:g}, "
          f"винты M2 × {cfg['screw']}, толщина {top + 7.6:.1f} мм → {os.path.relpath(out, HERE)}/")
    # Листы: детали одной толщины в ряд, с полями 3 мм.
    for t, items in sorted(sheets.items()):
        placed, x = [], 3.0
        hmax = 0.0
        for key, rings, circles in items:
            xs = [px for part in rings for ring in part for px, _ in ring]
            ys = [py for part in rings for ring in part for _, py in ring]
            x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
            placed.append((rings, circles, x - x0, 3.0 - y0))
            x += x1 - x0 + 3.0
            hmax = max(hmax, y1 - y0)
        name = f"case-acrylic-{t:g}mm".replace(".", "_")
        write_svg(os.path.join(out, name + ".svg"), placed, x, hmax + 6.0,
                  f"Корпус брелока {v.upper()}: акрил {t:g} мм")
        write_dxf(os.path.join(out, name + ".dxf"), placed)
        print(f"  {name}: {', '.join(sheets_def[k]['name'] for k, _, _ in items)} — лист {x:.0f} × {hmax + 6:.0f} мм")


def main():
    parts = build()
    for v in VARIANTS:
        write_variant(v, parts)


if __name__ == "__main__":
    main()
