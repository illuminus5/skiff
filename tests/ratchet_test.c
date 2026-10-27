/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* RATCH, a step's ratchet (core.h SF_RATCH, seq.c seq_step / seq_ratchet), on the real sequencer, project, user
 * preset and UI sources (with the stubs of tests/ui_test.c):
 *   PLAY       x1..x4: that many note-ons in one pass of the step, in equal parts of it, each retriggered after its
 *              gate; chords and the DRUM grid's hits repeat whole; x1 slides and ties out as before, x2..x4 never;
 *              the chance is rolled once per pass for all its parts; swing; STOP and an edit to fewer end them.
 *   PROJECTS   FUN8 round trip of x1..x4 (bit 7 of the velocity and chance bytes), an x1 project packs those bytes
 *              as before, FUN7 / FUN6 images load x1.
 *   PRESETS    a user preset's note pattern keeps it (flags 8 | 16, UP_PUT too); a drum grid record has no room: x1.
 *   UI         SEQ > CHANCE KNOB 3 (RATCH x1..x4, clamped, the cursor step; dimmed on a REST, a TIE, an empty
 *              step); the piano roll and the DRUM grid draw a ratcheted step in its parts, the roll no slide line
 *              out of one.
 *   FLAM       (Skiff) RATCH's fifth value: a grace at 9/16 of the velocity, the full hit 15 ms (at most half the step)
 *              later; lane hits too; one chance roll; RATCH or FLAM; a project (bit 7 of the meta byte) and a user
 *              preset's pattern keep it.
 * The editor protocol (STEP_SET / TRACK_STEP, INFO 52 01 04) is in tests/editor_test.c. Built and run by
 * tests/run_tests.sh. */
#define FELUCCA_LEGACY 1                        /* ((2.0) its projects are SKF1 and older: the converter's build) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* track 1 alone, a one-step loop: step 0 NOTE 60 (flags), RATCH hits, GATE 64, no swing; the transport started */
static track_t *one_step(uint32_t hits, uint32_t flags)
{
    track_t *t;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 1;
    t->p[P_SGATE] = 64;
    t->p[P_SSWING] = 0;
    song.g[G_SWING] = 0;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, (uint8_t)flags, 100};
    step_set_ratchet(&t->step[0], hits);
    seq_start();
    return t;
}
static uint32_t step_len(const track_t *t, uint32_t idx) { return step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), idx); }

/* note-ons (voice starts) over `blocks` CTL blocks; at[k]: the sample of the k-th; rel: releases seen between */
static uint32_t run(track_t *t, uint32_t blocks, uint32_t *at, uint32_t nat, uint32_t *rel)
{
    uint32_t b, n = 0, v0, was = 0;
    for (b = 0; b < blocks; b++) {
        v0 = vage;
        seq_tick(t, CTL);
        for (; v0 < vage; v0++, n++)
            if (at && n < nat)
                at[n] = b * CTL;
        if (rel && was && !t->seq_n)
            (*rel)++;
        was = t->seq_n;
    }
    return n;
}
/* blocks in `passes` passes of a one-step loop, the last block before the next pass left out */
static uint32_t passes(const track_t *t, uint32_t n) { return n * step_len(t, 0) / CTL - 1u; }

