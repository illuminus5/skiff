/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (2.5) FREE TAKES (src/take.c free_ev, src/ui_take.c free_open ..): notes played with the transport stopped, SHIFT +
 * SAVE: the tempo, length and first beat guessed, the phrase put into the track, the picker over it. Against the real
 * keys' path (input_on / input_off), the guess, the placement and the picker's knobs and keys:
 * 1. the guess: a 97 BPM riff with 10 ms of jitter, a 128 BPM drum beat, chords at 90, a pickup at 110: the right tempo
 *    first (within 1 BPM; its half and double after it),
 *    the right BARS, every note on its step (the pickup's at the loop's end
 *    with the first beat on the next note);
 * 2. the picker: the take in place of the track's pattern (every step, past LEN too), TEMPO SET sets the song's BPM,
 *    KEEP leaves it (KEEP first when another track has notes), BARS sets LEN (8: DIV 1/8), OCT- puts the steps, LEN,
 *    DIV and tempo back, OCT+ keeps them (one undo: SAVE held), SHIFT + SAVE again keeps it; a stop meanwhile moves
 *    nothing; nothing played since the stop: the take of before; KNOB 1 there and back: the same steps (each guess from
 *    the pattern as it opened, not the last guess's);
 * 3. chords strummed by hand (a note every 20 ms): one onset each, whole on one step at every guess.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

typedef struct { uint32_t t; uint8_t note, vel; } fev_t;
static fev_t fe[512];
static uint32_t fe_n;
static uint32_t jit = 12345u;
static int32_t jitter(int32_t ms)                    /* +- ms, deterministic */
{
    jit = jit * 1664525u + 1013904223u;
    return (int32_t)(jit >> 16) % (2 * ms + 1) - ms;
}
static void fe_add(uint32_t on, uint32_t dur, uint32_t note, uint32_t vel)
{
    fe[fe_n++] = (fev_t){on, (uint8_t)note, (uint8_t)vel};
    fe[fe_n++] = (fev_t){on + dur, (uint8_t)note, 0};
}
static void fe_play(track_t *t, uint32_t base)        /* the events in time order, through the keys' path */
{
    uint32_t i, j;
    for (i = 1; i < fe_n; i++)
        for (j = i; j > 0 && (fe[j - 1u].t > fe[j].t || (fe[j - 1u].t == fe[j].t && !fe[j - 1u].vel && fe[j].vel)); j--) {
            fev_t x = fe[j];
            fe[j] = fe[j - 1u];
            fe[j - 1u] = x;
        }
    for (i = 0; i < fe_n; i++) {
        fm1_ms = base + fe[i].t;
        if (fe[i].vel)
            input_on(t, fe[i].note, fe[i].vel);
        else
            input_off(t, fe[i].note);
    }
    fe_n = 0;
}
static track_t *ft_track(uint32_t k)                  /* track k: empty steps, LEN 16 at 1/16, selected, stopped */
{
    track_t *t;
    uint32_t i, j;
    ui_power_on();
    stop_transport();
    for (j = 0; j < NTRK; j++)
        for (i = 0; i < NSTEP; i++)
            trk[j].step[i] = (step_t){{0}, 0, ST_REST};
    t = &trk[k];
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->p[P_AMODE] = 0;
    sel_set(k);
    song.rec = 0;
    song.g[G_BPM] = 120;
    free_n0 = free_n;                                 /* (as after a stop) */
    fm1_ms = 100000u;
    return t;
}
static int has_note(const step_t *s, uint32_t note)
{
    for (uint32_t k = 0; k < s->n && k < 4u; k++)
        if (s->note[k] == note)
            return 1;
    return 0;
}
static int guess_has(uint32_t bpm)                     /* within 1 BPM, the first guess (1; the plan asked for the
                                                       * first two: these are) */
{
    uint32_t i;
    for (i = 0; i < fr.nc && i < 1u; i++)
        if (fr.bpm[i] + 1u >= bpm && fr.bpm[i] <= bpm + 1u)
            return (int)i + 1;
    return 0;
}
static void guesses_say(const char *what)
{
    uint32_t i;
    printf("freetake:   %s: guesses", what);
    for (i = 0; i < fr.nc; i++)
        printf(" %u", fr.bpm[i]);
    printf(" (bars %u, from note %u)\n", FR_BARS[fr.bi], fr.start + 1u);
}

static int riff97(void)
{
    static const uint8_t POS[7] = {0, 3, 6, 8, 10, 12, 14}, NT[7] = {48, 51, 55, 58, 55, 51, 53};
    int bad = 0, g, ok = 1;
    uint32_t bar, i;
    double six = 60000.0 / 97.0 / 4.0;                /* a 1/16 at 97 BPM: 154.6 ms */
    track_t *t = ft_track(0);
    for (bar = 0; bar < 2u; bar++)
        for (i = 0; i < 7u; i++)
            fe_add((uint32_t)((bar * 16u + POS[i]) * six + 20 + jitter(10)), (uint32_t)(six * 0.6), NT[i], 100);
    fe_play(t, fm1_ms);
    take_commit(0);
    guesses_say("97 BPM riff, 10 ms of jitter");
    g = guess_has(97);
    bad += check("free take opens its picker on SHIFT + SAVE stopped (its screen: FREE TAKE)",
                 fr.on && str_eq(head_title(""), "FREE TAKE") && !ui.take_t);
    bad += check("  .. a 97 BPM riff with 10 ms of jitter: 97 the first guess", g != 0);
    if (g == 2)
        fr_knob(0, 1);
    for (bar = 0; bar < 2u; bar++)
        for (i = 0; i < 7u; i++)
            ok &= t->step[bar * 16u + POS[i]].time == ST_NOTE && has_note(&t->step[bar * 16u + POS[i]], NT[i]);
    bad += check("  .. 2 BARS (LEN 32 at 1/16), every note on its step", FR_BARS[fr.bi] == 2u && t->p[P_SLEN] == 32 &&
                 t->p[P_SDIV] == 2 && ok);
    bad += check("  .. nothing else on another track: TEMPO SET (the song at the guess's BPM)",
                 fr.set && song.g[G_BPM] == fr.bpm[fr.ci]);
    fr_close(1);
    return bad;
}

static int drums128(void)
{
    int bad = 0, g, ok = 1;
    uint32_t bar, i;
    double six = 60000.0 / 128.0 / 4.0;
    track_t *t = ft_track(3);                         /* T4: the power-on DRUM KIT */
    for (bar = 0; bar < 2u; bar++)
        for (i = 0; i < 16u; i += 2u) {
            uint32_t at = (uint32_t)((bar * 16u + i) * six + 10 + jitter(8));
            if (i == 0u || i == 8u)
                fe_add(at, 30, 36, 110);
            if (i == 4u || i == 12u)
                fe_add(at + 2u, 30, 38, 100);
            fe_add(at + 1u, 20, 42, 80);
        }
    fe_play(t, fm1_ms);
    take_commit(3);
    guesses_say("128 BPM drum beat");
    g = guess_has(128);
    bad += check("a 128 BPM drum beat: 128 the first guess", fr.on && g != 0);
    if (g == 2)
        fr_knob(0, 1);
    for (bar = 0; bar < 2u; bar++)
        for (i = 0; i < 16u; i++) {
            const step_t *s = &t->step[bar * 16u + i];
            uint32_t want = (i % 2u == 0u ? 1u << drum_lane(42) : 0u) | (i == 0u || i == 8u ? 1u << drum_lane(36) : 0u) |
                            (i == 4u || i == 12u ? 1u << drum_lane(38) : 0u);
            ok &= (s->hit & want) == want && !(s->hit & ~want);
        }
    bad += check("  .. the hits in their lanes on their steps (kick, snare, the hats on the 1/8s)", ok);
    fr_close(1);
    return bad;
}

static int chords90(void)
{
    int bad = 0, g, ok = 1;
    uint32_t i;
    double q = 60000.0 / 90.0;
    track_t *t = ft_track(1);
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < 8u; i++) {                        /* C E G, spread over 16 ms, a quarter each, two bars */
        uint32_t at = (uint32_t)(i * q + 30 + jitter(6));
        fe_add(at, (uint32_t)(q * 0.8), 60, 90);
        fe_add(at + 8u, (uint32_t)(q * 0.8), 64, 90);
        fe_add(at + 16u, (uint32_t)(q * 0.8), 67, 90);
    }
    fe_play(t, fm1_ms);
    take_commit(1);
    guesses_say("chords at 90 BPM");
    g = guess_has(90);
    bad += check("chords on the beats at 90 BPM: 90 the first guess", fr.on && g != 0);
    if (g == 2)
        fr_knob(0, 1);
    for (i = 0; i < 8u; i++) {
        const step_t *s = &t->step[i * 4u];
        ok &= s->time == ST_NOTE && has_note(s, 60) && has_note(s, 64) && has_note(s, 67);
    }
    bad += check("  .. each chord whole on its beat's step (2 BARS)", ok && FR_BARS[fr.bi] == 2u);
    fr_close(1);
    return bad;
}

