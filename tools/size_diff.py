#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""(Skiff 2.0) What a change costs in flash and RAM: two builds' ELFs compared, section by section, symbol by symbol
and source file by source file, the biggest changes first. Build the base commit in a mirror of its own (git archive),
never in another session's build dir.
  size_diff.py OLD.elf NEW.elf [N]      (N: the rows of each list, default 30; needs binutils' nm and readelf)
The image is what the app slot holds: .text + .ram_text + .data. Symbols named .GJTIE* lie inside other functions
(their jump tables): left out, as are unsized labels."""
import subprocess
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import size_fns  # noqa: E402

SECTIONS = (".text", ".ram_text", ".data", ".bss", ".pool", ".noinit", ".ramx")
IMAGE = (".text", ".ram_text", ".data")


def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, check=True).stdout


def load(elf):
    """{section: size}, {symbol: (section, size)}"""
    sec, bounds = {}, []
    for line in run("readelf", "-S", "-W", elf).splitlines():
        p = line.replace("[ ", "[").split()
        if len(p) > 5 and p[1] in SECTIONS:
            a, n = int(p[3], 16), int(p[5], 16)
            sec[p[1]] = n
            bounds.append((a, a + n, p[1]))
    syms = {}
    for line in run("nm", "-S", "--defined-only", elf).splitlines():
        p = line.split()
        if len(p) != 4 or p[3].startswith("."):
            continue
        a, n = int(p[0], 16), int(p[1], 16)
        s = next((b[2] for b in bounds if b[0] <= a < b[1]), None)
        if s:
            syms[p[3]] = (s, syms.get(p[3], (s, 0))[1] + n)   # (a name static in two places: summed)
    return sec, syms


def file_of():
    """symbol -> the source file defining it (size_fns' definitions: the unity build's symbols carry no file)"""
    m = {}
    for f in sorted(size_fns.SRC.glob("*.c")):
        for n in size_fns.definitions(f.read_text(errors="replace")):
            m.setdefault(n, f.name)
    return m


def main(old, new, n=30):
    (s0, y0), (s1, y1) = load(old), load(new)
    print(f"{'section':<10} {'old':>9} {'new':>9} {'delta':>8}")
    for s in SECTIONS:
        if s in s0 or s in s1:
            print(f"{s:<10} {s0.get(s, 0):9d} {s1.get(s, 0):9d} {s1.get(s, 0) - s0.get(s, 0):+8d}")
    i0, i1 = sum(s0.get(s, 0) for s in IMAGE), sum(s1.get(s, 0) for s in IMAGE)
    print(f"{'image':<10} {i0:9d} {i1:9d} {i1 - i0:+8d}   (.text + .ram_text + .data)\n")
    d = {k: y1.get(k, ("", 0))[1] - y0.get(k, ("", 0))[1] for k in set(y0) | set(y1)}
    d = {k: v for k, v in d.items() if v}
    print(f"symbols changed: {len(d)}")
    for k in sorted(d, key=lambda k: (-abs(d[k]), k))[:n]:
        sec = (y1.get(k) or y0.get(k))[0]
        print(f"  {d[k]:+7d}  {k}  ({sec}{', new' if k not in y0 else ', gone' if k not in y1 else ''})")
    fm, per = file_of(), Counter()
    for k, v in d.items():
        per[fm.get(k.split(".")[0], "?")] += v
    print("\nper source file (inlined code counts where it lands):")
    for f, v in sorted(per.items(), key=lambda x: -abs(x[1]))[:n]:
        if v:
            print(f"  {v:+7d}  {f}")
    return 0


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2], *(int(a) for a in sys.argv[3:4])))
