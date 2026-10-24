#!/usr/bin/env python3
"""Checks the simulator's picture against mGBA running the GBA ROM, frame by frame, with the same input.

  compare_frames.py <anodyne_sim> <first.gba> <input.gbainput|-> <outdir> <frame>[,<frame>...]

For each frame N it compares the simulator's frame N with mGBA's frames N-2..N+2 (the two may be a frame or so
apart at boot) and reports the closest: the number of differing pixels and a side-by-side PNG (sim | mGBA | diff)
in outdir. Exit status 1 if any frame differs. A pixel only counts as different if a channel is off by more than
8: mGBA blends in 8-bit colour, the hardware (and the simulator) in 5-bit, so blended pixels can be a step apart."""
import os
import subprocess
import sys

from PIL import Image, ImageChops

WINDOW = int(os.environ.get("CMP_WINDOW", "2"))
HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    sim, rom, inp, out, frames = sys.argv[1:6]
    want = sorted(int(f) for f in frames.split(","))
    os.makedirs(out, exist_ok=True)
    args = [sim, "--headless", "--no-audio", "--frames", str(want[-1])]
    if inp != "-":
        args += ["--input", inp]
    for n in want:
        args += ["--shot", f"{n}:{out}/sim_{n:06d}.png"]
    subprocess.run(args, check=True)
    near = sorted({m for n in want for m in range(max(1, n - WINDOW), n + WINDOW + 1)})
    env = dict(os.environ)
    subprocess.run([sys.executable, os.path.join(HERE, "mgba_frames.py"), rom, inp, out, ",".join(map(str, near))],
                   check=True, env=env)
    bad = 0
    for n in want:
        a = Image.open(f"{out}/sim_{n:06d}.png").convert("RGB")
        best = None
        for m in range(max(1, n - WINDOW), n + WINDOW + 1):
            b = Image.open(f"{out}/mgba_{m:06d}.png").convert("RGB")
            diff = ImageChops.difference(a, b).point(lambda v: 255 if v > 8 else 0)
            count = sum(1 for p in diff.get_flattened_data() if p != (0, 0, 0))
            if best is None or count < best[0]:
                best = (count, m, b, diff)
        count, m, b, diff = best
        side = Image.new("RGB", (240 * 3, 160))
        side.paste(a, (0, 0))
        side.paste(b, (240, 0))
        side.paste(diff, (480, 0))
        side.save(f"{out}/cmp_{n:06d}.png")
        print(f"frame {n}: closest mGBA frame {m}, {count} pixels differ" + ("" if count == 0 else "  <--"))
        bad += count != 0
    sys.exit(1 if bad else 0)


main()
