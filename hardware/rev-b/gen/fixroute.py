"""Доводка: соединения, которые не развёл Freerouting, прокладываются здесь.

Берёт из отчёта DRC пары несоединённых элементов сигнальных цепей и соединяет каждую
кратчайшим путём по сетке 0.1 мм в слоях L1, L3 и L4 с переходными между ними. Чужая
медь, края платы и запретные зоны обходятся с зазорами классов цепей; заливки земли
после доводки заливаются заново и сами отступают от новых дорожек.
"""
import heapq
import math
import re

import numpy as np
import pcbnew

import netlist

STEP = 0.1  # мм
EDGE = 0.3  # медь от края платы
LAYERS = (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)
NAMES = {"F.Cu": pcbnew.F_Cu, "In2.Cu": pcbnew.In2_Cu, "B.Cu": pcbnew.B_Cu}
DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]
VIA_COST = 12.0
NARROW = 0.15  # мм: запасная ширина, если класс не проходит
HOLE_CLEAR = 0.25  # от края отверстия до чужой меди (DRC требует 0.2)
TURN_COST = 0.5


def _cls(net):
    for c in netlist.NETCLASSES.values():
        if net in c["nets"]:
            return c
    return netlist.NETCLASSES["Default"]


def _mm(v):
    return pcbnew.ToMM(v)


