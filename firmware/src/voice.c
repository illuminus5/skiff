/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering.
 * Runs in the audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]), but only VOICE_BUDGET of all the parts' voices sound at once ((Skiff 1.3) NVOICE, 8; more with the second
 * core running: core.h): a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place, as before. Extra UNISON
 * voices only start when there is room. */
static uint32_t vage;                                   /* voice ages: one clock for every part */
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* the S&H value of a new LFO cycle. Parts 1..3 draw it from the shared generator (rng); part 4 has its
 * own xorshift, so the shared sequence, which also seeds the engines' noise (PHYS, VOICE, ..), does not
 * depend on part 4's LFO (the golden renders rely on it). (#9) The hits (DRUM, DRUM-X) too: their LFO runs free
 * across them, and its cycles must not move the shared sequence the hits' own noise draws from */
#define LFO_SHARED_RNG 3u
static __attribute__((noinline)) uint32_t lfo_rand(track_t *t)   /* ((#9) noinline: a cycle's start only, and kept out of
                                                                   * the ISR's loops, whose layout it moved (+14 %)) */
{
    uint32_t s = t->lfo_rnd ? t->lfo_rnd : 0x9E3779B9u;
    if ((uint32_t)(t - trk) < LFO_SHARED_RNG && !ENGINES[t->engine]->oneshot)
        return rng();
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/* (Skiff 1.3, #16, from Felucca 1.2) LFO 2's SYNC (params.c N_LSYNC): one cycle lasts the note value of the tempo
 * (fx.c div_samples: BPM, or the external clock's measured tempo while it runs), whatever RATE says (the matrix's RATE
 * too): the phase advances 2^32 x CTL / cycle a block (two 32-bit divides; the truncation drifts by less than a
 * millionth of a cycle a cycle). OFF: RATE's table, as before. LSYNC_DIV: each value's N_DIV (div_samples) */
static const uint8_t LSYNC_DIV[11] = {0, 9, 8, 7, 6, 0, 1, 4, 2, 5, 3};
static uint32_t div_samples(uint32_t div);         /* (fx.c) */
static __attribute__((noinline)) uint32_t lfo_sync_inc(const track_t *t)   /* (not inlined: its divides stay out of
                                                                           * the audio ISR's loop over the tracks) */
{
    uint32_t cyc, q, r;
    cyc = div_samples(LSYNC_DIV[(uint32_t)t->p[P_LSYNC] % NELEM(LSYNC_DIV)]);
    cyc = cyc ? cyc : 1u;
    q = 0xFFFFFFFFu / cyc;
    r = 0xFFFFFFFFu % cyc;
    return q * CTL + (r * CTL + CTL) / cyc;
}
static uint32_t lfo_inc(const track_t *t) { return t->p[P_LSYNC] ? lfo_sync_inc(t) : LFO_INC[t->p[P_LRATE] & 127]; }

#if FELUCCA_CPU2
/* (Skiff 1.3, as Felucca 1.6.1) the LFO's block in two: its phase on the track's own core, in track_render (at the
 * RATE the matrix may have moved, mod.c), and the rest after the tracks (fx.c mix_block, track by track as before:
 * lfo_rand's shared generator is drawn in the same order on one core or two). Nothing reads the LFO between the two
 * (track_render and mod_begin read the block before's) */
static void track_lfo_adv(track_t *t)
{
    uint32_t old = t->lfo_ph;
    if (t->lfo_t != 0xFFFFFFFFu)
        t->lfo_t++;
    t->lfo_ph += lfo_inc(t);
    t->lfo_wrap = t->lfo_ph < old;
}
static void track_lfo_end(track_t *t)
{
    if (t->lfo_wrap) {
        t->lfo_wrap = 0;
        t->lfo_prev = (int16_t)mod_sh(t);               /* ((1.3) mod.c SLEW: it glides on from there) */
        t->lfo_rnd = lfo_rand(t);
    }
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->p[P_LPOL])                                   /* (#16) UNI: 0..+1 (the depths reach as far, one way only) */
        t->lfo_val = (t->lfo_val + 32768) >> 1;
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}
#else
static void track_lfo_tick(track_t *t)                  /* (one core: the whole block in track_render, as up to 1.2) */
{
    uint32_t old = t->lfo_ph;
    if (t->lfo_t != 0xFFFFFFFFu)
        t->lfo_t++;
    t->lfo_ph += lfo_inc(t);
    if (t->lfo_ph < old) {
        t->lfo_prev = (int16_t)mod_sh(t);               /* ((1.3) mod.c SLEW: it glides on from there) */
        t->lfo_rnd = lfo_rand(t);
    }
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->p[P_LPOL])                                   /* (#16) UNI: 0..+1 (the depths reach as far, one way only) */
        t->lfo_val = (t->lfo_val + 32768) >> 1;
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}
#endif

