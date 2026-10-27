/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian).
 * Text and icons are 4-bit alpha bitmaps made at build time (tools/gen_aa_font.py: Barlow Condensed;
 * tools/gen_aa_icons.py: Fukiai). A glyph is trimmed to its ink box, advances are in 1/16 px, and
 * each glyph is rasterised at 1 << psh horizontal phases (S M 2: drawn at a pen of +0 and +1/2 px; L V 1: the rounded
 * pen; (Skiff 2.0) were 4 and 2);
 * cv_text takes the phase that puts it within a phase step of its true place. Blending uses a 16-entry
 * ramp per (ink, background) pair, cached, so a pixel costs a nibble read and a store. Colours come from the theme tokens. */
typedef struct { uint16_t off, adv; int8_t bx; uint8_t by, bw, bh; } aag_t;
typedef struct {
    uint8_t h, asc, first, last, nex;   /* line height, baseline; ASCII range; extras */
    uint16_t nk;                        /* kerning pairs */
    const aag_t *g;
    const uint8_t *data;
    const uint16_t *kkey;               /* a << 8 | b, sorted */
    const int8_t *kd;                   /* 1/16 px */
    const uint8_t *ex;                  /* codes of the extra glyphs */
    uint8_t psh;                        /* log2 of the phases per glyph: g[i << psh] .. g[(i << psh) + phases - 1] */
    const uint8_t *hc;                  /* data's Huffman code (cv_alpha_hc), 0 = 2 px per byte (cv_alpha) */
    uint8_t sc;                         /* (Skiff 2.0) a pixel face (cv_alpha_px): its scale 1..3, | PX_BOLD; 0 = Barlow */
} aafont_t;
#define PX_BOLD 0x80u
typedef struct { const char *name; uint16_t bg, surf, text, theme, accent; } ui_pal_t;
typedef struct { uint16_t off; uint8_t w; const char *label; } kc_t;
#include "ui_fonts.h"                   /* AF_S 12 px / 400, AF_M 15 px / 500, AF_L 28 px / 600 */
#include "ui_pixfont.h"                 /* (Skiff) THEME CRYPT's pixel faces AF_S_PX .. (tools/gen_pixel_font.py) */
#include "ui_palettes.h"
#include "ui_keycaps.h"                 /* KC_*: the keycaps / badges (tools/gen_aa_keycaps.py), KNOB_* arcs */
#define ELLIPSIS '\x85'                 /* the ellipsis glyph of AF_S and AF_M */

/* (Skiff) the theme's faces: Barlow Condensed's, or THEME CRYPT's and NEON's pixel ones (palette_set). From here on AF_S AF_M
 * AF_L AF_V name the theme's, and AF_x_CAP_Y / _CAP_H its capitals' rows (CAP_IN centres by them) */
static const aafont_t *const UF_FACE[2][4] = {{&AF_S, &AF_M, &AF_L, &AF_V}, {&AF_S_PX, &AF_M_PX, &AF_L_PX, &AF_V_PX}};
static const uint8_t UF_CAP[2][4][2] = {
    {{AF_S_CAP_Y, AF_S_CAP_H}, {AF_M_CAP_Y, AF_M_CAP_H}, {AF_L_CAP_Y, AF_L_CAP_H}, {AF_V_CAP_Y, AF_V_CAP_H}},
    {{AF_S_PX_CAP_Y, AF_S_PX_CAP_H}, {AF_M_PX_CAP_Y, AF_M_PX_CAP_H}, {AF_L_PX_CAP_Y, AF_L_PX_CAP_H},
     {AF_V_PX_CAP_Y, AF_V_PX_CAP_H}}};
static struct { const aafont_t *const *f; const uint8_t (*cap)[2]; } uf = {UF_FACE[0], UF_CAP[0]};
#undef AF_S_CAP_Y
#undef AF_S_CAP_H
#undef AF_M_CAP_Y
#undef AF_M_CAP_H
#undef AF_L_CAP_Y
#undef AF_L_CAP_H
#undef AF_V_CAP_Y
#undef AF_V_CAP_H
#define AF_S (*uf.f[0])
#define AF_M (*uf.f[1])
#define AF_L (*uf.f[2])
#define AF_V (*uf.f[3])
#define AF_S_CAP_Y (uf.cap[0][0])
#define AF_S_CAP_H (uf.cap[0][1])
#define AF_M_CAP_Y (uf.cap[1][0])
#define AF_M_CAP_H (uf.cap[1][1])
#define AF_L_CAP_Y (uf.cap[2][0])
#define AF_L_CAP_H (uf.cap[2][1])
#define AF_V_CAP_Y (uf.cap[3][0])
#define AF_V_CAP_H (uf.cap[3][1])

/* host tests hook in here (layout lint, draw cost); nothing in the firmware */
#ifndef GFX_HOOK_TEXT
#define GFX_HOOKS 0                     /* (the firmware: what only the hooks read is not even computed) */
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) ((void)0)   /* an ink box; flags 1 ellipsised, 2 cut, 4 icon, 8 free text, 32 in a scrolled view */
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() ((void)0)
#define GFX_HOOK_PIXELS(n) ((void)0)
#endif
#ifndef GFX_HOOKS
#define GFX_HOOKS 1
#endif
#ifndef GFX_HOOK_CELL
#define GFX_HOOK_CELL(x0, y0, x1, y1) ((void)0)    /* a cell, card, row or button (cv_rrect): a box touching it lies inside it */
#endif
#ifndef GFX_HOOK_INK
#define GFX_HOOK_INK(ink, bg) ((void)0)            /* a text, icon or keycap label's ink and what it is drawn on */
#endif
#ifndef GFX_HOOK_RULE
#define GFX_HOOK_RULE(x, y, w, h) ((void)0)        /* a divider drawn on the screen (ui_draw.c lcd_rule): no box may touch it */
#endif
#ifndef GFX_HOOK_BACKDROP
#define GFX_HOOK_BACKDROP(x, y, w, h) ((void)0)  /* (Skiff) what lies under a dialog: drawn, never read (the lint: as a
                                                   * fill over it) */
