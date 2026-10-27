/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff 2.0, #25) per-note ties (design/TIER_B.md, piece 1 step 2): the sequencer (seq.c seq_step, seq_gate_end) and
 * the record (project.c step_put9 / step_get9, bank.c bk_pack_raw), on the real sources with tests/ui_test.c's stubs:
 *   OVERLAP  C4 held four steps by per-note ties (steps 2..4 list it tied), E4 starting on step 3: C4 one note-on,
 *            never retriggered, sounding to the end of step 4 as a TIE chain's note does; E4 one note-on, ended at
 *            its GATE
 *   MISS     E4's step not playing (its chance): C4 goes on
 *   SILENT   a tie never starts a note: C4's start not playing, its tied steps stay silent; E4 still plays
 *   MONO     E4 takes over, C4 back when E4 ends (as a key played over a held one)
 *   RATCH    E4 x2 beside the tied C4: E4 twice, C4 once and on throughout
 *   STOP     mid-hold: everything ends
 *   RECORD   ties round trip in the 9-byte step (byte 7 of a step without hits); ties of notes a step doesn't have are
 *            refused; a record with ties is version 3 (1.4 reads 1 and 2 only), one without stays version 1, byte 7
 *            of every hitless step 0
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define C4 60u
#define E4 64u

/* track 1: LEN 8, GATE 64, no swing, voice mode vm; C4 on step 1 going on over steps 2..4 (each lists it tied: acc bit
 * 0), E4 starting on step 3 beside it; the transport started */
static track_t *overlap(uint32_t vm)
{
    track_t *t;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 8;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = (int16_t)vm;
    song.g[G_SWING] = 0;
    t->step[0] = (step_t){{C4}, 1, ST_NOTE, 0, 100};
    t->step[1] = (step_t){{C4}, 1, ST_NOTE, 0, 100, 0, 1};
    t->step[2] = (step_t){{C4, E4}, 2, ST_NOTE, 0, 100, 0, 1};
    t->step[3] = (step_t){{C4}, 1, ST_NOTE, 0, 100, 0, 1};
    return t;
}
static uint32_t slen(const track_t *t) { return step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 0); }
/* the note a gated voice of t plays: 1 if one plays note */
static int gated(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
/* the sequencer from where it is (*pos samples into the run) to at; on[0] / on[1]: C4's / E4's note-ons on the way */
static void run_to(track_t *t, uint32_t *pos, uint32_t at, uint32_t *on)
{
    uint32_t v0, i;
    while (*pos + CTL <= at) {
        v0 = vage;
        seq_tick(t, CTL);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && t->v[i].age > v0)
                on[t->v[i].note == E4] += t->v[i].note == C4 || t->v[i].note == E4;
        *pos += CTL;
    }
}

