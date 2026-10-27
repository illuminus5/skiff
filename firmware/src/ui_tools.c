/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca 1.2: the SEQ TOOLS layer's actions (ui_layer.c: SEQ held on NOTES / the DRUM grid, PATTERN, FEEL,
 * AUTOMATION). Included by ui_layer.c. (Skiff 1.3) EUCLID, DOUBLE and HALF from Felucca 1.5.5. The white keys from F3
 * (TL_*):
 *   F3 CLEAR  G3 REVERSE  A3 SHIFT <  B3 SHIFT >       the whole sequence
 *   C4 RANDOM D4 COOK     E4 BEAT (DRUM)  F4 EUCLID      new / a little changed (EUCLID: 1.5.5, below)
 *   G4 CLEAR  A4 REVERSE  B4 FILL  C5 RANDOM             the selected lane (DRUM: ui.lane, black keys 1..8 pick it)
 *   D5 DOUBLE E5 HALF                                    LEN x2 / half (1.5.5, below)
 * Each works on the selected track's steps 1..LEN (the steps past LEN stay) and is one undo: SAVE held swaps back
 * the copy taken just before it (ui.c undo, UNDO_PAT; unlike a load the track's automation stays), so after a few
 * COOKs SAVE held takes back the last one only; the layer's OCT- puts back the track as the layer opened (tl_put_back:
 * (2.0) from the undo copy its first change took, ui.c upin).
 * A step that held something and is empty after loses its locks (as EDIT's step clear); REVERSE and SHIFT move the
 * steps' automation and locks with them. RANDOM and COOK draw from libc.c rng(): no seed is kept, each press
 * differs. A sequence of ties reversed keeps its note first: N T T -> N T T at the mirrored place */
enum { TL_CLEAR, TL_REV, TL_LEFT, TL_RIGHT, TL_RANDOM, TL_COOK, TL_BEAT, TL_EUCLID, TL_LCLR, TL_LREV, TL_LFILL, TL_LRND,
       TL_DOUBLE, TL_HALF, TL_N };

static uint32_t tl_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }
static uint32_t tl_rnd(uint32_t n) { return n ? rng() % n : 0u; }
static int tl_gate(const step_t *s) { return s->time == ST_NOTE && s->n && step_ties(s) != (1u << s->n) - 1u; }   /* ((2.0)
                                                    * a note starts there: one whose notes all go on is a tie) */
static uint32_t tl_lane(const step_t *s, uint32_t l) { return (step_lanes(s) >> l) & 1u; }
/* place p's action is there for this track (the lane row and BEAT: DRUM only) */
static int tl_cell(const track_t *t, uint32_t p)
{
    return p < TL_N && (p < TL_BEAT || p == TL_EUCLID || p >= TL_DOUBLE || drum_track(t));
}

/* the undo copy of t as it is now (ui.c load_begin, but the automation is copied and stays): one action, named */
static const char *const TL_UNDO[TL_N] = {"CLEAR", "REVERSE", "SHIFT", "SHIFT", "RANDOM", "COOK", "BEAT", "EUCLID",
    "LANE CLEAR", "LANE REVERSE", "LANE FILL", "LANE RANDOM", "DOUBLE", "HALF"};   /* (Skiff: the undo box) */
static void tl_undo_take(track_t *t, const char *name)
{
    uint32_t i = trk_index(t);
    undo_label = name;
    undo_push(UNDO_PAT);
    undo.trk = (uint8_t)(i + 1u);
    undo.what = UNDO_PAT;
    undo.keep = 0;                                      /* (the next load or action takes its own copy) */
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    memcpy(undo.fm6, fm6_patch[i], FP_SIZE);
    undo.fm6_slot = fm6_slot[i];
    undo.pid = trk_pid[i];
    motion_snapshot_track(t, &undo.motion_backup);
}

/* the layer's OCT- (SEQ TOOLS, REC): t's steps and automation as the undo copy of its first change holds them (the track
 * as the layer opened; (2.0) no copy of its own: ui.c upin keeps that one in the ring), after a copy of now (PUT BACK:
 * SAVE held, the tools again). 0: nothing (undone already); not into another pattern (as an undo: its message) */
static int tl_put_back(track_t *t)
{
    int32_t s = undo_find(upin.pin);
    if (s < 0)
        return 0;
    if (undo_ring[s].pid != trk_pid[trk_index(t)]) {
        undo_other_pattern(undo_ring[s].pid);
        return 0;
    }
    tl_undo_take(t, "PUT BACK");
    s = undo_find(upin.pin);                            /* (it may have moved on in the ring) */
    memcpy(t->step, undo_ring[s].step, sizeof t->step);
    if (motion_replace_track(t, &undo_ring[s].motion_backup))
        motion_full = 1;
    return 1;
}

