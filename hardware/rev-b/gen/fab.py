"""Файлы для заказа: Gerber + сверловка (zip), BOM и CPL для JLCPCB, BOM и расстановка для PCBWay,
сборочные чертежи и превью — для одной платы (out/) и для панели монтажа (out/panel/).

Запускается из build.py (стадия fab) или сам: python3 hardware/rev-b/gen/fab.py [--no-panel]

Прежде чем что-то писать, fab проверяет плату сам: DRC без ошибок и без несоединённых
площадок, дорожки есть, у деталей, которых завод не ставит, нет пасты. Не прошла — в out/
ничего не меняется. Отчёт DRC — out/keychain-rev-b-drc.rpt (в репозитории).

Панель собирает gen/panel.py через KiKit 1.5. Если KiKit не установлен в том же Python,
fab запускает panel.py интерпретатором из KIKIT_PYTHON (см. panel.py). Без панели
(--no-panel) старые файлы панели удаляются: они бы уже не совпадали с платой.
"""
import csv
import os
import re
import shutil
import subprocess
import sys
import zipfile

import netlist

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.normpath(os.path.join(HERE, ".."))
NAME = "keychain-rev-b"
PCB = os.path.join(REV, NAME + ".kicad_pcb")
OUT = os.path.join(REV, "out")
WORK = os.path.join(OUT, "work")
DRC_REPORT = os.path.join(OUT, NAME + "-drc.rpt")
PANEL_DIR = os.path.join(OUT, "panel")
PANEL_NAME = NAME + "-panel"
PANEL_PCB = os.path.join(PANEL_DIR, PANEL_NAME + ".kicad_pcb")

LAYERS = ["F.Cu", "In1.Cu", "In2.Cu", "B.Cu", "F.Paste", "B.Paste", "F.SilkS", "B.SilkS",
          "F.Mask", "B.Mask", "Edge.Cuts"]

# CPL для JLCPCB: угол = угол KiCad (у оборота — отражённый, 180° − θ) плюс поправка корпуса.
# Нулевой поворот у JLCPCB — как посадочное место детали в их библиотеке (EasyEDA), а оно не
# всегда повёрнуто как в KiCad. По имени места EasyEDA угол виден: суффикс -BL/-BR — первый
# вывод внизу слева/справа. Открытые таблицы расходятся (проверено в октябре 2026):
#   корпус                          KiBot  JLCKicadTools /       Fabrication-  pcb-kit (по
#                                          kicad-jlcpcb-tools    Toolkit       предпросмотру JLC)
#   SOT-23 (3 вывода, -BR)          180    −90 (с 09.2024)       180           180
#   SOT-23-5, SOT-23-6 (-BL)        180    −90                   180           −90, не проверено
#   USB_C_..._HRO_TYPE-C-31-M-12    180    180                   нет           —
#   SW_SPST_B3                      90     90                    90            —
# Здесь: SOT-23 — 180 (Q1–Q4): так его показал предпросмотр JLCPCB (pcb-kit PR 14,
# kicad-jlcpcb-tools PR 881), и место EasyEDA у AO3400A/AO3401A — «-BR». SOT-23-5/6 (U2, U3) —
# −90: место «-BL», первый вывод KiCad (вверху слева) попадает туда поворотом на 90°, как у SOIC
# (270 во всех таблицах). USB-C J1 — 180: обе таблицы, где он есть. SW1 (B3U-3100P) и U1
# (MDBT50Q) идут через Global Sourcing: посадочное место им рисует сама JLCPCB, и запись
# ^SW_SPST_B3 (она для B3U из каталога) к ним не относится — угол как в KiCad. Остальные корпуса
# (SOD-123/323, светодиод 0603, LGA, LTR-303) — без поправки. Всё это подсказка, а не гарантия:
# в предпросмотре JLCPCB каждую полярную деталь сверяют со сборочным чертежом (README, «Заказ на
# JLCPCB») и при надобности поворачивают там.
JLC_ROTATION = [
    (r"^SOT-23$", 180),
    (r"^SOT-23-[56]$", -90),
    (r"^USB_C_Receptacle_HRO_TYPE-C-31-M-12", 180),
]

