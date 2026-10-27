"""Корпус-«бутерброд» для платы rev B: пластины акрила под лазерную резку.

Слои считаются из той же платы (keychain-rev-b.kicad_pcb): вырезы рамок обходят
дворы деталей, ушки рамок стоят на отверстиях модуля экрана. Пишет версию S в out/,
M и L — в out/m/ и out/l/:

- по SVG и DXF на каждую толщину акрила, детали разложены рядом;
- case-model.json — слои с высотами и размеры стека для 3D-просмотра (viewer.py).

Стек от лица к обороту (z — от лицевой стороны платы, мм; H — высота под модулем
экрана: 6, 7 или 8 мм):

  крышка 2 мм             H+3.2 … H+5.2  прозрачная; винты, отверстие микрофона
  шайбы 2 мм              H+1.2 … H+3.2  между платой модуля экрана и крышкой
  рамка экрана 3 мм       H … H+3        стенки вокруг модуля, держатель трубки микрофона
  рамка ячейки, верх      3 … H          3, 4 или 5 мм; ушки под модулем вместо стоек
  рамка ячейки, низ 3 мм  0 … 3          окно под вилку USB-C
  плата 1.6 мм            −1.6 … 0
  рамка оборота 4 мм      −5.6 … −1.6    ушки под винтами, толкатель кнопки
  дно 2 мм                −7.6 … −5.6    прозрачное, вырез под вилку USB-C

Модуль экрана лежит на ушках рамок ячейки: стопка рамок и есть «проставка», поэтому
её высота всегда совпадает с высотой рамок. Всё стягивают четыре винта M2 с гайками
на дне. В конце сборки модели идут проверки (check()): пересечения деталей, зазоры
до деталей платы, модуля, вилки USB-C и трубки микрофона, тонкие перемычки, длина
винтов. Любая ошибка — выход с перечнем, файлы не пишутся.
"""
import glob
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
WALL = 2.5          # стенка корпуса за краем платы
INSET = 1.0         # на сколько рамки заходят на край платы
GAP = 0.3           # зазор рамок вокруг дворов деталей
MIN_WEB = 1.5       # перемычки рамок не тоньше: тоньше лазер пережигает, акрил ломается
MIN_RING = 0.9      # стенка вокруг отверстий под винт в ушках, крышке и дне
SCREW_D = 2.3       # отверстие под M2
SHAFT_D = 2.0       # сам винт M2
WASHER_D = 4.2      # шайба между модулем и крышкой; отверстие в ней — как в модуле (2.2)
EAR_R = 2.65        # ушко вокруг винта: кольцо 1.5 мм вокруг отверстия 2.3
COMMON_SHEETS = (2.0, 3.0, 4.0, 5.0, 6.0, 8.0)   # акрил, который продают везде
SCREW_LENGTHS = (16, 18, 20, 22, 25, 30)         # ходовые длины M2

# Ушко платы для кольца выходит сверху. Вырез стенок под ним — до TAB_NOTCH_Y: кольцо
# ходит в отверстии H1 (y −3.6 … −0.4), а перемычка рамок между вырезом и двором U1
# (y 0.25) остаётся MIN_WEB.
TAB_NOTCH = 5.5     # полуширина выреза (ушко платы 9 мм)
TAB_NOTCH_Y = -1.25

# Модуль экрана (footprints.OLED) и что на нём.
MODULE_T = 1.2      # плата модуля
GLASS_T = 1.4       # стекло с поляризатором на скотче
MODULE_GAP = 0.5    # рамка экрана вокруг платы модуля: у продавцов размеры гуляют на ±0.2
GLASS_GAP = 0.3     # шайбы не ближе к стеклу: нижние упираются в стекло уже на 1.9 мм от оси
PIN_TOP = 1.8       # короткий конец штыря над платой модуля (3.0 − 1.2)
MODULE_BACK = (1.1, 1.45)  # самая высокая деталь на обороте модуля: SOT-23 … конденсатор 0805
MODULE_FREE_R = 3.3        # у отверстий модуля на обороте нет деталей: там обычно стоят стойки
# Шлейф стекла загибается за нижний край платы модуля по центру. Ширину и вылет у
# продавцов никто не пишет: берём с запасом.
FPC_W, FPC_OUT = 13.0, 0.7

# Гребёнка 2.54: пластик 2.5 под модулем, хвост 6 мм, короткий конец 3 мм.
HEADER_PLASTIC, HEADER_TAIL, HEADER_SHORT = 2.5, 6.0, 3.0
PIN_OUT_MIN = 0.5   # штырь выходит из платы для пайки не меньше
LONG_HEADER = 15.0  # длинная гребёнка: 3 + 2.5 + 9.5

# Ячейка: под ней каптон на спиленных лапках USB-C и тонкий двусторонний скотч.
UNDER_CELL = 0.3
SWELL = 0.3         # запас на вздутие ячейки с зарядом и возрастом
POCKET_W = 21.0     # гнездо ячейки в рамках: ячейка до 20.7 мм шириной с запасом 0.15
LEAD_W = 1.4        # канал для проводов ячейки справа от гнезда

