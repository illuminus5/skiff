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

PER8 stores all ten route assignments even in eight-channel builds, allowing
switching between these profiles without losing the hidden two assignments.
PER1..PER7 imports preserve existing valid routes and initialize new slots to
the defaults above. Old compact metadata is retained for binary compatibility,
but its processing is disabled. Retired click/CMP source IDs 25..31 become OFF.
Projects retain their existing format. Back up settings before installing;
older packaged PER6/PER7 firmware cannot read PER8 settings.

Editor IDs 20..25 retain channels 1..6. IDs 33..36 add channels 7..10; IDs 26..32
remain retired. Source IDs stay stable: 22 REV, 23 DELAY, 24 CHOR. Discover the
available MENU count dynamically.

## Build, install and test

For ten channels, use:

```sh
FELUCCA_USB_6CH=1 FELUCCA_USB_CHANNELS=10 FELUCCA_CDC_DEFAULT=0 ./build.sh --release 1.4-usb10final
```

Use CHANNELS=8 for an eight-channel fallback; omitting CHANNELS selects six.
USB SERIAL must be OFF on the device; saved preferences take precedence over
the build default. Builds do not flash firmware. Manual installation is:

```sh
python3 tools/fm1_install.py build/skiff-1.4-usb10final.fwsc
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
