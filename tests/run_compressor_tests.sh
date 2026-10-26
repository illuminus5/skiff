#!/bin/sh
# Host-only tests; no device access or automatic installation.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/compressor
CFLAGS="-O1 -g -w -DFELUCCA_TAPE=1 -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=10 -Ibuild/gen -Ifirmware/src -Itests"
cc $CFLAGS -fsanitize=address,undefined -fno-sanitize=shift,signed-integer-overflow tests/compressor_test.c -lm -o build/host/compressor/dsp
build/host/compressor/dsp
cc $CFLAGS tests/insert_test.c -lm -o build/host/compressor/insert
build/host/compressor/insert
cc $CFLAGS -DVOICES_2CORE=8 -pthread tests/dualcore_test.c -lm -o build/host/compressor/dualcore
COMP_TEST=1 SCENE=mix: build/host/compressor/dualcore
cc $CFLAGS tests/usb_lane_v2_test.c -lm -o build/host/compressor/usb-lane
COMP_TEST=1 build/host/compressor/usb-lane
sh tests/run_tape_tests.sh
