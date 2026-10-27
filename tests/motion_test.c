/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Real sequencer/project/UI sources: motion lifecycle and compact migration. */
#define FELUCCA_LEGACY 1                        /* ((2.0) its projects are SKF1 and older: the converter's build) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

static int motion_recording(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_REV] = 23; sel_set(0); song.rec = 1;
    seq_start(); seq_tick(t, CTL); /* first real step */
    t->p[P_REV] = 92;
    bad += check("motion records the selected armed track at its current step", !motion_capture(t, P_REV, 92) &&
        motion.count == 1u && motion.event[0].place == 0u && motion.event[0].value == 92);
    t->p[P_REV] = 110; motion_capture(t, P_REV, 110);
    bad += check("same step/parameter overwrites rather than consuming capacity", motion.count == 1u && motion.event[0].value == 110);
    bad += check("sounding values never replace the original patch base", t->p[P_REV] == 110 && motion_base_value(t, P_REV) == 23);
    project_t q; project_store_t packed;
    project_capture(&q);
    bad += check("project snapshot saves base + independent events while sounding", q.t[0].p[P_REV] == 23 && q.motion.event[0].value == 110 &&
        proj_pack(&packed, &q) && sizeof packed == 3840u);
    seq_stop();
    bad += check("stop before another step restores the original parameter", t->p[P_REV] == 23);
    seq_start(); seq_tick(t, CTL);
    bad += check("recorded motion plays back at step zero", t->p[P_REV] == 110);
    motion_set_enabled(t, 0);
    bad += check("bypass restores base without deleting automation", t->p[P_REV] == 23 && motion_count(t) == 1u && !motion_enabled(t));
    motion_set_enabled(t, 1); motion_step(t, 0, &motion, 1);
    song.rec = 0; t->p[P_REV] = 37; motion_capture(t, P_REV, 37); seq_stop();
    bad += check("manual edits outside recording become the new base", t->p[P_REV] == 37);
    song.rec = 3; seq_start(); seq_tick(t, CTL);
    trk[1].p[P_REV] = 45; motion_capture(&trk[1], P_REV, 45);
    bad += check("only the selected track captures knob motion", motion_count(&trk[1]) == 0u);
    bad += check("transport and engine-switch parameters cannot be captured", !motion_param(P_SLEN) && !motion_param(P_AMODE) &&
        motion_set_event(t, 0, P_SLEN, 8) == 1);
    seq_stop(); motion_clear(t);
    t->p[P_REV] = 42; song.rec = 1; seq_start(); seq_tick(t, CTL);
    motion_clear(t); t->p[P_REV] = 70; motion_capture(t, P_REV, 70); seq_stop();
    bad += check("clearing then recording mid-play keeps the correct new base", t->p[P_REV] == 42);
    return bad;
}
static int motion_capacity(void)
{
    int bad = 0; ui_power_on();
    for (uint32_t i = 0; i < MOTION_MAX; i++) bad += motion_set_event(&trk[0], i, P_REV, (int16_t)i);
    motion_store_t saved = motion;
    bad += check("full event pool refuses append without overwriting earlier events", motion_set_event(&trk[1], 0, P_REV, 90) == 2 &&
        !memcmp(&motion, &saved, sizeof saved));
    bad += check("full pool still permits a targeted overwrite", !motion_set_event(&trk[0], 12, P_REV, 100) && motion.count == MOTION_MAX);
    motion_delete_event(&trk[0], 12, P_REV);
    bad += check("deleting one event releases one slot", motion.count == MOTION_MAX - 1u && !motion_set_event(&trk[1], 0, P_REV, 90));
    saved = motion; motion_store_t invalid = motion; invalid.count = MOTION_MAX + 1;
    bad += check("invalid restore leaves the pool intact", motion_replace_track(&trk[0], &invalid) != 0 && !memcmp(&motion, &saved, sizeof saved));
    motion_clear(&trk[0]);
    bad += check("track clear keeps every other track's events", motion.count == 1u && motion.event[0].place >> 6 == 1u);
    return bad;
}
static int probability_playback(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    step_t s = {{60, 64, 0, 0}, 2, ST_NOTE, 0, 90, 1u << DV_KICK, 0, 0};
    bad += check("zero-initialized probability remains legacy 100 percent", step_chance(&s) == 100u);
    step_set_chance(&s, 0); seq_step(t, &s, div_samples(2), 0);
    bad += check("zero percent suppresses the whole chord and drum hits", step_chance(&s) == 0u && !t->seq_n);
    step_set_chance(&s, 100); seq_step(t, &s, div_samples(2), 0);
    bad += check("100 percent plays all chord notes and drum hits", t->seq_n == 3u);
    seq_release(t); step_set_chance(&s, 50); uint32_t heard = 0;
    for (uint32_t i = 0; i < 1000u; i++) { seq_step(t, &s, div_samples(2), 0); heard += t->seq_n != 0; seq_release(t); }
    bad += check("chance is evaluated each repeat with one decision per step", heard > 350u && heard < 650u);
    return bad;
}
static int compact_project(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->step[3] = (step_t){{60, 67}, 2, ST_NOTE, SF_ACCENT, 110, 0x81, 0x80, 0};
    step_set_chance(&t->step[3], 25); t->p[P_FM1_LEVEL] = 80; t->p[P_ED_FLT] = -50;
    motion_set_event(t, 3, P_REV, 110);
    project_t before, after; project_store_t packed, corrupt;
    project_capture(&before);
    proj_power_on(&before, PROJ_NT); before.sum = proj_sum(&before);   /* ((Skiff phase 6) SKF1 holds four tracks: the rest
                                                                        * come back as at power-on) */
    bad += check("SKF1 fits the retained and flash extent", sizeof(proj_slot) == 4u * 3840u && proj_pack(&packed, &before));
    bad += check("SKF1 round trip preserves signed values/FM params/probability/motion", proj_import(&after, &packed, sizeof packed) &&
        !memcmp(&before, &after, sizeof before));
    corrupt = packed; corrupt.raw[112] ^= 1u;
    bad += check("FUN7 torn or corrupted payload is refused", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; corrupt.raw[68] = 255; uint32_t sum = proj_hash(corrupt.raw, sizeof corrupt - 4u);
    memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("compact parameter range is validated even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    corrupt = packed; uint32_t probability_byte = 68u + PROJ_NP + 2u + 8u; corrupt.raw[probability_byte] = 127;
    sum = proj_hash(corrupt.raw, sizeof corrupt - 4u); memcpy(corrupt.raw + sizeof corrupt - 4u, &sum, 4);
    bad += check("invalid probability is refused even with a correct hash", !proj_import(&after, &corrupt, sizeof corrupt));
    project_v6_t old; memset(&old, 0, sizeof old); old.magic = PROJ_MAGIC_V6; old.size = sizeof old;
    memcpy(old.g, before.g, sizeof old.g); old.parts = PROJ_PARTS; old.phys = PROJ_PHYS;
    for (uint32_t k = 0; k < PROJ_NT; k++) {
        for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = before.t[k].p[j];
        for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = before.t[k].p[P_E0 + j];
        old.t[k].engine = before.t[k].engine; old.t[k].preset = before.t[k].preset;
        for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &before.t[k].step[j], sizeof(step10_t));
    }
    old.chain = before.chain; old.sum = proj_hash(&old, sizeof old - 4u);
    bad += check("real FUN6 disk image migrates with FM defaults/100% chance/no motion", proj_import(&after, &old, sizeof old) &&
        after.t[0].p[P_E0] == before.t[0].p[P_E0] && after.t[0].p[P_ED_FLT] == -50 &&
        after.t[0].p[P_FM1_LEVEL] == 127 && !after.motion.count && step_chance(&after.t[0].step[3]) == 100u);
    bad += check("migrated FUN6 can be written as fixed-size FUN7", proj_pack(&packed, &after));
    project_save(1); motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    project_load(1);
    bad += check("(phase 5) a project load: its sound (operator settings); the pattern as it is (edits since: kept)",
        t->p[P_FM1_LEVEL] == 80 && !motion_count(t) && step_chance(&t->step[3]) == 100u);
    step_set_chance(&t->step[3], 25); motion_set_event(t, 3, P_REV, 110);
    project_save(1);
    bank_put(0, 1, 0);                                   /* (T1 onto a copy, B: the edits its) */
    motion_clear(t); t->p[P_FM1_LEVEL] = 127; step_set_chance(&t->step[3], 100);
    project_load(1);
    bad += check("actual project save/load restores motion, chance and operator settings (its pattern, as saved)",
        motion_count(t) == 1u && step_chance(&t->step[3]) == 25u && t->p[P_FM1_LEVEL] == 80);
    return bad;
}
/* a project of 89 parameters (before the chord keys P_CHRD / P_VOIC; in a FUN9 frame): the engine's values at
 * 81..88, motion ids from 81 on for E0..E7 (m: its events as that firmware numbered them) */
static void pack_fun7_89(project_store_t *out, const project_t *q, const motion_store_t *m)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i, magic = PROJ_MAGIC_V9, size = PROJ_STORE_V9, sum;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = 89;
    for (t = 0; t < PROJ_NT; t++) {
        for (i = 0; i < 89u; i++) b[pos++] = (uint8_t)(q->t[t].p[i < 81u ? i : P_E0 + i - 81u] + 64);
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++) {
            const step_t *s = &q->t[t].step[i];
            memcpy(b + pos, s->note, 4); pos += 4;
            b[pos++] = (uint8_t)(s->n | s->time << 3 | s->flags << 5);
            b[pos++] = s->vel; b[pos++] = s->hit; b[pos++] = s->acc; b[pos++] = s->probability;
        }
    }
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    proj_mot_put(b + pos, m);                     /* (the stored block: its own layout, not RAM's) */
    sum = proj_hash(b, PROJ_STORE_V9 - 4u); memcpy(b + PROJ_STORE_V9 - 4u, &sum, 4);
}
static int fun7_89(void)
{
    int bad = 0, ok; ui_power_on(); track_t *t = &trk[0];
    project_t before, after; project_store_t old; motion_store_t m;
    uint32_t i, k;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < 8u; i++) trk[k].p[P_E0 + i] = (int16_t)(ENGINES[trk[k].eng_req]->edit[i].min + (int16_t)(k + i) %
            (ENGINES[trk[k].eng_req]->edit[i].max - ENGINES[trk[k].eng_req]->edit[i].min + 1));
    t->p[P_FM1_ATK] = 33; t->p[P_REV] = 20;
    t->step[2] = (step_t){{60, 64, 67}, 3, ST_NOTE, 0, 100};
    project_capture(&before);
    memset(&m, 0, sizeof m);                                       /* as the 89-parameter firmware numbered them */
    m.count = 3; m.on = 1;
    m.event[0] = (motion_event_t){.place = 3, .id = P_REV, .value = 90};
    m.event[1] = (motion_event_t){.place = 5, .id = 81, .value = 40};                      /* its P_E0 (81) */
    m.event[2] = (motion_event_t){.place = 6, .id = 61, .value = 20};                      /* FM OP1 ATK: 61 then and now */
    pack_fun7_89(&old, &before, &m);
    ok = proj_import(&after, &old, sizeof old);
    for (k = 0; ok && k < PROJ_NT; k++) {
        for (i = 0; i < 8u; i++) ok &= after.t[k].p[P_E0 + i] == before.t[k].p[P_E0 + i];
        for (i = 0; i < 81u; i++) ok &= after.t[k].p[i] == before.t[k].p[i];
        ok &= after.t[k].p[P_CHRD] == 0 && after.t[k].p[P_VOIC] == 0;
        for (i = P_LN0; i <= P_LN7; i++) ok &= after.t[k].p[i] == 127;
    }
    bad += check("89 parameters: E0..E7 at P_E0.., the chord keys OFF / CLOSE, lanes 100 %, the rest in place", ok &&
        after.t[0].p[P_FM1_ATK] == 33 && !memcmp(after.t[0].step, before.t[0].step, sizeof before.t[0].step));
    bad += check("  its motion: E0 (81) -> P_E0, REV and FM OP1 ATK (61) kept",
        after.motion.count == 3u && (after.motion.event[0].id == P_REV && !after.motion.event[0].lock) && (after.motion.event[1].id == P_E0 && !after.motion.event[1].lock) &&
        after.motion.event[1].value == 40 && (after.motion.event[2].id == P_FM1_ATK && !after.motion.event[2].lock));
    bad += check("  written again ((phase 9) PROJ_NP parameters): the same project", proj_pack(&old, &after) && old.raw[66] == PROJ_NP &&
        proj_import(&before, &old, sizeof old) && !memcmp(&before, &after, sizeof before));
    m.event[1].id = 82;                                          /* (any id P_E0 .. P_E7 of then moves up) */
    pack_fun7_89(&old, &before, &m);
    bad += check("  E1 (82) -> P_E1", proj_import(&after, &old, sizeof old) && (after.motion.event[1].id == P_E1 && !after.motion.event[1].lock));
    return bad;
}
static int loads_and_song(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    int16_t v;
    uint32_t e0 = t->eng_req, e1 = e0 == ENGI_DRUM ? ENGI_FM6 : ENGI_DRUM;
    t->p[P_REV] = 21; motion_set_event(t, 0, P_REV, 100); t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    motion_set_lock(t, 1, P_E0, motion_base_value(t, P_E0));
    apply_preset_to(t, 1);
    bad += check("1.1.5: a sound load of the same engine keeps the motion (both kinds, the engine's own E1 too), the pattern",
                 motion_count(t) == 2u && motion_lock_get(t, 1, P_E0, &v) && motion_enabled(t) && t->step[0].note[0] == 60);
    undo_step(0);                                  /* (back to the sound before it: REV 21) */
    set_engine_of(t, e1);
    bad += check("  another engine: the motion on the common parameters (REV) stays, the engine's own (E1) goes, PLAY ON",
                 motion_count(t) == 1u && !motion_lock_get(t, 1, P_E0, &v) && motion_enabled(t) && t->step[0].note[0] == 60);
    undo_step(0); bad += check("sound undo restores the original motion pool and base", motion_count(t) == 2u && t->p[P_REV] == 21 &&
                              t->eng_req == e0);
    undo_step(1); bad += check("sound redo restores the loaded motion state", motion_count(t) == 1u && t->eng_req == e1);
    undo_step(0);
    return bad;
}
static int repeat_mode(void)
{
    int bad = 0; ui_power_on(); track_t *t = &trk[0];
    t->p[P_AMODE] = 6; t->p[P_AOCT] = 4; t->p[P_APROB] = 127;
    arp_add(t, 72); arp_add(t, 60);
    arp_tick(t, CTL);
    bad += check("REPEAT retriggers the last played note without octave traversal", t->arp_note == 60);
    t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT remains on the same note on the next pulse", t->arp_note == 60);
    arp_remove(t, 60); arp_remove(t, 72); arp_tick(t, CTL);
    bad += check("REPEAT releases after the last held key is released", !t->nheld && !t->arp_note);
    t->p[P_AHOLD] = 1; arp_add(t, 65); arp_remove(t, 65); t->arp_pos = 0xFFFFFFF; arp_tick(t, CTL);
    bad += check("REPEAT supports ARP HOLD", t->nheld == 1u && t->arp_note == 65);
    return bad;
}

