/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (2.5, #110) Host test: a fading note keeps its shape to the DAC and to USB, through the real audio path (hostsim.c,
 * audio.c audio_block):
 *   build/host/tail_test
 * ANALOG SUB BASS (a sine through the filter, as in #110: its own H3 at ~-35 dB) at C4, sends off, a long release, at
 * MASTER 100 % and 25 %. Every 4096 samples of its tail, the 3rd harmonic against the fundamental (Goertzel), on the
 * DAC's 24-bit words and on USB's 16-bit ones. A cut whose error is tied to the note shows as a harmonic that stops
 * falling while the note falls: its share grows. 2.2 (2 bits floored before the track's LEVEL and before MASTER, the
 * voice's level in steps of 2, the soft clip's own Q15 cut last, USB's plain cut) let H3 / H1 rise from -35 dB to -26
 * dB at 50 dB down (MASTER 100 %) and to -10 dB at 60 dB down (25 %). Checked: from 10 dB down to 30, 50 and 60 dB down,
 * H3 / H1 grows by 3 dB at most, on the DAC and coherently averaged USB. Single-pass USB is printed diagnostically:
 * at the quietest windows its random dither exceeds H3. Averaging signed samples preserves deterministic distortion.
 * (2.5, #110 on the unit) with each SPEAKER EQ too: LOWCUT's and BASS+'s high-passes and the DC blocker took away
 * estimates of the Q15 part only, an error of up to a step at a fixed level after MASTER (H3 / H1 +18 dB and +20 dB at
 * 25 %; H2 at -109 dBFS); and H2 is printed (the voice's own, before MASTER, grows too, at ~-130 dBFS: not checked).
 * -DOLD_TREE=1 builds it against firmware without dac_lo / uac_dith (prints only: the numbers before)
 * Skiff: round render buffers to CTL, coherently average USB dither and reject nonfinite measurements. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define FM1_AUDIO_HALF 0x80u
#define FM1_TICKS_PER_US 1u
static volatile uint32_t t5_nested_ticks;
static uint32_t fm1_ticks(void) { return 0; }
static uint8_t fm1_audio_pending(void) { return 0; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return 0; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *b, uint32_t n, void (*isr)(void), uint32_t p) { (void)b; (void)n; (void)isr; (void)p; }
void isr_alnk0(void) {}
#define FELUCCA_UAC 1                                  /* audio.c taps USB: a stub (the words are made here, below) */
static void uac_tap(const int32_t *out, const uint8_t *lo, uint32_t n) { (void)out; (void)lo; (void)n; }
static void uac_render_start(void) {}
#include "../firmware/src/audio.c"

#define WIN 4096u
#define SECS 14u
#define TAIL_SAMPLES (((SECS * 44100u + CTL - 1u) / CTL) * CTL)
#define USB_PASSES 256u
/* Test-only mutation seam: no production quantizer is changed. */
#ifndef TAIL_USB_MUTATION
#define TAIL_USB_MUTATION 0
#endif
static int32_t t_dac[TAIL_SAMPLES], t_usb[TAIL_SAMPLES];

static double goertzel(const int32_t *x, uint32_t n, double f)
{
    double w = 2.0 * M_PI * f / FS, c = 2.0 * cos(w), s0 = 0, s1 = 0, s2 = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        double h = 0.5 - 0.5 * cos(2.0 * M_PI * i / (n - 1));   /* Hann */
        s0 = x[i] * h + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2);
}

/* the note's tail at master (Q12) and SPEAKER EQ eq (0 FLAT, 1 LOWCUT, 2 BASS+) into t_dac[] and t_usb[]; returns the
 * samples written */
static uint32_t render(int32_t master, uint32_t eq)
{
    int32_t out[2 * CTL];
    uint32_t f, i, n = 0;
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    host_preset(&trk[0], 0, 0);
    for (i = 0; i < NELEM(ANALOG_PRESETS); i++)
        if (str_eq(ANALOG_PRESETS[i].name, "SUB BASS"))
            host_preset(&trk[0], 0, i);
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
    trk[0].p[P_REL] = 120;                             /* a long release: the tail to measure */
    song.master_q12 = (uint32_t)master;
    fx_usb_fixed = 0;
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    fx_lowcut = (uint8_t)eq;
    lc_l1 = lc_l2 = lc_r1 = lc_r2 = 0;
    memset(lce, 0, sizeof lce);
    sb_lp1 = sb_lp2 = sb_lp3 = sb_lp4 = sb_env = sb_h1 = sb_h2 = sb_hl = 0;
    trk_note_on(&trk[0], 60, 100);
    for (f = 0; f < SECS * 44100u; f += CTL) {
        if (f == 22048u)
            trk_note_off(&trk[0], 60);                 /* held half a second */
        audio_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2u * i];
            t_dac[n] = x;
#if OLD_TREE
            t_usb[n] = x >> 7;                            /* (2.2: USB took the plain Q15) */
#else
            t_usb[n] = uac_s16(uac_dith(x >> 7, (uint32_t)(x & 127)));
#endif
            n++;
        }
    }
    return n;
}

/* Hk / H1 (dB) at the windows where H1 is first `down` dB under its level at the release's start (*h1 that level, dB
 * re full scale of the signal's words) */
static double ratio_at(const int32_t *x, uint32_t n, double fs_word, double down, double k, double *h1db)
{
    uint32_t w0 = 22048u / WIN + 1u, w;
    double ref;
    *h1db = NAN;
    if ((w0 + 1u) * WIN > n)
        return NAN;
    ref = goertzel(x + w0 * WIN, WIN, 261.6256);
    if (!isfinite(ref) || ref <= 0.0)
        return NAN;
    for (w = w0; (w + 1u) * WIN <= n; w++) {
        double a = goertzel(x + w * WIN, WIN, 261.6256), b = goertzel(x + w * WIN, WIN, k * 261.6256);
        if (!isfinite(a) || !isfinite(b) || a <= 0.0)
            return NAN;
        if (20.0 * log10(a / ref) <= -down) {
            *h1db = 20.0 * log10(a / (fs_word * WIN / 4.0));
            return 20.0 * log10((b + 1e-9) / a);
        }
    }
    *h1db = NAN;
    return NAN;                                        /* never fell that far */
}

static int check(const char *what, int ok)
{
    printf("tail: %-100s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* Quantize the same real DAC waveform with distinct deterministic seeds. Signed
 * averaging suppresses uncorrelated dither, but preserves note-correlated error.
 * A single 4096-sample window cannot resolve H3 below its dither noise floor. */
#if !OLD_TREE
static int average_usb(uint32_t n)
{
    uint32_t pass, i;
    int bounded = 1;
    memset(t_usb, 0, n * sizeof *t_usb);
    for (pass = 0; pass < USB_PASSES; pass++) {
        uac_rng = 0x2545F491u + pass * 0x9e3779b9u;
        for (i = 0; i < n; i++) {
            int32_t x = t_dac[i], y;
#if TAIL_USB_MUTATION == 1
            y = uac_s16(x >> 7);                       /* lost dither */
#elif TAIL_USB_MUTATION == 2
            y = uac_s16((x + 64) >> 7);                /* rounding without dither */
#elif TAIL_USB_MUTATION == 3
            int32_t coarse = (x >> 7) * 128;           /* lost low bits upstream */
            y = uac_s16(uac_dith(coarse >> 7, (uint32_t)(coarse & 127)));
#else
            y = uac_s16(uac_dith(x >> 7, (uint32_t)(x & 127)));
#endif
            /* For this unclipped waveform TPDF error is strictly below 1.5 LSB.
             * Keep this per-sample assertion: averaging must not hide excess noise. */
            int64_t error = (int64_t)y * 128 - x;
            if (error < -191 || error > 191)
                bounded = 0;
            t_usb[i] += y;                             /* 256 signed 16-bit words fit int32_t */
        }
    }
    return bounded;
}

#endif

int main(void)
{
    static const int32_t M[2] = {4088, 1024};
    static const double DOWN[4] = {10, 30, 50, 60};
    static const char *const EQ[3] = {"FLAT", "LOWCUT", "BASS+"};
    uint32_t mi, d, eq, fails = 0;
    for (eq = 0; eq < 3u; eq++)
        for (mi = 0; mi < 2u; mi++) {
            uint32_t n = render(M[mi], eq);
            double r10d = 0, r10u = 0, growd = -99, growu = -99;
            int valid_dac = 1, valid_usb = 1;
            for (d = 0; d < 4u; d++) {
                double hd, hu, rd = ratio_at(t_dac, n, 8388608.0, DOWN[d], 3.0, &hd),
                               ru = ratio_at(t_usb, n, 32768.0, DOWN[d], 3.0, &hu),
                               r2d = ratio_at(t_dac, n, 8388608.0, DOWN[d], 2.0, &hd),
                               r2u = ratio_at(t_usb, n, 32768.0, DOWN[d], 2.0, &hu);
                printf("tail: %-6s MASTER %4d, the note %2.0f dB down: DAC H1 %6.1f dBFS, H3/H1 %6.1f, H2/H1 %6.1f dB; "
                       "USB H1 %6.1f dBFS, H3/H1 %6.1f, H2/H1 %6.1f dB\n", EQ[eq], (int)M[mi], DOWN[d], hd, rd, r2d, hu, ru,
                       r2u);
                valid_dac &= isfinite(hd) && isfinite(rd);
                if (!d) {
                    r10d = rd;
                    r10u = ru;
                } else {
                    growd = rd - r10d > growd ? rd - r10d : growd;
                    growu = ru - r10u > growu ? ru - r10u : growu;
                }
            }
#if !OLD_TREE
            {
                char what[180];
                int bounded = average_usb(n);
                growu = -99;
                for (d = 0; d < 4u; d++) {
                    double hu, ru = ratio_at(t_usb, n, 32768.0 * USB_PASSES, DOWN[d], 3.0, &hu);
                    valid_usb &= isfinite(hu) && isfinite(ru);
                    printf("tail: %-6s MASTER %4d, note %2.0f dB down: USB coherent %u seeds H1 %.1f dBFS, H3/H1 %.2f dB\n",
                           EQ[eq], (int)M[mi], DOWN[d], USB_PASSES, hu, ru);
                    if (!d)
                        r10u = ru;
                    else if (ru - r10u > growu)
                        growu = ru - r10u;
                }
                snprintf(what, sizeof what, "SPEAKER EQ %s, MASTER %d: H3/H1 grows 3 dB at most as the note fades 60 dB "
                         "(DAC %+.1f, coherent USB %+.1f)", EQ[eq], (int)M[mi], growd, growu);
                fails += check(what, valid_dac && valid_usb && growd <= 3.0 && growu <= 3.0);
                fails += check("USB: every quantization error is strictly below 1.5 LSB", bounded);
            }
#endif
        }
#if !OLD_TREE
    {   /* silence stays exact 0 on USB (the dither only under sound) */
        uint32_t i, nz = 0;
        for (i = 0; i < 64u; i++)
            nz += uac_dith(0, 0) != 0;
        fails += check("USB: silence (0 and nothing below) stays exact 0, no dither", !nz);
    }
    printf(fails ? "tail: %u FAILED\n" : "tail: all passed\n", (unsigned)fails);
#endif
    return fails ? 1 : 0;
}
