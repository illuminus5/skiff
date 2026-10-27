/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff 2.0, Tier B: design/TIER_B.md 10, step 3) Felucca 1.2+ in the converter (web/conv/conv.c includes it): what no
 * Skiff reads, from Felucca's own sources (GPL-3.0, hugelton/Felucca, the git remote upstream: its tags v1.4 .. v1.6.1
 * and main, Leo Kuroshita's history):
 *   - FUNA projects ("FUN10", written since Felucca 1.2; its project.c, the top and proj_pack / proj_unpack): read here
 *     into project_t (fel_funa), their four tracks' steps and automation patterns in the pool as bank.c proj_migrate
 *     makes them, each with its own automation (fel_pats), then SKF4 (a slot) or the music (fel_put); their songs'
 *     sections onto the lists where they map, after every object (fel_songs);
 *   - user presets of versions 4 and 5 that Felucca 1.2+ wrote (np 103, 104, 111), by parameter name: conv_fix writes
 *     them as version 6 (fel_up_values), an FM6 sound's patch kept.
 * The page says whose an archive is (conv_source: Felucca's hold no pattern bank; both report "FELUCCA vX.Y"); a FUNA
 * is Felucca's whatever the archive. What has no place in Skiff is noted per object (conv_note). */

/* ---- Felucca's track parameters by id. Both grew from 1.1's 99 (v1.1.5.1, which Skiff forked): P_LEVEL .. P_LN7
 * (0..90) are the same in both by label, range and values (params.c TP, its value lists), then the engine's 8. Each
 * added its own before P_E0 since; Felucca's, in its order (core.h of v1.4, v1.5 .. main): 1.2's LFO SYNC TRIG POL and
 * QUANTIZE (a store of 103: a 1.2 build before SPREAD), SPREAD (104: v1.4, v1.4.1), 1.5's INSERT TYPE A B C MIX,
 * ANALOG's filter TYPE and ENV SYNC (111: v1.5 .. v1.6.1, main). A store of np holds the first np - 99 of FEL_P, then
 * the engine's 8: P_E0 .. P_E7, the same per engine in both (engines 0..13, ENGINES[] append-only; eng_*.c of v1.4 ..
 * v1.6 against Skiff's: only labels differ). P_COUNT: none of Skiff's (dropped) ---- */
#define FEL_BASE 91u                             /* (the ids both have: P_LEVEL .. P_LN7) */
#define FEL_SQNT 94u                             /* QUANTIZE (ON: the steps' NUDGE does not play, fel_step) */
#define FEL_FTYPE 101u                           /* ANALOG's filter TYPE: LP, BP, HP (Skiff's is LP) */
static const uint8_t FEL_P[] = {P_LSYNC, P_LTRIG, P_LPOL, P_COUNT, P_SPRD, P_ITYPE, P_IA, P_IB, P_IC, P_IMIX, P_COUNT,
                                P_ESYNC};
_Static_assert(FEL_BASE == P_LN7 + 1u && NELEM(FEL_P) == 12u && P_COUNT < 256u, "Felucca's ids 91..102");
static uint32_t fel_pid(uint32_t i, uint32_t np)   /* Felucca's id i of a store of np -> Skiff's P_* (P_COUNT none) */
{
    uint32_t nc = np - 8u;
    return i >= nc ? (i < np ? P_E0 + i - nc : P_COUNT) : i < FEL_BASE ? i :
           i - FEL_BASE < NELEM(FEL_P) ? FEL_P[i - FEL_BASE] : P_COUNT;
}
/* a Felucca value of Skiff's parameter p: MOD's SRC S&H and SLEW (Felucca 1.2's 9 and 10) come after Skiff's ENV2 (9).
 * Every other value list is the same, or Skiff's goes on past Felucca's (params.c N_*, eng_*.c; an FM6 SLOT of 1.4.1's
 * voice bank, B1..B32, is OWN once fitted: the record holds the patch) */
static int16_t fel_value(uint32_t p, int32_t v)
{
    return (int16_t)(p >= P_M1SRC && p <= P_M4SRC && (p - P_M1SRC) % 3u == 0u && v >= 9 ? v + 1 : v);
}
/* np stored values b (value + 64) of a Felucca track or preset -> v by name (the ones Felucca lacks as v holds them:
 * the caller's defaults). 0 for a byte no record holds */
static int fel_values(int16_t *v, const uint8_t *b, uint32_t np)
{
    uint32_t i, p;
    for (i = 0; i < np; i++) {
        if (b[i] > 191u)
            return 0;
        if ((p = fel_pid(i, np)) < P_COUNT)
            v[p] = fel_value(p, (int32_t)b[i] - 64);
    }
    return 1;
}

/* what had no place in Skiff, per object (conv_note): bit k a track (T k+1) or a preset (its bank's slot k) */
typedef struct {
    uint16_t ftype, nudge, after;                /* ANALOG's filter TYPE BP / HP; NUDGE that did not play (QUANTIZE
                                                  * ON, a ratchet); a song's track on its own steps, after the song */
    uint16_t mot, cat;                           /* automation records with no place; user presets' categories */
    uint8_t song, midi;                          /* a song's sections not mapped; MIDI IN TRACK (1.5.5) */
} fel_drop_t;
static fel_drop_t fel_drop[10];
static uint8_t fel_src;                          /* conv_source: the archive is Felucca's */
static uint8_t fel_ids;                          /* bit id: object id was a FUNA, converted (fel_chain[id] its song) */
static uint8_t fel_chain[6][84];
static void fel_reset(void)
{
    fel_src = fel_ids = 0;
    memset(fel_drop, 0, sizeof fel_drop);
    memset(fel_chain, 0, sizeof fel_chain);
}

/* ---- FUNA: 3840 bytes. 0 magic, 4 size, 8 the globals (27 x int16, G_* the same in both), 62 sel, 63 parts (4), 64
 * phys (2), 66 np; from 68 each of the four tracks: np values + 64, engine, preset, 64 steps of 9 bytes; then the
 * song's sections (84), the motion (388); 3309 MIDI IN TRACK's channels (1.5.5); 3312 the four FM6 patches; 3824 the
 * name; the hash (FNV-1a, proj_hash) last. The same from v1.4 to main (1.5.5 took spare bytes, chance values) ---- */
#define FEL_MAGIC 0x46554E41u                    /* "FUNA" */
#define FEL_SIZE 3840u
#define FEL_NAME (FEL_SIZE - 4u - PROJ_NAME_LEN)
#define FEL_FM6 (FEL_NAME - PROJ_NT * FM6_PACKED)
#define FEL_MIDI (FEL_FM6 - 3u)                  /* (24 bits; bit 23: MIDI IN is TRACK, each track on its channel) */
#define FEL_CHAIN 84u                            /* count, 3 reserved, 16 x (a slot per track: 0..3 A..D, 4 silent;
                                                  * repeats 1..16): song_chain.c */
