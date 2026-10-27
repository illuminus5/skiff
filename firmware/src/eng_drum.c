/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DRUM: a drum kit of 8 lanes (drum_voice.c, Felucca's own): KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM,
 * BELL. Before 1.0 it was PHYS's MODEL DRUM; projects and user presets saved then load as this engine
 * (drum_from_phys in core.h).
 *
 * Keys and notes: the keys play the General MIDI drum map with the kick on the first C (C KICK, C# RIM,
 * D SNARE, D# CLAP, F# HAT CL, A# HAT OP, F G A B C D TOMS, C# CYM, G# BELL; OCT -/+ move it), and a note
 * plays its GM drum (35..81: kicks, snares, toms, hats, cymbals, bells, congas, claves; other notes as
 * their octave of 36..47), so GM patterns and MIDI parts play as they did on SAMPLE PERC (retired: its sounds
 * load as this engine, core.h drum_from_perc).
 *
 * Parameters: KIT the kit: STD (Felucca's own: TOM RIM BELL on lanes 6..8, other GM notes their congas, claves,
 * cymbals) or a model kit (80 10 66 55 77, 1.5.6 DEEP TIGHT PERC: every lane that kit's own piece, drum_voice.c DV_KIT; KICK does not
 * apply), or ((2.0, #35) USR1..3) a user sample slot: each lane its sample (drum_smp; TONE below its middle a low-pass,
 * SNAP and KICK do not apply). Its stored values 1..3 were HAND CYM H+CYM (STD with CONGA CLAVE / CYM on lanes 6..8) until 1.0.4: they
 * play 66 10 77 now (DK_PLAYS), and whatever stores them lands there (params.c enum_orig, param_fit: the knob
 * steps over them, the editor's SET, projects, user presets, motion). KICK the kick (PUNCH, ROUND). TUNE (64: as
 * designed, +-12 semitones), TONE, DECY and SNAP (each lane's extra: the kick's drive, the snare's snappiness,
 * the clap's spread, the hats' noise, the toms' bend, the rim's drive, the bell's strike) move every lane from its designed value. ACC
 * is the accent at full velocity (velocity scales it), DRV a soft clip on every hit (x1..x4, level kept).
 * Each lane has its LEVEL (P_LN0..P_LN7, EDIT > LANES / LANES 2, #97: square law, 100 % the kit as designed, the
 * default, so every older sound plays as it did), on the voice amplitude after the knee (a quieter lane is cleaner).
 * A hit keeps the drum and the variant it was struck with; the knobs move it while it rings.
 *
 * Polyphony: a lane per drum and part, mono: a hit reuses its lane's voice even at another pitch, the 8 lanes ring
 * together. A sounding voice plays one lane (s[0]); the lane remembers its voice (owner), and a voice
 * whose lane was struck again by another voice, or has rung out (-84 dB), ends itself (drum_amp). The
 * voices come from the shared budget (voice.c), up to 8 per part. A closed hat chokes the open one.
 *
 * What applies (engine_t.oneshot): the hit is the envelope. VOICE (always POLY), GLIDE, the ADSR, ENV DEST,
 * LFO DEST PIT / FLT / SHP, the global TUNE and the matrix's PITCH CUT SHP do nothing; LEVEL, PAN, the
 * sends, DIST, the SLICER, LFO DEST AMP, velocity, the matrix's AMP and its E1..E8 (the knobs, per
 * block) do. A released key does not end a hit.
 *
 * State: 8 lanes per part in the part's shared engine area (engines.c eng_mem: the parameters, coefficients, voice
 * and metal source of each lane). */
#include "drum_voice.c"

/* 1.5.6 (Discussion #216): DEEP TIGHT PERC (9..11), kits put together from the five's pieces (drum_voice.c DV_KIT),
 * appended. Firmware before (Skiff 1.2, Felucca before 1.5.6) clamps 9..11 to 8 (77) when it loads them (param_fit) */
/* (2.0, #35, the user's pick A of design/mockups/v2_kits.png) USR1..USR3 (12..14): a user sample slot as the kit
 * (drum_user), appended; the SMK-37 offers USR1 only (SMP_SHOWN) */
enum { DK_STD, DK_HAND, DK_CYM, DK_HCYM, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP, DK_TIGHT, DK_PERC, DK_USR1, DK_USR2,
       DK_USR3, DK_COUNT };   /* (stored values) */
/* the kit a stored KIT value plays: HAND CYM H+CYM (retired after 1.0.4) -> 66 (a conga on TOM), 10 (a cymbal on
 * BELL), 77 (claves on RIM, a cymbal on BELL) */
static const uint8_t DK_PLAYS[DK_COUNT] = {DK_STD, DK_66, DK_10, DK_77, DK_80, DK_10, DK_66, DK_55, DK_77, DK_DEEP,
                                           DK_TIGHT, DK_PERC, DK_USR1, DK_USR2, DK_USR3};
static uint32_t drum_kit_plays(int32_t v) { return DK_PLAYS[clamp(v, 0, DK_COUNT - 1)]; }
/* the model kits' pieces where a lane plays another than its own (the lane's name): 1 TOM -> CONGA, 2 RIM -> CLAVE,
 * 4 BELL -> CYM */
static const uint8_t DK_SWAP[DV_NKIT] = {0, 4, 1, 0, 2 | 4, 4, 0, 1 | 2};
static const uint8_t DV_TYPE_LANE[DVT_COUNT] = {
    DV_KICK, DV_KICK, DV_SNARE, DV_CLAP, DV_HATC, DV_HATO, DV_TOM, DV_TOM, DV_RIM, DV_RIM, DV_BELL, DV_BELL,
};

/* .. a user kit's: its slot + 1, 0 a kit of DRUM's own */
static uint32_t drum_user(const int16_t *p)
{
    uint32_t k = drum_kit_plays(p[P_E0]);
    return k >= DK_USR1 ? k - DK_USR1 + 1u : 0u;
}

typedef struct {
    dv_param_t key;              /* the parameters the coefficients were set up with */
    dv_coef_t c;
    dv_voice_t v;
    dv_metal_t mb;
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    uint8_t role;                /* the drum struck (DVT_*) */
    int8_t st;                   /* its semitones from the designed pitch (the GM map) */
    uint8_t pad;
} drum_lane_t;


/* 1..3 named as the kit they play: aliases, never shown or offered (EDITOR_PROTOCOL.md: retired values) */
static const char *const N_DRUM_KIT[] = {"STD", "66", "10", "77", "80", "10", "66", "55", "77", "DEEP", "TIGHT", "PERC",
                                         "USR1", "USR2", "USR3"};
static const char *const N_DRUM_KICK[] = {"PUNCH", "ROUND"};

/* General MIDI notes 35..81 -> the drum (DVT_*; DVT_PUNCH: the kick KICK picks) and semitones from its
 * designed pitch */
static const int8_t DRUM_GM[47][2] = {
    {DVT_PUNCH, -2}, {DVT_PUNCH, 0}, {DVT_RIM, 0}, {DVT_SNARE, 0}, {DVT_CLAP, 0}, {DVT_SNARE, 2},     /* 35 */
    {DVT_TOM, -7}, {DVT_HATC, 0}, {DVT_TOM, -4}, {DVT_HATC, -2}, {DVT_TOM, 0}, {DVT_HATO, 0},         /* 41 */
    {DVT_TOM, 3}, {DVT_TOM, 5}, {DVT_CYM, 0}, {DVT_TOM, 8}, {DVT_CYM, -3}, {DVT_CYM, 2},              /* 47 */
    {DVT_BELL, 5}, {DVT_HATC, 5}, {DVT_CYM, 4}, {DVT_BELL, 0}, {DVT_CYM, 1}, {DVT_CLAVE, -12},        /* 53 */
    {DVT_CYM, -2}, {DVT_CONGA, 5}, {DVT_CONGA, 2}, {DVT_CONGA, 0}, {DVT_CONGA, 0}, {DVT_CONGA, -5},   /* 59 */
    {DVT_TOM, 10}, {DVT_TOM, 7}, {DVT_BELL, 7}, {DVT_BELL, 3}, {DVT_HATC, 3}, {DVT_HATC, 7},          /* 65 */
    {DVT_CLAVE, 7}, {DVT_CLAVE, 5}, {DVT_HATC, -4}, {DVT_HATC, -6}, {DVT_CLAVE, 0}, {DVT_CLAVE, -4},  /* 71 */
    {DVT_CLAVE, -7}, {DVT_CONGA, 7}, {DVT_CONGA, 3}, {DVT_BELL, 12}, {DVT_BELL, 12},                  /* 77 */
};
/* the drum of a note (DVT_*, or a model kit's lane: DV_KTYPE) and its semitones; KICK picks the kick; a model kit
 * plays its own piece on the note's lane */
static uint32_t drum_gm(const int16_t *p, uint32_t note, int32_t *st)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u, t = (uint32_t)DRUM_GM[n - 35u][0];
    uint32_t kit = drum_kit_plays(p[P_E0]);           /* STD, the model kits */
    *st = DRUM_GM[n - 35u][1];
    if (kit >= DK_80 && kit < DK_USR1)
        return DV_KTYPE(kit - DK_80 + 1u, DV_TYPE_LANE[t]);
    return t == DVT_PUNCH && p[P_E6] > 0 ? DVT_ROUND : t;
}

/* ----------------------------------------------------- the grid's lanes --- */
/* A step's lane hits (step_t.hit / acc, the DRUM grid: SEQ > NOTES on a DRUM track) play these GM notes, on any
 * engine: DRUM strikes its lanes, a synth plays the pitches. Each is the designed pitch of
 * its lane (st 0 in DRUM_GM) */
static const uint8_t DRUM_LANE_NOTE[NLANE] = {36, 38, 39, 42, 46, 45, 37, 56};

/* the lane a note strikes (KIT-proof: a kit plays its own piece inside the lane) */
static uint32_t drum_lane(uint32_t note)
{
    uint32_t n = note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u;
    return DV_TYPE_LANE[DRUM_GM[n - 35u][0]];
}

/* KIT's swaps of lanes 6..8 (1 TOM -> CONGA, 2 RIM -> CLAVE, 4 BELL -> CYM) */
static uint32_t drum_swaps(const track_t *t)
{
    uint32_t kit = drum_kit_plays(t->p[P_E0]);
    if (t->eng_req % NENGINES != ENGI_DRUM)          /* ((Skiff) DRUM-X: its E0 is MRPH, its lanes their own names) */
        return 0u;
    return kit >= DK_80 && kit < DK_USR1 ? DK_SWAP[kit - DK_80] : 0u;
}

/* (2.0, #35) user kit slot k's sample for lane l: the narrowest zone holding the lane's note (DRUM_LANE_NOTE, as
 * SAMPLE picks one), as a voice keeps it (0x8000 | k << 5 | zone); 0 none: the lane silent */
static uint32_t drum_user_zone(uint32_t k, uint32_t l)
{
    uint32_t i, zi = 0, w = 128u, note = DRUM_LANE_NOTE[l & (NLANE - 1u)];
    k %= SMP_USER_SLOTS;
    for (i = 0; i < usr_nz[k]; i++) {
        const smp_zone_t *z = &usr_zone[k][i];
        if (note >= z->lo && note <= z->hi && (uint32_t)(z->hi - z->lo) <= w) {
            zi = 0x8000u | k << 5 | i;
            w = (uint32_t)(z->hi - z->lo);
        }
    }
    return zi;
}
/* .. lane l of track t sounds: a user kit's has a sample (LANES and LANE MIX draw one without it empty); every other's */
static int drum_lane_has(const track_t *t, uint32_t l)
{
    uint32_t u = t->eng_req % NENGINES == ENGI_DRUM ? drum_user(t->p) : 0u;
    return !u || drum_user_zone(u - 1u, l) != 0u;
}

/* the lane's name as the track's KIT plays it (5 characters at most) */
/* (Skiff 1.3, from Felucca 1.4.1) the lanes' mutes, a bit each a track (KICK .. BELL), DRUM's and DRUM-X's: the EDIT
 * layer's black keys 1..8 (ui_layer.c). Performance state: not saved; a sound load clears the track's, a project load
 * every track's. A muted lane's voice rings on unheard (its hits still take a voice: un-muting is seamless) */
static uint8_t drum_mute[NTRK];
static const char *drum_lane_name(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "TOM", "RIM", "BELL"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CONGA";
    if (l == DV_RIM && (kit & 2u))
        return "CLAVE";
    return (kit & 4u) && l == DV_BELL ? "CYM" : N[l];
}
/* .. in two letters, the drum machine way (the grid's lane column) */
static const char *drum_lane_abbr(const track_t *t, uint32_t l)
{
    static const char *const N[NLANE] = {"BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"};
    uint32_t kit = drum_swaps(t);
    l &= NLANE - 1u;
    if (l == DV_TOM && (kit & 1u))
        return "CG";
    if (l == DV_RIM && (kit & 2u))
        return "CL";
    return (kit & 4u) && l == DV_BELL ? "CY" : N[l];
}

/* the lanes step s strikes: its hits, and its notes on their lanes (a NOTE step only) */
static uint32_t step_lanes(const step_t *s)
{
    uint32_t m = 0, i;
    if (s->time != ST_NOTE)
        return 0;
    for (i = 0; i < s->n && i < 4u; i++)
        m |= 1u << drum_lane(s->note[i]);
    return m | s->hit;
}

/* .. and which of them are accented */
static uint32_t step_accents(const step_t *s) { return s->flags & SF_ACCENT ? step_lanes(s) : s->acc & step_lanes(s); }

/* a step's notes that are a lane's note become that lane's hits (the same note, the same velocity: nothing
 * sounds different). Other notes (a low tom 41, a crash 49) stay notes, shown on their lane. A step accent
 * of a step left with hits only becomes the hits' accents. Pattern loads into a DRUM track, projects of
 * before the grid, the grid's edits */
static void step_to_grid(step_t *s)
{
    uint32_t i, k = 0;
    if (s->time != ST_NOTE)
        return;
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t l = drum_lane(s->note[i]);
        if (s->note[i] == DRUM_LANE_NOTE[l])
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[k++] = s->note[i];
    }
    for (i = k; i < 4u; i++)
        s->note[i] = 0;
    s->n = (uint8_t)k;
    if (!k && (s->flags & SF_ACCENT)) {
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
}

static drum_lane_t *drum_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drum_lane_t *)eng_part(t) : 0;   /* (Skiff: the shared area, engines.c) */
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drum_lane_t *drum_lane_of(track_t *t, const voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice. */
static voice_t *drum_reuse(track_t *t, uint32_t note)
{
    drum_lane_t *K = drum_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drum_note_on(track_t *t, voice_t *v)
{
    drum_lane_t *K = drum_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), role, lane, u;
    int32_t st;
    if (!K || i >= NVOICE)
        return;
    role = drum_gm(t->p, v->note, &st);
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    u = drum_user(t->p);
    if (u && v->env && L->owner == i + 1u && (v->s[5] & 0x8000) && v->s[6] != 1) {   /* ((#35) its sample still */
        int32_t o = v->s[3] + (((v->s[1] - v->s[3]) * (int32_t)(v->ph[1] >> 1)) >> 15);   /* playing (env: sounding, */
        o = t->p[P_E2] < 64 ? (int32_t)v->ph[2] : (o * (v->s[7] >> 9)) >> 15;    /* voice_start): where it is now, */
        v->s[4] = clamp(o + v->s[4], -32767, 32767);    /* the new hit's declick (drum_smp) */
    } else {
        v->s[4] = 0;
    }
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    if (u) {                                             /* ((2.0, #35) a user kit: the lane's sample, from its start */
        L->st = (int8_t)st;
        L->owner = (uint8_t)(i + 1u);
        v->s[0] = (int32_t)lane;
        v->s[5] = (int32_t)drum_user_zone(u - 1u, lane);
        v->ph[0] = v->ph[1] = v->ph[2] = 0;
        v->s[1] = v->s[2] = v->s[3] = 0;
        v->s[6] = !v->s[5];                              /* (no sample: ended, the lane silent) */
        v->s[7] = 1 << 24;
        v->env_out = v->vel * 258;
        v->env = 1 << 24;
        if (lane == DV_HATC && K[DV_HATO].owner && t->v[K[DV_HATO].owner - 1u].active &&
            !t->v[K[DV_HATO].owner - 1u].s[6])
            t->v[K[DV_HATO].owner - 1u].s[6] = 2;       /* (the open hat fades out: drum_smp) */
        return;
    }
    if (L->role != role || L->v.type != dv_run_type(role)) {   /* another drum on this lane: from rest */
        dv_init(&L->v, role);
        L->key.type = 0xFF;
        L->role = (uint8_t)role;
    }
    L->st = (int8_t)st;
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drum_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here, not from drum_amp:
                                                          * a key-off before the first block (zero-length MIDI
                                                          * notes) would end a fresh voice at env 0 in env_tick */
    dv_trigger(&L->v);
    if (lane == DV_HATC && K[DV_HATO].v.live)            /* a closed hat chokes the open one */
        dv_choke(&K[DV_HATO].v);
}

/* the voice's amplitude: the hit, not the ADSR (which still runs, held at full so a release never ends
 * it). A voice that plays no lane any more, or whose drum has rung out, ends here */
static int32_t drum_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drum_lane_t *L = drum_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (drum_user(t->p) ? v->s[6] == 1 : !L->v.live && !L->v.trig)) {   /* ((#35) a user kit's: its sample over) */
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

/* (2.0, #35) a user kit's lane's next sample: ph[0] the position, s[1] s[2] the ADPCM state (s[1] the sample; a call
 * per sample decoded, as SAMPLE's sample_next) */
static __attribute__((noinline)) void drum_dec(const smp_zone_t *z, voice_t *v)
{
    adpcm_next(z, &v->ph[0], &v->s[1]);
}
/* .. its render: its sample (s[5] the zone, 0 none) decoded as SAMPLE's (drum_dec; s[3] s[1] the samples interpolated,
 * ph[1] the fraction); PITCH and the lane's TUNE its rate (st, the GM map's semitones), DECAY and the lane's below their
 * middle a fall (s[7] Q24, a time constant of 2^sh samples; s[6] 2, a closed hat's choke: 3 ms), TONE below 64 a
 * low-pass (ph[2]), DRIVE and the lane's LEVEL (m) as the kit's; a muted lane plays on unheard. s[6] 1: over (drum_amp
 * ends the voice). s[4]: a hit struck while the lane's last still played starts from where that one was, the step
 * falling away (a time constant of 32 samples, -40 dB in 3 ms; drum_note_on), not a click. Not inlined: drum_render's
 * own cost stays its synth's (tests/target_budget.txt) */
static __attribute__((noinline)) void drum_smp(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m,
                                               uint32_t ln, int32_t st)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[5]);
    int32_t d16 = clamp(st * 16 + (p[P_E1] - 64) * 3 + (p[P_LTUNE0 + ln] - 64) * 3, -1536, 576);
    int32_t dd = p[P_E3] - 64 + p[P_LDECAY0 + ln], sh = v->s[6] == 2 ? 4 : dd < 0 ? clamp(17 + dd / 4, 6, 17) : 0;
    int32_t lp = p[P_E2] < 64 ? 2000 + p[P_E2] * 480 : 0, y = (int32_t)v->ph[2], drv = p[P_E7], g = 0, mk = 0, s;
    int32_t off = v->s[4];
    uint32_t stepq = (pow2_q16(d16) >> 8) * (z->rate >> 8), frac = v->ph[1], i;
    int heard = !((drum_mute[(uint32_t)(t - trk) % NTRK] >> ln) & 1u);
    if (!v->s[5] || !z->n || v->s[6] == 1) {             /* (no sample, its slot emptied since: over) */
        v->s[6] = 1;
        return;
    }
    if (drv > 0) {                                       /* DRV: the kit's soft clip */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    if (!v->ph[0] && !frac)                              /* (its first sample in) */
        drum_dec(z, v);
    for (i = 0; i < n; i++) {
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[3] = v->s[1];
            if (v->ph[0] >= z->n) {                      /* its end */
                v->s[6] = 1;
                break;
            }
            drum_dec(z, v);
        }
        if (v->s[6] == 1)
            break;
        s = v->s[3] + (((v->s[1] - v->s[3]) * (int32_t)(frac >> 1)) >> 15);
        if (sh) {
            v->s[7] -= v->s[7] >> sh;
            if (v->s[7] < 1 << 12) {                     /* (-72 dB: over) */
                v->s[6] = 1;
                break;
            }
            s = (s * (v->s[7] >> 9)) >> 15;
        }
        if (lp)
            s = y += mulq15(s - y, lp);
        if (off) {                                       /* (the declick: s[4]; no divide here, and it reaches 0) */
            s += off;
            off -= (off >> 5) + (off > 0);
        }
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        if (heard)
            out[i] += voice_amp(s, m, i) << 1;
    }
    v->ph[1] = frac;
    v->ph[2] = (uint32_t)y;
    v->s[4] = off;
}

