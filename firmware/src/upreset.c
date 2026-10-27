/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, A/B sectors at
 * 0xDC000..0xDFFFF), 16 records each, mirrored in RAM so browsing never
 * reads flash. A record: engine, name, the instrument parameters, and a 16-step pattern's fields. An FM6
 * sound's patch is kept beside the record (up_fm6.c, since 1.0.3: the record itself is unchanged). Loading one
 * loads the sound only. Skiff (phase 5): a user preset is a sound; SAVE writes no pattern (the fields stay 0) and
 * nothing reads one (the editor's UP_PUT / UP_GET still carry the fields: the format is unchanged).
 *
 * Versions: a bank whose magic, record size or slot count differ reads as
 * empty; so does a record with another layout version. A record keeps np =
 * the P_COUNT it was stored with; when that differs from today's it is
 * mapped by count: its last 8 values are P_E0..P_E7, the first np - 8 are
 * P_LEVEL.. in order, and parameters it does not have take their defaults.
 * That holds as long as common parameters are only ever added just before
 * P_E0 (else bump UP_VER and translate). Version 1 (before 1.0) has the
 * same layout; only its PHYS MODEL 2 meant DUST, which up_values loads as
 * MODAL bowed (eng_phys.c phys_legacy). A record of PHYS MODEL 4 (DRUM, version 2
 * until the kit became the DRUM engine) is that engine: up_migrate rewrites it
 * in the RAM mirror when a bank is read and when UP_PUT sends one (core.h
 * drum_from_phys); flash keeps the old record until its bank is written again,
 * which stores the new one. No version bump: today's PHYS has no MODEL 4, so
 * such a record cannot be anything else, and older firmware keeps reading the bank.
 * A record of SAMPLE SET 4 (PERC, the GM kit, retired after 1.0.2) is migrated the same way to the DRUM engine
 * with its default kit (core.h drum_from_perc: its E values the kit's, the rest of the sound and its pattern as
 * stored; the GM notes play the same drums).
 * Version 3 (UP_VER_GRID, since the DRUM grid) is the same record with a drum grid as its pattern: note[i] is
 * step i's lane hits (bit l = lane l), flags[i] their accents. A DRUM track whose first 16 steps strike a
 * lane is stored so (its notes on their lanes); every other sound as version 2, which older firmware reads.
 * Older firmware shows a version 3 record as empty and keeps its bytes.
 * A note pattern keeps each step's RATCH in its flags (core.h SF_RATCH, 8 | 16; 0 = x1, every older record):
 * bits firmware before RATCH drops when it loads the pattern, so the record is the same version. (Skiff) FLAM
 * likewise (core.h SF_FLAM, 32). A drum grid
 * has no room for it (its flags are the accents): it loads x1.
 * (Skiff) A version 4 or 5 record keeps its pattern's NUDGE (both kinds) in the last 12 bytes of its parameter area,
 * packed[132..143]: 6 bits a step, two's complement (core.h
 * nudge_pack). Firmware before wrote 0s there (on the grid) and never reads them: the same version both ways. A
 * loaded user pattern brings its DIR too, with its LEN, DIV, SWING and GATE (a record without one: FWD).
 * A record of engine 1 (DIGITAL, retired in 1.0) stays as it is (UP_PUT takes it too): its values are DIGITAL's,
 * and every load converts them to an FM6 sound with its own patch (ui.c fm4_apply, fm4_convert.c); lists count it
 * with FM6's (up_engine).
 * (Skiff 2.0, Tier B: design/TIER_B.md 7) Version 6 (UP_VER_SID), what SAVE > USER and UP_PUT write: no pattern, the
 * parameters by stored id (core.h SID) in the parameter area and the old pattern's 32 bytes (sid[], UP_SIDS): np is
 * how many, value + 64 each. A newer record's ids past ours are passed over, an older one's missing ones load at their
 * defaults. Versions 1..5 are read as before. Firmware before 2.0 shows a version 6 record as empty and keeps its bytes.
 * (2.0, Tier B 10) Versions 4..6 only, and no DIGITAL record: versions 1..3, the PHYS DRUM / SAMPLE PERC migration and
 * DIGITAL's conversion are built with FELUCCA_LEGACY alone (the converter, web/conv). Such a record keeps its bytes and
 * shows OLD FORMAT (up_old).
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* 2 since 1.0; 1 is read too (PHYS MODEL 2 was DUST) */
#define UP_VER_GRID 5u                           /* 2 with a drum grid as the pattern (see the top) */
#define UP_VER_SID 6u                            /* ((2.0) by stored id, no pattern: see the top) */
#define UP_SIDS (UP_PMAX * 2u + 32u)             /* (.. its room: the parameters' and the pattern's bytes) */
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored ((2.0) v6: the ids) */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    union {
        struct {
            union { int16_t p[UP_PMAX]; uint8_t packed[UP_PMAX * 2u]; };
            uint8_t note[16], flags[16];         /* note 0 = rest; flags 1 accent, 2 slide, 4 tie, SF_RATCH, SF_FLAM
                                                  * (UP_VER_GRID: lane hits, their accents) */
        };
        uint8_t sid[UP_SIDS];                    /* (v4, v5: packed; (2.0) v6: by stored id, value + 64) */
    };
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 192, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX * 2u && SID_COUNT <= UP_SIDS, "user preset record: P_COUNT, the SIDs");
/* (Skiff) packed[132..143] held the pattern's NUDGE (see the top); (phase 5) a user preset holds no pattern and nothing
 * reads one, so (1.3) the parameters may pass 132 into it: firmware before maps them by count, as any np */
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK];

