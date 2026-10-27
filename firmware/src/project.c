/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots. The slots live in .noinit RAM: they
 * survive resets and UBOOT entry. With FELUCCA_FLASH (default) every save
 * also goes to flash through storage.c, and an
 * empty RAM slot is filled from flash on load.
 *
 * (Skiff 2.0, Tier B: design/TIER_B.md 10) This firmware reads SKF3 and SKF4 (below) only. The formats before them
 * (FUN1..FUN9, SKF1, SKF2) and the conversions of their sounds are built with FELUCCA_LEGACY alone: the converter
 * (web/conv), which makes SKF4 and patterns of them. A slot holding one keeps its bytes and shows OLD FORMAT (proj_old).
 *
 * Format 7 ("FUN7", written) stores P_COUNT (byte 66) and maps an older count as user presets do: FUN7 of
 * 89 parameters (before the chord keys P_CHRD / P_VOIC) loads its engine values at today's P_E0..P_E7,
 * the chord keys OFF / CLOSE, and its motion events' ids from its P_E0 on move up with them (proj_motion_ids).
 * 68 + 4 x (91 + 2 + 64 x 9) + chain + motion = 3040 of the 3372 bytes before the name: 332 spare, room for
 * 83 more track parameters (4 bytes each).
 * Format 6 ("FUN6") adds a 36-byte song chain before the checksum.
 * Format 5 ("FUN5", read only) = format 4 with the drum grid: a step is 10 bytes (step_t: its lane hits and
 * their accents after the 8 bytes it was), and each track has 40 reserved bytes (lane[8][5], written 0: room
 * for per-lane sounds of the DRUM engine). 4 ("FUN4": PROJ_NP_V4 parameters, the modulation matrix), 3
 * ("FUN3": PROJ_NP_V3, the SLICER), 2 ("FUN2") and 1 ("FUN1": PROJ_NP_V2) are read and converted, mapped by
 * count as user presets are (the first np - 8 are P_LEVEL.. in order, the last 8 P_E0..P_E7; the
 * parameters added since take their defaults: the SLICER OFF, every matrix slot OFF). Their steps get no
 * hits; on a DRUM track their notes that are a lane's note become its hits (proj_grid: the same notes and
 * velocities play, the grid shows them as its own).
 * Their engine bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines
 * added since (SLICE 8, ..) were appended, no index moved.
 *
 * Track 4 was the GM drum part until 1.0 (no engine: its byte 0; its level and reverb send in the
 * globals G_DRLVL / G_DRREV, now (Skiff phase 9) G_CMODE / G_CFDBK). A project says which it has in `parts`: 4 (PROJ_PARTS)
 * when written since, 0 before (a reserved byte, always written 0: the format and its size did not
 * change). proj_drums_to_part turns such a track 4 into a DRUM part with its default kit (the GM map, so
 * its drum steps still play drums; until 1.0.2 it was the SAMPLE engine's PERC set), keeping its steps, its
 * pattern and mix parameters (LEN DIV SWING GATE, PAN MUTE), its SLICER, and the drum level and reverb send
 * as LEVEL and REV.
 *
 * The byte `phys` (reserved, always 0, before 1.0) says what a PHYS track's MODEL means: 0 MODEL 2 was
 * DUST (dropped: it loads as MODAL bowed, eng_phys.c phys_legacy); 1 MODEL 4 was DRUM (the kit is the DRUM
 * engine since: such a track loads as DRUM, core.h drum_from_phys); 2 (PROJ_PHYS) as today. proj_phys.
 *
 * Format 8 ("FUN8", written since 1.0) = FUN7 with each track's FM6 patch (eng_fm6.c, the 128-byte packed
 * record, 4 x 128 bytes just before the name): a project is self-contained. On load an FM6 track's SLOT shows F n
 * when its patch is that factory one, else OWN (eng_fm6.c fm6_adopt; a stored 8..34, the B slots of the patch bank
 * before 1.0.3, is OWN too: the project has the patch).
 * It is 3584 bytes (FUN7: 3388); FUN7 is read (its tracks get the init patch). The retained cache (proj_slot,
 * .noinit) grew with it: after an update its slot 1 still starts with a FUN7 record, which is read; the other
 * slots fail their hash and come back from flash (persist_boot).
 *
 * Format 9 ("FUN9", written since 1.1) = FUN8 with 64 more bytes: the DRUM lane levels (P_LN0..P_LN7, #97) made
 * P_COUNT 99 (P_E0 91), and 68 + 4 x (99 + 2 + 576) + chain + motion = 3072 no longer fit before FUN8's patches
 * (3056). 3648 bytes, the patches at PROJ_FM6_OFF (3120): 48 spare, room for 12 more track parameters. FUN8 (3584,
 * its 91 parameters mapped by count: the lane levels 100 %) and FUN7 are read; the retained cache grew again (its
 * slot 1 still starts with the older record, read as above).
 *
 * Skiff's format ("SKF1", written since its NUDGE) = FUN9 with each track's NUDGE plane after its steps: the 64
 * steps' nudges in 6 bits, two's complement, 4 steps in 3 bytes (48 bytes; core.h nudge_pack). 3840 bytes, a
 * flash object's most (storage.c ST_PAYLOAD_MAX): 68 + 4 x (103 + 2 + 576 + 48) + chain + motion = 3280, 32
 * spare (8 more track parameters; the play direction DIR made P_COUNT 100, the ducking's BY DEPTH REL 103), the
 * patches at 3312. An SKF1 of 100 parameters (before the ducking) is read as FUN9 is, by count: no ducking. FUN9
 * (np 99 or, from a Skiff build before its NUDGE, 100: every step on the grid), FUN8 and FUN7 are read. Felucca
 * refuses it (a newer format).
 *
 * Parameter locks (1.1, core.h MOTION_LOCK) are motion records with bit 7 of their id byte set (P_COUNT 99 < 128:
 * the bit is free; (Skiff 1.3) P_COUNT passed 127, but a store's ids are PROJ_NP's, proj_motion_np): no byte moved for them, and every project before 1.1 has none. FUN9 holds them. A FUN8 / FUN7
 * record of a 1.1 development build may hold some: on load their ids move to today's positions as the others do,
 * the lock bit kept (proj_motion_ids). Firmware before 1.1 refuses a project that holds a lock (an id out of its
 * range), as with RATCH below; FUN9 it refuses anyway (a newer format).
 *
 * A step's RATCH (core.h SF_RATCH, the hits - 1, 0..3) is in bit 7 of its velocity byte (bit 0) and of its chance
 * byte (bit 1) of FUN7 / FUN8: bits every firmware before wrote 0, so every older project loads with its steps x1,
 * and no format or size changed. Firmware before RATCH refuses a project that holds a ratchet (those bytes out
 * of their range), as it would a newer format. FUN6..FUN1 have none (x1).
 * Skiff: a step's FLAM (core.h SF_FLAM, with RATCH x1 only) is bit 7 of its meta byte (n, time, accent, slide), and its
 * trig condition (TC_*) bit 7 of its four note bytes: bits firmware before wrote 0 and refuses set, as with RATCH.
 *
 * DIGITAL (engine 1) was retired in 1.0 (fm4_convert.c): a track of it, in any format, loads as FM6 with the
 * patch converted from its values as the track's own (proj_fm4, on every import; the stored record keeps what it
 * holds until saved again). Its motion events on the EDIT or OP ENV values are dropped.
 *
 * SAMPLE's SET 4, PERC (the GM drum kit), was retired after 1.0.2: a SAMPLE track that selects it, in any
 * format, loads as the DRUM engine with its default kit (core.h drum_from_perc: the same GM key map, its steps
 * as they are, the rest of its sound kept; proj_perc, on every import as proj_fm4). Its motion events on the
 * EDIT values are dropped (SAMPLE's meanings, not DRUM's).
 *
 * Built on the Mac too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP) and engines.c. */
#define PROJ_MAGIC 0x534B4631u                 /* (Skiff) "SKF1": FUN9 + the steps' NUDGE (see the top) */
#define PROJ_MAGIC2 0x534B4632u                /* (Skiff phase 5) "SKF2": no steps, each track's list (proj_pack2) */
#define PROJ_MAGIC_V9 0x46554E39u              /* "FUN9": FUN8, 64 bytes longer (the DRUM lane levels, 99 parameters) */
#define PROJ_MAGIC_V8 0x46554E38u              /* "FUN8": FUN7 + the tracks' FM6 patches */
#define PROJ_MAGIC_V7 0x46554E37u              /* "FUN7": serialized (byte params, packed steps), chain, motion */
#define PROJ_MAGIC_V6 0x46554E36u              /* FUN6: 69 parameters, drum grid, chain */
#define PROJ_MAGIC_V5 0x46554E35u              /* "FUN5": the grid, without the chain; read only */
#define PROJ_MAGIC_V4 0x46554E34u              /* "FUN4": four tracks, PROJ_NP_V4 parameters, 8-byte steps; read only */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": four tracks, PROJ_NP_V3 parameters; read only */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
#define PROJ_NP_V3 57u                         /* P_COUNT of format 3 (P_E0 was 49) */
#define PROJ_NP_V4 69u                         /* P_COUNT of format 4 (P_E0 61) */
#define PROJ_PHYS 2u                           /* project_t.phys: PHYS without DUST and DRUM (see the top) */
#define PROJ_NT 4u                             /* (Skiff phase 6) the tracks every format before SKF3 holds (FUN1's one
                                                * aside): their readers and writers keep to them; a track past them
                                                * loads as at power-on (proj_power_on) */
#define PROJ_PARTS 4u                          /* project_t.parts as every format since 1.0 writes it (0: track 4 is the
                                                * old GM drum part, PROJ_DRUMT) */
#define PROJ_DRUMT 3u
#define PROJ_NAME_LEN 12u                      /* the name: FUN7 bytes PROJ_NAME_OFF.. (the reserved tail's end) */
typedef struct {                               /* one track */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    uint8_t user;                              /* ((2.0, #20) SKF4: the user sound it came from, slot + 1; 0 none) */
#if FELUCCA_LEGACY
    step_t step[NSTEP];                        /* (the formats before SKF2: (2.0) SKF3, SKF4 hold none, the patterns do) */
#endif
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel;                               /* the selected track */
    uint8_t parts;                             /* PROJ_PARTS; 0: track 4 is the old GM drum part (see the top) */
    uint8_t phys;                              /* PROJ_PHYS: PHYS MODEL values as today; 1: MODEL 4 was DRUM;
                                                * 0 (a reserved byte before 1.0): MODEL 2 was DUST */
    uint8_t rsv;
    proj_trk_t t[NTRK];
#if FELUCCA_LEGACY
    chain_config_t chain;                      /* (FUN6 .. SKF1's) */
    motion_store_t motion;
#endif
    uint8_t fm6[NTRK][FM6_PACKED];             /* each track's FM6 patch, packed (eng_fm6.c) */
    char name[PROJ_NAME_LEN];                  /* the project's name: upper-case ASCII 32..126, 0-padded; "" = none */
    uint8_t ent[NTRK][NENT][2];                /* (SKF2) each track's list: pattern id + 1 (0: none), repeats - 1 */
    uint8_t snd[NTRK][NENT][2];                /* ((2.0, #30) SKF4) each entry's sound (ui.c lists.snd) */
    uint8_t cur[NTRK];                         /* (SKF2) the entry each track is on */
    uint8_t song;                              /* (SKF2) SONG on */
    uint8_t scene[PROJ_SCENES];                /* (SKF2) the scenes (bank.c) */
    uint8_t dx_on;                             /* (Skiff phase 7) 1: dx is the project's DRUM-X kit; 0 none (the factory's) */
    dx_lane_t dx[DV_NLANE];                    /* (SKF3) the DRUM-X kit (drumx_voice.c dx_kit), one for every DRUM-X track */
    uint32_t sum;
} project_t;
#if FELUCCA_LEGACY
/* Historical FUN5/6 types are frozen, independent of today's P_COUNT/step_t. */
typedef struct { uint8_t note[4], n, time, flags, vel, hit, acc; } step10_t;
typedef struct { int16_t p[69]; uint8_t engine, preset; step10_t step[NSTEP]; uint8_t lane[NLANE][5]; } proj_trk_v5_t;
typedef struct {                               /* format 5, before the song chain */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[PROJ_NT];
    uint32_t sum;
} project_v5_t;
typedef struct { uint32_t magic, size; int16_t g[G_COUNT]; uint8_t sel, parts, phys, rsv;
    proj_trk_v5_t t[PROJ_NT]; chain_config_t chain; uint32_t sum; } project_v6_t;
