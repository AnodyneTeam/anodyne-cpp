#!/usr/bin/env python3
"""Client for the simulator's bot link (sim_ipc.h): start the simulator with --ipc PORT, then drive it.

    from simlink import Sim
    sim = Sim.launch("build-sim/anodyne/platform/sim/anodyne_sim")
    sim.hold("start", 1)
    print(sim.state["player"])

Also usable from the command line for quick checks:

    simlink.py SIM_BINARY script.txt

where each line of script.txt is "keys <names> <frames>", "wait_mode <mode>", "walk_to <x> <y>", "shot <png>",
"interact" or "print".
"""
import json
import os
import socket
import subprocess
import sys
import time

# KEYINPUT bit order.
KEYS = {"a": 0, "b": 1, "select": 2, "start": 3, "right": 4, "left": 5, "up": 6, "down": 7, "r": 8, "l": 9}


# Entity classes the player walks over or through (the path finder ignores them).
WALK_OVER = {"Placeholder", "Button", "Checkpoint", "KeyBlockSentinel", "CrackedTile", "Dust", "SpringPad",
             "JumpTrigger", "HealthPickup", "TreasureItem", "Explosion", "Key", "Blocker", "VBlocker", "FollowerBro",
             "GatePart", "Treasure_Content", "SageBlank"}


def mask(names):
    m = 0
    for n in names.replace("+", " ").split():
        if n and n != "none":
            m |= 1 << KEYS[n.lower()]
    return m