static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

static int up_valid(const up_rec_t *r)
{
    if (!(r->used == UP_USED && r->ver >= (FELUCCA_LEGACY ? 1u : 4u) && r->ver <= UP_VER_SID && r->engine < NENGINES &&
          (FELUCCA_LEGACY || FELUCCA_FM4 || r->engine != ENGI_DIGITAL) && r->np >= 8u &&
          r->np <= (r->ver >= UP_VER_SID ? UP_SIDS : r->ver >= 4u ? UP_PMAX * 2u : UP_PMAX) && r->name[0])) return 0;
    if (r->ver >= 4u) for (uint32_t i = 0; i < r->np; i++) if (r->sid[i] > 191u) return 0;
    return 1;
}

static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }
/* (2.0, D3) slot k holds a sound of a format of before (versions 1..3, a DIGITAL one): kept, OLD FORMAT */
static int up_old(uint32_t k)
{
    const up_rec_t *r = up_rec(k % UP_SLOTS);
    return !FELUCCA_LEGACY && r->used == UP_USED && r->ver && (r->ver < 4u || (r->ver < UP_VER_SID && r->engine == ENGI_DIGITAL));
}

static int up_grid(const up_rec_t *r) { return r->ver == 3u || r->ver == UP_VER_GRID; }
static int up_pat(const up_rec_t *r) { return r->ver < UP_VER_SID; }   /* (it holds a 16-step pattern's fields) */
/* value k as stored: its place by count (versions 1..5) or (v6) its stored id */
static int16_t up_value(const up_rec_t *r, uint32_t k) { return r->ver >= 4u ? (int16_t)r->sid[k] - 64 : r->p[k]; }
static void up_set_value(up_rec_t *r, uint32_t k, int16_t v)
{
    if (r->ver >= 4u) r->sid[k] = (uint8_t)(v + 64); else r->p[k] = v;
}
/* (2.0) r as a version 6 record of the sound v (today's P_* order, each value inside its range): no pattern */
static void up_set_sound(up_rec_t *r, const int16_t *v)
{
    uint32_t s;
    r->ver = UP_VER_SID;
    r->np = (uint8_t)SID_COUNT;
    memset(r->sid, 0, sizeof r->sid);
    for (s = 0; s < SID_COUNT; s++)
        r->sid[s] = (uint8_t)(v[sid_p(s)] + 64);
}
#if FELUCCA_LEGACY
/* Legacy PHYS drums map through decoded parameters, not their disk representation. */
static void up_migrate(up_rec_t *r)
{
    int16_t e[8]; uint32_t k;
    if (!up_valid(r) || r->ver >= UP_VER_SID) return;   /* ((2.0) 2.0 writes neither) */
    for (k = 0; k < 8u; k++) e[k] = up_value(r, r->np - 8u + k);
    if (drum_from_phys(r->engine, e) || drum_from_perc(r->engine, e)) {   /* (SAMPLE PERC: see the top) */
        r->engine = ENGI_DRUM;
        for (k = 0; k < 8u; k++) up_set_value(r, r->np - 8u + k, e[k]);
    }
}
#endif

