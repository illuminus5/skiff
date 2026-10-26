/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff U5, V1) the page graphs for the pages that showed the output's scope (design/mock_u5.py, as the user picked
 * them): GLO's DELAY REVERB CHORUS GLOBAL, VOICE, VOICE 2, ANALOG's OSC and FLT, ARP and ARP 2 ((2.0, D 1A) ENV DEST's and
 * LFO DEST's went with their pages: MOD's grid).
 * Included by ui_graph.c; drawn on the panel's canvas from its top (cv_oy 0: y 0 .. 137), in the track's colour c.
 * Of the curves of a page the knob just turned's is the bright one (else the biggest amount's), the others MID, a
 * zero one DIM. */

static void pg_text(int32_t x, int32_t base, const char *s, uint16_t fg) { cv_text(x, base - AF_S.asc, &AF_S, s, fg); }
static void pg_text_r(int32_t xr, int32_t base, const char *s, uint16_t fg)
{
    cv_text_r(xr, base - AF_S.asc, &AF_S, s, fg, T_SURF);
}
static void pg_hline(int32_t x0, int32_t x1, int32_t y, uint16_t c) { cv_rect(x0, y, x1 - x0 + 1, 1, c); }

/* DELAY: the dry hit, then an echo every TIME, the first MIX high, each next FEEDBACK lower (fx.c fx_buses), over a
 * bar's beats (TIME longer than a beat: four echoes, over bars) */
static const uint16_t DLY_6TH[] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384, 18, 9, 36};   /* N_DTIME in sixths of a 1/16
                                                                                       * ((1.3) the dotted ones too) */
static void graph_delay(uint16_t c)
{
    uint32_t tap = DLY_6TH[song.g[G_DTIME] % 13], win = tap * 4u > 96u ? tap * 4u : 96u, unit = win > 96u ? 96u : 24u;
    int32_t lvl = 1000, i, n = (int32_t)(win / unit), w = (int32_t)(tap * 208u / win) - 3;
    char b[8];
    for (i = 0; i < n; i++) {                        /* the beats (bars), every one named up to 8, else every 4th */
        int32_t x = 14 + (int32_t)((uint32_t)i * unit * 208u / win);
        cv_rect(x, 123, 2, 3, T_DIM);
        if (n <= 8 || i % 4 == 0) {
            fmt_int(b, i + 1);
            pg_text(x, 136, b, T_DIM);
        }
    }
    w = w > 13 ? 13 : w < 2 ? 2 : w;
    int32_t qpx = -1, qpy = 0;                       /* ((Skiff Q4) QUARK: the last nucleus, its decay to the next) */
    if (ux.neon) {                                   /* (Skiff N4) NEON: the echoes run off toward the sun on the grid */
        ne_sun(176, 64, 26, 55);
        ne_grid(PANEL_X0, PANEL_X0 + PANEL_W, 64, 121, NE_VIOLET, T_SURF);
    }
    for (i = 0; i < 9 && lvl >= 40; i++) {
        int32_t x = 14 + (int32_t)((uint32_t)i * tap * 208u / win), h = lvl / 10;
        if (x + w > 228)
            break;
        if (ux.crypt) {                              /* (Skiff C4) CRYPT: # columns, the dry hit under a torch, the echoes
                                                      * under skulls, darker each */
            int32_t r, n = h * 9 / 100 > 1 ? h * 9 / 100 : 1;   /* (9 at most: the torch under DRY) */
            uint16_t col = i == 0 ? T_TEXT : i == 1 ? T_THEME : i < 4 ? CR_EMBER : i < 6 ? CR_BLOOD : T_DIM;
            for (r = 0; r < n; r++)
                cr_ch(x, 120 - r * 9, &AF_S, '#', col);
            if (i == 0)
                cr_torch(x, 120 - n * 9 - 13, 0, 1);
            else
                cr_skull(x - 2, 120 - n * 9 - 11, col, T_SURF, 0);
        } else if (ux.quark) {                       /* (Skiff Q4) QUARK: the echoes a decay chain: a stem, its nucleus
                                                      * on it, smaller each; the decay an arrow from the last */
            int32_t hq = h * 8 / 10, nx = x + w / 2, ny = 120 - hq - 8;   /* (8/10 tall: the nucleus clear of DRY) */
            cv_rect(nx - 1, 120 - hq, 2, hq, i ? ux_mix(T_THEME, T_SURF, 12 * i) : T_TEXT);
            qk_nucleus(nx, ny, i ? T_THEME : T_ACCENT, 9 - i > 4 ? 9 - i : 4);
            if (qpx >= 0) {
                cv_line(qpx + 8, qpy, nx - 8, ny, T_DIM);
                cv_line(nx - 8, ny, nx - 11, ny - 2, T_DIM);
                cv_line(nx - 8, ny, nx - 11, ny + 2, T_DIM);
            }
            qpx = nx;
            qpy = ny;
        } else if (ux.neon) {                        /* (.. each a tube ghosting wider, cyan left, pink right) */
            cv_rect(x - i, 120 - h, w, h, ux_mix(T_ACCENT, T_SURF, 35 + 6 * i));
            cv_rect(x + i, 120 - h, w, h, ux_mix(T_THEME, T_SURF, 35 + 6 * i));
            cv_rect(x, 120 - h, w, h, i ? ux_mix(T_TEXT, NE_VIOLET, 30 + 8 * i) : T_TEXT);
        } else
        cv_rect(x, 120 - h, w, h, i ? ux_mix(c, T_SURF, 15 + 50 * (i - 1) / 6) : T_TEXT);
        lvl = i ? lvl * song.g[G_DFDBK] * 230 / 32768 : song.g[G_DMIX] * 1000 / 127;
    }
    pg_text(14, 16, "DRY", T_MID);
}

