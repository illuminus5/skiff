# DRUM COMP prototype

Independent compression for each of the eight drum lanes, on each instrument
track. Supports DRUM synthesized kits, DRUM-X, and DRUM kits using uploaded
USR1/USR2/USR3 samples. Existing track INSERT COMP, TAPE and USB10 remain.

## Use

1. Select a DRUM or DRUM-X track and open **EDIT → DRUM COMP** (5/5).
2. Turn **PRESETS** to choose the drum lane, or play its key. ALGORITHM keeps
   its normal track-selection function.
3. Four headers: **THRESH / RATIO / RELEASE / MIX**. MIX starts at 0% (off).
   Raise MIX to hear compression. PRESETS on this page chooses the lane without
   loading another kit.
4. Start around -18 dB, 3:1, 200 ms, MIX 100%, then adjust by ear. The eight-drum faders match EDIT > LANES (the former page 3/4); each shows
   that drum’s compression MIX, and the selected drum is highlighted.

Threshold -48..0 dB, eight ratios 1:1..12:1, release 20..1000 ms, fixed 3 ms
attack, no automatic makeup gain. The ratio knob advances one ratio per click.
The threshold reference is one engine voice's 32768 amplitude. MIX blends dry
and compressed audio. Quiet lanes incur no compressor sample-processing loop
when MIX is zero and its bypass fade has completed.

Signal path: rendered drum (including its own drive/velocity/envelope) -> lane
compressor -> lane LEVEL and PAN -> kit mix -> track DRIVE/INSERT/SLICER/DUCK
-> track LEVEL/sends/USB. Existing square-law lane LEVEL is retained. Both the
whole-kit compressor and individual lane compressors can be used together.
Compressing a kick does not reduce the hats; shared FX still behave as before.

State is independent per track/lane and owned by its rendering core. It uses
fixed-point block peak detection and smoothed sample gains/mix, without heap
allocation. Silent gaps recover lazily using fixed-point exponential decay.
All controls default to bypass. Unused existing audio remains bit-identical.
Factory sound loads/NEW reset controls; sound undo/redo restores them. The
initial page is controlled on the device: there are no lane-compressor motion
locks, automation lanes, or web-editor controls yet.

## Saved data and compatibility

Projects save lane settings, and quiet-stop autosave includes them. Existing
SKF3/SKF4 projects load with lane compression off; TAPE metadata remains intact.
A project with all lane controls at their defaults still saves as SKF4.

**Changing lane controls uses a new SKF5 project record (2816 bytes). Older
firmware cannot read SKF5. Use a spare project slot for initial testing.**
Existing saved projects are not converted until saved. Flash sectors, object
IDs, samples, user-slot count and patterns are unchanged.

SKF5 wraps the unchanged 2560-byte SKF4 payload, then 192 bytes of encoded lane
settings, 40 reserved zero bytes, a normalized duplicate name and an outer
checksum. The inner checksum, duplicate name, reserved bytes and all control
values are validated before restore or backup programming. It fits the
existing 3840-byte object limit and existing A/B flash transactions.

User preset records keep their existing 192-byte layout/version and slot
count. Thirty-two spare bytes after SID 142 hold one track's encoded settings;
a final marker identifies the extension. A compile-time guard prevents future
SID growth overlapping it. Older preset records load with compression off.
Older firmware can use the basic sound but will not apply the lane settings,
and resaving on it discards them. Full bank backups preserve the extension;
legacy sound-only SysEx/editor exports do not carry these additional controls.

## Validation

Run `sh tests/run_drum_comp_tests.sh` after generating headers. It includes:

- Independent lane/track state, bypass/fades, silent-gap recovery, and DRUM,
  DRUM-X and uploaded-sample kit renders. A panned kick's compression leaves
  the opposite hat's TAPE signal bit-identical; USB differences remain within
  eight internal LSBs from the existing mixer's separate integer rounding.
- Existing square-law lane LEVEL, UI visibility/headers, lane selection by
  PRESETS/key, idle redraw suppression, factory load reset and sound undo/redo.
- SKF4 migration; SKF5 save/reload/reboot/backup/autosave; USER preset flash
  reload; malformed value/checksum/name/reserved-byte rejection before erase;
  unchanged samples; all 12 interrupted payload/header write positions.
- Original track-compressor, TAPE DSP/UI/codec/project tests.
- Five mixed-engine scenes with both lane and kit compressors match across
  all tested one/two-core track splits.

Native drum/drum-X, FM6/audio, insert, project/backup/editor/settings, full UI
and USB tests pass. All 176 golden renders are unchanged; zero health/routing
failures or crashes. Three existing host CPU baseline checks remain flagged:
ANALOG STRINGS +34%, PHASE WIRE at approximately +25%, idle +33%. Baselines
were not changed. These CPU checks are not passing.

Original target/memory checks pass. New voice/lane rendering and mono mixing
have explicit out-of-line boundaries; their separate code-cost report is in
the experiment results. No original budget was raised. The new compressor
paths still require physical timing validation. RAM code + undo uses
24484/24576 bytes, leaving only 92 bytes in that region.

The kit-plus-five-synth host benchmark increased from 1435 to 1518 instructions
per sample for DRUM and 1486 to 1576 for DRUM-X when lane compressors were
enabled (approximately 6%). The workload retriggers all lanes but is not a
worst-case all-lanes-continuously-active measurement. Results including kit
compression and TAPE/filter/LFO are retained in the experiment log. These are
host instruction counts, not FM-1 cycles or proof of real-time headroom.

Sanitizers cover DSP, storage and UI tests. The uploaded-sample host fixture
uses the existing XIP offset mapping beyond the built-in sample array; its
static array-bounds instrumentation is disabled while ASan still checks actual
memory accesses. Existing DSP signed-shift/overflow idioms use the same UBSan
exclusions as the original playback-FX tests.

No physical test or automatic flash has been performed for this prototype.
First test one kick, then a whole kit, then a dense song with track compression,
TAPE and USB recording. The earlier user-tested compressor/TAPE firmware is
preserved as the rollback build.