static int play(void)
{
    int bad = 0, ok = 1;
    uint32_t h, n, at[8], rel, len;
    track_t *t;
    for (h = 1; h <= 4u; h++) {
        t = one_step(h, 0);
        rel = 0;
        n = run(t, passes(t, 1), at, 8, &rel);
        len = step_len(t, 0);
        ok &= n == h && rel >= h - 1u;
        for (uint32_t k = 0; k < n && k < 8u; k++) {   /* part k at k / h of the step, to the block */
            int32_t d = (int32_t)at[k] - (int32_t)(k * (len / h));
            ok &= d > -(int32_t)CTL && d <= (int32_t)CTL;
        }
        if (n != h)
            printf("ui:   RATCH x%u: %u note-ons in one pass\n", h, n);
    }
    bad += check("RATCH x1..x4: that many note-ons in a pass, in equal parts, each ended by its gate", ok);
    t = one_step(4, 0);
    bad += check("  x4 over 8 passes: 32 note-ons", run(t, passes(t, 8), 0, 0, 0) == 32u);
    t = one_step(3, 0);
    t->p[P_VOICE] = V_POLY;
    t->step[0].n = 3; t->step[0].note[1] = 64; t->step[0].note[2] = 67;
    bad += check("  a chord of 3 at x3: 9 note-ons", run(t, passes(t, 1), 0, 0, 0) == 9u);
    t = one_step(1, SF_SLIDE);
    run(t, 4, 0, 0, 0);
    ok = t->seq_hold == 1u;
    t = one_step(2, SF_SLIDE);
    run(t, passes(t, 1), 0, 0, 0);
    bad += check("  x1 slides out as before; x2 never (its last part ends at its gate)", ok && !t->seq_hold && !t->seq_n);
    t = one_step(4, 0);
    t->p[P_SLEN] = 2;
    t->step[1] = (step_t){{0}, 0, ST_TIE};
    seq_start();
    run(t, step_len(t, 0) / CTL + step_len(t, 1) / CTL - 1u, 0, 0, 0);
    bad += check("  nor ties out: a TIE after a ratcheted step holds nothing", !t->seq_n);

    t = one_step(4, 0);                               /* the DRUM grid: hits of BD (accented) and CH */
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1u << DV_KICK | 1u << DV_HATC, 1u << DV_KICK};
    step_set_ratchet(&t->step[0], 3);
    seq_start();
    n = run(t, passes(t, 1), 0, 0, 0);
    ok = 0;
    for (h = 0; h < NVOICE; h++)
        ok |= t->v[h].note == DRUM_LANE_NOTE[DV_KICK] && t->v[h].vel == 127u;
    bad += check("  DRUM grid: BD + CH at x3, 6 hits, BD accented", n == 6u && ok);
    step_set_chance(&t->step[0], 0);
    seq_start();
    bad += check("  chance 0: no part plays", run(t, passes(t, 2), 0, 0, 0) == 0u);

    t = one_step(4, 0);                               /* chance: one roll for all four parts */
    step_set_chance(&t->step[0], 50);
    {   /* a pass starts where the step's position wraps (seq_pos below a block) */
        uint32_t heard = 0, p = 0, odd = 0, v0;
        n = 0;
        while (p < 400u) {
            v0 = vage;
            seq_tick(t, CTL);
            if (t->seq_pos < CTL) {                  /* (this block started a pass: count the one before) */
                odd += p && n != 0u && n != 4u;
                heard += p ? n : 0u;
                p++;
                n = 0;
            }
            n += vage - v0;
        }
        bad += check("  chance 50 % at x4: each pass all 4 parts or none, about half", !odd && heard > 4u * 140u &&
                     heard < 4u * 260u);
    }

    t = one_step(2, 0);                               /* swing: a step's parts stay inside its swung length */
    t->p[P_SLEN] = 2;
    t->p[P_SSWING] = SWING_MAX;
    t->step[1] = t->step[0];
    seq_start();
    n = run(t, (step_len(t, 0) + step_len(t, 1)) / CTL - 1u, at, 8, 0);
    bad += check("  SWING 100: x2 on a long and a short step, 4 note-ons, the short step's within it",
                 n == 4u && at[1] < step_len(t, 0) && at[2] >= step_len(t, 0) - CTL &&
                 at[3] - at[2] <= step_len(t, 1) / 2u + CTL);

    t = one_step(4, 0);
    run(t, 2, 0, 0, 0);
    ok = t->rat_left == 3u;
    seq_stop();
    bad += check("  STOP: no part left", ok && !t->rat_left && !t->seq_n);
    t = one_step(4, 0);
    run(t, 2, 0, 0, 0);
    step_set_ratchet(&t->step[0], 1);
    bad += check("  an edit to x1 while it plays ends the parts", run(t, passes(t, 1) - 2u, 0, 0, 0) == 0u && !t->rat_left);
    seq_stop();
    return bad;
}

