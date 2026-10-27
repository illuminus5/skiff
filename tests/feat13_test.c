/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Skiff 1.3: the features taken from Felucca 1.2 .. 1.6.1, on the real sources (with the stubs of tests/ui_test.c):
 *   LFO 2 (#16)  SYNC: a cycle per note value of the tempo; TRIG FREE: a note leaves the phase; POLARITY UNI: one
 *                side; PLAY puts a FREE + SYNC LFO on the bar; the page, RATE dim while SYNC is on
 *   SEQ TOOLS    F4 EUCLID (hits / steps / rotation, DRUM lane or notes, OCT+ applies, OCT- cancels, one undo),
 *                D5 DOUBLE, E5 HALF (LEN x2 / half, ties, locks and the steps' own fields with them, one undo)
 *   FOLLOW       the step pages show the playhead's bank on a melodic pattern over 16 steps while it plays; held on
 *                the cursor's by a cursor move or an edit, again after 3 s (from Felucca 1.6's tests)
 *   MOMENTARY    LFO held: KNOB 1..4 on HOME, the sound / FX pages and MIXER change their values, LFO let go puts them
 *                back; OCT+ keeps; no automation; a load puts them back first; STEP and a layer as ever; 8 at most
 *   LANE MUTES   DRUM and DRUM-X: the EDIT layer's black keys 1..8 mute the lanes (the hit unheard, the others as
 *                ever); OCT- puts them back as the layer opened; (2.0.1, #54) OCT+ every lane on at once; a sound or
 *                project load clears them
 *   PREVIEW      MENU > CONTROL > STEP PREVIEW ON: KNOB 1 on ROLL, stopped, sounds the step the cursor lands on; OFF,
 *                or playing, silent; the MIDI tab holds CLOCK and MIDI IN
 *   PROGRAM      MIDI Program Change: the channel's track loads its n-th preset (PRESETS order, aliases skipped);
 *                past the last, nothing; MIDI IN SEL: the selected track
 *   MIDI LEARN   MENU > MIDI > MIDI LEARN on (the MENU closed), a knob picks its parameter, the next CC is learned to it
 *                (any channel heard, never one ignored; never CC64 and the others with a meaning); then the CC sets it
 *                over its range, out of the standard map; learning again replaces; OCT- clears the pick's CC, OCT+
 *                done; ML_N at most (LEARN FULL); LEARN CLEAR; kept with the settings (engine 1's row, its stars
 *                byte still migrated); RESET SETTINGS clears it
 *   MOD          the matrix's S&H (the LFO cycle's value) and SLEW (from the cycle before's, by the phase), drawn at
 *                the LFO's wrap; DEPTH (MODW 0 closes the LFO, full opens it; none: untouched); at a note VEL
 *                (KEY +12 at AMT 32: +11) and TIME (VEL 127 at AMT 63: 1302 samples late, onto the step's NUDGE;
 *                a ratcheted step on time); none of them: as before
 *   SPREAD       VOICE 2's KNOB 3: a POLY part's voices left and right of PAN in turn (127: the first hard left, the
 *                next hard right); 0, or MONO: every voice on PAN, as before
 *   LANE PANS    (2.0, #36) DRUM and DRUM-X: a lane panned hard left silent on the right (and the other way), half way
 *                at half; the lanes at C as before, the track's PAN over them; all at C: mix_part's own loop
 *   ENV SYNC     ENV DEST's KNOB 4: ATK DEC REL as note values (1/4 at 120 BPM: an attack of 22050 samples, a release
 *                99 % down after it), a knob step a note value; OFF: the tables, bit for bit
 *   CHANNELS     MENU > MIDI > T1..T6 CHANNEL: a track heard on its channel, its own one let go (its held notes too);
 *                two on one channel: the first; MIDI IN says TRACK; the MIDI tab's ten rows scroll, six shown
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static void lfo_block(track_t *t)                      /* one control block of the LFO, as the audio ISR runs it */
{
#if FELUCCA_CPU2
    track_lfo_adv(t);
    track_lfo_end(t);
#else
    track_lfo_tick(t);
#endif
}

/* ---------------------------------------------------------------- LFO 2 --- */
static int lfo2(void)
{
    int bad = 0, ok, neg = 0;
    track_t *t;
    uint32_t n, wraps = 0, old, cyc;
    ui_power_on();
    t = &trk[0];
    song.g[G_BPM] = 120;
    t->p[P_LSYNC] = 5;                                  /* 1/4: a beat, 22050 samples at 120 BPM */
    t->p[P_LRATE] = 0;                                  /* (RATE's slowest: nothing it does would wrap here) */
    t->lfo_ph = 0;
    cyc = (uint32_t)FS * 60u / 120u;
    for (n = 0; n < 8u * cyc / CTL; n++) {             /* eight beats */
        old = t->lfo_ph;
        lfo_block(t);
        wraps += t->lfo_ph < old;
    }
    ok = wraps == 8u || (wraps == 7u && t->lfo_ph > 0xF0000000u);
    t->p[P_LSYNC] = 9;                                  /* 16T: a beat in six */
    for (n = wraps = 0; n < cyc / CTL; n++) {
        old = t->lfo_ph;
        lfo_block(t);
        wraps += t->lfo_ph < old;
    }
    ok &= wraps >= 5u && wraps <= 6u;
    bad += check("LFO 2 SYNC: one cycle per note value of the tempo (1/4: 8 in 8 beats; 16T: 6 a beat), RATE aside", ok);
    t->p[P_LSYNC] = 0;
    t->p[P_LTRIG] = 1;                                  /* FREE */
    t->p[P_LPHASE] = 64;
    t->lfo_ph = 0x12345678u;
    trk_note_on(t, 60, 100);
    ok = t->lfo_ph == 0x12345678u && t->lfo_fade == 0;  /* (FADE starts again) */
    trk_note_off(t, 60);                                /* (its gate shut: the next note is a fresh phrase) */
    t->p[P_LTRIG] = 0;                                  /* NOTE: from PHASE, as before */
    trk_note_on(t, 60, 100);
    ok &= t->lfo_ph == 64u << 25;
    trk_note_off(t, 60);
    bad += check("  TRIG FREE: a note leaves the phase (FADE starts again); NOTE: back to PHASE, as before", ok);
    t->p[P_LWAVE] = 0;                                  /* SIN */
    t->p[P_LRATE] = 100;
    for (n = 0, neg = 0; n < 2000u; n++) {
        lfo_block(t);
        neg |= t->lfo_val < 0;
    }
    ok = neg;
    t->p[P_LPOL] = 1;                                   /* UNI */
    for (n = 0, neg = 0; n < 2000u; n++) {
        lfo_block(t);
        neg |= t->lfo_val < 0;
    }
    ok &= !neg;
    t->p[P_LPOL] = 0;
    bad += check("  POLARITY: BI swings both ways; UNI stays at 0 and up", ok);
    t->p[P_LTRIG] = 1;
    t->p[P_LSYNC] = 5;
    t->p[P_LPHASE] = 32;
    t->lfo_ph = 0x0BADF00Du;
    seq_start();
    ok = t->lfo_ph == 32u << 25;
    seq_stop();
    t->p[P_LSYNC] = 0;
    t->lfo_ph = 0x0BADF00Du;
    seq_start();
    ok &= t->lfo_ph == 0x0BADF00Du;
    seq_stop();
    t->p[P_LTRIG] = 0;
    bad += check("  PLAY: a FREE + SYNC LFO from PHASE, on the bar; FREE alone runs on", ok);
    ui_power_on();
    go_title("LFO 2"); frame();
    ok = cur_page()->id[0] == P_LSYNC && cur_page()->id[1] == P_LTRIG && cur_page()->id[2] == P_LPOL &&
         str_eq(TP[P_LSYNC].label, "SYNC") && !TSEL->p[P_LSYNC] && !TSEL->p[P_LTRIG] && !TSEL->p[P_LPOL];
    go_title("LFO"); frame();
    ok &= !card_dead(cur_page(), 0);
    TSEL->p[P_LSYNC] = 3;
    ok &= card_dead(cur_page(), 0);
    TSEL->p[P_LSYNC] = 0;
    bad += check("  the LFO 2 page (OFF NOTE BI: as before); RATE dim while SYNC is on", ok);
    return bad;
}

/* --------------------------------------------- SEQ TOOLS: EUCLID, DOUBLE, HALF (from Felucca 1.5.5's tests) --- */
static track_t *melody(uint32_t on)                     /* T1, LEN 16 at 1/16, 120 BPM: note 60 + i where bit i is set */
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    sel_set(0);
    t = &trk[0];
    track_defaults_steps(t);
    motion_clear(t);
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    t->p[P_VOICE] = V_POLY;
    song.g[G_SWING] = 0;
    song.g[G_BPM] = 120;
    for (i = 0; i < 16u; i++)
        if ((on >> i) & 1u)
            t->step[i] = (step_t){.note = {(uint8_t)(60u + i)}, .n = 1, .time = ST_NOTE};
    return t;
}
static void tkey(uint32_t w) { key_down(white(w)); frame(); key_up(white(w)); frame(); }
static void kt(uint32_t role, int32_t s) { memset(ui.enc_t, 0, sizeof ui.enc_t); turn(role, s); }
static int tl_lock_at5(void) { int16_t v; return motion_lock_get(TSEL, 5, P_REV, &v) && v == 50; }
static void oct_fwd(void) { btn_down(B_OCTUP); frame(); btn_up(B_OCTUP); frame(); }
static int ref_hit(uint32_t i, uint32_t k, uint32_t n, uint32_t r) { return ((((i % n) + n - r % n) % n) * k) % n < k; }
static int eu_rest_but(const track_t *t, uint32_t len, uint32_t k, uint32_t n, uint32_t r)   /* NOTE where it hits, */
{                                                                                              /* else a REST or a TIE */
    uint32_t i;
    for (i = 0; i < len; i++)
        if ((t->step[i].time == ST_NOTE && t->step[i].n) != ref_hit(i, k, n, r) ||
            (!ref_hit(i, k, n, r) && t->step[i].time == ST_NOTE))
            return 0;
    return 1;
}
static int euclid(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u | 1u << 5);                 /* C4 (60) tied over step 2, F4 (65) on step 6; a lock on 6 */
    step_t ref[NSTEP];
    uint32_t i, m;
    t->step[1] = (step_t){.time = ST_TIE};
    t->step[0].flags = SF_ACCENT;
    t->step[30] = (step_t){.note = {90}, .n = 1, .time = ST_NOTE};   /* (past LEN: untouched) */
    motion_set_lock(t, 5, P_REV, 50);
    memcpy(ref, t->step, sizeof ref);
    go_page(GR_ROLL); frame();
    eu.steps = 0;                                      /* (the first EUCLID since the power came: 4 / 16) */
    btn_down(B_SEQ); frames(500);
    tkey(7);                                         /* F4: EUCLID */
    ok = eu.on && eu.hits == 4u && eu.steps == 16u && !eu.rot && msg_is("EUCLID 4/16") && eu_rest_but(t, 16, 4, 16, 0);
    ok &= t->step[0].note[0] == 60 && (t->step[0].flags & SF_ACCENT) && t->step[1].time == ST_TIE &&   /* C4 kept, its tie */
          t->step[4].note[0] == 60 && t->step[8].note[0] == 60 && t->step[4].n == 1u && !t->step[4].flags &&   /* the root */
          t->step[5].time == ST_REST && t->step[30].note[0] == 90;
    bad += check("SEQ TOOLS EUCLID (F4): 4 / 16 at once: a NOTE step kept (C4, its accent and tie), the root on the new "
                 "hits, the rest emptied; past LEN untouched", ok);
    kt(EN_K1, 1);
    ok = eu.hits == 5u && eu_rest_but(t, 16, 5, 16, 0) && msg_is("EUCLID 5/16");
    kt(EN_K3, 2);
    ok &= eu.rot == 2u && eu_rest_but(t, 16, 5, 16, 2) && msg_is("EUCLID 5/16 +2");
    kt(EN_K2, -8);
    ok &= eu.steps == 8u && eu_rest_but(t, 16, 5, 8, 2);
    kt(EN_K2, 100);
    ok &= eu.steps == 16u;                             /* (STEPS up to LEN) */
    kt(EN_K1, 100);
    ok &= eu.hits == 16u && eu_rest_but(t, 16, 16, 16, 2);
    kt(EN_K1, -100);
    ok &= eu.hits == 0u;
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].time != ST_NOTE;
    bad += check("  KNOB 1 HITS (0..STEPS), 2 STEPS (1..LEN, the cycle repeated), 3 ROTATE: rewritten from the copy each "
                 "turn", ok);
    ui.force = 1; frame();
    oct_back();
    ok = !eu.on && !memcmp(t->step, ref, sizeof ref) && tl_lock_at5() && msg_is("EUCLID CANCELLED") && ui.layer == LAYER_SEQ;
    bad += check("  OCT-: cancelled, the steps as they were (the layer stays open)", ok);
    tkey(7);
    kt(EN_K1, 4);                                      /* (0 -> 4 hits) */
    ok = eu.hits == 4u && eu.on;
    oct_fwd();
    ok &= !eu.on && msg_is("EUCLID APPLIED") && eu_rest_but(t, 16, 4, 16, 2) && !tl_lock_at5();
    bad += check("  OCT+: applied; an emptied step's lock goes", ok);
    btn_up(B_SEQ); frame();
    hold(B_SAVE);
    bad += check("  SAVE held: one undo for all of it (the steps and the lock back)",
                 !memcmp(t->step, ref, sizeof ref) && tl_lock_at5());
    btn_down(B_SEQ); frames(500);
    tkey(7);
    ok = eu.on;
    btn_up(B_SEQ); frame();
    ok &= !eu.on && eu_rest_but(t, 16, 4, 16, 2) && !ui.layer;
    bad += check("  the layer let go with it open: applied", ok);
    ui_power_on();                                     /* DRUM: the selected lane only */
    sel_set(3);
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
    for (i = 0; i < 16u; i += 8u)
        grid_hit(TSEL, i, DV_KICK, 1);
    grid_hit(TSEL, 4, DV_SNARE, 1);
    grid_acc(TSEL, 4, DV_SNARE, 1);
    memcpy(ref, TSEL->step, sizeof ref);
    go_page(GR_ROLL); frame();
    eu.steps = 0;
    btn_down(B_SEQ); frames(500);
    key_down(black(3)); frame(); key_up(black(3)); frame();   /* the closed hat */
    tkey(7);
    kt(EN_K1, 1);                                      /* 5 / 16 */
    for (m = 0, i = 0; i < 16u; i++)
        m |= ((step_lanes(&TSEL->step[i]) >> 3) & 1u) << i;
    ok = ui.lane == 3u && m == 0x2491u;
    for (i = 0; i < 16u; i++)
        ok &= (step_lanes(&TSEL->step[i]) & 7u) == (step_lanes(&ref[i]) & 7u) &&
              step_accents(&TSEL->step[i]) == step_accents(&ref[i]);
    bad += check("  on DRUM: the selected lane (5 / 16), the other lanes and accents untouched", ok);
    key_down(black(2)); frame(); key_up(black(2)); frame();   /* another lane: the hits go there */
    for (m = 0, i = 0; i < 16u; i++)
        m |= ((step_lanes(&TSEL->step[i]) >> 2) & 1u) << i | ((step_lanes(&TSEL->step[i]) >> 3) & 1u) << 16;
    ok = m == 0x2491u;
    oct_fwd();
    btn_up(B_SEQ); frame();
    bad += check("  another lane picked while open: the hits go to it, the first lane as it was", ok && !eu.on);
    return bad;
}
static int double_half(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u << 2 | 1u << 7);
    step_t ref[NSTEP];
    uint32_t i, mc;
    int16_t v;
    t->p[P_SLEN] = 8;
    t->step[0] = (step_t){.time = ST_TIE};             /* step 8's note tied over the loop's end into step 1 */
    t->step[3] = (step_t){.time = ST_TIE};             /* step 3's held over 4 */
    t->step[2].cond = 1;
    step_set_ratchet(&t->step[2], 2);
    t->step[2].nudge = 3;
    t->step[2].vel = 80;
    step_set_chance(&t->step[7], 40);
    t->step[9] = (step_t){.note = {90}, .n = 1, .time = ST_NOTE};   /* stored past LEN, where the copy goes */
    motion_set_lock(t, 2, P_REV, 50);
    motion_set_event(t, 7, P_CHOR, 20);
    motion_set_lock(t, 10, P_DIST, 30);                /* (past LEN: replaced by the copy's) */
    motion_set_enabled(t, 1);
    memcpy(ref, t->step, sizeof ref);
    mc = motion_count(t);
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);
    tkey(12);                                        /* D5: DOUBLE */
    ok = t->p[P_SLEN] == 16 && msg_is("DOUBLED: 16 STEPS") && !memcmp(t->step, ref, 8u * sizeof ref[0]) &&
         !memcmp(t->step + 8, ref, 8u * sizeof ref[0]) && !memcmp(t->step + 16, ref + 16, 48u * sizeof ref[0]);
    ok &= motion_lock_get(t, 10, P_REV, &v) && v == 50 && !motion_lock_get(t, 10, P_DIST, &v) &&
          motion_find(t, 15, P_CHOR) >= 0 && motion_find(t, 7, P_CHOR) >= 0 && motion_count(t) == 4u;
    ok &= t->step[10].cond == 1u && step_ratchet(&t->step[10]) == 2u && t->step[10].nudge == 3 &&
          t->step[10].vel == 80u && step_chance(&t->step[15]) == 40u && t->step[8].time == ST_TIE;
    bad += check("SEQ TOOLS DOUBLE (D5): LEN 8 -> 16, steps 9..16 a copy (ties, condition, RATCH, NUDGE, VEL, CHANCE), "
                 "the locks and automation copied, what was stored there replaced", ok);
    btn_up(B_SEQ); frame();
    hold(B_SAVE);
    ok = t->p[P_SLEN] == 8 && !memcmp(t->step, ref, sizeof ref) && motion_count(t) == mc &&
         motion_lock_get(t, 10, P_DIST, &v) && v == 30;
    bad += check("  SAVE held: one undo (LEN, the steps, the automation back)", ok);
    t = melody(1u << 2 | 1u << 7);
    t->p[P_SLEN] = 8;
    t->step[3] = (step_t){.time = ST_TIE};
    t->step[4] = (step_t){.time = ST_TIE};             /* step 3's held over 4 and 5 */
    t->step[9] = (step_t){.note = {90}, .n = 1, .time = ST_NOTE};
    motion_set_lock(t, 2, P_REV, 50);
    motion_set_lock(t, 7, P_REV, 60);
    memcpy(ref, t->step, sizeof ref);
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);
    tkey(13);                                        /* E5: HALF */
    ok = t->p[P_SLEN] == 4 && msg_is("HALVED: 4 STEPS") && !memcmp(t->step, ref, 4u * sizeof ref[0]) &&
         t->step[4].time == ST_REST && t->step[7].time == ST_REST && t->step[9].note[0] == 90 &&
         motion_lock_get(t, 2, P_REV, &v) && !motion_lock_get(t, 7, P_REV, &v);
    bad += check("SEQ TOOLS HALF (E5): LEN 8 -> 4, the first half kept, the second cleared with its locks, past the old "
                 "LEN untouched", ok);
    tkey(13); tkey(13);
    ok = t->p[P_SLEN] == 1;
    tkey(13);
    ok &= t->p[P_SLEN] == 1 && msg_is("1 STEP: NO HALF");
    bad += check("  HALF down to 1 step; there: 1 STEP: NO HALF", ok);
    btn_up(B_SEQ); frame();
    hold(B_SAVE);
    bad += check("  SAVE held: the last HALF undone (LEN 2)", t->p[P_SLEN] == 2);
    t = melody(0xFFFFu);                                /* LEN 40: 64 (the first 24 copied); 64: no room */
    t->p[P_SLEN] = 40;
    for (i = 16; i < NSTEP; i++)
        t->step[i] = (step_t){.note = {(uint8_t)(20u + i)}, .n = 1, .time = ST_NOTE};
    memcpy(ref, t->step, sizeof ref);
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);
    tkey(12);
    ok = t->p[P_SLEN] == 64 && !memcmp(t->step, ref, 40u * sizeof ref[0]) && !memcmp(t->step + 40, ref, 24u * sizeof ref[0]);
    memcpy(ref, t->step, sizeof ref);
    tkey(12);
    ok &= t->p[P_SLEN] == 64 && msg_is("64 STEPS: NO ROOM") && !memcmp(t->step, ref, sizeof ref);
    bad += check("  DOUBLE from LEN 40: 64 (steps 1..24 copied); at 64: NO ROOM, nothing done", ok);
    t->p[P_SLEN] = 8;
    for (i = 0; motion.count < MOTION_MAX - 4u; i++)
        motion_set_event(&trk[1 + i % 3u], i / 3u % NSTEP, P_REV, (int16_t)(i % 100u));
    for (i = 0; i < 4u; i++)
        motion_set_lock(t, i, P_REV, 40);               /* (the 64 used: no room for 4 copies) */
    memcpy(ref, t->step, sizeof ref);
    tkey(12);
    ok = t->p[P_SLEN] == 8 && msg_is("AUTOMATION FULL") && !memcmp(t->step, ref, sizeof ref) && motion_count(t) == 4u;
    btn_up(B_SEQ); frame();
    bad += check("  the 64 records with no room for the copies: AUTOMATION FULL, nothing done", ok);
    for (i = 0; i < NTRK; i++)
        motion_clear(&trk[i]);
    return bad;
}

