/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * (Skiff, modified 2026-10-10) after ui_dx.c of JIANT FM by juanjiant-bit, https://github.com/juanjiant-bit/JIANT
 * (GPL-3.0-only, at 9d1c453): its follow of the keys and the mode's names; (phase 8, 2026-10-11) the page is Skiff's
 * own: the sound sheet (design/mock_p8.py, direction 2, chosen by the user). */
/* EDIT > SOUND on a DRUM-X track (ui.c page_visible): one sound of the project's kit (drumx_voice.c dx_kit, one for
 * every DRUM-X track: project.c SKF3), all of it on one sheet. Included by ui_input.c.
 *   The sound: a key struck on the track, or PRESETS (BD .. CB, the ends where they are; ui_input.c presets_turn).
 *   The knobs: a window of the sheet, moved by SELECT (ui_input.c select_turn; past its ends, the pages either side):
 *     PTCH A, PTCH B, PMOD A, PMOD B   /   DCAY A, DCAY B, NOIS A, NOIS B   /   COLR A, COLR B, LEVL, DRV   /
 *     WAVE, FILT, SNAP, PSHP (the pitch modulation: DECAY LONG NOISE SINE)
 *   A beside B: no side to choose. LEVL is the sound's lane level (P_LN0 + lane, as EDIT > LANES: automation too).
 * The panel (graph_dx): the kit's 8 sounds, the one edited in the track's colour; then the sheet, a row a value, A's bar
 * (the track's colour) and B's (white) with their numbers; LEVL and DRV; the switches in words; the window's rows on a
 * band with a mark at the left, a scroll bar at the right */
static const char *const DX_LANE_ABBR[8] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
static const char *const DX_WAVE[4] = {"SIN", "FM", "MTL", "BEL"};
static const char *const DX_FILT[3] = {"LP", "BP", "HP"};
static const char *const DX_PMOD[4] = {"DECAY", "LONG", "NOISE", "SINE"};
static const char *const DX_VAL[DXP_N] = {"PITCH", "P MOD", "DECAY", "NOISE", "COLOR"};
enum { DXK_A, DXK_B, DXK_LEVEL, DXK_DRV, DXK_WAVE, DXK_FILT, DXK_SNAP, DXK_PSHP };
#define DX_NWIN 4u
static const uint8_t DX_WIN[DX_NWIN][4][2] = {          /* each window's knobs: {what, the value} */
    {{DXK_A, DXP_PITCH}, {DXK_B, DXP_PITCH}, {DXK_A, DXP_PMOD}, {DXK_B, DXP_PMOD}},
    {{DXK_A, DXP_DECAY}, {DXK_B, DXP_DECAY}, {DXK_A, DXP_NOISE}, {DXK_B, DXP_NOISE}},
    {{DXK_A, DXP_COLOR}, {DXK_B, DXP_COLOR}, {DXK_LEVEL, 0}, {DXK_DRV, 0}},
    {{DXK_WAVE, 0}, {DXK_FILT, 0}, {DXK_SNAP, 0}, {DXK_PSHP, 0}},
};
#define DX_ROWS 7u                                      /* PTCH PMOD DCAY NOIS COLR, LEVL + DRV, MODE */
static const uint8_t DX_ROW0[DX_NWIN] = {0, 2, 4, 6}, DX_NROW[DX_NWIN] = {2, 2, 2, 1};   /* the rows a window is on */

static void dx_lane_turn(int32_t s) { dx_ui_lane = (uint8_t)clamp((int32_t)(dx_ui_lane & 7u) + (s > 0 ? 1 : -1), 0, 7); }
static const char *dx_title(void)                     /* the header's: "SOUND SD" */
{
    static char t[12];
    str_cpy(t, "SOUND ", sizeof t);
    str_cpy(t + 6, DX_LANE_ABBR[dx_ui_lane & 7u], sizeof t - 6u);
    return t;
}