_Static_assert(sizeof(project_v5_t) == 3352u && sizeof(project_v6_t) == 3388u, "frozen formats 5 / 6 sizes");
#endif
/* Serialized FUN7 keeps the retained cache's exact extent. Params are biased
 * bytes, steps pack n/time/flags. Reserved tail is zero and covered by hash.
 * The tail's last 12 bytes (PROJ_NAME_OFF, just before the hash) are the project's name since 1.0:
 * ASCII 32..126 (upper case), 0-padded, all 0 = no name ("PROJECT A"). Firmware before wrote them 0 and
 * never reads them, so every FUN7 file stays valid both ways; FUN6..FUN1 imports get no name.
 * FUN8: the same, 3584 bytes, the four packed FM6 patches at PROJ_FM6_OFF (before the name); 16 bytes of the
 * reserved tail were left for parameters added later. FUN9: the same, 3648 bytes (see the top). */
#define PROJ_STORE_SIZE 3840u                  /* (Skiff) SKF1 */
#define PROJ_STORE_V9 3648u                    /* FUN9 */
#define PROJ_STORE_V8 3584u                    /* FUN8 */
#define PROJ_NUDGE_BYTES (NSTEP / 4u * 3u)     /* (Skiff) a track's NUDGE plane */
#define PROJ_STORE_V7 3388u                    /* FUN7 */
#define PROJ_NAME_OFF (PROJ_STORE_SIZE - 4u - PROJ_NAME_LEN)
#define PROJ_FM6_OFF (PROJ_NAME_OFF - PROJ_NT * FM6_PACKED)
/* (Skiff phase 5) "SKF2": a project without steps; the patterns are the pool's (bank.c), a project names them. 1280
 * bytes: 0..67 SKF1's header (65: SONG on), from 68 each track's np parameters (value + 64), engine and preset, then
 * the lists (PROJ_NT x NENT x (pattern id + 1, 0 none; repeats - 1)), the entry each track is on, the scenes
 * (PROJ_SC2 bytes); the FM6 patches at 752 (PROJ_FM6_AT), the name, the hash. Room for np up to 123 */
#define PROJ_STORE2 1280u
#define PROJ_SC2 50u                          /* SKF2's scene bytes (bank.c: 3 a scene, 5 bits a track of four) */
_Static_assert(PROJ_SCENES >= PROJ_SC2, "SKF2's scenes are the first bytes of today's");
/* (Skiff phase 6) "SKF3": SKF2 for six tracks and their scenes, 2048 bytes, the same codec (proj_fmt_t: a format's
 * magic, size, tracks, scene bytes): 68..697 the six tracks' parameters (np 103), engines and presets, 698..889 the
 * lists, 890..895 the entries, 896..977 the scenes; (phase 7) the DRUM-X kit at 1175 (PROJ_DX_AT: PROJ_DX_MARK, then
 * its 8 lanes, 11 bytes each, drumx_voice.c dx_lane_t; a record of before, 0 there: the factory kit), the FM6 patches
 * at 1264, the name at 2032, the hash. SKF2 holds no kit.
 * (Skiff phase 9) Every format stores PROJ_NP parameters a track, the layout before phase 9 (P_COUNT 103), by count:
 * the first PROJ_NP - 8 ids in order, then P_E0..P_E7 (proj_np_put; params_by_count reads them), so firmware before,
 * which refuses a count above its own, reads what this one writes. The parameters added since (PROJ_NX of them, P_DTYPE
 * .. P_LDECAY7) are SKF3's PX block, before the kit (PROJ_PX_AT, 981..1174: PROJ_PX_MARK, their count n, then each
 * track's n values + 64), written only when one is not its default: a project that uses none is the bytes it was, and
 * one without the block (every one of before) reads them as the defaults. Firmware before reads it without them. SKF1
 * and SKF2 hold none: only old records are written as them again (proj_fetch, a rename, a backup), their defaults */
#define PROJ_MAGIC3 0x534B4633u
#define PROJ_STORE3 2048u
typedef struct { uint32_t magic, size, nt, sc, dx; } proj_fmt_t;   /* (dx: 1 = it holds the DRUM-X kit) */
static const proj_fmt_t PROJ_SKF2 = {PROJ_MAGIC2, PROJ_STORE2, PROJ_NT, PROJ_SC2, 0u};
static const proj_fmt_t PROJ_SKF3 = {PROJ_MAGIC3, PROJ_STORE3, 6u, PROJ_SCENES, 1u};
/* (Skiff 2.0, Tier B: design/TIER_B.md 5) "SKF4": 2560 bytes, the parameters by stored id (core.h sid_put): one added
 * or moved never shifts the others. 8 the layout (PROJ_LAYOUT4; another is not one this firmware reads), 9 flags (0);
 * from 10 (PROJ_ENT4) the lists, 4 bytes an entry (pattern id + 1 or PAT_REST, the REPS byte, the entry's sound: #30's,
 * 0 0 none, engine + 1 and its preset, 0x80 + a user slot and 0), the entry each track is on, the scenes, sel, SONG,
 * the DRUM-X kit (PROJ_DX_MARK
 * and its lanes; 0: the factory's), the FM6 patches; then the globals by GSID and each track's engine, preset, user
 * slot + 1 (#20's, 0 none) and parameters by SID, the counts the record's (a newer one's past ours passed over, an
 * older one's missing at their defaults); the name, the hash last. Room for about 50 more parameters a track */
#define PROJ_MAGIC4 0x534B4634u
#define PROJ_STORE4 2560u
#define PROJ_LAYOUT4 1u
#define PROJ_ENT4 10u
static const proj_fmt_t PROJ_SKF4 = {PROJ_MAGIC4, PROJ_STORE4, 6u, PROJ_SCENES, 1u};
_Static_assert(PROJ_ENT4 + NTRK * (NENT * 4u + 1u) + PROJ_SCENES + 2u + 1u + DV_NLANE * 11u + NTRK * FM6_PACKED +
               1u + 2u * GSID_COUNT + NTRK * (4u + SID_COUNT) + PROJ_NAME_LEN + 4u <= PROJ_STORE4, "SKF4 holds it all");
#define PROJ_SKF (&PROJ_SKF4)                  /* the format a save writes */
#define PROJ_NAME_AT(f) ((f)->size - 4u - PROJ_NAME_LEN)
#define PROJ_FM6_AT(f) (PROJ_NAME_AT(f) - (f)->nt * FM6_PACKED)
#define PROJ_DX_MARK 0x58u                    /* ('X') the kit follows */
#define PROJ_DX_AT(f) (PROJ_FM6_AT(f) - 1u - DV_NLANE * sizeof(dx_lane_t))   /* (a dx format's) */
#define PROJ_NP 103u                          /* (phase 9) the parameters a track a store holds (see above) */
#define PROJ_NX 32u                           /* .. the ones after them, in the PX block: P_DTYPE .. P_ESYNC ((2.0, #36)
                                               * frozen: the lane pans after them are SKF4's alone) */
#define PROJ_ESHIFT (P_E0 - (PROJ_NP - 8u))   /* a store of PROJ_NP: its engine values' ids + this are today's */
#define PROJ_PX_MAX 32u                       /* .. its room: so many a track */
#define PROJ_PX_MARK 0x50u                    /* ('P') the PX block follows */
#define PROJ_PX_AT(f) (PROJ_DX_AT(f) - 2u - (f)->nt * PROJ_PX_MAX)   /* (a dx format's) */
#define PROJ_HEAD_END(f) ((f)->dx ? PROJ_PX_AT(f) : PROJ_FM6_AT(f))      /* where the tracks, lists and scenes end */
_Static_assert(sizeof(dx_lane_t) == 11u, "a DRUM-X lane is 11 bytes in SKF3");
_Static_assert(NTRK == 6u, "SKF3: six tracks");
_Static_assert(P_COUNT >= PROJ_NP && PROJ_NX <= PROJ_PX_MAX, "the parameters past PROJ_NP fit the PX block");
_Static_assert(68u + 6u * (PROJ_NP + 2u + NENT * 2u + 1u) + PROJ_SCENES <=
               PROJ_STORE3 - 4u - PROJ_NAME_LEN - 6u * FM6_PACKED - 1u - DV_NLANE * 11u - 2u - 6u * PROJ_PX_MAX,
               "SKF3's tracks, lists and scenes end before its PX block");
typedef union { uint32_t align; uint8_t raw[FELUCCA_LEGACY ? PROJ_STORE_SIZE : PROJ_STORE4]; } project_store_t;   /* ((2.0)
                                                * SKF4's, the largest record it reads; the converter's SKF1) */
_Static_assert(G_COUNT == 27u, "FUN7 globals retain original IDs");
#if FELUCCA_LEGACY
_Static_assert(sizeof(project_store_t) == 3840u && PROJ_STORE_V7 == sizeof(project_v6_t), "SKF1 / FUN7 sizes");
typedef struct {                               /* a track of format 4, read only */
    int16_t p[PROJ_NP_V4];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v4_t;
typedef struct {                               /* format 4 (1.0 development builds), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, phys, rsv;
    proj_trk_v4_t t[PROJ_NT];
    uint32_t sum;
} project_v4_t;
typedef struct {                               /* a track of format 3, read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3 (0.9 .. 1.0), read only */
    uint32_t magic, size;
    int16_t g[G_COUNT];
    uint8_t sel, parts, rsv[2];
    proj_trk_v3_t t[PROJ_NT];
    uint32_t sum;
} project_v3_t;
#endif
#define PROJ_DEF_SOUND 0xFFu                   /* preset byte: the track's power-on sound, no steps (format 1) */
#define PROJ_DEF_KEEP 0xFEu                    /* .. DRUM's kit (once SAMPLE PERC), steps and the rest kept (old drums) */
#if FELUCCA_LEGACY
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step8_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[PROJ_NT];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u && sizeof(project_v3_t) == 2584u &&
               sizeof(project_v4_t) == 2680u, "formats 1 / 2 / 3 / 4 as they were stored");
#endif
project_store_t proj_slot[4] __attribute__((section(".noinit")));

#if FELUCCA_LEGACY
/* the song chain the formats FUN6 .. SKF1 carry (rows of project slot A..D, repeats): read and checked, never
 * played (Skiff phase 5: a song is each track's list of patterns); a capture writes an empty one */
static void chain_defaults(chain_config_t *c)
{
    uint32_t i;
    memset(c, 0, sizeof *c);
    for (i = 0; i < CHAIN_ROWS; i++)
        c->row[i].repeat = 1;
}
static int chain_valid(const chain_config_t *c)
{
    uint32_t i;
    if (c->count > CHAIN_ROWS)
        return 0;
    for (i = 0; i < c->count; i++)
        if (c->row[i].slot >= 4u || !c->row[i].repeat || c->row[i].repeat > 16u)
            return 0;
    return 1;
}
#endif

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
static int proj_ok(const project_t *q) { return q->magic == PROJ_MAGIC && q->size == sizeof *q &&
    q->sum == proj_sum(q)
#if FELUCCA_LEGACY
    && chain_valid(&q->chain) && motion_valid(&q->motion)
#endif
    ; }

#if FELUCCA_LEGACY
/* G_RTYPE (id 24) was G_DRCH, the GM drum part's MIDI channel (0..16, 10 by default) until 1.0: a project of a
 * format before FUN7 may hold any channel there. FUN7 came after it was inert (always 0), so only those imports
 * set it: ROOM, the only reverb they knew */
static void proj_rtype_room(int16_t *g) { g[G_RTYPE] = 0; }

/* the globals of formats 1 and 2 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_v2(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V2 ? g2[i] : GP[i].def;
    proj_rtype_room(g);
}

/* np stored parameters, engine, preset and steps of an older track -> today's, mapped by count (see the top);
 * the steps without hits */
static void proj_trk_from(proj_trk_t *d, const int16_t *p, uint32_t np, uint8_t engine, uint8_t preset, const step8_t *step)
{
    int16_t def[P_E0];
    uint32_t k;
    for (k = 0; k < P_E0; k++)
        def[k] = TP[k].def;
    params_by_count(d->p, p, np, def);
    d->engine = engine;                         /* (indices 0..7 of formats 1 and 2 as they were) */
    d->preset = preset;
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &d->step[k];
        memcpy(s->note, step[k].note, 4);
        s->n = step[k].n;
        s->time = step[k].time;
        s->flags = step[k].flags & 3u;          /* accent, slide: x1 (no RATCH then) */
        s->vel = step[k].vel;
        s->hit = s->acc = 0;
    }
}

/* the DRUM tracks of a project of before the grid: their lanes' notes as hits (see the top) */
static void proj_grid(project_t *q)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++)
        if (q->t[k].engine == ENGI_DRUM)
            for (i = 0; i < NSTEP; i++)
                step_to_grid(&q->t[k].step[i]);
    q->sum = proj_sum(q);
}
static void proj_trk_from_v2(proj_trk_t *d, const proj_trk_v2_t *s)
{
    proj_trk_from(d, s->p, PROJ_NP_V2, s->engine, s->preset, s->step);
}

/* a project written with the GM drum part as track 4 (parts 0) -> track 4 a part (see the top);
 * project_load gives it DRUM's kit (PROJ_DEF_KEEP; the SAMPLE PERC sound until 1.0.2). Idempotent */
