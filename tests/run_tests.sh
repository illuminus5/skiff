#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests of the Felucca sources (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the GM map on DRUM, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# UI renders (tests/ui_render.c): every screen in every palette from the real drawing code: the layout lint (no text
#                   off the screen, cut, hidden, overlapping or spilling out of its cell / card; only free text
#                   ellipsised), GREY gray, MONO neutral, the draw cost, the text audit (build/ui_new/text_audit.tsv);
#                   PNGs of GREY MONO GREEN PAPER NIGHT in build/ui_new (tests/ui_render.py), the findings in build/ui_new/report.txt;
#                   FM6's 32 algorithm charts as drawn (no box overlapping, no route through a box or crossing another);
#                   DIGITAL's screens (its algorithm charts, OP ENV) with FELUCCA_FM4=1 too (build/ui_fm4: lint, GREY, MONO);
#                   every frame of the rolling digits (lint, GREY, MONO), their filmstrips in build/ui_slot;
#                   the alignment: every text / icon / keycap meant to be centred, or on its neighbours' line, by its
#                   ink against its box (cells, chips, buttons, rows, knobs, the roll's strip, the keycaps' pills), every
#                   screen in FLAT and LINE and every palette, over every value it can show; 1 px off or more fails
#                   (build/ui_new/align.txt). MENU > LARGE: every screen again (FLAT, LINE, every palette) and the page / value
#                   sweep with the tall cards, the same lint and alignment; sheet_LARGE_GREY.png, sheet_LARGE_MONO.png.
#                   (Skiff 2.0) The UI golden: every picture written is hashed and compared with tests/ui_golden.txt; a
#                   change of the look fails with the list of pictures (UI_GOLDEN_UPDATE=1 rewrites the file).
# UI (tests/ui_test.c): the UI sources against stub display / buttons / knobs: sound loads keep the steps and
#                   the track's ARP / SCL / SLICER, the undo (SAVE held), REC on TRACKS / SEQ / ARP, STEP and ARP
#                   while recording, MIDI IN ROUT, saves refused while playing and the OVERWRITE? dialog, MUTE on
#                   TRACKS KNOB 1, the DRUM grid (keys, knobs, LEDs, pages, live recording into it, a BEAT in it).
# Audio / persistence / editor: the real C paths against simulated DMA and NOR flash: bounded overload
#                   fades, shared-voice limits, deferred settings and retries, failed-save rollback, the autosave
#                   (1.2: when it writes, the power-on restore, damaged copies, a write cut short, wear over a session),
#                   malformed transfers, transport-stop timeouts, MIDI and UART recovery; the MENU settings over the
#                   editor (MENU_DESC / MENU_SET: every item, clamping, unknown ids, saving, USB SERIAL applied later).
# CHORD (tests/chord_test.c): the chord keys (src/chord.c): diatonic triads / sevenths of several scales and roots,
#                   the fixed shapes and voicings (at most 4 notes), names, MONO plays the root, a release ends
#                   exactly what its key / MIDI note started, recording, the ARP, MIDI IN, kits ignore CHRD.
# RATCH (tests/ratchet_test.c): a step's ratchet (x1..x4): its parts in the sequencer (equal, gated, chords and drum
#                   hits whole, one chance roll, swing, no slide or tie out, STOP), FUN8 round trip and older projects x1,
#                   user preset patterns, SEQ > CHANCE KNOB 3 and the roll / grid drawing.
# TIES (tests/ties_test.c): (2.0, #25) per-note ties: a note going on while another starts (one note-on each, the
#                   held one ending where a TIE chain's would), a miss, a tie never starting a note, MONO, RATCH, STOP;
#                   the 9-byte step's byte 7 and the record's version 3 (1.4 reads 1 and 2).
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# PERFORM (tests/perform_test.c): the FX hold layer (src/perform.c): 1/16 starts, stereo buffer effects, the
#                   too-long REPEAT, the SLICER interplay, silent layer keys, idle bit-identical, cost; build/perform_demo/;
#                   OCT UP / DN (the harmonizer): pitch, stereo, clicks, the shimmer bounded, cost; build/fx_demo/.
# REVERB (tests/reverb_test.c): REVERB TYPE (src/fx.c): ROOM bit for bit as before, SPRING's decay against SIZE,
#                   its chirp (group delay rising with frequency), stability at the corners, level, a model change
#                   without a click, its cost against ROOM (+30 % at most); demos in build/fx_demo/.
# SLICE (tests/slice_test.c): slice tables, AUTO onsets of a user-slot loop, reverse, keys, modes, the MAN slices
#                   (SLICES page) and their store in the slot (src/slice_store.c); the presets and the loop;
#                   demos in build/slice_demo/.
# SMPMIX (tests/smpmix_test.c, Skiff #21): a user slot's sounds' LEVEL (square law), REV and DLY (their own sends, as
#                   the track's would be, 127 at most in all, none while muted), kept with the slot (slice_store.c: for
#                   its header only, with SLICE's record, torn or junk ignored), a slot of before as uploaded.
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column;
#                   the breath (#119): dark .. ~60 % of lit (DIM LO ~30 %), smooth, no dark run over ~10 ms near its peak;
#                   the power-on sweep (1.1, hal/fm1_led_anim.h): its length, every LED every frame at its level, the
#                   head left to right with its tail, the buttons, the end on the idle glow, the tick as before after.
# CLICK (tests/click_test.c, 1.1): the metronome (src/click.c) and the count-in (src/seq.c) through audio.c: each beat at
#                   the first sample of its block, with a 1/16 track's steps 1 5 9 13 (internal and external clock, DIV 1/8
#                   and SWING too), the accent, the frequencies, length, LEVEL, MASTER, OFF / REC / ON, USB audio bit for bit
#                   without it; COUNT-IN 1 / 2 BARS (step 1 exactly N bars after PLAY, STOP cancels, PLAY again nothing,
#                   never with an external clock), notes in its last eighth onto step 1; build/click_demo/.
# USB audio (tests/uac_test.c): the UAC1 descriptors as a host parses them (with and without CDC; 44100 and
#                   48000 Hz), the ring and packetiser: 44.1 frames per packet, every frame in order (bit for bit),
#                   underrun / overrun, restart; SET_CUR; the 44.1 -> 48 kHz resampler (every frame against a
#                   double-precision one, response, SNR, cost); the stream at 48 kHz (47..49 frames per packet,
#                   fast and slow I2S clocks, switching rates mid-stream).
# web (web/test_web.mjs): the editor protocol against its mock device, whose tables must equal the
#                   firmware's (tests/descdump.c -> build/host/desc.json; the MENU settings: tests/editor_test.c -> build/host/menu.json),
#                   the package builder, the updater.
# Browser emulator (web/emu, when emcc is there): the firmware in WebAssembly (build/emu) boots, plays keys and MIDI,
#                   draws, lights its LEDs, keeps a save across instances, plays a song bit for bit as the same file
#                   built with cc (web/emu/native_check.c); the cost of 1 s of a heavy song against real time.
#                   Always, built with cc (web/emu/song_check.c, Skiff phase 5): every track's list played with SONG on
#                   through the device's own loop, an edit while playing, the autosave; a fresh device on that flash.
# PHYS (tests/phys_test.c): stability over the whole parameter and pitch range, the worst-case cost against
#                   the heaviest factory preset, demos in build/phys_demo/; tests/phys_ref.cpp compares the
#                   fixed-point models with DaisySP's float originals when DaisySP is there (DAISYSP=path).
# DRUM (tests/drum_test.c): the drum voices (src/drum_voice.c): pitch, decay, centroid and level against
#                   Felucca's targets, the controls' directions, no clipping, DC, retriggers, the hat choke, the
#                   kick on a small speaker; the DRUM engine (src/eng_drum.c): its key map, the 8 lanes together,
#                   one hit per lane, the choke between lanes; the cost per voice; demos in build/drum_demo/.
# DRUM-X (tests/drumx_test.c, after JIANT FM's): the engine (src/eng_drumx.c, src/drumx_voice.c): every lane sounds,
#                   under full scale, ends; MORPH between A and B and while a hit rings; TUNE; the hat choke; the
#                   knobs, FOLD and FM; a drum track as DRUM's; the project's kit plays; demos in build/drumx_demo/.
# NOISE (tests/noise_test.c): the engine (src/eng_noise.c): COLR's slope (white, pink, brown), the filter and the
#                   register clock following the key, META periodic at the key, no DC, no clipping at the
#                   corners, a note from silence the same twice, the cost per voice; demos in build/noise_demo/.
# DIGITAL -> FM6 (tests/fm4_test.c, built with FELUCCA_FM4=1): the retired four-operator engine against its conversion
#                   (src/fm4_convert.c): routes and carriers per algorithm, the presets' PTCH, and the sound (pitch,
#                   centroid, RMS envelope) of its presets and algorithms; demos in build/fm4_demo/. tests/digital_test.c
#                   (FELUCCA_FM4=1 too): DIGITAL's operator envelopes. Default builds have no DIGITAL (engine 1 reserved).
#                   tests/fm4_div0_test.c (UBSan, #61): the conversion without a divide that can be 0 gives the values the
#                   guarded form gave (every INDEX x FLT ENV, random sounds); a 0.9 project with DIGITAL ORGAN converts.
# FM6 (tests/fm6_test.c): the 6-operator FM engine (src/eng_fm6.c, src/fm6_core.c): the 32 algorithms' carriers, the
#                   operator envelopes (stages, rates, the voice ending), bit-stable notes, a click-free retrigger, no DC /
#                   clipping over the factory patches, the macros' directions, PTCH, pack / unpack and the SysEx
#                   layouts, the 6-voice cap, the cost per voice; demos in build/fm6_demo/.
# ROBUST (tests/robust_test.c): damaged or crafted stored data and editor requests: a SLICE scan bounded by the slot,
#                   a slot that fails its check leaves no zone, engine numbers past the last refused, a retained older
#                   RAM project bounded, user preset patterns inside their fields, malformed requests never stop PLAY.
# CONV (tests/conv_test.c, 2.0): the converter (web/conv/conv.c) on archives of before: a 1.x archive needs nothing;
#                   FUN9 / SKF1 / SKF2 projects become SKF4 with their steps as patterns, presets of versions 1-3 and
#                   DIGITAL sounds version 6 (an FM6 sound's patch kept), and a fresh unit restoring the result plays
#                   the same notes and sounds. Felucca 1.2+ (web/conv/felucca.c): FUNA projects as its project.c writes
#                   them, their sounds by parameter name, NUDGE, automation and song; its user presets by name.
# LEGACY (2.0, Tier B 10): the formats of before are read by the converter alone (FELUCCA_LEGACY, core.h). The tests
#                   of them (project_test, motion_test, ratchet_test, fm4_*, conv_test) build so; upreset, settings, UI,
#                   persistence, bank, backup and robust run twice: as 2.0 is (OLD FORMAT where a record of before is
#                   met) and as the converter's build (*_legacy: the migrations of 1.x), the fuzzer under ASan both ways.
# Sanitizers (ASan + UBSan, when the compiler has them; SANITIZE=0 skips): the stored-data and protocol tests again
#                   (loader, M-UPGRADE entry, editor, projects, backup, ROBUST) and three short fuzz runs with fixed
#                   seeds: tests/fuzz_ed.c (editor SysEx and raw USB-MIDI packets), tests/fuzz_proj.c (mutated project
#                   stores -> import -> restore -> render), tests/fuzz_smp.c (user sample slot headers -> scan -> SAMPLE /
#                   GRAIN / SLICE). Longer runs: build/host/asan/fuzz_ed 300000 7 (iterations, seed), the same for the others.
#                   UBSan leaves out the DSP's intended wraps (signed overflow, shifts) and the XIP rebase of a user zone's
#                   offset (bounds, object-size, pointer-overflow: correct on the device's flat flash, not in C's model).
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; shift; "$@" || fail=1; }

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions 4..6, grid records; (2.0) DIGITAL and versions 1..3 OLD FORMAT)" "$OUT/upreset_test"
LEGACY="-DFELUCCA_LEGACY=1"                     # ((2.0, Tier B 10) the converter's build: the formats of before, core.h)
$CC $LEGACY -o "$OUT/upreset_test_legacy" tests/upreset_test.c
run "user presets, the converter's build (FELUCCA_LEGACY): versions 1..3, PHYS DRUM and SAMPLE PERC -> DRUM, DIGITAL kept" "$OUT/upreset_test_legacy"

$CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -O2 -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c -lm
run "USB audio input: descriptors (with CDC), ring and packets, 44.1 -> 48 kHz resampler, 48 kHz stream" "$OUT/uac_test"
$CC -O2 -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c -lm
run "USB audio input: descriptors (without CDC), ring and packets, resampler, 48 kHz stream" "$OUT/uac_test_nocdc"
# USB descriptor layouts (#67): CDC UAC LAYOUT CDC-presented 48K; layout 0 and the console left out = 1.0's bytes
# (+ the 48 kHz rate with 48K = 1, the default; 48K = 0: byte for byte)
for v in 1.1.0.1.1 1.1.1.1.1 1.1.2.1.1 1.1.3.1.1 1.1.0.0.1 1.1.2.0.1 0.1.0.1.1 1.1.0.1.0 1.1.0.0.0 1.1.2.1.0 0.1.0.1.0 \
         1.0.0.1.1 1.0.1.1.1 1.0.0.0.1 0.0.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -o "$OUT/usb_desc_test" \
        tests/usb_desc_test.c
    run "USB descriptors: CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48), layout $t_lay" "$OUT/usb_desc_test"
done
# FELUCCA_UAC_AS_FIRST=0 (Felucca #67): the AC collection of Skiff 1.0 .. 1.2 (MIDI first), 1.0's bytes; the parser
# model of the kernel AppleUSBAudio (macOS <= 15) stops before the audio streaming interface there
for v in 1.1.0.1.1 1.1.0.0.1 1.1.0.1.0 1.1.0.0.0 0.1.0.1.1; do
    IFS=. read -r t_cdc t_uac t_lay t_on t_48 <<EOF