/* REVERB: the first reflections, then the tail SIZE long, its top dimmed by DAMP; SPRING: its coil over it; (Skiff
 * phase 9) HALL: the tail twice as long */
static void graph_reverb(uint16_t c)
{
    static const uint8_t ER[5] = {84, 60, 72, 46, 52};
    int32_t hall = song.g[G_RTYPE] == 2;
    int32_t i, x, e = 32768, len = (40 + song.g[G_RSIZE] * 144 / 127) * (hall ? 2 : 1), k = 32768 - 98304 / len;
    int32_t dim = 20 + song.g[G_RDAMP] * 60 / 127;   /* % of the tail's height dimmed */
    if (ux.crypt) {                                  /* (Skiff C4) CRYPT: the reflections ! columns, the tail shade
                                                      * characters # * + : . fading along it, its top blood (DAMP);
                                                      * SPRING its coil ~ */
        static const char SH[5] = {'#', '*', '+', ':', '.'};
        int32_t r;
        for (i = 0; i < 5; i++)
            for (r = 0; r < ER[i] / 9; r++)
                cr_ch(12 + i * 5, 126 - r * 9, &AF_S, '!', T_TEXT);
        for (x = 44; x <= 222; x += 6) {
            int32_t h = 84 * e >> 15, n = h / 9, t = (x - 44) * 100 / 184;
            for (r = 0; r < n; r++) {
                int top = r >= n * (100 - dim) / 100;
                int32_t si = t * 4 / 100 + top;
                cr_ch(x, 126 - r * 9, &AF_S, SH[si > 4 ? 4 : si], top ? CR_BLOOD : t < 35 ? T_THEME : CR_EMBER);
            }
            for (r = 0; r < 6; r++)
                e = e * k >> 15;
        }
        if (song.g[G_RTYPE] == 1) {
            for (x = 64; x <= 222; x += 6)
                cr_ch(x, 17, &AF_S, '~', T_MID);
            pg_text(12, 17, "SPRING", T_MID);
        } else
            pg_text(12, 17, hall ? "HALL" : "ROOM", T_MID);
        return;
    }
    for (i = 0; i < 5; i++) {                        /* ((Skiff phase 9) NEON: lit tubes; QUARK: calorimeter towers) */
        if (ux.neon)
            ne_tube(12 + i * 6, 126 - ER[i], 3, ER[i], T_TEXT, T_SURF);
        else if (ux.quark)
            qk_tower(12 + i * 6 - 1, 126, 4, 84, ER[i], T_TEXT, 0);
        else
            cv_rect(12 + i * 6, 126 - ER[i], 3, ER[i], T_TEXT);
    }
    for (x = 44; x <= 228; x++) {
        int32_t h = 84 * e >> 15, top = h * dim / 100, t = (x - 44) * 100 / 184, y;
        if (ux.neon) {                               /* ((Skiff phase 9) NEON: the sun's bands under it, DAMP's top violet) */
            cv_rect(x, 126 - h, 1, top, ux_mix(NE_VIOLET, T_SURF, 45 + t * 30 / 100));
            ne_band_col(x, 126 - h + top - 3, 126 - 84, 126);
        } else if (ux.quark) {                       /* ((Skiff phase 9) QUARK: a Monte Carlo fill, DAMP's top sparse) */
            for (y = 126 - h; y < 126 - h + top; y++)
                if ((x + y) % 3 == 0 && y % 2 == 0)
                    cv_rect(x, y, 1, 1, T_DIM);
            qk_mc(x, 126 - h + top - 2, 126);
        } else {
            cv_rect(x, 126 - h, 1, top, ux_mix(c, T_SURF, 55 + t * 30 / 100));
            cv_rect(x, 126 - h + top, 1, h - top, ux_mix(c, T_SURF, t * 60 / 100));
        }
        e = e * k >> 15;
    }
    if (song.g[G_RTYPE] == 1) {
        pg_text(12, 17, "SPRING", T_MID);
        for (x = 64; x <= 228; x++)
            cv_pset(x, 13 + sine_i((uint32_t)(x - 64) * 0x0D000000u) * 3 / 32768, T_MID);
    } else
        pg_text(12, 17, hall ? "HALL" : "ROOM", T_MID);
}

/* (Skiff phase 9) FLANGER's and PHASER's response at x of 0 .. 216 (low .. high), Q15 (32768: as the dry; up to
 * twice): FLANGER the comb of the dry and its copy, DEPTH more teeth, FEEDBACK sharper and higher; PHASER the two
 * notches of its four stages, DEPTH wider, FEEDBACK a peak beside each */
static int32_t pg_cho_resp(uint32_t mode, int32_t x)
{
    int32_t fb = song.g[G_CFDBK], dp = song.g[G_CDEPTH], v, k, h;
    if (mode == 1u) {
        uint32_t n = 4u + (uint32_t)dp * 6u / 127u;
        v = sine_i((uint32_t)x * n * 9942053u + 0x40000000u);   /* (cos: 2^31 / 216 a px and tooth) */
        v = v < 0 ? -v : v;
        k = (v * v >> 15) * (v * v >> 15) >> 15;
        return (v * (127 - fb) + k * fb) / 127 * (127 + fb) / 127;
    }
    for (h = 32768, k = 0; k < 2; k++) {
        int32_t d = x - (k ? 150 : 66), w = 10 + dp / 8, r = d - (d < 0 ? -2 : 2) * w;
        h -= 30000 * w * w / (w * w + d * d);
        h += fb * 128 * w * w / (w * w + r * r);
    }
    return h < 0 ? 0 : h;
}
/* CHORUS: the dry sound and its copy, its pitch drifting (fx.c: one modulated delay): RATE how fast, DEPTH how far;
 * (Skiff phase 9) FEEDBACK: a fainter second pass; FLANGER, PHASER: their response across the spectrum, drawn as ANALOG's
 * FLT draws its curve in each theme */