/* ---------------------------------------------------------- FOLLOW --- */
static track_t *pattern(uint32_t len, uint32_t cur, uint32_t idx, uint32_t graph)   /* T1 playing at idx, on that page */
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    sel_set(0);
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = 2;
    for (i = 0; i < len; i += 4)
        t->step[i] = (step_t){.note = {(uint8_t)(48 + i)}, .n = 1, .time = ST_NOTE};
    go_page(graph);
    ui.cursor = (uint8_t)cur;
    ui.bank = (uint8_t)(cur / 16u);
    song.playing = 1;
    t->seq_idx = (uint16_t)idx;
    frame();
    return t;
}
static void at(track_t *t, uint32_t idx) { t->seq_idx = (uint16_t)idx; frame(); }
static int follow_play(void)
{
    int bad = 0, ok;
    track_t *t = pattern(40, 0, 0, GR_ROLL);
    step_t ref[NSTEP];
    memcpy(ref, t->step, sizeof ref);
    ok = ui.bank == 0u;
    at(t, 20);
    ok &= ui.bank == 1u;
    at(t, 36);
    ok &= ui.bank == 2u;
    at(t, 5);
    ok &= ui.bank == 0u && ui.cursor == 0u && t->p[P_SLEN] == 40 && !memcmp(ref, t->step, sizeof ref);
    t = pattern(40, 0, 0, GR_FEEL);
    at(t, 39);
    ok &= ui.bank == 2u;
    bad += check("FOLLOW: ROLL and STEP, 40 steps, playing: the bank is the playhead's (0 1 2 0; the last 8 too); the "
                 "cursor, steps and LEN untouched", ok);
    t = pattern(16, 0, 0, GR_ROLL);
    at(t, 9);
    ok = ui.bank == 0u;
    t = pattern(40, 0, 0, GR_ROLL);
    song.playing = 0;
    at(t, 36);
    ok &= ui.bank == 0u;
    t = pattern(40, 0, 0, GR_EVENTS);
    at(t, 36);
    ok &= ui.bank == 0u;
    t = pattern(40, 0, 0, GR_ROLL);
    ui.home = 1;
    at(t, 36);
    ok &= ui.bank == 0u;
    ui.home = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    ui.bank = 0;
    at(t, 36);
    ok &= drum_track(t) && ui.bank == 0u;
    bad += check("  16 steps, stopped, AUTOMATION, HOME, a DRUM grid: the bank stays", ok);
    t = pattern(40, 0, 0, GR_ROLL);
    at(t, 20);
    ok = ui.bank == 1u;
    kt(EN_PRESET, 1);                                  /* the cursor to step 2: its bank (0) */
    ok &= ui.cursor == 1u && ui.bank == 0u;
    at(t, 36);
    ok &= ui.bank == 0u;
    fm1_ms += 3100u;
    frame();
    ok &= ui.bank == 2u;
    kt(EN_K4, 1);                                      /* an edit of the cursor's step ((2.0) VELOCITY): back to it */
    ok &= ui.bank == 0u;
    at(t, 0); at(t, 20); at(t, 39); at(t, 1); at(t, 20);   /* (once round the pattern: following again) */
    ok &= ui.bank == 1u;
    bad += check("  PRESETS (the cursor) or an edit holds the cursor's bank; 3 s, or one pass round, follows again", ok);
    song.playing = 0;
    return bad;
}

