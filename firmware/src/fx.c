/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses (HALL with its stereo difference), stereo dry mix. */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING, (Skiff phase 9) 2 HALL) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
    int32_t cho_y;                       /* (Skiff phase 9) the CHORUS bus: its last output (FEEDBACK), */
    int32_t ph_s[4];                     /* .. PHASER's allpasses */
    uint8_t dmode, dclr;                 /* (2.5) the DELAY's MODE running (DM_*); after a change, 1 + the 32nd of the line
                                          * being cleared (dly_clear), 0 none */
    int32_t dly_lpr, dpl, dpr, dmp, dsp;   /* (2.5) STEREO / PING: the right line's low-pass (dly_lp the left's), the
                                            * sends' last samples (1 2 1), the last mid and side out */
} fx;

/* SPRING (G_RTYPE 1): one spring of a spring tank, mono like the other buses, in the ROOM's own buffers (no
 * RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little bass) ->
 * SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel: below
 * fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more of
 * it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop
 * (the first sound 15 .. 30 ms after the send: the tank's own pre-delay; a slow wobble of a sample or two
 * on it), plus a second, quieter pickup at three quarters (a shorter spring beside it: denser). SIZE sets
 * the loop's length (30 .. 60 ms) and its decay; DAMP the loop's low-pass. The allpasses' states: 4
 * samples each (int32), in rev_ap's memory. Changing the model fades the old one's block out and clears both buffers. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 (0.7: Q12 keeps (x - o) * a in 32 bits up to
                                          * |x - o| < 749000, far past any peak the chain reaches) */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* HALL (G_RTYPE 2, 1.2; (Skiff #19) Felucca's, in place of phase 9's ROOM at half the rate, which was mono): a smooth,
 * dense, stereo reverb, again in ROOM's buffers (its few states and one block
 * of work in the pool, no .bss). ROOM's 4 combs each ring on their own (the metallic tone); here every echo
 * goes round 8 lines that all feed each other, and they run at half the rate (22.05 kHz: the same memory holds
 * twice the time, so twice the resonances, at half the work; the tail stops near 10 kHz, as a hall's air
 * does). The send (every other sample of it, low-passed 1 2 1; ROOM's level) -> a low cut (~110 Hz: no boom
 * in a long tail) -> 2 allpass diffusers (rev_ap: a drum's click smeared into a burst) -> into the 8 lines
 * (rev_comb, mutually prime lengths, 21 .. 34 ms). Lines 1, 2, 4, 7 are read through slow sines (+-HL_MOD
 * samples, between samples; 0.37 or 0.74 Hz, each in its own phase): the resonances drift instead of ringing.
 * Lines 0, 3, 5, 6, read at their ends, go through a one-pole low-pass (DAMP: the mixing takes every echo
 * through them); every
 * line through its gain (from its length: all decay alike; RT60 below the damping 0.5 s at SIZE 0, 4 s at
 * 90, 9 s at 127; ~2.4 s heard at 90 with DAMP 60); then the 8 x 8 mixing: a conference matrix (Paley,
 * order 8: every line feeds the 7 others, +-1 each, none itself: no flutter at a line's own length), with
 * the send added. Stereo: left lines 0 3 4 -7, right 1 2 5 6 (sums spread over the matrix's rows); the wet
 * bus (mono) gets left + right, rev_side left - right, which mix_block adds to the left and takes from the
 * right (rev_side_mix). Back to 44.1 kHz between samples. Rounding: a loud line's gain rounds to the nearest
 * (the tail decays as it should), a quiet one's towards zero (it ends in exact silence: no limit cycle), and
 * the low-passes keep their steps' remainders (no offset). */
#define HL_NL 8u                         /* lines */
#define HL_NAP 2u                        /* diffusers */
#define HL_NC (CTL / 2u)                 /* a block at half the rate */
#define HL_MOD 8                         /* the taps' swing, samples (read 2 .. 18 samples short of the end) */
#define HL_LPM 0x69u                     /* the lines with a low-pass */
#define HL_KD(d) (32767 - (d) * 180)     /* its coefficient (Q15) for DAMP d: none at 0 .. ~1 kHz at 127 */
#define HL_LOUD 32                       /* a line this loud (two samples' sum) rounds its gain to the nearest */
#define HL_OUT 4                         /* the outputs' gain: the level of ROOM's tail */
static const uint16_t HL_LEN[HL_NL] = {467, 509, 547, 593, 631, 673, 709, 743};   /* (+1 each: the mirror) */
static const uint16_t HL_AP[HL_NAP] = {131, 307};
static const int16_t HL_APG[HL_NAP] = {11469, 10240};               /* Q14: 0.7, 0.625 */
_Static_assert(467 + 509 + 547 + 593 + 631 + 673 + 709 + 743 + HL_NL <= sizeof rev_comb / 2u &&
               131 + 307 <= sizeof rev_u.ap / 2u, "HALL in ROOM's buffers");
_Static_assert(CTL % 2u == 0u && HL_NC + 2u * HL_MOD + 2u < 467u && HL_NC <= 131u, "HALL: one wrap a block at most");
static struct {                          /* HALL's state (in the pool: no .bss) */
    uint16_t i[HL_NL], d[HL_NAP];        /* the lines' write indices, the diffusers' */
    int32_t lp[HL_NL], le[HL_NL];        /* the low-passes and their steps' remainders */
    int32_t x, m, s, hp, he;             /* the send's last sample, its low cut and that step's remainder; the
                                          * last mid and side out (half rate) */
    int16_t g[HL_NL], sz;                /* the lines' gains / sqrt(7) (Q14), for SIZE sz - 1 (0: none yet) */
    uint32_t ph;                         /* the taps' drift */
    uint8_t side;                        /* sd holds this block's stereo difference (HALL running) */
    int32_t sd[CTL];                     /* the block's left minus right (mix_block: rev_side_mix) */
    int32_t bx[HL_NC], l[HL_NC], r[HL_NC];   /* the block at half the rate: the diffused send, left, right */
    int16_t y[HL_NL][HL_NC];             /* the lines' ends, then what goes back into them */
} hl __attribute__((section(".pool")));
#define rev_side (hl.sd)

/* (Skiff 1.3) FX > INSERT's TYPEs (track_insert, below); (2.0, D 2A) the drives' and CRUSH's shapes are DRIVE's too */
enum { IT_OFF, IT_SOFT, IT_HARD, IT_FOLD, IT_FUZZ, IT_CRUSH, IT_PHASER, IT_FLANGER, IT_CHORUS, IT_FILTER, IT_N };   /* ((2.5)
                                                     * FILTR, the user's 6B: stored, appended) */
/* the drives' and CRUSH's shape of v (a voice's full scale 32768; v already through the drive): the curve the INSERT
 * and DRIVE graphs draw too (ui_pgraph.c graph_insert, graph_drive) */
static inline int32_t ins_shape(uint32_t ty, int32_t v, uint32_t sh)
{
    switch (ty) {
    case IT_SOFT:
        return softclip(v);
    case IT_HARD:
        return clamp(v, -24000, 24000);
    case IT_FOLD: {
        int32_t u = (int32_t)((uint32_t)(v + 16384) & 0xFFFFu) - 32768;   /* a triangle of period 65536: +-16384 */
        u = 16384 - (u < 0 ? -u : u);
        return u + (u >> 1);                            /* x 1.5 (no divide) */
    }
    case IT_FUZZ:
        return v > 0 ? (v > 20000 ? 20000 : v) : softclip(v) >> 1;
    case IT_CRUSH:                                      /* sh: the bits taken (16 - BITS); to the nearest step */
        return ((v + ((1 << sh) >> 1)) >> sh) << sh;
    default:
        return v;
    }
}

/* (Skiff phase 9) FX > DRIVE: DIST's kind (P_DTYPE) at the same drive (P_DIST), and its TONE (P_DTONE, -64..63: the
 * tone low-pass moved from where the drive puts it; 0 as before). (2.0, D 2A) The kinds are INSERT's shapers
 * (ins_shape): SOFT the soft clip with DIST's bias (a little: even harmonics), as before; HARD a flat clip at 24000 (odd
 * harmonics: a buzz); FOLD a triangle wavefolder, a fold every 4 x 16384 (the louder, the more folds: bright, hollow);
 * CRUSH (BITS until 2.0, the same) the drive as bits and rate taken away, up to 11 bits, each sample held 1 2 4 or 8 (a
 * run starts every block: CTL is a multiple of 8; aliasing, grit), its low-pass closing less; FUZZ (2.0) flat on top,
 * soft and halved below. Each after the low cut, before the two-pole tone and the make-up, as SOFT. (HARD and FOLD
 * until 2.0 were DIST's own: the ceiling 24576, the folds x 2) */
enum { DT_SOFT, DT_HARD, DT_FOLD, DT_CRUSH, DT_FUZZ, DT_N };
#define DIST_TONE(k, t) clamp((k) + (t)->p[P_DTONE] * 150, 2000, 32700)   /* TONE's move of the low-pass (Q15) */
/* HARD, FOLD, FUZZ: the drive into INSERT's shaper ty (a constant: a loop each, no switch a sample) */
static inline __attribute__((always_inline)) void dist_clip(track_t *t, int32_t *b, uint32_t n, int32_t g, int32_t k,
                                                             int32_t mk, uint32_t ty)
{
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;
        x = clamp(x - t->dist_hp, -230000, 230000);
        y = ins_shape(ty, ((x >> 2) * g) >> 10, 0);
        t->dist_dc += (y - t->dist_dc + 256) >> 9;      /* (~14 Hz: a clip of an uneven wave has a mean, kept out of */
        y -= t->dist_dc;                                /* the buses' feedback) */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}
static __attribute__((noinline)) AUDIO_RAM void dist_hard(track_t *t, int32_t *b, uint32_t n, int32_t g, int32_t k, int32_t mk)
{
    dist_clip(t, b, n, g, k, mk, IT_HARD);
}
static __attribute__((noinline)) AUDIO_RAM void dist_fold(track_t *t, int32_t *b, uint32_t n, int32_t g, int32_t k, int32_t mk)
{
    dist_clip(t, b, n, g, k, mk, IT_FOLD);
}
static __attribute__((noinline)) AUDIO_RAM void dist_fuzz(track_t *t, int32_t *b, uint32_t n, int32_t g, int32_t k, int32_t mk)
{
    dist_clip(t, b, n, g, k, mk, IT_FUZZ);
}
static __attribute__((noinline)) AUDIO_RAM void dist_crush(track_t *t, int32_t *b, uint32_t n, int32_t d, int32_t k, int32_t mk)
{
    uint32_t s = (uint32_t)d / 11u, hm = (1u << ((uint32_t)d >> 5)) - 1u;   /* bits taken 0..11, held 1 2 4 8 */
    int32_t h = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t x = b[i];
        t->dist_hp += (x - t->dist_hp + 64) >> 7;
        x = clamp(x - t->dist_hp, -230000, 230000);
        if (!(i & hm))
            h = ins_shape(IT_CRUSH, x, s);              /* (rounded: no offset) */
        t->dist_lp1 += mulq15(h - t->dist_lp1, k);
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* DIST: low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain (straight into tanh over the full band, it would sound like a
 * broken digital fuzz). State per part (track_t dist_*). (Skiff phase 9) DRIVE's TYPE and TONE: above */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST], i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = DIST_TONE(32000 - d * 95, t);                    /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    switch (t->p[P_DTYPE]) {
    case DT_HARD: dist_hard(t, b, n, g, k, mk); return;
    case DT_FOLD: dist_fold(t, b, n, g, k, mk); return;
    case DT_CRUSH: dist_crush(t, b, n, d, DIST_TONE(32700 - d * 40, t), 32000 - d * 20); return;
    case DT_FUZZ: dist_fuzz(t, b, n, g, k, mk); return;
    default: break;
    }
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = ins_shape(IT_SOFT, (((x >> 2) * g) >> 10) + bias, 0) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
/* MENU > USB LEVEL FIXED (for #42: record over USB with the speaker turned down): the mix goes to master_out at the
 * full MASTER level, USB audio takes that (audio.c uac_tap), and only then does MASTER scale what the DAC gets
 * (usb_fixed_dac). MASTER (0, the default): MASTER before master_out, as always (USB follows the knob) */
static volatile uint8_t fx_usb_fixed;
#define MASTER_FULL 4096                /* main.c: the MASTER knob's top, Q12 */
/* the MASTER pot (ADC 0..1023, smoothed in main.c) to its level, square law: 0 .. 4088 (Q12) */
static inline uint32_t master_of_pot(uint32_t k10) { return (k10 * k10) >> 8; }
static uint8_t dac_lo[2u * CTL];       /* ((2.5, #110) the block's 7 bits below Q15 (master_q22, mix_block): audio.c */
                                       /* adds them under the DAC's 24 bits; USB's dither takes them (usb.c uac_dith) */
static __attribute__((noinline)) void usb_fixed_dac(int32_t *out, uint32_t n)   /* audio ISR, after uac_tap */
{
    uint32_t i;
    int32_t m = (int32_t)song.master_q12;
    for (i = 0; i < 2u * n; i++) {
        int32_t x = out[i] * m;                     /* (|out| <= 32767 after the soft clip: fits) */
        out[i] = x >> 12;
        dac_lo[i] = (uint8_t)((x >> 5) & 127);      /* ((2.5, #110) the 7 bits below, for the DAC) */
    }
}
static volatile uint8_t fx_lowcut;     /* settings: 1 LOWCUT 12 dB/oct ~110 Hz, 2 BASS+ (the small speaker):
                                        * 12 dB/oct ~220 Hz plus the harmonics of the bass (spk_bass) */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (A rounded step of (x - dc) / 4096 would stop moving at |x - dc| < 2048 and leave an offset.)
 * (2.5, #110) on the whole Q22 value, as SPEAKER EQ's high-passes (lowcut1): an estimate of the Q15 part, taken away
 * in whole Q15 steps, is an error of up to a step that moves with the wave, harmonics at a fixed level after MASTER */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = x - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - *dc;
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err, uint32_t sh)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> sh;
    *err = e - (d << sh);
    *lc += d;
    return x - *lc;
}

/* BASS+: what the speaker cannot play, heard through its harmonics. The bass below ~150 Hz is clipped at its
 * own envelope (a level-following trapezoid: odd harmonics), then band-passed ~220 Hz..1 kHz and added.
 * The low-pass has 4 poles (#42: with 2, the trapezoid rebuilt the 300 .. 600 Hz of the mix itself, late,
 * and cancelled up to 6 dB of it; now under 0.5 dB) */
static int32_t sb_lp1, sb_lp2, sb_lp3, sb_lp4, sb_env, sb_h1, sb_h2, sb_hl;
static inline int32_t spk_bass(int32_t m)
{
    int32_t a, t, u;
    sb_lp1 += ((m - sb_lp1) * 692) >> 15;
    sb_lp2 += ((sb_lp1 - sb_lp2) * 692) >> 15;
    sb_lp3 += ((sb_lp2 - sb_lp3) * 692) >> 15;
    sb_lp4 += ((sb_lp3 - sb_lp4) * 692) >> 15;
    a = sb_lp4 < 0 ? -sb_lp4 : sb_lp4;
    if (a > sb_env)
        sb_env += (a - sb_env) >> 2;
    else if (sb_env > 0)
        sb_env -= (sb_env >> 11) + 1;
    t = clamp(sb_lp4 * 8, -sb_env, sb_env);
    sb_h1 += (t - sb_h1) >> 5;
    u = t - sb_h1;
    sb_h2 += (u - sb_h2) >> 5;
    u -= sb_h2;
    sb_hl += (u - sb_hl) >> 3;
    return sb_hl + (sb_hl >> 1);      /* x1.5 (Felucca #180: x3 peaked at 1.5 .. 3.5 x a full-scale kick, the */
}                                     /* limiter pulled the mix down up to 16 dB on each hit and the buzz took over) */

/* (2.5, #110) softclip of a Q22 value (Q15 x 128): the same table, interpolated over all 15 bits under each of its
 * steps. softclip's own cut at Q15 (a floor, the error a function of the wave) was the last word before the DAC: a fading
 * note's harmonics stopped falling at ~-100 dBFS, whatever MASTER was */
static inline int32_t softclip22(int32_t x)
{
    int32_t a = x < 0 ? -x : x, i = a >> 15, y;
    if (i >= 256)
        y = TANH_Q15[256] << 7;
    else
        y = (TANH_Q15[i] << 7) + (((TANH_Q15[i + 1] - TANH_Q15[i]) * (a & 32767)) >> 8);
    return x < 0 ? -y : y;
}

/* the master: DC blocker, SPEAKER EQ, the limiter, the soft clip, on Q22 values in and out ((2.5, #110) MASTER's
 * product, fx.c master_q22). The DC blocker and SPEAKER EQ's high-passes take all 22 bits (BASS+'s harmonics are
 * made from the Q15 part); the limiter follows the Q15 part and, acting on a loud signal, drops the bits under it;
 * the soft clip takes all 22 bits */
static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a, xl = dc_block(*l, &dc_l, &dce_l), xr = dc_block(*r, &dc_r, &dce_r), ql, qr;
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        uint32_t sh = fx_lowcut == 2u ? 5u : 6u;    /* rounded step stopped at |x - lc| < 32: an offset) */
        int32_t b = fx_lowcut == 2u ? spk_bass(((xl >> 7) + (xr >> 7)) >> 1) * 128 : 0;
        xl = lowcut1(xl, &lc_l1, &lce[0], sh);
        xl = lowcut1(xl, &lc_l2, &lce[1], sh) + b;
        xr = lowcut1(xr, &lc_r1, &lce[2], sh);
        xr = lowcut1(xr, &lc_r2, &lce[3], sh) + b;
    }
    ql = xl >> 7;
    qr = xr >> 7;
    al = ql < 0 ? -ql : ql;
    ar = qr < 0 ? -qr : qr;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        xl = (((ql >> 4) * g) >> 11) * 128;              /* >> 4 first: |l| may be far above Q15 */
        xr = (((qr >> 4) * g) >> 11) * 128;
    }
    *l = softclip22(xl);
    *r = softclip22(xr);
}