/* KNOB k (0..3) turned s: the window's value; a switch one a detent */
static void dx_knob(uint32_t k, int32_t s)
{
    uint32_t l = dx_ui_lane & 7u, w = dx_ui_win % DX_NWIN, i = DX_WIN[w][k & 3u][1];
    dx_lane_t *L = &dx_kit[l];
    int32_t d = s > 0 ? 1 : -1;
    switch (DX_WIN[w][k & 3u][0]) {
    case DXK_A:
    case DXK_B: {
        uint8_t *v = DX_WIN[w][k & 3u][0] == DXK_A ? L->a : L->b;
        v[i] = (uint8_t)clamp(v[i] + accel(EN_K1 + k, s, 127), 0, 127);
        break;
    }
    case DXK_LEVEL: {                                   /* (as any knob: its automation too) */
        const param_desc_t *pd = &TP[P_LN0 + l];
        int16_t *vp = &TSEL->p[P_LN0 + l];
        *vp = (int16_t)param_turn(pd, *vp, accel(EN_K1 + k, s, pd->max - pd->min));
        motion_capture(TSEL, P_LN0 + l, *vp);
        break;
    }
    case DXK_DRV:
        L->mode = (uint8_t)(s > 0 ? L->mode | DX_DRIVE : L->mode & ~DX_DRIVE);
        break;
    case DXK_WAVE:
        L->mode = (uint8_t)((L->mode & ~3u) | (uint32_t)clamp((int32_t)(L->mode & 3u) + d, 0, 3));
        break;
    case DXK_FILT:
        L->mode = (uint8_t)((L->mode & ~0x0Cu) | (uint32_t)clamp((int32_t)((L->mode >> 2) & 3u) + d, 0, DXF_HP) << 2);
        break;
    case DXK_SNAP:
        L->mode = (uint8_t)(s > 0 ? L->mode | 0x10u : L->mode & ~0x10u);
        break;
    default:                                            /* PSHP */
        L->mode = (uint8_t)((L->mode & ~0x60u) | (uint32_t)clamp((int32_t)((L->mode >> 5) & 3u) + d, 0, 3) << 5);
        break;
    }
}

/* the four cards: the window's */
static void dx_cards(void)
{
    static const uint8_t ICON[DXP_N] = {ICON_PITCH, ICON_ENV, ICON_DECAY, ICON_NOISE, ICON_TONE};
    uint32_t l, w, k;
    const dx_lane_t *L;
    lane_follow(&dx_ui_lane);                           /* (a key struck picks the sound it plays: ui.c) */
    l = dx_ui_lane & 7u;
    w = dx_ui_win % DX_NWIN;
    L = &dx_kit[l];
    for (k = 0; k < 4u; k++) {
        uint32_t i = DX_WIN[w][k][1], m = L->mode;
        char lab[12], val[12];
        const char *unit = "";
        switch (DX_WIN[w][k][0]) {
        case DXK_A:
        case DXK_B: {
            uint32_t v = DX_WIN[w][k][0] == DXK_A ? L->a[i] : L->b[i];
            str_cpy(lab, DX_VAL[i], sizeof lab);       /* ((Skiff U4) the word in full: PITCH A) */
            str_cpy(lab + str_len(lab), DX_WIN[w][k][0] == DXK_A ? " A" : " B", 3);
            fmt_int(val, (int32_t)v);
            draw_column(k, lab, val, "", VR_LIVE, (int32_t)v * 1000 / 127, ICON[i]);
            break;
        }
        case DXK_LEVEL: {
            int16_t v = TSEL->p[P_LN0 + l];
            param_format(&TP[P_LN0 + l], v, val, &unit);
            draw_column(k, "LEVEL", val, unit, VR_LIVE, v * 1000 / 127, ICON_LEVEL);
            break;
        }
        case DXK_DRV:
            draw_column(k, "DRIVE", m & DX_DRIVE ? "ON" : "OFF", "", m & DX_DRIVE ? VR_LIVE : VR_DIM, m & DX_DRIVE ? 1000 : 0,
                        ICON_DRIVE);
            break;
        case DXK_WAVE:
            draw_column(k, "WAVE", DX_WAVE[m & 3u], "", VR_LIVE, (int32_t)(m & 3u) * 333, ICON_WAVE);
            break;
        case DXK_FILT:
            draw_column(k, "FILTER", DX_FILT[((m >> 2) & 3u) % 3u], "", VR_LIVE, (int32_t)((m >> 2) & 3u) * 500, ICON_CUTOFF);
            break;
        case DXK_SNAP:
            draw_column(k, "SNAP", (m >> 4) & 1u ? "ON" : "OFF", "", (m >> 4) & 1u ? VR_LIVE : VR_DIM, (m >> 4) & 1u ? 1000 : 0,
                        ICON_ENV);
            break;
        default:
            draw_column(k, "P SHAPE", DX_PMOD[(m >> 5) & 3u], "", VR_LIVE, (int32_t)((m >> 5) & 3u) * 333, ICON_ENV);
            break;
        }
    }
}

