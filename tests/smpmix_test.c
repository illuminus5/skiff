/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff, #21) a user slot's sounds' LEVEL, REV and DLY (eng_sample.c usr_mix), through hostsim.c (run_tests.sh):
 *   build/host/smpmix_test
 * Slots are built here (two or three sounds of a crude ADPCM wave, one key each) in a RAM image of the flash slots
 * (SMP_USER_XIP), read by smp_user_scan as at boot.
 * 1. a slot of before (no record) plays every sound as uploaded: LEVEL 127, REV 0, DLY 0;
 * 2. the store (slice_store.c): written once stopped and off the SLICES page, only when it changed ("MIX SAVED"), read back
 *    at a scan; kept for its header only (another sample, or the same under other keys: as uploaded again; the same
 *    upload again: back); a save of SLICE's record keeps it; a torn write or junk ignored; a long sample: "TOO LONG TO
 *    SAVE", the values stay until the power goes;
 * 3. LEVEL: a sound at 64 plays at a quarter (square law, as DRUM's lanes), at 127 as before; a built-in set as before;
 *    values out of range (a crafted record) play at full level;
 * 4. REV / DLY: a sound's own sends reach the REVERB and DELAY sums as the track's would by as much (with the track's,
 *    127 at most), at the track's LEVEL; none from a sound without them, none from a built-in set's, none while the
 *    FX layer mutes the track; the dry mix as before;
 * 5. (2.0, #35) a slot as DRUM's KIT (USR1..3): each lane its sample to its end, a lane without one silent; TUNE, the
 *    lane's DECAY, the closed hat's choke, a lane's mute; an empty slot silent; STD after it; a lane struck again while
 *    it rings starts its sample again, without a click. */
#include <stdarg.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3, as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* the store's hooks over host_slots, the UI's stubs (as slice_test's) */
static uint8_t flash_ok = 1, host_busy, host_page, host_torn;
static char host_msg[32];
static int transport_busy(void) { return host_busy; }
static int slice_page_on(void) { return host_page; }
static int smx_page_on(void) { return 0; }               /* ((#21) the SAMPLES page: as SLICES', above) */
static void ui_message(const char *s) { snprintf(host_msg, sizeof host_msg, "%s", s); }
static uint32_t host_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off)
{
    if (off < SMP_USER_BASE || off + 0x1000u > SMP_USER_BASE + sizeof host_slots || (off & 0xFFFu))
        return -8;
    memset((uint8_t *)host_slots + (off - SMP_USER_BASE), 0xFF, 0x1000u);
    host_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off < SMP_USER_BASE || off + n > SMP_USER_BASE + sizeof host_slots)
        return -8;
    if (host_torn)
        return -1;                                       /* power gone after the erase */
    for (i = 0; i < n; i++)                              /* NOR: bits only go 1 -> 0 */
        ((uint8_t *)host_slots)[off - SMP_USER_BASE + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/slice_store.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("smpmix: %-62s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

/* slot k: nz sounds of zb ADPCM bytes each (a wave going 4 samples up, 4 down), sound i on key 60 + i (lo = hi), its
 * name, salt changing the first data byte (another sample); an erased slot under it */
static void slot_put(uint32_t k, uint32_t nz, uint32_t zb, uint8_t salt)
{
    uint8_t *b = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    smp_user_hdr_t h;
    uint32_t i;
    memset(b, 0xFF, SMP_USER_SIZE);
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = (uint8_t)nz;
    memcpy(h.name, "MIX", 3);
    h.data_len = nz * zb;
    for (i = 0; i < h.data_len; i++)
        b[SMP_USER_DATA + i] = (i & 2u) ? 0xFFu : 0x77u;
    b[SMP_USER_DATA] ^= salt;
    for (i = 0; i < nz; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = i * zb;
        z->n = 2u * zb;
        z->le = z->n - 1u;
        z->rate = 65536u;
        z->root16 = (int16_t)((60 + (int32_t)i) * 16);
        z->lo = z->hi = (uint8_t)(60u + i);
    }
    h.crc = st_crc32(b + SMP_USER_DATA, h.data_len);
    memcpy(b, &h, sizeof h);
}
static void mix_set(uint32_t k, uint32_t z, int lv, int rv, int dl)
{
    usr_mix[k][SMX_LVL][z] = (int16_t)lv;
    usr_mix[k][SMX_REV][z] = (int16_t)rv;
    usr_mix[k][SMX_DLY][z] = (int16_t)dl;
}
static int mix_is(uint32_t k, uint32_t z, int lv, int rv, int dl)
{
    return usr_mix[k][SMX_LVL][z] == lv && usr_mix[k][SMX_REV][z] == rv && usr_mix[k][SMX_DLY][z] == dl;
}
static int all_neutral(uint32_t k)
{
    uint32_t z;
    int ok = 1;
    for (z = 0; z < 16u; z++)
        ok &= mix_is(k, z, 127, 0, 0);
    return ok;
}
static int str_is(const char *a, const char *b) { return !strcmp(a, b); }
/* the poll as the main loop runs it, stopped and off the page */
static void store(void)
{
    host_busy = host_page = 0;
    host_msg[0] = 0;
    slc_store_poll();
}

/* 3, 4: track 1 on SAMPLE, SET set (USR1 = SMP_NSETS), no DRIVE / INSERT, its sends rev and dly, LEVEL lvl, ADSR 0 / 127
 * / 127 / 10, plays note at velocity 100 for nb blocks: the dry mix's peak (left, before the buses' return), the REVERB
 * and DELAY sums' energy */
typedef struct {
    uint32_t peak;
    double er, ed;
} run_t;
static run_t run(int16_t set, uint32_t note, int16_t rev, int16_t dly, int16_t lvl, uint32_t nb)
{
    track_t *t = &trk[0];
    run_t r = {0, 0, 0};
    uint32_t i, k, j;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < NVOICE; j++)
            trk[k].v[j].active = trk[k].v[j].gate = 0;
    host_tracks_init();
    song.g[G_RTYPE] = 0;                                 /* (ROOM: no stereo side in the dry mix) */
    host_preset(t, ENGI_SAMPLE, 0);
    t->p[P_E0] = set;
    t->p[P_E2] = 0;                                      /* (BITS, CUTOFF, DRIVE: clean) */
    t->p[P_E4] = 127;
    t->p[P_E6] = 0;
    t->p[P_DIST] = t->p[P_CHOR] = 0;
    t->p[P_REV] = rev;
    t->p[P_DLY] = dly;
    t->p[P_LEVEL] = lvl;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = 127;
    t->p[P_SUS] = 127;
    t->p[P_REL] = 10;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);                               /* (the engine in) */
    }
    trk_note_on(t, note, 100);
    for (k = 0; k < nb; k++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)abs(mix_l[i]);
            r.peak = a > r.peak ? a : r.peak;
            r.er += (double)send_r[i] * send_r[i];
            r.ed += (double)send_d[i] * send_d[i];
        }
    }
    trk_note_off(t, note);
    return r;
}
static int close_to(double a, double b, double tol) { return b > 0 ? fabs(a / b - 1.0) <= tol : a == 0; }