$v
EOF
    $CC -DT_CDC="$t_cdc" -DT_UAC="$t_uac" -DT_LAYOUT="$t_lay" -DT_ON="$t_on" -DT_48K="$t_48" -DT_ASF=0 \
        -o "$OUT/usb_desc_test" tests/usb_desc_test.c
    run "USB descriptors (AC collection MIDI first): CDC $t_cdc (presented $t_on), UAC $t_uac (48 kHz $t_48)" \
        "$OUT/usb_desc_test"
done

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }

$CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
# M-VAVE's own V15, from FM1_STOCK_DIR when set (never in the repo): Skiff's loader writes its flash.bin's app area only,
# the header last
v15="${FM1_STOCK_DIR:-/nonexistent}/FM-1.fwsc"
if [ -f "$v15" ]; then
    run "update loader: M-VAVE's V15 flash.bin, the app area only, the header last" "$OUT/ldr_test" "$OUT/old.fwsc" "$v15" stock
else
    echo "FM-1 V15: skipped (FM1_STOCK_DIR not set)"
fi
$CC -o "$OUT/ldr_screen_test" tests/ldr_screen_test.c
run "update loader's screen (Skiff): UPDATE MODE, WRITING and its bar, UPDATE FAILED" "$OUT/ldr_screen_test"