# Производитель и описание для BOM PCBWay: по MPN. Пассивные — «или аналог», остальное — точно.
MAKERS = {
    "PKMCS0909E4000-R1": ("Murata", "Piezo buzzer 9x9 mm, 4 kHz"),
    "CL10A106KP8NNNC": ("Samsung Electro-Mechanics", "Capacitor 10uF 10V X5R 10% 0603, or equivalent"),
    "CL05B104KO5NNNC": ("Samsung Electro-Mechanics", "Capacitor 100nF 16V X7R 10% 0402, or equivalent"),
    "CL05A105KA5NQNC": ("Samsung Electro-Mechanics", "Capacitor 1uF 25V X5R 10% 0402, or equivalent"),
    "CL10A475KO8NNNC": ("Samsung Electro-Mechanics", "Capacitor 4.7uF 16V X5R 10% 0603, or equivalent"),
    "B5819W SL": ("Jiangsu Changjing Electronics Technology (JSCJ)", "Schottky diode 40V 1A SOD-123"),
    "1N4148WS": ("Jiangsu Changjing Electronics Technology (JSCJ)", "Switching diode 100V 150mA SOD-323"),
    "SMF5.0A": ("Jiangsu Changjing Electronics Technology (JSCJ)",
                "TVS diode unidirectional, 5V standoff, 6.4V min breakdown, 200W, SOD-123FL; cathode to VBUS"),
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


class Target:
    """Что выпускаем: одну плату или панель. copies — суффиксы обозначений копий платы на
    панели ("_1", "_2"); у одной платы — [""]."""

    def __init__(self, pcb, out, name, copies=("",)):
        self.pcb, self.out, self.name, self.copies = pcb, out, name, list(copies)
        self.work = os.path.join(out, "work") if out == OUT else os.path.join(WORK, "panel")

    def path(self, suffix):
        return os.path.join(self.out, self.name + suffix)

    def split(self, ref):
        """Обозначение на этой плате → (обозначение в netlist, суффикс копии) или None."""
        for c in self.copies:
            if c and ref.endswith(c):
                return ref[:-len(c)], c
            if not c:
                return ref, c
        return None


BOARD = Target(PCB, OUT, NAME)


def kicad(*args):
    subprocess.run(["kicad-cli", *args], check=True, capture_output=True)


# ---------------------------------------------------------------- проверка платы

def check_board(pcb=PCB, report=DRC_REPORT):
    """DRC платы и отказ, если плату нельзя отдавать на завод. Отчёт пишется в out/work, а в
    report (он в репозитории) копируется, только если плата прошла. Возвращает сводку."""
    import pcbnew
    import build
    board = pcbnew.LoadBoard(pcb)
    tracks = len(board.GetTracks())
    draft = os.path.join(WORK, os.path.basename(report))
    found, errors, text = build.drc(board, draft)
    unplaced = build.paste_on_unplaced(board)
    problems = []
    if tracks == 0:
        problems.append("в плате нет дорожек — это не разведённая плата (build.py place без route?)")
    if found.get("unconnected pads"):
        problems.append(f"несоединённых площадок: {found['unconnected pads']}")
    if errors:
        problems.append(f"ошибок DRC: {errors}")
    if unplaced:
        problems.append(f"паста на площадках деталей, которых завод не ставит: {unplaced} (build.py finish)")
    if problems:
        sys.exit(f"fab: {os.path.relpath(pcb, REV)} не годится для заказа, файлы в out/ не тронуты:\n  - "
                 + "\n  - ".join(problems) + f"\nОтчёт: {os.path.relpath(draft, REV)}\n"
                 "Вернуть плату и файлы из репозитория: git checkout -- hardware/rev-b/keychain-rev-b.kicad_pcb "
                 "hardware/rev-b/Keychain.pretty hardware/rev-b/out")
    shutil.copyfile(draft, report)
    print(f"DRC {os.path.basename(pcb)}: ошибок 0, несоединённых 0, предупреждений {found.get('warnings', 0)}, "
          f"дорожек и переходных {tracks} → {os.path.relpath(report, REV)}")
    return found


# ---------------------------------------------------------------- Gerber и сверловка

def gerbers(t=BOARD):
    gdir = os.path.join(t.work, "gerber")
    if os.path.isdir(gdir):
        shutil.rmtree(gdir)
    os.makedirs(gdir)
    kicad("pcb", "export", "gerbers", "--layers", ",".join(LAYERS), "--subtract-soldermask",
          "--no-x2", "-o", gdir + "/", t.pcb)
    kicad("pcb", "export", "drill", "--format", "excellon", "--excellon-units", "mm",
          "--excellon-oval-format", "alternate", "--excellon-separate-th", "--drill-origin", "absolute",
          "-o", gdir + "/", t.pcb)
    zpath = t.path("-gerbers.zip")
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(os.listdir(gdir)):
            z.write(os.path.join(gdir, f), f)
    return zpath


# ---------------------------------------------------------------- BOM и расстановка

def footprint_name(fp):
    return fp.split(":", 1)[1]


def refkey(r):
    m = re.match(r"([A-Z]+)(\d+)(?:_(\d+))?", r)
    return m.group(1), int(m.group(2)), int(m.group(3) or 0)


def placed(p):
    """Деталь ставит завод: есть номер LCSC или MPN и она не DNP."""
    return not p["dnp"] and bool(p["lcsc"] or p["mpn"])


def designators(refs, t):
    return ",".join(sorted((r + c for r in refs for c in t.copies), key=refkey))


def bom(t=BOARD):
    """BOM для JLCPCB: Comment, Designator, Footprint, LCSC. На панели — обозначения всех копий."""
    groups = {}
    for p in netlist.parts():
        if placed(p):
            key = (p["value"], footprint_name(p["footprint"]), p["lcsc"], p["mpn"])
            groups.setdefault(key, []).append(p["ref"])
    path = t.path("-bom-jlcpcb.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC", "MPN"])
        for (value, fp, lcsc, mpn), refs in sorted(groups.items(), key=lambda kv: refkey(sorted(kv[1], key=refkey)[0])):
            w.writerow([value, designators(refs, t), fp, lcsc, mpn])
    return path