static void graph_chorus(uint16_t c)
{
    int32_t x, py = 74, a = song.g[G_CDEPTH] * 44 / 127, fb = song.g[G_CFDBK], b = a * fb / 127, ey;
    uint32_t inc = (0xFFFFFFFFu / 216u) * (uint32_t)(1 + song.g[G_CRATE] * 5 / 127), mode = (uint32_t)song.g[G_CMODE];
    if (mode == 1u || mode == 2u) {
        if (ux.crypt) {                              /* (CRYPT: ASCII art over its shade, 37 columns of 9 lines, a skull on
                                                      * its highest peak) */
            uint8_t L[37];
            int32_t pk = 0;
            for (x = 0; x < 37; x++) {               /* (the skull's peak: the highest, nearest the middle) */
                L[x] = (uint8_t)clamp(8 - pg_cho_resp(mode, x * 6) * 4 / 32768, 0, 8);
                if (L[x] < L[pk] || (L[x] == L[pk] && (x > 18 ? x - 18 : 18 - x) < (pk > 18 ? pk - 18 : 18 - pk)))
                    pk = x;
            }
            cr_dots(12, 34, 6, 10, L, 37, 9, T_DIM);
            cr_art(12, 34, 6, 10, L, 37, T_THEME);
            cr_skull(12 + pk * 6 - 4, 34 + L[pk] * 10 - 12, CR_FLAME, T_SURF, 0);
        } else {
            for (x = 0; x <= 216; x++) {             /* (under it: NEON the sun's bands, QUARK a Monte Carlo fill) */
                int32_t y = 124 - pg_cho_resp(mode, x) * 40 / 32768;
                pg_under(12 + x, y, 44, 124, ux_mix(c, T_SURF, 75));
                if (x)
                    cv_line_t(12 + x - 1, py, 12 + x, y, c, 3);
                py = y;
            }
            pg_hline(12, 228, 84, T_LINE);           /* (the dry's level) */
        }
        pg_text(12, 16, mode == 1u ? "THE DRY AND ITS COPY: A COMB, SWEPT" : "FOUR STAGES: TWO NOTCHES, SWEPT", T_MID);
        return;
    }
    pg_hline(12, 228, 74, T_MID);
    if (ux.crypt) {                                  /* (Skiff C4) CRYPT: the copy a snake, 5 lines of 8 px either way;
                                                      * ((phase 9) FEEDBACK: its copy ASCII art behind, MID) */
        uint8_t L[37], L2[37];
        int32_t i, A = (a + 4) / 8, B = (b + 4) / 8;
        for (i = 0; i < 37; i++) {
            L[i] = (uint8_t)(5 + (sine_i((uint32_t)(i * 6) * inc) * A + (A ? 16384 : 0)) / 32768);
            L2[i] = (uint8_t)(5 + (sine_i((uint32_t)(i * 6) * inc - 0x30000000u) * B + (B ? 16384 : 0)) / 32768);
        }
        if (fb)
            cr_art(12, 34, 6, 8, L2, 37, T_MID);
        cr_snake(12, 34, 6, 8, L, 36, T_THEME, CR_EMBER);   /* (its head and tongue inside the frame) */
        pg_text(12, 16, "DRY AND ITS DRIFTING COPY", T_MID);
        return;
    }
    if (fb) {                                        /* ((Skiff phase 9) FEEDBACK: the copy's copy, fainter, behind: NEON a
                                                      * ghost, QUARK dotted) */
        for (x = 0, py = 74; x <= 216; x += 2) {
            int32_t y = 74 + sine_i((uint32_t)x * inc - 0x30000000u) * b / 32768;
            if (ux.quark) {
                if (x % 6 == 0)
                    cv_rect(12 + x, y, 1, 1, T_MID);
            } else if (x) {
                cv_line_t(12 + x - 2, py, 12 + x, y, ux.neon ? ux_mix(c, T_SURF, 55) : T_MID, 1);
            }
            py = y;
        }
        py = 74;
    }
    for (x = 0; x <= 216; x += 2) {
        int32_t y = 74 + sine_i((uint32_t)x * inc) * a / 32768;
        if (x)
            cv_line_t(12 + x - 2, py, 12 + x, y, c, 3);
        py = y;
    }
    ey = 74 + sine_i(216u * inc) * a / 32768;        /* ((Skiff phase 9) its end, as LFO's now: NEON a white-hot trail into a
                                                      * sparkle, QUARK the electron) */
    if (ux.neon) {
        for (x = 0; x < 36; x++)
            cv_rect(228 - x, 74 + sine_i((uint32_t)(216 - x) * inc) * a / 32768, 1, 2, ux_mix(T_TEXT, c, x * 100 / 36));
        ne_spark(228, ey + 1, T_ACCENT, 2);
    } else if (ux.quark) {
        qk_disc(228, ey, 3, T_ACCENT);
    }
    pg_text(12, 16, "DRY AND ITS DRIFTING COPY", T_MID);
}

/* (Skiff phase 9) FX > DRIVE: the TYPE's transfer at the DRIVE, as fx.c track_dist shapes it (its low cut and tone left
 * out): the input across, the output up, the straight line dim behind it; at the right the TYPE, DIST and TONE (CRUSH:
 * the bits left, the hold). (2.0, D 2A) INSERT's shapers (ins_shape), DT_* to IT_* */
static int32_t pg_drive_at(uint32_t type, int32_t d, int32_t x)
{
    static const uint8_t DT_IT[DT_N] = {IT_SOFT, IT_HARD, IT_FOLD, IT_CRUSH, IT_FUZZ};
    int32_t g = 4096 + d * d * 2, y = ((x >> 2) * g) >> 10;
    if (!d)
        return x;                                    /* (DIST 0: no DIST at all) */
    if (type == DT_CRUSH)
        return mulq15(ins_shape(IT_CRUSH, x, (uint32_t)d / 11u), 32000 - d * 20);
    y = type == DT_SOFT ? softclip(y + 2400) - softclip(2400) : ins_shape(DT_IT[type], y, 0);
    return mulq15(clamp(y, -40000, 40000), 30000 - d * 120);
}
/* the inset: a sine's cycle of 88 samples through the TYPE, CRUSH's hold, the TONE's two-pole low-pass and the make-up,
 * as fx.c runs them (its second pass: the filter settled) */
