"""3D-просмотр собранного брелока: плата, детали, модуль экрана, ячейка и корпус.

Собирает одну страницу out/keychain-3d.html из шаблона viewer.html: текстуры платы
(чёрная маска, золото площадок, белая шелкография) рисуются из слоёв KiCad, лицо на
экране — кадр из facegen, детали — коробки по дворам с типовыми высотами, корпус —
слои всех трёх версий из case-model.json (сначала запустите case.py).

  python3 viewer.py                      # out/keychain-3d.html
  python3 viewer.py --render PNG         # картинка платы: лицо и оборот рядом
  python3 viewer.py --shots DIR          # case-3d.png и case-exploded.png для README

facegen берётся из build/ в корне репозитория или из переменной FACEGEN. Для --shots
нужен Node.js с Playwright (shots.js); если браузер не достаёт до CDN, укажите в
THREE_DIR распакованный пакет npm three@0.128.0.
"""
import base64
import io
import json
import os
import re
import subprocess
import sys
import tempfile

import pcbnew
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
REV = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(REV))
sys.path.insert(0, os.path.join(REV, "gen"))
import footprints  # noqa: E402
import netlist  # noqa: E402

sys.path.insert(0, HERE)
import case  # noqa: E402

PCB = os.path.join(REV, "keychain-rev-b.kicad_pcb")
OUT = os.path.join(HERE, "out")
PX_PER_MM = 24  # текстуры и картинки платы
CAIROSVG = os.environ.get("CAIROSVG", "cairosvg")
FACEGEN = os.environ.get("FACEGEN", os.path.join(ROOT, "build", "facegen"))

# Высоты корпусов деталей над платой, мм, и цвет.
BODY = {
    "U1": (2.2, "#c8ccd2"), "BZ1": (1.9, "#1b1b1d"), "J1": (3.2, "#b9bec6"), "M1": (2.0, "#a9adb3"),
    "SW1": (2.0, "#d9d9d9"), "U5": (0.7, "#2a2b2e"), "U6": (1.0, "#b9bec6"), "U4": (1.0, "#202124"),
    "LED1": (0.6, "#e0473c"), "D1": (1.1, "#1d1d1f"), "D2": (1.0, "#1d1d1f"),
}
SKIP = {"H1", "J2", "J3", "TP1", "TP2", "TP3", "BT1"}


def body_of(ref, fpname):
    if ref in BODY:
        return BODY[ref]
    if "0402" in fpname:
        return (0.5, "#b08a5a") if ref.startswith("C") else (0.35, "#1c1c1c")
    if "0603" in fpname:
        return (0.8, "#b08a5a") if ref.startswith("C") else (0.45, "#1c1c1c")
    if "SOT" in fpname:
        return (1.1, "#1d1d1f")
    return (0.9, "#202124")


def _render(layers, tmp):
    """Слои KiCad в чёрно-белую картинку; у SVG KiCad свои поля вокруг платы."""
    svg = os.path.join(tmp, "l.svg")
    png = os.path.join(tmp, "l.png")
    subprocess.run(["kicad-cli", "pcb", "export", "svg", "--layers", layers, "--black-and-white",
                    "--exclude-drawing-sheet", "--page-size-mode", "2", "-o", svg, PCB],
                   check=True, capture_output=True)
    head = open(svg).read(2000)
    w_mm = float(re.search(r'width="([\d.]+)mm"', head).group(1))
    subprocess.run([CAIROSVG, svg, "-o", png, "--output-width", str(round(w_mm * PX_PER_MM)), "-b", "white"],
                   check=True, capture_output=True)
    return ImageChops.invert(Image.open(png).convert("L"))


_EDGE = {}


def layer_mask(layers, bbox, tmp):
    """Маска слоёв (255 — рисунок), обрезанная точно по контуру платы bbox."""
    if "box" not in _EDGE:
        _EDGE["box"] = _render("Edge.Cuts", tmp).point(lambda v: 255 if v > 64 else 0).getbbox()
    x0, y0, x1, y1 = _EDGE["box"]
    im = _render(layers, tmp).crop((x0, y0, x1, y1))
    size = (round((bbox[2] - bbox[0]) * PX_PER_MM), round((bbox[3] - bbox[1]) * PX_PER_MM))
    return im.resize(size, Image.LANCZOS)