/* 1.2 (Discussion #95): the ARP modes appended after REPEAT (seq.c AM_*), each over a 3- and a 4-note chord, OCT,
 * ORD PLAY, HOLD, REPEAT switched in and out, WALK's random steps the same from the same seed */
static void arp_fresh(track_t *t, int32_t mode, int32_t oct, const uint8_t *notes, uint32_t n)
{
    uint32_t i;
    if (t->arp_note)
        arp_off(t);
    t->nheld = 0; t->arp_phys = 0;
    t->p[P_AMODE] = (int16_t)mode; t->p[P_AOCT] = (int16_t)oct; t->p[P_APROB] = 127;
    for (i = 0; i < n; i++)
        arp_add(t, notes[i]);
}
static int arp_plays(track_t *t, const uint8_t *want, uint32_t n)   /* the next n steps play these notes */
{
    uint32_t i;
    int ok = 1;
    for (i = 0; i < n; i++) {
        t->arp_pos = 0xFFFFFFF;
        arp_tick(t, CTL);
        ok &= t->arp_note == want[i] && !t->arp_ch[0];
        if (t->arp_note != want[i])
            printf("    step %u: %u, not %u\n", (unsigned)i, (unsigned)t->arp_note, (unsigned)want[i]);
    }
    return ok;
}
static int arp_new_modes(void)
{
    static const uint8_t CEG[3] = {60, 64, 67}, CEGB[4] = {60, 64, 67, 71}, GCE[3] = {67, 60, 64};
    static const uint8_t FIVE[5] = {60, 64, 67, 71, 74};
    int bad = 0, ok;
    uint32_t i, k;
    uint8_t a[32], b[32];
    track_t *t;
    ui_power_on();
    t = &trk[0];
    ok = TP[P_AMODE].max == 14 && str_eq(TP[P_AMODE].names[6], "REPEAT") && str_eq(TP[P_AMODE].names[7], "DNUP") &&
         str_eq(TP[P_AMODE].names[8], "UP+8") && str_eq(TP[P_AMODE].names[9], "CONV") &&
         str_eq(TP[P_AMODE].names[10], "DIVG") && str_eq(TP[P_AMODE].names[11], "PINKY") &&
         str_eq(TP[P_AMODE].names[12], "THUMB") && str_eq(TP[P_AMODE].names[13], "WALK") &&
         str_eq(TP[P_AMODE].names[14], "CHORD");
    for (i = 0; i <= 14u; i++)
        ok &= strlen(TP[P_AMODE].names[i]) <= 6u;
    bad += check("ARP modes: OFF..REPEAT keep their numbers, DNUP UP+8 CONV DIVG PINKY THUMB WALK CHORD appended (<= 6 chars)", ok);
    /* the modes before 1.2 play as they did */
    arp_fresh(t, AM_UP, 1, CEG, 3);   ok = arp_plays(t, (const uint8_t[]){60, 64, 67, 60}, 4);
    arp_fresh(t, AM_DN, 1, CEG, 3);   ok &= arp_plays(t, (const uint8_t[]){67, 64, 60, 67}, 4);
    arp_fresh(t, AM_UPDN, 1, CEG, 3); ok &= arp_plays(t, (const uint8_t[]){60, 64, 67, 64, 60}, 5);
    arp_fresh(t, AM_ORD, 1, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){60, 64, 67, 60}, 4);
    bad += check("ARP UP, DN, UPDN, ORD as before", ok);
    arp_fresh(t, AM_DNUP, 1, CEG, 3);  ok = arp_plays(t, (const uint8_t[]){67, 64, 60, 64, 67, 64}, 6);
    arp_fresh(t, AM_DNUP, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){71, 67, 64, 60, 64, 67, 71}, 7);
    arp_fresh(t, AM_DNUP, 2, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){79, 76, 72, 67, 64, 60, 64, 67, 72, 76, 79}, 11);
    bad += check("ARP DNUP: G E C E; B G E C E G for 4; across OCT 2", ok);
    arp_fresh(t, AM_UP8, 1, CEG, 3);  ok = arp_plays(t, (const uint8_t[]){60, 64, 67, 72, 60}, 5);
    arp_fresh(t, AM_UP8, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){60, 64, 67, 71, 72, 60}, 6);
    arp_fresh(t, AM_UP8, 2, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){60, 64, 67, 72, 76, 79, 84, 60}, 8);
    t->p[P_AORDER] = 1;                               /* ORD PLAY: as played, G C E, then G an octave up */
    arp_fresh(t, AM_UP8, 1, GCE, 3);  ok &= arp_plays(t, (const uint8_t[]){67, 60, 64, 79, 67}, 5);
    t->p[P_AORDER] = 0;
    bad += check("ARP UP+8: C E G C'; C E G B C'; OCT 2 ends on C''; ORD PLAY: the first played an octave over the top", ok);
    arp_fresh(t, AM_CONV, 1, CEG, 3);  ok = arp_plays(t, (const uint8_t[]){60, 67, 64, 60}, 4);
    arp_fresh(t, AM_CONV, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){60, 71, 64, 67, 60}, 5);
    arp_fresh(t, AM_CONV, 2, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){60, 79, 64, 76, 67, 72, 60}, 7);
    arp_fresh(t, AM_DIVG, 1, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){64, 67, 60, 64}, 4);
    arp_fresh(t, AM_DIVG, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){67, 64, 71, 60, 67}, 5);
    bad += check("ARP CONV: C G E, C B E G (outside in), OCT 2; DIVG: E G C, G E B C (inside out)", ok);
    arp_fresh(t, AM_PINKY, 1, CEG, 3);  ok = arp_plays(t, (const uint8_t[]){60, 67, 64, 67, 60}, 5);
    arp_fresh(t, AM_PINKY, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){60, 71, 64, 71, 67, 71, 60}, 7);
    arp_fresh(t, AM_PINKY, 2, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){60, 79, 64, 79, 67, 79, 72, 79, 76, 79, 60}, 11);
    arp_fresh(t, AM_THUMB, 1, CEG, 3);  ok &= arp_plays(t, (const uint8_t[]){60, 64, 60, 67, 60}, 5);
    arp_fresh(t, AM_THUMB, 1, CEGB, 4); ok &= arp_plays(t, (const uint8_t[]){60, 64, 60, 67, 60, 71, 60}, 7);
    arp_fresh(t, AM_THUMB, 1, CEG, 1);  ok &= arp_plays(t, (const uint8_t[]){60, 60, 60}, 3);   /* (one note) */
    bad += check("ARP PINKY: C G E G, C B E B G B, the OCT 2 top; THUMB: C E C G, C E C G C B; one note", ok);
    /* CHORD: the held notes together, an octave up each step through OCT; the first 4 of more */
    arp_fresh(t, AM_CHORD, 2, CEG, 3);
    ok = 1;
    for (i = 0; i < 4u; i++) {
        uint32_t o = 12u * (i & 1u);
        t->arp_pos = 0xFFFFFFF;
        arp_tick(t, CTL);
        ok &= t->arp_note == 60u + o && t->arp_ch[0] == 64u + o && t->arp_ch[1] == 67u + o && !t->arp_ch[2];
    }
    arp_fresh(t, AM_CHORD, 1, FIVE, 5);
    t->arp_pos = 0xFFFFFFF;
    arp_tick(t, CTL);
    ok &= t->arp_note == 60u && t->arp_ch[0] == 64u && t->arp_ch[1] == 67u && t->arp_ch[2] == 71u;
    t->arp_off = 1;                                    /* its gate ends: all four end */
    arp_tick(t, 2);
    ok &= !t->arp_note && !t->arp_ch[0] && !t->arp_ch[1] && !t->arp_ch[2];
    arp_fresh(t, AM_CHORD, 1, CEG, 3);
    t->arp_pos = 0xFFFFFFF;
    arp_tick(t, CTL);
    arp_remove(t, 60); arp_remove(t, 64); arp_remove(t, 67);
    arp_tick(t, CTL);                                  /* let go: the chord ends */
    ok &= !t->nheld && !t->arp_note && !t->arp_ch[0] && !t->arp_ch[1];
    bad += check("ARP CHORD: C E G together, then an octave up (OCT 2); 5 held: the first 4; the gate and a release end them all", ok);
    /* WALK: from the bottom, one place up or down each step, inside the list; the same from the same seed */
    ok = 1;
    for (k = 0; k < 2u; k++) {
        rng_state = 0x2468ACEu;
        arp_fresh(t, AM_WALK, 2, CEGB, 4);             /* the list: C E G B C' E' G' B' */
        for (i = 0; i < 32u; i++) {
            t->arp_pos = 0xFFFFFFF;
            arp_tick(t, CTL);
            (k ? b : a)[i] = t->arp_note;
        }
    }
    ok &= a[0] == 60u && !memcmp(a, b, sizeof a);
    {
        static const uint8_t L[8] = {60, 64, 67, 71, 72, 76, 79, 83};
        uint32_t pos[32], up = 0, dn = 0;
        for (i = 0; i < 32u; i++) {
            for (pos[i] = 0; pos[i] < 8u && L[pos[i]] != a[i]; pos[i]++)
                ;
            ok &= pos[i] < 8u;
            if (i) {
                ok &= pos[i] == pos[i - 1u] + 1u || pos[i] + 1u == pos[i - 1u];
                up += pos[i] > pos[i - 1u];
                dn += pos[i] < pos[i - 1u];
            }
        }
        ok &= up > 4u && dn > 4u;
    }
    rng_state = 0x13579BDu;
    arp_fresh(t, AM_WALK, 2, CEGB, 4);
    for (i = 0; i < 32u; i++) {
        t->arp_pos = 0xFFFFFFF;
        arp_tick(t, CTL);
        b[i] = t->arp_note;
    }
    ok &= memcmp(a, b, sizeof a) != 0;
    arp_remove(t, 71); arp_remove(t, 67);              /* notes let go: back inside the shorter list */
    for (i = 0; i < 8u; i++) {
        t->arp_pos = 0xFFFFFFF;
        arp_tick(t, CTL);
        ok &= t->arp_note == 60 || t->arp_note == 64 || t->arp_note == 72 || t->arp_note == 76;
    }
    bad += check("ARP WALK: from the bottom, a step up or down each time inside the list; the same seed the same walk", ok);
    /* HOLD: the latched chord keeps its mode; a new chord replaces it */
    t->p[P_AHOLD] = 1;
    arp_fresh(t, AM_PINKY, 1, CEG, 3);
    arp_remove(t, 60); arp_remove(t, 64); arp_remove(t, 67);
    ok = t->nheld == 3u && arp_plays(t, (const uint8_t[]){60, 67, 64, 67}, 4);
    arp_add(t, 62); arp_add(t, 65); arp_add(t, 69);   /* D F A: replaces it, from the top of the pattern */
    ok &= t->nheld == 3u && arp_plays(t, (const uint8_t[]){62, 69, 65, 69}, 4);
    arp_remove(t, 62); arp_remove(t, 65); arp_remove(t, 69);
    t->p[P_AHOLD] = 0;
    bad += check("ARP HOLD: PINKY keeps playing the latched C E G; D F A replaces it", ok);
    /* REPEAT in and out: the last played note alone, then the new mode over the whole chord again */
    arp_fresh(t, AM_REPEAT, 2, CEG, 3);
    ok = arp_plays(t, (const uint8_t[]){67, 67}, 2);
    t->p[P_AMODE] = AM_THUMB;                          /* (its step count goes on: step 2 of C E C G C' ..) */
    ok &= arp_plays(t, (const uint8_t[]){60, 67, 60, 72}, 4);
    t->p[P_AMODE] = AM_REPEAT;
    ok &= arp_plays(t, (const uint8_t[]){67, 67}, 2);
    t->p[P_AMODE] = AM_CHORD;
    t->arp_pos = 0xFFFFFFF;
    arp_tick(t, CTL);
    ok &= t->arp_ch[0] && t->arp_ch[1];
    t->p[P_AMODE] = AM_REPEAT;
    ok &= arp_plays(t, (const uint8_t[]){67}, 1);      /* (CHORD's other notes ended) */
    bad += check("ARP REPEAT switched to THUMB and back, CHORD to REPEAT: each plays its own", ok);
    /* recording: CHORD writes its notes into one step, as the keys' chords (POLY) */
    sel_set(0); song.rec = 1;
    t->p[P_VOICE] = V_POLY;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){0};
    seq_start();
    seq_tick(t, CTL);
    arp_fresh(t, AM_CHORD, 1, CEG, 3);
    t->arp_pos = 0xFFFFFFF;
    arp_tick(t, CTL);
    for (i = 0, k = 0; i < NSTEP; i++)
        k += t->step[i].n == 3u && t->step[i].note[0] == 60 && t->step[i].note[1] == 64 && t->step[i].note[2] == 67;
    seq_stop();
    song.rec = 0;
    bad += check("ARP CHORD recorded: C E G into one step", k == 1u);
    arp_fresh(t, AM_OFF, 1, CEG, 0);
    return bad;
}
/* ------------------------------------------------ 1.1 parameter locks --- */
/* the transport on to the next step of t (its seq_idx changes) */
static void lock_next(track_t *t)
{
    uint32_t i = t->seq_idx, n = 0;
    while (t->seq_idx == i && n++ < 100000u)
        seq_tick(t, CTL);
}
/* track 1 alone: 8 NOTE steps, REV 20, swing sw */
static track_t *lock_track(int32_t sw)
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 8;
    t->p[P_SSWING] = (int16_t)sw;
    song.g[G_SWING] = 0;
    t->p[P_REV] = 20;
    for (i = 0; i < 8u; i++)
        t->step[i] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    return t;
}
/* two passes from step 0: REV at each step's start as want[]; 1 = all as wanted */
static int lock_passes(track_t *t, const int16_t *want)
{
    uint32_t i, pass;
    seq_start();
    seq_tick(t, CTL);
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < 8u; i++) {
            if (pass || i)
                lock_next(t);
            if (t->seq_idx != i || t->p[P_REV] != want[i]) {
                printf("motion:   pass %u step %u: REV %d, wanted %d\n", pass, i, t->p[P_REV], want[i]);
                return 0;
            }
        }
    return 1;
}
static int lock_playback(void)
{
    static const int16_t want[8] = {20, 20, 100, 20, 60, 60, 90, 60};   /* locks on 2, 6; automation on 4 */
    int bad = 0, ok;
    uint32_t n;
    track_t *t = lock_track(0);
    project_t q;
    ok = !motion_set_lock(t, 2, P_REV, 100) && !motion_set_event(t, 4, P_REV, 60) && !motion_set_lock(t, 6, P_REV, 90);
    bad += check("a lock is one of the shared 64 records, bit 7 of its id (MOTION_LOCK)", ok && motion.count == 3u &&
        (motion.event[0].id == P_REV && motion.event[0].lock) && (motion.event[1].id == P_REV && !motion.event[1].lock) && motion_lock_count(t) == 2u &&
        motion_count(t) == 3u && motion_lock_steps(0) == ((1u << 2) | (1u << 6)));
    bad += check("lock: its value on its step only, back after it (the sound's own, or the automation held)",
        lock_passes(t, want));
    lock_next(t); lock_next(t); lock_next(t);
    project_capture(&q);
    bad += check("  a lock sounding: the sound's own value is the base, a project saves that", t->seq_idx == 2u &&
        t->p[P_REV] == 100 && motion_base_value(t, P_REV) == 20 && q.t[0].p[P_REV] == 20);
    motion_set_enabled(t, 0);
    bad += check("  AUTOMATION PLAY OFF: the sound's own value at once, the locks kept", t->p[P_REV] == 20 &&
        motion_lock_count(t) == 2u);
    motion_set_enabled(t, 1);
    lock_next(t); lock_next(t); lock_next(t); lock_next(t);
    seq_stop();
    bad += check("  STOP while a lock sounds puts the sound's own value back", t->p[P_REV] == 20);
    t = lock_track(100);
    motion_set_lock(t, 2, P_REV, 100); motion_set_event(t, 4, P_REV, 60); motion_set_lock(t, 6, P_REV, 90);
    bad += check("  with SWING 100: the same values step by step", lock_passes(t, want));
    seq_stop();
    t = lock_track(0);                                   /* the chance: a step that does not play applies no lock */
    motion_set_lock(t, 2, P_REV, 100);
    step_set_chance(&t->step[2], 0);
    {
        static const int16_t w0[8] = {20, 20, 20, 20, 20, 20, 20, 20};
        bad += check("  CHANCE 0 %: the step does not play, nor its lock", lock_passes(t, w0));
    }
    seq_stop();
    step_set_chance(&t->step[2], 100);
    {
        static const int16_t w1[8] = {20, 20, 100, 20, 20, 20, 20, 20};
        bad += check("  CHANCE 100 %: its lock", lock_passes(t, w1));
    }
    seq_stop();
    step_set_chance(&t->step[2], 50);                    /* the one roll: the lock with the notes, or neither */
    seq_start(); seq_tick(t, CTL);
    for (n = 0, ok = 1; n < 400u; n++) {
        lock_next(t);
        if (t->seq_idx == 2u)
            ok &= (t->p[P_REV] == 100) == (t->seq_n != 0u);
    }
    bad += check("  CHANCE 50 %: the lock applies exactly when the step's notes play", ok);
    seq_stop();
    t = lock_track(0);                                   /* RATCH x3: the lock over all its parts */
    motion_set_lock(t, 2, P_REV, 100);
    step_set_ratchet(&t->step[2], 3);
    seq_start(); seq_tick(t, CTL);
    lock_next(t); lock_next(t);
    {
        uint32_t v0 = vage;
        for (ok = t->seq_idx == 2u; t->seq_idx == 2u;) {
            ok &= t->p[P_REV] == 100;
            seq_tick(t, CTL);
        }
        bad += check("  RATCH x3: its 3 parts all with the lock, back at the next step", ok && vage - v0 >= 3u &&
            t->p[P_REV] == 20);
    }
    seq_stop();
    t = lock_track(0);                                   /* an engine parameter, read by the note-on */
    t->p[P_E4] = 10;
    motion_set_lock(t, 1, P_E4, 90);
    motion_set_lock(t, 1, P_E4, 300);                    /* (out of its range: refused, the lock as it was) */
    seq_start(); seq_tick(t, CTL); lock_next(t);
    bad += check("  an engine parameter: set at the step's start (before its notes), in its range", t->p[P_E4] == 90 &&
        t->seq_n == 1u);
    lock_next(t);
    bad += check("  .. and back at the next step", t->p[P_E4] == 10);
    seq_stop();
    return bad;
}