static void pg_drive_wave(const track_t *t, uint32_t type, int32_t d, int32_t *y)
{
    uint32_t i, hm = type == DT_CRUSH ? (1u << ((uint32_t)d >> 5)) - 1u : 0u;
    int32_t lp1 = 0, lp2 = 0, h = 0, k = clamp((type == DT_CRUSH ? 32700 - d * 40 : 32000 - d * 95) +
                                              t->p[P_DTONE] * 150, 2000, 32700);
    for (i = 0; i < 176u; i++) {
        int32_t x = sine_i(i * 48806447u) * 3 / 4;   /* (2^32 / 88 a sample, -2.5 dBFS) */
        if (!(i & hm))
            h = pg_drive_at(type, d, x);
        lp1 += mulq15(h - lp1, k);
        lp2 += mulq15(lp1 - lp2, k);
        if (i >= 88u)
            y[i - 88u] = d ? lp2 : x;
    }
}
/* ((1.3) shared with INSERT's graph) a transfer curve and a wave: the curve of at() across the input (-32768 .. 32768),
 * the wave w (88 samples) at the right, in each theme's look; k: at()'s own values */
typedef struct { uint32_t type; int32_t d, a, b, c, mix; } pg_curve_t;
static void pg_curve_graph(int32_t (*at)(const pg_curve_t *, int32_t), const pg_curve_t *k, const int32_t *w, uint16_t c)
{
    int32_t i, py = 0, pk = 0;
    int16_t ys[117];
    for (i = 0; i <= 116; i++) {                     /* (the curve; its highest point, the first) */
        ys[i] = (int16_t)(66 - at(k, (i - 58) * 32768 / 58) * 58 / 32768);
        if (ys[i] < ys[pk])
            pk = i;
    }
    if (ux.crypt) {                                  /* (CRYPT, as LFO's: the straight line faint ASCII art, the curve a
                                                      * snake, its head at the end (39 columns of 3 px, 13 lines of 9); the
                                                      * wave as OSC's: ASCII art, blood in its teeth (22 of 4, 9 of 8)) */
        uint8_t L[39], S[39], W[22];
        for (i = 0; i < 39; i++) {
            L[i] = (uint8_t)clamp(6 - at(k, (i * 2 - 38) * 32768 / 38) * 6 / 32768, 0, 12);
            S[i] = (uint8_t)(12 - i * 12 / 38);
        }
        for (i = 0; i < 22; i++)
            W[i] = (uint8_t)clamp(4 - w[i * 4] * 4 / 32768, 0, 8);
        pg_hline(12, 128, 66, T_LINE);
        cv_rect(70, 8, 1, 117, T_LINE);
        pg_hline(140, 227, 88, T_LINE);
        cr_art(12, 8, 3, 9, S, 39, T_LINE);
        cr_snake(12, 8, 3, 9, L, 38, T_THEME, CR_EMBER);   /* (its head and tongue clear of the wave) */
        for (i = 0; i + 1 < 22; i++)
            if (W[i + 1] <= W[i])
                cr_dither(140 + i * 4, 56 + W[i] * 8 + 2, 4, (8 - W[i]) * 8 - 2, CR_BLOOD);
        cr_art(140, 56, 4, 8, W, 22, T_THEME);
    } else {
        for (i = 0; i <= 116; i++) {                 /* (under the curve, as ANALOG's FLT: NEON the sun's bands, QUARK a
                                                      * Monte Carlo fill) */
            pg_under(12 + i, ys[i], 8, 124, ux_mix(c, T_SURF, 75));
        }
        pg_hline(12, 128, 66, T_LINE);
        cv_rect(70, 8, 1, 117, T_LINE);
        pg_hline(140, 227, 88, T_LINE);
        cv_line(12, 124, 128, 8, T_DIM);             /* (the straight line: no drive) */
        for (i = 1; i <= 116; i++)
            cv_line_t(12 + i - 1, ys[i - 1], 12 + i, ys[i], c, 3);
        for (i = 0; i < 88; i++) {                   /* (the wave) */
            int32_t y = 88 - w[i] * 32 / 32768;
            if (i)
                cv_line_t(140 + i - 1, py, 140 + i, y, c, 2);
            py = y;
        }
        py = 88 - w[87] * 32 / 32768;                /* (its end, as LFO's now) */
        if (ux.quark) {                              /* (QUARK: a nucleus on the curve's highest point, the electron at
                                                      * the wave's end) */
            qk_nucleus(12 + pk, ys[pk], T_THEME, 7);
            qk_disc(227, py, 3, T_ACCENT);
        } else if (ux.neon) {                        /* (NEON: a sparkle at the curve's end; a white-hot trail into one at
                                                      * the wave's) */
            ne_spark(128, ys[116] + 1, T_ACCENT, 2);
            for (i = 0; i < 24; i++)
                cv_rect(227 - i, 88 - w[87 - i] * 32 / 32768, 1, 2, ux_mix(T_TEXT, c, i * 100 / 24));
            ne_spark(227, py + 1, T_ACCENT, 1);
        }
    }
}
static int32_t pg_drive_curve(const pg_curve_t *k, int32_t x) { return pg_drive_at(k->type, k->d, x); }
static void graph_drive(const track_t *t, uint16_t c)
{
    uint32_t type = (uint32_t)clamp(t->p[P_DTYPE], 0, DT_N - 1);
    int32_t d = t->p[P_DIST], w[88];
    pg_curve_t k = {type, d, 0, 0, 0, 0};
    char b[24];
    pg_drive_wave(t, type, d, w);
    pg_curve_graph(pg_drive_curve, &k, w, c);
    cv_text(140, 26 - AF_M.asc, &AF_M, N_DTYPE[type], d ? c : T_DIM);
    if (type == DT_CRUSH && d) {                     /* (the bits left of 16, each sample held) */
        fmt_int(b, 16 - d / 11);
        str_cpy(b + str_len(b), " BITS, HOLD ", 13);
        fmt_int(b + str_len(b), 1 << (d >> 5));
        pg_text(140, 42, b, T_MID);
    }
}

