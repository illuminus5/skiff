/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Icons: 4-bit alpha cells of the Fukiai icon font (MIT), Huffman-coded (cv_alpha_hc), 12 px (parameters, lists) and 16 px
 * (header, dialogs, menu): web/fukiai.ttf -> build/gen/ui_icons.h
 * (tools/gen_aa_icons.py; names from assets/icons.json). Which icon a parameter gets is decided
 * here, by its label. FELUCCA_ICONS=0 turns the parameter icons off (labels get their full width back). */
#include "ui_icons.h"
#ifndef FELUCCA_ICONS
#define FELUCCA_ICONS 1
#endif
#define ICON_CELL 12

/* track k's indicator (1.0.2): not an icon of the font (its numerals alias at 12 to 16 px) but the digit 1..4 in
 * Inter Tight on a cushion, a rounded square in the keycap's language, drawn by cv_icon_on: the cushion in the
 * icon's ink (the selected track's: the accent), the digit in what lies under it; rounded in every STYLE, as the
 * keycaps are. (filled: kept for the callers; every cushion is filled) */
#define ICON_TRK 0xF0u                                /* + k (above ICON_COUNT, below ICON_AUTO) */
static uint32_t trk_icon(uint32_t k, int filled) { (void)filled; return ICON_TRK + k % NTRK; }
_Static_assert(NTRK <= UI_NTRK && ICON_TRK + NTRK <= 0xFEu,   /* ((Skiff phase 6) 0xFE: ICON_AUTO) */
               "a colour (tools/gen_ui_palettes.py) and an icon id for every track");
/* (Skiff) BLOCKS: no cushion, the number alone in the track's colour, as the chosen mockup's HOME lanes and PERFORM
 * (design/mock.py, B): in a 16 px cushion's place V (17 px figures), in a 12 px one's M (12 px); on a light ground (a
 * hot tile, a held cell) INK, a silent track DIM. Its cell: the figure's advance (tabular: 1..4 alike) by size rows,
 * the face's capitals' band (the figures') centred in them, as text is (CAP_IN). (The TAKE box's chip is ui_take.c's) */
static int trk_bare(uint32_t size) { return size <= 16u; }
static const aafont_t *trk_face(uint32_t size) { return size >= 16u ? &AF_V : &AF_M; }
#define TRK_CAP_H(size) ((size) >= 16u ? AF_V_CAP_H : AF_M_CAP_H)
#define TRK_TOP(size) HALF_UP((int32_t)(size) - TRK_CAP_H(size))   /* the capitals' first row in the cell */
static int32_t cv_trk(int32_t x, int32_t y, uint32_t size, uint32_t k, uint16_t fill, uint16_t under)
{
    const aafont_t *f = size > 16u ? &AF_M : &AF_S;
    char s[2] = {(char)('1' + k), 0};
    const aag_t *g = &f->g[glyph(f, (uint8_t)s[0])];
    int32_t in = size > 16u ? 2 : 0, w = (int32_t)size - 2 * in;
    int32_t nud = k == 0u ? 1 : 0;   /* the 1's ink centred looks right of centre (its flag): nudged left 1 px (2 looked left) */
    if (trk_bare(size)) {
        if (ux.neon) {                                /* ((Skiff N2) NEON: slanted, half its slant left: centred as before) */
            cv_fx_on(y + TRK_TOP(size) - (size >= 16u ? AF_V_CAP_Y : AF_M_CAP_Y), trk_face(size), 0);
            x -= cv_fx_w(trk_face(size)) / 2;
        }
        cv_text_on(x, y + TRK_TOP(size) - (size >= 16u ? AF_V_CAP_Y : AF_M_CAP_Y), trk_face(size), s,
                   fill == T_DIM ? T_DIM : under == T_BG || under == T_SURF || under == T_RAISE ? T_TRK(k) : T_INK, under);
        cv_fx_off();
        return text_w(trk_face(size), s);
    }
    if (fill != T_DIM)                                /* BLOCKS: every track in its own colour (the number on it) */
        fill = T_TRK(k);
    cv_rrect(x + in, y + in, w, w, -(w / 4), fill, under);
    GFX_HOOK_ALIGN(x + in - nud, y + in, x + in + w - nud, y + in + w, AL_HV, "track cushion digit");   /* (the 1: measured against its nudge) */
    cv_text_on(x + in + (w - g->bw) / 2 - g->bx - nud, y + in + (w - g->bh + 1) / 2 - g->by, f, s, under, fill);   /* its ink
                                                     * centred (up/down: a pixel left over goes down, Barlow's odd digits) */
    GFX_HOOK_ITEM(x + in, y + in, x + in + w, y + in + w);
    return (int32_t)size;
}

