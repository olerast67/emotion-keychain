"""Плата rev B: расстановка, трассировка, проверка и файлы для заказа.

Плата в репозитории уже разведена и проверена, а файлы для заказа лежат в out/. Для заказа
ничего запускать не нужно. Скрипты нужны, только чтобы изменить плату:

    python3 hardware/rev-b/gen/build.py place --force  # расставить детали заново, БЕЗ дорожек
    python3 hardware/rev-b/gen/build.py route   # переходные, шины питания, Freerouting, доводка, DRC
    python3 hardware/rev-b/gen/build.py finish  # доводка разведённой платы: паста, шёлк, отверстия
    python3 hardware/rev-b/gen/build.py fab     # DRC, Gerber, BOM и CPL, файлы PCBWay, панель, превью
    python3 hardware/rev-b/gen/build.py all --force

place заменяет разведённую плату платой без дорожек, поэтому без --force отказывается, если
в плате уже есть дорожки. route сначала проверяет Java и Freerouting и только потом трогает
плату. fab сам прогоняет DRC и не пишет файлы для заказа, если есть ошибки, несоединённые
площадки или в плате нет дорожек. Вернуть плату и файлы из репозитория:
git checkout -- hardware/rev-b/keychain-rev-b.kicad_pcb hardware/rev-b/Keychain.pretty hardware/rev-b/out

Нужны KiCad 7 (pcbnew для Python и kicad-cli); для route — Java 21 и Freerouting 2.1
(путь к freerouting.jar — в FREEROUTING_JAR); для панели в fab — KiKit 1.5 (см. panel.py).
"""
import csv
import json
import math
import os
import re
import shutil
import subprocess
import sys
import zipfile

import pcbnew

import footprints
import netlist
import fixroute
import powerroute

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.normpath(os.path.join(HERE, ".."))
NAME = "keychain-rev-b"
PCB = os.path.join(REV, NAME + ".kicad_pcb")
PRO = os.path.join(REV, NAME + ".kicad_pro")
DRU = os.path.join(REV, NAME + ".kicad_dru")
OUT = os.path.join(REV, "out")
WORK = os.path.join(REV, "out", "work")
LIB = os.path.join(REV, "Keychain.pretty")
KICAD_FP = os.environ.get("KICAD7_FOOTPRINT_DIR", "/usr/share/kicad/footprints")

# Плата: 30 × 46 мм, 4 слоя, 1.6 мм: детали и сигналы / сплошная земля / питание
# широкими шинами и земля / детали и сигналы. Верхние 18 мм — ушко: кольцо, модуль nRF,
# датчики; ниже — модуль экрана на высоте батареи.
W, H, R = netlist.BOARD_W, netlist.BOARD_H, 2.0
MM = pcbnew.FromMM

# Отверстия разных цепей — не ближе 0.5 мм край к краю: норма JLCPCB (той же цепи — 0.254 мм).
# В общих правилах проекта такой нормы нет, она — своим правилом в .kicad_dru.
HOLE_GAP = 0.5
DRU_TEXT = """(version 1)
(rule "JLCPCB hole to hole, different nets"
  (constraint hole_to_hole (min {gap}mm))
  (condition "A.Net != B.Net"))
""".format(gap=HOLE_GAP)


def V(x, y):
    return pcbnew.VECTOR2I(MM(x), MM(y))


def load_fp(libref):
    lib, name = libref.split(":")
    path = LIB if lib == "Keychain" else os.path.join(KICAD_FP, lib + ".pretty")
    fp = pcbnew.FootprintLoad(path, name)
    if fp is None:
        sys.exit(f"нет посадочного места {libref}")
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


# ---------------------------------------------------------------- проект и правила

def write_project():
    classes = []
    for name, c in netlist.NETCLASSES.items():
        classes.append({
            "name": name, "clearance": c["clearance"], "track_width": c["track"],
            "via_diameter": c["via"][0], "via_drill": c["via"][1],
            "diff_pair_gap": 0.2, "diff_pair_via_gap": 0.25, "diff_pair_width": 0.2,
            "microvia_diameter": 0.3, "microvia_drill": 0.1, "bus_width": 12, "wire_width": 6,
            "line_style": 0, "pcb_color": "rgba(0, 0, 0, 0.000)", "schematic_color": "rgba(0, 0, 0, 0.000)",
        })
    patterns = [{"netclass": name, "pattern": net}
                for name, c in netlist.NETCLASSES.items() for net in c["nets"]]
    # Нормы JLCPCB и PCBWay для 4 слоёв: дорожка/зазор от 0.09–0.1 мм, отверстие переходного
    # от 0.2 мм, поясок от 0.15 мм (PCBWay). Переходные 0.55/0.25 проходят у обоих.
    rules = {
        "min_clearance": 0.1, "min_track_width": 0.1, "min_via_diameter": 0.5, "min_via_annular_width": 0.15,
        "min_through_hole_diameter": 0.25, "min_hole_clearance": 0.2, "min_hole_to_hole": 0.25,
        "min_copper_edge_clearance": 0.3, "min_silk_clearance": 0.0, "min_text_height": 0.6,
        "min_text_thickness": 0.1, "solder_mask_clearance": 0.0, "solder_mask_min_width": 0.0,
        "min_microvia_diameter": 0.2, "min_microvia_drill": 0.1, "min_resolved_spokes": 1,
        "min_connection": 0.0, "use_height_for_length_calcs": True,
    }
    severities = {
        # Шёлк поверх площадок обрезает сам завод, текст у края — тоже не беда.
        "silk_overlap": "ignore", "silk_over_copper": "warning", "silk_edge_clearance": "ignore",
        "text_height": "warning", "text_thickness": "warning",
        "lib_footprint_issues": "ignore", "lib_footprint_mismatch": "ignore",
    }
    pro = {
        "board": {"design_settings": {"meta": {"version": 2}, "rules": rules, "rule_severities": severities,
                                      "defaults": {"board_outline_line_width": 0.1, "copper_line_width": 0.2,
                                                   "silk_line_width": 0.15, "silk_text_size_h": 0.8,
                                                   "silk_text_size_v": 0.8, "silk_text_thickness": 0.15},
                                      "track_widths": [], "via_dimensions": [], "diff_pair_dimensions": []}},
        "meta": {"filename": NAME + ".kicad_pro", "version": 1},
        "net_settings": {"classes": classes, "meta": {"version": 3}, "net_colors": None,
                         "netclass_assignments": None, "netclass_patterns": patterns},
        "pcbnew": {"page_layout_descr_file": ""},
        "sheets": [], "text_variables": {},
    }
    with open(PRO, "w") as f:
        json.dump(pro, f, indent=2, ensure_ascii=False)
    write_dru()


def write_dru(path=DRU):
    """Свои правила DRC рядом с проектом: KiCad читает их вместе с .kicad_pro."""
    with open(path, "w") as f:
        f.write(DRU_TEXT)


# ---------------------------------------------------------------- геометрия

def add_outline(board):
    def seg(a, b):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(V(*a))
        s.SetEnd(V(*b))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)

    def arc(c, start):
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_ARC)
        s.SetCenter(V(*c))
        s.SetStart(V(*start))
        s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)

    # Ушко для кольца — посередине сверху, отдельным выступом над платой.
    tx0, tx1, tr, tcy = W / 2 - netlist.TAB_R, W / 2 + netlist.TAB_R, netlist.TAB_R, netlist.TAB_CY
    seg((R, 0), (tx0, 0))
    seg((tx0, 0), (tx0, tcy))
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetCenter(V(W / 2, tcy))
    s.SetStart(V(tx0, tcy))
    s.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(180, pcbnew.DEGREES_T), True)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)
    seg((tx1, tcy), (tx1, 0))
    seg((tx1, 0), (W - R, 0))
    seg((W, R), (W, H - R))
    seg((W - R, H), (R, H))
    seg((0, H - R), (0, R))
    arc((W - R, R), (W - R, 0))
    arc((W - R, H - R), (W, H - R))
    arc((R, H - R), (R, H))
    arc((R, R), (0, R))


