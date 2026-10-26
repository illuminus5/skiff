/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA core types: tracks, voices, engines, parameters.
 * Six tracks (T5, T6 light: engines.c eng_allowed), each a synth part: its own engine, preset, parameters, voices and 64-step
 * pattern. The parts share one budget of NVOICE sounding voices (voice.c). Drums are the DRUM
 * engine (General MIDI map) on any part; the SAMPLE engine's PERC set is retired (drum_from_perc).
 * Sections: sizes, parameters, voices and engines, tracks and the song, system. */
#include <stdint.h>

/* ------------------------------------------------------------ sizes --- */
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NTRK 6                   /* tracks (the formats and the protocol count these): every track is a part;
                                  * (Skiff phase 6) T5 and T6 light (NHEAVY) */
#define NPART NTRK               /* synth parts: tracks 1..NTRK */
#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1           /* (Skiff 1.3) the second core (src/felucca.c): 1 = built in, MENU > CPU 2 CORES lets it
                                  * render some tracks; 0 = none of it */
#endif
#define C1_NP (FELUCCA_CPU2 ? NPART : 1)   /* the copies of the state a track's render writes that tracks shared before the
                                            * second core (mod.c, eng_formant.c, eng_phys.c): one each with two cores,
                                            * one for all with one, as up to 1.2 */
#define C1_PI(t) ((uint32_t)((t) - trk) % C1_NP)   /* a track's copy */
#if FELUCCA_CPU2 && !defined(C1_RUN)              /* (no second core here: the host's tests, the browser; felucca.c and
                                                  * tests/c1_host.h define their own before this) */
#define C1_ON() 0                                 /* a second core is answering */
#define C1_RUN(fn, arg) 0                         /* hand it fn(arg): 1 = taken */
#define C1_WAIT() 0                               /* wait for it: 0 = done */
#endif
/* (Skiff 1.3) the voices that sound at once across the tracks (voice.c voice_room, voice_alloc): NVOICE with one core;
 * VOICES_2CORE while the second core runs (C1_ON: MENU > CPU 2 CORES and it answered), the room its render gives. A
 * track still has NVOICE of its own (track_t.v); audio.c's shedding still guards the load either way */
#ifndef VOICES_2CORE
#define VOICES_2CORE 12u
#endif
#if FELUCCA_CPU2
#define VOICE_BUDGET (C1_ON() ? VOICES_2CORE : NVOICE)
#else
#define VOICE_BUDGET NVOICE
#endif
_Static_assert(NTRK <= 8 && NPART < NVOICE, "a track mask is a uint8_t; each part protects one voice (voice.c)");
#define NSTEP 64
#define HALF_FRAMES 128          /* I2S half buffer: 2.9 ms at 44.1 kHz (a key waits 0..1 half, then plays 1 half later) */
#ifndef AUDIO_RAM
#define AUDIO_RAM                /* (Skiff) audio code run from RAM on the unit (felucca.c); host builds: nothing */
#define AUDIO_FAR(fn) fn         /* .. a call between it and XIP code: on the unit through a pointer; here direct */
#endif
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 1          /* the SLICE engine (eng_slice.c), engine 13; FELUCCA_SLICE=0 builds without it */
#endif
#ifndef FELUCCA_FM4
#define FELUCCA_FM4 0            /* the DIGITAL engine (eng_digital.c, four-operator FM): kept in the tree, not built
                                  * by default; replaced by FM6, its sounds convert (fm4_convert.c) */
#endif
#ifndef FELUCCA_LEGACY
#define FELUCCA_LEGACY 0         /* (2.0, Tier B: design/TIER_B.md 10) the formats of before and their conversions: projects
                                  * FUN1..FUN9, SKF1, SKF2 (project.c, bank.c proj_migrate), user presets 1..3 and DIGITAL
                                  * sounds (fm4_convert.c), settings PER1..PER3, 1.0's FM6 bank. Only the converter
                                  * (web/conv) and its tests build them; 2.0 keeps such a record's bytes (OLD FORMAT) */
