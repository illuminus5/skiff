/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca menu (HOME held): its rows in tabs (1.0.5: DISPLAY CONTROL AUDIO SYSTEM; ALGORITHM between them, PRESETS
 * within one; no scrolling), the rows in menu_items.c (the settings, shared with the editor's MENU_DESC / MENU_SET).
 * No BACK row: HOME closes it (ui_input.c). The two-valued rows are bits of ui_prefs (MENU_FLAGS). PRESETS scrolls from
 * ABOUT through all credits (OCT- back to the tabs). ui.menu: 1 the tabs, 2 information. */
/* ------------------------------------------------------------ menu --- */
/* (the rows, their values and names, usb_serial_apply: menu_items.c) */

/* https://github.com/illuminus5/skiff, QR version 3, correction M, mask 3 (tools/gen_qr.py).
 * One row per word, leftmost module in bit 0; the constant matrix needs no encoder or RAM. */
static const uint32_t ABOUT_QR[29] = {
    0x1FD99B7Fu, 0x1056C141u, 0x175CFA5Du, 0x174A195Du, 0x17482A5Du, 0x104D4641u,
    0x1FD5557Fu, 0x0007F100u, 0x1A4778EDu, 0x11DF1002u, 0x0CA25A6Bu, 0x116CE8ABu,
    0x060A9F7Eu, 0x1C49BA8Du, 0x1C5E534Eu, 0x09FCAAB0u, 0x0B1C8B47u, 0x0E84B330u,
    0x05D19951u, 0x05C68D8Cu, 0x07F7BD7Au, 0x1F117700u, 0x0B593D7Fu, 0x0B158341u,
    0x0DF7C45Du, 0x13AFDB5Du, 0x14940F5Du, 0x0AB12C41u, 0x0878B97Fu,
};

static void draw_about_qr(int32_t x, int32_t y)
{
    uint32_t row, col;
    cv_rect(x, y, 74, 74, UI_QR_LIGHT);             /* fixed light / dark, independent of the palette */
    for (row = 0; row < 29u; row++)
        for (col = 0; col < 29u; col++)
            if ((ABOUT_QR[row] >> col) & 1u)
                cv_rect(x + 8 + (int32_t)col * 2, y + 8 + (int32_t)row * 2, 2, 2, UI_QR_DARK);
}

/* All notices are in flash. Wrap at the font's actual width, without dropping URL
 * characters; the same layout computes the scroll limit and draws clipped bands.
 * Mixed case here (long text); S throughout, the title in L. */
#define MENU_DOC_H 196                                /* the document: screen rows 24..220 */
enum { MC_TEXT, MC_TITLE, MC_URL };
typedef struct { const char *text; uint8_t kind; } menu_credit_t;
static const menu_credit_t MENU_CREDITS[] = {
    {"FELUCCA", MC_TITLE},                     /* (Skiff 1.0) the firmware Skiff is built on */
    {"Leo Kuroshita, H\xFCgelton Instruments / GPL-3.0", MC_TEXT},
    {"https://github.com/hugelton/Felucca", MC_URL},
    {"", MC_TEXT},
    {"H\xFCgelton Instruments", MC_TITLE},  /* Felucca's own: above; these are ours too */
    {"Leo Kuroshita", MC_TEXT},
    {"CrispyZebra (PHASE waveforms), the DRUM voices: GPL-3.0", MC_TEXT},
    {"Fukiai icon font: MIT", MC_TEXT},
    {"https://github.com/hugelton", MC_URL},
    {"", MC_TEXT},
    {"KLATTSCH", MC_TITLE},
    {"Tony Gies / MIT", MC_TEXT},
    {"https://github.com/tgies/klattsch", MC_URL},
    {"", MC_TEXT},
    {"DAISYSP", MC_TITLE},
    {"Electrosmith, Emilie Gillet", MC_TEXT},
    {"MIT", MC_TEXT},
    {"https://github.com/electro-smith/DaisySP", MC_URL},
    {"", MC_TEXT},
    {"DEXED + MSFA", MC_TITLE},
    {"Pascal Gauthier, Google Inc.", MC_TEXT},
    {"msfa: Apache-2.0, Dexed: GPL-3.0", MC_TEXT},
    {"https://github.com/asb2m10/dexed", MC_URL},
    {"", MC_TEXT},
    {"JIANT FM", MC_TITLE},
    {"juanjiant-bit: the DRUM-X engine / GPL-3.0", MC_TEXT},
    {"https://github.com/juanjiant-bit/JIANT", MC_URL},
    {"", MC_TEXT},
    {"RINGS", MC_TITLE},
    {"Emilie Gillet / MIT", MC_TEXT},
    {"https://github.com/pichenettes/eurorack", MC_URL},
    {"", MC_TEXT},
    {"FM1-CLK320-PROBE", MC_TITLE},              /* (Skiff) MENU > CPU CLOCK 320 MHZ (hal/fm1_clk320.h) */
    {"curist: the 320 MHz switch / MIT, after the JieLi AC79 SDK (Apache-2.0)", MC_TEXT},
    {"https://github.com/curist/fm1-clk320-probe", MC_URL},
    {"", MC_TEXT},
#if BOARD_SMK37
    {"SMK-37 PORT", MC_TITLE},                   /* (Skiff) the SMK-37 build: genkigenki's board layer (smk_input.h ..) */
    {"genkigenki (M. Geisler): the SMK-37 board layer for Felucca / GPL-3.0", MC_TEXT},
    {"https://github.com/genkigenki/Felucca", MC_URL},
    {"", MC_TEXT},
#endif
    {"BARLOW CONDENSED", MC_TITLE},              /* (Skiff U1) the face on the screen (Inter Tight: not in the build) */
    {"The Barlow Project Authors", MC_TEXT},
    {"SIL Open Font License 1.1", MC_TEXT},
    {"https://github.com/jpt/barlow", MC_URL},
    {"", MC_TEXT},
    {"VSCO-2 CE + VCSL", MC_TITLE},
    {"Versilian Studios / CC0 1.0", MC_TEXT},
    {"https://github.com/sgossner/VSCO-2-CE", MC_URL},
    {"https://github.com/sgossner/VCSL", MC_URL},
    {"", MC_TEXT},
    {"YUE2", MC_TITLE},                          /* (Skiff phase 9) the music model SLICE's BREAK and FUNK were made with */
    {"Multimodal Art Projection: SLICE's BREAK and FUNK", MC_TEXT},
    {"https://github.com/multimodal-art-projection/YuE", MC_URL},
    {"", MC_TEXT},
    {"VOICE", MC_TITLE},
    {"Dennis H. Klatt (1980)", MC_TEXT},
    {"https://doi.org/10.1121/1.383940", MC_URL},
    {"Hillenbrand, Getty, Clark and Wheeler (1995)", MC_TEXT},
    {"https://doi.org/10.1121/1.411872", MC_URL},
};

static int32_t menu_text(const char *s, int32_t y, uint16_t col, int draw)
{
    while (*s) {
        char line[64];
        uint32_t n = 0, space = 0, take, prev = 0;
        int32_t pen = 0;                                 /* 1/16 px, as text_w adds it up */
        while (s[n] && n < sizeof line - 1u) {
            uint32_t c = (uint8_t)s[n];
            line[n] = s[n];
            line[n + 1u] = 0;
            pen += (prev ? kern(&AF_S, prev, c) : 0) + AF_S.adv[glyph(&AF_S, c) >> AF_S.psh];
            prev = c;
            if ((pen + 8) >> 4 > 226)
                break;
            if (s[n] == ' ')
                space = n;
            n++;
        }
        take = s[n] && space ? space : n;
        if (!take) take = 1;                         /* progress even for an unusually wide glyph */
        line[take] = 0;
        if (draw && y + AF_S.h > 0 && y < MENU_DOC_H)
            cv_text(6, y, &AF_S, line, col);
        y += 18;
        s += take;
        while (*s == ' ') s++;
    }
    return y;
}

static int32_t menu_document(int32_t y, int draw)
{
    uint32_t i;
    if (draw) {
        cv_text(6, y + 2, &AF_L, "SKIFF", T_THEME);
        cv_text(6, y + 38, &AF_S, "Groovebox on Felucca", T_TEXT);
        cv_text(6, y + 58, &AF_M, FELUCCA_VERSION, T_THEME);
        cv_text_r(232, y + 61, &AF_S, __DATE__, T_MID, T_BG);
        {   /* (Skiff) the core clock now, from the PLL (hal/fm1_clk.h; MENU > SYSTEM > CPU CLOCK) */
            char c[16] = "CPU ?";
            if (CLK_MHZ())
                fmt_int(c + 4, (int32_t)CLK_MHZ());
            str_cpy(c + str_len(c), " MHZ", sizeof c - str_len(c));
            cv_text_r(232, y + 82, &AF_S, c, T_MID, T_BG);
        }
        cv_text(6, y + 82, &AF_S, "Skiff: illuminus5", T_TEXT);   /* (Skiff 1.0) its source (the QR), Felucca's */
        cv_text(6, y + 100, &AF_S, "Felucca (C) 2026", T_TEXT);      /* credit; GPL-3.0-only */
        cv_text(6, y + 118, &AF_S, "Leo Kuroshita, H\xFCgelton", T_TEXT);
        cv_text(6, y + 138, &AF_S, "GPL-3.0-only", T_TEXT);
        cv_text(6, y + 160, &AF_S, "github.com/illuminus5", T_MID);
        cv_text(6, y + 178, &AF_S, "/skiff", T_MID);
        draw_about_qr(158, y + 122);
    }
    y = menu_text("CREDITS", y + 216, T_THEME, draw) + 8;
    for (i = 0; i < NELEM(MENU_CREDITS); i++) {
        const menu_credit_t *c = &MENU_CREDITS[i];
        const char *text = c->text;
        /* Display host/path once; the full source URL stays in the flash table. */
        if (c->kind == MC_URL && str_len(text) >= 8u && !memcmp(text, "https://", 8))
            text += 8;
        if (!text[0]) y += 8;
        else y = menu_text(text, y, c->kind == MC_TITLE ? T_THEME : c->kind == MC_URL ? T_MID : T_TEXT, draw);
    }
    y = menu_text("LICENSES + SOURCE", y + 12, T_THEME, draw);
    y = menu_text("DRUM + own samples: GPL-3.0-only", y, T_TEXT, draw);
    y = menu_text("Fonts and icons: their licences travel with the source and the release files.", y, T_TEXT, draw);
    y = menu_text("NO WARRANTY", y, T_TEXT, draw);
    y = menu_text("Full notices in source:", y + 8, T_TEXT, draw);
    y = menu_text("github.com/illuminus5/skiff", y, T_MID, draw);
    return y + 4;
}
static int32_t menu_scroll_max(void)
{
    int32_t h = menu_document(0, 0);
    return h > MENU_DOC_H ? h - MENU_DOC_H : 0;
}

static void menu_dots(void);
/* the menu's header (0..24): its icon and title, the REC mark, the way back (the tabs: HOME CLOSE before the dots) */
static void menu_head(void)
{
    cv_begin(240, H_HEAD, T_BG);
    GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V, "header icon on its title's line");
    cv_icon_mid(8, H_HEAD / 2, 16, ui.menu == 2 ? ICON_X_INFO : ICON_X_COG, T_THEME, T_BG);   /* (as draw_head's) */
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(30, HEAD_MY, &AF_M, ui.menu == 2 ? "ABOUT / CREDITS" : "MENU", T_TEXT);
    if (ui.menu >= 2) {                               /* the way back at the right, the REC mark before it (then */
        const char *w = song.rec ? 0 : "BACK";        /* the keycap goes without its word: no room for both) */
        int32_t x = 232 - kh_w(KC_OCTDN, w);
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_key_hint(x, HALF_UP(H_HEAD - KC_H), KC_OCTDN, w, 1, T_BG);
        draw_rec_mark(x - 20, T_BG);
    } else {
        int32_t x = 232 - (12 + (int32_t)(MTAB_COUNT - 1u) * 8) - 8 - kh_w(KC_HOME, "CLOSE");   /* (menu_dots' left) */
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        cv_key_hint(x, HALF_UP(H_HEAD - KC_H), KC_HOME, "CLOSE", 1, T_BG);
        draw_rec_mark(x - 20, T_BG);
        menu_dots();
    }
    cv_blit(0, Y_HEAD);
}