/* voices the engine may use (POLY and UNISON): its cap, else all of them */
static uint32_t trk_nvoice(const track_t *t)
{
    uint32_t c = ENGINES[t->engine]->poly;
    return c && c < NVOICE ? c : NVOICE;
}

/* the part's voice mode: P_VOICE, POLY for an engine of hits (engine_t.oneshot: DRUM) */
static uint32_t trk_vmode(const track_t *t)
{
    return ENGINES[t->engine]->oneshot ? V_POLY : (uint32_t)t->p[P_VOICE];
}

/* ------------------------------------------------- the shared voice budget --- */
static uint32_t voices_busy(void)                       /* sounding voices of all parts (not the fading ones) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    return n;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra voices of other non-POLY parts (also ones left after a mode change).
 * Returns its index, *pp its part; NVOICE = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t);
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode != V_POLY && i > 0)
                c = 2;                                  /* extra UNISON, or a voice left by a mode / cap change */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
static void voice_kill(voice_t *v)                      /* fade out over the next block (env_tick stage 4) */
{
    v->gate = 0;
    v->stage = 4;
    voice_kills++;
}

/* the budget is full: free a voice for part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    track_t *vp = 0;
    uint32_t k;
    if (voices_busy() < VOICE_BUDGET)
        return 1;
    k = voice_victim(t, soft, &vp);
    if (k == NVOICE)
        return !soft;                                   /* hard: over the budget (cannot happen: each of the
                                                         * NPART < NVOICE parts protects one voice at most) */
    voice_kill(&vp->v[k]);
    return 1;
}

static voice_t *voice_reuse(track_t *t, voice_t *v)
{
    if (v->stage == 4u)
        voice_room(t, 0);                               /* its budget was taken before this block */
    return v;
}

/* POLY allocation (within the engine's voice cap). DRUM: reuse its lane; other engines: the same note.
 * Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
    if (t->engine == ENGI_DRUM || t->engine == ENGI_DRUMX) {
        voice_t *v = t->engine == ENGI_DRUM ? drum_reuse(t, note) : drumx_reuse(t, note);
        if (v)
            return voice_reuse(t, v);
    }
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note)
            return voice_reuse(t, &t->v[i]);
        nfree += !t->v[i].active;
    }
    if (nfree && voices_busy() >= VOICE_BUDGET) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t)
            return &t->v[k];                            /* our own: restart it in place (no click) */
        if (k < NVOICE)
            voice_kill(&vp->v[k]);
    }
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    return voice_reuse(t, &t->v[best == np ? 0 : best]);
}

