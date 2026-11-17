/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff, #92) per-note trig conditions (core.h NC_*, seq.c ncond.c): the sequencer, FEEL's KNOB 1 and the edits that
 * move notes, on the real sources with tests/ui_test.c's stubs:
 *   PLAY     a chord's E4 on 1:2, C4 on 2:2: one pattern alternating, C4 alone every other pass, E4 the others
 *   BOTH     the step's own condition and the note's: the note only when both pass; every note failing, the step plays
 *            nothing; FILL; a RATCH x2 step leaves the note out of every part; a preview plays every note
 *   LANES    a DRUM step: the snare's hit on 1:2 beside the kick
 *   FEEL     notes picked: KNOB 1 the picked notes' own condition (--, FILL .. 4:4), the step's CHANCE untouched;
 *            back to -- takes it out; one undo; (drum track) SHIFT + KNOB 1 the LANE card's lane's hits; no hit: nothing;
 *            a key on FEEL picks its note
 *   EDITS    PITCH keeps a note's condition on it; START note by note and DELETE take it out; a paste brings none;
 *            REVERSE (whole steps) takes it out; DOUBLE's copy has none; HALF keeps it inside the new LEN; a hit cleared
 *            takes its own
 *   FULL     NC_MAX a pattern: the next refused (NOTE CONDS FULL)
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define C4 60u
#define E4 64u
#define F4 65u
#define G4 67u

static void nc_power_on(void)
{
    ui_power_on();
    memset(ncs, 0, sizeof ncs);                        /* (.pool at power-on) */
    memset(undo_ncs, 0, sizeof undo_ncs);
    memset(nc_skip, 0, sizeof nc_skip);
    nc_repitch = 0;
}
/* track 1: LEN 2, POLY, C4 and E4 on step 1, nothing on step 2 */
static track_t *chord(void)
{
    track_t *t;
    nc_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 2;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0;
    t->step[0] = (step_t){{C4, E4}, 2, ST_NOTE, 0, 100};
    return t;
}
/* track 4 (DRUM): LEN 2, the kick and the snare on step 1 */
static track_t *kit(void)
{
    track_t *t;
    nc_power_on();
    t = &trk[3];
    track_defaults_steps(t);
    t->p[P_SLEN] = 2;
    t->p[P_SSWING] = 0;
    song.g[G_SWING] = 0;
    t->step[0] = (step_t){{0}, 0, ST_NOTE, 0, 100, 3};
    return t;
}
/* n passes of t's loop from PLAY: on[i] the note-ons of pitch p on pass i + 1 */
static void play(track_t *t, uint32_t p, uint32_t n, uint32_t *on)
{
    uint32_t l = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 0) * (uint32_t)t->p[P_SLEN], pos = 0, v0, i;
    memset(on, 0, n * sizeof *on);
    seq_start();
    while (pos + CTL <= n * l) {
        v0 = vage;
        seq_tick(t, CTL);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && t->v[i].age > v0 && t->v[i].note == p)
                on[pos / l]++;
        pos += CTL;
    }
    seq_stop();
}
/* the passes of 4 pitch p starts on (bit i: pass i + 1) */
static uint32_t passes(track_t *t, uint32_t p)
{
    uint32_t on[4], i, m = 0;
    play(t, p, 4, on);
    for (i = 0; i < 4u; i++)
        m |= (on[i] != 0u) << i;
    return m;
}

