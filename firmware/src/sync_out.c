/* SPDX-License-Identifier: GPL-3.0-only */
/* MENU > AUDIO > SYNC OUT, SYNC PPQN: an analog sync out of the line out / headphone jack, for gear that follows pulses
 * in its audio input (a Teenage Engineering Pocket Operator in SY4: sync on the left, audio on the right, 2 pulses a
 * quarter note; a Volca, a modular's clock input, through a cable split into L and R).
 *   SYNC OUT   NO SYNC: STEREO        the line out as always
 *              LEFT SYNC: MONO/SYNC   the pulses on the left, the music summed to mono on the right (the PO's)
 *              RIGHT SYNC: SYNC/MONO  the music summed to mono on the left, the pulses on the right
 *   SYNC PPQN  1..24                  pulses a quarter note (2: the PO's and the Volca's; 24: MIDI clock's rate)
 *   SYNC INVERT ON, OFF               the pulse upside down: the FM-1's headphone amp inverts, so ON (the default)
 *                                     gives the PO a rising edge; OFF for an output that doesn't (the emulator's)
 * The unit's settings, as CLOCK OUT (not a project's): one byte, favorites.factory[15][29], STYLE's until Skiff U2
 * retired it (left as saved since, 0..3): bit 7 INVERT OFF, SYNC OUT in bits 5..6, SYNC PPQN - 1 in bits 0..4 XOR 1;
 * 0 (a new unit, RESET SETTINGS) = NO SYNC, 2 PPQN, INVERT ON; a STYLE kept reads NO SYNC. MENU writes it (menu_items.c),
 * the audio ISR reads it (MSET).
 * The pulses keep the sequencer's own grid as the click and CLOCK OUT do (seq.c): the first with step 0, each at its
 * sample in the block, PPQN of them to 4 x div_samples(1/16), the internal clock's or an external clock's. Only while
 * the transport runs: a PO starts with the first pulse and stops when they stop, so none in a count-in or while
 * stopped. The pulse's shape is PocketSync's (github.com/chr15m/PocketSync, a phone app the POs follow), as JAN3000
 * found it (github.com/JanSchulten/JAN3000-M-Wave-Firmware, commit d06f34e, GPL-3.0; its PO INV, SYNC INVERT ON here,
 * is the one an FM-1 drives a PO-33 with): SY_PULSE_MS at full scale (half
 * the period at most: 24 PPQN, a fast tempo), and between the pulses -1 % of it, never 0: the FM-1's I2S DAC mutes a
 * channel that stays at 0, so a pulse from 0 came out of a muted channel, ramped up, a tick in the phones and no edge
 * for the PO. After MASTER, the click and USB audio (audio.c audio_block): MASTER does not scale it, USB audio stays
 * stereo and pulse-free, the HOME scope shows the music */
enum { SO_STEREO, SO_LEFT, SO_RIGHT };                     /* SYNC OUT: no sync; sync left, music right; the other way */
#define SY_BYTE MSET(15, 29)                               /* (ui.c ui_sync, menu_items.c) */
#define SY_NORMAL 0x80u                                    /* (SYNC INVERT OFF: the pulse upward) */
#define SY_PULSE_MS 20u                                    /* the pulse's width (PocketSync's) */
#define SY_W (FS * SY_PULSE_MS / 1000u)                    /* .. in samples */
#define SY_LEVEL ((1 << 23) - 128)                         /* the pulse: the DAC's 24-bit full scale (JAN3000's) */
#define SY_IDLE (-SY_LEVEL / 100)                          /* .. between the pulses: -1 %, the DAC kept live (INVERT
                                                            * OFF; ON both the other way) */
#define SY_START 0xFFFFFFFFu                               /* (seq.c seq_start) the next block pulses at its start */
static uint32_t sy_clk = SY_START;                         /* samples x PPQN into the pulse's period (4 x a 1/16) */
static int32_t sy_at = -1;                                 /* this block: the sample its pulse starts at; -1 none */
static uint32_t sy_hi, sy_w = SY_W;                        /* the pulse's samples still high; its width */
static uint32_t sy_mode(uint32_t b)                        /* SO_* of the byte b: SYNC OUT (3, no name: NO SYNC) */
{
    uint32_t m = b >> 5 & 3u;
    return m <= SO_RIGHT ? m : SO_STEREO;
}

/* the transport ran adv samples on in a block of n (seq.c events_block, after the click): its pulse, if one falls in it.
 * (As mo_block_begin's MIDI clock, with PPQN for 24 and the beat for the 1/16: at 24 PPQN and 240 BPM a pulse every
 * 459 samples, so at most one a block.) With NO SYNC too, so SYNC OUT switched on while playing pulses on the beat at
 * once (sy_render passes sy_at over). Out of line: inlined, events_block's loops grew (tests/target_budget.py) */
static __attribute__((noinline)) void sy_tick(uint32_t adv, uint32_t n)
{
    uint32_t q = 4u * div_samples(2), ppqn = ((SY_BYTE & 31u) ^ 1u) + 1u, c0, at;
    sy_w = q / ppqn / 2u < SY_W ? q / ppqn / 2u : SY_W;   /* (half the period at most) */
    c0 = sy_clk < q ? sy_clk : q;                          /* (q: due on this block's first sample; SY_START, a tempo
                                                            * raised, a catch-up: so, no burst of pulses) */
    sy_clk = c0 + ppqn * adv;
    at = (q - c0 + ppqn - 1u) / ppqn;                      /* (the sample it is due on) */
    if (at < adv) {                                        /* (one of this block's samples, not the next's) */
        sy_clk -= q;
        sy_at = (int32_t)(at < n ? at : n - 1u);
    }
}

/* the DAC's block (24-bit stereo, audio.c, last): in LEFT / RIGHT SYNC the music summed to mono on one side, the pulse
 * on the other. Out of line, a loop only with a sync on (NO SYNC: the block as it was, bit for bit) */
static __attribute__((noinline)) void sy_render(int32_t *out, uint32_t n)
{
    uint32_t b = SY_BYTE, i, m = sy_mode(b), s;
    int32_t hi = SY_LEVEL, lo = SY_IDLE;                   /* (SYNC INVERT OFF; ON both the other way) */
    if (m == SO_STEREO) {
        sy_at = -1;
        return;
    }
    s = m == SO_LEFT ? 0u : 1u;                            /* (the pulse's side) */
    if (!(b & SY_NORMAL)) {
        hi = -hi;
        lo = -lo;
    }
    for (i = 0; i < n; i++) {
        int32_t mono = (out[2u * i] + out[2u * i + 1u]) >> 1;
        if ((int32_t)i == sy_at)
            sy_hi = sy_w;
        out[2u * i + s] = sy_hi ? hi : lo;
        out[2u * i + (s ^ 1u)] = mono;
        sy_hi -= sy_hi != 0u;
    }
    sy_at = -1;
}