static void proj_drums_to_part(project_t *q)
{
    proj_trk_t *d = &q->t[PROJ_DRUMT];
    if (q->parts)
        return;
    d->engine = ENGI_DRUM;                      /* DRUM's first kit: independent of today's power-on drum sound */
    d->preset = PROJ_DEF_KEEP;
    d->p[P_LEVEL] = (int16_t)clamp(q->g[G_CMODE], 0, 127);   /* the drum part's level and reverb send (the ids */
    d->p[P_REV] = (int16_t)clamp(q->g[G_CFDBK], 0, 127);     /* G_DRLVL / G_DRREV then: core.h) */
    q->parts = PROJ_PARTS;
    q->sum = proj_sum(q);
}
#endif

/* (Skiff phase 6) tracks from .. NTRK - 1 as at power-on, for a format holding fewer: the engine's defaults, its
 * power-on sound (PROJ_DEF_SOUND: project_restore_runtime loads it), no steps, the init patch, no list */
static void proj_power_on_trk(project_t *q, uint32_t i)
{
    uint32_t k;
    for (k = 0; k < P_COUNT; k++)
        q->t[i].p[k] = param_desc_of(trk_def_engine(i), k)->def;
    q->t[i].engine = (uint8_t)trk_def_engine(i);
    q->t[i].preset = PROJ_DEF_SOUND;
#if FELUCCA_LEGACY
    for (k = 0; k < NSTEP; k++)
        q->t[i].step[k].time = ST_REST;
#endif
    memcpy(q->fm6[i], FM6_INIT, FM6_PACKED);
}
static void proj_power_on(project_t *q, uint32_t from)
{
    uint32_t i;
    for (i = from; i < NTRK; i++)
        proj_power_on_trk(q, i);
}
#if FELUCCA_LEGACY

/* PHYS tracks of a project written before PROJ_PHYS (see the top) -> today's: DUST as MODAL bowed, MODEL
 * DRUM as the DRUM engine (its E values moved, the preset its first). Idempotent */
static void proj_phys(project_t *q)
{
    uint32_t k;
    if (q->phys >= PROJ_PHYS)
        return;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        if (d->engine != ENGI_PHYS)
            continue;
        if (!q->phys)
            phys_legacy(&d->p[P_E0]);
        if (drum_from_phys(d->engine, &d->p[P_E0])) {
            d->engine = ENGI_DRUM;
            d->preset = 0;
        }
    }
    q->phys = PROJ_PHYS;
    q->sum = proj_sum(q);
}

/* DIGITAL tracks (engine 1; without FELUCCA_FM4) -> FM6 with the converted patch as the track's own (fm4_convert.c,
 * whatever format the project is: the conversion runs on every load, the stored record keeps what it holds until it
 * is saved again). Their motion events on the EDIT values or the OP ENV values go: DIGITAL's meanings do not carry
 * over to FM6's macros. Idempotent */
static void proj_fm4(project_t *q)
{
#if !FELUCCA_FM4
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        uint8_t v[FP_SIZE + 1u];
        uint32_t pr;
        if (d->engine != ENGI_DIGITAL)
            continue;
        for (i = 0; i < P_COUNT; i++)
            d->p[i] = (int16_t)clamp(d->p[i], param_desc_of(ENGI_DIGITAL, i)->min, param_desc_of(ENGI_DIGITAL, i)->max);
        pr = fm4_convert(d->p, v);
        fm6_pack(v, q->fm6[k]);
        d->engine = ENGI_FM6;
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = (uint8_t)pr;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && (MOTION_ID(e) >= P_E0 || (MOTION_ID(e) >= P_FM1_ATK && MOTION_ID(e) <= P_FM4_LEVEL)))
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
#else
    (void)q;
#endif
}

/* SAMPLE tracks of the retired PERC set (SET 4) -> DRUM with its default kit (see the top; core.h drum_from_perc,
 * whatever format the project is: on every load, the stored record keeps what it holds until it is saved again).
 * Their motion events on the EDIT values go. Idempotent */
static void proj_perc(project_t *q)
{
    uint32_t k, i, n, hit = 0;
    for (k = 0; k < NTRK; k++) {
        proj_trk_t *d = &q->t[k];
        if (!drum_from_perc(d->engine, &d->p[P_E0]))
            continue;
        d->engine = ENGI_DRUM;
        if (d->preset < PROJ_DEF_KEEP)
            d->preset = 0;
        hit |= 1u << k;
    }
    if (!hit)
        return;
    for (i = n = 0; i < q->motion.count && i < MOTION_MAX; i++) {
        const motion_event_t *e = &q->motion.event[i];
        if (((hit >> (e->place >> 6)) & 1u) && MOTION_ID(e) >= P_E0)
            continue;
        q->motion.event[n++] = *e;
    }
    for (i = n; i < q->motion.count && i < MOTION_MAX; i++)
        memset(&q->motion.event[i], 0, sizeof q->motion.event[i]);
    q->motion.count = (uint8_t)n;
    q->sum = proj_sum(q);
}

/* a format 4 project (n bytes in *v4) -> slot q as format 6 */
static int proj_from_v4(project_t *q, const project_v4_t *v4, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v4 || v4->magic != PROJ_MAGIC_V4 || v4->size != sizeof *v4 ||
        v4->sum != proj_hash(v4, sizeof *v4 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v4->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v4->sel;
    q->parts = v4->parts;
    q->phys = v4->phys;
    for (i = 0; i < PROJ_NT; i++)
        proj_trk_from(&q->t[i], v4->t[i].p, PROJ_NP_V4, v4->t[i].engine, v4->t[i].preset, v4->t[i].step);
    proj_power_on(q, PROJ_NT);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);
    return 1;
}

/* a format 3 project (n bytes in *v3) -> slot q as format 6 */
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    memcpy(q->g, v3->g, sizeof q->g);
    proj_rtype_room(q->g);
    q->sel = v3->sel;
    q->parts = v3->parts;
    for (i = 0; i < PROJ_NT; i++)
        proj_trk_from(&q->t[i], v3->t[i].p, PROJ_NP_V3, v3->t[i].engine, v3->t[i].preset, v3->t[i].step);
    proj_power_on(q, PROJ_NT);
    q->sum = proj_sum(q);
    proj_drums_to_part(q);                     /* (a format 3 of firmware before 1.0: parts 0) */
    return 1;
}

/* a format 2 project (n bytes in *v2) -> slot q as format 6 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v2->g);
    q->sel = v2->sel;
    for (i = 0; i < PROJ_NT; i++)
        proj_trk_from_v2(&q->t[i], &v2->t[i]);
    proj_power_on(q, PROJ_NT);
    proj_drums_to_part(q);                     /* (format 2 had the drum track: parts 0) */
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 6: the instrument becomes track 1,
 * the other tracks start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v1->g);
    q->parts = PROJ_PARTS;                     /* (format 1 had no track 4) */
    proj_trk_from_v2(&q->t[0], &v1->t);
    proj_power_on(q, 1u);
    q->sum = proj_sum(q);
    return 1;
}

/* n bytes of a stored project (any format) -> slot q as format 6; 0 = not a project */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t size);
/* the init patch on every track (a project of a format before FUN8) */
static void proj_fm6_init(project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
    q->sum = proj_sum(q);
}
static int proj_import_old(project_t *q, const void *b, int n);
#endif
static int proj_import_any(project_t *q, const void *b, int n);
static const proj_fmt_t *proj_fmt_of(const void *b, int n);
/* every track's engine one this firmware has (DIGITAL, 1, is one: it converts; (2.0) only with FELUCCA_LEGACY): a
 * project of another number is not one (it never was: no engine number past the last was ever stored) */
static int proj_engines_ok(const project_t *q)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (q->t[t].engine >= NENGINES || (!FELUCCA_LEGACY && !FELUCCA_FM4 && q->t[t].engine == ENGI_DIGITAL))
            return 0;
    return 1;
}
/* n bytes of a stored project (any format) -> q as today's, DIGITAL and SAMPLE PERC tracks converted; 0 = not a
 * project */
static int proj_import(project_t *q, const void *b, int n)
{
    uint32_t k;
    if (!proj_import_any(q, b, n) || !proj_engines_ok(q))
        return 0;
#if FELUCCA_LEGACY
    if (proj_fmt_of(b, n) != &PROJ_SKF3 && proj_fmt_of(b, n) != &PROJ_SKF4 &&   /* (Skiff phase 9) the CHORUS MODE */
        (q->g[G_CMODE] || q->g[G_CFDBK])) {
        q->g[G_CMODE] = q->g[G_CFDBK] = 0;          /* and FEEDBACK: in an older format the old GM drum part's level */
        q->sum = proj_sum(q);                       /* and reverb send, read above: as at power-on) */
    }
    proj_fm4(q);
    proj_perc(q);
#endif
    for (k = NHEAVY; k < NTRK; k++)                /* (Skiff phase 6) a light track holding PHYS or GRAIN (no firmware */
        if (!eng_allowed(k, q->t[k].engine)) {     /* writes one): as at power-on, its list kept */
            proj_power_on_trk(q, k);
            q->sum = proj_sum(q);
        }
    return 1;
}
static int proj_unpack2(project_t *q, const uint8_t *b, const proj_fmt_t *f);
#if FELUCCA_LEGACY
static void proj_ent_none(project_t *q);
#endif
/* (Skiff phase 6) the format of a record of n bytes if SKF2 (FELUCCA_LEGACY only), SKF3 or (2.0) SKF4 (a retained slot
 * too: its size in it); 0 not */
static const proj_fmt_t *proj_fmt_of(const void *b, int n)
{
    const uint32_t *w = (const uint32_t *)b;
    const proj_fmt_t *f = n < 8 ? 0 : w[0] == PROJ_MAGIC4 ? &PROJ_SKF4 : w[0] == PROJ_MAGIC3 ? &PROJ_SKF3 :
                          FELUCCA_LEGACY && w[0] == PROJ_MAGIC2 ? &PROJ_SKF2 : 0;
    return f && n >= (int)f->size && w[1] == f->size ? f : 0;
}
/* (2.0, D3) a record of a format of before by its magic: FUN1..FUN9, Felucca's FUNA, SKF1, SKF2. 2.0 keeps its bytes
 * (OLD FORMAT); the converter reads it (web/conv) */
static int proj_old_magic(uint32_t m) { return (m >= 0x46554E31u && m <= 0x46554E41u) || m == PROJ_MAGIC || m == PROJ_MAGIC2; }
/* .. n bytes holding one whole (its own size, at most n; its hash, as every format of before has it last): that size,
 * 0 not (editor_backup.c keeps such a slot as it is) */
static uint32_t proj_old_whole(const void *b, uint32_t n)
{
    const uint32_t *w = (const uint32_t *)b;
    uint32_t m = n >= 12u ? w[1] : 0u;
    return m >= 12u && m <= n && !(m & 3u) && proj_old_magic(w[0]) && w[m / 4u - 1u] == proj_hash(b, m - 4u) ? m : 0u;
}
static int proj_import_any(project_t *q, const void *b, int n)
{
    const proj_fmt_t *f = proj_fmt_of(b, n);
    if (f)
        return proj_unpack2(q, b, f);              /* (Skiff phase 5, 6) */
#if FELUCCA_LEGACY
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[0] == PROJ_MAGIC)
        return proj_unpack(q, b, PROJ_STORE_SIZE);
    if (n == PROJ_STORE_SIZE && ((const uint32_t *)b)[1] >= 8u && ((const uint32_t *)b)[1] < PROJ_STORE_SIZE)
        n = (int)((const uint32_t *)b)[1];      /* a retained slot holding an older, shorter record: its own size
                                                 * (every format checks its magic and hash) */
    if (n == (int)PROJ_STORE_V9 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V9)
        return proj_unpack(q, b, PROJ_STORE_V9);
    if (n == (int)PROJ_STORE_V8 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V8)
        return proj_unpack(q, b, PROJ_STORE_V8);
    if (n == (int)PROJ_STORE_V7 && ((const uint32_t *)b)[0] == PROJ_MAGIC_V7)
        return proj_unpack(q, b, PROJ_STORE_V7);
    if (n == (int)sizeof *q && proj_ok((const project_t *)b)) {
        memcpy(q, b, sizeof *q);
        proj_drums_to_part(q);
        proj_phys(q);
        return 1;
    }
    if (!proj_import_old(q, b, n))
        return 0;
    proj_fm6_init(q);
    proj_ent_none(q);
    q->sum = proj_sum(q);
    return 1;
#else
    return 0;