static int sequencer(void)
{
    int bad = 0, ok;
    uint32_t on[4], c, e;
    track_t *t;

    t = chord();
    ok = nc_set(t, 0, 1, TC_1_2) == 0 && nc_set(t, 0, 0, TC_2_2) == 0;
    c = passes(t, C4);
    e = passes(t, E4);
    bad += check("PLAY: E4 on 1:2, C4 on 2:2 in one chord: one pattern alternating, E4 passes 1 3, C4 passes 2 4",
                 ok && c == 0xAu && e == 0x5u);
    if (c != 0xAu || e != 0x5u)
        printf("ncond:  C4 passes %x, E4 passes %x\n", c, e);
    (void)nc_set(t, 0, 0, TC_NONE);
    bad += check("  C4 back to --: every pass, E4 still 1 3", passes(t, C4) == 0xFu && passes(t, E4) == 0x5u);

    t = chord();
    t->step[0].cond = TC_1_2;
    (void)nc_set(t, 0, 1, TC_2_2);
    bad += check("BOTH: the step on 1:2 and E4 on 2:2: E4 never, C4 on the step's passes", passes(t, E4) == 0u &&
                 passes(t, C4) == 0x5u);
    t = chord();
    (void)nc_set(t, 0, 0, TC_1_2);
    (void)nc_set(t, 0, 1, TC_1_2);
    bad += check("  every note of the step failing: nothing of it on passes 2 4", passes(t, C4) == 0x5u &&
                 passes(t, E4) == 0x5u);
    t = chord();
    (void)nc_set(t, 0, 1, TC_FILL);
    c = passes(t, E4);
    seq_fill = 1;
    e = passes(t, E4);
    seq_fill = 0;
    bad += check("  E4 on FILL: only while FILL is held; C4 throughout", !c && e == 0xFu && passes(t, C4) == 0xFu);
    t = chord();
    step_set_ratchet(&t->step[0], 2);
    (void)nc_set(t, 0, 1, TC_1_2);
    play(t, E4, 4, on);
    ok = on[0] == 2u && on[1] == 0u && on[2] == 2u && on[3] == 0u;
    play(t, C4, 4, on);
    bad += check("  RATCH x2: E4 in both parts of passes 1 3, in neither of 2 4; C4 twice each pass",
                 ok && on[0] == 2u && on[1] == 2u);
    t = chord();
    (void)nc_set(t, 0, 1, TC_2_2);
    nc_skip[0] = 2u;                                   /* (as a step played last left it) */
    aud_req = 1u;                                      /* (STEP PREVIEW: track 1, step 1) */
    seq_audition();
    bad += check("  a preview (STEP PREVIEW) plays every note of the step", nc_skip[0] == 0u);

    t = kit();
    (void)nc_set(t, 0, NC_LANE + 1u, TC_1_2);
    c = passes(t, DRUM_LANE_NOTE[0]);
    e = passes(t, DRUM_LANE_NOTE[1]);
    bad += check("LANES: a DRUM step's snare on 1:2 beside the kick: the kick every pass, the snare passes 1 3",
                 c == 0xFu && e == 0x5u);
    return bad;
}

