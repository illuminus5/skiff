/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > AUDIO > SYNC OUT, SYNC PPQN (src/sync_out.c) through the real audio path (audio.c audio_block): NO SYNC bit for
 * bit as before, a retired STYLE byte (no mark) too; LEFT and RIGHT SYNC: the pulses on their side, the music summed to
 * mono on the other; the first pulse with step 0, PPQN a beat on the sequencer's grid (no drift over many bars), each
 * SY_PULSE_MS long, none while stopped; USB audio unchanged */
#include <stdint.h>
static uint8_t host_mset[16 * 32];                     /* (the unit's settings, usb.c mset_io: [15][29] SYNC OUT, PPQN) */
static const uint8_t *const mset_io = host_mset;
#define main hostsim_main
#include "hostsim.c"
#undef main
#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 1u
static volatile uint32_t t5_nested_ticks;
static uint32_t fm1_ticks(void) { return 0; }
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p) { (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define FELUCCA_UAC 1
static int32_t tap_blk[2 * CTL];
static void uac_tap(const int32_t *out, const uint8_t *lo, uint32_t n) { (void)lo; memcpy(tap_blk, out, 8u * n); }
static void uac_render_start(void) {}
#include "../firmware/src/audio.c"

static int bad;
static void check(const char *what, int ok) { printf("sync: %-90s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

#define MAXB 12000u
static int32_t L[MAXB * CTL], R[MAXB * CTL], UL[MAXB * CTL], UR[MAXB * CTL];
static uint32_t nb;
static void fresh(uint32_t mode, uint32_t ppqn)
{
    uint32_t p;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song); sel_set(0);
    memset(&midi_clock, 0, sizeof midi_clock);
    host_tracks_init();
    for (p = 0; p < NPART; p++) host_preset(&trk[p], 0, 0);
    for (p = 0; p < G_COUNT; p++) song.g[p] = GP[p].def;
    transport_req = panic_req = 0;
    seq_stop();
    lim_env = LIM_T; dc_l = dc_r = dce_l = dce_r = 0;
    click_req = 0; click_mode = CLICK_OFF; cin_bars = 0; fx_usb_fixed = 0;
    sy_clk = SY_START; sy_hi = 0; sy_at = -1;
    song.g[G_BPM] = 120;
    host_mset[32u * 15u + 29u] = (uint8_t)(SY_NORMAL | mode << 5 | ((ppqn - 1u) ^ 1u));   /* (as menu_items.c: INVERT
                                                                                         * OFF, the checks' pulse upward) */
    nb = 0; fm1_ms = 0;
}
static void run(uint32_t n)
{
    int32_t out[2 * CTL];
    uint32_t i;
    while (n-- && nb < MAXB) {
        audio_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            L[nb * CTL + i] = out[2u * i]; R[nb * CTL + i] = out[2u * i + 1u];
            UL[nb * CTL + i] = tap_blk[2u * i]; UR[nb * CTL + i] = tap_blk[2u * i + 1u];
        }
        nb++;
        fm1_ms = (uint32_t)((uint64_t)nb * CTL * 1000u / FS);
    }
}
/* rising edges of x[0..n) at SY_LEVEL: their samples; each pulse's length */
#define MAXP 2000
static uint32_t at[MAXP], len[MAXP], np;
static int all_level;
static void edges(const int32_t *x, uint32_t n)
{
    uint32_t i;
    np = 0; all_level = 1;
    for (i = 0; i < n; i++) {
        if (x[i] != SY_IDLE && x[i] != SY_LEVEL) all_level = 0;
        if (x[i] == SY_LEVEL && (i == 0 || x[i - 1] != SY_LEVEL) && np < MAXP) { at[np] = i; len[np] = 0; np++; }
        if (x[i] == SY_LEVEL && np) len[np - 1]++;
    }
}

int main(void)
{
    static int32_t SL[MAXB * CTL], SR[MAXB * CTL], SUL[MAXB * CTL], NL[MAXB * CTL], NR[MAXB * CTL];
    uint32_t i, n = 0, q, ok, w = SY_W, pp, st;
    static const uint8_t PPQNS[] = {1, 2, 3, 4, 5, 7, 12, 24};

    fresh(SO_STEREO, 2); transport_req = 1; run(6000);
    memcpy(SL, L, sizeof SL); memcpy(SR, R, sizeof SR); memcpy(SUL, UL, sizeof SUL);
    fresh(SO_LEFT, 2); transport_req = 1; run(6000);
    n = 6000u * CTL;
    edges(L, n);
    q = 4u * div_samples(2);
    check("LEFT SYNC: the left channel only the pulse (+full scale) and the idle (-1 %: never 0, the DAC kept live)", all_level);
    ok = np > 4;
    for (i = 0; i < np; i++) ok &= at[i] == i * q / 2u || at[i] == i * q / 2u + 1u;   /* (2 PPQN: every half beat) */
    check("LEFT SYNC, 2 PPQN: the first pulse at sample 0 (step 0), then every half beat, no drift", ok && at[0] == 0);
    ok = 1;
    for (i = 0; i + 1 < np; i++) ok &= len[i] == w;
    check("each pulse SY_PULSE_MS (20 ms) long at 2 PPQN, 120 BPM", ok && w == FS * 20u / 1000u);
    ok = 1;
    for (i = 0; i < n; i++) ok &= R[i] == (SL[i] + SR[i]) >> 1;
    check("LEFT SYNC: the right channel the stereo music summed to mono", ok);
    ok = !memcmp(UL, SUL, sizeof SUL);
    check("USB audio unchanged by the sync", ok);
    memcpy(NL, L, sizeof NL); memcpy(NR, R, sizeof NR);
    fresh(SO_LEFT, 2); host_mset[32u * 15u + 29u] &= (uint8_t)~SY_NORMAL; transport_req = 1; run(6000);   /* (INVERT ON) */
    ok = 1;
    for (i = 0; i < n; i++) ok &= L[i] == -NL[i] && R[i] == NR[i];
    check("SYNC INVERT ON (the default): the sync side OFF's upside down (-full, idle +1 %), the music the same",
          ok && L[0] == -SY_LEVEL && L[n - 1u] == -SY_IDLE);
    fresh(SO_LEFT, 2); host_mset[32u * 15u + 29u] = 1u << 5; transport_req = 1; run(2000);   /* (only LEFT set: the PPQN
                                                                                                * and INVERT bits 0) */
    for (i = 0; i < 2000u * CTL; i++) NL[i] = -L[i];   /* (INVERT ON: upward again for edges) */
    edges(NL, 2000u * CTL);
    ok = np >= 5 && all_level && at[0] == 0 && at[1] == q / 2u && at[4] == 2u * q;
    check("PPQN stored as PPQN - 1 XOR 1: 0 is 2 PPQN (a new unit, RESET SETTINGS)", ok && L[0] == -SY_LEVEL);
    fresh(SO_STEREO, 2); host_mset[32u * 15u + 29u] |= SY_NORMAL; transport_req = 1; run(500);   /* (on while playing) */
    host_mset[32u * 15u + 29u] |= (uint8_t)(SO_LEFT << 5); run(1500);
    edges(L + 500u * CTL, 1500u * CTL);
    ok = np >= 3;
    for (i = 0; i < np; i++) ok &= (500u * CTL + at[i]) % (q / 2u) == 0u;   /* (on the eighths from PLAY) */
    check("SYNC OUT switched on while playing: the pulses on the beat's grid at once", ok);
    for (pp = 0; pp < sizeof PPQNS; pp++) {
        uint32_t k = PPQNS[pp];
        fresh(SO_RIGHT, k); transport_req = 1; run(6000);
        edges(R, n);
        ok = np > 4 && all_level;
        for (i = 0; i < np; i++) {
            uint32_t e = (uint32_t)((uint64_t)i * q / k);
            ok &= at[i] >= e && at[i] <= e + 1u;
        }
        {
            char b[96];
            snprintf(b, sizeof b, "RIGHT SYNC, %u PPQN: the pulses on the right, %u a beat on the grid", k, k);
            check(b, ok);
        }
        if (k == 24u) {
            ok = 1;
            for (i = 0; i + 1 < np; i++) ok &= len[i] == q / 24u / 2u && q / 24u / 2u < w;
            check("24 PPQN: a pulse high for half its period (shorter than 20 ms there)", ok);
        }
    }
    fresh(SO_LEFT, 2); run(2000);
    edges(L, 2000u * CTL);
    check("stopped: no pulses", np == 0);
    transport_req = 1; run(2000); transport_req = 2; run(2000);
    edges(L + 4000u * CTL, 2000u * CTL);
    check("after STOP: no pulses", np == 0);
    for (st = 0; st < 4u; st++) {                     /* (a STYLE kept from before: no mark, NO SYNC) */
        fresh(SO_STEREO, 2); host_mset[32u * 15u + 29u] = (uint8_t)st; transport_req = 1; run(6000);
        ok = !memcmp(L, SL, sizeof SL) && !memcmp(R, SR, sizeof SR);
        if (!ok) break;
    }
    check("a retired STYLE byte (0..3): NO SYNC, the DAC as it was", ok);
    printf(bad ? "sync: %d FAILED\n" : "sync: all passed\n", bad);
    return bad != 0;
}