# the SMK-37 Pro build (BOARD_SMK37; after genkigenki's SMK-37 layer): the flash map and the store's owner tag, the
# package layout, the OTA staging and the loader's windows on SMK-layout packages (made here from the FM-1 binaries:
# only the layout and the protocol are under test); the pool and persistence on its layout below (build/gen)
$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 Pro flash map: every object, the pool and the sample slot inside the windows, protected ranges never"     "$OUT/smk_flash_test"
$CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_owner_test" tests/smk_owner_test.c
run "SMK-37 Pro store owner tag (family SKIFF): another firmware's store cleared, ours kept, the stock mapping"     "$OUT/smk_owner_test"
python3 tools/fm1pkg_make.py --board smk37 build/felucca.bin build/loader/ota.bin "$OUT/smk_new.fwsc" >/dev/null
python3 tools/fm1pkg_make.py --board smk37 "$OUT/old_app.bin" build/loader/ota.bin "$OUT/smk_old.fwsc" >/dev/null
run "SMK-37 Pro package layout (fm1pkg_make --board smk37): PRCT 0x9D000, the stock region table"     python3 tests/smk_pkg_test.py "$OUT/smk_new.fwsc"
$CC -DBOARD_SMK37=1 -DOWN_PKG=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 Pro build (staging 0xBB000, record ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk_new.fwsc"
for m in 1 2; do                         # the MODEL=elite / mke builds of ota.c
    $CC -DBOARD_SMK37=1 -DT_MODEL=$m -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK model $m build (MODEL=elite / mke: its own record identity)" "$OUT/ota_test_smk_m$m"         "$OUT/smk_new.fwsc"
done
for m in pro elite mke; do               # the packages BOARD=smk37 MODEL=... sh build.sh made, when present
    f=build/skiff-smk37.fwsc; id="SMK-37 Pro_700"
    [ $m = elite ] && f=build/skiff-smk37-elite.fwsc && id="SMK-37 Elite_700"
    [ $m = mke ] && f=build/skiff-smk37-mke.fwsc && id="MKE-P37_700"
    [ -f $f ] && run "SMK package $f: identity $id, the stock region table" python3 tests/smk_pkg_test.py $f "$id"
done
$CC -DBOARD_SMK37=1 -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37 Pro: app area 0x4000-0x9CFFF, protected ranges untouched" "$OUT/ldr_test_smk"     "$OUT/smk_old.fwsc" "$OUT/smk_new.fwsc"
# M-VAVE's own V016 package, from SMK_STOCK_DIR when set (never in the repo): the SMK app stages its own loader for it
# (SMK37.md), and that loader writes the official flash.bin's app area only, the header last
v016="${SMK_STOCK_DIR:-/nonexistent}/SMK-37_Pro_016.fwsc"
if [ -f "$v016" ]; then
    run "M-UPGRADE entry, SMK-37 Pro, M-VAVE's V016: the app stages its own loader, record ota-SMK-37 Pro_016"     "$OUT/ota_test_smk" "$v016" official
    run "update loader, SMK-37 Pro: M-VAVE's V016 flash.bin, the app area only, the header last"     "$OUT/ldr_test_smk" "$OUT/smk_old.fwsc" "$v016" stock
else
    echo "SMK-37 Pro V016: skipped (SMK_STOCK_DIR not set)"
fi

if [ -f build/gen/felucca_tables.h ]; then
    run "mapped USB profiles, routing, V2 settings and editor compatibility" sh tests/run_usb_tests.sh
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
    run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/chord_test" tests/chord_test.c -lm
    run "chord keys: diatonic and fixed chords, voicings, MONO root, releases, recording, ARP, MIDI IN, kits" "$OUT/chord_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/speaker_test" tests/speaker_test.c -lm
    run "SPEAKER EQ: FLAT / LOWCUT / BASS+ responses, BASS+ harmonics of the bass, the sub cut, no offset after" "$OUT/speaker_test"
    run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
    mkdir -p build/tracks_demo
    run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
    run "project formats, the converter's build (FUN1..FUN5 -> FUN6, the grid and song chain; DIGITAL -> FM6, SAMPLE PERC -> DRUM)" "$OUT/project_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/motion_test" tests/motion_test.c -lm
    run "motion, whole-step chance, FUN7 migration, song restore, ARP repeat and the 1.2 ARP modes (the converter's build)" "$OUT/motion_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fixes13_test" tests/fixes13_test.c -lm
    run "1.3: Felucca's fixes (PAN +100, AUTOMATION KNOB 3 keeps its row)" "$OUT/fixes13_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/feat13_test" tests/feat13_test.c -lm
    run "1.3: Felucca's features (LFO 2, SEQ TOOLS, FOLLOW, MOMENTARY, LANE MUTES, STEP PREVIEW, PROGRAM CHANGE)" "$OUT/feat13_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/insert_test" tests/insert_test.c -lm
    run "1.3: the INSERT (bit for bit off, every type bounded with no DC, MIX's glide, automation, stored)" "$OUT/insert_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/ratchet_test" tests/ratchet_test.c -lm
    run "RATCH: x1..x4 in a step (notes, chords, drum hits), gates, chance, swing, projects (the converter's build), user presets, CHANCE page; FLAM" "$OUT/ratchet_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/ties_test" tests/ties_test.c -lm
    run "2.0 TIES (#25): notes going on while others start, a miss, a tie never starts a note, MONO, RATCH, STOP; record version 3" "$OUT/ties_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_control_test" tests/midi_control_test.c -lm
    run "USB/TRS clock, bend, sustain, ownership and panic recovery" "$OUT/midi_control_test"
    $CC -O1 -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -o "$OUT/digital_test" tests/digital_test.c -lm
    run "DIGITAL (retired, built here with FELUCCA_FM4=1): operator envelopes/levels" "$OUT/digital_test"
    $CC -O2 -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -o "$OUT/fm4_test" tests/fm4_test.c -lm
    mkdir -p build/fm4_demo
    run "DIGITAL -> FM6: the conversion against DIGITAL (FELUCCA_FM4=1): pitch, centroid, RMS envelope; demos" \
        "$OUT/fm4_test" build/fm4_demo
    $CC -O2 -w -fsanitize=integer-divide-by-zero -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src \
        -o "$OUT/fm4_div0_test" tests/fm4_div0_test.c -lm
    run "DIGITAL -> FM6 without a divide by zero (#61): the same values as before, a 0.9 ORGAN project (UBSan)" \
        "$OUT/fm4_div0_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/theme_test" tests/theme_test.c -lm
    run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
    run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    if python3 -c "import PIL" 2>/dev/null; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.5 px, L V <= 1 px" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    fi
    $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/settings_test.c
    run "settings: PER4 and SKS1, palette ids and preference preservation; (2.0) PER1..PER3 refused" "$OUT/settings_test"
    $CC -O1 -w $LEGACY -Ibuild/gen -o "$OUT/settings_test_legacy" tests/settings_test.c
    run "settings, the converter's build (FELUCCA_LEGACY): PER1..PER3 migration" "$OUT/settings_test_legacy"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test" tests/ui_test.c -lm
    run "UI: sounds keep steps, undo, recording, MIDI overflow, pending saves, panel recovery, drum grid, song chain, GREY gray, MONO neutral" "$OUT/ui_test"
    $CC -w $LEGACY -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test_legacy" tests/ui_test.c -lm
    run "UI, the converter's build (FELUCCA_LEGACY): DIGITAL sounds, SAMPLE PERC and old GM projects converted as 1.x did" "$OUT/ui_test_legacy"
    $CC -w -DFELUCCA_CDC=0 -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test_rel" tests/ui_test.c -lm
    run "UI as a release builds it (FELUCCA_CDC=0, build.py --release): no USB SERIAL row, the rest as above" "$OUT/ui_test_rel"
    FV=$(sed -n 's/^#define FELUCCA_VERSION "\([^"]*\)".*/\1/p' firmware/src/felucca.c)   # (the splash, ABOUT: the real version)
    # (Skiff 2.0) ABOUT's build date fixed, so the UI golden holds from day to day
    $CC -O1 -w "-DFELUCCA_VERSION=\"$FV\"" '-D__DATE__="Jan  1 2026"' -Ibuild/gen -Ifirmware/src -Itests \
        -o "$OUT/ui_render" tests/ui_render.c -lm
    mkdir -p build/ui_new/ppm build/ui_slot
    run "UI renders: layout lint (every screen and palette, every page, engine and column value), GREY gray, MONO neutral, alignment by ink (1 px fails), draw cost, every picture as tests/ui_golden.txt has it" \
        env UI_GOLDEN=tests/ui_golden.txt "$OUT/ui_render" build/ui_new build/ui_slot
    if python3 -c "import PIL" 2>/dev/null; then python3 tests/ui_render.py build/ui_new build/ui_slot; fi
    $CC -w -DFELUCCA_FM4=1 -Ibuild/gen -Ifirmware/src -o "$OUT/ui_test_fm4" tests/ui_test.c -lm
    run "UI built with FELUCCA_FM4=1 (DIGITAL, kept in the tree): its OP pages, EDIT cycle, algorithm charts" "$OUT/ui_test_fm4"
    $CC -O1 -w -DFELUCCA_FM4=1 "-DFELUCCA_VERSION=\"$FV\"" -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/ui_render_fm4" tests/ui_render.c -lm
    mkdir -p build/ui_fm4/ppm build/ui_fm4_slot
    run "UI renders with FELUCCA_FM4=1: DIGITAL's screens (EDIT, OP ENV, the 8 algorithm charts), lint, GREY gray, MONO neutral" \
        "$OUT/ui_render_fm4" build/ui_fm4 build/ui_fm4_slot
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/audio_test" tests/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/click_test" tests/click_test.c -lm
    run "metronome and count-in: beats sample for sample (internal and external clock), accent, level, MASTER, not in USB, count-in timing, notes onto step 1" "$OUT/click_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test" tests/persistence_test.c -lm
    run "persistence: deferred settings, retry and failed-save rollback; (2.0) a FUN5 slot OLD FORMAT" "$OUT/persistence_test"
    $CC -w $LEGACY -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test_legacy" tests/persistence_test.c -lm
    run "persistence, the converter's build (FELUCCA_LEGACY): an SKF2 autosave, FUN5 and FUN7 slots" "$OUT/persistence_test_legacy"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/bank_test" tests/bank_test.c -lm
    run "pattern bank (Skiff): patterns kept exactly, torn saves, fallback, wear, the sequence wrap" "$OUT/bank_test"
    $CC -w $LEGACY -Ibuild/gen -Ifirmware/src -o "$OUT/bank_test_legacy" tests/bank_test.c -lm
    run "pattern bank, the converter's build (FELUCCA_LEGACY): projects of before become patterns (proj_migrate)" "$OUT/bank_test_legacy"
    $CC -w -DBOARD_SMK37=1 -Ifirmware/hal -Ibuild/gen -Ifirmware/src -o "$OUT/bank_test_smk" tests/bank_test.c -lm
    run "pattern bank on the SMK-37 Pro layout (USRFLASH 0xD6000..)" "$OUT/bank_test_smk"
    $CC -w -DBOARD_SMK37=1 -Ifirmware/hal -Ibuild/gen -Ifirmware/src -o "$OUT/persistence_test_smk"         tests/persistence_test.c -lm
    run "persistence on the SMK-37 Pro layout (the store from 0x9D000, its owner tag at boot)" "$OUT/persistence_test_smk"
    $CC -O1 -w -DBOARD_SMK37=1 -Ifirmware/hal -Ibuild/gen -Ifirmware/src -o "$OUT/smk_ui_test" tests/smk_ui_test.c -lm
    run "SMK-37 Pro controls (SMK37_UX.md): pads, colours, faders, entries, outer keys, KEY VELOCITY, wheels, the pedal holding the keys, SHIFT, MASTER, TRS clock" "$OUT/smk_ui_test"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/backup_test" tests/backup_test.c -lm
    run "full backup: CRC before writes, stale runtime, USB reset / timeout, malformed objects; (2.0) OLD FORMAT slots backed up" "$OUT/backup_test"
    $CC -w $LEGACY -Ibuild/gen -Ifirmware/src -o "$OUT/backup_test_legacy" tests/backup_test.c -lm
    run "full backup, the converter's build (FELUCCA_LEGACY): older projects, 1.0's FM6 bank, SKF2" "$OUT/backup_test_legacy"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/conv_test" tests/conv_test.c -lm
    mkdir -p "$OUT/conv"
    run "(2.0) the converter (web/conv/conv.c): archives of before -> SKF4 and presets v6; a fresh unit plays them" \
        "$OUT/conv_test" "$OUT/conv"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/editor_test" tests/editor_test.c -lm
    run "editor: real C protocol, malformed transfers, queue recovery, MENU settings (writes build/host/menu.json)" \
        env MENU_JSON="$OUT/menu.json" "$OUT/editor_test"
    $CC -w -DFELUCCA_CDC=0 -Ibuild/gen -Ifirmware/src -o "$OUT/editor_test_rel" tests/editor_test.c -lm
    run "editor as a release builds it (FELUCCA_CDC=0): MENU settings without USB SERIAL (17; id 11 refused)" "$OUT/editor_test_rel"
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/robust_test" tests/robust_test.c -lm
    run "robustness: crafted sample slots, engine numbers, retained old projects, preset patterns, malformed requests" \
        "$OUT/robust_test"
    $CC -w $LEGACY -Ibuild/gen -Ifirmware/src -o "$OUT/robust_test_legacy" tests/robust_test.c -lm
    run "robustness, the converter's build (FELUCCA_LEGACY): FUN8 / FUN5 engine numbers, a retained FUN5 slot" \
        "$OUT/robust_test_legacy"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/mod_test" tests/mod_test.c -lm
    mkdir -p build/mod_demo
    run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
    mkdir -p build/slicer_demo
    run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/swing_test" tests/swing_test.c -lm
    run "SWING: track + global at most 100, sequencer and SLICER step lengths, the SWG display" "$OUT/swing_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/perform_test" tests/perform_test.c -lm
    mkdir -p build/perform_demo build/fx_demo
    run "FX layer effects: on the 1/16, stereo, too-long REPEAT, SLICER, silent keys, idle bit-identical, clicks, OCT UP / DN, cost, demos" "$OUT/perform_test" build/perform_demo build/fx_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/duck_test" tests/duck_test.c -lm
    run "ducking (Skiff): under the source's hits (a kit's KICK only), DEPTH, RELEASE, no click, OFF bit-identical" "$OUT/duck_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drive_test" tests/drive_test.c -lm
    run "DRIVE (Skiff phase 9): DIST's TYPE SOFT HARD FOLD BITS and TONE; SOFT at TONE 0 as before, bit for bit" "$OUT/drive_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
    run "REVERB TYPE: ROOM bit-identical, SPRING decay / chirp / stability / level, model change, cost, demos" "$OUT/reverb_test" build/fx_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
    run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    # (Skiff 1.3) the second core (MENU > CPU 2 CORES: fx.c mix_block, tests/c1_host.h): a thread renders the tracks
    # handed to it, bit for bit; a FELUCCA_CPU2=0 build (none of it) gives the same golden renders
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -DC1_HOST -DVOICES_2CORE=8 -pthread -o "$OUT/regress_c1" tests/regress.c -lm
    run "second core: the golden renders with a thread taking every split of the tracks in turn" \
        "$OUT/regress_c1" tests/golden.txt tests/cpu_baseline.txt
    $CC -O2 -w -DFELUCCA_CPU2=0 -Ibuild/gen -Ifirmware/src -o "$OUT/regress_cpu2_0" tests/regress.c -lm
    run "regression, a FELUCCA_CPU2=0 build (none of the second core): the same golden renders" \
        "$OUT/regress_cpu2_0" tests/golden.txt tests/cpu_baseline.txt
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -DVOICES_2CORE=8 -pthread -o "$OUT/dualcore_test" tests/dualcore_test.c -lm
    run "second core: six tracks of every preset, mixes: bit for bit on one core and on two, every split" \
        "$OUT/dualcore_test"
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -pthread -o "$OUT/dualcore_budget" tests/dualcore_test.c -lm
    run "second core: the voice budget, 8 on one core, VOICES_2CORE with two" env BUDGET=1 "$OUT/dualcore_budget"
    if $CC -fsanitize=thread -pthread -x c -o /dev/null - <<EOF 2>/dev/null
int main(void) { return 0; }
EOF
    then                                        # (Skiff 1.3) the same under ThreadSanitizer: shared state
        $CC -O1 -g -w -Ibuild/gen -Ifirmware/src -DVOICES_2CORE=8 -fsanitize=thread -pthread -o "$OUT/dualcore_tsan" tests/dualcore_test.c -lm
        run "second core under ThreadSanitizer: the mixes of engines" \
            env TSAN_OPTIONS=halt_on_error=1 SCENE="mix:" "$OUT/dualcore_tsan"
    else
        echo "== skip the second core under ThreadSanitizer (no -fsanitize=thread here)"
    fi
    $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/descdump" tests/descdump.c -lm
    echo "== parameter and engine tables as JSON (for the editor mock test)"
    "$OUT/descdump" > "$OUT/desc.json" || fail=1
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/phys_test" tests/phys_test.c -lm
    mkdir -p build/phys_demo
    run "PHYS: stability C-1..G9 over the parameter corners, worst-case cost against PHASE WIRE, demos" "$OUT/phys_test" build/phys_demo
    D=${DAISYSP:-vendor/DaisySP}/Source
    if [ -d "$D/PhysicalModeling" ] && command -v c++ >/dev/null 2>&1; then
        $CC -O2 -w -Ibuild/gen -Ifirmware/src -Itests -c -o "$OUT/phys_fixed.o" tests/phys_fixed.c
        c++ -O2 -std=c++14 -w -I"$D" -I"$D/Utility" -o "$OUT/phys_ref" tests/phys_ref.cpp \
            "$D/PhysicalModeling/modalvoice.cpp" "$D/PhysicalModeling/resonator.cpp" "$D/PhysicalModeling/stringvoice.cpp" \
            "$D/PhysicalModeling/KarplusString.cpp" "$D/Filters/svf.cpp" "$D/Utility/dcblock.cpp" \
            "$D/Dynamics/crossfade.cpp" "$OUT/phys_fixed.o"
        run "PHYS: the fixed-point models against DaisySP's float originals (mode frequencies, decays, Svf)" "$OUT/phys_ref"
    else
        echo "== skip PHYS reference test (no DaisySP: set DAISYSP to a checkout)"
    fi
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drum_test" tests/drum_test.c -lm
    mkdir -p build/drum_demo
    run "DRUM: voice targets, controls, no clipping, retrigger, hat choke, the kick on a small speaker, keys, 8 lanes, cost, demos" "$OUT/drum_test" build/drum_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drumx_test" tests/drumx_test.c -lm
    mkdir -p build/drumx_demo
    run "DRUM-X: every lane sounds and ends, MORPH, TUNE, the pitch envelope, hat choke, the knobs, FOLD, FM, a drum track, demos" "$OUT/drumx_test" build/drumx_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/noise_test" tests/noise_test.c -lm
    mkdir -p build/noise_demo
    run "NOISE: colour slopes, key-tracked filter and clock, META period, DC, clipping, retrigger, cost, demos" "$OUT/noise_test" build/noise_demo
    $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_test" tests/fm6_test.c -lm
    mkdir -p build/fm6_demo
    run "FM6: algorithms, envelopes, retrigger, DC, clipping, macros, patch formats, voices, cost, demos" "$OUT/fm6_test" build/fm6_demo
    # SLICE is in the standard build (firmware/src/core.h): its test always runs (after #22 by andreahaku)
    if grep -q '^#define SLC_BREAK_BPM ' build/gen/felucca_samples.h; then
        mkdir -p build/slice_demo
        python3 tools/gen_waves.py build/genwav >/dev/null      # (the loop's drums; the BREAK no longer uses them)
        python3 tests/slice_loop.py build/slice_demo/loop
        python3 tools/fm1_sample_upload.py build LOOP build/slice_demo/loop build/slice_demo/loop.wav:60 >/dev/null
        $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slice_test" tests/slice_test.c -lm
        run "SLICE: tables, onsets, reverse, keys, modes, MAN slices and their store, demos" "$OUT/slice_test" \
            build/slice_demo/loop build/slice_demo
        $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/smpmix_test" tests/smpmix_test.c -lm
        run "(#21) a user slot's sounds: LEVEL, REV, DLY, their store with the slot, old slots as uploaded; (#35) a slot as DRUM's KIT" "$OUT/smpmix_test"
    else
        echo "== SLICE: build/ was made with FELUCCA_SLICE=0 (no BREAK); run ./build.sh without it first"
        fail=1
    fi
else
    echo "== skip hostsim (run ./build.sh once)"
fi

# ASan + UBSan: the stored-data and protocol paths, and short fuzz runs (fixed seeds: the same inputs every run)
SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=signed-integer-overflow,shift,bounds,object-size,pointer-overflow"
SAN="$SAN -fno-sanitize-recover=undefined -w"
san_ok() {
    printf 'int main(void){return 0;}\n' > "$OUT/san_probe.c" &&
        ${CC%% *} $SAN -o "$OUT/san_probe" "$OUT/san_probe.c" 2>/dev/null && "$OUT/san_probe"
}
if [ "${SANITIZE:-1}" = 0 ]; then
    echo "== skip the sanitizer runs (SANITIZE=0)"
elif [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the sanitizer runs (run ./build.sh once)"
elif ! san_ok; then
    echo "== skip the sanitizer runs (the compiler has no ASan / UBSan)"
else
    mkdir -p "$OUT/asan"
    A="$OUT/asan"
    SCC="${CC%% *} $SAN -Ibuild/gen -Ifirmware/src"
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
    $SCC -o "$A/ldr_test" tests/ldr_test.c
    run "ASan/UBSan: update loader (other app -> this build)" "$A/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc
    $SCC -DOWN_PKG=1 -o "$A/ota_test" tests/ota_test.c
    run "ASan/UBSan: M-UPGRADE entry (own loader)" "$A/ota_test" build/felucca.fwsc
    for t in editor_test project_test backup_test conv_test robust_test bank_test duck_test persistence_test ui_test; do
        $SCC -o "$A/$t" tests/$t.c -lm
        run "ASan/UBSan: $t" "$A/$t"
    done
    $SCC -o "$A/fuzz_ed" tests/fuzz_ed.c -lm
    run "ASan/UBSan fuzz: editor SysEx and raw USB-MIDI packets (20000, seed 7)" "$A/fuzz_ed" 20000 7
    $SCC -o "$A/fuzz_proj" tests/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: project stores -> import -> restore -> render (5000, seed 13)" "$A/fuzz_proj" 5000 13
    $SCC $LEGACY -o "$A/fuzz_proj_legacy" tests/fuzz_proj.c -lm
    run "ASan/UBSan fuzz: the converter's readers (FELUCCA_LEGACY), the same (5000, seed 13)" "$A/fuzz_proj_legacy" 5000 13
    $SCC -o "$A/fuzz_smp" tests/fuzz_smp.c -lm
    run "ASan/UBSan fuzz: user sample slot headers -> scan -> SAMPLE / GRAIN / SLICE (1000, seed 17)" "$A/fuzz_smp" 1000 17
fi

run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py
run "(#21) sample upload CLI: mix (a sound's LEVEL REV DLY) against a fake device" python3 tests/sample_upload_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol + samples, package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
else
    echo "== skip web tests (no node)"
fi

cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o "$OUT/song_check" web/emu/song_check.c -lm
run "browser emulator, native: a song through the device's loop (lists, REPS, an edit while playing, the autosave)" \
    "$OUT/song_check" a "$OUT/song.nor"
run "browser emulator, native: a fresh device on that flash (the lists, each track's steps, the edit)" \
    "$OUT/song_check" b "$OUT/song.nor"

if ! command -v emcc >/dev/null 2>&1; then
    echo "== skip the browser emulator (no emcc: Emscripten builds web/emu)"
elif ! command -v node >/dev/null 2>&1 || [ ! -f build/gen/felucca_tables.h ]; then
    echo "== skip the browser emulator (needs node and build/gen)"
else
    run "browser emulator: the firmware to WebAssembly (web/emu/build.sh -> build/emu)" sh web/emu/build.sh
    cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o "$OUT/emu_native" web/emu/native_check.c -lm
    run "browser emulator: the same song from the native build" "$OUT/emu_native" "$OUT/emu_native.f32"
    run "browser emulator: boot, keys, MIDI, screen, LEDs, PLAY, a save kept across instances, = native, cost" \
        node web/emu/emu_test.mjs build/emu/felucca.wasm "$OUT/emu_native.f32"
    run "(2.0) the converter to WebAssembly (web/conv/build.sh -> build/conv)" sh web/conv/build.sh
    run "(2.0) the converter in the page (web/conv/conv.js): conv.wasm = the native build, the write-back set" \
        node web/conv/conv_test.mjs build/conv/conv.wasm "$OUT/conv"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