def polygon_zone(board, pts, layers, net=None, rule=False, name="", priority=0):
    z = pcbnew.ZONE(board)
    ls = pcbnew.LSET()
    for layer in layers:
        ls.AddLayer(layer)
    z.SetLayerSet(ls)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in pts:
        ol.Append(MM(x), MM(y))
    if rule:
        z.SetIsRuleArea(True)
        z.SetDoNotAllowCopperPour(True)
        z.SetDoNotAllowTracks(True)
        z.SetDoNotAllowVias(True)
        z.SetDoNotAllowPads(False)
        z.SetDoNotAllowFootprints(False)
        z.SetZoneName(name)
    else:
        z.SetNet(net)
        z.SetAssignedPriority(priority)
        z.SetLocalClearance(MM(0.2))
        z.SetMinThickness(MM(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetThermalReliefGap(MM(0.2))
        z.SetThermalReliefSpokeWidth(MM(0.25))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    board.Add(z)
    return z


def circle_pts(cx, cy, r, n=32):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


CU = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)
LAYER_NAMES = {pcbnew.F_Cu: "F.Cu", pcbnew.In1_Cu: "In1.Cu", pcbnew.In2_Cu: "In2.Cu", pcbnew.B_Cu: "B.Cu"}
EDGE_CLEAR = 0.35  # мм: медь от края платы у роутера (DRC требует 0.3)


def module_box():
    side, x, y, rot = netlist.PLACE["U1"]
    return x, y


def add_keepouts(board):
    mx, my = module_box()
    # Антенна модуля: ни меди, ни дорожек на всех слоях — зона из документации Raytac. Шире
    # (вниз вдоль правого края) не делаем: там единственный проход сигналов от модуля к SWD
    # и кнопке; угол платы и так открыт с двух сторон.
    polygon_zone(board, [(mx + 4.0, -1), (W + 1, -1), (W + 1, my + 6.2), (mx + 4.0, my + 6.2)], CU,
                 rule=True, name="antenna")
    # Под корпусом модуля на лицевой стороне — ни дорожек, ни переходных, ни заливки: земля модуля
    # у него внутри, а под ним по всей площади сплошной слой земли L2.
    polygon_zone(board, [(mx - 5.6, my - 3.25), (mx + 3.25, my - 3.25), (mx + 3.25, my + 3.25),
                         (mx - 5.6, my + 3.25)], [pcbnew.F_Cu], rule=True, name="module")
    # Кольцо брелока.
    side, hx, hy, rot = netlist.PLACE["H1"]
    polygon_zone(board, circle_pts(hx, hy, 3.0), CU, rule=True, name="ring")


def add_planes(board):
    """Земля — сплошным слоем L2 и заливкой всего свободного места на L1, L3 и L4."""
    pts = [(0.0, 0.0), (W, 0.0), (W, H), (0.0, H)]
    for layer in (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu):
        polygon_zone(board, pts, [layer], net=board.FindNet("GND"), priority=0)


def has_copper_zones(board):
    return any(not z.GetIsRuleArea() for z in board.Zones())


SILK_LINE = footprints.SILK_LINE  # мм: линия знаков на шёлке, минимум JLCPCB 0.153


def add_texts(board):
    """Надписи на шёлке из netlist.SILK_*: высота SILK_H, линия SILK_LINE."""
    def text(s, x, y, layer, width, mirrored=False):
        t = pcbnew.PCB_TEXT(board)
        t.SetText(s)
        t.SetPosition(V(x, y))
        t.SetLayer(layer)
        t.SetTextSize(V(width, netlist.SILK_H))
        t.SetTextThickness(MM(SILK_LINE))
        t.SetMirrored(mirrored)
        board.Add(t)

    for layer, items in ((pcbnew.B_SilkS, netlist.SILK_BACK), (pcbnew.F_SilkS, netlist.SILK_FRONT)):
        for s, x, y, *w in items:
            text(s, x, y, layer, w[0] if w else netlist.SILK_H, mirrored=layer == pcbnew.B_SilkS)


def sync_texts(board):
    """Надписи платы — заново из netlist.SILK_*, а надписи своих посадочных мест (Keychain) —
    по библиотеке: размер, линия, место. Медь и детали не трогаются."""
    for d in list(board.GetDrawings()):
        if isinstance(d, pcbnew.PCB_TEXT) and d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            board.Delete(d)
    add_texts(board)
    for fp in board.GetFootprints():
        fpid = fp.GetFPID()
        if str(fpid.GetLibNickname()) != "Keychain":
            continue
        lib = pcbnew.FootprintLoad(LIB, str(fpid.GetLibItemName()))
        want = {(g.GetText(), g.GetLayer()): g for g in lib.GraphicalItems()
                if isinstance(g, pcbnew.FP_TEXT) and g.GetType() == pcbnew.FP_TEXT.TEXT_is_DIVERS}
        flip = fp.IsFlipped()
        for g in fp.GraphicalItems():
            if not isinstance(g, pcbnew.FP_TEXT) or g.GetType() != pcbnew.FP_TEXT.TEXT_is_DIVERS:
                continue
            layer = pcbnew.FlipLayer(g.GetLayer()) if flip else g.GetLayer()
            src = want.get((g.GetText(), layer))
            if src is None:
                continue
            g.SetTextSize(src.GetTextSize())
            g.SetTextThickness(src.GetTextThickness())
            p = src.GetPos0()
            g.SetPos0(pcbnew.VECTOR2I(p.x, -p.y) if flip else p)
            g.SetDrawCoord()


def strip_paste(board):
    """Площадки деталей, которых завод не ставит (руками и DNP), — без пасты: трафарет не печатает
    на них припой. У DNP-перемычек R17/R19 капля между площадками замкнула бы питание экрана."""
    n = 0
    for fp in board.GetFootprints():
        if not fp.GetAttributes() & pcbnew.FP_EXCLUDE_FROM_POS_FILES:
            continue
        for pad in fp.Pads():
            ls = pad.GetLayerSet()
            if ls.Contains(pcbnew.F_Paste) or ls.Contains(pcbnew.B_Paste):
                ls.RemoveLayer(pcbnew.F_Paste)
                ls.RemoveLayer(pcbnew.B_Paste)
                pad.SetLayerSet(ls)
                n += 1
    return n


def paste_on_unplaced(board):
    """Площадки с пастой у деталей, которых нет в CPL: [(ref, pad)]. Должно быть пусто."""
    return [(fp.GetReference(), pad.GetNumber()) for fp in board.GetFootprints()
            if fp.GetAttributes() & pcbnew.FP_EXCLUDE_FROM_POS_FILES for pad in fp.Pads()
            if pad.IsOnLayer(pcbnew.F_Paste) or pad.IsOnLayer(pcbnew.B_Paste)]


def clip_silk(board):
    """Отрезки шёлка посадочных мест — только внутри контура платы (с полушириной линии):
    за краем завод их всё равно срезает. Вышедший за край отрезок укорачивается, целиком
    снаружи — удаляется. Так DRC, превью и сборочные чертежи показывают то, что выйдет."""
    outline = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(outline)
    inner = {}
    cut = removed = 0
    for fp in board.GetFootprints():
        for g in list(fp.GraphicalItems()):
            if not isinstance(g, pcbnew.FP_SHAPE) or g.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS) \
                    or g.GetShape() != pcbnew.SHAPE_T_SEGMENT:
                continue
            w = g.GetWidth()
            if w not in inner:
                inner[w] = pcbnew.SHAPE_POLY_SET(outline)
                inner[w].Inflate(-(w // 2 + MM(0.02)), 16)
            a, b = pcbnew.VECTOR2I(g.GetStart()), pcbnew.VECTOR2I(g.GetEnd())
            n = max(2, int(math.hypot(b.x - a.x, b.y - a.y) / MM(0.01)))
            pts = [pcbnew.VECTOR2I(int(a.x + (b.x - a.x) * i / n), int(a.y + (b.y - a.y) * i / n)) for i in range(n + 1)]
            ok = [inner[w].Contains(p) for p in pts]
            if all(ok):
                continue
            runs, start = [], None
            for i, v in enumerate(ok + [False]):
                if v and start is None:
                    start = i
                elif not v and start is not None:
                    runs.append((start, i - 1))
                    start = None
            runs = [r for r in runs if r[1] - r[0] >= 10]  # короче 0.1 мм — не линия
            if not runs:
                fp.Delete(g)
                removed += 1
                continue
            i0, i1 = max(runs, key=lambda r: r[1] - r[0])
            g.SetStart(pts[i0])
            g.SetEnd(pts[i1])
            g.SetLocalCoord()
            cut += 1
    return cut, removed


def mask_openings(board, side):
    """Окна маски стороны side (F или B) — площадки с их отступом маски, координаты платы."""
    layer = pcbnew.F_Mask if side == "F" else pcbnew.B_Mask
    out = pcbnew.SHAPE_POLY_SET()
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if not pad.IsOnLayer(layer):
                continue
            p = pcbnew.SHAPE_POLY_SET(pad.GetEffectivePolygon())
            p.Inflate(pad.GetSolderMaskExpansion(), 16)
            out.BooleanAdd(p, pcbnew.SHAPE_POLY_SET.PM_FAST)
    return out


def trim_silk_polys(board, clearance=0.02):
    """Закрашенные метки на шёлке (треугольники первого вывода) отступают от окон маски: завод
    срезает шёлк над площадками, а DRC так не ругается на обрезанную метку (U6 рядом с R8)."""
    opens = {s: mask_openings(board, s) for s in "FB"}
    n = 0
    for fp in board.GetFootprints():
        a = fp.GetOrientation().AsRadians()
        ca, sa = math.cos(a), math.sin(a)
        pos = fp.GetPosition()
        for g in fp.GraphicalItems():
            if not isinstance(g, pcbnew.FP_SHAPE) or g.GetShape() != pcbnew.SHAPE_T_POLY \
                    or g.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                continue
            # Многоугольник посадочного места хранится в его координатах: в плату — поворот и сдвиг.
            local = g.GetPolyShape()
            if local.OutlineCount() != 1:
                continue
            o = local.Outline(0)
            pts = [o.CPoint(i) for i in range(o.PointCount())]
            board_poly = pcbnew.SHAPE_POLY_SET()
            board_poly.NewOutline()
            for p in pts:
                board_poly.Append(int(pos.x + p.x * ca + p.y * sa), int(pos.y - p.x * sa + p.y * ca))
            hole = pcbnew.SHAPE_POLY_SET(opens["F" if g.GetLayer() == pcbnew.F_SilkS else "B"])
            hole.Inflate(MM(clearance) + g.GetWidth() // 2, 16)
            test = pcbnew.SHAPE_POLY_SET(board_poly)
            test.BooleanIntersection(hole, pcbnew.SHAPE_POLY_SET.PM_FAST)
            if not test.OutlineCount():
                continue
            board_poly.BooleanSubtract(hole, pcbnew.SHAPE_POLY_SET.PM_FAST)
            if board_poly.OutlineCount() != 1:
                continue
            ob = board_poly.Outline(0)
            res = pcbnew.SHAPE_POLY_SET()
            res.NewOutline()
            for i in range(ob.PointCount()):
                q = ob.CPoint(i)
                dx, dy = q.x - pos.x, q.y - pos.y
                res.Append(int(dx * ca - dy * sa), int(dx * sa + dy * ca))
            g.SetPolyShape(res)
            n += 1
    return n


def silk_check(board, clearance=0.15):
    """Надписи на шёлке ближе clearance к окну маски или к краю платы: [(надпись, сторона, x, y)].
    Норма JLCPCB: шёлк не ближе 0.15 мм к площадкам."""
    outline = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(outline)
    inner = pcbnew.SHAPE_POLY_SET(outline)
    inner.Inflate(-MM(clearance), 16)
    opens = {s: mask_openings(board, s) for s in "FB"}
    texts = [d for d in board.GetDrawings() if isinstance(d, pcbnew.PCB_TEXT)]
    texts += [g for fp in board.GetFootprints() for g in fp.GraphicalItems()
              if isinstance(g, pcbnew.FP_TEXT) and g.IsVisible()]
    bad = []
    for t in texts:
        if t.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            continue
        side = "F" if t.GetLayer() == pcbnew.F_SilkS else "B"
        shp = t.GetEffectiveTextShape()
        bb = shp.BBox()
        corners = [pcbnew.VECTOR2I(x, y) for x in (bb.GetX(), bb.GetRight()) for y in (bb.GetY(), bb.GetBottom())]
        if shp.Collide(opens[side], MM(clearance)) or not all(inner.Contains(c) for c in corners):
            p = t.GetPosition()
            bad.append((t.GetText(), side, round(pcbnew.ToMM(p.x), 2), round(pcbnew.ToMM(p.y), 2)))
    return bad


# ---------------------------------------------------------------- расстановка

def board_tracks(path=PCB):
    """Сколько дорожек и переходных в файле платы; 0 — если файла нет."""
    if not os.path.exists(path):
        return 0
    return len(pcbnew.LoadBoard(path).GetTracks())


def place(force=False):
    n = board_tracks()
    if n and not force:
        sys.exit(f"{os.path.relpath(PCB, REV)}: в плате {n} дорожек и переходных — это разведённая плата.\n"
                 "place заменит её платой без дорожек. Если так и надо (меняете расстановку и потом\n"
                 "разводите заново через route), запустите: build.py place --force\n"
                 "Вернуть плату из репозитория: git checkout -- hardware/rev-b/keychain-rev-b.kicad_pcb "
                 "hardware/rev-b/Keychain.pretty hardware/rev-b/out")
    os.makedirs(WORK, exist_ok=True)
    footprints.write(LIB)
    board = pcbnew.BOARD()
    board.SetCopperLayerCount(4)
    board.SetLayerType(pcbnew.In1_Cu, pcbnew.LT_POWER)   # сплошная земля
    board.SetLayerType(pcbnew.In2_Cu, pcbnew.LT_SIGNAL)  # шины питания, остальное — земля
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(MM(1.6))
    ds.m_SolderMaskExpansion = MM(0.05)

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    items = [(p["ref"], p["footprint"], p["value"], {**p["pins"], **p["pads"]}, p) for p in netlist.parts()]
    items += [(ref, fpname, role, {}, None) for ref, fpname, role in netlist.MECHANICAL]
    for ref, fpname, value, pins, part in items:
        where = netlist.PLACE.get(ref)
        if where is None:
            continue
        side, x, y, rot = where
        fp = load_fp(fpname)
        fp.SetReference(ref)
        fp.SetValue(value)
        board.Add(fp)
        fp.SetPosition(V(x, y))
        if side == "B":
            fp.Flip(fp.GetPosition(), False)
        fp.SetOrientationDegrees(rot)
        fp.Reference().SetVisible(False)
        fp.Value().SetVisible(False)
        # Позиционные обозначения — только на сборочном чертеже (Fab), шёлк остаётся чистым.
        for g in fp.GraphicalItems():
            if isinstance(g, pcbnew.FP_TEXT) and g.GetText() == "${REFERENCE}" and g.IsOnLayer(
                    pcbnew.B_SilkS if side == "B" else pcbnew.F_SilkS):
                g.SetVisible(False)
        hand = part is None or not (part["lcsc"] or part["mpn"]) or part["dnp"]
        if hand:
            fp.SetAttributes(fp.GetAttributes() | pcbnew.FP_EXCLUDE_FROM_POS_FILES | pcbnew.FP_EXCLUDE_FROM_BOM)
        for pad in fp.Pads():
            name = pins.get(pad.GetNumber())
            if name:
                pad.SetNet(net(name))
    add_outline(board)
    add_keepouts(board)
    net("GND")  # заливки земли добавляются после трассировки (route)
    add_texts(board)
    strip_paste(board)
    clip_silk(board)
    trim_silk_polys(board)
    # Сначала файл проекта с классами цепей и нормами, потом плата без своих настроек:
    # при загрузке KiCad возьмёт правила из проекта.
    write_project()
    pcbnew.SaveBoard(PCB, board, True)
    board = pcbnew.LoadBoard(PCB)
    check_netclasses(board)
    fill(board)
    pcbnew.SaveBoard(PCB, board)
    return board


def check_netclasses(board):
    want = {net: name for name, c in netlist.NETCLASSES.items() for net in c["nets"]}
    for netname, cls in want.items():
        n = board.FindNet(netname)
        got = n.GetNetClassName() if n else None
        if got != cls:
            sys.exit(f"цепь {netname}: класс {got}, а нужен {cls}")


def fill(board):
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())


# ---------------------------------------------------------------- трассировка

def freerouting_jar():
    jar = os.environ.get("FREEROUTING_JAR")
    if not jar or not os.path.exists(jar):
        sys.exit("route: укажи путь к freerouting.jar (2.1.x) в FREEROUTING_JAR; плата не тронута")
    return jar


def strip_tracks(board):
    for t in list(board.GetTracks()):
        board.Remove(t)


PLANE_VIA = (0.55, 0.25)
STUB = 0.3   # отвод от площадки к переходному
CLEAR = 0.15


def dist_seg(p, a, b):
    ax, ay, bx, by, px, py = a.x, a.y, b.x, b.y, p.x, p.y
    dx, dy = bx - ax, by - ay
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / L))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def crosses(a, b, c, d):
    def orient(p, q, r):
        return (q.x - p.x) * (r.y - p.y) - (q.y - p.y) * (r.x - p.x)
    o1, o2, o3, o4 = orient(a, b, c), orient(a, b, d), orient(c, d, a), orient(c, d, b)
    return (o1 > 0) != (o2 > 0) and (o3 > 0) != (o4 > 0)