/* (Skiff 1.3) FX > INSERT and INSERT 2: the TYPE as fx.c track_insert runs it, in DRIVE's look (pg_curve_graph). The
 * drives and CRUSH: the transfer (DRIVE / BITS, blended by MIX) across the input, and a sine's cycle through it, TONE /
 * LPF and LEVEL (CRUSH: its held samples); the swept ones: the sweep over one of its cycles (PHASR and FLANG a triangle,
 * FLANG's squared, CHOR a sine, as deep as DEPTH) and two of its cycles at the right. OFF or MIX 0: the straight line */
static int32_t pg_ins_curve(const pg_curve_t *k, int32_t x)
{
    int32_t y;
    if (!k->type || !k->mix)
        return x;
    if(k->type==IT_COMP){
        int32_t g=(int32_t)cmp_target(cmp_abs(x),(uint32_t)clamp(k->a,0,127),(uint32_t)clamp(k->b,0,127));
        int32_t wet=(int32_t)((int64_t)x*g/16777216);
        return x+(int32_t)((int64_t)(wet-x)*k->mix/32768);
    }
    if (k->type >= IT_PHASER) {                      /* (x: the place in the sweep's cycle) */
        uint32_t ph = (uint32_t)(x + 32768) << 16;      /* (one whole cycle across: the end wraps to the start) */
        int32_t s = k->type == IT_CHORUS ? osc_sine(ph) : (int32_t)ins_tri(ph) - 32768;
        if (k->type == IT_FLANGER)
            s = (int32_t)(((uint32_t)(s + 32768) * (uint32_t)(s + 32768)) >> 16) - 32768;
        return s * k->b / 127;
    }
    if (k->type == IT_CRUSH)
        y = ins_shape(IT_CRUSH, x, ins_bits_sh(k->a));
    else
        y = (ins_shape(k->type, ((x >> 3) * (4096 + k->a * k->a * 3)) >> 9, 0) * LEVEL_Q12[k->c & 127]) >> 12;
    return x + (int32_t)(((int64_t)(clamp(y, -40000, 40000) - x) * k->mix) >> 15);
}
static void graph_insert(const track_t *t, uint16_t c)
{
    uint32_t ty = (uint32_t)clamp(t->p[P_ITYPE], 0, IT_N - 1), i;
    pg_curve_t k = {ty, 0, t->p[P_IA], t->p[P_IB], t->p[P_IC], ins_mixq(t->p[P_IMIX])};
    int32_t w[88], lp1 = 0, lp2 = 0, h = 0, kq = ins_lpk(ty == IT_CRUSH ? k.c : k.b);
    uint32_t hold = INS_HOLD[clamp(k.b, 0, 127) >> 3];
    for (i = 0; i < 176u; i++) {                     /* (the wave: a sine's cycle of 88 samples, its second pass) */
        int32_t x = sine_i(i * 48806447u) * 3 / 4, y;
        if(ty==IT_COMP){y=pg_ins_curve(&k,x);}
        else if (ty >= IT_PHASER) {
            y = pg_ins_curve(&k, (int32_t)((i % 44u) * 65536u / 44u) - 32768);   /* (two of its cycles) */
        } else {
            if (ty != IT_CRUSH || !(i % hold))
                h = pg_ins_curve(&k, x);
            lp1 += mulq16(h - lp1, (uint32_t)kq) * 2;
            lp2 += mulq16(lp1 - lp2, (uint32_t)kq) * 2;
            y = ty ? lp2 : x;
        }
        if (i >= 88u)
            w[i - 88u] = y;
    }
    pg_curve_graph(pg_ins_curve, &k, w, c);
    cv_text(140, 26 - AF_M.asc, &AF_M, N_ITYPE[ty], ty && k.mix ? c : T_DIM);
}

/* GLOBAL: a bar's 16 steps (a beat's tallest), the off steps late by SWING+ (seq: core.h swing_step_len) over a mark
 * where they would be straight, the beats numbered; the long and short steps' share */
static void graph_global(uint16_t c)
{
    int32_t i, sw = song.g[G_SWING], push = sw * 13 / 250;
    char b[16];
    for (i = 0; i < 16; i++) {
        int32_t x = 14 + i * 13, h = i % 4 == 0 ? 74 : i % 2 == 0 ? 52 : 40;
        if (ux.crypt) {                              /* (Skiff C4) CRYPT: # columns, ^ where an off step would be */
            int32_t r;
            if (i % 2) {
                cr_ch(x, 135, &AF_S, '^', T_DIM);
                x += push;
            }
            for (r = 0; r < h / 9; r++)
                cr_ch(x, 120 - r * 9, &AF_S, '#', i % 4 == 0 ? T_TEXT : i % 2 == 0 ? T_THEME : CR_EMBER);
            continue;
        }
        if (i % 2) {
            cv_rect(x, 125, 7, 3, T_DIM);
            x += push;
        }
        if (ux.quark)                                /* ((Skiff Q4) QUARK: calorimeter towers, the downbeats white) */
            qk_tower(x, 120, 7, 76, h, i % 4 == 0 ? T_TEXT : c, 0);
        else if (ux.neon)                            /* ((Skiff N4) NEON: VU columns, the downbeats white) */
            ne_vu(x, 120, 7, 76, h, i % 4 == 0 ? T_TEXT : 0, T_SURF, 0);
        else
        cv_rect(x, 120 - h, 7, h, i % 4 == 0 ? T_TEXT : i % 2 == 0 ? c : ux_mix(c, T_SURF, 35));
    }
    for (i = 0; i < 4; i++) {
        b[0] = (char)('1' + i);
        b[1] = 0;
        cv_text(14 + i * 52, 36 - AF_M.asc, &AF_M, b, T_MID);
    }
    str_cpy(b, "SWING ", 8);
    fmt_int(b + str_len(b), 50 + sw * 50 / 250);
    str_cpy(b + str_len(b), ":", 2);
    fmt_int(b + str_len(b), 50 - sw * 50 / 250);
    pg_text_r(228, 16, b, T_MID);
}