/* icon id's cell number at *size 12, 16 or 24 px (missing at 16 or 24: its 12 px cell, *size 12), -1: none */
static int32_t icon_cell(uint32_t *size, uint32_t id)
{
    const uint8_t *idx = *size == 24u ? AI24_IDX : *size == 16u ? AI16_IDX : AI12_IDX;
    if (id >= ICON_COUNT)
        return -1;
    if (idx[id] == 0xFFu) {
        if (*size == 12u || AI12_IDX[id] == 0xFFu)
            return -1;
        *size = 12u;
        idx = AI12_IDX;
    }
    return idx[id];
}
/* the ink box of icon id drawn at (0, 0) in a size px cell: b = x0 y0 x1 y1 (past the ink), from the generator's
 * table (tools/gen_aa_icons.py AI*_INK); a cushion: the cushion; nothing: the cell */
static void icon_ink(uint32_t size, uint32_t id, int32_t *b)
{
    uint32_t sz = size, v, i;
    int32_t k = id - ICON_TRK < NTRK ? -1 : icon_cell(&sz, id), in = id - ICON_TRK < NTRK && size > 16u ? 2 : 0;
    if (id - ICON_TRK < NTRK && trk_bare(size)) {   /* (Skiff) BLOCKS: the bare figure (cv_trk): across its ink, */
        char s[2] = {(char)('1' + (id - ICON_TRK)), 0};   /* up and down the capitals' band */
        text_ink(trk_face(size), s, b);
        b[1] = TRK_TOP(size);
        b[3] = b[1] + TRK_CAP_H(size);
        return;
    }
    if (k < 0) {
        b[0] = b[1] = in;
        b[2] = b[3] = (int32_t)size - in;
        return;
    }
    if (sz == 24u) {
        v = AI24_INK[k];                            /* x0 y0 x1 y1: a byte each */
    } else {                                        /* 12 16: a nibble each, x1-1 y1-1: as the 24's */
        v = sz == 16u ? AI16_INK[k] : AI12_INK[k];
        v = ((v & 15u) | (v & 0xF0u) << 4 | (v & 0xF00u) << 8 | (v & 0xF000u) << 12) + 0x01010000u;
    }
    for (i = 0; i < 4u; i++)
        b[i] = (int32_t)(v >> (8u * i) & 255u);
}

/* (Skiff phase 6) 1: the icons drawn now give the lint (ui_render.c) their ink, not their cell, as their box: a cell
 * with no room for an icon's empty margins (ui_layer.c l6_mute's speaker); 0 else */
static uint8_t icon_lint_ink;
/* icon id at size 12, 16 or 24 px (24: the header battery and USB, the FX map; one missing: its 12 px cell),
 * fg on bg; returns its width */
static int32_t cv_icon_on(int32_t x, int32_t y, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    int32_t k;
    if (id - ICON_TRK < NTRK)
        return cv_trk(x, y, size, id - ICON_TRK, fg, bg);
    if ((k = icon_cell(&size, id)) < 0)
        return 0;
    cv_alpha_hc(x, y, size, size, size == 24u ? AI24_DATA + AI24_OFF[k] : size == 16u ? AI16_DATA + AI16_OFF[k] :
                AI12_DATA + AI12_OFF[k], AI_HC, ramp(fg, bg));
    GFX_HOOK_ICON(x, y, size, id);
    if (GFX_HOOKS && icon_lint_ink) {                /* ((Skiff phase 6) a cell with no room for its empty margins) */
        int32_t b[4];
        icon_ink(size, id, b);
        GFX_HOOK_TEXT(x + b[0], y + b[1] + cv_oy, x + b[2], y + b[3] + cv_oy, "icon", 4u);
    } else {
        GFX_HOOK_TEXT(x, y + cv_oy, x + (int32_t)size, y + cv_oy + (int32_t)size, "icon", 4u);
    }
    return (int32_t)size;
}
/* the same, its ink centred across x .. x + w (w 0: the cell at x) and up and down in y .. y + h (h 0: the cell at y);
 * a pixel left over goes left / up */