/* FEEL's KNOB 1 turned d detents on track t's selection */
static void k1(track_t *t, int32_t d) { feel_turn(t, 0, d); }
static int feel(void)
{
    int bad = 0, ok;
    track_t *t = chord();
    sel_set(0);
    ui.home = 0;
    ui.cursor = 0;
    nsel.vp[0] = 2u;                                   /* E4 picked */
    nsel.picked = 1;
    ok = feel_nc_mode(t) == 1u;
    k1(t, 1);
    ok &= nc_get(t, 0, 1) == TC_FILL;
    k1(t, 1);
    k1(t, 1);
    ok &= nc_get(t, 0, 1) == TC_1_2 && nc_get(t, 0, 0) == TC_NONE && step_chance(&t->step[0]) == 100u &&
          !t->step[0].cond && ncs[0].n == 1u;
    bad += check("FEEL: E4 picked, KNOB 1 its own condition (a detent each: FILL !FILL 1:2), the step's CHANCE and "
                 "COND untouched", ok);
    undo_step(0);
    ok = ncs[0].n == 0u;
    undo_step(1);
    bad += check("  one undo: SAVE held takes it out, OCT+ in the window puts it back", ok && nc_get(t, 0, 1) == TC_1_2);
    k1(t, -1);
    k1(t, -1);
    k1(t, -1);
    k1(t, -1);
    bad += check("  turned back past FILL to --: the note's condition gone (the store empty)", ncs[0].n == 0u);
    nsel.picked = 0;
    nsel.vp[0] = 0;
    k1(t, -5);
    bad += check("  nothing picked: KNOB 1 the step's CHANCE as before", step_chance(&t->step[0]) < 100u && !ncs[0].n);
    t->step[0] = (step_t){{C4, E4}, 2, ST_NOTE, 0, 100};
    {
        uint32_t k;                                    /* a key on FEEL (REC off) picks its note */
        for (k = 0; k < 27u && kb_map(t, k) != E4; k++)
            ;
        ok = k < 27u && sel_keys(1u << k) && nsel.picked && nsel.vp[0] == 2u;
    }
    bad += check("  FEEL, not armed: E4's key picks E4 (sel_keys)", ok);

    t = kit();
    sel_set(3);
    ui.home = 0;
    ui.cursor = 0;
    ui.lane = 1;                                       /* the LANE card on the snare */
    ui.home_t0 = 1;                                    /* SHIFT held */
    ok = feel_nc_mode(t) == 2u;
    k1(t, 1);
    k1(t, 1);
    k1(t, 1);
    ok &= nc_get(t, 0, NC_LANE + 1u) == TC_1_2 && nc_get(t, 0, NC_LANE) == TC_NONE && (ui.home_t0 & 2u) &&
          step_chance(&t->step[0]) == 100u;
    ui.lane = 2;                                       /* the clap: no hit there */
    ui.msg[0] = 0;
    k1(t, 1);
    ok &= !ui.msg[0] && ncs[3].n == 1u;
    ui.home_t0 = 0;
    ok &= feel_nc_mode(t) == 0u;
    bad += check("  a DRUM track: SHIFT + KNOB 1 the LANE card's lane's hits (SNARE 1:2), a SHIFT combo; no hit there: "
                 "nothing; SHIFT let go: CHANCE", ok);
    {
        uint8_t e = t->eng_req;
        t->eng_req = ENGI_DRUMX;                       /* DRUM-X: its sheet's own lane on the kick, the grid's on the snare */
        dx_ui_lane = 0;
        ui.lane = 1;
        ui.home_t0 = 1;
        k1(t, 1);
        ok = nc_get(t, 0, NC_LANE + 1u) == TC_2_2 && nc_get(t, 0, NC_LANE) == TC_NONE;
        ui.home_t0 = 0;
        t->eng_req = e;
        bad += check("  a DRUM-X track: the grid's lane (SNARE 2:2), not DRUM-X's sheet's", ok);
    }
    return bad;
}

