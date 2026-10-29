#!/bin/sh
# Display-only USB profile. Local synthesis / MIDI remain; USB audio is omitted.
set -eu
cd "$(dirname "$0")/.."
# Prefer the prepared project runtime; keep explicit PYTHON overrides.
if [ -z "${PYTHON:-}" ] && [ -x "$PWD/.build-deps/python/bin/python" ]; then
    export PYTHON="$PWD/.build-deps/python/bin/python"
fi
if [ -z "${JIELI_TOOLCHAIN:-}" ] && [ -x "$PWD/.build-deps/jieli-linux-toolchains-20260730.1/pi32v2/bin/clang" ]; then
    export JIELI_TOOLCHAIN="$PWD/.build-deps/jieli-linux-toolchains-20260730.1"
fi
if [ -z "${AC79_SDK:-}" ] && [ -f "$PWD/.build-deps/ac79-sdk/cpu/wl82/tools/uboot.boot" ]; then
    export AC79_SDK="$PWD/.build-deps/ac79-sdk"
fi
# Opt-in video profile; ordinary firmware defaults are unchanged.
export FELUCCA_DISPLAY_MIRROR=1 FELUCCA_UAC=0
export FELUCCA_CDC_DEFAULT=0
case "${1:-}" in
    --bulk)
        export FELUCCA_DISPLAY_BULK=1 FELUCCA_CDC=1 FELUCCA_CDC_DEFAULT=1
        # LSB/(3+1): 13.33 MHz at 320 MHz CPU, 15 MHz at 360 MHz.
        # Keep the 66 ns minimum write cycle of the original ST7789V.
        export FELUCCA_MIRROR_LCD_BAUD="${FELUCCA_MIRROR_LCD_BAUD:-3}"
        exec sh build.sh --release 2.0.2-mirrorbulk ;;
    "")
        export FELUCCA_DISPLAY_BULK=0
        exec sh build.sh --release 2.0.2-mirror ;;
    *) echo "usage: sh fm1-display-mirror/build.sh [--bulk]" >&2; exit 2 ;;
esac
