/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SAMPLE: IMA ADPCM sample sets. */
static const char *const N_ONOFF_E[] = {"OFF", "ON"};
/* IMA ADPCM sample sets (tools/gen_samples.py), native rates, zones across
 * the keyboard, loops with the ADPCM state stored at the loop start.
 * SET is P_E0 (EDIT 1, KNOB 1). SET 4 was PERC, the GM drum kit: retired, an alias of PIANO; its sounds
 * load as the DRUM engine (core.h drum_from_perc). */
typedef struct {
    uint32_t off, n, ls, le;     /* byte offset, samples, loop start / end (sample index) */
    uint32_t rate;               /* source rate / 44100, Q16 */
    int16_t root16;              /* root pitch in 1/16 semitone */
    int16_t pred;                /* ADPCM state at the loop start */
    uint8_t idx, lo, hi, looped;
} smp_zone_t;
typedef struct {
    const char *name;
    uint16_t z0, nz;
} smp_set_t;
#include "felucca_samples.h"
#if defined(SMP_PERC_SLOT) && SMP_PERC_SLOT != SMP_SET_PERC
#error "tools/gen_samples.py PERC_SLOT != core.h SMP_SET_PERC"
#endif

static const int16_t IMA_STEP[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
    107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
    4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
    22385, 24623, 27086, 29794, 32767};
static const int8_t IMA_IDX[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
/* 2^(i/192), Q16: pitch ratios in 1/16 semitones, d16 >= -3072 (16 octaves down) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;     /* no loop for negative d16 */
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);   /* 2^(d/192) via the pitch table */
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

/* ---- user sample slots (loaded from the web editor into flash, see web/EDITOR_PROTOCOL.md)
 * 3 slots of 80 KiB at flash 0xA0000.. (Felucca data region), read through the plain XIP
 * window. Slot = header (magic, count, name, data length, CRC32) + up to 16 zones in the
 * smp_zone_t layout (off relative to the slot's data at +512) + IMA ADPCM data. */
#include "../hal/fm1_xip.h"   /* relative: hostsim includes this file too */
#define SMP_USER_SLOTS 3
#define SMP_USER_SIZE 0x14000u
#if BOARD_SMK37
/* SMK-37 Pro: only USR1 has flash (FL_SLOT 0xC2000..0xD5FFF in USRFLASH, fm1_flash_map.h; the pattern pool has the
 * rest); USR2 and USR3 stay empty: never scanned, their erase / write refused (SMP_USER_HAS). Their offsets are the
 * pool's and the stock marker's sectors: never a slot */
#include "../hal/fm1_flash_map.h"   /* FL_SLOT (relative, as fm1_xip.h) */
#define SMP_USER_FLASH_SLOTS 1u
#define SMP_USER_BASE FL_SLOT_LO
#define SMP_USER_HAS(k) ((uint32_t)(k) < SMP_USER_FLASH_SLOTS)
#define SMP_SHOWN SMP_USER_FLASH_SLOTS               /* (SMK37_UX.md question 11: USR2, USR3 not offered) */
#define SMP_PICK_MAX (SMP_NSETS + SMP_USER_FLASH_SLOTS - 1)   /* SAMPLE's SET, GRAIN's SOURCE: up to USR1 */
_Static_assert(SMP_USER_BASE + SMP_USER_FLASH_SLOTS * SMP_USER_SIZE <= FL_SLOT_HI, "SMK: the user slot past FL_SLOT");
#else
#define SMP_USER_BASE 0xA0000u
#define SMP_USER_HAS(k) 1
#define SMP_SHOWN SMP_USER_SLOTS                     /* the slots offered (the editor's SMP_INFO) */
#define SMP_PICK_MAX SMP_NALL - 1                    /* SAMPLE's SET, GRAIN's SOURCE: the last value */
#endif
#define SMP_USER_DATA 512u
#define SMP_USER_MAGIC 0x504D5346u                  /* "FSMP" */
#define SMP_NALL (SMP_NSETS + SMP_USER_SLOTS)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t nz, rsv;
    char name[8];
    uint32_t data_len, crc, rsv2[2];
    smp_zone_t zone[16];
} smp_user_hdr_t;                                   /* 32 + 16 x 28 = 480 B, data at +512 */
static smp_zone_t usr_zone[SMP_USER_SLOTS][16];     /* RAM copy, off rebased onto SMP_DATA */
static uint8_t usr_nz[SMP_USER_SLOTS];
/* (Skiff, #21) each sound of a user slot (a zone) has its own LEVEL, and sends more to the REVERB and the DELAY than
 * its track does by its REV and DLY: usr_mix[slot][0 LEVEL, 1 REV, 2 DLY][zone], 0..127. LEVEL 127 plays the sound as
 * it was uploaded (square law, as DRUM's lanes: 64 a quarter), REV and DLY 0 leave the track's sends alone. Set on
 * the unit, by the editor (SMP_MIX) and by tools/fm1_sample_upload.py; kept with the slot (slice_store.c: a record
 * after SLICE's in the slot's last sector, for this header only). A slot of before, or a new upload: 127, 0, 0 */