# Вилка USB-C: корпус по стандарту не больше 12.35 × 6.5 мм. Ось гнезда HRO
# TYPE-C-31-M-12 (корпус 3.26 мм) на обороте — 1.63 мм от платы.
PLUG_W, PLUG_T, PLUG_GAP = 12.35, 6.5, 0.5
J1_AXIS = -1.6 - 1.63

# Микрофон U6 на обороте слушает через отверстие в плате. На лице до крышки идёт
# силиконовая трубка 1 × 2 мм: так он слышит наружу, а не всю полость корпуса.
# Трубка сдвинута от C4 на 0.15 мм, отверстие в плате остаётся внутри её канала.
TUBE_ID, TUBE_OD, TUBE_DX = 1.0, 2.0, -0.15
TUBE_SQUEEZE = 0.3  # трубка длиннее промежутка плата — крышка: поджата с обоих концов
GUIDE_D = 2.1       # отверстие держателя трубки в рамках
GUIDE_R = 2.55      # бобышка держателя: стенка 1.5 мм
MIC_D = 1.2         # отверстие в крышке над трубкой

# Толкатель кнопки (SW1 — B3U-3100P, ход 0.2 мм). Упор — стенка корпуса: толкатель
# выступает на BTN_OUT, палец останавливается на стенке.
BTN_REST = 0.1      # зазор до штока кнопки в покое
BTN_OUT = 0.5
SW_TRAVEL = 0.2
KERF_MAX = 0.3      # ширина реза, если мастерская её не вычитает

# Высоты деталей над платой, мм (остальные на лице — 1.0, на обороте — 1.1).
FRONT_H = {"U1": 2.2, "U5": 0.7, "BT1": 1.5}
BACK_H = {"J1": 3.26, "BZ1": 1.9, "M1": 2.0, "SW1": 1.6}

SHEET_T = {"cover": 2.0, "washers": 2.0, "frame_screen": 3.0, "frame_back": 4.0, "button": 4.0, "bottom": 2.0}
NUT_T = 1.6

# Три версии под ячейки 20 × 25 мм разной толщины. Плата, прошивка и раскрой платы те же;
# меняется только верхняя рамка ячейки, а с ней высота под модулем и винты.
VARIANTS = {
    "s": {"cell": "402025", "mah": 150, "days": 11, "cell_t": 4.0, "mid": [3.0, 3.0]},
    "m": {"cell": "502025", "mah": 200, "days": 14, "cell_t": 5.0, "mid": [3.0, 4.0]},
    "l": {"cell": "602025", "mah": 250, "days": 18, "cell_t": 6.0, "mid": [3.0, 5.0]},
}

# Пороги проверок — отдельно от размеров построения: если кто-то уменьшит зазор
# выше, проверка это поймает.
NEED = {"детали": 0.25, "плата модуля": 0.4, "стекло": 0.25, "пластик гребёнки": 0.25, "штыри экрана": 0.2,
        "шлейф стекла": 0.2, "ячейка": 0.25, "вилка USB-C": 0.5, "трубка": 0.25, "держатель трубки": 0.03,
        "винты": 0.1, "оборот модуля": 0.0, "ячейка — винты": 0.2, "слой": 0.25}

CLEAR = ("#dff3ff", 0.28)
COLOR = ("#ff7a3d", 0.55)


# ---------------------------------------------------------------- геометрия

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


def circle(x, y, r, n=48):
    return poly([(x + r * math.cos(2 * math.pi * k / n), y + r * math.sin(2 * math.pi * k / n)) for k in range(n)])


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


def intersect(a, b):
    out = pcbnew.SHAPE_POLY_SET(a)
    out.BooleanIntersection(b, pcbnew.SHAPE_POLY_SET.PM_FAST)
    return out


def inflated(s, d):
    """Раздуть на d мм (d < 0 — сжать)."""
    out = pcbnew.SHAPE_POLY_SET(s)
    if d:
        out.Inflate(MM(d), 32)
    return out


def closed(s, r):
    """Морфологическое закрытие: сливает вырезы, между которыми меньше 2r материала."""
    return inflated(inflated(s, r), -r)


def area(s):
    return s.Area() / 1e12


def topology(s):
    return s.OutlineCount(), sum(s.HoleCount(i) for i in range(s.OutlineCount()))


def bbox(s):
    b = s.BBox()
    return pcbnew.ToMM(b.GetX()), pcbnew.ToMM(b.GetY()), pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())


def holes_of(circles):
    return union(*[circle(x, y, d / 2) for x, y, d in circles]) if circles else pcbnew.SHAPE_POLY_SET()


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


# ---------------------------------------------------------------- плата

def fab_points(fp):
    """Концы линий слоя Fab детали: по ним — шток кнопки и лицо гнезда USB-C."""
    pts = []
    for g in fp.GraphicalItems():
        if isinstance(g, pcbnew.FP_SHAPE) and g.GetLayerName().endswith("Fab") and g.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            for p in (g.GetStart(), g.GetEnd()):
                pts.append((pcbnew.ToMM(p.x), pcbnew.ToMM(p.y)))
    return pts


