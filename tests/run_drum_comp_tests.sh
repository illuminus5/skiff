#!/bin/sh
# Native tests only. No device access or automatic installation.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/drum-comp
FLAGS="-O1 -g -w -DFELUCCA_TAPE=1 -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=10 -Ibuild/gen -Ifirmware/src -Itests"
# User-XIP offsets intentionally reach the separately mapped sample region;
# ASan checks actual memory accesses. Other existing DSP shift/overflow idioms
# use the same UBSan exclusions as the existing playback-FX checks.
cc $FLAGS -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow,array-bounds \
 tests/drum_comp_test.c -lm -o build/host/drum-comp/dsp-storage
build/host/drum-comp/dsp-storage
cc $FLAGS -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow \
 tests/drum_comp_ui_test.c -lm -o build/host/drum-comp/ui
build/host/drum-comp/ui
cc $FLAGS -DVOICES_2CORE=8 -pthread tests/dualcore_test.c -lm -o build/host/drum-comp/worker
COMP_TEST=1 DRUM_COMP_TEST=1 SCENE=mix: build/host/drum-comp/worker
sh tests/run_compressor_tests.sh
