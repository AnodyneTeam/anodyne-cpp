#!/usr/bin/env python3
"""Compares pc_sNN.png (160x180, 20px header on top) against gba_sNN.png (160x160 playfield) per block size.

  compare.py <dir> [montage strides...]

Prints, per stride, the share of playfield pixels whose colour (quantised to the GBA's 5 bits per channel) differs,
and writes montage.png (PC row above GBA row) for the given strides (default a few). A pixel-exact port prints 0%."""
import os
import sys

from PIL import Image


def q(px):
    return (px[0] >> 3, px[1] >> 3, px[2] >> 3)


def main():
    d = sys.argv[1]
    strides = [int(a) for a in sys.argv[2:]] or [2, 5, 8, 12, 16, 17, 20, 24, 30, 40]
    print("stride  mismatch%")
    for s in range(1, 49):
        pc = Image.open(os.path.join(d, f"pc_s{s:02d}.png")).convert("RGB").crop((0, 20, 160, 180))
        gba = Image.open(os.path.join(d, f"gba_s{s:02d}.png")).convert("RGB")
        a, b = pc.load(), gba.load()
        bad = sum(1 for y in range(160) for x in range(160) if q(a[x, y]) != q(b[x, y]))
        print(f"{s:6d}  {100.0 * bad / (160 * 160):6.1f}")
    w = Image.new("RGB", (160 * len(strides), 320))
    for i, s in enumerate(strides):
        w.paste(Image.open(os.path.join(d, f"pc_s{s:02d}.png")).convert("RGB").crop((0, 20, 160, 180)), (i * 160, 0))
        w.paste(Image.open(os.path.join(d, f"gba_s{s:02d}.png")).convert("RGB"), (i * 160, 160))
    w.save(os.path.join(d, "montage.png"))


if __name__ == "__main__":
    main()