/* VOICE: what the mode does, and the GLIDE between two notes, its time under it */
static void graph_voice(const track_t *t, uint16_t c)
{
    static const char *const SAYS[] = {"POLY: A VOICE A NOTE", "MONO: ONE VOICE, NEW ATTACK",
                                       "LEG: ONE VOICE, NO NEW ATTACK", "UNI: ALL VOICES, ONE NOTE"};
    int32_t g = t->p[P_GLIDE], w = g ? 6 + g * 90 / 127 : 0;
    char b[16];
    const char *unit;
    pg_text(12, 16, SAYS[t->p[P_VOICE] & 3], T_MID);
    if (ux.crypt) {                                  /* (Skiff C4) CRYPT: the two notes and the glide as ASCII art, the
                                                      * glide's time a sword as long as it */
        uint8_t L[37];
        int32_t i;
        for (i = 0; i < 37; i++) {
            int32_t x = 12 + i * 6;
            L[i] = (uint8_t)(x <= 100 ? 4 : x >= 100 + w ? 0 : 4 - (x - 100) * 4 / (w ? w : 1));
        }
        cr_art(12, 34, 6, 14, L, 37, T_THEME);
        if (w)
            cr_sword(91, 112, w + 10, 1000, T_THEME, CR_GOLD, T_MID, T_LINE);
    } else {
    cv_rect(12, 92, 88, 11, T_MID);
    cv_line_t(100, 97, 100 + w, 39, c, 4);
    cv_rect(100 + w, 34, 128 - w, 11, c);
    }
    if (w && !ux.crypt) {
        cv_rect(100, 116, 1, 9, T_DIM);
        cv_rect(100 + w, 116, 1, 9, T_DIM);
        pg_hline(100, 100 + w, 120, T_DIM);
    }
    param_format(&TP[P_GLIDE], g, b, &unit);
    if (unit && *unit) {
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), unit, 6);
    }
    cv_text_in(100 + w / 2 - 40, 135 - AF_S.asc, 80, &AF_S, g ? b : "NO GLIDE", T_MID, T_SURF);
}

/* VOICE 2: the engine's voices (those sounding lit), the one the next note takes outlined (ROT: in turn; REUSE: the
 * nearest pitch); DETUNE: the UNI voices' spread (voice.c mono_play) */
