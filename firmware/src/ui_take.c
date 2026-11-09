/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff: SHIFT + SAVE puts the last complete pass of a track's TAKES log (take.c) into the track. Included by ui.c
 * after load_end (the undo copy). Main loop only. */

/* (2.0, #25) the NOTES edit copy (ui_input.c): a take on a POLY track playing forward ties each held note on by itself */
static void ed_open(step_t *w, const track_t *t, uint32_t len);
static int ed_add(step_t *s, uint32_t p, uint32_t tie);
static void ed_close(track_t *t, step_t *w, uint32_t len, uint64_t keep);

/* the hold of the note on in log entry i (pass want, step a): the steps it ties into, a + 1 .. *end - 1 (the
 * note's end, or the next note on the track: a new note ends the hold, as REC; (2.0) not when own: notes overlap);
 * held past the pass: to its end */
static uint32_t take_hold_end(uint32_t k, uint32_t i, uint32_t n, uint32_t want, uint32_t len, int own)
{
    const take_ev_t *e = &take_ev[i % TAKE_EV];
    uint32_t j;
    for (j = i + 1u; j < n; j++) {
        const take_ev_t *f = &take_ev[j % TAKE_EV];
        if (f->trk != k)
            continue;
        if (f->pass != want)
            return len;                               /* held into the next pass: to the end (no ties wrap) */
        if (f->vel && f->step > e->step && !own)
            return f->step;                           /* another note: the hold ends before it */
        if (!f->vel && f->note == e->note)
            return f->step > e->step ? f->step : e->step + 1u;   /* let go (stamped as REC: before a step's middle
                                                       * is that step, not held into it) */
    }
    return len;                                       /* still held */
}

/* (2.0, #44) a chain: the note on in log entry i, before pass want, held on into it (the next part): the step its hold
 * ends there (let go there, or (not own) the track's next note there; held on: the part's end); 0 not held into it */
static uint32_t take_carry_end(uint32_t k, uint32_t i, uint32_t n, uint32_t want, uint32_t len, int own)
{
    const take_ev_t *e = &take_ev[i % TAKE_EV];
    uint32_t j;
    for (j = i + 1u; j < n; j++) {
        const take_ev_t *f = &take_ev[j % TAKE_EV];
        if (f->trk != k)
            continue;
        if (f->pass > want)
            return len;
        if ((f->vel && !own) || (!f->vel && f->note == e->note))
            return f->pass < want ? 0u : f->step;
    }
    return len;
}
/* .. a held note on into step s: its own tie beside what is there (own; no room: 0), else a TIE into an empty step */
static int take_tie(step_t *d, uint32_t note, int own)
{
    if (own)
        return ed_add(d, note, 1);
    if (!d->n && !d->hit) {
        d->time = ST_TIE;
        d->flags = 0;
        d->vel = 0;
    }
    return 1;
}
/* the notes of pass want of track k's log into steps tmp (LEN len), as REC merges them; ((2.0, #44) carry: a chain's
 * part, the notes held on into it from the pass before go on from its first step, *tied set). How many went in */
static uint32_t take_notes(uint32_t k, step_t *tmp, uint32_t len, uint32_t want, int own, int carry, uint32_t *tied)
{
    track_t *t = &trk[k % NTRK];
    uint32_t n = take_ev_n, first = n > TAKE_EV ? n - TAKE_EV : 0u, i, s, end, notes = 0;
    for (i = first; i < n; i++) {
        const take_ev_t *e = &take_ev[i % TAKE_EV];
        step_t *d;
        if (e->trk != k || !e->vel)
            continue;
        if (carry && e->pass < want && !drum_track(t)) {
            for (end = take_carry_end(k, i, n, want, len, own), s = 0; s < end && s < len && take_tie(&tmp[s], e->note, own); s++)
                *tied = 1;
            continue;
        }
        if (e->pass != want || e->step >= len)
            continue;
        d = &tmp[e->step];
        if (rec_feel && !(((1u << d->n) - 1u) & ~step_ties(d)) && !d->hit)
            d->nudge = e->feel;                       /* (Skiff) FEEL: a step with no note starting plays where its */
        rec_put(t, d, e->note, e->vel);               /* note was played */
        notes++;
        if (drum_track(t))
            continue;                                 /* (hits: no holds) */
        end = take_hold_end(k, i, n, want, len, own);
        for (s = e->step + 1u; s < end && s < len && take_tie(&tmp[s], e->note, own); s++)
            ;                                         /* ((2.0) its own tie on each, beside what is there; no room: it
                                                       * ends. Else a held note ties on into empty steps only: a take is
                                                       * an overdub, it never wipes a note) */
    }
    return notes;
}

