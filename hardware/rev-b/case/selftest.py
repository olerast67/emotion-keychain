"""Проверка проверок case.py: каждая нарочная поломка корпуса должна дать свою ошибку.

  python3 selftest.py

Файлы не пишет. Ломает модель в памяти (толкатель без крыльев, нет отверстия пищалки,
провода у винта, окно к ячейке и т. п.) и смотрит, что check() это ловит; плюс пересчёт по
мерам листов (--sheet, --module): то, что правят при сборке, — предупреждение с размерами, а не
ошибка, — и правило длины винтов. Любой промах — выход с кодом 1.
"""
import contextlib
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import case  # noqa: E402
from case import circle, minus, rect, union  # noqa: E402

case.board_data = (lambda bd: lambda: bd)(case.board_data())  # плату читаем один раз

failed = []


def report(ok, what, got):
    print(("ок   " if ok else "ОШИБКА ") + what + ("" if ok else f": {got}"))
    if not ok:
        failed.append(what)


def copy(model):
    parts, solids, info = model
    return {k: dict(p) for k, p in parts.items()}, solids, dict(info)


def broken(v, what, mutate, expect):
    """Ломает модель версии v и ждёт ошибку, в которой есть все слова expect."""
    parts, solids, info = copy(BASE[v])
    mutate(parts, info)
    err, _ = case.check(v, parts, solids, info)
    hit = [e for e in err if all(w in e for w in expect)]
    report(bool(hit), f"{v.upper()}: {what}", err[:4])


def nominal():
    case.SHEET_REAL.clear()
    case.MEASURED = False
    case.MODULE_T = MODULE_T


MODULE_T = case.MODULE_T
BASE = {v: case.build(v) for v in case.VARIANTS}

# Целые модели проходят.
for v, model in BASE.items():
    err, warn = case.check(v, *model)
    report(not err and not warn, f"{v.upper()}: модель без ошибок и предупреждений", err + warn)

# Толкатель кнопки и край платы.


def old_plunger(parts, info):
    """Толкатель до правки: без крыльев, полка 0.73 мм, стенка без прилива."""
    x0, po, y = info["x0"], case.W + case.WALL - case.MIN_WEB, info["btn_y"]
    parts["button"]["shape"] = union(rect(x0, y - 2.4, po, y + 2.4),
                                     rect(po - 0.01, y - 1.1, case.W + case.WALL + case.BTN_OUT, y + 1.1))
    info["pocket_out"] = po


broken("s", "толкатель без крыльев поднимется к плате", old_plunger, ["крылья заходят под плату"])
broken("s", "толкатель без крыльев упрётся в край платы", old_plunger, ["упрётся в край платы"])
broken("s", "полка толкателя 0.73 мм", old_plunger, ["полка толкателя тоньше"])
broken("s", "тонкие плечи толкателя", old_plunger, ["толкатель кнопки:", "тоньше 1.5"])
broken("m", "крылья почти не под платой",
       lambda p, i: p["button"].update(shape=minus(p["button"]["shape"], rect(20, 0, 29.6, 50))),
       ["крылья заходят под плату"])
broken("l", "крыло только с одной стороны",
       lambda p, i: p["button"].update(shape=minus(p["button"]["shape"], rect(20, 27.85, 30.25, 50))),
       ["крылья заходят под плату"])
broken("s", "под платой лишь угол крыла (0.4 мм по y)",
       lambda p, i: p["button"].update(shape=minus(p["button"]["shape"], rect(20, 0, 29.6, 22.5))),
       ["полосой короче"])
broken("s", "крыло на площадке кнопки",
       lambda p, i: p["button"].update(shape=union(p["button"]["shape"], rect(28.8, 22.8, 29.6, 23.2))),
       ["площадкам кнопки"])
broken("s", "толкатель в корпус кнопки",
       lambda p, i: p["button"].update(shape=union(p["button"]["shape"], rect(29.9, 24.0, 30.3, 24.4))),
       ["корпусу кнопки"])
broken("s", "карман короче хода",
       lambda p, i: p["frame_back"].update(shape=union(p["frame_back"]["shape"], rect(28.1, 27.9, 28.45, 29.5))),
       ["упирается в рамку оборота"])

# Пищалка.
broken("s", "нет отверстия против пищалки",
       lambda p, i: p["bottom"].update(circles=[c for c in p["bottom"]["circles"] if c[2] != case.BUZZER_D]),
       ["нет отверстия против пищалки"])
