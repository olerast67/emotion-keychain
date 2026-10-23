"""Шины питания в слое питания (L3) — свой маршрутизатор по сетке.

В L3 нет площадок SMD: только переходные, штыри и отверстия, поэтому задача простая.
Каждая цепь питания соединяет свои переходные (их ставит plane_fanout) деревом:
от уже построенной части — кратчайшим путём к ближайшему ещё не подключённому
переходному. Ходы по сетке 0.1 мм в восьми направлениях дают линии под 0° и 45°,
повороты штрафуются, поэтому шины выходят прямыми. Ширина и зазор — из класса цепи.
"""
import heapq
import math

import numpy as np
import pcbnew

import netlist

STEP = 0.1  # мм
EDGE = 0.3  # медь от края платы
FALLBACK = 0.3  # запасная ширина шины, мм
DIRS = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)]


def _cls(net):
    for c in netlist.NETCLASSES.values():
        if net in c["nets"]:
            return c
    return netlist.NETCLASSES["Default"]


class Grid:
    def __init__(self, board):
        self.board = board
        self.nx = int(round(netlist.BOARD_W / STEP)) + 1
        self.ny = int(round(netlist.BOARD_H / STEP)) + 1
        xs = np.arange(self.nx) * STEP
        ys = np.arange(self.ny) * STEP
        self.X, self.Y = np.meshgrid(xs, ys, indexing="xy")  # [y, x]
        # Что занято в L3: (вид, параметры, цепь). Круги — переходные и отверстия.
        self.items = []
        for t in board.GetTracks():
            if isinstance(t, pcbnew.PCB_VIA):
                p = t.GetPosition()
                self.items.append(("c", (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), pcbnew.ToMM(t.GetWidth()) / 2), t.GetNetname()))
            elif t.GetLayer() == pcbnew.In2_Cu:
                a, b = t.GetStart(), t.GetEnd()
                self.items.append(("s", (pcbnew.ToMM(a.x), pcbnew.ToMM(a.y), pcbnew.ToMM(b.x), pcbnew.ToMM(b.y),
                                         pcbnew.ToMM(t.GetWidth()) / 2), t.GetNetname()))
        for fp in board.GetFootprints():
            for pad in fp.Pads():
                if pad.GetDrillSize().x <= 0 and not pad.IsOnLayer(pcbnew.In2_Cu):
                    continue
                p = pad.GetPosition()
                sz = pad.GetSize()
                r = max(pcbnew.ToMM(sz.x), pcbnew.ToMM(sz.y), pcbnew.ToMM(pad.GetDrillSize().x)) / 2
                self.items.append(("c", (pcbnew.ToMM(p.x), pcbnew.ToMM(p.y), r), pad.GetNetname() or "~hole"))
        # Края платы и запретные зоны — одинаково для всех цепей.
        outline = pcbnew.SHAPE_POLY_SET()
        board.GetBoardPolygonOutlines(outline)
        self.outline = outline
        self.keepouts = [z for z in board.Zones() if z.GetIsRuleArea() and z.IsOnLayer(pcbnew.In2_Cu)]
        self._static = {}  # (half) -> маска краёв и запретных зон

    def blocked(self, net, half, clr, items=None):
        """Где центр дорожки цепи net полуширины half ставить нельзя."""
        bad = self._static_mask(half).copy()
        for kind, g, n in (self.items if items is None else items):
            if n == net:
                continue
            c = max(clr, _cls(n)["clearance"]) + 0.01  # зазор — наибольший из двух классов
            if kind == "c":
                cx, cy, r = g
                R = r + c + half
                self._mark_circle(bad, cx, cy, R)
            else:
                ax, ay, bx, by, hw = g
                self._mark_segment(bad, ax, ay, bx, by, hw + c + half)
        return bad

    def _static_mask(self, half):
        """Край платы и запретные зоны — одинаковы для всех цепей одной ширины."""
        if half in self._static:
            return self._static[half]
        inner = pcbnew.SHAPE_POLY_SET(self.outline)
        inner.Inflate(-pcbnew.FromMM(EDGE + half), 16)
        bad = np.zeros((self.ny, self.nx), dtype=bool)
        for iy in range(self.ny):
            for ix in range(self.nx):
                bad[iy, ix] = not inner.Contains(pcbnew.VECTOR2I(pcbnew.FromMM(ix * STEP), pcbnew.FromMM(iy * STEP)))
        for zone in self.keepouts:
            poly = pcbnew.SHAPE_POLY_SET(zone.Outline())
            poly.Inflate(pcbnew.FromMM(half), 16)
            bb = poly.BBox()
            x0, x1 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetRight())
            y0, y1 = pcbnew.ToMM(bb.GetY()), pcbnew.ToMM(bb.GetBottom())
            for iy in range(max(0, int(y0 / STEP)), min(self.ny, int(y1 / STEP) + 2)):
                for ix in range(max(0, int(x0 / STEP)), min(self.nx, int(x1 / STEP) + 2)):
                    if poly.Contains(pcbnew.VECTOR2I(pcbnew.FromMM(ix * STEP), pcbnew.FromMM(iy * STEP))):
                        bad[iy, ix] = True
        self._static[half] = bad
        return bad

    def _window(self, x0, y0, x1, y1):
        ix0, ix1 = max(0, int(x0 / STEP) - 1), min(self.nx, int(x1 / STEP) + 2)
        iy0, iy1 = max(0, int(y0 / STEP) - 1), min(self.ny, int(y1 / STEP) + 2)
        return iy0, iy1, ix0, ix1

    def _mark_circle(self, bad, cx, cy, R):
        iy0, iy1, ix0, ix1 = self._window(cx - R, cy - R, cx + R, cy + R)
        X, Y = self.X[iy0:iy1, ix0:ix1], self.Y[iy0:iy1, ix0:ix1]
        bad[iy0:iy1, ix0:ix1] |= (X - cx) ** 2 + (Y - cy) ** 2 < R * R

    def _mark_segment(self, bad, ax, ay, bx, by, R):
        iy0, iy1, ix0, ix1 = self._window(min(ax, bx) - R, min(ay, by) - R, max(ax, bx) + R, max(ay, by) + R)
        X, Y = self.X[iy0:iy1, ix0:ix1], self.Y[iy0:iy1, ix0:ix1]
        dx, dy = bx - ax, by - ay
        L = dx * dx + dy * dy
        t = np.clip(((X - ax) * dx + (Y - ay) * dy) / L, 0, 1) if L > 0 else 0
        px, py = ax + t * dx, ay + t * dy
        bad[iy0:iy1, ix0:ix1] |= (X - px) ** 2 + (Y - py) ** 2 < R * R

    def add_segment(self, net, ax, ay, bx, by, half):
        self.items.append(("s", (ax, ay, bx, by, half), net))