#endif
#ifndef FELUCCA_TAPE
#define FELUCCA_TAPE 0
#endif
#define NENGINES 15u             /* (Skiff) DRUM-X (14) after SLICE (13): the other engines keep their numbers */
#define ENGI_DIGITAL 1u          /* reserved without FELUCCA_FM4: never selectable (eng_ok), its sounds load as FM6 */
#define ENGI_SLICE 13u           /* reserved without FELUCCA_SLICE, as DIGITAL is without FELUCCA_FM4 */
#define NENG_SHOWN (NENGINES - !FELUCCA_FM4 - !FELUCCA_SLICE)   /* the engines one can pick: PRESETS, the EDIT layer,
                                                * the editor, in the display order of engines.c ENGINE_ORDER */
#define UP_SLOTS 32u             /* user presets (upreset.c) */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS, F_ST16   /* (Skiff U1) F_ST16: 0..127, 64 = 0, 3/16 semitone a step (DRUM's TUNE) */
};

/* (Skiff 2.0) 16 bytes, its fields in this order (the initializers' too): a unit is one of the few a descriptor names
 * (PD_UNIT_OF; every other unit comes from the format, params.c param_format) */
typedef struct {
    const char *label;
    const char *const *names;   /* F_ENUM */
    int16_t min, max, def;
    uint8_t fmt;
    uint8_t unit;               /* F_INT / F_ENUM optional unit: U_CT, U_ST, U_HZ, 0 none */
} param_desc_t;
enum { U_NONE, U_CT, U_ST, U_HZ };
#define PD_UNIT_OF(d) ((d)->unit == U_CT ? "ct" : (d)->unit == U_ST ? "st" : (d)->unit == U_HZ ? "Hz" : (const char *)0)

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    P_M1SRC, P_M1DST, P_M1AMT,                 /* modulation matrix (mod.c): 4 slots of SRC, DST, AMT; */
    P_M2SRC, P_M2DST, P_M2AMT,                 /* new common parameters go just before P_E0 (user presets */
    P_M3SRC, P_M3DST, P_M3AMT,                 /* and projects map by count) */
    P_M4SRC, P_M4DST, P_M4AMT,
    P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL, P_FM1_LEVEL,
    P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL, P_FM2_LEVEL,
    P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL, P_FM3_LEVEL,
    P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL, P_FM4_LEVEL,
    P_CHRD, P_VOIC,                            /* chord keys (chord.c): one key plays a chord; its voicing */
    P_LN0, P_LN1, P_LN2, P_LN3, P_LN4, P_LN5, P_LN6, P_LN7,   /* DRUM lane levels (eng_drum.c, #97): KICK .. BELL */
    P_DIR,                                     /* (Skiff) play direction (seq.c seq_next): DIR_FWD REV PING RAND */
    P_DKSRC, P_DKAMT, P_DKREL,                 /* (Skiff) ducking (duck.c): the track it ducks under, DEPTH, RELEASE */
    P_DTYPE, P_DTONE,                          /* (Skiff phase 9) FX > DRIVE (fx.c track_dist): DIST's kind, its tone */
    P_EG2_ATK, P_EG2_DEC, P_EG2_SUS, P_EG2_REL,   /* (Skiff phase 9) ENV 2 (voice.c env2_tick): a MOD source */
    P_LTUNE0, P_LTUNE1, P_LTUNE2, P_LTUNE3, P_LTUNE4, P_LTUNE5, P_LTUNE6, P_LTUNE7,   /* (Skiff phase 9) the DRUM lanes' */
    P_LDECAY0, P_LDECAY1, P_LDECAY2, P_LDECAY3, P_LDECAY4, P_LDECAY5, P_LDECAY6, P_LDECAY7,   /* TUNE and DECAY */
    P_LSYNC, P_LTRIG, P_LPOL,                  /* (Skiff 1.3, #16, from Felucca 1.2) LFO SYNC (OFF, a note value), TRIG (NOTE,
                                                * FREE), POL (BI, UNI) */
    P_SPRD,                                    /* (1.3, from Felucca) SPREAD: a POLY / UNISON track's voices around PAN */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* (1.3, from Felucca 1.5) the INSERT, its TYPE, three values and MIX (fx.c) */
    P_ESYNC,                                   /* (1.3, from Felucca 1.5) ENV SYNC: ATK DEC REL as note values of the tempo */
    P_LPAN0, P_LPAN1, P_LPAN2, P_LPAN3, P_LPAN4, P_LPAN5, P_LPAN6, P_LPAN7,   /* (2.0, #36) the drum lanes' PAN (voice.c
                                                * lpan_render): stored ids 135..142, after the engine values' (sid_p) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_COUNT
};

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,   /* G_ROUTE: MIDI IN, 0 CH1-6 (channels 1..6 -> parts 1..6, 7..16 ignored), 1 SEL (seq.c) */
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page: the editor switches the engine with a SET of G_ENGSEL; G_ENGGO is
                                 * unused (ids are fixed by the formats and the protocol) */
    G_CLRSEQ, G_INITSND,
    G_RTYPE,                    /* REVERB TYPE: 0 ROOM, 1 SPRING, (Skiff phase 9) 2 HALL (fx.c). Was G_DRCH, the GM drum part's MIDI
                                 * channel (inert since 1.0, never read); projects of formats before FUN7 load it
                                 * as ROOM (project.c proj_rtype_room) */
    G_CMODE, G_CFDBK,           /* (Skiff phase 9) GLO > CHORUS's MODE (0 CHORUS, 1 FLANGER, 2 PHASER) and FEEDBACK
                                 * (fx.c cho_mode). Were G_DRLVL / G_DRREV, the GM drum part's level and reverb send
                                 * (inert since 1.0): an import of a format before SKF3 reads them as those
                                 * (project.c proj_drums_to_part), then sets them 0 (proj_import) */
    G_COUNT
};