class Sim:
    def __init__(self, port, proc=None):
        self.proc = proc
        for _ in range(200):
            try:
                self.sock = socket.create_connection(("127.0.0.1", port))
                break
            except OSError:
                time.sleep(0.05)
        else:
            raise RuntimeError("can't connect to the simulator")
        self.file = self.sock.makefile("rb")
        self.frame = 0
        self.state = {}
        self._read_state()

    @classmethod
    def launch(cls, binary, port=47011, extra=()):
        env = dict(os.environ)
        env.setdefault("ASAN_OPTIONS", "detect_leaks=0")
        # SIMLINK_LOG=path keeps the simulator's own output out of the client's.
        log = os.environ.get("SIMLINK_LOG")
        out = open(log, "w") if log else None
        proc = subprocess.Popen([binary, "--headless", "--ipc", str(port), *extra], env=env, stdout=out, stderr=out)
        return cls(port, proc)

    def _read_state(self):
        line = self.file.readline()
        if not line:
            raise EOFError("simulator closed the link")
        msg = json.loads(line)
        self.frame = msg["frame"]
        self.state = msg["state"]
        return self.state

    def _send(self, line):
        self.sock.sendall((line + "\n").encode())

    def hold(self, keys="", frames=1):
        """Holds keys (names like "a", "left+up" or a mask) for some frames; returns the state after them."""
        m = keys if isinstance(keys, int) else mask(keys)
        self._send(f"keys {m:x} {frames}")
        return self._read_state()

    def wait(self, frames=1):
        return self.hold(0, frames)

    def tap(self, keys, release=6):
        self.hold(keys, 1)
        return self.wait(release)

    def shot(self, path):
        self._send(f"shot {path}")
        return self.file.readline().decode().strip()

    def peek(self, addr, length):
        self._send(f"peek {addr:x} {length}")
        return bytes.fromhex(self.file.readline().decode().strip())

    def warp(self, map_name, x, y):
        """Test shortcut: warps to (x, y) of a map with the broom; waits until the player is there."""
        self._send(f"warp {map_name} {x} {y}")
        reply = self.file.readline().decode().strip()
        if reply != "ok":
            raise ValueError(reply)
        self.wait_until(lambda s: s.get("map") == map_name, 400)
        return self.wait(60)  # the fade in

    def heal(self):
        """Test shortcut: back to full health."""
        self._send("heal")
        self.file.readline()

    def quit(self):
        try:
            self._send("quit")
        except OSError:
            pass
        if self.proc:
            self.proc.wait(timeout=10)

    def collision(self):
        """The map's collision: (w, h, cells) with cells[y][x] = (blocked sides, tile event)."""
        self._send("collision")
        words = self.file.readline().decode().split()
        w, h = int(words[0]), int(words[1])
        data = words[2] if len(words) > 2 else ""
        cells = [[(int(data[(y * w + x) * 2], 16), int(data[(y * w + x) * 2 + 1], 16)) for x in range(w)]
                 for y in range(h)]
        return w, h, cells

    # ---- helpers for tests --------------------------------------------------------------------------------------
    def wait_until(self, pred, limit=3600, keys=0):
        for _ in range(limit):
            if pred(self.state):
                return True
            self.hold(keys, 1)
        return pred(self.state)

    def player(self):
        return self.state["player"]

    def walk_to(self, x, y, limit=900, tolerance=1):
        """Walks the hitbox top-left towards (x, y) in a straight line, x first then y. Stops when stuck."""
        stuck = 0
        last = None
        for _ in range(limit):
            if self.state.get("dialog"):
                self.tap("a")
                continue
            p = self.player()
            dx, dy = x - p["x"], y - p["y"]
            if abs(dx) <= tolerance and abs(dy) <= tolerance:
                return True
            k = ""
            if abs(dx) > tolerance:
                k = "right" if dx > 0 else "left"
            elif abs(dy) > tolerance:
                k = "down" if dy > 0 else "up"
            self.hold(k, 1)
            pos = (self.player()["x"], self.player()["y"])
            stuck = stuck + 1 if pos == last and not self.state.get("transition") else 0
            last = pos
            if stuck > 30:
                return False
        return False


    def path_to(self, tx, ty):
        """BFS over 16x16 cells from the player's cell to the cell holding map pixel (tx, ty); open cells only,
        solid non-placeholder entities block. Returns the cell list or None."""
        from collections import deque
        w, h, cells = self.collision()
        blocked = set()
        for e in self.state.get("entities", []):
            if e["solid"] and e["exists"] and e["type"] != "Placeholder":
                for cy in range(e["y"] // 16, (e["y"] + e["h"] - 1) // 16 + 1):
                    for cx in range(e["x"] // 16, (e["x"] + e["w"] - 1) // 16 + 1):
                        blocked.add((cx, cy))
        p = self.player()
        start = ((p["x"] + 5) // 16, (p["y"] + 6) // 16)
        goal = (tx // 16, ty // 16)

        def ok(c):
            x, y = c
            if not (0 <= x < w and 0 <= y < h) or c in blocked:
                return False
            allow, ev = cells[y][x]
            return allow == 0 and ev not in (3, 5)

        prev = {start: None}
        q = deque([start])
        while q:
            c = q.popleft()
            if c == goal:
                break
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (c[0] + d[0], c[1] + d[1])
                if n not in prev and (ok(n) or n == goal):
                    prev[n] = c
                    q.append(n)
        if goal not in prev:
            return None
        path = []
        c = goal
        while c is not None:
            path.append(c)
            c = prev[c]
        return path[::-1]

    def _grid(self):
        w, h, cells = self.collision()
        blocked = set()
        for e in self.state.get("entities", []):
            if e["exists"] and (e["solid"] or e["type"] == "Hole") and e["type"] not in WALK_OVER and not e.get("enemy"):
                for cy in range(e["y"] // 16, (e["y"] + e["h"] - 1) // 16 + 1):
                    for cx in range(e["x"] // 16, (e["x"] + e["w"] - 1) // 16 + 1):
                        blocked.add((cx, cy))
        return w, h, cells, blocked

    def path_where(self, goal):
        """BFS over open 16x16 cells from the player's cell to the first cell where goal(cx, cy) holds."""
        from collections import deque
        w, h, cells, blocked = self._grid()
        p = self.player()
        start = ((p["x"] + 5) // 16, (p["y"] + 6) // 16)

        def ok(c):
            x, y = c
            if not (0 <= x < w and 0 <= y < h) or c in blocked:
                return False
            allow, ev = cells[y][x]
            return allow == 0 and ev not in (3, 5)

        prev = {start: None}
        q = deque([start])
        end = None
        while q:
            c = q.popleft()
            if goal(*c) and c != start:
                end = c
                break
            for d in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = (c[0] + d[0], c[1] + d[1])
                if n not in prev and ok(n):
                    prev[n] = c
                    q.append(n)
        if end is None:
            return None
        path = []
        c = end
        while c is not None:
            path.append(c)
            c = prev[c]
        return path[::-1]

    def follow(self, path, limit=240):
        for cx, cy in path[1:]:
            if not self.walk_to(cx * 16 + 3, cy * 16 + 2, limit=limit):
                return False
        return True

    def go_screen(self, gx, gy):
        """Walks onto screen (gx, gy) of this map (BFS to its nearest cell, then waits out the scroll)."""
        path = self.path_where(lambda cx, cy: cx // 10 == gx and cy // 10 == gy)
        if path is None:
            return False
        ok = self.follow(path)
        self.wait_until(lambda s: not s["transition"] and s["grid"] == [gx, gy], 300)
        self.wait(10)
        return ok and self.state["grid"] == [gx, gy]

    def go_dir(self, d):
        gx, gy = self.state["grid"]
        dx, dy = {"n": (0, -1), "s": (0, 1), "e": (1, 0), "w": (-1, 0)}[d]
        return self.go_screen(gx + dx, gy + dy)

    def read_dialogue(self, limit=400):
        n = 0
        while (self.state.get("dialog") or self.state.get("cutscene")) and n < limit:
            if self.state.get("dialog"):
                self.tap("a")
            else:
                self.wait(1)
            n += 1

    def interact_at(self, x, y, side="d"):
        """Stands next to the object whose cell holds (x, y) on `side` (d: below, u: above, l: left, r: right),
        faces it and presses A."""
        cx, cy = x // 16, y // 16
        dx, dy, face = {"d": (0, 1, "up"), "u": (0, -1, "down"), "l": (-1, 0, "right"), "r": (1, 0, "left")}[side]
        if not self.go(( cx + dx) * 16 + 8, (cy + dy) * 16 + 8):
            return False
        self.hold(face, 8)
        self.tap("a", 10)
        self.read_dialogue()
        return True

    def enemies(self):
        g = self.state["grid"]
        return [e for e in self.state["entities"] if e.get("enemy") and e["exists"]
                and e["x"] // 160 == g[0] and e["y"] // 160 == g[1]]

    def fight(self, limit=3000):
        """Swings at the screen's enemies until none are left: walks (along a BFS path when the straight line is
        blocked) next to the nearest one, faces it and presses A."""
        n = 0
        stuck = 0
        last = None
        while self.enemies() and n < limit:
            if self.state.get("dialog"):
                self.read_dialogue()
                continue
            p = self.player()
            px, py = p["x"] + 5, p["y"] + 6
            e = min(self.enemies(), key=lambda e: abs(e["x"] + e["w"] / 2 - px) + abs(e["y"] + e["h"] / 2 - py))
            ex, ey = e["x"] + e["w"] / 2, e["y"] + e["h"] / 2
            dx, dy = ex - px, ey - py
            if abs(dx) < 18 and abs(dy) < 18:
                face = ("right" if dx > 0 else "left") if abs(dx) > abs(dy) else ("down" if dy > 0 else "up")
                self.hold(face, 1)
                self.tap("a", 8)
                n += 9
                stuck = 0
                continue
            pos = (p["x"], p["y"])
            stuck = stuck + 1 if pos == last else 0
            last = pos
            if stuck > 6:
                # The straight line is blocked: a few cells along a path to the enemy's neighbourhood.
                ecx, ecy = int(ex) // 16, int(ey) // 16
                path = self.path_where(lambda cx, cy: abs(cx - ecx) + abs(cy - ecy) <= 1)
                if path:
                    self.follow(path[:4], limit=60)
                    n += 60
                stuck = 0
                continue
            k = []
            if abs(dx) > 10:
                k.append("right" if dx > 0 else "left")
            if abs(dy) > 10:
                k.append("down" if dy > 0 else "up")
            self.hold("+".join(k) or "", 4)
            n += 4
        return not self.enemies()

    def go(self, tx, ty):
        """Walks to the cell holding map pixel (tx, ty) along a BFS path; the hitbox ends centred in it."""
        path = self.path_to(tx, ty)
        if path is None:
            return False
        for cx, cy in path[1:]:
            if not self.walk_to(cx * 16 + 3, cy * 16 + 2, limit=240):
                return False
        return True

    def face(self, direction):
        """Turns to face a way without moving much (one frame of the d-pad)."""
        return self.hold(direction, 1)


def run_script(binary, script):
    sim = Sim.launch(binary)
    try:
        for raw in open(script):
            words = raw.split("#")[0].split()
            if not words:
                continue
            cmd = words[0]
            if cmd == "keys":
                sim.hold(words[1], int(words[2]) if len(words) > 2 else 1)
            elif cmd == "wait_mode":
                if not sim.wait_until(lambda s: s.get("mode") == words[1], keys=0):
                    print("timeout waiting for mode", words[1])
            elif cmd == "walk_to":
                ok = sim.walk_to(int(words[1]), int(words[2]))
                print("walk_to", words[1], words[2], "ok" if ok else "STUCK", sim.player())
            elif cmd == "interact":
                sim.tap("a")
            elif cmd == "shot":
                print(sim.shot(os.path.abspath(words[1])))
            elif cmd == "print":
                print(json.dumps(sim.state))
            else:
                print("unknown command", cmd)
    finally:
        sim.quit()


if __name__ == "__main__":
    run_script(sys.argv[1], sys.argv[2])
