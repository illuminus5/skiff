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

/* SHIFT + SAVE (ui_input.c) on track k */
static void take_commit(uint32_t k)
{
    track_t *t = &trk[k % NTRK];
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, want, i, e, b0, b1, j, chain = 0, tied = 0, mine;
    uint32_t notes = 0, moves = 0, full = 0, from;
    int own = rec_poly(t);                            /* ((2.0, #25) notes overlap: each held note its own tie) */
    step_t tmp[NSTEP];
    char b[24];
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
