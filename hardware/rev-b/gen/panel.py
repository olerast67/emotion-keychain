"""Панель для заводского монтажа: две платы, рамка, фрезерованные пазы и перемычки с mouse bites.

Зачем. Детали стоят с двух сторон, а такой монтаж у JLCPCB — только Standard PCBA: плата или
панель от 70 × 70 мм с технологическими полями, реперными точками и технологическими
отверстиями. Плата 30 × 52.5 мм (с ушком) меньше, а у двух её краёв почти ничего нельзя:
  - правый: модуль U1 в 0.25 мм от края, толкатель кнопки SW1 выступает за край на ~0.2 мм —
    перемычек нет;
  - нижний: корпус USB-C J1 выступает на ~0.7 мм, TVS D3 — в 0.95 мм от края, у обоих нижних
    углов — отверстия винтов корпуса; одна короткая перемычка — справа, между J1 и винтом.
Поэтому нигде нет V-cut, а перемычки — на левом крае, на верхнем левее ушка и одна на нижнем:
плата держится с трёх сторон (JLCPCB советует перемычки по разные стороны платы).

Раскладка (KiKit 1.5):
  - 1 ряд × 2 платы, вторая повёрнута на 180°: правые края обеих смотрят в общий паз
    посередине (перемычек там нет), нижние — в пазы у рамки;
  - сплошная рамка (tightframe): материал везде вокруг плат, кроме фрезерованного паза
    PANEL_SLOT вдоль контура каждой платы; ширина рамки снаружи — FRAME, вся панель не меньше
    MIN_SIZE по обеим сторонам;
  - перемычки по TABS с mouse bites по рекомендации JLCPCB: отверстия 0.6 мм, между краями
    отверстий 0.4 мм (шаг 1.0), в перемычке 5 мм — 5 отверстий. Где у края места меньше
    (между R3 и R4, у C15 сверху, внизу между J1 и винтом), перемычка уже и отверстий 3–4.
    Отверстия сдвинуты наружу, в перемычку, на BITE_OFFSET: заливки платы стоят в 0.3 мм от
    края, и отверстие не подходит к меди ближе 0.25 мм. Обломки перемычек поэтому выступают за
    край на ~0.3 мм — их сошлифовывают заподлицо (README платы и корпуса);
  - у каждой перемычки в контуре заливок земли всех слоёв — вырез на KEEPOUT_IN мм внутрь от
    края (по длине — с запасом на скругление фрезы). На плате заливку держит в 0.3 мм от края
    контур платы, а поперёк перемычки контура нет: без выреза любая перезаливка (кнопка B в
    KiCad, DRC с перезаливкой) довела бы землю до линии разлома. make() это проверяет
    (copper_check): меряет медь до отверстий и до каждой линии разлома на каждом слое, как есть
    и на перезалитой копии — DRC KiCad ни того, ни другого не видит;
  - 3 реперные точки (медь 1 мм, окно в маске 2 мм) с обеих сторон в трёх углах рамки —
    в 5 мм от краёв панели (JLCPCB: от 3.85 мм, PCBWay: от 5 мм);
  - 4 технологических отверстия 1.152 мм без металлизации, окно маски +0.148 мм (JLCPCB),
    по углам рамки;
  - меди на рамке нет, кроме реперных точек.

Собирается панель в out/work/panel-new и только целиком, после DRC и всех файлов, заменяет
out/panel. Не вышло — out/panel остаётся как была.

Результат — out/panel/: плата панели (keychain-rev-b-panel.kicad_pcb), отчёт DRC, Gerber,
BOM и CPL для JLCPCB, BOM и расстановка для PCBWay, сборочные чертежи и картинка панели.
Обозначения на панели — с номером копии: R1_1 на левой плате, R1_2 на правой (повёрнутой).

Запуск — из build.py fab (или build.py panel). Нужен Python, в котором есть и pcbnew KiCad 7,
и KiKit 1.5.1:
    python3 -m venv --system-site-packages ~/kikit-venv && ~/kikit-venv/bin/pip install kikit==1.5.1
    KIKIT_PYTHON=~/kikit-venv/bin/python python3 build.py fab
или напрямую: ~/kikit-venv/bin/python hardware/rev-b/gen/panel.py (сначала DRC платы, как в fab).
    panel.py --stage — только собрать в out/work/panel-new (так её зовёт fab);
    panel.py --copper-check ПАНЕЛЬ ЛИНИИ — медь у перемычек и перезаливка (её зовёт make()).
"""
import json
import math
import os
import shutil
import subprocess
import sys

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build  # noqa: E402
import fab  # noqa: E402
import netlist  # noqa: E402

