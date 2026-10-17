#!/usr/bin/env python3
"""Runs the GBA pixelate test scene (hold L+R at boot) headless and saves gba_sNN.png (the 160x160 playfield) for
every block size 1..48 into the output dir. Pair with the C# side (ANODYNE_PIXELATE_TEST=<dir> ./AnodyneSharp, which
writes pc_sNN.png) and compare.py.

  capture_gba.py <first.elf> <first.gba> <outdir>

Uses the mGBA python bindings built in src/tools/mgba (see docs/running-the-csharp-reference.md for the venv)."""
import os
import subprocess
import sys

MGBA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mgba")
sys.path.insert(0, os.path.join(MGBA, "build", "python", "lib.linux-x86_64-cpython-311"))
os.environ.setdefault("LD_LIBRARY_PATH", os.path.join(MGBA, "build"))

import mgba.core  # noqa: E402
import mgba.image  # noqa: E402

KEY_L, KEY_R = 9, 8
SETTLE_FRAMES = 8  # the software blocks lag a frame or two behind the stride


def symbol(elf, name):
    out = subprocess.check_output(["arm-none-eabi-nm", elf], text=True)
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise SystemExit(f"symbol {name} not found in {elf}")


def main():
    elf, rom, outdir = sys.argv[1:4]
    os.makedirs(outdir, exist_ok=True)
    addr = symbol(elf, "gPixelateTestStride")

    core = mgba.core.load_path(rom)
    img = mgba.image.Image(240, 160)
    core.set_video_buffer(img)
    core.reset()
    core.set_keys(KEY_L, KEY_R)  # held through boot: the ROM checks for L+R before starting the title
    for _ in range(120):  # the boot setup (maps, audio) takes a few frames before the ROM looks at the keys
        core.run_frame()
    core.set_keys()
    for _ in range(4):
        core.run_frame()

    for stride in range(1, 49):
        core.memory.u32[addr] = stride
        for _ in range(SETTLE_FRAMES):
            core.run_frame()
        img.to_pil().convert("RGB").crop((40, 0, 200, 160)).save(os.path.join(outdir, f"gba_s{stride:02d}.png"))


if __name__ == "__main__":
    main()
