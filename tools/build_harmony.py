#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the optional FM-1 sample choir on current Skiff. No device writes.
Requires JIELI_TOOLCHAIN and AC79_SDK as tools/build.py does.
Usage: python tools/build_harmony.py [--release VERSION]
"""
import sys
import build
for i,arg in enumerate(sys.argv[1:],1):
    if (arg=="--board" and sys.argv[i+1:i+2]!=["fm1"]) or (arg.startswith("--board=") and arg!="--board=fm1"):
        raise SystemExit("HARMONY currently targets FM-1 only")
build.CFLAGS += ["-DFELUCCA_HARMONY=1", "-DVS_METRICS=0"]
if len(sys.argv)==1:
    sys.argv += ["--release", "2.1.1-harmony1"]
build.main()