from kikit import panelize_ui_impl as ki  # noqa: E402
from kikit.annotations import TabAnnotation  # noqa: E402
from kikit.panelize import Panel  # noqa: E402
from kikit.units import mm  # noqa: E402
from shapely.geometry import LineString, Polygon  # noqa: E402  (copper_check)

PANEL_SLOT = 2.0    # мм: фрезерованный паз вокруг каждой платы и между ними
FRAME = 5.0         # мм: рамка снаружи
MIN_SIZE = 72.0     # мм: панель не меньше по обеим сторонам (JLCPCB Standard PCBA — от 70 × 70)
# mouse bites: отверстие, шаг (KiKit делит длину реза на целое число шагов не длиннее этого —
# выходит ровно 1.0 мм), сдвиг (минус — наружу, в перемычку), продление реза за края перемычки.
BITE_DRILL, BITE_SPACING, BITE_OFFSET, BITE_PROLONG = 0.6, 0.98, -0.25, 0.5
MILL_R = 1.0        # мм: радиус фрезы — скругления у основания перемычек
KEEPOUT_IN = 0.3    # мм: вырез в заливке у перемычки — внутрь от края платы (зазор меди до края)
FID_OFFSET = 5.0    # мм: реперные точки от краёв панели
TOOL_OFFSET = 2.5   # мм: технологические отверстия от краёв панели
TOOL_D, TOOL_MASK = 1.152, 0.148

# Перемычки в координатах платы (мм, y вниз, ноль — левый верхний угол прямоугольника 30 × 46):
# (край, середина перемычки по краю, ширина). Свободные места у края — по дворам деталей с обеих
# сторон (скругления углов — до 2 мм от угла):
#   слева: угол … C16 (y 9.7, x от 3.1) — перемычка 3.25–8.25;
#          отверстие J2 (y до 21.3) … R3 (y 33.3, x от 1.7), рядом мотор M1 (x от 3.1, руками) —
#          24.5–29.5;
#          R3 (y до 34.3) … R4 (y 39.1, x от 1.9) — только 3 мм: 35.17–38.17, по 0.9 мм до них;
#   сверху, левее ушка: угол … C15 (x 8.0, y от 2.1) — 2.5–6.5, до C15 по диагонали 2.6 мм;
#   снизу: корпус J1 (x 10.5–19.5, выступает за край на 0.7 мм, двор до x 20.3) … отверстие винта
#          (x 25.65–27.85, в 0.9 мм от края) — 22.0–25.0: до корпуса J1 2.5 мм, до двора 1.7,
#          скругление фрезы — в 1.5 мм от выступа J1; ближе к J1 не ставить. Это вторая сторона,
#          за которую плата держится, — напротив верхней, у дальнего от левого края угла.
# Справа (U1 в 0.25 мм, толкатель SW1 за краем) — ничего; снизу левее (D3 в 0.95 мм от края,
# винт в углу) и под J1 — тоже.
TABS = [
    ("left", 5.75, 5.0),
    ("left", 27.0, 5.0),
    ("left", 36.67, 3.0),
    ("top", 4.5, 4.0),
    ("bottom", 23.5, 3.0),
]


def stubs():
    """Где на краю платы останутся обломки перемычек: [(край, от, до)] в мм."""
    return [(edge, round(c - w / 2, 2), round(c + w / 2, 2)) for edge, c, w in TABS]


def to_panel(sub, rotated):
    """Точка платы (мм, координаты платы) → точка панели (единицы KiCad) для копии sub."""
    minx, miny, maxx, maxy = sub.bounds()       # контур копии на панели, единицы KiCad
    oy = netlist.TAB_CY - netlist.TAB_R         # верх ушка; контур платы: x 0…W, y oy…H
    w, h = netlist.BOARD_W, netlist.BOARD_H - oy

    def f(x, y):
        fx, fy = x / w, (y - oy) / h
        if rotated:  # повёрнутая копия: зеркально по обеим осям
            fx, fy = 1 - fx, 1 - fy
        return minx + fx * (maxx - minx), miny + fy * (maxy - miny)
    return f


