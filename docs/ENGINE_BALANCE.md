<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Experimental engine output balance

**Opt-in prototype. Not release-ready.** Base revision: `illuminus5/skiff`
main `fe387231f7b7cb98823f19d8d4f99fee4fcd3792`.

## Build and compatibility

Default `FELUCCA_ENGINE_BALANCE=0` matches all 176 existing audio golden renders.
The shared LEVEL multiply also fixes an overflowing intermediate at extreme
inputs; where that old intermediate was valid, its rounding is unchanged.
No settings, preset fields, project formats or editor IDs change. Enable with
`FELUCCA_ENGINE_BALANCE=1 ./build.sh`. There is no runtime switch. Enabled builds
change existing projects and user samples too; a default build restores their
original mix. Browser emulator:
`EMU_CFLAGS=-DFELUCCA_ENGINE_BALANCE=1 web/emu/build.sh`.

Host-only checks: `sh tests/run_engine_balance_tests.sh`. They write diagnostics
under `build/host/engine_balance/`, never replace committed golden files, do not
need the JieLi compiler and do not access hardware. Both flags must pass their
frozen audio hashes, calibration checks and the same 3 dB DAC/USB harmonic-growth
limit. The USB estimator is noise-aware and checked against deliberately broken
quantizers. It also runs enabled multicore equivalence, voice-budget and
performance-effect checks. The experimental calibration remains separate from
the normal aggregate suite.

## Provisional calibration

150 factory presets x three notes: MIDI 48/60/72, or kick/snare/hat 36/38/42 for
DRUM/DRUM-X. One note at velocity 100 held for one second; factory sound/envelope,
ARP off, LEVEL 112 (0 dB), no DRIVE/INSERT or sends, SPREAD off, master full,
speaker processing off. Each render starts in a fresh process. Metric: loudest
non-overlapping approximately 100 ms RMS window, aggregated as the median over
that engine's presets/notes. **This is not LUFS or equal perceived loudness.**

PHYS is the quietest median, about -20 dBFS. Other engines are attenuated toward
it in half-dB increments, never boosted. Gains are
`round(32768 * 10**(trim_dB / 20))`, with 32768 exact unity. DIGITAL is retired,
not in default builds, unmeasured and left at unity.

The final calibration point is the dry mixer before the master, not the raw
voice renderer. This includes normal modulation processing. Pre-master sums are
32-bit and can exceed 0 dBFS without integer clipping. Relative reference: 32768.

| Engine | Presets | Trim dB | Before master, old | Before master, balanced | Output old | Output balanced |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ANALOG | 25 | -12.0 | -7.97 | -19.97 | -10.16 | -20.01 |
| PHASE | 9 | -14.5 | -5.55 | -20.05 | -10.51 | -20.11 |
| LOFI | 8 | -7.0 | -13.04 | -20.04 | -13.32 | -20.10 |
| SAMPLE | 10 | -13.0 | -6.79 | -19.79 | -10.98 | -19.85 |
| VOICE | 8 | -13.5 | -6.39 | -19.89 | -10.64 | -19.96 |
| TRIO | 9 | -6.5 | -13.36 | -19.86 | -14.56 | -19.95 |
| WHEEL | 8 | -11.0 | -9.19 | -20.19 | -12.23 | -20.25 |
| GRAIN | 8 | -7.5 | -12.56 | -20.06 | -14.41 | -20.16 |
| PHYS | 11 | 0.0 | -19.89 | -19.89 | -20.00 | -20.00 |
| DRUM | 13 | -10.5 | -9.52 | -20.02 | -14.58 | -20.16 |
| NOISE | 8 | -3.5 | -16.52 | -20.02 | -16.85 | -20.12 |
| FM6 | 14 | -6.0 | -14.10 | -20.10 | -14.30 | -20.15 |
| SLICE | 9 | -5.0 | -14.98 | -19.98 | -15.27 | -20.05 |
| DRUM-X | 10 | -4.0 | -15.89 | -19.89 | -17.38 | -20.08 |

These values align engine **medians**, not every patch. Percussion, sparse noise,
short samples, pads and basses should not all have identical energy. Preset
variation remains; arbitrary user samples and imported FM patches are not
calibrated. Solo levels are lower. Do not undo the headroom with blind boosts.
A production pass should use musical roles, phrase-based loudness and listening.

## Implementation and precision

The final patch does not scale voice amplitude endpoints. An early prototype did
and failed the existing DAC tail-distortion checks. Folding the gain into Q12
track LEVEL still cut quiet samples too early and failed those checks as well.

The raw engines and the order of track DRIVE/INSERT/LEVEL/PAN processing stay
unchanged. The dry contribution is attenuated into Q22, preserving seven bits
below Q15. Both cores accumulate Q22 directly in their existing dry buffers.
After summing the cores, the Q15 part goes to THROW/DUB and the buses; its seven
low bits temporarily use the existing `dac_lo` buffer. The master consumes each
residue before replacing it with final output low bits. There are no additional
balance buffers and no cross-core writes to `dac_lo`.

Sends retain their existing precision and double-rounding rule. SAMPLE extra
sends use the corresponding level coefficient without applying the dry trim
twice. The sounding engine ID is used through switch fades. No AGC, compressor,
operator adjustment, saved-state change or gain boost is introduced.

