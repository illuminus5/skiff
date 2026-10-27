/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff: STYLE BLOCKS' header, knob card and dialog (DESIGN.md, direction B). Included by ui_draw.c; draw_head,
 * draw_column and draw_confirm draw through them. */

/* the header: the transport left, the state right. Left: the play state (stop, play, the song's disc; counting in, play
 * in the accent) or, a track armed, the REC mark in its place (filled: the selected track; outlined: another); the
 * selected track's bar.beat (16 of its steps a bar, 4 a beat, as its step pages count them; stopped, 1.1 in MID: PLAY
 * starts there); the BPM (M, the accent while it is being changed) and "BPM" (S) on its baseline. Right: a message or
 * a layer's label from HB_MSG_X or past the BPM (the octave and the battery give way); else the octave or the song
 * row (head_group) and the battery. No metronome, no BPM LOCK mark (a turn says BPM LOCKED), no rolling digits. The selected track is
 * the footer's (its cushion before the engine). Redrawn when a beat moves while playing. */
#define HB_POS_X 22                                   /* bar.beat's pen */
#define HB_BPM_X 52                                   /* the BPM's pen */
#define HB_MSG_X 106                                  /* a message from here: 130 px to the edge */
/* (Skiff phase 5) before the title, in the selected track's colour: on SONG, playing with SONG on, "SONG ON"; on a SEQ
 * page that edits the pattern, which one: the entry the track is on, the pattern's number, how many entries name it
 * when more than one ("C \xB7 06 \xB7 2x", the mockup's; b: 16 bytes); ((2.0, #44) on a chain: its entries, the
 * part's number: "A-B \xB7 02", the join sheet's). 0: nothing to say */
