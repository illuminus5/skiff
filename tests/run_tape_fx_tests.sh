#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Native playback FX checks; requires generated headers. No hardware access.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/tape-fx
CC="${CC:-cc} -O1 -g -w -DFELUCCA_TAPE=1 -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=10 -Ibuild/gen -Ifirmware/src -Itests"
$CC -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow \
    tests/tape_fx_test.c -lm -o build/host/tape-fx/dsp
build/host/tape-fx/dsp
$CC tests/tape_port_ui_test.c -lm -o build/host/tape-fx/ui
build/host/tape-fx/ui