/* steps 1..len of t that are not empty (a REST); after the action the ones emptied lose their locks */
static uint64_t tl_used_of(const step_t *s, uint32_t len)
{
    uint64_t m = 0;
    uint32_t i;
    for (i = 0; i < len; i++)
        if (s[i].time != ST_REST)
            m |= (uint64_t)1 << i;
    return m;
}
static uint64_t tl_used(const track_t *t, uint32_t len) { return tl_used_of(t->step, len); }
static void tl_unlock(track_t *t, uint64_t was, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (((was >> i) & 1u) && t->step[i].time == ST_REST)
            (void)motion_clear_locks(t, i);
}

/* step j of t becomes the undo copy's step src[j] (taken just before), its automation and locks with it */
static void tl_order(track_t *t, uint32_t len, const uint8_t *src)
{
    uint8_t to[NSTEP];
    uint32_t j, k = trk_index(t), f;
    for (j = 0; j < len; j++) {
        t->step[j] = undo.step[src[j]];
        to[src[j]] = (uint8_t)j;
    }
    f = motion_guard();
    for (j = 0; j < motion.count; j++) {
        motion_event_t *e = &motion.event[j];
        if ((e->place >> 6) == k && (e->place & 63u) < len)
            e->place = (uint16_t)(k << 6 | to[e->place & 63u]);
    }
    motion_unguard(f);
}

/* (2.0, #25) REVERSE with notes that overlap: note by note on the edit copy (ui_input.c ed_open), each starting where
 * it ended, its ties after it, with its step's values; a step's hits mirror. A step's locks and automation go where
 * its first note starts now (a step without a start of its own: the mirrored one; two of one parameter meeting: the
 * one following a note stays). The TIE of step 1 (the loop end's notes) goes. (Not inlined: its two copies, 1.7 KB,
 * on the stack only while it runs, not in ui_input's frame) */
static __attribute__((noinline)) void tl_reverse_notes(track_t *t, uint32_t len)
{
    step_t w[NSTEP], r[NSTEP];
    uint8_t to[NSTEP];
    uint64_t own = 0;
    uint32_t i, j, k, e, n, x, y, had, f, tk = trk_index(t);
    ed_open(w, t, len);
    for (j = 0; j < len; j++) {
        step_clear(&r[j]);
        to[j] = (uint8_t)(len - 1u - j);
    }
    for (j = 0; j < len; j++)
        if (w[j].hit) {
            ed_vals(&r[to[j]], &w[j]);
            r[to[j]].hit = w[j].hit;
            r[to[j]].acc = w[j].acc;
        }
    for (i = 0; i < len; i++)
        for (k = 0; w[i].time != ST_TIE && k < w[i].n; k++) {
            if (step_tied(&w[i], k))
                continue;
            e = ed_end(w, len, i, w[i].note[k]);
            n = len - 1u - e;
            had = (uint32_t)step_starts(&r[n]);
            if (!ed_add(&r[n], w[i].note[k], 0))
                continue;
            if (!had)
                ed_vals(&r[n], &w[i]);
            for (j = n + 1u; j <= len - 1u - i && ed_add(&r[j], w[i].note[k], 1); j++)
                ;
            if (!((own >> i) & 1u)) {
                own |= (uint64_t)1 << i;
                to[i] = (uint8_t)n;
            }
        }
    f = motion_guard();
    motion_restore(t);
    for (x = 0; x < motion.count; x++) {
        motion_event_t *a = &motion.event[x];
        uint32_t pa = a->place & 63u;
        for (y = 0; (a->place >> 6) == tk && pa < len && y < motion.count; y++) {
            motion_event_t *b = &motion.event[y];
            uint32_t pb = b->place & 63u, oa = (uint32_t)(own >> pa) & 1u, ob = (uint32_t)(own >> pb) & 1u;
            if (y != x && (b->place >> 6) == tk && pb < len && pb != pa && to[pb] == to[pa] &&
                MOTION_ID(b) == MOTION_ID(a) && (ob > oa || (ob == oa && y < x)))
                a->place = 511u;
        }
    }
    for (n = 0, x = 0; x < motion.count; x++) {
        motion_event_t ev = motion.event[x];
        if (ev.place == 511u)
            continue;
        if ((ev.place >> 6) == tk && (ev.place & 63u) < len)
            ev.place = (uint16_t)(tk << 6 | to[ev.place & 63u]);
        motion.event[n++] = ev;
    }
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
    ed_close(t, r, len, 0);
}

/* REVERSE: the mirror order; a note's ties (after it) would come before it, so the note goes first again. (2.0) With a
 * per-note tie anywhere in LEN (notes that overlap): note by note (above) */