#endif
#ifndef GFX_HOOK_OVERLAY
#define GFX_HOOK_OVERLAY(x0, y0, x1, y1) ((void)0) /* (Skiff) an opaque box over what this canvas drew so far (the TAKE
                                                    * box): what lies under it, even in part, is hidden on purpose */
#endif
/* the alignment check (tests/ui_render.c): GFX_HOOK_ALIGN declares that the next item(s) drawn (a text, an icon, a
 * keycap, a cushion) are meant to sit centred in the box x0..x1, y0..y1 (canvas drawing coordinates, x1 y1 past it)
 * along AL_H and / or AL_V; AL_N(n): the next n items together. A text's ink is measured across (its glyphs' boxes)
 * and its capitals' band up and down (GFX_HOOK_PEN: its line top and face); an icon's ink from its cell
 * (GFX_HOOK_ITEM). The tag names the place in the report. */
#ifndef GFX_HOOK_ALIGN
#define GFX_HOOK_ALIGN(x0, y0, x1, y1, mode, tag) ((void)0)
#define GFX_HOOK_PEN(y, f) ((void)0)
#define GFX_HOOK_ITEM(x0, y0, x1, y1) ((void)0)
#define GFX_HOOK_ICON(x, y, size, id) ((void)0)        /* (an icon drawn: GFX_HOOK_ITEM with its ink, icons.c icon_ink) */
#endif
#define AL_H 1u                                    /* centred across */
#define AL_V 2u                                    /* centred up and down (a row's items: on the same centre line) */
#define AL_HV 3u
#define AL_B 4u                                    /* on the baseline y1 (a text: its capitals' bottom) */
#define AL_R 8u                                    /* the ink ending at x1 */
#define AL_PASS 16u                                /* (its item counts for the place declared before it too) */
#define AL_CELLS 32u                               /* the next cells (cv_rrect), not ink: a row of keys, a fill */
#define AL_N(n) ((uint32_t)(n) << 8)

#define CV_MAX (240u * 138u)      /* the graph strip is 240 x 124 ((Skiff) BLOCKS: the panel 138, its footer one line) */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static uint16_t cv_bg;           /* what cv_begin cleared the canvas to */
static uint8_t cv_scroll;        /* 1: drawing a scrolled view (its text may run past the canvas) */
static int32_t cv_oy;            /* y offset for graph drawing */
static int16_t cv_cy0, cv_cy1;   /* text clip: canvas rows cv_cy0 .. cv_cy1 - 1 (cv_begin: the whole canvas) */
static uint8_t cv_flip;          /* cv_alpha_hc: the cell mirrored left <-> right (the header's metronome, ui_draw.c) */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* ------------------------------------------------------------- theme --- */
/* Five stored colours per palette, the rest derived with fixed blends (tools/gen_ui_palettes.py) */
static struct {
    uint16_t bg, surf, text, theme, accent;
    uint16_t mid, dim, line, sel, tint, ink, rec, raise, key, lane, grid;
    uint16_t trk[UI_NTRK];       /* the tracks' colours (UI_TRACKS; a palette without its own: THEME) */
    uint8_t light, crypt, neon;  /* (Skiff) THEME CRYPT, its own font and widgets (C2, C3); NEON, CRYPT's font and its
                                  * own widgets (design/mock_neon.py) */
    uint8_t quark;               /* (Skiff) THEME QUARK: Skiff's font, its own widgets (design/mock_quark.py) */
    uint32_t gen;                /* bumped by palette_set (the text ramps follow) */
} ux;
/* The look (Skiff, DESIGN.md: BLOCKS; U2: the only one, FLAT and LINE retired): SURF tiles with square corners on the
 * BG, no dividers; the knob just turned: its label row a band of the accent (ui_draw.c draw_column); track cushions in
 * their tracks' colours; key names as words, no keycaps */
#define T_TRK(k) ux.trk[(k) % UI_NTRK]   /* track k's colour (always with its number: never the colour alone) */
#define T_BG ux.bg               /* background */
#define T_SURF ux.surf           /* dialogs, menu rows, mixer strips */
#define T_TEXT ux.text           /* primary text */
#define T_THEME ux.theme         /* values, curves, the gauge's end line */
#define T_ACCENT ux.accent       /* the one active thing: hot knob, cursor, playhead */
#define T_MID ux.mid             /* labels, units, secondary text */
#define T_DIM ux.dim             /* inactive, empty */
#define T_LINE ux.line           /* 1 px dividers, gauge track */
#define T_SEL ux.sel             /* selection and gauge fill */
#define T_TINT ux.tint           /* a faint theme area */
#define T_INK ux.ink             /* text on T_SEL / T_THEME */
#define T_REC ux.rec             /* REC: fixed red (GREY, MONO: the accent) */
#define T_RAISE ux.raise         /* a raised area on a surface: stubs, guides, slots, chips, button wells */
#define T_KEY ux.key             /* a keycap's fill (its label: T_INK; unavailable: a T_DIM fill) */
#define T_LANE ux.lane           /* the piano roll: an in-scale row (SURF -> THEME 10 %) */
#define T_GRID ux.grid           /* the piano roll: a step line (SURF -> TEXT 6 %; beats and C rows: RAISE) */
#define NPALETTES UI_NPALETTES