def hand_assembly():
    """Что паяется руками или не ставится — на каждую плату одинаково."""
    hand = [p for p in netlist.parts() if not placed(p)]
    path = os.path.join(OUT, NAME + "-hand-assembly.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Value", "Footprint", "Note"])
        for p in sorted(hand, key=lambda p: refkey(p["ref"])):
            w.writerow([p["ref"], p["value"], footprint_name(p["footprint"]), p["status"]])
    return path


def maker(p):
    mpn = p["mpn"]
    if mpn in MAKERS:
        return MAKERS[mpn]
    if mpn == "0402WGF0000TCE":
        return "UNI-ROYAL", "Jumper 0R 0402, or equivalent"
    if mpn.startswith("0402WGF"):
        return "UNI-ROYAL", f'Resistor {p["value"]} 1% 1/16W 0402, or equivalent'
    raise SystemExit(f'{p["ref"]}: нет производителя для {mpn} в MAKERS')


def bom_pcbway(t=BOARD):
    """BOM в колонках PCBWay: производитель, MPN, описание с номиналами; DNP — отдельными строками."""
    groups = {}
    dnp = []
    for p in netlist.parts():
        if p["dnp"] and (p["lcsc"] or p["mpn"]):
            dnp.append(p)
        if placed(p):
            groups.setdefault(p["mpn"], []).append(p)
    path = t.path("-bom-pcbway.csv")
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Item #", "Designator", "Qty", "Manufacturer", "Mfg Part #", "Description / Value",
                    "Package/Footprint", "Type", "LCSC #"])
        rows = sorted(groups.values(), key=lambda ps: refkey(min((q["ref"] for q in ps), key=refkey)))
        for i, ps in enumerate(rows, 1):
            p = ps[0]
            make, desc = maker(p)
            refs = [q["ref"] for q in ps]
            w.writerow([i, designators(refs, t), len(refs) * len(t.copies), make, p["mpn"], desc,
                        footprint_name(p["footprint"]), "SMD+THT" if p["mpn"] in THT else "SMD", p["lcsc"]])
        for p in sorted(dnp, key=lambda p: refkey(p["ref"])):
            w.writerow(["", designators([p["ref"]], t), 0, "", "", f'DNP - do not place ({p["value"]})',
                        footprint_name(p["footprint"]), "DNP", ""])
    return path