static int head_mark(char *b)
{
    const page_t *pg = cur_page();
    uint32_t k = song.sel % NTRK, id = trk_pat(k), e = bank_cur(k), n = 0, t, j, b0, b1;
    b[0] = 0;
    if (ui.home || pg->fam != FAM_SEQ)
        return 0;
    if (pg->graph == GR_SONG) {
        if (song.playing && lists.song)
            str_cpy(b, lists.song == SONG_ONCE ? "SONG ONCE" : "SONG ON", 16);
        return b[0] != 0;
    }
    if (id == PAT_NONE)
        return 0;
    if (e < NENT && ent_block(k, e, &b0, &b1)) {
        str_cpy(b, "A-B \xB7 ", 16);
        b[0] = (char)('A' + b0);
        b[2] = (char)('A' + b1);
        pat_num(b + str_len(b), id);
        return 1;
    }
    if (e < NENT) {
        b[0] = (char)('A' + e);
        str_cpy(b + 1, " \xB7 ", 15);
    }
    pat_num(b + str_len(b), id);
    for (t = 0; t < NTRK; t++)
        for (j = 0; j < NENT; j++)
            n += lists.ent[t][j][0] == id + 1u;
    if (n > 1u) {
        str_cpy(b + str_len(b), " \xB7 ", 16 - str_len(b));
        fmt_int(b + str_len(b), (int32_t)n);
        str_cpy(b + str_len(b), "x", 16 - str_len(b));
    }
    return 1;
}
static void draw_head_blocks(void)
{
    const track_t *t = TSEL;
    uint32_t rec = (song.rec >> song.sel) & 1u ? 2u : song.rec != 0u, id, sig;
    int msg = ui.msg_t || ui.layer;
    char pos[12] = "1.1", b[8], pn[16], mk[16];      /* (stopped: PLAY starts from the top) */
    int32_t x, lx;
    uint16_t ic;
    int mark = head_mark(mk);                        /* ((Skiff phase 5) SONG ON, the pattern edited) */
    project_cur_name(pn);                            /* (HOME's title) */
    if (song.playing) {                              /* (in time: REV, PING, RAND count up too) */
        fmt_int(pos, (int32_t)(seq_at(t) / 16u) + 1);
        str_cpy(pos + str_len(pos), ".", 2);
        fmt_int(pos + str_len(pos), (int32_t)(seq_at(t) % 16u / 4u) + 1);
    }
    fmt_int(b, song.g[G_BPM]);
    if (seq_counting())
        id = ICON_X_PLAY, ic = T_ACCENT;
    else if (rec)
        id = rec == 2u ? ICON_X_REC : ICON_X_REC_O, ic = T_REC;
    else
        id = song.playing ? ICON_X_PLAY : ICON_X_STOP, ic = song.playing ? T_THEME : T_MID;
    sig = str_hash(str_hash(id * 31u + ic, pos), b) + (ui.msg_t ? str_hash(7u, ui.msg) : ui.layer * 7919u +
          (uint32_t)layer_locked() * 3u + (ui.layer == LAYER_PERF ? launch_mode() * 131u : 0u)) + (ui.bpm_t != 0) * 31u +
          (uint32_t)(song.octave + 8) * 11u +
          (uint32_t)batt_shown() * 7777u + 0xB10C5u +
          str_hash(9u + (uint32_t)ui.home, ui.shift ? "SHIFT" : ui.home ? pn : page_head()) +   /* (the title) */
          (mark ? str_hash(3u + song.sel, mk) : 0u) + project_mark() * 104729u;
    ui.roll_bpm = song.g[G_BPM];                      /* (no rolling digits: none left to finish in another style) */
    ui.roll[ROLL_BPM].from[0] = 0;
    if (!ui.force && sig == ui.head_sig)
        return;
    ui.head_sig = sig;
    cv_begin(240, H_HEAD, T_BG);
    if (ux.crypt && rec && !seq_counting())           /* (Skiff C3) CRYPT: REC a blood drop */
        cr_drop(8, (H_HEAD - 9) / 2, rec == 2u);
    else if (ux.neon && rec && !seq_counting())       /* (Skiff N3) NEON: a red light */
        ne_rec(11, H_HEAD / 2 - 1, rec == 2u);
    else if (ux.quark && rec && !seq_counting())      /* (Skiff Q2) QUARK: a red nucleus */
        qk_rec(11, H_HEAD / 2 - 1, rec == 2u);
    else
        cv_icon_mid(4, H_HEAD / 2, 16, id, ic, T_BG);
    if (ux.crypt)                                     /* .. and the castle wall's crenels along the header's foot */
        for (x = 0; x < 240; x += 4)
            cv_rect(x, H_HEAD - 1, 2, 1, T_LINE);
    if (ux.neon)                                      /* (Skiff N3) NEON: a neon rule along it */
        ne_rule(H_HEAD - 2, 0);
    if (ux.quark)                                     /* (Skiff Q2) QUARK: a dotted rule */
        qk_rule(H_HEAD - 2);
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    cv_text(HB_POS_X, HEAD_MY, &AF_M, pos, song.playing ? T_TEXT : T_MID);
    GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
    if (ux.neon)                                      /* ((Skiff N2) NEON: slanted) */
        cv_fx_on(HEAD_MY, &AF_M, 0);
    x = cv_text(HB_BPM_X, HEAD_MY, &AF_M, b, ui.bpm_t ? T_ACCENT : T_THEME) + (ux.neon ? cv_fx_w(&AF_M) : 0);
    cv_fx_off();
    GFX_HOOK_ALIGN(0, 0, 0, HEAD_MY + AF_M.asc, AL_B, "BPM on its value's baseline");
    x = cv_text(x + 3, HEAD_MY + AF_M.asc - AF_S.asc, &AF_S, "BPM", T_MID);
    lx = x;                                            /* (where the transport's half ends) */
    if (msg && !ui.msg_t) {                           /* a layer: its title at the right, as the mockup's PERFORM BAR, */
        char h[24];                                    /* in words ([GLO] SET: GLO SET); PERFORM: when a switch comes */
        if (ui.layer == LAYER_PERF) {                  /* in (LAUNCH); locked: the lock before it */
            str_cpy(h, "PERFORM ", sizeof h);
            str_cpy(h + 8, LAUNCH_NAME[launch_mode()], sizeof h - 8u);
        } else {
            kc_words(h, sizeof h, layer_head(), 0);
            if (layer_locked() && ui.layer != LAYER_FX) {   /* ((Skiff U2) locked: says so, as FX LATCH does; its
                                                             * button closes it, the footer says DONE) */
                char *sp = h;
                while (*sp && *sp != ' ')
                    sp++;
                str_cpy(sp, " LOCKED", sizeof h - (uint32_t)(sp - h));
            }
        }
        GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
        x = cv_text_r(234, HEAD_SY, &AF_S, h, layer_locked() ? T_THEME : T_MID, T_BG);
        if (layer_locked())
            cv_icon_mid(x - 18, H_HEAD / 2, 16, ICON_X_LOCK, T_THEME, T_BG);
    } else if (msg) {
        x = x + 9 > HB_MSG_X ? x + 9 : HB_MSG_X;
        head_msg(x, 236 - x);
    } else {                                           /* the page's title at the right, as the mockup's (HOME: the */
        int32_t r = HEAD_BAT_X - 4;                     /* project's name); the battery after it, at every level (#5), */
        char h[16];                                     /* the octave or the song row before it while they matter */
        draw_battery(HEAD_BAT_X);
        str_cpy(h, ui.shift ? "SHIFT" : ui.home ? pn : page_head(), sizeof h);   /* ((U5) SHIFT's map) */
        x = r;
        if (h[0]) {
            GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
            x = cv_text_r(r, HEAD_SY, &AF_S, h, T_MID, T_BG) - 8;
        }
        if ((id = project_mark()) != 0u) {            /* ((Skiff U3, A2) the project: its letter in a box before the
                                                        * title, a dot on its corner while it changed since saved or
                                                        * loaded) */
            char pl[2] = {(char)(id & 0xFFu), 0};
            x -= 14;
            cv_frame(x, (H_HEAD - 15) / 2, 14, 15, T_MID);
            GFX_HOOK_ALIGN(x, 0, x + 14, H_HEAD, AL_HV, "header project letter in its box");
            cv_text_in(x, HEAD_SY, 14, &AF_S, pl, T_TEXT, T_BG);
            if (id >> 8)
                cv_rect(x + 11, (H_HEAD - 15) / 2 - 2, 5, 5, T_THEME);
            x -= 8;
        }
        if (mark && x - text_w(&AF_S, mk) - (song.octave ? text_w(&AF_S, "OCT") + 4 + text_w(&AF_M, "+3") + 8 : 0) >=
            lx + 8) {                                   /* ((Skiff phase 5) where it fits) */
            GFX_HOOK_ALIGN(0, 0, 0, H_HEAD, AL_V, "header text on its middle");
            x = cv_text_r(x, HEAD_SY, &AF_S, mk, T_TRK(song.sel), T_BG) - 8;
        }
        if (song.octave)
            head_group(x);
    }
    cv_blit(0, Y_HEAD);
}