/* a + (b - a) * pct / 100 per channel, rounded */
static uint16_t ux_mix(uint16_t a, uint16_t b, int32_t pct)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << SH[k];
    }
    return (uint16_t)out;
}
static uint32_t ux_luma(uint16_t c)        /* 0..255 */
{
    return ((c >> 11) * 255u / 31u * 54u + ((c >> 5) & 63u) * 255u / 63u * 183u + (c & 31u) * 255u / 31u * 19u) >> 8;
}

/* (Skiff #27) THEME CUSTOM's colours over the palette's: 11 RGB565, low byte first (BG SURF TEXT THEME ACCENT, the six
 * tracks'), the rest derived from them as from a palette's; 0: the palette's own (ui.c theme_apply) */
static const uint8_t *ux_cpal;
static void palette_set(uint32_t i)
{
    const ui_pal_t *p = &UI_PALETTES[i % NPALETTES];
    uint32_t k;
    i %= NPALETTES;
    ux.crypt = i == UI_CRYPT_INDEX;
    ux.neon = i == UI_NEON_INDEX;
    ux.quark = i == UI_QUARK_INDEX;
    uf.f = UF_FACE[ux.crypt || ux.neon];         /* (CRYPT, NEON: the pixel faces) */
    uf.cap = UF_CAP[ux.crypt || ux.neon];
    ux.bg = p->bg; ux.surf = p->surf; ux.text = p->text; ux.theme = p->theme;
    ux.accent = p->accent;
    for (k = 0; k < UI_NTRK; k++)
        ux.trk[k] = UI_TRACKS[i][k];
    if (ux_cpal) {                               /* (low byte first, as the settings record is) */
        memcpy(&ux.bg, ux_cpal, 10u);            /* (BG .. ACCENT in a row) */
        memcpy(ux.trk, ux_cpal + 10, 2u * UI_NTRK);
    }
    ux.mid = ux_mix(ux.bg, ux.text, UI_MID_PCT);
    ux.dim = ux_mix(ux.bg, ux.text, UI_DIM_PCT);
    ux.line = ux_mix(ux.bg, ux.text, UI_LINE_PCT);
    ux.sel = ux_mix(ux.bg, ux.theme, UI_SEL_PCT);
    ux.tint = ux_mix(ux.bg, ux.theme, UI_TINT_PCT);
    ux.ink = ux.bg;
    ux.raise = ux_mix(ux.surf, ux.text, UI_RAISE_PCT);
    ux.key = ux_mix(ux.bg, ux.text, UI_KEY_PCT);
    ux.lane = ux_mix(ux.surf, ux.theme, 10);
    ux.grid = ux_mix(ux.surf, ux.text, 6);
    ux.light = ux_luma(ux.bg) > 128u;
    ux.rec = ux.light ? UI_REC_LIGHT : UI_REC_DARK;
    ux.gen++;                                    /* the text ramps change with the coverage curve */
}

/* settings store a tagged id, THEME's (Skiff, R1: UI_THEME_TAG + index); earlier ids are migrated: the eleven palettes
 * before THEME (UI_PAL_TAG + index) and Felucca's oldest twenty (below 20); a palette that is gone is SKIFF */
static uint32_t palette_from_stored(uint32_t v)
{
    if (v >= UI_THEME_TAG && v - UI_THEME_TAG < NPALETTES) return v - UI_THEME_TAG;
    if (v >= UI_PAL_TAG && v - UI_PAL_TAG < 11u) return UI_PALETTE_MIGRATE11[v - UI_PAL_TAG];
    return v < 20u ? UI_PALETTE_MIGRATE20[v] : UI_SKIFF_INDEX;
}
static uint32_t palette_to_stored(uint32_t i) { return UI_THEME_TAG + i % NPALETTES; }
static int palette_stored_ok(uint32_t v)
{
    return v < 20u || (v >= UI_PAL_TAG && v - UI_PAL_TAG < 11u) || (v >= UI_THEME_TAG && v - UI_THEME_TAG < NPALETTES);
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

/* ------------------------------------------------------------ canvas --- */
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    uint16_t s = swap16(bg);
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    GFX_HOOK_BEGIN();
    cv_w = w;
    cv_h = h;
    cv_cy0 = 0;
    cv_cy1 = (int16_t)h;
    cv_bg = bg;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = s;
}

/* (Skiff N3) THEME NEON's dialog: the page redrawn under it paused like a tape, every other screen row darker */
static uint8_t cv_vhs;
static void cv_vhs_rows(uint32_t y, uint32_t r0)
{
    uint32_t r, i;
    for (r = r0; cv_vhs && r < cv_h; r++)
        if ((y + r) & 1u)
            for (i = 0; i < cv_w; i++)
                cv_px[r * cv_w + i] = swap16(ux_mix(swap16(cv_px[r * cv_w + i]), ux.bg, 55));
}

static void cv_blit(uint32_t x, uint32_t y)
{
    cv_vhs_rows(y, 0);
    GFX_HOOK_BLIT(x, y, 0u);
    lcd_blit(x, y, cv_w, cv_h, cv_px);
}

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    cv_vhs_rows(y, r0);
    GFX_HOOK_BLIT(x, y, r0);
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if ((uint32_t)x < cv_w && (uint32_t)y < cv_h)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > (int32_t)cv_w)
        x1 = (int32_t)cv_w;
    if (y1 > (int32_t)cv_h)
        y1 = (int32_t)cv_h;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}


/* a 1 px outline */
static void cv_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x, y, w, 1, c);
    cv_rect(x, y + h - 1, w, 1, c);
    cv_rect(x, y, 1, h, c);
    cv_rect(x + w - 1, y, 1, h, c);
}