static int pickup110(void)
{
    static const uint8_t POS[6] = {0, 2, 4, 7, 10, 12}, NT[6] = {62, 64, 67, 69, 67, 64};
    int bad = 0, g;
    uint32_t i, rep, len;
    double six = 60000.0 / 110.0 / 4.0;
    track_t *t = ft_track(2);
    for (rep = 0; rep < 2u; rep++) {                  /* an 1/8 pickup, then the bar's riff, twice */
        fe_add((uint32_t)((rep * 16u + 14u) * six + 15 + jitter(6)), (uint32_t)(six * 0.5), 60, 100);
        for (i = 0; i < 6u; i++)
            fe_add((uint32_t)((rep * 16u + 16u + POS[i]) * six + 15 + jitter(6)), (uint32_t)(six * 0.5), NT[i], 100);
    }
    fe_play(t, fm1_ms);
    take_commit(2);
    guesses_say("a pickup, then a bar at 110");
    g = guess_has(110);
    bad += check("a pickup and a bar's riff at 110 BPM: 110 the first guess", fr.on && g != 0);
    if (g == 2)
        fr_knob(0, 1);
    fr_knob(2, 1);                                    /* KNOB 3: the 1 on the second note */
    fr_knob(1, -3);                                   /* .. BARS 1 */
    fr_knob(1, 1);                                    /* .. 2 */
    len = (uint32_t)t->p[P_SLEN];
    bad += check("  .. KNOB 3: the 1 on the next note; the pickup at the loop's end (an 1/8 before it)",
                 fr.start == 1u && len == 32u && t->step[len - 2u].time == ST_NOTE && has_note(&t->step[len - 2u], 60) &&
                 has_note(&t->step[0], 62) && has_note(&t->step[7], 69));
    fr_close(1);
    return bad;
}