static void up_pat_norm(uint8_t *note, uint8_t *flags);
/* after loading bank b (len bytes, -1 = none): wrong shape -> empty; each record's pattern inside its fields, as
 * UP_PUT and SAVE store it (flash or a restored backup may hold anything there) */
static void up_bank_check(uint32_t b, int len)
{
    up_bank_t *bk = &up_bank[b];
    uint32_t i, k;
    if (len != (int)sizeof *bk || bk->magic != UP_BANK_MAGIC || bk->rsize != sizeof(up_rec_t) ||
        bk->nslot != UP_PER_BANK)
        memset(bk, 0, sizeof *bk);
    for (i = 0; i < UP_PER_BANK; i++) {
        up_rec_t *r = &bk->r[i];
        if (!up_valid(r) || !up_pat(r))                 /* ((2.0) v6: no pattern, its bytes values) */
            continue;
#if FELUCCA_LEGACY
        up_migrate(r);
#endif
        for (k = 0; k < 16u; k++)
            if (up_grid(r))
                r->flags[k] &= r->note[k];          /* a drum grid: accents only on its hits */
            else
                up_pat_norm(&r->note[k], &r->flags[k]);
    }
}

/* the record's values in today's P_* order (mapped by count, see above; (2.0) v6 by stored id); def = the defaults */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    int16_t values[UP_PMAX * 2u];
    uint32_t i;
    if (r->ver >= UP_VER_SID) {
        for (i = 0; i < P_COUNT; i++)
            out[i] = def[i];
        for (i = 0; i < r->np; i++)
            if (sid_p(i) < P_COUNT)
                out[sid_p(i)] = up_value(r, i);
        return;
    }
    for (i = 0; i < r->np && i < NELEM(values); i++) values[i] = up_value(r, i);
    params_by_count(out, values, r->np, def);
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

/* slot k's name, upper case, 0-terminated (b holds 13); ((2.0) none there "--", a sound of a format of before OLD FORMAT:
 * the question before SAVE writes over it) */
static void up_name(uint32_t k, char *b)
{
    const char *s = up_used(k) ? up_rec(k)->name : up_old(k) ? "OLD FORMAT" : "--";
    uint32_t i;
    for (i = 0; i < 12u && s[i]; i++)
        b[i] = s[i] >= 'a' && s[i] <= 'z' ? (char)(s[i] - 32) : s[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE | SF_RATCH | SF_FLAM)) : 0u;
    }
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) [, kind, 16 x hi] -> *r (values not
 * yet clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. kind 1 (and its 16 bytes):
 * a drum grid, the pairs the low 7 bits of each step's hits and accents, hi bit 0 / 1 their bit 7 (lane 8) */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k, end;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    end = k + 2u * P_COUNT + 32u;
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || (!FELUCCA_LEGACY && !FELUCCA_FM4 && a[1] == ENGI_DIGITAL) ||
        2u + n >= na || !up_name_ok(a + 2, n) || (na != end && (na != end + 17u || a[end] > 1u)))
        return 1;                                /* ((2.0) DIGITAL: none, as up_valid) */
    up_rec_t staged, *target = r;
    r = &staged;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        {
            int32_t value = (int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192;
            if (value < -64 || value > 127) return 1;
            up_set_value(r, i, (int16_t)value);
        }
    if (na >= k + 33u + 16u && a[k + 32u] == 1u) {    /* a drum grid */
        r->ver = UP_VER_GRID;
        for (i = 0; i < 16u; i++) {
            uint32_t hi = a[k + 33u + i];
            r->note[i] = (uint8_t)((a[k + 2u * i] & 127u) | (hi & 1u) << 7);
            r->flags[i] = (uint8_t)(((a[k + 2u * i + 1u] & 127u) | (hi & 2u) << 6) & r->note[i]);
        }
        *target = *r;
        return 0;
    }
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
#if FELUCCA_LEGACY
    up_migrate(r);                               /* (an editor of before the DRUM engine) */
#endif
    *target = *r;
    return 0;
}

