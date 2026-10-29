# Skiff v1.4 stereo USB audio investigation

2026-10-21. Installed version reported by user: v1.4; host reported macOS 15.1.1.
An optional diagnostic source change has now been prepared; no diagnostic
firmware has been flashed or verified on hardware.

## Evidence

The direct-port post-restart snapshots (`~/Desktop/fm1-usb-retry.txt` and
`~/Desktop/fm1-audio-retry.txt`) show a new USB session, VID 1209/PID 0001,
bcdDevice 0x0328, no app user clients, and no published Felucca audio device.
The earlier ChatGPT exclusive-access error was real but is not sufficient to
explain this clean-session failure. Absence of an AppleUSBAudio kernel child
alone is not a complete driver diagnosis: current captured logs show the
user-space `usbaudiod` service discovering this device.

`tools/fm1_usb_descriptors.c` was compiled with macOS IOKit and CoreFoundation
and run successfully with host access permission. It reads cached configuration
data via GetConfigurationDescriptorPtr, whose SDK documentation explicitly says
the device need not be open. It never calls USBDeviceOpen/Seize or sends device
requests. The initial sandboxed run could not create the USB plugin; the
permission-approved run succeeded. All interfaces/plugins are released on exit.

Actual installed configuration, 178 bytes, three interfaces, CDC absent:

```text
0000: 09 02 b2 00 03 01 00 80 32
0009: 09 04 00 00 00 01 01 00 00
0018: 0a 24 01 00 01 1f 00 02 02 01
0028: 0c 24 02 01 03 06 00 02 03 00 00 00
0040: 09 24 03 02 01 01 00 01 00
0049: 09 04 01 00 02 01 03 00 00
0058: 07 24 01 00 01 41 00
0065: 06 24 02 01 01 00
0071: 06 24 02 02 02 00
0077: 09 24 03 01 03 01 02 01 00
0086: 09 24 03 02 04 01 01 01 00
0095: 09 05 01 02 40 00 00 00 00
0104: 05 25 01 01 01
0109: 09 05 81 02 40 00 00 00 00
0118: 05 25 01 01 03
0123: 09 04 02 00 00 01 02 00 00
0132: 09 04 02 01 01 01 02 00 00
0141: 07 24 01 02 01 01 00
0148: 0e 24 02 01 02 02 10 02 44 ac 00 80 bb 00
0162: 09 05 84 05 c4 00 01 00 00
0171: 07 25 01 01 00 00 00
```

These advertise stereo s16 at 44100/48000 Hz, async EP84 with 196-byte maximum,
idle/active AS alternate settings, and AC collection order AS2 then MIDI1.
Thus the installed device includes the audio-first ordering fix and audio is
not compiled out. Interface 2 having no endpoints at alt 0 is expected.
Descriptor presence and the host parser model do not establish host acceptance.

Permission-approved unified-log reads including info/debug show `usbaudiod`
detecting Felucca and constructing its USBDevice. One kernel message about five
seconds after a reconnect reports:

```text
Felucca@01100000 endpoint 0x00: status 0xe0005000 (pipe stalled): 0 bytes transferred
```

This is an EP0 control-transfer stall, not EP4 audio underrun evidence. The log
does not identify the eight-byte SETUP packet. Unsupported optional USB requests
can legitimately stall, so temporal proximity does not establish the root cause.
Other generic XPC/bundle messages are also not proof of a damaged OS installation.

## Next engineering step

Identify the failing control request and host initialization decision before
changing audio descriptors or request responses. Installed v1.4 does not expose
a history of stalled SETUP packets with CDC off. The prepared optional source
change records that history and exports it through the existing MIDI editor
protocol; using it on hardware requires a target build and an explicitly
approved flash.
Alternatively obtain a host-side USB control trace with tooling that actually
exposes SETUP packets. The cached-descriptor reader does not provide such a trace.

Source review notes: sampling frequency GET_CUR/MIN/MAX and SET_CUR are handled;
GET_RES is not. This is a candidate to investigate, not evidence that macOS sent
GET_RES or that adding a response would fix this failure. Do not blindly ACK
unsupported requests or disable unrelated audio software based on these logs.

The existing descriptor test for CDC-built-but-off, UAC on, both rates and
audio-first ordering was rebuilt and run during this follow-up. It passes;
its macOS parser model does not exercise the real host service or EP0 hardware.

## Prepared diagnostic source (not installed)

