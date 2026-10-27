/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 4 ("FUN4", 8-byte
 * steps: before the drum grid), a format 3 ("FUN3", 57 parameters per track: before the modulation matrix), a
 * format 2 ("FUN2", 53: before the SLICER) and a format 1 ("FUN1") project, built byte for byte as the
 * firmware stored them, convert to format 6 ("FUN6"): every old value at its parameter, the parameters
 * added since at their defaults (SLICER OFF,
 * every matrix slot OFF), steps, globals, selection, the engine bytes (0..7 kept: the engines added since
 * were appended); damaged ones are refused. Track 4 of a project written before 1.0 (formats 2 and 3,
 * `parts` 0) was the GM drum part: it becomes a DRUM part 4 (PROJ_DEF_KEEP: project_load gives it DRUM's kit;
 * SAMPLE PERC until 1.0.2), its steps (its lanes' notes as hits) and parameters kept, the drum level / reverb send (G_DRLVL /
 * G_DRREV) as its LEVEL / REV; global id 24 (the drum part's MIDI channel then, REVERB TYPE now) loads as ROOM
 * from every format before FUN7. A FUN4 written while the drums were PHYS's MODEL DRUM (`phys` 1) loads such
 * a track as the DRUM engine with the same sound (its E values moved), other PHYS tracks as they were; one
 * of before 1.0 (`phys` 0) its DUST as MODAL bowed. The steps of a DRUM track of before FUN5 (DRUM, or
 * PHYS DRUM become DRUM) get their lanes' notes as hits, the other notes (a low tom, a crash) kept, the
 * step accent as the hits' accents; other tracks' steps stay as they were (no hits). A SAMPLE track of the retired
 * PERC set (SET 4) loads as DRUM with its default kit, in any format (proj_perc).
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define FELUCCA_LEGACY 1                        /* ((2.0) the formats of before: the converter's build, core.h) */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* (Skiff phase 6) the tracks past a stored format's four load as at power-on: the engine's defaults, PROJ_DEF_SOUND,
 * no steps, the init patch, no list (nothing to check with four tracks) */
static int pow_on(const project_t *q)
{
    uint32_t t, k;
    int ok = 1;
    for (t = PROJ_NT; t < NTRK; t++) {
        ok &= q->t[t].engine == trk_def_engine(t) && q->t[t].preset == PROJ_DEF_SOUND && !q->cur[t] &&
              !memcmp(q->fm6[t], FM6_INIT, FM6_PACKED) &&
              q->t[t].p[P_LEVEL] == param_desc_of(trk_def_engine(t), P_LEVEL)->def;
        for (k = 0; k < NSTEP; k++)
            ok &= q->t[t].step[k].time == ST_REST && !q->t[t].step[k].n && !q->t[t].step[k].hit;
        for (k = 0; k < NENT; k++)
            ok &= !q->ent[t][k][0];
    }
    return ok;
}

/* (2.0) SKF4 record s as another firmware's layout of it: n bytes put in at at (n > 0, values + 9 each) or taken out
 * (n < 0), the count at c changed by dc, the hash made again */
static void skf4_edit(project_store_t *o, const project_store_t *s, uint32_t at, int n, uint32_t c, int dc)
{
    uint32_t end = PROJ_NAME_AT(&PROJ_SKF4), m = (uint32_t)(n < 0 ? -n : n), sum;
    *o = *s;
    if (n > 0) {
        memmove(o->raw + at + m, o->raw + at, end - at - m);
        memset(o->raw + at, 64u + 9u, m);
    } else {
        memmove(o->raw + at, o->raw + at + m, end - at - m);
        memset(o->raw + end - m, 0, m);
    }
    o->raw[c] = (uint8_t)(o->raw[c] + dc);
    sum = proj_hash(o->raw, PROJ_STORE4 - 4u); memcpy(o->raw + PROJ_STORE4 - 4u, &sum, 4);
}

/* the value parameter k (old id) of track t had in the old project */
/* (Skiff) an SKF1 record as FUN9 laid it out (no NUDGE planes; the patches at 3120, the name at 3632) */
static void fun9_from(uint8_t *v9, const project_store_t *st)
{
    uint32_t k, pos = 68u, pos9 = 68u, n = PROJ_NP + 2u + NSTEP * 9u, sum;
    memset(v9, 0, PROJ_STORE_V9);
    memcpy(v9, st->raw, 68);
    for (k = 0; k < PROJ_NT; k++, pos += n, pos9 += n + PROJ_NUDGE_BYTES)
        memcpy(v9 + pos, st->raw + pos9, n);
    memcpy(v9 + pos, st->raw + pos9, sizeof(chain_config_t) + sizeof(motion_store_t));
    memcpy(v9 + 3120u, st->raw + PROJ_FM6_OFF, PROJ_NT * FM6_PACKED);
    memcpy(v9 + 3632u, st->raw + PROJ_NAME_OFF, PROJ_NAME_LEN);
    ((uint32_t *)v9)[0] = 0x46554E39u;
    ((uint32_t *)v9)[1] = PROJ_STORE_V9;
    sum = proj_hash(v9, PROJ_STORE_V9 - 4u);
    memcpy(v9 + PROJ_STORE_V9 - 4u, &sum, 4);
}
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[PROJ_NT] = {7, 0, 6, 0};   /* WHEEL, ANALOG, TRIO; the drum track: 0 (no engine) */
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* today's steps n are the old steps o (8 bytes each), with no hits */
static int steps_same(const step_t *n, const step8_t *o)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (memcmp(n[k].note, o[k].note, 4) || n[k].n != o[k].n || n[k].time != o[k].time ||
            n[k].flags != o[k].flags || n[k].vel != o[k].vel || n[k].hit || n[k].acc)
            return 0;
    return 1;
}

/* .. as a DRUM track of before the grid gets them (proj_grid: its lanes' notes as hits) */
static int steps_grid_same(const step_t *n, const step8_t *o)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++) {
        step_t s;
        memset(&s, 0, sizeof s);
        memcpy(s.note, o[k].note, 4);
        s.n = o[k].n; s.time = o[k].time; s.flags = o[k].flags; s.vel = o[k].vel;
        step_to_grid(&s);
        if (memcmp(n[k].note, s.note, 4) || n[k].n != s.n || n[k].time != s.time || n[k].flags != s.flags ||
            n[k].vel != s.vel || n[k].hit != s.hit || n[k].acc != s.acc)
            return 0;
    }
    return 1;
}

/* today's project q as format 4 stored it (8-byte steps; the hits dropped) */
static void to_v4(project_v4_t *v, const project_t *q)
{
    uint32_t t, k;
    memset(v, 0, sizeof *v);
    v->magic = PROJ_MAGIC_V4;
    v->size = sizeof *v;
    memcpy(v->g, q->g, sizeof v->g);
    v->sel = q->sel;
    v->parts = q->parts;
    v->phys = q->phys;
    for (t = 0; t < PROJ_NT; t++) {
        for (k = 0; k < 61u; k++) v->t[t].p[k] = q->t[t].p[k];
        for (k = 0; k < 8u; k++) v->t[t].p[61u + k] = q->t[t].p[P_E0 + k];
        v->t[t].engine = q->t[t].engine;
        v->t[t].preset = q->t[t].preset;
        for (k = 0; k < NSTEP; k++) {
            memcpy(v->t[t].step[k].note, q->t[t].step[k].note, 4);
            v->t[t].step[k].n = q->t[t].step[k].n;
            v->t[t].step[k].time = q->t[t].step[k].time;
            v->t[t].step[k].flags = q->t[t].step[k].flags;
            v->t[t].step[k].vel = q->t[t].step[k].vel;
        }
    }
    v->sum = proj_hash(v, sizeof *v - 4u);
}

/* track t of the converted project has the old values where they belong; drum: track 4 was the drum part,
 * lvl / rev: its level and reverb send (the old globals) */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t, int drum, int16_t lvl, int16_t rev)
{
    uint32_t k;
    int ok = (drum ? n->engine == ENGI_DRUM && n->preset == PROJ_DEF_KEEP && steps_grid_same(n->step, o->step)
                   : n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step));
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == (drum && k == P_LEVEL ? lvl : drum && k == P_REV ? rev : oldv(t, k));
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == 0;                     /* every matrix slot OFF */
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    return ok;
}

/* a format 3 track (57 parameters: the SLICER at 45..48, P_E0 49) */
static int16_t oldv3(uint32_t t, uint32_t k) { return (int16_t)(t * 200u + k * 5u + 3u); }
static void fill_v3_track(proj_trk_v3_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V3; k++)
        d->p[k] = oldv3(t, k);
    d->engine = (uint8_t)(t * 3u);
    d->preset = (uint8_t)(t + 1u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(40u + (k * 7u + t) % 40u);
        s->n = (uint8_t)((k + t) % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)((k + 1u) & 3u);
        s->vel = (uint8_t)(70u + t);
    }
}
static int track_v3_ok(const proj_trk_t *n, const proj_trk_v3_t *o, uint32_t t)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step);
    for (k = 0; k <= P_SLDEPTH; k++)
        ok &= n->p[k] == oldv3(t, k);           /* up to the SLICER: the same ids */
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == TP[k].def && TP[k].def == 0;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv3(t, 49u + k);
    return ok;
}