#endif
}
#if FELUCCA_LEGACY
static int proj_import_old(project_t *q, const void *b, int n)
{
    if (n == (int)sizeof(project_v5_t) || n == (int)sizeof(project_v6_t)) {
        const project_v5_t *v = b;
        const project_v6_t *v6 = b;
        uint32_t bytes = n, i, k;
        if (((bytes == sizeof *v && v->magic == PROJ_MAGIC_V5) ||
             (bytes == sizeof *v6 && v->magic == PROJ_MAGIC_V6 && chain_valid(&v6->chain))) &&
            v->size == bytes && ((const uint32_t *)b)[bytes / 4u - 1u] == proj_hash(b, bytes - 4u)) {
            memset(q, 0, sizeof *q);
            q->magic = PROJ_MAGIC; q->size = sizeof *q;
            memcpy(q->g, v->g, sizeof q->g);
            proj_rtype_room(q->g);
            q->sel = v->sel; q->parts = v->parts; q->phys = v->phys;
            for (i = 0; i < PROJ_NT; i++) {
                int16_t def[P_COUNT];
                if (v->t[i].engine >= NENGINES) return 0;
                for (k = 0; k < P_COUNT; k++) def[k] = param_desc_of(v->t[i].engine, k)->def;
                params_by_count(q->t[i].p, v->t[i].p, 69u, def);
                q->t[i].engine = v->t[i].engine; q->t[i].preset = v->t[i].preset;
                for (k = 0; k < NSTEP; k++) {
                    memcpy(&q->t[i].step[k], &v->t[i].step[k], sizeof(step10_t));
                    q->t[i].step[k].flags &= 3u;       /* (x1: no RATCH then) */
                }
            }
            if (bytes == sizeof *v6) q->chain = v6->chain; else chain_defaults(&q->chain);
            proj_power_on(q, PROJ_NT);
            q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
            return 1;
        }
    }
    if (proj_from_v4(q, (const project_v4_t *)b, n) || proj_from_v3(q, (const project_v3_t *)b, n) ||
        proj_from_v2(q, (const project_v2_t *)b, n) || proj_from_v1(q, (const project_v1_t *)b, n)) {
        proj_phys(q);                          /* (formats 1..3 had no PHYS track: only the byte) */
        proj_grid(q);                          /* (after it: a PHYS DRUM track is DRUM now) */
        return 1;
    }
    return 0;
}
#endif

/* 12 stored name bytes -> d (PROJ_NAME_LEN + 1): up to the first 0, upper case; a byte outside 32..126 makes it
 * no name (a damaged tail never refuses the project) */
static uint32_t proj_name_get(char *d, const uint8_t *s)   /* its length */
{
    uint32_t i;
    for (i = 0; i < PROJ_NAME_LEN && s[i]; i++) {
        if (s[i] < 32u || s[i] > 126u) { i = 0; break; }
        d[i] = (char)(s[i] >= 'a' && s[i] <= 'z' ? s[i] - 32u : s[i]);
    }
    d[i] = 0;
    return i;
}

/* a step as FUN7..FUN9 / SKF1 store it, 9 bytes: its four notes (bit 7 of each: (Skiff) a bit of the trig condition),
 * the meta byte (n, time, accent, slide; bit 7 (Skiff) FLAM), velocity, the lane hits and their accents, the chance;
 * RATCH in bit 7 of the velocity and chance bytes (see the top). What step_get9 checks, so what is stored always
 * loads: 0 for a step no format can hold. (Skiff: the pattern bank, bank.c, stores steps the same way) */
static int step_put9(uint8_t *b, const step_t *s)
{
    uint32_t r = step_ratchet(s) - 1u, j;
    if (s->n > 4u || s->time > ST_REST || (s->flags & ~(3u | SF_RATCH | SF_FLAM)) || s->probability > 101u ||
        s->cond >= TC_COUNT || s->nudge < NUDGE_MIN || s->nudge > NUDGE_MAX)
        return 0;
    for (j = 0; j < 4u; j++)
        b[j] = (uint8_t)((s->note[j] > 127u ? 127u : s->note[j]) | ((s->cond >> j) & 1u) << 7);
    b[4] = (uint8_t)(s->n | s->time << 3 | (s->flags & 3u) << 5 | ((s->flags & SF_FLAM) ? 128u : 0u));
    b[5] = (uint8_t)((s->vel > 127u ? 127u : s->vel) | (r & 1u) << 7);
    b[6] = s->hit;
    b[7] = (uint8_t)(s->hit ? s->acc & s->hit : step_ties(s));   /* ((2.0, #25) no hits: the per-note ties) */
    b[8] = (uint8_t)(s->probability | (r >> 1) << 7);
    return 1;
}
/* 9 stored bytes -> *s (its nudge untouched): 0 for bytes no firmware wrote (FLAM with RATCH x2.., n above 4, ..) */
static int step_get9(step_t *s, const uint8_t *b)
{
    uint32_t meta = b[4], j;
    memcpy(s->note, b, 4);
    s->n = meta & 7u; s->time = (meta >> 3) & 3u; s->flags = (meta >> 5) & 3u;
    s->vel = b[5] & 127u; meta |= (b[5] & 128u) << 1u;   /* (RATCH bit 0 at bit 8 of meta) */
    s->hit = b[6]; s->acc = b[7];
    s->probability = b[8] & 127u; meta |= (b[8] & 128u) << 2u;   /* (bit 1 at bit 9) */
    if (((meta & 128u) && (meta >> 8)) || s->n > 4u || s->time > ST_REST || s->probability > 101u)
        return 0;                                       /* ((Skiff) FLAM: bit 7 of meta, with RATCH x1 only) */
    s->cond = 0;                                        /* (Skiff) the trig condition: bit 7 of the note bytes */
    for (j = 0; j < 4u; j++) {
        s->cond |= (uint8_t)((s->note[j] >> 7) << j);
        s->note[j] &= 127u;
    }
    if (s->cond >= TC_COUNT || (s->acc & ~(s->hit ? s->hit : (1u << s->n) - 1u)))   /* ((2.0) no hits: ties of its */
        return 0;                                                                   /* notes; 1.4 refuses them) */
    step_set_ratchet(s, (meta >> 8) + 1u);
    if (meta & 128u)
        s->flags |= SF_FLAM;
    return 1;
}

#if FELUCCA_LEGACY
/* (Skiff phase 6) the motion block of FUN7 .. SKF1, 260 bytes: the count, the on mask, 2 reserved, then all 64
 * records as 4 bytes: place (track << 6 | step: tracks 1..4 only), the parameter id, the value (int16, little-endian).
 * Whatever motion_event_t is in RAM; written so, it is the block the raw copy of before wrote. A track past them is not
 * the format's: its on bit and records are left out (as its steps), the others' records closing up */
#define PROJ_MOT_BYTES (4u + MOTION_MAX * 4u)
static void proj_mot_put(uint8_t *b, const motion_store_t *m)
{
    uint32_t i, n = 0, out = 0;
    for (i = 0; i < m->count && i < MOTION_MAX; i++)
        out |= (m->event[i].place >> 6) >= PROJ_NT;
    memset(b, 0, PROJ_MOT_BYTES);
    b[1] = (uint8_t)(m->on & ((1u << PROJ_NT) - 1u)); b[2] = m->rsv[0]; b[3] = m->rsv[1];
    for (i = 0; i < MOTION_MAX; i++) {
        const motion_event_t *e = &m->event[i];
        uint16_t v = (uint16_t)e->value;
        if (out && (i >= m->count || (e->place >> 6) >= PROJ_NT))
            continue;
        b[4u + 4u * n] = (uint8_t)e->place;
        b[5u + 4u * n] = (uint8_t)(MOTION_ID(e) | (e->lock ? MOTION_LOCK : 0u));   /* (< 128: proj_motion_np) */
        b[6u + 4u * n] = (uint8_t)v;
        b[7u + 4u * n++] = (uint8_t)(v >> 8);
    }
    b[0] = (uint8_t)(out ? n : m->count);
}
/* .. read: 0 for a record's value outside -64..127 (motion_valid's range: no firmware wrote it; it is a byte in RAM) */
static int proj_mot_get(motion_store_t *m, const uint8_t *b)
{
    uint32_t i;
    m->count = b[0]; m->on = b[1]; m->rsv[0] = b[2]; m->rsv[1] = b[3];
    for (i = 0; i < MOTION_MAX; i++) {
        motion_event_t *e = &m->event[i];
        int32_t v = (int16_t)(b[6u + 4u * i] | (uint16_t)b[7u + 4u * i] << 8);
        e->place = b[4u + 4u * i];
        e->id = b[5u + 4u * i] & 127u;
        e->lock = b[5u + 4u * i] >> 7;
        e->value = (int8_t)v;
        if (i < m->count && (v < -64 || v > 127))
            return 0;
    }
    return 1;
}
#endif

/* (Skiff phase 9) a track's values p -> the PROJ_NP bytes a store holds (+ 64, by count: the first PROJ_NP - 8 ids,
 * then P_E0..P_E7; see PROJ_NP): 0 for a value no format holds */
static int proj_np_put(uint8_t *b, const int16_t *p)
{
    uint32_t i;
    for (i = 0; i < PROJ_NP; i++) {
        int32_t v = p[i < PROJ_NP - 8u ? i : P_E0 + i - (PROJ_NP - 8u)];
        if (v < -64 || v > 127) return 0;
        b[i] = (uint8_t)(v + 64);
    }
    return 1;
}
#if FELUCCA_LEGACY
/* .. motion m in the ids of a store of PROJ_NP (proj_motion_ids the other way): 0 for one past them (phase 9's) */
static int proj_motion_np(motion_store_t *m)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        uint32_t id = MOTION_ID(&m->event[i]);
        if (id >= P_E0)
            m->event[i].id = (uint8_t)(id - PROJ_ESHIFT);
        else if (id >= PROJ_NP - 8u)
            return 0;
    }
    return 1;
}

/* A stable serialized schema: first header retains FUN6's fields; at byte66
 * np, format flags, then four byte-param tracks and nine-byte steps; FUN8: the patches at PROJ_FM6_OFF.
 * (Skiff phase 6) The first PROJ_NT tracks: SKF1 holds no more */
static int proj_pack(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw; uint32_t pos = 68u, t, i; uint32_t magic = PROJ_MAGIC, size = PROJ_STORE_SIZE, sum;
    motion_store_t mo = q->motion;
    for (t = 0; t < PROJ_NT; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!chain_valid(&q->chain) || !motion_valid(&q->motion) || !proj_motion_np(&mo)) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &magic, 4); memcpy(b + 4, &size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[66] = PROJ_NP;
    for (t = 0; t < PROJ_NT; t++) {
        if (!proj_np_put(b + pos, q->t[t].p)) return 0;   /* ((phase 9) PROJ_NP, by count) */
        pos += PROJ_NP;
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
        for (i = 0; i < NSTEP; i++, pos += 9u)
            if (!step_put9(b + pos, &q->t[t].step[i]))
                return 0;
        nudge_pack(b + pos, q->t[t].step, NSTEP);       /* (Skiff) the NUDGE plane (core.h, see the top) */
        pos += PROJ_NUDGE_BYTES;
    }
    if (pos + sizeof q->chain + PROJ_MOT_BYTES > PROJ_FM6_OFF) return 0;
    memcpy(b + pos, &q->chain, sizeof q->chain); pos += sizeof q->chain;
    proj_mot_put(b + pos, &mo);
    memcpy(b + PROJ_FM6_OFF, q->fm6, PROJ_NT * FM6_PACKED);
    {   /* the name (0-padded; stops at the first 0) */
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_OFF, n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE_SIZE - 4u); memcpy(b + PROJ_STORE_SIZE - 4u, &sum, 4);
    return 1;
}
#endif
/* the motion of a FUN7 / FUN8 written with np parameters (np < P_COUNT: a FUN7 of 89 before the chord keys, every
 * FUN8 of 91 before the DRUM lane levels) -> today's ids: an event names a parameter id, and the ids from that
 * store's P_E0 (np - 8) on moved up with P_E0, as its values did (params_by_count); the ones below kept theirs. A
 * lock stays a lock: the id moves (a FUN8 / FUN7 of a 1.1 development build may hold
 * locks). 0: an id that store could not have named (np or more) */
static int proj_motion_ids(motion_store_t *m, uint32_t np)
{
    uint32_t i;
    for (i = 0; i < m->count && i < MOTION_MAX; i++) {
        uint32_t id = MOTION_ID(&m->event[i]);
        if (id >= np) return 0;
        if (np < P_COUNT && id >= np - 8u)
            m->event[i].id = (uint8_t)(id + P_COUNT - np);
    }
    return 1;
}
#if FELUCCA_LEGACY
/* a stored SKF1 (st = PROJ_STORE_SIZE), FUN9 (PROJ_STORE_V9: no NUDGE planes), FUN8 (PROJ_STORE_V8: FUN9, its tail
 * 64 bytes shorter) or FUN7 (PROJ_STORE_V7: no patches, the init one); PROJ_NT tracks, the rest as at power-on */