/* stored parameters of an older layout -> today's P_* order. A store keeps np = the P_COUNT it was
 * written with; common parameters are only ever added just before P_E0, so the first np - 8 are
 * P_LEVEL.. in order and the last 8 are P_E0..P_E7; the parameters a store does not have take def[]
 * (user presets, projects of formats 1 and 2) */
static void params_by_count(int16_t *out, const int16_t *in, uint32_t np, const int16_t *def)
{
    uint32_t i, nc = np - 8u;
    for (i = 0; i < P_E0; i++)
        out[i] = i < nc ? in[i] : def[i];
    for (i = 0; i < 8u; i++)
        out[P_E0 + i] = in[nc + i];
}

/* (Skiff 2.0, Tier B: design/TIER_B.md 4) stored parameter ids (SID): a parameter's number in a record, fixed for good.
 * One added later takes the next free number wherever it goes in the enum; one removed keeps its own, retired; 0..254.
 * 2.0's first records had the P_* positions (P_LEVEL 0 .. P_E7 134). (#36) The lane pans came before P_E0 (P_LPAN0 127):
 * their SIDs are the next free, 135..142, and the engine values keep 127..134, so sid_p swaps those two blocks of
 * eight (its own inverse: p_sid). The globals likewise (GSID: today's G_*) */
#define SID_COUNT ((uint32_t)P_COUNT)          /* the SIDs this firmware knows: 0 .. SID_COUNT - 1 */
#define GSID_COUNT ((uint32_t)G_COUNT)
#define SID_E0 127u                            /* the engine values' first SID (P_E0 before #36): frozen */
_Static_assert(P_LPAN0 == SID_E0 && P_E0 == SID_E0 + 8u && P_COUNT == P_E0 + 8u, "sid_p: the lane pans, then E0..E7");
static uint32_t sid_p(uint32_t sid)            /* P_COUNT: not one of ours */
{
    return sid < SID_E0 ? sid : sid < SID_E0 + 8u ? sid + 8u : sid < SID_COUNT ? sid - 8u : P_COUNT;
}
#define p_sid(p) sid_p(p)                      /* (a swap: its own inverse) */
static uint32_t gsid_g(uint32_t sid) { return sid < GSID_COUNT ? sid : G_COUNT; }
/* a track's parameters p into b by SID: their count, then each value + 64. How many bytes; 0 for a value no record
 * holds (below -64, above 127) */