/* "14 NOTES" / "1 NOTE" after what b holds (a ", " between them) */
static void say_count(char *b, uint32_t sz, uint32_t n, const char *what)
{
    uint32_t i = str_len(b);
    if (i)
        str_cpy(b + i, ", ", sz - i);
    i = str_len(b);
    fmt_int(b + i, (int32_t)n);
    i = str_len(b);
    str_cpy(b + i, what, sz - i);
    if (n != 1u) {
        i = str_len(b);
        str_cpy(b + i, "S", sz - i);
    }
}

/* the TAKE box (STYLE BLOCKS, DESIGN.md direction B): over the panel for TB_FRAMES (ui.take_t: ui_draw.c counts it
 * down, a message ends it): the track's number on its colour, TAKE KEPT (TAKE PARTIAL: the log ran over), what it
 * put in, and the undo. A white tile, INK on it, a BG line round it. FLAT, LINE, and a view with no panel for it
 * (a layer, MENU > LARGE's tall cards, the MIXER): the header says it instead. Drawn by draw_graph (ui_graph.c). */
#define TB_FRAMES 130                                 /* (~2 s) */
#define TB_X 20                                       /* in the panel: x 20..219, y 8..80 */
#define TB_Y 8
#define TB_W 200
#define TB_H 73
static struct { uint8_t trk, gen, kind; char title[24], line[40]; } take_box;   /* kind 1: the undo box */
static int take_box_fits(void)
{
    return !ui.layer && large_kind() != LK_TALL && (ui.home || cur_page()->graph != GR_TRK);
}
static void lower_words(char *s)                      /* a sentence the UI wrote (counts, words): the B mockup's case */
{
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z')
            *s = (char)(*s + 32);
}
/* a key and its word on the box, as the mockup writes them: "OCT-: BACK" (MID: nothing that way) */
static int32_t take_box_key(int32_t x, uint32_t id, const char *w, int on)
{
    char b[32];
    kh_plain(b, sizeof b, id, w);
    return cv_text_on(x, TB_Y + 49, &AF_S, b, on ? T_INK : T_MID, T_TEXT);
}
static void take_box_draw(void)
{
    char d[2] = {(char)('1' + take_box.trk), 0};
    GFX_HOOK_OVERLAY(TB_X - 1, TB_Y - 1, TB_X + TB_W + 1, TB_Y + TB_H + 1);   /* (the lanes' words under it: hidden) */
    cv_rect(TB_X - 1, TB_Y - 1, TB_W + 2, TB_H + 2, T_BG);
    cv_rect(TB_X, TB_Y, TB_W, TB_H, T_TEXT);
    cv_rect(TB_X + 10, TB_Y + 9, 16, 16, T_TRK(take_box.trk));   /* the track: its number on its colour */
    GFX_HOOK_ALIGN(TB_X + 10, TB_Y + 9, TB_X + 26, TB_Y + 25, AL_HV, "take box track number");
    cv_text_in(TB_X + 10, TB_Y + 9 + CAP_IN(M, 16), 16, &AF_M, d, T_INK, T_TRK(take_box.trk));
    GFX_HOOK_ALIGN(0, TB_Y + 9, 0, TB_Y + 25, AL_V, "take box title on its number's line");
    cv_text_fit(TB_X + 33, TB_Y + 9 + CAP_IN(M, 16), &AF_M, take_box.title, T_INK, T_TEXT, TB_W - 43);
    {                                                 /* ("14 notes, 3 knob moves", as the mockup's) */
        char ln[sizeof take_box.line];
        str_cpy(ln, take_box.line, sizeof ln);
        lower_words(ln);
        cv_text_fit(TB_X + 10, TB_Y + 31, &AF_S, ln, T_INK, T_TEXT, TB_W - 20);
    }
    if (take_box.kind)                                /* the undo window (ui.c undo_step): further back, or redo */
        take_box_key(take_box_key(TB_X + 10, KC_OCTDN, "BACK", undo_back < undo_n) + 14, KC_OCTUP, "REDO", undo_back);
    else                                              /* the take's undo: SAVE held (the mockup's words) */
        cv_text_on(TB_X + 10, TB_Y + 49, &AF_S, "HOLD SAVE: UNDO", T_INK, T_TEXT);
}
/* the undo box (ui.c undo_step): UNDO or REDO and the copy's name, how far back of how many */
static void take_box_undo(int redo, uint32_t trk, const char *name)
{
    uint32_t n;
    take_box.trk = (uint8_t)(trk % NTRK);
    take_box.gen++;
    take_box.kind = 1;
    str_cpy(take_box.title, redo ? "REDO " : "UNDO ", sizeof take_box.title);
    str_cpy(take_box.title + 5, name, sizeof take_box.title - 5);
    fmt_int(take_box.line, (int32_t)undo_back);
    n = str_len(take_box.line);
    str_cpy(take_box.line + n, " OF ", sizeof take_box.line - n);
    n = str_len(take_box.line);
    fmt_int(take_box.line + n, (int32_t)undo_n);
    n = str_len(take_box.line);
    str_cpy(take_box.line + n, " UNDONE", sizeof take_box.line - n);
    ui.msg_t = 0;                                     /* (the box says it: no header word) */
    ui.msg2[0] = 0;
    ui.take_t = UNDO_WIN;
}