int main(void)
{
    static project_v4_t v4;
    static project_v3_t v3;
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static union {
        project_t v5;
        project_v4_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0 ((Skiff) DIR, the ducking after the lane levels)",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_LN0 &&
                 P_LN7 + 1 == P_DIR && P_DIR + 1 == P_DKSRC && P_DKREL + 1 == P_DTYPE && P_LDECAY7 + 1 == P_LSYNC &&
                 P_ESYNC + 1 == P_LPAN0 && P_LPAN7 + 1 == P_E0 && P_E0 == 135 &&   /* ((1.3) LFO SYNC .. ENV SYNC: P_COUNT
                                                                                * past 127; (2.0, #36) the lane pans) */
                 P_COUNT == PROJ_NP_V3 + 86u && PROJ_NP == 103u && PROJ_NX == 32u && PROJ_NX == PROJ_PX_MAX &&
                 PROJ_NP_V3 == PROJ_NP_V2 + 4u);
    bad += check("SKF1 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3840u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < PROJ_NT; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN6: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE || i == G_CMODE || i == G_CFDBK ? 0 : (int16_t)(500 + i));
    bad += check("FUN2 -> FUN6: globals (id 24, the old drum channel: ROOM; 25 26 CHORUS MODE, FEEDBACK 0) and selected track", ok);
    ok = 1;
    for (t = 0; t < PROJ_NT; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t, t == 3u, 127, 127);   /* (G_DRLVL / G_DRREV 525 / 526: 127; G_CMODE / G_CFDBK now) */
    bad += check("FUN2 -> FUN6: every parameter mapped, SLICER and matrix OFF (4 tracks; more: power-on)", ok && pow_on(&q));

    bad += check("FUN2 -> FUN6: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6)",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);
    bad += check("FUN2 -> FUN6: the drum track -> part 4, DRUM (its kit on load), steps kept (lanes as hits)",
                 q.parts == PROJ_PARTS && q.t[3].engine == ENGI_DRUM && str_eq(ENGINES[ENGI_DRUM]->name, "DRUM") &&
                 TRK_DEF[3][0] == ENGI_DRUM && q.t[3].preset == PROJ_DEF_KEEP &&
                 steps_grid_same(q.t[3].step, v2.t[3].step));

    /* format 3 (1.0, four parts), as written before the modulation matrix */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < G_COUNT; i++)
        v3.g[i] = (int16_t)(600 + i);
    v3.sel = 3;
    v3.parts = PROJ_PARTS;
    for (t = 0; t < PROJ_NT; t++)
        fill_v3_track(&v3.t[t], t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q, &buf, (int)sizeof v3) && proj_ok(&q) && q.magic == PROJ_MAGIC && q.sel == 3 &&
         q.parts == PROJ_PARTS;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE || i == G_CMODE || i == G_CFDBK ? 0 : (int16_t)(600 + i));
    for (t = 0; t < PROJ_NT; t++)
        ok &= track_v3_ok(&q.t[t], &v3.t[t], t);
    bad += check("FUN3 -> FUN6: every parameter kept, the matrix OFF, steps, globals (24: ROOM; 25 26: 0)", ok && pow_on(&q));

    /* a FUN3 written before 1.0 (parts 0: track 4 the drum part, engine byte 0) */
    v3.parts = 0;
    v3.t[3] = v3.t[0];
    v3.t[3].engine = 0;
    v3.t[3].preset = 0;
    v3.g[G_CMODE] = 90;                               /* (the drum level and reverb send: G_DRLVL / G_DRREV then) */
    v3.g[G_CFDBK] = 20;
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) && q2.parts == PROJ_PARTS && q2.t[3].engine == ENGI_DRUM &&
         q2.t[3].preset == PROJ_DEF_KEEP && q2.t[3].p[P_LEVEL] == 90 && q2.t[3].p[P_REV] == 20 &&
         q2.t[3].p[P_PAN] == oldv3(0, P_PAN) && q2.t[3].p[P_SLEN] == oldv3(0, P_SLEN) &&
         q2.t[3].p[P_SLCR] == oldv3(0, P_SLCR) && q2.t[3].p[P_M1SRC] == 0 &&
         steps_grid_same(q2.t[3].step, v3.t[0].step) && !memcmp(&q2.t[0], &q.t[0], sizeof q.t[0]) && pow_on(&q2) &&
         !q2.g[G_CMODE] && !q2.g[G_CFDBK];
    bad += check("FUN3 before 1.0: the drum track -> part 4 (LEVEL / REV from G_DRLVL / G_DRREV), never a later one; "
                 "(phase 9) those ids then 0: CHORUS, no FEEDBACK", ok);

    /* a FUN6 round trip: stored as is (a matrix slot set; an engine added since: SLICE, 8; a DRUM track with
     * notes on its lanes stays so: only an older format is converted) */
    q.t[1].engine = 8;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.t[0].engine = ENGI_DRUM;
    q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 1u << DV_SNARE, 1u << DV_SNARE};
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("FUN6 -> FUN6: as stored (a matrix slot, engine 8, a DRUM step with notes and hits)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8 &&
                 q2.t[2].p[P_M2AMT] == -17 && q2.t[0].step[0].n == 2u && q2.t[0].step[0].hit == 1u << DV_SNARE);
    q.t[0].engine = 3;
    q.t[0].step[0] = q.t[0].step[1];
    q.sum = proj_sum(&q);

    {
        project_v5_t old;
        memset(&old, 0, sizeof old);
        memcpy(old.g, q.g, sizeof old.g); old.sel = q.sel; old.parts = q.parts; old.phys = q.phys;
        for (uint32_t k = 0; k < PROJ_NT; k++) {
            for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = q.t[k].p[j];
            for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = q.t[k].p[P_E0 + j];
            old.t[k].engine = q.t[k].engine; old.t[k].preset = q.t[k].preset;
            for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &q.t[k].step[j], sizeof(step10_t));
        }
        old.magic = PROJ_MAGIC_V5;
        old.size = sizeof old;
        old.sum = proj_hash(&old, sizeof old - 4u);
        bad += check("FUN5 -> FUN6: sounds, grid and reserved lane bytes kept, no chain",
            proj_import(&q2, &old, sizeof old) && !memcmp(q2.t, q.t, sizeof q.t) &&
            !q2.chain.count && chain_valid(&q2.chain));
        old.sum ^= 1u;
        bad += check("FUN5: damaged checksum refused", !proj_import(&q2, &old, sizeof old));
        q.chain.count = 2;
        q.chain.row[0] = (chain_row_t){0, 4};
        q.chain.row[1] = (chain_row_t){3, 16};
        q.sum = proj_sum(&q);
        bad += check("FUN6: chain round trip", proj_import(&q2, &q, sizeof q) &&
            !memcmp(&q.chain, &q2.chain, sizeof q.chain));
        q.chain.row[0].repeat = 0;
        q.sum = proj_sum(&q);
        bad += check("FUN6: invalid row refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));
        chain_defaults(&q.chain);
        q.sum = proj_sum(&q);
    }

    /* format 4 (1.0 development builds): 8-byte steps; a DRUM track's lane notes become its grid */
    to_v4(&v4, &q);
    bad += check("FUN4 image is 2680 bytes (as stored)", sizeof v4 == 2680u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == q.sel &&
         !memcmp(q2.g, q.g, sizeof q.g);
    for (t = 0; t < PROJ_NT; t++)
        ok &= !memcmp(q2.t[t].p, q.t[t].p, sizeof q.t[t].p) && q2.t[t].engine == q.t[t].engine &&
              steps_same(q2.t[t].step, v4.t[t].step);
    bad += check("FUN4 -> FUN6: parameters, engines, globals; steps as they were, no hits", ok && pow_on(&q2));
    {   /* track 2 a DRUM track: BEAT-like steps, a chord, a low tom 41, a crash 49, a step accent, a TIE */
        static const step8_t D[6] = {
            {{36, 0, 0, 0}, 1, ST_NOTE, SF_ACCENT, 96}, {{42, 38, 0, 0}, 2, ST_NOTE, 0, 100},
            {{41, 36, 0, 0}, 2, ST_NOTE, SF_ACCENT, 96}, {{49, 0, 0, 0}, 1, ST_NOTE, 0, 96},
            {{0, 0, 0, 0}, 0, ST_TIE, 0, 0}, {{38, 46, 0, 0}, 2, ST_REST, 0, 96},
        };
        const step_t *s = q2.t[1].step;
        v4.t[1].engine = ENGI_DRUM;
        for (i = 0; i < 6u; i++)
            v4.t[1].step[i] = D[i];
        v4.sum = proj_hash(&v4, sizeof v4 - 4u);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.t[1].engine == ENGI_DRUM;
        ok &= s[0].n == 0 && s[0].hit == 1u << DV_KICK && s[0].acc == 1u << DV_KICK && !(s[0].flags & SF_ACCENT) &&
              s[0].vel == 96 && s[0].time == ST_NOTE;
        ok &= s[1].n == 0 && s[1].hit == ((1u << DV_HATC) | (1u << DV_SNARE)) && !s[1].acc && s[1].vel == 100;
        ok &= s[2].n == 1 && s[2].note[0] == 41 && s[2].hit == 1u << DV_KICK && (s[2].flags & SF_ACCENT) &&
              step_lanes(&s[2]) == ((1u << DV_KICK) | (1u << DV_TOM)) && step_accents(&s[2]) == step_lanes(&s[2]);
        ok &= s[3].n == 1 && s[3].note[0] == 49 && !s[3].hit && step_lanes(&s[3]) == 1u << DV_BELL;
        ok &= s[4].time == ST_TIE && !s[4].hit && s[5].time == ST_REST && s[5].n == 2 && !s[5].hit;
        ok &= steps_same(q2.t[0].step, v4.t[0].step);   /* (not DRUM: as it was) */
        bad += check("FUN4 -> FUN6: a DRUM track's lane notes -> hits (accents too), a low tom / crash kept", ok);
    }
    v3.t[0].p[7]++;
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V3;
    bad += check("FUN6 size with a FUN3 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V2;
    bad += check("FUN6 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0, 0, 0, 0) && q.g[5] == 705 &&
         q.parts == PROJ_PARTS;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == PROJ_DEF_SOUND && q.t[t].engine == trk_def_engine(t) && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN6: track 1 mapped, tracks 2..4 defaults", ok);

    /* PHYS MODEL DRUM (a FUN4 with phys 1) -> the DRUM engine; the old DRUM KIT preset becomes DRUM's */
    {
        static const int16_t OLD_KIT[8] = {4, 64, 70, 64, 64, 100, 0, 0};   /* PHYS DRUM KIT before 1.0 */
        static const int16_t E0[8] = {4, 70, 80, 60, 50, 110, 100, 70}, E1[8] = {2, 10, 20, 30, 40, 50, 60, 70};
        const preset_t *dk = &ENGINES[ENGI_DRUM]->presets[0];
        q.t[0].engine = ENGI_PHYS;                      /* MODEL DRUM, PERC 70 (CYM), KICK 100 (ROUND) */
        q.t[0].preset = 9;
        q.t[1].engine = ENGI_PHYS;                      /* MEMB (2: DUST only before phys 1) */
        q.t[2].engine = ENGI_PHYS;
        q.t[3].engine = ENGI_PHYS;
        for (i = 0; i < 8u; i++) {
            q.t[0].p[P_E0 + i] = E0[i];
            q.t[1].p[P_E0 + i] = E1[i];
            q.t[2].p[P_E0 + i] = OLD_KIT[i];
            q.t[3].p[P_E0 + i] = (int16_t)(i ? 30 : PM_SYMP);
        }
        q.phys = 1;
        for (i = 1; i < NSTEP; i++)                     /* (the other steps rests) */
            q.t[0].step[i] = (step_t){{50, 0, 0, 0}, 1, ST_REST, 0, 0, 0, 0};
        q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 0, 0};
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.phys == PROJ_PHYS &&
             q2.t[0].engine == ENGI_DRUM && q2.t[0].preset == 0 && str_eq(ENGINES[ENGI_DRUM]->name, "DRUM") &&
             q2.t[0].p[P_E0] == 2 && q2.t[0].p[P_E6] == 1 && q2.t[0].p[P_E7] == 0;
        for (i = 1; i < 6u; i++)
            ok &= q2.t[0].p[P_E0 + i] == E0[i];         /* TUNE TONE DECY SNAP ACC in place */
        ok &= q2.t[1].engine == ENGI_PHYS && !memcmp(q2.t[1].p, q.t[1].p, sizeof q.t[1].p);
        ok &= q2.t[3].engine == ENGI_PHYS && !memcmp(q2.t[3].p, q.t[3].p, sizeof q.t[3].p);
        ok &= q2.t[2].engine == ENGI_DRUM;              /* the old DRUM KIT: DRUM's DRUM KIT */
        for (i = 0; i < 8u; i++)
            ok &= q2.t[2].p[P_E0 + i] == dk->e[i];
        ok &= !memcmp(&q2.t[0].step[1], &q.t[0].step[1], sizeof q.t[0].step - sizeof(step_t)) &&
              q2.t[0].p[P_LEVEL] == q.t[0].p[P_LEVEL];
        ok &= q2.t[0].step[0].n == 0 && q2.t[0].step[0].hit == ((1u << DV_KICK) | (1u << DV_HATC));   /* the grid */
        ok &= !memcmp(q2.t[1].step, q.t[1].step, sizeof q.t[1].step);   /* (PHYS: as they were) */
        bad += check("FUN4 phys 1: PHYS MODEL DRUM -> DRUM engine (its lane notes as hits), MEMB kept", ok);
        memcpy(&buf, &q2, sizeof q2);
        bad += check("FUN6 after it: imported again, as it is", proj_import(&q, &buf, (int)sizeof q2) &&
                                                                !memcmp(&q, &q2, sizeof q));
        q.phys = 0;                                     /* before 1.0: MODEL 2 was DUST */
        q.t[1].engine = ENGI_PHYS;
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.phys == PROJ_PHYS && q2.t[1].engine == ENGI_PHYS &&
             q2.t[1].p[P_E0] == PM_MODAL && q2.t[1].p[P_E6] == 64;
        bad += check("FUN4 phys 0: PHYS DUST -> MODAL bowed", ok);
    }

    {   /* the FUN7 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("SKF1 name at the end of the reserved tail, the FM6 patches before it, after the data (32 spare)",
                     PROJ_NAME_OFF == 3824u && PROJ_FM6_OFF == 3312u && 68u + PROJ_NT * (PROJ_NP + 2u + NSTEP * 9u +
                     PROJ_NUDGE_BYTES) + sizeof(chain_config_t) + sizeof(motion_store_t) + 32u == PROJ_FM6_OFF);
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN7 no name: the tail all zero (as firmware before wrote it)", ok && zero);
        bad += check("FUN7 of before names (zero tail): loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN7 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN7 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
        bad += check("FUN6..FUN1 imports: no name", !q2.name[0] && !q.name[0]);
    }

    {   /* (Skiff) SKF1: each step's NUDGE, -32..31, in its track's plane; a FUN9 (no planes) loads on the grid */
        static project_t a, c;
        static project_store_t st;
        static uint8_t v9[PROJ_STORE_V9];
        static const int8_t ND[6] = {NUDGE_MIN, NUDGE_MAX, -1, 1, 17, -20};
        uint32_t i, k;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < PROJ_NT; k++)                   /* ((phase 9) SKF1 holds none past PROJ_NP: their defaults) */
            for (i = PROJ_NP - 8u; i < P_E0; i++)
                a.t[k].p[i] = TP[i].def;
        for (k = 0; k < PROJ_NT; k++)
            for (i = 0; i < NSTEP; i += 5u) {
                a.t[k].step[i] = (step_t){{(uint8_t)(48u + i)}, 1, ST_NOTE, 0, 100};
                a.t[k].step[i].nudge = ND[(i + k) % 6u];
            }
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st) && !memcmp(c.t, a.t, PROJ_NT * sizeof a.t[0]) && pow_on(&c);
        bad += check("SKF1: each step's NUDGE round trips (-32, 31, -1, 1, ..), the rest of the step kept", ok);
        a.motion.count = 1;                             /* ((phase 9) a lock on DRIVE's TONE: SKF1 cannot name it) */
        a.motion.event[0] = (motion_event_t){.place = 1u << 6 | 2u, .lock = 1, .id = P_DTONE, .value = 5};
        bad += check("  (phase 9) SKF1 refuses a motion record of a parameter added since 103", !proj_pack(&st, &a));
        a.motion.count = 0;
        a.t[2].step[7].nudge = NUDGE_MAX + 1;
        bad += check("SKF1: a NUDGE outside -32..31 cannot be packed", !proj_pack(&st, &a));
        a.t[2].step[7].nudge = 0;
        ok = proj_pack(&st, &a);
        fun9_from(v9, &st);                             /* the same project as FUN9: no planes, its tail shorter */
        ok = ok && proj_import(&c, v9, sizeof v9);
        for (k = 0; ok && k < PROJ_NT; k++)
            for (i = 0; i < NSTEP; i++)
                ok &= !c.t[k].step[i].nudge && c.t[k].step[i].n == a.t[k].step[i].n && c.t[k].step[i].note[0] == a.t[k].step[i].note[0];
        bad += check("FUN9 (Felucca 1.1, Skiff before NUDGE, 100 parameters): loads, every step on the grid", ok);
        memset(&st, 0xA5, sizeof st);
        memcpy(st.raw, v9, sizeof v9);
        bad += check("  in a retained (longer) slot: loads", proj_import(&c, &st, sizeof st) && c.t[0].step[5].note[0] == 53u);
    }

    {   /* FUN8: each track's FM6 patch travels with the project; FUN7 and older get the init patch */
        project_t a, c;
        static project_store_t st, slot;
        static uint8_t v7[PROJ_STORE_V7], v9[PROJ_STORE_V9];
        uint32_t i, k, init = 1, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < PROJ_NT; k++) {
            memcpy(a.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            a.t[k].engine = ENGI_FM6;
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == 0x534B4631u && proj_import(&c, &st, sizeof st);
        for (k = 0; k < PROJ_NT; k++)
            ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED) && c.t[k].engine == ENGI_FM6 &&
                  !memcmp(st.raw + PROJ_FM6_OFF + k * FM6_PACKED, FM6_FACTORY[k * 2u], FM6_PACKED);
        bad += check("SKF1: the four FM6 patches round trip (bytes at PROJ_FM6_OFF)", ok && !memcmp(c.name, "FM SONG", 7));
        {   /* (phase 6) loaded and saved again: the same bytes (its motion through the block's own codec) */
            static project_store_t st2;
            project_t m = a;
            m.motion.count = 2; m.motion.on = 9;
            m.motion.event[0] = (motion_event_t){.place = 3u << 6 | 63u, .lock = 1, .id = P_E3, .value = -64};
            m.motion.event[1] = (motion_event_t){.place = 0u << 6 | 1u, .id = P_REV, .value = 127};
            m.sum = proj_sum(&m);
            ok = proj_pack(&st2, &m) && proj_import(&c, &st2, sizeof st2) && !memcmp(&c.motion, &m.motion, sizeof m.motion);
            st = st2;
            bad += check("SKF1 loaded and saved again: byte for byte (a lock on T4 step 64 at -64 too)",
                         ok && proj_pack(&st2, &c) && !memcmp(&st, &st2, sizeof st));
            m.motion.on = (uint8_t)((1u << NTRK) - 1u);        /* every track's automation on (as at power-on) */
            m.sum = proj_sum(&m);
            ok = proj_pack(&st2, &m) && st2.raw[68u + PROJ_NT * (PROJ_NP + 2u + NSTEP * 9u + PROJ_NUDGE_BYTES) +
                                               sizeof(chain_config_t) + 1u] == 0x0Fu &&
                 proj_import(&c, &st2, sizeof st2) && c.motion.on == 0x0Fu && c.motion.count == 2u;
            bad += check("  saved with every track's automation on: SKF1 holds the four tracks' bits (more: left out)", ok);
            m.motion.count = 3;                                /* (phase 6) a record of the last track between them */
            m.motion.event[2] = m.motion.event[1];
            m.motion.event[1] = (motion_event_t){.place = (uint16_t)((NTRK - 1u) << 6 | 4u), .id = P_REV, .value = 33};
            m.sum = proj_sum(&m);
            ok = proj_pack(&st2, &m) && proj_import(&c, &st2, sizeof st2) && motion_valid(&c.motion);
            ok &= c.motion.count == 2u && c.motion.event[1].value == 127;
            bad += check("  a track past four: its records left out, the others' closing up", ok);
            m.motion.count = 2; m.motion.event[1] = m.motion.event[2];   /* (back to two: then a value of 200 stored) */
            m.sum = proj_sum(&m);
            ok = proj_pack(&st2, &m);
            st2.raw[68u + PROJ_NT * (PROJ_NP + 2u + NSTEP * 9u + PROJ_NUDGE_BYTES) + sizeof(chain_config_t) + 4u + 2u] = 200u;
            st2.raw[68u + PROJ_NT * (PROJ_NP + 2u + NSTEP * 9u + PROJ_NUDGE_BYTES) + sizeof(chain_config_t) + 4u + 3u] = 0u;
            {
                uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
                memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
            }
            bad += check("  a stored record's value outside -64..127 (200): refused, as before",
                         ok && !proj_import(&c, &st2, sizeof st2));
            ok = proj_pack(&st, &a);
        }
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("SKF1: a patch byte changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        {   /* FUN8 as 1.0.x wrote it (3584 bytes, 91 parameters: E0..E7 at 83..90, the patches at 3056): the DRUM
             * lane levels 100 %, E0..E7 and their motion at today's P_E0.., the patches and the name kept */
            static uint8_t v8[3584];
            uint32_t pos = 68u, pos9 = 68u, j;
            project_t e = a;
            e.t[3].engine = ENGI_DRUM; e.t[3].p[P_E3] = 30;
            e.motion.count = 2; e.motion.on = 8;
            e.motion.event[0] = (motion_event_t){.place = 3u << 6 | 2u, .id = P_E3, .value = 99};
            e.motion.event[1] = (motion_event_t){.place = 3u << 6 | 4u, .id = P_REV, .value = 50};
            {   /* FUN9 holds parameter locks (bit 7 of the id): on a DRUM lane level and on an engine value */
                project_t f = e, g;
                f.motion.count = 4;
                f.motion.event[2] = (motion_event_t){.place = 3u << 6 | 5u, .lock = 1, .id = P_LN2, .value = 40};
                f.motion.event[3] = (motion_event_t){.place = 3u << 6 | 6u, .lock = 1, .id = P_E5, .value = -3};
                f.sum = proj_sum(&f);
                ok = proj_pack(&st, &f) && proj_import(&g, &st, sizeof st) && !memcmp(&f.motion, &g.motion, sizeof f.motion);
                bad += check("SKF1 round trip: locks on a lane level (P_LN2) and an engine value (P_E5) kept", ok &&
                             st.raw[68u + PROJ_NT * (PROJ_NP + 2u + NSTEP * 9u + PROJ_NUDGE_BYTES) + sizeof(chain_config_t) +
                                    4u + 8u + 1u] ==
                             (P_LN2 | MOTION_LOCK) && (g.motion.event[3].id == P_E5 && g.motion.event[3].lock));
            }
            e.motion.count = 3;   /* a FUN8 of a 1.1 development build (91 parameters) could hold a lock: its E5 then */
            e.motion.event[2] = (motion_event_t){.place = 3u << 6 | 6u, .lock = 1, .id = P_E5, .value = -3};
            e.sum = proj_sum(&e);
            ok = proj_pack(&st, &e);
            memset(v8, 0, sizeof v8);
            memcpy(v8, st.raw, 68);
            v8[66] = 91;
            for (k = 0; k < PROJ_NT; k++) {
                for (j = 0; j < 91u; j++) v8[pos++] = st.raw[pos9 + (j < 83u ? j : j + (PROJ_NP - 8u - 83u))];
                pos9 += PROJ_NP;
                memcpy(v8 + pos, st.raw + pos9, 2u + NSTEP * 9u);
                pos += 2u + NSTEP * 9u; pos9 += 2u + NSTEP * 9u + PROJ_NUDGE_BYTES;   /* (Skiff: SKF1's NUDGE plane) */
            }
            memcpy(v8 + pos, st.raw + pos9, sizeof(chain_config_t) + sizeof(motion_store_t));
            v8[pos + sizeof(chain_config_t) + 4u + 1u] = 83u + 3u;   /* its E3 then */
            v8[pos + sizeof(chain_config_t) + 4u + 8u + 1u] = (83u + 5u) | MOTION_LOCK;   /* its E5 then, a lock */
            memcpy(v8 + 3056u, st.raw + PROJ_FM6_OFF, PROJ_NT * FM6_PACKED);
            memcpy(v8 + 3568u, st.raw + PROJ_NAME_OFF, PROJ_NAME_LEN);
            ((uint32_t *)v8)[0] = 0x46554E38u;
            ((uint32_t *)v8)[1] = 3584u;
            sum = proj_hash(v8, 3580u);
            memcpy(v8 + 3580u, &sum, 4);
            ok = ok && proj_import(&c, v8, sizeof v8);
            for (k = 0; ok && k < PROJ_NT; k++) {
                for (j = P_LN0; j <= P_LN7; j++) ok &= c.t[k].p[j] == 127;
                ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            }
            bad += check("FUN8 of 1.0.x (91 parameters): lane levels 100 %, E0..E7 at P_E0, patches and name kept", ok &&
                         c.t[3].engine == ENGI_DRUM && c.t[3].p[P_E3] == 30 && c.t[2].p[P_E7] == 4 &&
                         !memcmp(c.name, "FM SONG", 7) && pow_on(&c));
            bad += check("  its motion: E3 (86 then) at P_E3, REV kept", c.motion.count == 3u &&
                         (c.motion.event[0].id == P_E3 && !c.motion.event[0].lock) && c.motion.event[0].value == 99 && (c.motion.event[1].id == P_REV && !c.motion.event[1].lock));
            bad += check("  a lock on its E5 (88 then): at P_E5, still a lock", (c.motion.event[2].id == P_E5 && c.motion.event[2].lock) &&
                         c.motion.event[2].value == -3);
            bad += check("  one byte short: refused", !proj_import(&c, v8, sizeof v8 - 1u));
            memset(&slot, 0xA5, sizeof slot);
            memcpy(slot.raw, v8, sizeof v8);
            bad += check("  in a retained (longer) slot: loads", proj_import(&c, &slot, sizeof slot) && c.t[3].p[P_E3] == 30);
        }
        /* the same music as FUN7 (3388 bytes, no patches): the data where it was, the name at 3372 */
        fun9_from(v9, &st);                             /* ((Skiff) SKF1 -> FUN9's layout first) */
        memcpy(v7, v9, PROJ_STORE_V7 - 16u);
        memset(v7 + 3120u, 0, PROJ_STORE_V7 - 16u - 3120u);
        memcpy(v7 + PROJ_STORE_V7 - 16u, st.raw + PROJ_NAME_OFF, 12);
        ((uint32_t *)v7)[0] = 0x46554E37u;
        ((uint32_t *)v7)[1] = PROJ_STORE_V7;
        sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
        memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
        ok = proj_import(&c, v7, sizeof v7);
        for (k = 0; k < NTRK; k++)
            init &= !memcmp(c.fm6[k], FM6_INIT, FM6_PACKED);
        bad += check("FUN7 -> FUN9: the music and name kept, every track the init patch",
                     ok && init && c.t[2].engine == ENGI_FM6 && c.t[2].p[P_E7] == 4 && !memcmp(c.name, "FM SONG", 7));
        {   /* REVERB TYPE (id 24) of a FUN7 holding the old drum channel (10): ROOM; SPRING (1) kept */
            int16_t g24 = 10;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok = proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 0 && proj_ok(&c);
            g24 = 1;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok &= proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 1;
            bad += check("FUN7 REVERB TYPE above SPRING (an old drum channel): ROOM", ok);
        }
        ok = proj_import(&c, v7, sizeof v7 - 1u);
        bad += check("FUN7 one byte short: refused", !ok);
        /* the retained cache after an update: slot 1's FUN7 record, longer slot, garbage after it */
        memset(&slot, 0xA5, sizeof slot);
        memcpy(slot.raw, v7, sizeof v7);
        bad += check("a retained 3840-byte slot holding a FUN7 record loads it", proj_import(&c, &slot, sizeof slot) &&
                     c.t[2].p[P_E7] == 4 && !memcmp(c.fm6[0], FM6_INIT, FM6_PACKED));
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        for (i = 0; i < FM6_PACKED; i++) a.fm6[1][i] = (uint8_t)(i | 0x80u);
        a.sum = proj_sum(&a);
        bad += check("FUN8: a patch byte above 127 cannot be packed", !proj_pack(&st, &a));
        memcpy(a.fm6[1], FM6_INIT, FM6_PACKED);
        a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8: a step out of range packs inside it (note, velocity 127; accents on hits): it loads",
                     ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                     c.t[0].step[3].hit == 0x05u && c.t[0].step[3].acc == 0x05u);
    }