def positions(t=BOARD):
    """Позиционный файл KiCad: строки деталей, которые ставит завод, с деталью из netlist."""
    raw = os.path.join(t.work, t.name + "-pos.csv")
    os.makedirs(t.work, exist_ok=True)
    kicad("pcb", "export", "pos", "--side", "both", "--format", "csv", "--units", "mm", "-o", raw, t.pcb)
    parts = {p["ref"]: p for p in netlist.parts()}
    rows = []
    with open(raw) as f:
        for row in csv.DictReader(f):
            s = t.split(row["Ref"])
            p = parts.get(s[0]) if s else None
            if p is not None and placed(p):
                rows.append((row, p))
    return rows


def jlc_rotation(row):
    """Угол для CPL JLCPCB: у оборота угол отражается (180° − θ), потом поправка корпуса —
    так считают kicad-jlcpcb-tools (fabrication.py) и KiBot (mirror_bottom); таблица своя."""
    rot = float(row["Rot"])
    if row["Side"].lower() != "top":
        rot = 180 - rot
    for pattern, delta in JLC_ROTATION:
        if re.search(pattern, row["Package"]):
            rot += delta
    return round(rot % 360, 3) % 360


def cpl(rows, t=BOARD):
    """CPL (координаты монтажа) в колонках JLCPCB."""
    path = t.path("-cpl-jlcpcb.csv")
    with open(path, "w", newline="") as fout:
        w = csv.writer(fout)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row, p in rows:
            bottom = row["Side"].lower() != "top"
            w.writerow([row["Ref"], f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Bottom" if bottom else "Top", f"{jlc_rotation(row):g}"])
    return path


def centroid_pcbway(rows, t=BOARD):
    """Расстановка для PCBWay: углы KiCad как есть, без поправок JLCPCB; сверка — по сборочным чертежам."""
    path = t.path("-centroid-pcbway.csv")
    with open(path, "w", newline="") as fout:
        w = csv.writer(fout)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        for row, p in rows:
            rot = round(float(row["Rot"]) % 360, 3) % 360
            w.writerow([row["Ref"], f'{float(row["PosX"]):.3f}mm', f'{float(row["PosY"]):.3f}mm',
                        "Top" if row["Side"].lower() == "top" else "Bottom", f"{rot:g}"])
    return path


def check_placements(rows, t=BOARD):
    """Каждая деталь, которую ставит завод, — ровно один раз в каждой копии платы."""
    want = {p["ref"] for p in netlist.parts() if placed(p)}
    seen = {}
    for row, p in rows:
        seen[row["Ref"]] = seen.get(row["Ref"], 0) + 1
    need = {r + c for r in want for c in t.copies}
    missing = sorted(need - set(seen), key=refkey)
    extra = sorted(set(seen) - need, key=refkey)
    twice = sorted((r for r, n in seen.items() if n > 1), key=refkey)
    if missing or extra or twice:
        sys.exit(f"{t.name}: в расстановке не хватает {missing}, лишние {extra}, дважды {twice}")
    return len(rows)


# ---------------------------------------------------------------- чертежи и превью

def assembly(t=BOARD):
    """Сборочные чертежи сторон: площадки, контуры деталей с метками первого вывода, позиции.
    Оборот — зеркально, как его видит монтажник."""
    import build
    out = []
    for side, layers, mirror in (("top", "F.Paste,F.Fab,F.SilkS,Edge.Cuts", False),
                                 ("bottom", "B.Paste,B.Fab,B.SilkS,Edge.Cuts", True)):
        svg = os.path.join(t.work, f"assembly-{side}.svg")
        cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
               "--page-size-mode", "2", "-o", svg, t.pcb]
        if mirror:
            cmd.insert(4, "--mirror")
        subprocess.run(cmd, check=True, capture_output=True)
        png = t.path(f"-assembly-{side}.png")
        if build.to_png(svg, png, width=1600 if t is BOARD else 2400):
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
            svg = os.path.join(WORK, f"pv-{side}-{layer}.svg")
            cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layer, "--exclude-drawing-sheet",
                   "--page-size-mode", "2", "--black-and-white", "-o", svg, PCB]
            if mirror:
                cmd.insert(4, "--mirror")
            subprocess.run(cmd, check=True, capture_output=True)
            png = svg[:-4] + ".png"
            if not build.to_png(svg, png, width=1100):
                return []
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


