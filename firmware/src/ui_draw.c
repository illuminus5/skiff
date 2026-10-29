/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI drawing: the header, the four knob cards, the footer (steps + engine / preset / page)
 * and the frame; the panel between the cards and the footer is ui_graph.c. The layout:
 * flat SURF cards and panels on BG,
 * rounded corners, no rules (MENU > STYLE LINE: no SURF, 1 px rules between the areas instead: draw_rules), colours from the theme tokens only (gfx.c T_*). Type: S (12 px) labels,
 * M (15 px) values and the header, L (28 px) big numerals. A card value too wide for M is set in S;
 * free text (names, messages) is ellipsised at its size. */
static void draw_menu(void);
static int name_on(void);                              /* NAME (ui_name.c) */
static int mom_down(void);                             /* MOMENTARY: LFO held where its turns are (ui_input.c) */
static uint64_t lock_held(void);                       /* parameter locks: the steps held on NOTES (ui_input.c) */
static void name_draw(void);

/* --------------------------------------------------------- drawing --- */
#define COL_W CARD_W                                  /* a card: 57 x 44 at x 3 + 59 c, y 28 */
#define COL_H CARD_H

static int32_t batt_level(void)                         /* thresholds 531 / 561 / 591 on ADC ch3 */
{
#if BOARD_SMK37
    return song.batt_raw >= 602 ? 3 : song.batt_raw >= 560 ? 2 : song.batt_raw >= 530 ? 1 : 0;   /* (the SMK's own) */
#else
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
#endif
}
/* A USB host powers us; there is no separate charger status line.
 * 4 means external power (a steady bolt inside the battery), otherwise its level. */
static int32_t batt_shown(void)
{
    if (usb.config && !usb.suspended)
        return 4;
    return batt_level();
}

/* REC: filled, the selected track armed; outlined, another track armed */
static void draw_rec_mark(int32_t x, uint16_t bg)
{
    if (song.rec)
        cv_icon_mid(x, H_HEAD / 2, 16, (song.rec >> song.sel) & 1u ? ICON_X_REC : ICON_X_REC_O, T_REC, bg);
}

/* the battery: a 16 px Fukiai icon. The stock thresholds give 0..3 bars of 3; the font has 0..4 bars of 4:
 * each level takes the nearest share, 0 -> battery_0 (empty), 1 (1/3) -> battery_1 (1/4), 2 (2/3) ->
 * battery_3 (3/4), 3 (full) -> battery_4 (battery_2 is not used); USB power: battery_charging.
 * Colours as the drawn battery had them: one bar left the accent, empty MID (its outline), else THEME */
static uint32_t batt_icon(int32_t lvl)
{
    static const uint8_t I[5] = {ICON_X_BAT0, ICON_X_BAT1, ICON_X_BAT3, ICON_X_BAT4, ICON_X_BAT_CHG};
    return I[clamp(lvl, 0, 4)];
}
static void draw_battery(int32_t bx)
{
    int32_t lvl = batt_shown();
    cv_icon_mid(bx, H_HEAD / 2, 24, batt_icon(lvl), lvl == 1 ? T_ACCENT : lvl == 0 ? T_MID : T_THEME, T_BG);   /* 24 px: the glyph is wide and short */
}

/* ---------------------------------------------------- rolling digits --- */
/* A number that changes rolls its changed digits like a slot machine; only the drawing: the value, the gauge,
 * the hot colour and the sound change at once. Up: the old digit leaves upward and the new one comes from below;
 * down: the reverse. ROLL_FRAMES UI frames (ui.frame, ~135 ms) on an integer ease-out (ROLL_EASE: cubic, over
 * half-way in a quarter of the time, no overshoot); the ones digit first, each place one frame later, all ending
 * together on a frame equal to the static render. Clipped to the value strip, which alone is redrawn while it
 * rolls (a card: rows ROLL_Y.., the header: the BPM's columns). Characters other than digits never roll.
 * A change during a roll retargets it: it restarts from the value it was going to. Shown at once (no roll):
 * a change within ROLL_SNAP frames of the last one (a fast turn reads better as plain numbers), another
 * shape (length, sign, a non-digit, a name or an enum, a value set in S), another label or unit, a track /
 * engine / palette change (ui.roll[].sig), ui.force (a page change), a card's first draw; MENU > ANIM OFF (#46). */
#define ROLL_FRAMES 9u
#define ROLL_SNAP 3u                                    /* frames (~45 ms) */
#define ROLL_BPM 4u                                     /* ui.roll[]: the four cards, then the header BPM */
#define ROLL_Y 18                                       /* a card's value strip: rows 18..34, its figures (21..31) in the middle, */
#define ROLL_H 17                                       /* the label (MONO's chip: to row 17) above it, the gauge (38) below */
static const uint8_t ROLL_EASE[17] = {0, 45, 84, 118, 147, 172, 193, 210, 223, 234, 242, 247, 251, 253, 254, 255, 255};

static int roll_digit(char c) { return c >= '0' && c <= '9'; }
/* +1 / -1: b rolls in from a (same length, digits where a has digits, the rest equal and one of + - .),
 * the sign of b - a; 0: b is shown at once */
static int roll_dir(const char *a, const char *b)
{
    uint32_t i;
    int d = 0, neg = 0;
    for (i = 0; a[i] || b[i]; i++) {
        if (i + 1u >= sizeof ui.roll[0].from || roll_digit(a[i]) != roll_digit(b[i]))
            return 0;
        if (!roll_digit(a[i]) && (a[i] != b[i] || (a[i] != '-' && a[i] != '+' && a[i] != '.')))
            return 0;
        neg |= a[i] == '-';
        if (!d && a[i] != b[i])
            d = b[i] > a[i] ? 1 : -1;
    }
    return neg ? -d : d;
}
/* field k now shows b instead of a: roll (a retarget mid-roll), or snap */
static void roll_note(uint32_t k, const char *a, const char *b, int snap)
{
    uint32_t el = (uint8_t)(ui.frame - ui.roll[k].t0);
    int d = roll_dir(a, b);
    ui.roll[k].t0 = (uint8_t)ui.frame;
    ui.roll[k].from[0] = 0;
    if (snap || !d || el < ROLL_SNAP || (ui_prefs & PREF_ANIM_OFF))
        return;
    str_cpy(ui.roll[k].from, a, sizeof ui.roll[k].from);
    ui.roll[k].dir = (int8_t)d;
}
/* s (M) at x, y with field k's roll: each character at its pen in s (the digits are tabular and never kerned,
 * so the old value has the same pens); a rolling digit has moved o of the strip's h rows (ROLL_EASE, one frame
 * later per place from the right), clipped to the strip. The roll's last frame clears it: the static text. */
static int32_t roll_text(uint32_t k, int32_t x, int32_t y, const char *s, uint16_t fg)
{
    const aafont_t *f = &AF_M;
    uint32_t e = (uint8_t)(ui.frame - ui.roll[k].t0) + 1u, i, p = 0;
    const char *a = ui.roll[k].from;
    int32_t cy = k < ROLL_BPM ? ROLL_Y : 0, h = k < ROLL_BPM ? ROLL_H : H_HEAD;
    uint16_t bg = k < ROLL_BPM ? T_SURF : T_BG;
    char t[8], ch[2] = {0, 0};
    if (e >= ROLL_FRAMES)
        ui.roll[k].from[0] = 0;
    if (!a[0])
        return cv_text_on(x, y, f, s, fg, bg);
    str_cpy(t, s, sizeof t);
    for (i = 0; s[i]; i++)
        p += (uint32_t)roll_digit(s[i]);
    cv_cy0 = (int16_t)(cy + cv_oy);                     /* the strip (the static characters lie inside it) */
    cv_cy1 = (int16_t)(cy + cv_oy + h);
    cv_scroll = 1;                                      /* (the lint: rolling digits are cut on purpose) */
    for (i = 0; s[i]; i++) {
        int32_t px, ny = y;
        t[i] = 0;
        px = x + text_w(f, t);                          /* (its pen: no digit kerns) */
        t[i] = s[i];
        p -= (uint32_t)roll_digit(s[i]);
        if (s[i] != a[i]) {                             /* p: the places right of it */
            int32_t o = 0, dir = ui.roll[k].dir;
            if (e > p)                                  /* (e < ROLL_FRAMES: the LUT index stays in 1..16) */
                o = (int32_t)((ROLL_EASE[((e - p) * 32u / (ROLL_FRAMES - p) + 1u) >> 1] * (uint32_t)h + 128u) >> 8);
            ch[0] = a[i];
            cv_text_on(px, y - dir * o, f, ch, fg, bg);
            ny = y + dir * (h - o);
        }
        ch[0] = s[i];
        cv_text_on(px, ny, f, ch, fg, bg);
    }
    cv_cy0 = 0;
    cv_cy1 = (int16_t)cv_h;
    cv_scroll = 0;
    return x + text_w(f, s);
}

/* the header (y 0..24): BLOCKS' (ui_blocks.c draw_head_blocks; U2: the only look). Text by its capitals' band
 * (CAP_IN): M from y 2, S from y 4 */
#define HEAD_SY CAP_IN(S, H_HEAD)
#define HEAD_MY CAP_IN(M, H_HEAD)
#define HEAD_BAT_X 214                                 /* the battery's cell */
#define HEAD_GRP_R 208                                 /* the octave / song row ends here */
/* the width cv_free_hint(x, .., s, .., maxw) draws (its word, keycap and the ellipsised rest) */
static int32_t head_hint_w(const char *s, int32_t maxw)
{
    char b[64], p[64];
    kc_words(p, sizeof p, s, 1);                       /* (Skiff) the words (gfx.c kc_words) */
    text_fit(b, sizeof b, p, &AF_S, maxw);
    return text_w(&AF_S, b);
}
/* a message / a layer's label at x, or centred on x 120 (x < 0), at most maxw px with its
 * icon (ui.c MSG_NOFILE: the accent's) and a locked layer's lock */
static void head_msg(int32_t x, int32_t maxw)
{
    const char *m = ui.msg_t ? ui.msg : layer_head();
    int lock = !ui.msg_t && layer_locked();   /* #83: locked open (a double tap): the lock after its name */
    int icon = m[0] == MSG_NOFILE[0];
    int32_t lw = lock ? 15 : 0, iw = icon ? 16 + KH_GAP : 0;   /* the lock: its cell 6 px on, its ink to 15 */
    if (x < 0)
        x = 120 - HALF_UP(iw + head_hint_w(m + icon, maxw - lw - iw) + lw);
    if (icon)
        x += cv_icon_mid(x, H_HEAD / 2, 16, ICON_X_NOFILE, T_ACCENT, T_BG) + KH_GAP;
    x = cv_free_hint(x, HEAD_SY, m + icon, T_TEXT, T_BG, maxw - lw - iw);   /* (may start with a keycap) */
    if (lock)
        cv_icon_mid(x + 6, H_HEAD / 2, 16, ICON_X_LOCK, T_THEME, T_BG);
}
/* the octave, its right end at r (HEAD_GRP_R; (Skiff) BLOCKS: before the title). lim: left bound past the
 * BPM (lx + 8): when the title is long (FM6's OPERATOR n, #70) drop the "OCT" word and keep the signed
 * number; if that still will not fit, draw nothing so it never covers the BPM */
static void head_group(int32_t r, int32_t lim)
{
    char b[8];
    int32_t nw, full, x;
    str_cpy(b, song.octave > 0 ? "+" : "", sizeof b);
    fmt_int(b + str_len(b), song.octave);
    nw = text_w(&AF_M, b);
    full = text_w(&AF_S, "OCT") + 4 + nw;
    if (r - full >= lim) {
        x = r - full;
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        x = cv_text(x, HEAD_SY, &AF_S, "OCT", T_MID);
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_text(x + 4, HEAD_MY, &AF_M, b, T_THEME);
    } else if (r - nw >= lim) {                        /* (#70) crowded: number only */
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_text(r - nw, HEAD_MY, &AF_M, b, T_THEME);
    }
}
/* (Skiff 1.3, from Felucca 1.6.1 #251) MENU > LARGE: a tall card sets its value in one face over everything its knob
 * can show (L where every value fits L, else M, else S: tall_face), so turning it never changes the value's size.
 * BLOCKS' tiles keep a face per value (the user's pick: most of their knobs would lose the big V face). card_span (what the knob shows: fn's text and unit of each value lo..hi)
 * is set just before a draw_column (card_span_of / card_span_by) and taken by it; none: the value's own face. The face
 * is worked out when what the card is of changes (span_face's key), not every frame */
