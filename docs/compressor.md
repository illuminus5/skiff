# Per-track compressor prototype

Built on the tested TAPE + USB10 configuration, in a separate branch and firmware.
The TAPE pull request and the working TAPE firmware are unchanged.

## Controls

On the instrument track to process, press FX until INSERT, then select COMP
with knob 1. The four headers are TYPE / THRESH / RATIO / RELEASE:

- THRESH: -48 to 0 dB, relative to one engine voice's 32768 amplitude before
  track LEVEL. The summed voices can exceed this reference.
- RATIO: 1:1, 1.5:1, 2:1, 3:1, 4:1, 6:1, 8:1, 12:1.
- RELEASE: 20–1000 ms. Both the peak detector and gain recovery use this time;
  recovery is gradual, not a fixed duration to full level.
- Attack is fixed at approximately 3 ms. No lookahead delay or automatic makeup
  gain is added. Existing INSERT 2 > MIX blends dry and compressed audio.

Start around -18 dB, 3:1, 200 ms, MIX 100%, then adjust while listening. Lower
output level is expected: there is no automatic makeup gain. Track LEVEL remains
available after compression.

Each track has independent state. A drum track processes its whole kit. There
are no individual kick/snare/lane compressors yet. COMP occupies the existing
INSERT slot, so it replaces that track's other insert effect. DRIVE, SLICER,
DUCK, shared FX and TAPE still operate. Existing DUCK remains the note-triggered
sidechain option; COMP currently detects its own track only.

The linked detector takes the peak of the mono and stereo component buses;
its gain is applied equally to all components, preserving their relationships
and panned drum isolation. Compression runs after DRIVE and before SLICER,
DUCK, LEVEL, shared-effect sends, TAPE SEND and track USB capture. SPREAD's side
components are compressed together with its mono bus; the other INSERT types
retain their existing mono processing.

## Storage and implementation

COMP is appended as INSERT type 9; all existing type numbers are unchanged.
THRESH/RATIO/RELEASE reuse A/B/C and MIX uses the existing insert mix. No new
parameter IDs, SKF4 space, per-track RAM or flash sectors are allocated. The
existing insert state is reused, reset and faded on type changes. Projects and
user presets retain the settings. Older firmware does not understand COMP;
use a separate test project if you need the previous build to play it unchanged.

The hard-knee peak curve uses fixed-point log2/exp2 interpolation once per audio
block. Peak hold/release prevents the gain following individual waveform lobes.
Gain and mix are smoothed per sample. The control path uses no runtime floating
point or dynamic allocation. Ratio 1:1 from rest, TYPE OFF and MIX 0 are neutral.

## Validation and limits

Run `sh tests/run_compressor_tests.sh` after generated headers exist.

- Analytical transfer-curve comparison: maximum error 0.030 dB over the tested
  thresholds, ratios and peaks up to eight voices' full scale.
- Sanitized tests: attack/release, linked stereo, side-only detector, bypass,
  bounded extremes, independent track state, project and user-preset restore,
  and correct displayed parameter labels.
- Existing INSERT tests, full UI, project/backup/editor/settings/FM6/audio tests
  and USB tests pass. Compressor-enabled panned-drum USB/TAPE tests pass.
- Five mixed-engine scenes match bit-for-bit across all tested core splits
  with compressors enabled. Existing non-compressor worker tests pass as well.
- 176 golden renders are unchanged; no health/routing failures or crashes.
  The full host regression still fails ANALOG STRINGS and idle CPU baselines
  (+33% each in this run). Baselines were not rewritten.
- Original target budgets pass. Image 564400 B; data+bss 94196/98304 B;
  pool 346392/356352 B; RAM code 24228/24576 B.
- New `ins_compress` path: 162 target instructions, 88 in loops, no loop
  divides/calls according to the existing static analyzer. This is a code-cost
  report, not an established target-time budget for the new path.
- Host benchmark, six sustained synth tracks: 1301.6 instructions/sample with
  COMP off, 1573.9 with six compressors (+20.9%); 2052.7 including TAPE capture,
  2534.2 including TAPE playback/filter/LFO. These are host measurements, not
  pi32v2 cycles, and do not establish FM-1 real-time headroom.

This prototype has not been physically tested or flashed automatically. Test
one track first, then a drum kit, then a dense arrangement with TAPE and USB10.
Listen for glitches and check the device CPU display. The earlier tested TAPE
package remains available as the rollback build.
