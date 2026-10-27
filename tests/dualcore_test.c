/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 gospodindilly-ops (Felucca 1.6.1's tests/dualcore_test.c); Skiff 1.3: six tracks, Skiff's own state */
/* The second core (fx.c mix_block, hal/fm1_cpu1.h) on the host, after Jangada's tests/dualcore_test.c
 * (github.com/zednaked/jangada, GPL-3.0) and Felucca 1.6.1's. Six tracks of the same engine at once (every preset of
 * every engine; PHYS and GRAIN on the four heavy tracks, ANALOG on T5 and T6), and mixes of different engines; every
 * track with something that keeps state of its own while it renders: DRIVE's types (track_dist), the SLICER (T2), the
 * DUCK under T1's hits (T3), the matrix with an LFO S&H, which draws from the shared generator (T1, T4), a per-block
 * destination (T4), VOICE's breath noise and PHYS's model changed under a sounding note. Each scene is rendered on one
 * core, and again with a thread as core 1 (tests/c1_host.h) taking every split of the tracks in turn, the firmware's own
 * choice (mix_split) and fixed splits. The outputs must be bit for bit the same; built with -fsanitize=thread
 * (run_tests.sh TSAN=1), nothing two tracks share may be touched by both at once.
 * Built with -DVOICES_2CORE=8 (run_tests.sh): the two cores' voice budget as one core's, so the renders can be the same;
 * the bigger budget has its own check (voice_budget below).
 *   build/host/dualcore_test          SCENE=text: only the scenes whose name holds it */
#include "c1_host.h"
#define main hostsim_main
#include "hostsim.c"
#undef main

#define SECS 3u

typedef struct { const char *name; uint8_t eng[NPART], pre[NPART]; uint8_t morph; } scen_t;

static int32_t peak[NPART];                         /* each track's loudest (track_t.peak), for the check */

static void part_setup(track_t *t, uint32_t k)
{
    t->p[P_DIST] = (int16_t)(10 + 15 * (k % 3u));    /* DRIVE: each track its type (t->dist_*) */
    t->p[P_DTYPE] = (int16_t)(k % DT_N);
    t->p[P_REV] = (int16_t)(20 + 10 * k);
    t->p[P_DLY] = (int16_t)(10 * k);
    t->p[P_CHOR] = (int16_t)(30 - 5 * k);
    if (k == 0u) {
        t->p[P_LWAVE] = 4;                          /* S&H: lfo_rand, the shared generator (T1..T3) */
        t->p[P_LRATE] = 110;
        t->p[P_LD_FLT] = 20;
    }
    if (k == 1u) {
        t->p[P_SLCR] = 1;                           /* the SLICER: sl[1] */
        t->p[P_SLDEPTH] = 100;
    }
    if (k == 2u) {
        t->p[P_DKSRC] = 1;                          /* DUCK under T1's hits: duck[2] */
        t->p[P_DKAMT] = 100;
    }
    if (k == 3u) {
        t->p[P_LWAVE] = 4;                          /* S&H (T4: its own generator) into the matrix */
        t->p[P_LRATE] = 100;
        t->p[P_M1SRC] = MS_LFO;
        t->p[P_M1DST] = MD_CUT;
        t->p[P_M1AMT] = 40;
        t->p[P_M2SRC] = MS_LFO;
        t->p[P_M2DST] = MD_RATE;                    /* (a per-block destination: t->p while it renders) */
        t->p[P_M2AMT] = 20;
    }
}

static uint64_t render(const scen_t *sc, int dual, const char *mask)
{
    static const uint8_t CH[3] = {0, 7, 12};
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t f, k, i;
    int32_t o[2 * CTL];
    host_tracks_init();
    c1h_off = !dual;
    if (mask)
        setenv("C1_MASK", mask, 1);
    for (k = 0; k < NPART; k++) {
        host_preset(&trk[k], sc->eng[k], sc->pre[k]);
        part_setup(&trk[k], k);
    }
    for (f = 0; f < SECS * FS; f += CTL) {
        uint32_t ms = f / (FS / 1000u);
        for (k = 0; k < NPART; k++) {
            uint32_t on = 100u + 220u * k, off = on + 1400u, base = 48u + 5u * k;
            if (ms == on || ms == on + 700u)
                for (i = 0; i < 2u; i++)
                    trk_note_on(&trk[k], base + CH[i + (ms != on)], 90 + 6 * k);
            if (ms == off)
                for (i = 0; i < 3u; i++)
                    trk_note_off(&trk[k], base + CH[i]);
        }
        if (sc->morph && ms == 600u)                /* PHYS: MODEL changed under a sounding note (phys_render) */
            trk[0].p[P_E0] = (int16_t)((trk[0].p[P_E0] + 1) % PM_COUNT);
        mix_block(o, CTL);
        for (k = 0; k < NPART; k++)
            if (trk[k].peak > peak[k])
                peak[k] = trk[k].peak;
        for (i = 0; i < 2u * CTL; i++) {
            uint32_t u = (uint32_t)o[i];
            for (k = 0; k < 4u; k++) {
                h ^= (u >> (8u * k)) & 0xFFu;
                h *= 0x100000001B3ull;
            }
        }
    }
    return h;
}

/* in a child of its own: every render starts from the state at boot */
static uint64_t render_fork(const scen_t *sc, int dual, const char *mask, uint32_t *taken)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        h = render(sc, dual, mask);
        if (write(fd[1], &h, sizeof h) != sizeof h || write(fd[1], peak, sizeof peak) != sizeof peak ||
            write(fd[1], &c1h_taken, sizeof c1h_taken) != sizeof c1h_taken)
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h || read(fd[0], peak, sizeof peak) != sizeof peak ||
        read(fd[0], taken, sizeof *taken) != sizeof *taken)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

