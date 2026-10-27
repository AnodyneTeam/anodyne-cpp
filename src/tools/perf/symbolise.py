#!/usr/bin/env python3
# Groups perf_tour's perf_profile.txt ("pc lr cycles" lines) by function: symbolise.py first.elf perf_profile.txt [top]
# The second table puts the time spent in libgcc helpers (soft float, division...) and the memcpy/memset family on
# the functions that called them.
import bisect, subprocess, sys

syms = []
for line in subprocess.run(["arm-none-eabi-nm", "-C", "-S", "--defined-only", sys.argv[1]], capture_output=True,
                           text=True).stdout.splitlines():
    parts = line.split(" ", 3)
    if len(parts) == 4 and parts[2] in "TtWw":
        syms.append((int(parts[0], 16) & ~1, int(parts[1], 16), parts[3]))
syms.sort()
starts = [s[0] for s in syms]


def name_of(pc):
    i = bisect.bisect_right(starts, pc & ~1) - 1
    return syms[i][2] if i >= 0 and pc < syms[i][0] + max(syms[i][1], 2) else "?%08x" % pc


def is_helper(name):
    return name.startswith("__") or name.startswith("____") or name in ("memset", "memcpy", "strcmp", "strlen",
                                                                       "memcpy32", "memcpy16", "memset32", "memset16")


self_time = {}
inclusive = {}
total = 0
for line in open(sys.argv[2]):
    pc, lr, cycles = line.split()
    pc, lr, cycles = int(pc, 16), int(lr, 16), int(cycles)
    total += cycles
    name = name_of(pc)
    self_time[name] = self_time.get(name, 0) + cycles
    owner = name_of(lr) if is_helper(name) else name
    inclusive[owner] = inclusive.get(owner, 0) + cycles

top = int(sys.argv[3]) if len(sys.argv) > 3 else 30
print("# %d cycles" % total)
for title, table in (("self", self_time), ("with the helpers it called", inclusive)):
    print("## " + title)
    for name, cycles in sorted(table.items(), key=lambda kv: -kv[1])[:top]:
        print("%5.1f%% %10d %s" % (100.0 * cycles / total, cycles, name[:110]))
