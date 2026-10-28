"""Свои посадочные места: модуль экрана, батарея, вибромотор.

Пишет библиотеку hardware/rev-b/Keychain.pretty. Размеры модуля экрана у
продавцов немного разные: сверь их штангенциркулем с купленным модулем,
поправь OLED ниже и перезапусти build.py.
"""
import math
import os

# Белый модуль 0.96" SSD1306 128×64, I2C, 4 штыря. Все размеры в мм.
OLED = {
    "w": 27.3, "h": 27.8,          # плата модуля
    "pin_y": 1.3,                  # ось штырей от верхнего края платы модуля
    "hole_dx": 1.9, "hole_dy": 2.0,  # центры отверстий M2 от краёв
    "hole_d": 2.2,
    "glass_w": 26.7, "glass_h": 19.3, "glass_y": 4.6,  # стекло от верхнего края модуля
    "view_w": 21.7, "view_h": 10.9, "view_y": 7.4,     # активная область (128×64)
}

# Ячейка Li-Po 402025: 4 × 20 × 25 мм.
CELL = {"w": 20.0, "h": 25.0, "pad_x": 12.4, "pad_y": (-5.0, -2.0)}

# Вибромотор-монетка 0820: Ø8 × 2 мм, провода — на площадки сбоку.
MOTOR = {"d": 8.0, "pad_x": 5.2, "pad_y": (-1.4, 1.4)}

HEAD = '(footprint "{name}" (version 20221018) (generator "keychain")\n  (layer "F.Cu")\n  (descr "{descr}")\n  (attr {attr})\n'


SILK_LINE = 0.16  # мм: линия знаков на шёлке — минимум JLCPCB 0.153


def _text(kind, text, x, y, layer, size=0.8, hide=False, width=None):
    """Надпись; на шёлке линия не тоньше SILK_LINE. width — ширина знака, если уже высоты."""
    h = " hide" if hide else ""
    t = max(SILK_LINE, size * 0.16) if "SilkS" in layer else size * 0.15
    return (f'  (fp_text {kind} "{text}" (at {x:.3f} {y:.3f}) (layer "{layer}"){h}\n'
            f'    (effects (font (size {size} {width or size}) (thickness {t:.3f}))))\n')  # высота, ширина


def _line(x0, y0, x1, y1, layer, w=0.1):
    return (f'  (fp_line (start {x0:.3f} {y0:.3f}) (end {x1:.3f} {y1:.3f}) '
            f'(stroke (width {w}) (type solid)) (layer "{layer}"))\n')


def _rect(x0, y0, x1, y1, layer, w=0.1):
    return (f'  (fp_rect (start {x0:.3f} {y0:.3f}) (end {x1:.3f} {y1:.3f}) '
            f'(stroke (width {w}) (type solid)) (fill none) (layer "{layer}"))\n')


def _circle(x, y, r, layer, w=0.1):
    return (f'  (fp_circle (center {x:.3f} {y:.3f}) (end {x + r:.3f} {y:.3f}) '
            f'(stroke (width {w}) (type solid)) (fill none) (layer "{layer}"))\n')


def oled_module():
    o = OLED
    # Начало координат — середина ряда штырей; модуль висит над платой на высоте батареи.
    left, right = -o["w"] / 2, o["w"] / 2
    top, bottom = -o["pin_y"], o["h"] - o["pin_y"]
    s = HEAD.format(name="OLED_SSD1306_0.96in_I2C_Module", attr="through_hole",
                    descr="Модуль OLED 0.96 дюйма SSD1306 128x64 I2C, 4 штыря 2.54 мм, "
                          "крепёж M2 по углам. Стоит над батареей на ушках рамок корпуса, 6/7/8 мм")
    s += _text("reference", "REF**", 0, -2.4, "F.SilkS")
    s += _text("value", "OLED", 0, o["h"] / 2, "F.Fab")
    s += _rect(left, top, right, bottom, "F.Fab")
    gx0, gy0 = -o["glass_w"] / 2, top + o["glass_y"]
    s += _rect(gx0, gy0, -gx0, gy0 + o["glass_h"], "F.Fab", 0.05)
    vx0, vy0 = -o["view_w"] / 2, top + o["view_y"]
    s += _rect(vx0, vy0, -vx0, vy0 + o["view_h"], "F.Fab", 0.05)
    s += _text("user", "128x64", 0, vy0 + o["view_h"] / 2, "F.Fab", 1.0)
    # Контур модуля на шёлке — по углам, чтобы не лезть на площадки.
    for sx, sy in ((left, top), (right, top), (left, bottom), (right, bottom)):
        dx = 2.0 if sx < 0 else -2.0
        dy = 2.0 if sy < 0 else -2.0
        s += _line(sx, sy, sx + dx, sy, "F.SilkS", 0.15)
        s += _line(sx, sy, sx, sy + dy, "F.SilkS", 0.15)
    # Подписи штырей — 1.0 мм, знак уже (0.75), чтобы SCL и SDA не сливались при шаге 2.54.
    for i, label in enumerate(("1", "2", "SCL", "SDA")):
        s += _text("user", label, -3.81 + 2.54 * i, 1.8, "F.SilkS", 1.0, width=0.75)
    # Двор: только вокруг штырей и крепежа, под модулем живёт батарея.
    s += _rect(-5.1, -1.3, 5.1, 1.3, "F.CrtYd", 0.05)
    holes = []
    for hx in (left + o["hole_dx"], right - o["hole_dx"]):
        for hy in (top + o["hole_dy"], bottom - o["hole_dy"]):
            holes.append((hx, hy))
            s += _circle(hx, hy, 1.8, "F.CrtYd", 0.05)   # винт M2; ушки рамок корпуса шире, их проверяет case.py
            s += _circle(hx, hy, 2.35, "B.CrtYd", 0.05)  # гайка M2 с обратной стороны
    for i in range(4):
        shape = "rect" if i == 0 else "circle"
        s += (f'  (pad "{i + 1}" thru_hole {shape} (at {-3.81 + 2.54 * i:.3f} 0) (size 1.7 1.7) (drill 1.0) '
              f'(layers "*.Cu" "*.Mask"))\n')
    for hx, hy in holes:
        s += (f'  (pad "" np_thru_hole circle (at {hx:.3f} {hy:.3f}) (size {o["hole_d"]} {o["hole_d"]}) '
              f'(drill {o["hole_d"]}) (layers "*.Cu" "*.Mask"))\n')
    return s + ")\n"