static int proj_unpack(project_t *q, const uint8_t *b, uint32_t st)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66], v7 = st == PROJ_STORE_V7;
    uint32_t name_off = st - 4u - PROJ_NAME_LEN, end = v7 ? name_off : name_off - PROJ_NT * FM6_PACKED;
    uint32_t nud = st == PROJ_STORE_SIZE ? PROJ_NUDGE_BYTES : 0u;   /* (Skiff) SKF1: the NUDGE planes */
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + st - 4u, 4);
    if (magic != (v7 ? PROJ_MAGIC_V7 : st == PROJ_STORE_V8 ? PROJ_MAGIC_V8 : st == PROJ_STORE_V9 ? PROJ_MAGIC_V9 : PROJ_MAGIC) ||
        size != st || sum != proj_hash(b, st - 4u) || np < 8u || np > P_COUNT ||
        68u + PROJ_NT * (np + 2u + NSTEP * 9u + nud) + sizeof q->chain + PROJ_MOT_BYTES > end)
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = b[63]; q->phys = b[64];
    if (v7 && (q->g[G_RTYPE] < 0 || q->g[G_RTYPE] > 1))   /* a FUN7 may still hold the old drum channel there */
        proj_rtype_room(q->g);
    for (t = 0; t < PROJ_NT; t++) {
        int16_t values[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++) { if (b[pos] > 191u) return 0; values[i] = (int16_t)b[pos++] - 64; }
        q->t[t].engine = b[pos++]; q->t[t].preset = b[pos++];
        if (q->t[t].engine >= NENGINES) return 0;
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine, i)->def;
        params_by_count(q->t[t].p, values, np, def);
        for (i = 0; i < NSTEP; i++, pos += 9u)
            if (!step_get9(&q->t[t].step[i], b + pos))
                return 0;
        if (nud) {                                      /* (Skiff) the NUDGE plane (core.h) */
            nudge_unpack(q->t[t].step, b + pos, NSTEP);
            pos += nud;
        }
    }
    memcpy(&q->chain, b + pos, sizeof q->chain); pos += sizeof q->chain;
    if (!proj_mot_get(&q->motion, b + pos) || !proj_motion_ids(&q->motion, np) || !chain_valid(&q->chain) ||
        !motion_valid(&q->motion)) return 0;
    for (t = 0; t < PROJ_NT; t++) {
        if (v7)
            memcpy(q->fm6[t], FM6_INIT, FM6_PACKED);
        else
            for (i = 0; i < FM6_PACKED; i++)
                q->fm6[t][i] = b[end + t * FM6_PACKED + i] & 0x7Fu;
    }
    proj_power_on(q, PROJ_NT);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + name_off));
    }
    proj_ent_none(q);
    q->sum = proj_sum(q); proj_drums_to_part(q); proj_phys(q);
    return 1;
}
#endif

/* (Skiff phase 5) the lists: id + 1 up to the pool (0 none; (#28) PAT_REST a REST: firmware before refuses a project
 * holding one), repeats - 1 up to 15 ((2.0) or REPS_CHAIN), each track on one of its entries, SONG 0 / 1 ((2.0) 2
 * ONCE; both refused before) */
static int proj_ent_ok(const project_t *q)
{
    uint32_t t, e;
    if (q->song > SONG_ONCE)
        return 0;
    for (t = 0; t < NTRK; t++) {
        if (q->cur[t] >= NENT)
            return 0;
        for (e = 0; e < NENT; e++)
            if (q->ent[t][e][0] > PAT_REST || (q->ent[t][e][1] > 15u && q->ent[t][e][1] != REPS_CHAIN))
                return 0;
    }
    return 1;
}
#if FELUCCA_LEGACY
static void proj_ent_none(project_t *q)          /* a project of a format before SKF2: no lists, no scenes */
{
    memset(q->ent, 0, sizeof q->ent);
    memset(q->cur, 0, sizeof q->cur);
    q->song = 0;
    memset(q->scene, 0, sizeof q->scene);
}
#endif
/* (2.0) q -> an SKF4 record (see the top; no steps or motion: the patterns hold them); 0 for a value it can't hold */
static int proj_pack4(project_store_t *out, const project_t *q)
{
    uint8_t *b = out->raw;
    uint32_t pos = PROJ_ENT4, t, e, n, sum;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < FM6_PACKED; e++)
            if (q->fm6[t][e] > 127u) return 0;
    if (!proj_ent_ok(q)) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &PROJ_SKF4.magic, 4); memcpy(b + 4, &PROJ_SKF4.size, 4);
    b[8] = PROJ_LAYOUT4;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < NENT; e++, pos += 4u) {            /* (+2, +3: the entry's sound, #30) */
            b[pos] = q->ent[t][e][0];
            b[pos + 1u] = q->ent[t][e][1];
            b[pos + 2u] = q->snd[t][e][0];
            b[pos + 3u] = q->snd[t][e][1];
        }
    memcpy(b + pos, q->cur, NTRK); pos += NTRK;
    memcpy(b + pos, q->scene, PROJ_SCENES); pos += PROJ_SCENES;
    b[pos++] = q->sel; b[pos++] = q->song;
    if (q->dx_on && dx_kit_ok(q->dx)) {
        b[pos] = PROJ_DX_MARK;
        memcpy(b + pos + 1u, q->dx, sizeof q->dx);
    }
    pos += 1u + sizeof q->dx;
    memcpy(b + pos, q->fm6, sizeof q->fm6); pos += sizeof q->fm6;
    pos += gsid_put(b + pos, q->g);
    for (t = 0; t < NTRK; t++) {
        b[pos] = q->t[t].engine; b[pos + 1u] = q->t[t].preset; b[pos + 2u] = q->t[t].user; pos += 3u;   /* (#20) */
        if (!(n = sid_put(b + pos, q->t[t].p))) return 0;
        pos += n;
    }
    {
        char nm[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_AT(&PROJ_SKF4), nm, proj_name_get(nm, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, PROJ_STORE4 - 4u); memcpy(b + PROJ_STORE4 - 4u, &sum, 4);
    return 1;
}
/* (2.0) an SKF4 record (its magic and size checked: proj_fmt_of) -> q; 0 if it is not one this firmware reads */
static int proj_unpack4(project_t *q, const uint8_t *b)
{
    uint32_t pos = PROJ_ENT4, end = PROJ_NAME_AT(&PROJ_SKF4), t, e, n, sum;
    memcpy(&sum, b + PROJ_STORE4 - 4u, 4);
    if (b[8] != PROJ_LAYOUT4 || sum != proj_hash(b, PROJ_STORE4 - 4u)) return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    q->parts = PROJ_PARTS; q->phys = PROJ_PHYS;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < NENT; e++, pos += 4u) {
            q->ent[t][e][0] = b[pos];
            q->ent[t][e][1] = b[pos + 1u];
            q->snd[t][e][0] = b[pos + 2u];             /* ((#30) as it is: one no longer there plays none, esnd_make) */
            q->snd[t][e][1] = b[pos + 3u];
        }
    memcpy(q->cur, b + pos, NTRK); pos += NTRK;
    memcpy(q->scene, b + pos, PROJ_SCENES); pos += PROJ_SCENES;
    q->sel = b[pos++]; q->song = b[pos++];
    if (!proj_ent_ok(q)) return 0;
    if (b[pos] == PROJ_DX_MARK && dx_kit_ok((const dx_lane_t *)(b + pos + 1u))) {
        memcpy(q->dx, b + pos + 1u, sizeof q->dx);
        q->dx_on = 1;
    }
    pos += 1u + sizeof q->dx;
    for (t = 0; t < NTRK; t++)
        for (e = 0; e < FM6_PACKED; e++)
            q->fm6[t][e] = b[pos++] & 0x7Fu;
    for (e = 0; e < G_COUNT; e++)
        q->g[e] = GP[e].def;
    if (!(n = gsid_get(q->g, b + pos, end - pos))) return 0;
    pos += n;
    for (t = 0; t < NTRK; t++) {
        if (end - pos < 3u) return 0;
        q->t[t].engine = b[pos]; q->t[t].preset = b[pos + 1u];
        q->t[t].user = b[pos + 2u] <= UP_SLOTS ? b[pos + 2u] : 0u;   /* ((#20) past the slots: none, the project still loads) */
        pos += 3u;
        if (q->t[t].engine >= NENGINES) return 0;
        for (e = 0; e < P_COUNT; e++)
            q->t[t].p[e] = param_desc_of(q->t[t].engine, e)->def;
        if (!(n = sid_get(q->t[t].p, b + pos, end - pos))) return 0;
        pos += n;
#if FELUCCA_LEGACY
        for (e = 0; e < NSTEP; e++)
            q->t[t].step[e].time = ST_REST;               /* (empty: the patterns hold the steps) */
#endif
    }
#if FELUCCA_LEGACY
    chain_defaults(&q->chain);
#endif
    {
        char nm[PROJ_NAME_LEN + 1u];
        memcpy(q->name, nm, proj_name_get(nm, b + end));
    }
    q->sum = proj_sum(q);
    return 1;
}
/* q -> an SKF2 or (Skiff phase 6) SKF3 record, format f (its steps and motion not: they are the patterns'): 0 for a
 * value no format can hold, or a format of more tracks than this firmware has. Its first f->nt tracks, the first f->sc
 * scene bytes: SKF2 holds four tracks, their scenes. (2.0) SKF4: proj_pack4 */
static int proj_pack2(project_store_t *out, const project_t *q, const proj_fmt_t *f)
{
    uint8_t *b = out->raw;
    uint32_t pos = 68u, t, i, sum;
    if (f == &PROJ_SKF4) return proj_pack4(out, q);
    if (f->nt > NTRK || f->sc > PROJ_SCENES) return 0;
    for (t = 0; t < f->nt; t++)
        for (i = 0; i < FM6_PACKED; i++)
            if (q->fm6[t][i] > 127u) return 0;
    if (!proj_ent_ok(q)) return 0;
    memset(out, 0, sizeof *out); memcpy(b, &f->magic, 4); memcpy(b + 4, &f->size, 4);
    memcpy(b + 8, q->g, sizeof q->g); b[62] = q->sel; b[63] = q->parts; b[64] = q->phys; b[65] = q->song;
    b[66] = PROJ_NP;
    for (t = 0; t < f->nt; t++) {
        if (!proj_np_put(b + pos, q->t[t].p)) return 0;   /* ((phase 9) PROJ_NP, by count) */
        pos += PROJ_NP;
        b[pos++] = q->t[t].engine; b[pos++] = q->t[t].preset;
    }
    memcpy(b + pos, q->ent, f->nt * NENT * 2u); pos += f->nt * NENT * 2u;
    memcpy(b + pos, q->cur, f->nt); pos += f->nt;
    memcpy(b + pos, q->scene, f->sc); pos += f->sc;
    if (pos > PROJ_HEAD_END(f)) return 0;
    if (f->dx) {                                      /* ((phase 9) the parameters past PROJ_NP: the PX block, if one */
        uint32_t any = 0, j;                          /* is not its default) */
        for (t = 0; t < f->nt; t++)
            for (j = 0; j < PROJ_NX; j++) {
                int32_t v = q->t[t].p[PROJ_NP - 8u + j];
                if (v < -64 || v > 127) return 0;
                any |= v != TP[PROJ_NP - 8u + j].def;
            }
        if (any) {
            uint8_t *x = b + PROJ_PX_AT(f);
            x[0] = PROJ_PX_MARK;
            x[1] = (uint8_t)PROJ_NX;
            for (t = 0; t < f->nt; t++)
                for (j = 0; j < PROJ_NX; j++)
                    x[2u + t * PROJ_NX + j] = (uint8_t)(q->t[t].p[PROJ_NP - 8u + j] + 64);
        }
    }
    if (f->dx && q->dx_on && dx_kit_ok(q->dx)) {     /* (phase 7) the DRUM-X kit (none: 0, the factory's) */
        b[PROJ_DX_AT(f)] = PROJ_DX_MARK;
        memcpy(b + PROJ_DX_AT(f) + 1u, q->dx, sizeof q->dx);
    }
    memcpy(b + PROJ_FM6_AT(f), q->fm6, f->nt * FM6_PACKED);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(b + PROJ_NAME_AT(f), n, proj_name_get(n, (const uint8_t *)q->name));
    }
    sum = proj_hash(b, f->size - 4u); memcpy(b + f->size - 4u, &sum, 4);
    return 1;
}
/* how many entries of the saved projects (the SKF2, SKF3 and SKF4 slots) name pattern id (bank.c pat_refs) */
static uint32_t proj_slot_refs(uint32_t id)
{
    uint32_t sl, i, n = 0, np, off, w;
    for (sl = 0; sl < 4u; sl++) {
        const uint8_t *b = proj_slot[sl].raw;
        const proj_fmt_t *f = proj_fmt_of(b, sizeof proj_slot[sl]);
        np = b[66];
        if (f == &PROJ_SKF4)
            off = PROJ_ENT4, w = 4u;
        else if (!f || np < 8u || np > P_COUNT)
            continue;
        else
            off = 68u + f->nt * (np + 2u), w = 2u;
        for (i = 0; i < f->nt * NENT; i++)
            n += b[off + w * i] == id + 1u;
    }
    return n;
}
/* an SKF2 or SKF3 record (format f) -> q (no steps, no motion: the patterns hold them); f->nt tracks, the rest as at
 * power-on (an SKF2's scenes leave T5, T6 as they are: bank.c scene_trk). (2.0) SKF4: proj_unpack4 */
