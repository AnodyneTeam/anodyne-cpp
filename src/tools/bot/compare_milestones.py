#!/usr/bin/env python3
"""Compares a C# bot trace (golden/*.trace.tsv) with a GBA trace (gba_drive.py output) by milestones: every map
change and the player position on the first frame in each map. Frame counts differ by design (different title/intro
timing), so frames are only reported, not compared.

  compare_milestones.py <csharp.trace.tsv> <gba.tsv> [tolerance_px=4]
Exit status 1 if the map sequences differ or an arrival position is further than the tolerance."""
import sys


def milestones(path):
    """(map, first frame, x, y) where x,y = the player's last position in that map (where the warp triggered; for the
    final map, where the run ended). Arrival positions are unreliable on the GBA for a few frames."""
    rows = []
    with open(path) as f:
        next(f)
        for line in f:
            p = line.rstrip("\n").split("\t")
            rows.append((int(p[0]), p[1], int(p[2]), int(p[3])))
    out = []
    for i, (frame, mp, x, y) in enumerate(rows):
        if not mp:
            continue
        if out and out[-1][0] == mp:
            out[-1] = (mp, out[-1][1], x, y)
            continue
        if mp == "APARTMENT" and not out:
            continue  # the GBA's map viewer boots into APARTMENT before the title; not a game map
        out.append((mp, frame, x, y))
    return out


def main():
    cs, gba = milestones(sys.argv[1]), milestones(sys.argv[2])
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 4
    ok = True
    print(f"{'map':10} {'C# frame':>9} {'C# exit pos':>12}   {'GBA frame':>9} {'GBA exit pos':>12}  diff")
    for i in range(max(len(cs), len(gba))):
        a = cs[i] if i < len(cs) else None
        b = gba[i] if i < len(gba) else None
        if a and b and a[0] == b[0]:
            d = max(abs(a[2] - b[2]), abs(a[3] - b[3]))
            flag = "" if d <= tol else "  <-- MISMATCH"
            ok &= d <= tol
            print(f"{a[0]:10} {a[1]:9} {str((a[2], a[3])):>12}   {b[1]:9} {str((b[2], b[3])):>12}  {d}{flag}")
        else:
            ok = False
            print(f"{(a or ('-',))[0]:10} {str(a[1:]) if a else '-':>22}   {str(b[1:]) if b else '-':>22}  MAP SEQUENCE DIFFERS")
    print("MATCH" if ok else "DIFFERENT")
    sys.exit(0 if ok else 1)


main()