class Board:
    """Медь платы в виде простых фигур: (слои, вид, геометрия, цепь)."""

    def __init__(self, board):
        self.board = board
        x0 = -1.0
        y0 = netlist.TAB_CY - netlist.TAB_R - 1.0
        self.x0, self.y0 = x0, y0
        self.nx = int((netlist.BOARD_W + 2 - x0) / STEP) + 1
        self.ny = int((netlist.BOARD_H + 1 - y0) / STEP) + 1
        xs = x0 + np.arange(self.nx) * STEP
        ys = y0 + np.arange(self.ny) * STEP
        self.X, self.Y = np.meshgrid(xs, ys, indexing="xy")
        self.items = []
        for t in board.GetTracks():
            net = t.GetNetname()
            if isinstance(t, pcbnew.PCB_VIA):
                p = t.GetPosition()
                self.items.append((LAYERS, "c", (_mm(p.x), _mm(p.y), _mm(t.GetWidth()) / 2), net))
            elif t.GetLayer() in LAYERS:
                a, b = t.GetStart(), t.GetEnd()
                self.items.append(((t.GetLayer(),), "s", (_mm(a.x), _mm(a.y), _mm(b.x), _mm(b.y),
                                                          _mm(t.GetWidth()) / 2), net))
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                net = pad.GetNetname() or "~"
                if pad.GetDrillSize().x > 0:
                    p = pad.GetPosition()
                    r = max(_mm(pad.GetSize().x), _mm(pad.GetSize().y), _mm(pad.GetDrillSize().x)) / 2
                    self.items.append((LAYERS, "h", (_mm(p.x), _mm(p.y), r), net))
                    continue
                layers = tuple(l for l in LAYERS if pad.IsOnLayer(l))
                if not layers:
                    continue
                bb = pad.GetEffectivePolygon().BBox()
                self.items.append((layers, "r", (_mm(bb.GetX()), _mm(bb.GetY()), _mm(bb.GetRight()),
                                                 _mm(bb.GetBottom())), net))
        outline = pcbnew.SHAPE_POLY_SET()
        board.GetBoardPolygonOutlines(outline)
        self.outline = outline
        self.rules = [z for z in board.Zones() if z.GetIsRuleArea()]
        for fp in board.GetFootprints():
            self.rules += [z for z in fp.Zones() if z.GetIsRuleArea()]
        self._edge = {}

    def cell(self, x, y):
        return int(round((x - self.x0) / STEP)), int(round((y - self.y0) / STEP))

    def xy(self, ix, iy):
        return self.x0 + ix * STEP, self.y0 + iy * STEP

    def edge_mask(self, inset):
        """Клетки, где центр дорожки или переходного ближе inset к краю платы."""
        key = round(inset, 3)
        if key not in self._edge:
            inner = pcbnew.SHAPE_POLY_SET(self.outline)
            inner.Inflate(-pcbnew.FromMM(inset), 16)
            bad = np.ones((self.ny, self.nx), dtype=bool)
            bb = inner.BBox()
            ix0, iy0 = self.cell(_mm(bb.GetX()), _mm(bb.GetY()))
            ix1, iy1 = self.cell(_mm(bb.GetRight()), _mm(bb.GetBottom()))
            for iy in range(max(0, iy0 - 1), min(self.ny, iy1 + 2)):
                for ix in range(max(0, ix0 - 1), min(self.nx, ix1 + 2)):
                    x, y = self.xy(ix, iy)
                    bad[iy, ix] = not inner.Contains(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
            self._edge[key] = bad
        return self._edge[key]

    def _win(self, x0, y0, x1, y1):
        ix0, iy0 = self.cell(x0, y0)
        ix1, iy1 = self.cell(x1, y1)
        return max(0, iy0 - 1), min(self.ny, iy1 + 2), max(0, ix0 - 1), min(self.nx, ix1 + 2)

    def mark(self, bad, kind, g, R):
        if kind in ("c", "h"):
            cx, cy, r = g
            R += r
            a, b, c, d = self._win(cx - R, cy - R, cx + R, cy + R)
            X, Y = self.X[a:b, c:d], self.Y[a:b, c:d]
            bad[a:b, c:d] |= (X - cx) ** 2 + (Y - cy) ** 2 < R * R
        elif kind == "s":
            ax, ay, bx, by, hw = g
            R += hw
            a, b, c, d = self._win(min(ax, bx) - R, min(ay, by) - R, max(ax, bx) + R, max(ay, by) + R)
            X, Y = self.X[a:b, c:d], self.Y[a:b, c:d]
            dx, dy = bx - ax, by - ay
            L = dx * dx + dy * dy
            t = np.clip(((X - ax) * dx + (Y - ay) * dy) / L, 0, 1) if L > 0 else 0
            bad[a:b, c:d] |= (X - ax - t * dx) ** 2 + (Y - ay - t * dy) ** 2 < R * R
        else:
            x0, y0, x1, y1 = g
            a, b, c, d = self._win(x0 - R, y0 - R, x1 + R, y1 + R)
            X, Y = self.X[a:b, c:d], self.Y[a:b, c:d]
            ex = np.maximum(np.maximum(x0 - X, X - x1), 0)
            ey = np.maximum(np.maximum(y0 - Y, Y - y1), 0)
            bad[a:b, c:d] |= ex * ex + ey * ey < R * R

    def rule_mask(self, layer, via, r):
        """Запретные зоны слоя, раздутые на r: центр дорожки или переходного не ближе."""
        bad = np.zeros((self.ny, self.nx), dtype=bool)
        for z in self.rules:
            if not z.IsOnLayer(layer):
                continue
            if not (z.GetDoNotAllowVias() if via else z.GetDoNotAllowTracks()):
                continue
            bb = z.Outline().BBox()
            a, b, c, d = self._win(_mm(bb.GetX()) - r, _mm(bb.GetY()) - r, _mm(bb.GetRight()) + r,
                                   _mm(bb.GetBottom()) + r)
            dist = pcbnew.FromMM(r)
            for iy in range(a, b):
                for ix in range(c, d):
                    x, y = self.xy(ix, iy)
                    if z.Outline().Collide(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)), dist):
                        bad[iy, ix] = True
        return bad

    def masks(self, net, half, via_r):
        """Для каждого слоя: где нельзя центр дорожки; и где нельзя переходное."""
        clr_own = _cls(net)["clearance"]
        track = []
        via = np.zeros((self.ny, self.nx), dtype=bool)
        for layer in LAYERS:
            bad = self.edge_mask(EDGE + half) | self.rule_mask(layer, False, half + 0.02)
            vbad = self.rule_mask(layer, True, via_r + 0.02)
            for layers, kind, g, n in self.items:
                if n == net or layer not in layers:
                    continue
                clr = max(clr_own, _cls(n)["clearance"], HOLE_CLEAR if kind == "h" else 0) + 0.01
                self.mark(bad, kind, g, clr + half)
                self.mark(vbad, kind, g, clr + via_r)
            track.append(bad)
            via |= vbad
        via |= self.edge_mask(EDGE + via_r)
        return track, via

    def add(self, layers, kind, g, net):
        self.items.append((layers, kind, g, net))