/* ------------------------------------------------------------- FREE TAKES (2.5) --- */
/* SHIFT + SAVE with the transport stopped and notes played on the track since the stop (take.c free_ev): its last phrase
 * (after the last silence of over 2.5 s; 30 s at most) goes into the track at a guessed tempo, a picker over it: KNOB 1
 * the guess (the likeliest first; double and half tempo among them), KNOB 2 the BARS (1 2 4 at 1/16; 8 at 1/8), KNOB 3
 * the first beat (which note is the 1: the ones before it go at the loop's end, a pickup), KNOB 4 the TEMPO (SET: the
 * song's BPM the guess's; KEEP: as it is, the first choice when another track has notes, and the only one on an
 * external clock). PLAY listens to it as it is (the picker stays); OCT+ keeps it (SHIFT + SAVE again too), OCT- puts
 * the track and the tempo back. The picker is a screen of its own, as a page's (the user's ask, 2.5 dev D): the header
 * FREE TAKE, the four knobs as its cards (ui_draw.c fr_columns), the track's steps as PATTERN draws them (ui_graph.c),
 * the footer OCT+ KEEP, OCT- BACK, PLAY LISTEN. One undo copy (SAVE held: the steps, LEN and DIV back; the tempo stays). The notes as
 * REC records them (rec_put: chords, drum lanes, velocity; FEEL: how far off the grid as NUDGE), a held note tied on,
 * in place of the track's pattern (every step: played stopped, the take was never in time with it). The guess: every
 * BPM from 40 to 240 by how well the onsets (notes each within 30 ms of the one before, a chord strummed too: one, on
 * one step) sit on its 1/16 grid at its best phase, the length of the mean of their phases as unit vectors (1: all on
 * it); its peaks, the best first, but one within 80 .. 160 ahead when nearly as good. Main loop only */
