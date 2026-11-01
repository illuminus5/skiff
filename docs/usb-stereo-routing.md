> Historical 1.x design notes. For the v2.0.1 build, settings migration and editor IDs,
> use [USB ten-channel routing](usb-ten-channel-routing.md).

# Reduced USB routing (FM-1)

This implements independent source selection for the existing two USB capture
channels, at the existing 16-bit 44.1/48 kHz rates. It does not implement
12-channel USB, host-to-device audio, or new USB descriptors. The earlier
12-channel feasibility assessment remains in usb-multichannel-feasibility.md.

## Operation

Hold HOME, select SYSTEM, then USB LEFT or USB RIGHT. Each slot offers OFF,
MASTER L / R / MONO, then T1 through T6 L / R / MONO. Selecting a source affects
only that slot; duplicate sources are allowed. Defaults are MASTER L and MASTER R.
To record two separate mono tracks, select T1 MONO and T2 MONO. To record one
stereo track, select its L and R. Six tracks require successive recording passes.

Track taps follow the track's existing gain, pan, mute/solo and dry processing.
Shared reverb/delay returns are included in master sources, not individual track
taps. Mono is (left + right)/2 using a wide accumulator. The local mix, master
capture position, metronome exclusion, USB conversion and rate handling retain
the existing behavior. Track taps can be quieter than the master.

Routes are snapshotted once per audio block. Each selected track owns its capture
buffer; the main core clears before dispatch and consumes after the worker joins.
Persistent settings use PER5, migrate PER1–PER4 to master stereo and preserve
legacy preferences. Project formats are unchanged. Earlier firmware cannot read
PER5 settings; retain a pre-upgrade backup if downgrade is needed.

Editor MENU IDs 20 and 21 expose the two routes; old IDs remain unchanged. Source
IDs are 0 OFF, 1/2/3 master L/R/mono, then 4/5/6 T1 L/R/mono through 19/20/21 T6.
FELUCCA_USB_ROUTING=0 omits routing controls and taps. FELUCCA_USB_TRACE is separate
and disabled for this release. Lossless glyph metadata compaction and Huffman
storage of the small font allow the feature to fit existing flash.

## Validation and physical acceptance

The target build produces build/skiff-1.4-routing.fwsc. Building does not install
it. No firmware has been flashed automatically.

Host checks cover independent slots, mono limits, duplicate sources, silence,
block snapshots, legacy preference migration, editor, backup, audio, persistence,
metronome, text rendering and one/two-core consistency. These checks cannot prove
USB operation on the physical FM-1. The existing macOS capture-enumeration problem
is not resolved by routing: if Felucca is absent from Audio MIDI Setup, source
routing cannot make it appear.

After explicitly choosing to install and retaining recovery firmware/settings:

1. Close applications that claim exclusive USB access. Reconnect directly and
   check Audio MIDI Setup for Felucca stereo input at 44.1 and 48 kHz.
2. Record default master stereo and compare against local playback. Verify all
   engines, transport, MIDI, editor, metronome and normal local output.
3. Play different notes on T1 and T2. Select T1 MONO / T2 MONO and confirm isolated
   host channels, then swap them. Check OFF, duplicate sources and track L/R.
4. Test mute, solo, pan, drive and shared effects; confirm track/master tap behavior.
5. Change routes while playing, stress all six tracks on both CPUs and listen for
   clicks/dropouts. Measure DSP headroom; compilation supplies no timing proof.
6. Save while stopped, power cycle and confirm routes and previous preferences.
   Test backup/restore and both rates, reconnect and suspend/resume.

A passing hardware test requires actual recordings with channel isolation and
stable streaming. Enumeration alone is insufficient.

## Build and host results (2026-10-21)

- Firmware image 581,020 bytes; RAM data/bss 86,580 / 98,304 bytes;
  pool 346,316 / 356,352; RAM code 24,084 / 24,576.
- Package SHA-256:
  `6b98755f7b7b8d42d44c3ab4856eb225e8a71d63bd8d1d1b6ddf8f961144444b`.
- Focused routing, settings, editor, backup, audio, persistence, click, UI and
  text tests pass. Routing unit test passes AddressSanitizer/UndefinedBehaviorSanitizer.
- UAC host tests pass descriptor, ring/packet, 44.1-to-48 conversion and midstream
  rate switching. Routed dual-core check: 0 / 38 mixed scenes differ.
- Layout: 219 screens x 6 palettes plus value sweep, zero lint findings;
  2,153,658 alignment measurements, zero errors >= 1 pixel. All four generated
  font faces compare identically to pre-compaction glyphs, advances, kerning and
  bitmap bytes; text reference check has zero differing pixels or end pens.
- These are focused checks, not a claim that the entire test runner passed:
  an earlier optional WASM check could not run because felucca.wasm was absent.
  Physical USB enumeration, streaming and DSP timing remain unverified.

## Experimental macOS compatibility package

`build/skiff-1.4-macroute.fwsc` combines the two-channel routing with the
2026-10-19 contributor workaround in Felucca issue #67. Build with
`FELUCCA_UAC_MAC_COMPAT=1 FELUCCA_CDC_DEFAULT=0 ./build.sh --release 1.4-macroute`.

This profile supports **44.1 kHz only**. It puts AudioStreaming directly after
AudioControl, excludes MIDI from the AudioControl collection, updates the
control-request streaming-interface index, and gives the descriptors a different
bcdDevice revision. MIDI endpoint addresses are unchanged. Keep USB SERIAL OFF;
the saved setting can override the compile-time boot default.

The profile is an experiment for macOS 15.1.1, based on a contributor's successful
macOS 14.8.3 recording. It has not been physically verified here. After a manual
install, close the installer, reconnect, and check Audio MIDI Setup for two input
channels at 44.1 kHz. Then perform the routing/isolation tests above. Do not
expect 48 kHz in this package. The previous dual-rate package remains available.
