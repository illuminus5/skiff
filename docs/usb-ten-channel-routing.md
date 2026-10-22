# Ten-channel USB trial and working eight-channel profile

**Hardware report, 2026-10-21:** The user reports that eight channels worked
and ten channels did not when connected directly by USB to the TX-6. Use `build/skiff-1.4-usb8.fwsc` as the working profile.
The failing ten-channel package remains an experiment; software tests and the
920-byte packet calculation did not establish physical operation. The ten-channel failure symptom is not yet recorded.
This result does not establish the exact controller limit or failure cause.

Both profiles send independently mapped mono channels at 44.1 kHz / 16-bit,
without resampling. They are capture outputs from Skiff, not USB audio returns.
The ten-channel packet ceiling is 920 bytes; eight-channel is 736 bytes.
Both fit the full-speed 1,023-byte isochronous packet limit. Descriptor and
software tests do not establish physical controller or TX-6 compatibility.

## Install and roll back manually

Back up settings using the editor before installing. Keep USB SERIAL OFF.
From the repository directory, install ten channels first:

```sh
.build-deps/python/bin/python tools/fm1_install.py build/skiff-1.4-usb10.fwsc
```

If recording causes distortion, channel faults, or unreliable operation, stop
recording and install the eight-channel fallback:

```sh
.build-deps/python/bin/python tools/fm1_install.py build/skiff-1.4-usb8.fwsc
```

These commands write firmware when you run them. Building and testing have
not flashed the device. The preserved six-channel packages are unchanged.

## Defaults and custom routing

Hold HOME, select SYSTEM, then USB CH 1..10 (or 1..8).

| USB channel | Default source |
|---|---|
| 1..6 | Track 1..6 MONO, respectively |
| 7 | REV |
| 8 | CHOR |
| 9 | DELAY (ten-channel only) |
| 10 | OFF (ten-channel only) |

Every visible slot can select OFF, MASTER L/R/MONO, any track L/R/MONO,
or REV, CHOR, DELAY. Select a track's L and R on two slots for stereo;
this uses two of the available channels. There are still six synth tracks.

Track taps include DIST, insert FX, slicer, ducking, level/pan/mute, but exclude
shared effects returns. DIST is a per-track insert, not a fourth shared return.
REV/CHOR/DELAY are mono combined wet returns across tracks, tapped before master
EQ/limiting/MASTER attenuation. Shared CHOR can run chorus, flanger or phaser.
MASTER includes the final mix and shared FX. Avoid summing MASTER plus the
same tracks/returns together unless deliberately doubling those signals.
The metronome stays local; CLICK/CMP sources and compact FX remain removed.

## Persistent settings and editor

PER8 appends four five-bit source assignments to PER7. Six-, eight- and
ten-channel source builds now share that layout; eight-channel retains the
hidden assignments for channels 9 and 10. Imports from PER1..PER7 initialize
new slots to the defaults above, while keeping existing valid assignments.
Legacy compact metadata is retained but its processing stays disabled.
Retired sources 25..31 normalize to OFF. Backup restore checks reserved bits
before writing. Project formats are unchanged.

Earlier packaged PER6/PER7 firmware cannot read PER8 settings. Preserve an
older settings backup if returning to those six-channel packages.

Editor channel IDs 20..25 remain unchanged. IDs 33..36 add channels 7..10;
26..32 stay retired. Source IDs are unchanged: 22 REV, 23 DELAY, 24 CHOR.
Clients should discover the available menu count rather than assume six slots.

## Physical acceptance

1. On the Mac, run `system_profiler SPAudioDataType`. Expect Felucca with
   10 input channels (8 on fallback), at 44100 Hz.
2. In Audacity select Felucca and explicitly choose all 10 recording channels.
   If it still shows the old count, close Audacity, reconnect Skiff and reopen it.
3. Record one Skiff track at a time. Solo recorded channels to verify 1..6 order.
   Enable each shared effect and check its assigned return separately.
4. Listen to Skiff's own output while recording. It must stay clean. Repeat
   recording start/stop and test a dense six-track arrangement for several minutes.
5. Change a high-numbered slot to a track L/R or duplicate source; save settings,
   power cycle, and verify the assignment. Check ten-to-eight rollback separately.
6. Then test direct USB to the TX-6 in its USB host setup. Successful Mac capture
   does not prove the TX-6 accepts this device's channel count and sample rate.

The user reports eight-channel hardware operation and failure of ten channels.
The full acceptance sequence above remains to be documented. If eight becomes
unreliable, use the previously verified six-channel package and its older settings
backup. No automatic fallback or firmware flashing is implemented.

## Build and validation

Set `FELUCCA_USB_6CH=1 FELUCCA_USB_CHANNELS=10 FELUCCA_CDC_DEFAULT=0`
and build release `1.4-usb10`; use `FELUCCA_USB_CHANNELS=8` and release
`1.4-usb8` for the fallback. Omitting CHANNELS still selects six channels.

| Profile | Image bytes | Data/bss / 98304 | Pool / 356352 | RAM code / 24576 |
|---|---:|---:|---:|---:|
| 10 | 566340 | 95412 | 346316 | 24080 |
| 8 | 566312 | 92932 | 346316 | 24080 |

Focused host tests pass for settings, backup, editor, UI, native audio,
metronome and shared FX in all three mapped profiles. Shared FX preserve their
golden hash c5c723946bc887c4. Eight/ten transport tests pass with address and
undefined-behavior sanitizers, including clipping, full-frame packing,
maximum corrected packets, ring wrap, underrun hold, silence and routing
snapshots. Five mixed-engine scenes match across CPU splits in the ten-channel
build. These checks do not measure device ISR timing, USB scheduling or host
compatibility.

SHA-256:

- Ten: `a1db7ad7da8027e6a6e79d185a6650ab9f14f489a5d873f88630071352224cf6`
- Eight: `26b8303b075eb702ae4470b0bd9957cb03e8e35b7358b37638e4458e4084f35f`