static uint8_t cv_glow;                /* (Skiff N4) cv_line paints only where the canvas is cv_bg (a glow under a line) */
static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        if (!cv_glow || ((uint32_t)x0 < cv_w && (uint32_t)(y0 + cv_oy) < cv_h &&
                         cv_px[(uint32_t)(y0 + cv_oy) * cv_w + (uint32_t)x0] == swap16(cv_bg)))
            cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* a line `thick` px tall (the scope's trace: 2). (Skiff N4) THEME NEON: a curve (2 px and more) glows, a ring 1 px out
 * of it on the background only (a polyline's next segment never covers the last) */
static void cv_line_t(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c, int32_t thick)
{
    int32_t t, d;
    if (ux.neon && thick >= 2) {
        uint16_t g = ux_mix(c, cv_bg, 62);
        cv_glow = 1;
        for (t = -1; t <= thick; t++)
            for (d = -1; d <= 1; d++)
                cv_line(x0 + d, y0 + t, x1 + d, y1 + t, g);
        cv_glow = 0;
    }
    for (t = 0; t < thick; t++)
        cv_line(x0, y0 + t, x1, y1 + t, c);
}

/* a filled tile: the older looks' rounded rectangle, square in BLOCKS (U2: the only look). r and under are kept
 * for the callers (ponytail: no rounding left; a theme that wants corners brings them back) */
/* (Skiff C3) THEME CRYPT's */
/* a stone tile: base, a few lighter specks in a fixed pattern (in the canvas's coordinates) */
static void cr_stone(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t base)
{
    uint16_t sp = ux_mix(base, T_TEXT, 7);
    int32_t i, j;
    cv_rect(x, y, w, h, base);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            if ((i * 7 + j * 13) % 23 == 0 || (i * 11 + j * 5) % 31 == 0)
                cv_pset(i, j, sp);
}
/* (Skiff N3) THEME NEON's glass: SURF, its top edge lit violet (under every SURF cell) */
#define NE_VIOLET RGB(150, 70, 255)
static void ne_glass(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t base)
{
    cv_rect(x, y, w, h, base);
    cv_rect(x, y, w, 1, ux_mix(base, NE_VIOLET, 35));
}
static void cv_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    (void)r;
    (void)under;
    GFX_HOOK_CELL(x, y + cv_oy, x + w, y + h + cv_oy);   /* (the lint: what is drawn on it stays inside it) */
    if (ux.crypt && c == ux.surf)                    /* ((Skiff) CRYPT: a SURF cell is stone) */
        cr_stone(x, y, w, h, c);
    else if (ux.neon && c == ux.surf)                /* ((Skiff N3) NEON: glass) */
        ne_glass(x, y, w, h, c);
    else
        cv_rect(x, y, w, h, c);
}

/* ------------------------------------------------- 4-bit alpha blit --- */
/* coverage curves: a little heavier for light ink on dark, lighter for dark ink on light */
static const uint8_t CURVE_DARK[16] = {0, 24, 43, 62, 80, 97, 114, 130, 147, 163, 178, 194, 210, 225, 240, 255};
static const uint8_t CURVE_LIGHT[16] = {0, 12, 27, 42, 58, 75, 91, 109, 126, 144, 162, 180, 199, 217, 236, 255};
#define NRAMP 6u
static struct {
    uint16_t fg[NRAMP], bg[NRAMP], v[NRAMP][16];   /* v: byte-swapped panel values */
    uint32_t gen;
    uint8_t next, n;
} rc;

static const uint16_t *ramp(uint16_t fg, uint16_t bg)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    const uint8_t *cv = ux.light ? CURVE_LIGHT : CURVE_DARK;
    uint32_t i, a, k;
    uint16_t *v;
    GFX_HOOK_INK(fg, bg);
    if (rc.gen != ux.gen) {
        rc.gen = ux.gen;
        rc.n = rc.next = 0;
    }
    for (i = 0; i < rc.n; i++)
        if (rc.fg[i] == fg && rc.bg[i] == bg)
            return rc.v[i];
    i = rc.next;
    rc.next = (uint8_t)((i + 1u) % NRAMP);
    if (rc.n < NRAMP)
        rc.n++;
    rc.fg[i] = fg;
    rc.bg[i] = bg;
    v = rc.v[i];
    for (a = 0; a < 16u; a++) {
        uint32_t out = 0;
        int32_t w = cv[a];
        for (k = 0; k < 3u; k++) {
            int32_t x = (bg >> SH[k]) & MK[k], y = (fg >> SH[k]) & MK[k];
            out |= (uint32_t)(x + (((y - x) * w + 128) >> 8)) << SH[k];
        }
        v[a] = swap16(out);
    }
    return v;
}

/* (Skiff N2) THEME NEON's text effect, cv_fx_on .. cv_fx_off around a text in a pixel face (cv_alpha_px): its rows
 * slanted, a px right every 3 rows above the baseline (an italic, cv_fx_w px wider), and with a ramp its colour down
 * the capitals' rows, white to sky blue, violet at the horizon, orange to gold below it (80s chrome) */
#define FX_ROWS 32
static struct { uint8_t on; int32_t top, base; uint16_t col[FX_ROWS]; } cv_fx;
static const uint16_t FX_CHROME[7] = {RGB(255, 255, 255), RGB(196, 232, 255), RGB(130, 186, 255), RGB(176, 140, 255),
                                      RGB(255, 128, 84), RGB(255, 184, 96), RGB(255, 236, 150)};