def seg_seg(a, b, c, d):
    if crosses(a, b, c, d):
        return 0.0
    return min(dist_seg(a, c, d), dist_seg(b, c, d), dist_seg(c, a, b), dist_seg(d, a, b))


def net_clr(*nets):
    """Зазор между медью этих цепей по правилам классов (наибольший из них), мм."""
    out = CLEAR
    for n in nets:
        for c in netlist.NETCLASSES.values():
            if n in c["nets"]:
                out = max(out, c["clearance"])
    return out


class Space:
    """Где на плате свободно для нового переходного или короткого отвода."""

    def __init__(self, board):
        self.board = board
        outline = pcbnew.SHAPE_POLY_SET()
        board.GetBoardPolygonOutlines(outline)
        self.inner = pcbnew.SHAPE_POLY_SET(outline)
        self.inner.Inflate(-MM(0.3 + PLANE_VIA[0] / 2), 16)
        # Порядок — по обозначениям и номерам выводов: в файле он зависит от случайных UUID,
        # а от него — какая площадка первой займёт место под переходное.
        fps = sorted(board.GetFootprints(), key=lambda f: f.GetReference())
        self.pads = [(pad, pad.GetEffectivePolygon(), fp) for fp in fps
                     for pad in sorted(fp.Pads(), key=lambda q: q.GetNumber())]
        self.keepouts = [z.Outline() for z in board.Zones() if z.GetIsRuleArea()]
        for fp in board.GetFootprints():
            self.keepouts += [z.Outline() for z in fp.Zones() if z.GetIsRuleArea() and z.GetDoNotAllowTracks()]
        self.no_via = list(self.keepouts)
        for fp in board.GetFootprints():
            ref = fp.GetReference()
            if ref == "M1":
                continue  # мотор клеится поверх, переходные под ним не мешают
            if ref == "U1":
                mx, my = module_box()
                body = pcbnew.SHAPE_POLY_SET()
                body.NewOutline()
                for x, y in ((mx - 7.9, my - 5.4), (mx + 7.9, my - 5.4), (mx + 7.9, my + 5.4), (mx - 7.9, my + 5.4)):
                    body.Append(MM(x), MM(y))
                self.no_via.append(body)
                continue
            for layer in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
                c = fp.GetCourtyard(layer)
                if c.OutlineCount():
                    self.no_via.append(c)
        self.holes = [(pad.GetPosition(), pad.GetDrillSize().x / 2, pad.GetNetname())
                      for pad, _, _ in self.pads if pad.GetDrillSize().x > 0]
        # уже проложенная медь: (начало, конец, полуширина, слой — None у переходных, цепь)
        self.copper = []
        for t in board.GetTracks():
            if isinstance(t, pcbnew.PCB_VIA):
                self.copper.append((t.GetPosition(), t.GetPosition(), t.GetWidth() / 2, None, t.GetNetname()))
            else:
                self.copper.append((t.GetStart(), t.GetEnd(), t.GetWidth() / 2, t.GetLayer(), t.GetNetname()))

    def add(self, a, b, half, layer, net):
        self.copper.append((a, b, half, layer, net))

    def via_ok(self, c, net, near_pads, near_cu, own_poly=None):
        r = PLANE_VIA[0] / 2
        if not self.inner.Contains(c):
            return False
        if own_poly is not None and own_poly.Collide(c, MM(r + 0.05)):
            return False  # не в саму площадку
        if any(z.Collide(c, MM(r)) for z in self.no_via):
            return False
        for q, qp in near_pads:
            if qp.Collide(c, MM(r + (0.05 if q.GetNetname() == net else net_clr(net, q.GetNetname()) + 0.02))):
                return False
        # Отверстия площадок: медь не ближе 0.25 мм, а отверстие чужой цепи — не ближе HOLE_GAP.
        hole_r = PLANE_VIA[1] / 2
        if any(math.hypot(c.x - h.x, c.y - h.y) < MM(max(r + 0.25, hole_r + (HOLE_GAP if hn != net else 0.25))) + hr
               for h, hr, hn in self.holes):
            return False
        for a0, b0, hw, ly, n in near_cu:
            gap = 0.1 if n == net else net_clr(net, n) + 0.02
            if ly is None and n == net:
                gap = r + 0.1  # свои переходные не лепим вплотную
            elif ly is None:
                # чужое переходное: отверстия не ближе HOLE_GAP край к краю (у всех переходных 0.25 мм)
                gap = max(gap, HOLE_GAP + 2 * hole_r - r - pcbnew.ToMM(hw) + 0.01)
            if dist_seg(c, a0, b0) < MM(r + gap) + hw:
                return False
        return True

    def stub_ok(self, p, c, layer, net, near_pads, near_cu, own_pad):
        for a0, b0, hw, ly, n in near_cu:
            if n != net and (ly is None or ly == layer) and seg_seg(p, c, a0, b0) < MM(STUB / 2 + net_clr(net, n) + 0.02) + hw:
                return False
        d = math.hypot(c.x - p.x, c.y - p.y)
        n_s = max(2, int(pcbnew.ToMM(int(d)) / 0.05))
        for i in range(1, n_s + 1):
            t = i / n_s
            pt = pcbnew.VECTOR2I(int(p.x + (c.x - p.x) * t), int(p.y + (c.y - p.y) * t))
            for q, qp in near_pads:
                if q is not own_pad and q.GetNetname() != net and q.IsOnLayer(layer) and \
                        qp.Collide(pt, MM(STUB / 2 + net_clr(net, q.GetNetname()) + 0.02)):
                    return False
            if t > 0.3 and any(z.Collide(pt, MM(STUB / 2)) for z in self.keepouts):
                return False
        return True

    def near(self, p, radius_mm):
        R = MM(radius_mm)
        pads = [(q, qp) for q, qp, _ in self.pads if abs(q.GetPosition().x - p.x) < R and abs(q.GetPosition().y - p.y) < R]
        cu = [c for c in self.copper if min(abs(c[0].x - p.x), abs(c[1].x - p.x)) < R + MM(2) and
              min(abs(c[0].y - p.y), abs(c[1].y - p.y)) < R + MM(2)]
        return pads, cu