def across(edge, t):
    """Точка на расстоянии t от края edge внутрь платы (t < 0 — наружу), как функция места вдоль края."""
    W, H = netlist.BOARD_W, netlist.BOARD_H
    return {"left": lambda a: (t, a), "top": lambda a: (a, t), "bottom": lambda a: (a, H - t)}[edge]


def tab_annotations(sub, rotated):
    """Перемычки одной копии платы на панели. sub — её подложка; rotated — повёрнута на 180°."""
    f = to_panel(sub, rotated)
    out = []
    for edge, c, width in TABS:
        # начало перемычки — в пазу в 1 мм от края, направление — к плате
        p = f(*across(edge, -1.0)(c))
        q = f(*across(edge, 0.0)(c))
        n = math.hypot(q[0] - p[0], q[1] - p[1])
        out.append(TabAnnotation(None, p, ((q[0] - p[0]) / n, (q[1] - p[1]) / n), width * mm))
    return out


def tab_notches(sub, rotated):
    """Вырезы в контуре заливки у перемычек копии: от 1 мм снаружи до KEEPOUT_IN внутрь от края,
    вдоль края — ширина перемычки плюс радиус фрезы с каждой стороны. Возвращает
    [(вырез — точки в единицах KiCad, (край, от, до, (x0, y0, x1, y1) — линия разлома в мм))]."""
    f = to_panel(sub, rotated)
    out = []
    for edge, c, width in TABS:
        a, b = c - width / 2 - MILL_R, c + width / 2 + MILL_R
        outside, inside, on = across(edge, -1.0), across(edge, KEEPOUT_IN), across(edge, 0.0)
        pts = [f(*outside(a)), f(*inside(a)), f(*inside(b)), f(*outside(b))]
        (x0, y0), (x1, y1) = f(*on(a)), f(*on(b))
        line = (edge, round(c - width / 2, 2), round(c + width / 2, 2),
                tuple(pcbnew.ToMM(int(v)) for v in (x0, y0, x1, y1)))
        out.append((pts, line))
    return out


def notch_zones(panel, notches):
    """Вырезает notches из контура каждой заливки меди на панели. Залитая медь не меняется (она
    и так в 0.3 мм от края), а перезаливка у перемычки дальше выреза не пойдёт. Запретная зона
    (rule area) тут не годится: DRC KiCad считает заливку, которая лишь касается её края,
    нарушением, а медь на плате стоит ровно в 0.3 мм."""
    cut = pcbnew.SHAPE_POLY_SET()
    for pts in notches:
        cut.NewOutline()
        for x, y in pts:
            cut.Append(int(x), int(y))
    n = 0
    for z in panel.board.Zones():
        if z.GetIsRuleArea():
            continue
        outline = z.Outline()
        before = outline.Area()
        outline.BooleanSubtract(cut, pcbnew.SHAPE_POLY_SET.PM_STRICTLY_SIMPLE)
        if outline.Area() < before:
            n += 1
        z.HatchBorder()
    return n


def preset():
    return ki.obtainPreset([], validate=True,
        layout={"type": "grid", "rows": 1, "cols": 2, "hspace": f"{PANEL_SLOT}mm", "vspace": f"{PANEL_SLOT}mm",
                "alternation": "cols", "renamenet": "Board_{n}-{orig}", "renameref": "{orig}_{n}"},
        source={"type": "auto", "tolerance": "1mm"},
        tabs={"type": "annotation", "fillet": "0mm"},
        cuts={"type": "mousebites", "drill": f"{BITE_DRILL}mm", "spacing": f"{BITE_SPACING}mm",
              "offset": f"{BITE_OFFSET}mm", "prolong": f"{BITE_PROLONG}mm"},
        framing={"type": "tightframe", "width": f"{FRAME}mm", "slotwidth": f"{PANEL_SLOT}mm",
                 "hspace": f"{PANEL_SLOT}mm", "vspace": f"{PANEL_SLOT}mm",
                 "mintotalwidth": f"{MIN_SIZE}mm", "mintotalheight": f"{MIN_SIZE}mm"},
        tooling={"type": "4hole", "hoffset": f"{TOOL_OFFSET}mm", "voffset": f"{TOOL_OFFSET}mm",
                 "size": f"{TOOL_D}mm", "paste": False, "soldermaskmargin": f"{TOOL_MASK}mm"},
        fiducials={"type": "3fid", "hoffset": f"{FID_OFFSET}mm", "voffset": f"{FID_OFFSET}mm",
                   "coppersize": "1mm", "opening": "2mm", "paste": False},
        post={"millradius": "1mm", "origin": "tl", "refillzones": False, "copperfill": False},
        page={"type": "inherit", "anchor": "tl", "posx": "0mm", "posy": "0mm"})