/* ---- (2.0, #35, the user's pick A of design/mockups/v2_kits.png) a user slot as DRUM's KIT (USR1..3): each lane plays
 * the slot's sample on its note, a lane without one is silent; PITCH and TUNE its speed, DECAY and the lane's its fall,
 * the closed hat chokes the open one, a muted lane rings on unheard; an empty slot: every lane silent ---- */
static const uint8_t KIT_NOTE[7] = {36, 38, 39, 42, 46, 45, 37};   /* KICK SNARE CLAP CL HAT OP HAT TOM RIM: no BELL */
static const uint32_t KIT_ZB[7] = {2000, 200, 400, 300, 3000, 800, 150};   /* (their ADPCM bytes: 2 samples each) */
static void kit_put(uint32_t k)
{
    uint8_t *b = (uint8_t *)host_slots + k * SMP_USER_SIZE;
    smp_user_hdr_t h;
    uint32_t i, off = 0;
    memset(b, 0xFF, SMP_USER_SIZE);
    memset(&h, 0, sizeof h);
    h.magic = SMP_USER_MAGIC;
    h.version = 1;
    h.nz = 7;
    memcpy(h.name, "MY KIT", 6);
    for (i = 0; i < 7u; i++) {
        smp_zone_t *z = &h.zone[i];
        z->off = off;
        z->n = 2u * KIT_ZB[i];
        z->le = z->n - 1u;
        z->rate = 65536u;
        z->root16 = (int16_t)(KIT_NOTE[i] * 16);
        z->lo = z->hi = KIT_NOTE[i];
        off += KIT_ZB[i];
    }
    h.data_len = off;
    for (i = 0; i < off; i++)
        b[SMP_USER_DATA + i] = (i & 2u) ? 0xFFu : 0x77u;
    h.crc = st_crc32(b + SMP_USER_DATA, h.data_len);
    memcpy(b, &h, sizeof h);
}
/* track 1 on DRUM (its first preset, no sends), KIT kit */
static track_t *kit_track(int16_t kit)
{
    track_t *t = &trk[0];
    uint32_t k, j;
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < NVOICE; j++)
            trk[k].v[j].active = trk[k].v[j].gate = 0;
    host_tracks_init();
    song.g[G_RTYPE] = 0;
    host_preset(t, ENGI_DRUM, 0);
    t->p[P_E0] = kit;
    t->p[P_REV] = t->p[P_DLY] = t->p[P_CHOR] = t->p[P_DIST] = 0;
    memset(drum_mute, 0, sizeof drum_mute);
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);                               /* (the engine in) */
    }
    return t;
}
/* note struck on t (then a second note at block at2, 0 none): of nb blocks, how many have the dry mix above 64 */
static uint32_t kit_blocks(track_t *t, uint32_t note, uint32_t nb, uint32_t note2, uint32_t at2)
{
    uint32_t k, i, n = 0;
    trk_note_on(t, note, 100);
    for (k = 0; k < nb; k++) {
        int32_t o[2 * CTL], pk = 0;
        if (note2 && k == at2)
            trk_note_on(t, note2, 100);
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            pk = abs(mix_l[i]) > pk ? abs(mix_l[i]) : pk;
        n += pk > 64;
    }
    trk_note_off(t, note);
    if (note2)
        trk_note_off(t, note2);
    return n;
}
static int drumkit_test(void)
{
    track_t *t;
    uint32_t kick, kick_up, kick_short, snare, bell, oh, oh_cut, muted, empty, std, i;
    int has = 1;
    voice_t *v;
    kit_put(2);
    memset((uint8_t *)host_slots + 1u * SMP_USER_SIZE, 0xFF, SMP_USER_SIZE);   /* (USR2 empty) */
    smp_user_scan(1);
    smp_user_scan(2);
    t = kit_track(DK_USR3);
    kick = kit_blocks(t, 36, 400, 0, 0);
    snare = kit_blocks(kit_track(DK_USR3), 38, 400, 0, 0);
    bell = kit_blocks(kit_track(DK_USR3), 56, 400, 0, 0);
    check("(#35) KIT USR3: KICK and SNARE their samples to their ends, BELL (none) silent",
          kick + 2u >= 4000u / CTL && kick <= 4000u / CTL + 2u && snare + 2u >= 400u / CTL && snare <= 400u / CTL + 2u && !bell,
          "%u %u %u blocks", kick, snare, bell);
    t = kit_track(DK_USR3);
    t->p[P_LTUNE0] = 127;                                /* (KICK's TUNE: +11.8 semitones, about twice as fast) */
    kick_up = kit_blocks(t, 36, 400, 0, 0);
    t = kit_track(DK_USR3);
    t->p[P_LDECAY0] = -40;                               /* (KICK's DECAY below its middle: a fall) */
    kick_short = kit_blocks(t, 36, 400, 0, 0);
    check("  TUNE up: faster (half as long); the lane's DECAY down: shorter", kick_up * 2u <= kick + 4u &&
          kick_up * 2u + 4u >= kick && kick_short < kick / 2u, "%u %u of %u", kick_up, kick_short, kick);
    t = kit_track(DK_USR3);
    oh = kit_blocks(t, 46, 400, 0, 0);
    t = kit_track(DK_USR3);
    oh_cut = kit_blocks(t, 46, 400, 42, 10);
    check("  the closed hat chokes the open one", oh >= 6000u / CTL - 2u && oh_cut < 10u + 600u / CTL + 4u,
          "%u, %u with CL HAT at 10", oh, oh_cut);
    t = kit_track(DK_USR3);
    drum_mute[0] = 1u << 0;                              /* (KICK muted) */
    trk_note_on(t, 36, 100);
    {
        int32_t o[2 * CTL];
        for (i = 0; i < 20u; i++)
            mix_block(o, CTL);
    }
    v = drum_reuse(t, 36);
    muted = kit_blocks(t, 36, 20, 0, 0);
    check("  a muted lane rings on unheard (its voice playing)", !muted && v && v->active && !v->s[6], "%u", muted);
    empty = kit_blocks(kit_track(DK_USR2), 36, 100, 0, 0);
    std = kit_blocks(kit_track(DK_STD), 36, 100, 0, 0);
    t = kit_track(DK_USR3);
    for (i = 0; i < 7u; i++)
        has &= drum_lane_has(t, i);
    has &= !drum_lane_has(t, 7);
    t->p[P_E0] = DK_STD;
    has &= drum_lane_has(t, 7);
    check("  an empty slot (USR2): silent; STD after: its own KICK; LANES: BELL without a sample", !empty && std > 0 && has,
          "%u %u", empty, std);
    memset(drum_mute, 0, sizeof drum_mute);
    return 0;
}