def add_via(board, pos, net):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(pos)
    dia, drill = via_size(net.GetNetname())
    v.SetWidth(MM(min(dia, PLANE_VIA[0])))
    v.SetDrill(MM(drill))
    v.SetViaType(pcbnew.VIATYPE_THROUGH)
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(net)
    board.Add(v)


def add_track(board, a, b, layer, net, width=STUB):
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(a)
    t.SetEnd(b)
    t.SetWidth(MM(width))
    t.SetLayer(layer)
    t.SetNet(net)
    board.Add(t)


def fanout_layer(netname):
    return netlist.PLANES.get(netname, netlist.POWER_LAYER)


def plane_fanout(board, space, netname):
    """Каждой SMD-площадке цепи земли или питания — своё переходное в её слой.

    Ближайшая свободная точка вне дворов деталей, корпуса модуля и запретных зон;
    отвод от площадки не задевает чужие площадки и медь. Возвращает площадки без места.
    """
    net = board.FindNet(netname)
    placed, missed = 0, []
    for pad, poly, fp in space.pads:
        if pad.GetNetname() != netname or pad.GetAttribute() != pcbnew.PAD_ATTRIB_SMD:
            continue
        p = pad.GetPosition()
        layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
        if any(c[4] == netname and c[3] is not None and (c[0] == p or c[1] == p) for c in space.copper):
            continue  # уже подключена прошлым проходом
        near_pads, near_cu = space.near(p, 4.0)
        found = None
        for step in range(5, 31):
            d = step * 0.1
            for k in range(24):
                a = 2 * math.pi * k / 24
                c = pcbnew.VECTOR2I(int(p.x + MM(d) * math.cos(a)), int(p.y + MM(d) * math.sin(a)))
                if space.via_ok(c, netname, near_pads, near_cu, poly) and \
                        space.stub_ok(p, c, layer, netname, near_pads, near_cu, pad):
                    found = c
                    break
            if found:
                break
        if not found:
            # Своему переходному места нет — отвод к соседнему переходному той же цепи.
            vias = sorted((c[0] for c in near_cu if c[3] is None and c[4] == netname),
                          key=lambda v: math.hypot(v.x - p.x, v.y - p.y))
            for v in vias:
                if math.hypot(v.x - p.x, v.y - p.y) < MM(2.5) and \
                        space.stub_ok(p, v, layer, netname, near_pads, near_cu, pad):
                    add_track(board, p, v, layer, net)
                    space.add(p, v, MM(STUB) / 2, layer, netname)
                    placed += 1
                    break
            else:
                missed.append((fp.GetReference(), pad.GetNumber()))
            continue
        add_via(board, found, net)
        add_track(board, p, found, layer, net)
        space.add(found, found, MM(PLANE_VIA[0]) / 2, None, netname)
        space.add(p, found, MM(STUB) / 2, layer, netname)
        placed += 1
    print(f"{netname}: {placed} переходных в {fanout_layer(netname)}"
          + (f", без места: {', '.join(r + '.' + n for r, n in missed)}" if missed else ""))
    return missed


