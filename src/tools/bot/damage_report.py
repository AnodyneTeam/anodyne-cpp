#!/usr/bin/env python3
"""Lists every screen where the bot lost health, from a bot trace (frame, map, x, y, hp, ...).

usage: damage_report.py <trace.tsv> [more traces...]

Every hit is a problem (the goal is a run that dodges or kills everything cleanly), so this is the to-do list for the
dodging/fighting code: screens with the most lost hearts first, with the first frame and position to warp to for a test.
"""
import sys
from collections import defaultdict


def report(path):
    hits = defaultdict(lambda: {"lost": 0, "deaths": 0, "first": None, "pos": None})
    prev_hp = None
    with open(path) as f:
        next(f)
        for line in f:
            c = line.rstrip("\n").split("\t")
            if len(c) < 5 or not c[1] or not c[2] or not c[4]:
                prev_hp = None
                continue
            frame, name, x, y, hp = int(c[0]), c[1], int(c[2]), int(c[3]), int(c[4])
            if prev_hp is not None and hp < prev_hp:
                key = (name, x // 160, y // 160)
                h = hits[key]
                h["lost"] += prev_hp - hp
                if hp <= 0:
                    h["deaths"] += 1
                if h["first"] is None:
                    h["first"], h["pos"] = frame, (x, y)
            prev_hp = hp
    total = sum(h["lost"] for h in hits.values())
    print(f"{path}: {total} health lost on {len(hits)} screens")
    for (name, sx, sy), h in sorted(hits.items(), key=lambda kv: -kv[1]["lost"]):
        print(f"  {name:10s} screen ({sx},{sy}): lost {h['lost']:3d}  deaths {h['deaths']}  first hit frame {h['first']} at {h['pos']}")


for p in sys.argv[1:]:
    report(p)