static int proj_unpack2(project_t *q, const uint8_t *b, const proj_fmt_t *f)
{
    uint32_t pos = 68u, t, i, magic, size, sum, np = b[66];
    if (f == &PROJ_SKF4) return proj_unpack4(q, b);
    memcpy(&magic, b, 4); memcpy(&size, b + 4, 4); memcpy(&sum, b + f->size - 4u, 4);
    if (f->nt > NTRK || f->sc > PROJ_SCENES || magic != f->magic || size != f->size ||
        sum != proj_hash(b, f->size - 4u) || np < 8u || np > P_COUNT ||
        68u + f->nt * (np + 2u + NENT * 2u + 1u) + f->sc > PROJ_HEAD_END(f))
        return 0;
    memset(q, 0, sizeof *q); q->magic = PROJ_MAGIC; q->size = sizeof *q;
    if (f->dx && b[PROJ_DX_AT(f)] == PROJ_DX_MARK && dx_kit_ok((const dx_lane_t *)(b + PROJ_DX_AT(f) + 1u))) {
        memcpy(q->dx, b + PROJ_DX_AT(f) + 1u, sizeof q->dx);   /* (phase 7) its DRUM-X kit (bytes: dx_lane_t's are) */
        q->dx_on = 1;
    }
    memcpy(q->g, b + 8, sizeof q->g); q->sel = b[62]; q->parts = b[63]; q->phys = b[64]; q->song = b[65];
    for (t = 0; t < f->nt; t++) {
        int16_t values[P_COUNT], def[P_COUNT];
        for (i = 0; i < np; i++) { if (b[pos] > 191u) return 0; values[i] = (int16_t)b[pos++] - 64; }
        q->t[t].engine = b[pos++]; q->t[t].preset = b[pos++];
        if (q->t[t].engine >= NENGINES) return 0;
        for (i = 0; i < P_COUNT; i++) def[i] = param_desc_of(q->t[t].engine, i)->def;
        params_by_count(q->t[t].p, values, np, def);
#if FELUCCA_LEGACY
        for (i = 0; i < NSTEP; i++)
            q->t[t].step[i].time = ST_REST;          /* (empty: the patterns hold the steps) */
#endif
    }
    if (f->dx && np == PROJ_NP && b[PROJ_PX_AT(f)] == PROJ_PX_MARK) {   /* ((phase 9) the parameters past PROJ_NP: */
        const uint8_t *x = b + PROJ_PX_AT(f);         /* n of them a track, the ones this firmware knows taken) */
        uint32_t n = x[1], j;
        if (n > PROJ_PX_MAX) return 0;
        for (t = 0; t < f->nt; t++)
            for (j = 0; j < n; j++) {
                if (x[2u + t * n + j] > 191u) return 0;
                if (j < PROJ_NX)
                    q->t[t].p[PROJ_NP - 8u + j] = (int16_t)(x[2u + t * n + j] - 64);
            }
    }
    memcpy(q->ent, b + pos, f->nt * NENT * 2u); pos += f->nt * NENT * 2u;
    memcpy(q->cur, b + pos, f->nt); pos += f->nt;
    memcpy(q->scene, b + pos, f->sc);
    if (!proj_ent_ok(q)) return 0;
#if FELUCCA_LEGACY
    chain_defaults(&q->chain);
#endif
    for (t = 0; t < f->nt; t++)
        for (i = 0; i < FM6_PACKED; i++)
            q->fm6[t][i] = b[PROJ_FM6_AT(f) + t * FM6_PACKED + i] & 0x7Fu;
    proj_power_on(q, f->nt);
    {
        char n[PROJ_NAME_LEN + 1u];
        memcpy(q->name, n, proj_name_get(n, b + PROJ_NAME_AT(f)));
    }
    q->sum = proj_sum(q);
#if FELUCCA_LEGACY
    proj_drums_to_part(q); proj_phys(q);           /* ((2.0) SKF3 is always PROJ_PARTS, PROJ_PHYS: SKF2 of before) */
#endif
    return 1;
}

#ifndef PROJ_HOST
#if FELUCCA_LEGACY
/* the GM drum part of a project of before 1.0 (proj_drums_to_part): DRUM's first kit, as a preset load sets it
 * (until 1.0.2 the SAMPLE engine's PERC set, retired since) */
static void proj_legacy_drums(track_t *t)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const preset_t *pr = &ENGINES[ENGI_DRUM]->presets[0];
    uint32_t i;
    t->eng_req = ENGI_DRUM;
    t->preset = 0;
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = pr->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
}
#endif

static project_t proj_scratch;              /* decoded main-loop work, never audio ISR */
static char proj_name[PROJ_NAME_LEN + 1u]    /* the name of the music as it is now (loaded, saved, the editor's */
    __attribute__((section(".pool")));       /* runtime restore); "" = none. A save takes it unless one is given */
#define PROJ_NO_SLOT 0xFFu
static uint8_t proj_cur = PROJ_NO_SLOT;      /* the slot the music was loaded from or last saved to (a rename of it
                                              * renames the music too); PROJ_NO_SLOT none (the editor's restore) */
static union {                               /* serialized main-loop work; no retained expansion */
    project_store_t s;
    uint8_t raw[3840];                         /* (the staging of a backup object, up to a storage object: editor_backup.c) */
} proj_wire_u;
#define proj_wire (proj_wire_u.s)
static uint8_t proj_wire_gen;                /* +1 whenever proj_wire is rewritten (a backup's runtime copy lives there) */

#if FELUCCA_LEGACY
static void proj_steps(step_t *s)            /* a loaded sequence stays inside its fixed fields */
{
    uint32_t i, j;
    for (i = 0; i < NSTEP; i++) {
        if (s[i].n > 4u) s[i].n = 4;
        if (s[i].time > ST_REST) s[i].time = ST_REST;
        for (j = 0; j < 4u; j++) s[i].note[j] &= 127u;
        s[i].acc &= s[i].hit ? s[i].hit : (uint8_t)((1u << s[i].n) - 1u);   /* ((2.0) no hits: its notes' ties) */
    }
}
#endif

/* an imported older format may hold values FUN7 cannot pack: keep them inside the fields and ranges */
static void proj_bound(project_t *q)
{
    uint32_t t, i;
    for (t = 0; t < NTRK; t++) {
#if FELUCCA_LEGACY
        proj_steps(q->t[t].step);
        for (i = 0; i < NSTEP; i++) {
            step_t *s = &q->t[t].step[i];
            s->flags &= 3u | SF_RATCH | SF_FLAM;        /* ((Skiff) FLAM with x1 only, as proj_unpack reads it) */
            if (step_ratchet(s) > 1u) s->flags &= (uint8_t)~SF_FLAM;
            s->vel &= 127u;
            if (s->probability > 101u) s->probability = 0;
            if (s->cond >= TC_COUNT) s->cond = TC_NONE;  /* (Skiff) */
            if (s->nudge < NUDGE_MIN || s->nudge > NUDGE_MAX) s->nudge = 0;
        }
#endif
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = param_desc_of(q->t[t].engine % NENGINES, i);
            q->t[t].p[i] = (int16_t)param_fit(d, q->t[t].p[i]);   /* (a retired KIT: the kit it plays) */
        }
    }
    q->sum = proj_sum(q);
}

#if FELUCCA_FLASH
/* slot from flash into RAM (format 7, or format 6 / 5 / 4 / 3 / 2 / 1 converted; (2.0) SKF3, SKF4, and of a format of
 * before its magic alone: OLD FORMAT, proj_old) */
static void proj_fetch(uint32_t slot)
{
    project_store_t *q = &proj_slot[slot & 3u];
    const proj_fmt_t *f;
    int n;
    proj_wire_gen++;
    n = st_load(OBJ_PROJECT0 + (slot & 3u), proj_wire_u.raw, sizeof proj_wire_u.raw);   /* ((2.0) a longer one too) */
    f = proj_fmt_of(proj_wire_u.raw, n);           /* (phase 5, 6, 2.0) SKF2 .. SKF4 stay as they are: they name their
                                                   * patterns */
    if (!proj_import(&proj_scratch, proj_wire_u.raw, n))
        q->align = n >= 8 && proj_old_magic(proj_wire.align) ? proj_wire.align : 0u;
    else {
        proj_bound(&proj_scratch);
#if FELUCCA_LEGACY
        if (!(f ? proj_pack2(q, &proj_scratch, f) : proj_pack(q, &proj_scratch)))
#else
        if (!proj_pack2(q, &proj_scratch, f))
#endif
            memset(q->raw, 0, 4);
    }
}
#endif
/* (2.0, D3) slot holds a project of a format of before (proj_fetch): kept as it is, OLD FORMAT, converted by the
 * editor's Restore or the installer (web/conv) */
static int proj_old(uint32_t slot) { return !FELUCCA_LEGACY && proj_old_magic(proj_slot[slot & 3u].align); }

static void project_capture(project_t *p)
{
    uint32_t i;
    uint32_t f = motion_guard();
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    p->parts = PROJ_PARTS;
    p->phys = PROJ_PHYS;
#if FELUCCA_LEGACY
    chain_defaults(&p->chain);                    /* (phase 5: no project-slot song; an empty one) */
#endif
    for (i = 0; i < NTRK; i++) {
        for (uint32_t j = 0; j < P_COUNT; j++) p->t[i].p[j] = motion_base_value(&trk[i], j);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        p->t[i].user = (uint8_t)(user_of(&trk[i]) < UP_SLOTS ? user_of(&trk[i]) + 1u : 0u);   /* ((2.0, #20)) */
#if FELUCCA_LEGACY
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);   /* (SKF3, SKF4 write neither: the patterns hold them) */
#endif
        fm6_pack(fm6_patch[i], p->fm6[i]);
    }
#if FELUCCA_LEGACY
    p->motion = motion;
#endif
    motion_unguard(f);
    memcpy(p->name, proj_name, str_len(proj_name));
    memcpy(p->ent, lists.ent, sizeof p->ent);      /* (phase 5) each track's list, the entry it is on, SONG */
    memcpy(p->snd, lists.snd, sizeof p->snd);      /* ((2.0, #30) the entries' sounds) */
    memcpy(p->cur, lists.cur, sizeof p->cur);
    p->song = lists.song;
    memcpy(p->scene, lists.scene, sizeof p->scene);  /* (the scenes too: the project's) */
    if (memcmp(dx_kit, DX_KIT_DEF, sizeof dx_kit)) {   /* (phase 7) the DRUM-X kit, when not the factory's (none: */
        p->dx_on = 1;                              /* the factory's when loaded; a project of before as it was) */
        memcpy(p->dx, dx_kit, sizeof p->dx);
    }
    p->sum = proj_sum(p);
}

/* the music as it is now -> slot, named `name` (0: the current name; "" none); 0 saved, nonzero refused or failed.
 * The current name becomes the saved one */
static void project_clean(void);
static int project_save_as(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    int kr;
    if (transport_busy()) {                            /* a flash erase silences the audio and stalls the */
        ui_message("STOP TO SAVE");                     /* sequencer (storage_hw.c): only while stopped */
        return 1;
    }
    if ((kr = keep_all()) != 0) {                      /* (phase 5) the tracks' patterns first: a project names them */
        ui_way(kr == 1 ? "PATTERNS FULL" : "SAVE ERROR", kr == 1 ? WAY_PATTERNS : "");
        return kr;
    }
    project_capture(p);
    if (name) {
        memset(p->name, 0, sizeof p->name);
        memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    }
    proj_wire_gen++;
    if (!proj_pack2(&proj_wire, p, PROJ_SKF)) { ui_message("SAVE FORMAT ERROR"); return 2; }

#if FELUCCA_FLASH
    if (flash_ok) {
        if (st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, PROJ_SKF->size)) {
            ui_message("SAVE ERROR");
            return 2;
        }
        memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
        proj_name_get(proj_name, (const uint8_t *)p->name);
        proj_cur = (uint8_t)(slot & 3u);
        project_clean();
        ui_message("SAVED");
        return 0;
    }
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = (uint8_t)(slot & 3u);
    project_clean();
    ui_message("SAVED UNTIL POWER OFF");
    return 0;
}
static int project_save(uint32_t slot) { return project_save_as(slot, 0); }
static void project_cur_name(char *b) { str_cpy(b, proj_name, PROJ_NAME_LEN + 1u); }   /* b: 13 bytes */

