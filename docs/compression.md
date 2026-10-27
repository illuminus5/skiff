# Track and drum compression

This change groups the shared fixed-point compressor, per-track INSERT COMP,
and per-lane DRUM COMP into one feature review. It is based on the TAPE branch
(PR #67), itself based on USB routing (#37). Those branches are unchanged.

## Controls and signal flow

- Any track: FX > INSERT > TYPE COMP, then THRESH, RATIO, RELEASE; INSERT 2 > MIX
  blends the effect. It occupies the existing insert slot. Gain is linked across
  stereo components to preserve spread/panned drums. No new parameter IDs.
- DRUM/DRUM-X and uploaded sample kits: EDIT > DRUM COMP, page 5/5. PRESETS or a
  key selects a drum. Four headers: THRESH dB / RATIO / REL ms / MIX. The eight
  faders reuse EDIT > LANES' widgets and show each lane's MIX. MIX defaults off.
- Each drum lane is processed before its square-law level and pan, then mixed
  into the kit. Kit/track compression can be used in addition. Existing DUCK,
  shared effects, TAPE sends and USB routing remain available.
- Fixed 3 ms attack, 20–1000 ms release, threshold -48..0 dB, eight ratios from
  1:1 to 12:1, no automatic makeup gain. No external audio sidechain or drum
  compressor parameter locks/web-editor controls in this revision.

See [track compression](compressor.md) and [drum compression](drum-comp.md) for
implementation details, controls and limitations.

## Saved data

Track compression uses existing INSERT parameters in projects/user presets.
Per-drum settings use 192 encoded bytes for all six tracks. Default lane state
still saves as SKF4; changed lane controls use SKF5 (2816 bytes), wrapping the
unchanged SKF4 payload with validation and a checksum. Existing SKF3/SKF4
projects load with lane compression off. **Older firmware cannot read SKF5.**

The existing 192-byte USER record is extended in its unused tail, guarded
against future SID growth. Slot counts, flash object IDs, sample/preset sectors
and patterns are unchanged. Full backups preserve the extension. Older firmware
loads basic preset sounds without the lane settings and drops them on resave.
No existing project is rewritten merely by installing the firmware.

## Verification

Run `sh tests/run_drum_comp_tests.sh` after generating headers. This covers the
shared transfer curve and attack/release, original INSERT behavior, lane/track
isolation, DRUM/DRUM-X/uploaded kits, panning and lane level law, visible UI and
navigation, sound undo/redo, existing TAPE behavior, saves/presets/backups/
autosave, malformed records and all twelve SKF5 interrupted-write positions.
Mixed-engine scenes with lane and kit compressors match all tested core splits.
Sanitizer scope/exclusions are documented in drum-comp.md and the runner.

Broader native UI, drum/FM6/audio, project/backup/editor/settings and USB checks
pass. All 176 golden audio renders remain unchanged, with no health/routing
failures or crashes. Three host CPU baseline checks remain flagged: STRINGS
+34%, WIRE around +25%, idle +33%. These checks are not passing; no baseline
was raised or rewritten. The host benchmark is not a target timing guarantee.

The final combined build passes original target and memory checks: image
567332 B, data+bss 94388/98304 B, pool 348152/356352 B, RAM code+undo
24484/24576 B (92 B remaining there). New lane/voice rendering and mono mixing
have explicit out-of-line function boundaries; the unchanged original budgets
are supplemented by reports of the new paths rather than raised limits.

The user confirmed per-track compression works on FM-1 and approved the drum
page appearance. Per-drum physical audio stability and worst-case timing with
TAPE/USB10 remain unverified. No automatic flashing was performed.

## Reproduce the combined profile

With the repository's toolchain and SDK configured:

```sh
FELUCCA_TAPE=1 FELUCCA_USB_6CH=1 FELUCCA_USB_CHANNELS=10 FELUCCA_CDC=0 \
  ./build.sh --release 2.0.1-compression
python3 tests/target_budget.py build/felucca.dis tests/target_budget.txt
sh tests/run_drum_comp_tests.sh
```

Recorded new-path static counts (instructions / instructions within loops):
`dc_process` 190/47, `dc_render` 43/8, `dc_voice_add` 24/4,
`track_render` 549/337, `mix_mono` 164/54, `ins_compress` 162/88.
These are reports, not new accepted time budgets. The analyzer does not add
callee costs to callers; the host load tests and physical validation remain
necessary.
