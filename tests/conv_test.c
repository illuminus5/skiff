/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff 2.0, Tier B: design/TIER_B.md 10) The converter (web/conv/conv.c) on the host. Archives of before in,
 * 2.0's formats out, and the converted objects restored onto a fresh unit (the same code again) play the same notes
 * and sounds:
 *   - an archive of 1.x (the music SKF3, settings PER4, a v5 preset, its patterns): nothing to convert;
 *   - old projects (FUN9 and SKF1 in the slots, SKF1 as the music, an SKF2) -> SKF4, their steps patterns, a DIGITAL
 *     track FM6;
 *   - Felucca's settings (PER4 without Skiff's mark): kept (2.0 reads them), taken as SKS1 by a unit;
 *   - user presets of versions 2 and 3 (a grid), a DIGITAL sound and an FM6 sound of version 2 with its patch -> 6, a
 *     version 5 kept;
 *   - a damaged project refused (rc 2), the others converted;
 *   - Felucca 1.2+ (web/conv/felucca.c): FUNA projects of 111 and 104 parameters (the music, slots 1 and 2) built as
 *     Felucca's project.c writes them -> SKF4, their sounds by parameter name, steps with NUDGE, RATCH and conditions,
 *     automation and locks of its own parameters, the song's sections on the lists; its user presets of versions 4
 *     and 5 (np 103, 104, 111) -> 6 by name, an FM6 sound's patch kept; what has no place noted (conv_note).
 * conv_test DIR also writes the archives as web/fm1backup.js keeps them, for web/conv/conv_test.mjs (the same
 * conversion by conv.wasm): DIR/conv_1x.json, DIR/conv_old.json (Felucca's 13 objects), DIR/conv_funa.json
 * (Felucca 1.6's) and DIR/conv_old_native.json, DIR/conv_funa_native.json (what this build made of them).
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#include "../web/conv/conv.c"

static int check(const char *what, int ok)
{
    printf("conv: %-82s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

typedef struct { uint32_t len; uint8_t b[3840]; } obj_t;   /* an archive's object (ids 0..31: no samples here) */
static obj_t in[32], out[32];
static const uint8_t ORDER[] = {2, 3, 4, 5, 6, 7, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30,
                                31, 1, 0};        /* (web/fm1backup.js restoreBackup: the music last) */

static void get_all(obj_t *o)
{
    uint32_t id;
    for (id = 0; id < 32u; id++) {
        o[id].len = conv_get(id);
        memcpy(o[id].b, conv_bytes, o[id].len);
    }
}
/* the archive restored onto the unit as it is now (its non-empty objects); the rc of each in rc[] */
static void put_all(const obj_t *o, uint32_t *rc)
{
    uint32_t i;
    for (i = 0; i < sizeof ORDER; i++) {
        uint32_t id = ORDER[i];
        rc[id] = 0;
        if (!o[id].len)
            continue;
        memcpy(conv_bytes, o[id].b, o[id].len);
        rc[id] = conv_put(id, o[id].len);
    }
}
static int needs(const obj_t *o, uint32_t id)
{
    memcpy(conv_bytes, o[id].b, o[id].len);
    return (int)conv_needs(id, o[id].len);
}
static uint32_t magic(const obj_t *o, uint32_t id) { return o[id].len >= 4u ? *(const uint32_t *)o[id].b : 0u; }

/* the archive o (the objects ids, n of them; the samples, 32..34, empty) to path as fm1backup.js keeps one */
static const char *fixtures;                       /* (conv_test DIR) */
static void write_archive(const char *name, const obj_t *o, int felucca)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char path[512];
    FILE *f;
    uint32_t id, i, first = 1;
    if (!fixtures)
        return;
    snprintf(path, sizeof path, "%s/%s", fixtures, name);
    if (!(f = fopen(path, "w")))
        return;
    fprintf(f, "{\"format\":\"felucca-backup\",\"version\":1,\"firmware\":\"%s\",\"created\":\"2026-10-24T00:00:00.000Z\","
            "\"objects\":[", felucca == 2 ? "FELUCCA v1.6.1" : felucca ? "FELUCCA v1.1.5" : "FELUCCA v1.3.3");
    for (id = 0; id < 35u; id++) {
        uint32_t len = id < 32u ? o[id].len : 0u;
        const uint8_t *p = id < 32u ? o[id].b : 0;
        if ((id > 9u && id < 16u) || (felucca && id >= 16u && id < 32u))
            continue;
        fprintf(f, "%s{\"id\":%u,\"size\":%u,\"crc\":%u,\"data\":\"", first ? "" : ",", id, len,
                len ? st_crc32(p, len) : 0u);
        first = 0;
        for (i = 0; i < len; i += 3u) {
            uint32_t v = (uint32_t)p[i] << 16 | (i + 1u < len ? (uint32_t)p[i + 1u] << 8 : 0u) |
                         (i + 2u < len ? p[i + 2u] : 0u);
            fputc(T[v >> 18 & 63u], f);
            fputc(T[v >> 12 & 63u], f);
            fputc(i + 1u < len ? T[v >> 6 & 63u] : '=', f);
            fputc(i + 2u < len ? T[v & 63u] : '=', f);
        }
        fputs("\"}", f);
    }
    fputs("]}\n", f);
    fclose(f);
}

/* (project_test.c's) an SKF1 record as FUN9 laid it out */
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
/* tracks 1..4: a note each on steps 1, 5, 9 (base + the track, the step), the rest empty */
static void music(uint32_t base)
{
    uint32_t k, i;
    for (k = 0; k < 4u; k++)
        for (i = 0; i < 12u; i += 4u)
            trk[k].step[i] = (step_t){{(uint8_t)(base + k * 5u + i)}, 1, ST_NOTE, 0, 96, 0, 0};
}
static int music_is(uint32_t base)
{
    uint32_t k, i;
    int ok = 1;
    for (k = 0; k < 4u; k++)
        for (i = 0; i < 16u; i++)
            ok &= i % 4u || i >= 12u ? !trk[k].step[i].n || trk[k].step[i].time != ST_NOTE
                                     : trk[k].step[i].n == 1u && trk[k].step[i].note[0] == base + k * 5u + i;
    return ok;
}
static persist_t per_from(uint32_t magic_, uint32_t palette)   /* Felucca's settings of before: PER1..PER4 */
{
    persist_t p;
    memset(&p, 0, sizeof p);
    p.magic = magic_; p.palette = palette_to_stored(palette); p.lowcut = 1; p.panel = PANEL_DEFAULT;
    return p;
}
static up_rec_t rec_of(uint32_t ver, uint32_t engine, const char *name, uint32_t np, int32_t base)
{
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED; r.ver = (uint8_t)ver; r.engine = (uint8_t)engine; r.np = (uint8_t)np;
    memcpy(r.name, name, strlen(name));
    for (i = 0; i < np; i++)
        up_set_value(&r, i, (int16_t)(i + 8u < np ? (int32_t)(i % 40u) + base
                                                : param_desc_of(engine, P_E0 + i + 8u - np)->def));
    return r;
}

/* ---- Felucca 1.2+, as its own code writes it (upstream main: project.c proj_pack, upreset.c's records); its ids
 * past the shared 91 (core.h), its step (step_t: NUDGE -8..+7, RATCH 1..4, the chance 0..112) ---- */
enum { F_LSYNC = 91, F_LTRIG, F_LPOL, F_SQNT, F_SPRD, F_ITYPE, F_IA, F_IB, F_IC, F_IMIX, F_FTYPE, F_ESYNC };
#define F_E0(np) ((np) - 8u)
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc, chance, ratch; int8_t nudge; } fstep_t;
typedef struct {
    uint32_t np, mch;                            /* 111 (1.5 ..), 104 (1.2 .. 1.4); MIDI IN TRACK's bits (1.5.5) */
    int16_t g[G_COUNT];
    struct { int16_t p[111]; uint8_t engine; fstep_t step[NSTEP]; } t[4];
    uint8_t chain[84], mot[388], fm6[4][FM6_PACKED];
    char name[12];
} funa_t;
static void funa_trk(funa_t *f, uint32_t k, uint32_t engine)   /* track k: engine's sound, Felucca's defaults */
{
    static const int16_t NEW[12] = {0, 0, 0, 1, 0, 0, 64, 96, 96, 127, 0, 0};   /* (its TP: SYNC .. ENV SYNC) */
    uint32_t i, ne = F_E0(f->np);
    f->t[k].engine = (uint8_t)engine;
    for (i = 0; i < f->np; i++)
        f->t[k].p[i] = i < 91u ? param_desc_of(engine, i)->def : i < ne ? NEW[i - 91u]
                                                                        : param_desc_of(engine, P_E0 + i - ne)->def;
    f->t[k].p[P_SGATE] = 64;                     /* (Felucca's default) */
    for (i = 0; i < NSTEP; i++)
        f->t[k].step[i] = (fstep_t){{0}, 0, ST_REST, 0, 0, 0, 0, 0, 1, 0};
}
static void funa_note(funa_t *f, uint32_t k, uint32_t i, uint32_t note, int32_t nudge, uint32_t ratch, uint32_t chance)
{
    f->t[k].step[i] = (fstep_t){{(uint8_t)note}, 1, ST_NOTE, 0, 100, 0, 0, (uint8_t)chance, (uint8_t)ratch,
                                (int8_t)nudge};
}
static void funa_mot(funa_t *f, uint32_t k, uint32_t step, uint32_t id, int32_t v, int lock)   /* a motion record */
{
    uint8_t *r = f->mot + 4u + 3u * f->mot[0]++;
    r[0] = (uint8_t)(k << 6 | step);
    r[1] = (uint8_t)(id | (lock ? 128u : 0u));
    r[2] = (uint8_t)(int8_t)v;
}
static void funa_pack(uint8_t *b, const funa_t *f)   /* (its proj_pack, proj_midi_pack) */
{
    uint32_t pos = 68u, t, i, j, magic = 0x46554E41u, size = 3840u, sum;
    memset(b, 0, 3840);
    memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, f->g, 2u * G_COUNT); b[62] = 0; b[63] = 4; b[64] = 2; b[66] = (uint8_t)f->np;
    for (t = 0; t < 4u; t++) {
        for (i = 0; i < f->np; i++)
            b[pos++] = (uint8_t)(f->t[t].p[i] + 64);
        b[pos++] = f->t[t].engine; b[pos++] = 0;
        for (i = 0; i < NSTEP; i++) {
            const fstep_t *s = &f->t[t].step[i];
            uint32_t r = s->ratch - 1u, nd = (uint32_t)s->nudge & 15u;
            for (j = 0; j < 4u; j++)
                b[pos++] = (uint8_t)(s->note[j] | (nd >> j & 1u) << 7);
            b[pos++] = (uint8_t)(s->n | s->time << 3 | (s->flags & 3u) << 5);
            b[pos++] = (uint8_t)(s->vel | (r & 1u) << 7); b[pos++] = s->hit; b[pos++] = s->acc & s->hit;
            b[pos++] = (uint8_t)(s->chance | (r >> 1) << 7);
        }
    }
    memcpy(b + pos, f->chain, 84); pos += 84;
    memcpy(b + pos, f->mot, 388);
    b[3309] = (uint8_t)f->mch; b[3310] = (uint8_t)(f->mch >> 8); b[3311] = (uint8_t)(f->mch >> 16);
    memcpy(b + 3312, f->fm6, sizeof f->fm6);
    memcpy(b + 3824, f->name, 12);
    sum = proj_hash(b, 3836); memcpy(b + 3836, &sum, 4);
}
/* a Felucca user preset (its up_store: version 4, or 5 with a drum grid): engine's defaults by its order of np, then
 * the values {Felucca id, value} of set (n of them); cat its category (1.2, the record's last byte) */
static up_rec_t frec(uint32_t ver, uint32_t engine, const char *name, uint32_t np, const int16_t (*set)[2], uint32_t n,
                     uint32_t cat)
{
    funa_t f;
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED; r.ver = (uint8_t)ver; r.engine = (uint8_t)engine; r.np = (uint8_t)np;
    memcpy(r.name, name, strlen(name));
    f.np = np;
    funa_trk(&f, 0, engine);
    for (i = 0; i < n; i++)
        f.t[0].p[set[i][0]] = set[i][1];
    for (i = 0; i < np; i++)
        r.sid[i] = (uint8_t)(f.t[0].p[i] + 64);
    r.sid[143] = (uint8_t)cat;
    return r;
}

int main(int argc, char **argv)
{
    static project_t p, q, got;
    static project_store_t st;
    static uint32_t rc[32];
    uint32_t id, k;
    int bad = 0, ok;
    fixtures = argc > 1 ? argv[1] : 0;

    /* 1. an archive of 1.x: the music SKF3 naming its patterns, settings PER4, a v5 preset: nothing to convert */
    conv_reset();
    music(48);
    get_all(in);                                   /* (as 2.0 takes it: then made 1.x's) */
    ok = proj_import(&p, in[0].b, (int)in[0].len) && proj_pack2(&st, &p, &PROJ_SKF3);
    memcpy(in[0].b, &st, PROJ_STORE3); in[0].len = PROJ_STORE3;
    {
        persist_t s = per_from(PERSIST_MAGIC, 3);
        s.favorites.factory[15][24] = 0xA5u;       /* (Skiff's mark) */
        memcpy(in[1].b, &s, sizeof s); in[1].len = sizeof s;
    }
    {
        static up_bank_t b;
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC; b.rsize = sizeof(up_rec_t); b.nslot = UP_PER_BANK;
        b.r[0] = rec_of(UP_VER_GRID, ENGI_DRUM, "DRUM 01", P_COUNT, 0);
        memcpy(in[6].b, &b, sizeof b); in[6].len = sizeof b;
    }
    for (id = 0; id < 32u; id++)
        ok &= !needs(in, id);
    bad += check("an archive of 1.x (SKF3, PER4, a v5 preset, patterns): nothing to convert", ok && in[16].len);
    write_archive("conv_1x.json", in, 0);

    /* 1b. (2.0) an old project no free pattern can take (the archive's pool full): left as it is (SKF1, which a 2.0 unit
     * keeps whole: OLD FORMAT); conv.js reports it kept, "the patterns are full" (web/conv/conv_test.mjs) */
    {
        static uint8_t r[BK_REC];
        conv_reset();
        music(36);
        project_capture(&p);
        ok = proj_pack(&st, &p);                   /* (tracks 1..4 with steps: patterns none of the pool's) */
        ok &= bk_pack(r, &trk[5]);                 /* (an empty track's pattern, in every id) */
        for (id = 0; id < BK_POOL; id++)
            ok &= pool_put(id, r) == 0;
        get_all(in);
        memcpy(in[2].b, &st, sizeof st); in[2].len = sizeof st;
        ok &= needs(in, 2) && !needs(in, 0) && in[31].len;
        write_archive("conv_full.json", in, 0);
        conv_reset();
        put_all(in, rc);
        conv_fix();
        get_all(out);
        ok &= rc[2] == 0 && magic(out, 2) == PROJ_MAGIC && needs(out, 2) && !needs(out, 0);
        bad += check("(2.0) an old project no free pattern can take (the pool full): left as it is (SKF1)", ok);
    }

    /* 2. old projects and settings, user presets of before: converted, then read by a fresh unit */
    conv_reset();
    music(60);
    project_capture(&p);                           /* (slots 1 and 2: these notes; the music another) */
    p.t[1].engine = ENGI_DIGITAL;                  /* (a DIGITAL track: FM6 when it loads) */
    for (k = 0; k < P_COUNT; k++)
        p.t[1].p[k] = param_desc_of(ENGI_DIGITAL, k)->def;
    p.sum = proj_sum(&p);
    ok = proj_pack(&st, &p);                       /* SKF1 */
    memcpy(in[3].b, &st, sizeof st); in[3].len = sizeof st;
    fun9_from(in[2].b, &st); in[2].len = PROJ_STORE_V9;
    memcpy(in[5].b, &st, sizeof st); in[5].len = sizeof st;
    in[5].b[700] ^= 1u;                            /* (damaged: its hash) */
    q = p;
    q.t[2].engine = ENGI_DRUM;
    for (k = 0; k < P_COUNT; k++)
        q.t[2].p[k] = param_desc_of(ENGI_DRUM, k)->def;
    q.sum = proj_sum(&q);
    ok &= proj_pack2(&st, &q, &PROJ_SKF2);         /* (SKF2: four tracks, no steps) */
    memcpy(in[4].b, &st, PROJ_STORE2); in[4].len = PROJ_STORE2;
    memset(trk[0].step, 0, sizeof trk[0].step);
    music(72);
    project_capture(&q);
    ok &= proj_pack(&st, &q);                      /* the music: SKF1 */
    memcpy(in[0].b, &st, sizeof st); in[0].len = sizeof st;
    {
        persist_t s = per_from(PERSIST_MAGIC, 2);  /* Felucca's PER4 (no Skiff mark): 2.0 reads it */
        memcpy(in[1].b, &s, sizeof s); in[1].len = sizeof s;
    }
    {
        static up_bank_t b;
        uint8_t pk[FM6_PACKED];
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC; b.rsize = sizeof(up_rec_t); b.nslot = UP_PER_BANK;
        b.r[0] = rec_of(2, 0, "OLD ANALOG", 69, -20);
        b.r[1] = rec_of(3, ENGI_DRUM, "OLD GRID", 69, 0);
        b.r[1].note[0] = 9; b.r[1].flags[0] = 1;   /* (a grid's hits and accents: gone with the pattern) */
        b.r[2] = rec_of(UP_VER, ENGI_DIGITAL, "OLD DIGITAL", P_COUNT, 0);
        b.r[3] = rec_of(UP_VER_GRID, ENGI_DRUM, "KEPT V5", P_COUNT, 3);
        b.r[4] = rec_of(2, ENGI_FM6, "OLD FM6", 69, 0);
        up_bank[0] = b;                            /* (its patch, tied to the record as it is: upf_tag) */
        upf_empty();
        memcpy(pk, FM6_FACTORY[2], sizeof pk);
        pk[7] ^= 0x15u;
        upf_set(4, pk);
        memcpy(in[9].b, &upf, sizeof upf); in[9].len = sizeof upf;
        memcpy(in[6].b, &b, sizeof b); in[6].len = sizeof b;
        bad += check("(the archive) a v2 FM6 sound's patch in object 9", !upf_get(4, pk));
    }
    for (id = 16; id < 32u; id++)
        in[id].len = 0;                            /* (an archive without Skiff's pattern bank: Felucca's) */
    ok &= needs(in, 0) && !needs(in, 1) && needs(in, 2) && needs(in, 3) && needs(in, 4) && needs(in, 6);
    write_archive("conv_old.json", in, 1);
    bad += check("old projects (FUN9, SKF1, SKF2), presets of before: conv_needs (Felucca's PER4: not)", ok);

    conv_reset();
    conv_source(1);                                /* (no pattern bank: Felucca's, as conv.js says) */
    put_all(in, rc);
    conv_fix();
    get_all(out);
    write_archive("conv_old_native.json", out, 0);
    ok = rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && rc[3] == 0 && rc[4] == 0 && rc[5] == 2 && rc[6] == 0 && rc[9] == 0;
    bad += check("restored: every object taken, the damaged one refused (rc 2)", ok);
    ok = magic(out, 0) == PROJ_MAGIC4 && magic(out, 2) == PROJ_MAGIC4 && magic(out, 3) == PROJ_MAGIC4 &&
         magic(out, 4) == PROJ_MAGIC4 && !out[5].len && magic(out, 1) == SKS_MAGIC && out[1].len == sizeof(sks_t);
    for (id = 0; id < 32u; id++)
        ok &= !needs(out, id);
    bad += check("out: the music and three slots SKF4, settings SKS1; nothing left to convert", ok);
    {
        const up_bank_t *b = (const up_bank_t *)out[6].b;
        const up_bank_t *a = (const up_bank_t *)in[6].b;
        int16_t va[P_COUNT], vb[P_COUNT];
        ok = out[6].len == sizeof *b;
        for (k = 0; k < 5u; k++)
            ok &= up_valid(&b->r[k]) && !memcmp(b->r[k].name, a->r[k].name, 12);
        ok &= b->r[0].ver == UP_VER_SID && b->r[1].ver == UP_VER_SID && b->r[2].ver == UP_VER_SID &&
              b->r[4].ver == UP_VER_SID && !memcmp(&b->r[3], &a->r[3], sizeof b->r[3]) &&
              b->r[2].engine == ENGI_FM6 && b->r[4].engine == ENGI_FM6;
        up_values(&a->r[0], va); up_values(&b->r[0], vb);
        ok &= !memcmp(va, vb, sizeof va);
        up_values(&a->r[1], va); up_values(&b->r[1], vb);
        ok &= !memcmp(va, vb, sizeof va);
        bad += check("presets: v2, v3 grid, DIGITAL, v2 FM6 -> v6 (the same values), v5 kept byte for byte", ok);
    }

    /* 3. the converted objects onto a fresh unit: the same music and sounds */
    conv_reset();
    put_all(out, rc);
    ok = 1;
    for (id = 0; id < 32u; id++)
        ok &= !rc[id];
    ok &= music_is(72);
    bad += check("a fresh unit takes them all; the music plays its notes (its patterns)", ok);
    project_load(1);                               /* (slot 2: the SKF1's, now SKF4 and patterns) */
    ok = proj_cur == 1u && music_is(60) && trk[1].eng_req == ENGI_FM6;
    project_load(0);                               /* (slot 1: the FUN9's) */
    ok &= proj_cur == 0u && music_is(60);
    bad += check("  slots 1 and 2 (FUN9, SKF1): the same notes, DIGITAL as FM6", ok);
    project_load(2);                               /* (slot 3: the SKF2's, no steps) */
    ok = proj_cur == 2u && trk[2].eng_req == ENGI_DRUM && proj_import(&got, &proj_slot[2], sizeof proj_slot[2]) &&
         got.t[2].engine == ENGI_DRUM;
    bad += check("  slot 3 (SKF2): its sounds (T3 DRUM)", ok);
    {
        uint8_t pk[FM6_PACKED], want[FM6_PACKED];
        memcpy(want, FM6_FACTORY[2], sizeof want);
        want[7] ^= 0x15u;
        ok = up_used(4) && up_rec(4)->ver == UP_VER_SID && !upf_get(4, pk) && !memcmp(pk, want, sizeof pk) &&
             up_used(2) && up_rec(2)->engine == ENGI_FM6 && !upf_get(2, pk);
        bad += check("  presets: the v2 FM6 sound keeps its patch, the DIGITAL one has its own", ok);
    }
    ok = settings.palette == 2u && settings.lowcut == 1u;
    bad += check("  settings: the PER4's palette and low cut", ok);

    /* 4. Felucca 1.6's archive (felucca.c): FUNA projects (the music, slots 1 and 2), user presets of Felucca 1.2+ */
    {
        static funa_t fa, fb;
        static up_bank_t b;
        static const uint8_t ROWS[15] = {0, 0, 0, 0, 2, 1, 1, 4, 0, 1, 0, 1, 0, 4, 4};   /* A x2, B B - A, A B A - x4 */
        static const int16_t PAD[][2] = {{F_LSYNC, 4}, {F_SPRD, 30}, {P_M1SRC, 10}, {F_E0(104) + 1, 20}};
        static const int16_t BELL[][2] = {{F_ITYPE, 5}, {F_IA, 70}, {F_ESYNC, 1}, {F_E0(111) + 7, 20}};
        static const int16_t KIT[][2] = {{F_E0(111), 7}, {P_LN0, 100}, {F_FTYPE, 1}};
        static const int16_t BASS[][2] = {{F_FTYPE, 1}, {F_LPOL, 1}};
        static const int16_t OLD[][2] = {{F_LSYNC, 1}, {F_LTRIG, 1}, {F_LPOL, 1}, {F_SQNT, 0}, {F_E0(103) + 5, 77}};
        uint8_t pk[FM6_PACKED], fpk[FM6_PACKED];
        char note[260];
        int16_t v[P_COUNT];
        uint32_t i, n, a1;
        memset(&fa, 0, sizeof fa);
        fa.np = 111;
        for (k = 0; k < G_COUNT; k++)
            fa.g[k] = GP[k].def;
        fa.g[G_BPM] = 133; fa.g[G_SWING] = 10; fa.g[G_ROUTE] = 2; fa.g[G_RTYPE] = 1; fa.g[G_DTIME] = 3;
        funa_trk(&fa, 0, 0);                       /* T1 ANALOG, Felucca 1.2 .. 1.5's parameters set, QUANTIZE OFF */
        fa.t[0].p[F_LSYNC] = 3; fa.t[0].p[F_LTRIG] = 1; fa.t[0].p[F_LPOL] = 1; fa.t[0].p[F_SQNT] = 0;
        fa.t[0].p[F_SPRD] = 40; fa.t[0].p[F_ITYPE] = 2; fa.t[0].p[F_IA] = 80; fa.t[0].p[F_IMIX] = 100;
        fa.t[0].p[F_FTYPE] = 2; fa.t[0].p[F_ESYNC] = 1; fa.t[0].p[P_M1SRC] = 9; fa.t[0].p[F_E0(111) + 4] = 50;
        funa_note(&fa, 0, 0, 60, -8, 1, 0);
        funa_note(&fa, 0, 4, 64, 7, 1, 0);
        funa_note(&fa, 0, 8, 67, 5, 3, 0);         /* (x3: its NUDGE does not play) */
        funa_note(&fa, 0, 12, 72, 0, 1, 104);      /* (1:3) */
        funa_trk(&fa, 1, ENGI_FM6);                /* T2 FM6 on its voice bank's B4, QUANTIZE ON */
        fa.t[1].p[F_E0(111) + 7] = 12;
        funa_note(&fa, 1, 0, 48, 3, 1, 0);
        funa_note(&fa, 1, 8, 50, 0, 1, 111);       /* (FILL) */
        funa_trk(&fa, 2, ENGI_DRUM);               /* T3 DRUM: hits, accents, RATCH, a chance, !FILL */
        fa.t[2].step[0] = (fstep_t){{0}, 0, ST_NOTE, 0, 100, 1, 1, 0, 1, 0};
        fa.t[2].step[4] = (fstep_t){{0}, 0, ST_NOTE, 0, 100, 2, 0, 0, 2, 0};
        fa.t[2].step[8] = (fstep_t){{0}, 0, ST_NOTE, 0, 90, 5, 4, 50, 1, 0};
        fa.t[2].step[12] = (fstep_t){{0}, 0, ST_NOTE, 0, 100, 8, 0, 112, 1, 0};
        funa_trk(&fa, 3, 0);                       /* T4 empty */
        funa_mot(&fa, 0, 4, F_SPRD, 100, 1);       /* (SPREAD locked, CUTOFF's automation, INSERT A's) */
        funa_mot(&fa, 0, 0, F_E0(111) + 4, 30, 0);
        funa_mot(&fa, 1, 0, F_IA, 20, 0);
        fa.mot[1] = 1;                             /* (T1's plays) */
        fa.chain[0] = 3;
        memcpy(fa.chain + 4, ROWS, sizeof ROWS);
        for (k = 0; k < 4u; k++)
            memcpy(fa.fm6[k], FM6_INIT, FM6_PACKED);
        memcpy(fa.fm6[1], FM6_FACTORY[2], FM6_PACKED);
        fa.fm6[1][7] ^= 0x15u;
        fa.mch = 1u << 23;                         /* (MIDI IN TRACK) */
        memcpy(fa.name, "FELUCCA SONG", 12);
        memset(&fb, 0, sizeof fb);                 /* slot 2: Felucca 1.4's (104) */
        fb.np = 104;
        for (k = 0; k < G_COUNT; k++)
            fb.g[k] = GP[k].def;
        funa_trk(&fb, 0, 2);                       /* T1 PHASE */
        fb.t[0].p[F_LSYNC] = 2; fb.t[0].p[F_SPRD] = 20; fb.t[0].p[F_E0(104) + 2] = 99;
        funa_note(&fb, 0, 0, 62, 0, 1, 0);
        funa_note(&fb, 0, 8, 62, 0, 1, 0);
        funa_trk(&fb, 1, 3);                       /* T2 LOFI */
        funa_note(&fb, 1, 0, 55, 0, 1, 0);
        funa_trk(&fb, 2, 0);
        funa_trk(&fb, 3, 0);
        for (k = 0; k < 4u; k++)
            memcpy(fb.fm6[k], FM6_INIT, FM6_PACKED);
        memcpy(fb.name, "SLOT B", 6);

        conv_reset();
        memset(in, 0, sizeof in);
        funa_pack(in[0].b, &fa); in[0].len = 3840;  /* (the music: slot 1 as saved) */
        funa_pack(in[2].b, &fa); in[2].len = 3840;
        funa_pack(in[3].b, &fb); in[3].len = 3840;
        {
            persist_t s = per_from(PERSIST_MAGIC, 1);
            memcpy(in[1].b, &s, sizeof s); in[1].len = sizeof s;
        }
        memset(&b, 0, sizeof b);
        b.magic = UP_BANK_MAGIC; b.rsize = sizeof(up_rec_t); b.nslot = UP_PER_BANK;
        b.r[0] = frec(UP_VER, 0, "FEL PAD", 104, PAD, 4, 3);
        b.r[1] = frec(UP_VER, ENGI_FM6, "FEL BELL", 111, BELL, 4, 0);
        b.r[2] = frec(UP_VER_GRID, ENGI_DRUM, "FEL KIT", 111, KIT, 3, 0);
        b.r[2].note[0] = 5; b.r[2].flags[0] = 1;
        b.r[3] = frec(UP_VER, 0, "FEL BASS", 111, BASS, 2, 1);
        b.r[4] = frec(UP_VER, 0, "FEL OLD", 103, OLD, 5, 0);
        up_bank[0] = b;                            /* (the FM6 one's patch, tied as Felucca ties it: upf_tag) */
        upf_empty();
        memcpy(fpk, FM6_FACTORY[2], sizeof fpk);
        fpk[7] ^= 0x15u;
        upf_set(1, fpk);
        memcpy(in[9].b, &upf, sizeof upf); in[9].len = sizeof upf;
        memcpy(in[6].b, &b, sizeof b); in[6].len = sizeof b;
        write_archive("conv_funa.json", in, 2);
        conv_source(1);
        ok = needs(in, 0) && needs(in, 2) && needs(in, 3) && needs(in, 6) && !needs(in, 1);
        memcpy(conv_bytes, in[2].b, 3840);
        conv_bytes[1000] ^= 1u;                    /* (damaged: its hash) */
        ok &= conv_put(2, 3840) == 2u;
        bad += check("Felucca 1.6's: FUNA projects and 1.2+ presets to convert; a damaged FUNA refused", ok);

        conv_reset();
        conv_source(1);
        put_all(in, rc);
        conv_fix();
        get_all(out);
        write_archive("conv_funa_native.json", out, 0);
        ok = !rc[0] && !rc[1] && !rc[2] && !rc[3] && !rc[6] && !rc[9] && magic(out, 0) == PROJ_MAGIC4 &&
             magic(out, 2) == PROJ_MAGIC4 && magic(out, 3) == PROJ_MAGIC4 && out[16].len &&
             proj_import(&got, out[2].b, (int)out[2].len) && got.g[G_ROUTE] == 0 && got.g[G_BPM] == 133;
        ok &= !memcmp(got.name, "FELUCCA SONG", 12);   /* (slot 1's MIDI IN CH5-8: Skiff's CH1-6, what a first boot
                                                        * adopts; the music's is the unit's) */
        n = conv_note(0);
        memcpy(note, conv_bytes, n); note[n] = 0;
        ok &= !!strstr(note, "T1 ANALOG filter TYPE") && !!strstr(note, "T1 T2 NUDGE") &&
              !!strstr(note, "MIDI IN TRACK") && !strstr(note, "song") && conv_note(2) && !conv_note(3);
        n = conv_note(6);
        memcpy(note, conv_bytes, n); note[n] = 0;
        ok &= !!strstr(note, "U04 ANALOG filter TYPE") && !!strstr(note, "categories: 2") && !strstr(note, "U03");
        bad += check("  restored: the music, slots 1, 2 SKF4; noted: filter TYPE, NUDGE, MIDI, categories", ok);

        conv_reset();                              /* the converted objects onto a fresh unit */
        put_all(out, rc);
        ok = 1;
        for (id = 0; id < 32u; id++)
            ok &= !rc[id] && !needs(out, id);
        ok &= trk[0].eng_req == 0u && trk[0].p[P_LSYNC] == 3 && trk[0].p[P_LTRIG] == 1 && trk[0].p[P_LPOL] == 1 &&
              motion_base_value(&trk[0], P_SPRD) == 40 && trk[0].p[P_ITYPE] == 2 && trk[0].p[P_IA] == 80 &&
              trk[0].p[P_IMIX] == 100 && trk[0].p[P_ESYNC] == 1 && trk[0].p[P_M1SRC] == 10 &&
              motion_base_value(&trk[0], P_E0 + 4) == 50 && trk[0].p[P_SGATE] == 64 && trk[1].eng_req == ENGI_FM6 &&
              trk[1].p[P_E7] == FM6_OWN && song.g[G_BPM] == 133 && song.g[G_SWING] == 10 && song.g[G_RTYPE] == 1 &&
              song.g[G_DTIME] == 3;
        fm6_pack(fm6_patch[1], pk);
        ok &= !memcmp(pk, fa.fm6[1], sizeof pk);
        bad += check("a fresh unit takes them; the music's sounds by name (LFO SYNC, INSERT, S&H ..), B4 OWN", ok);
        ok = trk[0].step[0].note[0] == 60u && trk[0].step[0].nudge == -32 && trk[0].step[4].nudge == 28 &&
             trk[0].step[8].nudge == 0 && step_ratchet(&trk[0].step[8]) == 3u && trk[0].step[12].cond == TC_1_3 &&
             !trk[0].step[12].probability && trk[1].step[0].note[0] == 48u && !trk[1].step[0].nudge &&
             trk[1].step[8].cond == TC_FILL && trk[2].step[0].hit == 1u && trk[2].step[0].acc == 1u &&
             step_ratchet(&trk[2].step[4]) == 2u && trk[2].step[8].hit == 5u && trk[2].step[8].acc == 4u &&
             trk[2].step[8].probability == 50u && trk[2].step[12].cond == TC_NFILL && (motion.on & 3u) == 1u;
        for (i = n = 0; i < motion.count; i++) {
            const motion_event_t *e = &motion.event[i];
            n += e->place == 4u && e->id == P_SPRD && e->value == 100 && e->lock;
            n += e->place == 0u && e->id == P_E0 + 4 && e->value == 30 && !e->lock;
            n += e->place == 64u && e->id == P_IA && e->value == 20 && !e->lock;
        }
        bad += check("  its steps: NUDGE x4 (none where it did not play), RATCH, conditions; locks, automation",
                     ok && n == 3u);
        a1 = lists.ent[0][0][0];
        ok = a1 && a1 <= NPAT && lists.ent[0][0][1] == 1u && lists.ent[0][1][0] && lists.ent[0][1][0] != a1 &&
             !lists.ent[0][1][1] && lists.ent[0][2][0] == a1 && lists.ent[0][2][1] == 3u && !lists.ent[0][3][0] &&
             lists.cur[0] == 0u && trk_pid[0] == a1 && lists.ent[1][2][0] == lists.ent[1][1][0] &&
             lists.ent[1][1][0] != lists.ent[1][0][0] && lists.ent[2][1][0] == PAT_REST && !lists.ent[2][1][1] &&
             lists.ent[2][2][0] == lists.ent[2][0][0] && lists.ent[3][0][0] == PAT_REST && lists.ent[3][0][1] == 1u &&
             lists.ent[3][2][0] == PAT_REST && lists.ent[3][2][1] == 3u && lists.cur[3] == 3u && !lists.ent[3][3][0] &&
             lists.song == SONG_OFF;
        bad += check("  the song's 3 sections on T1-T4's lists (repeats, REST lanes), each on its own", ok);
        project_load(0);                           /* (slot 1: the music as saved, the song too) */
        ok = proj_cur == 0u && trk[0].p[P_LSYNC] == 3 && motion_base_value(&trk[0], P_SPRD) == 40 &&
             lists.ent[0][2][1] == 3u && lists.ent[2][1][0] == PAT_REST && lists.ent[0][0][0] == a1;
        project_load(1);                           /* (slot 2: Felucca 1.4's) */
        ok &= proj_cur == 1u && trk[0].eng_req == 2u && trk[0].p[P_LSYNC] == 2 && trk[0].p[P_SPRD] == 20 &&
              trk[0].p[P_E0 + 2] == 99 && !trk[0].p[P_ITYPE] && !trk[0].p[P_ESYNC] && trk[0].step[8].note[0] == 62u &&
              trk[1].eng_req == 3u && trk[1].step[0].note[0] == 55u;
        bad += check("  slots 1 and 2 (FUNA of 111 and 104): sounds by name, notes, slot 1's song", ok);
        ok = 1;
        for (k = 0; k < 5u; k++)
            ok &= up_used(k) && up_rec(k)->ver == UP_VER_SID && !memcmp(up_rec(k)->name, b.r[k].name, 12);
        up_values(up_rec(0), v);
        ok &= v[P_LSYNC] == 4 && v[P_SPRD] == 30 && v[P_M1SRC] == 11 && v[P_E0 + 1] == 20 && !v[P_ITYPE];
        up_values(up_rec(1), v);
        ok &= v[P_ITYPE] == 5 && v[P_IA] == 70 && v[P_ESYNC] == 1 && v[P_E7] == FM6_OWN;
        up_values(up_rec(2), v);
        ok &= up_rec(2)->engine == ENGI_DRUM && v[P_E0] == 7 && v[P_LN0] == 100;
        up_values(up_rec(4), v);
        ok &= v[P_LSYNC] == 1 && v[P_LTRIG] == 1 && v[P_LPOL] == 1 && v[P_E0 + 5] == 77 && !v[P_SPRD];
        ok &= !upf_get(1, pk) && !memcmp(pk, fpk, sizeof pk);
        bad += check("  presets of np 104, 111 (a grid), 103 -> v6 by name; the FM6 one keeps its patch", ok);
    }
    printf("%s\n", bad ? "CONVERTER TEST FAILED" : "converter test passed");
    return bad != 0;
}