typedef struct card_span {
    void (*fn)(const struct card_span *sp, int32_t v, char *val, const char **unit);   /* value v as the card shows it */
    const void *p;                                      /* fn's: a param_desc_t, a name list, a track */
    int32_t lo, hi;
} card_span_t;
static card_span_t card_span;
static void span_param(const card_span_t *sp, int32_t v, char *val, const char **unit)
{
    uint8_t named = fmt_named;                          /* (param_format sets it: the card's own, kept) */
    param_format((const param_desc_t *)sp->p, v, val, unit);
    fmt_named = named;
}
static void span_names(const card_span_t *sp, int32_t v, char *val, const char **unit)
{
    str_cpy(val, ((const char *const *)sp->p)[v], 12);
    *unit = "";
}
static void span_mod_dst(const card_span_t *sp, int32_t v, char *val, const char **unit)   /* MOD's DEST: the track's */
{
    str_cpy(val, mod_dst_name((const track_t *)sp->p, v), 12);
    *unit = "";
}
static void card_span_by(void (*fn)(const card_span_t *, int32_t, char *, const char **), const void *p, int32_t lo,
                         int32_t hi)
{
    card_span.fn = fn;
    card_span.p = p;
    card_span.lo = lo;
    card_span.hi = hi;
}
static void card_span_of(const param_desc_t *d) { card_span_by(span_param, d, d->min, d->max); }   /* its whole range */
/* the face card c's renderer (face: 0 its big face, 1 M, 2 S) needs for the widest value sp shows */
static uint32_t span_face(uint32_t c, const card_span_t *sp, uint32_t (*face)(const char *val, const char *unit))
{
    static uint32_t key[4], got[4] = {3u, 3u, 3u, 3u};   /* (3: none yet) */
    uint32_t k = (uint32_t)(uintptr_t)sp->fn * 31u + (uint32_t)(uintptr_t)sp->p * 7u + (uint32_t)sp->lo * 131u +
                 (uint32_t)sp->hi * 8191u + (uint32_t)(uintptr_t)face * 524287u + (uint32_t)(uintptr_t)uf.f * 3u +
                 song.sel * 977u + TSEL->eng_req * 4u + ux.neon;   /* (MOD DEST: the track's engine; the theme's faces) */
    int32_t v;
    char t[16];
    const char *u;
    c &= 3u;
    if (k == key[c] && got[c] <= 2u)
        return got[c];
    key[c] = k;
    got[c] = 0;
    for (v = sp->lo; v <= sp->hi && got[c] < 2u; v++) {   /* (at most a few hundred values, once) */
        uint32_t f;
        sp->fn(sp, v, t, &u);
        if ((f = face(t, u)) > got[c])
            got[c] = f;
    }
    return got[c];
}
#include "ui_blocks.c"                                 /* Skiff: STYLE BLOCKS' header and card (draw_head_blocks, draw_column_blocks) */

static void draw_head(void) { draw_head_blocks(); }   /* (U2: BLOCKS, the only look: ui_blocks.c) */
/* full redraw: the strips (header, cards, panel, footer) cover the rest; only the BG between them is filled */
static void draw_frame(void)
{
    uint32_t i, ch = card_h(), gy = graph_y(), gh = graph_h();   /* (MENU > LARGE: tall cards, a strip) */
    lcd_fill(0, H_HEAD, 240, Y_LABEL - H_HEAD, T_BG);
    lcd_fill(0, Y_LABEL + ch, 240, gy - Y_LABEL - ch, T_BG);
    lcd_fill(0, gy + gh, 240, foot_y() - gy - gh, T_BG);
    lcd_fill(0, Y_LABEL, (uint32_t)CARD_X(0), ch, T_BG);
    for (i = 0; i < 4u; i++)                            /* right of each card */
        lcd_fill((uint32_t)(CARD_X(i) + CARD_W), Y_LABEL, i < 3u ? (uint32_t)(CARD_X(i + 1u) - CARD_X(i) - CARD_W) :
                 240u - (uint32_t)(CARD_X(i) + CARD_W), ch, T_BG);
}

/* MOTION on the cards (#63: values that move on their own): the selected track's motion-driven parameters
 * (motion.c motion_mask: PLAY ON and an event for the id), once per draw_columns; card_mot_next is set just
 * before a draw_column of a track parameter (card_mot_of) and taken by it; card_mot: the cards drawn with it. */
static uint32_t card_mot_mask[(P_COUNT + 31u) / 32u];
static uint8_t card_mot_next, card_mot;
static void card_mot_of(const int16_t *vp)
{
    uintptr_t id = ((uintptr_t)vp - (uintptr_t)TSEL->p) / sizeof *vp;   /* (not the track's: huge) */
    card_mot_next = (uint8_t)(id < P_COUNT && ((card_mot_mask[id / 32u] >> (id % 32u)) & 1u));
}

/* MENU > LARGE, a tall card (ui.c LK_TALL; 57 x LG_CARD_H): which knob it is (a "K1" pill in M, the keycap's colours,
 * the accent while it turns, DIM when the knob does nothing here) and the icon at the right of it; the label in M
 * centred (S when M is too wide); the value centred in L, its figures twice the height of M (M, then S, when L is too
 * wide or the sparse L face lacks a glyph: gen_aa_font.py L_CHARS); the unit (S) centred under it; the gauge. The
 * value snaps (no rolling digits). MONO, the knob just turned: the label inverted as on the small cards. */
#ifndef LARGE_HOOK
#define LARGE_HOOK(s, why) ((void)0)                    /* host: a tall card's value and its face (0 L, 1 M: a glyph not
                                                         * in L, 2 M: L too wide, 3 S) */
#endif
#define LG_MY 4                                         /* the K pill: rows 4..21 */
#define LG_MH 18
#define LG_LY 24                                        /* the label's line: M capitals rows 28..38 */
#define LG_VB 49                                        /* the value's band: L capitals rows 49..69 */
#define LG_UY 73                                        /* the unit's line (S capitals rows 76..84) */
#define LG_GY 92                                        /* the gauge, 3 px */
/* a tall card's value s in L where the sparse L face has its glyphs and it fits with 3 px either side, else M, else S
 * (0, 1, 2; its unit is on a line of its own) */
static uint32_t tall_face(const char *s, const char *unit)
{
    (void)unit;
    if (large_face_has(s) && ink_w(&AF_L, s) + (ux.neon ? cv_fx_w(&AF_L) : 0) <= COL_W - 6)   /* ((Skiff N2) NEON: */
        return 0u;                                                                             /* slanted, the wider) */
    return ink_w(&AF_M, s) + (ux.neon ? cv_fx_w(&AF_M) : 0) <= COL_W - 6 ? 1u : 2u;
}
static void draw_column_tall(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc, int32_t ratio,
                             uint32_t icon, int hot, int mot, int32_t kid, const card_span_t *sp)
{
    uint16_t lc = hot ? T_ACCENT : mot ? T_THEME : T_MID;
    const aafont_t *lf = &AF_M, *vf = &AF_L;
    int32_t ly = LG_LY, vy, lw = 0, mw;
#if BOARD_SMK37
    char k[3] = {'K', (char)('5' + c), 0};          /* (SMK-37 Pro: KNOB 1..4 are K5..K8, SMK37_UX.md question 10) */
#else
    char k[3] = {'K', (char)('1' + c), 0};
#endif
    uint32_t ic = mot ? ICON_X_MOTION : icon, isz = 16u;
    cv_begin(COL_W, LG_CARD_H, T_BG);
    cv_rrect(0, 0, COL_W, LG_CARD_H, 4, T_SURF, T_BG);
    {   /* the knob: "K1" .. "K4" on a pill */
        uint16_t fill = hot ? T_ACCENT : label[0] || val[0] ? T_KEY : T_DIM;
        mw = ink_w(&AF_M, k) + 10;
        cv_rrect(5, LG_MY, mw, LG_MH, 5, fill, T_SURF);
        GFX_HOOK_ALIGN(5, LG_MY, 5 + mw, LG_MY + LG_MH, AL_HV, "large card knob pill");
        cv_text_in(5, LG_MY + CAP_IN(M, LG_MH), mw, &AF_M, k, T_INK, fill);
    }
    if (!label[0] && !val[0]) {                         /* a knob that does nothing on this page */
        cv_blit((uint32_t)CARD_X(c), Y_LABEL);
        return;
    }
    if (FELUCCA_ICONS && ic != ICON_NONE && label[0] && (ic - ICON_TRK < NTRK || icon_cell(&isz, ic) >= 0)) {
        GFX_HOOK_ALIGN(0, LG_MY, 0, LG_MY + LG_MH, AL_V, "large card icon on its pill's line");
        cv_icon_in(COL_W - 5 - 16, LG_MY, 16, LG_MH, 16, ic, mot ? (hot ? T_ACCENT : T_THEME) : lc, T_SURF);
    }
    if (label[0]) {
        if ((lw = ink_w(lf, label)) > COL_W - 6) {      /* M too wide: S, on the same capitals' middle */
            lf = &AF_S;
            ly = LG_LY + AF_M_CAP_Y + HALF_UP(AF_M_CAP_H - AF_S_CAP_H) - AF_S_CAP_Y;
            lw = ink_w(lf, label);
        }
        if (hot && T_ACCENT == T_MID) {                 /* MONO: the label inverted, the chip 2 px round its ink */
            int32_t cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
            int32_t x0 = HALF_UP(COL_W - lw) - 2;
            cv_rrect(x0, cy - 3, lw + 4, chh + 6, 3, T_ACCENT, T_SURF);
            GFX_HOOK_ALIGN(x0, cy - 3, x0 + lw + 4, cy + chh + 3, AL_HV, "large card label chip (MONO hot)");
            cv_text_in(0, ly, COL_W, lf, label, T_INK, T_ACCENT);
        } else {
            GFX_HOOK_ALIGN(0, 0, COL_W, 0, AL_H, "large card label centred");
            cv_text_in(0, ly, COL_W, lf, label, lc, T_SURF);
        }
    }
    if (kid >= 0) {                                     /* "[OCT+]": the keycap, centred */
        GFX_HOOK_ALIGN(0, LG_VB, COL_W, LG_VB + AF_L_CAP_H, AL_HV, "large card keycap centred");
        cv_keycap(HALF_UP(COL_W - kc_w((uint32_t)kid)), LG_VB + HALF_UP(AF_L_CAP_H - KC_H), (uint32_t)kid,
                  vc == T_ACCENT || vc == T_DIM ? vc : T_KEY, T_INK, T_SURF);
    } else if (val[0]) {
        uint32_t f = tall_face(val, unit), fs = sp->fn ? span_face(c, sp, tall_face) : 0u;
        int why;
        if (fs > f)
            f = fs;                                     /* (the value's own where it needs a smaller one) */
        vf = f == 0u ? &AF_L : f == 1u ? &AF_M : &AF_S;
        why = f == 2u ? 3 : f == 1u ? (!large_face_has(val) ? 1 : 2) : 0;
        LARGE_HOOK(val, why);
        vy = vf == &AF_L ? LG_VB - AF_L_CAP_Y : vf == &AF_M ? LG_VB + HALF_UP(AF_L_CAP_H - AF_M_CAP_H) - AF_M_CAP_Y
                                                          : LG_VB + HALF_UP(AF_L_CAP_H - AF_S_CAP_H) - AF_S_CAP_Y;
        if (ux.neon)                                    /* (Skiff N2) NEON: the value in chrome, slanted (its ink, the slant in
                                                         * it, centred) */
            cv_fx_on(vy, vf, 1);
        if (vf == &AF_S && ink_w(vf, val) > COL_W - 4) {   /* (a value always fits: the lint reports one cut) */
            cv_text_fit(2, vy, vf, val, vc, T_SURF, COL_W - 4);
        } else {
            GFX_HOOK_ALIGN(0, LG_VB, COL_W, LG_VB + AF_L_CAP_H, AL_HV, "large card value centred");
            cv_text_in(0, vy, COL_W, vf, val, vc, T_SURF);
        }
        cv_fx_off();
    }
    if (unit[0]) {
        GFX_HOOK_ALIGN(0, 0, COL_W, 0, AL_H, "large card unit centred");
        cv_text_in(0, LG_UY, COL_W, &AF_S, unit, T_MID, T_SURF);
    }
    if (ux.quark)                                     /* (Skiff Q2) QUARK: the cell's corners, its energy levels */
        qk_corners(0, 0, COL_W, LG_CARD_H, T_DIM, 6);
    if (ratio >= 0 && ux.quark)
        qk_dots(5, LG_GY, COL_W - 10, ratio, 0);
    else if (ratio >= 0 && ux.neon)                   /* (Skiff N3) NEON: a dash bargraph, as the cards' */
        ne_dash(5, LG_GY - 1, COL_W - 10, ratio, 0);
    else if (ratio >= 0 && ux.crypt)                  /* (Skiff) CRYPT: a sword, as the cards' */
        cr_sword(3, LG_GY - 1, COL_W - 6, ratio, hot ? T_ACCENT : vc == T_DIM ? T_DIM : T_THEME, hot ? T_ACCENT : CR_GOLD,
                 T_MID, T_LINE);
    else if (ratio >= 0) {
        int32_t gw = COL_W - 10, fx = ratio * gw / 1000;
        cv_rrect(5, LG_GY, gw, 3, 1, T_BG, T_SURF);
        cv_rrect(5, LG_GY, fx < 3 ? 3 : fx, 3, 1, hot ? T_ACCENT : vc == T_DIM ? T_DIM : T_THEME, T_BG);
    }
    cv_blit((uint32_t)CARD_X(c), Y_LABEL);
}