def _search(track, via, sources, targets, nx):
    """Дейкстра по (слой, клетка). Ход в соседнюю клетку, поворот или переходное."""
    INF = 1e18
    nl = len(track)
    ny = track[0].shape[0]
    size = nx * ny
    dist = np.full(nl * size, INF)
    came = np.full(nl * size, -1, dtype=np.int64)
    kdir = np.full(nl * size, -1, dtype=np.int8)
    flat = [t.ravel() for t in track]
    vflat = via.ravel()
    heap = []
    for l, x, y in sources:
        s = l * size + y * nx + x
        dist[s] = 0.0
        heap.append((0.0, s))
    heapq.heapify(heap)
    tset = {l * size + y * nx + x for l, x, y in targets}
    while heap:
        d, s = heapq.heappop(heap)
        if d > dist[s]:
            continue
        if s in tset:
            path = []
            while s >= 0:
                l, i = divmod(int(s), size)
                path.append((l, i % nx, i // nx))
                s = came[s]
            return path[::-1]
        l, i = divmod(int(s), size)
        x, y = i % nx, i // nx
        f = flat[l]
        di = kdir[s]
        for k, (dx, dy) in enumerate(DIRS):
            x2, y2 = x + dx, y + dy
            if x2 < 0 or y2 < 0 or x2 >= nx or y2 >= ny:
                continue
            j = y2 * nx + x2
            if f[j] or (dx and dy and (f[y * nx + x2] or f[y2 * nx + x])):
                continue
            nd = d + (1.4142 if dx and dy else 1.0) + (0.0 if di in (-1, k) else TURN_COST)
            t = l * size + j
            if nd < dist[t]:
                dist[t] = nd
                came[t] = s
                kdir[t] = k
                heapq.heappush(heap, (nd, t))
        if not vflat[i]:
            for l2 in range(nl):
                if l2 == l or flat[l2][i]:
                    continue
                t = l2 * size + i
                nd = d + VIA_COST
                if nd < dist[t]:
                    dist[t] = nd
                    came[t] = s
                    kdir[t] = -1
                    heapq.heappush(heap, (nd, t))
    return None


def _item_cells(geo, kind, g, layers):
    """Клетки, откуда можно начать путь от элемента: центр площадки, концы и середина дорожки."""
    if kind in ("c", "h"):
        pts = [(g[0], g[1])]
    elif kind == "s":
        pts = [(g[0], g[1]), (g[2], g[3]), ((g[0] + g[2]) / 2, (g[1] + g[3]) / 2)]
    else:
        pts = [((g[0] + g[2]) / 2, (g[1] + g[3]) / 2)]
    out = []
    for x, y in pts:
        ix, iy = geo.cell(x, y)
        for l in layers:
            out.append((LAYERS.index(l), ix, iy))
    return out


def _find(geo, net, x, y, layer_name):
    """Элемент цепи net в точке отчёта DRC."""
    best = None
    for layers, kind, g, n in geo.items:
        if n != net:
            continue
        if layer_name in NAMES and NAMES[layer_name] not in layers:
            continue
        if kind in ("c", "h"):
            d = math.hypot(g[0] - x, g[1] - y)
        elif kind == "s":
            d = min(math.hypot(g[0] - x, g[1] - y), math.hypot(g[2] - x, g[3] - y))
        else:
            d = math.hypot((g[0] + g[2]) / 2 - x, (g[1] + g[3]) / 2 - y)
        if best is None or d < best[0]:
            best = (d, layers, kind, g)
    return best if best and best[0] < 0.05 else None


ITEM = re.compile(r"@\(([-\d.]+) mm, ([-\d.]+) mm\): (.*?)\[([^\]]+)\](.*)")


def unconnected(report_text):
    """Пары из отчёта DRC: [(цепь, (x, y, слой), (x, y, слой))]. Заливки пропускаются."""
    out = []
    lines = report_text.splitlines()
    for i, line in enumerate(lines):
        if not line.startswith("[unconnected_items]"):
            continue
        pair = []
        for s in lines[i + 2:i + 4]:
            m = ITEM.search(s)
            if not m or m.group(3).startswith("Zone"):
                break
            lm = re.search(r"on (\w+\.Cu)", m.group(5))
            pair.append((float(m.group(1)), float(m.group(2)), lm.group(1) if lm and "-" not in m.group(5) else "*",
                         m.group(4)))
        if len(pair) == 2 and pair[0][3] == pair[1][3]:
            out.append((pair[0][3], pair[0][:3], pair[1][:3]))
    return out


def _simplify(path):
    pts = [path[0]]
    for i in range(1, len(path) - 1):
        a, b, c = pts[-1], path[i], path[i + 1]
        if a[0] != b[0] or b[0] != c[0]:
            pts.append(b)
            continue
        d1 = (b[1] - a[1], b[2] - a[2])
        d2 = (c[1] - b[1], c[2] - b[2])
        if d1[0] * d2[1] - d1[1] * d2[0] != 0:
            pts.append(b)
    pts.append(path[-1])
    return pts


def route_leftovers(board, report_text, add_track, add_via):
    """Прокладывает недостающие соединения. Возвращает список того, что не удалось."""
    pairs = unconnected(report_text)
    if not pairs:
        return []
    geo = Board(board)
    failed = []
    for net, a, b in pairs:
        c = _cls(net)
        width = 0.3 if net in netlist.POWER_NETS or net in netlist.PLANES else c["track"]
        half = width / 2
        dia, drill = c["via"]
        ia = _find(geo, net, *a)
        ib = _find(geo, net, *b)
        if not ia or not ib:
            failed.append((net, "не найден элемент"))
            continue
        src = _item_cells(geo, ia[2], ia[3], ia[1])
        dst = _item_cells(geo, ib[2], ib[3], ib[1])
        # Сначала ширина класса, потом узкая: слаботочным веткам (питание микрофона) хватает.
        for width in sorted({width, NARROW}, reverse=True):
            half = width / 2
            track, via = geo.masks(net, half, dia / 2)
            for l, x, y in src + dst:
                track[l][y, x] = False
            path = _search(track, via, src, dst, geo.nx)
            if path is not None:
                break
        if path is None:
            failed.append((net, f"нет пути {a[:2]} → {b[:2]}"))
            continue
        pts = _simplify(path)
        for p, q in zip(pts, pts[1:]):
            (xa, ya), (xb, yb) = geo.xy(p[1], p[2]), geo.xy(q[1], q[2])
            if p[0] != q[0]:
                add_via((xa, ya), net, dia, drill)
                geo.add(LAYERS, "c", (xa, ya, dia / 2), net)
            elif (xa, ya) != (xb, yb):
                add_track((xa, ya), (xb, yb), LAYERS[p[0]], net, width)
                geo.add((LAYERS[p[0]],), "s", (xa, ya, xb, yb, half), net)
        # концы пути — точно в центр площадки или переходного
        for item, end in ((ia, pts[0]), (ib, pts[-1])):
            kind, g = item[2], item[3]
            if kind == "s":
                continue
            cx, cy = (g[0], g[1]) if kind in ("c", "h") else ((g[0] + g[2]) / 2, (g[1] + g[3]) / 2)
            ex, ey = geo.xy(end[1], end[2])
            if math.hypot(cx - ex, cy - ey) > 1e-6:
                add_track((cx, cy), (ex, ey), LAYERS[end[0]], net, width)
                geo.add((LAYERS[end[0]],), "s", (cx, cy, ex, ey, half), net)
        print(f"  доводка {net}: {len(pts) - 1} отрезков")
    return failed


def connect_pads(board, pads, add_track, add_via):
    """Площадки без своего переходного (места не нашлось) — путём к любой меди своей цепи."""
    geo = Board(board)
    failed = []
    for ref, num in pads:
        pad = next(p for p in board.FindFootprintByReference(ref).Pads() if p.GetNumber() == num)
        net = pad.GetNetname()
        c = _cls(net)
        width = 0.3 if net in netlist.PLANES or net in netlist.POWER_NETS else c["track"]
        half = width / 2
        dia, drill = c["via"]
        p = pad.GetPosition()
        px, py = _mm(p.x), _mm(p.y)
        layers = tuple(l for l in LAYERS if pad.IsOnLayer(l))
        src = [(LAYERS.index(l), *geo.cell(px, py)) for l in layers]
        dst = []
        for ls, kind, g, n in geo.items:
            if n != net or kind != "c" or len(ls) < len(LAYERS):
                continue  # цели — переходные своей цепи
            if math.hypot(g[0] - px, g[1] - py) < 0.05:
                continue
            dst += _item_cells(geo, kind, g, ls)
        track, via = geo.masks(net, half, dia / 2)
        for l, x, y in src + dst:
            track[l][y, x] = False
        path = _search(track, via, src, dst, geo.nx)
        if path is None:
            failed.append(f"{ref}.{num}")
            continue
        pts = _simplify(path)
        ex, ey = geo.xy(pts[0][1], pts[0][2])
        if math.hypot(px - ex, py - ey) > 1e-6:
            add_track((px, py), (ex, ey), LAYERS[pts[0][0]], net, width)
        for a, b in zip(pts, pts[1:]):
            (xa, ya), (xb, yb) = geo.xy(a[1], a[2]), geo.xy(b[1], b[2])
            if a[0] != b[0]:
                add_via((xa, ya), net, dia, drill)
                geo.add(LAYERS, "c", (xa, ya, dia / 2), net)
            elif (xa, ya) != (xb, yb):
                add_track((xa, ya), (xb, yb), LAYERS[a[0]], net, width)
                geo.add((LAYERS[a[0]],), "s", (xa, ya, xb, yb, half), net)
        # конец — точно в центр целевого переходного
        lx, ly = geo.xy(pts[-1][1], pts[-1][2])
        tgt = min((g for ls, kind, g, n in geo.items if n == net and kind == "c" and len(ls) == len(LAYERS)),
                  key=lambda g: math.hypot(g[0] - lx, g[1] - ly))
        if math.hypot(tgt[0] - lx, tgt[1] - ly) > 1e-6:
            add_track((lx, ly), (tgt[0], tgt[1]), LAYERS[pts[-1][0]], net, width)
        print(f"  доводка {ref}.{num} ({net}): {len(pts) - 1} отрезков")
    return failed


def remove_dangling(board):
    """Убирает обрезки дорожек с висящим концом (Freerouting иногда оставляет дубли)."""
    removed = 0
    for _ in range(10):
        board.BuildConnectivity()
        conn = board.GetConnectivity()
        dead = [t for t in board.GetTracks()
                if not isinstance(t, pcbnew.PCB_VIA) and conn.TestTrackEndpointDangling(t, False)]
        dead += [v for v in board.GetTracks() if isinstance(v, pcbnew.PCB_VIA) and _via_layers(board, v) < 2]
        if not dead:
            break
        for t in dead:
            board.Remove(t)
        removed += len(dead)
    return removed


def _via_layers(board, v):
    """Сколько слоёв подходит к переходному сигнальной цепи (дорожки и площадки)."""
    net = v.GetNetname()
    if net in netlist.PLANES:
        return 2  # переходные земли держат заливки
    p, r = v.GetPosition(), v.GetWidth() / 2
    layers = set()
    for t in board.GetTracks():
        if t is v or isinstance(t, pcbnew.PCB_VIA) or t.GetNetname() != net:
            continue
        for e in (t.GetStart(), t.GetEnd()):
            if math.hypot(e.x - p.x, e.y - p.y) <= r:
                layers.add(t.GetLayer())
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            if pad.GetNetname() == net and pad.HitTest(p):
                layers.update(l for l in LAYERS if pad.IsOnLayer(l))
    return len(layers)