def edge_keepouts():
    """Полосы вдоль прямых краёв платы: роутер держит медь в EDGE_CLEAR от края, как DRC."""
    e = EDGE_CLEAR
    tx0, tx1, tcy = W / 2 - netlist.TAB_R, W / 2 + netlist.TAB_R, netlist.TAB_CY
    rects = [(R, 0, tx0, e), (tx1, 0, W - R, e), (R, H - e, W - R, H), (0, R, e, H - R), (W - e, R, W, H - R),
             (tx0, tcy, tx0 + e, e), (tx1 - e, tcy, tx1, e)]
    out = ""
    for x0, y0, x1, y1 in rects:
        pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)]
        coords = "  ".join(f"{x * 1000:.1f} {-y * 1000:.1f}" for x, y in pts)
        for layer in CU:
            out += f'(keepout "" (polygon {LAYER_NAMES[layer]} 0  {coords}))\n    '
    return out


def dsn_planes(dsn):
    """Цепи-плоскости (земля) — плоскостями в своих слоях: роутер считает площадки с
    переходными в плоскость подключёнными и сам эти цепи не разводит. Уже проложенные
    отводы земли закреплены."""
    text = open(dsn).read()
    # Плоскости из заливок прошлого прогона заменяются своими — по всему контуру платы.
    text = re.sub(r"\s*\(plane [^\s()]+ \(polygon [^()]*\)\)", "", text)
    m = re.search(r"\(boundary\s*\(path pcb 0\s+([^()]*)\)", text)
    planes = "".join(f"\n    (plane {net} (polygon {layer} 0 {m.group(1).strip()}))"
                     for net, layer in netlist.PLANES.items())
    i = text.index("(boundary")
    text = text[:i] + planes.strip() + "\n    " + text[i:]
    text = text[:i] + edge_keepouts() + text[i:]
    for net in netlist.PLANES:
        text = re.sub(r"(\((?:wire|via)\b[^\n]*?\(net %s\))\(type route\)" % re.escape(net),
                      r"\1(type protect)", text)
    with open(dsn, "w") as f:
        f.write(text)