/* length of one division (N_DIV order) in samples at the song tempo */
static const uint8_t DIV_DEN[6] = {1, 2, 4, 8, 3, 6};    /* original IDs stay fixed; slow rates append */
static uint32_t midi_beat_samples;                    /* zero until an external clock has a measured tempo */
static uint32_t beat_samples(void)
{
    return song.g[G_CLOCK] && midi_beat_samples ? midi_beat_samples : bpm_quarter((uint32_t)song.g[G_BPM]);   /* (#107) */
}
static uint32_t div_samples(uint32_t div)
{
    uint32_t quarter = beat_samples();
    return div < 6u ? quarter / DIV_DEN[div] : div < 10u ? quarter << (div - 5u) : quarter / DIV_DEN[div % 6u];
}

#include "perform.c"                                 /* the FX hold layer's effects (the master) */
#include "click.c"                                   /* the metronome's click (after the master: audio.c) */
#include "duck.c"                                    /* (Skiff) ducking: a track under another's hits (mix_part) */
#include "throw.c"                                   /* (Skiff) throws: the PERFORM layer's KNOB 1..4 (mix_block) */

/* the delay's TIME (params.c N_DTIME): N_DIV's ten (div_samples), then (Skiff 1.3, from Felucca 1.5.1) the dotted 1/8D
 * 1/16D 1/4D (10, 11, 12: 3/4, 3/8, 3/2 of a quarter); at most DLY_LEN - 1 samples: a long one clamps at slow tempos */
static const uint8_t DIV_DOT[3] = {2, 3, 1};           /* 3 quarters >> this */
static uint32_t delay_samples(void)
{
    uint32_t d = (uint32_t)song.g[G_DTIME];
    uint32_t s = d - 10u < 3u ? (beat_samples() * 3u) >> DIV_DOT[d - 10u] : div_samples(d);
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB: level as before the allpass fix */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v is a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* HALL's helpers: m / 2^sh rounded towards zero (a product that feeds back must not floor: a loop of floors
 * holds an offset and a little noise for ever) */
static inline int32_t sh_tz(int32_t m, uint32_t sh) { return (m + ((m >> 31) & ((1 << sh) - 1))) >> sh; }
/* 2^-x, x Q16 (0 .. 16): Q15 (32768 at x = 0). 2^-x = 2^u / 2^(i + 1), u = 1 - frac(x); 2^u by a cubic (1e-4) */
static uint32_t exp2_neg(uint32_t x)
{
    uint32_t i = x >> 16, u = 32768u - ((x & 0xFFFFu) >> 1), p = 2579u;
    p = ((p * u) >> 15) + 7409u;
    p = ((p * u) >> 15) + 22780u;
    p = ((p * u) >> 15) + 32768u;
    return i < 15u ? p >> (i + 1u) : 0u;
}
/* the lines' gains for SIZE s: RT60 = 0.5 s * 2^(s / 30); a line of D samples (22.05 kHz) loses 60 dB in
 * RT60 * 22050 / D passes: g = 2^(-D * 3 log2(10) / (22050 * RT60)) = 2^(-D * 59.2 / 65536 * 2^(-s / 30)),
 * at most 0.99, over sqrt(7) (the matrix's rows have 7 entries of +-1: / sqrt(7) makes it orthogonal) */