`FELUCCA_USB_TRACE=1` enables a 32-entry SETUP ring and stall markers, with no
allocation, output or USB control behavior changes in the ISR. Normal builds
compile it out. USB descriptors and audio packetization remain unchanged.
The reader copies the ring with interrupts off in the main loop and encodes it
outside that critical section. Ring plus static snapshot use roughly 600 bytes;
actual ISR timing still needs hardware measurement. History persists
across USB bus resets so initialization failures can be read after enumeration.
Old records can be overwritten; retrieve promptly. No data-stage samples or
user music are recorded, only eight-byte SETUP packets and firmware decisions.

Command 82 (0x52), no arguments, returns the snapshot over the existing editor
SysEx framing. The format is documented in `web/EDITOR_PROTOCOL.md`. Stock
firmware does not answer it. Do not attempt to read it expecting new evidence
before installing an explicitly approved diagnostic build.

Build command with the prerequisites configured (does not flash):

```sh
FELUCCA_USB_TRACE=1 ./build.sh --release 1.4-usbtrace
```

For the workspace-local dependencies installed in this session, configure:

```sh
export PATH="/opt/homebrew/bin:$PATH"
export DOCKER_HOST="unix://$HOME/.colima/skiff-build/docker.sock"
export DOCKER_CONFIG="$PWD/.build-deps/docker-config"
export JIELI_TOOLCHAIN="$PWD/.build-deps/jieli-linux-toolchains-20260730.1"
export AC79_SDK="$PWD/.build-deps/ac79-sdk"
export PYTHON="$PWD/.build-deps/python/bin/python"
```

After a diagnostic build has been separately approved and installed:

```sh
.build-deps/python/bin/python tools/fm1_usb_trace.py --list
.build-deps/python/bin/python tools/fm1_usb_trace.py --input Felucca --output Felucca
```

CoreMIDI port enumeration was verified on this Mac: both names are `Felucca`.
The trace query itself has not been sent to a diagnostic firmware on hardware.

The reader requires mido/python-rtmidi, uses normal MIDI ports and sends only the
read-only trace query. It does not seize the USB device, flash or alter settings.

Checks: ring wrap, stall marks and counter rollover pass under
AddressSanitizer/UndefinedBehaviorSanitizer. Four Python tests cover decoding,
chronological order and malformed replies. Both the normal editor suite and the
diagnostic editor suite pass. The latter exercises real SysEx framing, full
history replies, repeated read-only queries and rejection of extra arguments.
Descriptor tests pass with the diagnostic flag enabled and off.

The missing build dependencies have been resolved. The compiler archive is
JieLi's `jieli-linux-toolchains-20260730.1` (clang reports 4.0.1), SHA256
`a1545cddbc451a06bac43eb5cd96d831470c42a97c5c0b1f98879a20fc7647bd`.
All three SDK packaging files match the hashes in `tools/build.py`. Dependencies
are isolated in `.build-deps/`, apart from Homebrew-installed Colima, Docker CLI
and libraqm. Pillow was rebuilt in the isolated environment with macOS clang/ar
and libraqm 0.11.0. Generated fonts use the existing generator without changes.

The first diagnostic links exceeded the flash limit. Reusing the existing
pack7 encoder for the complete fixed-layout snapshot resolved this without
removing features or changing memory limits. `tools/size_fns.py` knows the trace
header; IRQ-reachable functions retain -Os, as in the existing build policy.

Target build **passes**, version `v1.4-usbtrace`, identity FM-1_714:

* Installable package: `build/skiff-1.4-usbtrace.fwsc` (610337 bytes).
* SHA256: `b785c202a689d33fd0e117c687da632a9c33550a2d2cd287c398c4d3f24286d9`.
* App image: 581532 bytes; normal RAM: 86628 / 98304 bytes.
* POOL: 346316 / 356352 bytes; RAM code + .ramx: 24080 / 24576 bytes.
* Loader: 7181 bytes, 755 bytes below its staging limit.
* Register-access and RAM-code checks pass. Trace storage plus snapshot: 592 bytes.

Build log: `build/diagnostic-build.log`. Editor logs: `build/editor-test.log` and
`build/editor-trace-test.log`. The broader suite is now run against this
diagnostic package using its expected `build/felucca.fwsc` alias; its results
belong in `build/full-tests.log`. No hardware execution, audio fix, actual
EP0 diagnosis or successful streaming is implied by these build/test results.

The dedicated Colima profile is `skiff-build`; the active Docker context was
not changed. Its socket is `~/.colima/skiff-build/docker.sock`, Docker client
configuration is `.build-deps/docker-config`, and only the workspace was
explicitly mounted writable. No autostart service was enabled.