def pos(item):
    p = item.GetPosition()
    return round(pcbnew.ToMM(p.x), 3), round(pcbnew.ToMM(p.y), 3)


def board_data():
    board = pcbnew.LoadBoard(PCB)
    front, back, pads = {}, {}, []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        if ref == "H1":
            continue
        flipped = fp.IsFlipped()
        c = fp.GetCourtyard(pcbnew.B_CrtYd if flipped else pcbnew.F_CrtYd)
        if c.OutlineCount():
            if flipped:
                back[ref] = (pcbnew.SHAPE_POLY_SET(c), BACK_H.get(ref, 1.1))
            else:
                front[ref] = (pcbnew.SHAPE_POLY_SET(c), FRONT_H.get(ref, 1.0))
        if not flipped and ref != "J2":
            for p in fp.Pads():
                if p.IsOnLayer(pcbnew.F_Cu):
                    b = p.GetBoundingBox()
                    pads.append((ref, rect(pcbnew.ToMM(b.GetX()), pcbnew.ToMM(b.GetY()),
                                           pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))))
    j2 = board.FindFootprintByReference("J2")
    screws = sorted(pos(p) for p in j2.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH)
    pins = sorted(pos(p) for p in j2.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH)
    u6 = board.FindFootprintByReference("U6")
    port = next(p for p in u6.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH)
    sw1 = board.FindFootprintByReference("SW1")
    j1 = board.FindFootprintByReference("J1")
    h1 = board.FindFootprintByReference("H1")
    return {
        "front": front, "back": back, "pads": pads, "screws": screws, "pins": pins,
        "port": pos(port) + (pcbnew.ToMM(port.GetDrillSize().x),),
        "sw_tip": max(x for x, _ in fab_points(sw1)), "sw_y": pos(sw1)[1],
        "usb_face": max(y for _, y in fab_points(j1)), "usb_x": pos(j1)[0],
        "h1": pos(h1) + (pcbnew.ToMM(next(iter(h1.Pads())).GetDrillSize().x),),
    }


# ---------------------------------------------------------------- модель

def stack(v):
    """z нижней грани и толщина каждого листа версии v."""
    h = sum(VARIANTS[v]["mid"])
    z = {
        "cover": h + MODULE_T + SHEET_T["washers"], "washers": h + MODULE_T, "frame_screen": h,
        "frame_back": -1.6 - SHEET_T["frame_back"], "button": -1.6 - SHEET_T["button"],
        "bottom": -1.6 - SHEET_T["frame_back"] - SHEET_T["bottom"],
    }
    t = dict(SHEET_T)
    z0 = 0.0
    for i, ti in enumerate(VARIANTS[v]["mid"]):
        key = f"frame_cell{i + 1}"
        z[key], t[key] = z0, ti
        z0 += ti
    return h, z, t


def ear(x, y):
    """Ушко рамки вокруг винта, сросшееся с ближней стенкой."""
    side = -WALL - 1 if x < W / 2 else W + WALL + 1
    return union(circle(x, y, EAR_R), rect(min(x, side), y - EAR_R, max(x, side), y + EAR_R))


def fork(x, y):
    """Отверстие ушка рамки ячейки, открытое к ячейке: перемычка между винтом и ячейкой
    была бы 0.1–0.3 мм, а вилка ещё и пускает ячейку шире."""
    to = x + 3.0 if x < W / 2 else x - 3.0
    return union(circle(x, y, SCREW_D / 2), rect(min(x, to), y - SCREW_D / 2, max(x, to), y + SCREW_D / 2))


