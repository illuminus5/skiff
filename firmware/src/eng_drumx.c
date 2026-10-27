/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * (Skiff, modified 2026-10-10) DRUM-X: the drum engine of JIANT FM by juanjiant-bit,
 * https://github.com/juanjiant-bit/JIANT (GPL-3.0-only, its eng_drum.c at 9d1c453). In JIANT it replaced DRUM; here it
 * is an engine of its own beside DRUM, without JIANT's group mutes, X-MOD, PUNCH, ATK+ and perform FX. */
/* DRUM-X (drumx_voice.c, after Sonic Charge's Microtonic): a kit of 8 lanes, KICK SNARE CLAP HAT CL HAT OP TOM RIM
 * BELL, each a synthesized sound with two patches A and B and a MORPH between them. The kit is the project's
 * (drumx_voice.c dx_kit: one for every DRUM-X track, project.c keeps it in SKF3), edited on EDIT > SOUND (ui_dx.c).
 *
 * A drum track as DRUM's is (seq.c drum_track): the same keys (the General MIDI drum map, eng_drum.c drum_keys),
 * lanes (drum_lane), grid and lane LEVELs (P_LN0..P_LN7, EDIT > LANES), and the same voices: a lane per drum, mono
 * (a hit reuses its lane's voice even at another pitch), the 8 lanes ringing together, a closed hat choking the open
 * one, a released key not ending a hit, the hit the envelope (engine_t.oneshot: what applies is DRUM's).
 *
 * Parameters (P_E0..P_E7): MRPH the MORPH A..B of every lane; TUNE (+-12 semitones), TONE (COLOR), DECY (DECAY) and
 * NOIS (NOISE) move every lane from its patch (64: as the kit has it); FM every sound through harmonic FM in eight
 * ratio bands (0: the patch as it is); FOLD every sound's oscillator through a wavefolder after its envelope; DRV a
 * soft clip on every hit (x1..x4, the level kept). The knobs move a hit while it rings.
 *
 * State: 8 lanes per part in the part's shared engine area (engines.c eng_mem, eng_lite): each lane's hit. */
#include "drumx_voice.c"

/* engine e plays a kit (DRUM, DRUM-X): a drum track (seq.c drum_track), its kick the duck's source (duck.c) */
static inline int drum_engine(uint32_t e) { return e == ENGI_DRUM || e == ENGI_DRUMX; }

/* General MIDI notes 35..81 -> the lane (DV_*, as drum_lane gives it) and semitones from the lane's pitch: congas on
 * TOM, claves on RIM, cymbals on BELL, each at its pitch from the lane's (JIANT's map) */
static const int8_t DX_GM[47][2] = {
    {DV_KICK, -2}, {DV_KICK, 0}, {DV_RIM, 0}, {DV_SNARE, 0}, {DV_CLAP, 0}, {DV_SNARE, 2},             /* 35 */
    {DV_TOM, -7}, {DV_HATC, 0}, {DV_TOM, -4}, {DV_HATC, -2}, {DV_TOM, 0}, {DV_HATO, 0},               /* 41 */
    {DV_TOM, 3}, {DV_TOM, 5}, {DV_BELL, 0}, {DV_TOM, 8}, {DV_BELL, -3}, {DV_BELL, 2},                 /* 47 */
    {DV_BELL, 5}, {DV_HATC, 5}, {DV_BELL, 4}, {DV_BELL, 0}, {DV_BELL, 1}, {DV_RIM, -12},              /* 53 */
    {DV_BELL, -2}, {DV_TOM, 10}, {DV_TOM, 7}, {DV_TOM, 5}, {DV_TOM, 5}, {DV_TOM, 0},                  /* 59 */
    {DV_TOM, 10}, {DV_TOM, 7}, {DV_BELL, 7}, {DV_BELL, 3}, {DV_HATC, 3}, {DV_HATC, 7},                /* 65 */
    {DV_RIM, 7}, {DV_RIM, 5}, {DV_HATC, -4}, {DV_HATC, -6}, {DV_RIM, 0}, {DV_RIM, -4},                /* 71 */
    {DV_RIM, -7}, {DV_TOM, 12}, {DV_TOM, 8}, {DV_BELL, 12}, {DV_BELL, 12},                            /* 77 */
};
static uint32_t dx_gm_ix(uint32_t note) { return (note >= 35u && note <= 81u ? note : 36u + (note + 120u - 36u) % 12u) - 35u; }

