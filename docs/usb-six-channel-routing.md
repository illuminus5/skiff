> Historical 1.x design notes. For the v2.0.1 build, settings migration and editor IDs,
> use [USB ten-channel routing](usb-ten-channel-routing.md).

# Six-channel USB routing experiment

Package: `build/skiff-1.4-usb6.fwsc`. Six mono channels at **44.1 kHz, 16-bit**.
No sample-rate conversion or downsampling filter. Synthesis and local output
keep their existing rate. No audio return from the host.

Hold HOME, select SYSTEM, then USB CH 1 through USB CH 6. Each channel can select
OFF, MASTER L/R/MONO or T1..T6 L/R/MONO. Defaults are T1 MONO, T2 MONO, T3 MONO,
T4 MONO, T5 MONO, T6 MONO. A mono choice averages L/R safely; choosing L or R
copies that side. Sources can be duplicated. Each edit changes only that channel.
For example, map CH1=T1 MONO, CH2=T3 MONO, CH3=T2 MONO, CH4=OFF, CH5=MASTER L,
CH6=MASTER R. Track sources retain dry track processing, level, pan, mute/solo;
shared effects are present in MASTER sources. The local master isn't replaced.

Keep USB SERIAL OFF. The profile uses the previously successful contiguous
AudioControl/AudioStreaming layout with MIDI outside the AC collection. Its USB
revision differs from both stereo and 12-channel packages. The maximum packet
is 46 frames x 6 channels x 2 bytes = 552 bytes. Full-speed bandwidth fits, but
actual controller service timing/FIFO and direct TX-6 acceptance need testing.
The 12-channel build distorted local playback when capture started; CPU overload
was suspected, not measured/proven. Removing its filters does not itself prove
this package will stream cleanly.

## Settings and recovery

Back up music/settings before installing. This package uses PER6 settings to
retain the old stereo routes plus a separate six-channel map. PER1..PER5 imports
retain previous preferences and initialize the six routes to T1..T6 mono. The
six sources are stored/snapshotted as one aligned 32-bit word. Existing deferred
save behavior applies: stop playback and close MENU to allow settings to save.
Backup restore accepts PER4/PER5 and PER6, rejecting invalid maps before writing.
Projects have no format change. MENU IDs 20/21 are channels 1/2; new IDs 22..25
are channels 3..6. Stereo builds retain their original controls and PER5 format.

**Earlier firmware packages cannot import PER6 settings.** If returning to the
working `skiff-1.4-macroute.fwsc`, restore your pre-upgrade settings backup; do not
assume existing calibration/preferences will migrate backward. Keep that package
available. No firmware is flashed by building or testing.

## Build

`FELUCCA_USB_6CH=1 FELUCCA_CDC_DEFAULT=0 ./build.sh --release 1.4-usb6`

Default builds keep
the existing stereo transport. The compile-time serial default does not override
saved USB SERIAL settings.

## Verification

Target build: image 565,032 bytes; RAM data/bss 89,844 / 98,304; pool
346,316 / 356,352; RAM code 24,080 / 24,576 bytes.

Host tests: six-channel descriptors (including maximum packet, rate and interface
mapping), default/custom sources, duplicates/OFF, per-block snapshots, clipping,
packed channel order, priming and 44,100-frame packet cadence pass ASan/UBSan.
Settings migration/export, backup validation and editor tests pass. UI tests pass;
219 screens x six palettes plus value sweep have zero lint findings; 2,159,478
alignment checks have no error >=1 px. All 38 mixed-engine dual-core scenes match,
including captured track data. Stereo UAC and settings regressions pass. These
are focused tests; no claim of a complete rerun of every repository test.

Physical acceptance:

1. Install manually; close installer/editor, reconnect. Mac System Report should
   list Felucca with six inputs at 44100 Hz. This is enumeration only.
2. Record six inputs in Audacity with playback monitoring disabled. Play one
   track at a time, then all tracks. Confirm mapping, silence and local output.
3. Change CH1 to T6 MONO, CH6 to T1 MONO; confirm swap. Try OFF and duplicate
   sources, then save stopped, close menu, power cycle and confirm mapping.
4. Stress six tracks and voices during capture. If local sound distorts, stop;
   record CPU mode/load and whether even a single active track causes it.
5. Connect directly to TX-6. Confirm USB host/audio recognition, incoming-channel
   mapping and sound. The TX-6's six stereo/twelve mono mixer capacity does not
   prove acceptance of generic six-channel USB capture. Its firmware version,
   rate support and host mapping remain hardware test questions.

Additional checks: audio USB/DAC level behavior, metronome exclusion, deferred
persistence and ThreadSanitizer dual-core checks pass. Package SHA-256:
`ce074cf2d48a58f965e3de59c9ff27dd8d4f799726029c6fd9ca2940e5b70254`.