def build(v):
    """Детали версии v: ключ → форма (мм), отверстия-окружности, z, толщина, имя, цвет.
    Плюс твёрдые тела вокруг (детали платы, модуль, вилка, трубка) для проверок."""
    b = board_data()
    h, Z, T = stack(v)
    cfg = VARIANTS[v]
    a = MIN_WEB / 2 - 0.05

    body = rounded(0, 0, W, H, R)
    outer = minus(rounded(-WALL, -WALL, W + WALL, H + WALL, R + WALL),
                  rect(W / 2 - TAB_NOTCH, -WALL - 1, W / 2 + TAB_NOTCH, TAB_NOTCH_Y))
    inner_body = inflated(body, -INSET)
    screws = b["screws"]

    # Модуль экрана: середина ряда штырей и ось штырей от верхнего края модуля.
    o = footprints.OLED
    pins = b["pins"]
    px0, px1, py = pins[0][0], pins[-1][0], pins[0][1]
    mx0, my0 = (px0 + px1) / 2 - o["w"] / 2, py - o["pin_y"]
    module = rect(mx0, my0, mx0 + o["w"], my0 + o["h"])
    gx0 = mx0 + (o["w"] - o["glass_w"]) / 2
    glass = rect(gx0, my0 + o["glass_y"], gx0 + o["glass_w"], my0 + o["glass_y"] + o["glass_h"])
    mod_holes = sorted((round(x, 3), round(y, 3)) for x in (mx0 + o["hole_dx"], mx0 + o["w"] - o["hole_dx"])
                       for y in (my0 + o["hole_dy"], my0 + o["h"] - o["hole_dy"]))
    plastic = rect(px0 - 1.27, py - 1.27, px1 + 1.27, py + 1.27)
    pin_pads = rect(px0 - 0.85, py - 0.85, px1 + 0.85, py + 0.85)
    fpc = rect(W / 2 - FPC_W / 2, my0 + o["h"] - 1.0, W / 2 + FPC_W / 2, my0 + o["h"] + FPC_OUT)
    fpc_z = (h - 0.3, h + MODULE_T + 0.5)

    # Ячейка 20 × 25 по центру BT1 у нижнего края платы; гнездо в рамках шире и
    # переходит в канал для проводов к площадкам BT1 справа.
    _, bx, by, _ = netlist.PLACE["BT1"]
    c = footprints.CELL
    cell = rect(bx - c["w"] / 2, by - c["h"] / 2, bx + c["w"] / 2, by + c["h"] / 2)
    pocket = (bx - POCKET_W / 2, by - c["h"] / 2 - GAP, bx + POCKET_W / 2, by + c["h"] / 2 + GAP)
    bt1 = bbox(b["front"]["BT1"][0])
    # Канал — от правого верхнего угла ячейки вниз к площадкам, под верхним правым винтом:
    # 1.4 мм к запасу гнезда 0.5 мм — места на два провода 30 AWG.
    y_screw = max(y for x, y in b["screws"] if x > W / 2 and y < by) + SCREW_D / 2
    channel = union(rect(pocket[2] - 0.01, y_screw, pocket[2] + LEAD_W, bt1[1] + 0.01),
                    inflated(b["front"]["BT1"][0], GAP))
    pocket = rect(*pocket)

    # Вилка USB-C перед гнездом: окно во всех листах, куда она достаёт по высоте.
    ux = b["usb_x"]
    plug = rect(ux - PLUG_W / 2, b["usb_face"], ux + PLUG_W / 2, H + WALL + 10)
    plug_z = (J1_AXIS - PLUG_T / 2, J1_AXIS + PLUG_T / 2)
    usb_cut = rect(ux - PLUG_W / 2 - PLUG_GAP, b["usb_face"] - PLUG_GAP, ux + PLUG_W / 2 + PLUG_GAP, H + WALL + 1)

    def needs_window(z0, t):
        return z0 < plug_z[1] + PLUG_GAP and z0 + t > plug_z[0] - PLUG_GAP

    # Трубка микрофона и её держатель (рука от левой стенки) в листах выше конденсаторов.
    mx, my, md = b["port"]
    tube_c = (mx + TUBE_DX, my)
    tube = circle(*tube_c, TUBE_OD / 2)
    guide_hole = (tube_c[0], tube_c[1], GUIDE_D)
    arm = union(circle(*tube_c, GUIDE_R), rect(-WALL - 1, tube_c[1] - GUIDE_R, tube_c[0], tube_c[1] + GUIDE_R))

    def arm_fits(z0):
        """Держатель трубки — только в листах выше деталей лица под ним (C4, C6)."""
        return not any(hgt > z0 and area(intersect(crt, inflated(arm, GAP))) > 1e-6
                       for ref, (crt, hgt) in b["front"].items())

    ears = union(*[ear(x, y) for x, y in screws])
    parts = {}

    def add(key, shape, circles, name, color):
        parts[key] = {"shape": shape, "circles": circles, "z": Z[key], "t": T[key], "name": name,
                      "color": color[0], "opacity": color[1]}

    # Крышка и дно.
    add("cover", outer, [(x, y, SCREW_D) for x, y in screws] + [(tube_c[0], tube_c[1], MIC_D)], "крышка", CLEAR)
    add("bottom", minus(outer, usb_cut), [(x, y, SCREW_D) for x, y in screws], "дно", CLEAR)

    # Шайбы между модулем и крышкой; у стекла срезаны лыской.
    washers = union(*[circle(x, y, WASHER_D / 2) for x, y in screws])
    washers = minus(washers, inflated(glass, GLASS_GAP))
    add("washers", washers, [(x, y, o["hole_d"]) for x, y in screws], "шайбы", (CLEAR[0], 0.5))

    # Рамка экрана: стенки вокруг платы модуля и держатель трубки.
    z0, t = Z["frame_screen"], T["frame_screen"]
    cut = union(minus(inner_body, arm), inflated(module, MODULE_GAP))
    if z0 < fpc_z[1] and z0 + t > fpc_z[0]:
        cut = union(cut, inflated(fpc, GAP))
    add("frame_screen", minus(outer, closed(cut, a)), [guide_hole], "рамка экрана", COLOR)

    # Рамки ячейки: стопка высотой H, ушки под модулем вместо стоек.
    for i in range(len(cfg["mid"])):
        key = f"frame_cell{i + 1}"
        z0, t = Z[key], T[key]
        guide = arm_fits(z0)
        keep = [pocket, channel, inflated(plastic, GAP), union(*[fork(x, y) for x, y in screws])]
        keep += [inflated(crt, GAP) for ref, (crt, hgt) in b["front"].items() if ref != "J2" and hgt > z0]
        if z0 < fpc_z[1] and z0 + t > fpc_z[0]:
            keep.append(inflated(fpc, GAP))
        if needs_window(z0, t):
            keep.append(usb_cut)
        if not guide:
            keep.append(inflated(tube, GAP))
        solid = union(ears, arm) if guide else ears
        cut = union(minus(inner_body, solid), *keep)
        where = "низ" if i == 0 else "верх"
        add(key, minus(outer, closed(cut, a)), [guide_hole] if guide else [], f"рамка ячейки, {where}", COLOR)

    # Рамка оборота: ушки под винтами вместо шайб, карман и щель толкателя, окно USB-C.
    sw_crt = bbox(b["back"]["SW1"][0])
    btn_y = b["sw_y"]
    pocket_out = W + WALL - MIN_WEB
    x0 = b["sw_tip"] + BTN_REST
    btn_pocket = rect(sw_crt[0] - GAP, btn_y - 2.8, pocket_out, btn_y + 2.8)
    btn_slot = rect(pocket_out - 0.05, btn_y - 1.3, W + WALL + 1, btn_y + 1.3)
    keep = [btn_pocket, btn_slot] + [inflated(crt, GAP) for crt, _ in b["back"].values()]
    if needs_window(Z["frame_back"], T["frame_back"]):
        keep.append(usb_cut)
    cut = union(minus(inner_body, ears), *keep)
    add("frame_back", minus(outer, closed(cut, a)), [(x, y, SCREW_D) for x, y in screws], "рамка оборота", COLOR)

    # Толкатель: полка в кармане держит его, шток выходит из стенки на BTN_OUT.
    plunger = union(rect(x0, btn_y - 2.4, pocket_out, btn_y + 2.4),
                    rect(pocket_out - 0.01, btn_y - 1.1, W + WALL + BTN_OUT, btn_y + 1.1))
    add("button", plunger, [], "толкатель кнопки", ("#ffd23d", 0.9))

    # Твёрдые тела вокруг: (z0, z1, форма, зазор, кому можно ближе).
    top = Z["cover"] + T["cover"]
    guide = NEED["держатель трубки"]
    solids = {
        "плата модуля": (h, h + MODULE_T, module, NEED["плата модуля"], {}),
        "стекло": (h + MODULE_T, h + MODULE_T + GLASS_T, glass, NEED["стекло"], {}),
        "детали оборота модуля": (h - MODULE_BACK[1], h, minus(module, *[circle(x, y, MODULE_FREE_R) for x, y in mod_holes]),
                                  NEED["оборот модуля"], {}),
        "пластик гребёнки": (h - HEADER_PLASTIC, h, plastic, NEED["пластик гребёнки"], {}),
        "штыри экрана": (-1.6 - 1.0, h + MODULE_T + PIN_TOP, pin_pads, NEED["штыри экрана"], {}),
        "шлейф стекла": (fpc_z[0], fpc_z[1], fpc, NEED["шлейф стекла"], {}),
        "ячейка": (UNDER_CELL, UNDER_CELL + cfg["cell_t"] + SWELL, cell, NEED["ячейка"], {}),
        "вилка USB-C": (plug_z[0] - NEED["вилка USB-C"], plug_z[1] + NEED["вилка USB-C"], plug, NEED["вилка USB-C"], {}),
        "трубка микрофона": (0.0, Z["cover"], tube, NEED["трубка"],
                             {"frame_screen": guide, "frame_cell2": guide, "frame_cell3": guide}),
        "винты": (Z["bottom"] - NUT_T, top, union(*[circle(x, y, SHAFT_D / 2) for x, y in screws]), NEED["винты"], {}),
    }
    for ref, (crt, hgt) in b["front"].items():
        if ref != "J2":
            solids[ref] = (0.0, hgt, crt, NEED["детали"], {})
    for ref, (crt, hgt) in b["back"].items():
        solids[ref] = (-1.6 - hgt, -1.6, crt, NEED["детали"], {"button": None} if ref == "SW1" else {})

    info = {
        "board": b, "h": h, "module": (mx0, my0, o["w"], o["h"]), "mod_holes": mod_holes, "glass": glass,
        "tube_c": tube_c, "tube_len": Z["cover"] + TUBE_SQUEEZE, "pocket": bbox(pocket),
        "x0": x0, "pocket_out": pocket_out, "btn_y": btn_y, "plug_z": plug_z, "top": top,
    }
    return parts, solids, info


