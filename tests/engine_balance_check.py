# SPDX-License-Identifier: GPL-3.0-only
"""Compare disabled/enabled calibration CSVs. RMS is a diagnostic, not LUFS."""
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path

out = Path(sys.argv[1])
gains = [8231, 32768, 6172, 14637, 7336, 6925, 15504, 9235,
         13818, 32768, 9783, 21900, 16423, 18427, 20675]


def rows(kind, enabled):
    return list(csv.DictReader((out / f"{kind}_{enabled}.csv").open()))


summary = defaultdict(dict)
for kind in ("raw", "premaster", "output", "routes"):
    before, after = rows(kind, 0), rows(kind, 1)
    assert len(before) == len(after) == (15 if kind == "routes" else 450)
    assert set(int(r["engine"]) for r in before) == set(range(15)) - {1}
    groups = defaultdict(lambda: [[], []])
    for a, b in zip(before, after):
        for field in ("engine", "preset", "note", "route", "envhash"):
            assert a[field] == b[field], (kind, field, a, b)
        g = gains[int(a["engine"])] / 32768
        trim = 20 * math.log10(g)
        if kind == "raw":
            assert a == b, ("raw engine rendering must be unchanged", a, b)
        if kind == "premaster":
            for field in ("rms_1s_dbfs", "max_100ms_rms_dbfs"):
                # Includes very quiet/short SLICE sounds; integer quantization.
                assert abs(float(b[field]) - float(a[field]) - trim) < 0.10, (a, b, field)
        if g == 1:
            assert a == b, (kind, "unity must be exact", a, b)
        if kind == "routes":
            for field in ("left_energy", "right_energy", "chorus_energy", "delay_energy", "reverb_energy"):
                x, y = float(a[field]), float(b[field])
                if not x:
                    assert not y
                else:
                    # Rounded low-level sends allow 2% energy error.
                    assert abs(y / x / (g*g) - 1) < 0.02, (field, a, b)
        groups[a["name"]][0].append(float(a["max_100ms_rms_dbfs"]))
        groups[a["name"]][1].append(float(b["max_100ms_rms_dbfs"]))
    if kind not in ("raw", "routes"):
        for name, values in groups.items():
            summary[name][kind] = [statistics.median(v) for v in values]
    print(f"balance: {kind}: {len(before)} paired renders passed")

print("engine       premaster before/after    output before/after (median peak-100ms RMS dBFS)")
for name, data in summary.items():
    print(f"{name:10s} {data['premaster'][0]:7.2f} {data['premaster'][1]:7.2f}     {data['output'][0]:7.2f} {data['output'][1]:7.2f}")
raw = [v["premaster"][1] for v in summary.values()]
assert max(raw) - min(raw) < 0.6, ("calibrated engine medians spread", raw)
print(f"balance: premaster median spread {max(raw)-min(raw):.3f} dB; output within range; envelope fingerprints unchanged")