static void graph_voice2(const track_t *t, uint16_t c)
{
    uint32_t np = trk_nvoice(t), i, mode = trk_vmode(t), uni = mode == V_UNISON;
    uint32_t rot = mode == V_POLY && !t->p[P_ALLOC] && !t->p[P_GLIDE];
    int32_t sp = 216 / (int32_t)np, w = sp - 6;
    char b[24];
    for (i = 0; i < np; i++) {
        int32_t x = 12 + (int32_t)i * sp, on = t->v[i].active;
        b[0] = (char)('1' + i);
        b[1] = 0;
        if (ux.crypt) {                              /* (Skiff C4) CRYPT: the voices skulls, those sounding lit, the next
                                                      * one gold in a double frame */
            int nx = rot && i == t->rr % np;
            cr_spritez(x + (w - 18) / 2, 29, SPR_SKULL, on ? T_THEME : nx ? CR_GOLD : T_DIM, T_SURF, 2);
            if (nx)
                cr_frame2(x - 2, 23, w + 4, 30, CR_GOLD);
            continue;
        }
        cv_rect(x, 24, w, 28, on ? c : T_RAISE);
        cv_text_in(x, 24 + CAP_IN(M, 28), w, &AF_M, b, on ? T_INK : T_MID, on ? c : T_RAISE);
        if (rot && i == t->rr % np) {
            cv_frame(x - 2, 22, w + 4, 32, T_TEXT);
            cv_frame(x - 1, 23, w + 2, 30, T_TEXT);
        }
    }
    if (rot) {
        str_cpy(b, "NEXT NOTE: VOICE ", 20);
        b[17] = (char)('1' + t->rr % np);
        b[18] = 0;
    } else
        str_cpy(b, mode != V_POLY ? "ALLOC: ONLY IN POLY" : "NEXT NOTE: THE NEAREST", 24);
    pg_text(12, 16, b, T_MID);
    for (i = 0; i < (uni ? np : 1u); i++) {
        int32_t k = 2 * (int32_t)i - (int32_t)(uni ? np - 1u : 0u);
        if (uni && np > 1u && np < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(np - 1u);
        if (ux.crypt) {                              /* (Skiff C4) CRYPT: a row of dashes each, - - - */
            int32_t x;
            for (x = 76; x <= 224; x += 6)
                cv_rect(x, 95 + k * t->p[P_DETUNE] * 40 / 889, 4, 2, !uni ? T_DIM : k >= -1 && k <= 1 ? T_THEME : CR_EMBER);
        } else
        cv_rect(76, 95 + k * t->p[P_DETUNE] * 40 / 889, 152, 2, !uni ? T_DIM : k == 0 || k == 1 || k == -1 ? c : T_MID);
    }
    pg_text(12, 100, uni ? "DETUNE" : "UNI ONLY", T_DIM);   /* ((1.3) DETUNE's: SPREAD is the stereo one) */
}

/* ANALOG's OSC: the wave three times as the oscillator makes it (SAW SQR TRI SIN PWM), the second one (MIX in) under
 * it, a little out (DETUNE), NOISE's specks */
static int32_t osc_shape(uint32_t w, uint32_t ph)    /* +-1000 */
{
    int32_t f = (int32_t)(ph >> 22);                 /* 0..1023 */
    switch (w) {
    case 1: return f < 512 ? 1000 : -1000;
    case 2: return f < 512 ? -1000 + f * 4000 / 1024 : 3000 - f * 4000 / 1024;
    case 3: return sine_i(ph) * 1000 / 32768;
    case 4: return f < 256 ? 1000 : -1000;
    default: return -1000 + f * 2000 / 1024;
    }
}
static void graph_osc(const track_t *t, uint16_t c)
{
    uint32_t w = (uint32_t)t->p[P_E0] % 5u, cyc = 0xFFFFFFFFu / 72u, i;
    int32_t mix = t->p[P_E2], det = t->p[P_E1], x, py = 0, py2 = 0;
    char b[40], v[12];
    const char *unit;
    if (ux.crypt) {                                  /* (Skiff C4) CRYPT: the wave as ASCII art (8 lines of 12 px), blood in
                                                      * its teeth; the second one faint */
        uint8_t L[37], L2[37];
        int32_t i;
        for (i = 0; i < 37; i++) {
            L[i] = (uint8_t)(4 - (osc_shape(w, (uint32_t)(i * 6) * cyc) * 4 + (osc_shape(w, (uint32_t)(i * 6) * cyc) < 0 ? -500 : 500)) / 1000);
            L2[i] = (uint8_t)(4 - osc_shape(w, (uint32_t)(i * 6) * (cyc + cyc / 1000u * (uint32_t)det)) * 4 / 1000);
        }
        for (i = 0; i + 1 < 37; i++)
            if (L[i + 1] <= L[i])
                cr_dither(12 + i * 6, 24 + L[i] * 12 + 2, 6, (8 - L[i]) * 12 - 2, CR_BLOOD);
        if (mix > 0)
            cr_art(12, 24, 6, 12, L2, 37, T_DIM);
        if (mix < 127)
            cr_art(12, 24, 6, 12, L, 37, T_THEME);
    } else
    for (x = 0; x <= 216; x++) {
        int32_t y = 72 - osc_shape(w, (uint32_t)x * cyc) * 48 / 1000;
        int32_t y2 = 72 - osc_shape(w, (uint32_t)x * (cyc + cyc / 1000u * (uint32_t)det)) * 48 / 1000;
        if (x && mix > 0)
            cv_line_t(12 + x - 1, py2, 12 + x, y2, T_MID, 1);
        if (x && mix < 127)
            cv_line_t(12 + x - 1, py, 12 + x, y, c, 3);
        py = y;
        py2 = y2;
    }
    for (i = 0; i < (uint32_t)t->p[P_E3] / 3u; i++) {
        uint32_t h = (i + 1u) * 2654435761u;
        cv_rect(12 + (int32_t)(h % 216u), 24 + (int32_t)((h >> 16) % 96u), 2, 2, T_MID);
    }
    str_cpy(b, ENG_ANALOG.edit[0].names[w], 8);
    if (mix > 0 && det > 0) {
        param_format(&ENG_ANALOG.edit[1], det, v, &unit);
        str_cpy(b + str_len(b), ", A SECOND ONE ", 16);
        str_cpy(b + str_len(b), v, 8);
        str_cpy(b + str_len(b), " CT UP", 7);
    }
    pg_text(12, 16, b, T_MID);
}

/* ANALOG's FLT: the low pass's curve on a pitch axis: flat, a peak at CUTOFF as high as RES, falling after */
static void graph_flt(const track_t *t, uint16_t c)
{
    int32_t fc = t->p[P_E4] * 1000 / 127, res = t->p[P_E5], x, py = 0, xc = 12 + fc * 216 / 1000;
    uint16_t fill = ux_mix(c, T_SURF, 75);
    char b[16], v[12];
    const char *unit;
    if (ux.crypt) {                                  /* (Skiff C4) CRYPT: the curve as ASCII art (8 lines of 13 px) over its
                                                      * shade, a skull on its peak, a dagger at the cutoff */
        uint8_t L[37];
        int32_t i, pk = 0;
        for (i = 0; i < 37; i++) {
            int32_t f = i * 1000 / 36, d = fc - f, l = d >= 0 ? 620 + (d < 60 ? (380 * res / 127) * (3600 - d * d) / 3600 : 0) :
                                                                620 + 380 * res / 127 + d * 32 / 10;
            L[i] = (uint8_t)(8 - (l < 0 ? 0 : l) * 8 / 1000);
            if (L[i] < L[pk])
                pk = i;
        }
        cr_dots(12, 16, 6, 13, L, 37, 8, T_DIM);
        cr_art(12, 16, 6, 13, L, 37, T_THEME);
        cr_skull(12 + pk * 6 - 4, 16 + L[pk] * 13 - 12, CR_FLAME, T_SURF, 0);
        cr_dagger(xc - 4, 104, CR_GOLD);
    } else {
    cv_rect(xc, 8, 1, 114, T_DIM);
    for (x = 0; x <= 216; x++) {
        int32_t f = x * 1000 / 216, d = fc - f, peak = 620 + 380 * res / 127, l, y;
        if (d >= 0)
            l = 620 + (d < 60 ? (380 * res / 127) * (3600 - d * d) / 3600 : 0);
        else
            l = peak + d * 32 / 10;
        if (l < 0) l = 0;
        y = 120 - l * 104 / 1000;
        pg_under(12 + x, y, 16, 120, fill);          /* ((Skiff N4, Q4) NEON the sun's bands under it, QUARK a Monte
                                                      * Carlo fill) */
        if (x)
            cv_line_t(12 + x - 1, py, 12 + x, y, c, 3);
        py = y;
    }
    }
    param_format(&ENG_ANALOG.edit[4], t->p[P_E4], v, &unit);
    str_cpy(b, v, 10);
    if (unit && *unit) {
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), unit, 5);
    }
    if (ux.crypt && xc < 170)                        /* (Skiff C4) CRYPT: beside the dagger */
        pg_text(xc + 10, 117, b, CR_GOLD);
    else if (ux.crypt)
        pg_text_r(xc - 6, 117, b, CR_GOLD);
    else if (xc < 170)
        pg_text(xc + 5, 20, b, T_TEXT);
    else
        pg_text_r(xc - 4, 20, b, T_TEXT);
    pg_text(12, 135, "LOW", T_DIM);
    pg_text_r(228, 135, "HIGH", T_DIM);
}