/* IMA ADPCM of x as sampleio.ima_encode (from predictor 0 / index 0, low nibble first) */
static void ima_enc(const int16_t *x, uint32_t n, uint8_t *out)
{
    int32_t pred = 0, idx = 0;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t step = IMA_STEP[idx], diff = x[i] - pred, code = 0, vd = step >> 3;
        if (diff < 0) {
            code = 8;
            diff = -diff;
        }
        if (diff >= step) {
            code |= 4;
            diff -= step;
            vd += step;
        }
        if (diff >= step >> 1) {
            code |= 2;
            diff -= step >> 1;
            vd += step >> 1;
        }
        if (diff >= step >> 2) {
            code |= 1;
            vd += step >> 2;
        }
        pred = clamp(pred + ((code & 8) ? -vd : vd), -32768, 32767);
        idx = clamp(idx + IMA_IDX[code & 7], 0, 88);
        out[i >> 1] = (i & 1u) ? (uint8_t)(out[i >> 1] | code << 4) : (uint8_t)code;
    }
}
/* (#35, the unit 2026-10-24: "a subtle click on all the sounds", "the bass drum has to ring out before it can retrigger")
 * KICK a 110 Hz sine falling over 4000 samples, struck, then struck again at block 30 while it rings: the second hit
 * starts the sample again (its position back at its start, as long again), and its steepest step there is no larger
 * than the first hit's from silence (its ADPCM catching up with the sine) plus the sine's own steepest after it: not
 * the jump from where the first hit was to the second's start */