/* one card = one knob: [icon] LABEL / value unit / gauge on a SURF card, redrawn only when it changed.
 * The value is M, or S when M is too wide (engine names, long ENUMs); the unit S after it. The gauge is
 * a 3 px rounded bar: BG track, THEME fill. The knob just turned (hot): icon, label, value and gauge in
 * the accent. ratio: 0..1000 for the gauge, -1 = no gauge. icon: ICON_* (icons.c), ICON_AUTO = by label;
 * a label too long to share the card with its icon goes without it. A parameter MOTION drives (card_mot_next):
 * the motion icon in place of its own, in the theme colour; a long label moves it left, up to the card's edge;
 * a label with no room for it at all (or FELUCCA_ICONS=0): the label in the theme colour.
 * vr: the value's colour, a VR_* role (ui.c: VR_LIVE the theme's, the accent while its knob is the one just turned) */
static void draw_column(uint32_t c, const char *label, const char *val, const char *unit, uint32_t vr,
                        int32_t ratio, uint32_t icon)
{
    char key[48];
    int hot = c == ui.hot_col && ui.hot_t, named = fmt_named, mot = card_mot_next && label[0], strip, snap;
    uint16_t vc = vr == VR_LIVE ? (hot ? T_ACCENT : T_THEME) : vr == VR_DIM ? T_DIM : vr == VR_ACCENT ? T_ACCENT
                : vr == VR_MID ? T_MID : T_THEME;
    uint32_t kind = large_kind();                       /* MENU > LARGE: tall (draw_column_tall), or the label in M */
    int blocks = kind != LK_TALL;                       /* Skiff: BLOCKS' tile (ui_blocks.c), its values snap; LARGE's
                                                         * tall card: the older one */
    uint8_t sig;
    uint16_t lc = hot ? T_ACCENT : T_MID;
    int32_t x, lx = 5, uw, room = COL_W - 8;
    const aafont_t *vf = &AF_M;
    uint32_t n, kn;
    int32_t kid = kc_tag(val, &kn);
    card_span_t sp = card_span;                         /* (Felucca #251: what the knob shows, this card only) */
    card_span.fn = 0;
    fmt_named = 0;                                      /* (params.c: a name, for this card only) */
    card_mot_next = 0;                                  /* (the same: MOTION, this card only) */
    card_mot = (uint8_t)((card_mot & ~(1u << c)) | (uint32_t)mot << c);
    if (str_eq(unit, label))
        unit = "";                                      /* "BPM 124 BPM", "USB OFF USB": the label says it */
    if (icon == ICON_AUTO)
        icon = icon_for_label(label);
    str_cpy(key, label, 12);                            /* cache key: texts + colour + gauge */
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), val, 14);
    str_cpy(key + str_len(key), "|", 2);
    str_cpy(key + str_len(key), unit, 8);
    n = str_len(key);
    /* every RGB565 bit: status colours can change with identical text */
    key[n] = (char)('A' + (vc & 15u));
    key[n + 1] = (char)('A' + ((vc >> 4) & 15u));
    key[n + 2] = (char)('A' + ((vc >> 8) & 15u));
    key[n + 3] = (char)('A' + (vc >> 12));
    key[n + 4] = (char)('A' + (ratio < 0 ? 0 : 1 + ratio % 32));   /* ((Skiff) the gauge exactly: in 20ths its end moved */
    key[n + 5] = (char)('A' + (ratio < 0 ? 0 : ratio / 32));       /* a pixel under the same text, the card not redrawn) */
    key[n + 6] = (char)(icon == ICON_NONE ? '~' : '!' + icon % 90u);
    key[n + 7] = (char)('0' + hot + 2 * mot + 4 * (int)kind);
    key[n + 8] = 0;
    uw = unit[0] ? text_w(&AF_S, unit) + 3 : 0;
    if (text_w(vf, val) + uw > room)
        vf = &AF_S;
    sig = (uint8_t)str_hash(str_hash(song.sel + TSEL->eng_req * 4u + ux.gen * 64u, label), unit);
    snap = ui.force || sig != ui.roll[c].sig;           /* what the value is of: label, unit, track, engine, palette */
    if (kind == LK_TALL || blocks)
        ui.roll[c].from[0] = 0;                         /* (a tall card: no rolling digits) */
    strip = !snap && str_eq(key, ui.col[c]);
    if (strip && !ui.roll[c].from[0])
        return;
    if (strip) {                                        /* rolling: the value strip only */
        cv_begin(COL_W, ROLL_H, T_SURF);
        cv_oy = -ROLL_Y;
    } else {
        char ov[16];                                    /* the value drawn before (in the cache key) */
        const char *k = ui.col[c];
        uint32_t i = 0;
        while (*k && *k != '|')
            k++;
        while (*k && k[1] && k[1] != '|' && i + 1u < sizeof ov)
            ov[i++] = *++k;
        ov[i] = 0;
        ui.roll[c].sig = sig;
        if (!str_eq(ov, val))
            roll_note(c, ov, val, snap || named || vf != &AF_M || kid >= 0 || kind == LK_TALL || blocks);
        else if (snap)
            ui.roll[c].from[0] = 0;
        str_cpy(ui.col[c], key, sizeof ui.col[c]);
        if (kind == LK_TALL) {
            draw_column_tall(c, label, val, unit, vc, ratio, icon, hot, mot, kid, &sp);
            return;
        }
        if (blocks) {
            draw_column_blocks(c, label, val, unit, vc, ratio, icon, hot, mot, kid, kind);
            return;
        }
        cv_begin(COL_W, COL_H, T_BG);
        cv_rrect(0, 0, COL_W, COL_H, 4, T_SURF, T_BG);
    }
    if (label[0] || val[0]) {
        if (!strip && label[0]) {
            /* MENU > LARGE on a list / graph page or a layer (LK_LABEL): the label in M where it fits the card (its
             * capitals rows 5..15, the icon on them when it fits too), else in S as without LARGE */
            const aafont_t *lf = kind == LK_LABEL && 5 + text_w(&AF_M, label) <= COL_W - 2 ? &AF_M : &AF_S;
            int32_t lw = text_w(lf, label), ix = 5, ly = lf == &AF_M ? 1 : 3;
            int32_t cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y), chh = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H;
            uint32_t ic = mot ? ICON_X_MOTION : icon;
            if (mot && ix + ICON_CELL + ICON_GAP + lw > COL_W - 2)
                ix = COL_W - 2 - lw - ICON_GAP - ICON_CELL;   /* a long label: MOTION's icon moves left */
            if (FELUCCA_ICONS && ic != ICON_NONE && ix >= 1 && ix + ICON_CELL + ICON_GAP + lw <= COL_W - 2) {
                GFX_HOOK_ALIGN(ix, cy, ix, cy + chh, AL_V, "card icon on its label's line");
                lx = ix + cv_icon_in(ix, cy, 0, chh, ICON_CELL, ic,   /* its ink on the label's line */
                                     mot ? (hot ? T_ACCENT : T_THEME) : lc, T_SURF) + ICON_GAP;
            }
            else if (mot)                               /* the label from x 19 (5 + 12 + 2) */
                lc = hot ? T_ACCENT : T_THEME;          /* no room for MOTION's icon: the label says it */
            if (hot && T_ACCENT == T_MID) {             /* MONO: no colour tells the hot knob: its label inverted, */
                int32_t b[4], w;                        /* the chip 2 px round its ink (M: 3 above, 2 below) */
                text_ink(lf, label, b);
                w = b[2] - b[0] + 4;
                if (w > COL_W + 1 - lx - b[0]) w = COL_W + 1 - lx - b[0];
                cv_rrect(lx + b[0] - 2, cy - 3, w, lf == &AF_M ? chh + 5 : 15, 3, T_ACCENT, T_SURF);
                GFX_HOOK_ALIGN(lx + b[0] - 2, cy - 3, lx + b[0] - 2 + w, lf == &AF_M ? cy + chh + 2 : 18, AL_HV,
                               "card label chip (MONO hot)");
                cv_text_fit(lx, ly, lf, label, T_INK, T_ACCENT, COL_W - 2 - lx);
            } else
                cv_text_fit(lx, ly, lf, label, lc, T_SURF, COL_W - 2 - lx);
        }
        if (kid >= 0)                                   /* "[OCT+]": the keycap (accent: it would act; DIM: it would not) */
            x = cv_keycap(5, 20, (uint32_t)kid, vc == T_ACCENT || vc == T_DIM ? vc : T_KEY, T_INK, T_SURF);
        else if (vf == &AF_M) {
            if (!ui.roll[c].from[0] && val[0])
                GFX_HOOK_ALIGN(0, ROLL_Y, COL_W, ROLL_Y + ROLL_H, AL_V, "card value in its roll window");
            x = roll_text(c, 5, 17, val, vc);
        } else
            x = cv_text_fit(5, 20, vf, val, vc, T_SURF, room - uw);
        if (unit[0]) {
            GFX_HOOK_ALIGN(0, 0, 0, vf == &AF_M ? 17 + AF_M.asc : 20 + AF_S.asc, AL_B, "card unit on its value's baseline");
            cv_text_on(x + 3, 20, &AF_S, unit, T_MID, T_SURF);     /* on the value's baseline (S) */
        }
        if (!strip && ratio >= 0) {
            int32_t gw = COL_W - 10, fx = ratio * gw / 1000;
            cv_rrect(5, 38, gw, 3, 1, T_BG, T_SURF);
            cv_rrect(5, 38, fx < 3 ? 3 : fx, 3, 1, hot ? T_ACCENT : vc == T_DIM ? T_DIM : T_THEME, T_BG);
        }
    }
    cv_oy = 0;
    cv_blit((uint32_t)CARD_X(c), Y_LABEL + (strip ? ROLL_Y : 0));
}