/* The page (1.0.5): the tabs across the top, the rows of the one shown below, the key hints at the bottom.
 *   header   0..24  the cog and MENU, the REC mark, HOME CLOSE, the tabs as dots at the right (the one shown a THEME
 *                   pill)
 *   tabs    29..53  a 24 px cushion per tab: its icon alone (SURF, MID); the one shown wider, with its name (ACCENT,
 *                   INK; S, LARGE M), MT_GAP between any two. With ANIM ON the cushions grow and shrink into place
 *                   (mt_follow), the gaps kept
 *   rows    59..    one SURF panel (FLAT; LINE: none, a rule under the tabs), a row a label and its value, 1 px rules
 *                   between them; the selected row THEME with INK, as the browser's. A value THEME (M) at the right,
 *                   ON / OFF as a switch, COLOR its palette's five colours, CALIBRATION and ABOUT a chevron (OCT+
 *                   opens them). LARGE: 28 px rows (a tab of up to 5), the labels in M
 *   keys   224      ALGO TAB, PRESETS ROW, OCT-/+ VALUE (any of KNOB 1..4 too; CALIBRATION, ABOUT: OCT+ OPEN). HOME
 *                   CLOSE is in the header: the four would not fit in S (cv_key_row drops words, from the last)
 * Drawn in two bands (the canvas holds 124 rows) split in a gap between two rows; while the tabs slide only their
 * strip (24..58) is drawn again */
