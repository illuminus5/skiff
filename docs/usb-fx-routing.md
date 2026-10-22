# Simple USB FX routing

Package: `build/skiff-1.4-fxonly.fwsc`. Six custom mono USB channels at
44.1 kHz / 16-bit. No automatic flashing.

Hold HOME, select SYSTEM, then USB CH 1..6. Each slot offers the existing OFF,
MASTER L/R/MONO and T1..T6 L/R/MONO sources, plus just three effects sources:

- **REV**: shared reverb return only (mono).
- **CHOR**: shared chorus/flanger/phaser return only (mono).
- **DELAY**: shared delay return only (mono).

Set the effects and track send amounts using Skiff's existing controls.
These are the combined shared returns across all tracks, not independent
per-track effect instances. Track sources include their normal inserts but
exclude the shared returns. MASTER includes the finished mix with shared FX.
Isolated FX taps are before master EQ/limiting/MASTER attenuation; adjust sends
to avoid clipping. Stereo HALL is folded to mono for REV.

For example: USB1=T1 MONO, USB2=T2 MONO, USB3=T3 MONO, USB4=REV, USB5=CHOR,
USB6=DELAY. The default remains T1..T6 MONO. Mapping FX replaces a chosen slot;
it does not add channels beyond six.

There are no CLICK/CMP USB sources, FX MODE, FX TRACK, compact parameters, or
LOCAL CLICK controls. The compact engine is removed. Skiff's original AUDIO
CLICK/count-in controls remain; the metronome plays locally and is excluded
from USB as before.

## Existing settings

PER7 storage compatibility is retained, including imports from PER1..PER6.
Existing track/master/REV/DELAY/CHOR assignments stay intact. Removed CLICK/CMP
assignments (source IDs 25..31) become OFF. Compact mode is disabled on import
and export; obsolete parameter fields remain reserved for binary compatibility.
Backup restore normalizes retired assignments to OFF and rejects reserved bits.
Project formats are unchanged. Keep a settings backup before installation;
older PER6 firmware cannot read PER7 settings.

MENU IDs 20..25 still describe USB CH 1..6. Compact/click IDs 26..32 are retired.
Source IDs remain stable: 22 REV, 23 DELAY, 24 CHOR. Discover counts dynamically.

Build: `FELUCCA_USB_6CH=1 FELUCCA_CDC_DEFAULT=0 ./build.sh --release 1.4-fxonly`.
Keep USB SERIAL OFF. Building does not flash firmware.

## Verification and hardware checks

Focused tests check routing order, duplicate sources, FX selection, retired
source normalization, settings/backup/editor, original audio/metronome behavior,
UI and shared FX's golden hash (`c5c723946bc887c4`). Physical testing of this
specific package is still required. First record the unchanged default map,
then assign REV/CHOR/DELAY and solo each recorded channel to verify isolated
returns. Summing dry tracks, MASTER and returns together can double-count audio.
Confirm local playback stays clean, mappings persist, and the TX-6 accepts the
same six-channel format.

Build verification: image 565,980 bytes; data/bss 90,420 / 98,304; pool
346,316 / 356,352; RAM code 24,080 / 24,576. Focused routing, settings, backup,
audio, metronome, editor, UI and stereo USB regressions pass. Five mixed-engine
scenes match across CPU splits; UI reports zero lint/alignment findings.
Package SHA-256:
`a43d949aacd9763f9d71c2b1d7f05e6ec3d23c641fa8643d506e90d4b46edaa5`.