# ---------------------------------------------------------------- проверки

def thin_spots(shape, web):
    """Места тоньше web: перемычки, которые рвут кольцо, и узкие стенки или зубцы."""
    r = web / 2 - 0.05
    er = inflated(shape, -r)
    found = []
    if topology(er) != topology(shape):
        found.append(f"перемычка тоньше {web:g} мм рвёт деталь (контуры/отверстия {topology(shape)} → {topology(er)})")
    rest = minus(shape, inflated(er, r + 0.01))
    for i in range(rest.OutlineCount()):
        piece = rest.Subset(i, i + 1)
        # Скругление прямого угла даёт обрезок толщиной ~0.12 мм — его не считаем.
        if inflated(piece, -0.15).OutlineCount():
            x0, y0, x1, y1 = bbox(piece)
            found.append(f"тоньше {web:g} мм у x {x0:.1f}…{x1:.1f}, y {y0:.1f}…{y1:.1f}")
    return found


def ring_min(shape, x, y, d):
    """Самая тонкая стенка вокруг отверстия, мм (с шагом 0.05)."""
    w = 0.0
    while w < 3.0 and area(minus(circle(x, y, d / 2 + w + 0.05), shape, circle(x, y, d / 2))) < 1e-4:
        w += 0.05
    return w


def check(v, parts, solids, info):
    err = []
    cfg = VARIANTS[v]
    h = info["h"]

    def material(p):
        return minus(p["shape"], holes_of(p["circles"]))

    # Толщины листов и высоты.
    for key, p in parts.items():
        if p["t"] not in COMMON_SHEETS:
            err.append(f"{p['name']}: лист {p['t']:g} мм редкий")
    if abs(sum(cfg["mid"]) - h) > 1e-9:
        err.append("стопка рамок ячейки не равна высоте под модулем")
    gap = parts["cover"]["z"] - (parts["frame_screen"]["z"] + parts["frame_screen"]["t"])
    if not 0.1 <= gap <= 0.3:
        err.append(f"рамка экрана ниже крышки на {gap:.2f} мм, нужно 0.1…0.3")
    if parts["cover"]["z"] - (h + MODULE_T + GLASS_T) < 0.5:
        err.append("над стеклом меньше 0.5 мм")
    if max(hgt for _, hgt in info["board"]["back"].values()) > SHEET_T["frame_back"] - 0.5:
        err.append("деталь оборота ближе 0.5 мм к дну")

    # Перемычки и стенки вокруг отверстий.
    for key in ("frame_screen", "frame_cell1", "frame_cell2", "frame_cell3", "frame_back"):
        if key in parts:
            err += [f"{parts[key]['name']}: {s}" for s in thin_spots(parts[key]["shape"], MIN_WEB)]
    for key in ("cover", "bottom", "frame_back", "frame_screen", "frame_cell2", "frame_cell3"):
        p = parts.get(key)
        for x, y, d in (p["circles"] if p else []):
            if ring_min(p["shape"], x, y, d) < MIN_RING - 1e-6:
                err.append(f"{p['name']}: стенка вокруг отверстия ({x:.1f}, {y:.1f}) тоньше {MIN_RING} мм")
    for x, y, d in parts["washers"]["circles"]:
        if ring_min(parts["washers"]["shape"], x, y, d) < 0.45:
            err.append(f"шайба ({x:.1f}, {y:.1f}): стенка тоньше 0.45 мм")
    if info["pocket_out"] - info["x0"] < 0.7:
        err.append("полка толкателя тоньше 0.7 мм")
    # Толкатель: вдавленный до стенки, он дожимает кнопку на весь ход при любом резе
    # от 0 до KERF_MAX и не давит дальше 0.35 мм после упора кнопки.
    for k in (0.0, KERF_MAX):
        press = (BTN_OUT + k) - (info["x0"] - info["board"]["sw_tip"] + 1.5 * k) - SW_TRAVEL
        if not 0.0 <= press <= 0.35:
            err.append(f"толкатель при резе {k:g} мм дожимает кнопку на {press:.2f} мм, нужно 0…0.35")
    # Ячейка в гнезде не трётся о винты.
    shafts = union(*[circle(x, y, SHAFT_D / 2 + NEED["ячейка — винты"] - 0.01) for x, y in info["board"]["screws"]])
    if area(intersect(rect(*info["pocket"]), shafts)) > 1e-3:
        err.append("гнездо ячейки ближе 0.2 мм к винтам")

    # Детали одного слоя по высоте не пересекаются.
    keys = list(parts)
    for i, ka in enumerate(keys):
        for kb in keys[i + 1:]:
            pa, pb = parts[ka], parts[kb]
            if min(pa["z"] + pa["t"], pb["z"] + pb["t"]) - max(pa["z"], pb["z"]) <= 1e-6:
                continue
            c = 0.0 if {ka, kb} == {"button", "frame_back"} else NEED["слой"]
            if area(intersect(material(pa), inflated(material(pb), c - 0.01 if c else -0.005))) > 1e-3:
                err.append(f"{pa['name']} и {pb['name']} ближе {c:g} мм")

    # Зазоры до тел вокруг.
    for name, (z0, z1, shape, c, special) in solids.items():
        for key, p in parts.items():
            if min(z1, p["z"] + p["t"]) - max(z0, p["z"]) <= 1e-6:
                continue
            cc = special.get(key, c) if key in special else c
            if cc is None:
                continue
            probe = inflated(shape, cc - 0.01) if cc > 0 else inflated(shape, -0.005)
            if area(intersect(material(p), probe)) > 1e-3:
                err.append(f"{p['name']}: до «{name}» меньше {cc:g} мм")

    # Каждый лист столбика под винтом держит винт кольцом, а не краем.
    for key in ("cover", "washers", "frame_cell1", "frame_cell2", "frame_cell3", "frame_back", "bottom"):
        p = parts.get(key)
        for x, y in (info["board"]["screws"] if p else []):
            ring = minus(circle(x, y, 2.1), circle(x, y, SCREW_D / 2))
            if area(intersect(material(p), ring)) < 4.0:
                err.append(f"{p['name']}: под винтом ({x:.1f}, {y:.1f}) мало опоры")

    # Трубка: отверстие в плате внутри её канала, площадки лица не под её стенкой.
    tx, ty = info["tube_c"]
    px, py, pd = info["board"]["port"]
    if math.hypot(px - tx, py - ty) + pd / 2 > TUBE_ID / 2 - 0.05:
        err.append("отверстие микрофона в плате выходит за канал трубки")
    for ref, pad in info["board"]["pads"]:
        if area(intersect(pad, circle(tx, ty, TUBE_OD / 2 + 0.1))) > 1e-4:
            err.append(f"трубка микрофона ближе 0.1 мм к площадке {ref}")

    # Модуль экрана: отверстия сходятся с отверстиями платы.
    if [tuple(p) for p in info["mod_holes"]] != [tuple(p) for p in info["board"]["screws"]]:
        err.append(f"отверстия модуля {info['mod_holes']} не на отверстиях J2 {info['board']['screws']}")

    if err:
        print(f"Версия {v.upper()}: проверки не прошли:")
        for e in err:
            print("  -", e)
        sys.exit(1)


