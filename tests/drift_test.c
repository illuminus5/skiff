/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (2.5, #107) Host test: the tracks' step clocks, the click and the launch points stay on one grid however long they
 * play, through the same sources as the firmware (hostsim.c):
 *   build/host/drift_test
 * 1. the quarter (core.h bpm_quarter): a multiple of 24 at every BPM (40..240), within 12 samples of FS * 60 / BPM, and
 *    every straight and triplet DIV (fx.c div_samples) a whole share of it.
 * 2. T1 1/8 (8 steps), T2 1/16 (16), T3 8T (12), T4 1/32 (32), T5 16T (24): each loops a bar. Run 30 minutes at 120 BPM
 *    and 10 at 40 97 128 140 174 240: at every bar start all five wrap to step 0 in the same block, the same samples into
 *    it, and the click's bar and the launch points' bar start as many blocks from it as at the third bar. 2.2 (each
 *    DIV rounded on its own) fails: at 120 BPM its 1/16 track gained 2 samples a beat on the 1/8 one (#107: they met
 *    after ~30 minutes). */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NO_OFF 0x7FFFFFFF

static int check(const char *what, int ok)
{
    printf("drift: %-100s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static int test_quarter(void)
{
    static const uint8_t DEN[6] = {1, 2, 4, 8, 3, 6};  /* N_DIV 1/4 .. 16T (fx.c DIV_DEN) */
    uint32_t b, d, bad = 0, worst = 0;
    song.g[G_CLOCK] = 0;
    for (b = 40; b <= 240u; b++) {
        uint32_t q = bpm_quarter(b), x = (uint32_t)FS * 60u, e = q * b > x ? q * b - x : x - q * b;
        song.g[G_BPM] = (int16_t)b;
        bad += q % 24u != 0 || beat_samples() != q;
        e = (e + b - 1u) / b;                           /* |q - FS 60 / b|, samples (rounded up) */
        worst = e > worst ? e : worst;
        for (d = 0; d < 6u; d++)
            bad += div_samples(d) * DEN[d] != q;
    }
    return check("the quarter: x24, within 12 samples of FS*60/BPM, every DIV 1/4..16T a whole share (BPM 40..240)",
                 !bad && worst <= 12u);
}

/* a counter's bar starting in block blk: the same blocks after the tracks' bar each time (the first is the reference) */
static void bar_offset(int32_t *off, uint32_t blk, uint32_t bar_blk, uint32_t *bad)
{
    int32_t o = (int32_t)blk - (int32_t)bar_blk;
    if (*off == NO_OFF)
        *off = o;
    else if (o != *off)
        ++*bad;
}

/* minutes at bpm; returns the bars, *bad the bars the clocks disagreed at */
static uint32_t run(uint32_t bpm, uint32_t minutes, uint32_t *bad)
{
    static const uint8_t DIV[5] = {1, 2, 4, 3, 5};      /* 1/8 1/16 8T 1/32 16T (N_DIV) */
    static const uint8_t LEN[5] = {8, 16, 12, 32, 24};  /* a bar each */
    uint32_t k, blk, nblk = minutes * 60u * (uint32_t)FS / CTL, bars = 0, last[5], bar_blk = 0, clk_last = 0, ls_last = 0;
    int32_t clk_off = NO_OFF, ls_off = NO_OFF;
    host_tracks_init();
    song.g[G_BPM] = (int16_t)bpm;
    for (k = 0; k < 5u; k++) {
        trk[k].p[P_SDIV] = DIV[k];
        trk[k].p[P_SLEN] = LEN[k];
        last[k] = 0xFFFFu;
    }
    *bad = 0;
    transport_req = 1;
    for (blk = 0; blk < nblk; blk++) {
        uint32_t w = 0;
        events_block(CTL);
        for (k = 0; k < 5u; k++) {
            w |= (uint32_t)(trk[k].seq_idx == 0 && last[k] != 0) << k;
            last[k] = trk[k].seq_idx;
        }
        if (w) {                                         /* a bar starts: for all five, the same samples into it */
            bars++;
            bar_blk = blk;
            if (w != 31u)
                ++*bad;
            else
                for (k = 1; k < 5u; k++)
                    if (trk[k].seq_pos != trk[0].seq_pos) {
                        ++*bad;
                        break;
                    }
        }
        if (bars >= 3u && clk_step == 0 && clk_last != 0)                       /* the click's bar */
            bar_offset(&clk_off, blk, bar_blk, bad);
        if (bars >= 3u && launch_steps != ls_last && !(launch_steps % 16u))   /* the launch points' bar */
            bar_offset(&ls_off, blk, bar_blk, bad);
        clk_last = clk_step;
        ls_last = launch_steps;
    }
    transport_req = 2;
    events_block(CTL);
    return bars;
}

int main(void)
{
    static const uint16_t BPM[7] = {120, 40, 97, 128, 140, 174, 240};
    uint32_t i, fails = 0;
    char what[160];
    fails += test_quarter();
    for (i = 0; i < 7u; i++) {
        uint32_t bad, min = i ? 10u : 30u, bars = run(BPM[i], min, &bad);
        snprintf(what, sizeof what, "%u BPM, %u min: 1/8 1/16 8T 1/32 16T, the click, the launch points: %u bars, %u off",
                 (unsigned)BPM[i], (unsigned)min, (unsigned)bars, (unsigned)bad);
        fails += check(what, !bad && bars + 1u >= min * BPM[i] / 4u);
    }
    printf(fails ? "drift: %u FAILED\n" : "drift: all passed\n", (unsigned)fails);
    return fails ? 1 : 0;
}