static int32_t cv_icon_in(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    int32_t b[4];
    icon_ink(size, id, b);
    if (w)
        x += HALF_UP(w - (b[2] - b[0])) - b[0];
    if (h)
        y += HALF_UP(h - (b[3] - b[1])) - b[1];
    return cv_icon_on(x, y, size, id, fg, bg);
}
/* the same, its ink centred on the line cy (between rows cy - 1 and cy: the middle of rows 0 .. 2 cy - 1, the odd row
 * left over going up; header icons: each glyph sits differently in its cell) */
static int32_t cv_icon_mid(int32_t x, int32_t cy, uint32_t size, uint32_t id, uint16_t fg, uint16_t bg)
{
    GFX_HOOK_ALIGN(0, 0, 0, 2 * cy, AL_V | AL_PASS, "header icon on its middle");
    return cv_icon_in(x, 0, 0, 2 * cy, size, id, fg, bg);
}
#define ICON_NONE 0xFFu                   /* no icon (empty column) */
#define ICON_AUTO 0xFEu                   /* draw_column: look the label up */
#define ICON_GAP 2                        /* px between the icon and the label */

#if FELUCCA_ICONS
typedef struct {
    const char *label;
    uint8_t icon;
} icon_map_t;

/* label -> icon. Labels shared by several parameters (RATE, WAVE, SET) are
 * told apart by descriptor in param_icon(). Unknown labels get ICON_GENERIC. */