static int drumkit_retrig_test(void)
{
    static int16_t x[4000];
    uint8_t *b = (uint8_t *)host_slots + 2u * SMP_USER_SIZE;
    smp_user_hdr_t h;
    track_t *t;
    voice_t *v = 0;
    int32_t prev = 0, step0 = 0, step1 = 0, step2 = 0;
    uint32_t i, k, n2 = 0, pos = 0;
    kit_put(2);
    for (i = 0; i < 4000u; i++)
        x[i] = (int16_t)(20000.0 * sin(i * 2.0 * 3.14159265 * 110.0 / 22050.0) * exp(-(double)i / 2500.0));
    memcpy(&h, b, sizeof h);
    ima_enc(x, 4000u, b + SMP_USER_DATA + h.zone[0].off);
    h.crc = st_crc32(b + SMP_USER_DATA, h.data_len);
    memcpy(b, &h, sizeof h);
    smp_user_scan(2);
    t = kit_track(DK_USR3);
    trk_note_on(t, 36, 100);
    for (k = 0; k < 400u; k++) {
        int32_t o[2 * CTL];
        if (k == 30u) {
            trk_note_on(t, 36, 100);
            v = drum_reuse(t, 36);
        }
        mix_block(o, CTL);
        if (k == 30u)
            pos = v ? v->ph[0] : 99999u;
        for (i = 0; i < CTL; i++) {
            int32_t d = abs(mix_l[i] - prev);
            prev = mix_l[i];
            if (k < 3u)
                step0 = d > step0 ? d : step0;
            else if (k < 30u)
                step1 = d > step1 ? d : step1;
            else if (k < 33u)
                step2 = d > step2 ? d : step2;
        }
        {
            int32_t pk = 0;
            for (i = 0; i < CTL; i++)
                pk = abs(mix_l[i]) > pk ? abs(mix_l[i]) : pk;
            n2 += k >= 30u && pk > 64;
        }
    }
    trk_note_off(t, 36);
    check("(#35) a lane struck again while it rings: its sample from the start, as long again, no click",
          pos <= 2u * CTL && n2 + 2u >= 4000u / CTL && step2 <= step0 + step1,
          "position %u after the second hit's block, %u blocks after it, step %d at it vs %d + %d", pos, n2, step2, step0,
          step1);
    return 0;
}
/* (2.0.3, found making the 2.0 video) SAMPLE struck again while it still sounds (MONO: the next note on its one voice):
 * a steady sine in USR2 under every key, struck, then another key at block 30, the sine at its peak there: the second
 * note starts the sample again (its position back at its start) and the steepest step at it is a small part of the
 * level just before it, not the jump from that level to the second note's start */
