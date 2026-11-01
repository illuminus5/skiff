# TAPE project persistence (Skiff v2.0.1)

Candidate: `build/skiff-2.0.1-usb10tapesave.fwsc`. Never flashed automatically.
Retains USB10fast, all six instrument tracks and the TAPE filter/LFO.

Normal project SAVE and quiet-stop autosave now retain:
- all 64 TAPE event words and lane length;
- each track's TAPE SEND;
- pitch, speed, playback length and mix;
- filter HP/LP/resonance/mix and LFO rate/shape/depth/destination.

Reload restores controls and events but leaves TAPE and its sequencer disarmed.
Recorded audio, selected history position, cursor, held keys and LFO phase are
transient. Capture fresh audio before enabling the restored TAPE sequence.
NEW and older projects reset TAPE to defaults. Existing full project backups
include the new metadata. Stock v2.0.1 reads the instrument data, but discards
TAPE metadata if it saves the project again.

## Format and storage

TP1 packs 266 bytes into SKF4's existing reserved tail, immediately before the
project name. Its version tag and values are validated before restore; the
existing whole-project checksum protects it. Default TAPE state writes zeroes,
preserving the stock default format. Each 31-bit event is stored losslessly,
including speed values that are not multiples of five. A compile-time bound
rejects future parameter growth that would overlap TP1. Flash addresses,
transaction format and object sizes are unchanged. No additional flash region
or sample/preset storage is claimed.

Audio remains the existing approximately 2.09-second RAM loop. Source inspection
finds no safe unallocated 48 KiB flash region while preserving samples, presets,
projects and the existing wear/torn-write rotation sectors. SKF4's tail cannot
hold the loop. This build therefore does not save recorded audio or lengthen it.
This is a source/layout finding, not a new physical flash-capacity measurement.

## Validation

Run `sh experiments/tape-store/test.sh` from the repository root. Checks include
10,000 randomized lossless roundtrips, 20,000 malformed payloads, bounds guards,
actual SAVE/load/reboot/backup/autosave against simulated NOR, malformed backup
rejection before erase, sample/preset-region preservation, all 11 interrupted
payload/header write points, and visible control synchronization. Sanitizers
cover the codec and simulated project storage.

The original target budgets and memory checks pass: image 562884 B,
data+bss 94196/98304 B, pool 346392/356352 B, RAM code 24228/24576 B.
Full UI checks pass; 176 golden renders remain unchanged with no health/routing
failures or crashes. The host CPU regression reports three budget failures: ANALOG STRINGS +34%,
PHASE WIRE +26%, and idle +32%. STRINGS and idle also failed in the earlier
stock comparison; WIRE is a further marginal failure in this candidate. These
host instruction counts do not measure FM-1 cycles. Baselines were not changed.

The user reported the previous filter/LFO build sounds stable on the FM-1.
The user also confirmed that saving and restoring TAPE settings works on the FM-1. Build success and
simulated flash tests do not establish target timing, storage or USB reliability.

## Manual acceptance

1. Install the candidate using the existing manual installer; no automatic flash.
2. Set sends, main TAPE controls, filter/LFO and several sequenced triggers.
3. Save a project, load another, then reload the saved project. Confirm settings
   and events return, with TAPE disarmed and no old audio playing.
4. Capture fresh audio, enable the lane and verify the saved triggers play.
5. Restart and reload; verify instrument presets and user samples remain intact.
6. If testing autosave, stop transport and wait for the normal quiet-save period.

Build from the prepared isolated source:
```
JIELI_TOOLCHAIN="$PWD/.build-deps/jieli-linux-toolchains-20260730.1" \
AC79_SDK="$PWD/.build-deps/ac79-wifi-sdk" \
.build-deps/python/bin/python experiments/v2-port/build.py --tape-save
```