broken("s", "рамка оборота над пищалкой",
       lambda p, i: p["frame_back"].update(shape=union(p["frame_back"]["shape"],
                                                       circle(i["board"]["bz"][0], i["board"]["bz"][1] + 2.5, 1.0))),
       ["отверстию пищалки"])

# Провода ячейки.
broken("s", "рамка на пути проводов ячейки",
       lambda p, i: p["frame_cell1"].update(shape=union(p["frame_cell1"]["shape"], rect(25.0, 24.0, 25.5, 25.0))),
       ["нет места проводам"])
case.LEAD_PATH = 1.3
broken("s", "провода ячейки у винта", lambda p, i: None, ["провода ячейки", "к винту"])
case.LEAD_PATH = 0.6

# Ячейка закрыта со всех сторон, вилка USB-C — под нижней рамкой ячейки без окна.


def usb_window(p, i):
    """Окно под вилку, как в нижней рамке ячейки до правки: открывает торец ячейки."""
    b = i["board"]
    ux, g = b["usb_x"], case.PLUG_GAP
    p["frame_cell1"].update(shape=minus(p["frame_cell1"]["shape"],
                                        rect(ux - case.PLUG_W / 2 - g, b["usb_face"] - g, ux + case.PLUG_W / 2 + g,
                                             case.H + case.WALL + 1)))


for v in case.VARIANTS:
    broken(v, "окно USB-C в нижней рамке ячейки", usb_window, ["нижняя рамка ячейки", "ячейка открыта наружу"])
broken("m", "щель к ячейке в верхней рамке",
       lambda p, i: p["frame_cell2"].update(shape=minus(p["frame_cell2"]["shape"], rect(-3, 34.5, 6, 35.5))),
       ["верхняя рамка ячейки", "ячейка открыта наружу"])
broken("s", "вилка выше лица платы, чем допуск касания",
       lambda p, i: i.update(plug_z=(i["plug_z"][0], case.PLUG_TOUCH + 0.05)), ["вилка USB-C", "допуск касания"])
broken("s", "рамка оборота без окна USB-C",
       lambda p, i: p["frame_back"].update(shape=union(p["frame_back"]["shape"], rect(10, 46.5, 20, 48.5))),
       ["рамка оборота", "вилка USB-C"])
broken("l", "дно без выреза USB-C",
       lambda p, i: p["bottom"].update(shape=union(p["bottom"]["shape"], rect(10, 46.5, 20, 48.5))),
       ["дно", "вилка USB-C"])

# Винты: запас над гайкой, и подходящей длины может не быть.
for v, (parts, solids, info) in BASE.items():
    n = case.numbers(v, parts, info)
    ok = min(n["screw_out"], n["screw_steel_out"]) >= case.SCREW_MIN_OUT - 1e-9
    report(ok, f"{v.upper()}: винт выходит из гайки не меньше {case.SCREW_MIN_OUT:g} мм "
               f"(M2 × {n['screw']} нейлон, M2 × {n['screw_steel']} сталь)", n)
report(case.numbers("m", *BASE["m"][::2])["screw_steel"] == 25, "M: сталь M2 × 25, а не впритык M2 × 22", "")
saved = case.SCREW_LENGTHS, case.NYLON_LENGTHS
case.SCREW_LENGTHS, case.NYLON_LENGTHS = (16, 18, 20, 22), (16, 20, 22)
broken("l", "нет длинных винтов", lambda p, i: None, ["нет винта"])
case.SCREW_LENGTHS, case.NYLON_LENGTHS = saved

# Меры листов: высоты, трубка и винты пересчитываются, а то, что правят при сборке, —
# предупреждение с мерами и ошибка без них.
n0 = case.numbers("s", *BASE["s"][::2])
case.SHEET_REAL.update({3.0: 2.8})
case.MEASURED = True
parts, solids, info = case.build("s")
err, warn = case.check("s", parts, solids, info)
n1 = case.numbers("s", parts, info)
report(not err, "S, листы 3 мм по 2.8: без ошибок", err)
report(any("ячейка 402025" in w for w in warn) and any("рамка экрана ниже крышки" in w for w in warn),
       "S, листы 3 мм по 2.8: ячейка не входит и рамка экрана низкая — предупреждения", warn)
report(abs(n0["free"][1] - n1["free"][1] - 0.4) < 1e-6 and abs(n0["tube_len"] - n1["tube_len"] - 0.4) < 0.051
       and abs(n0["screw_need"] - n1["screw_need"] - 0.4) < 1e-6,
       "S, листы 3 мм по 2.8: свободная высота, трубка и винт короче на 0.4 мм", (n0, n1))