/* glide: RATE = a fixed speed, TIME = the same time for any interval */
static void glide_set(track_t *t, voice_t *v, int glide)
{
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE]) {
        int32_t d = v->pitch16 - v->pitch_cur, ticks = 1 + t->p[P_GLIDE] * t->p[P_GLIDE] / 8;
        d = d < 0 ? -d : d;
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    uint32_t keep = e->keep;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->stage2 = 1;                                      /* ((Skiff phase 9) ENV 2: its attack, from where it is) */
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    v->mvel = t->m_vel;                                 /* mod.c: the note's VEL and RAND */
    v->mrnd = t->m_rnd;
    t->m_vi = (uint8_t)(v - t->v);
    glide_set(t, v, glide);
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
        v->env2 = 0;
        v->side = t->sp_alt;                            /* ((1.3) SPREAD: left, right, left .. (UNISON: mono_play); a */
        t->sp_alt ^= 1u;                                /* sounding voice keeps its side: no jump) */
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !e->sampled && !(t->engine == ENGI_DRUM && drum_user(t->p))) {   /* retrigger / steal: keep */
        v->ph[0] = ph0;                                 /* phases and filter states (resetting them clicks); ((#35) a
                                                         * DRUM user kit's sample starts again: its declick, drum_note_on) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (keep & 0x01u)                               /* engine_t.keep: the slots the engines use */
            v->s[0] = s0;
        if (keep & 0x02u)
            v->s[1] = s1;
        if (keep & 0x10u)
            v->s[4] = s4;
        if (keep & 0x20u)
            v->s[5] = s5;
        if (keep & 0x40u)
            v->s[6] = s6;
        if (keep & 0x80u)
            v->s[7] = s7;
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (eight for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_nvoice(t) : 1u, i;
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv < NVOICE)
            k = k * (NVOICE - 1) / (int32_t)(nv - 1u);      /* fewer voices: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NVOICE ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            v->side = (uint8_t)(i & 1u);                    /* ((1.3) SPREAD: the detuned voices alternate) */
            if (nv > 1u && i && !ENGINES[t->engine]->sampled) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            v->mvel = t->m_vel;
            v->mrnd = t->m_rnd;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

static void duck_note(const track_t *t, uint32_t note);   /* (Skiff) duck.c (fx.c) */
/* a fresh phrase restarts the LFO and its fade; (#9) not on the hits (DRUM, DRUM-X): every hit restarted it, so each
 * heard the same LFO value; it runs free across them (noinline: off the ISR's loops, as lfo_rand). (#16) LFO 2's TRIG
 * FREE: the phase runs on (FADE still starts again); NOTE: from PHASE, as before */
static __attribute__((noinline)) void lfo_restart(track_t *t)
{
    if (ENGINES[t->engine]->oneshot)
        return;
    if (!t->p[P_LTRIG])
        t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    t->lfo_fade = 0;
    t->lfo_t = 0;
}
static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = trk_vmode(t);
    if (t->p[P_MUTE])
        return;
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    duck_note(t, note);                                 /* (Skiff) a hit another track may duck under */
    t->hits++;                                          /* ((Skiff Q6) QUARK's event display) */
    vel = mod_note(t, note, vel);                       /* the matrix's VEL / KEY / RAND ((1.3) VEL as a destination) */
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any)                                           /* fresh phrase: LFO retrigger and fade */
        lfo_restart(t);
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0 && !ENGINES[t->engine]->oneshot);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= 8u)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = trk_vmode(t);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    mono_remove(t, note);                              /* a mode change must not retain a released key */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* a fallback above already has its new pitch */
        if (t->v[i].note == note && t->v[i].gate) {
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
        t->v[i].stage = t->v[i].active ? 3 : 0;
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = v->stage2 = 0;
            v->env = v->env_out = v->env2 = 0;
        }
        dc_reset_audio((uint32_t)(t-trk));
        t->engine = eng_idx(t->eng_req);
        memset(eng_part(t), 0, eng_size(t));          /* (Skiff) the part's shared engine area, the new engine's now: from
                                                       * zero, as at power-on (engines.c eng_mem; a light track's eng_lite) */
        t->xf_on = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
/* (Skiff 1.3, from Felucca 1.5's #175) ENV SYNC (P_ESYNC, params.c ESYNC_NAMES): ATK DEC REL as note values of the
 * tempo (BPM, or the external clock's measured tempo: fx.c beat_samples, as LFO 2 SYNC). The part's three steps
 * (t->esync_k), made once a block before its voices (track_render), as gen_tables.py makes ENV_LIN / ENV_EXP for a time
 * in ms: the attack's linear step 2^24 CTL / T (rounded), the decay's and release's 1 - e^-x (x = 4.6 CTL / T: ~99 % of
 * the way after T), here as x - x^2/2 + x^3/6 - x^4/24 (x <= 0.33 at the shortest, 1/64T at 240 BPM: under 0.04 % off).
 * The coefficient is Q16 as ENV_EXP's: past ~2 s it is a few units, the time within half of one (4BAR at 90 BPM:
 * 2.5 %). Value 0 (and a time below a block) takes the tables' 0. OFF: the tables' values, as before (looked up once a
 * block here instead of once a voice in env_tick: the same numbers) */