static int heavy_only(uint32_t e) { return e == ENGI_PHYS || e == ENGI_GRAIN; }   /* (not on T5, T6: NHEAVY) */

/* (Skiff 1.3) the voice budget (core.h VOICE_BUDGET): 8 across the tracks on one core, VOICES_2CORE while the second
 * core runs; four POLY tracks of three notes each. Run with BUDGET=1, built without -DVOICES_2CORE=8 (run_tests.sh) */
static int voice_budget(void)
{
    uint32_t pass, k, i, n[2], want = VOICES_2CORE < 12u ? VOICES_2CORE : 12u;
    int32_t o[2 * CTL];
    for (pass = 0; pass < 2u; pass++) {
        host_tracks_init();
        c1h_off = !pass;
        for (k = 0; k < 4u; k++) {
            host_preset(&trk[k], 0u, 1u);           /* (ANALOG SOFT PAD: POLY) */
            for (i = 0; i < 3u; i++)
                trk_note_on(&trk[k], 48u + 12u * k + 4u * i, 100);
        }
        mix_block(o, CTL);
        n[pass] = voices_busy();
    }
    printf("dualcore: voice budget: %u voices on one core, %u with the second (VOICES_2CORE %u)   %s\n", n[0], n[1],
           (unsigned)VOICES_2CORE, n[0] == NVOICE && n[1] == want ? "ok" : "FAIL");
    return !(n[0] == NVOICE && n[1] == want);
}

int main(void)
{
    if (getenv("BUDGET"))
        return voice_budget();
    static scen_t sc[256];
    static char names[256][40];
    static const char *const MASKS[] = {"rot", "auto", "21", "42", "7", "56", "1", "62"};
    uint32_t n = 0, e, i, k, g, fail = 0;
    for (e = 0; e < NENGINES; e++) {                    /* every preset: six tracks at a time */
        if (!eng_ok(e))                                 /* (a retired number: DIGITAL without FELUCCA_FM4) */
            continue;
        for (g = 0; g * NPART < ENGINES[e]->npresets; g++) {
            snprintf(names[n], sizeof names[n], "6 x %s %u..%u", ENGINES[e]->name, g * NPART, g * NPART + NPART - 1u);
            sc[n].name = names[n];
            for (k = 0; k < NPART; k++) {
                int light = heavy_only(e) && k >= NHEAVY;
                sc[n].eng[k] = (uint8_t)(light ? 0u : e);
                sc[n].pre[k] = (uint8_t)(light ? k : (g * NPART + k) % ENGINES[e]->npresets);
            }
            sc[n++].morph = 0;
        }
    }
    sc[n++] = (scen_t){"mix: PHYS FM6 ANALOG DRUM VOICE FM6", {ENGI_PHYS, ENGI_FM6, 0, ENGI_DRUM, 5, ENGI_FM6}, {1, 4, 7, 0, 2, 6}, 0};
    sc[n++] = (scen_t){"mix: GRAIN NOISE VOICE LOFI VOICE DRUMX", {ENGI_GRAIN, 11, 5, 3, 5, ENGI_DRUMX}, {1, 4, 7, 2, 3, 0}, 0};
    sc[n++] = (scen_t){"mix: TRIO WHEEL PHASE FM6 WHEEL SAMPLE", {6, 7, 2, ENGI_FM6, 7, ENGI_SAMPLE}, {1, 4, 5, 3, 2, 1}, 0};
    sc[n++] = (scen_t){"mix: PHYS morph + DRUM GRAIN ..", {ENGI_PHYS, ENGI_DRUM, ENGI_GRAIN, 0, ENGI_DRUMX, 3}, {2, 1, 3, 5, 0, 1}, 1};
#if FELUCCA_SLICE
    sc[n++] = (scen_t){"mix: SAMPLE SLICE GRAIN PHASE SLICE", {ENGI_SAMPLE, ENGI_SLICE, ENGI_GRAIN, 2, ENGI_SLICE, ENGI_DRUM}, {2, 1, 3, 5, 0, 1}, 0};
#endif
    for (i = 0; i < n; i++) {
        uint64_t ref;
        uint32_t m, bad = 0, quiet = 0, taken = 0, took = 0;
        if (getenv("SCENE") && !strstr(sc[i].name, getenv("SCENE")))
            continue;
        ref = render_fork(&sc[i], 0, 0, &taken);
        for (k = 0; k < NPART; k++)
            quiet += peak[k] < 200;
        if (quiet) {                                    /* a silent track proves nothing */
            printf("dualcore: %-40s %u track(s) silent: peaks %d %d %d %d %d %d   FAIL\n", sc[i].name, quiet, peak[0],
                   peak[1], peak[2], peak[3], peak[4], peak[5]);
            fail++;
            continue;
        }
        for (m = 0; m < sizeof MASKS / sizeof MASKS[0]; m++) {
            uint64_t h = render_fork(&sc[i], 1, MASKS[m], &taken);
            took += taken != 0;
            if (h != ref || !h) {
                printf("dualcore: %-40s split %-4s FAIL (%016llx, one core %016llx)\n", sc[i].name, MASKS[m],
                       (unsigned long long)h, (unsigned long long)ref);
                bad++;
            }
        }
        if (took < 2u) {                                /* the thread must have rendered tracks (auto may hand none) */
            printf("dualcore: %-40s the second core never took tracks   FAIL\n", sc[i].name);
            bad++;
        }
        if (!bad)
            printf("dualcore: %-40s the same on two cores, every split   ok\n", sc[i].name);
        fail += bad != 0;
    }
    printf("dualcore: %u of %u scenes differ\n", fail, n);
    return fail != 0;
}