#define FEL_MOT (4u + 128u * 3u)                 /* count, on (bit k: track k's plays), 2 reserved, 128 x (place: track
                                                  * << 6 | step, id | 128 a lock, the value as an int8_t) */
static int fel_is(const uint8_t *b, uint32_t len)
{
    uint32_t m;
    memcpy(&m, b, 4);
    return len == FEL_SIZE && m == FEL_MAGIC;
}
/* a FUNA step's 9 bytes (Felucca's proj_pack: the notes, bit 7 of note j bit j of the NUDGE; n | time << 3 | accent,
 * slide << 5; velocity, bit 7 RATCH's bit 0; the hits; their accents; the chance, bit 7 RATCH's bit 1) -> s; 0 for
 * bytes Felucca refuses. Its NUDGE (1/16 of the step, -8..+7) as Skiff's 64ths where Felucca plays it (seq.c
 * seq_nudge: QUANTIZE OFF, a NOTE step that plays, x1; else on the grid, *lost set); its condition (the chance
 * 102..112, 1.5.5: 1:2 .. 4:4, FILL, !FILL) as Skiff's TC_* */
static int fel_step(step_t *s, const uint8_t *b, uint32_t qnt, uint32_t *lost)
{
    uint32_t j, nd = 0, meta = b[4], c = b[8] & 127u;
    memset(s, 0, sizeof *s);
    for (j = 0; j < 4u; j++) {
        nd |= (uint32_t)(b[j] >> 7) << j;
        s->note[j] = b[j] & 127u;
    }
    s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
    s->vel = b[5] & 127u; s->hit = b[6]; s->acc = b[7];
    if (meta > 127u || s->n > 4u || s->time > ST_REST || c > 112u || (s->acc & ~s->hit))
        return 0;
    step_set_ratchet(s, (b[5] >> 7 | (b[8] >> 7) << 1) + 1u);
    s->probability = (uint8_t)(c > 101u ? 0u : c);
    s->cond = (uint8_t)(c < 102u ? TC_NONE : c < 111u ? TC_1_2 + c - 102u : c == 111u ? TC_FILL : TC_NFILL);
    if (!qnt && s->time == ST_NOTE && (s->n || s->hit) && step_ratchet(s) == 1u)
        s->nudge = (int8_t)(((int32_t)(nd ^ 8u) - 8) * 4);
    else if (nd)
        *lost = 1;
    return 1;
}
/* a FUNA record (len bytes at b) -> q (its four tracks; T5 T6 as at power-on), each track's automation mk[k], its
 * song's sections c, what has no place in d. 0 for a record Felucca refuses (proj_unpack's checks) or one of an engine
 * past Felucca's */
