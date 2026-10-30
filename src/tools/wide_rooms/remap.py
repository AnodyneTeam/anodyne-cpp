#!/usr/bin/env python3
"""Wide rooms: re-cuts every map's 10x10-tile rooms into 15x9-tile rooms (240x144, under a 16px HUD row).

Each room stays the same room (same grid position, same doors, same entities), only bigger across and one row
shorter: five of its columns are drawn twice and one of its rows is left out. This is an automatic first pass, meant
to be fixed up by hand: the output is plain CSV/XML in the same formats as the input, so edit it like any map.

    remap.py [content dir] [out dir]      (default: old/Content -> old/ContentWide)

Choosing the columns and the row: per grid column (every room in it, all layers) the five inner columns (1-8) whose
tiles most often repeat their neighbours' get doubled, and per grid row the inner row that most often repeats the rows
next to it is dropped. Sharing the choice along a grid column/row keeps the openings between neighbouring rooms lined
up. Out: the maps (BG/BG2/FG.csv), Entities.xml, Settings.json areas and Swapper.dat regions moved to match, the other
files copied, and remap.json/remap.txt (per map: each grid column's 15 source columns, each grid row's 9 source rows) for code
that has positions of its own.
"""
import json
import os
import re
import shutil
import sys

ROOM = 10    # tiles a side in the original rooms
NEW_W = 15   # tiles across in the wide rooms
NEW_H = 9    # tiles down
TILE = 16
LAYERS = ("BG", "BG2", "FG")


def read_csv(path):
    with open(path) as f:
        return [[int(v) for v in line.strip().split(",")] for line in f if line.strip()]


def write_csv(path, grid):
    with open(path, "w") as f:
        for row in grid:
            f.write(",".join(str(v) for v in row) + "\n")


def choose_columns(layers, gx):
    """The 15 source columns of grid column gx: 0-9 with the five most repetitive inner ones doubled."""
    scores = []
    for c in range(1, ROOM - 1):
        same = total = 0
        for grid in layers:
            x = gx * ROOM + c
            if x + 1 >= len(grid[0]):
                continue  # (a layer can be smaller than BG)
            for row in grid:
                total += 1
                same += row[x] == row[x - 1] or row[x] == row[x + 1]
        scores.append((same / max(total, 1), -abs(c - 4.5), c))
    doubled = {c for _, _, c in sorted(scores, reverse=True)[: NEW_W - ROOM]}
    cols = []
    for c in range(ROOM):
        cols.append(c)
        if c in doubled:
            cols.append(c)
    return cols


def choose_rows(layers, gy):
    """The 9 source rows of grid row gy: 0-9 less the most repetitive inner row."""
    best = None
    for r in range(1, ROOM - 1):
        same = total = 0
        for grid in layers:
            y = gy * ROOM + r
            if y + 1 >= len(grid):
                continue
            for x in range(len(grid[0])):
                total += 1
                same += grid[y][x] == grid[y - 1][x] or grid[y][x] == grid[y + 1][x]
        key = (same / max(total, 1), -abs(r - 4.5))
        if best is None or key > best[0]:
            best = (key, r)
    return [r for r in range(ROOM) if r != best[1]]