static const uint8_t *fx_cap(const aafont_t *f)  /* f's capitals' rows (the theme's faces) */
{
    uint32_t k;
    for (k = 0; k < 3u && uf.f[k] != f; k++)
        ;
    return uf.cap[k];
}
static int32_t cv_fx_w(const aafont_t *f) { return (fx_cap(f)[1] - 1) / 3; }
static void cv_fx_on(int32_t y, const aafont_t *f, int chrome)   /* y: the text's (its line box's top) */
{
    const uint8_t *cap = fx_cap(f);
    int32_t r, h = cap[1] > 1 ? cap[1] : 2;
    cv_fx.on = 1;
    cv_fx.top = y + cap[0] + cv_oy;
    cv_fx.base = cv_fx.top + cap[1];
    for (r = 0; r < FX_ROWS; r++) {
        int32_t t = (r < h ? r : h - 1) * 600 / (h - 1);   /* 0 .. 600: the six steps between the stops */
        cv_fx.col[r] = chrome ? swap16(ux_mix(FX_CHROME[t / 100 < 6 ? t / 100 : 5], FX_CHROME[t / 100 < 6 ? t / 100 + 1 : 6],
                                              t / 100 < 6 ? t % 100 : 100)) : 0;
    }
}
static void cv_fx_off(void) { cv_fx.on = 0; }