static int fel_funa(project_t *q, motion_store_t *mk, uint8_t *c, fel_drop_t *d, const uint8_t *b, uint32_t len)
{
    uint32_t np = b[66], pos = 68u, t, i, k, size, sum, lost;
    const uint8_t *m;
    memcpy(&size, b + 4, 4);
    memcpy(&sum, b + FEL_SIZE - 4u, 4);
    if (!fel_is(b, len) || size != FEL_SIZE || sum != proj_hash(b, FEL_SIZE - 4u) || np < 8u ||
        68u + PROJ_NT * (np + 2u + NSTEP * 9u) + FEL_CHAIN + FEL_MOT > FEL_MIDI)
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g);
    if (q->g[G_ROUTE] >= 2)                      /* (MIDI IN CH5-8 .. CH13-16, TRACK's block: Skiff's CH1-6; the */
        q->g[G_ROUTE] = 0;                       /* unit's anyway, ui.c unit_g) */
    q->g[G_CMODE] = q->g[G_CFDBK] = 0;           /* (Felucca's G_DRLVL G_DRREV there: inert) */
    q->sel = b[62] < PROJ_NT ? b[62] : 0u; q->parts = b[63]; q->phys = b[64];
    for (t = 0; t < PROJ_NT; t++) {
        proj_trk_t *r = &q->t[t];
        uint32_t e = b[pos + np], qnt = np - 8u > FEL_SQNT ? b[pos + FEL_SQNT] != 64u : 1u;
        if (e > ENGI_SLICE)
            return 0;
        for (i = 0; i < P_COUNT; i++)
            r->p[i] = param_desc_of(e, i)->def;
        if (!fel_values(r->p, b + pos, np))
            return 0;
        if (np - 8u > FEL_FTYPE && b[pos + FEL_FTYPE] != 64u && !e)
            d->ftype |= (uint16_t)(1u << t);
        r->engine = (uint8_t)e; r->preset = b[pos + np + 1u];
        pos += np + 2u;
        for (i = lost = 0; i < NSTEP; i++, pos += 9u)
            if (!fel_step(&r->step[i], b + pos, qnt, &lost))
                return 0;
        d->nudge |= (uint16_t)(lost << t);
    }
    memcpy(c, b + pos, FEL_CHAIN);
    if (c[0] > CHAIN_ROWS)
        return 0;
    for (i = 0; i < c[0]; i++)
        for (k = 0; k < 5u; k++)                 /* (song_chain.c chain_valid) */
            if (k < PROJ_NT ? c[4u + i * 5u + k] > 4u : !c[4u + i * 5u + k] || c[4u + i * 5u + k] > 16u)
                return 0;
    m = b + pos + FEL_CHAIN;
    if (m[0] > 128u || (m[1] >> PROJ_NT))
        return 0;
    memset(mk, 0, PROJ_NT * sizeof *mk);
    for (i = 0; i < m[0]; i++) {
        const uint8_t *r = m + 4u + 3u * i;
        uint32_t id = r[1] & 127u, p = fel_pid(id, np);
        motion_store_t *o = &mk[r[0] >> 6];
        if (id >= np || (int8_t)r[2] < -64)      /* (Felucca: proj_motion_ids, motion_valid) */
            return 0;
        if (p == P_COUNT || !motion_param(p) || o->count >= MOTION_MAX) {
            d->mot++;
            continue;
        }
        o->event[o->count++] = (motion_event_t){.place = r[0], .lock = r[1] >> 7, .id = (uint8_t)p,
                                                .value = (int8_t)fel_value(p, (int8_t)r[2])};
    }
    for (k = 0; k < PROJ_NT; k++)
        mk[k].on = (uint8_t)(m[1] & (1u << k));
    d->midi = b[FEL_MIDI + 2u] >> 7;
    for (t = 0; t < PROJ_NT; t++)
        for (i = 0; i < FM6_PACKED; i++)
            q->fm6[t][i] = b[FEL_FM6 + t * FM6_PACKED + i] & 0x7Fu;
    proj_power_on(q, PROJ_NT);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + FEL_NAME));
    }
    proj_bound(q);                               /* (what proj_migrate does to an import; no DIGITAL or SAMPLE PERC */
    proj_fm4(q);                                 /* track in a FUNA: Felucca saved what it loaded, FM6, DRUM. */
    proj_perc(q);                                /* ponytail: else its EDIT automation would stay in mk) */
    return 1;
}
/* q's four tracks as pool patterns, each with its automation mk[k] (bank.c proj_migrate's: one the same there already,
 * that one; an empty track none), on q's lists (entry A). 0 done, 2 a step no format holds, 4 the pool full or a write
 * failed */