class Remap:
    def __init__(self, cols, rows):
        self.cols = cols  # per grid column: 15 source columns
        self.rows = rows  # per grid row: 9 source rows

    @staticmethod
    def _axis(v, maps, old_room, new_tiles):
        g, local = divmod(v, old_room * TILE)
        if g >= len(maps):
            return v - len(maps) * old_room * TILE + len(maps) * new_tiles * TILE
        src = maps[g]
        tile, sub = divmod(local, TILE)
        if tile in src:
            i = src.index(tile)
        else:
            # (a dropped row: the row above it, or below for the top one)
            i = src.index(tile - 1) if tile - 1 in src else src.index(tile + 1)
        return g * new_tiles * TILE + i * TILE + sub

    def x(self, v):
        return self._axis(v, self.cols, ROOM, NEW_W)

    def y(self, v):
        return self._axis(v, self.rows, ROOM, NEW_H)

    def rect(self, x, y, w, h):
        """A region: its edges on room boundaries stay on them, others move with the tiles."""
        x0, y0 = self.x(x), self.y(y)
        x1, y1 = self.x(x + w - 1) + 1, self.y(y + h - 1) + 1
        # (whole rooms, or a pixel past them like Swapper.dat's 161: keep that slack)
        if (x + w) % (ROOM * TILE) in (0, 1):
            x1 = ((x + w) // (ROOM * TILE)) * NEW_W * TILE + (x + w) % (ROOM * TILE)
        if (y + h) % (ROOM * TILE) in (0, 1):
            y1 = ((y + h) // (ROOM * TILE)) * NEW_H * TILE + (y + h) % (ROOM * TILE)
        return x0, y0, x1 - x0, y1 - y0


def remap_map(src_dir, out_dir):
    layers = {}
    for name in LAYERS:
        path = os.path.join(src_dir, name + ".csv")
        if os.path.exists(path):
            layers[name] = read_csv(path)
    grids = list(layers.values())
    rooms_w = max(len(g[0]) for g in grids) // ROOM
    rooms_h = max(len(g) for g in grids) // ROOM
    cols = [choose_columns(grids, gx) for gx in range(rooms_w)]
    rows = [choose_rows(grids, gy) for gy in range(rooms_h)]
    os.makedirs(out_dir, exist_ok=True)
    for name, grid in layers.items():
        out = []
        for gy in range(len(grid) // ROOM):
            for r in rows[gy]:
                src_row = grid[gy * ROOM + r]
                out.append([src_row[gx * ROOM + c] for gx in range(len(grid[0]) // ROOM) for c in cols[gx]])
        write_csv(os.path.join(out_dir, name + ".csv"), out)
    remap = Remap(cols, rows)
    for f in os.listdir(src_dir):
        src = os.path.join(src_dir, f)
        dst = os.path.join(out_dir, f)
        if f in (n + ".csv" for n in LAYERS):
            continue
        if f == "Settings.json":
            with open(src) as fh:
                settings = json.load(fh)
            for area in settings.get("Areas", []):
                area["X"], area["Y"], area["Width"], area["Height"] = remap.rect(
                    area["X"], area["Y"], area["Width"], area["Height"])
            with open(dst, "w") as fh:
                json.dump(settings, fh, separators=(",", ":"))
        elif f == "Swapper.dat":
            with open(src) as fh, open(dst, "w") as out:
                for line in fh:
                    w = line.split()
                    if len(w) == 5:
                        x, y, ww, hh = remap.rect(*(int(float(v)) for v in w[1:]))
                        line = f"{w[0]}\t{x}\t{y}\t{ww}\t{hh}\n"
                    out.write(line)
        elif os.path.isdir(src):
            shutil.copytree(src, dst, dirs_exist_ok=True)
        else:
            shutil.copy2(src, dst)
    return remap


def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
    content = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "old", "Content")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "old", "ContentWide")
    remaps = {}
    maps_dir = os.path.join(content, "Maps")
    for name in sorted(os.listdir(maps_dir)):
        if os.path.exists(os.path.join(maps_dir, name, "BG.csv")):
            remaps[name] = remap_map(os.path.join(maps_dir, name), os.path.join(out, "Maps", name))
    # Entities.xml: every entity's x/y, by its map's remap (kept as text so the rest of the file is untouched)
    with open(os.path.join(content, "Entities.xml")) as fh:
        lines = fh.readlines()
    current = None
    with open(os.path.join(out, "Entities.xml"), "w") as fh:
        for line in lines:
            m = re.search(r'<map name="([^"]+)"', line)
            if m:
                current = remaps.get(m.group(1))
            elif current is not None and re.search(r'\sx="-?\d+"', line) and re.search(r'\sy="-?\d+"', line):
                line = re.sub(r'(\sx=")(-?\d+)(")', lambda g: f"{g.group(1)}{current.x(int(g.group(2)))}{g.group(3)}", line, count=1)
                line = re.sub(r'(\sy=")(-?\d+)(")', lambda g: f"{g.group(1)}{current.y(int(g.group(2)))}{g.group(3)}", line, count=1)
            fh.write(line)
    with open(os.path.join(out, "remap.json"), "w") as fh:
        json.dump({n: {"cols": r.cols, "rows": r.rows} for n, r in remaps.items()}, fh, indent=1)
    # (the same, flat for the C++ content compiler: "<map> <rooms across> <rooms down> <cols...> <rows...>")
    with open(os.path.join(out, "remap.txt"), "w") as fh:
        for n, r in remaps.items():
            flat = [v for c in r.cols for v in c] + [v for row in r.rows for v in row]
            fh.write(f"{n} {len(r.cols)} {len(r.rows)} {' '.join(map(str, flat))}\n")
    print(f"{len(remaps)} maps -> {out}")


if __name__ == "__main__":
    main()