def numbers(v, parts, info):
    """Размеры версии, которые нужны README и просмотру."""
    cfg = VARIANTS[v]
    h = info["h"]
    top, low = info["top"], parts["bottom"]["z"]
    need = top - low + NUT_T
    screw = min(n for n in SCREW_LENGTHS if n >= need - 1e-9)
    below = h + 1.6 + PIN_OUT_MIN
    std = HEADER_PLASTIC + HEADER_TAIL
    free = [h - m for m in MODULE_BACK]
    cell_max = [f - UNDER_CELL - SWELL for f in free]
    # Толкатель: зазор в покое растёт на 1.5 ширины реза, выступ — на одну.
    press = [(BTN_OUT + k) - (BTN_REST + 1.5 * k) - SW_TRAVEL for k in (0.0, KERF_MAX)]
    return {
        "standoff": h, "thickness": round(top - low, 2), "screw": screw, "screw_need": round(need, 2),
        "screw_out": round(screw - need, 2), "below_module": round(below, 2), "std_header_margin": round(std - below, 2),
        "long_header": std - below < 0.3, "long_header_out": round(LONG_HEADER - HEADER_SHORT - h - 1.6, 1),
        "free": [round(f, 2) for f in free], "cell_max": [round(c_, 2) for c_ in cell_max],
        "cell_width_max": round(POCKET_W - 0.3, 2), "tube_len": round(info["tube_len"], 1),
        "press": [round(p, 2) for p in press],
        "sheets": sorted({p["t"] for p in parts.values()}),
    }