static int picker(void)
{
    int bad = 0, ok;
    uint32_t i;
    double six = 60000.0 / 100.0 / 4.0;
    track_t *t = ft_track(0);
    step_t before[NSTEP];
    t->step[3] = (step_t){{72}, 1, ST_NOTE};          /* a note there already: the take replaces it */
    memcpy(before, t->step, sizeof before);
    for (i = 0; i < 8u; i++)
        fe_add((uint32_t)(i * 2u * six + 40), (uint32_t)six, 60 + i, 100);   /* 1/8s at 100 BPM */
    fe_play(t, fm1_ms);
    take_commit(0);
    ok = fr.on && !has_note(&t->step[3], 72) && has_note(&t->step[0], 60);
    bad += check("the picker: the take in place of the pattern (the note on step 4 gone)", ok);
    fr_knob(3, -1);
    bad += check("  .. KNOB 4 left: TEMPO KEPT (the song's 120 back)", !fr.set && song.g[G_BPM] == 120);
    fr_knob(3, 1);
    bad += check("  .. right: TEMPO SET again", fr.set && song.g[G_BPM] == fr.bpm[fr.ci]);
    fr_knob(1, 5);
    bad += check("  .. KNOB 2 to 8 BARS: LEN 64 at 1/8", FR_BARS[fr.bi] == 8u && t->p[P_SLEN] == 64 && t->p[P_SDIV] == 1);
    play_press();                                     /* PLAY listens; its stop moves nothing */
    ok = song.playing || transport_req == 1;
    seq_start();
    seq_stop();
    bad += check("  .. PLAY listens with the picker open; the stop leaves its notes (free_n0 kept)",
                 ok && fr.on && free_n0 != free_n);
    fr_close(0);
    ok = !fr.on && !memcmp(t->step, before, sizeof before) && t->p[P_SLEN] == 16 && t->p[P_SDIV] == 2 &&
         song.g[G_BPM] == 120 && str_eq(ui.msg, "FREE TAKE: BACK");
    bad += check("  .. OCT-: the steps, LEN, DIV and the tempo back", ok);
    take_commit(0);                                   /* the same take again (nothing kept yet) */
    ok = fr.on;
    take_commit(0);                                   /* SHIFT + SAVE again: kept */
    ok &= !fr.on && t->step[0].time == ST_NOTE && has_note(&t->step[0], 60) && str_eq(ui.msg, "FREE TAKE KEPT") &&
          free_n0 == free_n;
    bad += check("  .. SHIFT + SAVE again keeps it; the next free take starts after it", ok);
    undo_step(0);
    bad += check("  .. one undo (SAVE held): the steps, LEN and DIV back", !memcmp(t->step, before, sizeof before) &&
                 t->p[P_SLEN] == 16 && t->p[P_SDIV] == 2);
    take_commit(0);
    bad += check("  .. nothing played since: SHIFT + SAVE is the take of before (no picker)", !fr.on);
    t = ft_track(0);
    trk[3].step[0] = (step_t){{36}, 1, ST_NOTE};     /* another track with a note */
    for (i = 0; i < 8u; i++)
        fe_add((uint32_t)(i * 2u * six + 40), (uint32_t)six, 60, 100);
    fe_play(t, fm1_ms);
    take_commit(0);
    bad += check("another track with notes: TEMPO KEPT first", fr.on && !fr.set && song.g[G_BPM] == 120);
    fr_close(0);
    return bad;
}