static const uint16_t MTAB_ICON[MTAB_COUNT] = {ICON_X_EYE, ICON_X_KNOB, ICON_X_SPEAKER, ICON_X_COG, ICON_MIDI};
#define MT_Y 29                                        /* the tab bar */
#define MT_H 24
#define MT_IW 28                                       /* a cushion with its icon alone (the icon centred in it; (1.3) 28,
                                                        * was 32: five tabs) */
#define MT_NX 26                                       /* the name's pen in the one shown; 10 px after it */
#define MT_X0 6
#define MT_X1 234
#define MT_GAP 5                                       /* between two tabs, always (the bar centred in MT_X0..MT_X1; (1.3) 5,
                                                        * was 8: five tabs) */
#define MT_ONE 64                                      /* mt.pos per tab */
#define MP_Y 59                                        /* the rows' panel */
#define MP_PAD 3
#define MR_X 8                                         /* a row */
#define MR_W 224
#define MR_LX 16                                       /* its label */
#define MR_VR 224                                      /* its value's right edge */
#define MK_Y 224                                       /* the key hints */
static int menu_large(void) { return (ui_prefs & PREF_LARGE) != 0u; }
static uint32_t menu_tab(void) { return MI_TAB[ui.menu_sel % MI_COUNT]; }
/* the shown tab's rows: 24 px (LARGE 28, a tab of up to 5 rows), 2 px apart (a tab of 6: 1 px, its panel then ends
 * 6 px above the key hints as the others do; the rule between two rows is that gap row). (1.3) A tab of more shows
 * MTAB_ROWS of them from mr_top, so the cursor's row is the last shown once past them (MIDI's ten) */