/* ------------------------------------------------------ MOMENTARY --- */
static int momentary(void)
{
    int bad = 0, ok;
    int16_t *p0, *p1, v0, v1, g0;
    uint32_t k;
    ui_power_on();
    ret_set(RET_SNAP);                                 /* ((2.0, D 2A) back at once, as 1.3's: ui_test.c's way back) */
    go_home();
    (void)home_param(0, &p0);
    (void)home_param(1, &p1);
    v0 = *p0;
    v1 = *p1;
    btn_down(B_LFO); frame();
    kt(EN_K1, 6);
    kt(EN_K2, -4);
    ok = *p0 != v0 && *p1 != v1 && mom.n == 2u && msg_is("MOMENTARY");
    btn_up(B_LFO); frame();
    bad += check("MOMENTARY: LFO held, KNOB 1 / 2 on HOME change their values; LFO let go puts them back (no LFO page)",
                 ok && *p0 == v0 && *p1 == v1 && !mom.n && ui.home && msg_is("BACK"));
    press(B_LFO);
    bad += check("  LFO alone still opens its page", !ui.home && cur_page()->fam == FAM_LFO);
    go_home();
    kt(EN_K1, 2);
    bad += check("  without LFO a turn stays", *p0 != v0 && !mom.n);
    v0 = *p0;
    btn_down(B_LFO); frame();
    kt(EN_K1, 5);
    k = (uint32_t)song.octave;
    press(B_OCTUP);
    ok = !mom.n && msg_is("KEPT") && song.octave == (int8_t)k;
    btn_up(B_LFO); frame();
    bad += check("  OCT+ with LFO held keeps them (no octave shift); LFO let go then changes nothing",
                 ok && *p0 != v0 && ui.home);
    go_title("REVERB"); frame();
    g0 = song.g[G_RSIZE];
    btn_down(B_LFO); frame();
    kt(EN_K2, 9);
    ok = song.g[G_RSIZE] != g0;
    btn_up(B_LFO); frame();
    bad += check("  a global FX value (REVERB SIZE) too: back on release", ok && song.g[G_RSIZE] == g0);
    go_home();
    song.playing = 1; song.rec = 1u << song.sel;
    k = motion.count;
    btn_down(B_LFO); frame();
    kt(EN_K1, 7);
    ok = motion.count == k;
    btn_up(B_LFO); frame();
    song.playing = 0; song.rec = 0;
    bad += check("  a momentary turn is not recorded as automation (armed and playing)", ok);
    v0 = *p0;
    btn_down(B_LFO); frame();
    kt(EN_K1, 8);
    ok = *p0 != v0;
    turn(EN_PRESET, 1);
    ok &= !mom.n;
    btn_up(B_LFO); frame();
    frames(30);
    hold(B_SAVE);                                      /* (the undo copy: the sound as it was) */
    (void)home_param(0, &p0);
    ok &= *p0 == v0;
    bad += check("  a sound load with LFO held: the values back first (SAVE held: the sound as it was)", ok);
    ui_power_on();
    ret_set(RET_SNAP);
    go_page(GR_ROLL); frame();
    btn_down(B_LFO); frame();
    kt(EN_K2, 1);
    ok = !mom.n;
    btn_up(B_LFO); frame();
    bad += check("  on STEP (and the action pages, the lists) the knobs act as ever", ok && cur_page()->graph == GR_ROLL);
    go_page(GR_TRK); frame();
    {
        int16_t lv = TSEL->p[P_LEVEL], pn = TSEL->p[P_PAN], rv = TSEL->p[P_REV], mu = TSEL->p[P_MUTE];
        btn_down(B_LFO); frame();
        kt(EN_K1, -5);
        kt(EN_K2, 7);
        kt(EN_K3, 9);
        kt(EN_K4, 1);
        ok = TSEL->p[P_LEVEL] != lv && TSEL->p[P_PAN] != pn && TSEL->p[P_REV] != rv && TSEL->p[P_MUTE] != mu && mom.n == 4u;
        btn_up(B_LFO); frame();
        ok &= TSEL->p[P_LEVEL] == lv && TSEL->p[P_PAN] == pn && TSEL->p[P_REV] == rv && TSEL->p[P_MUTE] == mu && !mom.n;
        bad += check("  MIXER: LEVEL PAN REVERB MUTE momentary, back on release", ok);
    }
    go_home(); frames(400);
    (void)home_param(0, &p0);
    v0 = *p0;
    btn_down(B_LFO); frame();
    btn_down(B_GLO); frames(60);
    kt(EN_K1, 3);
    ok = !mom.n && *p0 == v0;
    btn_up(B_GLO); frame();
    btn_up(B_LFO); frame();
    bad += check("  with a layer's button held the knobs are the layer's", ok);
    go_home(); frames(400);
    btn_down(B_LFO); frame();
    for (k = 0; k < MOM_LFO; k++)
        (void)mom_hold(&trk[k % NTRK].p[P_LEVEL + k / NTRK], 0);
    v0 = *p0;
    ui.msg_t = 0;
    kt(EN_K1, -3);
    ok = *p0 == v0 && msg_is("MOMENTARY FULL");
    btn_up(B_LFO); frame();
    bad += check("  at most 8 values: MOMENTARY FULL, the turn does nothing", ok && !mom.n);
    return bad;
}

