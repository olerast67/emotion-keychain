"""Файлы для заказа: Gerber + сверловка (zip), BOM и CPL для JLCPCB, BOM и расстановка для PCBWay,
сборочные чертежи и превью.

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

# Производитель и описание для BOM PCBWay: по MPN. Пассивные — «или аналог», остальное — точно.
MAKERS = {
    "PKMCS0909E4000-R1": ("Murata", "Piezo buzzer 9x9 mm, 4 kHz"),
    "CL10A106KP8NNNC": ("Samsung Electro-Mechanics", "Capacitor 10uF 10V X5R 10% 0603, or equivalent"),
    "CL05B104KO5NNNC": ("Samsung Electro-Mechanics", "Capacitor 100nF 16V X7R 10% 0402, or equivalent"),
    "CL05A105KA5NQNC": ("Samsung Electro-Mechanics", "Capacitor 1uF 25V X5R 10% 0402, or equivalent"),
    "B5819W SL": ("Jiangsu Changjing Electronics Technology (JSCJ)", "Schottky diode 40V 1A SOD-123"),
    "1N4148WS": ("Jiangsu Changjing Electronics Technology (JSCJ)", "Switching diode 100V 150mA SOD-323"),
    "TYPE-C-31-M-12": ("Korean Hroparts Elec", "USB-C receptacle 16P, SMD pins + 4 THT shell tabs"),
    "KT-0603R": ("Hubei KENTO Elec", "LED red 0603"),
    "AO3401A": ("Alpha & Omega Semiconductor", "P-MOSFET -30V -4A SOT-23"),
    "AO3400A": ("Alpha & Omega Semiconductor", "N-MOSFET 30V 5.7A SOT-23"),
    "B3U-3100P": ("Omron", "Tactile switch, side actuated, with GND terminal"),
    "MDBT50Q-1MV2": ("Raytac", "BLE module nRF52840, chip antenna"),
    "MCP73832T-2ACI/OT": ("Microchip", "Li-Po charger 4.2V SOT-23-5"),
    "USBLC6-2SC6": ("STMicroelectronics", "USB ESD protection SOT-23-6"),
    "LIS3DHTR": ("STMicroelectronics", "Accelerometer 3-axis I2C LGA-16 3x3"),
    "LTR-303ALS-01": ("Lite-On", "Ambient light sensor I2C"),
    "SPH0641LM4H-1": ("Knowles", "PDM MEMS microphone, bottom port"),
}
THT = {"TYPE-C-31-M-12"}  # смешанный монтаж: лапки корпуса в отверстиях


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


def refkey(r):
    m = re.match(r"([A-Z]+)(\d+)", r)
    return m.group(1), int(m.group(2))


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


def maker(p):
    mpn = p["mpn"]
    if mpn in MAKERS:
        return MAKERS[mpn]
    if mpn == "0402WGF0000TCE":
        return "UNI-ROYAL", "Jumper 0R 0402, or equivalent"
    if mpn.startswith("0402WGF"):
        return "UNI-ROYAL", f'Resistor {p["value"]} 1% 1/16W 0402, or equivalent'
    raise SystemExit(f'{p["ref"]}: нет производителя для {mpn} в MAKERS')


def bom_pcbway():
    """BOM в колонках PCBWay: производитель, MPN, описание с номиналами; DNP — отдельными строками."""
    groups = {}
    dnp = []
    for p in netlist.parts():
        if p["dnp"] and (p["lcsc"] or p["mpn"]):
            dnp.append(p)
        if p["dnp"] or not (p["lcsc"] or p["mpn"]):
            continue
        groups.setdefault(p["mpn"], []).append(p)
    path = os.path.join(OUT, NAME + "-bom-pcbway.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item #", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                    "Package/Footprint", "Type", "LCSC #"])
        rows = sorted(groups.values(), key=lambda ps: refkey(min((q["ref"] for q in ps), key=refkey)))
        for i, ps in enumerate(rows, 1):
            p = ps[0]
            make, desc = maker(p)
            refs = sorted((q["ref"] for q in ps), key=refkey)
            w.writerow([i, ",".join(refs), len(refs), make, p["mpn"], desc, footprint_name(p["footprint"]),
                        "SMD+THT" if p["mpn"] in THT else "SMD", p["lcsc"]])
        for p in sorted(dnp, key=lambda p: refkey(p["ref"])):
            w.writerow(["", p["ref"], 0, "", "", f'DNP - do not place ({p["value"]})',
                        footprint_name(p["footprint"]), "DNP", ""])
    return path


def cpl():
    """CPL (координаты монтажа) из позиционного файла KiCad в колонки JLCPCB."""
    raw = os.path.join(OUT, "work", NAME + "-pos.csv")
    kicad("pcb", "export", "pos", "--side", "both", "--format", "csv", "--units", "mm", "-o", raw, PCB)
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
            bottom = row["Side"].lower() != "top"
            rot = float(row["Rot"])
            # Оборот JLCPCB видит со своей стороны: угол отражается, (180 − θ), и только потом
            # поправка корпуса — как в kicad-jlcpcb-tools (_rotation_for_match) и KiBot (mirror_bottom).
            if bottom:
                rot = 180 - rot
            for pattern, delta in JLC_ROTATION:
                if re.search(pattern, row["Package"]):
                    rot += delta
            rot = round(rot % 360, 3) % 360
            w.writerow([ref, f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Bottom" if bottom else "Top", f"{rot:g}"])
    return path, raw


def centroid_pcbway(raw):
    """Расстановка для PCBWay: углы KiCad как есть, без поправок JLCPCB; сверка — по сборочным чертежам."""
    parts = {p["ref"]: p for p in netlist.parts()}
    path = os.path.join(OUT, NAME + "-centroid-pcbway.csv")
    with open(raw) as fin, open(path, "w", newline="") as fout:
        w = csv.writer(fout)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row in csv.DictReader(fin):
            p = parts.get(row["Ref"])
            if p is None or p["dnp"] or not (p["lcsc"] or p["mpn"]):
                continue
            rot = round(float(row["Rot"]) % 360, 3) % 360
            w.writerow([row["Ref"], f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Top" if row["Side"].lower() == "top" else "Bottom", f"{rot:g}"])
    return path


def assembly():
    """Сборочные чертежи сторон: площадки, контуры деталей с метками первого вывода, позиции.
    Оборот — зеркально, как его видит монтажник."""
    import build
    out = []
    for side, layers, mirror in (("top", "F.Paste,F.Fab,F.SilkS,Edge.Cuts", False),
                                 ("bottom", "B.Paste,B.Fab,B.SilkS,Edge.Cuts", True)):
        svg = os.path.join(OUT, "work", f"assembly-{side}.svg")
        cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
               "--page-size-mode", "2", "-o", svg, PCB]
        if mirror:
            cmd.insert(4, "--mirror")
        subprocess.run(cmd, check=True, capture_output=True)
        png = os.path.join(OUT, f"{NAME}-assembly-{side}.png")
        build.to_png(svg, png, width=1600)
        if os.path.exists(png):
            out.append(png)
    return out


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
    c, raw = cpl()
    print("CPL:", os.path.relpath(c, REV))
    print("PCBWay:", os.path.relpath(bom_pcbway(), REV), "·", os.path.relpath(centroid_pcbway(raw), REV))
    for p in assembly():
        print("сборочный чертёж:", os.path.relpath(p, REV))
    try:
        for p in previews():
            print("превью:", os.path.relpath(p, REV))
    except ImportError:
        print("превью пропущены: нужен Pillow и cairosvg")


if __name__ == "__main__":
    run()