#ifndef UP_HOST
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = param_desc_of(r->engine, i)->def;
    up_params(r, v, def);
#if FELUCCA_LEGACY
    if (r->ver == 1u && ENGINES[r->engine] == &ENG_PHYS)   /* (before 1.0: MODEL 2 was DUST) */
        phys_legacy(&v[P_E0]);
#endif
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)param_fit(param_desc_of(r->engine, i), v[i]);   /* (a retired KIT: the kit it plays) */
}

#include "up_fm6.c"                            /* the FM6 user presets' patches: the same kind of store */

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, flash_ok ? st_load(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]) : -1);
#endif
    upf_boot();                                  /* (after the banks: it may move the retired FM6 bank's patches) */
#ifdef FELUCCA_FAVORITES
    for (uint32_t k = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) favorite_set(NENGINES, k, 0);
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the transport runs (nothing
 * written), 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk;
#if FELUCCA_FLASH
    up_rec_t old;
    uint32_t magic;
    uint16_t rsize, nslot;
#endif
    if (k >= UP_SLOTS)
        return 1;
    if (transport_busy()) {                            /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    bk = &up_bank[k / UP_PER_BANK];
#if FELUCCA_FLASH
    old = *up_rec(k);
    magic = bk->magic;
    rsize = bk->rsize;
    nslot = bk->nslot;
#endif
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk)) {
        *up_rec(k) = old;
        bk->magic = magic;
        bk->rsize = rsize;
        bk->nslot = nslot;
        return 2;
    }
#endif
    if (!r) {
#ifdef FELUCCA_FAVORITES
        if (favorite_set(NENGINES, k, 0)) settings_save();
#endif
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if FELUCCA_FLASH
    if (flash_ok)
        return 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the automatic name of engine e's sound in slot k: engine name + slot number ("ANALOG 07"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[4];
    e %= NENGINES;
    str_cpy(b, ENGINES[eng_ok(e) ? e : ENGI_FM6]->name, 9);   /* (a DIGITAL record plays as FM6) */
    up_slot_label(l, k);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), l + 1, 3);
}

/* the record's name = name (at most 12), 0 or "": the automatic one */
static void up_set_name(up_rec_t *r, uint32_t k, const char *name)
{
    char b[16];
    uint32_t i;
    if (!name || !name[0]) {
        up_auto_name(b, r->engine, k);
        name = b;
    }
    memset(r->name, 0, sizeof r->name);
    for (i = 0; i < 12u && name[i]; i++)
        r->name[i] = name[i];
}

static void autosave_soon(void);                       /* (project.c) */

/* the selected part's sound -> slot k (no pattern: the fields stay 0); name 0 or "": the automatic name
 * (up_auto_name); up_put's result. ((2.0, #20) Stored, the track's sound is the slot's: on it as a LOAD would put it,
 * its name on HOME, kept by a project and by RESTORE LAST, brought up to date at once) */
static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    int16_t v[P_COUNT];
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.engine = TSEL->eng_req;
    up_set_name(&r, k, name);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)motion_base_value(TSEL, i);
    up_set_sound(&r, v);                                /* ((2.0) version 6) */
    if (r.engine == ENGI_FM6 && TSEL->p[P_E7] == FM6_OWN && name && name[0]) {   /* (FM6 LAB) its own patch takes a
                                                         * typed name (its first 10: .syx keeps it); a factory patch
                                                         * (F n) and the automatic name leave it as it is */
        char a[16];
        up_auto_name(a, r.engine, k);
        if (!str_eq(a, name)) {
            uint32_t tr = trk_index(TSEL) % NTRK, n = str_len(name);
            uint8_t v[FP_SIZE + 1u];
            memcpy(v, fm6_patch[tr], sizeof v);
            for (i = 0; i < 10u; i++)
                v[FP_NAME + i] = (uint8_t)(i < n ? name[i] : ' ');
            fm6_set_patch(tr, v);
        }
    }
    {
        int rc = up_put(k, &r);                         /* the record first, then its FM6 patch (up_fm6.c) */
        if ((rc == 0 || rc == 3) && r.engine == ENGI_FM6) {
            int u = upf_store(k, (uint32_t)(TSEL - trk));
            if (u == 2)
                rc = 2;
        }
        if (rc == 0 || rc == 3) {
            for (i = 0; i < NTRK; i++)                  /* (another track on the slot plays what it had: off it) */
                if (trk[i].user == k + 1u)
                    trk[i].user = 0;
            TSEL->user = (uint8_t)(k + 1u);
            autosave_soon();
        }
        return rc;
    }
}