static int lock_edit(void)
{
    int bad = 0;
    int16_t v;
    track_t *t = lock_track(0);
    motion_set_event(t, 3, P_REV, 50);
    bad += check("a lock replaces an automation event of its step and id (one record, either kind)",
        !motion_set_lock(t, 3, P_REV, 70) && motion.count == 1u && motion_lock_get(t, 3, P_REV, &v) && v == 70);
    motion_set_event(t, 3, P_REV, 40);
    bad += check("  .. and back (an automation event over a lock)", motion.count == 1u && !motion_lock_count(t));
    motion_set_lock(t, 3, P_ATK, 9); motion_set_lock(t, 5, P_ATK, 9); motion_set_lock(&trk[1], 3, P_REV, 1);
    bad += check("clear a step's locks: its automation and the other steps' and tracks' stay",
        motion_clear_locks(t, 3) == 1u && motion.count == 3u && motion_lock_count(t) == 1u && motion_lock_count(&trk[1]) == 1u);
    bad += check("clear every step's locks (NSTEP)", motion_clear_locks(t, NSTEP) == 1u && !motion_lock_count(t) &&
        motion_count(t) == 1u);
    motion_delete_event(&trk[1], 3, P_REV);
    bad += check("delete an event deletes a lock too (either kind)", !motion_count(&trk[1]));
    return bad;
}
static int lock_project(void)
{
    int bad = 0;
    track_t *t = lock_track(0);
    project_t before, after;
    project_store_t a, b;
    uint32_t i, ok = 1;
    motion_set_event(t, 1, P_REV, 33);                   /* no lock: as every project before 1.1 */
    project_capture(&before);
    ok = proj_pack(&a, &before) && proj_import(&after, &a, sizeof a) && proj_pack(&b, &after) && !memcmp(&a, &b, sizeof a);
    for (i = 0; i < before.motion.count; i++)
        ok &= !before.motion.event[i].lock;
    bad += check("a project without locks: the bytes as before (ids < 128), the same round trip", ok);
    motion_set_lock(t, 2, P_REV, 101); motion_set_lock(t, 7, P_E4, 77);
    project_capture(&before);
    proj_power_on(&before, PROJ_NT); before.sum = proj_sum(&before);   /* ((Skiff phase 6) four tracks in SKF1) */
    ok = proj_pack(&a, &before) && proj_import(&after, &a, sizeof a) && !memcmp(&before, &after, sizeof before);
    bad += check("FUN9 round trip keeps the locks (bit 7 of the id byte), their values and steps", ok &&
        (after.motion.event[1].id == P_REV && after.motion.event[1].lock) && after.motion.event[2].value == 77);
    project_save(2);
    bank_put((uint32_t)(t - trk), 1, 0);                 /* (phase 5: the track onto a copy, B: the clear its) */
    motion_clear(t);
    project_load(2);
    bad += check("  saved and loaded: the locks play again", motion_lock_count(t) == 2u && lock_passes(t,
        (const int16_t[8]){20, 33, 101, 33, 33, 33, 33, 33}));
    seq_stop();
    {   /* a lock on an engine id of a store of fewer parameters (a FUN7 of 89, before the chord keys; a FUN8 of 91,
         * before the DRUM lane levels, as a 1.1 development build wrote it): moves with its id, keeps bit 7 */
        static const uint32_t np[2] = {89u, 91u};
        for (uint32_t n = 0; n < 2u; n++) {
            motion_store_t m = before.motion;
            for (i = 0; i < m.count; i++)
                if (m.event[i].id == P_E4)
                    m.event[i].id = (uint8_t)(P_E4 - (P_COUNT - np[n]));   /* (a lock: motion_set_lock above) */
            ok = proj_motion_ids(&m, np[n]);
            bad += check(n ? "  FUN8 of 91: a lock's id moves up with P_E0 (87 -> P_E4), still a lock"
                           : "  FUN7 of 89: a lock's id moves up with P_E0 (85 -> P_E4), still a lock",
                ok && (m.event[2].id == P_E4 && m.event[2].lock) && (m.event[1].id == P_REV && m.event[1].lock) &&
                (m.event[0].id == P_REV && !m.event[0].lock));
            m = before.motion;
            m.event[2].id = (uint8_t)np[n];
            bad += check("    an id that store could not name (np): refused", !proj_motion_ids(&m, np[n]));
        }
    }
    return bad;
}
/* the UI: hold a step, turn a knob (ui_input.c lock_*) */
static void lk_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame(); }
static void lk_up(uint32_t k) { fm1_in.notes &= ~(1u << k); frame(); }
static int lock_ui(void)
{
    int bad = 0, ok;
    int16_t v;
    track_t *t;
    uint32_t k0, k4, kn;
    ui_power_on();
    sel_set(3);                                        /* track 4: DRUM, the grid */
    t = &trk[3];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_home();
    open_family(FAM_SEQ);
    frame();
    k0 = key_at(0, 0); k4 = key_at(0, 4);
    grid_hit(t, 0, 0, 1);                                /* a KICK on step 1 */
    bad += check("the grid on SEQ > STEP; the locks' knobs: HOME's (DRUM: E2..E5)", grid_on() && lock_id(0) == P_E1 &&
        lock_id(3) == P_E4);
    lk_down(k4);
    bad += check("a step key down: its step held (an empty one gets its hit at once)", lock_held() == (1u << 4) &&
        (t->step[4].hit & 1u));
    turn(EN_K1, 3);
    ok = motion_lock_get(t, 4, P_E1, &v) && v == motion_base_value(t, P_E1) + 3;
    turn(EN_K1, 2);
    bad += check("  KNOB 1 turned: step 5 gets a lock of E2 (TUNE), from the sound's own value", ok &&
        motion_lock_get(t, 4, P_E1, &v) && v == motion_base_value(t, P_E1) + 5 && t->p[P_E1] == motion_base_value(t, P_E1));
    lk_up(k4);
    bad += check("  let go: the hit stays, the lock is there, the sound unchanged", (t->step[4].hit & 1u) &&
        motion_lock_count(t) == 1u && !lock_held());
    lk_down(k0);
    turn(EN_K2, -4);
    lk_up(k0);
    bad += check("a key pressed on a hit and held for a lock: the hit stays", (t->step[0].hit & 1u) &&
        motion_lock_get(t, 0, P_E2, &v));
    tap_key(k0);
    bad += check("  tapped alone: the hit goes (when let go), its lock stays", !(t->step[0].hit & 1u) &&
        motion_lock_count(t) == 2u);
    fm1_in.notes |= 1u << k0 | 1u << k4; host_notes |= 1u << k0 | 1u << k4; frame();
    turn(EN_K4, 1);
    fm1_in.notes = 0; frame();
    bad += check("two step keys held: both get the lock", motion_lock_get(t, 0, P_E4, &v) && motion_lock_get(t, 4, P_E4, &v) &&
        motion_lock_count(t) == 4u);
    lk_down(k4);
    press(B_EDIT);
    lk_up(k4);
    bad += check("a step held + [EDIT]: its locks go, the step and the others' locks stay", motion_lock_count(t) == 2u &&
        !motion_lock_get(t, 4, P_E1, &v) && motion_lock_get(t, 0, P_E4, &v) && (t->step[4].hit & 1u) &&
        str_eq(ui.msg, "LOCKS CLEARED"));
    bad += check("the steps with a lock (the grid marks them): step 1", motion_lock_steps(3) == 1u);
    open_family(FAM_FX);                                 /* the lock's knobs: the sound page shown last */
    open_family(FAM_SEQ);
    frame();
    bad += check("from FX: the knobs lock DIST CHO DLY REV", lock_id(0) == P_DIST && lock_id(3) == P_REV);
    lk_down(k4); turn(EN_K4, 5); lk_up(k4);
    bad += check("  step 5 gets a REV lock", motion_lock_get(t, 4, P_REV, &v) && v == t->p[P_REV] + 5);
    go_page(GR_EVENTS);
    frame();
    bad += check("AUTOMATION: EVENT 0, LOCK 3", motion_count(t) - motion_lock_count(t) == 0u && motion_lock_count(t) == 3u);
    /* the roll: the step being entered is the one held */
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    go_home();
    open_family(FAM_SEQ);
    frame();
    for (kn = 0; kn < 27u && kb_map(t, kn) > 127u; kn++)
        ;
    lk_down(kn);                                         /* (1.2, #133: not armed, a key only plays: no step held) */
    ok = !ui.entry_open && !lock_held() && !t->step[0].n;
    turn(EN_K3, 4);
    lk_up(kn);
    bad += check("the roll, not armed: a key held writes no step and holds none; KNOB 3 locks nothing",
                 ok && !motion_lock_count(t) && !t->step[0].n && ui.cursor == 0u);
    song.rec = 1;                                        /* armed, stopped: step recording */
    lk_down(kn);
    ok = ui.entry_open && lock_held() == 1u && t->step[0].n == 1u;
    turn(EN_K3, 4);                                      /* ANALOG's HOME: E5 E6 ATK REL */
    bad += check("the roll, armed (step recording): a note key held (the entry) is the step held; KNOB 3 locks ATK, TIME untouched", ok &&
        motion_lock_get(t, 0, P_ATK, &v) && v == t->p[P_ATK] + 4 && t->step[0].time == ST_NOTE);
    lk_up(kn);
    bad += check("  let go: the note and the lock on step 1, the cursor on", ui.cursor == 1u && t->step[0].n == 1u &&
        motion_lock_count(t) == 1u && motion_lock_steps(0) == 1u);
    cursor_set(0);
    press(B_EDIT);
    bad += check("  [EDIT] alone clears the step and its locks", !t->step[0].n && !motion_lock_count(t));
    return bad;
}
/* 1.1.5: lock edits by hand are one undo (SAVE held), redo too */
/* Skiff: a step's own note length: a lock of GATE (motion.c motion_param; SEQ > PLAY's KNOB 4, ui_input.c step_gate;
 * or a step held on STEP with PATTERN's KNOB 4). The step's notes last that long (seq_fire applies the lock before
 * seq_step takes the length), the next step the track's GATE again; RATCH parts take it too. On PLAY: KNOB 4 shows and
 * turns the cursor step's, from the track's; back at the track's the lock goes; SAVE held undoes */