def lipo_pads():
    c = CELL
    s = HEAD.format(name="LiPo_Pads_20x25mm", attr="smd",
                    descr="Площадки под провода ячейки Li-Po 402025/502025/602025, контур ячейки на слое Fab")
    s += _text("reference", "REF**", c["pad_x"], c["pad_y"][0] - 2.0, "F.SilkS", 0.8, hide=True)
    s += _text("value", "LiPo", 0, 0, "F.Fab")
    s += _rect(-c["w"] / 2, -c["h"] / 2, c["w"] / 2, c["h"] / 2, "F.Fab")
    s += _text("user", "LiPo 20x25", 0, 2.0, "F.Fab", 1.0)
    for i, (label, y) in enumerate(zip(("+", "-"), c["pad_y"])):
        # Без пасты: провода паяются руками, капли припоя с завода только мешали бы.
        s += (f'  (pad "{i + 1}" smd roundrect (at {c["pad_x"]:.3f} {y:.3f}) (size 2.0 1.6) '
              f'(layers "F.Cu" "F.Mask") (roundrect_rratio 0.25))\n')
        s += _text("user", label, c["pad_x"] - 2.0, y, "F.SilkS", 1.0)
    y0, y1 = c["pad_y"][0] - 1.05, c["pad_y"][1] + 1.05
    s += _rect(c["pad_x"] - 2.7, y0, c["pad_x"] + 1.25, y1, "F.CrtYd", 0.05)
    return s + ")\n"


def vibro_motor():
    m = MOTOR
    r = m["d"] / 2
    s = HEAD.format(name="VibroMotor_Coin_D8mm", attr="smd",
                    descr="Вибромотор-монетка 8 мм на клейком слое, провода на площадки")
    s += _text("reference", "REF**", 0, 0, "F.SilkS", 0.8, hide=True)
    s += _text("value", "Vibro", 0, 1.5, "F.Fab")
    s += _circle(0, 0, r, "F.Fab")
    s += _circle(0, 0, r + 0.15, "F.SilkS", 0.12)
    s += _text("user", "VIBRO", 0, 0, "F.SilkS", 1.0)
    # Двор — один замкнутый контур: круг мотора и выступ под площадки.
    rc, tx, ty = r + 0.3, m["pad_x"] + 0.95, m["pad_y"][1] + 0.85
    a0 = math.atan2(ty, math.sqrt(rc * rc - ty * ty))
    pts = [(tx, -ty), (tx, ty)]
    for i in range(41):
        a = a0 + (2 * math.pi - 2 * a0) * i / 40
        pts.append((rc * math.cos(a), rc * math.sin(a)))
    s += ('  (fp_poly (pts ' + " ".join(f"(xy {x:.3f} {y:.3f})" for x, y in pts) +
          ') (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))\n')
    for i, (label, y) in enumerate(zip(("+", "-"), m["pad_y"])):
        s += (f'  (pad "{i + 1}" smd roundrect (at {m["pad_x"]:.3f} {y:.3f}) (size 1.4 1.2) '
              f'(layers "F.Cu" "F.Mask") (roundrect_rratio 0.25))\n')
        s += _text("user", label, m["pad_x"], y + (-1.65 if i == 0 else 1.65), "F.SilkS", 1.0)
    return s + ")\n"


def write(lib_dir):
    os.makedirs(lib_dir, exist_ok=True)
    for name, text in (("OLED_SSD1306_0.96in_I2C_Module", oled_module()),
                       ("LiPo_Pads_20x25mm", lipo_pads()),
                       ("VibroMotor_Coin_D8mm", vibro_motor())):
        with open(os.path.join(lib_dir, name + ".kicad_mod"), "w") as f:
            f.write(text)


if __name__ == "__main__":
    write(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Keychain.pretty"))