/* the knob card: a tile per knob: the label (S; M on MENU > LARGE's list pages where it fits) top left; the value big in V (M, then S,
 * where V lacks a glyph or the value is too wide) and its unit (S) set to the tile's right edge, both on one baseline;
 * the gauge a 3 px bar of the track's colour along the bottom edge, nothing under it. The knob just turned: the whole
 * tile the accent, all on it INK. A value MOTION drives: the motion icon right on the label's row (a label that leaves
 * no room for it: the label in the theme colour); the parameters' own icons are left out, the value says it, but a
 * track's cushion stays (GLO's LEVEL cards: the card is that track's, its gauge that track's colour). Values snap
 * (draw_column): no rolling digits, as on MENU > LARGE's tall cards. */
#define BK_L 4                                        /* the ink's margins in the tile: left, right */
#define BK_R 3
#define BK_LY 1                                       /* the label's line: S capitals rows 4..13 */
#define BK_BASE 37                                    /* the value's and the unit's baseline: their ink ends on row 36 */
#define BK_GAP 3                                      /* the value's ink to the unit's */
#define BK_GH 3                                       /* the gauge: the tile's last 3 rows */

static int face_has(const aafont_t *f, const char *s)   /* every glyph of s in a sparse face (V, L) */
{
    for (; *s; s++)
        if (glyph_at(f, fold(f, (uint8_t)*s)) < 0)
            return 0;
    return 1;
}