# ---------------------------------------------------------------- всё вместе

def outputs(t=BOARD):
    """Gerber, BOM, CPL, файлы PCBWay и сборочные чертежи цели t; возвращает число строк CPL."""
    print("Gerber:", os.path.relpath(gerbers(t), REV))
    print("BOM:", os.path.relpath(bom(t), REV))
    rows = positions(t)
    n = check_placements(rows, t)
    print("CPL:", os.path.relpath(cpl(rows, t), REV), f"({n} строк, по {n // len(t.copies)} на плату)")
    print("PCBWay:", os.path.relpath(bom_pcbway(t), REV), "·", os.path.relpath(centroid_pcbway(rows, t), REV))
    drawings = assembly(t)
    for p in drawings:
        print("сборочный чертёж:", os.path.relpath(p, REV))
    if not drawings:
        print("сборочные чертежи пропущены: нет cairosvg или rsvg-convert (pip install cairosvg)")
    return n


def kikit_python():
    """Интерпретатор, в котором есть и pcbnew, и KiKit: этот же или из KIKIT_PYTHON."""
    try:
        import kikit  # noqa: F401
        return sys.executable
    except ImportError:
        pass
    exe = os.environ.get("KIKIT_PYTHON")
    if exe and os.path.exists(exe):
        return exe
    return None


def run_panel():
    """Панель монтажа: gen/panel.py — в этом процессе, если KiKit здесь есть, иначе в KIKIT_PYTHON."""
    exe = kikit_python()
    if exe is None:
        sys.exit("панель: нужен KiKit 1.5 рядом с pcbnew KiCad 7 — см. gen/panel.py, или KIKIT_PYTHON=путь к его python;\n"
                 "без панели: build.py fab --no-panel (файлы панели в out/panel тогда удаляются)")
    if exe == sys.executable:
        import panel
        panel.main()
    else:
        subprocess.run([exe, os.path.join(HERE, "panel.py")], check=True)


def run(panel=True):
    if panel and kikit_python() is None:
        run_panel()  # выйдет с объяснением раньше, чем что-то записано
    os.makedirs(WORK, exist_ok=True)
    check_board()
    outputs(BOARD)
    print("ручной монтаж:", os.path.relpath(hand_assembly(), REV))
    try:
        shots = previews()
        for p in shots:
            print("превью:", os.path.relpath(p, REV))
        if not shots:
            print("превью пропущены: нет cairosvg или rsvg-convert (pip install cairosvg)")
    except ImportError:
        print("превью пропущены: нужен Pillow (pip install pillow)")
    if panel:
        run_panel()
    elif os.path.isdir(PANEL_DIR):
        shutil.rmtree(PANEL_DIR)
        print("панель не собрана (--no-panel): out/panel удалена — она уже не совпадала бы с платой")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--check"]:  # fab.py --check ПЛАТА ОТЧЁТ — DRC-проверка в чистом процессе
        check_board(sys.argv[2], sys.argv[3])
    else:
        run(panel="--no-panel" not in sys.argv[1:])