## Size/CPU optimization (2026-11-12)

The pre-optimization reference is
[`osleg/skiff@63ff3b4`](https://github.com/osleg/skiff/commit/63ff3b4). Measured target iterations
replaced split multiplies with the pi32v2's native signed wide multiply/shift,
reduced register lifetimes, scaled each track's peak once per block, replaced
selected early-return clamps with independent min/max operations, and resolved
the cross-section mixer call once per block instead of once per track.

- The enabled image shrank **580,192 -> 579,528 bytes** (664 saved), leaving
  **2,036 bytes** in the 581,564-byte app slot. The disabled build also shrank
  **579,544 -> 579,068 bytes** (476 saved); enabled now costs 460 bytes over it.
- `.data + .bss`: **89,272 -> 88,760 / 94,208 bytes** (512 saved).
- RAM code + `.ramx`: **30,080 -> 29,764 / 32,512 bytes** (316 saved).
- Pool: unchanged **351,128 / 360,448 bytes**. Loader: unchanged
  **7,273 / 7,936 bytes**.

| Static function cost | Before | Optimized | Original budget |
| --- | ---: | ---: | ---: |
| audio ISR (`fm1_alnk0_irq`) | 3600 | 1386 | 1266 |
| `perf_master` | 68 | 58 | 58 |
| `mix_spread` | 198 | 155 | 143 |
| `lpan_mix` | 136 | 93 | 91 |
| `mix_part` | 1325 | 1245 | 1576 |

All original target checks pass their existing **10% tolerance**. The estimator,
budgets and original audio/UI golden files are unchanged. These are weighted
static instruction estimates, **not CPU load or cycle measurements**. In
particular, much of the old ISR score came from a backward control-flow join
misidentified as a nested loop. Its actual instruction count is **726 -> 690**;
do not interpret 3600 -> 1386 as a comparable runtime speedup. Helpers called
once per block still cost work, even though the loop score counts only calls
inside loops. Linux lacks the macOS host instruction counter.

The LEVEL multiplication now uses the full wide intermediate before saturation.
It keeps the original rounding where the old intermediate was valid, and avoids
its signed overflow at extreme input/LEVEL combinations. All 128 LEVEL values
and every input in [-2^20, 2^20] are checked against a wide integer oracle under
UBSan with no excluded UB categories. Engine gains, dry-sum bounds, opposite-sign
cancellation, master boundaries and the shared Q16 multiplier are checked too.

## Quiet-tail measurement repair

The old test allocated 617,400 samples but rendered 617,408. AddressSanitizer
confirmed the eight-sample overflow; buffers now round up to a whole block.
At 25% master, enabled USB H1 reaches roughly -98 to -103 dBFS. Its expected H3
is below the dither-noise floor of a single 4096-sample spectral window, so random
noise contaminated the old single-run harmonic estimate.

The test retains those raw measurements as diagnostics, while checking USB H3
on the signed average of **256 distinct dither seeds** of the same real DAC
waveform. Averaging removes uncorrelated noise, not coherent distortion. The
original notes, EQ/master cases, windows, fade depths and **3 dB growth limit**
remain. Every individual quantization error must also stay below 1.5 USB LSB,
exact silence must remain zero, and nonfinite or missing measurements fail.
Plain truncation, rounding without dither and dropping the seven DAC low bits
are deliberately injected and must be rejected with both flags. Production
USB/dither code is unchanged. AddressSanitizer passes both configurations.

## Verification and remaining limits

- 450 raw render pairs remain identical and envelope fingerprints unchanged.
- 450 dry pairs match the intended trim within 0.1 dB. Engine median spread is
  about 0.40 dB; output remains in range. Fifteen send/routing cases pass.
- The disabled build retains all 176 upstream golden renders. The enabled build
  matches all 176 pre-optimization renders, now frozen separately in
  `tests/engine_balance_golden.txt`, rather than regenerated during CI.
- The enabled native simulator's 88,200 stereo frames match the pre-optimization
  output byte-for-byte. Multicore and performance-effect checks are retained.
- Target builds enforce original flash/RAM/pool/loader limits. The
  [fork verification run](https://github.com/osleg/skiff/actions/runs/43282006301)
  at `bc70671` built both browser simulators and checked native/Wasm parity and
  interaction/persistence. Its artifact-only workflow and SDK inputs are not
  part of this change. Browser emulation does not test USB or hardware timing.

The contributor flashed the `bc70671` experimental build on an FM-1 and reported
"sounds good for now" on 2026-11-12. This is an initial listening check, not a
complete hardware validation or a hardware timing measurement. No release was
published.

Before release: broader FM-1/USB listening and timing, role-appropriate loudness
calibration, quiet lane/sample levels, imported patches, parameter extremes,
busy six-track mixes and engine changes with insert tails. Static trims remain
experimental; mixer effects can depend on absolute level.

Methodology reference for a later pass:
[the SLOOP level-presets tool retained in STATTIC](https://github.com/Timqos/stattic/blob/acfafc77f1bcd16455a9b3d0aeb5edbbf650d795/tools/level_presets.py)
uses role-appropriate phrases, offline gated BS.1770 loudness and per-preset
trims. Its table is not portable across engine sets or master paths. No code or
trim values from it were copied here.
