#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Run after generated headers exist. No USB device access.
set -eu
cd "$(dirname "$0")/.."
OUT=build/host/usb
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -w"
fail=0
run() { "$@" || fail=1; }
$CC tests/usb_trace_test.c -o "$OUT/trace"
run "$OUT/trace"
run python3 tests/usb_trace_decode_test.py
run python3 tests/usb_service_schedule_test.py
$CC tests/usb_6ch_test.c -lm -o "$OUT/six"
run "$OUT/six"
$CC -Ibuild/gen -Ifirmware/src tests/usb_routing_unit_test.c -lm -o "$OUT/stereo"
run "$OUT/stereo"
for channels in 6 8 10; do
    if [ "$channels" != 6 ]; then
        $CC -DFELUCCA_USB_CHANNELS=$channels tests/usb_multichannel_test.c -lm -o "$OUT/ch$channels"
        run "$OUT/ch$channels"
    fi
    for name in settings editor backup; do
        $CC -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=$channels -Ibuild/gen -Ifirmware/src \
            "tests/${name}_test.c" -lm -o "$OUT/${name}$channels"
        run "$OUT/${name}$channels"
    done
done
for name in fx_returns usb_settings_v2 usb_lane_v2; do
    $CC -DFELUCCA_USB_6CH=1 -DFELUCCA_USB_CHANNELS=10 -Ibuild/gen -Ifirmware/src \
        "tests/${name}_test.c" -lm -o "$OUT/$name"
    run "$OUT/$name"
done
for cdc in 0 1; do
    $CC -DT_CDC=$cdc tests/usb_mac_compat_test.c -lm -o "$OUT/mac$cdc"
    run "$OUT/mac$cdc"
done
exit "$fail"