static int projects(void)
{
    int bad = 0, ok = 1;
    project_t before, after;
    project_store_t packed;
    uint32_t i, k, pos;
    track_t *t;
    ui_power_on();
    t = &trk[0];
    for (i = 0; i < 8u; i++) {
        t->step[i] = (step_t){{(uint8_t)(60 + i)}, 1, ST_NOTE, i & 1u ? SF_ACCENT : 0u, i & 2u ? 127u : 90u};
        step_set_ratchet(&t->step[i], 1u + i % 4u);
        step_set_chance(&t->step[i], i < 4u ? 100u : i == 4u ? 0u : 30u);
    }
    trk[3].step[5].hit = 0x81; trk[3].step[5].acc = 0x80; trk[3].step[5].time = ST_NOTE;
    step_set_ratchet(&trk[3].step[5], 4);
    project_capture(&before);
    proj_power_on(&before, PROJ_NT); before.sum = proj_sum(&before);   /* ((Skiff phase 6) SKF1 holds four tracks: the rest
                                                                        * come back as at power-on) */
    ok = proj_pack(&packed, &before) && proj_import(&after, &packed, sizeof packed) && !memcmp(&before, &after, sizeof before);
    for (i = 0; ok && i < 8u; i++)
        ok &= step_ratchet(&after.t[0].step[i]) == 1u + i % 4u && step_chance(&after.t[0].step[i]) == step_chance(&before.t[0].step[i]) &&
              after.t[0].step[i].vel == before.t[0].step[i].vel;
    bad += check("FUN8: x1..x4 round trip with their velocity, accent, chance (0 and 30 %) and lane hits", ok &&
                 step_ratchet(&after.t[3].step[5]) == 4u && after.t[3].step[5].hit == 0x81);
    pos = 68u + PROJ_NP + 2u + 3u * 9u;              /* step 3 of track 1: x4 -> bit 7 of its velocity and chance */
    bad += check("  x4 is bit 7 of the velocity and the chance bytes", (packed.raw[pos + 5] & 128u) && (packed.raw[pos + 8] & 128u) &&
                 (packed.raw[pos + 5] & 127u) == before.t[0].step[3].vel);
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NSTEP; i++)
            trk[k].step[i].flags &= (uint8_t)~SF_RATCH;
    project_capture(&before);
    proj_pack(&packed, &before);
    ok = 1;
    for (k = 0, pos = 68u; k < PROJ_NT; k++, pos += PROJ_NP + 2u + NSTEP * 9u + PROJ_NUDGE_BYTES)   /* ((Skiff) SKF1) */
        for (i = 0; i < NSTEP; i++)
            ok &= packed.raw[pos + PROJ_NP + 2u + i * 9u + 5u] < 128u && packed.raw[pos + PROJ_NP + 2u + i * 9u + 8u] < 128u;
    bad += check("  a project without ratchets: every velocity and chance byte below 128, as firmware before wrote", ok);
    {   /* a FUN7 image (its tracks without patches) as firmware before RATCH wrote it: x1 */
        project_store_t old;
        uint32_t sum, magic = PROJ_MAGIC_V7, size = PROJ_STORE_V7;
        project_t fun7;
        uint32_t src = 68u, dst = 68u, n = PROJ_NP + 2u + NSTEP * 9u;
        memcpy(&old, &packed, sizeof old);
        for (k = 0; k < PROJ_NT; k++, src += n + PROJ_NUDGE_BYTES, dst += n)   /* ((Skiff) SKF1's NUDGE planes out) */
            memmove(old.raw + dst, packed.raw + src, n);
        memmove(old.raw + dst, packed.raw + src, sizeof(chain_config_t) + sizeof(motion_store_t));
        memcpy(old.raw, &magic, 4); memcpy(old.raw + 4, &size, 4);
        memset(old.raw + PROJ_STORE_V7 - 4u - PROJ_NAME_LEN, 0, PROJ_NAME_LEN);
        sum = proj_hash(old.raw, PROJ_STORE_V7 - 4u); memcpy(old.raw + PROJ_STORE_V7 - 4u, &sum, 4);
        ok = proj_import(&fun7, &old, PROJ_STORE_V7);
        for (k = 0; ok && k < PROJ_NT; k++)
            for (i = 0; i < NSTEP; i++)
                ok &= step_ratchet(&fun7.t[k].step[i]) == 1u;
        bad += check("  FUN7 of before: every step x1", ok && fun7.t[0].step[1].flags == SF_ACCENT);
    }
    {   /* FUN6 (10-byte steps): only accent and slide come over, x1 */
        project_v6_t v6;
        memset(&v6, 0, sizeof v6);
        v6.magic = PROJ_MAGIC_V6; v6.size = sizeof v6; v6.parts = PROJ_PARTS; v6.phys = PROJ_PHYS;
        memcpy(v6.g, before.g, sizeof v6.g);
        chain_defaults(&v6.chain);
        for (k = 0; k < PROJ_NT; k++) {              /* ((Skiff phase 6) FUN6 holds four tracks) */
            for (i = 0; i < 69u; i++) v6.t[k].p[i] = TP[i < 61u ? i : i - 61u + P_E0].def;
            v6.t[k].step[0] = (step10_t){{60}, 1, ST_NOTE, 0xFBu, 100, 0, 0};   /* (stray high bits) */
        }
        v6.sum = proj_hash(&v6, sizeof v6 - 4u);
        bad += check("  FUN6: x1, accent and slide kept", proj_import(&after, &v6, sizeof v6) &&
                     step_ratchet(&after.t[0].step[0]) == 1u && after.t[0].step[0].flags == (SF_ACCENT | SF_SLIDE));
    }
    ui_power_on();                                   /* a real save and load */
    step_set_ratchet(&trk[1].step[7], 3);
    trk[1].step[7].time = ST_NOTE; trk[1].step[7].n = 1; trk[1].step[7].note[0] = 50;
    project_save(2);
    bank_put(1, 1, 0);                               /* (phase 5: T2 onto a copy, B: the edit its) */
    step_set_ratchet(&trk[1].step[7], 1);
    project_load(2);
    bad += check("  project save and load keep the ratchet", step_ratchet(&trk[1].step[7]) == 3u);
    return bad;
}