static int step_gates(void)
{
    int bad = 0, ok;
    int16_t v;
    uint32_t period;
    track_t *t = lock_track(0);
    t->p[P_SGATE] = 64;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    ok = !motion_set_lock(t, 2, P_SGATE, 16);
    seq_start(); seq_tick(t, CTL);
    lock_next(t);
    ok &= t->seq_idx == 1u && t->seq_off == period * 64u / 128u;
    lock_next(t);
    ok &= t->seq_idx == 2u && t->seq_off == period * 16u / 128u && t->p[P_SGATE] == 16;
    lock_next(t);
    ok &= t->seq_idx == 3u && t->seq_off == period * 64u / 128u && t->p[P_SGATE] == 64;
    seq_stop();
    bad += check("a GATE lock: its step's notes 1/8 of the step (16), the next step the track's (64)", ok);
    t = lock_track(0);
    t->p[P_SGATE] = 64;
    motion_set_lock(t, 2, P_SGATE, 32);
    step_set_ratchet(&t->step[2], 2);
    seq_start(); seq_tick(t, CTL);
    lock_next(t); lock_next(t);
    ok = t->seq_off == step_samples(t, period, 2) / 2u * 32u / 128u;
    seq_stop();
    bad += check("  RATCH x2: each part its gate from the lock", ok && t->p[P_SGATE] == 64);
    t = lock_track(0);
    t->p[P_SGATE] = 64;
    go_title("NOTES");                                   /* ((2.0) its LENGTH: PLAY's GATE before) */
    ui.cursor = 3;
    frame();
    ok = motion_param(P_SGATE) && !motion_param(P_SLEN) && !motion_param(P_SDIV) && !motion_param(P_SSWING);
    turn(EN_K3, -1);
    turn(EN_K3, -1);
    ok &= motion_lock_get(t, 3, P_SGATE, &v) && v == 62 && t->p[P_SGATE] == 64 && motion_lock_steps(0) == 1u << 3;
    turn(EN_K3, 2);
    ok &= !motion_lock_get(t, 3, P_SGATE, &v) && !motion_count(t);
    bad += check("NOTES KNOB 3 (LENGTH): the cursor step's GATE from the track's; back at the track's, no lock", ok);
    turn(EN_K3, -3);
    ok = motion_lock_get(t, 3, P_SGATE, &v) && v == 61;
    frames(2000);                                        /* (a new act: its own undo copy) */
    hold(B_SAVE);
    bad += check("  SAVE held: the lock undone", ok && !motion_lock_get(t, 3, P_SGATE, &v));
    open_family(FAM_SEQ);                                /* ((2.0.2, #56) PATTERN is no lock source: a step held on */
    go_title("PATTERN");                                 /* NOTES after it keeps the sound's knobs, LENGTH its GATE) */
    frame();
    go_page(GR_ROLL);
    ui.cursor = 5;                                       /* (the roll: the step being entered, its note key held: */
    song.rec = 1;                                        /* step entry, the track armed, stopped) */
    frame();
    key_down(key_at(0, 0));                              /* (a note key: the ISR's, keyboard_block) */
    frame();
    turn(EN_K4, -2);
    key_up(key_at(0, 0));
    frame();
    song.rec = 0;
    bad += check("  (2.0.2, #56) a step held on NOTES after PATTERN: KNOB 4 locks the sound's, no GATE (LENGTH's, above)",
                 lock_id(3) != P_SGATE && lock_id(3) != 0xFFu && motion_lock_get(t, 5, lock_id(3), &v) &&
                 !motion_lock_get(t, 5, P_SGATE, &v));
    return bad;
}
static int lock_undo(void)
{
    int bad = 0;
    int16_t v;
    track_t *t;
    uint32_t k4;
    ui_power_on();
    sel_set(3);
    t = &trk[3];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_home();
    open_family(FAM_SEQ);
    frame();
    k4 = key_at(0, 4);
    lk_down(k4);
    turn(EN_K1, 3);
    turn(EN_K1, 2);
    lk_up(k4);
    bad += check("lock undo: a step held, KNOB 1 turned twice: one lock", motion_lock_get(t, 4, P_E1, &v) &&
        v == motion_base_value(t, P_E1) + 5);
    hold(B_SAVE);
    bad += check("  SAVE held: the lock is gone (one undo for the whole turn), the step's hit stays",
        !motion_lock_count(t) && (t->step[4].hit & 1u));
    press(B_OCTUP);                                      /* (Skiff: OCT+ in the undo window: redo) */
    bad += check("  OCT+ then (redo): the lock is back at its value", motion_lock_get(t, 4, P_E1, &v) &&
        v == motion_base_value(t, P_E1) + 5);
    frames(2000);
    lk_down(k4);
    press(B_EDIT);
    lk_up(k4);
    bad += check("  a step held + [EDIT] clears it", !motion_lock_count(t) && str_eq(ui.msg, "LOCKS CLEARED"));
    hold(B_SAVE);
    bad += check("  SAVE held brings it back", motion_lock_get(t, 4, P_E1, &v) && v == motion_base_value(t, P_E1) + 5);
    frames(2000);
    lk_down(k4);
    press(B_EDIT);
    lk_up(k4);
    lk_down(k4);
    press(B_EDIT);                                       /* (no lock there now: NO LOCKS, the undo copy kept) */
    lk_up(k4);
    hold(B_SAVE);
    bad += check("  [EDIT] on a step without locks takes no undo copy (the clear before stays undoable)",
        motion_lock_count(t) == 1u);
    return bad;
}
/* 1.1.5: SEQ > AUTO LIST (ui_events.c) */
static int auto_list(void)
{
    int bad = 0, ok;
    int16_t v;
    track_t *t;
    uint32_t id0, id1, i;
    uint8_t idx[MOTION_MAX];
    ui_power_on();
    sel_set(0);
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    go_home();
    cursor_set(4);
    go_page(GR_EVENTS);
    frame();
    id0 = ui.ev_id;
    bad += check("AUTO LIST: empty, + ADD LOCK selected on the SEQ cursor's step, the first knob of the sound page",
        cur_page()->graph == GR_EVENTS && ev_rows(idx) == 0u && ui.ev_step == 4u && id0 == lock_id(0) &&
        str_eq(act_name(4), "ADD") && act_ready());
    press(B_OCTUP);
    bad += check("  OCT+: a lock on step 5 at the sound's own value, its row selected",
        motion_lock_get(t, 4, id0, &v) && v == motion_base_value(t, id0) && ev_row(ev_rows(idx)) == 0u &&
        str_eq(ui.msg, "LOCK ADDED") && motion_enabled(t));
    turn(EN_K4, 3);
    bad += check("  KNOB 4: its value", motion_lock_get(t, 4, id0, &v) && v == motion_base_value(t, id0) + 3);
    turn(EN_K2, 2);
    bad += check("  KNOB 2: its step (5 -> 7), the value kept", !motion_lock_get(t, 4, id0, &v) &&
        motion_lock_get(t, 6, id0, &v) && v == motion_base_value(t, id0) + 3);
    turn(EN_K3, 1);
    id1 = MOTION_ID(&motion.event[0]);
    bad += check("  KNOB 3: the next parameter, from the sound's own value", id1 > id0 && ev_id_ok(t, id1) &&
        motion_lock_get(t, 6, id1, &v) && v == motion_base_value(t, id1) && motion_count(t) == 1u);
    press(B_OCTUP);
    bad += check("  OCT+ on the row: LOCK -> AUTO", motion_count(t) == 1u && !motion_lock_count(t) &&
        str_eq(act_name(4), "LOCK"));
    press(B_OCTUP);
    ok = motion_lock_count(t) == 1u;
    press(B_OCTUP);
    bad += check("  and back (AUTO -> LOCK -> AUTO)", ok && !motion_lock_count(t));
    turn(EN_K1, 1);
    turn(EN_K2, -2);
    bad += check("  KNOB 1: + ADD LOCK; KNOB 2 there: its step", ev_row(ev_rows(idx)) == 1u && ui.ev_step == 2u);
    press(B_OCTUP);
    bad += check("  OCT+: a second lock on step 3, first in the list (by step)", motion_count(t) == 2u &&
        motion_lock_get(t, 2, ui.ev_id, &v) && ev_rows(idx) == 2u && ev_row(2u) == 0u &&
        (motion.event[idx[0]].place & 63u) == 2u);
    turn(EN_K1, 5);
    press(B_OCTUP);
    bad += check("  OCT+ on + ADD LOCK where one is: ALREADY LISTED, that row selected", motion_count(t) == 2u &&
        str_eq(ui.msg, "ALREADY LISTED") && ev_row(2u) == 0u);
    turn(EN_K4, -300);                                   /* (U2) KNOB 4 to the value's lowest, one more: DELETE */
    ok = !ui.ev_del && motion_count(t) == 2u;
    turn(EN_K4, -1);
    ok &= ui.ev_del && str_eq(act_name(4), "DELETE");
    turn(EN_K4, 1);
    ok &= !ui.ev_del;                                    /* (right again: the value, nothing deleted) */
    turn(EN_K4, -1);
    ok &= ui.ev_del;
    press(B_OCTDN);
    ok &= !ui.ev_del && cur_page()->graph == GR_EVENTS;  /* (OCT- cancels it, the page stays) */
    turn(EN_K4, -1);
    press(B_OCTUP);
    bad += check("  KNOB 4 past the lowest: DELETE (right again or OCT- drop it); OCT+: the row's record goes",
        ok && motion_count(t) == 1u && !motion_lock_get(t, 2, ui.ev_id, &v) && str_eq(ui.msg, "DELETED") && !ui.ev_del);
    hold(B_SAVE);
    bad += check("  SAVE held: it is back", motion_count(t) == 2u);
    press(B_OCTUP);
    bad += check("  OCT+ (the undo window): gone again", motion_count(t) == 1u);
    frames(2000);
    turn(EN_K1, -5);
    turn(EN_K1, ui.ev_top);                              /* ((U3, R5) not the page's own top row: the first record) */
    v = motion.event[0].value;
    turn(EN_K4, 2); turn(EN_K4, 2); turn(EN_K4, 2);
    ok = motion.event[0].value == v + 6;
    hold(B_SAVE);
    bad += check("  KNOB 4 turned on (three turns): one undo", ok && motion.event[0].value == v);
    frames(3000);                                        /* (the undo window, 2.7 s, over: OCT+ is the page's again) */
    for (i = 0; motion.count < MOTION_MAX; i++)          /* the 64 used (track 2) */
        motion_set_event(&trk[1], i % NSTEP, i < NSTEP ? P_REV : P_DLY, 10);
    turn(EN_K1, 5);
    press(B_OCTUP);
    bad += check("  the 64 used: OCT+ adds nothing, AUTOMATION FULL", motion_count(t) == 1u &&
        str_eq(ui.msg, "AUTOMATION FULL") && !act_ready());
    motion_clear(&trk[1]);
    for (i = 0; i < P_COUNT; i++)                        /* names: an id each, none of DIGITAL's, lanes on DRUM only */
        if (ev_id_ok(t, i) && ((i >= P_FM1_ATK && i <= P_FM4_LEVEL) || (i >= P_LN0 && i <= P_LN7) || !motion_param(i)))
            break;
    bad += check("  KNOB 3's list: what motion records and the sound has (no OP ENV, no DRUM lanes off DRUM)", i == P_COUNT &&
        ev_id_ok(&trk[3], P_LN0));
    {
        char a[12], b[12];
        ev_name(t, P_ED_FLT, a); ev_name(t, P_LD_FLT, b);
        bad += check("  names: ENV CUTOFF / LFO CUTOFF (U4)", str_eq(a, "ENV CUTOFF") && str_eq(b, "LFO CUTOFF"));
    }
    ui_prefs |= PREF_LARGE;
    bad += check("  MENU > LARGE: the list layout with labels in M (as the other lists)", large_kind() == LK_LABEL);
    ui_prefs &= ~PREF_LARGE;
    return bad;
}
/* Skiff TAKES (take.c, ui_take.c): played with REC off, logged by pass; SHIFT + SAVE puts the last whole pass into
 * the track as REC would (a held note tied, an overdub), its knob moves as automation, one undo */