/* 4-note chords a second apart on a POLY pad (C E G B, A C E G, F A C E, G B D F), each held 0.9 s */
static const uint8_t CH4[4][4] = {{60, 64, 67, 71}, {57, 60, 64, 67}, {53, 57, 60, 64}, {55, 59, 62, 65}};
static track_t *ch4_take(void)                        /* T2 empty, POLY */
{
    track_t *t = ft_track(1);
    t->p[P_VOICE] = V_POLY;
    return t;
}
static void ch4_play(track_t *t)
{
    uint32_t i, k;
    for (i = 0; i < 4u; i++)
        for (k = 0; k < 4u; k++)
            fe_add(i * 1000u + 30u, 900, CH4[i][k], 90);
    fe_play(t, fm1_ms);
    take_commit(1);
}
/* the user's 2.5 dev C report: a chord played stopped onto a step with three notes kept only its top note (an overdub,
 * 4 notes a step). In place of the pattern: the chord whole, nothing of the old one left (past its LEN either) */
static int replaces(void)
{
    static const uint8_t OLD[3] = {36, 40, 43};
    int bad = 0, ok = 1;
    uint32_t s, k;
    track_t *t = ch4_take();
    step_t before[NSTEP];
    put_step(t, 0, 3, OLD, ST_NOTE, 0);               /* step 1: three notes already */
    put_step(t, 9, 1, OLD, ST_NOTE, 0);               /* step 10: one */
    put_step(t, 20, 3, OLD, ST_NOTE, 0);              /* step 21, past LEN 16: stored there */
    t->seq_active = 1;
    memcpy(before, t->step, sizeof before);
    ch4_play(t);
    for (s = 0; s < NSTEP; s++)
        for (k = 0; k < 3u; k++)
            ok &= !has_note(&t->step[s], OLD[k]);
    bad += check("a free take on a pattern: in its place (nothing of it left, past its LEN either)", fr.on && ok);
    ok = t->step[0].time == ST_NOTE && t->step[0].n == 4u;
    for (k = 0; k < 4u; k++)
        ok &= has_note(&t->step[0], CH4[0][k]) && !step_tied(&t->step[0], k);
    bad += check("  .. the first chord whole on step 1, where three notes were", ok);
    fr_close(0);
    bad += check("  .. OCT-: the pattern as it was", !memcmp(t->step, before, sizeof before) && t->p[P_SLEN] == 16);
    return bad;
}
/* KNOB 1 to the next guess and back: the steps of the first again (on a POLY track each guess started from the last
 * one's notes) */