static void drum_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drum_lane_t *L = drum_lane_of(t, v);
    dv_param_t k;
    int32_t y[CTL], mb[CTL], acc = clamp(p[P_E5] * v->vel * 4, 0, 65536), drv = p[P_E7], g = 0, mk = 0;   /* acc Q16 */
    uint32_t ln = (uint32_t)v->s[0] & (DV_NLANE - 1u), i, r;
    vmod_t ml;
    if (!L)
        return;
    m = vmod_level(m, &ml, clamp(p[P_LN0 + ln], 0, 127));   /* the lane's LEVEL (dsp.c); 100 %: the ramp as it was */
    if (n > CTL)
        n = CTL;
    if (drum_user(p)) {                                  /* ((2.0, #35) a user kit: the lane's sample) */
        drum_smp(t, v, out, n, m, ln, L->st);
        return;
    }
    r = L->role;
    dv_default(&k, r);                                   /* the knobs move every lane from its design (64) */
    k.decay = (uint8_t)clamp(k.decay + p[P_E3] - 64 + p[P_LDECAY0 + ln], 0, 127);   /* ((Skiff phase 9) and the */
    k.tone = (uint8_t)clamp(k.tone + p[P_E2] - 64, 0, 127);                              /* lane its own: LANES) */
    k.extra = (uint8_t)clamp(k.extra + p[P_E4] - 64, 0, 127);
    k.accent = (uint8_t)clamp(acc >> 9, 0, 127);         /* (Q16 -> 0..127) */
    k.tune = (int16_t)(L->st * 16 + (p[P_E1] - 64) * 3 + (p[P_LTUNE0 + ln] - 64) * 3);   /* +-12 semitones, each */
    if (((const uint32_t *)&k)[0] != ((const uint32_t *)&L->key)[0] ||
        ((const uint32_t *)&k)[1] != ((const uint32_t *)&L->key)[1]) {
        dv_setup(&L->c, &k);                             /* (a parameter moved) */
        L->key = k;
        if (dv_uses_metal(L->c.type))
            dv_metal_tune(&L->mb, &L->c);
    }
    if (dv_uses_metal(L->c.type) && (L->v.live || L->v.trig))
        dv_metal_run(&L->mb, mb, n);
    dv_run(&L->c, &L->v, mb, y, n);
    if ((drum_mute[(uint32_t)(t - trk) % NTRK] >> ln) & 1u)
        return;                                          /* ((1.3) a muted lane rings on unheard) */
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}