def board_image(side, bbox, tmp):
    cu, mask, silk = (("F.Cu", "F.Mask", "F.Silkscreen") if side == "F" else ("B.Cu", "B.Mask", "B.Silkscreen"))
    m_cu = layer_mask(cu, bbox, tmp)
    m_open = layer_mask(mask, bbox, tmp)
    m_silk = layer_mask(silk, bbox, tmp)
    size = m_cu.size
    img = Image.new("RGB", size, (22, 24, 28))                      # чёрная маска
    img.paste(Image.new("RGB", size, (40, 44, 50)), mask=m_cu.point(lambda v: 120 if v > 128 else 0))
    img.paste(Image.new("RGB", size, (214, 173, 92)), mask=m_open.point(lambda v: 255 if v > 128 else 0))
    img.paste(Image.new("RGB", size, (236, 236, 232)), mask=m_silk.point(lambda v: 255 if v > 128 else 0))
    return img


def board_texture(side, bbox, tmp):
    img = board_image(side, bbox, tmp)
    buf = io.BytesIO()
    img.save(buf, "JPEG", quality=88)
    return "data:image/jpeg;base64," + base64.b64encode(buf.getvalue()).decode()


def face_texture(tmp, emotion="happy"):
    gif = os.path.join(tmp, "face.gif")
    subprocess.run([FACEGEN, "still", emotion, gif, "--scale", "1", "--raw"],
                   check=True, capture_output=True)
    im = Image.open(gif).convert("RGB")
    w, h = im.size
    im = im.crop(((w - 128) // 2, (h - 64) // 2, (w - 128) // 2 + 128, (h - 64) // 2 + 64))
    px = im.load()
    out = Image.new("RGB", (128, 64))
    po = out.load()
    for y in range(64):
        for x in range(128):
            on = sum(px[x, y]) > 300
            po[x, y] = (175, 235, 255) if on else (6, 10, 14)
    out = out.resize((512, 256), Image.NEAREST)
    buf = io.BytesIO()
    out.save(buf, "PNG")
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def outline_points(board):
    s = pcbnew.SHAPE_POLY_SET()
    board.GetBoardPolygonOutlines(s)
    res = []
    for i in range(s.OutlineCount()):
        rings = [s.COutline(i)] + [s.CHole(i, j) for j in range(s.HoleCount(i))]
        res.append([[(round(pcbnew.ToMM(r.CPoint(k).x), 3), round(pcbnew.ToMM(r.CPoint(k).y), 3))
                     for k in range(r.PointCount())] for r in rings])
    return res


def board_render(path):
    """Картинка для README: лицо и оборот платы рядом, фон прозрачный, отверстия сквозные."""
    from PIL import ImageDraw
    board = pcbnew.LoadBoard(PCB)
    bb = board.GetBoardEdgesBoundingBox()
    bbox = (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))
    with tempfile.TemporaryDirectory() as tmp:
        front, back = board_image("F", bbox, tmp), board_image("B", bbox, tmp)
    w, h = front.size
    sx, sy = w / (bbox[2] - bbox[0]), h / (bbox[3] - bbox[1])
    px = lambda x, y: ((x - bbox[0]) * sx, (y - bbox[1]) * sy)
    alpha = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(alpha)
    for part in outline_points(board):
        d.polygon([px(x, y) for x, y in part[0]], fill=255)
        for hole in part[1:]:
            d.polygon([px(x, y) for x, y in hole], fill=0)
    for fp in board.GetFootprints():
        for p in fp.Pads():
            r = pcbnew.ToMM(p.GetDrillSize().x) / 2
            if r >= 0.3:
                cx, cy = px(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
                d.ellipse((cx - r * sx, cy - r * sy, cx + r * sx, cy + r * sy), fill=0)
    front.putalpha(alpha)
    back = back.transpose(Image.FLIP_LEFT_RIGHT)  # оборот — как его видно, если перевернуть плату
    back.putalpha(alpha.transpose(Image.FLIP_LEFT_RIGHT))
    gap = int(w * 0.18)
    out = Image.new("RGBA", (2 * w + gap, h), (0, 0, 0, 0))
    out.paste(front, (0, 0), front)
    out.paste(back, (w + gap, 0), back)
    out.save(path)
    print(f"{path}: {out.size[0]}×{out.size[1]}")


def shots(out_dir):
    """Картинки корпуса для README: снимки страницы просмотра в Chromium, обрезанные по брелоку."""
    page = os.path.join(OUT, "keychain-3d.html")
    os.makedirs(out_dir, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["node", os.path.join(HERE, "shots.js"), page, tmp], check=True)
        for name in ("case-3d", "case-exploded"):
            im = Image.open(os.path.join(tmp, name + ".png")).convert("RGB")
            bg = Image.new("RGB", im.size, im.getpixel((0, 0)))
            x0, y0, x1, y1 = ImageChops.difference(im, bg).convert("L").point(lambda v: 255 if v > 12 else 0).getbbox()
            m = 16
            im = im.crop((max(0, x0 - m), max(0, y0 - m), min(im.width, x1 + m), min(im.height, y1 + m)))
            path = os.path.join(out_dir, name + ".png")
            im.save(path, optimize=True)
            print(f"{path}: {im.size[0]}×{im.size[1]}")


def main():
    if len(sys.argv) > 2 and sys.argv[1] == "--render":
        return board_render(sys.argv[2])
    if len(sys.argv) > 2 and sys.argv[1] == "--shots":
        return shots(sys.argv[2])
    board = pcbnew.LoadBoard(PCB)
    bb = board.GetBoardEdgesBoundingBox()
    bbox = (pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetRight()), pcbnew.ToMM(bb.GetBottom()))
    holes = []
    parts = []
    for fp in board.GetFootprints():
        ref = fp.GetReference()
        for p in fp.Pads():
            if p.GetDrillSize().x > 0:
                holes.append((round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3),
                              round(pcbnew.ToMM(p.GetDrillSize().x), 3)))
        if ref in SKIP:
            continue
        side = "B" if fp.IsFlipped() else "F"
        c = fp.GetCourtyard(pcbnew.B_CrtYd if side == "B" else pcbnew.F_CrtYd)
        if not c.OutlineCount():
            continue
        cb = c.BBox()
        x0, y0 = pcbnew.ToMM(cb.GetX()) + 0.25, pcbnew.ToMM(cb.GetY()) + 0.25
        x1, y1 = pcbnew.ToMM(cb.GetRight()) - 0.25, pcbnew.ToMM(cb.GetBottom()) - 0.25
        h, color = body_of(ref, str(fp.GetFPID().GetLibItemName()))
        parts.append({"ref": ref, "side": side, "box": [round(v, 3) for v in (x0, y0, x1, y1)], "h": h,
                      "color": color, "round": ref == "M1"})
    j2 = board.FindFootprintByReference("J2")
    pins = sorted((round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3))
                  for p in j2.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH)
    screws = sorted((round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3))
                    for p in j2.Pads() if p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH)
    o = footprints.OLED
    mod_x0 = pins[0][0] + (pins[-1][0] - pins[0][0]) / 2 - o["w"] / 2
    mod_y0 = pins[0][1] - o["pin_y"]
    _, bx, by, _ = netlist.PLACE["BT1"]
    variants = {v: json.load(open(os.path.join(OUT if v == "s" else os.path.join(OUT, v), "case-model.json")))
                for v in case.VARIANTS}
    with tempfile.TemporaryDirectory() as tmp:
        data = {
            "bbox": bbox, "outline": outline_points(board), "holes": holes, "parts": parts,
            "tex_front": board_texture("F", bbox, tmp), "tex_back": board_texture("B", bbox, tmp),
            "face": face_texture(tmp),
            "oled": {"x": mod_x0, "y": mod_y0, **o}, "pins": pins, "screws": screws,
            "cell": {"x": bx - footprints.CELL["w"] / 2, "y": by - footprints.CELL["h"] / 2,
                     "w": footprints.CELL["w"], "h": footprints.CELL["h"]},
            "k": {"module_t": case.MODULE_T, "glass_t": case.GLASS_T, "pin_top": case.PIN_TOP,
                  "header_plastic": case.HEADER_PLASTIC, "under_cell": case.UNDER_CELL,
                  "tube_od": case.TUBE_OD, "nut_t": case.NUT_T},
            "variants": variants,
        }
    html = open(os.path.join(HERE, "viewer.html")).read().replace("/*DATA*/null", json.dumps(data))
    path = os.path.join(OUT, "keychain-3d.html")
    with open(path, "w") as f:
        f.write(html)
    print(f"{os.path.relpath(path, REV)}: {len(html) // 1024} КБ")


if __name__ == "__main__":
    main()