/* ----------------------------------------------------- LANE MUTES --- */
static int32_t hit_level(track_t *t, uint32_t lane)    /* a hit on lane's note: |sum| of its first blocks */
{
    static int32_t buf[CTL];
    int32_t a = 0;
    uint32_t b, i;
    for (i = 0; i < NVOICE; i++)
        t->v[i].active = 0;
    trk_note_on(t, DRUM_LANE_NOTE[lane], 110);
    for (b = 0; b < 8u; b++) {
        memset(buf, 0, sizeof buf);
        track_render(t, buf, CTL);
        for (i = 0; i < CTL; i++)
            a += buf[i] < 0 ? -buf[i] / 64 : buf[i] / 64;
    }
    trk_note_off(t, DRUM_LANE_NOTE[lane]);
    return a;
}
static int lane_mutes(void)
{
    int bad = 0, ok, e;
    for (e = 0; e < 2; e++) {
        track_t *t;
        int32_t k0, s0;
        ui_power_on();
        sel_set(3);
        t = TSEL;
        set_engine_of(t, e ? ENGI_DRUMX : ENGI_DRUM);
        t->engine = t->eng_req;
        k0 = hit_level(t, 0);
        s0 = hit_level(t, 1);
        go_home(); frame();
        btn_down(B_EDIT); frames(500);
        key_down(black(0)); frame(); key_up(black(0)); frame();   /* black key 1: the kick */
        ok = drum_mute[3] == 1u && ui.layer == LAYER_EDIT && k0 > 0 && hit_level(t, 0) == 0 &&
             hit_level(t, 1) > s0 / 2;                  /* (DRUM-X's noise runs on: the snare near, not equal) */
        key_down(black(0)); frame(); key_up(black(0)); frame();
        ok &= !drum_mute[3] && hit_level(t, 0) > 0;
        key_down(black(1)); frame(); key_up(black(1)); frame();
        ok &= drum_mute[3] == 2u;
        oct_back();
        ok &= !drum_mute[3];
        key_down(black(1)); frame(); key_up(black(1)); frame();
        btn_up(B_EDIT); frame();
        ok &= drum_mute[3] == 2u;
        btn_down(B_EDIT); frames(500);                  /* ((2.0.1, #54) the layer again: two more muted, OCT+ all on) */
        key_down(black(0)); frame(); key_up(black(0)); frame();
        key_down(black(2)); frame(); key_up(black(2)); frame();
        ok &= drum_mute[3] == 7u && hit_level(t, 0) == 0;
        oct_fwd();
        ok &= !drum_mute[3] && hit_level(t, 0) > 0 && ui.layer == LAYER_EDIT;
        btn_up(B_EDIT); frame();
        ok &= !drum_mute[3];
        bad += check(e ? "  DRUM-X the same" : "LANE MUTES: DRUM, EDIT layer black key 1: the kick unheard, the snare as ever; "
                     "again: back; OCT- as the layer opened; kept when let go; OCT+ every lane on", ok);
    }
    apply_preset_to(TSEL, 0);                          /* (the engine's first preset: a sound load) */
    bad += check("  a sound load clears the track's mutes", !drum_mute[3]);
    drum_mute[2] = 5;
    project_save(1);
    project_load(1);
    bad += check("  a project load clears every track's", !drum_mute[2]);
    return bad;
}