#define FR_GAP 1250u                                    /* a phrase starts after a silence of 2.5 s (ms / 2) */
#define FR_SPAN 15000u                                  /* .. and is 30 s long at most */
#define FR_CHORD 15u                                    /* notes within 30 ms: one onset */
#define FR_NC 4u                                        /* guesses */
#define FR_NON 128u                                     /* onsets the guess reads (the phrase's first) */
#define FR_NST 8u                                       /* onsets offered as the first beat */
static const uint8_t FR_BARS[4] = {1, 2, 4, 8};
static struct {
    uint8_t on, trk, nc, ci, bi, start, set, nst;       /* open; its track; the guesses, the one in use; FR_BARS's;
                                                         * the first beat (an onset), TEMPO SET; first beats offered */
    uint16_t bpm[FR_NC];                                /* the guesses */
    uint32_t ph[FR_NC];                                 /* .. their grid's phase (2^32: a 1/16) */
    uint32_t p0, non;                                   /* the phrase: free_ev from entry p0; its onsets */
    uint32_t pat;                                       /* the pattern it opened on (trk_pat) */
    int16_t bpm0, len0, div0;                           /* the tempo, LEN and DIV it opened with */
} fr;
static step_t fr_from[NSTEP] __attribute__((section(".pool")));   /* the steps as it opened */
static uint16_t fr_on[FR_NON] __attribute__((section(".pool")));  /* the phrase's onsets: ms / 2 after its first */
#define FE_T(e) ((e) & 0x7FFFu)
#define FE_NOTE(e) (((e) >> 15) & 127u)
#define FE_VEL(e) (((e) >> 22) & 127u)
#define FE_TRK(e) ((e) >> 29)
static uint32_t fe_dt(uint32_t a, uint32_t b) { return (FE_T(b) - FE_T(a)) & 0x7FFFu; }   /* ms / 2 from a to b */

/* the angle of (x, y), 2^32 a turn (CORDIC, 16 rounds: to a 2^16th of a turn) */
static uint32_t fr_atan2(int64_t y, int64_t x)
{
    static const uint32_t AT[16] = {536870912u, 316933406u, 167458907u, 85004756u, 42667331u, 21354465u, 10679838u,
                                    5340245u, 2670163u, 1335087u, 667544u, 333772u, 166886u, 83443u, 41722u, 20861u};
    uint32_t a = 0, i;
    int64_t t;
    if (x < 0) {                                        /* half a turn round first: then within a quarter */
        x = -x;
        y = -y;
        a = 0x80000000u;
    }
    for (i = 0; i < 16u; i++) {
        if (y > 0) {
            t = x + (y >> i);
            y -= x >> i;
            a += AT[i];
        } else {
            t = x - (y >> i);
            y += x >> i;
            a -= AT[i];
        }
        x = t;
    }
    return a;
}
/* BPM b's 1/16 in phase a ms / 2 (a 1/16 is 15000 / b ms, 7500 / b of them: 2^32 each): (b << 32) / 7500 without a
 * 64-bit divide (none in the firmware: 2^32 = 7500 x 572662 + 2296) */
static uint32_t fr_inc(uint32_t b) { return b * 572662u + b * 2296u / 7500u; }
/* how well the n onsets sit on BPM b's 1/16 grid: the squared length of their phases' sum, >> 16 (n x 32767 all on
 * it, squared: the guesses of one phrase compare); *ph that grid's phase */
static uint32_t fr_fit(uint32_t n, uint32_t b, uint32_t *ph)
{
    uint32_t inc = fr_inc(b), i;
    int64_t c = 0, s = 0;
    for (i = 0; i < n; i++) {
        uint32_t p = fr_on[i] * inc;
        c += osc_sine(p + 0x40000000u);
        s += osc_sine(p);
    }
    *ph = fr_atan2(s, c);
    return (uint32_t)((uint64_t)(c * c + s * s) >> 16);   /* (n <= FR_NON: below 2^46 before it) */
}
/* track k's last phrase since the stop: its first entry (fr.p0) and onsets (fr_on, fr.non); 0: none */
static int fr_phrase(uint32_t k)
{
    uint32_t n = free_n, i0 = n - free_n0 > FREE_EV ? n - FREE_EV : free_n0, i, t = 0, last = 0, held = 0, p0 = n;
    uint32_t t0 = 0, tl = 0;
    for (i = i0; i < n; i++) {                          /* the last start after a silence (nothing held) */
        uint32_t e = free_ev[i % FREE_EV];
        if (i > i0)
            t += fe_dt(free_ev[(i - 1u) % FREE_EV], e);
        if (FE_TRK(e) != k)
            continue;
        if (FE_VEL(e)) {
            if (p0 == n || (!held && t - last > FR_GAP))
                p0 = i, t0 = t;
            held++;
            tl = t;
        } else if (held)
            held--;
        last = t;
    }
    if (p0 == n)
        return 0;
    if (tl - t0 > FR_SPAN) {                            /* 30 s at most: from the first onset in the last 30 s */
        for (i = p0, t = t0; i < n; i++) {
            if (i > p0)
                t += fe_dt(free_ev[(i - 1u) % FREE_EV], free_ev[i % FREE_EV]);
            if (FE_TRK(free_ev[i % FREE_EV]) == k && FE_VEL(free_ev[i % FREE_EV]) && tl - t <= FR_SPAN)
                break;
        }
        p0 = i;
    }
    fr.p0 = p0;
    fr.non = 0;
    for (i = p0, t = 0, last = 0; i < n && fr.non < FR_NON; i++) {   /* its onsets: a chord's notes one (a strum's) */
        uint32_t e = free_ev[i % FREE_EV];
        if (i > p0)
            t += fe_dt(free_ev[(i - 1u) % FREE_EV], e);
        if (FE_TRK(e) != k || !FE_VEL(e))
            continue;
        if (!fr.non || t - last > FR_CHORD)
            fr_on[fr.non++] = (uint16_t)(t < 65535u ? t : 65535u);
        last = t;
    }
    return 1;
}
/* the guesses (fr.bpm, fr.ph, fr.nc): the fit's peaks over 40 .. 240 BPM, the best first, then the others as good as
 * half of it; one within 80 .. 160 first when it is nearly (80 %) as good as the best. Fewer than 3 onsets: the song's
 * tempo */
