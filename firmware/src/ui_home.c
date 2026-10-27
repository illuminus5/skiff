/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff: HOME's panel, the tracks at a glance (DESIGN.md rule 10). Included by ui_graph.c (draw_graph). ((Skiff U3, N1)
 * all six: the selected one at today's size, the others compact; phase 6 showed four with a scroll bar)
 * A lane per track: its number (never the colour alone), its sound's name over its engine's, and the bar its sequencer
 * is in: 16 cells in fours, a note or a hit filled in the track's colour, a tie half filled; the step playing marked.
 * STYLE BLOCKS (direction B): each lane a tile on BG, the number big in the track's colour, an empty step a block of
 * LINE, the step playing a line before its cell; the selected track a band of its colour, all on it INK (its empty
 * steps the colour half way to INK). Else in the panel: the number on its cushion, an empty step a stub, the step
 * playing a bar under its cell, the selected track a RAISE row. A muted track's cells DIM. HOME tapped on HOME shows
 * the scope instead (ui.home_scope). Redrawn only when what it shows changes (home_sig), not every other frame. */
#define HL_H 31                                       /* a lane: the B mockup's 31, 3 between them from the panel's top */
#define HL_NUM_X 9                                    /* the number's ink (BLOCKS), the cushion's cell */
#define HL_NAME_X 28                                  /* the sound's name (M) over its engine's (S), to the steps */
#define HL_NAME_CY 2                                  /* their capitals' tops in the lane: 2..13, 16..25 */
#define HL_ENG_CY 16
#define HL_STEP_X 100                                 /* the 16 cells: 6 px, 2 between, 2 more between the fours */
#define HL_CELL_W 6
#define HL_CELL_H 16
static void sound_name(const track_t *t, char *b);   /* (ui_draw.c) */
static int32_t hl_cell_x(uint32_t i) { return HL_STEP_X + (int32_t)i * (HL_CELL_W + 2) + (int32_t)(i / 4u) * 2; }

/* the bar track k shows: the one its sequencer is in (from its first step), and how many of its cells exist */
static uint32_t hl_bar(const track_t *t, uint32_t *n)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 16u, first = (t->seq_idx % len) / 16u * 16u;
    *n = len - first > 16u ? 16u : len - first;
    return first;
}

/* what the overview shows: a change of it redraws the panel (draw_graph's signature) */
static uint32_t home_sig(void)
{
    uint32_t h = 2166136261u ^ song.sel * 7919u ^ (uint32_t)song.playing * 104729u ^ ux.gen * 131u, k, i, n;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        const step_t *s = t->step;
        uint32_t first = hl_bar(t, &n);
        h = (h ^ (first + 977u * n + 7u * (uint32_t)t->p[P_MUTE] + 31u * t->eng_req + 257u * t->preset + 65537u * t->user +
                  (uint32_t)midi_mark(k) * 0x40000000u)) * 16777619u;
        if (song.playing)
            h = (h ^ (t->seq_idx + 1u)) * 16777619u;
        for (i = 0; i < n; i++)
            h = (h ^ (s[first + i].n | (uint32_t)s[first + i].hit << 8 | (uint32_t)s[first + i].time << 16)) * 16777619u;
    }
    return h;
}

/* (Skiff U3, the review's N1; design/mock_u3.py, the user's pick A) every track at once: the selected one's lane as
 * before (HL_H, its name over its engine's), the others compact (HL_C_H: the number in M, the name in S, the cells
 * HL_C_CELL_H); where all fit at today's size (four tracks) all as before */