static uint32_t fel_pats(project_t *q, const motion_store_t *mk)
{
    uint32_t k, i, id, used;
    int16_t tv[5];
    for (k = 0; k < PROJ_NT; k++) {
        for (i = 0, used = mk[k].count; i < NSTEP; i++)
            used |= q->t[k].step[i].n | q->t[k].step[i].hit;
        if (!used)
            continue;
        for (i = 0; i < 5u; i++)
            tv[i] = q->t[k].p[BK_TIME[i]];
        if (!bk_pack_raw(bk_rec, k, q->t[k].step, tv, &mk[k], mk[k].on != 0))
            return 2;
        if ((id = pool_find(bk_rec)) == NPAT && ((id = pool_alloc()) == NPAT || pat_keep_now(id, bk_rec)))
            return 4;
        q->ent[k][0][0] = (uint8_t)(id + 1u);
    }
    while (pend_free() < PEND_N)
        if (pat_flush_one())
            return 4;
    q->sum = proj_sum(q);
    return 0;
}
/* conv_put of a FUNA (len bytes in conv_bytes) as object id, as the restore then the music's commit take an older
 * project (editor_backup.c ed_bk_commit, bank.c proj_migrate): its patterns first, then a slot's SKF4, or the music
 * (and the other slots' migration). conv_put's rc */
static uint32_t fel_put(uint32_t id, uint32_t len)
{
    static project_t q;
    static motion_store_t mk[PROJ_NT];
    uint32_t rc;
    if (id && (id < 2u || id > 5u))
        return 1;
    memset(&fel_drop[id], 0, sizeof fel_drop[id]);
    if (!fel_funa(&q, mk, fel_chain[id], &fel_drop[id], conv_bytes, len)) {
        memset(&fel_drop[id], 0, sizeof fel_drop[id]);   /* (refused: nothing of it noted) */
        return 2;
    }
    if ((rc = fel_pats(&q, mk)) != 0)
        return rc;
    if (!id) {
        if (project_restore_runtime(&q))
            return 2;
        (void)proj_migrate(1);
    } else {
        proj_wire_gen++;
        if (!proj_pack2(&proj_wire, &q, PROJ_SKF) || st_save(OBJ_PROJECT0 + id - 2u, &proj_wire, PROJ_SKF->size))
            return 4;
        memcpy(&proj_slot[id - 2u], &proj_wire, sizeof proj_wire);
        if (proj_cur == id - 2u)
            proj_cur = PROJ_NO_SLOT;
    }
    fel_ids |= (uint8_t)(1u << id);
    return 0;
}

/* ---- the songs: Felucca's (song_chain.c) is the music's sections, each naming the project slot each track plays (its
 * steps, automation and LEN DIV) for `repeat` loops of its clock track (the first one not silent); Skiff's is each
 * track's list of patterns ---- */
static uint16_t fel_len(int32_t len, int32_t div)   /* LEN steps of DIV in 48ths of a quarter (Felucca's len48_of) */
{
    uint32_t n = (uint32_t)clamp(len, 1, NSTEP), d = (uint32_t)clamp(div, 0, 9);
    return (uint16_t)(n * (d < 6u ? 48u / DIV_DEN[d] : 48u << (d - 5u)));
}
/* sections c onto the lists ent / cur of a project whose tracks are on their own patterns own[k] (id + 1, 0 none), of
 * LEN DIV ol[k]: each track's entries the sections, its lane's pattern (that slot's same track: pat[s][k], of length
 * len[s][k]; slot s there: bit s of ok) for the passes that fill the section; a silent lane, or an empty one, a REST
 * (of the track's LEN DIV then: the pattern's before it); then the track on its own pattern (that entry, else one
 * after them: *after bit k). 0 for a song that does not map so: a slot it names empty (Felucca would not play it), a
 * section a track can't fill with 1..16 whole passes, no entry left for the track's own */