static int sample_retrig_test(void)
{
    static int16_t x[16000];
    uint8_t *b = (uint8_t *)host_slots + 1u * SMP_USER_SIZE;
    smp_user_hdr_t h;
    track_t *t = &trk[0];
    int32_t prev = 0, lvl = 0, step2 = 0;
    uint32_t i, k, j, pos = 99999u;
    slot_put(1, 1, 8000, 0);                             /* (one sound, 16000 samples) */
    for (i = 0; i < 16000u; i++)                         /* (its peak at sample 959: the last before block 30) */
        x[i] = (int16_t)(20000.0 * sin(i * 2.0 * 3.14159265 * 2.25 / 959.0));
    memcpy(&h, b, sizeof h);
    h.zone[0].lo = 0;
    h.zone[0].hi = 127;
    ima_enc(x, 16000u, b + SMP_USER_DATA + h.zone[0].off);
    h.crc = st_crc32(b + SMP_USER_DATA, h.data_len);
    memcpy(b, &h, sizeof h);
    smp_user_scan(1);
    for (k = 0; k < NTRK; k++)
        for (j = 0; j < NVOICE; j++)
            trk[k].v[j].active = trk[k].v[j].gate = 0;
    host_tracks_init();
    song.g[G_RTYPE] = 0;
    host_preset(t, ENGI_SAMPLE, 0);
    t->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    t->p[P_E2] = t->p[P_E6] = 0;                         /* (BITS, DRIVE off, CUTOFF open, no sends) */
    t->p[P_E4] = 127;
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_REV] = t->p[P_DLY] = 0;
    t->p[P_ATK] = 0;
    t->p[P_DEC] = t->p[P_SUS] = 127;
    t->p[P_VOICE] = V_MONO;
    {
        int32_t o[2 * CTL];
        mix_block(o, CTL);                               /* (the engine in) */
    }
    trk_note_on(t, 60, 100);
    for (k = 0; k < 40u; k++) {
        int32_t o[2 * CTL];
        if (k == 30u) {
            lvl = abs(prev);
            trk_note_on(t, 62, 100);                     /* (MONO: the same voice, struck again) */
        }
        mix_block(o, CTL);
        if (k == 30u)
            for (j = 0; j < NVOICE; j++)
                if (t->v[j].active && t->v[j].note == 62)
                    pos = t->v[j].ph[0];
        for (i = 0; i < CTL; i++) {
            int32_t d = abs(mix_l[i] - prev);
            prev = mix_l[i];
            if (k >= 30u && k < 33u)
                step2 = d > step2 ? d : step2;
        }
    }
    trk_note_off(t, 60);
    trk_note_off(t, 62);
    t->p[P_VOICE] = V_POLY;
    check("(2.0.3) SAMPLE struck again while it sounds (MONO): from its start, no click",
          pos <= 2u * CTL && lvl > 2000 && step2 * 6 < lvl,
          "position %u after the second note's block, step %d at it vs the level %d before", pos, step2, lvl);
    return 0;
}