enum { SMX_LVL, SMX_REV, SMX_DLY };
static int16_t usr_mix[SMP_USER_SLOTS][3][16];   /* (int16: the page's knobs turn them, params.c page_desc) */
static void (*smx_load)(uint32_t k) __attribute__((section(".pool")));   /* slice_store.c: the slot's stored ones (a
                                                                          * scan) */
static uint8_t smx_save __attribute__((section(".pool")));   /* bit k: slot k's changed, to flash (slice_store.c) */
static const char *const SMP_ALL_NAMES[SMP_NALL] = {SMP_SET_NAMES_INIT, "USR1", "USR2", "USR3"};

#ifndef SMP_USER_XIP                                /* host tests: a RAM image of the slots */
#define SMP_USER_XIP(k) fm1_xip_ptr(SMP_USER_BASE + (k) * SMP_USER_SIZE)
#endif
static const uint8_t *smp_user_xip(uint32_t k) { return SMP_USER_XIP(k); }
#if FELUCCA_SLICE
static void slc_user_scan(uint32_t k, int valid);   /* eng_slice.c: SLICE's slice table of the slot */
#else
#define slc_user_scan(k, valid) ((void)0)
#endif
static uint32_t smp_user_gen;                       /* + 1 per slot scan (GRAIN: its seek index follows uploads) */

/* (re)read slot k from flash: valid header -> zones usable; call after boot and after an upload
 * (main loop: SLICE scans the slot's audio here). The zones are checked in a copy: an unusable slot leaves
 * every zone of it empty (n 0: a voice still on it ends), never a half-checked one */
static void smp_user_scan(uint32_t k)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    smp_zone_t zt[16];
    uint32_t i, base, nz = 0;
    smp_user_gen++;
    usr_nz[k] = 0;
    slc_user_scan(k, 0);
    if (SMP_USER_HAS(k) && h->magic == SMP_USER_MAGIC && h->version == 1 && h->nz && h->nz <= 16u &&
        h->data_len <= SMP_USER_SIZE - SMP_USER_DATA) {
        base = (uint32_t)(uintptr_t)(smp_user_xip(k) + SMP_USER_DATA) - (uint32_t)(uintptr_t)SMP_DATA;
        for (nz = h->nz, i = 0; i < nz; i++) {
            smp_zone_t *z = &zt[i];
            *z = h->zone[i];
            if (z->off > h->data_len || z->n > 2u * SMP_USER_SIZE || z->off + (z->n + 1u) / 2u > h->data_len ||
                z->idx > 88u || !z->rate || z->rate > 4u << 16 || z->ls > z->le || z->le >= z->n || z->lo > z->hi) {
                nz = 0;                             /* zone outside the data or malformed: slot unusable */
                break;
            }
            z->off += base;
        }
    }
    for (i = 0; i < 16u; i++) {
        usr_zone[k][i] = i < nz ? zt[i] : (smp_zone_t){0};
        usr_mix[k][SMX_LVL][i] = 127;               /* ((#21) every sound as uploaded, then the stored: smx_load) */
        usr_mix[k][SMX_REV][i] = usr_mix[k][SMX_DLY][i] = 0;
    }
    if (!nz)
        return;
    usr_nz[k] = (uint8_t)nz;
    if (smx_load)
        smx_load(k);
    slc_user_scan(k, 1);
}

/* A missing sample (SAMPLE, GRAIN, SLICE: an empty or invalid user slot, a set this build has no data for) plays
 * this instead, like a hardware sampler with its sample gone: a plain sine at the note's pitch (+ d16, 1/16
 * semitones), at half the peak of a full sample, through the engine's one-pole low-pass (lp Q15, state *y) and the
 * voice's amp (the ADSR: no click at the start, phase 0, or the end). *ph its phase. The UI says so once
 * (engines.c snd_missing, ui_input.c sample_notice). */