static int tk_has(const track_t *t, uint32_t note)   /* a step of t holds note */
{
    uint32_t i, k;
    for (i = 0; i < NSTEP; i++)
        for (k = 0; k < t->step[i].n && k < 4u; k++)
            if (t->step[i].note[k] == note) return 1;
    return 0;
}
static track_t *tk_track(void)                        /* track 1: 8 empty steps, REV 20, selected, REC off */
{
    track_t *t;
    uint32_t i;
    ui_power_on();
    t = &trk[0];
    track_defaults_steps(t);
    t->p[P_SLEN] = 8;
    t->p[P_AMODE] = 0;
    t->p[P_REV] = 20;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = (step_t){{0}, 0, ST_REST};
    motion_clear(t);
    sel_set(0);
    song.rec = 0;
    return t;
}
/* (Skiff U2: BLOCKS only) the take said: the TAKE box ("TAKE KEPT" / "TAKE PARTIAL" on T1), or the header where the box
 * does not fit ("TAKE T1", "TAKE T1 PARTIAL") */
static int take_said(int partial)
{
    return (ui.take_t && !take_box.kind && take_box.trk == 0u &&
            str_eq(take_box.title, partial ? "TAKE PARTIAL" : "TAKE KEPT")) ||
           str_eq(ui.msg, partial ? "TAKE T1 PARTIAL" : "TAKE T1");
}
static int takes(void)
{
    int bad = 0, ok;
    uint32_t i, e;
    track_t *t = tk_track();
    seq_start();
    seq_tick(t, CTL);                                 /* pass 0, step 1 */
    input_on(t, 60, 100);                             /* C4, held .. */
    lock_next(t); lock_next(t);
    input_off(t, 60);                                 /* .. let go early in step 3: a TIE in step 2 only */
    lock_next(t);
    motion_capture(t, P_REV, 77);                     /* a knob move in step 4 */
    for (i = 0; i < 5u; i++) lock_next(t);            /* pass 1, step 1 */
    input_on(t, 72, 100); input_off(t, 72);           /* a stray note in pass 1: not the take */
    take_commit(0);
    for (e = 0; e < motion.count && motion.event[e].place != 3u; e++) ;
    ok = t->step[0].time == ST_NOTE && t->step[0].n == 1u && t->step[0].note[0] == 60 && t->step[1].time == ST_TIE &&
         t->step[2].time == ST_REST && !tk_has(t, 72) && e < motion.count && MOTION_ID(&motion.event[e]) == P_REV &&
         motion.event[e].value == 77 && take_said(0);
    bad += check("takes: pass 1's note in step 1, held into step 2 (TIE), pass 2's note left out, the knob move "
                 "automation in step 4; TAKE T1", ok);
    undo_step(0);
    bad += check("takes:   SAVE held (undo): the steps and the automation as before the take",
                 !t->step[0].n && t->step[1].time == ST_REST && motion_count(t) == 0u);
    seq_stop();

    t = tk_track();
    t->step[1] = (step_t){{64}, 1, ST_NOTE, 0, 90};   /* a note there already: kept (a take adds) */
    seq_start(); seq_tick(t, CTL);
    input_on(t, 60, 100);
    for (i = 0; i < 4u; i++) lock_next(t);
    input_off(t, 60);
    for (i = 0; i < 4u; i++) lock_next(t);            /* pass 2 */
    take_commit(0);
    bad += check("takes:   an overdub: a held note ties into empty steps only, the note there stays",
                 t->step[0].note[0] == 60 && t->step[1].time == ST_NOTE && t->step[1].note[0] == 64 &&
                 t->step[2].time == ST_TIE && t->step[3].time == ST_TIE && t->step[4].time == ST_REST);
    seq_stop();

    {   /* (2.0, #25) POLY, forward: a held note goes on by itself, beside a note there and past one played meanwhile */
        int32_t x;
        t = tk_track();
        t->p[P_VOICE] = V_POLY;
        t->step[1] = (step_t){{64}, 1, ST_NOTE, 0, 90};
        seq_start(); seq_tick(t, CTL);
        input_on(t, 60, 100);
        for (i = 0; i < 4u; i++) lock_next(t);
        input_off(t, 60);
        for (i = 0; i < 4u; i++) lock_next(t);
        take_commit(0);
        x = note_at(&t->step[1], 60);
        ok = t->step[0].note[0] == 60 && note_span(t, 0, 0) == 4u && t->step[1].n == 2u && t->step[1].note[0] == 64 &&
             x >= 0 && step_tied(&t->step[1], (uint32_t)x) && t->step[3].time == ST_TIE && t->step[4].time == ST_REST;
        bad += check("takes:   (2.0) POLY: the held note goes on beside the note there, its own tie (E4 kept)", ok);
        seq_stop();

        t = tk_track();
        t->p[P_VOICE] = V_POLY;
        seq_start(); seq_tick(t, CTL);
        input_on(t, 60, 100);                         /* C4 on step 1 .. */
        lock_next(t); lock_next(t);
        input_on(t, 64, 100);                         /* .. E4 on step 3 while it is held .. */
        lock_next(t); lock_next(t);
        input_off(t, 60);                             /* .. C4 let go early in 5, E4 early in 6 */
        lock_next(t);
        input_off(t, 64);
        for (i = 0; i < 3u; i++) lock_next(t);
        take_commit(0);
        x = note_at(&t->step[2], 64);
        ok = note_span(t, 0, 0) == 4u && x >= 0 && !step_tied(&t->step[2], (uint32_t)x) &&
             note_span(t, 2, (uint32_t)x) == 3u && t->step[5].time == ST_REST;
        bad += check("takes:   (2.0) POLY: E4 played while C4 is held: both go on (C4 1-4, E4 3-5), no TIE cut", ok);
        seq_stop();
    }

    t = tk_track();
    song.rec = 1;
    seq_start(); seq_tick(t, CTL);
    input_on(t, 62, 100); input_off(t, 62);
    bad += check("takes:   an armed track: REC records it, nothing logged", take_ev_n == 0u && tk_has(t, 62));
    seq_stop();

    t = tk_track();
    seq_start(); seq_tick(t, CTL);
    take_commit(0);
    ok = str_eq(ui.msg, "PLAY ONE LOOP FIRST");
    for (i = 0; i < 8u; i++) lock_next(t);
    take_commit(0);
    bad += check("takes:   before a whole pass: PLAY ONE LOOP FIRST; a pass with nothing played: NOTHING PLAYED, no change",
                 ok && str_eq(ui.msg, "NOTHING PLAYED") && !t->step[0].n);
    seq_stop();

    t = tk_track();
    seq_start(); seq_tick(t, CTL);
    for (i = 0; i < 140u; i++) { input_on(t, 48u + i % 24u, 100); input_off(t, 48u + i % 24u); }   /* 280 entries */
    for (i = 0; i < 8u; i++) lock_next(t);
    take_commit(0);
    bad += check("takes:   more than the log holds in a pass: the rest taken, TAKE T1 PARTIAL", take_said(1));
    seq_stop();

    t = tk_track();                                   /* SHIFT + SAVE by the buttons: the take, no SAVE page, no undo */
    go_home(); frame();
    seq_start(); seq_tick(t, CTL);
    input_on(t, 65, 100); input_off(t, 65);
    for (i = 0; i < 8u; i++) lock_next(t);
    btn_down(B_HOME); frame(); frame();
    press(B_SAVE);
    btn_up(B_HOME); frame(); frame();
    bad += check("takes:   SHIFT (HOME held) + SAVE: the take; no SAVE page, no undo, no menu, HOME stays",
                 tk_has(t, 65) && take_said(0) && ui.home && !ui.menu);
    seq_stop();
    return bad;
}

