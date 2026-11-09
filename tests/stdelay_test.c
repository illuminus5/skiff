/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (2.5) The DELAY's MODE (src/fx.c, G_DMODE: MONO, STEREO, PING) and the chorus in stereo, through hostsim.c as
 * reverb_test.c.
 *   build/host/stdelay_test [DEMODIR]          (run_tests.sh: build/fx_demo)
 * 1. MONO bit for bit as 2.2: fx_buses against a copy of 2.2's buses (the chorus in each MODE, with FEEDBACK, then the
 *    delay), 20 s of noise sends with the settings moving; no stereo side. (The goldens say the same for the mix.)
 * 2. STEREO keeps an echo on its side: a burst on the left send echoes on the left only (the right within 2 LSB of
 *    silence), the right likewise; a centred one alike on both, as loud as MONO's (1 dB) and as late (3 samples).
 * 3. PING: a centred burst's echoes cross, left, right, left, right (the other side within 2 LSB each time).
 * 4. FEEDBACK: each STEREO echo the feedback's share of the one before (2 dB).
 * 5. The corners: FEEDBACK 120, COLOR 127, 2 s of full-scale noise on both sends in STEREO and PING: bounded, then
 *    exact silence 7 s after (the loop's products round towards 0: no offset held round the lines).
 * 6. A MODE change while the echoes ring: no step (the old return fades out), then nothing of them; the new MODE
 *    running after 33 blocks.
 * 7. The chorus: STEREO's two taps differ (a side), as loud as each other; MONO's has none.
 * 8. The sends: a track panned hard left sends nothing right in STEREO, a centred one the same both sides; MONO's
 *    right send stays empty. THROW (the FX layer, perform.c perf_pre) left to left, right to right.
 * Demos (WAV) into DEMODIR: two plucks panned left and right through the delay in MONO, STEREO and PING; a pad
 * through the chorus in MONO and STEREO. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("stdelay: %-96s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}
static uint32_t xs = 0x2545F491u;
static int32_t noise(int32_t amp)
{
    xs ^= xs << 13;
    xs ^= xs >> 17;
    xs ^= xs << 5;
    return (int32_t)(((int64_t)(int32_t)xs * amp) >> 31);
}

/* ------------------------------------------------------------ 2.2's buses --- */
static int16_t ref_dly[DLY_LEN], ref_cho[CHO_LEN];
static struct { uint32_t dly_w, cho_w, cho_ph; int32_t dly_lp, cho_y, ph_s[4]; } rf;
static void ref_buses(const int32_t *cho_in, const int32_t *dly_in, int32_t *wet, uint32_t n)
{
    uint32_t i, k, mode = (uint32_t)song.g[G_CMODE], dl = delay_samples();
    int32_t fb = song.g[G_CFDBK] * 240, depth = song.g[G_CDEPTH];
    int32_t dfb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240, dmix = song.g[G_DMIX] * 258;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    if (mode == 2u) {
        int32_t lfo = (osc_sine(rf.cho_ph) + 32768) >> 1, a;
        rf.cho_ph += cinc * n;
        a = -31130 + mulq15(lfo, 3000 + depth * 205);
        for (i = 0; i < n; i++) {
            int32_t x = (cho_in[i] >> 1) + mulq15(rf.cho_y, fb);
            for (k = 0; k < 4u; k++) {
                int32_t w = clamp(x - mulq15(rf.ph_s[k], a), -262144, 262144);
                x = mulq15(w, a) + rf.ph_s[k];
                rf.ph_s[k] = w;
            }
            rf.cho_y = clamp(x, -65536, 65536);
            wet[i] = rf.cho_y << 1;
        }
    } else if (mode || fb) {
        for (i = 0; i < n; i++) {
            int32_t r, f, c0, c1;
            uint32_t ri;
            ref_cho[rf.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp((cho_in[i] >> 1) + mulq15(rf.cho_y, fb), -32768, 32767);
            rf.cho_ph += cinc;
            r = mode == 1u ? (44 << 8) + ((osc_sine(rf.cho_ph) + 32768) * depth >> 7)
                           : (400 << 8) + ((osc_sine(rf.cho_ph) + 32768) * depth * 6 >> 8);
            ri = (uint32_t)r >> 8;
            f = r & 255;
            c0 = ref_cho[(rf.cho_w - ri) & (CHO_LEN - 1u)];
            c1 = ref_cho[(rf.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            rf.cho_y = c0 + (((c1 - c0) * f) >> 8);
            rf.cho_w++;
            wet[i] = rf.cho_y << 1;
        }
    } else {
        int32_t cdepth = depth * 6;
        for (i = 0; i < n; i++) {
            int32_t r;
            ref_cho[rf.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
            rf.cho_ph += cinc;
            r = (400 << 8) + ((osc_sine(rf.cho_ph) + 32768) * cdepth >> 8);
            {
                uint32_t ri = (uint32_t)r >> 8;
                int32_t f = r & 255, c0 = ref_cho[(rf.cho_w - ri) & (CHO_LEN - 1u)];
                int32_t c1 = ref_cho[(rf.cho_w - ri - 1u) & (CHO_LEN - 1u)];
                wet[i] = (c0 + (((c1 - c0) * f) >> 8)) << 1;
            }
            rf.cho_w++;
        }
    }
    for (i = 0; i < n; i++) {
        int32_t x = ref_dly[(rf.dly_w - dl) & (DLY_LEN - 1u)];
        rf.dly_lp += mulq15(x - rf.dly_lp, col);
        ref_dly[rf.dly_w & (DLY_LEN - 1u)] = (int16_t)clamp((dly_in[i] >> 1) + mulq15(rf.dly_lp, dfb), -32768, 32767);
        rf.dly_w++;
        wet[i] += mulq15(x << 1, dmix);
    }
}

/* ------------------------------------------------------------- helpers --- */
static int32_t zero[CTL];
static void fresh(int16_t mode)                          /* the buses silent, the delay's settings set, MODE running */
{
    static int32_t w[CTL];
    uint32_t k;
    host_tracks_init();
    rev_clear();
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(&fx, 0, sizeof fx);
    song.g[G_BPM] = 120;
    song.g[G_DTIME] = 1;                                 /* 1/8: 11028 samples */
    song.g[G_DFDBK] = 60;
    song.g[G_DCOLOR] = 127;
    song.g[G_DMIX] = 127;
    song.g[G_CMODE] = song.g[G_CFDBK] = 0;
    song.g[G_DMODE] = mode;
    for (k = 0; k < 40u && (fx.dmode != (uint8_t)mode || fx.dclr); k++)
        fx_buses(zero, zero, zero, zero, w, CTL);
}
#define RUN (9u * FS / CTL * CTL)
static int32_t L[RUN], R[RUN];
/* the buses with chorus send c (or none) and the delay's sends dl, dr (per sample, from 0) for n samples: L, R */
static void run(const int32_t *c, const int32_t *dl, const int32_t *dr, uint32_t len, uint32_t n)
{
    static int32_t cb[CTL], db[CTL], eb[CTL], w[CTL];
    uint32_t t, i;
    for (t = 0; t < n; t += CTL) {
        for (i = 0; i < CTL; i++) {
            cb[i] = c && t + i < len ? c[t + i] : 0;
            db[i] = dl && t + i < len ? dl[t + i] : 0;
            eb[i] = dr && t + i < len ? dr[t + i] : 0;
        }
        fx_buses(cb, db, eb, zero, w, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t s = hl.side ? rev_side[i] : 0;
            L[t + i] = w[i] + s;
            R[t + i] = w[i] - s;
        }
    }
}
#define BURST 882u                                       /* 20 ms */
static int32_t burst[BURST];
static void burst_make(void)                             /* 300 Hz under a Hann window: the half rate keeps it whole */
{
    uint32_t i;
    for (i = 0; i < BURST; i++)
        burst[i] = (int32_t)(20000.0 * sin(2 * M_PI * 300.0 * i / FS) * (0.5 - 0.5 * cos(2 * M_PI * i / BURST)));
}
static double energy(const int32_t *x, uint32_t a, uint32_t b)
{
    double e = 0;
    for (uint32_t i = a; i < b && i < RUN; i++)
        e += (double)x[i] * x[i];
    return e;
}
static int32_t peak_abs(const int32_t *x, uint32_t a, uint32_t b)
{
    int32_t m = 0;
    for (uint32_t i = a; i < b && i < RUN; i++)
        m = abs(x[i]) > m ? abs(x[i]) : m;
    return m;
}
static uint32_t peak_at(const int32_t *x, uint32_t a, uint32_t b)   /* where the envelope (|x| over 64) is largest */
{
    uint32_t i, best = a;
    double bm = -1;
    for (i = a; i + 64u < b && i + 64u < RUN; i++) {
        double e = energy(x, i, i + 64u);
        if (e > bm)
            bm = e, best = i;
    }
    return best;
}
static double db(double a, double b) { return 10.0 * log10((a + 1e-9) / (b + 1e-9)); }

/* ------------------------------------------------------------- the tests --- */
static void test_mono_identical(void)
{
    static int32_t c[CTL], d[CTL], w0[CTL], w1[CTL];
    uint32_t b, i, diff = 0, side = 0, nb = 20u * FS / CTL;
    fresh(DM_MONO);
    memset(ref_dly, 0, sizeof ref_dly);
    memset(ref_cho, 0, sizeof ref_cho);
    memset(&rf, 0, sizeof rf);
    for (b = 0; b < nb; b++) {
        if (b % 600u == 0u) {                            /* the settings move now and then: every chorus MODE too */
            song.g[G_CMODE] = (int16_t)((b / 600u) % 3u);
            song.g[G_CFDBK] = (int16_t)((b / 1800u) % 2u ? 90 : 0);
            song.g[G_CDEPTH] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
            song.g[G_DTIME] = (int16_t)((uint32_t)noise(1 << 30) % 13u);
            song.g[G_DFDBK] = (int16_t)((uint32_t)noise(1 << 30) % 121u);
            song.g[G_DCOLOR] = (int16_t)((uint32_t)noise(1 << 30) % 128u);
        }
        for (i = 0; i < CTL; i++) {
            int32_t on = (b / 300u) % 3u != 2u;
            c[i] = on ? noise(60000) : 0;
            d[i] = on ? noise(60000) : 0;
        }
        fx_buses(c, d, d, zero, w0, CTL);
        ref_buses(c, d, w1, CTL);
        for (i = 0; i < CTL; i++)
            diff += w0[i] != w1[i];
        side += hl.side;
    }
    check("MONO: the chorus (each MODE, FEEDBACK) and the delay bit for bit as 2.2, 20 s of noise; no side", !diff && !side);
}

static void test_sides(void)
{
    static int32_t lm[RUN];
    uint32_t D = delay_samples(), e1;
    int32_t rmax, lmax;
    double em, es;
    char what[160];
    burst_make();
    fresh(DM_MONO);
    D = delay_samples();
    run(0, burst, burst, BURST, 2u * D + 4000u);
    memcpy(lm, L, sizeof(int32_t) * (2u * D + 4000u));
    em = energy(lm, D - 300u, D + BURST + 300u);
    e1 = peak_at(lm, D - 300u, D + BURST + 300u);
    fresh(DM_STEREO);
    run(0, burst, 0, BURST, 2u * D + 4000u);
    rmax = peak_abs(R, 0, 2u * D + 4000u);
    lmax = peak_abs(L, D - 300u, D + BURST + 300u);
    snprintf(what, sizeof what, "STEREO: a burst sent left echoes left (peak %d), the right silent (%d)", lmax, rmax);
    check(what, rmax <= 2 && lmax > 4000);
    fresh(DM_STEREO);
    run(0, 0, burst, BURST, 2u * D + 4000u);
    rmax = peak_abs(R, D - 300u, D + BURST + 300u);
    lmax = peak_abs(L, 0, 2u * D + 4000u);
    snprintf(what, sizeof what, "  .. sent right: right (peak %d), the left silent (%d)", rmax, lmax);
    check(what, lmax <= 2 && rmax > 4000);
    fresh(DM_STEREO);
    run(0, burst, burst, BURST, 2u * D + 4000u);
    {
        uint32_t i, diff = 0, e2 = peak_at(L, D - 300u, D + BURST + 300u);
        for (i = 0; i < 2u * D + 4000u; i++)
            diff = abs(L[i] - R[i]) > (int32_t)diff ? (uint32_t)abs(L[i] - R[i]) : diff;
        es = energy(L, D - 300u, D + BURST + 300u);
        snprintf(what, sizeof what, "  .. sent both: left = right (%u LSB at most), %.2f dB against MONO's echo, %d samples "
                 "later", diff, db(es, em), (int)e2 - (int)e1);
        check(what, diff <= 2 && fabs(db(es, em)) <= 1.0 && abs((int)e2 - (int)e1) <= 3);
    }
}

static void test_ping(void)
{
    uint32_t D, k, ok = 1;
    char what[200];
    int n = 0;
    fresh(DM_PING);
    D = delay_samples();
    song.g[G_DFDBK] = 100;
    run(0, burst, burst, BURST, 5u * D + 2000u);
    for (k = 1; k <= 4u; k++) {
        uint32_t a = k * D - 300u, b = k * D + BURST + 600u;
        int32_t on = peak_abs(k & 1u ? L : R, a, b), off = peak_abs(k & 1u ? R : L, a, b);
        ok &= on > 1000 && off <= 2;
        n += snprintf(what + n, sizeof what - (size_t)n, " %s %d/%d", k & 1u ? "L" : "R", on, off);
    }
    check("PING: the echoes cross (side: peak / the other side's):" , ok);
    printf("stdelay:  %s\n", what);
}

static void test_feedback(void)
{
    uint32_t D;
    double e1, e2, want;
    char what[160];
    fresh(DM_STEREO);
    D = delay_samples();
    song.g[G_DFDBK] = 60;
    run(0, burst, burst, BURST, 3u * D + 2000u);
    e1 = energy(L, D - 300u, D + BURST + 600u);
    e2 = energy(L, 2u * D - 300u, 2u * D + BURST + 600u);
    want = 20.0 * log10(60.0 * 230.0 / 32768.0);
    snprintf(what, sizeof what, "STEREO FEEDBACK 60: the second echo %.2f dB under the first (the feedback: %.2f)",
             db(e1, e2), -want);
    check(what, fabs(db(e2, e1) - want) <= 2.0);
}

static void test_corners(void)
{
    static int32_t nz[2u * FS], nr[2u * FS];
    uint32_t i, m;
    for (i = 0; i < 2u * FS; i++) {
        nz[i] = noise(65536);
        nr[i] = noise(65536);
    }
    for (m = DM_STEREO; m <= DM_PING; m++) {
        int32_t pk, tail;
        char what[160];
        fresh((int16_t)m);
        song.g[G_DFDBK] = 120;
        song.g[G_DCOLOR] = 127;
        song.g[G_DTIME] = 3;                             /* 1/32: many trips round */
        run(0, nz, nr, 2u * FS, RUN);
        pk = peak_abs(L, 0, RUN) > peak_abs(R, 0, RUN) ? peak_abs(L, 0, RUN) : peak_abs(R, 0, RUN);
        tail = peak_abs(L, RUN - FS / 4u, RUN) + peak_abs(R, RUN - FS / 4u, RUN);
        snprintf(what, sizeof what, "%s at FEEDBACK 120: 2 s of full-scale noise, peak %d; 7 s after %d (exact silence)",
                 m == DM_PING ? "PING" : "STEREO", pk, tail);
        check(what, pk < (1 << 19) && tail == 0);
    }
}

static void test_switch(void)
{
    static const int16_t SEQ[3][2] = {{DM_STEREO, DM_MONO}, {DM_MONO, DM_PING}, {DM_PING, DM_STEREO}};
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        static int32_t sl[RUN], sr[RUN];
        uint32_t i, D, at, n;
        int32_t step = 0, own = 0, after;
        char what[200];
        fresh(SEQ[k][0]);
        D = delay_samples();
        song.g[G_DFDBK] = 110;
        for (i = 0; i < BURST * 4u; i++)
            sl[i] = sr[i] = burst[i % BURST];
        at = 2u * D + 2u * BURST;                        /* in the second echo, while it rings */
        at -= at % CTL;
        run(0, sl, sr, BURST * 4u, at);
        n = at;
        {
            int32_t pl = L[at - 1u], pr = R[at - 1u];   /* (both sides: PING's second echo is the right's) */
            song.g[G_DMODE] = SEQ[k][1];
            run(0, 0, 0, 0, 40u * CTL);                  /* the change: one block's fade, then the clearing */
            for (i = 0; i < 2u * CTL; i++) {
                int32_t s = abs(L[i] - pl) > abs(R[i] - pr) ? abs(L[i] - pl) : abs(R[i] - pr);
                step = s > step ? s : step;
                pl = L[i];
                pr = R[i];
            }
            (void)n;
        }
        fresh(SEQ[k][0]);                                /* the signal's own steps, the same run without the change */
        song.g[G_DFDBK] = 110;
        run(0, sl, sr, BURST * 4u, at + 2u * CTL);
        for (i = at - 4u * CTL; i < at + 2u * CTL; i++) {
            int32_t s = abs(L[i] - L[i - 1u]) > abs(R[i] - R[i - 1u]) ? abs(L[i] - L[i - 1u]) : abs(R[i] - R[i - 1u]);
            own = s > own ? s : own;
        }
        song.g[G_DMODE] = SEQ[k][1];                     /* .. and after it: silent until a new send */
        run(0, 0, 0, 0, 3u * FS);
        after = peak_abs(L, 40u * CTL, 3u * FS) + peak_abs(R, 40u * CTL, 3u * FS);
        snprintf(what, sizeof what, "MODE %s -> %s while the echoes ring: largest step %d (their own %d), nothing after %d, "
                 "running: %u", N_DMODE[SEQ[k][0]], N_DMODE[SEQ[k][1]], step, own, after, fx.dmode);
        check(what, step <= 2 * own + 64 && after == 0 && fx.dmode == (uint8_t)SEQ[k][1] && !fx.dclr);
    }
}

static void test_chorus(void)
{
    static int32_t cn[4u * FS];
    uint32_t i, diff = 0;
    double el, er;
    char what[160];
    for (i = 0; i < 4u * FS; i++)
        cn[i] = noise(40000);
    fresh(DM_STEREO);
    song.g[G_CDEPTH] = 100;
    run(cn, 0, 0, 4u * FS, 4u * FS);
    for (i = FS; i < 4u * FS; i++)
        diff += L[i] != R[i];
    el = energy(L, FS, 4u * FS);
    er = energy(R, FS, 4u * FS);
    snprintf(what, sizeof what, "the chorus in STEREO: the two taps differ (%u of %u samples), left %.2f dB against right",
             diff, 3u * FS, db(el, er));
    check(what, diff > 2u * FS && fabs(db(el, er)) <= 1.0);
    fresh(DM_STEREO);
    song.g[G_CMODE] = 1;                                 /* FLANGER, with FEEDBACK */
    song.g[G_CFDBK] = 80;
    run(cn, 0, 0, 4u * FS, 2u * FS);
    for (i = FS, diff = 0; i < 2u * FS; i++)
        diff += L[i] != R[i];
    check("  .. FLANGER with FEEDBACK: stereo too", diff > FS / 2u);
    fresh(DM_MONO);
    run(cn, 0, 0, 4u * FS, FS);
    for (i = 0, diff = 0; i < FS; i++)
        diff += L[i] != R[i];
    check("  .. in MONO: none (left = right)", !diff && !hl.side);
}

static void test_sends(void)
{
    int32_t o[2 * CTL], ml[CTL], mr[CTL], sd[CTL], se[CTL], sr[CTL];
    uint32_t b, i, pan;
    static const int8_t PAN[3] = {-64, 0, 63};
    for (pan = 0; pan < 3u; pan++) {
        double ed = 0, ee = 0;
        uint32_t same = 1;
        fresh(DM_STEREO);
        host_preset(&trk[0], 0, 5);                      /* ANALOG SINE KEY */
        trk[0].p[P_DLY] = 100;
        trk[0].p[P_PAN] = PAN[pan];
        trk_note_on(&trk[0], 60, 110);
        for (b = 0; b < 40u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                ed += (double)send_d[i] * send_d[i];
                ee += (double)send_e[i] * send_e[i];
                same &= send_d[i] == send_e[i];
            }
        }
        if (pan == 0u)
            check("STEREO's sends: a track panned hard left sends nothing right", ed > 0 && ee == 0);
        else if (pan == 1u)
            check("  .. centred: the same both sides", ed > 0 && same);
        else
            check("  .. hard right: the left within the dry's leak (2 %)", ee > 0 && ed <= ee * 0.0004 + 1);
    }
    {
        double ed = 0, ee = 0;
        fresh(DM_MONO);
        host_preset(&trk[0], 0, 5);
        trk[0].p[P_DLY] = 100;
        trk[0].p[P_PAN] = -64;
        trk_note_on(&trk[0], 60, 110);
        for (b = 0; b < 40u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < CTL; i++) {
                ed += (double)send_d[i] * send_d[i];
                ee += (double)send_e[i] * send_e[i];
            }
        }
        check("  .. MONO: everything in the one send, the right one empty", ed > 0 && ee == 0);
    }
    for (i = 0; i < CTL; i++) {                          /* THROW: the left mix only */
        ml[i] = 10000;
        mr[i] = 0;
        sd[i] = se[i] = sr[i] = 0;
    }
    perf_k[2] = 100;
    for (b = 0; b < 200u; b++)
        perf_pre(ml, mr, sd, se, sr, CTL);
    {
        uint32_t ok = 1;
        for (i = 0; i < CTL; i++)
            ok &= sd[i] != 0 && se[i] == 0;
        check("THROW into a stereo delay: the left mix left, nothing right", ok);
    }
    perf_k[2] = 0;
    for (b = 0; b < 200u; b++)
        perf_pre(ml, mr, sd, se, sr, CTL);
}

/* ------------------------------------------------------------- demos --- */
static void demo(const char *dir, const char *name, int16_t mode, int chorus)
{
    static const uint8_t A[16] = {64, 0, 0, 0, 67, 0, 0, 0, 71, 0, 0, 0, 69, 0, 0, 0};
    static const uint8_t B[16] = {0, 0, 72, 0, 0, 0, 76, 0, 0, 0, 74, 0, 0, 0, 79, 0};
    static const uint8_t CH[3] = {60, 64, 67};
    char path[512];
    FILE *f;
    uint32_t t, i, frames = 10u * FS;
    int32_t o[2 * CTL];
    memset(trk, 0, sizeof trk);
    fresh(mode);
    song.g[G_BPM] = 110;
    song.g[G_DTIME] = 10;                                /* 1/8D */
    song.g[G_DFDBK] = 55;
    song.g[G_DCOLOR] = 70;
    song.g[G_DMIX] = 100;
    song.g[G_CDEPTH] = 80;
    if (chorus) {
        host_preset(&trk[0], 0, 1);                      /* ANALOG SOFT PAD, a chord held through the chorus */
        trk[0].p[P_CHOR] = 110;
        trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    } else {
        host_preset(&trk[0], 0, 8);                      /* ANALOG SYNTH PLUCK, left */
        host_preset(&trk[1], 0, 8);                      /* .. and another, right */
        for (i = 0; i < 16u; i++) {
            put_step(&trk[0], i, A[i] ? 1u : 0u, &A[i], A[i] ? ST_NOTE : ST_REST, 0);
            put_step(&trk[1], i, B[i] ? 1u : 0u, &B[i], B[i] ? ST_NOTE : ST_REST, 0);
        }
        trk[0].p[P_PAN] = -48;
        trk[1].p[P_PAN] = 48;
        trk[0].p[P_DLY] = trk[1].p[P_DLY] = 90;
        trk[0].p[P_REV] = trk[1].p[P_REV] = 20;
        trk[0].p[P_CHOR] = trk[1].p[P_CHOR] = 0;
    }
    snprintf(path, sizeof path, "%s/%s.wav", dir, name);
    if (!(f = fopen(path, "wb")))
        return;
    wav_hdr(f, frames);
    if (chorus)
        for (i = 0; i < 3u; i++)
            trk_note_on(&trk[0], CH[i], 100);
    else
        transport_req = 1;
    for (t = 0; t < frames; t += CTL) {
        if (t == 7u * FS / CTL * CTL) {                  /* the last 3 s: the echoes, the tail alone */
            if (chorus)
                for (i = 0; i < 3u; i++)
                    trk_note_off(&trk[0], CH[i]);
            else
                transport_req = 2;
        }
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    printf("stdelay: demo %s\n", path);
}

int main(int argc, char **argv)
{
    test_mono_identical();
    test_sides();
    test_ping();
    test_feedback();
    test_corners();
    test_switch();
    test_chorus();
    test_sends();
    if (argc > 1) {
        demo(argv[1], "delay_mono", DM_MONO, 0);
        demo(argv[1], "delay_stereo", DM_STEREO, 0);
        demo(argv[1], "delay_ping", DM_PING, 0);
        demo(argv[1], "chorus_mono", DM_MONO, 1);
        demo(argv[1], "chorus_stereo", DM_STEREO, 1);
    }
    printf("%s\n", bad ? "STDELAY TEST FAILED" : "stdelay test passed");
    return bad != 0;
}
