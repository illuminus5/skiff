# FM-1 USB display mirror

An opt-in, video-only Skiff profile mirrors the actual 240 × 240 LCD pixels to a
local browser. Local synthesis, MIDI, controls and antialiasing remain available.
USB audio is disabled and CDC carries pixels instead of the text console.
Ordinary Skiff builds retain their defaults. This feature is independent of the
separate USB audio routing pull request.

## Build and install manually

Use the toolchain, SDK, Docker and Pillow prerequisites in [BUILDING.md](../BUILDING.md).
From the repository root:

```sh
sh fm1-display-mirror/build.sh --bulk
```

The output is `build/skiff-2.0.2-mirrorbulk.fwsc`. The script never flashes it.
Save an editor backup and retain a working firmware package before installing.
Stop the mirror bridge, then use the normal installer manually:

```sh
python3 tools/fm1_install.py build/skiff-2.0.2-mirrorbulk.fwsc
```

Rollback uses the same installer with the retained package. This assumes the
normal updater remains reachable; it does not establish independent recovery
from an arbitrary boot failure. The profile supports FM-1 only, not SMK-37,
stock Groove OS, or an unmodified Skiff installation.

## Run the viewer

Python 3.10+ is required. From the repository root:

```sh
python3 -m venv fm1-display-mirror/.venv
fm1-display-mirror/.venv/bin/python -m pip install -r fm1-display-mirror/requirements.txt
fm1-display-mirror/.venv/bin/python fm1-display-mirror/bridge/server.py
```

Open the private URL printed by the bridge. The server binds only to
`127.0.0.1:8787`, checking its URL token, Host and WebSocket Origin. There are no
browser synth controls. Close the bridge with Ctrl-C before updating firmware
or opening another serial application. Keep **USB SERIAL enabled** for bulk.

Automatic serial selection requires exactly one device with VID `0x1209`, PID
`0x0001`. Add `--serial /dev/cu.DEVICE` to select explicitly. `--port Felucca`
selects the unique MIDI port substring for optional timing diagnostics;
diagnostics can be unavailable while bulk video continues. `--listen-port 8788`
changes the HTTP port. Do not expose this development server to a network.

A labelled calibration pattern is available without hardware:

```sh
fm1-display-mirror/.venv/bin/python fm1-display-mirror/bridge/server.py --mock
```

Scaling, fullscreen, lowest-delay presentation and bounded 30/60 Hz presentation
buffers are available. The viewer only repeats/drops received complete snapshots;
it never interpolates the LFO cursor or recreates Skiff's UI. Source FPS counts
changed screens. An unchanged screen sends no frames, so 0 FPS alone does not
indicate a disconnect.

A slower diagnostic MIDI-only fallback builds without `--bulk`, producing
`build/skiff-2.0.2-mirror.fwsc`. Run the viewer with `--midi` for that profile.
It omits CDC by default and uses bounded SysEx replies. Bulk is recommended for
animation. Hardware access uses pyserial; MIDI fallback and optional stage timing
use python-rtmidi. Mock mode uses only the Python standard library.

## Architecture

LCD fill/blit hooks capture actual RGB565 pixels in panel byte order before SPI
transfer. A bounded lossless journal uses fills and independent LZ4 blocks, with
no full framebuffer or heap allocation on the device. Compression and release
run only in the main loop. Normal pages form one transaction; large held
HOME/GLO maps use two retained 120-row sections, published after final COMMIT.

Bulk EP3 copies at most 64 bytes per service call into separate endpoint DMA
storage, flushing an exact full-packet ending with a ZLP. The main loop retains
the journal until the final copy and commits graph-strip fingerprints only then.
Missed writes, overflow and sequence gaps invalidate partial state and request a
full capture. USB polls at 10 kHz in this dedicated profile, deferred during
nested audio rendering; ordinary profiles retain 2 kHz.