/* w x h nibbles, rows back to back, at canvas (x, y) */
static void cv_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint16_t *rv)
{
    uint32_t k = 0, gx, gy;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row;
        if (py < cv_cy0 || py >= cv_cy1) {
            k += w;
            continue;
        }
        row = cv_px + (uint32_t)py * cv_w;
        for (gx = 0; gx < w; gx++, k++) {
            uint32_t v = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t px = x + (int32_t)gx;
            if (v && (uint32_t)px < cv_w)
                row[px] = rv[v];
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* (Skiff 2.0) a pixel face's glyph, its box w x h: S's 1x pixels (tools/gen_pixel_font.py) drawn sc & 3 times their
 * size and, PX_BOLD, each doubled to the right (a column wider); NEON's slant and chrome (cv_fx) */
static uint32_t nib(const uint8_t *d, uint32_t k) { return (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u; }
static void cv_alpha_px(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint16_t *rv, uint32_t sc)
{
    const uint32_t s = sc & 3u, b = sc >> 7, sw = (w - b) / s;
    uint32_t gx, gy;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy, sh = 0;
        uint32_t k = gy / s * sw;
        uint16_t *row, c = 0;
        if (py < cv_cy0 || py >= cv_cy1)
            continue;
        row = cv_px + (uint32_t)py * cv_w;
        if (cv_fx.on) {                              /* ((Skiff N2) NEON: slanted, chrome) */
            sh = py < cv_fx.base ? (cv_fx.base - 1 - py) / 3 : 0;
            c = cv_fx.col[py < cv_fx.top ? 0 : py - cv_fx.top < FX_ROWS ? py - cv_fx.top : FX_ROWS - 1];
        }
        for (gx = 0; gx < w; gx++) {
            uint32_t i = gx / s, v = (i < sw ? nib(d, k + i) : 0u) | (b && gx ? nib(d, k + i - 1u) : 0u);
            int32_t px = x + (int32_t)gx + sh;
            if (v && (uint32_t)px < cv_w)
                row[px] = c ? c : rv[v];
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* the same from a glyph's Huffman-coded bit stream (tools/gen_aa_font.py huff_pack): canonical codes, MSB
 * first; hc = the number of codes of each length 1..15, then the symbols in code order, each a value and
 * its repeat count ((count - 1) << 4 | value: runs of zeros and 15s). Codes of up to 8 bits come from a
 * 256-entry table made once per font in RAM (symbol | length << 8; 0 = a longer code: bit by bit). The
 * stream is read in order, so rows outside the clip are decoded and dropped; a run spans rows */
#define HC_FONTS 3                       /* the codes: the faces M and L (gen_aa_font.py HUFF), the icons (AI_HC) */
static struct { const uint8_t *hc; uint16_t lut[256]; } hc_tab[HC_FONTS];
static const uint16_t *hc_lut(const uint8_t *hc)
{
    uint32_t k, len, code = 0, sym = 15, i;
    for (k = 0; k < HC_FONTS - 1u && hc_tab[k].hc && hc_tab[k].hc != hc; k++)
        ;
    if (hc_tab[k].hc != hc) {                        /* (more fonts than slots: the last slot is rebuilt) */
        hc_tab[k].hc = hc;
        for (i = 0; i < 256u; i++)
            hc_tab[k].lut[i] = 0;
        for (len = 1; len <= 8u; len++, code <<= 1)
            for (i = 0; i < hc[len - 1u]; i++, code++, sym++) {
                uint32_t a = code << (8u - len), n = 1u << (8u - len);
                while (n--)
                    hc_tab[k].lut[a + n] = (uint16_t)(hc[sym] | len << 8);
            }
    }
    return hc_tab[k].lut;
}

static void cv_alpha_hc(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint8_t *hc,
                        const uint16_t *rv)
{
    const uint16_t *lut = hc_lut(hc);
    uint32_t gy, v = 0, run = 0, acc = 0, nb = 0;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row = py < cv_cy0 || py >= cv_cy1 ? 0 : cv_px + (uint32_t)py * cv_w;
        uint32_t gx = 0;
        while (gx < w) {
            uint32_t n;
            if (!run) {                              /* the next symbol */
                uint32_t e, s;
                while (nb < 16u) {                   /* (the data ends with 2 spare bytes) */
                    acc = acc << 8 | *d++;
                    nb += 8;
                }
                e = lut[(acc >> (nb - 8u)) & 255u];
                if (e) {
                    nb -= e >> 8;
                    s = e & 255u;
                } else {                             /* a code over 8 bits */
                    uint32_t code = 0, first = 0, idx = 0, len = 0;
                    for (;;) {
                        code |= (acc >> --nb) & 1u;
                        if (code - first < hc[len])
                            break;
                        idx += hc[len];
                        first = (first + hc[len]) << 1;
                        code <<= 1;
                        len++;
                    }
                    s = hc[15u + idx + code - first];
                }
                v = s & 15u;
                run = (s >> 4) + 1u;
            }
            n = run < w - gx ? run : w - gx;
            if (v && row) {
                int32_t px = cv_flip ? x + (int32_t)(w - 1u - gx) : x + (int32_t)gx, dx = cv_flip ? -1 : 1;
                uint32_t k;
                for (k = 0; k < n; k++, px += dx)
                    if ((uint32_t)px < cv_w)
                        row[px] = rv[v];
            }
            gx += n;
            run -= n;
        }
    }
    GFX_HOOK_PIXELS(w * h);
}

/* ------------------------------------------------------------- text --- */
/* the glyph number of ch in f, -1 = not in the face */
static int32_t glyph_at(const aafont_t *f, uint32_t ch)
{
    uint32_t i;
    if (ch >= f->first && ch <= f->last)
        return (int32_t)(ch - f->first);
    for (i = 0; i < f->nex; i++)
        if (f->ex[i] == ch)
            return (int32_t)(f->last - f->first + 1u + i);
    return -1;
}

/* the table entry of ch at phase 0 (its other phases follow it). Not in the face: the ellipsis as '.',
 * else '?', else the face's first glyph (L is sparse: its space) */
static uint32_t glyph(const aafont_t *f, uint32_t ch)
{
    int32_t k;
    if (ch >= 'a' && ch <= 'z' && f->last < 'a')
        ch -= 32u;                                   /* L has capitals only */
    if ((k = glyph_at(f, ch)) < 0 && (ch != (uint8_t)ELLIPSIS || (k = glyph_at(f, '.')) < 0) &&
        (k = glyph_at(f, '?')) < 0)
        k = 0;
    return (uint32_t)k << f->psh;
}

static int32_t kern(const aafont_t *f, uint32_t a, uint32_t b)       /* 1/16 px */
{
    uint32_t key = (a << 8) | b, lo = 0, hi = f->nk;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (f->kkey[m] == key)
            return f->kd[m];
        if (f->kkey[m] < key)
            lo = m + 1u;
        else
            hi = m;
    }
    return 0;
}

static uint32_t fold(const aafont_t *f, uint32_t c) { return c >= 'a' && c <= 'z' && f->last < 'a' ? c - 32u : c; }

static int32_t text_w(const aafont_t *f, const char *s)
{
    int32_t pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        if (prev)
            pen += kern(f, prev, c);
        pen += f->g[glyph(f, c)].adv;
        prev = c;
    }
    return (pen + 8) >> 4;
}

static int32_t text_run(int32_t x, int32_t y, const aafont_t *f, const char *s, const uint16_t *rv, int32_t *bb)
{
    int32_t pen = 0, err = 0, x0 = 0x7FFF, y0 = 0x7FFF, x1 = -0x7FFF, y1 = -0x7FFF;
    const int32_t step = 16 >> f->psh;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        const aag_t *g;
        int32_t pos, d0, d1;
        if (prev)
            pen += kern(f, prev, c);
        g = &f->g[glyph(f, c)];
        if (f->psh && g[1].off != g->off) {
            pos = pen - (pen & (step - 1));          /* the phase position at or left of the pen */
            d0 = pos - pen - err;                    /* .. and the one right of it: which keeps the gap truer */
            d1 = d0 + step;
            if ((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1))
                pos += step;
            g += ((uint32_t)pos >> (4u - f->psh)) & ((1u << f->psh) - 1u);
        } else {
            pos = (pen + 8) & ~15;                   /* one phase (figures, ., blanks): the rounded pen */
        }
        err = pos - pen;
        if (g->bw) {
            int32_t gx = x + (pos >> 4) + g->bx, gy = y + g->by;
            if (!rv)
                ;                                    /* (text_ink: measured only) */
            else if (f->hc)
                cv_alpha_hc(gx, gy, g->bw, g->bh, f->data + g->off, f->hc, rv);
            else if (f->sc)
                cv_alpha_px(gx, gy, g->bw, g->bh, f->data + g->off, rv, f->sc);
            else
                cv_alpha(gx, gy, g->bw, g->bh, f->data + g->off, rv);
            if (gx < x0) x0 = gx;
            if (gx + g->bw > x1) x1 = gx + g->bw;
            if (GFX_HOOKS && gy < y0) y0 = gy;       /* (the rows: for the host's lint only) */
            if (GFX_HOOKS && gy + g->bh > y1) y1 = gy + g->bh;
        }
        pen += g->adv;
        prev = c;
    }
    bb[0] = x0;
    bb[2] = x1 + (cv_fx.on && x1 > x0 ? (cv_fx.base - cv_fx.top - 1) / 3 : 0);   /* ((Skiff N2) slanted: wider) */
    if (GFX_HOOKS) {
        bb[1] = y0;
        bb[3] = y1;
    }
    return pen;
}
/* y = top of the line box (the baseline is y + f->asc); returns the pen x at the end.
 * fg on bg: bg is what lies under the text (the canvas, a card, a selection).
 * Placement (text_run): the pen (1/16 px) lies between two phase positions a step (16 >> psh) apart; the glyph takes
 * the one whose offset from the pen is nearer the previous glyph's offset, so the space between neighbours stays
 * truest and no glyph is a whole step from its true place (tools/aa_raster.py, text_spacing_test.py).
 * A glyph with one phase (its phases share one bitmap: figures and .) sits at the rounded pen, as when it
 * is drawn alone at text_w of what precedes it (ui_draw.c roll_text). */
static int32_t cv_text_flags(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                             uint32_t flags)
{
    int32_t b[4], pen = text_run(x, y, f, s, ramp(fg, bg), b);
    if (GFX_HOOKS && b[2] > b[0]) {
        int32_t y0 = b[1], y1 = b[3];
        if (b[0] < 0 || b[2] > (int32_t)cv_w || y0 + cv_oy < cv_cy0 || y1 + cv_oy > cv_cy1)
            flags |= 2u;                             /* cut by the canvas */
        if (cv_scroll) {                             /* a scrolled view: what lands on the screen is its visible part */
            flags |= 32u;
            if (y0 + cv_oy < cv_cy0) y0 = cv_cy0 - cv_oy;
            if (y1 + cv_oy > cv_cy1) y1 = cv_cy1 - cv_oy;
        }
        GFX_HOOK_PEN(y, f);
        GFX_HOOK_TEXT(b[0], y0 + cv_oy, b[2], y1 + cv_oy, s, flags);
    }
    (void)flags;
    return x + ((pen + 8) >> 4);
}
static int32_t cv_text_on(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return cv_text_flags(x, y, f, s, fg, bg, 0);
}
/* .. on the canvas background */
static int32_t cv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg)
{
    return cv_text_flags(x, y, f, s, fg, cv_bg, 0);
}
/* right-aligned: the advance ends at xr */
static int32_t cv_text_r(int32_t xr, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    int32_t w = text_w(f, s);
    cv_text_on(xr - w, y, f, s, fg, bg);
    return xr - w;
}
/* (centred: cv_text_in, by the ink; by the advance a text sat up to 1.5 px off) */