typedef struct {
    dx_voice_t x;                /* the hit */
    uint8_t owner;               /* the voice playing the lane: index + 1, 0 = none */
    int8_t st;                   /* its semitones from the lane's pitch (the GM map) */
} drumx_lane_t;

static drumx_lane_t *drumx_kit_of(const track_t *t)
{
    return t >= &trk[0] && t < &trk[NPART] ? (drumx_lane_t *)eng_part(t) : 0;
}

/* the lane voice v plays, 0 when it plays none (any more) */
static drumx_lane_t *drumx_lane_of(track_t *t, const voice_t *v)
{
    drumx_lane_t *K = drumx_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v);
    if (!K || i >= NVOICE)
        return 0;
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    return L->owner == i + 1u ? L : 0;
}

/* Different GM pitches on a lane still share its one sounding voice (voice.c voice_alloc). */
static voice_t *drumx_reuse(track_t *t, uint32_t note)
{
    drumx_lane_t *K = drumx_kit_of(t);
    uint32_t lane = drum_lane(note), owner = K ? K[lane].owner : 0;
    voice_t *v;
    if (!owner || owner > NVOICE)
        return 0;
    v = &t->v[owner - 1u];
    return v->active && (uint32_t)v->s[0] == lane ? v : 0;
}

static void drumx_note_on(track_t *t, voice_t *v)
{
    drumx_lane_t *K = drumx_kit_of(t), *L;
    uint32_t i = (uint32_t)(v - t->v), lane;
    if (!K || i >= NVOICE)
        return;
    lane = drum_lane(v->note);
    L = &K[(uint32_t)v->s[0] & (DV_NLANE - 1u)];
    if (L->owner == i + 1u)                              /* this voice played another lane: it stops there */
        L->owner = 0;
    L = &K[lane];
    L->st = DX_GM[dx_gm_ix(v->note)][1];
    L->owner = (uint8_t)(i + 1u);                        /* (its last voice, if another, ends: drumx_amp) */
    v->s[0] = (int32_t)lane;
    v->env_out = v->vel * 258;                           /* the hit starts at its level (no ramp from 0) */
    v->env = 1 << 24;                                    /* the ADSR held at full from here (eng_drum.c: a key-off
                                                          * before the first block must not end a fresh voice) */
    dx_trigger(&L->x);
    if (lane == DV_HATC)                                 /* a closed hat chokes the open one */
        dx_choke(&K[DV_HATO].x);
}

/* the voice's amplitude: the hit, not the ADSR (held at full so a release never ends it). A voice that plays no
 * lane any more, or whose hit has rung out, ends here */
static int32_t drumx_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const drumx_lane_t *L = drumx_lane_of(t, v);
    (void)adsr;
    if (!v->active)                                      /* (taken for another part: env_tick ended it) */
        return 0;
    if (!L || (!L->x.live && !L->x.trig)) {
        v->active = v->gate = 0;
        v->stage = 0;
        v->env = 0;
        return 0;
    }
    v->env = 1 << 24;
    return 32767;
}

static void drumx_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    drumx_lane_t *L = drumx_lane_of(t, v);
    uint32_t lane = (uint32_t)v->s[0] & (DV_NLANE - 1u), i;
    int32_t y[CTL], drv = p[P_E7], g = 0, mk = 0;
    vmod_t ml;
    if (!L)
        return;
    m = vmod_level(m, &ml, clamp(p[P_LN0 + lane], 0, 127));   /* the lane's LEVEL, as DRUM's (dsp.c) */
    if (n > CTL)
        n = CTL;
    dx_run(&dx_kit[lane], &L->x, p[P_E0], L->st * 16 + (p[P_E1] - 64) * 3, p[P_E3] - 64, p[P_E2] - 64, p[P_E4] - 64,
           p[P_E6], p[P_E5], y, n);
    if ((drum_mute[(uint32_t)(t - trk) % NTRK] >> lane) & 1u)
        return;                                          /* ((1.3) a muted lane rings on unheard, as DRUM's) */
    if (drv > 0) {                                       /* DRV: x1..x4 into the soft clip, the level kept (Q12) */
        g = 4096 + drv * 3 * 4096 / 127;
        mk = (int32_t)((19661u << 15) / (uint32_t)softclip((19661 * g) >> 12));
    }
    for (i = 0; i < n; i++) {                            /* x1.25 and the knee below, in 32 bits (as DRUM's) */
        int32_t s = y[i];
        if (g)
            s = (softclip((s * g) >> 12) * mk) >> 15;
        out[i] += voice_amp(soft_knee(s + (s >> 2), 24000), m, i) << 1;
    }
}