/* --------------------------------------------------- STEP PREVIEW --- */
static uint32_t gates_on(const track_t *t, uint32_t note)   /* voices of t gated on note */
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate && t->v[i].note == note;
    return n;
}
static int step_preview(void)
{
    int bad = 0, ok;
    track_t *t = melody(1u << 3 | 1u << 4);            /* steps 4 and 5: 63, 64 */
    go_page(GR_ROLL); frame();
    ui.cursor = 2;
    favorites.factory[15][24] = 0xA5u;                  /* (a migrated unit's settings: the byte is free) */
    menu_put(MI_PREVIEW, 0);
    kt(EN_PRESET, 1);                                      /* OFF: the cursor to step 4, silent */
    events_block(CTL);
    ok = ui.cursor == 3u && !aud_req && !gates_on(t, 63);
    menu_put(MI_PREVIEW, 1);
    ok &= menu_get(MI_PREVIEW) == 1u && str_eq(menu_vname(MI_PREVIEW, 1), "ON") && MI_TAB[MI_PREVIEW] == MTAB_CONTROL;
    kt(EN_PRESET, 1);                                      /* ON: step 5 sounds once */
    ok &= aud_req == (4u << 8 | 1u);
    events_block(CTL);
    ok &= !aud_req && gates_on(t, 64) == 1u && !song.playing;
    events_block(div_samples(2));                      /* (a 1/16 at the track's GATE: let go) */
    ok &= !gates_on(t, 64);
    seq_start();
    kt(EN_PRESET, -1);                                     /* playing: no preview */
    ok &= !aud_req;
    seq_stop();
    menu_put(MI_PREVIEW, 0);
    ok &= MI_TAB[MI_CLOCK] == MTAB_MIDI && MI_TAB[MI_ROUTE] == MTAB_MIDI;
    bad += check("STEP PREVIEW: ON, PRESETS on NOTES (stopped) sounds the step it lands on, a 1/16; OFF or playing, "
                 "silent; MIDI tab: CLOCK, MIDI IN", ok);
    return bad;
}

/* ------------------------------------------------- PROGRAM CHANGE --- */
static int program_change(void)
{
    int bad = 0, ok;
    uint32_t e, k, n = 0;
    ui_power_on();
    song.g[G_ROUTE] = 0;                               /* MIDI IN CH1-6: channel 2 is T2 */
    e = trk[1].eng_req % NENGINES;
    midi_event(0xC0u, 1, 2, 0);                        /* program 3 (2 from 0) */
    ok = ui.mpc[1] == (0x80u | 2u);
    frame();
    for (k = 0; k < ENGINES[e]->npresets; k++)        /* the 3rd shown preset */
        if (preset_orig(ENGINES[e], k) == k && n++ == 2u)
            break;
    ok &= !ui.mpc[1] && trk[1].preset == k && trk[1].eng_req % NENGINES == e;
    k = trk[1].preset;
    midi_event(0xC0u, 1, 127, 0);                      /* past the last: nothing */
    frame();
    ok &= trk[1].preset == k && !ui.mpc[1];
    song.g[G_ROUTE] = 1;                               /* SEL: any channel, the selected track */
    sel_set(2);
    e = trk[2].eng_req % NENGINES;
    midi_event(0xC0u, 9, 1, 0);
    frame();
    for (k = 0, n = 0; k < ENGINES[e]->npresets; k++)
        if (preset_orig(ENGINES[e], k) == k && n++ == 1u)
            break;
    ok &= trk[2].preset == k;
    song.g[G_ROUTE] = 0;
    bad += check("PROGRAM CHANGE: the channel's track loads its n-th preset (PRESETS order); past the last nothing; "
                 "MIDI IN SEL: the selected track", ok);
    return bad;
}