static __attribute__((noinline)) void hall_gains(int32_t s)
{
    uint32_t q = exp2_neg((uint32_t)s * 65536u / 30u), k, g;
    for (k = 0; k < HL_NL; k++) {
        g = exp2_neg((HL_LEN[k] * 59u * q) >> 15);
        hl.g[k] = (int16_t)(((g > 32440u ? 32440u : g) * 6193u) >> 15);
    }
    hl.sz = (int16_t)(s + 1);
}

/* an allpass diffuser over x[0 .. n) from q (its next samples, no wrap): its state rounded towards zero */
static inline void hall_ap(int16_t *q, int32_t g, int32_t *x, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t y = q[i], v = x[i] + sh_tz(y * g, 14);
        q[i] = (int16_t)clamp(v, -32768, 32767);
        x[i] = y - ((v * g + 8192) >> 14);
    }
}
/* a line's end over y[0 .. n) from b (no wrap; b[n] is still the line's: its mirror at the end): between b[i]
 * and b[i + 1], f / 256 of the way (rounded to the nearest: it stays between the two), times its gain g (Q14).
 * The gain's product: rounded to the nearest while the line is loud (lo 0: the tail decays as it should; a
 * truncation would make it die early, the sooner the quieter), towards zero once it is quiet (lo 1: the last
 * steps of the tail, which then ends in exact silence) */
static inline void hall_end(const int16_t *b, int32_t f, int16_t *y, uint32_t n, int32_t g, int lo)
{
    uint32_t i;
    if (lo)
        for (i = 0; i < n; i++)
            y[i] = (int16_t)sh_tz((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g, 14);
    else
        for (i = 0; i < n; i++)
            y[i] = (int16_t)(((b[i] + (((b[i + 1u] - b[i]) * f + 128) >> 8)) * g + 8192) >> 14);
}
/* a line's end over y[0 .. n) from b (no wrap), through the low-pass (kd, Q15; the state *lp and its step's
 * remainder *le), times its gain g (rounded as hall_end's) */
static inline void hall_end_lp(const int16_t *b, int16_t *y, uint32_t n, int32_t g, int lo, int32_t *lp, int32_t *le,
                               int32_t kd)
{
    uint32_t i;
    int32_t l = *lp, r = *le, e;
    if (lo)
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)sh_tz(l * g, 14);
        }
    else
        for (i = 0; i < n; i++) {
            e = (b[i] - l) * kd + r, r = e & 32767, l += e >> 15;
            y[i] = (int16_t)((l * g + 8192) >> 14);
        }
    *lp = l;
    *le = r;
}
/* the block's HL_NC samples of a circular buffer of len from index d in at most two runs, no wrap inside them:
 * body with s (the run's start in the block) and k (its length); d moves on */
#define HL_RUNS(d, len, k, s, body)                                                         \
    for (s = 0; s < HL_NC; s += k) {                                                        \
        k = (len) - (d) < HL_NC - s ? (len) - (d) : HL_NC - s;                              \
        body;                                                                               \
        d = (d) + k >= (len) ? 0u : (d) + k;                                                \
    }

/* HALL (see the top): a block of the send (n = CTL samples: audio.c) -> the mid added to out, the side into
 * rev_side. At half the rate (HL_NC samples) in passes over the block, each with few states (every line is
 * longer than a block: what the lines give in this block was written before it): the send, every other
 * sample, low cut, diffusers (hl.bx); the lines' ends at their drifting taps, low-passed, scaled (hl.y); the
 * outputs and the matrix; back into the lines; the outputs back to 44.1 kHz. */
#define HL_LFO ((uint32_t)(0.37 * CTL / FS * 4294967296.0))   /* the drift: 0.37 Hz (even lines), 0.74 Hz (odd) */
static __attribute__((noinline)) void rev_hall(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    int16_t *b = rev_comb, *a = rev_ap;
    uint32_t i, k, s, m, d;
    int32_t kd = HL_KD(song.g[G_RDAMP]);
    int32_t xp = hl.x, hp = hl.hp, he = hl.he, mp = hl.m, sp = hl.s;
    (void)n;
    if (hl.sz != song.g[G_RSIZE] + 1)
        hall_gains(song.g[G_RSIZE]);
    for (i = 0; i < HL_NC; i++) {
        int32_t x = (xp + 2 * rev_in[2u * i] + rev_in[2u * i + 1u]) >> 2, v;   /* every other sample, 1 2 1 */
        xp = rev_in[2u * i + 1u];
        x = mulq15(clamp(x, -262143, 262143), 2580);    /* (ROOM's level) */
        v = x - hp + he;                                /* the low cut, its step's remainder kept */
        he = v & 31;
        hp += v >> 5;
        hl.bx[i] = x - hp;
    }
    hl.x = xp, hl.hp = hp, hl.he = he;
    for (k = 0; k < HL_NAP; k++) {                      /* the diffusers */
        d = hl.d[k];
        HL_RUNS(d, HL_AP[k], m, s, hall_ap(a + d, HL_APG[k], hl.bx + s, m));
        hl.d[k] = (uint16_t)d;
        a += HL_AP[k];
    }
    hl.ph += HL_LFO;
    for (k = 0; k < HL_NL; k++) {                       /* the lines' ends */
        int32_t g = hl.g[k], f = 0, lo;
        if ((HL_LPM >> k) & 1u)
            d = hl.i[k];                                /* (low-passed: the line's end) */
        else {                                          /* (drifting: HL_MOD + 2 +- HL_MOD samples short of it) */
            uint32_t r = (uint32_t)(((HL_MOD + 2) << 8) +
                                    ((osc_sine(hl.ph * (1u + (k & 1u)) + k * 0x2C000000u) * HL_MOD) >> 7));
            f = (int32_t)(r & 255u);
            d = hl.i[k] + (r >> 8);
            d = d >= HL_LEN[k] ? d - HL_LEN[k] : d;
        }
        s = d + HL_LEN[k] / 2u;                         /* quiet? (two samples half a line apart) */
        s = s >= HL_LEN[k] ? s - HL_LEN[k] : s;
        lo = (b[d] < 0 ? -b[d] : b[d]) + (b[s] < 0 ? -b[s] : b[s]) < HL_LOUD;
        if ((HL_LPM >> k) & 1u) {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end_lp(b + d, hl.y[k] + s, m, g, lo, &hl.lp[k], &hl.le[k], kd));
        } else {
            HL_RUNS(d, HL_LEN[k], m, s, hall_end(b + d, f, hl.y[k] + s, m, g, lo));
        }
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {
        int32_t y0 = hl.y[0][i], y1 = hl.y[1][i], y2 = hl.y[2][i], y3 = hl.y[3][i];
        int32_t y4 = hl.y[4][i], y5 = hl.y[5][i], y6 = hl.y[6][i], y7 = hl.y[7][i], x = hl.bx[i];
        int32_t t, p0, p1, p2, p3, p4, p5, p6;
        hl.l[i] = y0 + y3 + y4 - y7;                    /* the outputs */
        hl.r[i] = y1 + y2 + y5 + y6;
        /* the matrix: C = [0 1; -1 Q], Q (7 x 7) = chi(j - i), the quadratic character mod 7 (+1 at distances
         * 1 2 4, -1 at 3 5 6): row 1 + i of Q y = 2 (the three at +1) - (the sum of the 7) + y_1+i */
        t = y1 + y2 + y3 + y4 + y5 + y6 + y7;
        p0 = y2 + y3 + y5, p1 = y3 + y4 + y6, p2 = y4 + y5 + y7, p3 = y5 + y6 + y1;
        p4 = y6 + y7 + y2, p5 = y7 + y1 + y3, p6 = y1 + y2 + y4;
        y0 += t;
        hl.y[0][i] = (int16_t)clamp(t + x, -32768, 32767);   /* (+ the send: signs spread over the rows) */
        hl.y[1][i] = (int16_t)clamp(2 * p0 + y1 - y0 + x, -32768, 32767);
        hl.y[2][i] = (int16_t)clamp(2 * p1 + y2 - y0 + x, -32768, 32767);
        hl.y[3][i] = (int16_t)clamp(2 * p2 + y3 - y0 + x, -32768, 32767);
        hl.y[4][i] = (int16_t)clamp(2 * p3 + y4 - y0 + x, -32768, 32767);
        hl.y[5][i] = (int16_t)clamp(2 * p4 + y5 - y0 + x, -32768, 32767);
        hl.y[6][i] = (int16_t)clamp(2 * p5 + y6 - y0 + x, -32768, 32767);
        hl.y[7][i] = (int16_t)clamp(2 * p6 + y7 - y0 - x, -32768, 32767);
    }
    b = rev_comb;
    for (k = 0; k < HL_NL; k++) {                       /* back into the lines (and the mirror after a wrap) */
        d = hl.i[k];
        HL_RUNS(d, HL_LEN[k], m, s, for (i = 0; i < m; i++) b[d + i] = hl.y[k][s + i]; if (!d) b[HL_LEN[k]] = b[0]);
        hl.i[k] = (uint16_t)d;
        b += HL_LEN[k] + 1u;
    }
    for (i = 0; i < HL_NC; i++) {                       /* back to 44.1 kHz, between samples */
        int32_t mm = (hl.l[i] + hl.r[i]) * HL_OUT, ss = (hl.l[i] - hl.r[i]) * HL_OUT;
        out[2u * i] += (mp + mm) >> 1;
        out[2u * i + 1u] += mm;
        rev_side[2u * i] = (sp + ss) >> 1;
        rev_side[2u * i + 1u] = ss;
        mp = mm;
        sp = ss;
    }
    hl.m = mp, hl.s = sp;
    hl.side = 1;
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept (as */
        fx.sp_he = o & 63;                              /* dc_block): no dead band to hold an offset in the loop */
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;         /* towards 0, as the loop's gain: floors would feed */
            o = ap[j + k];                              /* the loop a little offset and noise for ever */
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one of) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++)
        fx.comb_lp[i] = 0;
    for (i = 0; i < HL_NL; i++)
        hl.lp[i] = hl.le[i] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
    hl.x = hl.m = hl.s = hl.hp = hl.he = 0;
}
static void rev_model(uint32_t m, const int32_t *rev_in, int32_t *out, uint32_t n)   /* G_RTYPE m, added to out */
{
    if (m == 1u)
        rev_spring(rev_in, out, n);
    else if (m == 2u)
        rev_hall(rev_in, out, n);
    else
        rev_room(rev_in, out, n);
}