static uint32_t beat_samples(void);                /* (fx.c) */
_Static_assert(CTL == 32, "esync_one: 4.6 CTL in Q16 is 9646899");
static uint32_t esync_one(uint32_t quarter, int32_t v, uint32_t i)
{
    uint32_t u = ESYNC_UNITS[esync_idx(v)], T = quarter * u / 96u, x, x2, x3, x4;   /* T: samples */
    if (T < CTL)
        return i ? ENV_EXP[0] : ENV_LIN[0];
    if (!i)
        return ((1u << 24) * CTL + T / 2u) / T;
    x = (9646899u + T / 2u) / T;                        /* 4.6 CTL (CTL 32), Q16, rounded */
    if (x > 49152u)                                     /* (below ~200 samples: 0.75, the series 0.3 % low) */
        x = 49152u;
    x2 = x * x >> 16;
    x3 = x2 * x >> 16;
    x4 = x3 * x >> 16;
    x = x - x2 / 2u + x3 / 6u - x4 / 24u;
    return x < 1u ? 1u : x > 65535u ? 65535u : x;
}
static __attribute__((noinline)) void esync_coef(track_t *t)   /* (not inlined: its divides stay out of the
                                                                    * audio ISR's loop over the voices) */
{
    const int16_t *p = t->p;
    if (p[P_ESYNC]) {
        uint32_t q = beat_samples();
        t->esync_k[0] = esync_one(q, p[P_ATK], 0);
        t->esync_k[1] = esync_one(q, p[P_DEC], 1);
        t->esync_k[2] = esync_one(q, p[P_REL], 2);
    } else {                                            /* OFF: the tables, as always */
        t->esync_k[0] = ENV_LIN[p[P_ATK] & 127];
        t->esync_k[1] = ENV_EXP[p[P_DEC] & 127];
        t->esync_k[2] = ENV_EXP[p[P_REL] & 127];
    }
}

