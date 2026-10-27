#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# (Skiff 2.0, Tier B: design/TIER_B.md 10) The converter (web/conv/conv.c) to WebAssembly with Emscripten, as
# web/emu/build.sh builds the emulator: the firmware's own sources with its old readers; an archive's objects in and
# out through conv_buf.
#   web/conv/build.sh  ->  build/conv/conv.wasm
# Run after one ./build.sh (build/gen).
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/felucca_tables.h ] || { echo "conv: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "conv: no emcc (Emscripten)"; exit 1; }
mkdir -p build/conv
emcc -O2 -ffp-contract=off -std=gnu99 -w -Ibuild/gen -Ifirmware/src \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/conv/conv.wasm web/conv/conv.c
echo "conv: build/conv/conv.wasm ($(wc -c < build/conv/conv.wasm | tr -d ' ') B)"
