"""Панель для заводского монтажа: две платы, рамка, фрезерованные пазы и перемычки с mouse bites.

Зачем. Детали стоят с двух сторон, а такой монтаж у JLCPCB — только Standard PCBA: плата или
панель от 70 × 70 мм с технологическими полями, реперными точками и технологическими
отверстиями. Плата 30 × 52.6 мм (с ушком) меньше, а у двух её краёв ничего нельзя:
  - правый: модуль U1 в 0.25 мм от края, толкатель кнопки SW1 выступает за край на ~0.2 мм;
  - нижний: корпус USB-C J1 выступает на ~0.7 мм, TVS D3 — в 0.95 мм от края.
Поэтому нигде нет V-cut, а перемычки — только на левом крае и на верхнем левее ушка.

Раскладка (KiKit 1.5):
  - 1 ряд × 2 платы, вторая повёрнута на 180°: правые края обеих смотрят в общий паз
    посередине, нижние — в пазы у рамки, и с этих сторон перемычек нет;
  - сплошная рамка (tightframe): материал везде вокруг плат, кроме фрезерованного паза
    PANEL_SLOT вдоль контура каждой платы; ширина рамки снаружи — FRAME, вся панель не меньше
    MIN_SIZE по обеим сторонам;
  - перемычки по TABS с mouse bites по рекомендации JLCPCB: отверстия 0.6 мм, между краями
    отверстий 0.4 мм (шаг 1.0), в перемычке 5 мм — 5 отверстий. Где у края места меньше
    (между R3 и R4, у C15 сверху), перемычка уже и отверстий 3–4. Отверстия сдвинуты наружу,
    в перемычку, на BITE_OFFSET: заливки платы стоят в 0.3 мм от края, и отверстие не подходит
    к меди ближе 0.25 мм. Обломки перемычек поэтому выступают за край на ~0.3 мм — их
    сошлифовывают заподлицо (README платы и корпуса);
  - 3 реперные точки (медь 1 мм, окно в маске 2 мм) с обеих сторон в трёх углах рамки —
    в 5 мм от краёв панели (JLCPCB: от 3.85 мм, PCBWay: от 5 мм);
  - 4 технологических отверстия 1.152 мм без металлизации, окно маски +0.148 мм (JLCPCB),
    по углам рамки;
  - меди на рамке нет, кроме реперных точек.

Результат — out/panel/: плата панели (keychain-rev-b-panel.kicad_pcb), отчёт DRC, Gerber,
BOM и CPL для JLCPCB, BOM и расстановка для PCBWay, сборочные чертежи и картинка панели.
Обозначения на панели — с номером копии: R1_1 на левой плате, R1_2 на правой (повёрнутой).

Запуск — из build.py fab (или build.py panel). Нужен Python, в котором есть и pcbnew KiCad 7,
и KiKit 1.5.1:
    python3 -m venv --system-site-packages ~/kikit-venv && ~/kikit-venv/bin/pip install kikit==1.5.1
    KIKIT_PYTHON=~/kikit-venv/bin/python python3 build.py fab
или напрямую: ~/kikit-venv/bin/python hardware/rev-b/gen/panel.py
"""
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

PANEL_SLOT = 2.0    # мм: фрезерованный паз вокруг каждой платы и между ними
FRAME = 5.0         # мм: рамка снаружи
MIN_SIZE = 72.0     # мм: панель не меньше по обеим сторонам (JLCPCB Standard PCBA — от 70 × 70)
# mouse bites: отверстие, шаг (KiKit делит длину реза на целое число шагов не длиннее этого —
# выходит ровно 1.0 мм), сдвиг (минус — наружу, в перемычку), продление реза за края перемычки.
BITE_DRILL, BITE_SPACING, BITE_OFFSET, BITE_PROLONG = 0.6, 0.98, -0.25, 0.5
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
#   сверху, левее ушка: угол … C15 (x 8.0, y от 2.1) — 2.5–6.5, до C15 по диагонали 2.6 мм.
# Справа (U1 в 0.25 мм, толкатель SW1 за краем) и снизу (USB-C J1 за краем, D3 у края) — ничего.
TABS = [
    ("left", 5.75, 5.0),
    ("left", 27.0, 5.0),
    ("left", 36.67, 3.0),
    ("top", 4.5, 4.0),
]