For up to 16 ms after capture, controls/editor/watchdog are serviced before
starting another redraw, avoiding an LCD DMA wait that delays release. Host
stalls cannot defer the local display indefinitely. Start/stop/keepalive are the
only bulk commands. Capture expires after three seconds without keepalive and
stops on DTR loss. Command 83 supplies bounded MIDI reads and read-only timings.

The bridge reconstructs pixels and keeps one replaceable complete snapshot per
browser. Backpressure cannot accumulate an unbounded frame queue. The bulk
profile uses a 16,000-byte BSS journal, a 1 KiB pool hash and 576-byte graph
fingerprints. Existing stack, pool and RAM-code reserves remain enforced by the
build. Integration is opt-in: `FELUCCA_DISPLAY_MIRROR=1`; bulk additionally needs
`FELUCCA_DISPLAY_BULK=1`, `FELUCCA_CDC=1`, `FELUCCA_OTA=1`, `FELUCCA_UAC=0`.
See [protocol/README.md](protocol/README.md) for wire details.

### LCD clock

The bulk script uses divisor 3: about 13.33 MHz at 320 MHz CPU, or 15 MHz at
360 MHz, versus divisor 4 (10.67/12 MHz) normally. This stays within the original
ST7789V's 66 ns minimum serial write cycle in section 7.4.3 of the
[controller datasheet](https://dl.espressif.com/dl/schematics/ST7789V_SPEC_V1.0.pdf).
Exact panel revision and board signal margin remain unverified; V2 clock ratings
are not assumed. Build at the original clock if necessary:

```sh
FELUCCA_MIRROR_LCD_BAUD=4 sh fm1-display-mirror/build.sh --bulk
```

## Validation and limitations

After building, with Node.js available:

```sh
MIRROR_PYTHON=fm1-display-mirror/.venv/bin/python sh fm1-display-mirror/tests/run.sh
```

Tests cover capture/compression with sanitizers, exact panel reconstruction,
488 visible engine/page combinations, held HOME/GLO, LFO/LFO 2 and MOD,
dropped updates and recovery, MIDI priority/fragmentation, USB descriptors and
packet boundaries, settings, timings, HTTP/WS access checks and browser pacing.
Run the ordinary repository suite separately after a normal firmware build.

Manual macOS/Safari testing of development stream4 reported about 20–22 changed
screens/s during LFO playback with stable local sound. Typical timings were
38–43 ms capture, 8–9 ms USB copying, and 0–1 ms main-loop release wait. This is
not a 60 FPS or universal performance claim. That development firmware also
contained the separate ten-channel routing settings; this standalone upstream
build still needs hardware validation.

Occasional apparent connection drops were reported. Whether these were idle
periods or actual stalls remains unconfirmed. Long sessions, reconnect,
screen sleep/wake, held overlays, palettes and loaded musical projects need
further hardware acceptance. Compilation and host tests do not prove physical
USB reliability.

This branch targets Skiff 2.0.2. Its MOD routing sheet replaces the former LFO
DEST page: MOD is captured normally; only LFO/LFO 2 use graph-strip transfers.
The mirror, default firmware, and MIDI fallback profiles pass the original
target/memory checks. Native UI and web checks pass with fresh firmware
descriptors; the menu reachability test uses a 64-bit mask for the expanded
menu. Dedicated mirror sanitizer tests pass; the broad whole-UI sanitizer
soak was not rerun for this update. This merged revision still needs hardware
acceptance; the earlier stream4 measurements above are not measurements of it.

Bulk USB is the default bridge transport. `--bulk` remains accepted for existing
commands; use `--midi` explicitly for the slower MIDI-only firmware. Add
`--diagnose` when troubleshooting to print performance and device-stage timings.

Normal viewing shows a compact connection status. `--diagnose` also reveals FPS,
stage timings and the display-buffering selector; buffering never changes audio
playback speed or Skiff tempo. Connection errors remain visible in normal mode.