static int knob_back(void)
{
    int bad = 0;
    track_t *t = ch4_take();
    step_t first[NSTEP], second[NSTEP];
    ch4_play(t);
    memcpy(first, t->step, sizeof first);
    fr_knob(0, 1);
    memcpy(second, t->step, sizeof second);
    fr_knob(0, -1);
    bad += check("KNOB 1 to the next guess and back: the first guess's steps again (POLY)",
                 fr.on && fr.nc >= 2u && memcmp(first, second, sizeof first) && !memcmp(t->step, first, sizeof first));
    fr_close(0);
    return bad;
}
/* chords strummed by hand: 4 notes 20 ms apart, a quarter apart at 150 BPM (where 60 ms is past half a 1/16) */
static int strum(void)
{
    static const uint8_t IV[4] = {0, 4, 7, 11};
    int bad = 0, ok = 1;
    uint32_t i, k, g, s;
    track_t *t = ch4_take();
    for (i = 0; i < 8u; i++)
        for (k = 0; k < 4u; k++)
            fe_add(i * 400u + 30u + k * 20u, 300, 48u + 2u * i + IV[k], 90);
    fe_play(t, fm1_ms);
    take_commit(1);
    guesses_say("strummed chords a quarter apart at 150");
    bad += check("strummed chords (a note every 20 ms): one onset each", fr.on && fr.non == 8u);
    for (g = 0; g < fr.nc; g++) {
        if (g)
            fr_knob(0, 1);
        for (s = 0; s < (uint32_t)t->p[P_SLEN]; s++) {
            uint32_t starts = 0;
            for (k = 0; k < t->step[s].n; k++)
                starts += t->step[s].time == ST_NOTE && !step_tied(&t->step[s], k);
            ok &= !starts || starts == 4u;
        }
    }
    bad += check("  .. each chord whole on one step, at every guess", fr.nc >= 2u && ok);
    fr_close(0);
    return bad;
}

/* the picker through the panel (ui_input.c): SHIFT + SAVE opens it, KNOB 2 turns BARS, other knobs and buttons nothing,
 * OCT- puts it back */
static int ft_panel(void)
{
    int bad = 0, ok;
    uint32_t i, sel;
    double six = 60000.0 / 120.0 / 4.0;
    track_t *t = ft_track(0);
    for (i = 0; i < 8u; i++)
        fe_add((uint32_t)(i * 4u * six + 40), (uint32_t)six, 60 + i, 100);   /* quarters at 120: 2 bars */
    fe_play(t, fm1_ms);
    frame();
    btn_down(B_HOME); frame();
    press(B_SAVE);
    btn_up(B_HOME); frame();
    ok = fr.on && FR_BARS[fr.bi] == 2u && t->p[P_SLEN] == 32;
    bad += check("the panel: SHIFT + SAVE stopped opens the picker (2 BARS)", ok);
    sel = song.sel;
    turn(EN_K2, -1);
    turn(EN_ALGO, 1);                                 /* (ALGORITHM: no other track while it is open) */
    ok = fr.on && FR_BARS[fr.bi] == 1u && t->p[P_SLEN] == 16 && song.sel == sel && str_eq(head_title(""), "FREE TAKE") &&
         ui.hot_col == 1u && ui.hot_t;
    bad += check("  .. KNOB 2: 1 BAR (LEN 16), its card lit; ALGORITHM does nothing meanwhile; the screen stays", ok);
    press(B_OCTDN);
    bad += check("  .. OCT-: back, the picker closed", !fr.on && t->p[P_SLEN] == 16 && t->step[0].time == ST_REST &&
                 !str_eq(head_title(""), "FREE TAKE"));
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += riff97();
    bad += drums128();
    bad += chords90();
    bad += pickup110();
    bad += picker();
    bad += replaces();
    bad += knob_back();
    bad += strum();
    bad += ft_panel();
    printf("%s\n", bad ? "FREETAKE TEST FAILED" : "freetake test passed");
    return bad != 0;
}