/* --------------------------------------------------------------- MOD --- */
static void mslot(track_t *t, uint32_t k, uint32_t s, uint32_t d, int32_t a)
{
    t->p[P_M1SRC + 3u * k] = (int16_t)s;
    t->p[P_M1DST + 3u * k] = (int16_t)d;
    t->p[P_M1AMT + 3u * k] = (int16_t)a;
}
static int mod_more(void)
{
    int bad = 0, ok;
    track_t *t;
    uint32_t n, sh, period, cur;
    int32_t a, b, c;
    ui_power_on();
    t = &trk[0];
    t->lfo_rnd = 0xC0000000u;                           /* S&H: the cycle's value */
    t->lfo_prev = -8192;
    t->lfo_ph = 0;
    a = mod_tsrc(t, MS_SLEW, 0);
    t->lfo_ph = 0x80000000u;
    b = mod_tsrc(t, MS_SLEW, 0);
    t->lfo_ph = 0xFFFF0000u;
    c = mod_tsrc(t, MS_SLEW, 0);
    ok = mod_tsrc(t, MS_SH, 0) == 16384 && a == -8192 && b >= 4090 && b <= 4100 && c >= 16300 && c <= 16384;
    t->p[P_LRATE] = 127;                                /* the wrap: the value moves to SLEW's start, a new one drawn */
    sh = t->lfo_rnd;
    for (n = 0; n < 5000u && t->lfo_rnd == sh; n++)
        lfo_block(t);
    ok &= t->lfo_rnd != sh && t->lfo_prev == 16384;
    bad += check("MOD: S&H the LFO cycle's value; SLEW from the cycle before's to it by the phase; the wrap moves it on", ok);
    mslot(t, 0, MS_MODW, MD_DEPTH, 63);                 /* MODW -> DEPTH */
    t->mw = 0;
    ok = mod_begin(t) && MOD(t).ldep < 1024;
    mod_end(t);
    t->mw = 127;
    ok &= mod_begin(t) && MOD(t).ldep >= 32700;
    mod_end(t);
    mslot(t, 0, MS_LFO, MD_PAN, 63);
    ok &= mod_begin(t) && MOD(t).ldep == 32767;
    mod_end(t);
    bad += check("  DEPTH: MODW 0 closes the LFO, full opens it; no DEPTH slot: the LFO untouched", ok);
    mslot(t, 0, MS_KEY, MD_VEL, 32);                    /* KEY -> VEL: +12 semitones, AMT 32: +11 */
    ok = !mod_begin(t);                                 /* (no per-block work for it) */
    trk_note_on(t, 72, 100);
    ok &= t->m_vel == 111u;
    trk_note_off(t, 72);
    mslot(t, 0, MS_RAND, MD_VEL, 63);                   /* RAND -> VEL: every note its own, 1..127 */
    for (n = 0, a = 0; n < 20u; n++) {
        trk_note_on(t, 60, 100);
        ok &= t->m_vel >= 1u && t->m_vel <= 127u;
        a |= t->m_vel != 100u;
        trk_note_off(t, 60);
    }
    ok &= a;
    mslot(t, 0, 0, 0, 0);
    trk_note_on(t, 60, 100);
    ok &= t->m_vel == 100u;
    trk_note_off(t, 60);
    bad += check("  VEL at the note: KEY +12 at AMT 32 plays 111 for 100; RAND each note its own (1..127); none: 100", ok);
    period = div_samples((uint32_t)t->p[P_SDIV]);
    memset(&t->step[0], 0, sizeof t->step[0]);
    t->step[0].time = ST_NOTE;
    t->step[0].n = 1;
    t->step[0].note[0] = 60;
    t->step[0].flags = SF_ACCENT;
    cur = step_samples(t, period, 0);
    ok = seq_time(t, 0, cur) == 0u;                     /* (no TIME slot: on time) */
    mslot(t, 0, MS_VEL, MD_TIME, 63);                   /* VEL -> TIME: 127 at AMT 63, 30 ms x 63 / 64 */
    ok &= seq_time(t, 0, cur) == 1302u;
    t->seq_idx = 0;
    t->nud = 0;
    seq_enter(t, period, (uint32_t)t->p[P_SLEN]);
    ok &= (t->nud & NUD_LATE) && t->late_at == 1302u;
    t->step[0].nudge = 8;                               /* onto its NUDGE */
    t->nud = 0;
    seq_enter(t, period, (uint32_t)t->p[P_SLEN]);
    ok &= (t->nud & NUD_LATE) && t->late_at == (uint32_t)nudge_samples(8, cur) + 1302u;
    t->step[0].nudge = 0;
    t->step[0].flags |= 1u << SF_RATCH_SH;              /* a ratchet: on time */
    ok &= seq_time(t, 0, cur) == 0u;
    mslot(t, 0, 0, 0, 0);
    t->nud = 0;
    bad += check("  TIME: VEL 127 at AMT 63 plays the step 1302 samples late, onto its NUDGE; a ratchet on time", ok);
    return bad;
}

/* ------------------------------------------------------------ SPREAD --- */
static int32_t sp_out[2 * CTL];
static void sp_run(uint32_t n, int64_t *el, int64_t *er, uint64_t *h)   /* n blocks: |L|, |R| summed, a hash */
{
    uint32_t i;
    *el = *er = 0;
    while (n--) {
        mix_block(sp_out, CTL);
        for (i = 0; i < CTL; i++) {
            *el += sp_out[2u * i] < 0 ? -(int64_t)sp_out[2u * i] : sp_out[2u * i];
            *er += sp_out[2u * i + 1u] < 0 ? -(int64_t)sp_out[2u * i + 1u] : sp_out[2u * i + 1u];
            *h = (*h ^ (uint32_t)sp_out[2u * i] ^ ((uint64_t)(uint32_t)sp_out[2u * i + 1u] << 32)) * 0x100000001B3ull;
        }
    }
}
static void sp_phrase(int32_t sprd, int32_t mode, int64_t *l1, int64_t *r1, int64_t *l2, int64_t *r2)
{
    uint64_t h = 0xCBF29CE484222325ull;
    track_t *t = &trk[0];
    uint32_t k;
    ui_power_on();
    stop_transport();
    song.master_q12 = MASTER_FULL;
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_LEVEL] = 0;
    t->p[P_VOICE] = (int16_t)mode;
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;      /* (the buses: stereo of their own) */
    t->p[P_DIST] = 0;                                   /* (DRIVE takes the mono sum: the side passes it by) */
    t->p[P_PAN] = 0;
    t->p[P_SPRD] = (int16_t)sprd;
    t->sp_alt = 0;
    memset(t->v, 0, sizeof t->v);                       /* (no release left from the phrase before, nor its */
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;   /* trace in the master's filters) */
    trk_note_on(t, 48, 100);
    sp_run(40, l1, r1, &h);
    trk_note_on(t, 55, 100);
    sp_run(40, l2, r2, &h);
    trk_note_off(t, 48);
    trk_note_off(t, 55);
}
static int spread(void)
{
    int bad = 0, ok;
    int64_t l1, r1, l2, r2;
    sp_phrase(127, V_POLY, &l1, &r1, &l2, &r2);         /* the first note hard left, the second hard right */
    ok = l1 > 0 && r1 < l1 / 100 && r2 > l2 / 4 && trk[0].spread_on;   /* (the right: the shifts' rounding) */
    go_title("VOICE 2");
    ok &= cur_page()->id[2] == P_SPRD;
    bad += check("SPREAD 127: a POLY part's first note hard left, the next right; VOICE 2's KNOB 3", ok);
    sp_phrase(0, V_POLY, &l1, &r1, &l2, &r2);           /* 0: on PAN, both sides the same, as before */
    ok = l1 > 0 && l1 == r1 && l2 == r2 && !trk[0].spread_on;
    sp_phrase(127, V_MONO, &l1, &r1, &l2, &r2);         /* MONO: no SPREAD (one voice, on PAN) */
    ok &= l1 > 0 && l1 == r1 && !trk[0].spread_on;
    bad += check("  SPREAD 0, or MONO: every voice on PAN (left and right the same), mix_part's own loop", ok);
    return bad;
}

