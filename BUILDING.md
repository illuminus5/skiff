# Building Skiff

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).
- Emscripten (optional, `emcc`: the browser emulator, `web/emu/`).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 1.2` makes a release build: the package identity becomes `FM-1_712`
(Skiff's 7XY; Felucca's are 9XY) and the version string `v1.2`; the package is `build/skiff-1.2.fwsc`, and
`build/release-1.2/` holds what a release ships: the package, the app
(`skiff-1.2-app.bin`), `SHA256SUMS`, the samples' attributions (`ATTRIBUTION.txt` for the CC0 sets,
`ATTRIBUTION-BREAKS.txt` for SLICE's BREAK and FUNK), `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).
`web/make_site.py build/skiff-1.2.fwsc 1.2 OUT` makes the GitHub Pages site from it: the browser installer, the
editor and the package with its licences.

`BOARD=smk37 ./build.sh` builds for the M-VAVE SMK-37 Pro instead ([SMK37.md](SMK37.md)): `build/skiff-smk37.fwsc`,
identity `SMK-37 Pro_700` (a release `SMK-37 Pro_7XY`, `skiff-smk37-X.Y.fwsc`). `MODEL=elite` or `MODEL=mke` makes the
same firmware under the SMK-37 Elite's or the MKE-P37's model name (`skiff-smk37-elite.fwsc`, `skiff-smk37-mke.fwsc`),
so each unit's stock updater still takes its factory package later. The FM-1 build is unchanged by it, byte for byte.
Both boards write the same files in `build/`: build the SMK first, copy its package out, build the FM-1 last, then
run the tests (they read the FM-1's binaries; the SMK packages left in `build/` are checked too).

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_CDC_DEFAULT` | 1 | `0`: the console is built in but left out of USB from boot (as MENU > USB SERIAL OFF) |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, stereo, 44.1 or 48 kHz) |
| `FELUCCA_UAC_48K` | 1 | `0`: the USB audio input at 44.1 kHz only, as up to 1.0.5.2 (`firmware/src/usb.c`) |
| `FELUCCA_USB_TRACE` | 0 | Diagnostic only: retain 32 EP0 SETUP requests and firmware stall decisions, readable over MIDI command 82; see `docs/usb-audio-diagnostics.md` |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 1 | the SLICE engine |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

`FELUCCA_USB_LAYOUT` (`0` to `3`, default `0`) picks other USB descriptor layouts for testing; see
`firmware/src/usb.c`.

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works, with no
instrument sets in the SAMPLE engine and no PIANO in SLICE (the PIANO set's middle C): they play a plain sine
at the note's pitch instead, and the screen says NO SAMPLE.

## Browser emulator

```
./build.sh
web/emu/build.sh
python3 -m http.server -d build 8790        # open http://localhost:8790/emu/ (the page loads ../panel.js)
```

`web/emu/felucca_web.c` builds the firmware's sources as the host tests do, against a simulated FM-1, and
Emscripten compiles it to WebAssembly (`build/emu/felucca.wasm`, with `index.html`, `worklet.js` and the font in `fonts/`). The page
keeps the flash's storage sectors (projects, user presets, settings) in the browser. Not emulated: USB (audio,
MIDI out, the editor, updates), TRS MIDI and the user sample slots. The site has it at
<https://illuminus5.github.io/skiff/try/> (`web/make_site.py` copies `build/emu` there: build it with `EMU_PREVIEW=0` after
the release build). It is Felucca's, after X0X's: <https://hugelton.github.io/Felucca/webapp/try/> runs Felucca.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests (and, with Emscripten too, the browser emulator's). Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

Reduced stereo USB routing is enabled by default (`FELUCCA_USB_ROUTING=1`).
See [USB stereo routing](docs/usb-stereo-routing.md) for sources, persistence,
limitations and the required physical acceptance checks. `FELUCCA_USB_ROUTING=0`
builds the previous master-only capture path. Building never flashes the device.

`FELUCCA_UAC_MAC_COMPAT=1` selects the experimental macOS profile reported in
[Felucca #67](https://github.com/hugelton/Felucca/issues/67): 44.1 kHz only,
AudioControl followed by AudioStreaming followed by MIDIStreaming, with only
AudioStreaming in the AudioControl collection. It also updates the streaming
interface used by GET/SET_INTERFACE and changes bcdDevice to avoid reusing the
old descriptor identity. The default is `0`, retaining the normal dual-rate layout.
Use `FELUCCA_CDC_DEFAULT=0` for a new installation; existing saved USB SERIAL
settings take precedence, so set USB SERIAL OFF on the device and close MENU.

`FELUCCA_USB_12CH=1` selects experimental fixed six-track stereo capture at
22.05 kHz/16-bit, enabling the macOS descriptor profile and replacing the two-slot
routing controls. See [experiment limits and tests](docs/usb-12ch-experiment.md).
The default is `0`; the hardware-tested stereo package is retained separately.

`FELUCCA_USB_6CH=1` selects six independently mapped mono capture channels at
44.1 kHz/16-bit, without resampling. SYSTEM exposes USB CH 1..6, default T1..T6
mono. Current source uses PER8 settings (older six-channel packages use PER6/PER7); back up settings before
installing or downgrading. See [six-channel routing](docs/usb-six-channel-routing.md).

The simplified `1.4-fxonly` six-channel package adds just REV, CHOR and DELAY
sources to the normal custom mapper. Click routing and compact FX are removed.
See [simple FX routing](docs/usb-fx-routing.md) for controls and settings migration.

For the ten-channel trial, additionally set `FELUCCA_USB_CHANNELS=10` and use
release `1.4-usb10`. Set it to `8` for fallback release `1.4-usb8`. Both remain
44.1 kHz/16-bit with custom routing and shared FX returns. See
[ten-channel trial](docs/usb-ten-channel-routing.md) for installation, rollback,
settings compatibility, and required physical acceptance checks.
