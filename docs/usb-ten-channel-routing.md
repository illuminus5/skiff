# USB routing: up to ten channels at 44.1 kHz

Mapped profiles provide 6, 8 or 10 independently assignable mono capture channels
at 44.1 kHz/16-bit without resampling. Ten is the maximum selectable profile.
The normal stereo firmware remains the default build. This is device-to-host
capture; no host-to-device audio return is implemented.

## Mapping

Hold HOME > SYSTEM, then select USB CH 1..N. Every slot offers OFF, MASTER
L/R/MONO, T1..T6 L/R/MONO, REV, CHOR and DELAY. Assigning a track's L and R to
two slots uses two output channels. Duplicate sources are allowed.

| Channel | Default |
|---|---|
| 1..6 | Track 1..6 MONO |
| 7 | REV |
| 8 | CHOR |
| 9 | DELAY |
| 10 | OFF |

Fewer-channel builds expose only their available slots. Track outputs include
DIST, insert effects, slicer, ducking, level/pan and mute, but exclude shared
returns. DIST is per-track processing; it is not a fourth shared return.
REV/CHOR/DELAY are combined mono wet returns across tracks, before master EQ,
limiting and MASTER attenuation. CHOR can run chorus, flanger or phaser.
MASTER includes the final mix and shared FX. Summing MASTER together with its
tracks/returns duplicates audio. The metronome stays local.

## USB timing

Ten-channel packets carry 880 or 900 bytes normally and at most 920 bytes with
clock correction, within the single full-speed endpoint's 1023-byte limit.
Their payload alone takes roughly 587–613 microseconds at 12 Mbit/s, leaving
little time to refill the endpoint before the next host request. Ten-channel
capture therefore services EP4 on each existing 100-us TIMER5 tick. USB control
and MIDI polling stay at 2 kHz; stereo/six/eight audio servicing is unchanged.
The no-consumption timeout stays at 20 ms by scaling its poll count.

The contributor reports clear audio with this faster-service build. Successful
enumeration and software tests alone do not establish reliable hardware audio.
Extended stability, host compatibility and timing measurements remain useful.
More frequent SIE accesses may increase interrupt load under dense arrangements.

## Settings and editor

Skiff 2.0.1 keeps all route assignments in the existing SKS1 settings record.
A USB1 tag and three words occupy reserved room[0..15]; room[16..31] is
preserved. The record size/magic and project/sample/user-preset formats are
unchanged. Stock 2.0.1 preserves these opaque bytes when saving preferences.
PER5..PER8 USB settings from the 1.x builds migrate on import and backup restore.
Retired click/CMP sources become OFF. Eight-channel builds retain hidden 9/10
assignments. RESET SETTINGS resets the routes with the other preferences.

Editor IDs 40..49 select channels 1..10. The upstream RETURN control keeps id 20;
1.x USB editor IDs 20..25 and 33..36 must not be hard-coded by a client talking
to this port. Discover rows through MENU_DESC. Source IDs remain stable: 22 REV,
23 DELAY, 24 CHOR. No TAPE source or TAPE engine is included in this PR.

The v2.0 panned-drum-lane mixer path also feeds the track taps, preserving lane
panning. Shared return and master semantics remain as described above. The
master output arithmetic is factored into a common non-inlined epilogue to keep
the compiler's IRQ branch graph from combining adjacent loops in the static
cost estimator. No target or CPU budgets were changed.

## Build, install and test

For ten channels, use:

```sh
FELUCCA_USB_6CH=1 FELUCCA_USB_CHANNELS=10 FELUCCA_CDC=0 ./build.sh --release 2.0.1-usb10fast
```

Use CHANNELS=8 for an eight-channel fallback; omitting CHANNELS selects six.
Release builds omit the CDC serial console. In development builds that include
CDC, turn USB SERIAL off for mapped audio. Builds do not flash firmware. Manual installation is:

```sh
python3 tools/fm1_install.py build/skiff-2.0.1-usb10fast.fwsc
```

Keep a known-working fallback package and a settings backup. On macOS, check
`system_profiler SPAudioDataType` for ten inputs at 44100 Hz. In Audacity select
Felucca, all ten recording channels and 44100 Hz. First record one clean track
with CH1=T1 MONO and other slots OFF; listen to its isolated recording. Then
verify all tracks and shared returns, repeated start/stop, dense arrangements,
local headphones, mapping persistence and reconnect behavior. Test direct USB
mixers separately; clean computer capture does not prove every host accepts it.

## Validation

Tests cover actual TIMER5 scheduling with hardware mocks, nested rendering,
control/MIDI cadence and the unchanged timeout; channel/packet order, clipping,
ring wrap, underruns and snapshots; settings/backup/editor migration; audio,
metronome and shared FX regressions. ASan/UBSan exercises eight/ten transport.
The shared FX golden sound and per-core track output must stay unchanged.

Twelve-channel capture and its low-rate prototype are outside this PR. At
44.1 kHz/16-bit, twelve channels exceed the current single endpoint's packet
limit; the discarded prototype is not a selectable firmware profile.

## 2.0.1 port validation

The official v2.0.1 FM6 fix/core and test are unchanged. Builds and the original
pi32v2 budgets pass. Native audio, metronome, SKS1/PER8 settings, project, backup,
editor, shared returns, descriptors/packets, scheduling and full UI tests pass.
The worker produces identical output in all 38 tested scenes and splits.
A dedicated ASan test verifies isolated L/R for a fully panned drum lane.
176 golden audio renders remain unchanged; zero health, voice/routing failures
or crashes. These are software checks, not physical FM-1/TX-6 validation.

The host CPU regression exits nonzero for ANALOG STRINGS and idle. Both fail
in untouched v2.0.1 too: stock 1010/372 host instructions per sample, this port
1006/378, against stored budgets 756/286 (+25% allowance). No baselines were
updated. The complete aggregate release/UI-golden/WASM suite has not been rerun
for this port; the focused suites and full functional UI/audio checks above
were run. Physical audio stability, timing and CPU headroom remain manual tests.

Before moving from 1.x, follow the official v2 upgrade/conversion instructions.
A v2 project save may be unreadable after rolling back to 1.4. Keep the original
backup if rollback matters; this PR does not run migration or flash the unit.
See https://github.com/illuminus5/skiff/releases/tag/v2.0.1 and
https://github.com/illuminus5/skiff/releases/tag/v2.0 .