static void tl_reverse(track_t *t, uint32_t len)
{
    uint8_t src[NSTEP], h;
    uint32_t j, e, x;
    for (j = 0; j < len && !step_ties(&t->step[j]); j++)
        ;
    if (j < len) {
        tl_reverse_notes(t, len);
        return;
    }
    for (j = 0; j < len; j++)
        src[j] = (uint8_t)(len - 1u - j);
    for (j = 0; j < len; j++) {
        for (e = j; e < len && undo.step[src[e]].time == ST_TIE; e++)
            ;
        if (e == j)
            continue;
        if (e < len && undo.step[src[e]].time == ST_NOTE) {   /* ties j .. e - 1, their note at e: the note to j */
            for (h = src[e], x = e; x > j; x--)
                src[x] = src[x - 1u];
            src[j] = h;
        }
        j = e;                                          /* (past the run and its note) */
    }
    tl_order(t, len, src);
}

/* SHIFT: rotated by one step inside LEN, d = 1 later (>), len - 1 earlier (<) */
static void tl_shift(track_t *t, uint32_t len, uint32_t d)
{
    uint8_t src[NSTEP];
    uint32_t j;
    for (j = 0; j < len; j++)
        src[j] = (uint8_t)((j + len - d) % len);
    tl_order(t, len, src);
}

/* ------------------------------------------------------------- drums --- */
/* hits per 16 steps -> per len (at least one when any) */
static uint32_t tl_per(uint32_t k16, uint32_t len) { return !k16 ? 0u : k16 * len < 16u ? 1u : (k16 * len + 8u) / 16u; }
/* k hits spread evenly over len steps (euclidean: the first on step rot; mir: mirrored around it) into lane l */
static void tl_euclid(track_t *t, uint32_t len, uint32_t l, uint32_t k, uint32_t rot, uint32_t mir)
{
    uint32_t i;
    for (i = 0; k && i < len; i++)
        if ((i * k) % len < k)
            grid_hit(t, ((mir ? len - i : i) + rot) % len, l, 1);
}
static void tl_lane_clear(track_t *t, uint32_t len, uint32_t l)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        grid_hit(t, i, l, 0);
}
/* lane l at random, the drum machine way: the kick 3..5 a bar (one on step 1), the snare on beats 2 and 4, the
 * closed hat 8..12, the others sparse (any: they may stay empty, RANDOM; else at least one hit, RANDOM LANE) */
static void tl_lane_rnd(track_t *t, uint32_t len, uint32_t l, int any)
{
    tl_lane_clear(t, len, l);
    if (l == DV_KICK)
        tl_euclid(t, len, l, tl_per(3u + tl_rnd(3), len), 0, tl_rnd(2));
    else if (l == DV_SNARE)
        tl_euclid(t, len, l, tl_per(2, len), 4u % len, 0);
    else if (l == DV_HATC)
        tl_euclid(t, len, l, tl_per(8u + tl_rnd(5), len), tl_rnd(2), 0);
    else if (!any || !tl_rnd(l == DV_HATO ? 2u : 3u))
        tl_euclid(t, len, l, tl_per(1u + tl_rnd(2), len), l == DV_HATO ? (2u + 4u * tl_rnd(4)) % len : tl_rnd(len), 0);
    if (l == DV_SNARE && !tl_rnd(3))
        grid_hit(t, tl_rnd(len), l, 1);                 /* (a ghost snare now and then) */
}

/* the lane hits of steps 1..len (n-th: its step and lane), the kick on step 1 not counted (it stays) */
static uint32_t tl_hit_nth(const track_t *t, uint32_t len, uint32_t n, uint32_t *ip, uint32_t *lp)
{
    uint32_t i, l, c = 0;
    for (i = 0; i < len; i++)
        for (l = 0; l < NLANE; l++)
            if (tl_lane(&t->step[i], l) && (i || l != DV_KICK) && c++ == n) {
                *ip = i;
                *lp = l;
                return 1;
            }
    return c;
}
static uint32_t tl_lane_hits(const track_t *t, uint32_t len, uint32_t l)
{
    uint32_t i, c = 0;
    for (i = 0; i < len; i++)
        c += tl_lane(&t->step[i], l);
    return c;
}
/* COOK on drums: 10..20 % of the hits (at least one) moved a step, or one added on a lane in use, or one taken
 * away (never a lane's last, never the kick on step 1). A try that cannot (the step taken, a lane's last hit) does
 * not count: another one is made (at most a few times as many) */
