/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: ducking (DESIGN.md, FX > DUCK). A track ducks under another track's hits: each note its source (BY,
 * P_DKSRC: T1..T6, OFF; itself counts as OFF) starts pulls the track's level down by DEPTH (P_DKAMT, 100 %: silent)
 * within DUCK_ATK, and it comes back in a straight line over RELEASE (P_DKREL, as the envelopes' times). A DRUM
 * track's KICK lane is its only hit (its hats never duck anything). A muted source strikes nothing (voice.c).
 * The gain is on the track's own signal, after its DIST and SLICER and before its LEVEL, PAN and sends (fx.c
 * mix_part): its sends dip with it, the delay and reverb tails already there ring on. Ramped through each block:
 * no click. Included by fx.c; voice.c trk_note_on marks the hits (duck_note), mix_block runs duck_block once a
 * block after the events, mix_part duck_apply for a track whose gain is not 1. Nothing ducked: the mix bit for
 * bit as without it. */
#define DUCK_ATK 132u                                   /* samples (3 ms) to its depth */
static volatile uint8_t duck_hits;                       /* bit k: track k struck since the last block */
static struct { int32_t env, r0, r1; uint8_t atk; } duck[NPART];   /* env Q15 (1: down); what the gain is below 1,
                                                                      * Q15 (0: not ducked) */
static uint8_t duck_on;                                  /* bit k: track k's gain this block is not 1 */

/* a note track t starts (voice.c trk_note_on) */
static void duck_note(const track_t *t, uint32_t note)
{
    if (!drum_engine(t->engine % NENGINES) || !drum_lane(note))   /* (a kit, DRUM or DRUM-X: its KICK lane only) */
        duck_hits |= (uint8_t)(1u << (t - trk));
}

/* once a block, after its events: each track's envelope (a hit of its source: down, else back) and its gain, from
 * the last block's (r0 below 1) to this one's (r1) */
static __attribute__((noinline)) void duck_block(uint32_t n)
{
    uint32_t k, hits = duck_hits, on = 0;
    duck_hits = 0;
    for (k = 0; k < NPART; k++) {
        const track_t *t = &trk[k];
        uint32_t src = (uint32_t)t->p[P_DKSRC];
        int32_t e = duck[k].env;
        duck[k].r0 = duck[k].r1;
        if (!src || src > NPART || src - 1u == k)
            e = duck[k].atk = 0;
        else if ((hits >> (src - 1u)) & 1u)
            duck[k].atk = 1;
        if (duck[k].atk) {
            e += (int32_t)(32768u * n / DUCK_ATK);
            if (e >= 32768) {
                e = 32768;
                duck[k].atk = 0;
            }
        } else if (e) {
            uint32_t len = TIME_MS_X10[t->p[P_DKREL] & 127] * 441u / 100u;
            e -= (int32_t)(32768u * n / (len ? len : 1u));
            e = e > 0 ? e : 0;
        }
        duck[k].env = e;
        duck[k].r1 = (t->p[P_DKAMT] * 258 * e) >> 15;
        if (duck[k].r0 || duck[k].r1)
            on |= 1u << k;
    }
    duck_on = (uint8_t)on;
}

/* track k's signal this block, its gain ramped from 1 - r0 to 1 - r1 */
static __attribute__((noinline)) AUDIO_RAM void duck_apply(uint32_t k, int32_t *b, uint32_t n)
{
    uint32_t i;
    int32_t g = 32768 - duck[k].r0, d = (duck[k].r0 - duck[k].r1) / (int32_t)n;
    for (i = 0; i < n; i++, g += d)
        b[i] = mulq16(b[i], (uint32_t)g << 1);
}