/* ------------------------------------------------- (2.0, #36) LANE PANS --- */
static void lp_hit(uint32_t eng, uint32_t lane, int32_t kick, int32_t snare, int32_t pan, int64_t *l, int64_t *r)
{
    uint64_t h = 0;
    track_t *t = &trk[0];
    uint32_t k;
    ui_power_on();
    stop_transport();
    song.master_q12 = MASTER_FULL;
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_LEVEL] = 0;
    set_engine_of(t, eng);
    t->engine = t->eng_req;
    t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = t->p[P_DIST] = 0;
    t->p[P_LPAN0] = (int16_t)kick;
    t->p[P_LPAN0 + 1u] = (int16_t)snare;
    t->p[P_PAN] = (int16_t)pan;
    memset(t->v, 0, sizeof t->v);
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = dc_l = dc_r = dce_l = dce_r = 0;
    trk_note_on(t, DRUM_LANE_NOTE[lane], 100);
    sp_run(40, l, r, &h);
    trk_note_off(t, DRUM_LANE_NOTE[lane]);
}
static int lane_pans(void)
{
    int bad = 0, ok;
    int64_t l, r;
    lp_hit(ENGI_DRUM, 0, 0, 0, 0, &l, &r);
    ok = l > 0 && l == r && !trk[0].lpan_on;
    lp_hit(ENGI_DRUM, 0, -64, 0, 0, &l, &r);
    ok &= l > 0 && r == 0 && trk[0].lpan_on;
    lp_hit(ENGI_DRUM, 0, 63, 0, 0, &l, &r);
    ok &= r > 0 && l < r / 60;                          /* (63: 1/64 left on the far side) */
    lp_hit(ENGI_DRUM, 0, -32, 0, 0, &l, &r);
    ok &= r > l * 45 / 100 && r < l * 55 / 100;
    bad += check("LANE PANS: all at C, the kit as before (mix_part's own loop); the KICK hard left: the right silent; "
                 "right: the left; half left: the right at half", ok);
    lp_hit(ENGI_DRUM, 1, -64, 0, 0, &l, &r);            /* (the snare, at C, beside the kick hard left) */
    ok = l > 0 && l == r && trk[0].lpan_on;
    lp_hit(ENGI_DRUM, 1, -64, 63, 0, &l, &r);           /* (the snare right, the kick left: its own pan) */
    ok &= r > 0 && l < r / 60;
    lp_hit(ENGI_DRUM, 0, 0, 63, -64, &l, &r);           /* (the kick at C, the snare right, the track's PAN hard left) */
    ok &= l > 0 && r == 0 && trk[0].lpan_on;
    lp_hit(ENGI_DRUMX, 0, -64, 0, 0, &l, &r);           /* (DRUM-X: its voices' lane in s[0] as DRUM's) */
    ok &= l > 0 && r == 0 && trk[0].lpan_on;
    bad += check("  a lane at C beside them: both sides the same; the track's PAN over the lanes; DRUM-X the same", ok);
    return bad;
}

/* ---------------------------------------------------------- ENV SYNC --- */
static int env_sync(void)
{
    int bad = 0, ok;
    track_t *t;
    voice_t *v;
    uint32_t n, q4 = 72u;                               /* (the first value showing 1/4: (14 * 128 + 24) / 25) */
    const param_desc_t *d;
    char val[8];
    const char *u;
    ui_power_on();
    t = &trk[0];
    song.g[G_BPM] = 120;
    song.g[G_CLOCK] = 0;
    esync_coef(t);                                      /* OFF: the tables */
    ok = t->esync_k[0] == ENV_LIN[t->p[P_ATK] & 127] && t->esync_k[1] == ENV_EXP[t->p[P_DEC] & 127] &&
         t->esync_k[2] == ENV_EXP[t->p[P_REL] & 127] && track_desc(t, P_ATK) == &TP[P_ATK];
    t->p[P_ESYNC] = 1;
    d = track_desc(t, P_ATK);
    param_format(d, (int32_t)q4, val, &u);
    ok &= str_eq(val, "1/4") && param_turn(d, (int32_t)q4, 1) == 77 && param_turn(d, (int32_t)q4, -1) == 67;
    param_format(d, param_turn(d, (int32_t)q4, 1), val, &u);
    ok &= str_eq(val, "1/2T");
    t->p[P_ATK] = (int16_t)q4;
    t->p[P_REL] = (int16_t)q4;
    t->p[P_SUS] = 127;
    esync_coef(t);
    v = &t->v[0];
    memset(v, 0, sizeof *v);
    v->active = 1;
    v->stage = 1;
    for (n = 0; v->stage == 1 && n < 2000u; n++)       /* the attack: a beat (22050 samples, 689 blocks) */
        env_tick(t, v);
    ok &= n >= 688u && n <= 690u;
    v->stage = 3;
    for (n = 0; v->env > (1 << 24) / 100 && n < 2000u; n++)   /* the release: 99 % down after about a beat */
        env_tick(t, v);
    ok &= n >= 650u && n <= 720u;
    song.g[G_BPM] = 60;                                 /* half the tempo: twice the time */
    esync_coef(t);
    ok &= t->esync_k[0] * 2u >= ((1u << 24) * CTL / 44100u) * 2u - 2u && t->esync_k[0] <= (1u << 24) * CTL / 44100u + 1u;
    t->p[P_ESYNC] = 0;
    song.g[G_BPM] = 120;
    go_title("LFO 2");                                  /* ((2.0, D 1A) ENV DEST went into MOD: its SYNC on LFO 2) */
    ok &= cur_page()->id[3] == P_ESYNC && str_eq(TP[P_ESYNC].label, "ENV SYNC");
    bad += check("ENV SYNC: ATK / REL 1/4 at 120 BPM: a beat (22050 samples); a step a note value (1/4 -> 1/2T); twice "
                 "at half the tempo; OFF the tables; LFO 2's KNOB 4", ok);
    return bad;
}