static void smp_sine(int32_t *out, uint32_t n, const vmod_t *m, int32_t d16, int32_t lp, uint32_t *ph, int32_t *y)
{
    uint32_t i, p = *ph, inc = pitch_inc((uint32_t)clamp(m->pitch16 + d16, 0, 2047));
    int32_t s = *y;
    for (i = 0; i < n; i++, p += inc) {
        s += mulq15((sine_i(p) >> 1) - s, lp);
        out[i] += voice_amp(s, m, i) << 1;
    }
    *ph = p;
    *y = s;
}
/* user slot k's name, its printable ASCII (nm holds 9; "" for an empty slot): the editor's SMP_INFO, (2.0, #35) a user
 * kit on the footer */
static void smp_user_name(uint32_t k, char *nm)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(k);
    uint32_t j;
    for (j = 0; j < 9u; j++)
        nm[j] = j < 8u && usr_nz[k % SMP_USER_SLOTS] && h->name[j] >= 32 && h->name[j] < 127 ? h->name[j] : 0;
}
/* SET / SRC si (0..SMP_NALL - 1) has no sample data: an empty or invalid user slot, a built-in set without data */
static int smp_set_missing(uint32_t si)
{
    if (si < SMP_NSETS)
        return !SMP_ZONES[SMP_SETS[si].z0].n;
    return !usr_nz[(si - SMP_NSETS) % SMP_USER_SLOTS];
}

/* zone index in a voice: < 0x8000 built-in, else 0x8000 | slot << 5 | zone */
static inline const smp_zone_t *smp_zone(uint32_t zi)
{
    return zi < 0x8000u ? &SMP_ZONES[zi] : &usr_zone[(zi >> 5) & 3u][zi & 31u];
}

/* zone z's IMA ADPCM code at *pos decoded: ps[0] the predictor (the sample), ps[1] the step index; *pos on by one.
 * ((2.0, #35) SAMPLE's and DRUM's user kits') */
static inline void adpcm_next(const smp_zone_t *z, uint32_t *pos, int32_t *ps)
{
    uint32_t b = SMP_DATA[z->off + (*pos >> 1)];
    uint32_t code = (*pos & 1u) ? (b >> 4) : (b & 15u);
    int32_t step = IMA_STEP[(uint32_t)ps[1] <= 88u ? ps[1] : 88], vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    ps[0] = clamp(ps[0] + ((code & 8u) ? -vd : vd), -32768, 32767);
    ps[1] = clamp(ps[1] + IMA_IDX[code & 7u], 0, 88);
    (*pos)++;
}
/* voice: ph[0] position (samples), ph[1] fraction Q16, s[0] predictor, s[1] step
 * index, s[2] previous sample, s[3] current sample, s[4] zone, s[5] ((2.0.3) the declick: sample_note_on), s[6] its
 * state, s[7] the CUT low-pass. ((2.0) A call per sample decoded, as it always was: inlined, the render loop's cost
 * grows past its budget, tests/target_budget.txt) */
static __attribute__((noinline)) int32_t sample_next(const smp_zone_t *z, voice_t *v, int loop)
{
    uint32_t pos = v->ph[0];
    adpcm_next(z, &pos, v->s);
    if (pos > z->le && z->looped && loop) {
        pos = z->ls;
        v->s[0] = z->pred;
        v->s[1] = z->idx;
    }
    v->ph[0] = pos;
    return v->s[0];
}

