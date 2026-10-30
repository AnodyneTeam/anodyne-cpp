#!/usr/bin/env python3
"""Reference frames from mGBA for checking the simulator: plays a .gbainput on the GBA ROM in headless mGBA (the
python bindings of src/tools/mgba) and saves the frames asked for.

  mgba_frames.py <first.gba> <input.gbainput|-> <outdir> <frame>[,<frame>...]

Frame N is the picture after N run_frame calls, which lines up with the simulator's --shot N (give or take one
frame: compare_frames.py tries the neighbours)."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MGBA = os.path.join(HERE, "..", "..", "..", "..", "tools", "mgba", "build")
for d in os.listdir(os.path.join(MGBA, "python")):
    if d.startswith("lib."):
        sys.path.insert(0, os.path.join(MGBA, "python", d))

import mgba.core  # noqa: E402
import mgba.image  # noqa: E402
import mgba.log  # noqa: E402


def load_input(path):
    runs = []
    if path == "-":
        return runs
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        n, mask = line.split()[:2]
        runs.append((int(n), int(mask, 16)))
    return runs


def main():
    rom, inp, out, frames = sys.argv[1:5]
    want = sorted(int(f) for f in frames.split(","))
    os.makedirs(out, exist_ok=True)
    mgba.log.silence()
    core = mgba.core.load_path(rom)
    img = mgba.image.Image(240, 160)
    core.set_video_buffer(img)
    core.reset()
    keys = []
    for n, mask in load_input(inp):
        keys += [mask] * n
    frame = 0
    while want and frame < want[-1]:
        mask = keys[frame] if frame < len(keys) else 0
        core.set_keys(raw=mask)
        core.run_frame()
        frame += 1
        if frame in want:
            img.to_pil().convert("RGB").save(os.path.join(out, f"mgba_{frame:06d}.png"))


main()