/* Skiff trig conditions (core.h TC_*, seq.c step_cond_ok): A:B on pass A of every B of the track's loop, FILL and
 * !FILL with GLO + F4 held (seq_fill); a step without one plays as before */
static int cond_played(const track_t *t, uint32_t note)   /* the step just entered sounds this note */
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        if (t->seq_notes[i] == note) return 1;
    return 0;
}
static int conditions(void)
{
    static const uint8_t NOTE[4] = {60, 62, 64, 65}, COND[4] = {TC_1_2, TC_2_2, TC_FILL, TC_NFILL};
    /* passes 1..4 (FILL held in pass 3): which of the four steps play */
    static const uint8_t WANT[4][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {1, 0, 1, 0}, {0, 1, 0, 1}};
    int bad = 0, ok = 1;
    uint32_t pass, i, k;
    track_t *t = tk_track();
    for (k = 0; k < 4u; k++) {
        t->step[2u * k] = (step_t){{NOTE[k]}, 1, ST_NOTE, 0, 100};
        t->step[2u * k].cond = COND[k];
    }
    t->p[P_SGATE] = 64;                               /* (a gate inside its step: the notes end before the next) */
    seq_start();
    seq_tick(t, CTL);
    for (pass = 1; pass <= 4u && ok; pass++) {
        seq_fill = pass == 3u;
        for (i = 0; i < 8u; i++) {
            if (pass > 1u || i)
                lock_next(t);
            if (i % 2u == 0u && cond_played(t, NOTE[i / 2u]) != WANT[pass - 1u][i / 2u]) {
                printf("cond:   pass %u step %u (%s): played %d, wanted %d\n", pass, i + 1u, TC_NAME[COND[i / 2u]],
                       cond_played(t, NOTE[i / 2u]), WANT[pass - 1u][i / 2u]);
                ok = 0;
            }
        }
    }
    seq_fill = 0;
    seq_stop();
    bad += check("conditions: 1:2 on passes 1 and 3, 2:2 on 2 and 4, FILL only with FILL held, !FILL without it", ok);
    t = tk_track();
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[0].cond = TC_3_4;
    seq_start(); seq_tick(t, CTL);
    ok = 1;
    for (pass = 1; pass <= 8u; pass++) {
        if (pass > 1u)
            for (i = 0; i < 8u; i++) lock_next(t);
        ok &= cond_played(t, 60) == ((pass - 1u) % 4u == 2u);
    }
    seq_stop();
    bad += check("conditions:   3:4 plays on passes 3 and 7 of 8", ok);
    {   /* saved in bit 7 of the note bytes; a step without one reads back none */
        project_t q, q2;
        project_store_t packed;
        t = tk_track();
        t->step[3] = (step_t){{67}, 1, ST_NOTE, 0, 100};
        t->step[3].cond = TC_3_4;
        t->step[5].cond = TC_NFILL;                   /* (a REST may hold one: it plays nothing either way) */
        project_capture(&q);
        ok = proj_pack(&packed, &q) && proj_import(&q2, &packed, (int)sizeof packed) && q2.t[0].step[3].cond == TC_3_4 &&
             q2.t[0].step[3].note[0] == 67 && q2.t[0].step[5].cond == TC_NFILL && q2.t[0].step[0].cond == TC_NONE &&
             sizeof packed == 3840u;
        bad += check("conditions:   saved with the project and read back (bit 7 of the note bytes)", ok);
    }
    return bad;
}

