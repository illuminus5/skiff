/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * SMK-37 Pro port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* (Skiff) The SMK-37 input's parts without a register: the panel's numbers, the pads' tables and step taps, KEY
 * VELOCITY's curves, the outer keys' queue, MASTER's virtual pot. smk_input.h (the scan) fills them; the UI
 * (seq.c, smk_ui.c) reads them; tests/smk_ui_test.c takes this file as it is. */
#pragma once
#include <stdint.h>

#define SMK_NPAD 16u
#define SMK_NLED 19u
#define SMK_KEY_F3 5u             /* keybed key 5 = F3: keys 5..31 are the FM-1's 27 note keys (bits 0..26) */
enum { SMK_BTN_KNOB_BANK = 0, SMK_BTN_FADER_BANK = 1, SMK_BTN_LEFT = 2, SMK_BTN_RIGHT = 3 };   /* (main.c: the power-on combo) */

/* (Skiff, SMK37_UX.md pads C) the pads are always a grid, read like text: hardware pad p (0..7 the top row, 8..15 the
 * bottom; padtest on a unit, 2026-10-10) is white key p of the FM-1's 16 (F3 G3 A3 B3 C4 D4 E4 F4 / G4 A4 B4 C5 D5 E5
 * F5 G5), so the DRUM grid's steps, a layer's tiles and NAME's letters are where the FM-1's white keys have them. Where
 * the keys only play notes the UI sets smk_pad_steps (smk_ui.c): a press is a step tap for the UI instead
 * (smk_padq_take), never a note. A release goes where its press went */
static const uint8_t SMK_WHITE_NOTE[SMK_NPAD] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
#define SMK_PAD_TAP 0x80u
static uint8_t smk_pad_note[SMK_NPAD];   /* what a pad's press did: its white key + 1, SMK_PAD_TAP a step tap, 0 none */
static volatile uint8_t smk_pad_steps;   /* the UI's: 1 the pads are step taps (smk_padq), 0 the white keys */
static volatile uint8_t smk_pedal;       /* the UI's (smk_ui.c): 1 the pedal is down, it holds the keys (seq.c) */
#define SMK_PADQ 32u
static volatile uint16_t smk_padq[SMK_PADQ];   /* step taps: pad << 8 | velocity (0: let go) */
static volatile uint8_t smk_padq_w, smk_padq_r;
/* (Skiff) MENU > KEY VELOCITY (smk_ui.c, a settings byte): how a key's or a pad's velocity plays. DYNAMIC as struck;
 * SOFT for a light touch (loud sooner), HARD for a heavy one; FIXED 64 / 100 / 127 */
enum { SMK_KV_DYN, SMK_KV_SOFT, SMK_KV_HARD, SMK_KV_F64, SMK_KV_F100, SMK_KV_F127, SMK_KV_N };
static volatile uint8_t smk_kv_mode;
static uint32_t smk_vel(uint32_t v)            /* v 1..127 as struck -> as it plays */
{
    v = v < 1u ? 1u : v > 127u ? 127u : v;
    switch (smk_kv_mode) {
    case SMK_KV_SOFT: return 127u - (127u - v) * (127u - v) / 127u;
    case SMK_KV_HARD: return v * v / 127u ? v * v / 127u : 1u;
    case SMK_KV_F64: return 64u;
    case SMK_KV_F100: return 100u;
    case SMK_KV_F127: return 127u;
    default: return v;
    }
}

/* key events beyond the FM-1's 27 (keybed keys 0..4 and 32..36): key << 8 | velocity (0 = release) */
#define SMK_EXTQ 32u
static volatile uint16_t smk_extq[SMK_EXTQ];
static volatile uint8_t smk_extq_w, smk_extq_r;

/* the next outer-key event (keybed key << 8 | velocity, 0 = release), -1 if none */
static int32_t smk_extkey_take(void)
{
    int32_t v = -1;
    if (smk_extq_r != smk_extq_w) {
        v = smk_extq[smk_extq_r];
        smk_extq_r = (uint8_t)((smk_extq_r + 1u) % SMK_EXTQ);
    }
    return v;
}

/* the next step tap (pad << 8 | velocity, 0 = let go), -1 if none */
static int32_t smk_padq_take(void)
{
    int32_t v = -1;
    if (smk_padq_r != smk_padq_w) {
        v = smk_padq[smk_padq_r];
        smk_padq_r = (uint8_t)((smk_padq_r + 1u) % SMK_PADQ);
    }
    return v;
}

/* the FM-1's MASTER pot is knob K1 here, a virtual pot (smk_input.h fm1_adc_read: 0..1023, 24 a detent; starts at 724:
 * master_q12 2048, the level the firmware starts with; smk_ui.c keeps it with the settings) */
static int32_t smk_master = 724;