static int sequencer(void)
{
    int bad = 0, ok;
    uint32_t on[2], pos, l, c_end_tie, c_end;
    track_t *t;

    t = overlap(V_POLY);
    seq_start();
    l = slen(t);
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 4u, on);
    ok = gated(t, C4) && gated(t, E4);
    run_to(t, &pos, 2u * l + 3u * l / 4u, on);
    ok &= gated(t, C4) && !gated(t, E4);              /* (E4 ended at its GATE, C4 on) */
    run_to(t, &pos, 3u * l + 3u * l / 4u, on);
    ok &= gated(t, C4);
    for (c_end = pos; gated(t, C4) && pos < 6u * l; c_end = pos)
        run_to(t, &pos, pos + CTL, on);
    run_to(t, &pos, 8u * l - CTL, on);
    ok &= on[0] == 1u && on[1] == 1u;
    bad += check("OVERLAP: C4 held over steps 2..4 by per-note ties, E4 starting on step 3: one note-on each, C4 on "
                 "through E4, E4 ended at its GATE", ok);
    if (on[0] != 1u || on[1] != 1u)
        printf("ties:   C4 %u, E4 %u note-ons\n", on[0], on[1]);

    t = overlap(V_POLY);                               /* the same note as a TIE chain: it ends where C4 does */
    t->step[1] = t->step[3] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[2] = (step_t){{0}, 0, ST_TIE, 0, 0};
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 3u * l + 3u * l / 4u, on);
    for (c_end_tie = pos; gated(t, C4) && pos < 6u * l; c_end_tie = pos)
        run_to(t, &pos, pos + CTL, on);
    bad += check("  C4 ends where the same note as a TIE chain does (half a step after the last step's GATE)",
                 c_end == c_end_tie && c_end >= 4u * l - CTL && c_end <= 4u * l + CTL);

    t = overlap(V_POLY);
    t->step[2].probability = 101;                      /* (E4's step: never plays) */
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 4u, on);
    ok = gated(t, C4) && !gated(t, E4);
    run_to(t, &pos, 3u * l + 3u * l / 4u, on);
    ok &= gated(t, C4);
    run_to(t, &pos, 8u * l - CTL, on);
    ok &= on[0] == 1u && on[1] == 0u;
    bad += check("MISS: E4's step not playing (its chance): C4 goes on, not retriggered", ok);

    t = overlap(V_POLY);
    t->step[0].probability = 101;                      /* (C4's start: never plays) */
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 4u, on);
    ok = !gated(t, C4) && gated(t, E4);
    run_to(t, &pos, 8u * l - CTL, on);
    ok &= on[0] == 0u && on[1] == 1u;
    bad += check("SILENT: a tie never starts a note (C4's start not playing: its steps silent); E4 still plays", ok);

    t = overlap(V_MONO);
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 4u, on);
    ok = gated(t, E4) && !gated(t, C4);
    run_to(t, &pos, 2u * l + 3u * l / 4u, on);
    ok &= gated(t, C4) && !gated(t, E4);
    run_to(t, &pos, 8u * l - CTL, on);
    ok &= on[0] == 1u && on[1] == 1u;
    bad += check("MONO: E4 takes over, C4 back when E4 ends (no new note-on for it)", ok);

    t = overlap(V_POLY);
    step_set_ratchet(&t->step[2], 2);
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 2u - CTL, on);
    ok = gated(t, C4);
    run_to(t, &pos, 2u * l + l / 2u + l / 8u, on);
    ok &= gated(t, C4) && gated(t, E4);
    run_to(t, &pos, 8u * l - CTL, on);
    ok &= on[0] == 1u && on[1] == 2u;
    bad += check("RATCH: E4 x2 beside the tied C4: E4 twice, C4 once and on throughout", ok);

    t = overlap(V_POLY);
    seq_start();
    on[0] = on[1] = 0;
    pos = 0;
    run_to(t, &pos, 2u * l + l / 4u, on);
    seq_stop();
    bad += check("STOP mid-hold: everything ends", !gated(t, C4) && !gated(t, E4) && !t->seq_n);
    return bad;
}