#define HL_C_H 18
#define HL_C_CELL_H 12
static void graph_tracks(void)
{
    uint32_t k, i, n;
    int32_t tx = -1, ty = 0;                          /* (Skiff C3) CRYPT: the torch's place (the selected track's playhead) */
    int32_t qx = -1, qy = 0;                          /* (Skiff Q3) QUARK: the collision's (the same) */
    int six = (int32_t)NTRK * (HL_H + 3) - 3 > (int32_t)graph_h();
    int32_t y = six ? ((int32_t)graph_h() - (HL_H + ((int32_t)NTRK - 1) * (HL_C_H + 3))) / 2 : 0;
    cv_rect(0, 0, 240, (int32_t)graph_h(), T_BG);      /* (over the panel: the lanes are its tiles) */
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        const step_t *s = t->step;
        uint32_t first = hl_bar(t, &n), play = t->seq_idx % 16u;
        int sel = k == song.sel, muted = t->p[P_MUTE] != 0, big = sel || !six;
        int32_t h = big ? HL_H : HL_C_H, ch = big ? HL_CELL_H : HL_C_CELL_H, b[4];
        int ib = sel && !ux.quark;                    /* (the selected lane a band, INK on it; (Skiff Q3) QUARK: a beam,
                                                       * its colour faint, the colours as on the others) */
        uint16_t col = muted ? T_DIM : T_TRK(k), lane = sel ? (ux.quark ? ux_mix(col, T_BG, 80) : col) : ux.quark ? T_BG : T_SURF;
        uint16_t ink = ib ? T_INK : T_TEXT, on = ib ? T_INK : col, off = ib ? ux_mix(col, T_INK, 55) : T_LINE;
        char name[20], d[2] = {(char)('1' + k), 0};
        if (ux.crypt && !sel)                         /* (Skiff C3) CRYPT: the lanes stone */
            cr_stone(3, y, 234, h, lane);
        else if (ux.neon && sel) {                    /* (Skiff N3) NEON: the selected lane a neon sign (its colour into
                                                       * violet), the others asphalt, the lane's paint between them */
            uint16_t st[2] = {col, ux_mix(col, NE_VIOLET, 70)};
            ne_hgrad(3, y, 234, h, st, 2);
        } else
            cv_rect(3, y, 234, h, lane);
        if (ux.quark && sel) {                        /* (Skiff Q3) QUARK: the beam's walls lit; the others two rails */
            cv_rect(3, y, 234, 1, col);
            cv_rect(3, y + h - 1, 234, 1, col);
        } else if (ux.quark) {
            cv_rect(HL_STEP_X - 2, y + h / 2 - 3, 236 - HL_STEP_X + 2, 1, T_LINE);
            cv_rect(HL_STEP_X - 2, y + h / 2 + 3, 236 - HL_STEP_X + 2, 1, T_LINE);
        }
        if (ux.neon && k + 1u < NTRK)
            for (i = 6; i < 234u; i += 10u)
                cv_rect((int32_t)i, y + h + 1, 5, 1, ux_mix(NE_YELLOW, T_BG, 65));
        text_ink(big ? &AF_V : &AF_M, d, b);             /* the number itself: the colour on SURF, INK on the band */
        GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "home lane number on its middle");
        cv_text_on(HL_NUM_X - b[0], y + (big ? CAP_IN(V, HL_H) : CAP_IN(M, HL_C_H)), big ? &AF_V : &AF_M, d,
                   ib ? T_INK : col, lane);
        if (midi_mark(k))                             /* ((Skiff U4, F4) MIDI IN playing it: a mark after the number) */
            cv_rect(HL_NUM_X + (big ? 14 : 10), y + h / 2 - 2, 4, 4, ib ? T_INK : T_THEME);
        sound_name(t, name);
        if (big) {
            cv_free_text(HL_NAME_X, y + HL_NAME_CY - AF_M_CAP_Y, &AF_M, name[0] ? name : "--", muted && !sel ? T_DIM : ink,
                         lane, HL_STEP_X - 4 - HL_NAME_X);
            cv_free_text(HL_NAME_X, y + HL_ENG_CY - AF_S_CAP_Y, &AF_S, ENGINES[t->eng_req % NENGINES]->name,
                         ib ? T_INK : T_MID, lane, HL_STEP_X - 4 - HL_NAME_X);
        } else {
            GFX_HOOK_ALIGN(0, y, 0, y + h, AL_V, "home compact lane name on its middle");
            cv_free_text(HL_NAME_X, y + CAP_IN(S, HL_C_H), &AF_S, name[0] ? name : "--", muted ? T_DIM : T_TEXT, lane,
                         HL_STEP_X - 4 - HL_NAME_X);
        }
        for (i = 0; i < n; i++) {                     /* (past the track's length: nothing) */
            const step_t *st = &s[first + i];
            int32_t x = hl_cell_x(i), cy = y + (h - ch) / 2;
            if (ux.quark) {                           /* (Skiff Q3) QUARK: a note a bunch of particles in the pipe (played
                                                       * this bar: spent, a ring), a tie its trail, nothing a dot */
                int32_t bx = x + HL_CELL_W / 2, by = y + h / 2;
                int past = song.playing && t->seq_idx / 16u * 16u == first && i < play;
                if (step_on(st) || st->hit) {
                    if (past)
                        qk_ellipse(bx, by, 2, 2, 0, ux_mix(col, T_BG, 45), 8);
                    else {
                        qk_disc(bx, by, big ? 3 : 2, col);
                        cv_rect(bx - 1, by - 1, 1, 1, ux_mix(col, T_TEXT, 60));
                    }
                } else if (step_tie(st))
                    cv_rect(x - 1, by, HL_CELL_W + 2, 1, ux_mix(col, T_BG, 40));
                else
                    cv_rect(bx, by, 1, 1, T_DIM);
            } else if (ux.crypt) {                    /* (Skiff) CRYPT: the steps in ASCII, # a note, - a tie, . nothing */
                int32_t base = cy + ch / 2 + 4;       /* (S's capitals centred on the cell) */
                if (step_on(st) || st->hit)
                    cr_ch(x, base, &AF_S, '#', on);
                else if (step_tie(st))
                    cr_ch(x + 1, base, &AF_S, '-', on);
                else
                    cr_ch(x + 2, base, &AF_S, '.', sel ? off : T_DIM);
            } else if (ux.neon && !sel && (step_on(st) || st->hit)) {   /* (Skiff N3) NEON: a lit */
                cv_rect(x - 1, cy - 1, HL_CELL_W + 2, ch + 2, ux_mix(on, lane, 65));      /* tube: its glow, its hot top */
                cv_rect(x, cy, HL_CELL_W, ch, on);
                cv_rect(x + 1, cy + 1, HL_CELL_W - 2, 1, ux_mix(on, T_TEXT, 60));
            } else if (ux.neon && !sel && !step_tie(st)) {
                cv_rect(x + HL_CELL_W / 2 - 1, cy + ch / 2, 2, 1, off);   /* (nothing: a dim dash) */
            } else if (step_on(st) || st->hit) {
                cv_rect(x, cy, HL_CELL_W, ch, on);
            } else {
                cv_rect(x, cy, HL_CELL_W, ch, off);
                if (step_tie(st))               /* a tie: the note held on, half the cell */
                    cv_rect(x, cy + ch / 4, HL_CELL_W, ch / 2, on);
            }
            if (song.playing && i == play && t->seq_idx / 16u * 16u == first) {   /* the step playing: a line before it */
                if (ux.neon)                          /* ((Skiff N3) NEON: a laser, its glow) */
                    cv_rect(x - 3, y + 2, 3, h - 4, ux_mix(T_ACCENT, lane, 60));
                if (ux.quark) {                       /* ((Skiff Q3) QUARK: the beam line, dotted gold; on the selected
                                                       * lane a collision) */
                    int32_t yy;
                    for (yy = y; yy < y + h; yy += 2)
                        cv_rect(x - 2, yy, 1, 1, T_ACCENT);
                    if (sel && !ui_ph_line) {     /* ((Skiff #27) MENU > PLAYHEAD LINE: no collision) */
                        qx = x - 2;
                        qy = y + h / 2;
                    }
                } else
                cv_rect(x - 2, y + 2, 1, h - 4, ux.neon ? T_ACCENT : sel ? T_INK : ux.crypt ? CR_GOLD : T_TEXT);
                if (sel && !ui_ph_line) {         /* ((Skiff #27) PLAYHEAD LINE: no torch, no car) */
                    tx = x - 7;
                    ty = y + (h - 24) / 2;
                }
            }
        }
        y += h + 3;
    }
    if (ux.crypt && tx >= 0)                          /* (Skiff C3) CRYPT: a torch on it, flickering a step to the next */
        cr_torch(tx, ty, ui.frame / 6u, 2);
    if (ux.quark && qx >= 0)                          /* (Skiff Q3) QUARK: its tracks a new way each step */
        qk_burst(qx, qy, 8, TSEL->seq_idx * 2654435761u + 7u, 24, T_BG);
    if (ux.neon && tx >= 0) {                         /* (Skiff N3) NEON: the car, its nose at the playhead (on the steps
                                                       * just played), its light trails back to the lane's first step */
        int32_t cx = tx + 7 - 24;
        if (cx < HL_STEP_X - 6)
            cx = HL_STEP_X - 6;
        ne_car(cx, ty + 8, SPR_CAR24, 1, T_TEXT, cx - (HL_STEP_X - 4), T_TRK(song.sel));
    }
}