static uint32_t sid_put(uint8_t *b, const int16_t *p)
{
    uint32_t s;
    b[0] = (uint8_t)SID_COUNT;
    for (s = 0; s < SID_COUNT; s++) {
        int32_t v = p[sid_p(s)];
        if (v < -64 || v > 127)
            return 0;
        b[1u + s] = (uint8_t)(v + 64);
    }
    return 1u + SID_COUNT;
}
/* .. back from b (max bytes there): the SIDs this firmware knows into p, the others passed over (a newer firmware's),
 * the ones b lacks left as p holds them (the caller's defaults: an older record's). How many bytes it took; 0 for a
 * record shorter than its count says or holding a byte past 191 (a value no record holds, as SKF3's reader refuses it).
 * The values are as stored: the caller bounds them to their parameters (project.c proj_bound) */
static uint32_t sid_get(int16_t *p, const uint8_t *b, uint32_t max)
{
    uint32_t n = max ? b[0] : 0u, s;
    if (!max || 1u + n > max)
        return 0;
    for (s = 0; s < n; s++) {
        if (b[1u + s] > 191u)
            return 0;
        if (sid_p(s) < P_COUNT)
            p[sid_p(s)] = (int16_t)((int32_t)b[1u + s] - 64);
    }
    return 1u + n;
}
/* the globals g into b by GSID: their count, then each value (16 bits, little-endian). How many bytes */
static uint32_t gsid_put(uint8_t *b, const int16_t *g)
{
    uint32_t s;
    b[0] = (uint8_t)GSID_COUNT;
    for (s = 0; s < GSID_COUNT; s++) {
        uint16_t v = (uint16_t)g[gsid_g(s)];
        b[1u + 2u * s] = (uint8_t)v;
        b[2u + 2u * s] = (uint8_t)(v >> 8);
    }
    return 1u + 2u * GSID_COUNT;
}
/* .. back, as sid_get */
static uint32_t gsid_get(int16_t *g, const uint8_t *b, uint32_t max)
{
    uint32_t n = max ? b[0] : 0u, s;
    if (!max || 1u + 2u * n > max)
        return 0;
    for (s = 0; s < n; s++)
        if (gsid_g(s) < G_COUNT)
            g[gsid_g(s)] = (int16_t)(b[1u + 2u * s] | (uint16_t)b[2u + 2u * s] << 8);
    return 1u + 2u * n;
}

/* engine indices the stores name (engines.c ENGINES[]: append-only) */
#define ENGI_GRAIN 8u
#define ENGI_PHYS 9u
#define ENGI_DRUM 10u
#define ENGI_DRUMX 14u           /* (Skiff) DRUM-X (eng_drumx.c): a drum track as DRUM's (seq.c drum_track) */
#define NHEAVY 4u                /* (Skiff phase 6) the tracks that play every engine; the ones after them are light: no
                                  * PHYS, no GRAIN (engines.c eng_allowed: their state is the big one) */
_Static_assert(NTRK >= NHEAVY, "the light tracks come after the four");
/* PHYS MODEL DRUM (MODEL 4, before 1.0) -> the DRUM engine, its E values in place: {MODEL, TUNE, TONE, DECY,
 * SNAP, ACC, KICK 0..127, PERC 0..127} -> {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK 0..1, DRV 0}. 1 = it was one
 * (its engine is ENGI_DRUM now); projects (project.c) and user presets (upreset.c) */
static int drum_from_phys(uint32_t engine, int16_t *e)
{
    if (engine != ENGI_PHYS || e[0] != 4)
        return 0;
    e[0] = (int16_t)((e[7] < 0 ? 0 : e[7] > 127 ? 127 : e[7]) >> 5);   /* PERC -> KIT: STD HAND CYM H+CYM */
    e[6] = (int16_t)(e[6] >= 64);                                       /* KICK: PUNCH, ROUND */
    e[7] = 0;
    return 1;
}

/* SAMPLE SET 4 was PERC, the GM-mapped drum kit (tools/gen_samples.py); retired after 1.0.2: its index stays
 * (SET / GRAIN SRC 4 is an alias of PIANO, USR1..3 keep 5..7). A sound that selected it is the DRUM engine with
 * its default kit (eng_drum.c DRUM_PRESETS[0]: the same GM key map, so its patterns still play as drums): its E
 * values become the kit's, the rest of the sound (mix, sends, matrix, ..) stays. 1 = it was one (its engine is
 * ENGI_DRUM now); projects (project.c proj_perc), user presets (upreset.c up_migrate), factory preset 4 and
 * favourites (ui.c, settings_persist.c). Idempotent */