# ---------------------------------------------------------------- вывод

def pieces(p):
    """Отдельные куски детали с их отверстиями — для раскладки на листе."""
    res = []
    rings = outlines(p["shape"])
    s = p["shape"]
    for i, part in enumerate(rings):
        piece = s.Subset(i, i + 1)
        circ = [cc for cc in p["circles"] if piece.Contains(pcbnew.VECTOR2I(MM(cc[0]), MM(cc[1])))]
        res.append((part, circ))
    return res


def write_svg(path, items, w, h, title):
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{h:.2f}mm" '
           f'viewBox="0 0 {w:.3f} {h:.3f}">', f"<title>{title}</title>"]
    for key, name, rings, circles, dx, dy in items:
        out.append(f'<g id="{key}" fill="none" stroke="#ff0000" stroke-width="0.05"><title>{name}</title>')
        for ring in rings:
            d = " ".join(f"{x + dx:.3f},{y + dy:.3f}" for x, y in ring)
            out.append(f'<polygon points="{d}"/>')
        for x, y, dia in circles:
            out.append(f'<circle cx="{x + dx:.3f}" cy="{y + dy:.3f}" r="{dia / 2:.3f}"/>')
        out.append("</g>")
    out.append("</svg>")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def write_dxf(path, items):
    """DXF R12 в мм: контуры — LINE, отверстия — CIRCLE. Y перевёрнут (в DXF ось вверх)."""
    lines = ["0", "SECTION", "2", "ENTITIES"]
    for _, _, rings, circles, dx, dy in items:
        for ring in rings:
            for (x0, y0), (x1, y1) in zip(ring, ring[1:] + ring[:1]):
                lines += ["0", "LINE", "8", "CUT", "10", f"{x0 + dx:.4f}", "20", f"{-(y0 + dy):.4f}",
                          "11", f"{x1 + dx:.4f}", "21", f"{-(y1 + dy):.4f}"]
        for x, y, dia in circles:
            lines += ["0", "CIRCLE", "8", "CUT", "10", f"{x + dx:.4f}", "20", f"{-(y + dy):.4f}", "40", f"{dia / 2:.4f}"]
    lines += ["0", "ENDSEC", "0", "EOF"]
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def layout(items, row_w=125.0, gap=3.0):
    """Куски одной толщины рядами по ширине листа row_w: [(ключ, имя, кольца, окружности, dx, dy)], ширина, высота."""
    placed, x, y, row_h, width = [], gap, gap, 0.0, 0.0
    for key, name, rings, circles in items:
        xs = [px for ring in rings for px, _ in ring]
        ys = [py for ring in rings for _, py in ring]
        w, hh = max(xs) - min(xs), max(ys) - min(ys)
        if x > gap and x + w > row_w:
            x, y, row_h = gap, y + row_h + gap, 0.0
        placed.append((key, name, rings, circles, x - min(xs), y - min(ys)))
        x += w + gap
        row_h = max(row_h, hh)
        width = max(width, x)
    return placed, width, y + row_h + gap