#if !FELUCCA_FM4
    {   /* DIGITAL tracks (engine 1, retired: src/fm4_convert.c) in FUN8 / FUN7 / FUN4 / FUN1 projects load as FM6 with
         * the converted patch as the track's own; their motion on the EDIT or OP ENV values goes, the rest stays */
        project_t a, c, d;
        static project_store_t st;
        static uint8_t v7[PROJ_STORE_V7], v9[PROJ_STORE_V9];
        static project_v4_t w4;
        static project_v1_t w1;
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        int16_t p[P_COUNT];
        uint32_t i, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(t == 1u ? ENGI_DIGITAL : 0u, i)->def;
            a.t[t].engine = t == 1u ? ENGI_DIGITAL : 0u;
            a.t[t].preset = t == 1u ? 5u : 0u;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        fm4_preset_values(a.t[1].p, 5);                 /* DIGITAL PAD, an OP ENV edit on op 2 */
        a.t[1].p[P_FM2_ATK] = 30;
        a.t[1].p[P_FM2_LEVEL] = 100;
        a.t[1].p[P_LEVEL] = 90;
        memcpy(p, a.t[1].p, sizeof p);
        fm4_convert(p, v);
        fm6_pack(v, pk);
        a.motion.count = 5;
        a.motion.event[0] = (motion_event_t){.place = 1u << 6 | 3u, .id = P_E4, .value = 20};          /* track 2: INDEX */
        a.motion.event[1] = (motion_event_t){.place = 1u << 6 | 4u, .id = P_FM1_LEVEL, .value = 50};   /* track 2: OP LEVEL */
        a.motion.event[2] = (motion_event_t){.place = 1u << 6 | 5u, .id = P_CHOR, .value = 60};        /* track 2: a send: stays */
        a.motion.event[3] = (motion_event_t){.place = 0u << 6 | 3u, .id = P_E4, .value = 70};          /* track 1 (ANALOG): stays */
        a.motion.event[4] = (motion_event_t){.place = 1u << 6 | 9u, .id = P_E7, .value = 0};           /* track 2: E7 */
        a.sum = proj_sum(&a);
#define FM4_OK(c) ((c).t[1].engine == ENGI_FM6 && (c).t[1].preset == 4u && !memcmp((c).fm6[1], pk, FM6_PACKED) && \
                   !memcmp((c).t[1].p, p, sizeof p) && (c).t[0].engine == 0u && !memcmp((c).fm6[0], FM6_INIT, FM6_PACKED))
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8 with a DIGITAL track: FM6, the converted patch its own, PTCH / preset FM6 PAD", ok && FM4_OK(c));
        bad += check("  its motion on INDEX / OP LEVEL / E7 goes; a send's and the other track's stay",
                     c.motion.count == 2u && (c.motion.event[0].id == P_CHOR && !c.motion.event[0].lock) && c.motion.event[1].place == 3u &&
                     (c.motion.event[1].id == P_E4 && !c.motion.event[1].lock) && motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (FM6 now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
        fun9_from(v9, &st);                             /* the same as FUN7 (as the FUN8 block above) */
        memcpy(v7, v9, PROJ_STORE_V7 - 16u);
        memset(v7 + 3120u, 0, PROJ_STORE_V7 - 16u - 3120u);
        memset(v7 + PROJ_STORE_V7 - 16u, 0, 12);
        ((uint32_t *)v7)[0] = 0x46554E37u;
        ((uint32_t *)v7)[1] = PROJ_STORE_V7;
        sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
        memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
        bad += check("FUN7 with a DIGITAL track: the same", proj_import(&c, v7, sizeof v7) && FM4_OK(c) &&
                     c.motion.count == 2u);
        memset(&a.motion, 0, sizeof a.motion);
        a.sum = proj_sum(&a);
        to_v4(&w4, &a);
        memcpy(&buf, &w4, sizeof w4);
        memcpy(p, a.t[1].p, sizeof p);                  /* (FUN4 had no OP ENV values: they load as defaults) */
        for (i = P_FM1_ATK; i <= P_FM4_LEVEL; i++)
            p[i] = TP[i].def;
        fm4_convert(p, v);
        fm6_pack(v, pk);
        ok = proj_import(&c, &buf, (int)sizeof w4) && FM4_OK(c);
        bad += check("FUN4 with a DIGITAL track (no OP ENV then: their defaults): FM6, converted", ok);
        memset(&w1, 0, sizeof w1);
        w1.magic = PROJ_MAGIC_V1;
        w1.size = sizeof w1;
        for (i = 0; i < PROJ_NG_V2; i++)
            w1.g[i] = GP[i].def;
        for (i = 0; i < PROJ_NP_V2 - 8u; i++)
            w1.t.p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            w1.t.p[PROJ_NP_V2 - 8u + i] = DIGITAL_PRESETS[1].e[i];   /* BELL */
        w1.t.engine = ENGI_DIGITAL;
        w1.t.preset = 1;
        w1.sum = proj_hash(&w1, sizeof w1 - 4u);
        memcpy(&buf, &w1, sizeof w1);
        ok = proj_import(&c, &buf, (int)sizeof w1) && c.t[0].engine == ENGI_FM6 && c.t[0].preset == 1u &&
             c.t[1].engine == ENGI_FM6 && c.t[1].preset == PROJ_DEF_SOUND;   /* (track 2: the power-on FM6 PAD) */
        bad += check("FUN1 with DIGITAL: track 1 FM6 (BELL), track 2 the power-on sound (FM6 now)", ok);
#undef FM4_OK
    }
#endif
    {   /* SAMPLE tracks of SET 4 (PERC, the GM kit, retired after 1.0.2) in FUN8 / FUN4 projects load as DRUM with its
         * default kit: the E values the kit's, the rest of the sound, the steps and the preset byte's meaning kept; their
         * motion on the EDIT values goes, the rest stays; another SAMPLE set stays SAMPLE. Idempotent */
        project_t a, c, d;
        static project_store_t st;
        static project_v4_t w4;
        static const int16_t KIT[8] = DRUM_KIT_E;
        uint32_t i;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(ENGI_SAMPLE, i)->def;
            a.t[t].engine = ENGI_SAMPLE;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        a.t[2].p[P_E0] = SMP_SET_PERC;                  /* track 3: a saved PERC sound (SET 4, no loop) */
        a.t[2].p[P_E3] = 0;
        a.t[2].p[P_LEVEL] = 77; a.t[2].p[P_REV] = 41; a.t[2].p[P_PAN] = -9;
        a.t[2].preset = 4;
        a.t[2].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, SF_ACCENT, 100, 0, 0};
        a.t[2].step[4] = (step_t){{38, 49, 0, 0}, 2, ST_NOTE, 0, 90, 0, 0};
        a.t[0].p[P_E0] = 2;                             /* track 1: FLUTE stays */
        a.motion.count = 3;
        a.motion.event[0] = (motion_event_t){.place = 2u << 6 | 3u, .id = P_E4, .value = 20};          /* track 3: CUT: goes */
        a.motion.event[1] = (motion_event_t){.place = 2u << 6 | 5u, .id = P_REV, .value = 60};         /* track 3: a send: stays */
        a.motion.event[2] = (motion_event_t){.place = 0u << 6 | 3u, .id = P_E4, .value = 70};          /* track 1: stays */
        a.sum = proj_sum(&a);
#define PERC_OK(c) ((c).t[2].engine == ENGI_DRUM && (c).t[2].preset == 0u && !memcmp(&(c).t[2].p[P_E0], KIT, sizeof KIT) && \
                    !memcmp((c).t[2].p, a.t[2].p, P_E0 * sizeof(int16_t)) && \
                    !memcmp((c).t[2].step, a.t[2].step, sizeof a.t[2].step) && \
                    (c).t[0].engine == ENGI_SAMPLE && (c).t[0].p[P_E0] == 2 && (c).t[1].engine == ENGI_SAMPLE)
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8 with a SAMPLE PERC track: DRUM's kit, the rest of the sound and the steps kept", ok && PERC_OK(c) &&
                     str_eq(ENGINES[ENGI_DRUM]->presets[0].name, "DRUM KIT") &&
                     !memcmp(ENGINES[ENGI_DRUM]->presets[0].e, (int8_t[8])DRUM_KIT_E, 8));
        bad += check("  its motion on CUT goes; a send's and the other track's stay",
                     c.motion.count == 2u && (c.motion.event[0].id == P_REV && !c.motion.event[0].lock) && c.motion.event[1].place == 3u &&
                     motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (DRUM now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
        memset(&a.motion, 0, sizeof a.motion);
        a.sum = proj_sum(&a);
        to_v4(&w4, &a);
        memcpy(&buf, &w4, sizeof w4);
        ok = proj_import(&c, &buf, (int)sizeof w4) && PERC_OK(c);
        bad += check("FUN4 with a SAMPLE PERC track: DRUM's kit, the same", ok);
#undef PERC_OK
    }
    {   /* (Skiff phase 5) SKF2: no steps; each track's list of patterns, the entry it is on, SONG, the scenes */
        static project_t a, c;
        static project_store_t st, st2;
        uint32_t e, sum;
        memset(&a, 0, sizeof a);
        for (t = 0; t < PROJ_NT; t++) {
            a.t[t].engine = (uint8_t)(t == 3u ? ENGI_DRUM : 0u);
            for (i = 0; i < P_COUNT; i++)                 /* ((phase 9) SKF2 holds none past PROJ_NP: their defaults) */
                a.t[t].p[i] = (int16_t)clamp(param_desc_of(a.t[t].engine, i)->def +
                                             (i >= PROJ_NP - 8u && i < P_E0 ? 0 : (int32_t)t), -64, 127);
        }
        a.parts = PROJ_PARTS; a.phys = PROJ_PHYS; a.sel = 2; a.g[G_BPM] = 133;
        a.ent[0][0][0] = 5 + 1; a.ent[0][0][1] = 2 - 1; a.ent[0][3][0] = 63 + 1; a.ent[3][15][0] = 0 + 1; a.ent[3][15][1] = 16 - 1;
        a.cur[0] = 3; a.cur[3] = 15; a.song = 1; a.scene[0] = 0x21; a.scene[49] = 0x80;
        memcpy(a.name, "SKF2 TEST", 9);
        a.fm6[1][5] = 77;
        ok = proj_pack2(&st, &a, &PROJ_SKF2) && proj_import(&c, &st, (int)PROJ_STORE2);
        for (t = 0; t < PROJ_NT; t++)
            ok &= c.t[t].engine == a.t[t].engine && !memcmp(c.t[t].p, a.t[t].p, sizeof a.t[t].p) &&
                  !c.t[t].step[0].n;
        bad += check("SKF2: lists, entries, SONG, scenes, sounds, name round trip", ok && !memcmp(c.ent, a.ent, sizeof a.ent) &&
                     !memcmp(c.cur, a.cur, sizeof a.cur) && c.song == 1u && !memcmp(c.scene, a.scene, sizeof a.scene) &&
                     !memcmp(c.name, a.name, sizeof a.name) && c.fm6[1][5] == 77u && c.sel == 2u && c.g[G_BPM] == 133 &&
                     !c.motion.count && pow_on(&c));
        bad += check("  1280 bytes, one flash object; read from a retained 3840-byte slot too", PROJ_STORE2 <= 4096u - 256u &&
                     proj_import(&c, &st, (int)sizeof st) && c.ent[0][3][0] == 63u + 1u);
        bad += check("  loaded and saved again: byte for byte", proj_pack2(&st2, &c, &PROJ_SKF2) && !memcmp(&st, &st2, sizeof st));
        st2 = st; st2.raw[100] ^= 1u;
        ok = !proj_import(&c, &st2, (int)PROJ_STORE2);
#define SKF2_BAD(off, v) (st2 = st, st2.raw[(off)] = (v), sum = proj_hash(st2.raw, PROJ_STORE2 - 4u), \
                          memcpy(st2.raw + PROJ_STORE2 - 4u, &sum, 4), !proj_import(&c, &st2, (int)PROJ_STORE2))
        e = 68u + PROJ_NT * (PROJ_NP + 2u);           /* the lists */
        ok &= SKF2_BAD(e, PAT_REST + 1u) && SKF2_BAD(e + 1u, REPS_CHAIN + 1u) && SKF2_BAD(e + PROJ_NT * NENT * 2u, 16u) &&
              SKF2_BAD(65u, 3u) && !SKF2_BAD(65u, SONG_ONCE) && !SKF2_BAD(e, NPAT) && !SKF2_BAD(e, PAT_REST) &&
              !SKF2_BAD(e + 1u, 15u) && !SKF2_BAD(e + 1u, REPS_CHAIN);
#undef SKF2_BAD
        bad += check("  refused: a damaged hash, an id past the pool and REST, repeats past CHAIN, an entry past 16, SONG 3; "
                     "(#28) a REST read, ((2.0) SONG ONCE, a CHAIN)", ok);
        a.t[0].step[0].n = 1; a.t[0].step[0].note[0] = 60; a.t[0].step[0].time = ST_NOTE;
        ok = proj_pack(&st, &a) && proj_import(&c, &st, (int)sizeof st) && c.t[0].step[0].note[0] == 60u &&
             !c.ent[0][0][0] && !c.ent[0][0][1] && !c.cur[0] && !c.song && !c.scene[0];
        bad += check("  an SKF1 (and every older format) loads with no lists: its steps as before", ok);
    }
    {   /* (phase 6) SKF3: SKF2 for six tracks and their scenes, 2048 bytes */
        static project_t a, c;
        static project_store_t st, st2;
        uint32_t e, sum, k = NTRK - 1u;
        memset(&a, 0, sizeof a);
        for (t = 0; t < NTRK; t++) {
            a.t[t].engine = (uint8_t)(t == 3u || t == 5u ? ENGI_DRUM : t == 4u ? ENGI_FM6 : 0u);   /* (not SAMPLE: E0 4 is PERC) */
            a.t[t].preset = (uint8_t)t;
            for (i = 0; i < P_COUNT; i++)                 /* ((2.0, #36) the lane pans: SKF4's alone, their defaults) */
                a.t[t].p[i] = (int16_t)clamp(param_desc_of(a.t[t].engine, i)->def +
                                             (i >= P_LPAN0 && i < P_E0 ? 0 : (int32_t)t), -64, 127);
        }
        a.parts = PROJ_PARTS; a.phys = PROJ_PHYS; a.sel = (uint8_t)k; a.g[G_BPM] = 97;
        a.ent[k][15][0] = 63 + 1; a.ent[k][15][1] = 7; a.cur[k] = 15; a.song = 1;
        a.scene[0] = 0x21; a.scene[PROJ_SC2] = 0x05; a.scene[PROJ_SCENES - 1u] = 0x80;
        memcpy(a.name, "SKF3 TEST", 9);
        a.fm6[k][5] = 77;
        ok = proj_pack2(&st, &a, &PROJ_SKF3) && proj_import(&c, &st, (int)PROJ_STORE3);
        for (t = 0; t < NTRK; t++)
            ok &= c.t[t].engine == a.t[t].engine && c.t[t].preset == a.t[t].preset &&
                  !memcmp(c.t[t].p, a.t[t].p, sizeof a.t[t].p) && !c.t[t].step[0].n;
        bad += check("SKF3: six tracks' sounds, lists, entries, SONG, scenes, FM6 patches, name round trip", ok &&
                     !memcmp(c.ent, a.ent, sizeof a.ent) && !memcmp(c.cur, a.cur, sizeof a.cur) && c.song == 1u &&
                     !memcmp(c.scene, a.scene, sizeof a.scene) && !memcmp(c.name, a.name, sizeof a.name) &&
                     c.fm6[k][5] == 77u && c.sel == k && c.g[G_BPM] == 97);
        e = 68u + NTRK * (PROJ_NP + 2u);              /* the lists (698 with 103 parameters), the entries, the scenes */
        ok = ((const uint32_t *)st.raw)[0] == PROJ_MAGIC3 && ((const uint32_t *)st.raw)[1] == 2048u &&
             st.raw[e + (k * NENT + 15u) * 2u] == 64u && st.raw[e + NTRK * NENT * 2u + k] == 15u &&
             st.raw[e + NTRK * NENT * 2u + NTRK + PROJ_SC2] == 0x05u && e + NTRK * NENT * 2u + NTRK + 82u <= 1264u &&
             st.raw[1264u + k * FM6_PACKED + 5u] == 77u && !memcmp(st.raw + 2032u, "SKF3 TEST", 9);
        bad += check("  2048 bytes: lists, entries, 82 scene bytes after the six tracks; FM6 at 1264, the name at 2032", ok);
        ok = PROJ_PX_AT(&PROJ_SKF3) == 981u && st.raw[981] == PROJ_PX_MARK && st.raw[982] == PROJ_NX &&
             st.raw[983u + k * PROJ_NX + (P_DTONE - (PROJ_NP - 8u))] == (uint8_t)(a.t[k].p[P_DTONE] + 64);
        bad += check("  (phase 9) the parameters past 103: the PX block at 981 (its mark, 22 a track), round trip", ok);
        {
            static project_t d;
            d = a;
            for (t = 0; t < NTRK; t++)
                for (i = PROJ_NP - 8u; i < P_E0; i++)
                    d.t[t].p[i] = TP[i].def;
            ok = proj_pack2(&st2, &d, &PROJ_SKF3) && !st2.raw[981] && proj_import(&c, &st2, (int)PROJ_STORE3) &&
                 c.t[1].p[P_EG2_DEC] == TP[P_EG2_DEC].def && c.t[1].p[P_LTUNE3] == 64;
            bad += check("  .. all at their defaults: no block (a project of before), read as the defaults", ok);
        }
        bad += check("  read from a retained 3840-byte slot too; loaded and saved again: byte for byte",
                     proj_import(&c, &st, (int)sizeof st) && c.ent[k][15][0] == 64u &&
                     proj_pack2(&st2, &c, &PROJ_SKF3) && !memcmp(&st, &st2, sizeof st));
        st2 = st; st2.raw[100] ^= 1u;
        ok = !proj_import(&c, &st2, (int)PROJ_STORE3);
        st2 = st; ((uint32_t *)st2.raw)[1] = PROJ_STORE2;
        ok &= !proj_import(&c, &st2, (int)PROJ_STORE3);
        bad += check("  refused: a changed byte, another size in it", ok);
        a.t[k].engine = ENGI_PHYS; a.t[k].preset = 3;   /* (no firmware writes one) */
        ok = proj_pack2(&st, &a, &PROJ_SKF3) && proj_import(&c, &st, (int)PROJ_STORE3) &&
             c.t[k].engine == trk_def_engine(k) && c.t[k].preset == PROJ_DEF_SOUND && c.ent[k][15][0] == 64u;
        bad += check("  a crafted SKF3 with PHYS on T6: T6's power-on sound, its list kept", ok);
        a.t[k].engine = ENGI_DRUMX; a.t[k].preset = 0;   /* (phase 7) the project's DRUM-X kit */
        memcpy(a.dx, DX_KIT_DEF, sizeof a.dx);
        a.dx[DV_SNARE].b[DXP_COLOR] = 9;
        a.dx[DV_BELL].mode = (uint8_t)(DX_MODE(DXW_BELL, DXF_LP, 1) | DX_DRIVE | DXM_SINE << 5);
        a.dx_on = 1;
        ok = proj_pack2(&st, &a, &PROJ_SKF3) && st.raw[1175] == PROJ_DX_MARK && st.raw[1176] == a.dx[0].mode &&
             proj_import(&c, &st, (int)PROJ_STORE3) && c.dx_on && !memcmp(c.dx, a.dx, sizeof a.dx) &&
             c.t[k].engine == ENGI_DRUMX && proj_pack2(&st2, &c, &PROJ_SKF3) && !memcmp(&st, &st2, sizeof st);
        bad += check("  (phase 7) the DRUM-X kit at 1175 (its mark, 88 bytes): round trip, byte for byte", ok);
        st.raw[1176u + 3u] = 200u;                   /* (a value past 127: a damaged kit) */
        sum = proj_hash(st.raw, PROJ_STORE3 - 4u); memcpy(st.raw + PROJ_STORE3 - 4u, &sum, 4);
        ok = proj_import(&c, &st, (int)PROJ_STORE3) && !c.dx_on;
        a.dx_on = 0;
        ok &= proj_pack2(&st, &a, &PROJ_SKF3) && !st.raw[1175] && proj_import(&c, &st, (int)PROJ_STORE3) && !c.dx_on;
        bad += check("  no kit (a record of before: 0 there; a damaged one): none, the factory's when loaded", ok);
        a.dx_on = 1; memcpy(a.dx, DX_KIT_DEF, sizeof a.dx);
        ok = proj_pack2(&st, &a, &PROJ_SKF2) && proj_import(&c, &st, (int)PROJ_STORE2) && !c.dx_on;
        bad += check("  (phase 7) SKF2 holds no DRUM-X kit (SKF3 does)", ok);
    }
    {   /* (2.0, Tier B 5) SKF4: 2560 bytes, the parameters by stored id; the lists at 10, the counted parts after */
        static project_t a, c, d;
        static project_store_t st, st2;
        uint32_t sum, k = NTRK - 1u, g0, p0, p1;
        memset(&a, 0, sizeof a);
        for (t = 0; t < NTRK; t++) {
            a.t[t].engine = (uint8_t)(t == 3u || t == 5u ? ENGI_DRUM : t == 4u ? ENGI_FM6 : 0u);
            a.t[t].preset = (uint8_t)t;
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = (int16_t)clamp(param_desc_of(a.t[t].engine, i)->def + (int32_t)t, -64, 127);
        }
        for (i = 20u; i < G_COUNT; i++)
            a.g[i] = (int16_t)(GP[i].def + 1);           /* (G_CMODE 1: not the CHORUS reset of older formats) */
        a.parts = PROJ_PARTS; a.phys = PROJ_PHYS; a.sel = (uint8_t)k; a.g[G_BPM] = 97;
        a.ent[k][15][0] = 63 + 1; a.ent[k][15][1] = REPS_CHAIN; a.ent[0][1][0] = PAT_REST; a.cur[k] = 15;
        a.song = SONG_ONCE;
        a.scene[0] = 0x21; a.scene[PROJ_SCENES - 1u] = 0x80;
        memcpy(a.name, "SKF4 TEST", 9);
        a.fm6[k][5] = 77;
        memcpy(a.dx, DX_KIT_DEF, sizeof a.dx); a.dx[DV_SNARE].b[DXP_COLOR] = 9; a.dx_on = 1;
        ok = proj_pack2(&st, &a, PROJ_SKF) && proj_import(&c, &st, (int)PROJ_STORE4);
        for (t = 0; t < NTRK; t++)
            ok &= c.t[t].engine == a.t[t].engine && c.t[t].preset == a.t[t].preset &&
                  !memcmp(c.t[t].p, a.t[t].p, sizeof a.t[t].p) && !c.t[t].step[0].n;
        bad += check("(2.0) SKF4: sounds, globals, lists (REST, CHAIN), SONG ONCE, scenes, kit, FM6, name round trip",
                     ok && !memcmp(c.g, a.g, sizeof a.g) && !memcmp(c.ent, a.ent, sizeof a.ent) &&
                     !memcmp(c.cur, a.cur, sizeof a.cur) && c.song == SONG_ONCE && c.sel == k &&
                     !memcmp(c.scene, a.scene, sizeof a.scene) && !memcmp(c.name, a.name, sizeof a.name) &&
                     c.dx_on && !memcmp(c.dx, a.dx, sizeof a.dx) && c.fm6[k][5] == 77u && c.parts == PROJ_PARTS &&
                     c.phys == PROJ_PHYS);
        g0 = PROJ_ENT4 + NTRK * (NENT * 4u + 1u) + PROJ_SCENES + 2u + 1u + DV_NLANE * 11u + NTRK * FM6_PACKED;
        p0 = g0 + 1u + 2u * G_COUNT + 3u;                /* T1's parameters (its count first) */
        p1 = p0 + 1u + SID_COUNT + 3u;                   /* T2's */
        ok = ((const uint32_t *)st.raw)[0] == PROJ_MAGIC4 && ((const uint32_t *)st.raw)[1] == 2560u && st.raw[8] == 1u &&
             st.raw[PROJ_ENT4 + (k * NENT + 15u) * 4u] == 64u && st.raw[PROJ_ENT4 + (k * NENT + 15u) * 4u + 1u] == 16u &&
             st.raw[g0] == G_COUNT && st.raw[p0] == SID_COUNT && st.raw[p1 - 3u] == a.t[1].engine &&
             st.raw[p1 + 1u + P_LEVEL] == (uint8_t)(a.t[1].p[P_LEVEL] + 64) &&
             p1 + 4u * (1u + SID_COUNT + 3u) + 1u + SID_COUNT <= 2544u && !memcmp(st.raw + 2544u, "SKF4 TEST", 9);
        bad += check("  2560 bytes: layout 1, the lists at 10 (4 bytes an entry), globals and tracks by count, name at "
                     "2544", ok);
        bad += check("  read from a retained 3840-byte slot too; loaded and saved again: byte for byte",
                     proj_import(&c, &st, (int)sizeof st) && proj_pack2(&st2, &c, PROJ_SKF) && !memcmp(&st, &st2, sizeof st));
        ok = proj_pack2(&st2, &a, &PROJ_SKF3) && proj_import(&c, &st2, (int)PROJ_STORE3) && proj_pack2(&st, &c, PROJ_SKF) &&
             proj_import(&d, &st, (int)PROJ_STORE4) && !memcmp(&c, &d, sizeof c);
        bad += check("  an SKF3 project saved as SKF4: the same project", ok);
        (void)proj_pack2(&st, &a, PROJ_SKF);
        (void)proj_import(&d, &st, (int)PROJ_STORE4);
        skf4_edit(&st2, &st, p0 + 1u + SID_COUNT, 5, p0, 5);                 /* (a newer firmware's: 5 more on T1, */
        skf4_edit(&st2, &st2, g0 + 1u + 2u * G_COUNT, 4, g0, 2);             /* 2 more globals) */
        ok = proj_import(&c, &st2, (int)PROJ_STORE4) && !memcmp(&c, &d, sizeof c);
        bad += check("  a newer record (more globals, more parameters): theirs passed over, the same project", ok);
        skf4_edit(&st2, &st, p1 + 1u + 100u, -(int)(SID_COUNT - 100u), p1, -(int)(SID_COUNT - 100u));
        skf4_edit(&st2, &st2, g0 + 1u + 2u * 20u, -2 * (int)(G_COUNT - 20u), g0, -(int)(G_COUNT - 20u));
        ok = proj_import(&c, &st2, (int)PROJ_STORE4) && !memcmp(c.t[0].p, a.t[0].p, sizeof a.t[0].p) &&
             !memcmp(c.t[2].p, a.t[2].p, sizeof a.t[2].p) && c.t[2].engine == a.t[2].engine;
        for (i = 0; i < P_COUNT; i++)
            ok &= c.t[1].p[i] == (i < 100u ? a.t[1].p[i] : param_desc_of(a.t[1].engine, i)->def);
        for (i = 0; i < G_COUNT; i++)
            ok &= c.g[i] == (i < 20u ? a.g[i] : GP[i].def);
        bad += check("  an older record (20 globals, 100 parameters on T2): the rest at their defaults", ok);
#define SKF4_BAD(off, v) (st2 = st, st2.raw[(off)] = (v), sum = proj_hash(st2.raw, PROJ_STORE4 - 4u), \
                          memcpy(st2.raw + PROJ_STORE4 - 4u, &sum, 4), !proj_import(&c, &st2, (int)PROJ_STORE4))
        st2 = st; st2.raw[p0 + 1u] ^= 1u;
        ok = !proj_import(&c, &st2, (int)PROJ_STORE4) && SKF4_BAD(8u, 2u) && SKF4_BAD(p0 + 1u, 192u) &&
             SKF4_BAD(p0 - 3u, NENGINES) && SKF4_BAD(g0 - 1u - DV_NLANE * 11u - NTRK * FM6_PACKED - 1u, 3u) &&
             SKF4_BAD(PROJ_ENT4, PAT_REST + 1u) && SKF4_BAD(PROJ_ENT4 + 1u, REPS_CHAIN + 1u) &&
             !SKF4_BAD(9u, 1u);