def stubs():
    """Где на краю платы останутся обломки перемычек: [(край, от, до)] в мм."""
    return [(edge, round(c - w / 2, 2), round(c + w / 2, 2)) for edge, c, w in TABS]


def tab_annotations(sub, rotated):
    """Перемычки одной копии платы на панели. sub — её подложка; rotated — повёрнута на 180°."""
    minx, miny, maxx, maxy = sub.bounds()       # контур копии на панели, единицы KiCad
    oy = netlist.TAB_CY - netlist.TAB_R         # верх ушка; контур платы: x 0…W, y oy…H
    w, h = netlist.BOARD_W, netlist.BOARD_H - oy
    out = []
    for edge, c, width in TABS:
        # начало перемычки — в пазу в 1 мм от края, направление — к плате
        (px, py), d = ((-1.0, c), (1, 0)) if edge == "left" else ((c, -1.0), (0, 1))
        fx, fy = px / w, (py - oy) / h
        if rotated:  # повёрнутая копия: зеркально по обеим осям
            fx, fy, d = 1 - fx, 1 - fy, (-d[0], -d[1])
        out.append(TabAnnotation(None, (minx + fx * (maxx - minx), miny + fy * (maxy - miny)), d, width * mm))
    return out


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


def build_panel():
    """Собирает плату панели в fab.PANEL_PCB по шагам kikit panelize, со своими перемычками."""
    os.makedirs(fab.PANEL_DIR, exist_ok=True)
    p = preset()
    board = pcbnew.LoadBoard(fab.PCB)
    panel = Panel(fab.PANEL_PCB)
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
        sys.exit(f"панель: перемычек {len(cuts)}, а нужно {len(TABS) * len(subs)}")
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
    # Обозначения копий — с единицы: R1_1, R1_2.
    for f in panel.board.GetFootprints():
        ref = f.GetReference()
        base, _, n = ref.rpartition("_")
        if base and n.isdigit() and not ref.startswith("KiKit_"):
            f.SetReference(f"{base}_{int(n) + 1}")
    panel.save(reconstructArcs=False, refillAllZones=False)
    build.write_dru(os.path.splitext(fab.PANEL_PCB)[0] + ".kicad_dru")
    return fab.PANEL_PCB


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


def main():
    """Панель и её файлы. Не вышло — out/panel удаляется целиком: полупанель хуже, чем никакой."""
    if os.path.isdir(fab.PANEL_DIR):
        shutil.rmtree(fab.PANEL_DIR)
    try:
        make()
    except BaseException:
        shutil.rmtree(fab.PANEL_DIR, ignore_errors=True)
        raise


def make():
    t = fab.Target(build_panel(), fab.PANEL_DIR, fab.PANEL_NAME, copies=("_1", "_2"))
    os.makedirs(t.work, exist_ok=True)
    w, h = size_mm(t.pcb)
    print(f"панель: {w:.1f} × {h:.1f} мм → {os.path.relpath(t.pcb, fab.REV)}")
    if w < 70 or h < 70:
        sys.exit("панель меньше 70 × 70 мм")
    # DRC — отдельным процессом: в этом pcbnew уже открыт проект исходной платы, и плата панели
    # загрузилась бы без своих классов цепей (с зазорами KiCad по умолчанию).
    subprocess.run([sys.executable, os.path.join(HERE, "fab.py"), "--check", t.pcb, t.path("-drc.rpt")], check=True)
    fab.outputs(t)
    for p in preview(t):
        print("картинка панели:", os.path.relpath(p, fab.REV))
    print("обломки перемычек на краях платы (мм, координаты платы):",
          ", ".join(f"{e} {a}–{b}" for e, a, b in stubs()))


if __name__ == "__main__":
    main()