/* (Skiff phase 9) the CHORUS bus in its MODE (G_CMODE) with FEEDBACK (G_CFDBK, 0 .. 127: up to 0.93 of its output fed
 * back in), wet into wet (its part: the delay's is added after). CHORUS: the modulated delay of 5 .. 15 ms as before.
 * FLANGER: the same line swept 1 .. 7 ms, its comb against the dry tracks the jet. PHASER: four first-order allpasses on
 * the send, swept together by the chorus LFO once a block (RATE; DEPTH the sweep), notches against the dry. CHORUS
 * without FEEDBACK is the loop in fx_buses, bit for bit. (2.5) In a stereo DELAY MODE CHORUS and FLANGER are cho_st's;
 * PHASER stays mono */
static inline int32_t cho_sweep(uint32_t mode, uint32_t ph, int32_t depth)   /* the tap, Q8 samples back */
{
    return mode == 1u ? (44 << 8) + ((osc_sine(ph) + 32768) * depth >> 7)   /* FLANGER: 1 .. 7 ms */
                      : (400 << 8) + ((osc_sine(ph) + 32768) * depth * 6 >> 8);   /* CHORUS: 9 .. 18 ms */
}
static inline int32_t cho_read(int32_t r)               /* the line r (Q8) back, between samples */
{
    uint32_t ri = (uint32_t)r >> 8;
    int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
    int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
    return c0 + (((c1 - c0) * f) >> 8);
}
static __attribute__((noinline)) void cho_mode(const int32_t *cho_in, int32_t *wet, uint32_t n, uint32_t cinc)
{
    uint32_t i, k, mode = (uint32_t)song.g[G_CMODE];
    int32_t fb = song.g[G_CFDBK] * 240, depth = song.g[G_CDEPTH];
    if (mode == 2u) {                                   /* PHASER: a = -0.95 .. -0.15 by the LFO (Q15) */
        int32_t lfo = (osc_sine(fx.cho_ph) + 32768) >> 1, a;   /* 0 .. 32767 */
        fx.cho_ph += cinc * n;
        a = -31130 + mulq15(lfo, 3000 + depth * 205);
        for (i = 0; i < n; i++) {
            int32_t x = (cho_in[i] >> 1) + mulq15(fx.cho_y, fb);
            for (k = 0; k < 4u; k++) {                  /* (a + z^-1) / (1 + a z^-1): w = x - a w1, y = a w + w1 */
                int32_t w = clamp(x - mulq15(fx.ph_s[k], a), -262144, 262144);
                x = mulq15(w, a) + fx.ph_s[k];
                fx.ph_s[k] = w;
            }
            fx.cho_y = clamp(x, -65536, 65536);
            wet[i] = fx.cho_y << 1;
        }
    } else
    for (i = 0; i < n; i++) {                           /* CHORUS (with FEEDBACK), FLANGER */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp((cho_in[i] >> 1) + mulq15(fx.cho_y, fb), -32768, 32767);
        fx.cho_ph += cinc;
        fx.cho_y = cho_read(cho_sweep(mode, fx.cho_ph, depth));
        fx.cho_w++;
        wet[i] = fx.cho_y << 1;
    }
}

/* (2.5) CHORUS (any FEEDBACK) and FLANGER in a stereo DELAY MODE (fx_buses): the line read twice, the sweep half a cycle
 * apart; their mean into wet and the feedback, their difference into sd (to the left, from the right: rev_side_mix).
 * Its own loop: the mono ones stay as they were */
static __attribute__((noinline)) void cho_st(const int32_t *cho_in, int32_t *wet, int32_t *sd, uint32_t n, uint32_t cinc)
{
    uint32_t i, mode = (uint32_t)song.g[G_CMODE];
    int32_t fb = song.g[G_CFDBK] * 240, depth = song.g[G_CDEPTH];
    for (i = 0; i < n; i++) {
        int32_t y, y2;
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp((cho_in[i] >> 1) + mulq15(fx.cho_y, fb), -32768, 32767);
        fx.cho_ph += cinc;
        y = cho_read(cho_sweep(mode, fx.cho_ph, depth));
        y2 = cho_read(cho_sweep(mode, fx.cho_ph + 0x80000000u, depth));
        sd[i] += y - y2;
        fx.cho_y = (y + y2) >> 1;
        fx.cho_w++;
        wet[i] = fx.cho_y << 1;
    }
}

/* (2.5) the DELAY in MONO (G_DMODE; every project before 2.5): one line at the full rate, its feedback through COLOR's
 * low-pass; its return added to wet. 2.2's bit for bit (fx_buses and cho_mode each ran this loop; its own now, a block
 * at a time: tests/target_budget.txt) */
static __attribute__((noinline)) void dly_mono(const int32_t *in, int32_t *wet, uint32_t n, int32_t fb, int32_t col,
                                               int32_t dmix)
{
    uint32_t i, dl = delay_samples();
    for (i = 0; i < n; i++) {
        int32_t x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] = (int16_t)clamp((in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        wet[i] += mulq15(x << 1, dmix);
    }
}

/* (2.5) the DELAY in STEREO and PING (the user's pick: true stereo at half the rate): dly_buf as two lines, the left
 * then the right, at 22.05 kHz as HALL (the same 128 KiB, 1.49 s each; the echoes keep what is below ~10 kHz). The
 * sends, left and right (each track's PAN: mix_part), every other sample, 1 2 1; each line's feedback through its own
 * COLOR low-pass (its coefficient for two samples: 1 - (1 - c)^2). STEREO: each line its own side's send and its own
 * feedback, so an echo stays where its track is panned; PING: the sends' sum into the left, each line fed by the other's
 * end (the echoes cross, left, right, left ..). Their mean added to wet, half their difference into sd (to the left,
 * from the right: rev_side_mix); back to 44.1 kHz between samples. The loop's products round towards 0 (as SPRING's):
 * a floor would hold a small offset round the lines for ever (MONO's does, as 2.2's); these die to exact silence. A
 * block of CTL samples (audio.c) */
#define DLY_HALF (DLY_LEN / 2u)
static inline int32_t mulq15z(int32_t x, int32_t g)     /* x g in Q15, towards 0 (|x g| < 2^31) */
{
    int32_t v = x * g;
    return (v + ((v >> 31) & 32767)) >> 15;
}
static __attribute__((noinline)) void dly_stereo(const int32_t *inl, const int32_t *inr, int32_t *wet, int32_t *sd,
                                                 int32_t fb, int32_t col, int32_t dmix)
{
    uint32_t i, dl = delay_samples() >> 1, ping = fx.dmode == DM_PING;
    int32_t kd = 32768 - (2 * col - ((col * col) >> 15));   /* the low-pass for two samples: (1 - c)^2 of the step left */
    int32_t pl = fx.dpl, pr = fx.dpr, mp = fx.dmp, sp = fx.dsp;
    int16_t *bl = dly_buf, *br = dly_buf + DLY_HALF;
    for (i = 0; i < HL_NC; i++) {
        int32_t xl = (pl + 2 * inl[2u * i] + inl[2u * i + 1u]) >> 2, xr = (pr + 2 * inr[2u * i] + inr[2u * i + 1u]) >> 2;
        uint32_t w = fx.dly_w & (DLY_HALF - 1u), r = (fx.dly_w - dl) & (DLY_HALF - 1u);
        int32_t yl = bl[r], yr = br[r], m, s;
        pl = inl[2u * i + 1u];
        pr = inr[2u * i + 1u];
        fx.dly_lp = yl + mulq15z(fx.dly_lp - yl, kd);   /* (towards the line: it reaches it, no offset kept) */
        fx.dly_lpr = yr + mulq15z(fx.dly_lpr - yr, kd);
        if (ping) {
            bl[w] = (int16_t)clamp(((xl + xr) >> 2) + mulq15z(fx.dly_lpr, fb), -32768, 32767);
            br[w] = (int16_t)clamp(mulq15z(fx.dly_lp, fb), -32768, 32767);
        } else {
            bl[w] = (int16_t)clamp((xl >> 1) + mulq15z(fx.dly_lp, fb), -32768, 32767);
            br[w] = (int16_t)clamp((xr >> 1) + mulq15z(fx.dly_lpr, fb), -32768, 32767);
        }
        fx.dly_w++;
        yl = mulq15(yl << 1, dmix);
        yr = mulq15(yr << 1, dmix);
        m = (yl + yr) >> 1;
        s = (yl - yr) >> 1;
        wet[2u * i] += (mp + m) >> 1;
        wet[2u * i + 1u] += m;
        sd[2u * i] += (sp + s) >> 1;
        sd[2u * i + 1u] += s;
        mp = m;
        sp = s;
    }
    fx.dpl = pl, fx.dpr = pr, fx.dmp = mp, fx.dsp = sp;
}

/* (2.5) a MODE change: this block the old mode's return fades out (rendered apart, ramped in: as rev_switch); then the
 * line is cleared a 32nd a block (32 blocks, 23 ms with no echoes: the two layouts don't read each other's), and the new
 * mode starts from silence. dly_tw, dly_ts: that block's return and side */
#define DLY_NCLR 32u
static int32_t dly_tw[CTL] __attribute__((section(".pool")));
static int32_t dly_ts[CTL] __attribute__((section(".pool")));
static __attribute__((noinline)) void dly_switch(const int32_t *in, const int32_t *inr, int32_t *wet, int32_t *sd,
                                                 uint32_t n, int32_t fb, int32_t col, int32_t dmix)
{
    int32_t g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        dly_tw[i] = dly_ts[i] = 0;
    if (fx.dmode == DM_MONO)
        dly_mono(in, dly_tw, n, fb, col, dmix);
    else
        dly_stereo(in, inr, dly_tw, dly_ts, fb, col, dmix);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(dly_tw[i], (uint32_t)g);
        if (sd)
            sd[i] += mulq16(dly_ts[i], (uint32_t)g);
    }
    fx.dclr = 1;
}
static __attribute__((noinline)) void dly_clear(uint32_t want)   /* a 32nd of the line; the last: the new MODE */
{
    uint32_t i, at = (fx.dclr - 1u) * (DLY_LEN / DLY_NCLR);
    for (i = 0; i < DLY_LEN / DLY_NCLR; i++)
        dly_buf[at + i] = 0;
    if (++fx.dclr > DLY_NCLR) {
        fx.dclr = 0;
        fx.dmode = (uint8_t)want;
        fx.dly_lp = fx.dly_lpr = fx.dpl = fx.dpr = fx.dmp = fx.dsp = 0;
    }
}

#if FELUCCA_CPU2
/* (Skiff 1.3, as Felucca 1.6.1, after Jangada) the dry mix and the sends, as each core adds its tracks into them (mx[1]
 * is the second core's, mix_block), and a track's own block (b; mx[0].b also the fade of a reverb model change, after
 * the tracks) */