static uint32_t tl_cook_hit(track_t *t, uint32_t len)
{
    uint32_t i = 0, l = 0, j, r = tl_rnd(4), n = tl_hit_nth(t, len, ~0u, &i, &l);   /* (n: the count) */
    if (r == 2u || !n) {                                /* add: on a lane in use (none: the closed hat) */
        uint32_t used = 0;
        for (j = 0; j < len; j++)
            used |= step_lanes(&t->step[j]);
        for (l = DV_HATC, j = tl_rnd(NLANE); used && j < 2u * NLANE; j++)
            if ((used >> (j % NLANE)) & 1u) {
                l = j % NLANE;
                break;
            }
        j = tl_rnd(len);
        if (tl_lane(&t->step[j], l))
            return 0;
        grid_hit(t, j, l, 1);
        return 1;
    }
    tl_hit_nth(t, len, tl_rnd(n), &i, &l);
    if (r == 3u) {                                      /* take one away */
        if (tl_lane_hits(t, len, l) < 2u)
            return 0;
        grid_hit(t, i, l, 0);
        return 1;
    }
    j = (i + (tl_rnd(2) ? 1u : len - 1u)) % len;        /* move one a step on (or back) */
    if (tl_lane(&t->step[j], l))
        return 0;
    r = (step_accents(&t->step[i]) >> l) & 1u;
    grid_hit(t, i, l, 0);
    grid_hit(t, j, l, 1);
    if (r)
        grid_acc(t, j, l, 1);
    return 1;
}
static void tl_cook_drum(track_t *t, uint32_t len, uint32_t hits)
{
    uint32_t m = hits * (10u + tl_rnd(11)) / 100u, n, sig = steps_sig(t);
    for (m = m ? m : 1u, n = 4u * m + 64u; n; n--) {
        if (tl_cook_hit(t, len) && m)
            m--;
        if (!m && steps_sig(t) != sig)                  /* (a hit moved there and back: one more) */
            break;
    }
}

/* FILL LANE: each press the next of every step, every 2nd, every 4th, none (anything else: every step).
 * Returns the division now (0: cleared) */
static uint32_t tl_fill(track_t *t, uint32_t len, uint32_t l)
{
    static const uint8_t E[3] = {1, 2, 4};
    uint32_t e, i, now = 0;
    for (e = 0; e < 3u && !now; e++) {
        for (i = 0; i < len && tl_lane(&t->step[i], l) == (i % E[e] == 0u); i++)
            ;
        if (i == len)
            now = e + 1u;
    }
    e = now == 3u ? 0u : E[now % 3u];
    for (i = 0; i < len; i++)
        grid_hit(t, i, l, e && i % e == 0u);
    return e;
}

