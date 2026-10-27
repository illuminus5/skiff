/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: throws (DESIGN.md, the PERFORM layer's KNOB 1..4). A knob turned in the PERFORM layer pushes one of the
 * selected track's parameters (throw_id: the B mockup's CUT VERB DLY DRV) away from its value for a moment: an offset
 * over it, never saved, that glides back to nothing over one bar once the knob rests (THROW_REST ms), gone at STOP.
 * ((2.0, D 2A) one way back: over MENU > CONTROL > RETURN's time, as MOMENTARY's values and FX's macros (ui.c mom_poll
 * sets thr_beats); OCT+ in PERFORM keeps them: throw_keep. The offsets stay here, in the ISR, over what sounds: a
 * throw rides on automation and locks.)
 * The audio ISR puts the offsets on at the start of each block, before its events (a note starting reads them: ATK,
 * REL ..), and takes them off after the parts are mixed: the main loop never sees them (what it shows, saves and edits
 * is the parameter's own value). A value something else wrote during the block (automation, a lock) stays. The glide
 * is worked out each block. Included by fx.c; mix_block calls throw_on / throw_off while one is out */
#define THROW_REST 120u                                 /* ms the knob rests before its throw glides back */
#define THR_ONE 65536                                   /* an offset of 1 (fine enough that a bar's glide is a bar) */
static struct {
    int32_t off, dec;                                   /* the offset, x THR_ONE; how much of it a block takes (0: still) */
    uint32_t t;                                         /* fm1_ms of its knob's last turn */
    int16_t keep, put;                                  /* this block: the value before, the one put on */
    uint8_t id;                                         /* the parameter (P_*) */
} thr[NTRK * 4u];
static volatile uint32_t thr_out;                       /* bit 4k + c: track k's throw c is out */
static uint32_t thr_on;                                 /* .. put on this block (throw_off takes these off) */
static volatile uint8_t thr_beats = 4u;                 /* main: the way back's beats (RETURN: 0 SNAP, 1, 4) */
_Static_assert(NTRK * 4u <= 32u, "a throw a bit");

/* the start of a block (mix_block, before the events): each throw on over its parameter, gliding once its knob rests */
static __attribute__((noinline)) void throw_on(uint32_t n)
{
    uint32_t i, m = thr_out, on = 0, now = fm1_ms, len = thr_beats * beat_samples();
    for (i = 0; i < NTRK * 4u; i++) {
        track_t *t = &trk[i >> 2];
        const param_desc_t *d;
        if (!((m >> i) & 1u))
            continue;
        if (now - thr[i].t > THROW_REST) {               /* back over RETURN's time, in a line (SNAP, len 0: a step */
            int32_t a = thr[i].off < 0 ? -thr[i].off : thr[i].off;   /* past it, at once) */
            if (!thr[i].dec)
                thr[i].dec = (int32_t)((uint32_t)a * n / (len ? len : 1u)) + 1;
            a = a > thr[i].dec ? a - thr[i].dec : 0;
            thr[i].off = thr[i].off < 0 ? -a : a;
            if (!a) {
                thr_out &= ~(1u << i);
                continue;
            }
        }
        d = param_desc_of(eng_idx(t->eng_req), thr[i].id);
        thr[i].keep = t->p[thr[i].id];
        thr[i].put = (int16_t)clamp(thr[i].keep + thr[i].off / THR_ONE, d->min, d->max);
        t->p[thr[i].id] = thr[i].put;
        on |= 1u << i;
    }
    thr_on = on;
}
/* the parts mixed: the throws off again (a value written meanwhile kept) */
static __attribute__((noinline)) void throw_off(void)
{
    uint32_t i, m = thr_on;
    for (i = 0; m; i++, m >>= 1)
        if ((m & 1u) && trk[i >> 2].p[thr[i].id] == thr[i].put)
            trk[i >> 2].p[thr[i].id] = thr[i].keep;
    thr_on = 0;
}

/* the parameter KNOB c throws, as the B mockup has them: KNOB 1 the sound's tone, its first HOME parameter where that is
 * one (CUT on most; (Skiff U3, the review's V2) where it picks a sample, a source or a wave the engine's filter-like one
 * instead, THR_TONE: SAMPLE's CUT, LOFI's TONE, WHEEL's TOP, GRAIN's TONE, SLICE's TONE), then the reverb, delay and
 * drive sends, the same on every engine */
static const uint8_t THR_SEND[3] = {P_REV, P_DLY, P_DIST};
static const uint8_t THR_TONE[NENGINES] = {[3] = P_E7, [ENGI_SAMPLE] = P_E4, [7] = P_E3, [ENGI_GRAIN] = P_E7,
                                           [ENGI_SLICE] = P_E7};   /* (3 LOFI, 7 WHEEL; 0: the HOME KNOB 1) */
static uint32_t throw_id(const track_t *t, uint32_t c)
{
    uint32_t e = t->eng_req % NENGINES;
    return (c & 3u) ? THR_SEND[(c & 3u) - 1u] : THR_TONE[e] ? THR_TONE[e] : ENGINES[e]->knob[0];
}
/* (the main loop) the PERFORM layer's KNOB c turned s: track k's parameter c thrown further (from its own value; its
 * range the limit); its glide from when the knob rests */
static void throw_turn(uint32_t k, uint32_t c, int32_t s)
{
    track_t *t = &trk[k % NTRK];
    uint32_t i = (k % NTRK) * 4u + (c & 3u), id = throw_id(t, c);
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    int32_t o;
#if defined(FM1_IRQ_TARGET)                             /* (the ISR not in between: as motion.c motion_guard) */
    uint32_t f = fm1_icfg();
    fm1_irq_off();
#endif
    if (!((thr_out >> i) & 1u) || thr[i].id != id) {
        thr[i].off = 0;
        thr[i].id = (uint8_t)id;
    }
    o = clamp(thr[i].off / THR_ONE + s, d->min - t->p[id], d->max - t->p[id]);
    thr[i].off = o * THR_ONE;
    thr[i].t = fm1_ms;
    thr[i].dec = 0;
    if (o)
        thr_out |= 1u << i;
    else
        thr_out &= ~(1u << i);
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#endif
}
/* (2.0, D 2A; the main loop) OCT+ in PERFORM: every throw out kept, its value as it sounds now the parameter's own (as
 * MOMENTARY's OCT+: not recorded, no undo); 1: some */
static int throw_keep(void)
{
    uint32_t i, m;
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
#endif
    m = thr_out;
    for (i = 0; i < NTRK * 4u; i++)
        if ((m >> i) & 1u) {
            track_t *t = &trk[i >> 2];
            const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), thr[i].id);
            t->p[thr[i].id] = (int16_t)clamp(t->p[thr[i].id] + thr[i].off / THR_ONE, d->min, d->max);
        }
    thr_out = 0;
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#endif
    return m != 0u;
}
/* track k's thrown parameter c as it sounds now (its own value and its throw); *out: thrown */
static int32_t throw_value(uint32_t k, uint32_t c, int *out)
{
    const track_t *t = &trk[k % NTRK];
    uint32_t i = (k % NTRK) * 4u + (c & 3u), id = throw_id(t, c);
    const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id);
    int32_t v = t->p[id];
    *out = ((thr_out >> i) & 1u) && thr[i].id == id;
    return *out ? clamp(v + thr[i].off / THR_ONE, d->min, d->max) : v;
}