typedef struct { int32_t l[CTL], r[CTL], c[CTL], d[CTL], e[CTL], v[CTL], b[CTL]; } mixsum_t;   /* ((2.5) e: the delay's
                                                         * right send, a stereo MODE's; d its left, or MONO's) */
static mixsum_t mx[2];
#define mix_l (mx[0].l)
#define mix_r (mx[0].r)
#define send_c (mx[0].c)
#define send_d (mx[0].d)
#define send_e (mx[0].e)
#define send_r (mx[0].v)
#define part_buf (mx[0].b)
#define SM_DECL , mixsum_t *sm                          /* mix_part: the sums it adds into */
#define SM_ARG(x) , (x)
#define SM(f) (sm->f)
#else
/* one core: the arrays of 1.2, the same code (SM(l) = mix_l, ...; send_c .. mix_r are declared below) */
static int32_t part_buf[CTL];                            /* a part's block (mix_part); the fade of a model change */
#define SM_DECL
#define SM_ARG(x)
#define SM(f) SM1_##f
#define SM1_l mix_l
#define SM1_r mix_r
#define SM1_c send_c
#define SM1_d send_d
#define SM1_e send_e
#define SM1_v send_r
#define SM1_b part_buf
#endif

/* the model changed (to rt): the old one's block fades out (HALL's stereo difference with it), its buffers are
 * cleared, the new one starts from silence */
static __attribute__((noinline)) void rev_switch(const int32_t *rev_in, int32_t *wet, uint32_t n, int32_t rt)
{
    int32_t *t = part_buf, g = 65536, d = 65536 / (int32_t)n;
    uint32_t i;
    for (i = 0; i < n; i++)
        t[i] = 0;
    rev_model(fx.rtype, rev_in, t, n);
    for (i = 0; i < n; i++, g -= d) {
        wet[i] += mulq16(t[i], (uint32_t)g);
        rev_side[i] = mulq16(rev_side[i], (uint32_t)g);   /* (HALL only: hl.side set) */
    }
    rev_clear();
    fx.rtype = (uint8_t)rt;
}

/* process the three buses for one block; sends in (the delay's left and right: (2.5) dly_inr in a stereo MODE only),
 * wet out (the same to both sides; HALL's stereo difference in rev_side, hl.side set; (2.5) the chorus's and the
 * delay's in a stereo MODE too) */
static int32_t fx_sd[CTL] __attribute__((section(".pool")));   /* (2.5) the chorus's and the delay's left minus right */
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *dly_inr, const int32_t *rev_in,
                     int32_t *wet, uint32_t n)
{
    uint32_t i, want = (uint32_t)song.g[G_DMODE] <= DM_PING ? (uint32_t)song.g[G_DMODE] : DM_MONO;
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t depth = song.g[G_CDEPTH], rt;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    int32_t *sd = fx.dmode != DM_MONO ? fx_sd : 0;      /* ((2.5) a stereo MODE running: the sides) */
    perf_dub_dly(&fb, &dmix);                           /* ((phase 9) DUB's) */
    if (sd)
        for (i = 0; i < n; i++)
            sd[i] = 0;
    if (sd && song.g[G_CMODE] != 2)                     /* ((2.5) a stereo MODE: CHORUS, FLANGER in stereo) */
        cho_st(cho_in, wet, sd, n, cinc);
    else if (song.g[G_CMODE] || song.g[G_CFDBK])        /* (Skiff phase 9) another MODE, FEEDBACK: cho_mode */
        cho_mode(cho_in, wet, n, cinc);
    else
    for (i = 0; i < n; i++) {                           /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        wet[i] = cho_read(cho_sweep(0u, fx.cho_ph, depth)) << 1;   /* (Q8: read between samples) */
        fx.cho_w++;
    }
    if (fx.dclr)                                        /* ((2.5) the delay: its MODE changed, the line clearing; */
        dly_clear(want);
    else if (want != fx.dmode)                          /* changing now; */
        dly_switch(dly_in, dly_inr, wet, sd, n, fb, col, dmix);
    else if (sd)                                        /* STEREO, PING; */
        dly_stereo(dly_in, dly_inr, wet, sd, fb, col, dmix);
    else                                                /* MONO, as before) */
        dly_mono(dly_in, wet, n, fb, col, dmix);
    rt = song.g[G_RTYPE] == 1 || song.g[G_RTYPE] == 2 ? song.g[G_RTYPE] : 0;   /* (phase 9) HALL) */
    hl.side = 0;
    if (rt != fx.rtype)
        rev_switch(rev_in, wet, n, rt);
    else
        rev_model((uint32_t)rt, rev_in, wet, n);
    if (sd) {                                           /* ((2.5) the chorus's and the delay's sides, with HALL's) */
        for (i = 0; i < n; i++)
            rev_side[i] = (hl.side ? rev_side[i] : 0) + sd[i];
        hl.side = 1;
    }
}

/* after the buses: HALL's stereo difference to the left and from the right of the dry mix (the wet bus adds the
 * same to both) */
static __attribute__((noinline)) void rev_side_mix(int32_t *l, int32_t *r, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        l[i] += rev_side[i];
        r[i] -= rev_side[i];
    }
}

/* one block of the whole mix (shared with hostsim.c): events -> each part (with its modulation matrix)
 * -> dist -> SLICER -> level / pan / sends -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
#if FELUCCA_CPU2
static int32_t wet[CTL];                                 /* (the dry mix and the sends: mx[0], above) */
/* a track's cost for the second core's split (mix_split, after Jangada's and Felucca 1.6.1's): its voices x its engine's
 * cost per voice (ENG_VCOST: host instructions per sample per voice, Felucca's measures; DRUM-X as DRUM's), and its
 * DIST / SLICER while it sounds or its tail runs. ENGINES[] order. An estimate: it only steers the split, the sound does
 * not depend on it */
static uint16_t part_cost[NPART];                       /* the last block's, per track */
static const uint16_t ENG_VCOST[NENGINES] = {163, 0, 162, 67, 70, 174, 144, 143, 189, 309, 113, 134, 181, 129, 120};
_Static_assert(NENGINES == 15, "ENG_VCOST: a cost per engine");
#else
static int32_t send_c[CTL], send_d[CTL], send_e[CTL], send_r[CTL], wet[CTL], mix_l[CTL], mix_r[CTL];
#endif

/* (Skiff 1.3, from Felucca 1.5, its Discussions #78 and #177) INSERT: one effect per track, after DRIVE and before the
 * SLICER, with a dry / wet MIX. TYPE (P_ITYPE, N_ITYPE; stored, append-only) and its three values A B C (P_IA .. P_IC,
 * what they mean: params.c ins_desc):
 *   SOFT HARD FOLD FUZZ  DRIVE (1x .. ~13x, squared), TONE (two one-pole low-passes at F_CUTOFF), LEVEL (F_DB, as the
 *                        track's LEVEL: 112 is 0 dB). A slow low cut in front (~27 Hz), a DC blocker after (FUZZ is
 *                        lopsided). SOFT tanh, HARD a flat clip at a voice's level, FOLD a triangle folder (the wave
 *                        reflected at +-16384: every fold more harmonics), FUZZ a hard top and a soft, halved bottom;
 *   CRUSH                BITS (1 .. 16, of a voice's full scale), RATE (a held sample, 689 Hz .. 44.1 kHz, INS_HOLD:
 *                        aliasing as the old samplers), LPF (two one-pole low-passes at F_CUTOFF after them);
 *   PHASR                RATE (F_LFOHZ), DEPTH (the sweep around ~850 Hz: 0 still .. the whole 150 Hz .. 4.8 kHz),
 *                        FDBK (0 .. 0.7): four all-pass stages, mono, per track;
 *   FLANG CHOR           RATE, DEPTH, FDBK: a 256-sample line per track (5.8 ms); FLANG swept 0.25 .. 5 ms (a squared
 *                        triangle), feedback 0 .. 0.85; CHOR a sine around 3.2 ms (+-2.3 ms at DEPTH 127), feedback
 *                        0 .. 0.4. A DC blocker on the swept copy (a clipped loud input is lopsided).
 * The swept ones are the pedals': their wet is (dry + swept) / 2, the notches need both, so MIX 100 % is the whole
 * effect and less a lighter one. MIX (P_IMIX) blends dry and wet linearly; MIX 0 or TYPE OFF: the track bit for bit
 * as before (nothing runs). MIX glides (2.9 ms, SL_SLOPE); a TYPE change fades the old one out, clears the state,
 * fades the new one in; so does a start from MIX 0 (no stale line or filter).
 * Order: DRIVE -> INSERT -> SLICER: the insert colours the sound as DRIVE does; the SLICER's gate (and its STUT
 * recordings) comes after, so its edges stay sharp and a STUT repeats the processed sound. State in the pool, per track
 * (ins[], 580 B each: a track's render is one core's, so the second core needs no copy); noinline, its own loops: the
 * part's path gains one test (track_t ins_run). The TYPEs (IT_*) and the drives' shapes (ins_shape): above DRIVE's */
