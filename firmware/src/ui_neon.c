/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff N3) THEME NEON's widgets (design/mock_neon.py, A SUNSET, the user's pick): glass where BLOCKS lays a flat
 * tile (gfx.c ne_glass), a sunset on the hot card, the cards' gauges an 80s dash bargraph, neon lines; the motifs: the
 * striped sun, the grid floor, palms, the car and its light trails, sparkles, VHS snow. Our own pixel drawings, drawn on
 * the canvas (cv_*) when ux.neon; with any other THEME BLOCKS draws as before. Included by ui_graph.c. */

#define NE_YELLOW RGB(255, 222, 90)
#define NE_ORANGE RGB(255, 128, 56)
#define NE_RED RGB(255, 40, 80)
static const uint16_t NE_HOT[3] = {RGB(255, 222, 90), RGB(255, 128, 56), RGB(255, 88, 190)};   /* the hot card: dusk */
static const uint16_t NE_SUNSET[4] = {RGB(255, 232, 96), RGB(255, 150, 60), RGB(255, 64, 150), RGB(170, 40, 200)};

/* a colour along n stops, t of 1000 */
static uint16_t ne_ramp(const uint16_t *st, uint32_t n, int32_t t)
{
    int32_t s = (int32_t)n - 1, k;
    t = t < 0 ? 0 : t > 1000 ? 1000 : t;
    k = t * s / 1000;
    if (k >= s)
        k = s - 1;
    return ux_mix(st[k], st[k + 1], (t * s - k * 1000) / 10);
}

/* gradients over a box: down it (vgrad) or across it (hgrad) */
static void ne_vgrad(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *st, uint32_t n)
{
    int32_t j;
    for (j = 0; j < h; j++)
        cv_rect(x, y + j, w, 1, ne_ramp(st, n, h > 1 ? j * 1000 / (h - 1) : 0));
}
static void ne_hgrad(int32_t x, int32_t y, int32_t w, int32_t h, const uint16_t *st, uint32_t n)
{
    int32_t i;
    for (i = 0; i < w; i++)
        cv_rect(x + i, y, 1, h, ne_ramp(st, n, w > 1 ? i * 1000 / (w - 1) : 0));
}

/* the neon rule (the header's foot, the footer's top): pink, violet, cyan across (or back) */
static void ne_rule(int32_t y, int back)
{
    uint16_t st[3] = {T_THEME, NE_VIOLET, T_ACCENT};
    if (back) {
        st[0] = T_ACCENT;
        st[2] = T_THEME;
    }
    ne_hgrad(0, y, 240, 1, st, 3);
}

/* a card's gauge as an 80s dash bargraph: slanted segments 4 px apart, 5 rows, lit up to the value cyan, violet, pink
 * as they climb, the rest dark glass (the hot card: INK on its dusk) */
static void ne_dash(int32_t x, int32_t y, int32_t w, int32_t ratio, int hot)
{
    const uint16_t st[3] = {T_ACCENT, NE_VIOLET, T_THEME};
    int32_t n = (w - 2) / 4, lit = (n * (ratio > 1000 ? 1000 : ratio) + 500) / 1000, k, r;
    for (k = 0; k < n; k++) {
        uint16_t c = k < lit ? (hot ? T_INK : ne_ramp(st, 3, n > 1 ? k * 1000 / (n - 1) : 0))
                             : hot ? ux_mix(T_INK, NE_ORANGE, 55) : ux_mix(T_SURF, T_TEXT, 10);
        for (r = 0; r < 5; r++)
            cv_rect(x + k * 4 + (4 - r) / 2, y + r, 3, 1, c);
    }
}

/* a lit tube over a box: its glow 1 px round it (into `under`) and its hot top; drawn after: ne_tube_top */
static void ne_tube(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint16_t under)
{
    cv_rect(x - 1, y - 1, w + 2, h + 2, ux_mix(c, under, 62));
    cv_rect(x, y, w, h, c);
    if (w > 2 && h > 3)
        cv_rect(x + 1, y + 1, w - 2, 1, ux_mix(c, T_TEXT, 55));
}