/* slot's name -> b (PROJ_NAME_LEN + 1 bytes); 0 = an empty slot (b ""); ((2.0) one of a format of before: OLD FORMAT).
 * Uses proj_scratch */
static int project_name(uint32_t slot, char *b)
{
    b[0] = 0;
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) {
        if (!proj_old(slot))
            return 0;
        str_cpy(b, "OLD FORMAT", PROJ_NAME_LEN + 1u);
        return 1;
    }
    proj_name_get(b, (const uint8_t *)proj_scratch.name);
    return 1;
}

/* a stored project renamed in place (nothing else of it changes; the music playing is not touched, but the slot
 * it was loaded from / saved to: its name is the new one, the next SAVE's prefill): 0 done, 1 refused (playing,
 * an empty slot), 2 failed (the slot as it was) */
static int project_rename(uint32_t slot, const char *name)
{
    project_t *p = &proj_scratch;
    const proj_fmt_t *f = proj_fmt_of(&proj_slot[slot & 3u], sizeof(project_store_t));   /* (phase 5, 6) its format
                                                                                * kept: SKF2 .. SKF4 name their patterns */
    if (transport_busy()) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    if (!proj_import(p, &proj_slot[slot & 3u], sizeof(project_store_t))) {
        ui_message(proj_old(slot) ? "OLD FORMAT" : "EMPTY SLOT");
        return 1;
    }
    memset(p->name, 0, sizeof p->name);
    memcpy(p->name, name, str_len(name) < PROJ_NAME_LEN ? str_len(name) : PROJ_NAME_LEN);
    proj_wire_gen++;
#if FELUCCA_LEGACY
    if (!(f ? proj_pack2(&proj_wire, p, f) : proj_pack(&proj_wire, p))) { ui_message("SAVE FORMAT ERROR"); return 2; }
#else
    if (!proj_pack2(&proj_wire, p, f)) { ui_message("SAVE FORMAT ERROR"); return 2; }
#endif
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + (slot & 3u), &proj_wire, f ? f->size : sizeof proj_wire)) {
        ui_message("SAVE ERROR");
        return 2;
    }
#endif
    memcpy(&proj_slot[slot & 3u], &proj_wire, sizeof proj_wire);
    if (proj_cur == (slot & 3u)) {                      /* (the music's name too: as it was, saved or not) */
        int was = !project_changed();
        proj_name_get(proj_name, (const uint8_t *)p->name);
        if (was)
            project_clean();
    }
#if FELUCCA_FLASH
    if (flash_ok) { ui_message("RENAMED"); return 0; }
#endif
    ui_message("RENAMED UNTIL POWER OFF");
    return 0;
}

static int project_restore_runtime(const project_t *input)
{
    project_t *p = &proj_scratch;
    uint32_t i, k;
    if (!proj_ok(input) || !proj_engines_ok(input)) return 1;
    if (p != input) memcpy(p, input, sizeof *p);
#if FELUCCA_LEGACY
    proj_drums_to_part(p);                              /* a RAM slot of firmware before 1.0 */
    proj_phys(p);                                       /* .. before PHYS lost DUST and DRUM */
    proj_fm4(p);                                        /* .. that had DIGITAL tracks */
    proj_perc(p);                                       /* .. or SAMPLE PERC tracks */
#endif
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    seq_stop();
    launch_drop();                                      /* (Skiff) the bank's staged patterns: not over it (launch.c) */
    transport_req = 0;
#if FELUCCA_LEGACY
    motion = p->motion;
#else
    memset(&motion, 0, sizeof motion);                  /* ((2.0) the patterns hold it: each track's, as it loads) */
#endif
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    memcpy(lists.ent, p->ent, sizeof lists.ent);       /* (phase 5) the lists; the tracks on no pattern yet */
    memcpy(lists.snd, p->snd, sizeof lists.snd);       /* ((2.0, #30) the entries' sounds) */
    memcpy(lists.cur, p->cur, sizeof lists.cur);
    lists.song = p->song;
    memcpy(lists.scene, p->scene, sizeof lists.scene);
    scene_forget();
    memset(trk_pid, 0, sizeof trk_pid);
    motion_lost = 0;
    for (i = 0; i < G_COUNT; i++)                       /* ((Skiff U3, R8) not the unit's CLOCK MIDI IN TUNE: ui.c) */
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE && (unit_g_adopt || !unit_g_id(i)))
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = s->engine;                         /* (< NENGINES: proj_engines_ok) */
        t->eng_req = (uint8_t)e;
        t->user = s->user && up_used(s->user - 1u) && up_engine(s->user - 1u) == e ? s->user : 0u;   /* ((2.0, #20)
                                                         * the user sound it came from, while that slot still holds one
                                                         * of its engine: HOME and EDIT name it; else none, as before) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range (param_fit) */
            const param_desc_t *d = param_desc_of(e, i);
            t->p[i] = (int16_t)param_fit(d, s->p[i]);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset >= PROJ_DEF_KEEP ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
#if FELUCCA_LEGACY
        memcpy(t->step, s->step, sizeof t->step);
        proj_steps(t->step);
#else
        memset(t->step, 0, sizeof t->step);             /* ((2.0) none until its pattern loads, below) */
        for (i = 0; i < NSTEP; i++)
            t->step[i].time = ST_REST;
#endif
        {   /* the project's own FM6 patch, never reloaded from SLOT: F n if it is that factory patch, else OWN */
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            fm6_set_patch(k, v);
            fm6_adopt(k);
        }
    }
    sel_set((uint8_t)(p->sel < NTRK ? p->sel : 0u));
    memcpy(dx_kit, p->dx_on && dx_kit_ok(p->dx) ? p->dx : DX_KIT_DEF, sizeof dx_kit);   /* (phase 7) its DRUM-X kit */
    fm1_irq_on();
    proj_name_get(proj_name, (const uint8_t *)p->name);
    proj_cur = PROJ_NO_SLOT;                            /* (project_load: its slot) */
    undo_clear();                                       /* (ui.c) the undo copies belong to the old project */
    undo_depth++;                                       /* and these loads take none */
    for (k = 0; k < NTRK; k++) {                        /* the power-on sounds: format 1 (tracks 2..4), old drums */
        track_t *t = &trk[k];
        if (p->t[k].preset == PROJ_DEF_SOUND) {
            apply_preset_to(t, TRK_DEF[k][1]);
            track_defaults_steps(t);
        }
#if FELUCCA_LEGACY
        else if (p->t[k].preset == PROJ_DEF_KEEP) {     /* the steps, LEVEL PAN MUTE, LEN DIV SWING GATE kept */
            int16_t keep[P_COUNT];
            memcpy(keep, t->p, sizeof keep);
            fm1_irq_off();                           /* publish the legacy sound as one bounded parameter batch */
            proj_legacy_drums(t);                     /* (keeps the SLICER: param_kept) */
            t->p[P_REV] = keep[P_REV];                  /* and the drums' reverb send */
            for (i = P_AMODE; i <= P_TRANS; i++)        /* the drum part had no arp or scale */
                t->p[i] = TP[i].def;
            fm1_irq_on();
        }
#endif
        {   /* (phase 5) the pattern of the entry it is on (SKF2); none: the record's steps (a format before) */
            uint32_t id = ent_pat(k, lists.cur[k]);
            if (id != PAT_NONE && pool_used(id))
                (void)pat_load(t, id);
        }
    }
    undo_depth--;
    sync_reload = 1;
    ui.force = 1;
    ui_message("LOADED");
    memset(snd_said, 0, sizeof snd_said);               /* a missing sample is said again, after LOADED */
    return 0;
}
static void project_load(uint32_t slot)
{
    mom_back(0);                                  /* ((1.3) MOMENTARY: the values as they were, then the load) */
    memset(drum_mute, 0, sizeof drum_mute);       /* ((1.3) the DRUM lanes' mutes, performance state) */
#if FELUCCA_FLASH
    if (flash_ok && !proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) proj_fetch(slot);
#endif
    if (!proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t))) {
        ui_message(proj_old(slot) ? "OLD FORMAT" : "EMPTY SLOT");   /* ((2.0) converted by the editor's Restore) */
        return;
    }
    proj_bound(&proj_scratch);                          /* a retained RAM slot of an older format: as from flash */
    transport_stop_now();                               /* (phase 5) stopped first (the ISR swaps nothing more, the */
    (void)stage_cancel();                               /* steps are still while kept), nothing staged comes in; */
                                                        /* what the tracks have kept (a swap made: what went out) */
    if (keep_all() == 1) {
        ui_way("PATTERNS FULL", WAY_PATTERNS);
        return;
    }
    if (!project_restore_runtime(&proj_scratch)) {
        proj_cur = (uint8_t)(slot & 3u);
        project_clean();
    }
}

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
#include "settings_persist.c"
#if FELUCCA_FLASH && FELUCCA_SLICE
#include "slice_store.c"                          /* SLICE's MAN slices, kept in the user slots */
#endif
#if FELUCCA_FLASH
static sks_t persist_saved;                     /* ((2.0) SKS1: what the flash holds, or would after a PER4 read) */
static uint8_t persist_pending;                 /* 1 requested, 2 waiting after a flash error */
static uint32_t persist_retry_ms;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    union { persist_t per; sks_t sks; } p;         /* ((2.0) SKS1, or a PER4 of before) */
    uint32_t f = irq_save();
#if BOARD_SMK37
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());      /* the SMK-37's part (or the FM-1's): fm1_flash_map.h */
#else
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
#endif
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 (SMK 0x9D000) reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
#if BOARD_SMK37
    st_owner_check();                              /* never parse another firmware's store (storage.c) */
#endif
#if FELUCCA_SLICE
    slc_store_boot();                              /* (the scans read each slot's stored slices) */
#endif
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (settings_import(&p, n)) {
            if (p.sks.magic != SKS_MAGIC)              /* (a PER4: as SKS1, written at the next change) */
                memset(&p.sks, 0, sizeof p.sks);
            persist_saved = p.sks;
            settings_export(&persist_saved);
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_import(&proj_scratch, &proj_slot[i], sizeof(project_store_t)))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

/* a project there, ((2.0) or one of a format of before: OLD FORMAT, kept until saved over or deleted) */
static int project_used(uint32_t slot)
{
    return proj_import(&proj_scratch, &proj_slot[slot & 3u], sizeof(project_store_t)) || proj_old(slot);
}

static void settings_poll(void)
{
#if FELUCCA_FLASH
    sks_t p;
#if FELUCCA_SLICE
    slc_store_poll();                              /* SLICE's slices edited on the SLICES page */
#endif
    if (!persist_pending || !flash_ok || transport_busy() ||
        (persist_pending == 2u && (uint32_t)(fm1_ms - persist_retry_ms) < 1000u))
        return;
    p = persist_saved;
    settings_export(&p);
    if (!memcmp(&p, &persist_saved, sizeof p)) {
        persist_pending = 0;
        return;                                    /* unchanged: no erase cycle */
    }
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0) {
        persist_saved = p;
        persist_pending = 0;
    } else {
        persist_pending = 2;
        persist_retry_ms = fm1_ms;
    }
#endif
}

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_pending = 1;
#endif
    settings_poll();
}

/* The autosave (1.2, Discussion #130): the music as it is (what a project holds: every track's sound, steps, LEN DIV
 * SWING GATE, mix, the song, the automation and locks, the FM6 patches, the name) kept in its own storage object
 * (OBJ_AUTOSAVE, A/B at 0xE5000 / 0xE6000, never one of the four project slots), and put back at power-on
 * (autosave_boot) with MENU > SYSTEM > RESTORE LAST ON (the default). Flash wear: a 4 KiB erase per write, the two
 * sectors taking turns, so a write only when
 *   - something a project holds changed since the last one (autosave_sig: a hash of those values, no copy; not the
 *     selected track, nor the PROJECT page's own controls),
 *   - and it has stayed the same for AS_IDLE_MS (2 s) with the transport stopped, no key or button down, no voice
 *     sounding and no editor transfer (a backup or a sample: autosave_hold); an erase stops the audio for its
 *     ~40 ms, as every save does, so it never happens while anything plays,
 *   - and at least AS_GAP_MS (20 s) after the one before (AS_RETRY_MS after a failed write);
 *   - (2.0) after a save, a load or NEW (autosave_soon): idle AS_SOON_MS (1 s), no gap.
 * So at most three writes a minute while editing, none while playing or idle: about 100,000 erase cycles per sector
 * give over 200,000 writes, three a minute for 8 hours a day for over four months, far more with real pauses. ((2.0,
 * the user's pick on the unit, 2026-10-24: a DRUM-X switch and a pan, the unit off two seconds on, both lost; 1.2 had
 * 10 s and a minute: a minute's edits lost, a year's wear at the worst).
 * RESTORE LAST OFF writes none (and restores none). Never after a crash, a hang or a failed boot (main.c): the music
 * that was playing then could be what crashed. Checked every AS_POLL_MS; the main loop only (never the audio ISR).
 * (Skiff phase 5) The steps are the patterns' (bank.c): each write is a round, the patterns the tracks changed kept
 * first (a chunk of them a poll, each an erase in the pool's rotating sectors), then the project; RESTORE LAST OFF
 * keeps the patterns all the same. Patterns waiting with no slot free for another: the round does not wait its gap */