#define IL_LEN 256u                    /* FLANG / CHOR: the line, samples (a power of 2: masked) */
#define IN_ST 4u                        /* PHASR: all-pass stages */
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};   /* CRUSH RATE */
/* PHASR: the stages' coefficient a (Q14) for a break of 150 Hz * 2^(5 i / 32): a = (g - 1) / (g + 1), g = tan(pi f / fs) */
static const int16_t IN_PH_A[33] = {
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
typedef struct {
    int32_t w;                          /* the wet share now, Q15 */
    uint8_t type;                       /* the type running */
    uint8_t cnt;                        /* CRUSH: samples left of the held one */
    uint16_t wp;                        /* FLANG / CHOR: the line's next write */
    uint32_t ph;                        /* the sweep's phase */
    int32_t hp, lp1, lp2, dc, hold;     /* low cut (Q8), the two low-passes, DC blocker (Q8), CRUSH's held sample */
    int32_t px[IN_ST], py[IN_ST], fb;   /* PHASR: each stage's last input and output, the fed back */
    int16_t line[IL_LEN];               /* FLANG / CHOR, at an eighth (headroom; (2.5, #101) a quarter before) */
} ins_t;
static ins_t ins[NTRK] __attribute__((section(".pool")));

static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
/* a triangle of the sweep's place q, 0 .. 65534 (Q16) */
static inline uint32_t ins_tri(uint32_t q)
{
    uint32_t t = q >> 16;
    return t < 32768u ? t << 1 : (65535u - t) << 1;
}
/* the one-pole low-pass (topology-preserving) coefficient of cutoff index c, Q16: G = g / (1 + g), g = tan(pi fc / FS) */
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
static inline uint32_t ins_bits_sh(int32_t a) { return 15u - (uint32_t)(clamp(a, 0, 127) >> 3); }   /* BITS 1..16 */

/* SOFT .. FUZZ: low cut -> DRIVE -> shape -> TONE -> LEVEL -> DC blocker; CRUSH: held sample -> bits -> LPF */
static __attribute__((noinline)) void ins_drive(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ty = s->type, crush = ty == IT_CRUSH, sh = ins_bits_sh(t->p[P_IA]);
    uint32_t hold = INS_HOLD[clamp(t->p[P_IB], 0, 127) >> 3];
    int32_t a = t->p[P_IA], g = 4096 + a * a * 3;       /* DRIVE, Q12: 1x .. 12.8x, gentle at first */
    int32_t k = ins_lpk(crush ? t->p[P_IC] : t->p[P_IB]), lvl = crush ? 4096 : LEVEL_Q12[t->p[P_IC] & 127];
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y, v;
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        if (crush) {
            if (!s->cnt) {
                s->hold = ins_shape(IT_CRUSH, clamp(x, -262144, 262143), sh);
                s->cnt = (uint8_t)hold;
            }
            s->cnt--;
            y = s->hold;
        } else {
            s->hp += ((x << 8) - s->hp) >> 8;            /* ~27 Hz low cut (Q8: no offset left by the rounding) */
            v = clamp(x - (s->hp >> 8), -230000, 230000);
            y = ins_shape(ty, ((v >> 3) * g) >> 9, 0);    /* (>> 3 first: 32 bits) */
        }
        y <<= 4;                                        /* (x16: the low cutoffs' rounding leaves no offset) */
        v = mulq16(y - s->lp1, (uint32_t)k);             /* TONE / LPF: two one-pole low-passes */
        y = v + s->lp1;
        s->lp1 = y + v;
        v = mulq16(y - s->lp2, (uint32_t)k);
        y = v + s->lp2;
        s->lp2 = y + v;
        y = (y + 8) >> 4;
        if (!crush) {
            y = (y * lvl) >> 12;                        /* LEVEL */
            s->dc += ((y << 8) - s->dc) >> 9;           /* DC blocker, ~14 Hz (FUZZ's lopsided halves) */
            y -= s->dc >> 8;
        }
        b[i] = pf_mix(x, y, s->w);
    }
}

/* PHASR, FLANG, CHOR: the sweep at RATE (one LFO per track), DEPTH, FDBK */
static __attribute__((noinline)) void ins_swept(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, inc = LFO_INC[clamp(t->p[P_IA], 0, 127)] / CTL, dep = (uint32_t)clamp(t->p[P_IB], 0, 127);
    int32_t c = clamp(t->p[P_IC], 0, 127);
    if (s->type == IT_PHASER) {                         /* the coefficients a block at a time (at its middle) */
        uint32_t tr = ins_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)dep / 127);
        int32_t a = IN_PH_A[q >> 11] + (((IN_PH_A[(q >> 11) + 1u] - IN_PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
        int32_t fbk = c * 180;                          /* 0 .. 0.7, Q15 */
        for (i = 0; i < n; i++) {
            int32_t x = b[i], xs = clamp((x + 4) >> 3, -16383, 16383), u = xs + ((s->fb * fbk + 16384) >> 15), y = u;
            uint32_t st;
            s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
            for (st = 0; st < IN_ST; st++) {            /* y = a (u - y1) + u1 */
                y = clamp(s->px[st] + ((a * (u - s->py[st]) + 8192) >> 14), -65535, 65535);
                s->px[st] = u;
                s->py[st] = y;
                u = y;
            }
            s->fb = clamp(y, -32767, 32767);
            y <<= 2;                                    /* (y: at an eighth) */
            s->dc += ((y << 8) - s->dc) >> 9;           /* DC blocker, ~14 Hz (a clipped loud input is lopsided) */
            b[i] = pf_mix(x, (x >> 1) + y - (s->dc >> 8), s->w);   /* (dry + all-passed) / 2 */
        }
        s->ph += n * inc;
        return;
    }
    {
        int32_t chorus = s->type == IT_CHORUS, fbk = c * (chorus ? 103 : 219);   /* 0 .. 0.4 / 0 .. 0.85, Q15 */
        uint32_t dq = dep * 516u;                       /* DEPTH, Q16 */
        for (i = 0; i < n; i++, s->ph += inc) {
            int32_t x = b[i], r0, r1, v, fr;
            uint32_t d;                                 /* the delay, Q16 samples */
            s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
            if (chorus) {
                d = (uint32_t)((140 << 16) + ((osc_sine(s->ph) * (int32_t)dep * 50) >> 5));   /* 3.2 +- 2.3 ms */
            } else {
                uint32_t tr = ins_tri(s->ph), sw = (tr * tr) >> 16;   /* longer near the short end */
                d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);  /* 0.25 .. 5 ms */
            }
            r0 = (int32_t)(s->wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
            r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
            fr = (int32_t)((d >> 1) & 0x7FFFu);
            v = s->line[r0] + (((s->line[r1] - s->line[r0]) * fr) >> 15);
            s->line[s->wp] = (int16_t)clamp((x >> 3) + (softclip(mulq15(v, fbk) * 2) >> 1), -32767, 32767);   /* ((2.5,
                                                 * #101) an eighth: room to |x| 262143; a quarter clipped the loud chords
                                                 * of 18 of 150 presets at 131071, harsh as MIX rose, until they faded.
                                                 * The feedback's soft clip at the level it had: a quarter's) */
            s->wp = (uint16_t)((s->wp + 1u) & (IL_LEN - 1u));
            v <<= 2;                                    /* (v: at a half, as the dry's x >> 1) */
            s->dc += ((v << 8) - s->dc) >> 9;           /* DC blocker (the line's clip on a loud lopsided wave) */
            b[i] = pf_mix(x, (x >> 1) + v - (s->dc >> 8), s->w);   /* (dry + delayed) / 2 */
        }
    }
}

/* (2.5, the user's 6B) FILTR: ANALOG's 2-pole state-variable filter (dsp.c tsvf, trapezoidal: stable as CUTOFF sweeps)
 * at CUTOFF (A) and RES (B), its output by SHAPE (C): LP (0) into BP (64) into HP (127), the two beside it blended; BP
 * level-matched (k v1: at the cutoff as loud as LP's flat part), so a sweep of SHAPE keeps its level. The signal at a
 * half in it (its states within +-150000; RES's peak clips there, as ANALOG's); the states are lp1 and lp2: no new RAM */
static __attribute__((noinline)) void ins_filter(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    tsvf_t c;
    int32_t sh = clamp(t->p[P_IC], 0, 127), k = 8192 - clamp(t->p[P_IB], 0, 127) * 7600 / 127;   /* damping, Q12 */
    int32_t wl = sh < 64 ? (64 - sh) * 64 : 0, wh = sh > 64 ? (sh - 64) * 4096 / 63 : 0, wb = 4096 - wl - wh;   /* Q12 */
    uint32_t i;
    tsvf_coef_k(&c, clamp(t->p[P_IA], 0, 127) << 8, k);
    for (i = 0; i < n; i++) {
        int32_t x = b[i], in = clamp(x, -262143, 262143) >> 1, v3 = in - s->lp2, v1, v2, bp;   /* (|HP| x 4096 < 2^31) */
        s->w += clamp(tw - s->w, -SL_SLOPE, SL_SLOPE);
        v1 = (c.a1 * s->lp1 + c.a2 * v3 + 4096) >> 13;  /* (rounded: floored, the integrators turned its bias into a DC */
        v2 = s->lp2 + ((c.a2 * s->lp1 + c.a3 * v3 + 4096) >> 13);   /* of ~7 % at a low CUTOFF) */
        s->lp1 = clamp(2 * v1 - s->lp1, -150000, 150000);
        s->lp2 = clamp(2 * v2 - s->lp2, -150000, 150000);
        bp = (k * v1) >> 12;
        b[i] = pf_mix(x, (v2 * wl + bp * wb + (in - bp - v2) * wh) >> 11, s->w);   /* (Q12 shares, x2: the half back) */
    }
}

/* the INSERT at rest at once (mix_part: TYPE turned OFF while the part was silent, its fade never ran) */
static __attribute__((noinline)) AUDIO_RAM void ins_rest(track_t *t)
{
    memset(&ins[(uint32_t)(t - trk) % NTRK], 0, sizeof(ins_t));
    t->ins_run = 0;
}

/* the track's INSERT on its block b (mix_part): nothing while TYPE is OFF or MIX is 0 and faded */
static __attribute__((noinline)) void track_insert(track_t *t, int32_t *b, uint32_t n)
{
    ins_t *s = &ins[(uint32_t)(t - trk) % NTRK];
    int32_t ty = clamp(t->p[P_ITYPE], 0, IT_N - 1), tw;
    if (s->type != ty && !s->w)                         /* faded out: the new type from rest */
        s->type = (uint8_t)ty;
    tw = s->type == ty && ty ? ins_mixq(t->p[P_IMIX]) : 0;
    if (!s->w) {
        if (!tw) {
            t->ins_run = 0;
            return;                                     /* dry: bit for bit */
        }
        memset(s, 0, sizeof *s);                        /* starting: no stale line or filter */
        s->type = (uint8_t)ty;
    }
    if (s->type == IT_FILTER)
        ins_filter(s, t, b, n, tw);
    else if (s->type >= IT_PHASER)
        ins_swept(s, t, b, n, tw);
    else
        ins_drive(s, t, b, n, tw);
    t->ins_run = s->w != 0;                             /* (mix_part calls again while it fades) */
}

/* (Skiff 1.3, from Felucca's #148) SPREAD: a POLY or UNISON part whose SPREAD is above 0. Its voices start left and
 * right of PAN in turn (voice.c voice_start; UNISON's detuned voices alternate), at PAN -/+ SPREAD / 2 (127: hard left
 * and right), with the pan law PAN has (the far side down, the near one as it is). track_render gives every voice (b, as
 * always) and the left ones (spread_buf); here d = left - right, the side signal, so for a pair of pans L = b (gla +
 * glb) / 2 + d (gla - glb) / 2 (R alike): the mono sum is b's. b goes through DRIVE, the INSERT, the SLICER, the FX
 * layer's mute key and the DUCK as always; d gets the SLICER's gate, the mute's and the DUCK's gains and passes DRIVE
 * and the INSERT by (the spread of a driven chord is its voices' clean difference). The sends take b, mono, as before.
 * SPREAD 0, MONO or LEGATO: mix_part's own loop, bit for bit as before.
 * (2.0, #36) A drum kit with a lane panned (t->lpan_on, voice.c lpan_render) mixes here too: b is every lane, d (spread_buf)
 * what its left channel loses to the lanes panned right, e (lpan_buf) what the right loses to those panned left, each
 * through the same gains as d is; then the track's PAN over both. The kit's DRIVE, INSERT and sends hear b as before. (A
 * SPREAD a kit kept from another engine, hidden, spreads its hits as in 1.x until a lane is panned) */
/* .. (2.0, #36) a panned kit's end: each channel less its far side's sum (d the left's, e the right's), then the track's
 * PAN; the sends from b, mono, as mix_part's */
/* (2.5, #110) a track's sum x (Q15 voices, |x| < 2^20) at its LEVEL lvl (Q12, < 2^14), rounded: (x >> 2) * lvl (no
 * overflow) and the 2 bits that shift drops through their own product. Before, x >> 2 floored them away: a fading note
 * left in steps of 4, its error tied to the note. Held to 16 x full scale (never reached by music; sixteen voices at the
 * top LEVEL could), so the pans' Q12 products after it never wrap */
static inline int32_t lvl_mul(int32_t x, int32_t lvl)
{
    return clamp(((x >> 2) * lvl + (((x & 3) * lvl) >> 2) + 512) >> 10, -524287, 524287);
}
/* (2.5) the delay's send *dl (Q15) of a track at PAN gains gl, gr (Q12, as its dry's): in a stereo MODE (G_DMODE) the
 * left part stays in *dl and the right one is returned (a centred track sends whole to both); MONO: *dl as it is and 0,
 * nothing right (2.2's sums, bit for bit) */
static inline int32_t dly_send_r(int32_t *dl, int32_t gl, int32_t gr)
{
    int32_t d = *dl;
    if (!d || song.g[G_DMODE] == DM_MONO)
        return 0;
    *dl = (d * gl + 2048) >> 12;
    return (d * gr + 2048) >> 12;
}

static __attribute__((noinline)) AUDIO_RAM void lpan_mix(track_t *t, const int32_t *b, const int32_t *d, const int32_t *e,
                                               uint32_t n SM_DECL)
{
    int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
    int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
    int32_t c = t->p[P_CHOR] * 258, dl = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak, de;
    int32_t xmax = c > dl ? c : dl;
    uint32_t i;
    xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);
    de = dly_send_r(&dl, gl, gr);
    for (i = 0; i < n; i++) {
        int32_t x = lvl_mul(b[i], lvl), a = x < 0 ? -x : x, xs = clamp(x, -xmax, xmax);
        int32_t yl = lvl_mul(d[i], lvl), yr = lvl_mul(e[i], lvl);
        if (a > pk)
            pk = a;
        if (c)
            SM(c)[i] += mulq15(xs, c);
        if (dl)
            SM(d)[i] += mulq15(xs, dl);
        if (de)
            SM(e)[i] += mulq15(xs, de);
        if (r)
            SM(v)[i] += mulq15(xs, r);
        SM(l)[i] += ((x - yl) * gl + 2048) >> 12;
        SM(r)[i] += ((x - yr) * gr + 2048) >> 12;
    }
    t->peak = pk;
}
static __attribute__((noinline)) AUDIO_RAM void mix_spread(track_t *t, int32_t *b, uint32_t n SM_DECL)
{
    int32_t *d = SPREAD_BUF(t), *e = t->lpan_on ? LPAN_BUF(t) : 0;
    uint32_t i, k = (uint32_t)(t - trk);
    int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN], s = (t->p[P_SPRD] + 1) >> 1;
    int32_t pa = clamp(pan - s, -64, 63), pb = clamp(pan + s, -64, 63);
    int32_t gla = 4096 - (pa > 0 ? pa * 64 : 0), gra = 4096 + (pa < 0 ? pa * 64 : 0);
    int32_t glb = 4096 - (pb > 0 ? pb * 64 : 0), grb = 4096 + (pb < 0 ? pb * 64 : 0);
    int32_t glm = (gla + glb) >> 1, grm = (gra + grb) >> 1, gld = (gla - glb) >> 1, grd = (gra - grb) >> 1;
    int32_t c = t->p[P_CHOR] * 258, dl = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak, de;
    int32_t xmax = c > dl ? c : dl;
    xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);
    de = dly_send_r(&dl, glm, grm);                     /* ((2.5) a stereo MODE: by the spread's middle) */
    if (!e)
        for (i = 0; i < n; i++)
            d[i] = (d[i] << 1) - b[i];                  /* left - right */
    track_dist(t, b, n);
    if (t->p[P_ITYPE] | t->ins_run)
        AUDIO_FAR(track_insert)(t, b, n);               /* (the mono sum, as DRIVE) */
    AUDIO_FAR(slicer_track_sd)(t, b, d, e, n);
    if (((pf.mute >> k) & 1u) || t->p[P_MUTE] || pf.mg[k] != 32768) {   /* (the mute's ramp on both: from the same start,
                                                                          * the same; (2.0) a MUTE's too) */
        int32_t g0 = pf.mg[k];
        AUDIO_FAR(perf_mute)(k, b, n);
        pf.mg[k] = g0;
        AUDIO_FAR(perf_mute)(k, d, n);
        if (e) {
            pf.mg[k] = g0;
            AUDIO_FAR(perf_mute)(k, e, n);
        }
    }
    if ((duck_on >> k) & 1u) {                          /* (the DUCK's gains: the same on both) */
        duck_apply(k, b, n);
        duck_apply(k, d, n);
        if (e)
            duck_apply(k, e, n);
    }
    if (e) {
        lpan_mix(t, b, d, e, n SM_ARG(sm));
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t x = lvl_mul(b[i], lvl), a = x < 0 ? -x : x, y = lvl_mul(d[i], lvl);
        int32_t xs = clamp(x, -xmax, xmax);
        if (a > pk)
            pk = a;
        if (c)
            SM(c)[i] += mulq15(xs, c);
        if (dl)
            SM(d)[i] += mulq15(xs, dl);
        if (de)
            SM(e)[i] += mulq15(xs, de);
        if (r)
            SM(v)[i] += mulq15(xs, r);
        SM(l)[i] += ((x * glm + 2048) >> 12) + ((y * gld + 2048) >> 12);   /* ((2.5) rounded) */
        SM(r)[i] += ((x * grm + 2048) >> 12) + ((y * grd + 2048) >> 12);
    }
    t->peak = pk;
}