def _search(bad, sources, targets, nx, ny):
    """Дейкстра от всех клеток дерева до ближайшей цели. Одно состояние на клетку:
    штраф за поворот считается от направления, которым в клетку пришли."""
    INF = 1e18
    dist = np.full(nx * ny, INF)
    came = np.full(nx * ny, -1, dtype=np.int64)
    kdir = np.full(nx * ny, -1, dtype=np.int8)
    flat = bad.ravel()
    heap = []
    for x, y in sources:
        i = y * nx + x
        dist[i] = 0.0
        heap.append((0.0, i))
    heapq.heapify(heap)
    tset = {y * nx + x for x, y in targets}
    while heap:
        d, i = heapq.heappop(heap)
        if d > dist[i]:
            continue
        if i in tset:
            path = []
            while i >= 0:
                path.append((int(i % nx), int(i // nx)))
                i = came[i]
            return path[::-1]
        x, y = i % nx, i // nx
        di = kdir[i]
        for k, (ddx, ddy) in enumerate(DIRS):
            x2, y2 = x + ddx, y + ddy
            if x2 < 0 or y2 < 0 or x2 >= nx or y2 >= ny:
                continue
            j = y2 * nx + x2
            if flat[j]:
                continue
            if ddx and ddy and (flat[y * nx + x2] or flat[y2 * nx + x]):
                continue  # по диагонали — только если не задевает угол препятствия
            nd = d + (1.4142 if ddx and ddy else 1.0) + (0.0 if di in (-1, k) else 1.5)
            if nd < dist[j]:
                dist[j] = nd
                came[j] = i
                kdir[j] = k
                heapq.heappush(heap, (nd, j))
    return None


def _simplify(path):
    pts = [path[0]]
    for i in range(1, len(path) - 1):
        a, b, c = pts[-1], path[i], path[i + 1]
        d1 = (b[0] - a[0], b[1] - a[1])
        d2 = (c[0] - b[0], c[1] - b[1])
        if d1[0] * d2[1] - d1[1] * d2[0] != 0:
            pts.append(b)
    pts.append(path[-1])
    return pts


def _plan(grid, board, order):
    """Трассы цепей в порядке order. Возвращает (сегменты, не соединённые, длина)."""
    items = list(grid.items)
    segs, failed, length = [], [], 0.0
    for net in order:
        c = _cls(net)
        clr = c["clearance"]
        vias = sorted((pcbnew.ToMM(t.GetPosition().x), pcbnew.ToMM(t.GetPosition().y)) for t in board.GetTracks()
                      if isinstance(t, pcbnew.PCB_VIA) and t.GetNetname() == net)
        if len(vias) < 2:
            continue
        cells = [(int(round(x / STEP)), int(round(y / STEP))) for x, y in vias]
        tree = {cells[0]}
        left = set(range(1, len(vias)))
        # Сначала ширина класса; что не прошло — запасной шириной: длинные слаботочные
        # ветки (VBUS к модулю — только определение USB) обходятся узкой дорожкой.
        for width in (c["track"], FALLBACK):
            if not left or width > c["track"]:
                break
            half = width / 2
            bad = grid.blocked(net, half, clr, items)
            for cx, cy in cells:
                bad[cy, cx] = False  # клетка у своего переходного всегда доступна
            while left:
                targets = {cells[i]: i for i in left}
                path = _search(bad, list(tree), list(targets), grid.nx, grid.ny)
                if path is None:
                    break
                i = targets[path[-1]]
                left.discard(i)
                tree.update(path)
                pts = [(x * STEP, y * STEP) for x, y in _simplify(path)]
                if path[0] in cells:  # концы — точно в центры переходных
                    pts[0] = vias[cells.index(path[0])]
                pts[-1] = vias[i]
                for a, b in zip(pts, pts[1:]):
                    d = math.hypot(a[0] - b[0], a[1] - b[1])
                    if d < 1e-6:
                        continue
                    length += d * width
                    segs.append((a, b, net, width))
                    items.append(("s", (a[0], a[1], b[0], b[1], half), net))
        if left:
            failed.append((net, len(left)))
    return segs, failed, length


def route_power(board, nets, add_track):
    """Разводит цепи питания в L3. Пробует разные порядки цепей и берёт тот, где всё
    соединилось при наименьшей длине. Возвращает список цепей, у которых не всё соединилось."""
    grid = Grid(board)
    best = None
    # Порядок важен: кто раньше, тот проходит прямее. Пробуем сдвиги и разворот списка.
    orders = [nets[k:] + nets[:k] for k in range(len(nets))]
    orders += [o[::-1] for o in orders]
    for order in orders:
        segs, failed, length = _plan(grid, board, order)
        print("  питание, порядок", order, "не соединено", failed or "—", flush=True)
        score = (sum(n for _, n in failed), length)
        if best is None or score < best[0]:
            best = (score, segs, failed)
        if not failed:
            break  # первый полный вариант; порядок сам по себе мало меняет длину
    _, segs, failed = best
    for a, b, net, width in segs:
        add_track(a, b, net, width)
    return failed