static void draw_column_blocks(uint32_t c, const char *label, const char *val, const char *unit, uint16_t vc,
                               int32_t ratio, uint32_t icon, int hot, int mot, int32_t kid, uint32_t kind)
{
    uint16_t fill = hot ? (ux.quark ? T_THEME : T_ACCENT) : T_SURF;   /* ((Skiff Q2) QUARK: the hot cell cyan) */
    uint32_t tk = icon - ICON_TRK < NTRK ? icon - ICON_TRK : song.sel;   /* whose card: a cushion's track, else the selected */
    int32_t room = COL_W - BK_L - BK_R;
    cv_begin(COL_W, COL_H, T_BG);
    if (ux.neon && hot) {                             /* (Skiff N3) NEON: the hot card a dusk, framed; the others */
        ne_vgrad(0, 0, COL_W, COL_H, NE_HOT, 3);      /* glass on a dark rule */
        cv_frame(0, 0, COL_W, COL_H, T_TEXT);
    } else if (ux.neon) {
        ne_glass(0, 0, COL_W, COL_H, fill);
        cv_rect(0, COL_H - 1, COL_W, 1, T_LINE);
    } else if (ux.quark) {                            /* (Skiff Q2) QUARK: the periodic table's cell, its corners */
        cv_rect(0, 0, COL_W, COL_H, fill);
        if (!hot)
            qk_corners(0, 0, COL_W, COL_H, T_DIM, 5);
    } else if (!ux.crypt)
        cv_rect(0, 0, COL_W, COL_H, fill);
    else if (hot) {                                   /* (Skiff C3) CRYPT: the hot card lit, a frame inside it; */
        cv_rect(0, 0, COL_W, COL_H, fill);
        cv_frame(1, 1, COL_W - 2, COL_H - 2, T_THEME);
    } else {                                          /* the others stone slabs in a frame */
        cr_stone(0, 0, COL_W, COL_H, fill);
        cv_frame(0, 0, COL_W, COL_H, T_LINE);
    }
    if (label[0]) {
        const aafont_t *lf = kind == LK_LABEL && text_w(&AF_M, label) <= room ? &AF_M : &AF_S;
        int32_t ly = lf == &AF_M ? 0 : BK_LY, cy = ly + (lf == &AF_M ? AF_M_CAP_Y : AF_S_CAP_Y);
        int32_t ch = lf == &AF_M ? AF_M_CAP_H : AF_S_CAP_H, lx = BK_L, lw = room;
        uint16_t lc = hot ? T_INK : T_MID;
        if (icon - ICON_TRK < NTRK) {                 /* the track's cushion before the label (DIM: the track silent) */
            GFX_HOOK_ALIGN(0, cy, 0, cy + ch, AL_V, "card icon on its label's line");
            lx += cv_icon_in(BK_L, cy, 0, ch, ICON_CELL, icon, vc == T_DIM ? T_DIM : T_ACCENT, fill) + ICON_GAP;
            lw -= lx - BK_L;
        }
        if (mot && FELUCCA_ICONS && text_w(lf, label) + ICON_GAP + ICON_CELL <= lw) {
            GFX_HOOK_ALIGN(0, cy, 0, cy + ch, AL_V, "card icon on its label's line");
            cv_icon_in(COL_W - BK_R - ICON_CELL, cy, 0, ch, ICON_CELL, ICON_X_MOTION, hot ? T_INK : T_THEME, fill);
            lw -= ICON_GAP + ICON_CELL;
        } else if (mot && !hot) {
            lc = T_THEME;
        }
        cv_text_fit(lx, ly, lf, label, lc, fill, lw);
        if (ux.quark && !mot && lx == BK_L) {        /* ((Skiff Q2) QUARK: the cell's number, the knob's, where the
                                                       * label leaves room) */
            char ix[2] = {(char)('1' + c % 4u), 0};
            if (text_w(lf, label) + text_w(&AF_S, ix) + 6 <= lw)
                cv_text_r(COL_W - BK_R, ly + (lf == &AF_M ? AF_M.asc - AF_S.asc : 0), &AF_S, ix, hot ? T_INK : T_DIM, fill);
        }
    }
    if (kid >= 0) {                                   /* "[OCT+]": the keycap on the value's band (on the hot tile
                                                       * inverted: INK, its word the tile's) */
        int32_t kb[4];                                /* ((Skiff) a word, as the mockup writes keys: the value's, in M */
        const char *kl = KC[(uint32_t)kid < KC_COUNT ? (uint32_t)kid : 0u].label;   /* on its baseline) */
        text_ink(&AF_M, kl, kb);
        cv_text_on(BK_L - kb[0], BK_BASE - AF_M.asc, &AF_M, kl, hot ? T_INK : vc == T_ACCENT || vc == T_DIM ? vc : T_THEME,
                   fill);
    } else if (val[0]) {
        const aafont_t *vf = &AF_V;
        int32_t ub[4] = {0, 0, 0, 0}, vb[4], vr = room;
        if (unit[0]) {
            text_ink(&AF_S, unit, ub);
            vr -= ub[2] - ub[0] + BK_GAP - (ux.neon ? cv_fx_w(&AF_S) : 0);   /* ((Skiff N2) NEON: the slanted value's top
                                                                             * passes over the unit's: its gap at the unit's
                                                                             * height, the foot's wider) */
        }
        if (!face_has(vf, val) || ink_w(vf, val) + (ux.neon ? cv_fx_w(vf) : 0) > vr)   /* ((Skiff N2) NEON: */
            vf = &AF_M;                                                                 /* slanted, the wider) */
        if (vf == &AF_M && ink_w(vf, val) + (ux.neon ? cv_fx_w(vf) : 0) > vr)
            vf = &AF_S;
        text_ink(vf, val, vb);
        if (ux.neon)                                  /* (Skiff N2) NEON: the value in chrome, slanted (hot: INK) */
            cv_fx_on(BK_BASE - vf->asc, vf, !hot);
        if (vf == &AF_S && vb[2] - vb[0] > vr)        /* (a value always fits: the lint reports one cut) */
            cv_text_fit(BK_L, BK_BASE - vf->asc, vf, val, hot ? T_INK : vc, fill, vr);
        else
            cv_text_on(BK_L - vb[0], BK_BASE - vf->asc, vf, val, hot ? T_INK : vc, fill);
        cv_fx_off();
        if (unit[0]) {
            GFX_HOOK_ALIGN(0, 0, 0, BK_BASE, AL_B, "card unit on its value's baseline");
            cv_text_on(COL_W - BK_R - ub[2], BK_BASE - AF_S.asc, &AF_S, unit, hot ? T_INK : T_MID, fill);
        }
    }
    if (ratio >= 0 && ux.quark)                       /* (Skiff Q2) QUARK: energy levels */
        qk_dots(BK_L, COL_H - 5, COL_W - BK_L - BK_R, ratio, hot);
    else if (ratio >= 0 && ux.neon)                   /* (Skiff N3) NEON: a dash bargraph */
        ne_dash(BK_L, COL_H - 6, COL_W - BK_L - BK_R, ratio, hot);
    else if (ratio >= 0 && ux.crypt)                  /* (Skiff C3) CRYPT: a sword, the blade as long as the value */
        cr_sword(3, COL_H - 6, COL_W - 6, ratio, hot ? T_INK : vc == T_DIM ? T_DIM : T_TRK(tk), hot ? T_INK : CR_GOLD,
                 hot ? T_INK : T_MID, hot ? T_THEME : T_LINE);
    else if (ratio >= 0) {
        int32_t fx = ratio * COL_W / 1000;
        cv_rect(0, COL_H - BK_GH, fx < 3 ? 3 : fx, BK_GH, hot ? T_INK : vc == T_DIM ? T_DIM : T_TRK(tk));
    }
    cv_blit((uint32_t)CARD_X(c), Y_LABEL);
}