/* ----------------------------------------------------------- melodic --- */
static int32_t tl_in_scale(const track_t *t, int32_t n) { return (int32_t)((scale_mask(t) >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u); }
/* the next note of the scale above (d 1) or below (d -1) n */
static int32_t tl_neigh(const track_t *t, int32_t n, int32_t d)
{
    uint32_t g = 12;
    do
        n += d;
    while (g-- && !tl_in_scale(t, n));
    return clamp(n, 0, 127);
}
/* (1.4.1, a YouTube report: "random notes only fill 16 notes on a 64-step sequence") a 16-step bar after the first, inside
 * LEN, with nothing in it takes the rhythm of the bar before it, its steps whole, so RANDOM's notes fill the whole
 * pattern, not only the bars a shorter one had (LEN turned up leaves the new ones empty) */
static void tl_rhythm_fill(track_t *t, uint32_t len)
{
    uint32_t b, j, used;
    for (b = 16u; b < len; b += 16u) {
        for (used = 0, j = b; j < len && j < b + 16u; j++)
            used |= t->step[j].time == ST_TIE || t->step[j].n || t->step[j].hit;
        for (j = b; !used && j < len && j < b + 16u; j++)
            t->step[j] = t->step[j - 16u];
    }
}
/* RANDOM on a melodic track: the rhythm stays (the NOTE steps, their ties, accents and chance; (1.4.1) an empty bar takes
 * the one before: tl_rhythm_fill), the notes are new: a walk through the track's scale (ROOT, SCL) 9 semitones either
 * side of the pattern's middle (an empty pattern: around the root near C4, on an 8th-note rhythm with some rests). A
 * chord keeps its shape, each note snapped down into the scale */
/* (2.0, #25) the notes starting at step i (its untied ones, bit k of m) to the pitches q[k] at once, each with its
 * per-note ties after it (a TIE holds whatever sounds); 0: two would meet on a step (nothing changed). Without
 * overlaps that is step i alone, as RANDOM and COOK wrote it before 2.0 */
static int tl_chord_to(track_t *t, uint32_t len, uint32_t i, const uint8_t *q)
{
    step_t *s = &t->step[i];
    uint8_t p[4], at[4];
    uint32_t m = ((1u << s->n) - 1u) & ~step_ties(s), pass, alive, mv, j, k, y;
    int32_t x;
    for (k = 0; k < s->n; k++) {
        p[k] = s->note[k];
        at[k] = (uint8_t)k;
        for (y = 0; y < k; y++)                         /* (two onto one pitch) */
            if (((m >> k) & (m >> y) & 1u) && q[k] == q[y])
                return 0;
    }
    for (pass = 0; pass < 2u; pass++)
        for (alive = m, j = i; alive && j < len; j++) {
            step_t *u = &t->step[j];
            if (j > i && u->time == ST_TIE)
                continue;
            for (mv = 0, k = 0; k < s->n; k++)         /* (each one's place here, before any is changed) */
                if ((alive >> k) & 1u) {
                    if (j > i && ((x = note_at(u, p[k])) < 0 || !step_tied(u, (uint32_t)x)))
                        alive &= ~(1u << k);
                    else
                        mv |= 1u << (at[k] = (uint8_t)(j > i ? x : (int32_t)k));
                }
            for (k = 0; k < s->n; k++) {
                if (!((alive >> k) & 1u))
                    continue;
                for (y = 0; !pass && y < u->n; y++)    /* (onto a note staying here) */
                    if (!((mv >> y) & 1u) && u->note[y] == q[k])
                        return 0;
                if (pass)
                    u->note[at[k]] = q[k];
            }
        }
    return 1;
}

static void tl_random_notes(track_t *t, uint32_t len)
{
    uint8_t deg[20];
    int32_t c = 0, lo, d, x;
    uint32_t i, k, g = 0, nd = 0, at;
    tl_rhythm_fill(t, len);
    for (i = 0; i < len; i++)
        if (tl_gate(&t->step[i])) {
            c += t->step[i].note[0];
            g++;
        }
    if (g) {
        c /= (int32_t)g;
    } else {                                            /* no rhythm: 8ths, a few rests, always step 1 */
        c = 60 + t->p[P_ROOT] % 12;
        c -= c > 66 ? 12 : 0;
        for (i = 0; i < len; i++) {
            step_clear(&t->step[i]);
            if (i % 2u == 0u && (!i || tl_rnd(4))) {
                t->step[i].time = ST_NOTE;
                t->step[i].n = 1;
                t->step[i].vel = 96;
            }
        }
    }
    for (lo = clamp(c - 9, 12, 100), x = lo; x <= lo + 18 && nd < sizeof deg; x++)
        if (tl_in_scale(t, x))
            deg[nd++] = (uint8_t)x;
    if (!nd)
        return;
    at = nd / 2u + tl_rnd(5);
    at = at >= 2u ? at - 2u : 0u;
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        uint8_t q[4];
        uint32_t k0;
        if (!tl_gate(s))
            continue;
        at = at >= nd ? nd - 1u : at;
        for (k0 = 0; step_tied(s, k0); k0++)            /* ((2.0) the chord's root: its first note starting here) */
            ;
        d = (int32_t)deg[at] - s->note[k0];
        for (k = 0; k < s->n && k < 4u; k++) {
            x = clamp(s->note[k] + d, 1, 127);
            while (!tl_in_scale(t, x) && x > 1)
                x--;
            q[k] = (uint8_t)(k == k0 ? deg[at] : (uint32_t)x);
        }
        (void)tl_chord_to(t, len, i, q);                /* ((2.0) its ties along; two meeting: as it was) */
        if (!tl_rnd(6)) {                               /* a leap now and then, else a step or two */
            at = tl_rnd(nd);
        } else {
            x = (int32_t)at + (int32_t)tl_rnd(5) - 2;
            at = (uint32_t)(x < 0 ? -x : x >= (int32_t)nd ? 2 * ((int32_t)nd - 1) - x : x);
        }
    }
}

/* COOK on a melodic track: 15..25 % of the NOTE steps (at least one; more when they undid each other): a note to the
 * next scale note up or down (mostly) or an octave, an accent on / off, a chance of 50 / 75 % or back to 100 */
static void tl_cook_notes(track_t *t, uint32_t len, uint32_t g)
{
    uint32_t m = g * (15u + tl_rnd(11)) / 100u, i, k, r, n, sig = steps_sig(t), tries = 64;
    for (m = m ? m : 1u; tries-- && (m || steps_sig(t) == sig); m -= m != 0u) {   /* (undone again: one more) */
        step_t *s;
        n = tl_rnd(g);
        for (i = 0; i < len && (!tl_gate(&t->step[i]) || n--); i++)
            ;
        s = &t->step[i % NSTEP];
        r = tl_rnd(8);
        if (r == 6u) {
            s->flags ^= SF_ACCENT;
        } else if (r == 7u) {
            step_set_chance(s, step_chance(s) < 100u ? 100u : tl_rnd(2) ? 75u : 50u);
        } else {
            int32_t d = tl_rnd(2) ? 1 : -1, o = r == 5u ? d * 12 : 0;
            uint8_t q[4];
            for (k = 0; k < s->n && k < 4u; k++)       /* (an octave out of 24 .. 108: a scale step instead) */
                if (s->note[k] + o < 24 || s->note[k] + o > 108)
                    o = 0;
            for (k = 0; k < s->n && k < 4u; k++)
                q[k] = (uint8_t)(o ? s->note[k] + o : tl_neigh(t, s->note[k], d));
            (void)tl_chord_to(t, len, i % NSTEP, q);    /* ((2.0) its ties along) */
        }
    }
}

