# SPDX-License-Identifier: GPL-3.0-only
"""Validate tail-test sensitivity with the real production dither and bad quantizers.

The baseline must pass, and each deliberate quantization regression must fail
its harmonic-growth assertion, for both balance configurations by default.
Usage: python3 tests/engine_balance_tail.py OUTPUT [--balance 0|1] [--asan]
"""
import argparse
import os
import shlex
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
parser.add_argument("--balance", type=int, choices=(0, 1), action="append")
parser.add_argument("--asan", action="store_true", help="Check address bounds, including final render block")
args = parser.parse_args()
root = Path(__file__).resolve().parent.parent
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
mutations = {0: "baseline", 1: "truncate", 2: "round_without_dither", 3: "lost_low_bits"}
for balance in args.balance or (0, 1):
    for mutation, name in mutations.items():
        stem = f"tail_balance{balance}_{name}" + ("_asan" if args.asan else "")
        binary = out / stem
        command = shlex.split(os.environ.get("CC", "cc")) + [
            "-O1" if args.asan else "-O2", "-w", "-Itests", "-Ibuild/gen", "-Ifirmware/src",
            f"-DFELUCCA_ENGINE_BALANCE={balance}", f"-DTAIL_USB_MUTATION={mutation}",
            "tests/tail_test.c", "-lm", "-o", str(binary)]
        if args.asan:
            # Address sanitizer is separate from legacy DSP signed-wrap/shift UB.
            command += ["-g", "-fsanitize=address", "-fno-omit-frame-pointer"]
        built = subprocess.run(command, cwd=root, text=True, capture_output=True)
        (out / f"{stem}.build.log").write_text(built.stdout + built.stderr)
        built.check_returncode()
        env = dict(os.environ)
        if args.asan:
            env["ASAN_OPTIONS"] = "detect_leaks=0:halt_on_error=1"
        result = subprocess.run([str(binary)], cwd=root, env=env, text=True, capture_output=True)
        log = result.stdout + result.stderr
        (out / f"{stem}.log").write_text(log)
        harmonic_failures = sum("H3/H1 grows 3 dB" in line and "FAIL" in line for line in log.splitlines())
        noise_failures = any("quantization error" in line and "FAIL" in line for line in log.splitlines())
        if mutation == 0:
            assert result.returncode == 0 and "tail: all passed" in log, f"{stem} failed: {out / (stem + '.log')}"
        else:
            # A crash, sanitizer error, or unrelated failure is not evidence of sensitivity.
            assert result.returncode == 1 and harmonic_failures and "FAILED" in log, f"{stem} did not reject harmonic regression"
            assert not noise_failures and "AddressSanitizer" not in log, f"{stem} failed for an unrelated reason"
        print(f"{stem}: {'passed' if not mutation else f'regression rejected ({harmonic_failures} harmonic checks)'}", flush=True)
