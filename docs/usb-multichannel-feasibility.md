# FM-1 USB audio feasibility

The implemented scope is capture-only, up to ten independently mapped mono
channels at 44.1 kHz/16-bit. The hardware uses a full-speed USB connection;
there is no USB host-to-device audio return in this PR.

## Hardware and transport boundaries

The FM-1 board uses an AC7911B8. The
[JieLi AC7911B datasheet](https://github.com/amitv87/fw-AC79_AIoT_SDK/blob/release/AC79NN_SDK_V1.2.0/doc/datasheet/AC791N%E8%A7%84%E6%A0%BC%E4%B9%A6/datasheet/AC7911B_Datasheet_V1.3_20230303.pdf)
describes full-speed USB. macOS enumeration also reports 12 Mbit/s. There is no
established high-speed path on this board. Its existing class-compliant audio
stream uses EP4 IN; MIDI remains on EP1 and optional CDC on EP2/3.

The [USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification)
limits one full-speed isochronous transaction to 1023 bytes per 1-ms frame.

| Capture format | Normal payload bytes/ms | Firmware maximum with correction |
|---|---:|---:|
| 6 x 44.1 kHz x 16-bit | 528 / 540 | 552 |
| 8 x 44.1 kHz x 16-bit | 704 / 720 | 736 |
| 10 x 44.1 kHz x 16-bit | 880 / 900 | 920 |
| 12 x 44.1 kHz x 16-bit | 1056 / 1080 | exceeds endpoint limit |

Ten is the maximum implemented profile. Twelve-channel low-rate processing
has been removed. Multiple streaming endpoints, compressed formats, reduced
bit depth and USB audio return are outside this implementation. Packet sizing
alone does not establish controller timing, FIFO behavior, CPU headroom or
host acceptance; these require tests on physical hardware.

## Implementation and timing

Track taps are captured in separate per-track buffers so each core writes its
own tracks; the main core consumes after joining. Shared REV/CHOR/DELAY returns
are tapped without duplicating their DSP. Routing snapshots are taken per
render block. Normal local playback remains on the existing mixer path.

At ten channels, the payload alone occupies roughly 587–613 us at 12 Mbit/s.
The former 500-us audio poll interval left little time to refill EP4 before
the next host transaction. Ten-channel servicing now runs on every existing
100-us TIMER5 tick, including while rendering. MIDI/control polling remains
2 kHz, other profiles retain their audio cadence, and the timeout stays 20 ms.
The contributor reports clear recording with this change. This supports the
timing hypothesis but is not an instrumented USB timing measurement.

Mapped profiles use the macOS-compatible layout discussed in
[Felucca issue 67](https://github.com/hugelton/Felucca/issues/67): 44.1 kHz only,
AudioControl then AudioStreaming then MIDIStreaming, and an AS-only control
collection. The default stereo build retains its original descriptor layout.

## Remaining acceptance

Confirm isolated channel order, repeated recording start/stop, dense six-track
arrangements, FX returns, local playback under USB load, settings persistence,
reconnect and long recordings. Test each direct USB host separately; computer
capture does not establish universal host compatibility. Keep a known-working
fallback and older settings backup. Builds do not flash devices.

See [routing and testing](usb-ten-channel-routing.md) for controls, settings
migration and commands. Software checks include target memory budgets,
descriptors, packet/ring bounds under sanitizers, audio golden regressions,
actual TIMER5 scheduling with mocked hardware, UI, settings and backups.