/* the GM drum map, the first C (key 7) is the kick (C2, 36) */
static int32_t drum_keys(const track_t *t, uint32_t k)
{
    (void)t;
    return clamp(29 + 12 * song.octave + (int32_t)k, 0, 127);
}

/* {KIT, TUNE, TONE, DECY, SNAP, ACC, KICK, DRV}; every kit suggests the BEAT pattern (GM notes) */
static const preset_t DRUM_PRESETS[] = {
    {"DRUM KIT", DRUM_KIT_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},   /* (core.h: SAMPLE PERC's too) */
    /* (Skiff) for the themes: CRYPT (dungeon synth), NEON (synthwave), QUARK (particles) */
    {"WAR DRUMS", {0, 40, 38, 108, 40, 110, 1, 24}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 75), TAG(PT_DRUM)},
    {"NEON KIT", {4, 60, 80, 56, 90, 100, 0, 20}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 40), TAG(PT_DRUM)},
    /* (Skiff phase 9) more for every engine: basses, kits */
    {"BOOM KIT", {4, 58, 60, 100, 50, 100, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 25), TAG(PT_DRUM)},
    {"CRISP KIT", {5, 64, 76, 60, 80, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},
    {"RHYTHM BOX", {6, 66, 64, 56, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},
    {"ELECTRO KIT", {7, 64, 70, 60, 70, 100, 0, 15}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},
    {"STUDIO KIT", {8, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 25), TAG(PT_DRUM)},
    {"TIGHT KIT", {0, 70, 80, 30, 100, 110, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 12), TAG(PT_DRUM)},
    {"LOFI KIT", {0, 56, 30, 70, 40, 100, 1, 50}, {0, 100, 127, 100}, 0, 0, FX(20, 0, 0, 30), TAG(PT_DRUM)},
    /* (1.3, from Felucca 1.5.6) the kits DEEP TIGHT PERC (KIT's values appended), each a preset (appended) */
    {"DEEP DRUMS", {DK_DEEP, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 28), TAG(PT_DRUM)},
    {"SNAP KIT", {DK_TIGHT, 64, 64, 56, 64, 100, 0, 16}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 8), TAG(PT_DRUM)},
    {"PERC KIT", {DK_PERC, 64, 64, 64, 64, 100, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 24), TAG(PT_DRUM)},
};

static const engine_t ENG_DRUM = {
    .name = "DRUM",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"KIT", N_DRUM_KIT, 0, DK_PERC + SMP_SHOWN, DK_STD, F_ENUM, 0},   /* ((2.0, #35) .. USR1..3; SMK: USR1) */
        {"PITCH", 0, 0, 127, 64, F_ST16, 0},
        {"TONE", 0, 0, 127, 64, F_PCT, 0},
        {"DECAY", 0, 0, 127, 64, F_PCT, 0},
        {"SNAP", 0, 0, 127, 64, F_PCT, 0},
        {"ACCENT", 0, 0, 127, 100, F_PCT, 0},
        {"KICK", N_DRUM_KICK, 0, 1, 0, F_ENUM, 0},
        {"DRIVE", 0, 0, 127, 0, F_PCT, 0},
    },
    .presets = DRUM_PRESETS,
    .npresets = NELEM(DRUM_PRESETS),
    .note_on = drum_note_on,
    .render = drum_render,
    .amp = drum_amp,
    .knob = {P_E1, P_E2, P_E3, P_E4},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
};
