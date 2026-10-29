#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
CC=${CC:-cc}
MIRROR_PYTHON=${MIRROR_PYTHON:-python3}
mkdir -p build/host
$CC -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined fm1-display-mirror/tests/capture_test.c -o build/host/display-mirror-capture-test
build/host/display-mirror-capture-test build/host/display-mirror-capture.bin
$CC -O1 -w -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/midi_test.c -lm -o build/host/display-mirror-midi-test
build/host/display-mirror-midi-test
# Use the actual display-only target capacity.
$CC -O1 -w -DDM_CAPACITY=16320 -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/ui_capture_test.c -lm -o build/host/display-mirror-ui-sizing
build/host/display-mirror-ui-sizing
$CC -O1 -w -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/segmented_ui_test.c -lm -o build/host/display-mirror-segmented-ui
build/host/display-mirror-segmented-ui
$MIRROR_PYTHON -m unittest discover -s fm1-display-mirror/tests -v
node fm1-display-mirror/tests/viewer_test.cjs
$CC -w -DFELUCCA_DISPLAY_MIRROR=1 -DDM_POOL= -include fm1-display-mirror/firmware/skiff/capture.h -Ibuild/gen -Ifirmware/src tests/editor_test.c -lm -o build/host/display-mirror-editor-regression
build/host/display-mirror-editor-regression > build/display-mirror-editor-tests.log

$CC -O1 -w -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/video_usb_test.c -lm -o build/host/display-mirror-video-usb
build/host/display-mirror-video-usb
$CC -O1 -w -DFELUCCA_UAC=0 -Ibuild/gen -Ifirmware/src tests/settings_test.c -lm -o build/host/display-mirror-video-settings
build/host/display-mirror-video-settings

$CC -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined fm1-display-mirror/tests/bulk_test.c -o build/host/display-mirror-bulk-test
build/host/display-mirror-bulk-test
$CC -O1 -w -DDM_CAPACITY=16000 -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/ui_capture_test.c -lm -o build/host/display-mirror-bulk-ui
build/host/display-mirror-bulk-ui

$CC -O1 -w -DFELUCCA_DISPLAY_BULK=1 -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/video_usb_test.c -lm -o build/host/display-mirror-bulk-usb
build/host/display-mirror-bulk-usb


$CC -Wall -Wextra -Werror -Wno-unused-function -fsanitize=address,undefined fm1-display-mirror/tests/graph_strip_test.c -o build/host/display-mirror-graph-strips
build/host/display-mirror-graph-strips

$CC -O1 -w -DFELUCCA_DISPLAY_BULK=1 -Ibuild/gen -Ifirmware/src fm1-display-mirror/tests/midi_test.c -lm -o build/host/display-mirror-timing-midi
build/host/display-mirror-timing-midi