static uint32_t mr_n(void) { return mtab_rows(menu_tab()) < MTAB_ROWS ? mtab_rows(menu_tab()) : MTAB_ROWS; }
static uint32_t mr_top(void)
{
    uint32_t k = ui.menu_sel - mtab_first(menu_tab());
    return k < MTAB_ROWS ? 0u : k + 1u - MTAB_ROWS;
}
static int32_t mr_h(void) { return menu_large() && mr_n() <= 5u ? 28 : 24; }
static int32_t mr_gap(void) { return mr_n() <= 5u ? 2 : 1; }
static int32_t mr_y(uint32_t k) { return MP_Y + MP_PAD + (int32_t)k * (mr_h() + mr_gap()); }

/* the tab bar's place: the tab shown at pos tab * MT_ONE; with ANIM ON it follows in halving steps, a frame each
 * (a frame missed, as when the menu opens: it snaps, as ui_graph.c pr_follow) */
static struct { int16_t pos; uint32_t frame; } mt;
static void mt_follow(void)
{
    int32_t want = (int32_t)menu_tab() * MT_ONE, d = want - mt.pos;
    if (ui.frame != mt.frame + 1u || (ui_prefs & PREF_ANIM_OFF) || (d < 3 && d > -3))
        mt.pos = (int16_t)want;
    else
        mt.pos = (int16_t)(mt.pos + d / 2);
    mt.frame = ui.frame;
}
static const aafont_t *mt_font(void) { return menu_large() ? &AF_M : &AF_S; }
/* the tabs at the place mt.pos, MT_GAP apart, centred: each one's share of the shown look (0 .. MT_ONE), x, width */
static void mt_layout(int32_t *a, int32_t *x, int32_t *w)
{
    uint32_t k;
    int32_t tot = 0, cum = 0;
    for (k = 0; k < MTAB_COUNT; k++) {
        int32_t d = mt.pos - (int32_t)k * MT_ONE;
        a[k] = MT_ONE - (d < 0 ? -d : d);
        if (a[k] < 0) a[k] = 0;
        w[k] = MT_IW + (MT_NX - MT_IW + text_w(mt_font(), MTAB_NAME[k]) + 10) * a[k] / MT_ONE;
        tot += w[k];
    }
    cum = MT_X0 + HALF_UP(MT_X1 - MT_X0 - tot - (int32_t)(MTAB_COUNT - 1u) * MT_GAP);   /* (the bar centred) */
    for (k = 0; k < MTAB_COUNT; k++) {                 /* the same gap between every two, sliding too */
        x[k] = cum;
        cum += w[k] + MT_GAP;
    }
}
static void menu_tabs(void)                            /* (in screen rows: cv_oy set by the caller) */
{
    int32_t a[MTAB_COUNT], x[MTAB_COUNT], w[MTAB_COUNT];
    uint32_t k;
    mt_layout(a, x, w);
    for (k = 0; k < MTAB_COUNT; k++) {
        int on = k == (uint32_t)((mt.pos + MT_ONE / 2) / MT_ONE);   /* (half way, the shown look moves over) */
        uint16_t bg = on ? T_ACCENT : T_SURF;
        if (on && ux.neon) {                           /* ((Skiff N3) NEON: lit pink into violet) */
            uint16_t st[2] = {T_THEME, NE_VIOLET};
            ne_hgrad(x[k], MT_Y, w[k], MT_H, st, 2);
        } else if (ux.quark) {                         /* ((Skiff Q2) QUARK: lit cyan; the others in corners) */
            cv_rect(x[k], MT_Y, w[k], MT_H, on ? T_THEME : T_BG);
            if (!on)
                qk_corners(x[k], MT_Y, w[k], MT_H, T_DIM, 3);
        } else
        cv_rrect(x[k], MT_Y, w[k], MT_H, ux.crypt ? 0 : -8, bg, T_BG);   /* ((Skiff) CRYPT: square) */
        GFX_HOOK_ALIGN(x[k], MT_Y, x[k] + MT_IW, MT_Y + MT_H, AL_HV, "menu tab icon centred in its cushion");
        if (ux.quark)
            bg = on ? T_THEME : T_BG;
        cv_icon_in(x[k], MT_Y, MT_IW, MT_H, 16, MTAB_ICON[k], on ? T_INK : T_MID, bg);
        if (a[k] == MT_ONE) {                          /* (settled: its name) */
            GFX_HOOK_ALIGN(0, MT_Y, 0, MT_Y + MT_H, AL_V, "menu tab name centred up/down");
            cv_text_on(x[k] + MT_NX, MT_Y + (menu_large() ? CAP_IN(M, MT_H) : CAP_IN(S, MT_H)), mt_font(), MTAB_NAME[k],
                       T_INK, bg);
        }
    }
}
/* the header's tab dots at the right, the shown tab's a THEME pill */
static void menu_dots(void)
{
    uint32_t k, t = menu_tab();
    int32_t x = 232 - (12 + (int32_t)(MTAB_COUNT - 1u) * 8);
    for (k = 0; k < MTAB_COUNT; k++) {
        cv_rrect(x, H_HEAD / 2 - 2, k == t ? 12 : 4, 4, -2, k == t ? T_THEME : T_DIM, T_BG);
        x += (k == t ? 12 : 4) + 4;
    }
}