static int presets(void)
{
    int bad = 0;
    up_rec_t r;
    uint8_t a[400];
    uint32_t slot, i, k, n;
    track_t *t;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    for (i = 0; i < 16u; i++)
        t->step[i] = (step_t){{(uint8_t)(48 + i)}, 1, ST_NOTE, 0, 100};
    step_set_ratchet(&t->step[2], 3);
    t->step[5].flags = SF_ACCENT;
    step_set_ratchet(&t->step[5], 4);
    memset(&r, 0, sizeof r);
    for (i = 0; i < 16u; i++) {                      /* (the editor's 16 x (note, flags), as it sends them) */
        r.note[i] = t->step[i].note[0];
        r.flags[i] = t->step[i].flags;
    }
    k = 0;                                           /* UP_PUT: slot, engine, name, values, 16 x (note, flags) */
    a[k++] = 3; a[k++] = (uint8_t)t->eng_req;
    memcpy(a + k, "RAT", 4); k += 4;
    for (i = 0; i < P_COUNT; i++) { a[k++] = (uint8_t)((t->p[i] + 8192) & 127); a[k++] = (uint8_t)((t->p[i] + 8192) >> 7); }
    for (i = 0; i < 16u; i++) { a[k++] = r.note[i]; a[k++] = r.flags[i]; }
    n = k;
    memset(&r, 0, sizeof r);
    bad += check("UP_PUT keeps a pattern field's ratchet (the editor's format)", !up_parse(a, n, &r, &slot) &&
                 r.flags[2] == 2u << SF_RATCH_SH && r.flags[5] == (SF_ACCENT | 3u << SF_RATCH_SH));
    up_store(4, "SOUND");
    bad += check("  (Skiff) SAVE > USER stores the sound only: no pattern, no grid", up_used(4) && !up_grid(up_rec(4)) &&
                 !up_rec(4)->note[2] && !up_rec(4)->flags[2]);
    return bad;
}

/* pixels of colour c in card slot k (the cards' row) */
static uint32_t card_px(uint32_t k, uint16_t c)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = Y_LABEL; y < Y_LABEL + CARD_H; y++)
        for (x = CARD_X(k); x < CARD_X(k) + CARD_W; x++)
            n += swap16(host_screen[y * 240 + x]) == c;
    return n;
}
/* the slide line's pixels (TEXT) in the gap after step column i of the piano roll, between rows lo and hi */
static uint32_t pr_slide_px(uint32_t i, int32_t lo, int32_t hi)
{
    uint32_t n = 0;
    int32_t x, y, x0 = PR_X0 + (int32_t)i * PR_CW;
    for (y = Y_GRAPH + pr_row_y(hi); y <= Y_GRAPH + pr_row_y(lo) + PR_RH; y++)
        for (x = x0 + 11; x <= x0 + 13; x += 2)          /* (x0 + 12 the step line, x0 + 14 the next bars) */
            n += swap16(host_screen[y * 240 + x]) == T_TEXT;
    return n;
}

