/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff: TAKES (DESIGN.md, SHIFT + SAVE). While a track plays and is not recording, what is played on it (the keys and
 * their chords, MIDI, its arpeggiator: every note REC would record) and the knob moves on it are logged, each stamped
 * with the pass of the track's loop and the step REC would put it in (the step playing, or the next one when past the
 * middle of the playing one). SHIFT + SAVE (take_commit, ui_take.c) puts the last complete pass into the track: its
 * notes as REC records them (rec_put), a held note tied on, the knob moves as automation; one undo (SAVE held).
 * Included by seq.c (before motion.c: motion_capture logs the knobs). take_note runs in the audio ISR (the keys, MIDI
 * and the arp play there), take_knob in the main loop; each entry is written whole before its count is published. */
#define TAKE_EV 256u                                  /* notes on and off: ~128 notes, the oldest overwritten */
#define TAKE_KV 64u                                   /* knob moves (one per pass, step and parameter) */
#define TAKE_LAT 0u                                   /* samples a stamp is moved back (the key's way to the ISR):
                                                       * 0 as REC; the calibration knob for the hardware */
typedef struct { uint16_t pass; uint8_t trk, step, note, vel; int8_t feel; } take_ev_t;   /* vel 0: the note's end;
                                                       * feel: where it was played, its step's NUDGE with FEEL */
typedef struct { uint16_t pass; uint8_t trk, step, param; int16_t value; } take_kv_t;
static take_ev_t take_ev[TAKE_EV] __attribute__((section(".ramx")));   /* (after the RAM code: app.ld) */
static take_kv_t take_kv[TAKE_KV] __attribute__((section(".ramx")));
static volatile uint32_t take_ev_n;                   /* entries ever written (the slot: n % TAKE_EV) */
static uint32_t take_kv_n;                            /* (main loop only) */
static volatile uint16_t take_pass[NTRK];             /* each track's pass since PLAY, from 1 (seq_tick: its step 0;
                                                       * PLAY parks it on its last step, so the first block counts 1) */
static volatile uint16_t take_lost[NTRK];             /* 1 + the latest pass of a track's entry overwritten (0: none) */
static uint16_t take_p0[NTRK] __attribute__((section(".pool")));   /* (Skiff phase 5) the pass each track's pattern came
                                                       * in after (launch.c launch_step: a song's repeats count from it) */

/* (2.5) FREE TAKES: what is played on a track while the transport is stopped (take_note's callers: the keys and their
 * chords, MIDI, the arp), each note's on and off with its time (fm1_ms / 2): SHIFT + SAVE stopped (ui_take.c
 * free_open) guesses the tempo, the length and the first beat and puts it into the track. FREE_EV entries (as many
 * notes, on and off, as half of them), the oldest overwritten; only those since the last stop count (seq_stop:
 * free_n0). While a picker is open (free_hold) nothing is logged and its PLAY's stop moves nothing. An entry: the time
 * (15 bits: round in 65 s) | the note << 15 | its velocity << 22 (0: its end) | the track << 29. In the pool (.bss and
 * the RAM code's room are tight): 2 KiB */
#define FREE_EV 512u
static uint32_t free_ev[FREE_EV] __attribute__((section(".pool")));
static volatile uint32_t free_n;                      /* entries ever written (the slot: n % FREE_EV) */
static uint32_t free_n0;                              /* the first one since the last stop */
static volatile uint8_t free_hold;                    /* a picker open (ui_take.c) */
static void free_note(const track_t *t, uint32_t note, uint32_t vel)   /* (audio ISR) */
{
    if (free_hold)
        return;
    free_ev[free_n % FREE_EV] = ((fm1_ms >> 1) & 0x7FFFu) | (note & 127u) << 15 | (vel & 127u) << 22 |
                                (trk_index(t) & 7u) << 29;
    RING_PUBLISH();
    free_n++;
}
static void free_stop(void)                           /* the transport stopped (seq_stop): a new free take from here */
{
    if (!free_hold)
        free_n0 = free_n;
}

/* PLAY (seq_start): a new log */
static void take_clear(void)
{
    uint32_t k;
    take_ev_n = 0;
    take_kv_n = 0;
    for (k = 0; k < NTRK; k++)
        take_pass[k] = take_lost[k] = take_p0[k] = 0;
}

/* where track t is now, as REC places a note: *pass and the step; *feel: its NUDGE there (seq.c feel_of) */
static uint32_t take_stamp(const track_t *t, uint32_t *pass, int32_t *feel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, next = 0;
    uint32_t pos = t->seq_pos > TAKE_LAT ? t->seq_pos - TAKE_LAT : 0u;
    *pass = take_pass[trk_index(t)];
    if (t->seq_pos < 0x7FFFFFFFu && pos > step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u &&
        t->p[P_DIR] != DIR_RAND) {                    /* (0x7FFFFFFF: PLAY, before the first block; RAND: no next) */
        if (seq_pass_last(t, len))
            ++*pass;                                  /* past the middle of the pass's last step: the next pass */
        idx = seq_peek(t, len);                       /* the next step in its direction (seq.c) */
        next = 1;
    }
    *feel = t->seq_pos < 0x7FFFFFFFu ? feel_of(t, pos, next) : 0;
    return idx;
}

/* a note on (vel > 0) or off played on track t, not recorded: logged (audio ISR) */
static __attribute__((noinline)) void take_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t pass, step, i;
    int32_t feel;
    if (!song.playing) {
        free_note(t, note, vel);                      /* ((2.5) stopped: a free take's) */
        return;
    }
    if (rec_on(t))
        return;                                       /* (REC records it itself) */
    step = take_stamp(t, &pass, &feel);
    i = take_ev_n % TAKE_EV;
    if (take_ev_n >= TAKE_EV && take_ev[i].pass + 1u > take_lost[take_ev[i].trk % NTRK])
        take_lost[take_ev[i].trk % NTRK] = (uint16_t)(take_ev[i].pass + 1u);   /* (that pass: a partial take) */
    take_ev[i].pass = (uint16_t)pass;
    take_ev[i].trk = (uint8_t)trk_index(t);
    take_ev[i].step = (uint8_t)step;
    take_ev[i].note = (uint8_t)note;
    take_ev[i].vel = (uint8_t)vel;
    take_ev[i].feel = (int8_t)feel;
    RING_PUBLISH();
    take_ev_n++;
}

/* a knob move on track t's parameter id (motion.c motion_capture, not recording; main loop): the last value of each
 * pass, step and parameter */
static void take_knob(track_t *t, uint32_t id, int16_t value)
{
    uint32_t pass, step, k = trk_index(t), i, n = take_kv_n < TAKE_KV ? take_kv_n : TAKE_KV;
    int32_t feel;
    if (!song.playing)
        return;
    step = take_stamp(t, &pass, &feel);               /* (a knob move: on the grid, its feel unused) */
    for (i = 0; i < n; i++)
        if (take_kv[i].pass == pass && take_kv[i].trk == k && take_kv[i].step == step && take_kv[i].param == id) {
            take_kv[i].value = value;
            return;
        }
    i = take_kv_n++ % TAKE_KV;
    take_kv[i].pass = (uint16_t)pass;
    take_kv[i].trk = (uint8_t)k;
    take_kv[i].step = (uint8_t)step;
    take_kv[i].param = (uint8_t)id;
    take_kv[i].value = value;
}