/* (Skiff, #21) a voice on a user sample's sound (eng_sample.c usr_mix; voice.c track_render, track k): rendered at the
 * sound's LEVEL (dsp.c vmod_level, as DRUM's lanes) into dst as any voice, and into the REVERB and DELAY sums mix_part
 * adds track k into (vs_sm) as much as the sound's REV and DLY say more than the track's sends (127 at most in all), at
 * the track's LEVEL and the gains its block ended on last time: the SLICER's gate and repeat, the FX layer's mute, the
 * DUCK (a block late: 0.7 ms). The sound as it plays, before DRIVE and the INSERT. (Out of line: the voices' loop in
 * mix_part stays as it was) */
#if FELUCCA_CPU2
static mixsum_t *vs_sm[NPART];                          /* (mix_part: mx[0] or the second core's mx[1]) */
#define VS_R(k) (vs_sm[k]->v)
#define VS_D(k) (vs_sm[k]->d)
#define VS_E(k) (vs_sm[k]->e)
#else
#define VS_R(k) send_r
#define VS_D(k) send_d
#define VS_E(k) send_e
#endif
/* (2.5) a stereo DELAY MODE: the voice's delay send to the right as well (fe: its share, smx_render); a loop of its own,
 * smx_render's as before */
static __attribute__((noinline)) void smx_right(int32_t *e, const int32_t *vb, int32_t fe, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        e[i] += ((vb[i] >> 2) * fe) >> 10;
}
static __attribute__((noinline)) void smx_render(track_t *t, voice_t *v, uint32_t k, int32_t *dst, uint32_t n,
                                                 const vmod_t *m)
{
    const int16_t *sx = &usr_mix[((uint32_t)v->s[4] >> 5) & 3u][SMX_REV][v->s[4] & 15];   /* (sx[-16] its LEVEL, sx[16]
                                                                                           * its DLY) */
    int32_t vb[CTL], g, fr, fd, fe, *r = VS_R(k), *d = VS_D(k), *e = VS_E(k), pan = t->p[P_PAN];
    uint32_t i;
    vmod_t ml;
    n = n > CTL ? CTL : n;                              /* (a block, as SPREAD's buffer) */
    AUDIO_FAR(spread_clear)(vb);
    ENGINES[t->engine]->render(t, v, vb, n, vmod_level(m, &ml, sx[-16]));
    g = (LEVEL_Q12[t->p[P_LEVEL] & 127] * (32768 - sl[k].gc)) >> 15;   /* Q12 */
    g = (g * (32768 - sl[k].w)) >> 15;
    g = (g * pf.mg[k]) >> 15;
    g = (g * (32768 - duck[k].r1)) >> 15;
    fr = (g * clamp(sx[0], 0, 127 - t->p[P_REV]) * 258) >> 15;
    fd = (g * clamp(sx[16], 0, 127 - t->p[P_DLY]) * 258) >> 15;
    fe = dly_send_r(&fd, 4096 - (pan > 0 ? pan * 64 : 0), 4096 + (pan < 0 ? pan * 64 : 0));   /* ((2.5) by the PAN) */
    for (i = 0; i < n; i++) {                           /* (|vb| < 2^16, f < 2^13: no overflow) */
        int32_t x = vb[i] >> 2;
        dst[i] += vb[i];
        r[i] += (x * fr) >> 10;
        d[i] += (x * fd) >> 10;
    }
    if (fe)
        smx_right(e, vb, fe, n);
}

/* one synth part into the dry mix and the sends (of sm: mx[0]; mx[1] on the second core); a part with no voice sounding
 * costs the LFO tick and a cleared buffer only (after the DIST tail has run out) */
#if FELUCCA_CPU2
static __attribute__((noinline)) AUDIO_RAM void mix_part(track_t *t, uint32_t n SM_DECL)   /* (either core runs it: out of
                                                                                          * the ISR; (2.5) from RAM) */