def build_panel(dest):
    """Собирает плату панели в dest по шагам kikit panelize, со своими перемычками и вырезами
    в заливках у них. Возвращает (путь платы панели, линии разлома — см. tab_notches)."""
    os.makedirs(dest, exist_ok=True)
    pcb = os.path.join(dest, fab.PANEL_NAME + ".kicad_pcb")
    p = preset()
    board = pcbnew.LoadBoard(fab.PCB)
    panel = Panel(pcb)
    panel.inheritDesignSettings(board)
    panel.inheritProperties(board)
    panel.inheritTitleBlock(board)
    panel.inheritLayerNames(board)
    source = ki.readSourceArea(p["source"], board)
    subs, framing, backbone = ki.buildLayout(p, panel, fab.PCB, source)
    for i, s in enumerate(subs):
        s.annotations = [a for a in s.annotations if not isinstance(a, TabAnnotation)]
        s.annotations += tab_annotations(s, rotated=i % 2 == 1)
    cuts = ki.buildTabs(p, panel, subs, framing)
    if len(cuts) != len(TABS) * len(subs):
        sys.exit(f"панель: перемычек {len(cuts)}, а нужно {len(TABS) * len(subs)}; out/panel не тронута")
    frame_cuts = ki.buildFraming(p, panel)
    ki.buildTooling(p, panel)
    # Окно маски вокруг технологических отверстий шире отверстия на TOOL_MASK (так просит JLCPCB).
    # У площадки без меди KiCad не берёт отступ маски, окно — это сама площадка: она и шире.
    for f in panel.board.GetFootprints():
        if f.GetReference().startswith("KiKit_TO"):
            for pad in f.Pads():
                d = pcbnew.FromMM(TOOL_D + 2 * TOOL_MASK)
                pad.SetSize(pcbnew.VECTOR2I(d, d))
    ki.buildFiducials(p, panel)
    ki.buildPostprocessing(p["post"], panel)
    ki.makeTabCuts(p, panel, cuts)
    ki.makeOtherCuts(p, panel, list(backbone) + list(frame_cuts))
    ki.setStackup(p["source"], panel)
    ki.setPageSize(p["page"], panel, board)
    ki.positionPanel(p["page"], panel)
    # Вырезы в заливках у перемычек — по контуру копий уже на месте (positionPanel их сдвинул).
    notches, lines = [], []
    for i, s in enumerate(subs):
        for pts, line in tab_notches(s, rotated=i % 2 == 1):
            notches.append(pts)
            lines.append((i + 1, *line))
    zones = sum(1 for z in panel.board.Zones() if not z.GetIsRuleArea())
    if notch_zones(panel, notches) != zones:
        fail(f"вырезы у перемычек легли не во все {zones} заливок")
    # Обозначения копий — с единицы: R1_1, R1_2.
    for f in panel.board.GetFootprints():
        ref = f.GetReference()
        base, _, n = ref.rpartition("_")
        if base and n.isdigit() and not ref.startswith("KiKit_"):
            f.SetReference(f"{base}_{int(n) + 1}")
    # Заливки — как на плате (refillAllZones=False): копии совпадают с проверенной платой.
    # Перезаливку панели make() проверяет отдельно, на копии.
    panel.save(reconstructArcs=False, refillAllZones=False)
    build.write_dru(os.path.splitext(pcb)[0] + ".kicad_dru")
    return pcb, lines


def size_mm(pcb):
    b = pcbnew.LoadBoard(pcb)
    bb = b.GetBoardEdgesBoundingBox()
    return pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())