/* what the cards and the panel show changed (the lane's LEVL: ui_graph.c graph_signature hashes the track's values) */
static uint32_t dx_sig(void)
{
    const dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    uint32_t h = (uint32_t)dx_ui_lane * 31u + dx_ui_win * 977u + L->mode * 131u, i;
    for (i = 0; i < DXP_N; i++)
        h = (h ^ (uint32_t)L->a[i] << 8 ^ L->b[i]) * 16777619u;
    return h;
}

/* the panel (ui_graph.c draw_graph, GR_DXSND), from its top: BLOCKS' 138 px (rows 14 px), FLAT's and LINE's 122 (12) */
#define DX_TW 26                                        /* a sound's tile: 26 x 16, 2 px apart (x 9 .. 231) */
#define DX_AX 46                                        /* A's bar (62 px, its number right at 130), B's (at 226) */
#define DX_BX 142
#define DX_BW 62
/* a bar of w px for v (0..127), then its text right at x + 84 */
static void dx_row_bar(int32_t x, int32_t w, int32_t y, int32_t rh, uint32_t v, uint16_t col, uint16_t bg, const char *txt)
{
    cv_rect(x, y + (rh - 4) / 2, w, 4, T_RAISE);
    cv_rect(x, y + (rh - 4) / 2, (int32_t)v * w / 127, 4, col);
    GFX_HOOK_ALIGN(0, y, 0, y + rh, AL_V, "DRUM-X sheet number on its row's middle");
    cv_text_r(x + 84, y + CAP_IN(S, rh), &AF_S, txt, T_MID, bg);
}
static void graph_dx(const track_t *t, uint16_t c)
{
    const dx_lane_t *L = &dx_kit[dx_ui_lane & 7u];
    uint32_t i, w = dx_ui_win % DX_NWIN, m = L->mode;
    int32_t rh = ((int32_t)cv_h - 36) / (int32_t)DX_ROWS, y0 = 34, y, span;
    uint16_t band = ux_mix(cv_bg, c, 22);
    char b[40];
    const char *unit;
    if (rh > 14)
        rh = 14;
    for (i = 0; i < 8u; i++) {                           /* the kit's sounds, the one edited in the track's colour */
        int32_t x = 9 + (int32_t)i * (DX_TW + 2);
        int on = i == (dx_ui_lane & 7u);
        uint16_t f = on ? c : T_RAISE;
        cv_rect(x, 4, DX_TW, 16, f);
        GFX_HOOK_ALIGN(x, 4, x + DX_TW, 20, AL_HV, "DRUM-X sound name centred in its tile");
        cv_text_in(x, 4 + CAP_IN(S, 16), DX_TW, &AF_S, DX_LANE_ABBR[i], on ? T_INK : T_MID, f);
    }
    GFX_HOOK_ALIGN(DX_AX, 22, DX_AX + DX_BW, 34, AL_HV, "DRUM-X sheet's A over its bars");
    cv_text_in(DX_AX, 22 + CAP_IN(S, 12), DX_BW, &AF_S, "A", T_MID, cv_bg);
    GFX_HOOK_ALIGN(DX_BX, 22, DX_BX + DX_BW, 34, AL_HV, "DRUM-X sheet's B over its bars");
    cv_text_in(DX_BX, 22 + CAP_IN(S, 12), DX_BW, &AF_S, "B", T_MID, cv_bg);
    for (i = 0; i < DX_NROW[w]; i++) {                   /* the knobs' rows: a band, a mark */
        y = y0 + (int32_t)(DX_ROW0[w] + i) * rh;
        cv_rect(3, y, 231, rh, band);
        cv_rect(3, y, 2, rh, c);
    }
    for (i = 0; i < DX_ROWS; i++) {
        int on = i >= DX_ROW0[w] && i < DX_ROW0[w] + DX_NROW[w];
        uint16_t bg = on ? band : cv_bg;
        y = y0 + (int32_t)i * rh;
        GFX_HOOK_ALIGN(0, y, 0, y + rh, AL_V, "DRUM-X sheet label on its row's middle");
        cv_text_on(10, y + CAP_IN(S, rh), &AF_S, i < DXP_N ? DX_VAL[i] : i == 5u ? "LEVEL" : "MODE", on ? T_TEXT : T_MID, bg);
        if (i < DXP_N) {                                 /* a value: A and B */
            fmt_int(b, L->a[i]);
            dx_row_bar(DX_AX, DX_BW, y, rh, L->a[i], c, bg, b);
            fmt_int(b, L->b[i]);
            dx_row_bar(DX_BX, DX_BW, y, rh, L->b[i], T_TEXT, bg, b);
        } else if (i == 5u) {                            /* the lane's level (as LANES shows it: 100 %), DRV */
            int16_t v = t->p[P_LN0 + (dx_ui_lane & 7u)];
            param_format(&TP[P_LN0 + (dx_ui_lane & 7u)], v, b, &unit);
            str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
            dx_row_bar(DX_AX, DX_BW - 8, y, rh, (uint32_t)clamp(v, 0, 127), T_MID, bg, b);
            GFX_HOOK_ALIGN(0, y, 0, y + rh, AL_V, "DRUM-X sheet DRV on its row's middle");
            cv_text_on(cv_text_on(DX_BX, y + CAP_IN(S, rh), &AF_S, "DRIVE", T_MID, bg) + 8, y + CAP_IN(S, rh), &AF_S,
                       m & DX_DRIVE ? "ON" : "OFF", m & DX_DRIVE ? T_TEXT : T_DIM, bg);
        } else {                                         /* the switches, in words */
            str_cpy(b, DX_WAVE[m & 3u], sizeof b);
            str_cpy(b + str_len(b), "  ", sizeof b - str_len(b));
            str_cpy(b + str_len(b), DX_FILT[((m >> 2) & 3u) % 3u], sizeof b - str_len(b));
            str_cpy(b + str_len(b), (m >> 4) & 1u ? "  SNAP ON  PMOD " : "  SNAP OFF  PMOD ", sizeof b - str_len(b));
            str_cpy(b + str_len(b), DX_PMOD[(m >> 5) & 3u], sizeof b - str_len(b));
            GFX_HOOK_ALIGN(0, y, 0, y + rh, AL_V, "DRUM-X sheet switches on their row's middle");
            cv_text_on(DX_AX, y + CAP_IN(S, rh), &AF_S, b, on ? T_TEXT : T_MID, bg);
        }
    }
    span = (int32_t)DX_ROWS * rh;                        /* where the knobs are on the sheet */
    cv_rect(236, y0, 2, span, T_RAISE);
    cv_rect(236, y0 + (int32_t)DX_ROW0[w] * rh, 2, (int32_t)DX_NROW[w] * rh, T_MID);
}