#else
static void mix_part(track_t *t, uint32_t n SM_DECL)
#endif
{
    int32_t *b = SM(b);
    uint32_t i;
#if FELUCCA_CPU2
    uint32_t nr;
#endif
    mod_begin(t);                                       /* the matrix's per-block values into t->p (mod.c) */
    t->lpan_on = drum_engine(t->engine) && lanes_panned(t);   /* ((2.0, #36) a kit with a lane panned: mix_spread) */
    t->spread_on = !t->lpan_on && t->p[P_SPRD] > 0 &&   /* ((1.3) mix_spread; (2.0) a panned kit's lanes, not SPREAD) */
                   (trk_vmode(t) == V_POLY || trk_vmode(t) == V_UNISON);
#if FELUCCA_CPU2
    vs_sm[(uint32_t)(t - trk) % NPART] = sm;            /* ((#21) smx_render's sums) */
    nr = track_render(t, b, n);
    part_cost[(uint32_t)(t - trk) % NPART] = (uint16_t)(nr * ENG_VCOST[t->engine % NENGINES] + (nr || t->tail ? 60u : 0u));
    if (nr)
#else
    if (track_render(t, b, n))
#endif
        t->tail = t->p[P_ITYPE] ? 64 : 16;              /* blocks of DIST / INSERT state to run out after the last
                                                         * voice ((1.3) the INSERT's: 46 ms, a flanger's feedback) */
    else if ((!t->tail || !(t->p[P_DIST] | t->p[P_ITYPE]) || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (t->ins_run && !t->p[P_ITYPE])
            ins_rest(t);                                /* ((1.3) TYPE OFF while silent: no fade-out left for the next
                                                         * note) */
        if (MOD(t).on)
            mod_end(t);
        return;
    }
    if (t->spread_on | t->lpan_on) {                   /* ((1.3) SPREAD: its own mix, mix_spread; (2.0) a kit panned) */
        mix_spread(t, b, n SM_ARG(sm));
        if (MOD(t).on)
            mod_end(t);
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak, de;
        int32_t xmax = c > d ? c : d;
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        de = dly_send_r(&d, gl, gr);                    /* ((2.5) a stereo MODE: the delay's send by the PAN) */
        track_dist(t, b, n);
        if (t->p[P_ITYPE] | t->ins_run)
            AUDIO_FAR(track_insert)(t, b, n);           /* ((1.3) the INSERT: off, nothing) */
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        if (((pf.mute >> (t - trk)) & 1u) || t->p[P_MUTE] || pf.mg[t - trk] != 32768)   /* perform.c (RAM): a black */
            AUDIO_FAR(perf_mute)((uint32_t)(t - trk), b, n);   /* key held, (2.0) a MUTE, the ramp back */
        if ((duck_on >> (t - trk)) & 1u)
            duck_apply((uint32_t)(t - trk), b, n);      /* (Skiff) duck.c: under its source's hits */
        for (i = 0; i < n; i++) {
            int32_t x = lvl_mul(b[i], lvl), a = x < 0 ? -x : x;   /* ((2.5, #110) rounded, no bits lost) */
            int32_t xs = clamp(x, -xmax, xmax);         /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                SM(c)[i] += mulq15(xs, c);
            if (d)
                SM(d)[i] += mulq15(xs, d);
            if (de)
                SM(e)[i] += mulq15(xs, de);
            if (r)
                SM(v)[i] += mulq15(xs, r);
            SM(l)[i] += (x * gl + 2048) >> 12;
            SM(r)[i] += (x * gr + 2048) >> 12;
        }
        t->peak = pk;
    }
    if (MOD(t).on)
        mod_end(t);                                     /* the stored values back */
}

/* ---- (Skiff 1.3, as Felucca 1.6.1, after Jangada's, github.com/zednaked/jangada, GPL-3.0) the second core
 * (hal/fm1_cpu1.h) renders some of the tracks of each block into its own sums (mx[1]) while this core renders the
 * others; then this core adds them in and goes on (the LFOs' blocks, the buses, the FX layer, the master) as with one
 * core. The sums are integers, so the order they add up in changes nothing: the output is bit for bit one core's. A
 * track's render touches its own state only (its track_t, its voices, its matrix: mod.c MOD, its engine's memory, its
 * SLICER, DUCK and DIST state) and what nobody writes meanwhile (song, the tables, the kits); what tracks share is done
 * before the split (events_block: the notes, voice stealing; the throws, the ducking's gains, the FX layer's
 * perf_begin) or after it (the LFOs' S&H draw from the shared generator, track by track: voice.c track_lfo_end).
 * tests/dualcore_test.c checks it, under ThreadSanitizer too. Without a second core (the host, the browser, MENU > CPU
 * 1 CORE, one that did not answer or stopped) C1_ON is 0 and this core renders them all */
#if FELUCCA_CPU2
#ifndef C1_SPLIT
#define C1_SPLIT() mix_split()
#endif
static uint32_t c1_blocks;                              /* blocks the second core took tracks of (console) */

static AUDIO_RAM void c1_parts(uint32_t arg)            /* arg: the tracks (bit k = track k) | n << 8 */
{
    uint32_t i, n = arg >> 8;
    mixsum_t *sm = &mx[1];
    for (i = 0; i < n; i++)
        sm->l[i] = sm->r[i] = sm->c[i] = sm->d[i] = sm->e[i] = sm->v[i] = 0;
    for (i = 0; i < NPART; i++)
        if (arg >> i & 1u)
            mix_part(&trk[i], n, sm);
}

/* which tracks go to the second core: the split of the last block's costs that leaves the busier core the least; none
 * when that saves less than handing them over costs (C1_HAND) */
#define C1_HAND 40u
static __attribute__((noinline)) uint32_t mix_split(void)
{
    uint32_t m, i, best = 0, all = 0, lo;
    for (i = 0; i < NPART; i++)
        all += part_cost[i];
    lo = all;
    for (m = 1; m < (1u << NPART) - 1u; m++) {
        uint32_t a = 0, w;
        for (i = 0; i < NPART; i++)
            if (m >> i & 1u)
                a += part_cost[i];
        w = (a > all - a ? a : all - a) + C1_HAND;
        if (w < lo) {
            lo = w;
            best = m;
        }
    }
    return best;
}
#endif

/* (2.5, #110) MASTER's product, kept below Q15: the mix x (Q15, |x| < 2^19) at mg (Q12) in Q22. master_out takes its
 * Q15 part; the 7 bits under it go to the DAC (dac_lo; audio.c: out << 7 | dac_lo) and dither USB's 16-bit word
 * (usb.c uac_dith). Before, (x >> 2) * mg >> 10 cut the mix to Q15 after MASTER's attenuation: a fading note at a
 * low MASTER reached the DAC in coarse steps, its error tied to the note (#110) */
static inline int32_t master_q22(int32_t x, int32_t mg)
{
    return ((x >> 2) * mg + (((x & 3) * mg) >> 2)) >> 3;
}

/* the master with the FX layer's effects between its level and master_out (perform.c) */
static __attribute__((noinline)) AUDIO_RAM void perf_master(int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    void (*mo)(int32_t *, int32_t *) = AUDIO_FAR(master_out);   /* (Skiff: XIP, from RAM) */
    for (i = 0; i < n; i++) {                           /* ((2.5) as mix_block: the Q15 part to the effects, the 7 bits */
        int32_t l = master_q22(mix_l[i] + wet[i], mg), r = master_q22(mix_r[i] + wet[i], mg);   /* under it kept: */
        mix_l[i] = l >> 7;                              /* a sample the effects leave is mix_block's to the bit) */
        mix_r[i] = r >> 7;
        dac_lo[2u * i] = (uint8_t)(l & 127);
        dac_lo[2u * i + 1u] = (uint8_t)(r & 127);
    }
    perf_block(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        int32_t l = mix_l[i] * 128 + dac_lo[2u * i], r = mix_r[i] * 128 + dac_lo[2u * i + 1u];   /* (master_out: Q22) */
        mo(&l, &r);
        out[2u * i] = l >> 7;
        out[2u * i + 1u] = r >> 7;
        dac_lo[2u * i] = (uint8_t)(l & 127);
        dac_lo[2u * i + 1u] = (uint8_t)(r & 127);
    }
}

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    int perf;
    int32_t mg = fx_usb_fixed ? MASTER_FULL : (int32_t)song.master_q12;   /* (USB LEVEL FIXED: MASTER after) */
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_e[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    if (thr_out)
        throw_on(n);                                    /* (Skiff) the throws on for the block, its events too */
    events_block(n);
    duck_block(n);                                      /* (Skiff) the ducking's gains (duck.c) */
    perf = AUDIO_FAR(perf_begin)(n);                    /* the FX hold layer at work (perform.c; its code in RAM) */
#if FELUCCA_CPU2
    {
        uint32_t m = C1_ON() ? C1_SPLIT() & ((1u << NPART) - 1u) : 0u;
        if (m && !C1_RUN(c1_parts, m | n << 8))
            m = 0;
        c1_blocks += m != 0;
        for (i = 0; i < NPART; i++)
            if (!(m >> i & 1u))
                AUDIO_FAR(mix_part)(&trk[i], n, &mx[0]);
        if (m) {
            const mixsum_t *sm = &mx[1];
            if (C1_WAIT())                              /* it stopped answering (held from now on): its tracks here */
                AUDIO_FAR(c1_parts)(m | n << 8);
            for (i = 0; i < n; i++) {
                mix_l[i] += sm->l[i];
                mix_r[i] += sm->r[i];
                send_c[i] += sm->c[i];
                send_d[i] += sm->d[i];
                send_e[i] += sm->e[i];
                send_r[i] += sm->v[i];
            }
        }
        for (i = 0; i < NPART; i++)                     /* the LFOs' blocks, track by track (voice.c) */
            track_lfo_end(&trk[i]);
    }
#else
    for (i = 0; i < NPART; i++)
        mix_part(&trk[i], n);
#endif
    if (thr_on)
        throw_off();                                    /* (Skiff) .. and off: nothing else sees them */
    if (perf)                                           /* ((2.5) a stereo delay: THROW and DUB left and right) */
        AUDIO_FAR(perf_pre)(mix_l, mix_r, send_d, song.g[G_DMODE] != DM_MONO ? send_e : 0, send_r, n);
    fx_buses(send_c, send_d, send_e, send_r, wet, n);
    if (hl.side)
        rev_side_mix(mix_l, mix_r, n);                  /* HALL, (2.5) a stereo delay and chorus: stereo */
    if (perf) {
        AUDIO_FAR(perf_master)(out, n);
        return;
    }
    for (i = 0; i < n; i++) {
        int32_t l = master_q22(mix_l[i] + wet[i], mg), r = master_q22(mix_r[i] + wet[i], mg);   /* ((2.5, #110) Q22) */
        master_out(&l, &r);
        out[2u * i] = l >> 7;
        out[2u * i + 1u] = r >> 7;
        dac_lo[2u * i] = (uint8_t)(l & 127);
        dac_lo[2u * i + 1u] = (uint8_t)(r & 127);
    }
}