static int record(void)
{
    static const int16_t TV[5] = {8, 2, 0, 64, 0};
    static motion_store_t ms;
    static step_t st[NSTEP], back;
    static uint8_t r[BK_REC];
    uint8_t b[9];
    int bad = 0, ok;
    uint32_t i;
    step_t s = {{C4, E4}, 2, ST_NOTE, 0, 100, 0, 1};    /* C4 tied, E4 new */
    ok = step_put9(b, &s) && b[7] == 1u && step_get9(&back, b) && back.acc == 1u && step_ties(&back) == 1u &&
         back.n == 2u && back.note[0] == C4 && back.note[1] == E4;
    b[7] = 4u;                                         /* (a tie of note 3: the step has two) */
    ok &= !step_get9(&back, b);
    s = (step_t){{C4}, 1, ST_NOTE, 0, 100, 3u, 2u};    /* (hits: acc their accents, no ties) */
    ok &= step_put9(b, &s) && b[7] == 2u && step_get9(&back, b) && back.acc == 2u && !step_ties(&back);
    bad += check("RECORD: a step's ties in byte 7 (no hits), back as they were; a tie of a note it doesn't have "
                 "refused; with hits byte 7 is their accents", ok);

    memset(st, 0, sizeof st);
    st[0] = (step_t){{C4}, 1, ST_NOTE, 0, 100};
    st[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
    st[2] = (step_t){{C4, E4}, 2, ST_NOTE, 0, 100};
    ok = bk_pack_raw(r, 0, st, TV, &ms, 0) && r[0] == BK_VER;
    for (i = 0; i < NSTEP; i++)
        ok &= r[BK_STEPS + i * 9u + 7u] == 0u;
    st[2].acc = 1;                                     /* (C4 tied on step 3) */
    ok &= bk_pack_raw(r, 0, st, TV, &ms, 0) && r[0] == BK_VER3 && bk_ver(r[0]) && !(r[0] == 1u || r[0] == 2u);
    ok &= step_get9(&back, r + BK_STEPS + 2u * 9u) && step_ties(&back) == 1u;
    bad += check("  a record without ties stays version 1 (byte 7 of every hitless step 0); with one it is version 3: "
                 "2.0 reads it back, 1.4 (versions 1 and 2) shows it empty", ok);
    return bad;
}

/* ---- (2.0, #44) notes across a chain's parts (bank.c ent_block: A on CHAIN into B, LEN 4 each, track 1, POLY):
 *   CARRY   A's last step starts C4, B's first two tie it: the switch (launch.c launch_swap, LOOP, going forward) keeps
 *           it, one note-on, on into B as across any step, ended where a tie chain's note ends
 *   LENGTH  NOTES' LENGTH of A's last note past A's end writes its tail into B's first steps (bank.c chain_tails, B
 *           kept waiting); the LENGTH card counts it all; PITCH takes it along; deleting the note takes it out
 *   REC     a key held recording over the switch: B's first step ties it (seq.c rec_hold), A keeps its start
 *   TAKE    a note played over the switch, a whole round of the chain later SHIFT + SAVE: into A, on into B (ui_take.c
 *           take_commit, bank.c take_part), an undo copy each ---- */
static track_t *chain_len(uint32_t len, uint32_t at)   /* A, B len steps each: C4 on A's step at, B nothing */
{
    track_t *t;
    ui_power_on();
    lists_clear();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0;
    trk_pid[0] = 0;
    t->step[at] = (step_t){{C4}, 1, ST_NOTE, 0, 100};
    bank_put(0, 0, 0);                                /* A */
    track_defaults_steps(t);
    t->p[P_SLEN] = (int16_t)len;
    trk_pid[0] = 0;
    bank_put(0, 1, 0);                                /* B */
    lists.ent[0][0][1] = REPS_CHAIN;
    (void)ent_go(0, 0);
    cursor_set(at);
    undo_clear();
    song_poll();                                      /* (stopped: PLAY then starts the chain's passes anew) */
    return t;
}
static track_t *chain2(void) { return chain_len(4u, 3u); }   /* A: C4 on its last step; B: nothing */
static step_t kept_step(uint32_t e, uint32_t i)     /* step i of T1's entry e's pattern as kept (n 255: none) */
{
    step_t s;
    memset(&s, 0, sizeof s);
    if (pat_read(ent_pat(0, e), bk_rec) || !step_get9(&s, bk_rec + BK_STEPS + i * 9u))
        s.n = 255u;
    return s;
}
static int kept_tied(uint32_t e, uint32_t i, uint32_t note)   /* .. lists note tied */
{
    step_t s = kept_step(e, i);
    int32_t x = note_at(&s, note);
    return s.n != 255u && x >= 0 && step_tied(&s, (uint32_t)x);
}
/* the sequencer and the main loop's switching (a frame every block) from *pos to at; on[0]: C4's note-ons */
static void run_chain(track_t *t, uint32_t *pos, uint32_t at, uint32_t *on)
{
    uint32_t v0, i;
    while (*pos + CTL <= at) {
        bank_poll();
        song_poll();
        v0 = vage;
        seq_tick(t, CTL);
        for (i = 0; i < NVOICE; i++)
            on[0] += t->v[i].active && t->v[i].age > v0 && t->v[i].note == C4;
        *pos += CTL;
    }
}
static int chain_notes(void)
{
    int bad = 0, ok;
    uint32_t on[2] = {0, 0}, pos = 0, l, n;
    track_t *t = chain2();
    l = slen(t);
    ok = len_set(t, 3, 0, LEN_STEPS + 1) == 0 && kept_tied(1, 0, C4) && kept_tied(1, 1, C4) && !kept_tied(1, 2, C4) &&
         len_get(t, 3) == LEN_STEPS + 1;
    bad += check("(2.0, #44) LENGTH 3 STEPS on A's last step: its tail ties B's first two (kept); the card counts 3", ok);
    seq_start();
    run_chain(t, &pos, 3u * l + l / 4u, on);
    ok = gated(t, C4) && on[0] == 1u;
    run_chain(t, &pos, 5u * l + l / 4u, on);
    ok &= gated(t, C4) && on[0] == 1u && lists.cur[0] == 1u;
    run_chain(t, &pos, 6u * l + l / 4u, on);
    ok &= !gated(t, C4);
    seq_stop();
    bank_poll();
    song_poll();
    bad += check("CARRY: at the switch C4 goes on into B, one note-on, ended after B's second step", ok);
    t = chain_len(64u, 0u);                            /* ((2.0.2, #58) the knob to its end, 128 STEPS: from 2 STEPS */
    (void)len_set(t, 0, 0, LEN_STEPS);
    sel_length(t, 300);
    ok = len_get(t, 0) == LEN_STEPS + 126 && note_span(t, 0, 0) == 64u && kept_tied(1, 0, C4) && kept_tied(1, 63, C4);
    sel_length(t, 1);
    ok &= len_get(t, 0) == LEN_STEPS + 126;
    bad += check("(2.0.2, #58) LENGTH to its end from A's first step (A, B 64 each): 128 STEPS, all of A and B; no further",
                 ok);
    t = chain2();
    (void)len_set(t, 3, 0, LEN_STEPS + 1);
    sel_pitch(t, 1);
    n = t->step[3].note[0];
    ok = n != C4 && kept_tied(1, 0, n) && kept_tied(1, 1, n) && !kept_tied(1, 0, C4);
    sel_delete(t);
    ok &= !t->step[3].n && !kept_tied(1, 0, n) && !kept_tied(1, 1, n);
    bad += check("  PITCH takes the tail along; deleting the note takes it out", ok);
    t = chain2();
    t->step[3] = (step_t){{0}, 0, ST_REST, 0, 0};    /* (A empty: the key makes the note) */
    pos = 0;
    song.rec = 1;
    seq_start();
    run_chain(t, &pos, 2u * l + l / 4u, on);
    rec_note(t, C4, 100);                            /* (held from A's step 3 ..) */
    run_chain(t, &pos, 5u * l + l / 4u, on);
    rec_release(t, C4);                              /* (.. let go early in B's second: not held into it) */
    song.rec = 0;
    run_chain(t, &pos, 6u * l, on);
    seq_stop();
    bank_poll();
    ok = lists.cur[0] == 1u && t->step[0].time == ST_TIE && t->step[1].time != ST_TIE;
    ok &= (kept_step(0, 2).n == 1u && kept_step(0, 2).note[0] == C4 && kept_step(0, 3).time == ST_TIE);
    bad += check("REC: a key held over the switch: A keeps C4 and its TIE, B's first step a TIE (its second not)", ok);
    t = chain2();
    t->step[3] = (step_t){{0}, 0, ST_REST, 0, 0};
    pos = 0;
    seq_start();
    run_chain(t, &pos, 2u * l + l / 4u, on);
    take_note(t, C4, 100);                           /* (round 1: played on A's step 3, let go in B's second) */
    run_chain(t, &pos, 5u * l + l / 4u, on);
    take_note(t, C4, 0);
    run_chain(t, &pos, 9u * l, on);                  /* (round 2 under way: round 1 whole) */
    n = undo_n;
    take_commit(0);
    ok = lists.cur[0] == 0u && t->step[2].n == 1u && t->step[2].note[0] == C4 &&
         (t->step[3].time == ST_TIE || step_tied(&t->step[3], 0)) && kept_tied(1, 0, C4) && !kept_tied(1, 1, C4) &&
         undo_n == n + 2u;
    seq_stop();
    bad += check("TAKE: a whole round of the chain: C4 into A (its step 3 on to its end), on into B's first step; an "
                 "undo copy each", ok);
    lists_clear();
    return bad;
}

int main(void)
{
    int bad = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    bad += sequencer();
    bad += record();
    bad += chain_notes();
    if (bad)
        printf("ties: %d FAILED\n", bad);
    else
        printf("ties: all passed\n");
    return bad != 0;
}