def preview(t):
    """Картинка панели с обеих сторон: контур, медь, маска, шёлк — цветами KiCad."""
    out = []
    for side, layers, mirror in (("front", "Edge.Cuts,F.Cu,F.Mask,F.SilkS,F.Fab", False),
                                 ("back", "Edge.Cuts,B.Cu,B.Mask,B.SilkS,B.Fab", True)):
        svg = os.path.join(t.work, f"panel-{side}.svg")
        cmd = ["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--exclude-drawing-sheet",
               "--page-size-mode", "2", "-o", svg, t.pcb]
        if mirror:
            cmd.insert(4, "--mirror")
        subprocess.run(cmd, check=True, capture_output=True)
        png = t.path(f"-{side}.png")
        if build.to_png(svg, png, width=2400):
            out.append(png)
    return out


def stage():
    """Панель и все её файлы — в fab.PANEL_STAGE. out/panel не трогается; не вышло — черновик
    удаляется, а наружу уходит понятная ошибка (код возврата 1)."""
    if os.path.isdir(fab.PANEL_STAGE):
        shutil.rmtree(fab.PANEL_STAGE)
    try:
        make(fab.PANEL_STAGE)
    except BaseException:
        shutil.rmtree(fab.PANEL_STAGE, ignore_errors=True)
        raise


def main():
    """Сам по себе (python panel.py): DRC платы, панель в черновик, черновик — в out/panel."""
    os.makedirs(fab.WORK, exist_ok=True)
    fab.check_board(publish=False)
    stage()
    fab.commit_panel()


def fail(msg):
    sys.exit(f"панель: {msg}; out/panel не тронута")


def make(dest):
    pcb, lines = build_panel(dest)
    t = fab.Target(pcb, dest, fab.PANEL_NAME, copies=("_1", "_2"))
    os.makedirs(t.work, exist_ok=True)
    w, h = size_mm(t.pcb)
    print(f"панель: {w:.1f} × {h:.1f} мм → {os.path.relpath(t.pcb, fab.REV)}")
    if w < 70 or h < 70:
        fail("меньше 70 × 70 мм")
    # DRC — отдельным процессом: в этом pcbnew уже открыт проект исходной платы, и плата панели
    # загрузилась бы без своих классов цепей (с зазорами KiCad по умолчанию).
    if subprocess.run([sys.executable, os.path.join(HERE, "fab.py"), "--check", t.pcb, t.path("-drc.rpt")]).returncode:
        fail("DRC не прошёл")
    # Медь у перемычек — тоже отдельным процессом, на копии (copper_check): отверстия mouse bites
    # в 0.25 мм от меди, медь в 0.3 мм от линий разлома — и после перезаливки, DRC перезалитой чистый.
    lines_json = os.path.join(t.work, "tab-lines.json")
    with open(lines_json, "w") as f:
        json.dump(lines, f)
    if subprocess.run([sys.executable, os.path.abspath(__file__), "--copper-check", t.pcb, lines_json]).returncode:
        fail("медь у перемычек ближе нормы (выше) или DRC перезалитой панели не чистый")
    fab.outputs(t)
    for p in preview(t):
        print("картинка панели:", os.path.relpath(p, fab.REV))
    print("обломки перемычек на краях платы (мм, координаты платы):",
          ", ".join(f"{e} {a}–{b}" for e, a, b in stubs()))