def write_variant(v, parts, nums):
    out = OUT if v == "s" else os.path.join(OUT, v)
    os.makedirs(out, exist_ok=True)
    for old in glob.glob(os.path.join(out, "case-acrylic-*")):
        os.remove(old)
    cfg = VARIANTS[v]
    model = {"board": {"w": W, "h": H, "t": 1.6}, "variant": {"id": v, **{k: cfg[k] for k in ("cell", "mah", "days", "cell_t", "mid")},
                                                               **nums}, "layers": []}
    by_t = {}
    for key, p in parts.items():
        model["layers"].append({"id": key, "name": p["name"], "z": round(p["z"], 3), "t": p["t"], "color": p["color"],
                                "opacity": p["opacity"], "shapes": outlines(p["shape"]),
                                "circles": [[round(c, 3) for c in cc] for cc in p["circles"]]})
        for rings, circles in pieces(p):
            by_t.setdefault(p["t"], []).append((key, p["name"], rings, circles))
    model["layers"].sort(key=lambda L: -L["z"])
    with open(os.path.join(out, "case-model.json"), "w") as f:
        json.dump(model, f, ensure_ascii=False)

    print(f"Версия {v.upper()}: ячейка {cfg['cell']} (~{cfg['mah']} мА·ч), под модулем {nums['standoff']:g} мм "
          f"(рамки ячейки {' + '.join(f'{t:g}' for t in cfg['mid'])}), толщина {nums['thickness']:.1f} мм, "
          f"винты M2 × {nums['screw']} (нужно {nums['screw_need']:g}, из гайки выйдет {nums['screw_out']:g}) "
          f"→ {os.path.relpath(out, HERE)}/")
    print(f"  ячейка: свободно {nums['free'][1]:g}…{nums['free'][0]:g} мм, ячейка не толще "
          f"{nums['cell_max'][1]:g}…{nums['cell_max'][0]:g} мм, не шире {nums['cell_width_max']:g}; "
          f"гребёнка: нужно {nums['below_module']:g} мм под модулем, у обычной запас {nums['std_header_margin']:g}; "
          f"трубка {nums['tube_len']:g} мм; толкатель дожат на {nums['press'][1]:g}…{nums['press'][0]:g} мм")
    for t, items in sorted(by_t.items()):
        placed, w, hh = layout(items)
        name = f"case-acrylic-{t:g}mm".replace(".", "_")
        write_svg(os.path.join(out, name + ".svg"), placed, w, hh, f"Корпус брелока {v.upper()}: акрил {t:g} мм")
        write_dxf(os.path.join(out, name + ".dxf"), placed)
        names = []
        for key, nm, _, _ in items:
            if nm not in names:
                names.append(nm)
        counts = {nm: sum(1 for _, n2, _, _ in items if n2 == nm) for nm in names}
        print(f"  {name}: {', '.join(nm + (f' ×{counts[nm]}' if counts[nm] > 1 else '') for nm in names)}"
              f" — лист {w:.0f} × {hh:.0f} мм")


def main():
    results = {}
    for v in VARIANTS:
        parts, solids, info = build(v)
        check(v, parts, solids, info)
        results[v] = (parts, numbers(v, parts, info))
    for v, (parts, nums) in results.items():
        write_variant(v, parts, nums)


if __name__ == "__main__":
    main()