static void fr_guess(void)
{
    uint32_t q[3] = {0, 0, 0}, p[3] = {0, 0, 0}, b, i, j, best = 0, pk_q[8], pk_b[8], pk_p[8], np = 0;
    fr.nc = 1;
    fr.bpm[0] = (uint16_t)song.g[G_BPM];
    (void)fr_fit(fr.non, fr.bpm[0], &fr.ph[0]);
    if (fr.non < 3u)
        return;
    for (b = 40; b <= 241u; b++) {                      /* (241: the last one's peak test) */
        q[0] = q[1], p[0] = p[1];
        q[1] = q[2], p[1] = p[2];
        q[2] = b <= 240u ? fr_fit(fr.non, b, &p[2]) : 0u;
        if (b > 40u && q[1] >= q[0] && q[1] > q[2]) {   /* b - 1: a peak (40 too: q[0] 0) */
            for (i = 0; i < np && pk_q[i] >= q[1]; i++)
                ;
            if (i < 8u) {
                for (j = np < 8u ? np : 7u; j > i; j--)
                    pk_q[j] = pk_q[j - 1u], pk_b[j] = pk_b[j - 1u], pk_p[j] = pk_p[j - 1u];
                pk_q[i] = q[1], pk_b[i] = b - 1u, pk_p[i] = p[1];
                np += np < 8u;
            }
        }
    }
    if (!np)
        return;
    for (i = 0; i < np; i++)                            /* one within 80 .. 160, nearly as good: first */
        if (pk_b[i] >= 80u && pk_b[i] <= 160u) {
            if (i && (uint64_t)pk_q[i] * 5u >= (uint64_t)pk_q[0] * 4u)
                best = i;
            break;
        }
    fr.nc = 0;
    fr.bpm[fr.nc] = (uint16_t)pk_b[best], fr.ph[fr.nc++] = pk_p[best];
    for (i = 0; i < np && fr.nc < FR_NC; i++)
        if (i != best && (uint64_t)pk_q[i] * 2u >= pk_q[0])
            fr.bpm[fr.nc] = (uint16_t)pk_b[i], fr.ph[fr.nc++] = pk_p[i];
}

/* where the time t (ms / 2 after the phrase's first onset) sits on the grid in use, 2^32 a step, from the first beat */
static int64_t fr_pos(uint32_t t)
{
    uint32_t inc = fr_inc(fr.bpm[fr.ci]), ts = fr_on[fr.start];
    int32_t dev = (int32_t)(ts * inc - fr.ph[fr.ci]);   /* the first beat's own note off the 1/16 grid */
    int64_t p = (int64_t)((int32_t)t - (int32_t)ts) * inc + dev;
    return FR_BARS[fr.bi] >= 8u ? p >> 1 : p;           /* (BARS 8: 1/8 steps) */
}
static int32_t fr_step(int64_t p) { return (int32_t)((p + (1LL << 31)) >> 32); }   /* to the nearest step */
/* the phrase into the track's steps (all of them: the pattern it had goes) at the guess, BARS and first beat in use;
 * LEN, DIV; the tempo */