/* Skiff play direction (seq.c seq_next, P_DIR): FWD REV PING RAND; a pass is LEN steps in each (TAKES, the trig
 * conditions), REC and TAKES round a late note onto the next step in its direction */
static uint32_t dir_next(track_t *t)                  /* on to the next step played (seq_cnt moves on each, RAND's too) */
{
    uint32_t c = t->seq_cnt, n = 0;
    while (t->seq_cnt == c && n++ < 100000u)
        seq_tick(t, CTL);
    return t->seq_idx;
}
static void dir_late(track_t *t)                      /* on to past the middle of the playing step */
{
    while (t->seq_pos <= step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u + TAKE_LAT)
        seq_tick(t, CTL);
}
static track_t *dir_track(uint32_t dir)               /* tk_track, LEN 4, playing its first step */
{
    track_t *t = tk_track();
    t->p[P_SLEN] = 4;
    t->p[P_DIR] = (int16_t)dir;
    t->p[P_SGATE] = 64;
    seq_start();
    seq_tick(t, CTL);
    return t;
}
static int direction(void)
{
    static const char *const WANT[4] = {"123412341234", "432143214321", "123432123432", 0};
    int bad = 0, ok = 1;
    uint32_t d, i, seen = 0;
    char got[13];
    track_t *t;
    for (d = DIR_FWD; d <= DIR_RAND; d++) {
        t = dir_track(d);
        for (i = 0; i < 40u; i++) {                   /* 10 passes */
            if (i) dir_next(t);
            if (i < 12u) got[i] = (char)('1' + t->seq_idx);
            if (d == DIR_RAND) seen |= 1u << t->seq_idx;
        }
        got[12] = 0;
        if ((WANT[d] && !str_eq(got, WANT[d])) || take_pass[0] != 10u || t->seq_idx > 3u) {
            printf("dir:   %s played %s, %u passes\n", N_DIR[d], got, take_pass[0]);
            ok = 0;
        }
        seq_stop();
    }
    bad += check("direction: FWD 1234, REV 4321, PING 1234321.., RAND any; a pass every 4 steps in each",
                 ok && seen == 0xFu);
    t = dir_track(DIR_REV);                           /* REV: step 4 opens each pass, 1:2 plays it on passes 1, 3 */
    t->step[3] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[3].cond = TC_1_2;
    seq_stop(); seq_start(); seq_tick(t, CTL);
    for (i = 0, ok = 1; i < 4u; i++) {
        ok &= t->seq_idx == 3u && cond_played(t, 60) == !(i & 1u);
        dir_next(t); dir_next(t); dir_next(t); dir_next(t);
    }
    seq_stop();
    bad += check("direction:   REV, 1:2 on step 4 (each pass's first): passes 1 and 3", ok);
    t = dir_track(DIR_REV);
    song.rec = 1;
    dir_next(t);                                      /* step 3 */
    dir_late(t);
    input_on(t, 64, 100); input_off(t, 64);
    ok = tk_has(t, 64) && t->step[1].note[0] == 64;
    seq_stop();
    t = dir_track(DIR_RAND);
    song.rec = 1;
    dir_late(t);
    i = t->seq_idx;
    input_on(t, 65, 100); input_off(t, 65);
    seq_stop();
    bad += check("direction:   REC late in a step: REV the step before it (3 -> 2), RAND the playing one",
                 ok && t->step[i].note[0] == 65);
    t = dir_track(DIR_REV);                           /* TAKES: the pass, in REV order */
    dir_next(t);                                      /* step 3 */
    input_on(t, 60, 100); input_off(t, 60);
    dir_next(t); dir_next(t);                         /* step 1, the pass's last */
    dir_late(t);
    input_on(t, 67, 100); input_off(t, 67);           /* late: pass 2's step 4 */
    dir_next(t);
    take_commit(0);
    ok = t->step[2].note[0] == 60 && !tk_has(t, 67);
    for (i = 0; i < 4u; i++) dir_next(t);
    take_commit(0);
    seq_stop();
    bad += check("direction:   TAKES in REV: pass 1's note on its step, a late one in its last step is pass 2's "
                 "step 4", ok && t->step[3].note[0] == 67);
    return bad;
}