/* the tab's row k (menu_items.c row `row`) */
static void menu_row(uint32_t k, uint32_t row)
{
    int32_t y = mr_y(k), h = mr_h(), large = menu_large(), x0;
    int32_t ny = y + (large ? CAP_IN(M, h) : CAP_IN(S, h)), vy = y + CAP_IN(M, h);
    int sel = row == ui.menu_sel;
    uint16_t bg = sel ? (ux.quark ? ux_mix(T_BG, T_THEME, 18) : T_THEME) : T_SURF;   /* ((Skiff Q2) QUARK: the row
                                                       * a cyan tint in corners, its value gold) */
    uint16_t fg = sel && !ux.quark ? T_INK : T_TEXT, val = sel ? (ux.quark ? T_ACCENT : T_INK) : ux.neon ? T_ACCENT : T_THEME;
    uint16_t sk = sel && !ux.quark ? T_INK : T_MID;    /* (the chevron, a unit) */
    int lead = ux.crypt || ux.neon ? !sel : ux.quark;  /* (the dotted leaders: QUARK's every row) */
    const char *v = row < MI_VALUES ? menu_vname(row, menu_get(row)) : "";   /* ((1.3) LEARN CLEAR: its count) */
    int32_t nx, ly = y + h / 2 + 3;                    /* (Skiff) CRYPT: the leader's row, by the capitals' foot */
    if (sel && ux.neon) {                              /* (Skiff N3) NEON: a sunset bar */
        uint16_t st[3] = {T_THEME, NE_VIOLET, ux_mix(NE_VIOLET, T_ACCENT, 50)};
        ne_hgrad(MR_X, y, MR_W, h, st, 3);
    } else if (sel && ux.quark) {
        cv_rect(MR_X, y, MR_W, h, bg);
        qk_corners(MR_X, y, MR_W, h, T_THEME, 5);
    } else if (sel)
        cv_rrect(MR_X, y, MR_W, h, ux.crypt ? 0 : 6, T_THEME, T_SURF);   /* ((Skiff) no rules between the rows, as the mockup) */
    if (ux.neon) {                                     /* (Skiff N3) NEON: an arcade's high score table: the rank, slanted,
                                                        * in the tracks' colours, before the name */
        static const char RANK[6][4] = {"1ST", "2ND", "3RD", "4TH", "5TH", "6TH"};
        const aafont_t *f = large ? &AF_M : &AF_S;
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row name centred up/down");
        cv_fx_on(ny, f, 0);
        cv_text_on(MR_LX, ny, f, RANK[k % 6u], sel ? T_INK : T_TRK(k), bg);
        cv_fx_off();
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row name centred up/down");
        nx = cv_text_on(MR_LX + text_w(f, "6TH") + cv_fx_w(f) + 6, ny, f, MI_NAME[row], fg, bg) + 4;
    } else {
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row name centred up/down");
    nx = cv_text_on(MR_LX, ny, large ? &AF_M : &AF_S, MI_NAME[row], fg, bg) + 4;
    }
    if (menu_act(row)) {                               /* CALIBRATION, ABOUT: OCT+ opens them ((1.3) MIDI LEARN, LEARN
                                                        * CLEAR: does them, its value before the chevron) */
        x0 = MR_VR - 16;
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row chevron centred up/down");
        cv_icon_in(MR_VR - 12, y, 0, h, 12, ICON_X_RIGHT, sk, bg);
        if (*v) {
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row value centred up/down");
            x0 = cv_text_r(x0 - 4, vy, &AF_M, v, str_eq(v, "NONE") && !sel ? T_DIM : val, bg) - 4;
        }
        if (lead)
            cr_leader(nx, x0, ly);
        return;
    }
    if (row == MI_HOLD) {                              /* "0.4" and its unit */
        char b[8] = "0.4";
        b[2] = (char)('0' + HOLD_MS[settings_hold % 4u] / 100u);
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row value centred up/down");
        GFX_HOOK_ALIGN(0, 0, 0, vy + AF_M.asc, AL_B, "menu row unit on its value's baseline");
        x0 = cv_text_r(cv_text_r(MR_VR, vy + AF_M.asc - AF_S.asc, &AF_S, "s", sk, bg) - 3, vy, &AF_M, b, val, bg);
        if (lead)
            cr_leader(nx, x0 - 4, ly);
        return;
    }
    GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "menu row value centred up/down");
    if (row != MI_COLOR) {
        uint16_t vc = !sel && str_eq(v, "OFF") ? T_DIM : val;
        if (ux.quark) {                                /* ((Skiff Q2) QUARK: a readout in brackets) */
            x0 = cv_text_r(MR_VR, vy, &AF_M, "]", vc, bg);
            x0 = cv_text_r(x0, vy, &AF_M, v, vc, bg);
            x0 = cv_text_r(x0, vy, &AF_M, "[", vc, bg);
        } else
            x0 = cv_text_r(MR_VR, vy, &AF_M, v, vc, bg);   /* ((Skiff) ON / OFF as words: OFF dim) */
        if (lead)
            cr_leader(nx, x0 - 4, ly);
        return;
    }
    {   /* COLOR: the palette's name and its five colours (BG SURF TEXT THEME ACCENT), live */
        uint32_t c;
        const uint16_t *tok = &T_BG;
        int32_t sy = y + HALF_UP(h - 12);
        x0 = MR_VR - 74;
        if (lead)
            cr_leader(nx, cv_text_r(x0 - 6, vy, &AF_M, v, val, bg) - 4, ly);
        else
            cv_text_r(x0 - 6, vy, &AF_M, v, val, bg);
        for (c = 0; c < 5u; c++) {
            cv_rrect(x0 + (int32_t)c * 15, sy, 12, 12, -3, T_RAISE, bg);       /* a rim for the dark ones */
            cv_rrect(x0 + 1 + (int32_t)c * 15, sy + 1, 10, 10, -2, tok[c], T_RAISE);
        }
    }
}
/* the rows' two bands meet at `split`: in the gap above a row (or below the last), each band at most 124 rows */
static uint32_t menu_split(uint32_t n)
{
    int32_t s = mr_y(n) - 1;
    uint32_t k = n;
    while (s > H_HEAD + 124 && k > 1u)
        s = mr_y(--k) - 1;
    return (uint32_t)(s < 240 - 124 ? 240 - 124 : s);
}
static void draw_menu(void)
{
    static const khint_t KEYS[2][3] = {{{KC_ALGO, "TAB"}, {KC_PRESETS, "ROW"}, {KC_OCTPM, "VALUE"}},
                                       {{KC_ALGO, "TAB"}, {KC_PRESETS, "ROW"}, {KC_OCTUP, "OPEN"}}};
    uint32_t pass, k, first, n, split, sig;
    int32_t pos0 = mt.pos;
    if (ui.menu == 1)
        mt_follow();
    sig = ui.menu * 7u + ui.menu_sel * 131u + ui_prefs * 4099u + song.rec * 65537u + song.sel * 13u +
          ui_rec_prefs * 92821u +
          (ui.menu == 2 ? ui.menu_scroll * 48611u : 0u) +
          /* (1.2.1) what the rows show besides their values: the load SYSTEM > CPU shows, so a change of it redraws (the
           * rows stayed as drawn until the cursor moved); ((1.3) LEARN's count and state; CPU's STOP / RESTART) */
          song.cpu_q8 * 100u / 256u * 12289u + ml_count() * 2741u + ui.ml.on * 3463u +
          (uint32_t)transport_busy() * 40503u;
    for (k = 0; k < MI_VALUES; k++)                     /* ((Skiff, after 1.3.1) every row's value, wherever it lives, so a
                                                         * step redraws it: the list of them by hand this replaces had
                                                         * missed STEP PREVIEW and SCREEN OFF) */
        sig = sig * 31u + menu_get(k);
    if (!ui.force && sig == ui.menu_sig) {
        if (ui.menu == 1 && mt.pos != pos0) {          /* only the tabs slid: their strip */
            cv_begin(240, MP_Y - H_HEAD, T_BG);
            cv_oy = -H_HEAD;
            menu_tabs();
            cv_oy = 0;
            cv_blit(0, H_HEAD);
        }
        return;
    }
    ui.menu_sig = sig;
    menu_head();
    if (ui.menu >= 2) {
        int32_t scroll = ui.menu_scroll, max = menu_scroll_max();
        int32_t thumb = MENU_DOC_H * MENU_DOC_H / (max + MENU_DOC_H);
        if (thumb < 12) thumb = 12;
        for (pass = 0; pass < 2u; pass++) {
            cv_begin(240, pass ? MENU_DOC_H - 124u : 124u, T_BG);
            cv_oy = pass ? -124 : 0;
            cv_scroll = 1;
            menu_document(-scroll, 1);
            cv_scroll = 0;
            cv_rrect(235, 2, 3, MENU_DOC_H - 4, 1, T_LINE, T_BG);           /* the scroll bar */
            cv_rrect(235, 2 + (max ? scroll * (MENU_DOC_H - 4 - thumb) / max : 0), 3, thumb, 1, T_MID, T_LINE);
            cv_oy = 0;
            cv_blit(0, H_HEAD + pass * 124u);
        }
        cv_begin(240, 240 - (H_HEAD + MENU_DOC_H), T_BG);
        cv_key_hint(8, 4, KC_PRESETS, "SCROLL", 1, T_BG);
        cv_blit(0, H_HEAD + MENU_DOC_H);
        return;
    }
    first = mtab_first(menu_tab()) + mr_top();
    n = mr_n();
    split = menu_split(n);
    for (pass = 0; pass < 2u; pass++) {
        int32_t top_y = pass ? (int32_t)split : H_HEAD;
        cv_begin(240, pass ? 240u - split : split - H_HEAD, T_BG);
        cv_oy = -top_y;                               /* drawn in screen rows */
        if (!pass)
            menu_tabs();
        if (ux.crypt) {                               /* (Skiff) CRYPT: the rows' panel stone, a double frame */
            cr_stone(4, MP_Y, 232, mr_y(n) - mr_gap() + MP_PAD - MP_Y, T_SURF);
            cr_frame2(4, MP_Y, 232, mr_y(n) - mr_gap() + MP_PAD - MP_Y, T_LINE);
        } else
            cv_rrect(4, MP_Y, 232, mr_y(n) - mr_gap() + MP_PAD - MP_Y, 8, T_SURF, T_BG);   /* the rows' panel */
        for (k = 0; k < n; k++)
            if (mr_y(k) + mr_h() > top_y && mr_y(k) - 1 < top_y + (int32_t)cv_h)
                menu_row(k, first + k);
        if (mtab_rows(menu_tab()) > n) {              /* ((1.3) more rows than shown: where they are, at the right) */
            int32_t y0 = mr_y(0), hh = mr_y(n) - mr_gap() - y0, all = (int32_t)mtab_rows(menu_tab());
            cv_rect(233, y0, 2, hh, T_LINE);
            cv_rect(233, y0 + hh * (int32_t)mr_top() / all, 2, hh * (int32_t)n / all, T_MID);
        }
        if (pass) {                                   /* ((1.3) MIDI LEARN: START / STOP, LEARN CLEAR: CLEAR) */
            khint_t kh[3];
            memcpy(kh, KEYS[menu_act(ui.menu_sel)], sizeof kh);
            if (ui.menu_sel == MI_LEARN || ui.menu_sel == MI_LCLEAR)
                kh[2].act = ui.menu_sel == MI_LCLEAR ? "CLEAR" : ui.ml.on ? "STOP" : "START";
            cv_key_row(8, 232, MK_Y, kh, 3, 7u, T_BG);
        }
        cv_oy = 0;
        cv_blit(0, (uint32_t)top_y);
    }
}
static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    settings_save();                                   /* palette / panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    if (ui.home)                                       /* ((Skiff U3, N3) back where it was opened: was HOME always) */
        go_home();
    else
        page_entered();
}