static int screens(void)
{
    int bad = 0, ok;
    track_t *t;
    uint32_t x, y;
    ui_power_on();
    t = TSEL;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};   /* ((2.0) FEEL: a step with a note) */
    go_page(GR_FEEL);
    ui.cursor = 0;
    turn(EN_K2, 2);
    ok = step_ratchet(&t->step[0]) == 3u && step_ratchet(&t->step[1]) == 1u;
    turn(EN_K2, 1);
    ok &= step_ratchet(&t->step[0]) == 4u && !(t->step[0].flags & SF_FLAM);
    turn(EN_K2, 100);                                /* (Skiff) past x4: FLAM, on x1, the card says so */
    frame();
    ok &= step_rflam(&t->step[0]) == 5u && step_ratchet(&t->step[0]) == 1u && strstr(ui.col[1], "|FLAM|") != 0;
    turn(EN_K2, -100);
    bad += check("SEQ > FEEL KNOB 2: RATCH of the cursor step, x1..x4, then (Skiff) FLAM", ok &&
                 step_rflam(&t->step[0]) == 1u && step_chance(&t->step[0]) == 100u);
    {                                                /* RATCH dimmed where it does nothing: REST, TIE, empty */
        uint32_t dim[4], thm[4], k;
        static const step_t kind[4] = {
            {{60}, 1, ST_NOTE, 0, 100}, {{0}, 0, ST_REST, 0, 0}, {{0}, 0, ST_TIE, 0, 0}, {{0}, 0, ST_NOTE, 0, 0}};
        for (k = 0; k < 4u; k++) {
            t->step[0] = kind[k];
            step_set_ratchet(&t->step[0], 2);
            ui.hot_t = 0;                            /* ((U2) BLOCKS: a hot tile draws its value in INK) */
            memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
            dim[k] = card_px(1, T_DIM); thm[k] = card_px(1, T_THEME);
        }
        t->step[0] = kind[3]; t->step[0].hit = 1u;   /* a NOTE step of lane hits only: it plays */
        step_set_ratchet(&t->step[0], 2);
        ui.hot_t = 0;
        memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
        ok = card_px(1, T_DIM) == dim[0] && card_px(1, T_THEME) == thm[0];
        for (k = 1; k < 4u; k++)
            ok &= dim[k] > dim[0] && thm[k] < thm[0];
        bad += check("  RATCH shows dimmed on a REST, a TIE or an empty step (a note or a hit: not)", ok);
        t->step[0] = (step_t){{0}, 0, ST_REST, 0, 0};
    }
    ui_power_on();                                   /* the piano roll: x2 in two bars, the middle column empty */
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->step[1] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[3] = t->step[1];
    step_set_ratchet(&t->step[1], 2);
    go_page(GR_ROLL); ui.cursor = 8; ui.bank = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    y = Y_GRAPH + (uint32_t)graph_pad() + (uint32_t)pr_row_y(60) + 2u;   /* ((U2) BLOCKS: the panel's pad) */
    x = PR_X0 + 1u * PR_CW;
    ok = pr_is_bar(x + 3u, y) && pr_is_bar(x + 9u, y) && !pr_is_bar(x + 6u, y);
    x = PR_X0 + 3u * PR_CW;
    bad += check("  the piano roll draws an x2 step in two parts (x1 whole)", ok && pr_is_bar(x + 6u, y));
    t->step[4] = (step_t){{64}, 1, ST_NOTE, 0, 100};   /* SLIDE into a note: the line, but not from x2 */
    t->step[3].flags = SF_SLIDE;
    t->step[1].flags |= SF_SLIDE;                     /* (x2 kept) */
    t->step[2] = t->step[4];
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  a slide line from an x1 SLIDE step, none from an x2 one", pr_slide_px(3, 60, 64) > 0u &&
                 pr_slide_px(1, 60, 64) == 0u);
    t->step[1].flags &= (uint8_t)~SF_SLIDE; t->step[3].flags = 0;
    t->step[2] = t->step[4] = (step_t){{0}, 0, ST_REST, 0, 0};
    set_engine_of(t, ENGI_DRUM);                     /* the grid: x3 in three bars */
    t->engine = t->eng_req;
    t->step[1] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1u};
    step_set_ratchet(&t->step[1], 3);
    ui.lane = 1;
    go_page(GR_ROLL); ui.cursor = 8; ui.bank = 0;   /* ((2.0) NOTES: a drum track's grid) */
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    y = Y_GRAPH + (uint32_t)graph_pad() + 18u;      /* lane 0's hits: y 18 .. 21 of the grid, the grid the panel's pad
                                                       * down ((1.2.1, #10) on STEP as on CHANCE and PLAY) */
    x = 40u + 12u;
    bad += check("  the DRUM grid draws an x3 hit in three parts", pr_is_bar(x, y) && !pr_is_bar(x + 2u, y) &&
                 pr_is_bar(x + 3u, y) && !pr_is_bar(x + 5u, y) && pr_is_bar(x + 6u, y));
    return bad;
}