/* {MRPH, TUNE, TONE, DECY, NOIS, FM, FOLD, DRV} */
#define DRUMX_E {64, 64, 70, 64, 64, 0, 0, 0}
static const preset_t DRUMX_PRESETS[] = {
    {"OWN KIT", DRUMX_E, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},   /* (the footer: DRUM-X OWN KIT:
                                                                                         * it plays the project's own kit,
                                                                                         * as FM6's SLOT OWN its patch;
                                                                                         * DRUM KIT until phase 9, DRUM's
                                                                                         * name too) */
    /* (Skiff) for the themes: CRYPT (dungeon synth), NEON (synthwave), QUARK (particles) */
    {"HADRON KIT", {100, 72, 90, 40, 80, 70, 60, 25}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 30, 25), TAG(PT_DRUM)},
    /* (Skiff phase 9) more for every engine: basses, kits */
    {"FACTORY KIT", {64, 64, 70, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 20), TAG(PT_DRUM)},
    {"DEEP KIT", {64, 60, 64, 62, 60, 0, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 30), TAG(PT_DRUM)},
    {"CLICK KIT", {64, 64, 70, 64, 64, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 20, 15), TAG(PT_DRUM)},
    {"INDUSTRIAL", {64, 62, 70, 64, 70, 20, 30, 40}, {0, 100, 127, 100}, 0, 0, FX(10, 0, 10, 35), TAG(PT_DRUM)},
    {"SOFT KIT", {64, 64, 56, 60, 56, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 40), TAG(PT_DRUM)},
    {"GLITCH KIT", {64, 64, 70, 60, 64, 30, 20, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 35, 20), TAG(PT_DRUM)},
    {"TRIBAL KIT", {64, 64, 64, 64, 60, 0, 0, 0}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 0, 35), TAG(PT_DRUM)},
    {"LASER KIT", {64, 64, 70, 64, 64, 10, 0, 10}, {0, 100, 127, 100}, 0, 0, FX(0, 0, 30, 25), TAG(PT_DRUM)},
};

/* (Skiff phase 9) the factory kits a preset loads as the project's kit (ui.c apply_preset_to), 8 lanes as dx_kit has
 * them (drumx_voice.c): 0 the factory kit (DX_KIT_INIT, what INIT gives), then the kits of the presets FACTORY KIT on.
 * DRUMX_PKIT[preset]: its kit + 1, 0 none: OWN KIT, the first, loads on every switch to DRUM-X (ui.c
 * set_engine_of) and keeps the kit there is, as HADRON KIT (the kit's macros) */