/* ------------------------------------------------------------ EUCLID --- */
/* 1.5.5 (#186): F4 EUCLID: HITS spread as evenly as they go over a cycle of STEPS, turned by ROT, repeated through
 * 1..LEN. F4 opens it on the selected track: the undo copy is taken (one undo for all of it) and the pattern is written
 * at once (heard while playing); KNOB 1..3 HITS (0..STEPS) STEPS (1..LEN) ROT (0..STEPS - 1) rewrite it from that copy.
 * OCT+ (or F4 again, another tool, the layer let go) applies it, OCT- cancels it (the steps as they were; the layer's
 * own OCT- after that puts back the track as the layer opened). DRUM: the selected lane's hits (black keys 1..8 move it
 * to another lane), the other lanes and the step's own fields stay. Melodic: a hit keeps the step's own note (a NOTE
 * step as it is: its chord, accent, CHANCE, RATCH ..; a TIE: the note it holds), else the root (ROOT, near C4); the
 * other steps are emptied, but a TIE that holds a note on (its note's ties: the note keeps its length); the emptied
 * steps lose their locks when it is applied. The values stay for the next one (until the power goes). The track's steps
 * as it opened are its own copy (eu_from; the undo copy is the next load's, a MIDI Program Change or the editor's on
 * another track meanwhile), and it stays on the track it opened on */
static struct { uint8_t on, hits, steps, rot, trk; } eu;
static step_t eu_from[NSTEP] __attribute__((section(".pool")));   /* the steps as EUCLID opened (the pool: .bss is tight) */
static track_t *eu_track(void) { return &trk[eu.trk % NTRK]; }
static int eu_hit(uint32_t i)                           /* step i (0..) of the cycle repeated, turned by rot */
{
    uint32_t n = eu.steps ? eu.steps : 1u, p = (i + n - eu.rot % n) % n;
    return (p * eu.hits) % n < eu.hits;
}
/* the note a hit on step i plays: the NOTE step there, or the note a TIE there holds (its NOTE before it), else none */
static const step_t *eu_src(uint32_t i)
{
    uint32_t j = i;
    while (j && eu_from[j].time == ST_TIE)
        j--;
    return eu_from[j].time == ST_NOTE && eu_from[j].n && (j == i || eu_from[i].time == ST_TIE) ? &eu_from[j] : 0;
}
static void eu_render(track_t *t)                       /* (t: ignored, EUCLID's own track) */
{
    t = eu_track();
    uint32_t len = tl_len(t), i, l = ui.lane % NLANE;
    memcpy(t->step, eu_from, sizeof t->step);
    if (drum_track(t)) {
        for (i = 0; i < len; i++)
            grid_hit(t, i, l, eu_hit(i));
        return;
    }
    for (i = 0; i < len; i++) {
        step_t *s = &t->step[i];
        const step_t *o = eu_src(i);
        if (eu_hit(i)) {
            if (o != &eu_from[i]) {                   /* (a REST, an empty step, a TIE: a NOTE of its own) */
                int32_t r = 60 + t->p[P_ROOT] % 12;
                step_clear(s);
                s->time = ST_NOTE;
                if (o)
                    memcpy(s->note, o->note, sizeof s->note), s->n = o->n;
                else
                    s->note[0] = (uint8_t)(r > 66 ? r - 12 : r), s->n = 1;
            } else if (step_tie(s) && !s->hit) {
                s->acc = 0;                           /* ((2.0, #25) notes only going on there: they start there now) */
            }
        } else if (!(step_tie(s) && i && (step_tie(&t->step[i - 1u]) || (t->step[i - 1u].time == ST_NOTE &&
                                                                          t->step[i - 1u].n)))) {
            step_clear(s);
        }
    }
}
static void eu_fit(const track_t *t)                    /* the values inside LEN */
{
    uint32_t len = tl_len(t);
    if (!eu.steps || eu.steps > len)
        eu.steps = (uint8_t)(len < 16u ? len : 16u);
    if (eu.hits > eu.steps)
        eu.hits = eu.steps;
    eu.rot = (uint8_t)(eu.rot % eu.steps);
}
static void eu_say(void)                               /* "EUCLID 5/16" ("+2": turned) */
{
    char b[24];
    str_cpy(b, "EUCLID ", sizeof b);
    fmt_kn(b + 7, eu.hits, eu.steps);
    if (eu.rot) {
        str_cpy(b + str_len(b), " +", sizeof b - str_len(b));
        fmt_int(b + str_len(b), eu.rot);
    }
    ui_message(b);
}
static void eu_open(track_t *t)
{
    tl_undo_take(t, TL_UNDO[TL_EUCLID]);
    memcpy(eu_from, t->step, sizeof eu_from);
    eu.trk = (uint8_t)trk_index(t);
    if (!eu.steps) {                                    /* (the first since the power came: 4 / 16) */
        eu.hits = 4;
        eu.rot = 0;
    }
    eu_fit(t);
    eu.on = 1;
    eu_render(t);
    eu_say();
}
static void eu_knob(track_t *t, uint32_t k, int32_t s)
{
    t = eu_track();
    if (k == 0u)
        eu.hits = (uint8_t)clamp((int32_t)eu.hits + s, 0, eu.steps);
    else if (k == 1u)
        eu.steps = (uint8_t)clamp((int32_t)eu.steps + s, 1, (int32_t)tl_len(t));
    else if (k == 2u)
        eu.rot = (uint8_t)(((int32_t)eu.rot + s % (int32_t)eu.steps + (int32_t)eu.steps) % (int32_t)eu.steps);
    else
        return;
    eu_fit(t);
    eu_render(t);
    eu_say();
}
static void eu_lane(track_t *t)                         /* DRUM: the lane moved (black keys 1..8): on that lane now */
{
    (void)t;
    if (eu.on)
        eu_render(t);
}
/* OCT+ (apply: the emptied steps lose their locks) or OCT- (cancel: the steps of the copy back) */
static void eu_close(track_t *t, int apply)
{
    uint32_t len;
    if (!eu.on)
        return;
    t = eu_track();
    len = tl_len(t);
    eu.on = 0;
    if (apply) {
        tl_unlock(t, tl_used_of(eu_from, len), len);
        return;
    }
    memcpy(t->step, eu_from, sizeof t->step);
}