/* an action's column (act_cols): picked, the OCT+ keycap (accent while it would do something); else "--" */
static void draw_act_column(uint32_t c, const char *label, uint32_t vr, uint32_t icon)   /* icon: ICON_AUTO = by label */
{
    int picked = act_col() == c + 1u;
    draw_column(c, label, picked ? "[OCT+]" : "--", "", picked ? (act_ready() ? VR_ACCENT : VR_DIM) : vr, -1, icon);
}

/* (Skiff U3, N5) a card of a page that is there but does nothing on the track's engine: dimmed. On the hits (DRUM,
 * DRUM-X) (#9) LFO's PHASE and FADE (no retrigger: it runs free across the hits; (2.0) LFO DEST's PIT FLT SHP: MOD's
 * grid, mod_dead); DRUM's KICK on a model kit (80 10 66 55 77: every lane its kit's own) */
static int card_dead(const page_t *pg, uint32_t c)
{
    const track_t *t = TSEL;
    uint32_t id = pg->id[c & 3u];
    if (ENGINES[t->eng_req % NENGINES]->oneshot && (id == P_LPHASE || id == P_LFADE || id == P_LTRIG))
        return 1;                                       /* ((#16) TRIG too: the hits' LFO runs free) */
    if (id == P_LRATE && t->p[P_LSYNC])                 /* (#16) LFO 2 SYNC on: RATE does nothing */
        return 1;
    if (id == P_SPRD && trk_vmode(t) != V_POLY && trk_vmode(t) != V_UNISON)   /* ((1.3) SPREAD: POLY / UNISON only) */
        return 1;
    return pg->scope == SC_ENGINE && t->eng_req % NENGINES == ENGI_DRUM && id == P_E6 &&
           drum_kit_plays(t->p[P_E0]) != DK_STD;
}

/* action pages: the footer's first row says what OCT+ and OCT- do ("OCT+ LOAD   OCT- BACK") */
static void foot_hint(char *a, char *b)
{
    uint32_t c = act_col();
    str_cpy(a, "OCT+ ", 8);
    str_cpy(a + 5, c ? act_name(c - 1u) : "--", 19);   /* ((2.0) CLEAR 3 UNUSED, COPY 06 TO 22 AT F: a 24-byte a) */
    str_cpy(b, ui.act || ui.ev_del ? "OCT- CANCEL" : "OCT- HOME", 16);   /* ((Skiff U2) where it goes: HOME, not "BACK") */
}

/* the sound's name on the track: a user preset or the engine's preset (b holds 16); (FM6 LAB) an FM6 track on its own
 * patch (edited, INIT's): the patch's name ("INIT VOICE"); ((2.0, #35) DRUM on a user kit: its slot's name, "MY KIT") */
static void sound_name(const track_t *t, char *b)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t u = t->eng_req % NENGINES == ENGI_DRUM ? drum_user(t->p) : 0u;
    b[0] = 0;
    if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (u && usr_nz[u - 1u])
        smp_user_name(u - 1u, b);
    else if (t->eng_req % NENGINES == ENGI_FM6 && t->p[P_E7] == FM6_OWN) {
        uint32_t n = 10;
        memcpy(b, fm6_patch[trk_index(t) % NTRK] + FP_NAME, 10);
        while (n && b[n - 1u] == ' ')
            n--;
        b[n] = 0;
    } else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 16);
}

/* (Skiff) BLOCKS: where the footer's hints end, the page's title (and its icon) right of them on the one line */
static int32_t foot_hint_r(const char *ti)
{
    return 232 - text_w(&AF_S, ti) - (ui.home || page_icon(cur_page()) == ICON_NONE ? 0 : 16) - 12;
}
/* (#22) HOME shows its lanes, which name every track's sound (ui_home.c); MENU > LARGE's tall cards and the scope (HOME
 * tapped on HOME) have none, so there the footer names the sound instead of HOLD [HOME] SHIFT (both force a redraw) */
static int home_lanes(void) { return ui.home && !ui.home_scope && large_kind() != LK_TALL; }
/* (2.0, D 2A) what comes back, how (ret: 1 + RETURN) and OCT+ KEEP, side by side from the left (design/mockups/
 * v2_mutes.png): MOMENTARY's footer, FX's with its macros out, PERFORM's with a throw out (ui_layer.c draw_layer) */