/* menu: ALGORITHM steps between the tabs, PRESETS between the rows of one (both wrap; a tab comes back at the row
 * last picked in it), any of KNOB 1..4 or OCT+ / OCT- change a value (menu_items.c menu_step), OCT+ opens CALIBRATION /
 * ABOUT (OCT- nothing there); OCT- in ABOUT goes back to the tabs. HOME closes it, from ABOUT too (ui_input.c) */
static void menu_move(int32_t s)                       /* PRESETS: the next / previous row of the tab */
{
    uint32_t t = menu_tab(), f = mtab_first(t), n = mtab_rows(t), k = (ui.menu_sel - f + (s > 0 ? 1u : n - 1u)) % n;
    ui.menu_sel = (uint8_t)(f + k);
    ui.menu_row[t] = (uint8_t)k;
}
static void menu_tab_go(int32_t s)                     /* ALGORITHM: the next / previous tab, at its last row */
{
    uint32_t t = menu_tab(), k;
    ui.menu_row[t] = (uint8_t)(ui.menu_sel - mtab_first(t));
    t = (t + (s > 0 ? 1u : MTAB_COUNT - 1u)) % MTAB_COUNT;
    k = ui.menu_row[t] < mtab_rows(t) ? ui.menu_row[t] : 0u;
    ui.menu_sel = (uint8_t)(mtab_first(t) + k);
}
static void menu_input(uint32_t oct)                  /* oct: ui_input.c oct_taps */
{
    int32_t s;
    uint32_t k, up = (oct >> 1) & 1u, dn = oct & 1u;
    if (ui.menu >= 2) {                                /* ABOUT: PRESETS scrolls (bounded), OCT- back to the tabs */
        if (dn)
            ui.menu = 1, ui.force = 1;
        else if ((s = panel_enc(EN_PRESET)) != 0)
            ui.menu_scroll = (uint16_t)clamp((int32_t)ui.menu_scroll + clamp(s, -128, 128) * 18, 0, menu_scroll_max());
        enc_drop();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0)
        menu_move(s);
    if ((s = panel_enc(EN_ALGO)) != 0)
        menu_tab_go(s);
    for (s = 0, k = 0; k < 4u; k++)                    /* KNOB 1..4, any of them */
        s += panel_enc(EN_K1 + k);
    if (!menu_act(ui.menu_sel)) {
        /* a step (OCT+ the next value, OCT- the previous); COLOR previews the palette, STYLE is drawn so from the next
         * frame (FX LATCH: the latched effects are off while the menu is up) */
        s += (int32_t)up - (int32_t)dn;
        if (s)
            menu_put(ui.menu_sel, menu_step(ui.menu_sel, s));
    } else if (up) {
        if (ui.menu_sel == MI_PANEL) {
            panel_setup();
            scrn.idle = fm1_ms;                         /* (SCREEN OFF: its own loop had the input) */
            ui.force = 1;
        } else if (ui.menu_sel == MI_RESET) {          /* ((Skiff U4, A3) its dialog, on the page under the MENU) */
            menu_close();
            confirm_open(CF_RESET_SET, 0);
        } else if (ui.menu_sel == MI_LEARN) {          /* ((1.3) MIDI LEARN on, on the page under the MENU; on: done) */
            menu_close();
            if (ui.ml.on) {
                ml_off();
                ui_message("LEARN DONE");
            } else
                ml_start();
        } else if (ui.menu_sel == MI_LCLEAR) {         /* ((1.3) every learned CC) */
            ui_message(ml_clear_all() ? "LEARN CLEARED" : "NOTHING LEARNED");
        } else {                                       /* MI_ABOUT */
            ui.menu = 2;
            ui.menu_scroll = 0;
            ui.force = 1;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}
