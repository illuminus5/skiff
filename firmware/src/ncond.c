/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff (#92): per-note trig conditions (core.h NC_*). Each track's in ncs: the audio ISR reads them as a step fires
 * (nc_fails), the main loop writes them with the audio IRQ off. An edit of NOTES keeps one where its note still is
 * (nc_keep; PITCH: the note there, transposed), the steps a tool moves whole lose theirs (nc_drop), and one left on no
 * note goes (nc_prune).
 * Included by seq.c: in the pool, off the ISR's merged globals */
static ncond_store_t ncs[NTRK] __attribute__((section(".pool")));
/* the step track k fired last: what of it does not play, its conditions not met (bit g target g: note g, NC_LANE + l
 * lane l): its RATCH parts and FLAM read it again (seq_step) */
static uint16_t nc_skip[NTRK] __attribute__((section(".pool")));
/* (ui_input.c sel_pitch) the next ed_close transposes: a note's condition stays on the note in its place */
static uint8_t nc_repitch __attribute__((section(".pool")));

/* step s's targets (core.h NC_TG): bit g its note g starting there (not one going on), bit NC_LANE + l its lane l hit */
static uint32_t nc_mask(const step_t *s)
{
    if (s->time == ST_TIE)
        return 0;
    return (((1u << (s->n & 7u)) - 1u) & 15u & ~step_ties(s)) | (uint32_t)s->hit << NC_LANE;
}
/* (the audio ISR) step idx (s) of track t on this pass (tc_ok's ahead): nc_skip its notes and hits whose conditions
 * fail; 1: none of it plays. Not inlined: only a track with any calls it */
static __attribute__((noinline)) int nc_fails(const track_t *t, const step_t *s, uint32_t idx, uint32_t ahead)
{
    uint32_t k = trk_index(t), i, m = 0, own = nc_mask(s);
    const ncond_store_t *c = &ncs[k];
    for (i = 0; i < c->n && i < NC_MAX; i++)
        if (c->e[i].at == idx && !tc_ok(t, nc_cond(&c->e[i]), ahead))
            m |= 1u << nc_tgt(&c->e[i]);
    nc_skip[k] = (uint16_t)m;
    return own && (m & own) == own;                     /* (every note starting on it and every hit: none plays) */
}

/* (the audio ISR, a switch of patterns: launch_swap) the two stores change places, byte by byte: off its stack. Not
 * inlined: only a switch with any calls it */
static __attribute__((noinline)) void nc_swap(ncond_store_t *x, ncond_store_t *y)
{
    uint8_t *a = (uint8_t *)x, *b = (uint8_t *)y, v;
    uint32_t i;
    for (i = 0; i < NC_BYTES; i++) {
        v = a[i];
        a[i] = b[i];
        b[i] = v;
    }
}

/* ---- the main loop's ---- */
static void nc_put(const track_t *t, const ncond_store_t *c)
{
    uint32_t f = motion_guard();
    ncs[trk_index(t)] = *c;
    motion_unguard(f);
}
/* target g is on step s: a note starting there (not one going on), or the hit of a lane */
static int nc_on(const step_t *s, uint32_t g) { return (int)((nc_mask(s) >> g) & 1u); }
/* store c without what no firmware wrote and, on steps lo .. hi - 1, what is on no note of steps st (none: all there go;
 * old: the steps before an edit, each a note's moved to where its pitch starts now, or after PITCH kept in its place): 1
 * if any went.
 * The one filter: a note deleted, a hit cleared, steps moved whole, an edit of NOTES (ui_input.c ed_close) */
static int nc_keep(ncond_store_t *c, const step_t *st, const step_t *old, uint32_t lo, uint32_t hi)
{
    uint32_t i, n = 0, was = c->n, at, g, h;
    for (i = 0; i < was && i < NC_MAX; i++) {
        at = c->e[i].at;
        g = nc_tgt(&c->e[i]);
        if (at >= NSTEP || nc_cond(&c->e[i]) - 1u >= TC_COUNT - 1u)
            continue;
        if (at >= lo && at < hi) {
            if (!st)
                continue;
            if (old && g < NC_LANE && !nc_repitch) {    /* (a chord's note: to where its pitch starts now) */
                for (h = 0; h < NC_LANE && !(nc_on(&st[at], h) && st[at].note[h] == old[at].note[g]); h++)
                    ;
                if (h == NC_LANE)
                    continue;
                g = h;
            }
            if (!nc_on(&st[at], g))
                continue;
        }
        c->e[n].tg = NC_TG(g, nc_cond(&c->e[i]));
        c->e[n++].at = (uint8_t)at;
    }
    c->n = (uint8_t)n;
    return n != was;
}
/* .. what is on no note of steps st (from lim on: not looked at) */
static int nc_fit(ncond_store_t *c, const step_t *st, uint32_t lim) { return nc_keep(c, st, 0, 0, lim); }
/* the index of target g of step at in c, c->n: none */
static uint32_t nc_find(const ncond_store_t *c, uint32_t at, uint32_t g)
{
    uint32_t i;
    for (i = 0; i < c->n && !(c->e[i].at == at && nc_tgt(&c->e[i]) == g); i++)
        ;
    return i;
}
/* the condition of target g (core.h NC_TG) on step at of t, TC_NONE: none */
static uint32_t nc_get(const track_t *t, uint32_t at, uint32_t g)
{
    const ncond_store_t *c = &ncs[trk_index(t)];
    uint32_t i = nc_find(c, at, g);
    return i < c->n ? nc_cond(&c->e[i]) : TC_NONE;
}
/* .. set to cond (TC_NONE: none): 0 done, 1 no room (NC_MAX) */
static int nc_set(const track_t *t, uint32_t at, uint32_t g, uint32_t cond)
{
    ncond_store_t *c = &ncs[trk_index(t)];
    uint32_t i = nc_find(c, at, g), f;
    if (cond != TC_NONE && cond < TC_COUNT && i == NC_MAX)
        return 1;
    f = motion_guard();                                 /* (in place, the audio IRQ off) */
    if (cond == TC_NONE || cond >= TC_COUNT) {
        if (i < c->n)
            c->e[i] = c->e[--c->n];
    } else {
        c->e[i].at = (uint8_t)at;
        c->e[i].tg = NC_TG(g, cond);
        c->n += i == c->n;
    }
    motion_unguard(f);
    return 0;
}
/* t's without what is on no note now */
static void nc_prune(const track_t *t)
{
    uint32_t f = motion_guard();
    (void)nc_keep(&ncs[trk_index(t)], t->step, 0, 0, NSTEP);
    motion_unguard(f);
}
/* t's on steps lo .. hi - 1 go (steps moved there whole: SHIFT, REVERSE, START; DOUBLE's copy) */
static void nc_drop(const track_t *t, uint32_t lo, uint32_t hi)
{
    uint32_t f = motion_guard();
    (void)nc_keep(&ncs[trk_index(t)], 0, 0, lo, hi);
    motion_unguard(f);
}