static const dx_lane_t DX_KITS[][8] = {
    DX_KIT_INIT,
    {   /* DEEP */
        {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {6, 60, 84, 2, 10}, {10, 80, 92, 4, 30}},   /* KICK */
        {DX_MODE(DXW_SINE, DXF_BP, 0), {36, 20, 60, 90, 60}, {40, 28, 70, 100, 70}},   /* SNARE */
        {DX_MODE(DXW_SINE, DXF_BP, 0), {56, 0, 52, 127, 70}, {52, 0, 64, 127, 64}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {92, 0, 18, 100, 104}, {96, 0, 24, 110, 110}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {92, 0, 70, 100, 100}, {96, 0, 84, 110, 106}},   /* HAT OP */
        {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {22, 50, 80, 6, 20}, {28, 70, 92, 10, 40}},   /* TOM */
        {DX_MODE(DXW_BELL, DXF_HP, 1), {58, 8, 16, 10, 40}, {62, 16, 22, 20, 50}},   /* RIM */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {64, 0, 80, 0, 36}, {70, 0, 92, 10, 48}},   /* BELL */
    },
    {   /* CLICK */
        {DX_MODE(DXW_SINE, DXF_LP, 1), {16, 100, 60, 10, 40}, {20, 120, 68, 20, 70}},   /* KICK */
        {DX_MODE(DXW_FM, DXF_HP, 0), {60, 10, 40, 70, 110}, {66, 0, 46, 90, 120}},   /* SNARE */
        {DX_MODE(DXW_SINE, DXF_HP, 0), {70, 0, 30, 127, 100}, {66, 0, 40, 127, 96}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {116, 0, 8, 60, 124}, {120, 0, 12, 80, 127}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {116, 0, 40, 60, 120}, {120, 0, 50, 80, 124}},   /* HAT OP */
        {DX_MODE(DXW_FM, DXF_LP, 1), {40, 60, 36, 0, 50}, {46, 80, 44, 0, 70}},   /* TOM */
        {DX_MODE(DXW_BELL, DXF_HP, 1), {80, 20, 8, 0, 60}, {86, 30, 10, 10, 80}},   /* RIM */
        {DX_MODE(DXW_FM, DXF_BP, 0), {88, 0, 30, 0, 80}, {92, 0, 40, 0, 90}},   /* BELL */
    },
    {   /* INDUSTRIAL */
        {DX_MODE(DXW_FM, DXF_LP, 1) | DX_DRIVE, {10, 90, 70, 20, 70}, {14, 110, 80, 30, 100}},   /* KICK */
        {DX_MODE(DXW_METAL, DXF_BP, 0) | DX_DRIVE, {44, 30, 50, 100, 80}, {50, 40, 60, 110, 90}},   /* SNARE */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {64, 0, 46, 120, 90}, {60, 0, 56, 124, 84}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_HP, 0) | DX_DRIVE, {104, 0, 20, 110, 116}, {110, 0, 28, 120, 122}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {104, 0, 72, 110, 112}, {110, 0, 84, 120, 118}},   /* HAT OP */
        {DX_MODE(DXW_METAL, DXF_LP, 1), {28, 50, 60, 20, 60}, {34, 70, 72, 30, 80}},   /* TOM */
        {DX_MODE(DXW_BELL, DXF_BP, 1) | DX_DRIVE, {70, 20, 20, 30, 70}, {76, 30, 26, 40, 90}},   /* RIM */
        {DX_MODE(DXW_BELL, DXF_BP, 0), {60, 10, 90, 10, 60}, {66, 20, 94, 20, 74}},   /* BELL */
    },
    {   /* SOFT */
        {DX_MODE(DXW_SINE, DXF_LP, 1), {12, 40, 76, 0, 4}, {14, 54, 86, 2, 14}},   /* KICK */
        {DX_MODE(DXW_SINE, DXF_LP, 0), {42, 10, 46, 60, 50}, {44, 16, 56, 70, 56}},   /* SNARE */
        {DX_MODE(DXW_SINE, DXF_LP, 0), {60, 0, 40, 127, 60}, {58, 0, 50, 127, 56}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {90, 0, 16, 100, 90}, {94, 0, 22, 110, 96}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {90, 0, 60, 100, 86}, {94, 0, 70, 110, 92}},   /* HAT OP */
        {DX_MODE(DXW_SINE, DXF_LP, 1), {32, 20, 60, 4, 10}, {36, 30, 70, 8, 20}},   /* TOM */
        {DX_MODE(DXW_SINE, DXF_BP, 1), {66, 4, 12, 20, 40}, {70, 8, 16, 30, 50}},   /* RIM */
        {DX_MODE(DXW_SINE, DXF_BP, 0), {78, 0, 60, 0, 20}, {80, 0, 72, 4, 30}},   /* BELL */
    },
    {   /* GLITCH */
        {DX_MODE(DXW_FM, DXF_LP, 1) | DXM_NOISE << 5, {12, 80, 56, 10, 60}, {18, 110, 64, 20, 90}},   /* KICK */
        {DX_MODE(DXW_FM, DXF_BP, 0) | DXM_NOISE << 5, {50, 60, 40, 90, 100}, {58, 80, 48, 100, 110}},   /* SNARE */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {76, 20, 30, 110, 110}, {70, 30, 40, 120, 104}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_HP, 0) | DXM_NOISE << 5, {110, 30, 10, 80, 120}, {118, 50, 14, 90, 126}},   /* HAT CL */
        {DX_MODE(DXW_BELL, DXF_HP, 0), {108, 20, 44, 80, 116}, {114, 30, 52, 90, 122}},   /* HAT OP */
        {DX_MODE(DXW_FM, DXF_LP, 0) | DXM_NOISE << 5, {36, 90, 40, 10, 80}, {44, 110, 48, 20, 100}},   /* TOM */
        {DX_MODE(DXW_BELL, DXF_HP, 1) | DXM_NOISE << 5, {84, 40, 10, 30, 80}, {90, 60, 14, 40, 100}},   /* RIM */
        {DX_MODE(DXW_FM, DXF_BP, 0) | DXM_SINE << 5, {72, 50, 50, 0, 90}, {80, 70, 60, 10, 100}},   /* BELL */
    },
    {   /* TRIBAL */
        {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {14, 50, 80, 0, 20}, {18, 64, 90, 4, 30}},   /* KICK */
        {DX_MODE(DXW_SINE, DXF_BP, 1), {46, 40, 50, 40, 50}, {50, 50, 58, 50, 60}},   /* SNARE */
        {DX_MODE(DXW_SINE, DXF_BP, 0), {58, 0, 40, 127, 72}, {54, 0, 48, 127, 66}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {96, 0, 14, 110, 100}, {100, 0, 18, 120, 106}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_BP, 0), {96, 0, 40, 110, 96}, {100, 0, 50, 120, 102}},   /* HAT OP */
        {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_LONG << 5, {30, 60, 70, 4, 24}, {36, 80, 80, 8, 34}},   /* TOM */
        {DX_MODE(DXW_BELL, DXF_BP, 1), {74, 10, 18, 4, 50}, {78, 16, 22, 10, 60}},   /* RIM */
        {DX_MODE(DXW_BELL, DXF_BP, 0), {76, 0, 70, 0, 40}, {80, 0, 80, 4, 50}},   /* BELL */
    },
    {   /* LASER */
        {DX_MODE(DXW_SINE, DXF_LP, 1), {10, 110, 70, 0, 30}, {14, 127, 80, 6, 50}},   /* KICK */
        {DX_MODE(DXW_FM, DXF_BP, 0) | DXM_SINE << 5, {50, 70, 50, 80, 90}, {56, 90, 60, 90, 100}},   /* SNARE */
        {DX_MODE(DXW_SINE, DXF_BP, 0), {62, 0, 44, 127, 86}, {58, 0, 54, 127, 80}},   /* CLAP */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {106, 0, 14, 90, 120}, {110, 0, 18, 100, 124}},   /* HAT CL */
        {DX_MODE(DXW_METAL, DXF_HP, 0), {106, 0, 56, 90, 116}, {110, 0, 64, 100, 120}},   /* HAT OP */
        {DX_MODE(DXW_SINE, DXF_LP, 1) | DXM_SINE << 5, {40, 100, 60, 0, 40}, {48, 120, 70, 0, 60}},   /* TOM */
        {DX_MODE(DXW_FM, DXF_HP, 1) | DXM_SINE << 5, {90, 90, 20, 0, 80}, {96, 110, 26, 0, 100}},   /* RIM */
        {DX_MODE(DXW_SINE, DXF_LP, 0) | DXM_SINE << 5, {70, 127, 50, 0, 40}, {80, 127, 60, 0, 60}},   /* BELL */
    },
};
static const uint8_t DRUMX_PKIT[] = {0, 0, 1, 2, 3, 4, 5, 6, 7, 8};
_Static_assert(sizeof DRUMX_PKIT == NELEM(DRUMX_PRESETS), "a kit (or none) per DRUM-X preset");
_Static_assert(NELEM(DRUMX_PRESETS) <= 64u, "its favourites end before THEME CUSTOM's colours (ui.c CPAL_COLS)");
_Static_assert(sizeof DX_KITS[0] == sizeof dx_kit, "a factory kit is a kit");

static const engine_t ENG_DRUMX = {
    .name = "DRUM-X",
    .page_title = {"KIT", "HIT"},
    .edit = {
        {"MORPH", 0, 0, 127, 64, F_PCT, 0},
        {"PITCH", 0, 0, 127, 64, F_ST16, 0},
        {"COLOR", 0, 0, 127, 64, F_PCT, 0},
        {"DECAY", 0, 0, 127, 64, F_PCT, 0},
        {"NOISE", 0, 0, 127, 64, F_PCT, 0},
        {"FM", 0, 0, 127, 0, F_PCT, 0},
        {"FOLD", 0, 0, 127, 0, F_PCT, 0},
        {"DRIVE", 0, 0, 127, 0, F_PCT, 0},
    },
    .presets = DRUMX_PRESETS,
    .npresets = NELEM(DRUMX_PRESETS),
    .note_on = drumx_note_on,
    .render = drumx_render,
    .amp = drumx_amp,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = DV_NLANE,
    .oneshot = 1,
    .keys = drum_keys,
};