static void sample_note_on(track_t *t, voice_t *v)
{
    uint32_t si = (uint32_t)t->p[P_E0] % SMP_NALL, i, zi = 0xFFFFu, width = 128u;
    if (si < SMP_NSETS) {                           /* a built-in set: its zones split the keyboard */
        const smp_set_t *set = &SMP_SETS[si];
        for (i = 0; i < set->nz; i++)
            if (v->note >= SMP_ZONES[set->z0 + i].lo && v->note <= SMP_ZONES[set->z0 + i].hi) {
                zi = set->z0 + i;
                break;
            }
        v->s[4] = (int32_t)(zi == 0xFFFFu ? set->z0 : zi);
    } else {                                        /* user slot: silent if empty; the narrowest zone plays */
        uint32_t k = si - SMP_NSETS;
        for (i = 0; i < usr_nz[k]; i++) {
            const smp_zone_t *z = &usr_zone[k][i];
            if (v->note >= z->lo && v->note <= z->hi && z->hi - z->lo <= width) {
                zi = 0x8000u | k << 5 | i;
                width = z->hi - z->lo;
            }
        }
        v->s[4] = (int32_t)(zi == 0xFFFFu ? 0 : zi);
    }
    v->s[5] = v->env ? clamp(v->s[7] + v->s[5], -32767, 32767) : 0;   /* ((2.0.3) struck while it still sounds (a ringing
                                                                        * note again; MONO: every note): from where it
                                                                        * was, falling away, not a click (sample_declick) */
    v->ph[0] = 0;
    v->ph[1] = 0;
    v->s[0] = 0;
    v->s[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[6] = zi == 0xFFFFu;     /* 1 = sample ended (one-shot); a kit key with no sound stays silent */
    v->s[7] = 0;                 /* lo-pass state */
    if (smp_set_missing(si))
        v->s[6] = 2;             /* 2 = no sample data: the sine (smp_sine; ph[0] its phase) */
}

/* ((2.0.3) found making the 2.0 video) a note struck while the voice still sounded starts from where it was (s[5],
 * sample_note_on): that level added to the block, falling away (a time constant of 32 samples, -40 dB in 3 ms; no divide,
 * and it reaches 0), not a click. Not inlined: sample_render's loop keeps its cost (tests/target_budget.txt) */
static __attribute__((noinline)) void sample_declick(voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    int32_t off = v->s[5];
    uint32_t i;
    for (i = 0; i < n && off; i++) {
        out[i] += voice_amp(off, m, i) << 1;
        off -= (off >> 5) + (off > 0);
    }
    v->s[5] = off;
}

static void sample_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    const smp_zone_t *z = smp_zone((uint32_t)v->s[4]);
    uint32_t i, frac = v->ph[1];
    int32_t d16 = clamp(m->pitch16 + p[P_E1] * 16 - z->root16, -1536, 576);   /* <= 3 octaves up: bounded decode load */
    uint32_t r = pow2_q16(d16), stepq = (r >> 8) * (z->rate >> 8);   /* Q16 samples per output */
    int32_t bits = p[P_E2], lp = 4000 + ((clamp((p[P_E4] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15);
    int32_t drv = p[P_E6], sh = bits / 10;
    if (v->s[6] == 2) {                               /* no sample data: the sine at the note's pitch + TUNE */
        smp_sine(out, n, m, p[P_E1] * 16, lp, &v->ph[0], &v->s[7]);
        return;
    }
    if (v->s[6] || !z->n) {
        v->active = 0;
        return;
    }
    if (v->ph[0] == 0 && v->ph[1] == 0 && v->s[3] == 0)
        v->s[3] = sample_next(z, v, p[P_E3]);         /* prime the interpolator */
    for (i = 0; i < n; i++) {
        int32_t s;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (v->ph[0] >= z->n) {                   /* one-shot reached its end */
                v->s[6] = 1;
                v->s[3] = 0;
                if (frac >= 65536u) {                /* past the last sample-to-zero interval */
                    v->s[2] = 0;
                    frac &= 65535u;
                }
                break;
            }
            v->s[3] = sample_next(z, v, p[P_E3]);
        }
        s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        if (sh)                                       /* BITS: 0 = clean, up to 12 bits removed */
            s = (s >> sh) << sh;
        if (drv)
            s = softclip(s + (((s >> 2) * (drv * 150)) >> 11));   /* = s * drv * 600 / 32768, no overflow */
        v->s[7] += mulq15(s - v->s[7], lp);           /* gentle tone control (CUT) */
        out[i] += voice_amp(v->s[7], m, i) << 1;
        if (v->s[6])
            break;
    }
    v->ph[1] = frac;
    if (v->s[5])
        sample_declick(v, out, n, m);
}

static const engine_t ENG_SAMPLE = {
    .name = "SAMPLE",
    .page_title = {"SET", "TONE"},
    .edit = {
        {"SET", SMP_ALL_NAMES, 0, SMP_PICK_MAX, 0, F_ENUM, 0},
        {"PITCH", 0, -24, 24, 0, F_SEMI, 0},
        {"BITS", 0, 0, 127, 0, F_INT, 0},
        {"LOOP", N_ONOFF_E, 0, 1, 1, F_ENUM, 0},
        {"CUTOFF", 0, 0, 127, 127, F_INT, 0},
        {"-", 0, 0, 0, 0, F_INT, 0},
        {"DRIVE", 0, 0, 127, 0, F_PCT, 0},
        {"-", 0, 0, 0, 0, F_INT, 0},
    },
    .presets = SMP_PRESET_TABLE,
    .npresets = SMP_NPRESETS,
    .note_on = sample_note_on,
    .render = sample_render,
    .knob = {P_E0, P_E4, P_ATK, P_REL},
    .sampled = 1,
};