/* ARP and ARP 2: the arp's notes over 16 of its steps, as arp_step plays them (the held notes, or C E G as an
 * example; ORDER, OCTAVE, MODE), a tile as long as GATE, the off steps late by SWING; the steps CHANCE would skip (a
 * fixed pick of about as many) outlined; RND and WALK's picks fixed too */
static void graph_arp(const track_t *t, uint16_t c)
{
    static const uint8_t EX[3] = {60, 64, 67};
    uint32_t list[65], cnt = t->nheld ? t->nheld : 3u, len = 0, i, j, o, m = (uint32_t)t->p[P_AMODE];
    uint32_t oct = t->p[P_AOCT] > 0 ? (uint32_t)t->p[P_AOCT] : 1u;
    int32_t pitch, th, base, gw = 2 + t->p[P_AGATE] * 10 / 127, sw = t->p[P_ASWING] * 12 / 250;
    char nb[6];
    if (t->nheld && (len = arp_list(t, list, &cnt)) == 0u) {   /* (the notes as they play now: TRN, SCALE since) */
        list[0] = 60;                               /* (every key silent now: one row, no tiles' notes matter) */
        len = cnt = 1;
    }
    if (!t->nheld) {
        for (i = 0; i < cnt; i++)
            list[i] = EX[i];
        for (o = 0; o < oct; o++)
            for (i = 0; i < cnt && len < 64u; i++)
                list[len++] = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (m == AM_REPEAT) {
            list[0] = 67u;
            len = 1;
        }
    }
    if (m == AM_UP8)
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * (int32_t)oct, 0, 127);
    pitch = 102 / (int32_t)len > 18 ? 18 : 102 / (int32_t)len;
    th = pitch >= 12 ? 10 : pitch > 3 ? pitch - 1 : 2;
    base = 115 - (102 - ((int32_t)len - 1) * pitch) / 2;   /* the rows centred */
    for (i = 0; i < len; i++) {                      /* the rows, low to high, named when there is room */
        int32_t y = base - (int32_t)i * pitch, x;
        for (x = 30; x <= 228; x += 3)
            cv_pset(x, y, T_LINE);
        if (pitch >= 12) {
            str_cpy(nb, N_NOTE[list[i] % 12u], 4);
            pg_text(12, y + 4, nb, T_DIM);
        }
    }
    if (!t->nheld)
        pg_text_r(228, 13, "EXAMPLE: C E G", T_DIM);
    for (i = 0; i < 16u; i++) {
        uint32_t k = i % (2u * len > 2u ? 2u * len - 2u : 1u), r, n = 1;
        int32_t x = 30 + (int32_t)(i * 124u / 10u) + ((i & 1u) ? sw : 0), skip;
        uint32_t rows[4];
        switch (m) {
        case AM_DN: r = len - 1u - i % len; break;
        case AM_UPDN: r = k < len ? k : 2u * len - 2u - k; break;
        case AM_DNUP: r = k < len ? len - 1u - k : k - (len - 1u); break;
        case AM_RND:
        case AM_WALK: r = (i * 2654435761u >> 20) % len; break;
        case AM_CONV:
        case AM_DIVG:
            k = i % len;
            if (m == AM_DIVG) k = len - 1u - k;
            r = (k & 1u) ? len - 1u - k / 2u : k / 2u;
            break;
        case AM_PINKY: r = len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u; break;
        case AM_THUMB: r = len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u; break;
        default: r = i % len; break;
        }
        rows[0] = r;
        if (m == AM_CHORD)                           /* the first 4 held notes, an octave up each step */
            for (n = 0; n < (cnt < 4u ? cnt : 4u); n++)
                rows[n] = (i % oct) * cnt + n;
        skip = ((i * 2654435761u >> 25) & 127u) > (uint32_t)t->p[P_APROB];
        for (j = 0; j < n; j++) {
            int32_t y = base - (int32_t)rows[j] * pitch - th / 2;
            uint16_t col = !m ? T_DIM : i % 4u == 0u ? T_TEXT : c;
            if (ux.crypt) {                          /* (Skiff C4) CRYPT: a note a # (2x where there is room), a skipped one
                                                      * a crossed skull */
                if (skip)
                    cr_skull(x - 1, y + th / 2 - 4, T_MID, T_SURF, 1);
                else
                    cr_ch(x - 1, base - (int32_t)rows[j] * pitch + (pitch >= 16 ? 7 : 4), pitch >= 16 ? &AF_V : &AF_S, '#',
                          !m ? T_DIM : i % 4u == 0u ? T_TEXT : T_THEME);
                continue;
            }
            if (skip)
                cv_frame(x, y, gw, th, T_MID);
            else
                cv_rect(x, y, gw, th, col);
        }
    }
}