def copper(board, fills_only=False):
    """Медь панели по слоям в мм (shapely): заливки, а без fills_only — ещё дорожки, переходные
    и площадки с медью."""
    from shapely.geometry import Point
    import shapely.ops
    to_mm = pcbnew.ToMM
    layers = {board.GetLayerName(l): l for l in board.GetEnabledLayers().CuStack()}
    out = {name: [] for name in layers}

    def poly_set(ps):
        res = []
        for i in range(ps.OutlineCount()):
            o = ps.COutline(i)
            res.append(Polygon([(to_mm(o.CPoint(k).x), to_mm(o.CPoint(k).y)) for k in range(o.PointCount())]))
        return res

    for z in board.Zones():
        if not z.GetIsRuleArea():
            for layer in z.GetLayerSet().Seq():
                out[board.GetLayerName(layer)] += poly_set(z.GetFilledPolysList(layer))
    if not fills_only:
        for t in board.GetTracks():
            if isinstance(t, pcbnew.PCB_VIA):
                c = t.GetPosition()
                for name in out:
                    out[name].append(Point(to_mm(c.x), to_mm(c.y)).buffer(to_mm(t.GetWidth()) / 2, 16))
            else:
                a, b = t.GetStart(), t.GetEnd()
                out[board.GetLayerName(t.GetLayer())].append(
                    LineString([(to_mm(a.x), to_mm(a.y)), (to_mm(b.x), to_mm(b.y))]).buffer(to_mm(t.GetWidth()) / 2, 16))
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
                    continue
                shape = poly_set(pad.GetEffectivePolygon())
                for name, layer in layers.items():
                    if pad.IsOnLayer(layer):
                        out[name] += shape
    return {name: shapely.ops.unary_union(g) for name, g in out.items() if g}


def bite_holes(board):
    """Отверстия mouse bites панели: [(x, y, радиус)] в мм."""
    to_mm = pcbnew.ToMM
    out = []
    for fp in board.GetFootprints():
        if fp.GetReference().startswith("KiKit_MB"):
            for pad in fp.Pads():
                c = pad.GetPosition()
                out.append((to_mm(c.x), to_mm(c.y), to_mm(pad.GetDrillSize().x) / 2))
    return out


def copper_check(pcb, lines_json, clearance=0.3, bite_gap=-BITE_OFFSET):
    """Проверки меди у перемычек, которых DRC KiCad не видит (поперёк перемычки нет контура,
    а отверстие без металлизации над заливкой он не ловит). На копии панели pcb:
      - отверстия mouse bites — не ближе bite_gap (0.25 мм) к любой меди на любом слое;
      - медь — не ближе clearance к каждой линии разлома, на каждом слое, как есть и после
        перезаливки всех зон; DRC перезалитой панели — чистый.
    Выход 1, если что-то не так."""
    from shapely.geometry import Point
    work = os.path.join(fab.WORK, "panel-refill")
    if os.path.isdir(work):
        shutil.rmtree(work)
    os.makedirs(work)
    base = os.path.splitext(pcb)[0]
    copy = os.path.join(work, os.path.basename(base) + "-refill")
    for ext in (".kicad_pcb", ".kicad_pro", ".kicad_dru"):
        shutil.copyfile(base + ext, copy + ext)
    board = pcbnew.LoadBoard(copy + ".kicad_pcb")
    lines = json.load(open(lines_json))
    bad = []

    def to_lines(cu, what):
        worst = None
        for name, g in sorted(cu.items()):
            for c, edge, a, b, (x0, y0, x1, y1) in lines:
                d = g.distance(LineString([(x0, y0), (x1, y1)]))
                worst = d if worst is None else min(worst, d)
                if d < clearance - 0.002:
                    bad.append(f"{what}: копия {c}, {edge} {a}–{b}, {name}: медь в {d:.3f} мм от линии разлома")
        return worst

    cu = copper(board)
    holes = bite_holes(board)
    near = min((g.distance(Point(x, y)) - r, name) for x, y, r in holes for name, g in cu.items())
    if near[0] < bite_gap - 0.002:
        bad.append(f"отверстие mouse bite в {near[0]:.3f} мм от меди ({near[1]}), нужно от {bite_gap:.2f}")
    as_is = to_lines(cu, "как есть")
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    refilled = to_lines(copper(board, fills_only=True), "после перезаливки")
    found, errors, _ = build.drc(board, copy + "-drc.rpt")
    print(f"медь у перемычек: {len(holes)} отверстий mouse bites — до меди не ближе {near[0]:.3f} мм; "
          f"до {len(lines)} линий разлома на {len(cu)} слоях — {as_is:.3f} мм, после перезаливки {refilled:.3f} мм; "
          f"DRC перезалитой: ошибок {errors}, несоединённых {found.get('unconnected pads', 0)}")
    for line in bad:
        print("  ", line)
    if bad or errors or found.get("unconnected pads"):
        sys.exit(1)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--stage"]:
        stage()
    elif sys.argv[1:2] == ["--copper-check"]:
        copper_check(sys.argv[2], sys.argv[3])
    else:
        main()