#define ENGI_SAMPLE 4u
#define SMP_SET_PERC 4u
#define DRUM_KIT_E {0, 64, 70, 64, 64, 100, 0, 0}   /* {KIT STD, TUNE, TONE, DECY, SNAP, ACC, KICK PUNCH, DRV} */
static int drum_from_perc(uint32_t engine, int16_t *e)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    uint32_t i;
    if (engine != ENGI_SAMPLE || e[0] != (int16_t)SMP_SET_PERC)
        return 0;
    for (i = 0; i < 8u; i++)
        e[i] = KIT[i];
    return 1;
}

/* ------------------------------------------------- voices, engines --- */
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    uint8_t stage2;              /* (Skiff phase 9) ENV 2's (voice.c env2_tick): the same, its release at the gate's end */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    uint8_t mvel;                /* mod.c: the note-on velocity (before UNISON scaling) and RAND of the note */
    uint8_t side;                /* (1.3) SPREAD: 0 the left of PAN, 1 the right (voice.c voice_start, fx.c mix_spread) */
    int16_t mrnd;
    int32_t env2;                /* (Skiff phase 9) ENV 2, Q24: a MOD source only (mod.c MS_ENV2) */
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* the residual below 1/16 semitone in inc: unison detune, TUNE, bend, (Skiff 1.3.2) the LFO's vibrato (1/4096) */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends (a sound load never touches the steps; the
                                  * arp is the track's too: a preset does not set it) */
    struct { uint8_t id; int8_t v; } more[4];   /* (Skiff) up to four more of the sound's values, {P_* id, value}: the
                                  * LFO, GLIDE, ENV -> PITCH (sound parameters only, never param_kept's: editor_test); id 0 ends */
    uint8_t tag;                 /* (Skiff phase 9) its type, PT_*: PRESETS' LIST shows one (ui.c preset_in_list) */
} preset_t;
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define MORE(...) .more = {__VA_ARGS__}
#define TAG(t) .tag = (t)
enum { PT_NONE, PT_BASS, PT_LEAD, PT_PAD, PT_KEYS, PT_PLUCK, PT_DRUM, PT_FX, PT_N };   /* (stored: the LIST setting) */

struct track;
typedef struct {                 /* an engine (engines.c ENGINES[]; the eng_*.c files) */
    const char *name;            /* "ANALOG" (PRESETS, the editor) */
    const char *page_title[2];   /* EDIT 1 and EDIT 2 */
    param_desc_t edit[8];        /* P_E0..P_E7 */
    const preset_t *presets;
    uint8_t npresets;
    uint8_t knob[4];             /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    uint8_t sampled;             /* 1 = plays recorded material (a position, not a phase): voice.c keeps
                                  * no phases over a retrigger, spreads none for UNISON, renders at SUS 0 */
    uint8_t keep;                /* bit k: s[k] is kept when a sounding voice is retriggered (filters) */
    uint8_t oneshot;             /* 1 = hits (DRUM): always POLY, no glide; amp ends its voices (eng_drum.c) */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* optional: the note of key k (0 = the lowest) when the engine maps the keys itself, else -1
     * (then the scale keyboard of seq.c kb_map) */
    int32_t (*keys)(const struct track *t, uint32_t k);
    /* 1 = the engine's own envelopes are the voice's amplitude (FM6): voice.c renders it at 1.0 (no ADSR, no
     * velocity; LFO -> AMP, the matrix's AMP and the fades still apply; ENV is 0) and the voice ends when
     * done() says so (once per control tick, before the render), not at the end of the ADSR's release */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
} engine_t;

/* ------------------------------------------------- tracks, the song --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
#define SF_RATCH_SH 3u                   /* RATCH: the hits of a NOTE step - 1 (0..3: x1..x4, seq.c seq_ratchet); */
#define SF_RATCH (3u << SF_RATCH_SH)     /* bits 3..4, so a user preset's pattern (flag 4 = tie) carries it too */
#define SF_FLAM 32u                      /* Skiff: FLAM: a soft grace hit, the full one FLAM_SAMPLES later (seq.c
                                          * seq_step, seq_ratchet); RATCH x1 with it: RATCH's fifth value (step_rflam) */