/* slot k renamed (name 0 or "": the automatic one), the rest as it is; up_put's result, 1 for an empty slot */
static int up_rename(uint32_t k, const char *name)
{
    up_rec_t r;
    if (!up_used(k))
        return 1;
    r = *up_rec(k);
    up_set_name(&r, k, name);
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except the track's own (param_kept:
 * the mix, ARP, SCL, the pattern parameters, the SLICER). The steps stay. 0 ok, 1 empty, 2 refused: (Skiff phase 6)
 * an engine the track may not play (said; nothing changes) */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k))
        return 1;
    if (!eng_allowed(song.sel, up_engine(k))) {
        eng_refused(up_engine(k));
        return 2;
    }
    r = up_rec(k);
    up_values(r, v);
    load_begin(t, UNDO_SOUND);                          /* (ui.c: the copy for SAVE held = undo) */
    panic_req |= (uint8_t)(1u << song.sel);
#if !FELUCCA_FM4 && FELUCCA_LEGACY
    if (r->engine == ENGI_DIGITAL) {                    /* a DIGITAL sound (kept as it was stored): FM6 */
        int16_t p[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            p[i] = param_kept(i) ? t->p[i] : v[i];
        fm4_apply(t, p);
    } else
#endif
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        fm1_irq_on();
        upf_track_load(t, k);                           /* FM6: the preset's own patch (up_fm6.c) */
    }
    t->user = (uint8_t)(k + 1u);
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

/* the engine a used slot's sound plays on (a DIGITAL record: FM6, without FELUCCA_FM4) */
static uint32_t up_engine(uint32_t k) { return eng_ok(up_rec(k)->engine) ? up_rec(k)->engine : ENGI_FM6; }

/* the PRESETS list's user presets (ui.c preset_all_*): the used slots, (Skiff phase 6) those the selected track may
 * play (engines.c eng_allowed: no PHYS or GRAIN sound on a light track) */
static int up_listed(uint32_t k) { return up_used(k) && eng_allowed(song.sel, up_engine(k)); }
static uint32_t up_count(void)                 /* listed slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_listed(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th listed one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_listed(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* listed slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_listed(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar. name: the save's or the rename's (0 or "": automatic) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name)   /* 0 load, 1 erase, 2 save, 3 rename */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if ((op < 2u || op == 3u) && !up_used(k)) {
        ui_message(up_old(k) ? "OLD FORMAT" : "EMPTY SLOT");   /* ((2.0) converted by the editor's Restore) */
        return;
    }
    if (op && (song.playing || transport_req == 1u)) {    /* (up_put refuses too) */
        ui_message(op == 1u ? "STOP TO ERASE" : op == 3u ? "STOP TO RENAME" : "STOP TO SAVE");
        return;
    }
    if (op == 0u) {
        if (!up_load(k))                                   /* ((Skiff phase 6) refused: said why) */
            ui_say("LOADED ", l);
        return;
    }
    rc = op == 1u ? up_put(k, 0) : op == 3u ? up_rename(k, name) : up_store(k, name);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED UNTIL POWER OFF" : op == 3u ? "RENAMED UNTIL POWER OFF" : "SAVED UNTIL POWER OFF");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : op == 3u ? "RENAMED " : "SAVED ", l);
    ui.force = 1;
}
static void up_ui(uint32_t op, uint32_t k) { up_ui_named(op, k, 0); }
#endif