/* the OCT- / OCT+ dialog (ui_draw.c draw_confirm draws through it): a SURF tile on the cleared page; the question (M,
 * S where M is too wide); what it is about (S, MID: its name, or a sequence's steps and notes); whether SAVE held
 * undoes it (else, in REC's colour, that it can't be undone); CANCEL filled, the look of the safe choice, and the
 * action outlined in REC's colour, each over its key (OCT- cancels, OCT+ does it: ui_input.c) */
#define BD_X 16                                       /* the tile: x 16..223, y 56..196 */
#define BD_Y 56
#define BD_W 208
#define BD_H 141
#define BD_BY 80                                      /* the buttons: 89 x 29, their keys under them */
#define BD_BW 89
#define BD_BH 29
static void confirm_text(char *a, char *b);            /* (ui_draw.c) */
static void draw_frame(void);
static void draw_columns(void);
static void draw_foot(void);
/* the page under the dialog, as the B mockup's: drawn again with every colour 70 % of the way to BG (the header stays
 * itself: REC shows over a dialog); the lint takes it as a fill, a backdrop never read */
static void confirm_backdrop(void)
{
    __typeof__(ux) keep = ux;
    uint16_t *c[] = {&ux.surf, &ux.text, &ux.theme, &ux.accent, &ux.mid, &ux.dim, &ux.line, &ux.sel, &ux.tint, &ux.ink,
                     &ux.rec, &ux.raise, &ux.key, &ux.lane, &ux.grid};
    uint32_t i;
    for (i = 0; i < sizeof c / sizeof c[0]; i++)
        *c[i] = ux_mix(*c[i], ux.bg, 70);
    for (i = 0; i < UI_NTRK; i++)                     /* (every track's colour) */
        ux.trk[i] = ux_mix(ux.trk[i], ux.bg, 70);
    ux.gen++;                                         /* (the text ramps: the dimmed ones) */
    cv_vhs = ux.neon;                                 /* ((Skiff N3) NEON: paused, as a tape) */
    draw_frame();
    draw_columns();
    draw_graph();
    draw_foot();
    cv_vhs = 0;
    ux = keep;
    GFX_HOOK_BACKDROP(0, H_HEAD, 240, 240 - H_HEAD);
}
static void draw_confirm_blocks(void)
{
    static const char *const VERB[CF_COUNT] = {     /* by ui.confirm (ui.c CF_*) */
        [CF_CLEAR_SEQ] = "CLEAR", [CF_OVR_PROJ] = "OVERWRITE", [CF_OVR_USER] = "OVERWRITE",
        [CF_INIT_SOUND] = "INITIALIZE",
        [CF_CLEAR_MOTION] = "CLEAR", [CF_ERASE_USER] = "ERASE",
        [CF_ERASE_BANK] = "CLEAR", [CF_CLEAR_ENTRY] = "CLEAR", [CF_LOAD_PROJ] = "LOAD",
        [CF_STORE_SCENE] = "OVERWRITE", [CF_RESET_SET] = "RESET",
        [CF_ERASE_PROJ] = "DELETE", [CF_NEW_PROJ] = "NEW", [CF_EMPTY_BANK] = "EMPTY", [CF_CLEAR_UNUSED] = "CLEAR"};
    static const uint8_t KEY[2] = {KC_OCTDN, KC_OCTUP};
    char a[24], b[40];
    const aafont_t *tf = &AF_M;
    uint32_t k = ui.confirm, i;
    int undoable = k == CF_CLEAR_SEQ || k == CF_INIT_SOUND || k == CF_CLEAR_MOTION;
    const char *hint = undoable ? "hold SAVE: undo" : "can't be undone";   /* ((Skiff U4) the undo hint's words, in
                                                       * the explanations' sentence case) */
    confirm_text(a, b);
    if (k == CF_CLEAR_SEQ && !b[0]) {                 /* what goes: the steps, the notes among them */
        const track_t *t = &trk[ui.confirm_trk % NTRK];
        uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 0u, n = 0;
        for (i = 0; i < len && i < NSTEP; i++)
            n += (uint32_t)step_on(&t->step[i]);
        say_count(b, sizeof b, len, " STEP");
        say_count(b, sizeof b, n, " NOTE");
        lower_words(b);                               /* ("16 steps, 9 notes"; a name keeps its capitals) */
    }
    confirm_backdrop();
    ui.head_sig = ~0u;
    cv_begin(BD_W, BD_H, T_SURF);
    if (ux.crypt) {                                   /* (Skiff C3) CRYPT: a stone tablet in a double frame */
        cr_stone(0, 0, BD_W, BD_H, T_SURF);
        cr_frame2(0, 0, BD_W, BD_H, T_LINE);
    } else if (ux.quark) {                            /* (Skiff Q2) QUARK: an instrument's panel, gold corners; what
                                                       * can't be undone under the radiation sign */
        cv_frame(0, 0, BD_W, BD_H, T_LINE);
        qk_corners(0, 0, BD_W, BD_H, T_ACCENT, 9);
        if (!undoable)
            qk_hazard(BD_W / 2, 10, 8, T_ACCENT);
    } else if (ux.neon) {                             /* (Skiff N3) NEON: night glass in a double neon frame */
        uint16_t st[2] = {T_INK, T_SURF};
        ne_vgrad(0, 0, BD_W, BD_H, st, 2);
        cv_frame(0, 0, BD_W, BD_H, T_ACCENT);
        cv_frame(3, 3, BD_W - 6, BD_H - 6, T_THEME);
    }
    if (text_w(tf, a) > BD_W - 16)
        tf = &AF_S;
    GFX_HOOK_ALIGN(0, 0, BD_W, 0, AL_H, "dialog question centred");
    cv_text_in(0, 30 - tf->asc, BD_W, tf, a, T_TEXT, T_SURF);
    if (k == CF_CLEAR_ENTRY) {                        /* (Skiff phase 5) SONG: the pattern's fate, who uses it, the
                                                       * track that goes empty (MID), then "can't be undone" when the
                                                       * pattern goes: 1 .. 3 rows, as the mockup's */
        static const uint8_t ROW_Y[3][3] = {{57, 0, 0}, {50, 64, 0}, {44, 58, 72}};   /* (baselines, 14 apart: the
                                                                                          * descenders clear) */
        char l[3][32];
        int gone;
        uint32_t n = song_clear_text(ui.confirm_trk, l, &gone), rows = n + (uint32_t)gone;
        for (i = 0; i < rows && i < 3u; i++) {
            const char *s = i < n ? l[i] : hint;
            GFX_HOOK_ALIGN(0, 0, BD_W, 0, AL_H, "dialog detail centred");
            cv_text_in(0, ROW_Y[rows - 1u][i] - AF_S.asc, BD_W, &AF_S, s, i < n ? T_MID : T_REC, T_SURF);
        }
    } else {
        if (b[0]) {
            char f[40];
            text_fit(f, sizeof f, b, &AF_S, BD_W - 16);   /* a name: free text */
            GFX_HOOK_ALIGN(0, 0, BD_W, 0, AL_H, "dialog detail centred");
            cv_text_flags(ink_in(&AF_S, f, BD_W), 50 - AF_S.asc, &AF_S, f, T_MID, T_SURF, 8u | (f[str_len(f) - 1u] == ELLIPSIS));
        }
        /* (centred as the header centres a message: head_hint_w) */
        cv_free_hint(HALF_UP(BD_W - head_hint_w(hint, BD_W)), (b[0] ? 64 : 57) - AF_S.asc, hint, undoable ? T_MID : T_REC,
                     T_SURF, BD_W);                   /* (alone: where the two lines' middle is) */
    }
    for (i = 0; i < 2u; i++) {                        /* CANCEL (OCT-), the action (OCT+) */
        int32_t x = i ? BD_W - 12 - BD_BW : 12;
        const char *w = i ? VERB[k < CF_COUNT && VERB[k] ? k : CF_CLEAR_SEQ] : "CANCEL";
        const aafont_t *wf = text_w(&AF_M, w) <= BD_BW - 8 ? &AF_M : &AF_S;
        if (i)
            cv_frame(x, BD_BY, BD_BW, BD_BH, T_REC);
        else if (ux.quark)                            /* ((Skiff Q2) QUARK: CANCEL cyan) */
            cv_rect(x, BD_BY, BD_BW, BD_BH, T_THEME);
        else if (ux.neon) {                           /* ((Skiff N3) NEON: CANCEL lit cyan) */
            uint16_t st[2] = {T_ACCENT, ux_mix(T_ACCENT, NE_VIOLET, 55)};
            ne_vgrad(x, BD_BY, BD_BW, BD_BH, st, 2);
        } else
            cv_rect(x, BD_BY, BD_BW, BD_BH, T_TEXT);
        if (i && ux.quark)                            /* (.. QUARK: the action in red brackets) */
            qk_corners(x - 2, BD_BY - 2, BD_BW + 4, BD_BH + 4, T_REC, 4);
        if (i && ux.neon)                             /* (.. the action in red neon: its glow) */
            cv_frame(x + 1, BD_BY + 1, BD_BW - 2, BD_BH - 2, ux_mix(T_REC, T_SURF, 65));
        GFX_HOOK_ALIGN(x, BD_BY, x + BD_BW, BD_BY + BD_BH, AL_HV, "dialog button word");
        cv_text_in(x, BD_BY + (wf == &AF_M ? CAP_IN(M, BD_BH) : CAP_IN(S, BD_BH)), BD_BW, wf, w, i ? T_REC : T_INK,
                   i ? T_SURF : ux.quark ? T_THEME : T_TEXT);
        GFX_HOOK_ALIGN(x, 0, x + BD_BW, 0, AL_H, "dialog key under its button");
        cv_keycap(x + ink_in(&AF_S, KC[KEY[i]].label, BD_BW), BD_BY + BD_BH + 3, KEY[i], T_KEY, T_INK, T_SURF);   /* (its
                                                       * ink centred: CRYPT's spacing trails its letters) */
    }
    cv_blit(BD_X, BD_Y);
}