#define FLAM_SAMPLES 661u                /* 15 ms (at most half the step) */
#define NLANE 8                  /* drum lanes of a step (the DRUM engine's: eng_drum.c DRUM_LANE_NOTE) */
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide; drum hits */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE | SF_RATCH */
    uint8_t vel;
    uint8_t hit;                 /* bit l: lane l hits (its GM note, on any engine): the DRUM grid */
    uint8_t acc;                 /* bit l: that hit is accented (velocity 127) */
    uint8_t probability;         /* 0 = legacy 100%; 1..100 = percent, 101 = silent */
    uint8_t cond;                /* Skiff: the trig condition (TC_*; 0 = always: every older pattern) */
    int8_t nudge;                /* Skiff: NUDGE, NUDGE_MIN..NUDGE_MAX 64ths of the step later (seq.c seq_enter); 0 =
                                  * on the grid: every older pattern */
} step_t;
#define NUDGE_MIN (-32)
#define NUDGE_MAX 31
/* (Skiff 2.0, #25) per-note ties: bit k, note[k] goes on from the step before (seq.c: no note-on, no release; a tie never
 * starts a note). In acc on a step without lane hits (acc is the hits' accents; without hits it was always 0), so
 * step_t keeps its 13 bytes; a step with hits has none (design/TIER_B.md). A TIE step still holds everything */
static uint32_t step_ties(const step_t *s) { return s->hit ? 0u : s->acc & 15u & ((1u << (s->n & 7u)) - 1u); }
/* (Skiff) n steps' NUDGE as a stored plane: 6 bits each, two's complement (a plane of 0s: every step on the grid), 4
 * steps in 3 bytes (n a multiple of 4). Projects (project.c SKF1) and user patterns (upreset.c) */
static void nudge_pack(uint8_t *b, const step_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 4u, b += 3) {
        uint32_t w = 0;
        for (uint32_t j = 0; j < 4u; j++)
            w |= ((uint32_t)s[i + j].nudge & 63u) << (6u * j);
        b[0] = (uint8_t)w;
        b[1] = (uint8_t)(w >> 8);
        b[2] = (uint8_t)(w >> 16);
    }
}
static void nudge_unpack(step_t *s, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 4u, b += 3) {
        uint32_t w = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
        for (uint32_t j = 0; j < 4u; j++) {
            int32_t v = (int32_t)((w >> (6u * j)) & 63u);
            s[i + j].nudge = (int8_t)(v >= 32 ? v - 64 : v);
        }
    }
}
/* Skiff: a step's trig condition (seq.c step_cond_ok): FILL / !FILL while GLO + F4 is held (or not), A:B on pass A of
 * every B of its track's loop (take.c take_pass). Saved in bit 7 of the step's note bytes (project.c) */
enum { TC_NONE, TC_FILL, TC_NFILL, TC_1_2, TC_2_2, TC_1_3, TC_2_3, TC_3_3, TC_1_4, TC_2_4, TC_3_4, TC_4_4, TC_COUNT };
static const char *const TC_NAME[TC_COUNT] = {"--", "FILL", "!FILL", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4",
                                              "3:4", "4:4"};
typedef struct {                 /* a step as formats 1..4 (projects to FUN4) stored it: no hits */
    uint8_t note[4];
    uint8_t n, time, flags, vel;
} step8_t;

/* Probability keeps zero-initialized and legacy patterns at 100%. */
static uint32_t step_chance(const step_t *s) { return !s->probability ? 100u : s->probability <= 100u ? s->probability : 0u; }
static void step_set_chance(step_t *s, uint32_t chance) { s->probability = (uint8_t)(chance >= 100u ? 0u : chance ? chance : 101u); }
/* RATCH: a NOTE step plays its notes and hits this many times (1..4), in equal parts of the step; every older
 * pattern holds 0 there, x1 */
