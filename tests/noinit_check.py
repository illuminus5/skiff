#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""(2.7.2, #149) cast.c's cast_boot, the .noinit word that holds the splash for a cast's restart, as the target compiler
built it (build/felucca.dis, tools/build.py's pi32v2 listing): every access to it a whole word, and CAST_BOOT ("CSTB",
1129534530) in the code. .noinit is not cleared at power-on, so the word must be compared whole: 2.6 to 2.7.1 kept it as
a one-byte flag (the compiler's, as only 0 and CAST_BOOT are stored) and any leftover byte held the splash 10 s.
  tests/noinit_check.py [DIS]"""
import re
import sys

dis = open(sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis", encoding="utf-8", errors="replace").read().split("\n")
refs, bad = 0, []
for i, line in enumerate(dis):
    m = re.search(r"\b(r\d+) = \d+ <cast_boot : ", line)
    if not m:
        continue
    refs += 1
    reg = m.group(1)
    use = next((u for u in dis[i + 1:i + 12] if f"[{reg}+" in u or f"[{reg}]" in u), None)
    if use is None or re.search(r"\b[bh]\[" + reg + r"\b", use):
        bad.append((line.strip(), use.strip() if use else "no access found"))
cstb = sum(1 for line in dis if re.search(r"\b1129534530\b|0x43535442", line, re.I))
for ref, use in bad:
    print(f"noinit: cast_boot not a whole word: {ref}  ->  {use}")
ok = refs >= 3 and not bad and cstb >= 2
print(f"noinit: cast_boot: {refs} references, {len(bad)} not a whole word, CAST_BOOT in the code {cstb} times"
      + ("" if ok else " (need >= 3 references, all whole words, CAST_BOOT >= 2)"))
print("noinit check passed" if ok else "NOINIT CHECK FAILED")
sys.exit(0 if ok else 1)