/* centring by ink: the ink of s in f drawn at (0, 0) across (b[0] .. b[2], past it; b[2] <= b[0]: none); up and down
 * a text is centred by its face's capitals' band (AF_S_CAP_Y ..: the ink of H; figures and round letters reach at most
 * a faint row past it), so the words of a row keep one baseline */
static void text_ink(const aafont_t *f, const char *s, int32_t *b) { text_run(0, 0, f, s, 0, b); }
#define HALF_UP(v) ((v) >> 1)                       /* half, a pixel left over going left / up (also below 0) */
/* the line top that centres face F's (S M) capitals' band in h rows (a row, a cell, a button): constant */
#define CAP_IN(F, h) (HALF_UP((h) - AF_##F##_CAP_H) - AF_##F##_CAP_Y)
/* the pen x that centres s's ink in w px */
static int32_t ink_in(const aafont_t *f, const char *s, int32_t w)
{
    int32_t b[4];
    text_ink(f, s, b);
    return HALF_UP(w - (b[2] - b[0])) - b[0];
}
/* s with its ink centred across x0 .. x0 + w (y: its line top, CAP_IN to centre it up and down too); a pixel left
 * over goes left / up, as everywhere (the cushions too) */
static int32_t cv_text_in(int32_t x0, int32_t y, int32_t w, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    return cv_text_on(x0 + ink_in(f, s, w), y, f, s, fg, bg);
}

/* s into d (n bytes), at most maxw px wide: never a smaller font, the end ellipsised. 1 = cut */
static int text_fit(char *d, uint32_t n, const char *s, const aafont_t *f, int32_t maxw)
{
    uint32_t k = 0;
    while (s[k] && k + 1u < n) {
        d[k] = s[k];
        k++;
    }
    d[k] = 0;
    if (!s[k] && text_w(f, d) <= maxw)
        return 0;
    if (k + 2u > n)
        k = n - 2u;
    for (;;) {                                       /* drop characters until "text..." fits */
        while (k && d[k - 1u] == ' ')
            k--;
        d[k] = ELLIPSIS;
        d[k + 1u] = 0;
        if (!k || text_w(f, d) <= maxw)
            return 1;
        k--;
    }
}
/* text in maxw px, ellipsised when it is longer: a label or a value should always fit (the host
 * layout lint reports a cut one), free text (cv_free_text: a preset name, a message) may be cut */
static int32_t cv_text_fit(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                           int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 1u : 0u);
}
static int32_t cv_free_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg,
                            int32_t maxw)
{
    char b[48];
    int cut = text_fit(b, sizeof b, s, f, maxw);
    return cv_text_flags(x, y, f, b, fg, bg, cut ? 9u : 8u);
}

/* ---------------------------------------------------------- keycaps --- */
/* a pre-rendered pill (tools/gen_aa_keycaps.py): nibble 0 outside, 1..4 the edge over `under`, 5 the fill,
 * 6..15 the label over the fill; the 16 colours per (under, fill, ink) are cached like the text ramps */
/* (Skiff) BLOCKS draws no keycaps: a key's name is a word, as the B mockup writes them (HOLD HOME: SHIFT; OCT- under
 * a dialog's buttons; a red M for a mute): the label in S on the keycap's band, in the keycap's colour (KEY: MID) */
#define KC_TY (HALF_UP(KC_H - AF_S_CAP_H + 1) - AF_S_CAP_Y)   /* the word's pen in the keycap's band (a row left over
                                                               * below: where the keycap's own label sat) */
static int32_t kc_w(uint32_t id) { return id < KC_COUNT ? text_w(&AF_S, KC[id].label) : 0; }
static uint32_t kc_cat(char *d, uint32_t k, uint32_t n, const char *s)   /* s after d[0..k), cut to n; the new k */
{
    while (*s && k + 1u < n)
        d[k++] = *s++;
    d[k] = 0;
    return k;
}
/* keycap / badge `id` with its top-left at (x, y), KC_H tall: fill and label tinted with tokens, `under` what lies
 * behind its corners; returns its right edge */
static int32_t cv_keycap(int32_t x, int32_t y, uint32_t id, uint16_t fill, uint16_t ink, uint16_t under)
{
    const kc_t *k = &KC[id < KC_COUNT ? id : 0u];
    (void)ink;
    return cv_text_on(x, y + KC_TY, &AF_S, k->label, fill == T_KEY ? T_MID : fill, under);
}