static uint32_t step_ratchet(const step_t *s) { return ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u; }
static void step_set_ratchet(step_t *s, uint32_t hits)   /* (no FLAM then) */
{
    s->flags = (uint8_t)((s->flags & ~(SF_RATCH | SF_FLAM)) | ((hits < 1u ? 0u : hits > 4u ? 3u : hits - 1u) << SF_RATCH_SH));
}
/* Skiff: the RATCH knob's value (SEQ > CHANCE): x1..x4, 5 = FLAM */
static uint32_t step_rflam(const step_t *s) { return (s->flags & SF_FLAM) ? 5u : step_ratchet(s); }
static void step_set_rflam(step_t *s, uint32_t v)
{
    step_set_ratchet(s, v >= 5u ? 1u : v);
    if (v >= 5u)
        s->flags |= SF_FLAM;
}
#define MOTION_MAX 64u
/* place: track << 6 | step ((Skiff phase 6) 9 bits: up to eight tracks x 64 steps); the stored formats of before
 * hold it in a byte (four tracks: project.c proj_mot_put, the pattern records only the step). value: the parameter's
 * value, -64..127 (motion_valid, motion_put: what a project's byte holds), so one byte.
 * id: the P_* id, 0..254 ((Skiff 1.3) it was 7 bits, the lock bit 7 of the same byte: P_COUNT passed 127). lock (1.1):
 * a parameter lock: the value sounds on that step only and goes back after it (motion.c motion_step); without it an
 * automation event, the value holds until another one changes it. One record per (track, step, id), of either kind.
 * The stored formats keep the lock as bit 7 of their id byte (MOTION_LOCK), an id from 128 on as bit 6 of a pattern's
 * step byte (bank.c BK_VER2). The record stays 4 bytes: every copy (launch, undo, the tools) the size it was */
#define MOTION_LOCK 0x80u                       /* (the stored id byte's lock bit; motion_put's kind) */
#define MOTION_ID(e) ((uint32_t)(e)->id)
typedef struct { uint16_t place : 9, lock : 1, : 6; uint8_t id; int8_t value; } motion_event_t;
typedef struct { uint8_t count, on, rsv[2]; motion_event_t event[MOTION_MAX]; } motion_store_t;
_Static_assert(sizeof(motion_store_t) == 260u, "motion: a record is 4 bytes");
#define CHAIN_ROWS 16u
typedef struct { uint8_t slot, repeat; } chain_row_t;
/* (Skiff phase 5) the patterns: one pool (bank.c, ids 0..NPAT-1); each track's list of NENT entries names them */
#define NPAT 64u
#define NENT 16u
#define PAT_NONE 0xFFu                 /* no pattern (an entry stores id + 1, 0 = empty: ui.c lists) */
#define PAT_REST (NPAT + 1u)           /* (#28) what an entry stores for a REST (no pattern: ent_pat says PAT_NONE) */
enum { SONG_OFF, SONG_ON, SONG_ONCE };   /* ui.c lists.song ((2.0, #47, the user's pick 4A) ONCE: bank.c song_poll; a
                                          * project holding it: firmware before refuses it, as a REST) */
#define REPS_CHAIN 16u                 /* (2.0, #44, 3A) an entry's REPS byte (0..15: 1x..16x): on CHAIN into the next
                                        * (bank.c ent_block); firmware before refuses a project holding one */
#define PROJ_SCENES 82u                /* the 16 scenes as bank.c keeps them: 3 bytes each, then a used mask, then
                                        * (Skiff phase 6) 2 bytes each for T5 and T6 */