#define AS_POLL_MS 250u
#define AS_IDLE_MS 2000u
#define AS_GAP_MS 20000u
#define AS_RETRY_MS 30000u
#define AS_SOON_MS 1000u
static struct {
    uint32_t saved, seen;                        /* the signature written last (or loaded at power-on), seen last */
    uint32_t t, last, poll;                      /* fm1_ms: seen changed / busy, the last round, the last check */
    uint32_t writes;                             /* (written since power-on: the host tests count them) */
    uint8_t wrote, err;                          /* a round since power-on (last is valid); the last one failed */
    uint8_t round;                               /* (phase 5) a round going on: the patterns, then the project */
    uint8_t soon;                                /* (2.0) a save asked for one: idle AS_SOON_MS, no gap */
} as __attribute__((section(".pool")));
/* (2.0, the unit 2026-10-24: a user sound saved on T1, the unit switched off at once, T1 came back ACID) a save, a load
 * or NEW (SAVE > USER, SAVE > PROJECT) brings the autosave up to date within a second: the music as it is then comes
 * back at power-on. The user's own act, a flash write already: no wear to speak of */
static void autosave_soon(void) { as.soon = 1; }

static uint32_t as_mix(uint32_t h, const void *p, uint32_t n)   /* FNV-1a, continued */
{
    const uint8_t *b = p;
    while (n--) {
        h ^= *b++;
        h *= 16777619u;
    }
    return h;
}

/* what an autosave would hold, hashed (no copy, no RAM): project_capture's values */
static uint32_t autosave_sig(void)
{
    uint32_t h = 2166136261u, i, j;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        for (j = 0; j < P_COUNT; j++) {
            int16_t v = motion_base_value(t, j);
            h = as_mix(h, &v, sizeof v);
        }
        h = as_mix(h, &t->eng_req, 1);
        h = as_mix(h, &t->preset, 1);
        h = as_mix(h, &t->user, 1);                   /* ((2.0, #20) the user sound it is on: SKF4 holds it) */
        h = as_mix(h, t->step, sizeof t->step);
        h = as_mix(h, fm6_patch[i], sizeof fm6_patch[i]);
    }
    h = as_mix(h, dx_kit, sizeof dx_kit);             /* (phase 7) the DRUM-X kit */
    for (i = 0; i < G_COUNT; i++)                       /* ((Skiff U3) not the unit's CLOCK MIDI IN TUNE, ui.c) */
        if (i != G_SLOT && i != G_NAME && i != G_LOAD && i != G_SAVE && !unit_g_id(i))
            h = as_mix(h, &song.g[i], sizeof song.g[i]);
    h = as_mix(h, &motion, sizeof motion);
    h = as_mix(h, &lists, sizeof lists);              /* (phase 5) the lists, the patterns the tracks are on */
    h = as_mix(h, trk_pid, sizeof trk_pid);
    i = (ui_prefs & PREF_RESTORE_OFF) != 0;           /* (RESTORE LAST back ON: a round, the project written) */
    h = as_mix(h, &i, sizeof i);
    return as_mix(h, proj_name, sizeof proj_name);
}

/* (Skiff U3, the review's A2) the project the music is: the slot it was saved to or loaded from (proj_cur), and whether
 * it changed since (autosave_sig against its value then, looked at every 250 ms): the header's letter and dot
 * (ui_blocks.c), and the LOAD dialog asks only then (ui_input.c). Kept over a power cycle with the autosave in settings
 * bytes no engine uses ((2.0) SKS1's restore_slot, restore_clean): [15][0] the slot + 1 (0 none), [15][1..4] the value.
 * Unknown (none kept, another slot's music restored): changed, so nothing is lost without a question */
static uint32_t pj_clean __attribute__((section(".pool")));
static uint32_t pj_at __attribute__((section(".pool")));    /* fm1_ms | 1 of the last look; 0 none */
static uint8_t pj_dirty __attribute__((section(".pool")));
static void project_clean(void)                     /* a save or a load: the music is proj_cur's */
{
    pj_clean = autosave_sig();
    pj_dirty = 0;
    pj_at = fm1_ms | 1u;
    favorites.factory[15][0] = (uint8_t)(proj_cur < 4u ? proj_cur + 1u : 0u);
    memcpy(&favorites.factory[15][1], &pj_clean, 4);
    settings_save();
    autosave_soon();
}
static int project_dirty(void)
{
    if (!pj_at || fm1_ms - pj_at >= 250u) {
        pj_at = fm1_ms | 1u;
        pj_dirty = autosave_sig() != pj_clean;
    }
    return pj_dirty;
}
static int project_changed(void)                    /* (now, not as last looked: a dialog's question) */
{
    pj_at = 0;
    return project_dirty();
}
static uint32_t project_mark(void)
{
    return proj_cur < 4u ? ('A' + proj_cur) | (uint32_t)project_dirty() << 8 : 0u;
}
/* power-on, the autosave back (autosave_boot): the slot it was and its value then; nothing restored: the music as it is
 * now (nothing there to lose) */
static void project_mark_boot(int restored)
{
    uint32_t k = favorites.factory[15][0];
    pj_at = 0;
    if (!restored) {
        pj_clean = autosave_sig();
        return;
    }
    pj_clean = 0;
    if (k >= 1u && k <= 4u && project_used(k - 1u)) {
        proj_cur = (uint8_t)(k - 1u);
        memcpy(&pj_clean, &favorites.factory[15][1], 4);
    }
}

/* (1.2.1, #6) SAVE > PROJECT's DELETE: the slot emptied, in flash (an empty record: st_load reads 0) and in its RAM copy
 * (.noinit, kept over a warm reset; persist_boot and project_load read the flash only when it is empty). The music
 * playing stays; if it was this slot's it is on no slot now, and unknown counts as changed (project_dirty: nothing is
 * lost without a question). Patterns only it named stay in the pool, unused (PATTERNS can clear them).
 * 0 done, 1 refused (playing, an empty slot), 2 failed (the slot as it was) */
static int project_erase(uint32_t slot)
{
    slot &= 3u;
    if (transport_busy()) {
        ui_message("STOP TO DELETE");
        return 1;
    }
    if (!project_used(slot)) {
        ui_message("EMPTY SLOT");
        return 1;
    }
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_PROJECT0 + slot, &proj_wire, 0)) {
        ui_message("DELETE ERROR");
        return 2;
    }
#endif
    memset(&proj_slot[slot], 0, sizeof proj_slot[slot]);
    if (proj_cur == slot) {
        proj_cur = PROJ_NO_SLOT;
        pj_clean = autosave_sig() + 1u;
        pj_at = 0;
    }
#if FELUCCA_FLASH
    if (flash_ok) { ui_message("PROJECT DELETED"); return 0; }
#endif
    ui_message("DELETED UNTIL POWER OFF");
    return 0;
}

/* (1.2.1, #6) SAVE > PROJECT's NEW (KNOB 3 on an empty slot): the power-on project (felucca_init: the tracks' default
 * sounds, no steps, no lists, no motion, no scenes, the default DRUM-X kit), the way a load puts one in (stopped, the
 * tracks' patterns kept first, the ISR kept out while the sequencer's state changes). The unit's CLOCK MIDI IN TUNE and
 * SAVE's slot stay; unnamed and on no slot until SAVE writes one, with nothing to lose (clean) */
static void project_new(void)
{
    uint32_t i, k;
    mom_back(0);                                  /* ((1.3) MOMENTARY: as project_load) */
    memset(drum_mute, 0, sizeof drum_mute);
    transport_stop_now();
    (void)stage_cancel();
    if (keep_all() == 1) {
        ui_way("PATTERNS FULL", WAY_PATTERNS);
        return;
    }
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();
    seq_stop();
    launch_drop();
    transport_req = 0;
    memset(&motion, 0, sizeof motion);
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = 0;
    motion_lost = 0;
    lists_clear();
    scene_forget();
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE && !unit_g_id(i))
            song.g[i] = GP[i].def;
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    sel_set(0);
    fm1_irq_on();
    undo_clear();
    undo_depth++;                                       /* (no undo copies of these loads) */
    fm6_init();
    for (k = 0; k < NTRK; k++) {                        /* (as felucca_init; the engine switch is the ISR's, faded) */
        track_t *t = &trk[k];
        track_defaults(t);
        t->user = 0;
        set_engine_of(t, TRK_DEF[k][0]);
        apply_preset_to(t, TRK_DEF[k][1]);
        track_defaults_steps(t);
    }
    undo_depth--;
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    project_clean();
    sync_reload = 1;
    ui.force = 1;
    ui_message("NEW PROJECT");
    memset(snd_said, 0, sizeof snd_said);
}

/* nothing going on that an erase (the audio stopped ~40 ms) would be heard in, or would race */
static int autosave_quiet(void)
{
    uint32_t k, i;
    if (transport_busy() || fm1_in.notes || fm1_in.buttons)
        return 0;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < NVOICE; i++)
            if (trk[k].v[i].active)
                return 0;
    return 1;
}

static void autosave_hold(void) { as.t = fm1_ms; }   /* the editor's transfers: their staging RAM is proj_wire */

/* main loop, every pass (after settings_poll) */
static void autosave_poll(void)
{
#if FELUCCA_FLASH
    uint32_t sig, w;
    int rc;
    if (!flash_ok || fm1_ms - as.poll < AS_POLL_MS)
        return;
    as.poll = fm1_ms;
    sig = autosave_sig();
    if (sig != as.seen || !autosave_quiet()) {
        as.seen = sig;
        as.t = fm1_ms;
        return;
    }
    if (fm1_ms - as.t < (as.soon ? AS_SOON_MS : AS_IDLE_MS))
        return;
    if (!as.round) {                                   /* (phase 5) a round: the patterns kept, one erase a poll; */
        w = keep_waiting();                            /* then the project, RESTORE LAST ON */
        if (sig == as.saved && !w) {
            as.soon = 0;                               /* ((2.0) nothing new for it: the next change waits its 2 s) */
            return;
        }
        if (w < 2u && as.wrote && !as.soon && fm1_ms - as.last < (as.err ? AS_RETRY_MS : AS_GAP_MS))
            return;                                    /* (no slot free to wait in: not the gap) */
        as.round = 1;
        as.soon = 0;
        as.last = fm1_ms;
        as.wrote = 1;
    }
    rc = keep_poll();
    sig = as.seen = autosave_sig();                    /* (the round's own change, a new pattern named: no edit) */
    if (rc == 1)
        return;
    as.round = 0;
    as.err = rc != 0;
    if (as.err)
        return;
    if (!(ui_prefs & PREF_RESTORE_OFF)) {
        project_capture(&proj_scratch);
        proj_wire_gen++;
        as.err = !proj_pack2(&proj_wire, &proj_scratch, PROJ_SKF) || st_save(OBJ_AUTOSAVE, &proj_wire, PROJ_SKF->size) != 0;
        if (as.err)
            return;
        as.writes++;
    }
    as.saved = sig;
#endif
}

/* power-on, after the defaults (main.c): the autosave becomes the music when `allowed` (no crash, hang or failed boot
 * before) and RESTORE LAST is ON; an autosave that does not load (none, torn, damaged, another format) is ignored.
 * The music now (restored or not) is what is saved: nothing is written until it changes. 1 = restored */
static int autosave_boot(int allowed)
{
    int rc = 0;
#if FELUCCA_FLASH
    if (flash_ok && allowed && !(ui_prefs & PREF_RESTORE_OFF)) {
        int n;
        proj_wire_gen++;
        n = st_load(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire);
        if (n > 0 && proj_import(&proj_scratch, &proj_wire, n)) {
            proj_bound(&proj_scratch);
            if (!project_restore_runtime(&proj_scratch)) {
                ui_message("RESTORED");
                rc = 1;
            }
        }
    }
    if (flash_ok && !allowed && !(ui_prefs & PREF_RESTORE_OFF))   /* ((Skiff U4, F5) a crash, a hang or a failed boot
                                                         * before: skipped on purpose, and said) */
        ui_way("SESSION NOT RESTORED", "IT STOPPED BEFORE");
#else
    (void)allowed;
#endif
    as.saved = as.seen = autosave_sig();
    as.t = fm1_ms;
    project_mark_boot(rc);                              /* ((Skiff U3, A2) the project it is, changed or not) */
    return rc;
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_store_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
_Static_assert(sizeof(persist_t) <= ST_PAYLOAD_MAX && sizeof(sks_t) <= ST_PAYLOAD_MAX, "settings do not fit one flash sector");
#endif
#endif /* PROJ_HOST */