static void fr_render(void)
{
    track_t *t = &trk[fr.trk % NTRK];
    uint32_t len = FR_BARS[fr.bi] >= 8u ? 64u : 16u * FR_BARS[fr.bi], n = free_n, i, j, ti, tj, tl = 0, any = 0;
    int own = rec_poly(t), drum = drum_track(t);
    int32_t sl = 0;
    step_t tmp[NSTEP];
    for (i = 0; i < NSTEP; i++)                         /* (empty, every byte, as a new unit's: unfolded as it is, */
        tmp[i] = (step_t){{0}, 0, ST_REST};             /* for own's ed_close too) */
    for (i = fr.p0, ti = 0; i < n; i++) {
        uint32_t e = free_ev[i % FREE_EV], note = FE_NOTE(e), end = len;
        int64_t p;
        int32_t s, s0, f;
        if (i > fr.p0)
            ti += fe_dt(free_ev[(i - 1u) % FREE_EV], e);
        if (FE_TRK(e) != fr.trk || !FE_VEL(e))
            continue;
        p = fr_pos(ti);
        s = s0 = any && ti - tl <= FR_CHORD ? sl : fr_step(p);   /* (a chord's notes, strummed too: its first one's step) */
        any = 1, tl = ti, sl = s0;
        f = (int32_t)(((p - ((int64_t)s << 32)) * 64 + (1LL << 31)) >> 32);   /* how far off it: 64ths of a step */
        if (s < 0 && s >= -(int32_t)len)
            s += (int32_t)len;                          /* before the first beat: a pickup, at the loop's end */
        if (s < 0 || s >= (int32_t)len)
            continue;
        if (rec_feel && !(((1u << tmp[s].n) - 1u) & ~step_ties(&tmp[s])) && !tmp[s].hit)
            tmp[s].nudge = (int8_t)clamp(f, NUDGE_MIN, NUDGE_MAX);   /* (FEEL: where it was played) */
        rec_put(t, &tmp[s], note, FE_VEL(e));
        if (drum)
            continue;                                   /* (hits: no holds) */
        for (j = i + 1u, tj = ti; j < n; j++) {         /* its end: let go, or (not own) the track's next note */
            uint32_t g = free_ev[j % FREE_EV];
            tj += fe_dt(free_ev[(j - 1u) % FREE_EV], g);
            if (FE_TRK(g) != fr.trk || (FE_VEL(g) ? own : FE_NOTE(g) != note))
                continue;
            {
                int32_t se = fr_step(fr_pos(tj)) + (s0 < 0 ? (int32_t)len : 0);   /* (a pickup's: at the end too) */
                end = se > s ? (uint32_t)se : (uint32_t)s + 1u;
            }
            break;
        }
        for (j = (uint32_t)s + 1u; j < end && j < len && take_tie(&tmp[j], note, own); j++)
            ;
    }
    if (own)
        ed_close(t, tmp, len, 0);                       /* (folded: TIEs where they can be; it writes steps 1..LEN) */
    fm1_irq_off();                                      /* the audio ISR must not see half a pattern */
    memcpy(t->step, tmp, sizeof tmp);                   /* (own: the same 1..LEN again; past LEN: empty) */
    fm1_irq_on();
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = (int16_t)(FR_BARS[fr.bi] >= 8u ? 1 : 2);   /* (N_DIV: 1/8, 1/16) */
    t->seq_active = 1;
    if (!song.g[G_CLOCK])
        song.g[G_BPM] = (int16_t)(fr.set ? fr.bpm[fr.ci] : fr.bpm0);
}
static void tl_undo_take(track_t *t, const char *name);   /* (ui_tools.c) */
/* SHIFT + SAVE stopped (take_commit): track k's free take, its picker open; 0: nothing played on it since the stop */
static int free_open(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t j, s, other = 0;
    if (!fr_phrase(k % NTRK))
        return 0;
    fr_guess();
    fr.trk = (uint8_t)(k % NTRK);
    fr.ci = 0;
    fr.start = 0;
    fr.nst = (uint8_t)(fr.non < FR_NST ? fr.non : FR_NST);
    for (j = 0; j < NTRK; j++)                          /* another track with notes: KEEP the tempo first */
        for (s = 0; s < NSTEP && j != k % NTRK && !other; s++)
            other = trk[j].step[s].time == ST_NOTE && (trk[j].step[s].n || trk[j].step[s].hit);
    fr.set = (uint8_t)(!other && !song.g[G_CLOCK]);
    {                                                   /* BARS: what the phrase takes, from its first onset to its last */
        uint64_t steps = ((uint64_t)fr_on[fr.non - 1u] * fr_inc(fr.bpm[0]) >> 32) + 1u;
        for (fr.bi = 0; fr.bi < 3u && 16u * FR_BARS[fr.bi] < steps; fr.bi++)
            ;
    }
    tl_undo_take(t, "FREE TAKE");                       /* (one undo: SAVE held) */
    memcpy(fr_from, t->step, sizeof fr_from);
    fr.len0 = t->p[P_SLEN];
    fr.div0 = t->p[P_SDIV];
    fr.bpm0 = song.g[G_BPM];
    fr.pat = trk_pat(k % NTRK);
    free_hold = 1;
    fr.on = 1;
    fr_render();
    ui.take_t = ui.msg_t = 0;                           /* (its screen: a box or a message of before goes) */
    ui.msg2[0] = 0;
    ui.force = 1;
    return 1;
}
/* the picker's KNOB k (0..3) turned s steps */
static void fr_knob(uint32_t k, int32_t s)
{
    if (k == 0u)
        fr.ci = (uint8_t)clamp((int32_t)fr.ci + s, 0, (int32_t)fr.nc - 1);
    else if (k == 1u)
        fr.bi = (uint8_t)clamp((int32_t)fr.bi + s, 0, 3);
    else if (k == 2u)
        fr.start = (uint8_t)clamp((int32_t)fr.start + s, 0, (int32_t)fr.nst - 1);
    else
        fr.set = (uint8_t)(s > 0 && !song.g[G_CLOCK]);
    fr_render();
    ui.hot_col = (uint8_t)k;                            /* (its card lit, as a page's) */
    ui.hot_t = 40;
}
/* OCT+ (keep: the next free take starts after this one) or OCT- (the track and the tempo back) */
static void fr_close(int keep)
{
    track_t *t = &trk[fr.trk % NTRK];
    if (!fr.on)
        return;
    fr.on = 0;
    free_hold = 0;
    ui.take_t = 0;
    if (!keep && trk_pat(fr.trk) == fr.pat) {          /* (another pattern on it meanwhile: left as it is) */
        fm1_irq_off();
        memcpy(t->step, fr_from, sizeof fr_from);
        t->p[P_SLEN] = fr.len0;
        t->p[P_SDIV] = fr.div0;
        fm1_irq_on();
        if (!song.g[G_CLOCK])
            song.g[G_BPM] = fr.bpm0;
        ui_message("FREE TAKE: BACK");
    } else {
        free_n0 = free_n;
        ui_message("FREE TAKE KEPT");
    }
    ui.force = 1;
}