case.MEASURED = False
err, warn = case.check("s", parts, solids, info)
report(any("ячейка 402025" in e for e in err), "те же листы без режима мер — ошибка", err)
nominal()
case.MODULE_T = 1.6
case.MEASURED = True
parts, solids, info = case.build("m")
n = case.numbers("m", parts, info)
report(n["module_t"] == 1.6 and abs(n["screw_need"] - 21.8) < 1e-6, "M, плата модуля 1.6: винт длиннее на 0.4 мм", n)
nominal()


def measured(sheets, module=None):
    """Все версии по мерам: {версия: (ошибки, предупреждения, числа, детали)}."""
    nominal()
    case.SHEET_REAL.update(sheets)
    if module is not None:
        case.MODULE_T = module
    case.MEASURED = True
    res = {}
    for v in case.VARIANTS:
        parts, solids, info = case.build(v)
        err, warn = case.check(v, parts, solids, info)
        res[v] = (err, warn, None if err else case.numbers(v, parts, info), parts)
    return res


# Толстый лист 3 мм: рамка экрана выше «плата модуля + шайба». Её подшлифуют — предупреждение
# с размерами, модель уже подшлифована, а высоты, трубка и винты считаются по толстым рамкам ячейки.
res = measured({3.0: 3.3})
report(all(not e for e, _, _, _ in res.values()), "листы 3 мм по 3.3: без ошибок", [e for e, _, _, _ in res.values()])
report(all(any("подшлифуйте её до 2.90…3.10" in w for w in ws) for _, ws, _, _ in res.values()),
       "листы 3 мм по 3.3: рамку экрана подшлифовать до 2.90…3.10 мм — предупреждение", res["s"][1])
err, warn, n1, parts = res["s"]
report(n1 is not None and abs(n1["free"][1] - n0["free"][1] - 0.6) < 1e-6
       and abs(n1["screw_need"] - n0["screw_need"] - 0.6) < 1e-6 and abs(n1["tube_len"] - n0["tube_len"] - 0.6) < 0.051
       and abs(parts["frame_screen"]["t"] - 3.1) < 1e-9,
       "S, листы 3 мм по 3.3: свободная высота, трубка и винт на 0.6 мм больше, рамка экрана в модели 3.1", n1)
case.MEASURED = False
err, _ = case.check("s", *case.build("s"))
report(any("рамка экрана" in e for e in err), "те же листы без режима мер — ошибка", err)
# Тонкая плата модуля и тонкие шайбы: штыри над модулем укоротят — тоже предупреждение.
for sheets, module, what in (({3.0: 3.05}, 1.0, "плата модуля 1.0, листы 3 мм по 3.05"),
                             ({2.0: 1.75}, None, "листы 2 мм по 1.75"),
                             ({2.0: 1.9, 3.0: 3.15}, None, "листы 2 мм по 1.9 и 3 мм по 3.15")):
    res = measured(sheets, module)
    report(all(not e for e, _, _, _ in res.values()), f"{what}: без ошибок", [e for e, _, _, _ in res.values()])
    report(all(any("укоротите их над модулем" in w for w in ws) for _, ws, _, _ in res.values()),
           f"{what}: штыри над модулем укоротить — предупреждение", res["s"][1])
# Все листы на 0.3 толще и плата модуля 1.6: нейлоновых M2 такой длины нет, по мерам хватит стальных.
res = measured({2.0: 2.3, 3.0: 3.3, 4.0: 4.3, 5.0: 5.3}, 1.6)
err, warn, n, _ = res["l"]
report(not err and n["screw"] == 30 and not n["screw_nylon"] and any("нейлоновых M2" in w for w in warn),
       "L, все листы на 0.3 толще, плата модуля 1.6: стальные M2 × 30 — предупреждение, не ошибка", (err, warn))
nominal()
args = case.parse_args(["--sheet", "3=2.86,4=4.05", "--sheet", "2=1.9", "--module", "1.0"])
report(args == ({3.0: 2.86, 4.0: 4.05, 2.0: 1.9}, 1.0), "разбор --sheet и --module", args)
for bad in (["--sheet", "3=3.9"], ["--sheet", "7=7.1"], ["--sheet", "3"], ["--module", "3"]):
    try:
        with contextlib.redirect_stderr(io.StringIO()):
            case.parse_args(bad)
        report(False, f"{' '.join(bad)} — отказ", "принято")
    except SystemExit:
        report(True, f"{' '.join(bad)} — отказ", "")

print("Все проверки ловят свои поломки." if not failed else f"Не поймано: {len(failed)}")
sys.exit(1 if failed else 0)
