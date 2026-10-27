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
  верхняя рамка ячейки    3 … H          3, 4 или 5 мм; ушки под модулем вместо стоек
  нижняя рамка ячейки 3   0 … 3          окно под вилку USB-C
  плата 1.6 мм            −1.6 … 0
  рамка оборота 4 мм      −5.6 … −1.6    ушки под винтами, толкатель кнопки с крыльями под платой
  дно 2 мм                −7.6 … −5.6    прозрачное, вырез под вилку USB-C, отверстие над пищалкой

Модуль экрана лежит на ушках рамок ячейки: стопка рамок и есть «проставка», поэтому
её высота всегда совпадает с высотой рамок. Всё стягивают четыре винта M2 с гайками
на дне. В конце сборки модели идут проверки (check()): пересечения деталей, зазоры
до деталей платы, модуля, вилки USB-C и трубки микрофона, тонкие перемычки, ход
толкателя, длина винтов. Любая ошибка — выход с перечнем, файлы не пишутся.

Высоты считаются на номинальные листы. Листы акрила гуляют по толщине, поэтому
смерьте свои и пересчитайте:

  python3 case.py --sheet 3=2.86 --sheet 4=4.05 --module 1.6

--sheet НОМИНАЛ=МЕРА — лист, который продают как 3 мм, на деле 2.86; --module — толщина
платы модуля экрана. От них зависят высота над ячейкой, длина трубки и винтов.
"""
import argparse
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
SCREW_LENGTHS = (16, 18, 20, 22, 25, 30)         # ходовые длины стальных M2
# Нейлоновые M2 с полукруглой головкой обычно продают до 20 мм; M2 × 25 бывает с
# цилиндрической (Essentra 50M020040D025: полиамид 6.6, головка Ø 3.8).
NYLON_LENGTHS = (16, 20, 25)
# Винт выходит из гайки не меньше чем на SCREW_MIN_OUT на номинальных листах: это +0.2 мм
# на каждый из шести листов акрила под винтом. Лишнее откусывают после затяжки.
SCREW_MIN_OUT = 1.2
SCREW_TRIM_OUT = 1.0  # сколько оставить над гайкой, когда откусываете

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

# Ячейка: под ней каптон на спиленных лапках USB-C и тонкий двусторонний скотч, над ней —
# каптон на обороте модуля, чтобы углы SOT-23 и 0805 не продавили плёнку.
UNDER_CELL = 0.3
OVER_CELL = 0.1
SWELL = 0.3         # запас на вздутие ячейки с зарядом и возрастом
POCKET_W = 21.0     # гнездо ячейки в рамках: ячейка до 20.7 мм шириной с запасом 0.15
LEAD_W = 1.4        # канал для проводов ячейки справа от гнезда
LEAD_PATH = 0.6     # провода, прижатые к правому боку ячейки: полоса вдоль неё

# Вилка USB-C: корпус по стандарту не больше 12.35 × 6.5 мм. Ось гнезда HRO
# TYPE-C-31-M-12 (корпус 3.26 мм) на обороте — 1.63 мм от платы.
PLUG_W, PLUG_T, PLUG_GAP = 12.35, 6.5, 0.5
J1_AXIS = -1.6 - 1.63

# Микрофон U6 на обороте слушает через отверстие в плате. На лице до крышки идёт
# силиконовая трубка 1 × 2 мм: так он слышит наружу, а не всю полость корпуса.
# Трубка сдвинута от C4 на 0.15 мм, отверстие в плате остаётся внутри её канала.
TUBE_ID, TUBE_OD, TUBE_DX = 1.0, 2.0, -0.15
# Трубка длиннее промежутка плата — крышка и поджата с обоих концов. 0.6 мм покрывают
# разброс двух рамок ячейки, шайбы и платы модуля; мягкий силикон в держателях не гнётся.
TUBE_SQUEEZE = 0.6
GUIDE_D = 2.1       # отверстие держателя трубки в рамках
GUIDE_R = 2.55      # бобышка держателя: стенка 1.5 мм
MIC_D = 1.2         # отверстие в крышке над трубкой

# Толкатель кнопки (SW1 — B3U-3100P, ход 0.2 мм). Упор — стенка корпуса: толкатель
# выступает на BTN_OUT, палец останавливается на стенке.
BTN_REST = 0.1      # зазор до штока кнопки в покое
BTN_OUT = 0.5
SW_TRAVEL = 0.2
KERF_MAX = 0.3      # ширина реза, если мастерская её не вычитает
# Шток кнопки выступает за край платы всего на 0.17 мм, и вдавленный толкатель заходит
# под плату. Чтобы он не поднялся к плате и не упёрся в её край, по обе стороны кнопки
# у него крылья под платой: плата держит их сверху, дно снизу. Полка толкателя в кармане
# не тоньше MIN_WEB, поэтому стенка у кармана выходит наружу приливом.
BTN_WING = 1.5      # ширина крыльев по y
BTN_WING_IN = 1.2   # на сколько крылья заходят под плату в покое
BTN_WING_CH = 0.6   # срез наружного угла крыла: карман дальше от винта у кнопки
BTN_PLAY_Y = 0.1    # зазор толкателя в кармане по y на сторону
BOARD_TOL = 0.2     # край платы после фрезеровки гуляет на ±0.2
BTN_UNDER = 0.3     # крылья под платой при любом резе и самой маленькой плате

# Пищалка BZ1 на обороте звучит отверстием от платы, то есть в дно: в дне против неё отверстие.
BUZZER_D = 3.0

# Высоты деталей над платой, мм (остальные на лице — 1.0, на обороте — 1.1).
FRONT_H = {"U1": 2.2, "U5": 0.7, "BT1": 1.5}
BACK_H = {"J1": 3.26, "BZ1": 1.9, "M1": 2.0, "SW1": 1.6}

SHEET_T = {"cover": 2.0, "washers": 2.0, "frame_screen": 3.0, "frame_back": 4.0, "button": 4.0, "bottom": 2.0}
NUT_T = 1.6
BOARD_T = 1.6

# Измеренные толщины листов: номинал → мера (--sheet). Пусто — всё по номиналу.
# MEASURED — запуск с мерами: тогда то, что правят при сборке, — предупреждения, а не ошибки.
SHEET_REAL = {}
MEASURED = False

# Три версии под ячейки 20 × 25 мм разной толщины. Плата, прошивка и раскрой платы те же;
# меняется только верхняя рамка ячейки, а с ней высота под модулем и винты.
VARIANTS = {
    "s": {"cell": "402025", "mah": 150, "cell_t": 4.0, "mid": [3.0, 3.0]},
    "m": {"cell": "502025", "mah": 200, "cell_t": 5.0, "mid": [3.0, 4.0]},
    "l": {"cell": "602025", "mah": 250, "cell_t": 6.0, "mid": [3.0, 5.0]},
}

# Пороги проверок — отдельно от размеров построения: если кто-то уменьшит зазор
# выше, проверка это поймает.
NEED = {"детали": 0.25, "плата модуля": 0.4, "стекло": 0.25, "пластик гребёнки": 0.25, "штыри экрана": 0.2,
        "шлейф стекла": 0.2, "ячейка": 0.25, "вилка USB-C": 0.5, "трубка": 0.25, "держатель трубки": 0.03,
        "винты": 0.1, "оборот модуля": 0.0, "ячейка — винты": 0.2, "провода — винты": 0.2, "слой": 0.25,
        "площадки кнопки": 0.1, "пищалка": 0.5}

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


def cut_corner(x0, y0, x1, y1, ch, cy):
    """Прямоугольник со срезанным на ch углом у x0; cy — у какой стороны по y (y0 или y1)."""
    if cy == y0:
        return poly([(x0 + ch, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0 + ch)])
    return poly([(x0, y0), (x1, y0), (x1, y1), (x0 + ch, y1), (x0, y1 - ch)])


def moved(s, dx, dy=0.0):
    out = pcbnew.SHAPE_POLY_SET(s)
    out.Move(pcbnew.VECTOR2I(MM(dx), MM(dy)))
    return out


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
        if (isinstance(g, pcbnew.FP_SHAPE) and g.GetLayerName().endswith("Fab")
                and g.GetShape() == pcbnew.SHAPE_T_SEGMENT):
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
    # Корпус кнопки по Fab — прямоугольник во всю её ширину по y; шток торчит из него вправо.
    fab = fab_points(sw1)
    ys = [y for _, y in fab]
    sw_body = (min(x for x, _ in fab), min(ys), max(x for x, y in fab if y in (min(ys), max(ys))), max(ys))
    sw_pads = []
    for p in sw1.Pads():
        bb = p.GetBoundingBox()
        sw_pads.append(rect(pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()),
                            pcbnew.ToMM(bb.GetBottom())))
    return {
        "front": front, "back": back, "pads": pads, "screws": screws, "pins": pins,
        "port": pos(port) + (pcbnew.ToMM(port.GetDrillSize().x),),
        "sw_tip": max(x for x, _ in fab), "sw_y": pos(sw1)[1], "sw_body": sw_body, "sw_pads": sw_pads,
        "usb_face": max(y for _, y in fab_points(j1)), "usb_x": pos(j1)[0],
        "h1": pos(h1) + (pcbnew.ToMM(next(iter(h1.Pads())).GetDrillSize().x),),
        "bz": pos(board.FindFootprintByReference("BZ1")),
    }


# ---------------------------------------------------------------- модель

def real(t):
    """Толщина листа на деле: измеренная (--sheet) или номинальная."""
    return SHEET_REAL.get(t, t)


def stack(v):
    """z нижней грани, толщина на деле и номинал листа каждой детали версии v."""
    sheet = dict(SHEET_T)
    for i, ti in enumerate(VARIANTS[v]["mid"]):
        sheet[f"frame_cell{i + 1}"] = ti
    t = {key: real(s) for key, s in sheet.items()}
    z = {}
    z0 = 0.0
    for i in range(len(VARIANTS[v]["mid"])):
        key = f"frame_cell{i + 1}"
        z[key] = z0
        z0 += t[key]
    h = z0
    z.update({
        "cover": h + MODULE_T + t["washers"], "washers": h + MODULE_T, "frame_screen": h,
        "frame_back": -BOARD_T - t["frame_back"], "button": -BOARD_T - t["button"],
        "bottom": -BOARD_T - t["frame_back"] - t["bottom"],
    })
    return h, z, t, sheet


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
    h, Z, T, S = stack(v)
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
        parts[key] = {"shape": shape, "circles": circles, "z": Z[key], "t": T[key], "sheet": S[key], "name": name,
                      "color": color[0], "opacity": color[1]}

    # Крышка и дно; в дне — отверстие напротив пищалки.
    bz = b["bz"]
    add("cover", outer, [(x, y, SCREW_D) for x, y in screws] + [(tube_c[0], tube_c[1], MIC_D)], "крышка", CLEAR)
    add("bottom", minus(outer, usb_cut), [(x, y, SCREW_D) for x, y in screws] + [(bz[0], bz[1], BUZZER_D)],
        "дно", CLEAR)

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
        where = "нижняя" if i == 0 else "верхняя"
        add(key, minus(outer, closed(cut, a)), [guide_hole] if guide else [], f"{where} рамка ячейки", COLOR)

    # Рамка оборота: ушки под винтами вместо шайб, карман и щель толкателя, окно USB-C.
    sw_crt = bbox(b["back"]["SW1"][0])
    btn_y = b["sw_y"]
    x0 = b["sw_tip"] + BTN_REST
    # Полка толкателя x0 … pocket_out не тоньше MIN_WEB; стенка за ней — тоже MIN_WEB,
    # поэтому у кнопки рамка оборота выходит приливом за край корпуса.
    pocket_out = max(W + WALL - MIN_WEB, math.ceil((x0 + MIN_WEB) * 10 - 1e-6) / 10)
    boss_out = pocket_out + MIN_WEB
    # Крылья — по обе стороны двора кнопки, с зазором от её площадок, и под платой на
    # BTN_WING_IN; карман пускает их внутрь на весь ход до стенки с запасом BTN_PLAY_Y.
    xw = W - BTN_WING_IN
    wings_y = ((sw_crt[1] - BTN_PLAY_Y - BTN_WING, sw_crt[1] - BTN_PLAY_Y),
               (sw_crt[3] + BTN_PLAY_Y, sw_crt[3] + BTN_PLAY_Y + BTN_WING))
    wing_pockets = [inflated(cut_corner(xw - BTN_OUT, y0, pocket_out - BTN_PLAY_Y, y1, BTN_WING_CH, y0 if i == 0 else y1),
                             BTN_PLAY_Y) for i, (y0, y1) in enumerate(wings_y)]
    btn_pocket = union(rect(sw_crt[0] - GAP, btn_y - 2.8, pocket_out, btn_y + 2.8), *wing_pockets)
    btn_slot = rect(pocket_out - 0.05, btn_y - 1.3, boss_out + 1, btn_y + 1.3)
    by0, by1 = wings_y[0][0] - BTN_PLAY_Y - MIN_WEB, wings_y[1][1] + BTN_PLAY_Y + MIN_WEB
    boss = rounded(W + WALL - 2 * R, by0, boss_out, by1, boss_out - W - WALL)
    keep = [btn_pocket, btn_slot] + [inflated(crt, GAP) for crt, _ in b["back"].values()]
    if needs_window(Z["frame_back"], T["frame_back"]):
        keep.append(usb_cut)
    cut = union(minus(inner_body, ears), *keep)
    add("frame_back", minus(union(outer, boss), closed(cut, a)), [(x, y, SCREW_D) for x, y in screws],
        "рамка оборота", COLOR)

    # Толкатель: полка в кармане держит его, шток выходит из прилива на BTN_OUT, крылья
    # под платой не дают ему подняться к плате.
    wings = [cut_corner(xw, y0, x0 + 0.01, y1, BTN_WING_CH, y0 if i == 0 else y1) for i, (y0, y1) in enumerate(wings_y)]
    plunger = union(rect(x0, wings_y[0][0], pocket_out, wings_y[1][1]), *wings,
                    rect(pocket_out - 0.01, btn_y - 1.1, boss_out + BTN_OUT, btn_y + 1.1))
    add("button", plunger, [], "толкатель кнопки", ("#ffd23d", 0.9))

    # Твёрдые тела вокруг: (z0, z1, форма, зазор, кому можно ближе).
    top = Z["cover"] + T["cover"]
    guide = NEED["держатель трубки"]
    module_back = minus(module, *[circle(x, y, MODULE_FREE_R) for x, y in mod_holes])
    plug_need = NEED["вилка USB-C"]
    solids = {
        "плата модуля": (h, h + MODULE_T, module, NEED["плата модуля"], {}),
        "стекло": (h + MODULE_T, h + MODULE_T + GLASS_T, glass, NEED["стекло"], {}),
        "детали оборота модуля": (h - MODULE_BACK[1], h, module_back, NEED["оборот модуля"], {}),
        "пластик гребёнки": (h - HEADER_PLASTIC, h, plastic, NEED["пластик гребёнки"], {}),
        "штыри экрана": (-1.6 - 1.0, h + MODULE_T + PIN_TOP, pin_pads, NEED["штыри экрана"], {}),
        "шлейф стекла": (fpc_z[0], fpc_z[1], fpc, NEED["шлейф стекла"], {}),
        "ячейка": (UNDER_CELL, UNDER_CELL + cfg["cell_t"] + SWELL, cell, NEED["ячейка"], {}),
        "вилка USB-C": (plug_z[0] - plug_need, plug_z[1] + plug_need, plug, plug_need, {}),
        "трубка микрофона": (0.0, Z["cover"], tube, NEED["трубка"],
                             {"frame_screen": guide, "frame_cell2": guide, "frame_cell3": guide}),
        "винты": (Z["bottom"] - NUT_T, top, union(*[circle(x, y, SHAFT_D / 2) for x, y in screws]), NEED["винты"], {}),
    }
    for ref, (crt, hgt) in b["front"].items():
        if ref != "J2":
            solids[ref] = (0.0, hgt, crt, NEED["детали"], {})
    for ref, (crt, hgt) in b["back"].items():
        # Толкатель и кнопку проверяет ход толкателя в check(): по корпусу и площадкам кнопки, кроме штока.
        solids[ref] = (-1.6 - hgt, -1.6, crt, NEED["детали"], {"button": None} if ref == "SW1" else {})

    info = {
        "board": b, "h": h, "module": (mx0, my0, o["w"], o["h"]), "mod_holes": mod_holes, "glass": glass,
        "tube_c": tube_c, "tube_len": Z["cover"] + TUBE_SQUEEZE, "pocket": bbox(pocket), "cell": cell,
        "x0": x0, "pocket_out": pocket_out, "boss_out": boss_out, "btn_y": btn_y, "plug_z": plug_z, "top": top,
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


def plunger_at(shape, k, s=0.0, dy=0.0):
    """Толкатель при резе k, вдавленный на s. Рез сужает его на k/2 с каждой стороны, а
    стенка кармана от реза отходит на k/2: в покое, на полке у стенки, он сдвинут наружу на k."""
    return moved(inflated(shape, -k / 2), k - s, dy)


def plunger_sweep(shape, k, s1, dys=(0.0,), step=0.05):
    """Всё место, которое толкатель проходит от покоя до хода s1, со сдвигами dy по y."""
    n = max(1, math.ceil(s1 / step - 1e-9))
    return union(*[plunger_at(shape, k, s1 * i / n, dy) for i in range(n + 1) for dy in dys])


def check(v, parts, solids, info):
    """Проверки версии v: (ошибки, предупреждения). Предупреждения бывают только на измеренных
    листах — то, что правят при сборке: подшлифовать рамку, взять ячейку тоньше."""
    err, warn = [], []
    soft = warn if MEASURED else err
    cfg = VARIANTS[v]
    h = info["h"]
    b = info["board"]

    def material(p):
        return minus(p["shape"], holes_of(p["circles"]))

    # Толщины листов и высоты.
    for key, p in parts.items():
        if p["sheet"] not in COMMON_SHEETS:
            err.append(f"{p['name']}: лист {p['sheet']:g} мм редкий")
    if abs(sum(real(t) for t in cfg["mid"]) - h) > 1e-9:
        err.append("стопка рамок ячейки не равна высоте под модулем")
    gap = parts["cover"]["z"] - (parts["frame_screen"]["z"] + parts["frame_screen"]["t"])
    if not 0.1 - 1e-9 <= gap <= 0.3 + 1e-9:
        fix = "подшлифуйте её или клейте без скотча" if gap < 0.1 else "скотч на её верхней грани возьмите толще"
        soft.append(f"рамка экрана ниже крышки на {gap:.2f} мм, нужно 0.1…0.3: {fix}")
    if parts["cover"]["z"] - (h + MODULE_T + GLASS_T) < 0.5 - 1e-9:
        soft.append("над стеклом меньше 0.5 мм: шайбы режьте из листа потолще")
    if parts["cover"]["z"] - (h + MODULE_T + PIN_TOP) < 0.15 - 1e-9:
        soft.append("штыри над модулем ближе 0.15 мм к крышке: укоротите их над модулем")
    if max(hgt for _, hgt in b["back"].values()) > parts["frame_back"]["t"] - 0.5 + 1e-9:
        soft.append("деталь оборота ближе 0.5 мм к дну: рамку оборота режьте из листа потолще")
    # Ячейка своей версии входит хотя бы при низких деталях на обороте модуля.
    cell_max = h - MODULE_BACK[0] - UNDER_CELL - OVER_CELL - SWELL
    if cfg["cell_t"] > cell_max + 1e-9:
        soft.append(f"ячейка {cfg['cell']} ({cfg['cell_t']:g} мм) не входит даже при деталях модуля "
                    f"{MODULE_BACK[0]:g} мм: можно не толще {cell_max:.2f}, нужна версия на ступень выше")

    # Перемычки и стенки вокруг отверстий.
    for key in ("frame_screen", "frame_cell1", "frame_cell2", "frame_cell3", "frame_back", "button"):
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

    # Толкатель: полка не тоньше MIN_WEB; вдавленный до стенки, он дожимает кнопку на весь ход
    # при любом резе от 0 до KERF_MAX и не давит дальше 0.35 мм после упора кнопки.
    tip = b["sw_tip"]
    if info["pocket_out"] - info["x0"] < MIN_WEB - 1e-6:
        err.append(f"полка толкателя тоньше {MIN_WEB:g} мм")
    for k in (0.0, KERF_MAX):
        press = (BTN_OUT + k) - (info["x0"] - tip + 1.5 * k) - SW_TRAVEL
        if not 0.0 <= press <= 0.35:
            err.append(f"толкатель при резе {k:g} мм дожимает кнопку на {press:.2f} мм, нужно 0…0.35")
    # Толкатель и плата. Шток кнопки выступает за край платы меньше хода, поэтому дожатый
    # толкатель заходит под плату. Его крылья с обеих сторон кнопки должны быть под платой при
    # любом резе: иначе он поднимется к плате (выше над ним только нижняя рамка ячейки) и упрётся
    # в её край раньше, чем дожмёт кнопку.
    btn = parts["button"]["shape"]
    btn_top = parts["button"]["z"] + parts["button"]["t"]
    board = rounded(0, 0, W, H, R)
    held_zone = inflated(board, -BOARD_TOL - BTN_UNDER)
    sides = (rect(W / 2, -10, W + 10, info["btn_y"]), rect(W / 2, info["btn_y"], W + 10, H + 10))
    fb = material(parts["frame_back"])
    sw_body = rect(*b["sw_body"])
    for k in (0.0, KERF_MAX):
        rest = plunger_at(btn, k)
        held = all(area(intersect(intersect(rest, held_zone), side)) > 1e-3 for side in sides)
        if not held:
            err.append(f"толкатель при резе {k:g} мм: крылья заходят под плату меньше {BTN_UNDER:g} мм "
                       f"(край платы ±{BOARD_TOL:g}) — толкатель поднимется к плате")
        lift = 0.0 if held else parts["frame_cell1"]["z"] - btn_top
        s_need = (info["x0"] + 1.5 * k) - (tip - SW_TRAVEL)
        if btn_top + lift > -BOARD_T + 1e-6 and \
                area(intersect(plunger_sweep(btn, k, s_need), inflated(board, BOARD_TOL))) > 1e-4:
            err.append(f"толкатель при резе {k:g} мм упрётся в край платы раньше, чем дожмёт кнопку")
        # Весь ход до стенки и люфт по y: мимо корпуса и площадок кнопки (шток, на который он
        # давит, не в счёт) и деталей оборота; карман пускает его до стенки.
        play = BTN_PLAY_Y + k
        swept = plunger_sweep(btn, k, BTN_OUT + k, (-play, 0.0, play))
        if area(intersect(swept, inflated(sw_body, NEED["детали"] - 0.01))) > 1e-3:
            err.append(f"толкатель при резе {k:g} мм на ходе ближе {NEED['детали']:g} мм к корпусу кнопки")
        if any(area(intersect(swept, inflated(pad, NEED["площадки кнопки"] - 0.01))) > 1e-4 for pad in b["sw_pads"]):
            err.append(f"толкатель при резе {k:g} мм на ходе ближе {NEED['площадки кнопки']:g} мм к площадкам кнопки")
        for ref, (crt, _) in b["back"].items():
            if ref != "SW1" and area(intersect(swept, inflated(crt, NEED["детали"] - 0.01))) > 1e-3:
                err.append(f"толкатель при резе {k:g} мм на ходе ближе {NEED['детали']:g} мм к {ref}")
        pushed = plunger_at(btn, k, BTN_OUT + k)
        if area(intersect(inflated(pushed, -0.005), inflated(fb, -k / 2))) > 1e-3:
            err.append(f"толкатель при резе {k:g} мм упирается в рамку оборота раньше, чем в стенку")

    # Ячейка в гнезде и её провода не трутся о винты. Провода идут вниз вдоль правого края
    # ячейки (полоса LEAD_PATH) к площадкам BT1: там вырез во всех рамках ячейки.
    shafts = union(*[circle(x, y, SHAFT_D / 2 + NEED["ячейка — винты"] - 0.01) for x, y in b["screws"]])
    if area(intersect(rect(*info["pocket"]), shafts)) > 1e-3:
        err.append("гнездо ячейки ближе 0.2 мм к винтам")
    cx0, cy0, cx1, cy1 = bbox(info["cell"])
    lead = rect(cx1, cy0, cx1 + LEAD_PATH, bbox(b["front"]["BT1"][0])[1])
    shafts = union(*[circle(x, y, SHAFT_D / 2 + NEED["провода — винты"] - 0.01) for x, y in b["screws"]])
    if area(intersect(lead, shafts)) > 1e-4:
        err.append(f"провода ячейки вдоль её правого края ближе {NEED['провода — винты']:g} мм к винту")
    for key in ("frame_cell1", "frame_cell2", "frame_cell3"):
        if key in parts and area(intersect(lead, material(parts[key]))) > 1e-4:
            err.append(f"{parts[key]['name']}: нет места проводам ячейки вдоль её правого края")

    # Пищалка: в дне против неё отверстие, и рамка оборота его не закрывает.
    bx, by = b["bz"]
    if area(intersect(material(parts["bottom"]), circle(bx, by, BUZZER_D / 2 - 0.05))) > 1e-4:
        err.append("в дне нет отверстия против пищалки BZ1")
    if area(intersect(fb, circle(bx, by, BUZZER_D / 2 + NEED["пищалка"]))) > 1e-4:
        err.append(f"рамка оборота ближе {NEED['пищалка']:g} мм к отверстию пищалки")

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
        for x, y in (b["screws"] if p else []):
            ring = minus(circle(x, y, 2.1), circle(x, y, SCREW_D / 2))
            if area(intersect(material(p), ring)) < 4.0:
                err.append(f"{p['name']}: под винтом ({x:.1f}, {y:.1f}) мало опоры")

    # Винт с запасом SCREW_MIN_OUT над гайкой есть среди ходовых.
    need = info["top"] - parts["bottom"]["z"] + NUT_T
    for lengths, what in ((SCREW_LENGTHS, "стальных"), (NYLON_LENGTHS, "нейлоновых")):
        if not any(n >= need + SCREW_MIN_OUT - 1e-9 for n in lengths):
            err.append(f"среди {what} M2 нет винта на {need + SCREW_MIN_OUT:.1f} мм и длиннее")

    # Трубка: отверстие в плате внутри её канала, площадки лица не под её стенкой.
    tx, ty = info["tube_c"]
    px, py, pd = b["port"]
    if math.hypot(px - tx, py - ty) + pd / 2 > TUBE_ID / 2 - 0.05:
        err.append("отверстие микрофона в плате выходит за канал трубки")
    for ref, pad in b["pads"]:
        if area(intersect(pad, circle(tx, ty, TUBE_OD / 2 + 0.1))) > 1e-4:
            err.append(f"трубка микрофона ближе 0.1 мм к площадке {ref}")

    # Модуль экрана: отверстия сходятся с отверстиями платы.
    if [tuple(p) for p in info["mod_holes"]] != [tuple(p) for p in b["screws"]]:
        err.append(f"отверстия модуля {info['mod_holes']} не на отверстиях J2 {b['screws']}")
    return err, warn


def numbers(v, parts, info):
    """Размеры версии, которые нужны README и просмотру."""
    h = info["h"]
    top, low = info["top"], parts["bottom"]["z"]
    need = top - low + NUT_T
    nylon = min(n for n in NYLON_LENGTHS if n >= need + SCREW_MIN_OUT - 1e-9)
    steel = min(n for n in SCREW_LENGTHS if n >= need + SCREW_MIN_OUT - 1e-9)
    below = h + BOARD_T + PIN_OUT_MIN
    std = HEADER_PLASTIC + HEADER_TAIL
    free = [h - m for m in MODULE_BACK]
    cell_max = [f - UNDER_CELL - OVER_CELL - SWELL for f in free]
    # Толкатель: зазор в покое растёт на 1.5 ширины реза, выступ — на одну.
    press = [(BTN_OUT + k) - (BTN_REST + 1.5 * k) - SW_TRAVEL for k in (0.0, KERF_MAX)]
    return {
        "standoff": round(h, 3), "module_t": MODULE_T, "thickness": round(top - low, 2),
        "screw": nylon, "screw_out": round(nylon - need, 2), "screw_steel": steel,
        "screw_steel_out": round(steel - need, 2), "screw_need": round(need, 2),
        "screw_cut": round(need + SCREW_TRIM_OUT, 2), "below_module": round(below, 2),
        "std_header_margin": round(std - below, 2),
        "long_header": std - below < 0.3, "std_header_out": round(std - h - BOARD_T, 2),
        "long_header_out": round(LONG_HEADER - HEADER_SHORT - h - BOARD_T, 1),
        "free": [round(f, 2) for f in free], "cell_max": [round(c_, 2) for c_ in cell_max],
        "cell_width_max": round(POCKET_W - 0.3, 2), "tube_len": round(info["tube_len"], 1),
        "press": [round(p, 2) for p in press], "tube": [round(c_, 3) for c_ in info["tube_c"]],
        "sheets": sorted({p["sheet"] for p in parts.values()}),
    }


def buy(v, n):
    """Что купить к корпусу версии v — для README и просмотра."""
    cfg = VARIANTS[v]
    frames = ", ".join(f"{t:g}" for t in n["sheets"] if t != 2.0)
    screw = (f"Винты M2 × {n['screw']} нейлоновые с цилиндрической или полукруглой головкой, не потайные, и гайки M2, "
             f"лучше тоже нейлоновые, — по 4 шт. Винт выйдет из гайки на {n['screw_out']:g} мм: после затяжки "
             f"откусите, оставив около {SCREW_TRIM_OUT:g} мм. Нейлоновых не нашлось — подойдут стальные "
             f"M2 × {n['screw_steel']}, но радио у антенны станет чуть слабее")
    if n["long_header"]:
        header = (f"Модуль экрана без впаянной гребёнки и длинная гребёнка 2.54 на 4 штыря, 15 мм и длиннее "
                  f"(штыри выйдут из платы на {n['long_header_out']:g} мм, лишнее откусить)")
    else:
        header = f"Модуль экрана с обычной гребёнкой 2.54 (штыри выйдут из платы на {n['std_header_out']:g} мм)"
    sheets = "по вашим листам" if MEASURED else "на номинальных листах, свои листы смерьте"
    return [
        f"Акрил 2 мм прозрачный (крышка, дно, шайбы) и {frames} мм на рамки, можно цветной",
        screw,
        f"Ячейка {cfg['cell']} с платой защиты: толщина не больше {n['cell_max'][1]:g}…{n['cell_max'][0]:g} мм "
        f"(смотря по модулю, {sheets}), ширина до {n['cell_width_max']:g} мм, длина до 25.0 мм",
        header,
        f"Силиконовая трубка 1 × 2 мм, кусок {n['tube_len']:g} мм — к микрофону; длиннее можно, короче нельзя",
        "Каптоновый скотч (лапки USB-C, пайка ячейки, оборот модуля над ней) и тонкий двусторонний скотч 0.1–0.15 мм",
    ]


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
            lines += ["0", "CIRCLE", "8", "CUT", "10", f"{x + dx:.4f}", "20", f"{-(y + dy):.4f}",
                      "40", f"{dia / 2:.4f}"]
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
    variant = {"id": v, **{k: cfg[k] for k in ("cell", "mah", "cell_t", "mid")}, **nums}
    if SHEET_REAL:
        variant["measured"] = {f"{t:g}": m for t, m in sorted(SHEET_REAL.items())}
    model = {"board": {"w": W, "h": H, "t": BOARD_T}, "variant": variant, "layers": []}
    by_t = {}
    for key, p in parts.items():
        model["layers"].append({"id": key, "name": p["name"], "z": round(p["z"], 3), "t": p["t"], "color": p["color"],
                                "opacity": p["opacity"], "shapes": outlines(p["shape"]),
                                "circles": [[round(c, 3) for c in cc] for cc in p["circles"]]})
        for rings, circles in pieces(p):
            by_t.setdefault(p["sheet"], []).append((key, p["name"], rings, circles))
    # Запасные нижние шайбы: у них между отверстием и лыской всего 0.5 мм акрила.
    for rings, circles in pieces(parts["washers"]):
        if circles and circles[0][1] > H / 2:
            by_t[parts["washers"]["sheet"]].append(("washers_spare", "запасная нижняя шайба", rings, circles))
    model["layers"].sort(key=lambda L: -L["z"])
    with open(os.path.join(out, "case-model.json"), "w") as f:
        json.dump(model, f, ensure_ascii=False)

    print(f"Версия {v.upper()}: ячейка {cfg['cell']} (~{cfg['mah']} мА·ч), под модулем {nums['standoff']:g} мм "
          f"(рамки ячейки {' + '.join(f'{real(t):g}' for t in cfg['mid'])}), толщина {nums['thickness']:.1f} мм, "
          f"винты M2 × {nums['screw']} нейлон или M2 × {nums['screw_steel']} сталь (нужно {nums['screw_need']:g}, "
          f"из гайки выйдет {nums['screw_out']:g} и {nums['screw_steel_out']:g}) → {os.path.relpath(out, HERE)}/")
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


def parse_args(argv):
    ap = argparse.ArgumentParser(description="Корпус брелока: проверки и файлы резки S, M, L.")
    ap.add_argument("--sheet", action="append", default=[], metavar="НОМИНАЛ=МЕРА",
                    help="толщина листа по штангенциркулю, например 3=2.86; можно несколько раз или через запятую")
    ap.add_argument("--module", type=float, metavar="ММ", help=f"толщина платы модуля экрана (по умолчанию {MODULE_T:g})")
    args = ap.parse_args(argv)
    sheets = {}
    for item in ",".join(args.sheet).split(","):
        if not item.strip():
            continue
        nom, sep, meas = item.partition("=")
        try:
            nom, meas = float(nom), float(meas)
        except ValueError:
            ap.error(f"--sheet {item}: нужно НОМИНАЛ=МЕРА, например 3=2.86")
        if not sep or nom not in COMMON_SHEETS:
            ap.error(f"--sheet {item}: номинал — один из {', '.join(f'{t:g}' for t in COMMON_SHEETS)} мм")
        if abs(meas - nom) > 0.5:
            ap.error(f"--sheet {item}: мера отличается от номинала больше чем на 0.5 мм — опечатка?")
        sheets[nom] = meas
    if args.module is not None and not 0.8 <= args.module <= 2.0:
        ap.error(f"--module {args.module:g}: плата модуля бывает 1.0–1.6 мм")
    return sheets, args.module


def main(argv=None):
    global MODULE_T, MEASURED
    sheets, module = parse_args(argv)
    SHEET_REAL.update(sheets)
    if module is not None:
        MODULE_T = module
    MEASURED = bool(sheets) or module is not None
    if MEASURED:
        print("По вашим мерам: " + ", ".join([f"лист {t:g} мм — {m:g}" for t, m in sorted(sheets.items())]
                                            + ([f"плата модуля {MODULE_T:g} мм"] if module is not None else [])))
    results = {}
    failed = False
    for v in VARIANTS:
        parts, solids, info = build(v)
        err, warn = check(v, parts, solids, info)
        for w in warn:
            print(f"Версия {v.upper()}: внимание: {w}")
        if err:
            print(f"Версия {v.upper()}: проверки не прошли:")
            for e in err:
                print("  -", e)
            failed = True
            continue
        nums = numbers(v, parts, info)
        results[v] = (parts, {**nums, "buy": buy(v, nums)})
    if failed:
        sys.exit(1)
    for v, (parts, nums) in results.items():
        write_variant(v, parts, nums)


if __name__ == "__main__":
    main()