static const icon_map_t ICON_MAP[] = {
    /* track: ENV, LFO, destinations */
    {"LVL", ICON_LEVEL}, {"ATK", ICON_ATTACK}, {"DEC", ICON_DECAY}, {"SUS", ICON_SUSTAIN},
    {"REL", ICON_RELEASE}, {"FLT", ICON_CUTOFF}, {"SHP", ICON_SHAPE},
    {"FX", ICON_MOD}, {"RATE", ICON_RATE}, {"WAVE", ICON_WAVE},
    {"FADE", ICON_FADE}, {"AMP", ICON_LEVEL},
    /* arp, scale, pattern */
    {"MODE", ICON_ARP}, {"OCT", ICON_OCTAVE}, {"GATE", ICON_GATE},
    {"HOLD", ICON_HOLD}, {"ORD", ICON_ORDER}, {"ROOT", ICON_PITCH},
    {"LEN", ICON_LENGTH},
    {"DIV", ICON_DIVISION},   /* (CHRD: below, PHYS's) */
    /* fx sends, voice */
    {"CHO", ICON_CHORUS}, {"DLY", ICON_DELAY}, {"REV", ICON_REVERB},
    {"ALLOC", ICON_VOICE}, {"PAN", ICON_PAN}, {"MUTE", ICON_MUTE},
    /* global */
    {"BPM", ICON_TEMPO}, {"CLK", ICON_TEMPO}, {"TUNE", ICON_TUNE}, {"TIME", ICON_TIME},
    {"FDBK", ICON_FEEDBACK}, {"MIX", ICON_MIX}, {"SIZE", ICON_SIZE},
    {"DAMP", ICON_DAMP}, {"TYPE", ICON_REVERB}, {"MIDI", ICON_MIDI},
    {"SYNC", ICON_TEMPO}, {"CPU", ICON_CHIP}, {"SLOT", ICON_SAVE},
    {"LOAD", ICON_LOAD}, {"SAVE", ICON_SAVE}, {"ENG", ICON_WAVE}, {"CLRSQ", ICON_CLEAR},
    {"INIT", ICON_CLEAR}, {"ERASE", ICON_CLEAR}, {"CH", ICON_MIDI}, {"LEVEL", ICON_LEVEL},
    /* engines (eng_*.c edit[] labels) */
    {"NOIS", ICON_NOISE}, {"CUT", ICON_CUTOFF}, {"RES", ICON_RESO},
    {"ALG", ICON_ALGORITHM}, {"R2", ICON_RATIO},
    {"R3", ICON_RATIO}, {"R4", ICON_RATIO}, {"IDX", ICON_MOD}, {"MDEC", ICON_DECAY},
    {"FB", ICON_FEEDBACK}, {"CHIP", ICON_CHIP}, {"DUTY", ICON_PULSE},
    {"VIB", ICON_VIBRATO}, {"ARP", ICON_ARP}, {"TONE", ICON_TONE},
    {"SET", ICON_SAMPLE}, {"BITS", ICON_BITS}, {"LOOP", ICON_LOOP}, {"WAVE2", ICON_WAVE},
    {"DCW", ICON_SHAPE}, {"ENV", ICON_ENV}, {"LINE", ICON_MIX}, {"SUB", ICON_SUB},
    {"FOLD", ICON_FOLD}, {"WAV#", ICON_WAVE}, {"DCY", ICON_DECAY},
    {"PW", ICON_PULSE},
    {"REG", ICON_DRAWBAR}, {"BODY", ICON_LEVEL}, {"TOP", ICON_TONE}, {"PERC", ICON_DECAY},
    {"CLICK", ICON_ATTACK},
    {"SRC", ICON_SAMPLE}, {"START", ICON_STEPS},
    {"POS", ICON_PHASE},
    {"MODEL", ICON_PHYS}, {"BOW", ICON_SUSTAIN},   /* PHYS */
    {"HARM", ICON_RATIO}, {"BEND", ICON_SWEEP}, {"CHRD", ICON_SCALE}, {"SYMP", ICON_MIX},
    {"SNAP", ICON_NOISE}, {"KIT", ICON_SAMPLE}, {"KICK", ICON_ATTACK},       /* PHYS: MEMB, SYMP; DRUM */
    {"TALK", ICON_SWEEP}, {"SHIFT", ICON_TRANSPOSE},
    {"BUZZ", ICON_PULSE}, {"Q", ICON_RESO}, {"RAND", ICON_PROB},
    {"SLOT", ICON_LOAD},                                                    /* FM6 (ALG, FB: above) */
    /* fixed columns drawn by ui_draw.c (STEP page, preset browser, SYSTEM) */
    {"NOTE", ICON_PITCH}, {"STEP", ICON_STEPS}, {"FLAG", ICON_ACCENT}, {"ACC", ICON_ACCENT}, {"LANE", ICON_DRUM}, {"HIT", ICON_GATE}, {"SLD", ICON_SLIDE}, {"USB", ICON_MIDI},
    {"TRACK", ICON_MIX},                  /* TRACKS page (LEVEL, LEN, PAN: above) */
    {"PAT", ICON_STEPS}, {"DEPTH", ICON_MIX},   /* SLICER page (RATE: param_icon) */
    /* (Skiff U4) the words spelled out (L1, L3): the same icons */
    {"ATTACK", ICON_ATTACK}, {"DECAY", ICON_DECAY}, {"SUSTAIN", ICON_SUSTAIN}, {"RELEASE", ICON_RELEASE},
    {"CUTOFF", ICON_CUTOFF}, {"PITCH", ICON_PITCH}, {"SHAPE", ICON_SHAPE}, {"PHASE", ICON_PHASE}, {"OCTAVE", ICON_OCTAVE},
    {"SWING", ICON_SWING}, {"SWING+", ICON_SWING}, {"CHANCE", ICON_PROB}, {"ORDER", ICON_ORDER}, {"SCALE", ICON_SCALE},
    {"QUANTIZE", ICON_QUANTIZE}, {"TRANSP", ICON_TRANSPOSE}, {"LENGTH", ICON_LENGTH}, {"DIST", ICON_DIST},
    {"CHORUS", ICON_CHORUS}, {"DELAY", ICON_DELAY}, {"REVERB", ICON_REVERB}, {"VOICE", ICON_VOICE}, {"GLIDE", ICON_GLIDE},
    {"GL MODE", ICON_GLIDE}, {"PRIORITY", ICON_ORDER}, {"DETUNE", ICON_DETUNE}, {"SLICER", ICON_SLICE},
    {"PATTERN", ICON_STEPS}, {"CHORD", ICON_SCALE}, {"VOICING", ICON_OCTAVE}, {"CLOCK", ICON_TEMPO},
    {"FEEDBACK", ICON_FEEDBACK}, {"COLOR", ICON_TONE}, {"ROUTE", ICON_MIX}, {"ENGINE", ICON_WAVE}, {"NOISE", ICON_NOISE},
    {"DRIVE", ICON_DRIVE}, {"KEYTRK", ICON_KEYTRACK}, {"CRUSH", ICON_BITS}, {"SWEEP", ICON_SWEEP},
    {"VIBRATO", ICON_VIBRATO}, {"VOWEL", ICON_VOICE}, {"VOWEL2", ICON_VOICE}, {"BREATH", ICON_NOISE},
    {"RANDOM", ICON_PROB}, {"PITCH2", ICON_TRANSPOSE}, {"PITCH3", ICON_TRANSPOSE}, {"ROTOR", ICON_VIBRATO},
    {"SOURCE", ICON_SAMPLE}, {"DENSITY", ICON_GRAIN}, {"SPREAD", ICON_NOISE}, {"STRUCT", ICON_SHAPE},
    {"BRIGHT", ICON_TONE}, {"ACCENT", ICON_ACCENT}, {"EXCITE", ICON_MIX}, {"DRIFT", ICON_SWEEP}, {"ALGO", ICON_ALGORITHM},
    {"MOD LVL", ICON_MOD}, {"RATIO", ICON_RATIO}, {"MOD ENV", ICON_DECAY}, {"VEL MOD", ICON_ACCENT}, {"PATCH", ICON_LOAD},
    {"RVRS", ICON_X_REVERSE}, {"MORPH", ICON_SHAPE}, {"VELOCITY", ICON_ACCENT}, {"RATCHET", ICON_STEPS},
};