typedef struct {
    uint8_t count, rsv[3];
    chain_row_t row[CHAIN_ROWS];
} chain_config_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    int16_t lfo_prev;            /* (1.3) the S&H value of the cycle before (mod.c SLEW glides from it to lfo_rnd's) */
    uint32_t lfo_t;              /* (Skiff) control ticks since its retrigger (the graphs: ui_graph.c lfo_view_at) */
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_walk;            /* WALK: the place in the note list it stands on */
    uint8_t arp_ch[3];           /* CHORD: the other notes sounding with arp_note, 0 = none */
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_cnt;             /* (Skiff) steps of the pass so far (0: none yet, a run starts: PLAY, a song row); seq_next */
    uint8_t seq_down;            /* (Skiff) DIR PING: on its way down */
    uint8_t seq_notes[4 + NLANE];   /* sounding seq notes (the step's notes, then its hits) */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint8_t rat_left;            /* RATCH: repeats of the playing step still to come (seq.c seq_ratchet) */
    uint8_t fire_idx;            /* (Skiff) the step played last (NUDGE: not always seq_idx): its RATCH parts, FLAM */
    uint8_t nud;                 /* (Skiff) NUDGE: NUD_LATE, NUD_EARLY, NUD_DONE (seq.c seq_enter) */
    uint8_t early_idx;           /* (Skiff) NUD_EARLY / NUD_DONE: the next step, played early */
    uint32_t late_at, early_at;  /* (Skiff) the seq_pos the playing step (late) / the next (early) plays at */
    uint32_t rat_pos;            /* (Skiff) samples since the step played last started, its nudge in: RATCH, FLAM */
    uint32_t seq_off;
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[NLANE];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    int32_t dist_dc;                       /* (Skiff phase 9) the DC after HARD's and FOLD's clip (fx.c dist_hard) */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST tail; (1.3) the INSERT's) */
    uint8_t ins_run;             /* (Skiff 1.3) the INSERT runs (fx.c track_insert): on, or fading */
    uint8_t lfo_wrap;            /* (Skiff 1.3) the LFO's phase wrapped this block: its S&H is drawn after the tracks
                                  * (voice.c track_lfo_end; in the pad before armp: track_t keeps its size) */
    uint8_t hits;                /* (Skiff Q6) its note-ons, counted (voice.c trk_note_on): QUARK's event display
                                  * (ui_graph.c qk_detector; the pad's last byte) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[8];           /* P_E0..P_E7 of the sounding engine: the fade renders with these */
    uint32_t esync_k[3];         /* (1.3) ATK's step, DEC's and REL's coefficient (Q16) this block (voice.c esync_coef) */
    uint8_t spread_on;           /* (1.3) SPREAD this block (fx.c mix_part, voice.c track_render) */
    uint8_t lpan_on;             /* (2.0, #36) a drum kit with a lane panned, this block (the same, voice.c lpan_render) */
    uint8_t sp_alt;              /* (1.3) SPREAD: the side the next voice starts on (alternating, voice.c voice_start) */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
    /* modulation matrix (mod.c): MIDI performance controllers of the track's channel, 0..127 */
    uint8_t mw, at;              /* CC1 mod wheel, channel aftertouch */
    uint8_t ex_off;              /* 127 - CC11 expression (0: full, the MIDI default) */
    uint8_t m_vel, m_key, m_vi;  /* the latest note-on: velocity, note, voice index (per-block destinations) */
    int16_t m_rnd;               /* .. its RAND */
    int32_t m_env;               /* the amp envelope of voice m_vi, last block (Q15) */
    int32_t m_env2;              /* (Skiff phase 9) .. its ENV 2 (Q15; while a MOD slot uses it) */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t grid;                /* 1: the keys are the DRUM grid (ui.c grid_on): only the lane keys play (seq.c);
                                  * 2: NAME (ui_name.c): no key plays */
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: six parts */
static song_t song;
/* the selected track. (Skiff 2.0) A pointer kept with song.sel, so a use is a load, not song.sel times the track's
 * size: song.sel is only ever set through sel_set (the tests too) */
static track_t *tsel = trk;
#define TSEL tsel
static void sel_set(uint32_t k)
{
    song.sel = (uint8_t)k;
    tsel = &trk[k];
}

/* SWING of a track's step clock: the track's own plus the global one, at most 100 (#31). The sequencer
 * (seq.c step_samples) and the SLICER (slicer.c sl_enter) both time steps with it. At 100 the even
 * steps are 1.4 x the straight length and the odd ones 0.6 x. */
#define SWING_MAX 100
static inline int32_t track_swing(const track_t *t)
{
    int32_t sw = t->p[P_SSWING] + song.g[G_SWING];
    return sw < 0 ? 0 : sw > SWING_MAX ? SWING_MAX : sw;
}
/* the length of step idx of a straight length `base`, swung: even steps longer, odd ones shorter */
static inline uint32_t swing_step_len(const track_t *t, uint32_t base, uint32_t idx)
{
    int32_t sw = track_swing(t) * (int32_t)base / 250;
    return base + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* ----------------------------------------------------------- system --- */
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> UBOOT */
#define BOOTGUARD_MAGIC 0x42475244u
struct { uint32_t magic, failed, pending; } bootguard __attribute__((section(".noinit")));