/* a key hint: the keycap, then what it does (S, TEXT); unavailable: a DIM keycap and a DIM word. The word sits on
 * the keycap's centre line (S caps 9 rows, the keycap's 7). act may be 0 (the keycap alone); returns the end x */
#define KH_GAP 4                                  /* keycap -> its word */
/* (Skiff) BLOCKS: a hint is one line of words, "KEY: WHAT", as the mockup's PRESETS: STEP */
static void kh_plain(char *b, uint32_t n, uint32_t id, const char *act)
{
    uint32_t k = kc_cat(b, 0, n, KC[id < KC_COUNT ? id : 0u].label);
    if (act && act[0])
        kc_cat(b, kc_cat(b, k, n, ": "), n, act);
}
static int32_t kh_w(uint32_t id, const char *act)
{
    char b[32];
    kh_plain(b, sizeof b, id, act);
    return text_w(&AF_S, b);
}
/* .. its ink: the keycap to the word's last inked column (centring a hint by what is seen) */
static int32_t kh_ink(uint32_t id, const char *act)
{
    int32_t b[4];
    char p[32];
    kh_plain(p, sizeof p, id, act);
    text_ink(&AF_S, p, b);
    return b[2];
}
static int32_t cv_key_hint(int32_t x, int32_t y, uint32_t id, const char *act, int on, uint16_t under)
{
    char b[32];                                    /* (all of it MID, as the mockup's footers; DIM: unavailable) */
    kh_plain(b, sizeof b, id, act);
    return cv_text_on(x, y + KC_TY, &AF_S, b, on ? T_MID : T_DIM, under);
}
/* a row of key hints from x0 to x1: the first at x0, the last ending at x1, the space shared between them. When
 * the row is too wide the action words go first, from the last hint back (the keycaps never shrink).
 * on: a bit per hint (1 = available) */
typedef struct { uint8_t key; const char *act; } khint_t;
static void cv_key_row(int32_t x0, int32_t x1, int32_t y, const khint_t *h, uint32_t n, uint32_t on, uint16_t under)
{
    uint32_t i, words = n;                         /* hints 0 .. words-1 keep their word */
    int32_t w, gap, x = x0;
    for (;;) {
        w = 0;
        for (i = 0; i < n; i++)
            w += kh_w(h[i].key, i < words ? h[i].act : 0);
        if (w + 6 * (int32_t)(n - 1u) <= x1 - x0 || !words)
            break;
        words--;
    }
    gap = n > 1u ? (x1 - x0 - w) / (int32_t)(n - 1u) : 0;
    for (i = 0; i < n; i++) {
        if (n > 1u && i + 1u == n)
            x = x1 - kh_w(h[i].key, i < words ? h[i].act : 0);   /* the last one flush right */
        x = cv_key_hint(x, y, h[i].key, i < words ? h[i].act : 0, (on >> i) & 1u, under) + gap;
    }
}

/* "[OCT+]..." -> the keycap's id and the length of the bracket, else -1 (a bracketed word that is no keycap is text) */
static int32_t kc_tag(const char *s, uint32_t *len)
{
    uint32_t i, n;
    if (s[0] != '[')
        return -1;
    for (n = 1; s[n] && s[n] != ']'; n++)
        if (n > 8u)
            return -1;
    if (s[n] != ']')
        return -1;
    for (i = 0; i < KC_COUNT; i++) {
        const char *l = KC[i].label;
        uint32_t k = 0;
        while (k + 1u < n && l[k] == s[k + 1u])
            k++;
        if (k + 1u == n && !l[k]) {
            *len = n + 1u;
            return (int32_t)i;
        }
    }
    return -1;
}
/* (Skiff) BLOCKS: a text's "[KEY]" tags as words, a colon after one a word follows: "HOLD [HOME] SHIFT" -> "HOLD
 * HOME: SHIFT", "[SAVE] + WHITE: STORE" -> "SAVE + WHITE: STORE" (colon 0: none, "[GLO] SET" -> "GLO SET") */
static void kc_words(char *d, uint32_t n, const char *s, int colon)
{
    uint32_t k = 0, len;
    while (*s && k + 1u < n) {
        int32_t id = kc_tag(s, &len);
        if (id < 0) {
            d[k++] = *s++;
            continue;
        }
        k = kc_cat(d, k, n, KC[id].label);
        s += len;
        if (colon && s[0] == ' ' && ((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) && k + 1u < n)
            d[k++] = ':';
    }
    d[k] = 0;
}
/* free text (S) that may start with a keycap, "[SAVE] HOLD TO UNDO", or have one after its first word,
 * "HOLD [GLO] QUICK": the word, the keycap, then the rest, cut to maxw (BLOCKS: kc_words) */
static int32_t cv_free_hint(int32_t x, int32_t y, const char *s, uint16_t fg, uint16_t bg, int32_t maxw)
{
    char b[64];
    kc_words(b, sizeof b, s, 1);
    return cv_free_text(x, y, &AF_S, b, fg, bg, maxw);
}

/* one-shot: a line of text in a box of colour bg, blitted (align: 0 left, 1 centre, 2 right) */
static void draw_text_line(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                           uint16_t c, uint16_t bg, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->h, bg);
    if (align == 1) {
        tx = ink_in(f, s, (int32_t)w);
        GFX_HOOK_ALIGN(0, 0, (int32_t)w, 0, AL_H, "one-shot line centred");
    }
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const aafont_t *f, const char *s,
                          uint16_t c, int align)
{
    draw_text_line(x, y, w, f, s, c, T_BG, align);
}