/* ------------------------------------------------------- DOUBLE, HALF --- */
/* 1.5.5 (#85): D5 DOUBLE: steps 1..LEN once more after them (LEN + 1 .. 2 x LEN, at most the 64 steps: from LEN 33 on
 * only the first 64 - LEN steps fit) and LEN x2, the steps whole (notes, ties, hits, accents, CHANCE / conditions, RATCH,
 * NUDGE, VEL) with their automation and locks (a copy each; what was stored past LEN where the copy goes is replaced).
 * A note tied over the loop's end ties into the copy as it tied into step 1. E5 HALF: LEN / 2 (rounded down), the steps
 * of the second half cleared with their automation and locks (a note tied into it ends at the new LEN). Both one undo
 * (SAVE held: the steps, LEN and the automation back). DOUBLE at 64 steps and HALF at 1 do nothing (said why); DOUBLE
 * with too few of the 128 records left for the copies: AUTOMATION FULL, nothing done */
/* *m = track t's records with steps lo..hi-1 dropped, those of steps 0..cn-1 copied by `by` steps (cn 0: none): the
 * track's records as they will be ((2.0) the caller's, on the stack: was 260 B of the pool); 0 = they do not fit the
 * MOTION_MAX */
static int tl_motion_plan(motion_store_t *m, track_t *t, uint32_t lo, uint32_t hi, uint32_t cn, uint32_t by)
{
    uint32_t k = trk_index(t), i, f, n = 0, ok = 1;
    f = motion_guard();
    memset(m, 0, sizeof *m);
    m->on = motion.on & (uint8_t)(1u << k);
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        uint32_t s = e->place & 63u;
        if ((e->place >> 6) != k || (s >= lo && s < hi))
            continue;
        if (n < MOTION_MAX) m->event[n] = *e;
        n++;
        if (s < cn) {
            if (n < MOTION_MAX) {
                m->event[n] = *e;
                m->event[n].place = (uint16_t)(k << 6 | (s + by));   /* ((Skiff) six tracks: 9 bits) */
            }
            n++;
        }
    }
    if (motion.count - motion_count(t) + n > MOTION_MAX)
        ok = 0;
    m->count = (uint8_t)(n < MOTION_MAX ? n : MOTION_MAX);
    motion_unguard(f);
    return ok;
}
static uint32_t tl_nl(uint32_t len) { return 2u * len < NSTEP ? 2u * len : NSTEP; }   /* DOUBLE's LEN */
/* 0 = it cannot (said why): before the undo copy is taken */
static __attribute__((noinline)) int tl_resize_ok(track_t *t, uint32_t p, uint32_t len)
{
    motion_store_t m;
    if (p == TL_DOUBLE && len >= NSTEP) {
        ui_message("64 STEPS: NO ROOM");
        return 0;
    }
    if (p == TL_HALF && len < 2u) {
        ui_message("1 STEP: NO HALF");
        return 0;
    }
    if (p == TL_DOUBLE && !tl_motion_plan(&m, t, len, tl_nl(len), tl_nl(len) - len, len)) {
        ui_message("AUTOMATION FULL");
        return 0;
    }
    return 1;
}
static __attribute__((noinline)) uint32_t tl_double(track_t *t, uint32_t len)   /* the new LEN */
{
    motion_store_t m;
    uint32_t nl = tl_nl(len), i;
    for (i = len; i < nl; i++)
        t->step[i] = t->step[i - len];
    (void)tl_motion_plan(&m, t, len, nl, nl - len, len);
    (void)motion_replace_track(t, &m);
    t->p[P_SLEN] = (int16_t)nl;
    return nl;
}
static __attribute__((noinline)) uint32_t tl_half(track_t *t, uint32_t len)
{
    motion_store_t m;
    uint32_t h = len / 2u, i;
    for (i = h; i < len; i++)
        step_clear(&t->step[i]);
    (void)tl_motion_plan(&m, t, h, len, 0, 0);
    (void)motion_replace_track(t, &m);
    t->p[P_SLEN] = (int16_t)h;
    return h;
}

