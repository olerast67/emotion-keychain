"""Файлы для заказа на JLCPCB: Gerber + сверловка (zip), BOM и CPL для монтажа, превью.

Запускается из build.py (стадия fab) или сам: python3 hardware/rev-b/gen/fab.py
"""
import csv
import os
import re
import shutil
import subprocess
import zipfile

import netlist

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.normpath(os.path.join(HERE, ".."))
NAME = "keychain-rev-b"
PCB = os.path.join(REV, NAME + ".kicad_pcb")
OUT = os.path.join(REV, "out")
GERBER_DIR = os.path.join(OUT, "work", "gerber")

LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS",
          "F.Mask", "B.Mask", "Edge.Cuts"]

# Поворот в CPL у JLCPCB для некоторых корпусов отличается от KiCad. Известные поправки
# (по таблице kicad-jlcpcb-tools); остальное проверь в 3D-предпросмотре при заказе.
JLC_ROTATION = [
    (r"^SOT-23", 180),
]


def kicad(*args):
    subprocess.run(["kicad-cli", *args], check=True, capture_output=True)


def gerbers():
    if os.path.isdir(GERBER_DIR):
        shutil.rmtree(GERBER_DIR)
    os.makedirs(GERBER_DIR)
    kicad("pcb", "export", "gerbers", "--layers", ",".join(LAYERS), "--subtract-soldermask",
          "--no-x2", "-o", GERBER_DIR + "/", PCB)
    kicad("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
          "--excellon-oval-format", "alternate", "--excellon-separate-th", "--drill-origin", "absolute",
          "-o", GERBER_DIR + "/", PCB)
    zpath = os.path.join(OUT, NAME + "-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(GERBER_DIR)):
            z.write(os.path.join(GERBER_DIR, f), f)
    return zpath


def footprint_name(fp):
    return fp.split(":", 1)[1]


def bom():
    """BOM для JLCPCB: Comment, Designator, Footprint, LCSC. Ручной монтаж и «не ставится» — отдельно."""
    groups = {}
    hand = []
    for p in netlist.parts():
        if p["dnp"] or not (p["lcsc"] or p["mpn"]):
            hand.append(p)
            continue
        key = (p["value"], footprint_name(p["footprint"]), p["lcsc"], p["mpn"])
        groups.setdefault(key, []).append(p["ref"])

    def refkey(r):
        m = re.match(r"([A-Z]+)(\d+)", r)
        return m.group(1), int(m.group(2))

    path = os.path.join(OUT, NAME + "-bom-jlcpcb.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC", "MPN"])
        for (value, fp, lcsc, mpn), refs in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0])):
            w.writerow([value, ",".join(sorted(refs, key=refkey)), fp, lcsc, mpn])
    hand_path = os.path.join(OUT, NAME + "-hand-assembly.csv")
    with open(hand_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Footprint", "Note"])
        for p in sorted(hand, key=lambda p: refkey(p["ref"])):
            w.writerow([p["ref"], p["value"], footprint_name(p["footprint"]), p["status"]])
    return path, hand_path


def cpl():
    """CPL (координаты монтажа) из позиционного файла KiCad в колонки JLCPCB."""
    raw = os.path.join(OUT, "work", NAME + "-pos.csv")
    kicad("pcb", "export", "pos", "--side", "both", "--format", "csv", "--units", "mm",
          "--exclude-dnp", "-o", raw, PCB)
    parts = {p["ref"]: p for p in netlist.parts()}
    path = os.path.join(OUT, NAME + "-cpl-jlcpcb.csv")
    with open(raw) as fin, open(path, "w", newline="") as fout:
        w = csv.writer(fout)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in csv.DictReader(fin):
            ref = row["Ref"]
            p = parts.get(ref)
            if p is None or p["dnp"] or not (p["lcsc"] or p["mpn"]):
                continue
            rot = float(row["Rot"])
            for pattern, delta in JLC_ROTATION:
                if re.search(pattern, row["Package"]):
                    rot = (rot + delta) % 360
            side = "Top" if row["Side"].lower() == "top" else "Bottom"
            w.writerow([ref, f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm', side, f"{rot:g}"])
    return path


def previews():
    """Картинки сторон в духе готовой платы: медь под маской, шёлк сверху."""
    import build
    out = []
    for side, layers, mirror in (("front", ["F.Cu", "F.Mask", "F.SilkS", "Edge.Cuts"], False),
                                 ("back", ["B.Cu", "B.Mask", "B.SilkS", "Edge.Cuts"], True)):
        pngs = []
        for layer in layers:
            svg = os.path.join(OUT, "work", f"pv-{side}-{layer}.svg")
            cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layer, "--exclude-drawing-sheet",
                   "--page-size-mode", "2", "--black-and-white", "-o", svg, PCB]
            if mirror:
                cmd.insert(4, "--mirror")
            subprocess.run(cmd, check=True, capture_output=True)
            png = svg[:-4] + ".png"
            build.to_png(svg, png, width=1100)
            pngs.append(png)
        final = os.path.join(OUT, f"{NAME}-{side}.png")
        compose(pngs, final)
        out.append(final)
    return out


def compose(pngs, final):
    """Склеивает слои: зелёная маска, медь под ней чуть светлее, открытая медь золотом, белый шёлк."""
    from PIL import Image, ImageChops, ImageDraw
    cu, mask, silk, edge = [Image.open(p).convert("L") for p in pngs]
    w, h = cu.size
    black = lambda img: img.point(lambda v: 255 if v < 128 else 0)
    # внутренность платы — заливка от центра внутри контура
    filled = edge.point(lambda v: 255 if v >= 128 else 0)
    ImageDraw.floodfill(filled, (w // 2, h // 2), 0)
    inside = ImageChops.lighter(filled.point(lambda v: 255 if v == 0 else 0), black(edge))
    img = Image.new("RGB", (w, h), (255, 255, 255))
    img.paste((22, 86, 50), mask=inside)
    copper = ImageChops.multiply(black(cu), inside)
    img.paste((40, 118, 68), mask=copper)
    img.paste((212, 172, 86), mask=ImageChops.multiply(black(mask), copper))
    img.paste((246, 246, 240), mask=ImageChops.multiply(black(silk), inside))
    img.paste((70, 70, 70), mask=black(edge))
    img.save(final)


def run():
    os.makedirs(os.path.join(OUT, "work"), exist_ok=True)
    print("Gerber:", os.path.relpath(gerbers(), REV))
    b, h = bom()
    print("BOM:", os.path.relpath(b, REV), "· ручной монтаж:", os.path.relpath(h, REV))
    print("CPL:", os.path.relpath(cpl(), REV))
    try:
        for p in previews():
            print("превью:", os.path.relpath(p, REV))
    except ImportError:
        print("превью пропущены: нужен Pillow и cairosvg")


if __name__ == "__main__":
    run()