static uint32_t icon_for_label(const char *l)
{
    uint32_t i;
    if (!l || !l[0] || l[0] == '-')
        return ICON_NONE;
    for (i = 0; i < sizeof(ICON_MAP) / sizeof(ICON_MAP[0]); i++)
        if (str_eq(l, ICON_MAP[i].label))
            return ICON_MAP[i].icon;
    return ICON_GENERIC;
}

/* WAVE / WAVE2 set to a shape: that shape's icon (value names of the engines and the LFO) */
static const icon_map_t WAVE_ICON[] = {
    {"SIN", ICON_W_SIN}, {"TRI", ICON_W_TRI}, {"SAW", ICON_W_SAW}, {"SQR", ICON_W_SQR},
    {"PLS", ICON_W_PLS}, {"PWM", ICON_W_PWM}, {"S&H", ICON_W_SH}, {"NOIS", ICON_NOISE},
    {"DSIN", ICON_W_DSIN}, {"SPLS", ICON_W_SPLS}, {"RSAW", ICON_W_RSAW}, {"RTRI", ICON_W_RTRI},
    {"RTRP", ICON_W_RTRP},
};

static uint32_t param_icon(const param_desc_t *d, int32_t v)
{
    uint32_t i;
    if (!d)
        return ICON_NONE;
    if (d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max &&
        (str_eq(d->label, "WAVE") || str_eq(d->label, "WAVE2")))
        for (i = 0; i < sizeof(WAVE_ICON) / sizeof(WAVE_ICON[0]); i++)
            if (str_eq(d->names[v], WAVE_ICON[i].label))
                return WAVE_ICON[i].icon;
    if (d == &TP[P_LWAVE])
        return ICON_LFO_WAVE;                 /* "WAVE" is also the oscillator wave */
    if (d == &TP[P_ARATE] || d == &TP[P_SLRATE])
        return ICON_DIVISION;                 /* arp / SLICER RATE is a note division, not Hz */
    if (d->names == N_TRIO_MODE)
        return ICON_CUTOFF;                   /* TRIO's MODE is the filter type, not the arp mode */
    if (d->names == N_NOISE_MODE)
        return ICON_NOISE;                    /* NOISE's MODE is the source; its CLK the register clock */
    if (d == &NOISE_CLK)
        return ICON_RATE;
#if FELUCCA_SLICE
    if (d->names == N_SLC_DIV)
        return ICON_SLICE;                    /* SLICE: DIV is the slicing, MODE the gate, REV the direction */
    if (d->names == N_SLC_MODE)
        return ICON_GATE;
    if (d->names == N_SLC_REV)
        return ICON_ORDER;
#endif
    return icon_for_label(d->label);
}