static int fel_song(const uint8_t *c, uint8_t (*ent)[NENT][2], uint8_t *cur, const uint8_t *own, const uint16_t *ol,
                    uint8_t pat[4][PROJ_NT], uint16_t len[4][PROJ_NT], uint32_t ok, uint16_t *after)
{
    uint8_t out[PROJ_NT][NENT][2], at[PROJ_NT];
    uint32_t n = c[0], e, k, d, l[PROJ_NT];
    uint16_t af = 0;
    memset(out, 0, sizeof out);
    for (k = 0; k < PROJ_NT; k++)
        l[k] = ol[k];
    for (e = 0; e < n; e++) {
        const uint8_t *r = c + 4u + 5u * e;
        for (k = 0; k < PROJ_NT && r[k] > 3u; k++)
            ;
        d = r[4] * (k < PROJ_NT ? len[r[k]][k] : 192u);   /* (every lane silent: a 4/4 bar of 1/16) */
        for (k = 0; k < PROJ_NT; k++) {
            uint32_t s = r[k], p = 0;
            if (s < 4u && !((ok >> s) & 1u))
                return 0;
            if (s < 4u && (p = pat[s][k]) != 0u)
                l[k] = len[s][k];
            if (d % l[k] || d / l[k] > 16u)
                return 0;
            out[k][e][0] = (uint8_t)(p ? p : PAT_REST);
            out[k][e][1] = (uint8_t)(d / l[k] - 1u);
        }
    }
    for (k = 0; k < PROJ_NT; k++) {
        for (e = 0; e < n && out[k][e][0] != own[k]; e++)
            ;
        if (e == NENT)
            return 0;
        if (e == n && own[k]) {
            out[k][e][0] = own[k];
            af |= (uint16_t)(1u << k);
        }
        at[k] = (uint8_t)e;
    }
    memcpy(ent, out, sizeof out);                /* (T5 T6: no list) */
    memcpy(cur, at, sizeof at);
    *after |= af;
    return 1;
}
/* conv_fix, after every object: each FUNA's song onto its lists (the music's; a slot's SKF4 written again), the lanes
 * the slots' patterns as they are now (every project of the archive converted); a song that does not map: noted */
static void fel_songs(void)
{
    static project_t q;
    uint8_t pat[4][PROJ_NT] = {{0}}, own[PROJ_NT];
    uint16_t len[4][PROJ_NT] = {{0}}, ol[PROJ_NT];
    uint32_t s, k, id, ok = 0;
    for (s = 0; s < 4u; s++)
        if (proj_fmt_of(&proj_slot[s], sizeof proj_slot[s]) == &PROJ_SKF4 &&
            proj_import(&q, &proj_slot[s], sizeof proj_slot[s])) {
            ok |= 1u << s;
            for (k = 0; k < PROJ_NT; k++) {
                pat[s][k] = q.ent[k][q.cur[k]][0] <= NPAT ? q.ent[k][q.cur[k]][0] : 0u;
                len[s][k] = fel_len(q.t[k].p[P_SLEN], q.t[k].p[P_SDIV]);
            }
        }
    for (id = 0; id < 6u; id++) {
        if (!((fel_ids >> id) & 1u) || !fel_chain[id][0])
            continue;
        if (!id) {
            for (k = 0; k < PROJ_NT; k++) {
                own[k] = trk_pid[k];
                ol[k] = fel_len(trk[k].p[P_SLEN], trk[k].p[P_SDIV]);
            }
            fel_drop[0].song = !fel_song(fel_chain[0], lists.ent, lists.cur, own, ol, pat, len, ok, &fel_drop[0].after);
            continue;
        }
        fel_drop[id].song = 1;
        if (!proj_import(&q, &proj_slot[id - 2u], sizeof proj_slot[id - 2u]))
            continue;
        for (k = 0; k < PROJ_NT; k++) {
            own[k] = q.ent[k][q.cur[k]][0];
            ol[k] = fel_len(q.t[k].p[P_SLEN], q.t[k].p[P_SDIV]);
        }
        proj_wire_gen++;
        if (fel_song(fel_chain[id], q.ent, q.cur, own, ol, pat, len, ok, &fel_drop[id].after) &&
            proj_pack2(&proj_wire, &q, PROJ_SKF) && !st_save(OBJ_PROJECT0 + id - 2u, &proj_wire, PROJ_SKF->size)) {
            memcpy(&proj_slot[id - 2u], &proj_wire, sizeof proj_wire);
            fel_drop[id].song = 0;
        }
    }
}