/* Skiff FEEL (the REC layer's KNOB 4, seq.c feel_of): REC and TAKES keep where a note was played as the NUDGE of the
 * empty step it goes in (64ths of the step; a note past the middle early for the next one); a step with notes keeps
 * its own; OFF: on the grid */
static void at_64th(track_t *t, uint32_t f)          /* on to f 64ths into the playing step */
{
    uint32_t q = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 64u;
    while (t->seq_pos < f * q)
        seq_tick(t, CTL);
}
static int feel_take(uint32_t on)                     /* LEN 4: notes 10/64 into step 2, 12/64 before step 4; a take */
{
    track_t *t = dir_track(DIR_FWD);
    ui_rec_prefs = (uint8_t)(on ? REC_FEEL : 0u);
    rp_apply();
    dir_next(t);
    at_64th(t, 10);
    input_on(t, 60, 100); input_off(t, 60);
    dir_next(t);
    at_64th(t, 52);
    input_on(t, 64, 100); input_off(t, 64);
    dir_next(t); dir_next(t);                         /* pass 2 */
    take_commit(0);
    seq_stop();
    return t->step[1].note[0] == 60 && t->step[3].note[0] == 64 &&
           t->step[1].nudge == (on ? 10 : 0) && t->step[3].nudge == (on ? -12 : 0);
}
static int feel(void)
{
    int bad = 0, ok;
    track_t *t;
    bad += check("FEEL: a take keeps where its notes were played (+10 in step 2, -12 before step 4)", feel_take(1));
    bad += check("FEEL:   OFF: the take on the grid, as before", feel_take(0));
    t = dir_track(DIR_FWD);
    ui_rec_prefs = REC_FEEL;
    rp_apply();
    song.rec = 1;
    t->p[P_VOICE] = V_POLY;                           /* (a chord: REC keeps one note a step on a MONO part) */
    t->step[1] = (step_t){{67}, 1, ST_NOTE, 0, 100};
    t->step[1].nudge = 5;
    dir_next(t);
    at_64th(t, 20);
    input_on(t, 60, 100); input_off(t, 60);          /* into step 2, which has a note: a chord, its nudge kept */
    dir_next(t);
    at_64th(t, 20);
    input_on(t, 62, 100); input_off(t, 62);          /* into empty step 3 */
    ok = t->step[1].n == 2u && t->step[1].nudge == 5 && t->step[2].note[0] == 62 && t->step[2].nudge == 20;
    seq_stop();
    ui_rec_prefs = 0;
    rp_apply();
    bad += check("FEEL:   REC: a note into an empty step +20; into a step with notes, its nudge kept", ok);
    return bad;
}

int main(void)
{
    int bad = motion_recording() + motion_capacity() + probability_playback() + compact_project() + fun7_89() + loads_and_song() + repeat_mode();
    bad += arp_new_modes();
    bad += lock_playback() + lock_edit() + lock_project() + lock_ui() + lock_undo() + auto_list();
    bad += takes();                                   /* Skiff: TAKES (take.c, ui_take.c) */
    bad += conditions();                              /* Skiff: trig conditions (seq.c step_cond_ok) */
    bad += direction();                               /* Skiff: play direction (seq.c seq_next) */
    bad += feel();                                    /* Skiff: FEEL (seq.c feel_of) */
    bad += step_gates();                              /* Skiff: a step's own GATE (a lock) */
    printf("%s\n", bad ? "MOTION TEST FAILED" : "motion/chance/compact storage tests passed"); return bad != 0;
}