/* ------------------------------------------------- MIDI LEARN, CHANNELS --- */
static int midi_learn_channels(void)
{
    int bad = 0, ok;
    uint32_t i, id, cc;
    char b[24];
    track_t *t = &trk[0];
    const param_desc_t *d;
    ui_power_on();
    favorites.factory[15][24] = 0xA5u;                  /* (the settings bytes bank.c freed, as on a unit) */
    memset(&favorites.factory[15][6], 0, 6);
    memset(favorites.factory[ENGI_DIGITAL], 0, sizeof favorites.factory[ENGI_DIGITAL]);
    song.g[G_ROUTE] = 0;
    sel_set(0);
    hold(B_HOME);                                       /* MENU > MIDI > MIDI LEARN: OCT+, the MENU closes */
    ui.menu_sel = MI_LEARN;
    press(B_OCTUP);
    ok = !ui.menu && ui.ml.on && !ui.ml.pick && ml_count() == 0u;
    ml_line(b);
    ok &= str_eq(b, "LEARN: TURN A KNOB");
    go_home();
    frame();
    turn(EN_K1, 1);                                     /* HOME's KNOB 1: picked */
    id = ui.ml.id;
    d = param_desc_of(eng_idx(t->eng_req), id);
    ml_line(b);
    ok &= ui.ml.pick && ui.ml.trk == 0u && id < P_COUNT && b[0] == 'T' && b[1] == '1' && str_len(b) > 12u &&
          str_eq(b + str_len(b) - 11u, ": SEND A CC");
    {
        int16_t v0 = t->p[id];
        midi_event(0xB0u, 0, 20, 127);                  /* CC20: taken for the pick, not applied */
        ok &= t->p[id] == v0 && ml_heard == 21u;
        frame();                                        /* (ml_poll) */
    }
    ok &= ml_cc_of(0, id) == 21u && !ui.ml.pick && ml_count() == 1u && ui.ml.on;
    bad += check("MIDI LEARN: MENU > MIDI > MIDI LEARN on, the MENU closed; a knob picks its parameter, the next CC is "
                 "learned to it", ok);
    press(B_OCTUP);                                     /* OCT+: done */
    ok = !ui.ml.on && !ml_arm;
    midi_event(0xB0u, 0, 20, 0);
    ok &= t->p[id] == d->min;
    midi_event(0xB0u, 0, 20, 127);
    ok &= t->p[id] == d->max;
    midi_event(0xB0u, 4, 20, 0);                        /* (channel 5, T5's: any channel heard) */
    ok &= t->p[id] == d->min;
    midi_event(0xB0u, 9, 20, 127);                      /* (channel 10: no track's, ignored) */
    ok &= t->p[id] == d->min;
    bad += check("  OCT+ done; the CC sets it over its range, on any channel MIDI IN hears, not on one it ignores", ok);
    {   /* CC74 (the standard map's brightness): learned, it reaches no more than its own parameter */
        uint32_t c74 = P_E0 + (MIDI_CC_ENG[eng_idx(t->eng_req)] >> 4) - 1u;
        ok = (MIDI_CC_ENG[eng_idx(t->eng_req)] >> 4) != 0u && c74 != P_LEVEL;
        ok &= ml_learn(74, 0, P_LEVEL) == 1u;
        t->p[c74] = 5;
        midi_event(0xB0u, 0, 74, 127);
        ok &= t->p[c74] == 5 && t->p[P_LEVEL] == TP[P_LEVEL].max;
        (void)ml_drop(74, 0, P_LEVEL);
        midi_event(0xB0u, 0, 74, 127);                  /* (cleared: the standard map's again) */
        ok &= t->p[c74] != 5;
        bad += check("  a learned CC is out of the standard map (CC74 sets the learned LEVEL, not the engine's TONE)", ok);
    }
    ml_start();                                         /* learning again: the same parameter, another CC */
    turn(EN_K1, 1);
    midi_event(0xB0u, 0, 21, 64);
    frame();
    ok = ml_cc_of(0, id) == 22u && ml_count() == 1u;
    {
        int16_t v0 = t->p[id];
        midi_event(0xB0u, 0, 20, v0 == d->min ? 127 : 0);   /* (CC20: no longer anything's) */
        ok &= t->p[id] == v0;
    }
    turn(EN_K1, 1);                                     /* CC64 (the pedal's) is never learned */
    midi_event(0xB0u, 0, 64, 127);
    ok &= !ml_heard;
    midi_event(0xB0u, 0, 64, 0);
    ok &= ml_learn(64, 0, id) == 2u && ml_learn(1, 0, id) == 2u && ml_learn(123, 0, id) == 2u;
    press(B_OCTDN);                                     /* OCT-: the pick's CC cleared */
    ok &= ui.ml.on && ml_cc_of(0, id) == 0u && ml_count() == 0u;
    bad += check("  learning the parameter again replaces its CC; CC64 / CC1 / CC123 never learned; OCT- clears the "
                 "pick's CC", ok);
    press(B_OCTUP);
    for (i = 0, ok = 1; i < ML_N; i++)                  /* ML_N at most */
        ok &= ml_learn(40 + i, i % NTRK, P_LEVEL + i) == 1u;
    ok &= ml_learn(50, 0, P_PAN) == 0u && ml_count() == ML_N;
    ok &= ml_learn(40, 5, P_PAN) == 1u && ml_count() == ML_N;   /* (a CC learned again: in place of its old one) */
    {   /* kept with the settings: engine 1's row, its byte 0 still DIGITAL's stars (moved to FM6's) */
        sks_t p;
        uint8_t row[32];
        memcpy(row, favorites.factory[ENGI_DIGITAL], 32);
        settings_export(&p);                            /* ((2.0) SKS1: the map by name, ml) */
        p.fav[ENGI_DIGITAL][0] = 1u;
        memset(favorites.factory[ENGI_DIGITAL], 0, 32);
        favorites.factory[ENGI_FM6][FM4_TO_FM6[0] / 8u] &= (uint8_t)~(1u << (FM4_TO_FM6[0] % 8u));
        settings_import(&p, (int)sizeof p);
#if FELUCCA_LEGACY
        ok &= !memcmp(favorites.factory[ENGI_DIGITAL] + 1, row + 1, 31) && !favorites.factory[ENGI_DIGITAL][0] &&
              ((favorites.factory[ENGI_FM6][FM4_TO_FM6[0] / 8u] >> (FM4_TO_FM6[0] % 8u)) & 1u) && ml_count() == ML_N;
#else                                                   /* ((2.0) DIGITAL's stars as they are: no list shows engine 1) */
        ok &= !memcmp(favorites.factory[ENGI_DIGITAL] + 1, row + 1, 31) && favorites.factory[ENGI_DIGITAL][0] == 1u &&
              !((favorites.factory[ENGI_FM6][FM4_TO_FM6[0] / 8u] >> (FM4_TO_FM6[0] % 8u)) & 1u) && ml_count() == ML_N;
        favorites.factory[ENGI_DIGITAL][0] = 0;
#endif
        favorites.factory[ENGI_FM6][FM4_TO_FM6[0] / 8u] &= (uint8_t)~(1u << (FM4_TO_FM6[0] % 8u));
    }
    hold(B_HOME);                                       /* LEARN CLEAR */
    ui.menu_sel = MI_LCLEAR;
    frame();
    ok &= str_eq(menu_vname(MI_LCLEAR, 0), "10 CC");
    press(B_OCTUP);
    ok &= ml_count() == 0u && ui.menu == 1u && str_eq(menu_vname(MI_LCLEAR, 0), "NONE");
    bad += check("  ML_N at most (LEARN FULL); kept with the settings (DIGITAL's stars byte still moved); LEARN CLEAR", ok);

    /* T1..T6 CHANNEL */
    ui.menu_sel = MI_TCH2;
    for (i = 0; i < 8u; i++)
        press(B_OCTUP);                                 /* T2: CH 10 */
    ok = trk_midi_ch(1) == 9u && str_eq(menu_vname(MI_TCH2, menu_get(MI_TCH2)), "CH 10") &&
         str_eq(menu_vname(MI_ROUTE, 0), "TRACK") && str_eq(menu_vname(MI_TCH1, menu_get(MI_TCH1)), "CH 1");
    ok &= mr_top() == 0u;                               /* (the MIDI tab: six of ten shown, the cursor's the last) */
    ui.menu_sel = MI_TCH6;
    frame();
    ok &= mr_top() == 4u && mr_n() == 6u;
    ui.menu_sel = MI_CLOCK;
    frame();
    ok &= mr_top() == 0u;
    hold(B_HOME);                                       /* (closed) */
    stop_transport();
    events_block(CTL);
    midi_event(0x90u, 9, 60, 100);                      /* channel 10: T2 */
    midi_event(0x90u, 1, 62, 100);                      /* channel 2: no track's now */
    ok &= ui.menu == 0u && midi_notes[9][60] == 2u && midi_notes[1][62] == 0u;
    menu_put(MI_TCH3, 9);                               /* T3 on 10 too: the first, T2, plays it */
    midi_event(0x90u, 9, 64, 100);
    ok &= midi_notes[9][64] == 2u;
    menu_put(MI_TCH2, 1);                               /* T2 back on 2: T3 hears 10 (its notes held there kept: T3's */
    events_block(CTL);                                  /* channel), CH1-6's names again but for T3 */
    ok &= midi_slot(9) == 2 && midi_slot(1) == 1 && str_eq(menu_vname(MI_ROUTE, 0), "TRACK");
    menu_put(MI_TCH3, 2);
    events_block(CTL);                                  /* channel 10 no track's: its held notes let go */
    ok &= midi_notes[9][60] == 0u && midi_notes[9][64] == 0u && midi_slot(9) < 0 &&
          str_eq(menu_vname(MI_ROUTE, 0), "CH1-6");
    song.g[G_ROUTE] = 1;                                /* SEL: every channel the selected track, the channels aside */
    ok &= midi_track(9) == TSEL;
    song.g[G_ROUTE] = 0;
    bad += check("CHANNELS: T2 on CH 10 hears it, its own channel let go; two on one: the first; a channel let go "
                 "releases its notes; MIDI IN says TRACK; the MIDI tab scrolls (six shown)", ok);
    menu_put(MI_TCH2, 9);
    (void)ml_learn(60, 1, P_PAN);
    settings_reset();
    ok = !favorites.factory[15][6] && !favorites.factory[15][7] && ml_count() == 0u && trk_midi_ch(1) == 1u;
    bad += check("  RESET SETTINGS: T1..T6 on channels 1..6, nothing learned", ok);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += lfo2();
    bad += euclid();
    bad += double_half();
    bad += follow_play();
    bad += momentary();
    bad += lane_mutes();
    bad += step_preview();
    bad += program_change();
    bad += mod_more();
    bad += spread();
    bad += lane_pans();
    bad += env_sync();
    bad += midi_learn_channels();
    printf(bad ? "feat13: %d failed\n" : "feat13: all ok\n", bad);
    return bad != 0;
}
