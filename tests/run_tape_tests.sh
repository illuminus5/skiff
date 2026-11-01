#!/bin/sh
# Host-only tests, no FM-1 access. Requires generated headers.
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/tape-store
cc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/src \
 tests/tape_store_codec_test.c -o build/host/tape-store/codec
build/host/tape-store/codec
cc -O1 -g -w -fsanitize=address -DFELUCCA_TAPE=1 -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=10 \
 -Ibuild/gen -Ifirmware/src -Itests tests/tape_store_test.c -lm -o build/host/tape-store/project
build/host/tape-store/project
sh tests/run_tape_fx_tests.sh