static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)t->esync_k[0];               /* ATK's step ((1.3) esync_coef: the table's, or ENV SYNC's) */
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, t->esync_k[1]);
        break;
    case 3:
        v->env -= mulq16(v->env, t->esync_k[2]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: this block fades it */
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* (Skiff phase 9) one control tick of ENV 2 (P_EG2_ATK .. REL): the amplitude envelope's twin, a MOD source only (mod.c
 * MS_ENV2), ticked while a slot uses it; its release starts when the gate goes (where the amp envelope's is set), its end
 * ends nothing */
static __attribute__((noinline)) void env2_tick(track_t *t, voice_t *v)   /* (out of line: the ISR's loops stay as they were) */
{
    const int16_t *p = t->p;
    if (!v->gate && v->stage2 && v->stage2 < 3u)
        v->stage2 = 3;
    switch (v->stage2) {
    case 1:
        v->env2 += (int32_t)ENV_LIN[p[P_EG2_ATK] & 127];
        if (v->env2 >= (1 << 24)) {
            v->env2 = 1 << 24;
            v->stage2 = 2;
        }
        break;
    case 2:
        v->env2 += mulq16(((int32_t)p[P_EG2_SUS] << 17) - v->env2, ENV_EXP[p[P_EG2_DEC] & 127]);
        break;
    case 3:
        v->env2 -= mulq16(v->env2, ENV_EXP[p[P_EG2_REL] & 127]);
        if (v->env2 < (1 << 12)) {
            v->env2 = 0;
            v->stage2 = 0;
        }
        break;
    default:
        v->env2 = 0;
        break;
    }
}

/* render one block of a part into out (cleared here); returns the voices rendered */
/* Channel bend is live performance state, outside projects/presets. Q8 semitones. */
static int32_t midi_bend_q8[NTRK], midi_bend_target[NTRK];
/* (Skiff 1.3, from Felucca's #148) SPREAD (fx.c mix_spread): while t->spread_on (mix_part sets it for the part it
 * renders), track_render renders the voices of side 0 into the part's spread_buf and the others into out, then adds
 * spread_buf to out: out is every voice, as always (the engines add into either the same way), spread_buf the left
 * ones. A cost per block, none per voice */
static int32_t spread_buf[C1_NP][CTL];
#define SPREAD_BUF(t) spread_buf[C1_PI(t)]
static __attribute__((noinline)) void spread_clear(int32_t *sb)   /* (not inlined: the loops stay out of the ISR's) */
{
    uint32_t i;
    for (i = 0; i < CTL; i++)
        sb[i] = 0;
}
static __attribute__((noinline)) void spread_sum(int32_t *out, const int32_t *sb, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        out[i] += sb[i];
}
/* (2.0, #36) DRUM's and DRUM-X's lane pans (fx.c mix_spread), while t->lpan_on (a lane of the kit off C): a panned
 * lane's voice (its lane in s[0], both engines) is rendered into a block of its own, added to out (every lane: the mono
 * kit, as before) and, |PAN| / 64 of it, to the sum its channel's far side loses: a lane panned right to spread_buf
 * (taken off the left), one panned left to lpan_buf (taken off the right). That is the track's PAN law lane by lane:
 * the far side down, the near one as it is. A cost per panned lane's voice; the lanes at C as ever */
static int32_t lpan_buf[C1_NP][CTL];
#define LPAN_BUF(t) lpan_buf[C1_PI(t)]
static int lanes_panned(const track_t *t)
{
    const int16_t *p = &t->p[P_LPAN0];
    return (p[0] | p[1] | p[2] | p[3] | p[4] | p[5] | p[6] | p[7]) != 0;
}
static __attribute__((noinline)) void dc_render(track_t *t,voice_t *v,int32_t *b,uint32_t n,const vmod_t *m)
{
    uint32_t lane=(uint32_t)v->s[0]&7u;
    ENGINES[t->engine]->render(t,v,b,n,m);
    dc_process(t,lane,b,n);
    int32_t level=clamp(t->p[P_LN0+lane],0,127);
    if(level<127){int32_t gain=level*level*2+(level*level>>6);for(uint32_t i=0;i<n;i++)b[i]=(int32_t)((int64_t)b[i]*gain/32768);}
}
static __attribute__((noinline)) void dc_voice_add(track_t *t,voice_t *v,int32_t *out,uint32_t n,const vmod_t *m)
{
    int32_t b[CTL];spread_clear(b);dc_render(t,v,b,n,m);
    for(uint32_t i=0;i<n;i++)out[i]+=b[i];
}
static __attribute__((noinline)) void lpan_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int32_t vb[CTL], pan = t->p[P_LPAN0 + ((uint32_t)v->s[0] & 7u)], w = pan < 0 ? -pan : pan;
    int32_t *sd = pan > 0 ? SPREAD_BUF(t) : LPAN_BUF(t);
    uint32_t i;
    n = n > CTL ? CTL : n;
    spread_clear(vb);
    if(dc_on(t,(uint32_t)v->s[0]&7u))dc_render(t,v,vb,n,m);
    else ENGINES[t->engine]->render(t, v, vb, n, m);
    for (i = 0; i < n; i++) {                           /* (|vb| < 2^17, w <= 64: no overflow) */
        out[i] += vb[i];
        sd[i] += (vb[i] * w) >> 6;
    }
}
/* (Skiff 1.3.2) LFO DEST PITCH: depth d (-64..63) squared, so its low values are a fine vibrato: at a full LFO
 * d|d| x 0.762 / 16 semitone, d 63 the +-11.8 semitones it always reached, 8 +-19 cents (1 up to 1.3.1: one step was
 * +-19 cents, a clear vibrato at "1 %"). In 1/4096 semitone, then whole 1/16 semitones and the rest as TUNE's fine
 * factor: a smooth wobble, no 6-cent stairs */