static void ret_hints(uint32_t ret, int32_t y)
{
    khint_t kh[2] = {{KC_LETGO, RET_SAY[(ret - 1u) % 3u]}, {KC_OCTUP, "KEEP"}};
    cv_key_row(8, 8 + kh_w(KC_LETGO, kh[0].act) + 14 + kh_w(KC_OCTUP, "KEEP"), y - KC_TY, kh, 2, 3u, T_BG);
}
static void draw_foot(void)
{
    char s[48], pn[16], ti[20];
    const track_t *t = TSEL;
    uint32_t sig;
    const page_t *pg = cur_page();
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    const char *ename = e->name;
    int32_t x, ly = foot_line_y();                     /* (BLOCKS: the one line) */
    int tiles = !ui.home && !grid_on() &&              /* ((Skiff 2.0) NOTES, FEEL, PATTERN: their hints; (issue #3) not */
                (pg->graph == GR_ROLL || pg->graph == GR_FEEL || pg->graph == GR_STEPS);   /* under HOME's */
    int hints = act_cols() || ((grid_on() || tiles) && !ui_hints_off);   /* (the line says them, not the
                                                       * sound; (Skiff U4) MENU > HINTS OFF: the sound) */
    uint32_t ret = mom_down() && mom_held(0) ? 1u + ret_mode() : 0u;   /* ((2.0, D 2A) MOMENTARY: how its values */
                                                       /* come back, OCT+ KEEP, for the whole line */
    sound_name(t, pn);
    if (ui.home)
        ti[0] = 0;                                     /* ((Skiff U4, L5) the header names the page: here only where it */
    else                                               /* is in its family, "2/2" (was "ENV DEST 2/2", "HOME") */
        page_pos(ti);                                  /* (a sheet too, #7) */
    str_cpy(s, ename, sizeof s);
    s[str_len(s) + 1u] = 0;
    s[str_len(s)] = (char)('1' + song.sel);
    str_cpy(s + str_len(s), pn, 16);
    str_cpy(s + str_len(s), ti, sizeof ti);
    {   /* step markers: the playhead only when it is in the shown bank, the cursor only in SEQ */
        uint32_t ph = song.playing && t->seq_idx / 16u == ui.bank ? t->seq_idx : 0xFFu;
        sig = str_hash(0x9E3779B9u, s) + ph * 97u + (song.seq_mode ? ui.cursor : 0xFFu) * 3001u + steps_hash(t) +
              (ui.home ? 0u : page_icon(pg)) * 7121u +
              ui.bank * 7u + (uint32_t)t->p[P_SLEN] * 13u + list_mode() * 9173u + (ui.edge_t != 0) * 5501u +
              ui_hints_off * 3301u;
    }
    if (act_cols()) {                                  /* the hint, and whether OCT+ would act */
        char ha[24], hb[16];
        foot_hint(ha, hb);
        sig += str_hash(str_hash(act_ready() ? 7u : 3u, ha), hb) + (uint32_t)ui.act2 * 7717u + (uint32_t)ui.ev_del * 1931u;
        if (pg->graph == GR_SONG)                      /* ((Skiff phase 5) SONG's: the whole of it, EDIT's; (2.0, #30) */
            sig += str_hash(5u, song_act_name()) + (uint32_t)bank_used(song.sel, sg.entry) * 6007u +   /* the entry's */
                   (uint32_t)lists.snd[song.sel % NTRK][sg.entry % NENT][0] * 131u +                     /* sound */
                   (uint32_t)lists.snd[song.sel % NTRK][sg.entry % NENT][1] * 7u + up_gen * 0x3BD1u;
    }
    if (grid_on())
        sig += 0x51EDu + (uint32_t)black_held(GK_ACC) * 977u;
    if (tiles)                                         /* (Skiff U1) the REC hint comes and goes with the arming */
        sig += ((song.rec >> song.sel) & 1u) * 4421u;
    if (ui.ml.on) {                                    /* ((1.3) MIDI LEARN's line) */
        char ln[24];
        ml_line(ln);
        sig += str_hash(0x4C45u, ln);
    }
    sig += ret * 0x2E7B5u;
    if (!ui.force && sig == ui.foot_sig)
        return;
    ui.foot_sig = sig;
    cv_begin(240, foot_h(), T_BG);
    if (ux.neon)                                       /* (Skiff N3) NEON: a neon rule over it */
        ne_rule(0, 1);
    if (ux.quark)                                      /* (Skiff Q2) QUARK: a dotted rule */
        qk_rule(0);
    if (ui.ml.on && !act_cols()) {                    /* ((1.3) MIDI LEARN: what is picked and its CC, OCT+ DONE at the
                                                       * right, for the whole line (midi_learn.c ml_line) */
        char ln[24];
        int32_t w = kh_w(KC_OCTUP, "DONE");
        ml_line(ln);
        cv_free_text(8, ly, &AF_S, ln, T_ACCENT, T_BG, 232 - w - 10 - 8);
        cv_key_hint(232 - w, ly - KC_TY, KC_OCTUP, "DONE", 1, T_BG);
        cv_blit(0, foot_y());
        return;
    }
    if (ret) {                                         /* ((2.0, D 2A) MOMENTARY: LET GO: BACK OVER 1 BAR, OCT+: KEEP) */
        ret_hints(ret, ly);
        cv_blit(0, foot_y());
        return;
    }
    if (act_cols() && pg->graph == GR_SONG) {         /* (Skiff phase 5) SONG: what OCT+ does, then EDIT CLEAR (not with
                                                       * a pick), side by side (the mockup's), the title right */
        khint_t kh[1] = {{KC_OCTUP, song_act_name()}};   /* ((Skiff U2) CLEAR: KNOB 2's, no EDIT) */
        uint32_t n = 1u;
        int32_t r = foot_hint_r(ti), w = kh_w(kh[0].key, kh[0].act);
        char sn[24];
        cv_key_row(8, 8 + w < r ? 8 + w : r, ly - KC_TY, kh, n,
                   (act_ready() ? 1u : 0u) | (bank_used(song.sel, sg.entry) ? 2u : 0u), T_BG);
        str_cpy(sn, "SOUND ", sizeof sn);              /* ((2.0, #30, the user's pick 2A) no pick: the entry's sound */
        esnd_name(lists.snd[song.sel % NTRK][sg.entry % NENT], sn + 6);   /* named after it) */
        if (!song_pick() && sn[6] && 8 + w + 10 < r)
            cv_free_text(8 + w + 10, ly, &AF_S, sn, T_MID, T_BG, r - (8 + w + 10));
    } else if (act_cols()) {                          /* the OCT+ / OCT- hint ((Skiff U2) EDIT's NAME / DELETE gone) */
        char ha[24], hb[16];
        khint_t kh[2];
        foot_hint(ha, hb);                            /* "OCT+ LOAD", "OCT- HOME": their words */
        kh[0] = (khint_t){KC_OCTUP, ha + 5};
        kh[1] = (khint_t){KC_OCTDN, hb + 5};
        {                                             /* (Skiff) the hints' room on the one line: the title's name */
            uint32_t j;                               /* gives way first (the header says it), its n/m stays */
            int32_t need = 6 + kh_w(kh[0].key, kh[0].act) + kh_w(kh[1].key, kh[1].act);
            char *sp = 0;
            for (j = 0; ti[j]; j++)
                if (ti[j] == ' ')
                    sp = &ti[j];
            if (8 + need > foot_hint_r(ti) && sp)
                str_cpy(ti, sp + 1, sizeof ti);
        }
        {
            int32_t x1 = foot_hint_r(ti), w = kh_w(kh[0].key, kh[0].act) + 8 + kh_w(kh[1].key, kh[1].act);
            if (pg->graph == GR_BANK && 8 + w < x1)    /* ((Skiff phase 5) the pool view: side by side, the mockup's) */
                x1 = 8 + w;
            cv_key_row(8, x1, ly - KC_TY, kh, 2, act_ready() ? 3u : 2u, T_BG);
        }
    } else if (grid_on() && !ui_hints_off) {          /* row 1: the page, and what the keys do */
        char b[16];
        uint32_t all, off = step_off(&all, 16);         /* ((2.0, #44) a chain's pages counted on: PAGE 5/8) */
        str_cpy(b, "PAGE ", sizeof b);
        fmt_kn(b + 5, (int32_t)(off + ui.bank) + 1, (int32_t)all);
        x = cv_text(8, ly, &AF_S, b, T_THEME) + 10;   /* (Skiff) the page, then the keys, on the line */
        if (black_held(GK_ACC))
            cv_text(x, ly, &AF_S, "ACCENT", T_ACCENT);
        else
            cv_key_hint(x, ly - KC_TY, KC_KEYS, "STEPS", 1, T_BG);
    } else if (tiles && !ui_hints_off) {               /* (Skiff 2.0) NOTES, FEEL: PRESETS the cursor (SHIFT + PRESETS, the
                                                        * selection: SHIFT's map); NOTES' keys (REC off: they pick, on:
                                                        * they write); PATTERN's PRESETS */
        if (pg->graph == GR_STEPS) {
            cv_key_hint(8, ly - KC_TY, KC_PRESETS, "DIR", 1, T_BG);
        } else {
#if BOARD_SMK37
            x = cv_key_hint(8, ly - KC_TY, KC_K4, "STEP", 1, T_BG) + 10;   /* (K4 is PRESETS) */
#else
            x = cv_key_hint(8, ly - KC_TY, KC_PRESETS, "STEP", 1, T_BG) + 10;
#endif
            if (pg->graph == GR_ROLL)
                cv_key_hint(x, ly - KC_TY, KC_KEYS, (song.rec >> song.sel) & 1u ? "WRITE" : "PICK", 1, T_BG);
        }
    }
    {   /* the track's cushion, engine name, sound, page title at the right with its icon before it (MIXER, SONG,
         * CHANCE, AUTOMATION); icons: their ink on the names' capitals */
        uint32_t pi = ui.home ? ICON_NONE : page_icon(pg);
        int32_t r = 232, tx, ex, need = text_w(&AF_S, pn), ew = text_w(&AF_S, ename);
        ex = 8 + 16 + 5 + (ew < 80 ? ew : 80);        /* (where the engine's name ends) */
        tx = r - text_w(&AF_S, ti) - (pi != ICON_NONE ? 16 : 0);
        (void)need;
        if (home_lanes() && !ui_hints_off)             /* Skiff BLOCKS on HOME: the lanes name the sounds; the hint */
#if BOARD_SMK37
            cv_free_hint(8, ly, "HOLD [KNOB BANK] SHIFT", T_MID, T_BG, 160);   /* (SMK37_UX.md extra buttons A) */
#else
            cv_free_hint(8, ly, "HOLD [HOME] SHIFT", T_MID, T_BG, 160);
#endif
        if ((home_lanes() && !ui_hints_off) || hints) {   /* (an action page's or a grid's hints: the title) */
            if (pi != ICON_NONE) {
                GFX_HOOK_ALIGN(0, ly + AF_S_CAP_Y, 0, ly + AF_S_CAP_Y + AF_S_CAP_H, AL_V, "footer icons on the names' line");
                cv_icon_in(tx, ly + AF_S_CAP_Y, 0, AF_S_CAP_H, 12, pi, T_MID, T_BG);
            }
            cv_text_r(r, ly, &AF_S, ti, ui.edge_t ? T_ACCENT : T_MID, T_BG);   /* ((U4, F7) an end) */
            cv_blit(0, foot_y());
            return;
        }
        x = 8;                                         /* (Skiff) the selected track's cushion (not the header's) */
        GFX_HOOK_ALIGN(0, ly + AF_S_CAP_Y, 0, ly + AF_S_CAP_Y + AF_S_CAP_H, AL_V, "footer icons on the names' line");
        x += cv_icon_in(x, ly + AF_S_CAP_Y, 0, AF_S_CAP_H, 16, trk_icon(song.sel, 1), T_ACCENT, T_BG) + 5;
        x = cv_text_fit(x, ly, &AF_S, ename, T_THEME, T_BG, 80);
        if (list_mode())                               /* ((Skiff U2) PRESETS lists the favourites; phase 9: a type) */
            x = cv_text(x + 6, ly, &AF_S, LIST_NAME[list_mode()], T_ACCENT);
        cv_free_text(x + 10, ly, &AF_S, pn, T_TEXT, T_BG, tx - 12 - (x + 10));
        if (pi != ICON_NONE) {
            GFX_HOOK_ALIGN(0, ly + AF_S_CAP_Y, 0, ly + AF_S_CAP_Y + AF_S_CAP_H, AL_V,
                           "footer icons on the names' line");
            cv_icon_in(tx, ly + AF_S_CAP_Y, 0, AF_S_CAP_H, 12, pi, T_MID, T_BG);
        }
        cv_text_r(r, ly, &AF_S, ti, ui.edge_t ? T_ACCENT : T_MID, T_BG);
    }
    cv_blit(0, foot_y());
}
/* the EDIT layer's cards (ui_layer.c): ENG (the engine), No. (its sounds: KNOB 2's list), FAV, an empty card */
static void engine_columns(void)
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES;
    char val[8], u[8];
    fmt_int(val, (int32_t)cur + 1);
    fmt_kn(u, -1, (int32_t)total);
    draw_column(0, "ENG", ENGINES[e]->name, "", VR_LIVE, (int32_t)eng_rank(e) * 1000 / (NENG_SHOWN > 1 ? NENG_SHOWN - 1 : 1),
                engine_icon(ENGINES[e]->name));
    draw_column(1, "No.", val, u, VR_LIVE, total > 1u ? (int32_t)(cur * 1000u / (total - 1u)) : 0, ICON_NONE);
    draw_column(2, "FAV", preset_favorite() ? "ON" : "OFF", "", VR_LIVE, -1, ICON_X_STAR);
    draw_column(3, "", "", "", VR_THEME, -1, ICON_NONE);
}
static void draw_columns(void)
{
    uint32_t c;
    char val[12];
    const char *unit;
    (void)motion_mask(song.sel, card_mot_mask);       /* (PLAY OFF: no scan) */
    if (ui.home) {
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d = home_param(c, &vp);
            param_format(d, *vp, val, &unit);
            card_mot_of(vp);
            card_span_of(d);
            draw_column(c, d->label, val, unit, VR_LIVE, RATIO(d, enum_rank(d, *vp)), param_icon(d, *vp));
        }
        return;
    }
    if (cur_page()->graph == GR_FEEL) {                /* (Skiff 2.0) FEEL: CHANCE RATCHET NUDGE TYPE of the selection's
                                                        * first step (none: the cursor's; DIM there, or a value at rest) */
        static const char *const FL[4] = {"CHANCE", "RATCHET", "NUDGE", "TYPE"};
        static const uint16_t FI[4] = {ICON_PROB, ICON_X_REPEAT, ICON_TIME, ICON_AUTO};
        uint32_t f = sel_first(TSEL);
        const step_t *cs = &TSEL->step[f < NSTEP ? f : ui.cursor];
        for (c = 0; c < 4u; c++) {
            int32_t v = feel_get(cs, c);
            const char *u = feel_text(val, cs, c);
            if (c == 0u && !step_starts(cs)) {        /* (no note there: no chance to speak of) */
                str_cpy(val, "--", sizeof val);
                u = "";
            }
            draw_column(c, FL[c], val, u, f < NSTEP && (c == 0u || v != (c == 1u ? 1 : 0)) ? VR_LIVE : VR_DIM, -1, FI[c]);
        }
        return;
    }
    if (cur_page()->graph == GR_EVENTS) {              /* AUTOMATION (ui_events.c): ROW STEP PARAM VALUE */
        const track_t *t = TSEL;
        uint8_t idx[MOTION_MAX];
        uint32_t n = ev_rows(idx), r = ev_row(n), step, id;
        int16_t v;
        char nm[12];
        const param_desc_t *d;
        if (ui.ev_top) {                               /* ((Skiff U3, R5) the top row, the page's own: PLAY, CLEAR) */
            draw_column(0, "ROW", "ALL", "", VR_LIVE, -1, ICON_AUTO);
            draw_column(1, "PLAY", motion_enabled(t) ? "ON" : "OFF", "", VR_LIVE, -1, motion_icon());
            draw_column(2, "", "", "", VR_THEME, -1, ICON_NONE);
            draw_act_column(3, "CLEAR", motion_count(t) ? VR_THEME : VR_DIM, ICON_X_MOTION_DEL);
            return;
        }
        if (r < n) {
            const motion_event_t *e = &motion.event[idx[r]];
            fmt_kn(val, (int32_t)r + 1, (int32_t)n);
            step = e->place & 63u;
            id = MOTION_ID(e);
            v = e->value;
        } else {
            ev_fix();
            str_cpy(val, "ADD", sizeof val);
            step = ui.ev_step;
            id = ui.ev_id;
            v = id < P_COUNT ? motion_base_value(t, id) : 0;
        }
        draw_column(0, "ROW", val, "", VR_LIVE, -1, ICON_AUTO);
        fmt_int(val, (int32_t)step + 1);
        draw_column(1, "STEP", val, "", r < n || step < (uint32_t)t->p[P_SLEN] ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        if (id >= P_COUNT || !ev_id_ok(t, id)) {
            draw_column(2, "PARAM", "--", "", VR_DIM, -1, ICON_AUTO);
            draw_column(3, "VALUE", "--", "", VR_DIM, -1, ICON_AUTO);
            return;
        }
        ev_name(t, id, nm);
        {   /* ((Skiff U4) the words spelled out: ENV ATTACK's ENV on the label's line, ATTACK the value) */
            char pl[4] = {nm[0], nm[1], nm[2], 0};
            int pre = !memcmp(nm, "ENV ", 4) || !memcmp(nm, "LFO ", 4);
            draw_column(2, pre ? pl : "PARAM", pre ? nm + 4 : nm, "", VR_LIVE, -1, ICON_AUTO);
        }
        d = track_desc(t, id);
        param_format(d, v, val, &unit);
        if (r < n && ui.ev_del) {                     /* ((Skiff U2) past the lowest value: DELETE, was EDIT) */
            draw_column(3, "DELETE", "[OCT+]", "", VR_ACCENT, -1, ICON_AUTO);
            return;
        }
        card_span_of(d);
        draw_column(3, r == n ? "VALUE" : motion.event[idx[r]].lock ? "LOCK" : "AUTO", val, unit,
                    r < n ? VR_LIVE : VR_DIM, RATIO(d, enum_rank(d, v)), param_icon(d, v));
        return;
    }
    if (cur_page()->graph == GR_LMIX)                  /* ((2.0, #36) LANE MIX: a key struck picks its lane, as the grid's */
        lane_follow(page_lane_p());                    /* keys and DRUM-X's sheet do (ui_dx.c) */
    if (cur_page()->graph == GR_LANES || cur_page()->graph == GR_LMIX)   /* ((Skiff U3, R6) LANES: KNOB 1 the lane, by its
                                                                         * name; (2.0) LANE MIX's) */
        draw_column(0, "LANE", drum_lane_name(TSEL, page_lane()), "", VR_LIVE, (int32_t)page_lane() * 1000 / 7, ICON_AUTO);
    if (cur_page()->scope == SC_TRK) {                 /* LEVEL PAN REV MUTE of the selected track */
        const track_t *t = TSEL;
        uint32_t lvl = trk_level(song.sel);
        param_format(&TP[P_LEVEL], (int32_t)lvl, val, &unit);   /* (0: OFF) */
        card_mot_of(&TSEL->p[P_LEVEL]);
        card_span_of(&TP[P_LEVEL]);
        draw_column(0, "LEVEL", val, unit, lvl && !t->p[P_MUTE] ? VR_LIVE : VR_DIM, (int32_t)lvl * 1000 / 127, ICON_AUTO);
        param_format(&TP[P_PAN], t->p[P_PAN], val, &unit);
        card_mot_of(&TSEL->p[P_PAN]);
        card_span_of(&TP[P_PAN]);
        draw_column(1, "PAN", val, unit, VR_LIVE, RATIO(&TP[P_PAN], t->p[P_PAN]), param_icon(&TP[P_PAN], t->p[P_PAN]));
        param_format(&TP[P_REV], t->p[P_REV], val, &unit);
        card_mot_of(&TSEL->p[P_REV]);
        card_span_of(&TP[P_REV]);
        draw_column(2, "REVERB", val, unit, VR_LIVE, RATIO(&TP[P_REV], t->p[P_REV]), ICON_AUTO);
        draw_column(3, "MUTE", t->p[P_MUTE] ? "ON" : "OFF", "", t->p[P_MUTE] ? VR_ACCENT : VR_LIVE, -1, ICON_AUTO);
        return;
    }
    if (cur_page()->graph == GR_BROWSE) {
        uint32_t total, cur = preset_pos(&total);
        char u[8];
        if (cur < total) fmt_int(val, (int32_t)cur + 1);
        else str_cpy(val, "--", 8);
        fmt_kn(u, -1, (int32_t)total);
        draw_column(0, "No.", val, u, VR_LIVE, -1, ICON_NONE);
        draw_column(1, "ENG", ENGINES[TSEL->eng_req]->name, "", VR_LIVE, -1, engine_icon(ENGINES[TSEL->eng_req]->name));
        draw_column(2, "FAV", preset_favorite() ? "ON" : "OFF", "", VR_LIVE, -1, ICON_X_STAR);
        draw_column(3, "LIST", LIST_NAME[list_mode()], "", VR_LIVE, -1, ICON_X_FOLDER);
        return;
    }
    if (sheet_of(cur_page()->graph)) {                   /* (Skiff phase 7, 8; FM6 LAB) a sheet's window (SHEETS) */
        sheet_of(cur_page()->graph)->cards();
        return;
    }
    if (cur_page()->graph == GR_SONG) {                  /* (Skiff phase 5) ENTRY PATTERN REPS SONG (ui_input.c song_knob):
                                                         * the selected track's entry picked; its pattern (KNOB 2's pick
                                                         * while there is one), its repeats; SONG */
        uint32_t k = song.sel % NTRK, e = sg.entry % NENT, p = song_pick();
        uint32_t v = p ? p : lists.ent[k][e][0], r = (uint32_t)lists.ent[k][e][1] + 1u;
        val[0] = (char)('A' + e);
        val[1] = 0;
        draw_column(0, "ENTRY", val, "", VR_LIVE, ((int32_t)e * 2 + 1) * 1000 / 32, ICON_AUTO);
        if (v >= SPICK_NEW || v == PAT_REST)            /* ((#28) REST: below 01, its gauge empty; (2.0) NEW, COPY+; (#30)
                                                         * SOUND, NO SOUND: no gauge) */
            str_cpy(val, v == PAT_REST ? "REST" : v == SPICK_NEW ? "NEW" : v == SPICK_COPY ? "COPY" :
                         v == SPICK_COPYP ? "COPY+" : v == SPICK_CLEAR ? "CLEAR" : v == SPICK_SOUND ? "SOUND" :
                         "NO SOUND", sizeof val);
        else if (v)
            pat_num(val, v - 1u);
        else
            str_cpy(val, "--", sizeof val);
        draw_column(1, "PATTERN", val, "", v ? VR_LIVE : VR_DIM, v >= SPICK_SOUND ? -1 : v >= SPICK_NEW ? 1000 :
                    v == PAT_REST ? 0 : v ? (int32_t)(v * 1000u / NPAT) : -1, ICON_AUTO);
        fmt_int(val, (int32_t)r);                        /* ((1.2.1, #12) an empty entry's too: a rest's length) */
        if (r > 16u)                                     /* ((2.0, #44) on CHAIN into the next: below 1x, no gauge */
            draw_column(2, "REPS", "CHAIN", "", VR_LIVE, -1, ICON_AUTO);
        else
            draw_column(2, "REPS", val, "x", VR_LIVE, (int32_t)(r * 1000u / 16u), ICON_AUTO);
        draw_column(3, "SONG", lists.song == SONG_ONCE ? "ONCE" : lists.song ? "ON" : "OFF", "", VR_LIVE,
                    lists.song ? 1000 : 0, ICON_AUTO);   /* ((2.0, #47) OFF ON ONCE) */
        return;
    }
    if (cur_page()->graph == GR_BANK) {                  /* (Skiff phase 5) the pool view: PATTERN, its LEN, how many
                                                         * use it, ((2.0) ACTION; was CLEAR) (bank.c pool_refs) */
        uint32_t id = ui.bslot % NPAT, len = pool_len(id), n = len ? pool_refs(id) : 0u;
        pat_num(val, id);
        draw_column(0, "PATTERN", val, "", VR_LIVE, (int32_t)((id + 1u) * 1000u / NPAT), ICON_AUTO);
        if (len) {
            fmt_int(val, (int32_t)len);
            draw_column(1, "LENGTH", val, "", VR_MID, -1, ICON_AUTO);   /* ((Skiff U4, V3) read, not turned: no gauge, MID) */
            fmt_int(val, (int32_t)n);
            draw_column(2, "USED", val, "x", VR_MID, -1, ICON_AUTO);
        } else {
            draw_column(1, "LENGTH", "--", "", VR_DIM, -1, ICON_AUTO);
            draw_column(2, "USED", "--", "", VR_DIM, -1, ICON_AUTO);
        }
        draw_column(3, "ACTION", BA_NAME[ui.bact % BA_N], "", act_ready() ? VR_THEME : VR_DIM, -1, ICON_AUTO);   /* ((2.0,
                                                         * #46) KNOB 4 the action, OCT+ does it: the footer) */
        return;
    }
    if (cur_page()->graph == GR_USER) {                  /* SLOT, then three GO buttons */
        int used = up_used(ui.uslot);
        up_slot_label(val, ui.uslot);
        draw_column(0, "SLOT", val, "", VR_LIVE, (int32_t)ui.uslot * 1000 / (int32_t)(UP_SLOTS - 1u), ICON_AUTO);
        draw_act_column(1, "LOAD", used ? VR_THEME : VR_DIM, ICON_AUTO);
        draw_act_column(2, ui.act == 3u && ui.act2 ? "NAME" : "ERASE", used ? VR_THEME : VR_DIM, ICON_AUTO);   /* ((Skiff U2)
                                                       * one step further: NAME) */
        draw_act_column(3, "SAVE", VR_THEME, ICON_AUTO);
        return;
    }
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                /* SLICE POS, then SPLIT JOIN (ui_slice.c) */
        uint32_t n = slice_count(), j = slice_sel(), src, div, ok = slice_src(&src, &div) && src;
        char u[8];
        if (j < n) {
            fmt_int(val, (int32_t)j + 1);
            fmt_kn(u, -1, (int32_t)n);
        } else {
            str_cpy(val, n ? "END" : "--", sizeof val);
            u[0] = 0;
        }
        draw_column(0, "SLICE", val, u, VR_LIVE, -1, ICON_SLICE);
        slice_time(val, slice_mark(j));
        draw_column(1, "POS", val, "S", ok ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        draw_act_column(2, "SPLIT", ok ? VR_THEME : VR_DIM, ICON_AUTO);
        draw_act_column(3, "JOIN", ok ? VR_THEME : VR_DIM, ICON_AUTO);
        return;
    }
#endif
    if (cur_page()->graph == GR_MOD) {                   /* ROUTE, then its SOURCE DEST AMOUNT ((Skiff U4) DST was distortion
                                                          * too; (2.0, D 1A) a fixed route's SOURCE and DEST DIM: its
                                                          * AMOUNT alone turns) */
        static const uint8_t FDST[MOD_SLOT0] = {MD_PITCH, MD_CUT, MD_SHP, MD_AMP, MD_PITCH, MD_CUT, MD_SHP};
        const track_t *t = TSEL;
        uint32_t r = mod_ui_route, fx = r < MOD_SLOT0, id = fx ? MOD_FIXED[r] : P_M1SRC + 3u * (r - MOD_SLOT0) + 2u;
        int32_t s = fx ? (r < 4u ? MS_LFO : MS_ENV) : t->p[id - 2u], d = fx ? FDST[r] : t->p[id - 1u], a = t->p[id];
        fmt_int(val, (int32_t)r + 1);
        draw_column(0, "ROUTE", val, "/11", VR_LIVE, (int32_t)r * 1000 / (MOD_ROUTES - 1u), mod_src_icon(MS_OFF));   /* (the
                                                                                                    * mod icon) */
        card_span_by(span_names, N_MSRC, 0, MS_N - 1);
        draw_column(1, "SOURCE", N_MSRC[clamp(s, 0, MS_N - 1)], "", s && !fx ? VR_LIVE : VR_DIM, -1, mod_src_icon(s));
        card_span_by(span_mod_dst, t, 0, MD_N - 1);
        draw_column(2, "DEST", mod_dst_name(t, d), "", d && !fx ? VR_LIVE : VR_DIM, -1, mod_dst_icon(t, d));
        param_format(&TP[id], a, val, &unit);
        card_span_of(&TP[id]);
        draw_column(3, "AMOUNT", val, unit, a && !(fx && mod_dead(t, r)) ? VR_LIVE : VR_DIM, RATIO(&TP[id], a),
                    mod_src_icon(MS_OFF));
        return;
    }
    if (step_page(cur_page()->graph) && lock_held()) {   /* a step held: what KNOB 1..4 lock (ui_input.c lock_turn) */
        uint64_t held = lock_held();
        uint32_t st = 0;
        while (!((held >> st) & 1u))
            st++;                                       /* (the first step held: its values) */
        for (c = 0; c < 4u; c++) {
            uint32_t id = lock_id(c);
            const param_desc_t *d;
            int16_t v;
            int on;
            if (id == 0xFFu) {
                draw_column(c, "", "", "", VR_THEME, -1, ICON_AUTO);
                continue;
            }
            d = track_desc(TSEL, id);
            on = motion_lock_get(TSEL, st, id, &v);
            if (!on)
                v = motion_base_value(TSEL, id);      /* not locked: the sound's own, DIM */
            param_format(d, v, val, &unit);
            card_span_of(d);
            draw_column(c, d->label, val, unit, on ? VR_LIVE : VR_DIM, RATIO(d, enum_rank(d, v)), param_icon(d, v));
        }
        return;
    }
    if (cur_page()->scope == SC_STEP && drum_track(TSEL)) {   /* the grid ((2.0) PRESETS the cursor): LANE, the cursor's
                                                         * HIT ACC, the selection's VELOCITY */
        const step_t *st = &TSEL->step[ui.cursor];
        uint32_t b = 1u << ui.lane, on = (step_lanes(st) & b) != 0u, ac = (step_accents(st) & b) != 0u;
        uint32_t f = sel_first(TSEL);
        draw_column(0, "LANE", drum_lane_name(TSEL, ui.lane), "", VR_LIVE, -1, ICON_AUTO);
        draw_column(1, "HIT", on ? "ON" : "--", "", on ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        draw_column(2, "ACC", ac ? "ON" : "--", "", ac ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        fmt_int(val, f < NSTEP ? (int32_t)step_vel(&TSEL->step[f]) : 0);
        draw_column(3, "VELOCITY", f < NSTEP ? val : "--", "", f < NSTEP ? VR_LIVE : VR_DIM,
                    f < NSTEP ? (int32_t)step_vel(&TSEL->step[f]) * 1000 / 127 : -1, ICON_AUTO);
        return;
    }
    if (cur_page()->scope == SC_STEP) {               /* (Skiff 2.0) NOTES: PITCH START LENGTH VELOCITY of the selection's
                                                        * first step (PITCH its lowest note selected, "+" the others
                                                        * selected); nothing selected: DIM "--" ("TIE": the cursor on
                                                        * one; the header has the cursor's step) */
        const track_t *t = TSEL;
        uint32_t f = sel_first(t), i, k, cnt = 0, lo = 128u;
        char u[8], sl[8];
        int32_t c;
        for (i = sel_lo(); f < NSTEP && i <= sel_hi(); i++)
            for (k = 0; sel_step(t, i) && k < t->step[i].n; k++)
                if (sel_voice(i, k) && !step_tied(&t->step[i], k)) {   /* ((2.0) a note going on: the one before's) */
                    cnt++;
                    if (i == f && t->step[i].note[k] < lo)
                        lo = t->step[i].note[k];
                }
        u[0] = 0;
        str_cpy(val, "--", sizeof val);
        if (lo < 128u)
            note_name(val, lo);
        if (cnt > 1u) {
            u[0] = '+';
            fmt_int(u + 1, (int32_t)cnt - 1);
        }
        draw_column(0, "PITCH", val, u, f < NSTEP ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        str_cpy(val, "--", sizeof val);
        sl[0] = 0;
        if (f < NSTEP) {                              /* ((2.0, #44) a chain's counted on: 69 /128) */
            uint32_t all, off = step_off(&all, 1);
            fmt_int(val, (int32_t)(off + f) + 1);
            fmt_kn(sl, -1, (int32_t)all);
        }
        draw_column(1, "START", val, sl, f < NSTEP ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        unit = "";
        c = f < NSTEP ? len_get(t, f) : 0;
        if (c >= LEN_STEPS - 1) {                     /* ((2.0) 100 % is 1 STEP: the default, the next 2 STEPS) */
            fmt_int(val, c - LEN_STEPS + 2);
            unit = c >= LEN_STEPS ? "STEPS" : "STEP";
        } else if (c) {
            param_format(track_desc(t, P_SGATE), (int16_t)c, val, &unit);
        } else {
            str_cpy(val, t->step[ui.cursor].time == ST_TIE ? "TIE" : "--", sizeof val);
        }
        draw_column(2, "LENGTH", val, unit, f < NSTEP ? VR_LIVE : VR_DIM, -1, ICON_AUTO);
        fmt_int(val, f < NSTEP ? (int32_t)step_vel(&t->step[f]) : 0);
        draw_column(3, "VELOCITY", f < NSTEP ? val : "--", "", f < NSTEP ? VR_LIVE : VR_DIM,
                    f < NSTEP ? (int32_t)step_vel(&t->step[f]) * 1000 / 127 : -1, ICON_AUTO);
        return;
    }
    for (c = 0; c < 4u; c++) {
        int16_t *vp;
        const param_desc_t *d = page_desc(cur_page(), c, &vp);
        if (cur_page()->graph == GR_SLOTS && c == 1u) {   /* ((Skiff U2) PROJECT: NAME, was EDIT; (1.2.1, #6) DELETE, */
            draw_act_column(1, ui.act == 2u && ui.act2 ? "NAME" : "DELETE",   /* one step further NAME) */
                            project_used((uint32_t)song.g[G_SLOT] - 1u) ? VR_THEME : VR_DIM, ICON_AUTO);
            continue;
        }
        if (cur_page()->graph == GR_SLOTS && c == 2u && !project_used((uint32_t)song.g[G_SLOT] - 1u)) {
            draw_act_column(2, "NEW", VR_THEME, ICON_AUTO);   /* ((1.2.1, #6) an empty slot: a blank project) */
            continue;
        }
        if ((cur_page()->graph == GR_LANES || cur_page()->graph == GR_LMIX) && c == 0u)   /* (LANE: above) */
            continue;
        if (!d || !d->label || d->label[0] == '-') {
            draw_column(c, "", "", "", VR_THEME, -1, ICON_AUTO);
            continue;
        }
        if ((act_cols() >> c) & 1u) {
            draw_act_column(c, d->label, VR_THEME, ICON_AUTO);
            continue;
        }
        param_format(d, *vp, val, &unit);
        card_mot_of(vp);                                /* (a global: never) */
        card_span_of(d);
        draw_column(c, (cur_page()->graph == GR_LANES || cur_page()->graph == GR_LMIX) && c == 1u ? "LEVEL" : d->label,
                    val, unit,                          /* ((phase 9) TUNE DECAY; (2.0) LANE MIX's PAN) */
                    card_dead(cur_page(), c) ? VR_DIM : VR_LIVE,
                    d->fmt == F_ENUM && d->max < 2 ? -1 : RATIO(d, enum_rank(d, *vp)), param_icon(d, *vp));
    }
}


/* The power-on splash (main.c fm1_main, web/emu felucca_web.c; 1.1.5): a RAISE square centred on the screen, square
 * corners (in every style; LINE too: RAISE from the palette's SURF), in it in AF_M, the ink SPL_PAD in from the left:
 * at the top the name and the version, the first line's capitals' top SPL_PAD down; at the bottom the maker and
 * "with community", the last line's capitals' bottom (its baseline) SPL_PAD up from the square's bottom. Both
 * margins are measured to the capitals (the descender of "community" hangs into the bottom one, as off a baseline).
 * The square (176 x 176) is more than the canvas (CV_MAX, 240 x 124): it is filled straight on the screen and its
 * two text bands drawn on it as canvases, the top one from the square's top edge, the bottom one to its bottom edge.
 * In the saved palette (settings_init before it) */
#define SPL_SQ 176                                  /* the square (even: centred exactly) */
#define SPL_X0 ((240u - SPL_SQ) / 2u)               /* its top-left on the screen (x and y) */
#define SPL_PAD 16                                  /* the text's inset: left (its ink), top and bottom (the capitals) */
#define SPL_PITCH 20                                /* line to line */
static const char *const SPLASH_LINES[] = {"Skiff", FELUCCA_VERSION, "on Felucca 1.1.5", "H\xFCgelton + community"};
/* a band of two lines, h tall, at y on the screen: the first line's top at ly; the two lines' capitals declared
 * against al0..al1 (mode: AL_V the band centred there, AL_B the last baseline on al1) */
static void splash_band(uint32_t y, uint32_t h, int32_t ly, const char *const *l, uint32_t mode, int32_t al0,
                        int32_t al1, const char *tag)
{
    cv_begin(SPL_SQ, h, T_RAISE);
    GFX_HOOK_ALIGN(0, al0, 0, al1, mode | AL_N(2), tag);
    cv_text_on(SPL_PAD, ly, &AF_M, l[0], T_TEXT, T_RAISE);
    cv_text_on(SPL_PAD, ly + SPL_PITCH, &AF_M, l[1], T_TEXT, T_RAISE);
    cv_blit(SPL_X0, y);
}
static void draw_splash(void)
{
    int32_t h = SPL_PAD - AF_M_CAP_Y + SPL_PITCH + AF_M.h;     /* a band: its two lines, the descenders in */
    int32_t lb = h - SPL_PAD - AF_M_CAP_H - AF_M_CAP_Y;         /* the bottom band's last line: its top */
    lcd_fill(0, 0, 240, 240, T_BG);
    lcd_fill(SPL_X0, SPL_X0, SPL_SQ, SPL_SQ, T_RAISE);
    splash_band(SPL_X0, (uint32_t)h, SPL_PAD - AF_M_CAP_Y, SPLASH_LINES, AL_V, SPL_PAD,
                SPL_PAD + SPL_PITCH + AF_M_CAP_H, "splash name + version SPL_PAD from the top");
    splash_band(SPL_X0 + SPL_SQ - (uint32_t)h, (uint32_t)h, lb - SPL_PITCH, SPLASH_LINES + 2, AL_B, 0, h - SPL_PAD,
                "splash maker's baseline SPL_PAD from the bottom");
    if (ux.crypt) {   /* (Skiff) CRYPT: a skull between two torches, between the bands (design/mock_crypt.py splash) */
        cv_begin(SPL_SQ, 40, T_RAISE);
        cr_torch(36, 8, 0, 2);
        cr_spritez(SPL_SQ / 2 - 13, 6, SPR_SKULL, T_TEXT, T_RAISE, 3);
        cr_torch(SPL_SQ - 46, 8, 1, 2);
        cv_blit(SPL_X0, 98);
    } else if (ux.quark) {   /* (Skiff Q2) QUARK: an atom in a collision's tracks (design/mock_quark.py splash), on the
         * card as the other scenes are, whole ((1.0) no band of BG: it read as a squished box of another colour; 72 rows
         * between the bands, not 40: the turned orbits were cut) */
        uint32_t k;
        cv_begin(SPL_SQ, 72, T_RAISE);
        cv_cy0 = 0;
        qk_burst(SPL_SQ / 2, 36, 6, 7u, 36, T_RAISE);
        cv_rect(0, 0, SPL_PAD, 72, T_RAISE);              /* (the tracks kept off the margins) */
        cv_rect(SPL_SQ - SPL_PAD, 0, SPL_PAD, 72, T_RAISE);
        for (k = 0; k < 3u; k++) {                        /* the orbits a sixth of a turn apart, the far ones dimmer; */
            uint32_t rot = k * 0x2AAAAAAAu, ph = 0x18000000u + k * 0x56000000u;   /* an electron on each */
            int32_t ex = 30 * QK_COS(ph) / 32767, ey = 9 * QK_SIN(ph) / 32767;
            qk_ellipse(SPL_SQ / 2, 36, 30, 9, rot, ux_mix(T_THEME, T_RAISE, 25 * k), 40);
            qk_disc(SPL_SQ / 2 + (ex * QK_COS(rot) - ey * QK_SIN(rot)) / 32767,
                    36 + (ex * QK_SIN(rot) + ey * QK_COS(rot)) / 32767, 1, T_ACCENT);
        }
        qk_disc(SPL_SQ / 2, 33, 2, T_TRK(0));             /* the nucleus: its quarks */
        qk_disc(SPL_SQ / 2 + 3, 38, 2, T_TRK(1));
        qk_disc(SPL_SQ / 2 - 3, 38, 2, T_TRK(2));
        cv_blit(SPL_X0, 84);
    } else if (ux.neon) {   /* (Skiff N3) NEON: the striped sun on the horizon, palms, the grid floor, the car on it
         * (design/mock_neon.py splash) */
        cv_begin(SPL_SQ, 40, T_RAISE);
        ne_sun(SPL_SQ / 2, 24, 20, 0);
        cv_rect(SPL_PAD, 24, SPL_SQ - 2 * SPL_PAD, 16, T_BG);   /* (SPL_PAD in, as the text: the square whole) */
        ne_grid(SPL_PAD, SPL_SQ - SPL_PAD, 24, 40, T_THEME, T_BG);
        cv_rect(SPL_PAD, 24, SPL_SQ - 2 * SPL_PAD, 1, T_THEME);
        ne_palm(SPL_PAD + 4, 6, T_BG);
        ne_palm(SPL_SQ - SPL_PAD - 22, 9, T_BG);
        ne_car(SPL_SQ / 2 + 14, 30, SPR_CAR, 1, T_THEME, 30, T_BG);
        cv_blit(SPL_X0, 98);
    } else {   /* Skiff: the tracks' colours, a block each, between the bands (centred: 18 px in from each side; (phase 6)
         * six: 20 px, 24 apart, design/mock_p6.py splash) */
        uint32_t k;
        for (k = 0; k < NTRK; k++)
            lcd_fill(SPL_X0 + 18u + k * 24u, 108, 20, 20, T_TRK(k));
    }
    lcd_sync();
}

/* UPDATE MODE countdown (main.c: OCT- + OCT+ held): over everything, the menu and the dialogs too */
static void draw_uboot(void)
{
    static uint8_t shown;
    char d[4] = {(char)('0' + ui.uboot % 10u), 0, 0, 0};
    if (!ui.force && shown == ui.uboot)
        return;
    shown = ui.uboot;
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    draw_text_box(0, 76, 240, &AF_M, "UPDATE MODE IN", T_TEXT, 1);
    draw_text_box(0, 102, 240, &AF_L, d, T_THEME, 1);
    {   /* the two keycaps held, and what letting go does */
        int32_t w = kc_w(KC_OCTDN) + 3 + kh_ink(KC_OCTUP, "LET GO TO CANCEL"), x = HALF_UP(240 - w);
        cv_begin(240, KC_H + 2, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(3), "update-mode keys centred");
        x = cv_keycap(x, 1, KC_OCTDN, T_KEY, T_INK, T_BG) + 3;
        cv_key_hint(x, 1, KC_OCTUP, "LET GO TO CANCEL", 1, T_BG);
        cv_blit(0, 149);
        lcd_sync();
    }
    ui.force = 0;
}

/* the count-in (seq.c, MENU > COUNT-IN; 1.1, Discussion #131): over the page until the sequencer starts. Its bar's four
 * beats counted down, 4 3 2 1, in four cells (the beat now filled THEME, those gone dim), with 2 BARS which bar it is;
 * PLAY stops it. Drawn again on each beat (seq.c's state; the header stays live) */
#define CI_Y 88                                       /* the cells: 4 x 48 px, 8 px apart, 72 tall */
#define CI_H 72
static void draw_countin(void)
{
    static uint32_t shown;
    uint32_t total = cin_total, e = total - cin_left, j, sig = total * 64u + e + 1u;
    char sub[16];
    if (!ui.force && shown == sig)
        return;
    shown = sig;
    lcd_fill(0, H_HEAD, 240, 240 - H_HEAD, T_BG);
    ui.head_sig = ~0u;
    draw_text_box(0, 44, 240, &AF_M, "COUNT-IN", T_TEXT, 1);
    if (total > 4u) {
        str_cpy(sub, "BAR 1 OF 2", sizeof sub);
        sub[4] = (char)('1' + e / 4u % 9u);
        sub[9] = (char)('0' + total / 4u % 10u);
    } else
        str_cpy(sub, "1 BAR", sizeof sub);
    draw_text_box(0, 66, 240, &AF_S, sub, T_MID, 1);
    cv_begin(216, CI_H, T_BG);
    for (j = 0; j < 4u; j++) {
        int32_t x = (int32_t)j * 56;
        char d[2] = {(char)('4' - j), 0};
        int now = j == e % 4u, gone = j < e % 4u;
        uint16_t fill = now ? T_THEME : T_SURF;
        cv_rrect(x, 0, 48, CI_H, 8, fill, T_BG);
        GFX_HOOK_ALIGN(x, 0, x + 48, CI_H, AL_HV, "count-in beat centred in its cell");
        cv_text_in(x, CAP_IN(L, CI_H), 48, &AF_L, d, now ? T_INK : gone ? T_DIM : T_TEXT, fill);
    }
    cv_blit(12, CI_Y);
    draw_text_box(0, CI_Y + CI_H + 14, 240, &AF_S, "recording starts after 1", T_MID, 1);   /* (the mockup's case) */
    {   /* PLAY stops it */
        int32_t x = HALF_UP(240 - kh_ink(KC_PLAY, "STOP"));
        cv_begin(240, KC_H + 2, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "count-in key centred");
        cv_key_hint(x, 1, KC_PLAY, "STOP", 1, T_BG);
        cv_blit(0, 206);
        lcd_sync();
    }
    ui.force = 0;
}

/* the OCT- / OCT+ dialog: what it does (ui.confirm, ui.confirm_trk) on two lines, on a surface */
#define DLG_X 16
#define DLG_Y 62
#define DLG_W 208
#define DLG_H 116
static void confirm_text(char *a, char *b)
{
    uint32_t k = ui.confirm_trk;
    b[0] = 0;
    switch (ui.confirm) {
    case CF_OVR_PROJ:
        str_cpy(a, "OVERWRITE PROJECT A?", 24);
        a[18] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b))                   /* the project's name */
            b[0] = 0;
        break;
    case CF_RESET_SET:                                  /* ((Skiff U4, A3) MENU > SYSTEM > RESET SETTINGS) */
        str_cpy(a, "RESET SETTINGS?", 24);
        str_cpy(b, "the MENU's; the music stays", 40);
        break;
    case CF_STORE_SCENE:                                /* (Skiff U2) PERFORM's scene stored over */
        str_cpy(a, "OVERWRITE SCENE ", 24);
        fmt_int(a + 16, (int32_t)k + 1);
        str_cpy(a + str_len(a), "?", 2);
        break;
    case CF_ERASE_PROJ:                                 /* ((1.2.1, #6) SAVE > PROJECT's DELETE) */
        str_cpy(a, "DELETE PROJECT A?", 24);
        a[15] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b))
            b[0] = 0;
        break;
    case CF_NEW_PROJ:                                   /* ((1.2.1, #6) NEW over music changed since its save) */
        str_cpy(a, "NEW PROJECT?", 24);
        str_cpy(b, "the music now goes", 40);
        break;
    case CF_LOAD_PROJ:                                  /* (Skiff U1) the session goes: the project's name under it */
        str_cpy(a, "LOAD PROJECT A?", 24);
        a[13] = (char)('A' + (k & 3u));
        if (!project_name(k & 3u, b))
            b[0] = 0;
        break;
    case CF_INIT_SOUND:
        str_cpy(a, "INITIALIZE SOUND?", 24);
        if (TSEL->eng_req % NENGINES == ENGI_DRUMX)    /* ((Skiff phase 7) the project's kit, every DRUM-X track's) */
            str_cpy(b, "and the DRUM-X kit", 40);
        break;
    case CF_CLEAR_MOTION:
        str_cpy(a, "CLEAR T1 AUTOMATION?", 24); a[7] = (char)('1' + k % NTRK);
        break;
    case CF_OVR_USER:
        str_cpy(a, "OVERWRITE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);                               /* the sound stored there */
        break;
    case CF_ERASE_USER:
        str_cpy(a, "ERASE ", 24);
        up_slot_label(a + str_len(a), k);
        str_cpy(a + str_len(a), "?", 2);
        up_name(k, b);
        break;
    case CF_CLEAR_ENTRY: {                              /* (Skiff phase 5) SONG: "CLEAR ENTRY C?", its first line (the
                                                         * rest: draw_confirm, ui_blocks.c) */
        char l[3][32];
        int gone;
        str_cpy(a, "CLEAR ENTRY A?", 24);
        a[12] = (char)('A' + k % NENT);
        (void)song_clear_text(k, l, &gone);
        str_cpy(b, l[0], 24);
        break;
    }
    case CF_ERASE_BANK:                                 /* (Skiff phase 5) the pool view: "CLEAR PATTERN 23?" */
        str_cpy(a, "CLEAR PATTERN ", 24);
        pat_num(a + 14, k);
        str_cpy(a + 16, "?", 8);
        str_cpy(b, "no entry uses it", 24);
        break;
    case CF_EMPTY_BANK: {                               /* ((2.0, #46) "EMPTY PATTERN 21?", the entries it empties */
        uint32_t n = pool_refs(k);
        str_cpy(a, "EMPTY PATTERN ", 24);
        pat_num(a + 14, k);
        str_cpy(a + 16, "?", 8);
        if (n) {                                        /* "2 entries go quiet" */
            fmt_int(b, (int32_t)n);
            str_cpy(b + str_len(b), n == 1u ? " entry goes quiet" : " entries go quiet", 40 - str_len(b));
        } else
            str_cpy(b, "no entry uses it", 40);
        break;
    }
    case CF_CLEAR_UNUSED:                               /* ((2.0, #46) "CLEAR 3 UNUSED?" */
        str_cpy(a, "CLEAR ", 24);
        fmt_int(a + 6, (int32_t)k);
        str_cpy(a + str_len(a), " UNUSED?", 24 - str_len(a));
        str_cpy(b, "patterns no entry uses", 40);
        break;
    default:
        str_cpy(a, "CLEAR T1 SEQUENCE?", 24);     /* the header (T1..T4) is hidden */
        a[7] = (char)('1' + k % NTRK);
        break;
    }
}
static void draw_confirm(void) { draw_confirm_blocks(); }   /* (U2: BLOCKS' dialog, ui_blocks.c) */


static void ui_draw_page(uint32_t counting);
static void ui_draw(void)
{
    static uint8_t counting;
    ui.frame++;
    if (ui.take_t)                                      /* (Skiff: the TAKE box, ui_take.c) */
        ui.take_t--;
    if (ui.undo_w)                                      /* (Skiff: the undo window, ui.c undo_step) */
        ui.undo_w--;
    if (ui.edge_t)                                      /* ((Skiff U4, F7) SELECT's end cue) */
        ui.edge_t--;
    if (counting != (uint8_t)seq_counting()) {          /* a count-in began or ended: the page drawn again after it */
        counting = (uint8_t)seq_counting();
        ui.force = 1;
    }
    if (!scr_frame())                                   /* MENU > SCREEN OFF: dark (or waking), nothing drawn */
        return;
    ui_draw_page(counting);
    scr_shown();
}
static void ui_draw_page(uint32_t counting)
{
    if (ui.uboot) {
        draw_uboot();
        draw_head();
        return;
    }
    if (ui.menu) {
        draw_menu();
        ui.force = 0;
        return;
    }
    if (counting && !ui.confirm && !name_on()) {        /* the count-in: over the page (and a layer) */
        draw_countin();
        draw_head();
        return;
    }
    if (ui.confirm) {                                   /* the OCT- / OCT+ dialog */
        if (ui.force) {
            draw_confirm();
            ui.force = 0;
        }
        draw_head();                                    /* recording remains visible over confirmations */
        return;
    }
    if (name_on()) {                                    /* NAME: a user preset's or a project's (ui_name.c) */
        name_draw();
        return;
    }
    if (ui.layer || ui.shift) {                         /* a layer's map over the page (ui_layer.c); (Skiff U5, A1) SHIFT's,
                                                         * under the page's cards */
        if (ui.force)
            draw_frame();
        draw_head();
        if (ui.layer)
            draw_layer();
        else {
            draw_columns();
            draw_shift();
        }
        if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {
            str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
            ui.msg2[0] = 0;
            ui.msg_t = MSG_T;
        }
        if (ui.hot_t)
            ui.hot_t--;
        if (ui.bpm_t)
            ui.bpm_t--;
        ui.force = 0;
        return;
    }
    if (!ui.home && !page_visible(ui.page)) {          /* a page the track no longer has (another track, another
                                                         * engine): (Skiff U3, N3) its family's first, or HOME, and why */
        uint32_t f = page_vis_first(PAGES[ui.page].fam);
        page_none(PAGES[ui.page].title);
        if (f < NPAGES) {
            ui.page = (uint8_t)f;
            page_entered();
        } else {
            go_home();
        }
    }
    cursor_fix();
    if (ui.force)
        draw_frame();
    felucca_dbg.stage = 3;
    draw_head();
    felucca_dbg.stage = 4;
    draw_columns();
    felucca_dbg.stage = 5;
    draw_graph();
    if (ui.msg_t && !--ui.msg_t && ui.msg2[0]) {     /* the second message (ui_notices) */
        str_cpy(ui.msg, ui.msg2, sizeof ui.msg);
        ui.msg2[0] = 0;
        ui.msg_t = MSG_T;
    }
    if (ui.bpm_t)
        ui.bpm_t--;
    if (ui.hot_t)
        ui.hot_t--;
    felucca_dbg.stage = 6;
    draw_foot();
    ui.force = 0;
}
