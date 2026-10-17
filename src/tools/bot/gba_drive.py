#!/usr/bin/env python3
"""Headless mGBA driver for the GBA port: boots the ROM, plays the same semantic route as the C# bot
(WalkTo by reading the player position from RAM) and writes a trace in the C# trace format.

  gba_drive.py <first.elf> <first.gba> <route> <out.tsv> [--shots dir]

Routes live in ROUTES below. Position/map are read from the ROM's RAM via symbols (arm-none-eabi-nm)."""
import os
import subprocess
import sys

MGBA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mgba")
sys.path.insert(0, os.path.join(MGBA, "build", "python", "lib.linux-x86_64-cpython-311"))
os.environ.setdefault("LD_LIBRARY_PATH", os.path.join(MGBA, "build"))

import mgba.core  # noqa: E402
import mgba.image  # noqa: E402

A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L = range(10)


class Gba:
    def __init__(self, elf, rom, shots=None):
        syms = {}
        for line in subprocess.check_output(["arm-none-eabi-nm", elf], text=True).splitlines():
            p = line.split()
            if len(p) == 3:
                syms[p[2]] = int(p[0], 16)
        self.sym = syms
        self.core = mgba.core.load_path(rom)
        self.img = mgba.image.Image(240, 160)
        self.core.set_video_buffer(self.img)
        self.core.reset()
        self.frame = 0
        self.keys = set()
        self.log = []
        self.shots = shots
        self.names = None

    def s(self, mangled):
        return self.sym[mangled]

    def step(self, keys=()):
        self.core.set_keys(*keys)
        self.core.run_frame()
        self.frame += 1
        self.log.append((self.frame, self.map_name(), self.x, self.y))

    @property
    def x(self):
        return self.core.memory.u32[self.s("_ZN12_GLOBAL__N_15gPosXE")] / 65536.0

    @property
    def y(self):
        return self.core.memory.u32[self.s("_ZN12_GLOBAL__N_15gPosYE")] / 65536.0

    def map_name(self):
        idx = self.core.memory.u8[self.s("_ZL11gCurrentMap")] if False else self.core.memory.u32[self.s("_ZL11gCurrentMap")]
        if self.names is None:
            base = self.s("_ZL9kMapNames")
            self.names = []
            for i in range(64):
                ptr = self.core.memory.u32[base + 4 * i]
                if not (0x08000000 <= ptr < 0x0A000000):
                    break
                s, a = b"", ptr
                while True:
                    c = self.core.memory.u8[a]
                    if c == 0:
                        break
                    s += bytes([c])
                    a += 1
                self.names.append(s.decode())
        return self.names[idx] if idx < len(self.names) else f"?{idx}"

    def shot(self, name):
        if self.shots:
            os.makedirs(self.shots, exist_ok=True)
            self.img.to_pil().convert("RGB").save(os.path.join(self.shots, name + ".png"))

    @property
    def talking(self):
        return self.core.memory.u32[self.s("gDialogueOpen")] != 0

    def read_dialogue(self, timeout=4000):
        """Mash A through every open text box (like the C# bot's ReadDialogue). Screenshots the box while it types
        (dlg<N>_a: ~2s in, dlg<N>_b: ~1s later)."""
        idle = 0
        talked = 0
        self.dialogues = getattr(self, "dialogues", 0) + 1
        for i in range(timeout):
            if idle >= 20:
                return
            if self.talking:
                idle = 0
                talked += 1
                if talked == 90:
                    self.shot(f"dlg{self.dialogues}_a")
                if talked == 150:
                    self.shot(f"dlg{self.dialogues}_b")
                # Let the text type at normal speed for the screenshots, then mash.
                self.step((A,) if talked > 150 and i % 4 == 0 else ())
            else:
                idle += 1
                self.step()
        raise RuntimeError("dialogue never closed")

    def wait(self, n):
        for _ in range(n):
            self.step()

    def tap(self, key, gap=6):
        self.step((key,))
        self.wait(gap)

    def hold(self, n, *keys):
        for _ in range(n):
            self.step(keys)

    def walk_to(self, x, y, tol=2.0, y_first=False, timeout=900):
        last, still = (self.x, self.y), 0
        for _ in range(timeout):
            if self.talking:
                self.read_dialogue()
                last, still = (self.x, self.y), 0
            px, py = self.x, self.y
            still = still + 1 if (px, py) == last else 0
            last = (px, py)
            if still > 90:
                raise RuntimeError(f"stuck walking to ({x},{y}) at ({px:.1f},{py:.1f}) on {self.map_name()}")
            dx, dy = x - px, y - py
            xd, yd = abs(dx) <= tol, abs(dy) <= tol
            if xd and yd:
                return
            if (not yd) if y_first else xd:
                self.step((UP if dy < 0 else DOWN,))
            else:
                self.step((LEFT if dx < 0 else RIGHT,))
        raise RuntimeError(f"walk_to({x},{y}) timed out")


def boot_to_blank(g):
    """Title screen -> menu -> new game, until the player stands in BLANK."""
    for _ in range(4000):
        if g.map_name() == "BLANK" and abs(g.x - 77) < 1 and abs(g.y - 87) < 1:
            g.wait(30)
            return
        if g.talking:
            g.read_dialogue()  # the intro text (screenshots dlg1_*)
            continue
        g.tap(A, gap=8)
    raise RuntimeError("never reached BLANK")


def interact(g, x, y):
    """Stand below an object at (x,y), face up and press Accept (A)."""
    g.walk_to(x, y, y_first=True)
    g.hold(30, UP)
    for _ in range(3):
        g.tap(A, gap=8)


def route_nexus(g):
    """Mirror of the C# StartToNexus bot (src/tools/bot/csharp/StartToNexus.cs)."""
    boot_to_blank(g)
    g.shot("00_blank_start")
    g.walk_to(118, 87)
    for _ in range(400):
        if g.x > 300:
            break
        g.step((DOWN,))
    g.wait(60)
    g.walk_to(390, 300)
    g.shot("02_console_room")
    interact(g, 341, 282)
    interact(g, 443, 282)
    g.wait(120)
    g.shot("03_gates_open")
    g.walk_to(402, 250)
    g.walk_to(402, 100)
    g.walk_to(397, 70)
    for _ in range(600):
        if g.map_name() == "NEXUS":
            break
        g.step((UP,))
    g.wait(60)
    g.shot("04_nexus")
    if g.map_name() != "NEXUS":
        raise RuntimeError("did not reach the NEXUS")


ROUTES = {"nexus": route_nexus}


def main():
    args = sys.argv[1:]
    shots = None
    if "--shots" in args:
        i = args.index("--shots")
        shots = args[i + 1]
        del args[i:i + 2]
    elf, rom, route, out = args
    g = Gba(elf, rom, shots)
    err = None
    try:
        ROUTES[route](g)
    except Exception as e:  # keep the partial trace
        g.shot("fail")
        err = str(e)
    with open(out, "w") as f:
        f.write("frame\tmap\tx\ty\n")
        for fr, m, x, y in g.log:
            f.write(f"{fr}\t{m}\t{int(x)}\t{int(y)}\n")
    if err:
        print("ROUTE FAILED:", err)
        sys.exit(1)
    print("route ok,", g.frame, "frames")


if __name__ == "__main__":
    main()