def stitch_islands(board):
    """Островки заливки, где нет ни переходного, ни сквозной площадки своей цепи, сшиваются
    переходным с внутренней плоскостью. Без этого DRC видит их несоединёнными.
    Возвращает площадки на островках, где переходному места не нашлось."""
    added = 0
    orphans = set()
    for _ in range(3):
        fill(board)
        space = Space(board)
        anchors = {}
        for t in board.GetTracks():
            if isinstance(t, pcbnew.PCB_VIA):
                anchors.setdefault(t.GetNetname(), []).append(t.GetPosition())
        for pad, _, _ in space.pads:
            if pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH and pad.GetNetname():
                anchors.setdefault(pad.GetNetname(), []).append(pad.GetPosition())
        new = 0
        for z in board.Zones():
            if z.GetIsRuleArea() or z.GetLayer() == pcbnew.In1_Cu:
                continue
            netname = z.GetNetname()
            polys = z.GetFilledPolysList(z.GetLayer())
            for i in range(polys.OutlineCount()):
                isle = pcbnew.SHAPE_POLY_SET()
                isle.AddOutline(polys.COutline(i))
                if any(isle.Contains(a) for a in anchors.get(netname, [])):
                    continue
                bb = isle.BBox()
                if bb.GetWidth() < MM(0.9) or bb.GetHeight() < MM(0.9):
                    continue
                spot = None
                step = MM(0.2)
                y = bb.GetY() + step
                while y < bb.GetBottom() and not spot:
                    x = bb.GetX() + step
                    while x < bb.GetRight():
                        c = pcbnew.VECTOR2I(x, y)
                        if isle.Contains(c) and isle_room(isle, c):
                            near_pads, near_cu = space.near(c, 3.0)
                            if space.via_ok(c, netname, near_pads, near_cu):
                                spot = c
                                break
                        x += step
                    y += step
                if spot:
                    add_via(board, spot, board.FindNet(netname))
                    space.add(spot, spot, MM(PLANE_VIA[0]) / 2, None, netname)
                    anchors.setdefault(netname, []).append(spot)
                    new += 1
                else:  # переходному места нет — площадки на островке подведёт доводчик
                    orphans.update((fp.GetReference(), pad.GetNumber()) for pad, _, fp in space.pads
                                   if pad.GetNetname() == netname and pad.IsOnLayer(z.GetLayer())
                                   and isle.Contains(pad.GetPosition()))
        added += new
        if not new:
            break
    fill(board)
    print(f"сшивка заливок: {added} переходных")
    return sorted(orphans)


def isle_room(isle, c):
    """Переходной целиком лежит на островке (с отступом от его края)."""
    r = MM(PLANE_VIA[0] / 2 + 0.05)
    return all(isle.Contains(pcbnew.VECTOR2I(int(c.x + r * math.cos(a)), int(c.y + r * math.sin(a))))
               for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2))


def run_freerouting(dsn, ses, timeout):
    if os.path.exists(ses):
        os.remove(ses)
    cmd = ["java", "-Djava.awt.headless=true", "-jar", freerouting_jar(), "-de", dsn, "-do", ses,
           "--gui.enabled=false", "--router.max_passes=" + os.environ.get("FR_PASSES", "100"),
           "--router.max_threads=1", "--router.job_timeout=" + timeout,
           "--profile.allow_telemetry=false", "--usage_and_diagnostic_data.disable_analytics=true"]
    log = os.path.join(WORK, os.path.basename(ses) + ".log")
    with open(log, "w") as f:
        subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=7200)
    if not os.path.exists(ses):
        sys.exit(f"Freerouting не выдал SES, см. {log}")


SPILL_W = 0.3  # мм: остаток шины питания вне слоя питания (слаботочные ветки)


def dsn_prepare(dsn, spill=()):
    """Задача для роутера: уже проложенные отводы питания закреплены, каждому классу цепей —
    свои слои (питание только в слое питания, сигналы — по внешним слоям). Цепи питания из
    spill не уместились в слое питания целиком: их остаток роутер ведёт дорожкой SPILL_W
    и по внешним слоям тоже."""
    text = open(dsn).read()
    power = set(netlist.POWER_NETS)
    text = re.sub(r"(\((?:wire|via)\b[^\n]*?\(net (\S+?)\))\(type route\)",
                  lambda m: m.group(1) + ("(type protect)" if m.group(2) in power else "(type route)"), text)
    def layers_for(nets):
        for c in netlist.NETCLASSES.values():
            if set(nets) & set(c["nets"]):
                return c["layers"]
        return netlist.NETCLASSES["Default"]["layers"]
    def add_layers(m):
        head, nets = m.group(1), m.group(2).split()
        nets = [n for n in nets if n != '""']
        layers = layers_for(nets)
        use = "".join(f"\n        (use_layer {l})" for l in layers) if layers else ""
        return head + m.group(2) + "\n      (circuit" + use
    text = re.sub(r"(\(class\s+\S+\s)((?:[^()]*?))\s*\(circuit", add_layers, text)
    for net in spill:
        text = re.sub(r"(\(class\s+\S+[^()]*?)\s" + re.escape(net) + r"(?=[\s)])", r"\1", text, count=1)
        dia, drill = via_size(net)
        via = f"Via[0-3]_{int(dia * 1000)}:{int(drill * 1000)}_um"
        layers = "".join(f"\n        (use_layer {l})" for l in [netlist.POWER_LAYER, *netlist.SIGNAL_LAYERS])
        cls = (f"    (class Spill_{net} {net}\n      (circuit{layers}\n        (use_via {via})\n      )\n"
               f"      (rule\n        (width {int(SPILL_W * 1000)})\n        (clearance 200.1)\n      )\n    )\n")
        text = re.sub(r"(\n)(    \(class )", lambda m: m.group(1) + cls + m.group(2), text, count=1)
    with open(dsn, "w") as f:
        f.write(text)