#undef SKF4_BAD
        bad += check("  refused: a changed byte, another layout, a value past 127, an engine past the last, SONG 3, "
                     "an id past REST, REPS past CHAIN (flags: read)", ok);
        memset(proj_slot, 0, sizeof proj_slot);
        ok = proj_pack2(&proj_slot[1], &a, PROJ_SKF) && proj_slot_refs(63u) == 1u && !proj_slot_refs(62u);
        ok &= proj_pack2(&proj_slot[3], &a, &PROJ_SKF3) && proj_slot_refs(63u) == 2u;
        memset(proj_slot, 0, sizeof proj_slot);
        bad += check("  pattern references: an SKF4 slot's entries counted, beside an SKF3 one's", ok);
    }
    {   /* (phase 6) a light track holding PHYS (no firmware writes one: a crafted record) loads as at power-on, its list
         * kept; with four tracks the last plays every engine: as it is */
        static project_t a, c;
        uint32_t k = NTRK - 1u;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = PROJ_PARTS; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[k].engine = ENGI_PHYS; a.t[k].preset = 3; a.ent[k][0][0] = 5;
        a.sum = proj_sum(&a);
        ok = proj_import(&c, &a, sizeof a) && c.ent[k][0][0] == 5u;
        ok &= k >= NHEAVY ? c.t[k].engine == trk_def_engine(k) && c.t[k].preset == PROJ_DEF_SOUND
                          : c.t[k].engine == ENGI_PHYS && c.t[k].preset == 3u;
        bad += check("a light track holding PHYS (crafted): its power-on sound, its list kept (four tracks: as is)", ok);
    }
    {   /* (2.0, Tier B 4) stored parameter ids (core.h sid_put / sid_get, gsid_*): by number, a count first; a newer
         * record's numbers passed over, an older one's missing ones left at the caller's defaults */
        static int16_t p[P_COUNT], r[P_COUNT], g[G_COUNT], h[G_COUNT];
        static uint8_t b[512];
        uint32_t n;
        for (i = 0; i < P_COUNT; i++)
            p[i] = (int16_t)((int32_t)(i * 7u % 192u) - 64);
        for (i = 0; i < G_COUNT; i++)
            g[i] = (int16_t)(i * 1000u - 9000u);
        n = sid_put(b, p);
        for (i = 0; i < P_COUNT; i++)
            r[i] = 99;
        ok = n == 1u + P_COUNT && b[0] == P_COUNT && sid_get(r, b, n) == n && !memcmp(r, p, sizeof p);
        n = gsid_put(b, g);
        ok &= n == 1u + 2u * G_COUNT && gsid_get(h, b, n) == n && !memcmp(h, g, sizeof g);
        bad += check("(2.0) stored ids: a track's parameters and the globals by number, back as they were", ok);
        (void)sid_put(b, p);
        b[0] = (uint8_t)(P_COUNT + 5u);                  /* (a newer firmware's: five more numbers, unknown here) */
        memset(b + 1u + P_COUNT, 64u + 9u, 5);
        for (i = 0; i < P_COUNT; i++)
            r[i] = 99;
        ok = sid_get(r, b, 1u + P_COUNT + 5u) == 1u + P_COUNT + 5u && !memcmp(r, p, sizeof p);
        b[0] = 100;                                      /* (an older one: the first 100 only) */
        for (i = 0; i < P_COUNT; i++)
            r[i] = 99;
        ok &= sid_get(r, b, 101u) == 101u && !memcmp(r, p, 100u * sizeof r[0]) && r[100] == 99 && r[P_COUNT - 1u] == 99;
        ok &= !sid_get(r, b, 100u) && !gsid_get(h, b, 0u);   /* (shorter than its count says) */
        p[3] = 128;
        ok &= !sid_put(b, p);                            /* (a value no record holds; nor read: a byte past 191) */
        p[3] = 0;
        (void)sid_put(b, p);
        b[1u + P_COUNT - 1u] = 192u;
        ok &= !sid_get(r, b, 1u + P_COUNT);
        bad += check("  a newer record's numbers passed over, an older one's missing ones left; too short or out of "
                     "range refused", ok);
    }
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