static void lfo_pitch(int32_t lfo, int32_t d, int32_t *p16, int32_t *fine)
{
    int32_t o = (lfo * ((d * (d < 0 ? -d : d) * 780) >> 6)) >> 15;   /* (|lfo| x 49920 < 2^31) */
    *p16 = o >> 8;
    *fine = ((o & 255) * 3787 + 32768) >> 16;       /* 1/4096 semitone -> 1/4096 of the rate (1 ct = 2.367), to the
                                                     * nearest (1 is then 0.42 ct, not 0) */
}
static void smx_render(track_t *t, voice_t *v, uint32_t k, int32_t *dst, uint32_t n, const vmod_t *m);   /* (fx.c) */
/* Keep voice/lane rendering out of the mix/sends loop: one bounded render call per track. */
static __attribute__((noinline)) uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    if(drum_engine(t->engine))drum_comp_state[(uint32_t)(t-trk)].clock+=n;
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    int32_t bend, bend16, bend_fine;
    uint32_t ti = (uint32_t)(t - trk);
    if (drum_engine(eng_idx(t->eng_req))) {          /* ((Skiff) DRUM-X too) */
        midi_bend_q8[ti] = midi_bend_target[ti] = 0;
    } else {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    bend16 = bend >> 4;                                 /* (floor(bend / 16), as it was written with a divide: ((1.3) a
                                                         * shift, no slow path for the ISR budget's static count) */
    bend_fine = (bend - bend16 * 16) * 2367 / 2560;
    int32_t lpit, lfine;                                /* (Skiff 1.3.2) LFO DEST PITCH: lfo_pitch */
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
    int32_t *sb = SPREAD_BUF(t);
    for (i = 0; i < n; i++)
        out[i] = 0;
    if (t->spread_on | t->lpan_on)
        spread_clear(sb);
    if (t->lpan_on)
        spread_clear(LPAN_BUF(t));
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
#if FELUCCA_CPU2
    track_lfo_adv(t);                                   /* ((Skiff 1.3) the rest of its block after the tracks: fx.c) */
#else
    track_lfo_tick(t);
#endif
    if (MOD(t).on && MOD(t).ldep < 32767)               /* ((1.3) the matrix's DEPTH, mod.c) */
        lfo = mulq15(lfo, MOD(t).ldep);
    lfo_pitch(lfo, p[P_LD_PIT], &lpit, &lfine);         /* ((Skiff 1.3.2) the vibrato: one for every voice) */
    esync_coef(t);                                      /* ((1.3) this block's envelope steps for env_tick: ENV SYNC) */
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                if (e->done(t, v)) {
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = v->env_out = 0;
                    continue;
                }
                v->env = 1 << 24;                       /* (the ADSR's release never ends it) */
                env = 32767;
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15(p[P_LPOL] ? lfo : (lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16) {               /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            int32_t st = v->gstep ? v->gstep : (int32_t)(ENV_LIN[(p[P_GLIDE] ? p[P_GLIDE] : 40) & 127] >> 14);   /* slide: ~45 ms */
            if (st < 1)
                st = 1;
            v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
        if (!env && !m.amp0 && v->stage == 2 && !e->sampled)
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        pitch = v->pitch_cur + tune + bend16 + lpit + ((m.envq15 * p[P_ED_PIT] * 3) >> 15);
        m.pitch16 = clamp(pitch, 0, 2047);
        m.inc = pitch_inc(m.pitch16);
        m.fine = v->fine + tune_fine + bend_fine + lfine;
        if (m.fine)                                     /* residual below 1/16 semitone */
            m.inc += (uint32_t)((int32_t)(m.inc >> 12) * m.fine);
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
        if (MOD(t).on) {                                   /* the modulation matrix (mod.c) */
            if (MOD(t).env2)                               /* ((Skiff phase 9) a slot's ENV2) */
                env2_tick(t, v);
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
        }
        if (t->lpan_on && t->p[P_LPAN0 + ((uint32_t)v->s[0] & 7u)])   /* ((2.0, #36) a lane off C: its share of the
                                                                      * sides (lpan_render) */
            lpan_render(t, v, out, n, &m);
        else if(drum_engine(t->engine)&&dc_on(t,(uint32_t)v->s[0]&7u))
            dc_voice_add(t,v,t->spread_on>v->side?sb:out,n,&m);
        else if (t->engine == ENGI_SAMPLE && v->s[4] >= 0x8000)   /* ((Skiff, #21) a user sample's sound: its LEVEL and
                                                                * sends, fx.c) */
            smx_render(t, v, ti, t->spread_on > v->side ? sb : out, n, &m);
        else
            e->render(t, v, t->spread_on > v->side ? sb : out, n, &m);   /* ((1.3) SPREAD: the left ones) */
        nr++;
    }
    if (t->spread_on)
        spread_sum(out, sb, n);                         /* (out: every voice; spread_buf: the left ones) */
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