def route():
    """Сначала земля и питание — переходными в свои слои, потом Freerouting: шины питания
    в слое питания, сигналы по внешним слоям; потом заливки земли, сшивка и проверка."""
    # Java и Freerouting — до того, как плата тронута: без них route ничего не меняет.
    if not shutil.which("java"):
        sys.exit("route: нужна Java 21 (java в PATH); плата не тронута")
    freerouting_jar()
    board = pcbnew.LoadBoard(PCB)
    strip_tracks(board)
    planes_exist = has_copper_zones(board)  # прогон повторный: заливки остаются, в DSN не идут
    space = Space(board)
    for _ in range(2):  # второй проход подключает площадки к переходным соседей
        missed = []
        for netname in list(netlist.PLANES) + netlist.POWER_NETS:
            missed += plane_fanout(board, space, netname)
    # Шины питания — своим маршрутизатором в слое питания, до сигналов.
    def power_track(a, b, net, width):
        add_track(board, V(*a), V(*b), pcbnew.In2_Cu, board.FindNet(net), width)
    failed = powerroute.route_power(board, ["VDD", "VSYS", "VBAT", "VBUS"], power_track)
    print("шины питания:", "все соединены" if not failed else f"не соединено {failed}")
    dsn = os.path.join(WORK, NAME + ".dsn")
    ses = os.path.join(WORK, NAME + ".ses")
    if not pcbnew.ExportSpecctraDSN(board, dsn):
        sys.exit("не удалось выгрузить DSN")
    dsn_planes(dsn)
    dsn_prepare(dsn, netlist.POWER_NETS)  # остаток питания (площадки без своего переходного) — роутеру
    if not (os.environ.get("REUSE_SES") and os.path.exists(ses)):  # REUSE_SES=1 — взять прошлый результат
        run_freerouting(dsn, ses, os.environ.get("FR_TIMEOUT", "00:15:00"))
    fan = set(netlist.PLANES) | set(netlist.POWER_NETS)
    # Координаты копируются: GetStart() ссылается на память дорожки, а strip_tracks её удалит.
    saved = [(isinstance(t, pcbnew.PCB_VIA), pcbnew.VECTOR2I(t.GetStart()), pcbnew.VECTOR2I(t.GetEnd()),
              t.GetLayer(), t.GetNetname(), t.GetWidth())
             for t in board.GetTracks() if t.GetNetname() in fan]
    strip_tracks(board)
    import_ses(board, ses)
    def key(is_via, a, b, layer, net):
        q = lambda v: (round(pcbnew.ToMM(v.x), 2), round(pcbnew.ToMM(v.y), 2))
        ends = tuple(sorted([q(a), q(b)]))
        return (is_via, ends, None if is_via else layer, net)
    have = {key(isinstance(t, pcbnew.PCB_VIA), t.GetStart(), t.GetEnd(), t.GetLayer(), t.GetNetname())
            for t in board.GetTracks()}
    for is_via, a, b, layer, netname, width in saved:
        if key(is_via, a, b, layer, netname) in have:
            continue  # закреплённые отводы питания роутер вернул сам
        if is_via:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(a)
            dia, drill = via_size(netname)
            v.SetWidth(MM(dia))
            v.SetDrill(MM(drill))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(board.FindNet(netname))
            board.Add(v)
        else:
            add_track(board, a, b, layer, board.FindNet(netname), pcbnew.ToMM(width))
    print("висящих обрезков убрано:", fixroute.remove_dangling(board))
    if not planes_exist:
        add_planes(board)
    stitch_islands(board)
    # Доводка: что не развёл Freerouting — своим трассировщиком по отчёту DRC.
    def fix_track(a, b, layer, net, width):
        add_track(board, V(*a), V(*b), layer, board.FindNet(net), width)
    def fix_via(p, net, dia, drill):
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(V(*p))
        v.SetWidth(MM(dia))
        v.SetDrill(MM(drill))
        v.SetViaType(pcbnew.VIATYPE_THROUGH)
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNet(board.FindNet(net))
        board.Add(v)
    if missed:  # площадки земли и питания, которым не нашлось своего переходного
        failed = fixroute.connect_pads(board, missed, fix_track, fix_via)
        if failed:
            print("не подключены:", failed)
    for _ in range(3):
        _, _, text = drc(board)
        if "[unconnected_items]" not in text:
            break
        failed = fixroute.route_leftovers(board, text, fix_track, fix_via)
        if failed:
            print("доводка не смогла:", failed)
        orphans = stitch_islands(board)
        if orphans:
            print("не подключены:", fixroute.connect_pads(board, orphans, fix_track, fix_via) or "—")
            fill(board)
    finish(board)
    pcbnew.SaveBoard(PCB, board)
    return drc(board)


def finish(board):
    """Доводка разведённой платы, без перетрассировки: паста, шёлк, надписи, отверстия разных
    цепей. Повторный прогон ничего не меняет."""
    print("площадок без пасты (детали не ставит завод):", strip_paste(board))
    print("отрезков шёлка у края: укорочено %d, удалено %d" % clip_silk(board))
    sync_texts(board)
    print("меток шёлка у окон маски обрезано:", trim_silk_polys(board))
    spread_vias(board)
    fill(board)
    bad = silk_check(board)
    if bad:
        print("надписи ближе 0.15 мм к окнам маски или краю:", bad)


def hole_pairs(board, gap=HOLE_GAP):
    """Пары отверстий разных цепей ближе gap край к краю: [(зазор мм, A, B)]."""
    holes = [t for t in board.GetTracks() if isinstance(t, pcbnew.PCB_VIA)]
    holes += [pad for fp in board.GetFootprints() for pad in fp.Pads() if pad.GetDrillSize().x > 0]
    out = []
    for i, a in enumerate(holes):
        for b in holes[i + 1:]:
            if a.GetNetname() != b.GetNetname():
                g = hole_gap(a, b)
                if g < gap:
                    out.append((g, a, b))
    return sorted(out, key=lambda x: x[0])


def hole_r(item):
    """Радиус отверстия переходного или площадки (у овального — по меньшей стороне)."""
    if isinstance(item, pcbnew.PCB_VIA):
        return item.GetDrillValue() / 2
    d = item.GetDrillSize()
    return min(d.x, d.y) / 2


def hole_gap(a, b):
    """Зазор между отверстиями a и b край к краю, мм."""
    pa, pb = a.GetPosition(), b.GetPosition()
    return pcbnew.ToMM(int(math.hypot(pa.x - pb.x, pa.y - pb.y) - hole_r(a) - hole_r(b)))


def move_via(board, via, pos):
    """Переходное — в pos, концы дорожек его цепи, что сходились в нём, — вместе с ним."""
    old = pcbnew.VECTOR2I(via.GetPosition())
    for t in board.GetTracks():
        if isinstance(t, pcbnew.PCB_VIA) or t.GetNetCode() != via.GetNetCode():
            continue
        if t.GetStart() == old:
            t.SetStart(pos)
        if t.GetEnd() == old:
            t.SetEnd(pos)
    via.SetPosition(pos)


def parse_drc(text):
    """Нарушения из отчёта DRC: [(тип, важность)] и число несоединённых."""
    items = re.findall(r"^\[(\w+)\]:.*\n(?:.*\n)*?.*Severity: (\w+)", text, re.M)
    m = re.search(r"\*\* Found (\d+) unconnected pads", text)
    return items, int(m.group(1)) if m else 0


