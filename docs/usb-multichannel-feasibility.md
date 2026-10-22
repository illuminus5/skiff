# FM-1 USB multichannel feasibility and implementation plan

Investigation: 2026-10-21. **No-go for the original twelve-channel
44.1/48 kHz proposal on the existing single full-speed endpoint.**

The sections below preserve the initial investigation and its evidence limits.
Subsequent reduced requirements were implemented: six mapped channels, then
eight mapped channels with shared FX returns at 44.1 kHz/16-bit. The user
reports eight channels work directly into the TX-6; ten do not in that setup.
See [current eight-channel result and ten-channel experiment](usb-ten-channel-routing.md).
There is no USB host-to-device audio return. No automated flashing was used.

## Follow-up: exact chip evidence and connected-device observation

The user's macOS System Information output identifies Felucca VID 0x1209,
PID 0x0001, bcdDevice 3.29, at **12 Mb/s**. Its audio device listing contains
no Felucca device. This confirms current full-speed enumeration but alone
would not prove the silicon's limit.

Further investigation located the exact-family manufacturer datasheet:
[JieLi AC7911B Datasheet V1.3, 2023-03-03, SDK mirror](https://github.com/amitv87/fw-AC79_AIoT_SDK/blob/release/AC79NN_SDK_V1.2.0/doc/datasheet/AC791N%E8%A7%84%E6%A0%BC%E4%B9%A6/datasheet/AC7911B_Datasheet_V1.3_20230303.pdf).
The manufacturer document's printed page 1 (PDF page 2) lists **FUSB 1.1 OTG
controller**; printed page 9 (PDF page 10) identifies pins 47/48 as FUSBDM/FUSBDP.
Both pages were extracted and visually inspected. The complete relevant pinout
provides no high-speed USB pair.

[Felucca's author's firsthand board inspection](https://note.com/kurogedelix/n/n225a8e2374cd)
identifies the FM-1 SoC as AC7911B8. The published
[board hardware notes](https://github.com/aroum/fm1-custom-fw/blob/main/REVERSE_ENGINEERING.md)
also map the USB-C data connection to pins 47/48. Those notes are supporting
community evidence; the USB controller specification comes from JieLi's PDF.

**Updated conclusion:** for the documented AC7911B8 FM-1 board, there is no
documented firmware-only high-speed upgrade. The requested 12-in/2-out at
44.1/48 kHz needs a different transport/hardware design or revised requirements.
A high-speed bridge is a hardware engineering project, requiring a sufficiently
fast multichannel link from the SoC; it is not a USB cable or hub upgrade. The
user's individual board revision/chip marking has not been visually inspected.
The earlier unknowns below record the initial gate; this follow-up resolves
the chip-level high-speed question for the documented board, not FIFO details.

For the separately missing legacy audio device, `README.md:550` documents
MENU > SYSTEM > USB SERIAL OFF, applied when the menu closes, to omit CDC and
allow audio detection on macOS 13–15. MIDI/editor/installer remain available.
Try this reversible setting before considering new firmware; no success on the
user's Mac or other macOS versions has been verified. It does not increase speed.

## Findings and evidence boundaries

The existing transport is full-speed USB0, not a demonstrated high-speed path.
`firmware/hal/fm1_usb.h:3` identifies it as full-speed; lines 24–33 map its
registers at 0x11800 and its pads at 0x51000. `fm1_usb_attach()` at line 43 uses
PLL48M and a D+ pull-up. `firmware/src/usb.c:3` likewise describes full-speed
operation. There is no high-speed negotiation, speed-dependent descriptor set,
microframe packetizer or high-speed HAL in that driver. A USB 2.0 descriptor
version or a 48 MHz clock alone does not establish the controller's speed.

This establishes what Skiff uses, **not that every AC79 variant or every USB
controller in the SoC is exclusively full-speed**. The board schematic, exact
silicon/controller manual and USB connector wiring are not supplied here. The
vendor's [AC79 USB documentation](https://doc.zh-jieli.com/AC79/zh-cn/master/module_example/peripherals/usb.html)
describes USB0/USB1 selection and UAC playback/recording examples, but does not
establish high-speed availability on the FM-1 connector, endpoint FIFO sizes or
DMA count-field widths. Family-level capability must not be substituted for
board-level evidence. The SDK is absent at the documented local path, so its
controller implementation could not be cross-checked locally.

Before reopening implementation, obtain the exact board/controller evidence,
confirm its USB port mapping and negotiated bus speed on hardware, and establish
FIFO allocation and DMA restrictions. Do not write speculative high-speed or
large-packet register values to the device.

## Packet and bus budget

Direction here is device-relative: capture is IN (FM-1 to host), return is OUT.
All figures below are uncompressed 16-bit PCM at one service interval per ms.

| Stream | 44.1 kHz packets | 48 kHz packets | Average bytes/s |
| --- | --- | --- | --- |
| 12-channel capture | 1056 / 1080 B (44 / 45 frames) | 1152 B (48 frames) | 1,058,400 / 1,152,000 |
| Stereo return | 176 / 180 B | 192 B | 176,400 / 192,000 |
| Combined | 1232 / 1260 B if cadences align | 1344 B | 1,234,800 / 1,344,000 |

The [USB 2.0 specification](https://www.usb.org/document-library/usb-20-specification),
sections 5.6.3 and 5.6.4, limits a full-speed isochronous endpoint to 1023 bytes
per frame and periodic allocation to 90% of a full-speed frame. Consequently,
**one full-speed IN endpoint cannot carry 12 channels at either requested rate**.
UAC2 does not change the bus speed or this payload limit. Merely increasing
bNrChannels and wMaxPacketSize would produce an invalid full-speed configuration.

At 48 kHz the combined payload alone occupies 896 microseconds at 12 Mbit/s;
the 900 microsecond periodic budget leaves only 4 microseconds for all tokens,
data headers, CRCs, EOPs, gaps, stuffing, feedback and CDC interrupt allocation.
That cannot accommodate the required transactions. Splitting capture into
multiple endpoints does not fix this combined full-speed bandwidth blocker.

At 44.1 kHz the combined *average* raw payload occupies 823.2 microseconds, but
that is not a schedulability proof. Worst-case bit stuffing alone scales data
time by up to 7/6 (960.4 microseconds on that average), before protocol overhead;
hosts reserve endpoint maxima, not average traffic. A compliant guaranteed
12-in/2-out full-speed configuration at this rate is therefore not justified.
Even capture-only 48 kHz needs a careful split-endpoint scheduling calculation
(worst-case payload time alone is 896 microseconds, before transaction overhead).
Capture-only 44.1 kHz is a potential research alternative, not a verified feature.

These nominal sizes exclude clock drift headroom. The existing asynchronous
stereo sender reserves 49 frames at 48 kHz (196 B), or 46 frames in its 44.1-only
build (184 B): `usb.c:81–101`. An equivalent 12-channel reserve would require
1176 or 1104 bytes. Never size the new endpoint from average samples/ms alone.

## Endpoints, FIFO, DMA and service timing

`usb.c:213–218`, descriptors at 263–306 and `ep1_config()` at 475 establish:

| Address | Existing purpose | Descriptor payload/type |
| --- | --- | --- |
| 0x00 / 0x80 | Control | EP0, 64 B |
| 0x01 / 0x81 | MIDI | Bulk, 64 B |
| 0x82 | CDC notification | Interrupt, 8 B, 16 ms; currently never sent |
| 0x03 / 0x83 | CDC data | Bulk, 64 B |
| 0x84 | Stereo capture | Asynchronous isochronous, up to 196 B |

The HAL supplies EP0–3 count registers and separate EP4 count/TX/RX addresses
(`fm1_usb.h:26–32`). This is evidence of mapped registers, not proof of the
maximum endpoint count, simultaneous IN/OUT capability, FIFO capacity, legal
isochronous endpoint assignments or double buffering. SIE MAXP writes of 0xFF
in `ep1_config()` must not be interpreted as a documented 255-byte FIFO limit,
nor as evidence that a 1023-byte FIFO exists. Register bit widths and units
need the controller manual or exact SDK source.

EP2 OUT and EP4 OUT are not described as active streams. Their apparent unused
addresses do not prove they can provide return DMA or an additional large IN
FIFO. EP4 RADR is initially pointed at EP0 memory (`fm1_usb_attach():50`);
enabling OUT without a dedicated correctly sized buffer would be unsafe.
Disabling CDC frees addresses but violates the preservation requirement unless
explicitly approved, and does not create bus bandwidth.

`fm1_usb_ep4_send():101` uses csync before DMA publication; RX readers use ssync
(`fm1_usb_rx_sync():108`). No FIFO capacity, DMA alignment guarantee beyond the
current aligned buffers, transfer-length mask or sustained larger-packet test
is available. Establish all of these before enlarging transfers.

USB is polled at 2 kHz from the 10 kHz TIMER5 ISR, including service during
rendering (`main.c:38–55`); see `uac_service():1211` and `usb_poll():1238`.
The USB poll itself is serialized against re-entry. Increasing
channels multiplies data movement and resampling work in timing-sensitive code.
Host simulation cannot establish worst-case DMA refill deadlines or ISR jitter.

## Descriptor/topology options and macOS

**Preferred conditional topology if a usable high-speed path is demonstrated:**
one audio function, one 12-channel capture AS interface, one stereo playback AS
interface, each with idle alt 0 and active alt 1; retain MIDI and CDC. A UAC2
design would describe the actual common clock source and terminals and provide
the required rate/clock controls. Use a dedicated feedback IN endpoint for
asynchronous playback unless a documented, tested host-compatible implicit
feedback scheme is chosen. At high speed, 48 kHz averages 6 frames/microframe:
144 capture bytes and 24 return bytes, plus headroom. Actual endpoint addresses
and FIFO assignments remain deliberately unassigned until hardware evidence
supports them. Keep the existing full-speed stereo firmware configuration.

**Full-speed split research option:** two six-channel capture AS interfaces
(separate terminals/endpoints) under one audio control function: 528/540 bytes
each at 44.1 kHz, 576 bytes each at 48 kHz before drift allowance. This is not
two data endpoints in a single ordinary UAC1 Type I AS alternate setting. It
requires another proven isochronous IN endpoint and synchronized sample counts,
rate changes, start/stop and overflow policy across both streams. It does not
solve the full-duplex bandwidth problem above.

Apple supports multiple streaming interfaces, but descriptor grouping is not
proof that a particular topology appears as one 12-input Core Audio device.
[Apple TN3190](https://developer.apple.com/documentation/technotes/tn3190-usb-audio-device-design-considerations)
is the current host-design reference. Test actual grouping, clock association,
channel ordering and DAW access on supported macOS releases, including macOS 26;
the existing descriptor test's macOS <=15 parser model is insufficient.
No claim of automatic aggregation or six stereo streams becoming twelve
contiguous DAW inputs is made here. A user-created Aggregate Device would be an
explicit alternative, not fulfillment of the native-device requirement.

## Engine changes required after transport approval

Skiff is one translation unit (`firmware/src/felucca.c:4`); integration order
matters. `audio.c:34–41` takes the current stereo master before the metronome
and before optional local master attenuation for USB LEVEL FIXED. Preserve this.

`fx.c:1046` renders each track into a reusable mono scratch block, applies drive,
insert, slicer, performance mute and ducking, then level/pan and shared sends.
The dry L/R contributions are separable *at their addition to the mix*, not by
copying the shared scratch buffer after all tracks. The SPREAD branch at
`mix_spread():959` constructs stereo from sum/difference and must have its own
tap. Shared chorus/delay/reverb (`mix_block():1187`, `fx_buses()`) cannot be
described as isolated per-track returns. User sample extra sends also enter
shared sums through `smx_render():1016`. Defer FX bus and PRE/POST selectors.

Proposed tap semantics: post track processing, post level/pan and track mute/
ducking, pre shared bus/master processing. Capture the exact L/R dry contribution
in both normal and SPREAD loops. Confirm sequencer mute/solo semantics against
voice/event handling with tests; do not infer them from the performance mute
alone. Master sources retain today's processed master semantics, without click.

CPU2 renders disjoint tracks into mx[1] (`c1_parts():1133`), then `mix_block()`
waits before combining sums. Each track needs its own fixed output slice; clear
silent/early-return slices every block, write only from the owning core, and
consume only after the existing wait and memory synchronization. Exercise
CPU2 failure fallback as well as normal completion. Do not render a track twice
to obtain a tap or derive a track by subtracting two accumulated shared mixes.

Use 32-bit tap samples at the existing engine scale, explicit saturating s16
conversion at USB output, and an int64_t L+R intermediate divided by two for
MONO before saturation. Specify rounding, clipping and interleaved channel
order. Route OFF to zero; permit duplicate sources. Publish a validated 12-slot
table at block boundaries using a synchronization method valid for ISR and both
cores. No heap allocation, partial-table reads or USB service recursion into DSP.

RAM estimates: a 512-frame, 12-channel s16 ring is 12,288 bytes (10,240 bytes
more than today's ring). Six stereo int32 taps at HALF_FRAMES=128 require 6144
bytes if a full half-block is retained; actual mix control blocks are smaller,
so measure before choosing lifetime/size. Double packet buffers, return ring,
feedback state and resampler histories are additional. `firmware/app.ld:22–29`
allocates 96 KiB normal RAM and 348 KiB POOL. DESIGN.md's older utilization
figures are not a current linker map. No new memory fit is claimed. A naive
12-channel 48 kHz FIR costs roughly six times the stereo FIR work; benchmark
real worst-case songs and both CPU settings at 320/360 MHz before approval.

## Return, UI and persistence plan

Stereo return alone alongside legacy stereo capture is bandwidth-plausible,
but controller OUT/FIFO/feedback support remains unverified. The vendor UAC
example uses synchronization/resampling support; Skiff cannot assume its own
DAC clock follows the host. Existing capture explicitly notes about 44,117.6 Hz
versus 44,100 (`usb.c:72`) and regulates ring fill by sample-count corrections.

Implement return only with a bounded RX ring, safe RX DMA synchronization,
explicit feedback or adaptive resampling, 48-to-engine-rate conversion,
occupancy instrumentation and controlled under/overflow recovery. Monitor it
after the master capture tap, before local output saturation, with gain/mute
and fades; never feed it into a track or USB master capture by default. Define
local/return monitoring independently and reset queues on rate, interface,
suspend and configuration changes. Measure round-trip latency; ring occupancy,
128-frame DAC buffering and host scheduling preclude an untested latency claim.

UI integration points: `menu_items.c` (also exposed by editor_menu.c),
`ui_menu.c`, `ui_draw.c` and `ui_input.c`. Reuse current fonts, palette, focus,
encoder/button behavior and scrolling rows; no stand-in web UI. Offer 12 mono
slots, L/R/MONO sources for all six tracks and master, OFF, adjacent stereo
assignment, two-track mono assignment and 6 STEREO / 6 MONO / CUSTOM presets.
Editing one slot changes only that route. Return controls appear only in a
supported transport build. Route changes apply without re-enumeration; topology
changes require safe detach/re-enumeration with a visible warning.

`settings_persist.c:17–40` uses exact-size PER1–PER4 imports; simply appending
fields would reject existing settings. Add a versioned explicit routing section
and a new import migration preserving all legacy fields, including independently
built features. Validate every source ID, route count and return parameter.
Default migrated routes to six stereo tracks in the new mode; leave legacy
stereo mode's master behavior intact. Use global settings only initially.
`project.c:1419–1451` defers writes while transport is busy, compares unchanged
data and retries failures: reuse this path and update backup/export consumers
and migration/torn-write tests. Do not repurpose undocumented spare preference
bytes or alter song/project formats for an unapproved song override.

## Incremental acceptance plan

1. Resolve connector speed, endpoint/FIFO/DMA documentation and host topology;
   select an approved transport or revised requirement. Produce exact maxima,
   bus-time reservation and endpoint allocations. This gate is currently open.
2. Add fixed-size dry track taps and scaling tests, proving unchanged local
   master output, SPREAD, mute/solo, silence and CPU2/fallback equivalence.
3. Add validated routes, presets and block-boundary mutation tests; prove zero
   allocation and independent slot edits, duplication and overflow-safe MONO.
4. Add a compile-time transport variant while retaining the existing stereo
   build. Test descriptors, rate controls, idle/active interfaces, packet maxima,
   channel order, coherent split streams if used, ring stress and lifecycle reset.
5. Add return only after its separate gate; test resampling, drift, loss recovery,
   gain/mute and loopback exclusion. Add native UI and versioned global storage.
6. Run host regression, descriptor variants, UI rendering, persistence/backup and
   target builds after each stage. Compare map, code size and real DSP timing.
7. With explicitly authorized hardware testing, inspect negotiated speed and
   Audio MIDI Setup for 12 inputs/2 outputs; record all six tracks and independent
   mono assignments in a DAW. Test MIDI/CDC concurrently, both rates, hubs,
   reconnect, sleep/resume, long drift/dropout runs, monitoring and latency.
   Compilation and simulated packets do not satisfy this gate. Never auto-flash.

## Alternatives requiring an explicit choice

* Retain stereo USB capture and add flexible selection of its two sources first.
  Investigate stereo return separately; neither promises 12 simultaneous inputs.
* Research capture-only 12-channel 44.1 kHz split streaming, contingent on FIFO,
  endpoint, host grouping and timing proof. CDC preservation is unresolved.
* Research lower-rate 12-channel PCM, for example 32 kHz: 768 B capture and 128 B
  return per nominal frame. This clears the obvious payload barriers, not the
  hardware/host gates; adds conversion and changes fidelity. Never select it
  silently. Eight-bit PCM also reduces traffic but sacrifices fidelity and is
  not recommended as an implicit fallback.
* Use a demonstrated high-speed controller/board connection or hardware bridge
  with an adequate digital audio link. A connector/controller change may be
  required; a generic AC79 high-speed claim is insufficient.

## Checks actually run

`./build.sh`: exited 1 at the Pillow prerequisite check. The documented
`~/.jieli/toolchain` and `~/fw-AC79_AIoT_SDK` directories are also absent.
No target binary or current map was generated.

`sh tests/run_tests.sh`: storage, user presets, input debounce, TRS MIDI, both
CDC/no-CDC stereo UAC tests and all 20 descriptor configurations completed
successfully. The runner then exited 1 at its required `build/felucca.fwsc`
existence check with `run ./build.sh first`; the remaining suite did not run.
Full output for this session: `/tmp/skiff-feasibility-tests.log`.

No multichannel transport, return, macOS enumeration or physical audio behavior
was exercised. This workspace has no Git metadata, so no commits were created.