int main(void)
{
    const uint8_t *tail = (const uint8_t *)host_slots + SLC_REC_OFF;
    int ok;
    uint32_t e0;
    slc_store_boot();                                    /* (persist_boot: the scans read the records) */

    /* 1: a slot of before */
    slot_put(0, 3, 512, 0);
    mix_set(0, 1, 9, 9, 9);
    smp_user_scan(0);
    check("a slot of before plays every sound as uploaded (127, 0, 0)", usr_nz[0] == 3 && all_neutral(0), 0);

    /* 2: the store */
    mix_set(0, 0, 64, 40, 0);
    mix_set(0, 2, 100, 0, 90);
    smx_save |= 1u;
    host_busy = 1;
    e0 = host_erases;
    slc_store_poll();
    ok = host_erases == e0 && smx_save == 1u;            /* playing: waits */
    host_busy = 0;
    host_page = 1;
    slc_store_poll();
    ok &= host_erases == e0 && smx_save == 1u;           /* the SLICES page open: waits */
    store();
    ok &= host_erases == e0 + 1u && !smx_save && str_is(host_msg, "MIX SAVED");
    smx_save |= 1u;
    store();
    ok &= host_erases == e0 + 1u && !host_msg[0];        /* the same again: no erase, nothing said */
    check("store: written once stopped and off the page, only when it changed", ok, 0);

    smp_user_scan(0);
    check("store: a scan (boot) puts the values back",
          mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 127, 0, 0) && mix_is(0, 2, 100, 0, 90), 0);

    slc_man_save |= 1u;                                  /* SLICE's record saved again (MAN slices: none) */
    mix_set(0, 1, 30, 0, 0);
    store();
    smp_user_scan(0);
    ok = str_is(host_msg, "SLICES SAVED") && mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 30, 0, 0);
    check("store: a save of SLICE's record keeps (and writes) the mix", ok, 0);

    {
        uint8_t keep[0x1000];
        memcpy(keep, tail, sizeof keep);                 /* the last sector as the store left it (an upload leaves it) */
        slot_put(0, 3, 512, 0x10);                       /* another sample */
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok = all_neutral(0);
        slot_put(0, 3, 512, 0);                          /* the first sample again, the same keys */
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok &= mix_is(0, 0, 64, 40, 0) && mix_is(0, 1, 30, 0, 0);
        slot_put(0, 3, 512, 0);                          /* .. under other keys: as uploaded */
        ((smp_user_hdr_t *)host_slots)->zone[0].hi = 59;
        ((smp_user_hdr_t *)host_slots)->zone[0].lo = 0;
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        smp_user_scan(0);
        ok &= usr_nz[0] == 3 && all_neutral(0);
        check("store: kept for its header only (another sample, other keys: as uploaded; the same again: back)", ok, 0);

        slot_put(0, 3, 512, 0);
        memcpy((uint8_t *)host_slots + SLC_REC_OFF, keep, sizeof keep);
        ((uint8_t *)host_slots)[SMX_REC_OFF + 5] ^= 1u;   /* a bit gone */
        smp_user_scan(0);
        ok = all_neutral(0);
        memset((uint8_t *)host_slots + SMX_REC_OFF, 0, sizeof(smx_rec_t));   /* zeros */
        smp_user_scan(0);
        ok &= all_neutral(0);
        check("store: a damaged or zeroed record is ignored", ok, 0);
    }

    slot_put(0, 3, 512, 0);
    smp_user_scan(0);
    mix_set(0, 0, 50, 0, 0);
    smx_save |= 1u;
    host_torn = 1;
    store();
    host_torn = 0;
    ok = str_is(host_msg, "SAVE ERROR");
    smp_user_scan(0);
    ok &= all_neutral(0) && usr_nz[0] == 3;
    check("store: a write cut short loses the values, never the sample (SAVE ERROR)", ok, 0);

    slot_put(0, 2, (SMP_USER_SIZE - SMP_USER_DATA) / 2u, 0);   /* the sample fills the slot */
    smp_user_scan(0);
    mix_set(0, 1, 40, 0, 0);
    smx_save |= 1u;
    e0 = host_erases;
    store();
    ok = str_is(host_msg, "TOO LONG TO SAVE") && host_erases == e0 && mix_is(0, 1, 40, 0, 0) && !smx_save;
    smp_user_scan(0);
    ok &= all_neutral(0);
    check("store: a sample into the last sector: TOO LONG TO SAVE, kept in RAM only", ok, 0);

    /* 3: LEVEL */
    slot_put(0, 3, 4096, 0);
    smp_user_scan(0);
    {
        run_t a, b, c, d, x;
        a = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        mix_set(0, 1, 64, 0, 0);
        b = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        c = run((int16_t)SMP_NSETS, 60, 0, 0, 112, 40);   /* another sound: its own (127) */
        mix_set(0, 1, -5, 0, 0);                         /* crafted: out of range */
        d = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 40);
        mix_set(0, 1, 127, 0, 0);
        x = run(0, 60, 0, 0, 112, 40);                   /* a built-in set */
        ok = a.peak > 4000u && close_to(b.peak, a.peak * 8256.0 / 32508.0, 0.03) && c.peak > 4000u && d.peak == a.peak;
        check("LEVEL: 64 a quarter (square law), 127 as before, out of range full", ok,
              "127: %u, 64: %u (%.3f), out of range %u", a.peak, b.peak, (double)b.peak / a.peak, d.peak);
        mix_set(0, 1, 64, 0, 0);
        {
            run_t y = run(0, 60, 0, 0, 112, 40);
            check("LEVEL: a built-in set's sounds are as before", y.peak == x.peak && x.peak > 1000u, "%u %u", x.peak, y.peak);
        }
        mix_set(0, 1, 127, 0, 0);
    }

    /* 4: REV / DLY */
    {
        run_t t0, t64, z64, zd, both, cap, lvl, lv0, none, muted;
        mix_set(0, 1, 127, 0, 0);
        none = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        t64 = run((int16_t)SMP_NSETS, 61, 64, 0, 112, 60);   /* the track's REVERB 64 */
        mix_set(0, 1, 127, 64, 0);
        z64 = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);    /* the sound's REV 64 */
        ok = none.er == 0 && none.ed == 0 && t64.er > 0 && close_to(z64.er, t64.er, 0.02) && z64.ed == 0 &&
             z64.peak == none.peak;
        check("REV: the sound's 64 sends as the track's 64 would, the dry as before", ok, "track %.4g, sound %.4g",
              t64.er, z64.er);
        mix_set(0, 1, 127, 0, 50);
        zd = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        mix_set(0, 1, 127, 0, 0);                             /* (the references: the track's sends alone) */
        t0 = run((int16_t)SMP_NSETS, 61, 0, 50, 112, 60);
        check("DLY: the sound's 50 sends as the track's 50 would", close_to(zd.ed, t0.ed, 0.02) && zd.er == 0 && t0.ed > 0,
              "track %.4g, sound %.4g", t0.ed, zd.ed);
        mix_set(0, 1, 127, 64, 0);
        both = run((int16_t)SMP_NSETS, 61, 30, 0, 112, 60);   /* 30 + 64 */
        mix_set(0, 1, 127, 120, 0);
        cap = run((int16_t)SMP_NSETS, 61, 100, 0, 112, 60);   /* 100 + 120: 127 at most */
        mix_set(0, 1, 127, 0, 0);
        t0 = run((int16_t)SMP_NSETS, 61, 94, 0, 112, 60);
        t64 = run((int16_t)SMP_NSETS, 61, 127, 0, 112, 60);
        check("REV: added to the track's (30 + 64 as 94), 127 at most in all", close_to(both.er, t0.er, 0.02) &&
              close_to(cap.er, t64.er, 0.02), "%.4g %.4g, %.4g %.4g", both.er, t0.er, cap.er, t64.er);
        mix_set(0, 1, 127, 64, 0);
        lvl = run((int16_t)SMP_NSETS, 61, 0, 0, 100, 60);
        lv0 = run((int16_t)SMP_NSETS, 61, 0, 0, 0, 60);
        mix_set(0, 1, 127, 0, 0);
        t0 = run((int16_t)SMP_NSETS, 61, 64, 0, 100, 60);
        check("REV: at the track's LEVEL (LEVEL 0: nothing)", close_to(lvl.er, t0.er, 0.02) && lv0.er == 0, 0);
        mix_set(0, 1, 127, 64, 0);
        none = run((int16_t)SMP_NSETS, 60, 0, 0, 112, 60);    /* another sound of the slot: none of its own */
        t0 = run(0, 60, 0, 0, 112, 60);                       /* a built-in set */
        check("REV: none from a sound without it, none from a built-in set", none.er == 0 && t0.er == 0 && none.peak > 0, 0);
        perf_held = 1u << PF_M1;                              /* the FX layer mutes track 1 */
        muted = run((int16_t)SMP_NSETS, 61, 0, 0, 112, 60);
        perf_held = 0;
        {
            int32_t o[2 * CTL];
            uint32_t k;
            for (k = 0; k < 20u; k++)
                mix_block(o, CTL);                            /* (its ramp back) */
        }
        check("REV: none while the FX layer mutes the track (after its ramp)", muted.er < z64.er * 0.05, "%.4g of %.4g",
              muted.er, z64.er);
        mix_set(0, 1, 127, 0, 0);
    }

    drumkit_test();
    drumkit_retrig_test();
    sample_retrig_test();
    printf("%s\n", fails ? "SMPMIX TEST FAILED" : "smpmix test passed");
    return fails != 0;
}