def spread_vias(board, gap=HOLE_GAP, margin=0.012):
    """Переходные разных цепей ближе gap (край к краю) раздвигаются минимальным сдвигом: одно
    из пары уходит от другого вместе с концами своих дорожек. Каждый сдвиг проверяется
    перезаливкой и DRC (зазоры, соединения, переходное не в площадке); не прошёл — откат."""
    def state():
        fill(board)
        rpt = os.path.join(WORK, "spread.rpt")
        pcbnew.WriteDRCReport(board, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
        items, unconnected = parse_drc(open(rpt).read())
        other = sum(1 for k, sev in items if sev == "error" and k != "hole_near_hole")
        return other, unconnected

    def in_pad(via):
        r = via.GetWidth() // 2
        return any(pad.GetAttribute() == pcbnew.PAD_ATTRIB_SMD and pad.IsOnLayer(lay) and
                   pad.GetEffectivePolygon().Collide(via.GetPosition(), r)
                   for fp in board.GetFootprints() for pad in fp.Pads() for lay in (pcbnew.F_Cu, pcbnew.B_Cu))

    os.makedirs(WORK, exist_ok=True)
    pairs = hole_pairs(board, gap)
    if not pairs:
        return 0
    base = state()
    moved = 0
    for g, a, b in pairs:
        g = hole_gap(a, b)
        if g >= gap:
            continue  # пару уже развёл сдвиг соседней
        done = False
        for extra in (margin, 2 * margin, 4 * margin):
            for phi in (0, 30, -30, 60, -60):
                for via, other in ((a, b), (b, a)):
                    if not isinstance(via, pcbnew.PCB_VIA):
                        continue
                    p, q = via.GetPosition(), other.GetPosition()
                    d = math.hypot(p.x - q.x, p.y - q.y)
                    ux, uy = (p.x - q.x) / d, (p.y - q.y) / d
                    t = math.radians(phi)
                    vx, vy = ux * math.cos(t) - uy * math.sin(t), ux * math.sin(t) + uy * math.cos(t)
                    step = MM(gap - g + extra) / math.cos(t)
                    old = pcbnew.VECTOR2I(p)
                    move_via(board, via, pcbnew.VECTOR2I(int(p.x + vx * step), int(p.y + vy * step)))
                    other_err, unconnected = state()
                    mine = [x for x in hole_pairs(board, gap) if via in (x[1], x[2])]
                    if other_err <= base[0] and unconnected <= base[1] and not mine and not in_pad(via):
                        print("переходное %s (%.3f, %.3f) сдвинуто на %.3f мм: отверстие до %s было %.3f мм" % (
                            via.GetNetname(), pcbnew.ToMM(old.x), pcbnew.ToMM(old.y), pcbnew.ToMM(int(step)),
                            other.GetNetname(), g))
                        moved += 1
                        done = True
                        break
                    move_via(board, via, old)
                if done:
                    break
            if done:
                break
        if not done:
            print("не удалось развести отверстия: %s и %s, %.3f мм" % (a.GetNetname(), b.GetNetname(), g))
    state()
    return moved


def import_ses(board, path):
    """Читает дорожки и переходные из SES Freerouting (свой разбор: без GUI KiCad)."""
    text = open(path).read()
    tokens = re.findall(r'\(|\)|"[^"]*"|[^\s()]+', text)
    pos = 0

    def parse():
        nonlocal pos
        tok = tokens[pos]
        pos += 1
        if tok == "(":
            lst = []
            while tokens[pos] != ")":
                lst.append(parse())
            pos += 1
            return lst
        return tok.strip('"')

    tree = parse()

    def find(node, key):
        for item in node:
            if isinstance(item, list) and item and item[0] == key:
                yield item

    routes = next(find(tree, "routes"))
    res = next(find(routes, "resolution"))
    scale = {"um": 1000.0, "mm": 1.0, "mil": 1 / 0.0254}[res[1]] * float(res[2])  # единиц на мм
    layers = {board.GetLayerName(l): l for l in CU}
    vias_def = {}
    lib = next(find(routes, "library_out"), [])
    for ps in find(lib, "padstack"):
        for shape in find(ps, "shape"):
            circ = shape[1]
            if isinstance(circ, list) and circ[0] == "circle":
                vias_def[ps[1]] = float(circ[2]) / scale
    netout = next(find(routes, "network_out"))
    n_tracks = n_vias = 0
    for netnode in find(netout, "net"):
        net = board.FindNet(netnode[1])
        for wire in find(netnode, "wire"):
            path = next(find(wire, "path"))
            layer = layers[path[1]]
            width = float(path[2]) / scale
            coords = [float(v) / scale for v in path[3:]]
            pts = [(coords[i], -coords[i + 1]) for i in range(0, len(coords), 2)]
            for a, b in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])))
                t.SetEnd(pcbnew.VECTOR2I(MM(b[0]), MM(b[1])))
                t.SetWidth(MM(width))
                t.SetLayer(layer)
                t.SetNet(net)
                board.Add(t)
                n_tracks += 1
        for via in find(netnode, "via"):
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(MM(float(via[2]) / scale), MM(-float(via[3]) / scale)))
            dia, drill = via_size(netnode[1])
            v.SetWidth(MM(vias_def.get(via[1], dia)))
            v.SetDrill(MM(drill))
            v.SetViaType(pcbnew.VIATYPE_THROUGH)
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNet(net)
            board.Add(v)
            n_vias += 1
    print(f"SES: {n_tracks} отрезков, {n_vias} переходных")


def via_size(netname):
    for c in netlist.NETCLASSES.values():
        if netname in c["nets"]:
            return c["via"]
    return netlist.NETCLASSES["Default"]["via"]


DRC_REPORT = os.path.join(OUT, NAME + "-drc.rpt")  # отчёт в репозитории, его пишет и fab


def drc(board, report=DRC_REPORT):
    """DRC платы в отчёт report: ({вид: число}, ошибок, текст). Ошибки — всё с важностью error,
    в том числе несоединённые площадки."""
    os.makedirs(os.path.dirname(report), exist_ok=True)
    pcbnew.WriteDRCReport(board, report, pcbnew.EDA_UNITS_MILLIMETRES, True)
    text = open(report).read()
    # В шапке — имя файла платы без пути: отчёт лежит в репозитории.
    text = text.replace(board.GetFileName(), os.path.basename(board.GetFileName()))
    with open(report, "w") as f:
        f.write(text)
    found = {k: int(v) for v, k in re.findall(r"\*\* Found (\d+) (DRC violations|unconnected pads|Footprint errors)", text)}
    errors = len(re.findall(r"Severity: error", text))
    warnings = len(re.findall(r"Severity: warning", text))
    found["warnings"] = warnings
    print(f"DRC: {found}, ошибок {errors}, предупреждений {warnings} → {os.path.relpath(report, REV)}")
    return found, errors, text


# ---------------------------------------------------------------- превью

def render(tag="place"):
    for side, layers in (("front", "F.Cu,F.SilkS,F.Fab,F.CrtYd,Edge.Cuts,F.Mask"),
                         ("back", "B.Cu,B.SilkS,B.Fab,B.CrtYd,Edge.Cuts,B.Mask")):
        svg = os.path.join(WORK, f"{tag}-{side}.svg")
        cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
               "--page-size-mode", "2", "-o", svg, PCB]
        if side == "back":
            cmd.insert(4, "--mirror")
        subprocess.run(cmd, check=True, capture_output=True)
        to_png(svg, svg[:-4] + ".png")


def png_tool():
    """Чем переводить SVG в PNG: cairosvg (pip install cairosvg, или путь в CAIROSVG) или rsvg-convert."""
    return shutil.which("cairosvg") or os.environ.get("CAIROSVG") or shutil.which("rsvg-convert")


def to_png(svg, png, width=1400):
    """SVG → PNG. Без конвертера — False, и старый PNG удаляется, чтобы не выдать его за новый."""
    tool = png_tool()
    if not tool:
        if os.path.exists(png):
            os.remove(png)
        return False
    if os.path.basename(tool) == "rsvg-convert":
        subprocess.run([tool, "-w", str(width), "-b", "white", "-o", png, svg], check=True)
    else:
        subprocess.run([tool, svg, "-o", png, "--output-width", str(width), "-b", "white"], check=True)
    return True


USAGE = "build.py place [--force] | route | finish | fab [--no-panel] | panel | all --force"

if __name__ == "__main__":
    args = sys.argv[1:]
    force = "--force" in args
    no_panel = "--no-panel" in args
    args = [a for a in args if a not in ("--force", "--no-panel")]
    stage = args[0] if args else "all"
    if stage not in ("place", "route", "finish", "fab", "panel", "all") or len(args) > 1:
        sys.exit(USAGE)
    if stage in ("place", "all"):
        place(force)
        render("place")
    if stage in ("route", "all"):
        found, errors, _ = route()
        render("route")
        if errors or found.get("unconnected pads"):
            sys.exit(f"route: после доводки DRC — ошибок {errors}, несоединённых {found.get('unconnected pads')}; "
                     "fab такую плату не примет")
    if stage == "finish":
        write_dru()
        footprints.write(LIB)
        b = pcbnew.LoadBoard(PCB)
        if not b.GetTracks():
            sys.exit("finish — для разведённой платы, а в этой нет дорожек")
        finish(b)
        pcbnew.SaveBoard(PCB, b)
        found, errors, _ = drc(b)
        if errors:
            sys.exit(f"finish: DRC — ошибок {errors}")
    if stage in ("fab", "all"):
        import fab
        fab.run(panel=not no_panel)
    if stage == "panel":
        import fab
        fab.run_panel()
