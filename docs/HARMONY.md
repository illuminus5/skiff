<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Optional HARMONY sample choir

HARMONY is an opt-in FM-1 engine on track 1, based on Skiff 2.1.1. The normal build remains unchanged. Up to sixteen virtual singers use at most five allocated section voices and one shared sample decoder. This is recorded-vocal playback, not microphone input. The separate VOICE synth remains available. No recordings are bundled or installed.

## Build and test

With the dependencies and environment variables in BUILDING.md, run from the repository root:

```sh
python3 tools/build_harmony.py
python3 tests/harmony/run.py
tests/run_tests.sh
```

The wrapper enables FELUCCA_HARMONY=1 and disables diagnostic DSP counters. Its default candidate version is 2.1.1-harmony1, for FM-1 only. It never invokes an updater. Tests generate a deterministic ADPCM tone fixture; no private audio, MIDI device or NumPy is needed. Sanitizer runs retain the existing fixed-point shift/signed-wrap/XIP exclusions; they do not claim strict coverage for those conventions.

## Source and sections

Choose HARMONY and a populated USR bank/PHRASE. ROOT is the recording's reference MIDI note (60=C4). SOURCE excludes fully single-key-mapped banks, including mapped drum kits. This is a mapping rule: it cannot recognize a drum WAV uploaded as a broad-range sample, and single-key vocal banks are excluded too.

SECTION keeps K1 group, K2 count, K3 level, K4 pan. LEAD allows zero or one singer; BASS, TENOR, ALTO and SOPRANO share the remaining sixteen-singer total. These are virtual singers, not sixteen allocated Skiff voices. WIDTH spreads singers around each section pan; HUMAN adds timing/pitch motion. The six-track and global 8/12 single/dual-core budgets are unchanged.

## Harmony and correction

HARMONIZE has VOICING, CHORD and TUNE:

- FOLLOW preserves the played-note-relative stack.
- CHORD holds a manually chosen diatonic triad I..VII in the supporting sections' registers while the lead moves. KEY chooses major or natural minor. Nearby tones/common notes are retained where possible. This is initial section-aware voicing, not classical SATB optimization or a guarantee of four distinct pitches.
- TUNE OFF preserves the source contour. NATURAL applies partial smoothed correction; HARD applies faster full correction. Correction follows the detected source key before keyboard transposition, so transposing can still move the result outside that key. Pitch correction remains experimental, with the detector's range/artifact limitations and the existing +/-24-semitone render bound. This is not an Auto-Tune-branded implementation.

Upstream SCL FOLLOW still maps input notes before HARMONY receives them. It is separate from HARMONIZE's FOLLOW/CHORD setting; the section triad is not automatically derived from SCL FOLLOW's live chord in this version.

## Playback, trim and tone

RETRIG restarts the phrase for each key. LEGATO preserves the live timeline while changing pitch. Holding a key before/in the marked region sustains that region. Release allows the forward ending; re-pressing before the boundary re-arms sustain. Holding after the loop region can restart the whole phrase at SAMPLE END. Raised-cosine overlaps soften joins; overlap shortens the effective repeat period, so this is not tempo sync.

TRIM uses K1 sample start, K2 loop start, K3 loop end, K4 sample end. Hold HOME for coarse adjustment. The waveform prepares on selection and shows a source playhead. FLT and sample EQ are separate pages. Trim/EQ belong to the source phrase, not an embedded copy in each preset. Checked records append inside existing free sample-tail space, preserving audio/header bytes and old slice/mix offsets. Samples occupying that sector cannot save those records; storage is not expanded.

Presets: SOUL CHOIR is the sixteen-singer reference; INTIMATE a smaller/narrower arrangement; WIDE GOSPEL a full major-key chord choir; WARM TRIAD a softer minor-key chord choir; TUNED LEAD a single lead with NATURAL correction. They assume a C4 recording in USR2: choose source/root for your own sample.

## Compatibility and validation scope

Public engine IDs 0..14 are unchanged. HARMONY retains experimental ID 16. ID 15 is reserved and silent for an unsupported former VOCAL experiment; it has no selectable engine or presets. Former VOCAL USER data is retained but cannot be played or selected. Existing parameter/SID mappings, including Skiff 2.1.1's new follow fields, are retained. Full custom firmware projects can contain separate TAPE/compressor records; those formats are outside this HARMONY-only PR's compatibility claim.

The earlier 2.0.2-based hardware build was auditioned on an FM-1; the user reported a 73% peak with full choir, CHORD and NATURAL, and confirmed save/restart restoration. These are user-reported results for that earlier build. The 2.1.1 port has separate host/target validation and still needs a fresh hardware listening/CPU check. Digital silence does not establish a fix for system-wide analog headphone hiss.

The test suite also fixes an upstream test-only menu row coverage mask: more than 32 menu rows require a 64-bit mask. No menu behavior is changed by that correction.