/* SHIFT + SAVE (ui_input.c) on track k */
static void take_commit(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, want, i, e, b0, b1, j, chain = 0, tied = 0, mine;
    uint32_t notes = 0, moves = 0, full = 0, from;
    int own = rec_poly(t);                            /* ((2.0, #25) notes overlap: each held note its own tie) */
    step_t tmp[NSTEP];
    char b[24];
    if (fr.on) {                                      /* ((2.5) a free take's picker open: SHIFT + SAVE keeps it) */
        fr_close(1);
        return;
    }
    if (!song.playing && free_open(k))              /* ((2.5) stopped, notes played on it since: a free take) */
        return;
    if (len > NSTEP)
        len = NSTEP;
    if (rec_on(t)) {
        ui_message("DISARM REC FOR A TAKE");          /* (REC records it already; (Skiff U4) the way) */
        return;
    }
    if (take_pass[k % NTRK] < 2u) {
        ui_message("PLAY ONE LOOP FIRST");            /* not a whole pass played since PLAY (the first is pass 1) */
        return;
    }
    want = take_pass[k % NTRK] - 1u;                  /* the pass before the one playing (or stopped in) */
    if ((e = trk_ent(k)) < NENT && ent_block(k, e, &b0, &b1)) {   /* ((2.0, #44) on a chain: its last whole round, */
        if (!chain_pl[k % NTRK]) {                                 /* each part's pass into it (the others' first, */
            ui_message("PLAY ONE LOOP FIRST");                     /* each with an undo copy of its own) */
            return;
        }
        chain = 1;
        want = chain_pl[k % NTRK] + e - b0;
        for (j = b0; j <= b1; j++)
            if (j != e)
                take_part(k, j, chain_pl[k % NTRK] + j - b0, tmp, &notes, &moves, &full);
    }
    if (!chain && want <= take_p0[k % NTRK]) {        /* ((2.5) that pass was another pattern's: the one now came in */
        ui_message("PLAY ONE LOOP FIRST");            /* after it (launch.c launch_swap), so it isn't put in this one */
        return;
    }
    from = chain ? chain_pl[k % NTRK] : want;
    memcpy(tmp, t->step, sizeof tmp);
    if (own)                                          /* ((2.0) unfolded: a note played into a long one goes beside it) */
        ed_open(tmp, t, len);
    mine = take_notes(k, tmp, len, want, own, (int)chain, &tied);
    for (i = 0, j = 0; i < (take_kv_n < TAKE_KV ? take_kv_n : TAKE_KV); i++)
        j += take_kv[i].trk == k && take_kv[i].pass == want && take_kv[i].step < len;
    notes += mine;
    moves += j;
    if (!notes && !moves && !tied) {
        ui_message("NOTHING PLAYED");
        return;
    }
    if (mine || j || tied) {                          /* ((2.0, #44) a chain's other parts only: this one as it is) */
        undo.keep = 0;                                /* its own undo copy: SAVE held puts back steps and automation */
        undo_label = "TAKE";
        load_begin(t, UNDO_PAT);                      /* (which drops the track's automation: put back, as a take */
        if (motion_replace_track(t, &undo.motion_backup))   /* adds to what is there) */
            full = 1;
        if (own) {
            ed_close(t, tmp, len, 0);                 /* (folded: TIEs where they can be; at once, the IRQ off) */
            t->seq_active = 1;
        } else {
            fm1_irq_off();                            /* the audio ISR must not see half a pattern */
            memcpy(t->step, tmp, sizeof tmp);
            t->seq_active = 1;
            fm1_irq_on();
        }
        for (i = 0; i < (take_kv_n < TAKE_KV ? take_kv_n : TAKE_KV); i++) {
            const take_kv_t *m = &take_kv[i];
            if (m->trk == k && m->pass == want && m->step < len && motion_set_event(t, m->step, m->param, m->value))
                full = 1;
        }
        load_end(t);
    }
    if (take_box_fits()) {                            /* BLOCKS: the box, not the header */
        take_box.trk = (uint8_t)(k % NTRK);
        take_box.gen++;
        take_box.kind = 0;
        str_cpy(take_box.title, take_lost[k % NTRK] > from ? "TAKE PARTIAL" : "TAKE KEPT", sizeof take_box.title);
        take_box.line[0] = 0;
        if (notes)
            say_count(take_box.line, sizeof take_box.line, notes, " NOTE");
        if (full)                                     /* (some moves found no room: their count would mislead) */
            str_cpy(take_box.line + str_len(take_box.line), take_box.line[0] ? ", AUTOMATION FULL" : "AUTOMATION FULL",
                    sizeof take_box.line - str_len(take_box.line));
        else if (moves)
            say_count(take_box.line, sizeof take_box.line, moves, " KNOB MOVE");
        ui.msg_t = 0;                                 /* (the SHIFT hint is over) */
        ui.msg2[0] = 0;
        ui.take_t = TB_FRAMES;
        return;
    }
    str_cpy(b, "TAKE T", sizeof b);
    b[6] = (char)('1' + k % NTRK);
    b[7] = 0;
    ui_say(b, take_lost[k % NTRK] > from ? " PARTIAL" : full ? " AUTO FULL" : "");
    str_cpy(ui.msg2, "HOLD [SAVE]: UNDO", sizeof ui.msg2);
}