/* the MOD page: the icon of a source, of a destination (an engine parameter: its own) */
static uint32_t mod_src_icon(int32_t s)
{
    static const uint8_t I[MS_N] = {ICON_MOD, ICON_LFO_WAVE, ICON_ENV, ICON_ACCENT, ICON_KEYTRACK, ICON_PROB,
                                    ICON_MOD, ICON_MIDI, ICON_LEVEL, ICON_ENV};   /* ((Skiff phase 9) ENV2: ENV's) */
    return I[clamp(s, 0, MS_N - 1)];
}
static uint32_t mod_dst_icon(const track_t *t, int32_t d)
{
    static const uint8_t I[MD_E1] = {ICON_MOD, ICON_PITCH, ICON_CUTOFF, ICON_SHAPE, ICON_LEVEL, ICON_PAN, ICON_DIST,
                                     ICON_CHORUS, ICON_DELAY, ICON_REVERB, ICON_RATE, ICON_VIBRATO};
    uint32_t id;
    d = clamp(d, 0, MD_N - 1);
    if (d < MD_E1)
        return I[d];
    id = P_E0 + (uint32_t)(d - MD_E1);
    return param_icon(track_desc(t, id), t->p[id]);
}

/* engine name (ENGINES[]->name) -> icon */
static uint32_t engine_icon(const char *name)
{
    static const icon_map_t M[] = {
        {"ANALOG", ICON_WAVE},
#if FELUCCA_FM4
        {"DIGITAL", ICON_ALGORITHM},
#endif
        {"PHASE", ICON_PHASE}, {"LOFI", ICON_BITS},
        {"SAMPLE", ICON_SAMPLE}, {"VOICE", ICON_MOUTH}, {"TRIO", ICON_TRIO}, {"WHEEL", ICON_DRAWBAR},
        {"SLICE", ICON_SLICE},
        {"GRAIN", ICON_GRAIN},
        {"PHYS", ICON_PHYS},
        {"DRUM", ICON_DRUM},
        {"DRUM-X", ICON_DRUM},
        {"NOISE", ICON_NOISE},
        {"FM6", ICON_MOD},                    /* (symbol_modular: six operators patched; a glyph of its own wanted) */
    };
    uint32_t i;
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        if (str_eq(name, M[i].label))
            return M[i].icon;
    return ICON_GENERIC;
}

/* MOTION: the knob and its trace; recording into it (REC armed on the selected track, playing): its REC form */
static uint32_t motion_icon(void) { return rec_on(TSEL) ? ICON_X_MOTION_REC : ICON_X_MOTION; }

/* the icon beside a page's title in the footer (the pages that have one of their own; else ICON_NONE) */
static uint32_t page_icon(const page_t *pg)
{
    switch (pg->graph) {
    case GR_TRK: return ICON_X_MIXER;         /* MIXER (GLO): vertical faders */
    case GR_BANK: return ICON_X_PATTERN;      /* (Skiff) SAVE > PATTERNS: the bank */
    case GR_SONG: return ICON_X_SONG;         /* (Skiff phase 5) SONG: the disc */
    case GR_FEEL: return ICON_PROB;           /* (Skiff 2.0) FEEL: the die (CHANCE's) */
    case GR_EVENTS: return motion_icon();     /* AUTOMATION ((Skiff U3) the list) */
    default: return ICON_NONE;
    }
}

#else
static uint32_t icon_for_label(const char *l) { (void)l; return ICON_NONE; }
static uint32_t param_icon(const param_desc_t *d, int32_t v) { (void)d; (void)v; return ICON_NONE; }
static uint32_t engine_icon(const char *name) { (void)name; return ICON_NONE; }
static uint32_t mod_src_icon(int32_t s) { (void)s; return ICON_NONE; }
static uint32_t mod_dst_icon(const track_t *t, int32_t d) { (void)t; (void)d; return ICON_NONE; }
static uint32_t motion_icon(void) { return ICON_NONE; }
static uint32_t page_icon(const page_t *pg) { (void)pg; return ICON_NONE; }
#endif