/* (Skiff) FLAM (core.h SF_FLAM, seq.c seq_step / seq_ratchet): two note-ons a pass, the grace at the step at 9/16 of
 * the velocity, the full one FLAM_SAMPLES (15 ms) later; at most half a short step; a lane hit flams too; the chance
 * rolled once for both; RATCH and FLAM one or the other; a project (bit 7 of the meta byte; with RATCH x2: refused)
 * and a user preset's pattern keep it */
static uint32_t run_vel(track_t *t, uint32_t blocks, uint32_t *at, uint8_t *vel, uint32_t nat)
{
    uint32_t b, n = 0, v0, i;
    for (b = 0; b < blocks; b++) {
        v0 = vage;
        seq_tick(t, CTL);
        for (i = 0; i < NVOICE; i++)
            if (t->v[i].active && t->v[i].age > v0 && n < nat) {
                at[n] = b * CTL;
                vel[n++] = t->v[i].mvel;
            }
    }
    return n;
}
static int flam(void)
{
    int bad = 0, ok;
    uint32_t at[8], n, pos;
    uint8_t vel[8];
    project_t before, after;
    project_store_t packed;
    track_t *t = one_step(1, 0);
    step_set_rflam(&t->step[0], 5);
    ok = (t->step[0].flags & SF_FLAM) && step_ratchet(&t->step[0]) == 1u;
    n = run_vel(t, passes(t, 1), at, vel, 8);
    ok &= n == 2u && at[0] < CTL && at[1] + CTL > FLAM_SAMPLES && at[1] <= FLAM_SAMPLES + CTL &&
          vel[0] == 100u * 9u / 16u + 1u && vel[1] == 100u;
    bad += check("FLAM: the grace at the step (9/16 of the velocity), the full hit 15 ms later", ok);
    bad += check("  a step shorter than 30 ms: the full hit at its half", flam_at(t, 800) == 400u &&
                 flam_at(t, 40000) == FLAM_SAMPLES);
    t = one_step(1, 0);
    t->step[0] = (step_t){{0}, 0, ST_NOTE, 0, 100};
    t->step[0].hit = 1u << 1;                        /* a lane hit flams the same */
    step_set_rflam(&t->step[0], 5);
    n = run_vel(t, passes(t, 1), at, vel, 8);
    bad += check("  a lane hit: grace and full hit", n == 2u && vel[0] < vel[1]);
    step_set_chance(&t->step[0], 0);
    bad += check("  chance 0: neither", run(t, passes(t, 2), 0, 0, 0) == 0u);
    step_set_rflam(&t->step[0], 5);
    step_set_ratchet(&t->step[0], 3);
    ok = step_ratchet(&t->step[0]) == 3u && !(t->step[0].flags & SF_FLAM);
    step_set_rflam(&t->step[0], 5);
    bad += check("  RATCH or FLAM: x3 drops the flam, FLAM sets x1", ok && step_ratchet(&t->step[0]) == 1u &&
                 step_rflam(&t->step[0]) == 5u);
    ui_power_on();                                   /* a project: bit 7 of the step's meta byte */
    trk[0].step[3] = (step_t){{62}, 1, ST_NOTE, SF_ACCENT, 90};
    step_set_rflam(&trk[0].step[3], 5);
    project_capture(&before);
    proj_power_on(&before, PROJ_NT); before.sum = proj_sum(&before);   /* ((Skiff phase 6) four tracks in SKF1) */
    ok = proj_pack(&packed, &before) && proj_import(&after, &packed, sizeof packed) && !memcmp(&before, &after, sizeof before);
    pos = 68u + PROJ_NP + 2u + 3u * 9u;              /* step 3 of track 1 */
    ok &= (packed.raw[pos + 4] & 128u) && (after.t[0].step[3].flags & SF_FLAM) && (after.t[0].step[3].flags & SF_ACCENT);
    packed.raw[pos + 5] |= 128u;                     /* FLAM with RATCH x2: no such step, refused */
    {
        uint32_t sum = proj_hash(packed.raw, PROJ_STORE_SIZE - 4u);
        memcpy(packed.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
    }
    bad += check("  a project keeps it (bit 7 of the meta byte); FLAM with RATCH x2 is refused", ok &&
                 !proj_import(&after, &packed, sizeof packed));
    return bad;
}

/* (Skiff) NUDGE (core.h step_t.nudge, seq.c seq_enter / seq_nudge): a NOTE step plays its nudge (64ths of its length)
 * later or earlier than its step; early ones in the step before, in its direction (not at PLAY: the first step on the
 * grid); a trig condition on an early step counts its own pass; RATCH parts and a FLAM's full hit from where it played.
 * SEQ > PLAY: KNOB 2 the cursor step's nudge (-32..31), KNOB 3 DIR; the roll draws a nudged note where it plays */
static track_t *four_steps(uint32_t dir)              /* track 1: LEN 4, NOTE 60 62 64 65, GATE 64, no swing; started */
{
    static const uint8_t N[4] = {60, 62, 64, 65};
    track_t *t = one_step(1, 0);
    uint32_t i;
    seq_stop();
    t->p[P_SLEN] = 4;
    t->p[P_DIR] = (int16_t)dir;
    for (i = 0; i < 4u; i++)
        t->step[i] = (step_t){{N[i]}, 1, ST_NOTE, 0, 100};
    return t;
}
static int at_ok(uint32_t at, uint32_t want, uint32_t slack) { return at >= want && at < want + slack; }
static int nudge(void)
{
    int bad = 0, ok;
    uint32_t at[8], n, len, d16, e16, e20;
    track_t *t = four_steps(DIR_FWD);
    len = step_len(t, 0);
    d16 = (uint32_t)nudge_samples(16, len);
    e16 = d16;
    e20 = (uint32_t)nudge_samples(20, len);
    t->step[1].nudge = 16;
    seq_start();
    n = run(t, 4u * len / CTL, at, 8, 0);
    ok = n == 4u && at[0] < CTL && at_ok(at[1], len + d16, CTL) && at_ok(at[2], 2u * len, CTL) && at_ok(at[3], 3u * len, CTL);
    bad += check("NUDGE +16 on step 2: its note 16/64 of a step late, the others on the grid", ok);
    t = four_steps(DIR_FWD);
    t->step[2].nudge = -16;
    seq_start();
    n = run(t, 4u * len / CTL, at, 8, 0);
    bad += check("  -16 on step 3: 16/64 early, in step 2", n == 4u && at_ok(at[1], len, CTL) &&
                 at_ok(at[2], 2u * len - e16, CTL) && at_ok(at[3], 3u * len, CTL));
    t = four_steps(DIR_FWD);
    t->step[0].nudge = -20;
    seq_start();
    n = run(t, 5u * len / CTL, at, 8, 0);
    bad += check("  -20 on step 1: on the grid at PLAY, then early, at the end of the pass before", n == 5u && at[0] < CTL &&
                 at_ok(at[4], 4u * len - e20, CTL));
    t = four_steps(DIR_REV);                         /* REV: 4 3 2 1, step 2 comes after step 3 */
    t->step[1].nudge = -16;
    seq_start();
    n = run(t, 4u * len / CTL, at, 8, 0);
    bad += check("  REV: -16 on step 2 plays early in step 3, the step before it in REV", n == 4u &&
                 at_ok(at[2], 2u * len - e16, CTL) && at_ok(at[3], 3u * len, CTL));
    t = four_steps(DIR_FWD);                         /* 1:2 on an early step 1: passes 1 and 3, by its own pass */
    t->step[1] = t->step[2] = t->step[3] = (step_t){{0}, 0, ST_REST, 0, 0};
    t->step[0].nudge = -20;
    t->step[0].cond = TC_1_2;
    seq_start();
    n = run(t, 10u * len / CTL, at, 8, 0);
    bad += check("  1:2 on an early step 1: passes 1 and 3 (it counts the pass it opens)", n == 2u && at[0] < CTL &&
                 at_ok(at[1], 8u * len - e20, CTL));
    t = four_steps(DIR_FWD);                         /* RATCH x2 at +16: its parts from where it plays */
    t->step[1].nudge = 16;
    step_set_ratchet(&t->step[1], 2);
    seq_start();
    n = run(t, 3u * len / CTL, at, 8, 0);
    bad += check("  RATCH x2 at +16: both parts from where the step plays", n == 4u && at_ok(at[1], len + d16, CTL) &&
                 at_ok(at[2], len + d16 + len / 2u, 2u * CTL));
    t = four_steps(DIR_FWD);                         /* FLAM at -16: the full hit 15 ms after its grace */
    t->step[2].nudge = -16;
    step_set_rflam(&t->step[2], 5);
    seq_start();
    n = run(t, 3u * len / CTL, at, 8, 0);
    bad += check("  FLAM at -16: the grace early, the full hit 15 ms after it", n == 4u && at_ok(at[2], 2u * len - e16, CTL) &&
                 at_ok(at[3], 2u * len - e16 + FLAM_SAMPLES, 2u * CTL));
    t = four_steps(DIR_FWD);                         /* a REST or a TIE keeps to the grid */
    t->step[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[1].nudge = -30;
    t->step[2].nudge = 0;
    seq_start();
    n = run(t, 4u * len / CTL, at, 8, 0);
    bad += check("  a TIE's nudge does nothing (the note before held on the grid)", n == 3u && at_ok(at[1], 2u * len, CTL));
    seq_stop();
    {   /* (2.0) FEEL's KNOB 3 the cursor step's nudge, PATTERN's PRESETS DIR (PLAY's before); the card; the roll draws
         * it where it plays */
        uint32_t x, y;
        ui_power_on();
        t = TSEL;
        track_defaults_steps(t);
        t->p[P_SLEN] = 16;
        t->step[1] = (step_t){{60}, 1, ST_NOTE, 0, 100};
        t->step[3] = t->step[1];
        go_page(GR_FEEL);
        ui.cursor = 1; ui.bank = 0;
        turn(EN_K3, 3);
        ok = t->step[1].nudge == 3;
        turn(EN_K3, -100);
        ok &= t->step[1].nudge == NUDGE_MIN;
        turn(EN_K3, 100);
        ok &= t->step[1].nudge == NUDGE_MAX && !t->step[3].nudge;
        frame();
        ok &= strstr(ui.col[2], "|+31|") != 0;
        go_page(GR_STEPS);
        turn(EN_PRESET, 2);
        bad += check("SEQ > FEEL: KNOB 3 the cursor step's NUDGE (-32..31), the card +31 /64; PATTERN's PRESETS DIR",
                     ok && t->p[P_DIR] == DIR_PING);
        t->step[1].nudge = NUDGE_MIN;
        t->p[P_DIR] = DIR_FWD;
        go_page(GR_ROLL); ui.cursor = 8;
        memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
        y = Y_GRAPH + (uint32_t)graph_pad() + (uint32_t)pr_row_y(60) + 2u;   /* ((U2) BLOCKS: the panel's pad) */
        x = PR_X0 + 1u * PR_CW;
        ok = pr_is_bar(x - 3u, y) && !pr_is_bar(x + 6u, y);   /* -32: half a column left (x - 4 .. x + 4) */
        x = PR_X0 + 3u * PR_CW;
        bad += check("  the piano roll draws a note where it plays (-32: half a column early)", ok && pr_is_bar(x + 6u, y));
    }
    return bad;
}

int main(void)
{
    int bad = play() + projects() + presets() + screens() + flam() + nudge();
    printf("%s\n", bad ? "RATCHET TEST FAILED" : "ratchet tests passed");
    return bad != 0;
}