/* ---- user presets: a record of versions 4 and 5 that Felucca 1.2+ wrote (np one of its counts since 1.2; before them
 * the ids by count were the same as by name) in Felucca's archive ---- */
static int fel_up(const up_rec_t *r)
{
    return fel_src && (r->ver == UP_VER || r->ver == UP_VER_GRID) && (r->np == 103u || r->np == 104u || r->np == 111u);
}
/* slot k's record r, one of those, -> v (today's P_*, fitted), as up_values; its filter TYPE and category (1.2: its
 * last byte) noted. 0: not one of those */
static int fel_up_values(const up_rec_t *r, int16_t *v, uint32_t k)
{
    fel_drop_t *d = &fel_drop[6u + k / UP_PER_BANK];
    uint32_t i, c = r->sid[UP_PMAX * 2u - 1u];
    if (!fel_up(r))
        return 0;
    for (i = 0; i < P_COUNT; i++)
        v[i] = param_desc_of(r->engine, i)->def;
    (void)fel_values(v, r->sid, r->np);          /* (up_valid: no byte past 191) */
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)param_fit(param_desc_of(r->engine, i), v[i]);
    if (r->np - 8u > FEL_FTYPE && r->sid[FEL_FTYPE] != 64u && !r->engine)
        d->ftype |= (uint16_t)(1u << k % UP_PER_BANK);
    d->cat += c && c < 9u;                       /* (category.c CAT_BASS .. CAT_OTHER) */
    return 1;
}

/* ---- the exports ---- */
EXPORT void conv_source(uint32_t felucca) { fel_src = felucca != 0u; }   /* (after conv_reset) */

static uint32_t fel_n;                           /* (conv_note's text so far) */
static void fel_say(const char *s)
{
    while (*s && fel_n < 255u)
        conv_bytes[fel_n++] = (uint8_t)*s++;
}
static void fel_num(uint32_t v)
{
    char b[8], *p = b + 7;
    *p = 0;
    do
        *--p = (char)('0' + v % 10u);
    while ((v /= 10u) != 0u && p > b);
    fel_say(p);
}
static void fel_sep(void) { fel_say(fel_n ? "; " : ""); }
static void fel_item(uint32_t mask, uint32_t base, int preset, const char *what)   /* "T1 T3 what", "U02 U17 what" */
{
    uint32_t i;
    if (!mask)
        return;
    fel_sep();
    for (i = 0; i < 16u; i++)
        if ((mask >> i) & 1u) {
            fel_say(preset ? (base + i < 9u ? "U0" : "U") : "T");
            fel_num(base + i + 1u);
            fel_say(" ");
        }
    fel_say(what);
}
/* object id's note into conv_buf: what had no place in Skiff (a FUNA's, Felucca's user presets'). Its length; 0 none */
EXPORT uint32_t conv_note(uint32_t id)
{
    const fel_drop_t *d = &fel_drop[id < NELEM(fel_drop) ? id : 0u];
    uint32_t base = id == 7u ? UP_PER_BANK : 0u;
    int pr = id == 6u || id == 7u;
    fel_n = 0;
    if (id >= NELEM(fel_drop))
        return 0;
    fel_item(d->ftype, base, pr, "ANALOG filter TYPE (LP here)");
    fel_item(d->nudge, base, pr, "NUDGE that did not play (QUANTIZE ON, RATCH): on the grid");
    fel_item(d->after, base, pr, "own steps, not in the song: after it");
    if (d->mot) {
        fel_sep();
        fel_say("automation records of no parameter here, or past 64 on a track: ");
        fel_num(d->mot);
    }
    if (d->cat) {
        fel_sep();
        fel_say("categories: ");
        fel_num(d->cat);
    }
    if (d->song) {
        fel_sep();
        fel_say("the song's sections (a slot empty, or lengths that do not fill them)");
    }
    if (d->midi) {
        fel_sep();
        fel_say("MIDI IN TRACK (the unit's T1-T6 CHANNEL here)");
    }
    return fel_n;
}
