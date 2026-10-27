/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of Skiff's FX > DRIVE (phase 9; firmware/src/fx.c track_dist: DIST's TYPE and TONE). A 110 Hz saw at
 * -6 dBFS, 1 s, straight into one track's DIST insert at DRIVE 40, 80 and 127:
 * 1. every TYPE (SOFT HARD FOLD CRUSH FUZZ) under full scale, no DC (under 2 % of its peak), its RMS within 9 dB of
 *    SOFT's at the same drive (each its own make-up), and another sound than SOFT's (not the same samples);
 * 2. HARD flat at its ceiling, FOLD brighter than SOFT (bright(): the RMS of the steps over the RMS), CRUSH held: runs of
 *    8 equal samples before the tone at DRIVE 127 (the low-pass smooths them after);
 * 3. TONE: +63 brighter than -64 for every TYPE; 0 is SOFT as it was (the same samples as the tone left alone:
 *    DIST_TONE moves nothing at 0); DRIVE 0 passes the signal unchanged whatever the TYPE;
 * 4. (2.0, D 2A) HARD FOLD FUZZ are INSERT's shapers of the name (ins_shape) in DIST's own low cut, DC blocker, tone and
 *    make-up, bit for bit; CRUSH is BITS as it was.
 * Built and run by tests/run_tests.sh. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int check(const char *what, int ok)
{
    printf("drive: %-76s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

#define N (1378u * CTL)                          /* 1 s, whole blocks */
static int32_t in[N], out[N];

static void run(int32_t type, int32_t drive, int32_t tone)
{
    track_t *t = &trk[0];
    uint32_t i;
    t->dist_hp = t->dist_lp1 = t->dist_lp2 = t->dist_dc = 0;
    t->p[P_DIST] = (int16_t)drive;
    t->p[P_DTYPE] = (int16_t)type;
    t->p[P_DTONE] = (int16_t)tone;
    memcpy(out, in, sizeof out);
    for (i = 0; i < N; i += CTL)
        track_dist(t, out + i, CTL);
}
static double rms(void)
{
    double s = 0;
    for (uint32_t i = N / 10u; i < N; i++)
        s += (double)out[i] * out[i];
    return sqrt(s / (N - N / 10u));
}
static int32_t peak(void)
{
    int32_t p = 0;
    for (uint32_t i = 0; i < N; i++)
        p = abs(out[i]) > p ? abs(out[i]) : p;
    return p;
}
static double dc(void)
{
    double s = 0;
    for (uint32_t i = N / 10u; i < N; i++)
        s += out[i];
    return s / (N - N / 10u);
}
static double bright(void)                            /* the mean step over the RMS: the more highs, the more */
{
    double s = 0;
    for (uint32_t i = N / 10u + 1u; i < N; i++)
        s += ((double)out[i] - out[i - 1]) * ((double)out[i] - out[i - 1]);   /* (squared: a smoothed jump counts less) */
    return sqrt(s / (N - N / 10u - 1u)) / (rms() + 1e-9);
}

static const char *const NAME[DT_N] = {"SOFT", "HARD", "FOLD", "CRUSH", "FUZZ"};

/* SOFT as fx.c had it before phase 9 (the same code), for TONE 0's samples */
static void old_soft(track_t *t, int32_t *b, uint32_t n, uint32_t ity)
{
    (void)ity;
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;
    g = 4096 + d * d * 2;
    k = 32000 - d * 95;
    mk = 30000 - d * 120;
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;
        x = clamp(x - t->dist_hp, -230000, 230000);
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}
/* BITS as fx.c had it before 2.0 (CRUSH since) */
static void old_bits(track_t *t, int32_t *b, uint32_t n, uint32_t ity)
{
    int32_t d = t->p[P_DIST], k = clamp(32700 - d * 40 + t->p[P_DTONE] * 150, 2000, 32700), mk = 32000 - d * 20;
    uint32_t s = (uint32_t)d / 11u, hm = (1u << ((uint32_t)d >> 5)) - 1u;
    int32_t h = 0, r = s ? 1 << (s - 1u) : 0;
    (void)ity;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = b[i];
        t->dist_hp += (x - t->dist_hp + 64) >> 7;
        x = clamp(x - t->dist_hp, -230000, 230000);
        if (!(i & hm))
            h = ((x + r) >> s) << s;
        t->dist_lp1 += mulq15(h - t->dist_lp1, k);
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}
/* (2.0) HARD FOLD FUZZ: INSERT's shaper ity at DIST's drive, in DIST's low cut, DC blocker, tone and make-up */
static void ref_clip(track_t *t, int32_t *b, uint32_t n, uint32_t ity)
{
    int32_t d = t->p[P_DIST], g = 4096 + d * d * 2, k = clamp(32000 - d * 95 + t->p[P_DTONE] * 150, 2000, 32700);
    int32_t mk = 30000 - d * 120;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;
        x = clamp(x - t->dist_hp, -230000, 230000);
        y = ins_shape(ity, ((x >> 2) * g) >> 10, 0);
        t->dist_dc += (y - t->dist_dc + 256) >> 9;
        y -= t->dist_dc;
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}
/* the TYPE ty at drive and tone against ref (its IT_* ity) over the same input, bit for bit */
static int same_as(int32_t ty, int32_t drive, int32_t tone, void (*ref)(track_t *, int32_t *, uint32_t, uint32_t),
                   uint32_t ity)
{
    static int32_t was[N];
    track_t *t = &trk[0];
    uint32_t i;
    run(ty, drive, tone);
    t->dist_hp = t->dist_lp1 = t->dist_lp2 = t->dist_dc = 0;
    memcpy(was, in, sizeof was);
    for (i = 0; i < N; i += CTL)
        ref(t, was + i, CTL, ity);
    return !memcmp(out, was, sizeof was);
}

int main(void)
{
    static int32_t soft[N];
    static const int32_t DRV[3] = {40, 80, 127};
    int bad = 0, ok = 1, okd = 1, okr = 1, okn = 1, okt = 1;
    uint32_t i, k, ty;
    double rs;
    host_tracks_init();
    for (i = 0; i < N; i++)                               /* a saw, 110 Hz, -6 dBFS */
        in[i] = (int32_t)((((uint64_t)i * 110u * 65536u / 44100u) & 65535u)) / 2 - 16384;
    run(DT_SOFT, 0, 0);
    for (ty = 0; ty < DT_N; ty++) {
        run((int32_t)ty, 0, 63);
        okd &= !memcmp(out, in, sizeof out);
    }
    bad += check("DRIVE 0: the signal unchanged, whatever TYPE and TONE", okd);
    for (k = 0, okd = 1; k < 3u; k++)
        okd &= same_as(DT_SOFT, DRV[k], 0, old_soft, 0) && same_as(DT_CRUSH, DRV[k], 0, old_bits, 0) &&
               same_as(DT_CRUSH, DRV[k], -30, old_bits, 0);
    bad += check("SOFT at TONE 0, CRUSH (BITS until 2.0): the samples of before, bit for bit (DRIVE 40, 80, 127)", okd);
    for (k = 0, okd = 1; k < 3u; k++)
        okd &= same_as(DT_HARD, DRV[k], -20, ref_clip, IT_HARD) && same_as(DT_FOLD, DRV[k], -20, ref_clip, IT_FOLD) &&
               same_as(DT_FUZZ, DRV[k], -20, ref_clip, IT_FUZZ);
    bad += check("(2.0) HARD FOLD FUZZ: INSERT's shaper of the name at DIST's drive, bit for bit", okd);
    for (k = 0; k < 3u; k++) {
        run(DT_SOFT, DRV[k], 0);
        memcpy(soft, out, sizeof soft);
        rs = rms();
        printf("drive:   SOFT at DRIVE %d: RMS %.0f, peak %d, DC %.0f, brightness %.3f\n", DRV[k], rs, peak(), dc(), bright());
        for (ty = 1; ty < DT_N; ty++) {
            double r;
            run((int32_t)ty, DRV[k], 0);
            r = rms();
            if (!(peak() < 32767 && fabs(dc()) < peak() * 0.02)) {
                printf("drive:   %s at DRIVE %d: peak %d, DC %.0f\n", NAME[ty], DRV[k], peak(), dc());
                ok = 0;
            }
            okr &= r > rs * 0.355 && r < rs * 2.82;      /* (within 9 dB) */
            okn &= memcmp(out, soft, sizeof soft) != 0;
            if (k == 2u)
                printf("drive:   DRIVE 127 %s: RMS %.0f (SOFT %.0f), peak %d, DC %.0f, brightness %.3f\n",
                       NAME[ty], r, rs, peak(), dc(), bright());
        }
    }
    bad += check("every TYPE: under full scale, no DC", ok);
    bad += check("  its RMS within 9 dB of SOFT's at DRIVE 40, 80, 127 (its own make-up)", okr);
    bad += check("  another sound than SOFT's", okn);
    {
        uint32_t runs = 0;
        double cs, cf;
        int32_t g = 4096 + 127 * 127 * 2, hard = 0;
        run(DT_SOFT, 127, 0); cs = bright();
        run(DT_FOLD, 127, 0); cf = bright();
        for (i = 0; i < N; i++)                           /* the clip itself, as dist_hard has it, before the tone */
            hard += ins_shape(IT_HARD, ((clamp(in[i], -230000, 230000) >> 2) * g) >> 10, 0) == 24000;
        {   /* CRUSH at 127: a held run of 8 (as dist_crush takes it, before the tone) */
            int32_t h = 0, last = 0;
            uint32_t s = 127u / 11u, hm = (1u << (127u >> 5)) - 1u, same = 0;
            for (i = 0; i < 64u; i++) {
                if (!(i & hm))
                    h = ((in[i] + (1 << (s - 1u))) >> s) << s;
                same += i && h == last;
                last = h;
            }
            runs = same;
        }
        bad += check("HARD: flat at its ceiling; FOLD brighter than SOFT; CRUSH: runs of 8 held samples at 127",
                     hard > 1000u && cf > cs && runs >= 56u);
    }
    for (ty = 0; ty < DT_N; ty++) {
        double lo, hi;
        run((int32_t)ty, 80, -64); lo = bright();
        run((int32_t)ty, 80, 63); hi = bright();
        printf("drive:   TONE %s: -64 %.4f, +63 %.4f\n", NAME[ty], lo, hi);
        okt &= hi > lo;
    }
    bad += check("TONE: +63 brighter than -64, every TYPE", okt);
    printf(bad ? "DRIVE TEST FAILED\n" : "drive tests passed\n");
    return bad != 0;
}