static int edits(void)
{
    int bad = 0, ok;
    track_t *t = chord();
    sel_set(0);
    ui.home = 0;
    ui.cursor = 0;
    t->p[P_SLEN] = 4;
    t->p[P_SCALE] = 0;                                 /* (CHROMATIC: PITCH a semitone) */
    (void)nc_set(t, 0, 1, TC_1_2);
    nsel.vp[0] = 2u;
    nsel.picked = 1;
    sel_pitch(t, 1);
    ok = t->step[0].note[note_at(&t->step[0], F4)] == F4 && nc_get(t, 0, (uint32_t)note_at(&t->step[0], F4)) == TC_1_2 &&
         ncs[0].n == 1u;
    bad += check("EDITS: PITCH: E4 up to F4, its condition with it", ok);
    sel_start(t, 1);
    ok = note_at(&t->step[1], F4) >= 0 && ncs[0].n == 0u && note_at(&t->step[0], C4) >= 0;
    bad += check("  START: F4 alone a step on, its condition left behind and gone; C4 stays", ok);
    (void)nc_set(t, 1, (uint32_t)note_at(&t->step[1], F4), TC_1_2);
    sel_delete(t);
    bad += check("  DELETE: F4 gone, its condition too", note_at(&t->step[1], F4) < 0 && ncs[0].n == 0u);

    t = chord();                                       /* C4 E4 G4: E4 on 1:2, G4 on 2:2 */
    sel_set(0);
    ui.home = 0;
    ui.cursor = 0;
    t->step[0].note[2] = G4;
    t->step[0].n = 3;
    (void)nc_set(t, 0, 1, TC_1_2);
    (void)nc_set(t, 0, 2, TC_2_2);
    nsel.vp[0] = 1u;
    nsel.picked = 1;
    sel_delete(t);
    ok = note_at(&t->step[0], C4) < 0 && ncs[0].n == 2u &&
         nc_get(t, 0, (uint32_t)note_at(&t->step[0], E4)) == TC_1_2 && nc_get(t, 0, (uint32_t)note_at(&t->step[0], G4)) == TC_2_2;
    bad += check("  DELETE C4 under E4 (1:2) and G4 (2:2): theirs stay on them", ok);
    t->step[1] = (step_t){{0}, 0, ST_TIE, 0, 100};     /* a condition left on a TIE (no note starts there) */
    ncs[0].e[ncs[0].n].at = 1;
    ncs[0].e[ncs[0].n++].tg = NC_TG(0, TC_FILL);
    ok = !nc_fails(t, &t->step[1], 1, 0);
    bad += check("  a condition left on a TIE: the note going on through it isn't cut", ok);
    t->step[1] = (step_t){{0}, 0, ST_REST, 0, 100};
    ncs[0].n = 0;
    (void)nc_set(t, 0, 0, TC_1_2);
    (void)nc_set(t, 0, 1, TC_2_2);
    ui.entry_open = 0;
    ui.cursor = 0;
    song.sel = 0;
    seq_entry(1u);
    ok = ncs[0].n == 0u;
    ui.entry_open = 0;
    bad += check("  a chord entered anew on the step: none of the old one's conditions", ok);

    t = chord();
    sel_set(0);
    ui.home = 0;
    t->p[P_SLEN] = 4;
    (void)nc_set(t, 0, 1, TC_1_2);
    ui.cursor = 0;
    sel_copy(t);
    ui.cursor = 2;
    sel_paste(t);
    ok = note_at(&t->step[2], E4) >= 0 && nc_get(t, 2, (uint32_t)note_at(&t->step[2], E4)) == TC_NONE &&
         nc_get(t, 0, 1) == TC_1_2 && ncs[0].n == 1u;
    bad += check("  COPY / PASTE: the chord onto step 3 without E4's condition; the copied one keeps its own", ok);

    t = chord();
    sel_set(0);
    t->p[P_SLEN] = 4;
    (void)nc_set(t, 0, 1, TC_1_2);
    ok = tl_do(t, TL_REV, 0) != 0 && note_at(&t->step[3], E4) >= 0 && ncs[0].n == 0u;
    bad += check("  REVERSE: the chord to step 4, E4's condition gone with the move", ok);
    (void)nc_set(t, 3, (uint32_t)note_at(&t->step[3], E4), TC_1_2);
    ok = tl_do(t, TL_DOUBLE, 0) != 0 && t->p[P_SLEN] == 8 && ncs[0].n == 1u &&
         nc_get(t, 7, (uint32_t)note_at(&t->step[7], E4)) == TC_NONE;
    bad += check("  DOUBLE: steps 1..4 once more, the copy without the condition", ok);
    ok = tl_do(t, TL_HALF, 0) != 0 && t->p[P_SLEN] == 4 && ncs[0].n == 1u;
    bad += check("  HALF: the one inside the new LEN kept", ok);

    t = kit();
    sel_set(3);
    (void)nc_set(t, 0, NC_LANE + 1u, TC_1_2);
    grid_hit(t, 0, 1, 0);
    run_done(t);
    bad += check("  a DRUM hit cleared: its condition with it", ncs[3].n == 0u);
    return bad;
}

static int full(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k;
    track_t *t = chord();
    sel_set(0);
    ui.home = 0;
    t->p[P_SLEN] = 16;
    for (i = 0; i < 9u; i++)
        t->step[i] = (step_t){{C4, E4, 67, 71}, 4, ST_NOTE, 0, 100};
    for (i = 0; i < NC_MAX; i++)
        ok &= nc_set(t, i / 4u, i % 4u, TC_1_2) == 0;
    ok &= ncs[0].n == NC_MAX && nc_set(t, 8, 0, TC_1_2) == 1 && nc_set(t, 0, 0, TC_2_2) == 0;
    bad += check("FULL: 32 conditions a pattern; the 33rd refused, one there changed", ok);
    ui.cursor = 8;
    nsel.vp[8] = 1u;
    nsel.picked = 1;
    ui.msg[0] = 0;
    k1(t, 1);
    bad += check("  KNOB 1 on a 33rd note: NOTE CONDS FULL", !strcmp(ui.msg, "NOTE CONDS FULL") && ncs[0].n == NC_MAX);
    return bad;
}

int main(void)
{
    int bad = sequencer() + feel() + edits() + full();
    printf("%s\n", bad ? "NCOND TEST FAILED" : "ncond: all passed");
    return bad != 0;
}
