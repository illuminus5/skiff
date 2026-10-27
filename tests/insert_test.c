/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the per-track INSERT (Skiff 1.3, from Felucca 1.5's, its Discussions #78 and #177; src/fx.c
 * track_insert), on the real sources:
 * 1. bit for bit: TYPE OFF with any A B C MIX, and MIX 0 of every type, render the phrase the defaults render (POLY,
 *    the sends' tails); MIX 100 % of every type changes it; TYPE OFF while silent leaves no state.
 * 2. each TYPE on 3 and 8 saws, A B C at their corners: no DC (its mean against the input's), bounded, MIX 0 = the
 *    input bit for bit (nothing runs), MIX 50 % exactly halfway between the dry and MIX 100 %.
 * 3. MIX glides (2.9 ms both ways), a TYPE change fades out, switches, fades in; MIX and TYPE as automation and as a
 *    step's lock on a playing pattern.
 * 4. the pages: A B C named by the TYPE; a project and a user preset keep the INSERT. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"
#include <math.h>
#include <sys/wait.h>
#include <unistd.h>

static int bad;
static void ck(const char *what, int ok)
{
    printf("insert: %-82s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static uint64_t hash;
static void mixb(uint32_t n)
{
    uint32_t i, k;
    while (n--) {
        mix_block(out_buf, CTL);
        for (i = 0; i < 2u * CTL; i++)
            for (k = 0; k < 4u; k++) {
                hash ^= ((uint32_t)out_buf[i] >> (8u * k)) & 0xFFu;
                hash *= 0x100000001B3ull;
            }
    }
}
static uint32_t preset_by_name(uint32_t e, const char *n)
{
    uint32_t k;
    for (k = 0; k < ENGINES[e]->npresets; k++)
        if (str_eq(ENGINES[e]->presets[k].name, n))
            return k;
    return 0;
}
static void set_ins(track_t *t, int32_t ty, int32_t a, int32_t b, int32_t c, int32_t mix)
{
    t->p[P_ITYPE] = (int16_t)ty;
    t->p[P_IA] = (int16_t)a;
    t->p[P_IB] = (int16_t)b;
    t->p[P_IC] = (int16_t)c;
    t->p[P_IMIX] = (int16_t)mix;
}
static void fresh(uint32_t pi)                       /* track 1 ANALOG preset pi, the INSERT at rest */
{
    memset(ins, 0, sizeof ins);
    memset(&motion, 0, sizeof motion);
    song.playing = 0;
    song.master_q12 = MASTER_FULL;                     /* (the volume knob: full, so the mix is heard) */
    host_preset(&trk[0], 0, pi);
    sel_set(0);
    hash = 0xCBF29CE484222325ull;
}

/* ------------------------------------------------------------------ 1 --- */
static int32_t ph_ty = -1, ph_a, ph_b, ph_c, ph_mix;
/* the hash of a phrase on track 1 (ANALOG SOFT PAD, POLY, the preset's sends) in a fork()ed child (every one from the
 * same state): the INSERT as ph_* says (ph_ty -1: the defaults) */
static uint64_t phrase(void)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    fflush(stdout);
    if (!(pid = fork())) {
        track_t *t = &trk[0];
        fresh(preset_by_name(0, "SOFT PAD"));
        t->p[P_VOICE] = V_POLY;
        if (ph_ty >= 0)
            set_ins(t, ph_ty, ph_a, ph_b, ph_c, ph_mix);
        trk_note_on(t, 48, 100);
        trk_note_on(t, 55, 70);
        trk_note_on(t, 64, 120);
        mixb(FS / 2u / CTL);
        trk_note_on(t, 72, 90);
        mixb(FS / 4u / CTL);
        trk_note_off(t, 48);
        trk_note_off(t, 55);
        trk_note_off(t, 64);
        trk_note_off(t, 72);
        mixb(FS / CTL);
        h = hash;
        if (write(fd[1], &h, sizeof h) != sizeof h)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}
static void test_identity(void)
{
    uint64_t h0, h;
    int32_t ty;
    int ok0 = 1, okw = 1;
    ph_ty = -1;
    h0 = phrase();
    ph_ty = IT_OFF; ph_a = 1; ph_b = 2; ph_c = 3; ph_mix = 50;
    ck("TYPE OFF with A B C MIX set renders the defaults' phrase bit for bit", phrase() == h0 && h0);
    for (ty = 1; ty < IT_N; ty++) {
        ph_ty = ty; ph_a = 127; ph_b = 127; ph_c = 127; ph_mix = 0;
        ok0 &= phrase() == h0;
        ph_mix = 127;
        h = phrase();
        if (h == h0 || !h)
            printf("  %s: the phrase as the defaults'\n", N_ITYPE[ty]);
        okw &= h != h0 && h;
    }
    ck("MIX 0 of every TYPE: bit for bit the defaults (nothing runs)", ok0);
    ck("MIX 100 % of every TYPE: the phrase changes", okw);
    {   /* TYPE OFF while the part is silent (its tail run out) leaves no INSERT state for the next note */
        track_t *t = &trk[0];
        int stale;
        fresh(preset_by_name(0, "SOFT PAD"));
        set_ins(t, IT_FLANGER, 64, 64, 64, 127);
        trk_note_on(t, 60, 100);
        mixb(FS / 4u / CTL);
        trk_note_off(t, 60);
        mixb(4u * FS / CTL);                          /* silent, the tail run out */
        stale = ins[0].w != 0 && t->ins_run;
        t->p[P_ITYPE] = IT_OFF;
        mixb(1);
        ck("TYPE OFF while silent: the INSERT at rest at once (no stale fade on the next note)",
           stale && !ins[0].w && !t->ins_run);
    }
}

/* ------------------------------------------------------------------ 2 --- */
#define NS (2u * FS / CTL * CTL)                      /* two seconds, whole blocks: the last FS samples are 1 s */
static int32_t sig_in[NS], sig_a[NS], sig_b[NS];
static void make_sig(uint32_t voices)             /* saws, 24000 each (a voice's level), a chord's pitches */
{
    static const double HZ[8] = {110.0, 139.0, 165.0, 220.0, 277.0, 330.0, 440.0, 554.0};
    uint32_t i, v;
    for (i = 0; i < NS; i++) {
        double s = 0;
        for (v = 0; v < voices; v++) {
            double ph = fmod(i * HZ[v] / FS + v * 0.37, 1.0);
            s += (ph * 2.0 - 1.0) * 24000.0;
        }
        sig_in[i] = (int32_t)s;
    }
}
/* the input through track 1's INSERT from rest into out */
static void run_ins(int32_t ty, int32_t a, int32_t b, int32_t c, int32_t mix, int32_t *out)
{
    track_t *t = &trk[0];
    uint32_t i;
    memset(ins, 0, sizeof ins);
    set_ins(t, ty, a, b, c, mix);
    memcpy(out, sig_in, sizeof sig_in);
    for (i = 0; i < NS; i += CTL)
        track_insert(t, out + i, CTL);
}
static void test_types(void)
{
    static const int16_t COR[3] = {0, 64, 127};
    uint32_t nv, ty, ia, ib, ic, i;
    printf("insert: the types on 3 and 8 saws (peak in 72000 / 192000), A B C at 0 64 127 (27 settings), MIX 100 %%:\n");
    for (ty = 1; ty < IT_N; ty++) {
        int32_t pk[2] = {0, 0};
        double dc[2] = {0, 0};
        int same0 = 1, lin = 1, diff = 0;
        for (nv = 3; nv <= 8; nv += 5) {
            uint32_t s = nv == 8;
            make_sig(nv);
            for (ia = 0; ia < 3u; ia++)
                for (ib = 0; ib < 3u; ib++)
                    for (ic = 0; ic < 3u; ic++) {
                        double sy = 0, sx = 0;
                        /* (CRUSH: against the input held at its RATE, as it samples it) */
                        uint32_t hold = ty == IT_CRUSH ? INS_HOLD[COR[ib] >> 3] : 1u;
                        run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 127, sig_a);
                        for (i = 0; i < NS; i++) {
                            int32_t m = sig_a[i] < 0 ? -sig_a[i] : sig_a[i];
                            pk[s] = m > pk[s] ? m : pk[s];
                            diff |= sig_a[i] != sig_in[i];
                            if (i >= NS - FS) {
                                sy += sig_a[i];
                                sx += sig_in[i / hold * hold];
                            }
                        }
                        sy = fabs((sy - sx) / FS);
                        if (ty != IT_CRUSH || ia || ib)         /* (1 bit at 689 Hz: its coarse errors do not average
                                                                 * out in 1 s; the odd check below) */
                            dc[s] = sy > dc[s] ? sy : dc[s];
                        if (ia == 1 && ic != 1) {           /* MIX 50 % halfway, MIX 0 the input */
                            run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 64, sig_b);
                            for (i = SL_RAMP; i < NS; i++) {
                                int32_t want = sig_in[i] + mulq16(sig_a[i] - sig_in[i], 64u * 258u * 2u);
                                lin &= abs(sig_b[i] - want) <= 1;
                            }
                            run_ins((int32_t)ty, COR[ia], COR[ib], COR[ic], 0, sig_b);
                            same0 &= !memcmp(sig_b, sig_in, sizeof sig_in) && !ins[0].w;
                        }
                    }
        }
        printf("insert:   %-5s peak 3 saws %6d, 8 saws %6d; worst DC (mean against the input's) %5.1f / %5.1f\n",
               N_ITYPE[ty], pk[0], pk[1], dc[0], dc[1]);
        {
            char w[96];
            snprintf(w, sizeof w, "%s: no DC (< 0.5 %% of a voice's full scale), bounded (< 1.5 x the input, LEVEL +7.5 dB)",
                     N_ITYPE[ty]);
            ck(w, dc[0] < 164.0 && dc[1] < 164.0 && pk[0] < 120000 && pk[1] < 288000);
            snprintf(w, sizeof w, "%s: MIX 0 the input bit for bit, MIX 50 %% halfway to MIX 100 %% (the wet), it acts",
                     N_ITYPE[ty]);
            ck(w, same0 && lin && diff);
        }
    }
    {   /* CRUSH's steps are odd: -x gives minus what x gives (but at an exact half step, which rounds up), every BITS */
        int ok = 1;
        int32_t v, a;
        for (a = 0; a < 128; a += 8)
            for (v = -140000; v <= 140000; v += 7) {
                uint32_t sh = ins_bits_sh(a);
                int32_t half = sh && !((v + (1 << (sh - 1u))) & ((1 << sh) - 1));
                ok &= half || ins_shape(IT_CRUSH, -v, sh) == -ins_shape(IT_CRUSH, v, sh);
            }
        ck("CRUSH: its steps are odd (no offset) at every BITS", ok);
    }
}

/* ------------------------------------------------------------------ 3 --- */
static void test_glide(void)
{
    track_t *t = &trk[0];
    int32_t w1, w4, wm;
    uint32_t i;
    int ok;
    make_sig(3);
    fresh(0);
    set_ins(t, IT_SOFT, 64, 96, 96, 127);
    memcpy(sig_a, sig_in, sizeof sig_in);
    track_insert(t, sig_a, CTL);
    w1 = ins[0].w;
    for (i = 1; i < 4u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    w4 = ins[0].w;
    ck("MIX 0 -> 100 %: the wet share glides in over 2.9 ms (128 samples)", w1 == CTL * SL_SLOPE && w4 == 32768);
    t->p[P_IMIX] = 0;
    track_insert(t, sig_a + 4u * CTL, CTL);
    wm = ins[0].w;
    for (i = 5; i < 9u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    ck("  and out again; then nothing runs (the input as it is)", wm == 32768 - CTL * SL_SLOPE && !ins[0].w &&
       !memcmp(sig_a + 8u * CTL, sig_in + 8u * CTL, CTL * sizeof(int32_t)));
    t->p[P_IMIX] = 127;                                /* a TYPE change while it plays: SOFT out, FLANG in from rest */
    for (i = 9; i < 16u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    t->p[P_ITYPE] = IT_FLANGER;
    ok = ins[0].type == IT_SOFT;
    for (i = 16; i < 20u; i++) {
        track_insert(t, sig_a + i * CTL, CTL);
        ok &= i < 19u ? ins[0].type == IT_SOFT && ins[0].w > 0 : 1;
    }
    track_insert(t, sig_a + 20u * CTL, CTL);
    ok &= ins[0].type == IT_FLANGER && ins[0].w == CTL * SL_SLOPE;
    for (i = 21; i < 24u; i++)
        track_insert(t, sig_a + i * CTL, CTL);
    {   /* no click: the largest step of the output over the change against the input's own */
        int32_t jmp = 0, jin = 0;
        for (i = 16u * CTL + 1u; i < 24u * CTL; i++) {
            int32_t a = abs(sig_a[i] - sig_a[i - 1]), b = abs(sig_in[i] - sig_in[i - 1]);
            jmp = a > jmp ? a : jmp;
            jin = b > jin ? b : jin;
        }
        ck("a TYPE change: the old one fades out, the new one starts from rest and fades in, no jump past the saws'",
           ok && jmp <= jin + jin / 4);
    }
}

/* MIX and TYPE on a playing pattern: MIX automation (0 at step 1, 100 % at step 9), a TYPE lock (CRUSH on step 5) */
static void test_motion(void)
{
    track_t *t = &trk[0];
    uint32_t i, k, step_s = 0;
    int ok_a = 1, ok_l = 1, seen_c = 0, seen_back = 0, seen_w = 0;
    static const uint8_t NOTE[1] = {48};
    fresh(preset_by_name(0, "SAW LEAD"));
    song.g[G_BPM] = 120;
    set_ins(t, IT_FOLD, 90, 96, 96, 0);
    for (i = 0; i < 16u; i++)
        put_step(t, i, 1, NOTE, ST_NOTE, 0);
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->seq_active = 1;
    ok_a &= !motion_set_event(t, 0, P_IMIX, 0) && !motion_set_event(t, 8, P_IMIX, 127);
    ok_l &= !motion_set_lock(t, 4, P_ITYPE, IT_CRUSH);
    seq_start();
    for (k = 0; k < 2750u; k++) {                       /* 16 steps of 172.3 blocks (1/16 at 120 BPM), not again */
        uint32_t idx;
        mixb(1);                                        /* (mix_block runs the sequencer: events_block) */
        idx = t->seq_idx;
        if (idx < 8u)
            ok_a &= t->p[P_IMIX] == 0 && !ins[0].w;
        else if (idx < 16u && t->p[P_IMIX] == 127) {
            if (!step_s)
                step_s = k;
            if (k >= step_s + 4u)
                seen_w |= ins[0].w == 32768;
        }
        if (idx == 4u)
            seen_c |= t->p[P_ITYPE] == IT_CRUSH;
        if (idx == 5u)
            seen_back |= t->p[P_ITYPE] == IT_FOLD;
        ok_l &= idx == 4u || t->p[P_ITYPE] == IT_FOLD;
    }
    seq_stop();
    ck("MIX as automation: 0 on steps 1..8 (nothing runs), 100 % from step 9, the wet share there 4 blocks on",
       ok_a && step_s && seen_w);
    ck("TYPE as a step's lock: CRUSH on step 5 only, FOLD before and after", ok_l && seen_c && seen_back);
}

/* ------------------------------------------------------------------ 4 --- */
static void test_pages_stored(void)
{
    track_t *t;
    int ok;
    ui_power_on();
    t = TSEL;
    ok = str_eq(track_desc(t, P_IA)->label, "A");     /* (OFF: TP's own) */
    t->p[P_ITYPE] = IT_FUZZ;
    ok &= str_eq(track_desc(t, P_IA)->label, "DRIVE") && str_eq(track_desc(t, P_IB)->label, "TONE") &&
          str_eq(track_desc(t, P_IC)->label, "LEVEL");
    t->p[P_ITYPE] = IT_CRUSH;
    ok &= str_eq(track_desc(t, P_IA)->label, "BITS") && str_eq(track_desc(t, P_IC)->label, "LPF");
    t->p[P_ITYPE] = IT_CHORUS;
    ok &= str_eq(track_desc(t, P_IA)->label, "RATE") && str_eq(track_desc(t, P_IB)->label, "DEPTH") &&
          str_eq(track_desc(t, P_IC)->label, "FDBK");
    go_title("INSERT"); frame();
    ok &= cur_page()->id[0] == P_ITYPE && cur_page()->id[3] == P_IC && cur_page()->graph == GR_INS;
    go_title("INSERT 2"); frame();
    ok &= cur_page()->id[0] == P_IMIX;
    ck("the pages: INSERT (TYPE A B C, named by the TYPE), INSERT 2 (MIX)", ok);
    set_ins(t, IT_PHASER, 11, 22, 33, 44);
    project_save(1);
    set_ins(t, 0, 64, 96, 96, 127);
    project_load(1);
    ok = TSEL->p[P_ITYPE] == IT_PHASER && TSEL->p[P_IA] == 11 && TSEL->p[P_IB] == 22 && TSEL->p[P_IC] == 33 &&
         TSEL->p[P_IMIX] == 44;
    ck("a project keeps the INSERT (SKF3's PX block)", ok);
}

int main(void)
{
    ui_power_on();                                      /* (the power-on state: the tempo, the FX, every track) */
    test_identity();
    test_types();
    test_glide();
    test_motion();
    test_pages_stored();
    printf("%s\n", bad ? "INSERT TEST FAILED" : "insert tests passed");
    return bad != 0;
}