/* a laser frame (the cursor): its glow 1 px out, the frame in the accent */
static void ne_laser(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t under)
{
    cv_frame(x - 1, y - 1, w + 2, h + 2, ux_mix(T_ACCENT, under, 60));
    cv_frame(x, y, w, h, T_ACCENT);
}

/* a disc, radius r, centre (cx, cy) */
static void ne_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t j, w;
    for (j = -r; j <= r; j++) {
        for (w = r; w * w + j * j > r * r + r; w--)
            ;
        cv_rect(cx - w, cy + j, 2 * w + 1, 1, c);
    }
}

/* REC: a red light, glowing (the selected track armed); another track armed: the light out, its glow */
static void ne_rec(int32_t cx, int32_t cy, int full)
{
    ne_disc(cx, cy, 5, ux_mix(T_REC, T_BG, 60));
    ne_disc(cx, cy, 3, full ? T_REC : ux_mix(T_REC, T_BG, 30));
    if (full)
        cv_rect(cx - 1, cy - 2, 2, 1, ux_mix(T_REC, T_TEXT, 60));
}

/* the car (side on, facing right: its tail light T, the glass, the headlight, the wheels), z times its size, in a 1 px
 * dark outline; its light trails behind it from the tail light's row, `len` px, fading into `under`. SPR_CAR 18 x 6 (the
 * splash), SPR_CAR24 24 x 8 (HOME: the user's "a little smaller" than SPR_CAR at 2x; tools/gen_sprites.py) */
static void ne_car(int32_t x, int32_t y, const uint8_t *car, int32_t z, uint16_t body, int32_t len, uint16_t under)
{
    const uint16_t ol[5] = {T_BG, T_BG, T_BG, T_BG, T_BG}, cols[5] = {body, T_ACCENT, NE_RED, NE_YELLOW, T_BG};
    int32_t i, t;
    for (t = 0; t < car[1] && spr_px(car, (uint32_t)t * car[0]) != 3u; t++)   /* (the tail light: key 3, T) */
        ;
    for (i = 0; i < len; i++) {                       /* the trails: the tail light's red, a cyan streak under it */
        cv_rect(x - 1 - i, y + t * z, 1, z, ux_mix(NE_RED, under, i * 100 / len));
        cv_rect(x - 1 - i, y + (t + 1) * z + 1, 1, 1, ux_mix(T_ACCENT, under, i * 140 / len < 100 ? i * 140 / len : 100));
    }
    for (i = 0; i < 4; i++)
        ui_sprite(x + (i == 0) - (i == 1), y + (i == 2) - (i == 3), car, 5u, ol, z);
    ui_sprite(x, y, car, 5u, cols, z);
}

/* a palm, 18 x 18, a silhouette in c */
static void ne_palm(int32_t x, int32_t y, uint16_t c) { ui_sprite(x, y, SPR_PALM, 1u, &c, 1); }

/* the striped sun: a half disc on the horizon hy, gold at its top to pink and purple, cut by gaps that widen downward
 * (dim: that much into the glass, behind a graph) */
static void ne_sun(int32_t cx, int32_t hy, int32_t r, int32_t dim)
{
    int32_t y, w;
    for (y = hy - r; y < hy; y++) {
        int32_t t = (y - (hy - r)) * 1000 / r, d = hy - y;
        if (t > 420 && (y % 5) < 1 + (t - 420) * 6 / 1000)
            continue;
        for (w = r; w * w + d * d > r * r; w--)
            ;
        cv_rect(cx - w, y, 2 * w + 1, 1, ux_mix(ne_ramp(NE_SUNSET, 4, t), T_SURF, dim));
    }
}

/* the grid floor from the horizon hy down to y1, between x0 and x1: rays from the vanishing point, rungs closer toward
 * the horizon, fading into `under` there (ne_grid_run: the rungs `ph` of 1000 of their way on toward the viewer, a
 * moving floor) */