/* ------------------------------------------------------------ actions --- */
/* the action of place p on track t (lane l): its message; 0 = nothing done (said why) */
static const char *tl_do(track_t *t, uint32_t p, uint32_t l)
{
    static char b[24];
    uint32_t len = tl_len(t), i, n = 0;
    uint64_t was = tl_used(t, len);
    int drum = drum_track(t);
    const char *msg = 0;
    l %= NLANE;
    if ((p == TL_DOUBLE || p == TL_HALF) && !tl_resize_ok(t, p, len))
        return 0;
    if (p == TL_COOK) {                                 /* (nothing there: nothing to cook, no undo copy) */
        for (i = 0; i < len; i++)
            n += drum ? (uint32_t)__builtin_popcount(step_lanes(&t->step[i])) : (uint32_t)tl_gate(&t->step[i]);
        if (!n) {
            ui_message("NOTHING TO COOK");
            return 0;
        }
    }
    tl_undo_take(t, TL_UNDO[p % TL_N]);
    switch (p) {
    case TL_CLEAR:
        for (i = 0; i < len; i++)
            step_clear(&t->step[i]);
        msg = "SEQUENCE CLEARED";
        break;
    case TL_REV: tl_reverse(t, len); msg = "REVERSED"; break;
    case TL_LEFT: tl_shift(t, len, len - 1u); msg = "SHIFTED LEFT"; break;
    case TL_RIGHT: tl_shift(t, len, 1u); msg = "SHIFTED RIGHT"; break;
    case TL_RANDOM:
        if (drum) {
            for (i = 0; i < len; i++)
                step_clear(&t->step[i]);
            for (i = 0; i < NLANE; i++)
                tl_lane_rnd(t, len, i, 1);
            msg = "RANDOM BEAT";
        } else {
            tl_random_notes(t, len);
            msg = "RANDOM NOTES";
        }
        break;
    case TL_COOK:
        if (drum)
            tl_cook_drum(t, len, n);
        else
            tl_cook_notes(t, len, n);
        msg = "COOKED";
        break;
    case TL_DOUBLE:
    case TL_HALF:
        str_cpy(b, p == TL_DOUBLE ? "DOUBLED: " : "HALVED: ", sizeof b);
        fmt_int(b + str_len(b), (int32_t)(p == TL_DOUBLE ? tl_double(t, len) : tl_half(t, len)));
        str_cpy(b + str_len(b), " STEPS", sizeof b - str_len(b));
        return b;                                       /* (nothing emptied inside the new LEN: no unlock) */
    case TL_BEAT:                                       /* four on the floor, the backbeat, 8th hats */
        for (i = 0; i < len; i++) {
            step_clear(&t->step[i]);
            if (i % 4u == 0u) grid_hit(t, i, DV_KICK, 1);
            if (i % 8u == 4u) grid_hit(t, i, DV_SNARE, 1);
            if (i % 2u == 0u) grid_hit(t, i, DV_HATC, 1);
        }
        msg = "BASIC BEAT";
        break;
    default:                                            /* the lane's */
        str_cpy(b, drum_lane_name(t, l), sizeof b);
        if (p == TL_LCLR) {
            tl_lane_clear(t, len, l);
            msg = " CLEARED";
        } else if (p == TL_LREV) {
            for (i = 0; i < len; i++) {
                const step_t *s = &undo.step[len - 1u - i];
                grid_hit(t, i, l, tl_lane(s, l));
                if (tl_lane(s, l))
                    grid_acc(t, i, l, (step_accents(s) >> l) & 1u);
            }
            msg = " REVERSED";
        } else if (p == TL_LFILL) {
            n = tl_fill(t, len, l);
            msg = n == 1u ? " EVERY STEP" : n == 2u ? " EVERY 2ND" : n ? " EVERY 4TH" : " CLEARED";
        } else {
            tl_lane_rnd(t, len, l, 0);
            msg = " RANDOM";
        }
        str_cpy(b + str_len(b), msg, sizeof b - str_len(b));
        msg = b;
        break;
    }
    tl_unlock(t, was, len);
    return msg;
}
