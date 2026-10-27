/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Fuzz harness: mutated / random project stores (with and without a valid hash, FUN1..FUN8 magics, any length)
 * through proj_import -> project_restore_runtime -> render; (2.0) SKF4 records too, their counts mutated with the rest.
 * run_tests.sh runs it briefly under ASan / UBSan with a fixed seed, as 2.0 is (SKF4 and SKF3 seeds) and with
 * FELUCCA_LEGACY (the converter's readers: SKF4 and SKF1 seeds).   fuzz_proj [ITERATIONS [SEED]] */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"                                /* hostsim, the stubs, RAM flash, reset() */
static uint32_t rs = 0x2545F491u;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(int argc, char **argv)
{
    static project_t a, c;
    static union { uint32_t align; uint8_t raw[PROJ_STORE_SIZE + 64u]; } st;   /* (any length up to n's) */
    project_store_t *ps = (project_store_t *)st.raw;
    static int32_t o[CTL * 2 + 16];
    uint32_t iters = argc > 1 ? (uint32_t)atoi(argv[1]) : 20000u, i, j, k, n, imported = 0;
    if (argc > 2) rs = (uint32_t)atoi(argv[2]) | 1u;
    reset();
    for (i = 0; i < iters; i++) {
        uint32_t mode = rnd() % 8u, sz = mode && mode < 4u ? PROJ_STORE4 : FELUCCA_LEGACY ? PROJ_STORE_SIZE : PROJ_STORE3;
        if (mode == 0) {                                            /* fully random bytes */
            for (j = 0; j < sizeof st.raw; j++) st.raw[j] = (uint8_t)rnd();
            if (rnd() & 1u) { uint32_t m = 0x46554E30u + 1u + rnd() % 8u; memcpy(st.raw, &m, 4); }   /* "FUN1".."FUN8" */
        } else {                                                    /* a valid store, mutated */
            for (j = 0; j < NTRK; j++) if (rnd() & 1u) trk_note_on(&trk[j], 48u + rnd() % 36u, 1u + rnd() % 127u);
            project_capture(&a);
#if FELUCCA_LEGACY
            if (sz == PROJ_STORE4 ? !proj_pack2(ps, &a, PROJ_SKF) : !proj_pack(ps, &a)) continue;   /* (SKF4, SKF1) */
#else
            if (!proj_pack2(ps, &a, sz == PROJ_STORE4 ? PROJ_SKF : &PROJ_SKF3)) continue;           /* ((2.0) SKF4, SKF3) */
#endif
            k = 1u + rnd() % 48u;
            for (j = 0; j < k; j++) {
                uint32_t off = rnd() % 4u == 0 ? rnd() % 200u : rnd() % sz;
                st.raw[off] = (uint8_t)(rnd() % 4u == 0 ? 0xFF : rnd() % 4u == 0 ? 0x7F : rnd());
            }
        }
        if (rnd() % 2u) {                                           /* a valid checksum: attacker-controlled flash */
            uint32_t sum = proj_hash(st.raw, sz - 4u);
            memcpy(st.raw + sz - 4u, &sum, 4);
        }
        n = rnd() % 8u == 0 ? rnd() % (PROJ_STORE_SIZE + 64u) : rnd() % 8u == 0 ? PROJ_STORE_V7 :
            rnd() % 2u ? sz : PROJ_STORE_SIZE;
        if (proj_import(&c, st.raw, (int)n)) {
            imported++;
            project_restore_runtime(&c);
            for (j = 0; j < NTRK; j++) trk_note_on(&trk[j], 48u + rnd() % 36u, 100);
            for (j = 0; j < 12u; j++) mix_block(o, CTL);
        }
        if (rnd() % 64u == 0) reset();
    }
    printf("fuzz_proj ok: %u iterations, %u imported\n", iters, imported);
    return 0;
}