static void ne_grid_run(int32_t x0, int32_t x1, int32_t hy, int32_t y1, uint16_t c, uint16_t under, int32_t ph);
static void ne_grid(int32_t x0, int32_t x1, int32_t hy, int32_t y1, uint16_t c, uint16_t under)
{
    ne_grid_run(x0, x1, hy, y1, c, under, 1000);
}
static void ne_grid_run(int32_t x0, int32_t x1, int32_t hy, int32_t y1, uint16_t c, uint16_t under, int32_t ph)
{
    int32_t cx = (x0 + x1) / 2, i, k, n = y1 - hy;
    for (k = 0; n > 0 && k <= n; k++) {
        uint16_t f = ux_mix(c, under, 75 - 75 * k / n);
        for (i = -14; i <= 14; i++) {
            int32_t x = cx + i * 26 * k / n;
            if (x >= x0 && x < x1)
                cv_rect(x, hy + k, 1, 1, f);
        }
    }
    for (k = 1; k <= 4; k++) {
        int32_t u = (k - 1) * 1000 + ph, y = hy + n * (u / 10) * (u / 10) / 160000;
        if (y > hy && y <= y1)
            cv_rect(x0, y, x1 - x0, 1, ux_mix(c, under, 75 - 75 * u / 4000));
    }
}

/* VHS snow (no signal: a muted track), the same each time for a seed: specks from `base` toward `speck` */
static void ne_snow(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t seed, uint16_t base, uint16_t speck)
{
    int32_t i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            uint32_t v;
            seed = seed * 1103515245u + 12345u;
            v = (seed >> 16) & 255u;
            if (v > 150u)
                cv_rect(i, j, 1, 1, ux_mix(base, speck, (int32_t)v * 60 / 255));
        }
}

/* a VU column h tall up to its foot y1: 4 px segments lit to fh px, cyan, violet, pink, gold up it (on: one colour
 * instead), the rest off; cap: the top lit one in it (a fader's cap; 0: none) */
static void ne_vu(int32_t x, int32_t y1, int32_t w, int32_t h, int32_t fh, uint16_t on, uint16_t off, uint16_t cap)
{
    const uint16_t st[4] = {T_ACCENT, NE_VIOLET, T_THEME, NE_YELLOW};
    int32_t n = h / 4, lit = (fh * n + h / 2) / h, k;
    for (k = 0; k < n; k++)
        cv_rect(x, y1 - (k + 1) * 4, w, 3, k >= lit ? off : cap && k == lit - 1 ? cap : on ? on :
                ne_ramp(st, 4, n > 1 ? k * 1000 / (n - 1) : 0));
}

/* a sparkle (a lens flare's star), centred on (x, y): its arms c, its heart white; z times its 7 x 7 */
static void ne_spark(int32_t x, int32_t y, uint16_t c, int32_t z)
{
    const uint16_t cols[2] = {c, T_TEXT};
    ui_sprite(x - 3 * z, y - 3 * z, SPR_SPARK, 2u, cols, z);
}

/* the sun's bands under a curve: column x from below y to the foot, gold at `top` to pink and purple at the foot, cut
 * by gaps that widen downward, dimmed into the glass (ne_bands: a curve's columns, ys[i] at x0 + i) */
static void ne_band_col(int32_t x, int32_t y, int32_t top, int32_t foot)
{
    for (y += 3; y < foot; y++) {
        int32_t t = (y - top) * 1000 / (foot - top);
        if (t > 350 && (y % 5) < 1 + (t - 350) * 6 / 1000)
            continue;
        cv_rect(x, y, 1, 1, ux_mix(ne_ramp(NE_SUNSET, 4, t), T_SURF, 30));
    }
}
static void ne_bands(int32_t x0, const int16_t *ys, int32_t n, int32_t top, int32_t foot)
{
    int32_t i;
    for (i = 0; i < n; i++)
        ne_band_col(x0 + i, ys[i], top, foot);
}
