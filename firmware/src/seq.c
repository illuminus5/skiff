/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport.
 * Everything here runs in the audio ISR, once per CTL-sample block, and
 * ends in trk_note_on / trk_note_off: engines never see where a note came from.
 * Six tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI IN by GLO > SYSTEM ROUT (G_ROUTE):
 * CH1-6 (0) channels 1..6 play parts 1..6, channels 7..16 are ignored (midi_control.c midi_event); SEL (1)
 * every channel the selected track. Their CC1 (mod wheel), CC11 (expression) and channel aftertouch go to
 * the same track's modulation matrix (mod.c; the selected track: the one selected then).
 * A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). With the ARP on, the notes the arp
 * plays are recorded, not the keys held (what plays back is what was heard). A key or MIDI note plays a
 * chord when the track's CHRD is on (chord.c): its notes go through the same input as so many keys. */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static volatile uint8_t kb_boot_hold;    /* #137: set from power-on to the end of the splash (main.c): the keys sound nothing,
                                          * and one held across its end stays silent until it is let go and pressed again */
#if BOARD_SMK37
/* (SMK-37 Pro, SMK37_UX.md outer keys A) 37 keys: the FM-1's 27 (F3..G5) as keys 0..26, the outer ten after them, C3..E3
 * as 27..31 and G#5..C6 as 32..36 (kb_outer): kb_bed, the keybed's key; kb_semi, its offset from F3 (the low five below
 * it: a negative one). Each key plays with its velocity through MENU > KEY VELOCITY (smk_input.h smk_vel); the FM-1's
 * all at 100. KB_IN: the key the arp keeps (in_key); the outer ones none, they play as MIDI IN does */
#define KB_N 37u
static uint32_t kb_bed(uint32_t k) { return k < 27u ? k + SMK_KEY_F3 : k < 32u ? k - 27u : k; }
static uint32_t kb_semi(uint32_t k) { return k < 27u ? k : k < 32u ? k - 32u : k - 5u; }
#define KB_VEL(k) smk_vel(fm1_in.key_vel[kb_bed(k)])
#define KB_IN(k) ((k) < 27u ? (k) + 1u : 0u)
#else
#define KB_N 27u
#define KB_VEL(k) 100u
#define KB_IN(k) ((k) + 1u)
#endif
static uint8_t kb_note[KB_N], kb_trk[KB_N];   /* per key: the note it started and on which track */
static uint8_t in_key;                   /* the key (+ 1) whose notes go into input_on now (key_on), 0: none (MIDI IN) */
/* (Skiff 1.3, from Felucca 1.5 #191) where each track's held[i] came from, so the arp maps it again as it plays
 * (arp_list: TRN, ROOT, SCALE, QNT as they are now). held_key: the key + 1 (bits 0..4) and the octave buttons then + 3
 * (bits 5..7); 0: not a key's own mapping (MIDI IN, the DRUM grid, an engine's own key map), held[i] plays as it is.
 * held_dt: held[i] above the key's note (a CHRD chord's other notes). Out of track_t: its size stays as the audio
 * ISR's trk[] indexing wants it */
static uint8_t held_key[NTRK][16];
static int8_t held_dt[NTRK][16];
static volatile uint8_t kb_asleep;       /* MENU > SCREEN OFF (ui.c): the screen dark or waking, a key pressed now only
                                          * wakes it (silent, no layer, its release nothing) */
static uint8_t last_note = 60;
static uint8_t last_hits __attribute__((section(".pool")));   /* ((2.0) the notes struck, counted: the same key again
                                                                * picks its lane again too, ui.c lane_follow) */
static volatile uint8_t transport_req;   /* 1 start, 2 stop, 3 restart (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t midi_hint;       /* MIDI IN played track n - 1, which is not the selected one (the UI says so) */

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

/* (Skiff 1.3, from Felucca 1.5.5's #202) MENU > MIDI > T1..T6 CHANNEL, the unit's: each track's MIDI channel, kept as
 * its distance up from the track's own (ui.c mch_io: settings bytes; 0, every older setting: channel k + 1 for track k,
 * CH1-6 as before). MIDI IN CH1-6 hears them (midi_slot), the keys play out on them; a build without the UI: CH1-6 */
static const uint8_t *const mch_io;
static uint32_t trk_midi_ch(uint32_t i)            /* MIDI channel 0..15 of track i (MIDI IN; keys -> MIDI out) */
{
    i %= NTRK;
    return mch_io ? (i + mch_io[i]) & 15u : i;
}

static int drum_track(const track_t *t) { return drum_engine(eng_idx(t->eng_req)); }   /* ((Skiff) DRUM or DRUM-X) */

/* the 27 keys from F: black or white, and the key's place among the keys of its colour (white 0..15, black
 * 0..10). The DRUM grid: white keys are steps, black keys 1..8 lanes, 9 ACC, 10 / 11 the page */
static int key_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
static uint32_t key_place(uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++)
        n += (uint32_t)(key_black(i) == key_black(k));
    return n;
}
enum { GK_ACC = NLANE, GK_PGDN, GK_PGUP };       /* black keys 9..11 on the grid */

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

/* note n moved down onto the track's scale (ROOT, SCALE): QNT SNAP's keys and QNT SEQ's sequenced notes */
static int32_t scale_snap(const track_t *t, int32_t n)
{
    uint32_t mask = scale_mask(t), guard = 12;
    while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
        n--;
    return n;
}

enum { QN_OFF, QN_SNAP, QN_WHITE, QN_SEQ, QN_SEQT };   /* P_QUANT ((1.3, from Felucca 1.5.6) SEQ+T: appended; older
                                                         * firmware loads SEQ) */

/* the engine's own key map (DRUM's GM map, slices) for key k, -1 none */
static int32_t kb_engine(const track_t *t, uint32_t k)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];   /* (the engine it switches to) */
    return e->keys ? e->keys(t, k) : -1;
}

/* key k's note with the octave buttons at oct: QNT, ROOT, SCALE, TRN (KB_SILENT: a black key under WHITE). The keys
 * (kb_map) and the arp's latched keys (arp_list: as the keys play now, the octave they were pressed in) */
static uint32_t kb_pitch(const track_t *t, uint32_t k, int32_t oct)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (t->p[P_QUANT] == QN_SNAP || t->p[P_QUANT] >= QN_SEQ)   /* SNAP (and SEQ, SEQ+T): every key, rounded down */
        return (uint32_t)clamp(scale_snap(t, n + 12 * oct + t->p[P_TRANS]), 0, 127);
    if (t->p[P_QUANT] == QN_WHITE) {                    /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = scale_mask(t), i;
        int32_t count = 0, degree = DEGREE[n % 12], wo;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        wo = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            wo--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * wo + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * oct + t->p[P_TRANS], 0, 127);
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    int32_t n = kb_engine(t, k);
    return n >= 0 ? (uint32_t)n : kb_pitch(t, k, song.octave);
}

#include "chord.c"

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i, k = in_key - 1u;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u) {
        /* (Felucca #191) a key the track's scale keyboard maps (not the DRUM grid, not an engine's own map) is kept as
         * that key, so TRN, ROOT, SCALE and QNT reach the notes the arp holds (arp_list); the octave stays the one it
         * was pressed in. Anything else (MIDI IN) plays as it came, as before */
        uint8_t *hk = held_key[trk_index(t)];
        hk[t->nheld] = in_key && !song.grid && kb_engine(t, k) < 0
                           ? (uint8_t)(in_key | (uint32_t)(song.octave + 3) << 5) : 0u;
        held_dt[trk_index(t)][t->nheld] = (int8_t)(hk[t->nheld] ? (int32_t)note - kb_note[k] : 0);
        t->held[t->nheld++] = (uint8_t)note;
    }
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    uint8_t *hk = held_key[trk_index(t)];
    int8_t *dt = held_dt[trk_index(t)];
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note) {
            hk[k] = hk[i];
            dt[k] = dt[i];
            t->held[k++] = t->held[i];
        }
    t->nheld = (uint8_t)k;
}

static void rec_note(track_t *t, uint32_t note, uint32_t vel);
static void rec_release(track_t *t, uint32_t note);
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx);
static int rec_on(const track_t *t) { return ((song.rec >> trk_index(t)) & 1u) && song.playing; }

/* Skiff: the play direction (P_DIR). FWD 1..LEN; REV LEN..1; PING up and down, each end once (1 2 3 4 3 2 1 2 ..);
 * RAND any step. A pass is LEN steps played in any direction (seq_cnt: TAKES' and the trig conditions' passes, a
 * song's rows); FWD's passes start on step 1 as they always did */
enum { DIR_FWD, DIR_REV, DIR_PING, DIR_RAND };
enum { NUD_LATE = 1, NUD_EARLY = 2, NUD_DONE = 4 };   /* (Skiff) a track's NUDGE pending (track_t nud: seq_enter) */
/* the step after step i (< len) in direction d (PING: down, on its way down; RAND: no one can know, i) */
static uint32_t seq_after(uint32_t d, uint32_t i, uint32_t down, uint32_t len)
{
    switch (d) {
    case DIR_REV:
        return i ? i - 1u : len - 1u;
    case DIR_PING:
        if (len < 2u)
            return 0;
        if (down)
            return i ? i - 1u : 1u;
        return i + 1u < len ? i + 1u : len - 2u;
    case DIR_RAND:
        return i;
    default:
        return (i + 1u) % len;
    }
}
/* the step after the playing one, nothing changed (REC and TAKES round a late note onto it; RAND: the playing one) */
static uint32_t seq_peek(const track_t *t, uint32_t len)
{
    if (t->p[P_DIR] == DIR_FWD)
        return (t->seq_idx + 1u) % len;
    return seq_after((uint32_t)t->p[P_DIR], t->seq_idx < len ? t->seq_idx : len - 1u, t->seq_down, len);
}
/* the playing step is the last of its pass (a song's row ends after it; a late note rounds into the next pass) */
static int seq_pass_last(const track_t *t, uint32_t len)
{
    return t->p[P_DIR] == DIR_FWD ? t->seq_idx + 1u == len : t->seq_cnt >= len;
}
/* where in its pass the playing step is in time, 0.. (the header's bar.beat): FWD the step itself */
static uint32_t seq_at(const track_t *t)
{
    return t->p[P_DIR] == DIR_FWD || !t->seq_cnt ? t->seq_idx : t->seq_cnt - 1u;
}
/* (Skiff) FEEL (the REC layer's KNOB 4, ui.c REC_FEEL): REC and TAKES keep where a note was played, pos samples into
 * the playing step, as the NUDGE of the step it goes in (that one, or next: the next one, the note early for it) */
static uint8_t rec_feel;
static int32_t feel_of(const track_t *t, uint32_t pos, uint32_t next)
{
    uint32_t q = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 64u;
    int32_t f;
    if (!q)
        return 0;
    pos = pos < 64u * q ? pos : 64u * q;
    f = next ? -(int32_t)((64u * q - pos + q / 2u) / q) : (int32_t)((pos + q / 2u) / q);   /* (to the nearest 64th) */
    return f < NUDGE_MIN ? NUDGE_MIN : f > NUDGE_MAX ? NUDGE_MAX : f;
}
/* the next step: where a run starts (PLAY, a song's row: seq_cnt 0) by its direction, then seq_peek's, RAND's own */
static __attribute__((noinline)) uint32_t seq_next(track_t *t, uint32_t len)
{
    uint32_t d = (uint32_t)t->p[P_DIR], n;
    if (d == DIR_RAND)
        return rng() % len;
    if (!t->seq_cnt && d != DIR_FWD) {
        t->seq_down = 0;
        return d == DIR_REV ? len - 1u : 0u;
    }
    n = seq_peek(t, len);
    if (d == DIR_PING)
        t->seq_down = (uint8_t)(n < (t->seq_idx < len ? t->seq_idx : len - 1u));
    return n;
}

#include "take.c"                                   /* Skiff: TAKES' log (input_on/off, the arp, motion_capture) */
static volatile uint8_t seq_fill;                   /* Skiff: FILL held (GLO + F4, ui_layer.c): the FILL steps play */
/* Skiff: step s of track t passes its trig condition (core.h TC_*) on this pass of the track's loop (take_pass: the
 * first pass since PLAY is 1; ahead 1: the next pass, a step played early (NUDGE) before its pass starts); a step
 * without one always does. No random number taken (the CHANCE roll after it).
 * Not inlined, and called only for a step that has one: kept out of the audio ISR's loop (tests/target_budget.py) */
static __attribute__((noinline)) int step_cond_ok(const track_t *t, const step_t *s, uint32_t ahead)
{
    static const uint8_t A[TC_COUNT] = {0, 0, 0, 1, 2, 1, 2, 3, 1, 2, 3, 4};
    static const uint8_t B[TC_COUNT] = {1, 1, 1, 2, 2, 3, 3, 3, 4, 4, 4, 4};
    uint32_t c = s->cond, p;
    if (c == TC_NONE || c >= TC_COUNT)
        return 1;
    if (c == TC_FILL || c == TC_NFILL)
        return (c == TC_FILL) == (seq_fill != 0u);
    p = take_pass[trk_index(t)] + ahead;
    return p > 0u && (p - 1u) % B[c] + 1u == A[c];
}
#include "motion.c"

/* the ARP's modes (P_AMODE). Append-only: a stored value keeps its meaning (ORD plays as UP, the ORD switch of
 * ARP 2 orders the notes). 1.2 (Discussion #95), over the note list (the held notes low to high, or as played,
 * across OCT octaves), C E G held: DNUP G E C E; UP+8 C E G C' (the first note again an octave above the top);
 * CONV C G E (outside in); DIVG E G C (inside out); PINKY C G E G (the top note between the others); THUMB
 * C E C G (the bottom one); WALK a random step up or down the list (bouncing off its ends); CHORD all the held
 * notes together (the first 4), an octave higher each step through OCT */
enum { AM_OFF, AM_UP, AM_DN, AM_UPDN, AM_RND, AM_ORD, AM_REPEAT, AM_DNUP, AM_UP8, AM_CONV, AM_DIVG, AM_PINKY,
       AM_THUMB, AM_WALK, AM_CHORD };

static void arp_forget(track_t *t)               /* (its notes ended otherwise: trk_all_off) */
{
    t->arp_note = t->arp_ch[0] = t->arp_ch[1] = t->arp_ch[2] = 0;
}

/* CHORD's notes. Not inlined: they run once an arp step, and kept out of the audio ISR's loop over the tracks
 * (tests/target_budget.py) */
static __attribute__((noinline)) void arp_chord_off(track_t *t, int rec)   /* the other notes end */
{
    uint32_t i;
    for (i = 0; i < 3u; i++)
        if (t->arp_ch[i]) {
            if (rec)
                rec_release(t, t->arp_ch[i]);
            else
                take_note(t, t->arp_ch[i], 0);       /* (Skiff: TAKES) */
            trk_note_off(t, t->arp_ch[i]);
            t->arp_ch[i] = 0;
        }
}

static __attribute__((noinline)) void arp_chord(track_t *t, const uint32_t *list, uint32_t n)
{
    uint32_t i, nt, top = 0, o = t->arp_idx % (uint32_t)(t->p[P_AOCT] > 0 ? t->p[P_AOCT] : 1);
    if (n > 4u)
        n = 4u;
    for (i = 0; i < n; i++) {                       /* the held notes (sorted or as played), o octaves up */
        nt = (uint32_t)clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
        if (nt == 127u && top++)
            continue;                               /* (notes clamped onto the top one: once) */
        if (!t->arp_note)
            t->arp_note = (uint8_t)nt;
        else
            t->arp_ch[i - 1u] = (uint8_t)nt;
        if (rec_on(t))
            rec_note(t, nt, 100);
        else
            take_note(t, nt, 100);                  /* (Skiff: TAKES) */
        trk_note_on(t, nt, 100);
    }
}

static void arp_silence(track_t *t)              /* the sounding arp note(s) end */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, 0);
    trk_note_off(t, t->arp_note);
    t->arp_note = 0;
}

static void arp_off(track_t *t)                  /* the sounding arp note(s) end (a recorded one too) */
{
    if (t->arp_ch[0] | t->arp_ch[1] | t->arp_ch[2])
        arp_chord_off(t, rec_on(t));
    trk_note_off(t, t->arp_note);
    if (rec_on(t))
        rec_release(t, t->arp_note);
    else
        take_note(t, t->arp_note, 0);               /* (Skiff: TAKES) */
    t->arp_note = 0;
}

/* the place in the note list (len notes) the step arp_idx plays (not inlined, as arp_chord); UP+8 adds its note */
static __attribute__((noinline)) uint32_t arp_pick(track_t *t, uint32_t *list, uint32_t len)
{
    uint32_t i = t->arp_idx, m = (uint32_t)t->p[P_AMODE], cyc, k, w;
    if (m == AM_UP8 && len < 64u)                   /* UP+8: the first note again over the top */
        list[len++] = (uint32_t)clamp((int32_t)list[0] + 12 * t->p[P_AOCT], 0, 127);
    cyc = len > 1u ? 2u * len - 2u : 1u;
    k = i % cyc;
    switch (m) {
    case AM_DN:
        return len - 1u - i % len;
    case AM_UPDN:
        return k < len ? k : cyc - k;
    case AM_DNUP:
        return k < len ? len - 1u - k : k - (len - 1u);
    case AM_RND:
        return rng() % len;
    case AM_CONV:
    case AM_DIVG:
        k = i % len;
        if (m == AM_DIVG)
            k = len - 1u - k;
        return (k & 1u) ? len - 1u - k / 2u : k / 2u;
    case AM_PINKY:
        return len < 2u ? 0u : (k & 1u) ? len - 1u : k / 2u;
    case AM_THUMB:
        return len < 2u ? 0u : (k & 1u) ? 1u + k / 2u : 0u;
    case AM_WALK:
        w = t->arp_walk;
        if (!i)
            w = 0;                                  /* (from the bottom) */
        else if (w >= len)
            w = len - 1u;                           /* (notes let go) */
        else if (len > 1u)
            w = !w ? 1u : w == len - 1u ? w - 1u : (rng() >> 7) & 1u ? w + 1u : w - 1u;
        t->arp_walk = (uint8_t)w;
        return w;
    default:
        return i % len;
    }
}

/* n: samples the arp advances by (the external clock's while it runs, 0 stopped); gate_n: samples its
 * gate counts down by (real time when the external transport stops: a tapped or latched note still ends) */
static volatile uint32_t beat_pos, beat_n;      /* samples into the beat, the beat of the bar (0..3) */
static volatile uint32_t launch_steps;          /* (Skiff) 1/16 steps since PLAY (launch.c launch_clock: BAR / BEAT) */
static uint32_t launch_pos;                     /* (Skiff) .. samples into the one now (counted before the tracks tick) */

/* the metronome (click.c) and the count-in (1.1, Discussion #131). The click follows the song tempo's quarter notes
 * (BPM, or the external clock's 24 pulses), 4 to the bar, the first accented, whatever a track's DIV or SWING: a beat is
 * four 1/16 steps as the sequencer counts them (click_beat_len: 4 x div_samples(1/16), which may be a few samples
 * shorter than beat_samples, so the click never drifts from the 1/16 tracks it is recorded against). clk_pos and
 * clk_step count the 1/16 steps as seq_tick counts a 1/16 track's (and midi_clock_pulse rescales clk_pos with them when
 * an external clock's tempo moves): CLK_START at seq_start clicks the first beat in the block step 0 starts in, and
 * beat k in the block a 1/16 track's step 4k starts in (every 4th step; step 0 of 16, the bar's first, accented).
 * The count-in: with MENU > COUNT-IN on, PLAY from stop with a track armed (REC while stopped starts PLAY too) on
 * the internal clock counts cin_total beats first (1 or 2 bars, clicked whatever CLICK is), then
 * starts the sequencer at step 1 exactly that many beats later (song.playing stays 0 until then: nothing plays or
 * records; the keys sound). A note into an armed track in the count-in's last eighth (the second half of its last
 * beat) is recorded onto step 1 when it starts (cin_keep, cin_flush), held on or let go as it was. An external clock
 * never counts in (its Start starts). STOP (seq_stop) cancels it. */
#define CLK_START 0xFFFFFFFFu
static uint32_t clk_pos = CLK_START, clk_step;  /* samples into the 1/16 step, the step of the bar (0..15) */
static volatile uint8_t cin_left, cin_total;    /* the count-in's beats still to come (0: none), of how many */
static volatile uint32_t cin_pos;               /* samples into its beat playing */
static uint8_t cin_flush;                       /* the count-in ended in this block: record what was kept */
static uint8_t cin_note[NTRK][4], cin_vel[NTRK][4], cin_n[NTRK], cin_up[NTRK];   /* kept notes; cin_up bit k: let go */
static int seq_counting(void) { return cin_left != 0u; }
static uint32_t click_beat_len(void) { return 4u * div_samples(2); }   /* DIV 2: 1/16 */
static int click_wanted(void)                  /* CLICK REC: while a track is armed (not in a song: no recording) */
{
    return click_mode == CLICK_ON || (click_mode == CLICK_REC && song.rec);
}
/* the arp's note list (list, its length returned; 0: nothing to play): the held notes (sorted or as played) over OCT
 * octaves, *cnt of them. (Felucca #191) the notes as they play now, a key's mapped again through kb_pitch (TRN, ROOT,
 * SCALE, QNT now, the octave it was pressed in; a chord's note as far above it), the rest (MIDI IN) as they came; each
 * once (two keys may meet on one note now), a key silent now (a black key, QNT WHITE since) left out. With nothing
 * changed since the keys were pressed, exactly the list of held[] as before. (ui_pgraph.c graph_arp draws it) */
static uint32_t arp_list(const track_t *t, uint32_t *list, uint32_t *cnt)
{
    uint32_t i, j, n = 0, x, s, last = 0, len = 0, o;
    const uint8_t *hk = held_key[trk_index(t)];
    const int8_t *dt = held_dt[trk_index(t)];
    for (i = 0; i < t->nheld; i++) {
        x = t->held[i];
        if ((s = hk[i]) != 0u) {
            x = kb_pitch(t, (s & 31u) - 1u, (int32_t)(s >> 5) - 3);
            if (x == KB_SILENT)
                continue;
            x = (uint32_t)clamp((int32_t)x + dt[i], 0, 127);
        }
        last = x;
        for (j = 0; j < n && list[j] != x; j++)
            ;
        if (j == n)
            list[n++] = x;
    }
    *cnt = n;
    if (!t->p[P_AORDER])
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < n && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    if (n && t->p[P_AMODE] == AM_REPEAT) { list[0] = last; len = 1; } /* REPEAT: last played note, no octave traversal */
    return len;
}

/* an arp step starts (period: its length): the next note of the list. Not inlined: once a step, kept out of the audio
 * ISR's loop over the tracks (tests/target_budget.py) */
static __attribute__((noinline)) void arp_fire(track_t *t, uint32_t period)
{
    uint32_t cnt, list[64], len, j;
    if (!(len = arp_list(t, list, &cnt))) {        /* (every key silent now: QNT WHITE's black keys) */
        if (t->arp_note)
            arp_off(t);
        return;
    }
    t->arp_idx++;
    j = t->p[P_AMODE] == AM_CHORD ? 0u : arp_pick(t, list, len);
    if (t->arp_note)
        arp_off(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (t->p[P_AMODE] == AM_CHORD) {            /* CHORD: the first 4 notes, an octave up each step */
            arp_chord(t, list, cnt);
            return;
        }
        t->arp_note = (uint8_t)list[j];
        if (rec_on(t))
            rec_note(t, t->arp_note, 100);           /* recording: what the arp plays */
        else
            take_note(t, t->arp_note, 100);         /* (Skiff: TAKES, the same) */
        trk_note_on(t, t->arp_note, 100);
    }
}

static void arp_step(track_t *t, uint32_t n, uint32_t gate_n)
{
    uint32_t period;
    int32_t sw;
    if (t->arp_note) {
        if (t->arp_off <= gate_n)
            arp_off(t);
        else
            t->arp_off -= gate_n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld && t->arp_note)
            arp_off(t);
        return;
    }
    period = div_samples((uint32_t)t->p[P_ARATE]);
    sw = t->p[P_ASWING] * (int32_t)period / 250;
    t->arp_pos += n;
    if (t->arp_pos < period + (uint32_t)((t->arp_idx & 1u) ? sw : -sw) && t->arp_pos != 0xFFFFFFF + n)
        return;
    t->arp_pos = 0;
    arp_fire(t, period);
}

static void arp_tick(track_t *t, uint32_t n) { arp_step(t, n, n); }

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global, at most 100) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    return swing_step_len(t, period, idx);   /* core.h: own + global, at most 100 */
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only); (2.0, #25) on a POLY track playing forward it goes on beside it (below). */
/* (2.0, #25) Recording notes that overlap (a POLY track playing forward): a key held while another starts on a later
 * step goes on as its own pitch, a per-note tie a step (rec_hold_mix), beside what each step holds; recorded into a
 * TIE, the notes it holds go on beside the new one (rec_untie). rec_mix: holds started on different steps (0: the one
 * group of before 2.0, TIEs); rec_n: the steps each held note (rh_note's order) has gone on; rec_tm: those tied into
 * the step entered last (a release before its middle takes it out again) */
static uint8_t rec_mix[NTRK] __attribute__((section(".pool")));
static uint8_t rec_n[NTRK][4] __attribute__((section(".pool")));
static uint8_t rec_tm[NTRK] __attribute__((section(".pool")));
static int rec_poly(const track_t *t) { return trk_vmode(t) == V_POLY && t->p[P_DIR] == DIR_FWD && !drum_track(t); }
/* TIE step s (step i) as the notes it holds, tied: the notes of the step before them that can tie */
static __attribute__((noinline)) void rec_untie(const track_t *t, step_t *s, uint32_t i)
{
    const step_t *b = 0;
    uint32_t k, m;
    while (i && (b = &t->step[--i])->time == ST_TIE)
        b = 0;
    s->time = ST_NOTE;
    s->n = s->hit = s->acc = s->flags = 0;
    if (!b)
        return;
    m = b->time == ST_NOTE && step_ratchet(b) == 1u && !(b->flags & SF_FLAM) ? (1u << b->n) - 1u : step_ties(b);
    for (k = 0; k < b->n; k++)
        if ((m >> k) & 1u) {
            s->note[s->n] = b->note[k];
            s->acc |= (uint8_t)(1u << s->n++);
        }
}

/* a recorded note into step s: REC (rec_note) and Skiff's TAKES (ui_take.c) merge alike */
static void rec_put(track_t *t, step_t *s, uint32_t note, uint32_t vel)
{
    uint32_t k;
    if (drum_track(t)) {                            /* DRUM: into the grid (hits) */
        uint32_t l = drum_lane(note);
        if (s->time != ST_NOTE || (!s->n && !s->hit)) {
            s->n = s->hit = s->acc = 0;
            s->flags = 0;
            s->vel = 0;
        }
        s->time = ST_NOTE;
        if (note == DRUM_LANE_NOTE[l]) {            /* a lane's note: its hit */
            s->hit |= (uint8_t)(1u << l);
            if (vel > 110)
                s->acc |= (uint8_t)(1u << l);
        } else {                                    /* another GM drum (a low tom, a crash): a note */
            for (k = 0; k < s->n && s->note[k] != note; k++)
                ;
            if (k == s->n) {
                if (s->n < 4u)
                    s->n++;
                s->note[s->n - 1u] = (uint8_t)note;
            }
            if (vel > 110)
                s->flags |= SF_ACCENT;
        }
    } else {
        if (s->time != ST_NOTE || !s->n || trk_vmode(t) != V_POLY) {
            s->n = 0;                               /* a fresh step */
            s->flags = 0;
            s->vel = 0;
        }
        for (k = 0; k < s->n && s->note[k] != note; k++)
            ;
        if (k == s->n) {
            if (s->n < 4u)
                s->n++;
            s->note[s->n - 1u] = (uint8_t)note;
        }
        s->time = ST_NOTE;
        if (vel > 110)
            s->flags |= SF_ACCENT;
    }
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
}

static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, k, ti = trk_index(t);
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]);
    /* past its middle: the next step, (Skiff) the next in its direction (RAND: unknown, so the playing one) */
    uint32_t next = t->seq_pos > step_samples(t, period, t->seq_idx) / 2u && t->p[P_DIR] != DIR_RAND;
    step_t *s;
    if (next)
        idx = seq_peek(t, len);
    s = &t->step[idx];
    if (s->time == ST_TIE && rec_poly(t))           /* ((2.0) what it holds goes on beside it) */
        rec_untie(t, s, idx);
    if (rec_feel && !(((1u << s->n) - 1u) & ~step_ties(s)) && !s->hit)   /* (Skiff) FEEL: a note into a step with none */
        s->nudge = (int8_t)feel_of(t, t->seq_pos, next);                 /* starting there keeps its time */
    rec_put(t, s, note, vel);
    t->seq_active = 1;
    if (next && !((t->nud & NUD_DONE) && t->early_idx == idx)) {   /* it sounds now: the step must not trigger it
                                                                       * again ((Skiff) unless it has, early: NUDGE) */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < (drum_track(t) ? NLANE : 4u))
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (drum_track(t))                              /* DRUM: no holds (hits) */
        return;
    if (t->rh_n && t->rh_start != idx && rec_poly(t)) {   /* ((2.0) one in another step goes on: each its own */
        if (!rec_mix[ti])
            for (rec_mix[ti] = 1, k = 0; k < t->rh_n; k++)
                rec_n[ti][k] = t->rh_ties;
        t->rh_start = (uint8_t)idx;
    } else if (!t->rh_n || t->rh_start != idx) {    /* a new hold (one in another step ends) */
        t->rh_n = rec_mix[ti] = rec_tm[ti] = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u) {
        rec_n[ti][k] = 0;
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
    }
}

/* (2.0, #25) the sequencer enters step idx with held notes started on different steps: each goes on into it as its own
 * tie, beside what it holds (a TIE holds them already; its pitch a start there: its own, recorded ahead, or one of
 * before it takes on); no room there (4 notes, lane hits) or the whole pattern long: that one's hold ends */
static __attribute__((noinline)) void rec_hold_mix(track_t *t, uint32_t idx, uint32_t len)   /* (out of the ISR's loop) */
{
    step_t *s = &t->step[idx];
    uint32_t ti = trk_index(t), i, k = 0, x, tm = 0;
    t->rh_last = (uint8_t)idx;
    for (i = 0; i < t->rh_n; i++) {
        uint32_t p = t->rh_note[i], keep = rec_n[ti][i] + 1u < len, add = 0;
        if (keep && s->time != ST_TIE) {
            for (x = 0; x < s->n && s->note[x] != p; x++)
                ;
            if (x < s->n) {
                if (idx != t->rh_start && !s->hit)
                    s->acc |= (uint8_t)(1u << x);
            } else if (s->hit || s->n >= 4u) {
                keep = 0;
            } else {
                if (!s->n)
                    s->time = ST_NOTE;
                s->note[s->n] = (uint8_t)p;
                s->acc |= (uint8_t)(1u << s->n++);
                add = 1;
            }
        }
        if (keep) {
            t->rh_note[k] = (uint8_t)p;
            rec_n[ti][k] = (uint8_t)(rec_n[ti][i] + 1u);
            tm |= add << k;
            k++;
        }
    }
    t->rh_n = (uint8_t)k;
    rec_tm[ti] = (uint8_t)tm;
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it. (2.0) Out of the ISR's
 * track loop (target_budget.py): a step a call */
static __attribute__((noinline)) void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || (!rec_mix[trk_index(t)] && t->rh_ties + 1u >= len)) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (rec_mix[trk_index(t)]) {
        rec_hold_mix(t, idx, len);                  /* ((2.0) held notes started on different steps) */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* (recorded ahead into the step now starting) */
    s = &t->step[idx];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
    s->hit = s->acc = 0;
}

/* a key of a recorded note is up: the hold ends with the last one. (2.0, #25) Held notes started on different steps: each
 * ends with its own key; tied into the step playing, a release before its middle takes it out of it */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, ti = trk_index(t), tm = 0, out = 0, x, lo;
    int early = t->seq_idx == t->rh_last &&
                t->seq_pos < step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) / 2u;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note) {
            rec_n[ti][k] = rec_n[ti][i];
            tm |= ((rec_tm[ti] >> i) & 1u) << k;
            t->rh_note[k++] = t->rh_note[i];
        } else {
            out = (rec_tm[ti] >> i) & 1u;
        }
    if (k == t->rh_n)
        return;                                     /* not one of them */
    rec_tm[ti] = (uint8_t)tm;
    if (rec_mix[ti]) {
        step_t *s = &t->step[t->rh_last];
        t->rh_n = (uint8_t)k;
        for (x = 0; out && early && x < s->n && s->note[x] != note; x++)
            ;
        if (out && early && x < s->n) {             /* (its tie out of the step again, the ones above down a place) */
            lo = (1u << x) - 1u;
            for (; x + 1u < s->n; x++)
                s->note[x] = s->note[x + 1u];
            s->note[--s->n] = 0;
            s->acc = (uint8_t)((s->acc & lo) | ((s->acc >> 1) & ~lo));
            if (!s->n && !s->hit)
                s->time = ST_REST;
        }
        return;
    }
    if ((t->rh_n = (uint8_t)k))
        return;                                     /* others still held */
    if (t->rh_ties && early)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

/* a note into armed track t in the count-in's last eighth: kept for step 1 (cin_flush); once each, 4 per track */
static __attribute__((noinline)) void cin_keep(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t i = trk_index(t), k;
    if (cin_left != 1u || cin_pos < click_beat_len() / 2u || !((song.rec >> i) & 1u) || t->p[P_AMODE])
        return;
    for (k = 0; k < cin_n[i] && cin_note[i][k] != note; k++)
        ;
    if (k == cin_n[i]) {
        if (k == 4u)
            return;
        cin_n[i]++;
    }
    cin_note[i][k] = (uint8_t)note;
    cin_vel[i][k] = (uint8_t)vel;
    cin_up[i] &= (uint8_t)~(1u << k);
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    last_note = (uint8_t)note;
    last_hits++;
    if (cin_left)
        cin_keep(t, note, vel);
    if (rec_on(t) && !t->p[P_AMODE])               /* (ARP on: arp_tick records its notes) */
        rec_note(t, note, vel);
    else if (!t->p[P_AMODE])
        take_note(t, note, vel);                    /* (Skiff) not recorded: TAKES' log (take.c) */
    if (t->p[P_AMODE])
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static int midi_note_held(const track_t *t, uint32_t note);
static int midi_local_held(const track_t *t, uint32_t note);
static void input_off(track_t *t, uint32_t note)
{
    if (midi_note_held(t, note))
        return;
    if (cin_left) {                                 /* a kept note let go before step 1: recorded, then let go */
        uint32_t i = trk_index(t), k;
        for (k = 0; k < cin_n[i]; k++)
            if (cin_note[i][k] == note)
                cin_up[i] |= (uint8_t)(1u << k);
    }
    rec_release(t, note);
    if (!t->p[P_AMODE])
        take_note(t, note, 0);                      /* (Skiff: its end, for TAKES' holds) */
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* the effect of key k in the FX layer (PF_N none): the first PF_M1 white keys (F3 .. E5) in order, black keys
 * 1..6 (F#3 .. F#4) the tracks' cuts while held ((2.0) a tap: their MUTE, ui_layer.c mute_keys) */
static uint32_t perf_key(uint32_t k)
{
    uint32_t p = key_place(k);
    return !key_black(k) ? (p < PF_M1 ? p : PF_N) : p < NTRK ? PF_M1 + p : PF_N;
}

/* key k plays kb_note[k] on track t: its chord (chord.c; the note alone with CHRD OFF). A note another key
 * holds already sounds: it is not started again (nor sent to MIDI OUT); the key keeps its notes */
static void key_on(uint32_t k, track_t *t)
{
    uint32_t n = chord_build(t, kb_note[k], kb_chord[k]), i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    in_key = (uint8_t)KB_IN(k);                     /* (the arp keeps the key: arp_add) */
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_on(t, x, KB_VEL(k));
        midi_out_event(0x09u | (0x90u | mc) << 8 | x << 16 | KB_VEL(k) << 24);
    }
    in_key = 0;
    kb_chn[k] = (uint8_t)n;
    last_note = kb_note[k];                         /* (step entry, the SAMPLE zone: the key's note) */
}

/* key k is up: the notes it started end, but those another key still holds */
static void key_off(uint32_t k, track_t *t)
{
    uint32_t n = kb_chn[k], i, mc = trk_midi_ch(trk_index(t));
    kb_chn[k] = 0;
    for (i = 0; i < n; i++) {
        uint32_t x = kb_chord[k][i];
        if (midi_local_held(t, x))
            continue;
        input_off(t, x);
        midi_out_event(0x08u | (0x80u | mc) << 8 | x << 16);
    }
}

#ifdef FM1_INPUT_LAT
/* the press latency stats (hal/fm1_input.h fm1_in_stat, console `inp`): from the first scan that saw
 * key k closed to its note-on here, and to the note's first sample leaving the DMA (kb_out_tick:
 * audio.c, the TIMER4 tick at which the block being rendered starts to play) */
static uint32_t kb_out_tick;
static __attribute__((noinline)) void kb_lat(uint32_t k)
{
    uint32_t t0 = fm1_in.note_t0[k], d = fm1_ticks() - t0, o = kb_out_tick - t0;
    fm1_in_stat.kb_n++;
    fm1_in_stat.kb_sum += d;
    fm1_in_stat.dac_sum += o;
    if (d > fm1_in_stat.kb_max)
        fm1_in_stat.kb_max = d;
    if (o > fm1_in_stat.dac_max)
        fm1_in_stat.dac_max = o;
}
#endif

/* (SMK-37 Pro) the sustain pedal (smk_ui.c smk_pedal) holds the keys as MIDI IN's pedal its notes (midi_control.c);
 * ((2.0.3, #72) KBD HOLD, both boards) so does ARP 2's HOLD with the ARP's MODE OFF. While it holds, a key of the
 * selected track let go keeps its notes (kb_ped) until the hold ends (the pedal up; HOLD off, or the ARP on), another
 * track is selected, the track lets go of what it plays (panic_req: a preset, engine or project load) or the key is
 * pressed again (its notes end first); with KBD HOLD a key pressed with no key down starts a new chord (kb_fresh). A
 * DRUM track's keys end as they are let go */
static uint8_t kb_ped[KB_N], kb_ped_trk;            /* the keys held; their track + 1 (0: none) */
static int kb_pedal(void)
{
#if BOARD_SMK37
    return smk_pedal != 0;
#else
    return 0;
#endif
}
static int kb_hold_on(const track_t *t) { return kb_pedal() || (t->p[P_AHOLD] && !t->p[P_AMODE]); }
static void kb_up(uint32_t k)                       /* key k let go */
{
    track_t *t = &trk[kb_trk[k] % NTRK];
    if (kb_trk[k] == song.sel && !drum_track(t) && kb_hold_on(t)) {
        kb_ped[k] = 1;
        kb_ped_trk = (uint8_t)(song.sel + 1u);
    } else
        key_off(k, t);
}
static __attribute__((noinline)) void kb_again(uint32_t k)   /* key k pressed: what the hold keeps for it ends first */
{
    if (kb_ped[k]) {
        kb_ped[k] = 0;
        key_off(k, &trk[kb_trk[k] % NTRK]);
    }
}
/* the keys held end; a MONO track's sounding note last (else it would fall back to another one let go with it). Not
 * inlined, nor kb_again and kb_fresh: their loops stay out of the audio ISR's own cost (tests/target_budget.txt) */
static __attribute__((noinline)) void kb_ped_end(void)
{
    uint32_t k, last;
    kb_ped_trk = 0;
    for (last = 0; last < 2u; last++)
        for (k = 0; k < KB_N; k++) {
            track_t *t = &trk[kb_trk[k] % NTRK];
            if (kb_ped[k] && (last || kb_chord[k][0] != t->mono_note)) {
                kb_ped[k] = 0;
                key_off(k, t);
            }
        }
}
/* events_block, before the keys: the hold ended, another track selected or pr (panic_req's bits) for its track */
static void kb_ped_block(uint32_t pr)
{
    uint32_t i = kb_ped_trk - 1u;
    if (!kb_ped_trk || (i == song.sel && kb_hold_on(&trk[song.sel]) && !((pr >> i) & 1u)))
        return;
    kb_ped_end();
}
/* ((2.0.3, #72) KBD HOLD) key k pressed to play: with no other key down (playing), a new chord: what the hold kept ends
 * first; pressed while others are down, it joins them. Not while the SMK's pedal is down: that keeps them all */
static __attribute__((noinline)) void kb_fresh(uint32_t k)
{
    uint32_t j;
    if (!kb_ped_trk || kb_pedal())
        return;
    for (j = 0; j < KB_N; j++)
        if (j != k && kb_chn[j] && !kb_ped[j])
            return;
    kb_ped_end();
}

#if BOARD_SMK37
/* the ten outer keys (smk_input.h's queue, from the scan): notes on the selected track as the others play them; in a
 * layer (its tiles are F3..G5's), NAME, the DRUM grid and under the splash nothing; SCREEN OFF's wake press silent */
static void kb_outer(void)
{
    int32_t e;
    while ((e = smk_extkey_take()) >= 0) {
        uint32_t bed = (uint32_t)e >> 8, k = bed < 5u ? bed + 27u : bed;
        if (k < 27u || k >= KB_N)
            continue;
        if (e & 0xFF) {
            kb_again(k);
            kb_trk[k] = song.sel;
            kb_note[k] = KB_SILENT;
            if (kb_boot_hold || kb_asleep || (fm1_in.buttons & kb_mask) || kb_lock || song.grid)
                continue;
            kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], kb_semi(k));
            if (kb_note[k] != KB_SILENT) {
                kb_fresh(k);
                key_on(k, &trk[kb_trk[k]]);
            }
        } else if (kb_note[k] != KB_SILENT) {
            kb_up(k);
            kb_note[k] = KB_SILENT;
        }
    }
}
#endif

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    uint32_t lay, fx;
#if BOARD_SMK37
    kb_outer();
#endif
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    if (kb_boot_hold) {                           /* #137: under the splash; their release is silent too */
        for (k = 0; k < 27u; k++)
            if ((ch & cur) >> k & 1u)
                kb_note[k] = KB_SILENT;
        kb_prev = cur;
        return;
    }
    lay = (fm1_in.buttons & kb_mask) || kb_lock;  /* (#83: kb_lock, a layer locked open with no button held) */
    fx = (fm1_in.buttons & perf_mask) || (kb_lock & 2u);
#if FELUCCA_TAPE
    if(tape_page_on){lay=1;fx=0;} /* reuse layer key suppression outside the per-key loop */
#endif
    for (k = 0; k < 27u; k++) {
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            kb_again(k);
            kb_trk[k] = song.sel;
            if (kb_asleep) {                      /* (SCREEN OFF: the wake press is swallowed) */
                kb_note[k] = KB_SILENT;
                continue;
            }
            if (lay) {                            /* a layer's button held (or locked): the key is the layer's */
                kb_note[k] = KB_SILENT;           /* (ui_layer.c), with FX an effect (perform.c) */
                kb_layer |= 1u << k;
                if (fx)
                    perf_press(perf_key(k), 1);
                continue;
            }
            if (song.grid == 2u)                  /* NAME (ui_name.c): every key types, none sounds */
                kb_note[k] = KB_SILENT;
            else if (song.grid)                   /* the DRUM grid: a lane key plays its lane, the rest are the UI's */
                kb_note[k] = key_black(k) && key_place(k) < NLANE ? DRUM_LANE_NOTE[key_place(k)] : KB_SILENT;
            else
                kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            if (kb_note[k] == KB_SILENT)
                continue;
            kb_fresh(k);                          /* ((2.0.3, #72) KBD HOLD: a new chord, or one joined) */
            key_on(k, &trk[kb_trk[k]]);
#ifdef FM1_INPUT_LAT
            kb_lat(k);
#endif
        } else {
            if ((kb_layer >> k) & 1u) {
                kb_layer &= ~(1u << k);
                perf_press(perf_key(k), 0);
                continue;
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            kb_up(k);                             /* (held: the SMK's pedal, (2.0.3, #72) KBD HOLD) */
        }
    }
    kb_prev = cur;
}

/* -------------------------------------------------------- sequencer --- */
#if BOARD_SMK37
/* (SMK-37 Pro, SMK37_UX.md question 12) MIDI clock out of the TRS jack, from the internal clock: Start at PLAY, then 24
 * pulses a beat while playing (counted in samples, beat_samples: the song's tempo), Stop at STOP. With an external
 * clock (MENU > CLOCK) none: the clock is someone else's */
static uint32_t smk_clk_acc;
static void smk_clk_block(uint32_t n)
{
    uint32_t b;
    if (!song.playing || song.g[G_CLOCK])
        return;
    b = beat_samples();
    smk_clk_acc += n * 24u;
    while (b && smk_clk_acc >= b) {
        smk_uart_tx_put(0xF8u);
        smk_clk_acc -= b;
    }
}
#endif

static void seq_start(void)
{
    uint32_t i;
#if BOARD_SMK37
    if (!song.g[G_CLOCK]) {                        /* (the TRS jack: Start, the first pulse with the first block) */
        smk_uart_tx_put(0xFAu);
        smk_clk_acc = beat_samples();
    }
#endif
    motion_begin();
    take_clear();                                  /* (Skiff) TAKES: a new log from PLAY */
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->seq_cnt = 0;                            /* (Skiff) a run starts: its direction's first step */
        t->nud = 0;                                /* (Skiff) no NUDGE pending */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->rat_left = 0;
        if (t->p[P_LTRIG] && t->p[P_LSYNC])        /* (#16) LFO 2 FREE + SYNC: in time with the bars from PLAY */
            t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
    }
    song.tick = 0;
    beat_pos = 0; beat_n = 0;                      /* the ARP LED's beat from the top too */
    launch_steps = 0; launch_pos = CLK_START;      /* (Skiff) the launch points' steps from the top, as the click's */
    clk_pos = CLK_START;                           /* the metronome's first beat with step 0 */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
#if FELUCCA_TAPE
    skiff_tape_restart();
#endif
    perf_start();                                  /* perform.c: its 1/16 grid too */
}

/* (Skiff 2.0, #25) per-note ties (core.h step_ties), the sequencer's side. A NOTE step's tied note that still sounds goes
 * on (no note-on, no release); one that doesn't stays silent (a tie never starts a note); what sounds and isn't tied
 * on ends as it does before a NOTE step. Per track: the notes of seq_notes the next step ties (bit i: seq_notes[i]),
 * held into it at the GATE (seq_hold 2: no countdown, no glide); the ones that went on into this step, which end half a
 * step after its GATE as a TIE chain's last step does (seq_h2: that half step, pending). All 0 on a pattern without
 * per-note ties, so every step plays as before. Outside track_t (its size stays as the ISR's trk[] indexing wants), in
 * the pool: a new .bss global moves the others and their code with them */
static uint8_t seq_k[NTRK] __attribute__((section(".pool"))), seq_tm[NTRK] __attribute__((section(".pool")));
static uint32_t seq_h2[NTRK] __attribute__((section(".pool")));

static void seq_release(track_t *t)
{
    uint32_t i, k = trk_index(t);
    for (i = 0; i < t->seq_n; i++)
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
    seq_k[k] = seq_tm[k] = 0;
    seq_h2[k] = 0;
}
/* (#25) seq_notes not in keep (bit i) end; the rest stay, in order, their bits along */
static __attribute__((noinline)) void seq_keep_only(track_t *t, uint32_t keep)
{
    uint32_t k = trk_index(t), i, n = 0, km = 0, tm = 0;
    for (i = 0; i < t->seq_n; i++) {
        if (!((keep >> i) & 1u)) {
            trk_note_off(t, t->seq_notes[i]);
            continue;
        }
        km |= ((seq_k[k] >> i) & 1u) << n;
        tm |= ((seq_tm[k] >> i) & 1u) << n;
        t->seq_notes[n++] = t->seq_notes[i];
    }
    t->seq_n = (uint8_t)n;
    seq_k[k] = (uint8_t)km;
    seq_tm[k] = (uint8_t)tm;
}
/* (#25) the GATE of a step with notes going on (seq_tick): what the next step doesn't tie ends, the notes that went on
 * into this one half a step later; the tied ones stay, held for the next step */
static __attribute__((noinline)) void seq_gate_end(track_t *t)
{
    uint32_t k = trk_index(t);
    seq_keep_only(t, seq_k[k] | (seq_h2[k] ? seq_tm[k] : 0u));
    if (seq_h2[k] && (seq_tm[k] & ~seq_k[k])) {
        t->seq_off = seq_h2[k];
        seq_h2[k] = 0;
        return;
    }
    seq_h2[k] = 0;
    t->seq_hold = t->seq_n ? 2u : 0u;               /* (seq_tm kept: a RATCH part's repeat knows its tied notes) */
}
static const step_t *launch_next(const track_t *t, uint32_t len, const step_t *own);   /* ((2.0, #44) launch.c) */
/* (#25) the notes of seq_notes the next step s ties (as it will play them: QNT SEQ): bit i, seq_notes[i] goes on into
 * it */
static uint32_t seq_ties_of(track_t *t, const step_t *s, uint32_t qseq, int32_t qtrn)
{
    uint32_t ties = s->time == ST_TIE ? 0u : step_ties(s), i, j, x, m = 0;
    for (i = 0; ties && i < s->n && i < 4u; i++) {
        if (!((ties >> i) & 1u))
            continue;
        x = qseq ? (uint32_t)clamp(scale_snap(t, (int32_t)s->note[i] + qtrn), 0, 127) : s->note[i];
        for (j = 0; j < t->seq_n; j++)
            if (t->seq_notes[j] == x)
                m |= 1u << j;
    }
    return m;
}

static void seq_stop(void)
{
    uint32_t i;
#if BOARD_SMK37
    if (song.playing && !song.g[G_CLOCK])
        smk_uart_tx_put(0xFCu);                    /* (the TRS jack: Stop) */
#endif
    song.playing = 0;
    thr_out = 0;                                   /* (Skiff) the throws back at once (throw.c) */
    cin_left = 0;                                  /* (a count-in: cancelled, nothing recorded) */
    cin_flush = 0;
    memset(cin_n, 0, sizeof cin_n);
    if (song.g[G_CLOCK])                           /* external clock: the arp stops with the transport, */
        for (i = 0; i < NPART; i++)                /* its sounding note too (HOLD keeps the latched chord) */
            if (trk[i].arp_note)
                arp_off(&trk[i]);
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        trk[i].rat_left = 0;
        trk[i].nud = 0;                            /* (Skiff) no NUDGE pending */
    }
    motion_end();
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers its notes, then its lane hits (their GM
 * notes, DRUM_LANE_NOTE; an accented hit at 127); a SLIDE on the previous step makes this one legato with a
 * glide (acid style). skip: bit k = note k, bit 8 + l = the hit of lane l already sounds from live
 * recording (not triggered, not released here); SEQ_REP: a RATCH repeat (seq_ratchet).
 * RATCH x2..x4 (core.h step_ratchet): a NOTE step plays all of it again in equal parts of its swung length, each
 * part retriggered with GATE of the part; the chance is rolled once for the whole step (a failed roll: no part),
 * a slide into it glides into the first part only, and it never slides or ties out (its last part ends at its
 * gate). x1, what every older pattern holds, is the step exactly as before.
 * Skiff: FLAM (core.h SF_FLAM, x1): the step's first hit is a grace at 9/16 of the velocity, ended at the full hit,
 * which seq_ratchet plays FLAM_SAMPLES (15 ms; at most half the step) later; the chance is rolled once for both */
#define SEQ_REP (1u << 16)
#define SEQ_ROLLED (1u << 17)   /* seq_tick rolled the chance already (before the step's parameter locks): it passed */
#define SEQ_MISS (1u << 18)     /* .. it failed: nothing plays */
/* (Skiff) FLAM: where the full hit comes after the grace: FLAM_SAMPLES, at most half the step */
static uint32_t flam_at(const track_t *t, uint32_t period)
{
    uint32_t h = step_samples(t, period, t->fire_idx) / 2u;
    return h < FLAM_SAMPLES ? h : FLAM_SAMPLES;
}
static __attribute__((noinline)) void seq_step(track_t *t, const step_t *s, uint32_t period, uint32_t skip)
{
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold == 1u && t->seq_n && !(skip & SEQ_REP);   /* ((2.0) 2: held for ties, no slide) */
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u, f = t->fire_idx, k = trk_index(t);
    /* (Skiff) the step after it in its direction (played early by its NUDGE, it is the one after the playing one) */
    uint32_t nx = f == t->seq_idx ? seq_peek(t, len) : seq_after((uint32_t)t->p[P_DIR], f % len, f < t->seq_idx, len);
    /* ((2.0, #44) a LOOP switch at its end, a chain's next part: that pattern's first, so a note goes on across it) */
    const step_t *ns = f == t->seq_idx ? launch_next(t, len, &t->step[nx]) : &t->step[nx];
    uint32_t next_tie = ns->time == ST_TIE;
    uint32_t hits = step_ratchet(s), grace = (s->flags & SF_FLAM) && !(skip & SEQ_REP);   /* (Skiff) FLAM */
    uint8_t nn[4 + NLANE], vv[4 + NLANE];           /* the notes it plays: its notes, then its hits */
    uint32_t m = 0, sk = 0;
    /* (2.0, #25) its per-note ties (tn: of nn); held: notes sound on into it (a hold, or notes kept for its ties: an
     * early step fires before the GATE that keeps them); miss: it doesn't play, but its tied notes go on */
    uint32_t ties = s->time == ST_TIE ? 0u : step_ties(s), tn = 0, miss = 0;
    uint32_t held = (t->seq_hold || seq_k[k]) && t->seq_n;
    /* QNT SEQ: the step's notes (not the lane hits) snap to the scale as they play; never on a drum kit, the slices
     * or another engine that maps the keys itself. (1.3, from Felucca 1.5.6) SEQ+T: moved by TRN first, as the keys
     * are. seq_notes keeps the notes that sound: their note-offs match */
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t qseq = t->p[P_QUANT] >= QN_SEQ && !(e->keys && e->keys(t, 0) >= 0);
    int32_t qtrn = qseq && t->p[P_QUANT] == QN_SEQT ? t->p[P_TRANS] : 0;
    if (!(skip & SEQ_REP)) {
        t->rat_left = 0;
        if ((skip & SEQ_MISS) ||
            (!(skip & SEQ_ROLLED) && step_chance(s) < 100u && rng() % 100u >= step_chance(s))) {
            if (!ties || !held) {
                seq_release(t);
                return;
            }
            miss = 1;                               /* ((#25) its tied notes still go on) */
        }
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
            seq_tm[k] = 0;                          /* ((#25) its notes end with it, unless the next step ties them */
            seq_h2[k] = 0;                          /* (not in RAND: the next one isn't known)) */
            seq_k[k] = (uint8_t)(t->seq_hold || t->p[P_DIR] == DIR_RAND ? 0u : seq_ties_of(t, ns, qseq, qtrn));
        }
        return;
    }
    if (s->time == ST_REST || (!s->n && !s->hit)) {
        if (!ties || !held) {
            seq_release(t);
            return;
        }
        miss = 1;                                   /* ((#25) a REST: its tied notes go on, nothing new) */
    }
    if (miss) {
        hits = 1u;                                  /* (no parts: nothing of its own plays) */
        grace = 0;
    }
    if (hits > 1u) {                                /* RATCH: the gate of a part, no slide or tie out */
        gate = step_samples(t, period, f) / hits * (uint32_t)t->p[P_SGATE] / 128u;
        if (!(skip & SEQ_REP))
            t->rat_left = (uint8_t)(hits - 1u);
    } else if (grace) {                           /* (Skiff) FLAM: this hit the grace, short; seq_ratchet the full one */
        gate = flam_at(t, period);
        t->rat_left = 1;
    }
    for (i = 0; i < s->n && i < 4u; i++) {
        uint32_t x = s->note[i];
        if (qseq) {                                 /* QNT SEQ: onto the scale now; two notes snapping */
            x = (uint32_t)clamp(scale_snap(t, (int32_t)x + qtrn), 0, 127);   /* together play once */
            for (j = 0; j < m && nn[j] != x; j++)
                ;
            if (j < m) {
                tn |= ((ties >> i) & 1u) << j;      /* ((#25) either one tied: it goes on) */
                continue;
            }
        }
        nn[m] = (uint8_t)x;
        vv[m] = (uint8_t)vel;
        sk |= ((skip >> i) & 1u) << m;
        tn |= ((ties >> i) & 1u) << m;
        m++;
    }
    for (i = 0; i < NLANE; i++)
        if ((s->hit >> i) & 1u) {
            nn[m] = DRUM_LANE_NOTE[i];
            vv[m] = (uint8_t)((s->acc >> i) & 1u ? 127u : vel);
            sk |= ((skip >> (8u + i)) & 1u) << m;
            m++;
        }
    if (grace)                                      /* (Skiff) the grace: 9/16 of the velocity */
        for (i = 0; i < m; i++)
            vv[i] = (uint8_t)(vv[i] * 9u / 16u + 1u);
    /* what sounds into it (a slide, (2.0) or notes going on: tn) carries over: its tied notes still sounding go on
     * (no note-on), a tied one not sounding stays silent, the rest of what sounded ends; else everything ends first.
     * Its own notes start (none on a miss). A RATCH part ends the part before's own notes first, as it retriggers.
     * Without per-note ties (tn, seq_tm, seq_k 0) this is the step exactly as before 2.0 */
    if (skip & SEQ_REP)
        seq_keep_only(t, seq_tm[k]);
    held = (t->seq_hold || seq_k[k] || (skip & SEQ_REP)) && t->seq_n;
    {
        uint32_t on = 0, xs = 0, tm = 0, keep;
        for (i = 0; i < m; i++) {
            if (!((tn >> i) & 1u)) {
                xs |= miss << i;
                continue;
            }
            for (j = 0; held && j < t->seq_n && t->seq_notes[j] != nn[i]; j++)
                ;
            if (held && j < t->seq_n)
                on |= 1u << i;
            else
                xs |= 1u << i;
        }
        t->slide_glide = (uint8_t)slide_in;
        if (!held)
            seq_release(t);
        for (i = 0; i < m; i++)
            if (!(((sk | on | xs) >> i) & 1u))
                trk_note_on(t, nn[i], vv[i]);
        for (i = 0; i < t->seq_n; i++) {            /* release what is not held over (nor plays again) */
            for (j = 0; j < m && (nn[j] != t->seq_notes[i] || ((xs >> j) & 1u)); j++)
                ;
            if (j == m)
                trk_note_off(t, t->seq_notes[i]);
        }
        t->seq_n = 0;
        for (i = 0; i < m; i++)
            if (!(((sk | xs) >> i) & 1u)) {
                tm |= ((on >> i) & 1u) << t->seq_n;
                t->seq_notes[t->seq_n++] = nn[i];
            }
        t->seq_off = gate;
        /* next step a TIE: keep the notes to it (a ratchet or, Skiff, a flam never slides or ties out); (2.0) a NOTE
         * step tying some of them: those, at the GATE (seq_gate_end; not in RAND: the next one isn't known) */
        t->seq_hold = hits == 1u && !(s->flags & SF_FLAM) && ((s->flags & SF_SLIDE) != 0 || next_tie);
        keep = t->seq_hold || t->p[P_DIR] == DIR_RAND ? 0u : seq_ties_of(t, ns, qseq, qtrn);
        if (hits != 1u || (s->flags & SF_FLAM))
            keep &= tm;
        if (t->rat_left)
            keep |= tm;                             /* (its tied notes go on over its parts) */
        seq_k[k] = (uint8_t)keep;
        seq_tm[k] = (uint8_t)tm;
        seq_h2[k] = tm ? period / 2u : 0u;          /* (the tied ones end half a step after the GATE) */
    }
}

/* (Skiff 1.3, from Felucca 1.2, Discussion #169) MENU > CONTROL > STEP PREVIEW: the UI asks for a step of a track to sound
 * once (aud_req: step << 8 | track + 1; ui_input.c cursor_audition); with the transport stopped it plays here as the
 * sequencer would play it, through the track's sound: its notes or hits, accent, QNT SEQ (not its chance, ratchet repeats
 * or locks), for a 1/16 at the track's GATE (seq_tick lets it go, stopped too). Nothing is recorded */
static volatile uint16_t aud_req;
static __attribute__((noinline)) void seq_audition(void)
{
    uint32_t r = aud_req, i = (r & 0xFFu) - 1u, idx = r >> 8;
    track_t *t;
    uint16_t at, fi;
    aud_req = 0;
    if (song.playing || cin_left || i >= NTRK || idx >= NSTEP)
        return;
    t = &trk[i];
    at = t->seq_idx;
    fi = t->fire_idx;
    t->seq_idx = t->fire_idx = (uint16_t)idx;
    seq_step(t, &t->step[idx], div_samples(2), SEQ_ROLLED);
    t->seq_idx = at;
    t->fire_idx = fi;
    t->seq_hold = 0;
    t->rat_left = 0;
    seq_k[i] = seq_tm[i] = 0;                       /* ((2.0) nothing held for a next step: it ends at its GATE) */
    seq_h2[i] = 0;
}


/* RATCH: the next part of the step played last once the sequencer is that far into it (seq_step played the first;
 * (Skiff) rat_pos: from where it played, its NUDGE in). The step is read again: an edit to fewer parts (or another
 * source in a song) ends the repeats */
static __attribute__((noinline)) void seq_ratchet(track_t *t, uint32_t period)
{
    const step_t *s = &t->step[t->fire_idx];
    uint32_t hits = step_ratchet(s);
    if (s->flags & SF_FLAM) {                     /* (Skiff) FLAM: the full hit once the grace has had its time */
        if (t->rat_pos < flam_at(t, period))
            return;
        t->rat_left = 0;
        seq_step(t, s, period, SEQ_REP);
        return;
    }
    if (t->rat_left >= hits) {
        t->rat_left = 0;
        return;
    }
    if (t->rat_pos < (hits - t->rat_left) * (step_samples(t, period, t->fire_idx) / hits))
        return;
    t->rat_left--;
    seq_step(t, s, period, SEQ_REP);
}

/* (Skiff) step idx plays now: its trig condition and chance (ahead: it is played early, its pass not begun), its
 * automation and locks, then its notes (seq_step) */
static __attribute__((noinline)) void seq_fire(track_t *t, uint32_t idx, uint32_t period, uint32_t ahead)
{
    const step_t *s = &t->step[idx];
    uint32_t skip = SEQ_ROLLED, i, k;
    t->fire_idx = (uint8_t)idx;
    /* the chance first (the one roll, as seq_step made it): a step that does not play applies no lock; the
     * automation and the locks before the notes, so a note-on reads them (eng_drum's KIT, ..) */
    if (s->cond && !step_cond_ok(t, s, ahead))      /* (Skiff) its trig condition first: not this pass, no roll */
        skip = SEQ_MISS;
    else if (step_chance(s) < 100u && rng() % 100u >= step_chance(s))
        skip = SEQ_MISS;
    motion_step(t, idx, &motion, skip == SEQ_ROLLED);
    if (t->rskip_n && t->rskip_idx == idx) {
        for (k = 0; k < t->rskip_n; k++) {
            for (i = 0; i < s->n; i++)
                if (s->note[i] == t->rskip[k])
                    skip |= 1u << i;
            for (i = 0; i < NLANE; i++)
                if (((s->hit >> i) & 1u) && DRUM_LANE_NOTE[i] == t->rskip[k])
                    skip |= 1u << (8u + i);
        }
        t->rskip_n = 0;
    }
    seq_step(t, s, period, skip);
}

/* (Skiff) NUDGE: a step plays its nudge (64ths of its own length) from its start, later or earlier. On entering a
 * step: it plays now (on the grid), later (NUD_LATE at late_at) or has already (NUD_DONE: early, in the step before).
 * Then the next step, if it comes early, is set to play in this one (NUD_EARLY at early_at): not in RAND (the next is
 * not known) nor where a staged pattern may come in (launch.c launch_ahead: the next would be the outgoing one's). A
 * NOTE step's nudge only: a TIE or a REST keeps to the grid */
static int launch_ahead(const track_t *t, uint32_t len);
static int32_t nudge_samples(int32_t nudge, uint32_t len) { return nudge * (int32_t)(len / 64u); }
static int32_t step_nudge(const step_t *s) { return s->time == ST_NOTE ? s->nudge : 0; }
/* (1.3, from Felucca 1.5.6's #213) the matrix's TIME on step idx: how late it plays (samples, 0: on time). A NOTE step
 * that plays something, x1; its VEL and first note are the sources' VEL and KEY (mod.c mod_time) */
static __attribute__((noinline)) uint32_t seq_time(track_t *t, uint32_t idx, uint32_t cur)
{
    const step_t *s = &t->step[idx];
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    if (s->time != ST_NOTE || (!s->n && !s->hit) || step_ratchet(s) != 1u)
        return 0;
    return mod_time(t, s->n ? s->note[0] : DRUM_LANE_NOTE[__builtin_ctz(s->hit) & (NLANE - 1u)], vel, cur);
}
static __attribute__((noinline)) void seq_enter(track_t *t, uint32_t period, uint32_t len)
{
    uint32_t i = t->seq_idx, cur = step_samples(t, period, i), h;
    int32_t d = step_nudge(&t->step[i]);
    if ((t->nud & NUD_DONE) && t->early_idx == i) {
        /* played early */
    } else if ((h = (d > 0 ? (uint32_t)nudge_samples(d, cur) : 0u) + seq_time(t, i, cur)) != 0u) {   /* ((1.3) and
                                                         * the matrix's TIME: later still; past the step, at its end) */
        t->late_at = h;
        t->nud |= NUD_LATE;
    } else {
        t->rat_pos = t->seq_pos;
        seq_fire(t, i, period, 0u);
    }
    t->nud &= (uint8_t)~(NUD_EARLY | NUD_DONE);
    if (t->p[P_DIR] != DIR_RAND && !launch_ahead(t, len)) {
        uint32_t nx = seq_peek(t, len);
        d = step_nudge(&t->step[nx]);
        if (d < 0) {
            uint32_t e = (uint32_t)-nudge_samples(d, step_samples(t, period, nx));
            t->early_idx = (uint8_t)nx;
            t->early_at = cur > e ? cur - e : 0u;
            t->nud |= NUD_EARLY;
        }
    }
}
/* (Skiff) NUDGE: a step set to play later (the playing one) or early (the next) once the sequencer is that far */
static __attribute__((noinline)) void seq_nudge(track_t *t, uint32_t period, uint32_t len)
{
    if ((t->nud & NUD_LATE) && t->seq_pos >= t->late_at) {
        t->nud &= (uint8_t)~NUD_LATE;
        t->rat_pos = t->seq_pos - t->late_at;
        seq_fire(t, t->seq_idx, period, 0u);
    }
    if ((t->nud & NUD_EARLY) && t->seq_pos >= t->early_at) {
        t->nud = (uint8_t)((t->nud & ~NUD_EARLY) | NUD_DONE);
        t->rat_pos = t->seq_pos - t->early_at;
        seq_fire(t, t->early_idx, period, (uint32_t)seq_pass_last(t, len));
    }
}

#include "launch.c"                                 /* Skiff: live pattern switching (the bank's, at launch points) */

static void seq_tick(track_t *t, uint32_t n)
{
    uint32_t period, len;
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off > n)
            t->seq_off -= n;
        else if (seq_k[trk_index(t)] | seq_tm[trk_index(t)])
            seq_gate_end(t);                        /* ((2.0, #25) notes going on: those stay) */
        else
            seq_release(t);
    }
    if (!song.playing)
        return;
    period = div_samples((uint32_t)t->p[P_SDIV]);
    len = (uint32_t)t->p[P_SLEN];
    t->seq_pos += n;
    t->rat_pos += n;
    for (;;) {
        uint32_t cur_len = step_samples(t, period, t->seq_idx), ln = len ? len : 1u;
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + n)
            break;
        if (t->nud & NUD_LATE) {                    /* (Skiff) NUDGE: later than its step lasts now (an edit): now */
            t->nud &= (uint8_t)~NUD_LATE;
            t->rat_pos = 0;
            seq_fire(t, t->seq_idx, period, 0u);
        }
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? 0u : t->seq_pos - cur_len;
        if ((launch_q[trk_index(t)].armed | mute_arm[trk_index(t)]) && launch_step(t, ln, period)) {   /* (Skiff) a
                                                     * pattern from the bank came in (a mute: launch_step turns it) */
            period = div_samples((uint32_t)t->p[P_SDIV]);
            len = (uint32_t)t->p[P_SLEN];
            ln = len ? len : 1u;
        }
        t->seq_idx = (uint16_t)seq_next(t, ln);    /* (Skiff) the next step in its direction (seq_next) */
        if (t->p[P_DIR] == DIR_FWD ? !t->seq_idx : !t->seq_cnt || t->seq_cnt >= ln) {
            t->seq_cnt = 0;
            take_pass[trk_index(t)]++;              /* a new pass of its loop: TAKES (take.c), the conditions */
        }
        t->seq_cnt++;
        rec_hold(t, t->seq_idx, ln);
        seq_enter(t, period, ln);                   /* (Skiff) it plays: now, later or already (NUDGE) */
    }
    if (t->nud)
        seq_nudge(t, period, len ? len : 1u);
    if (t->rat_left)
        seq_ratchet(t, period);
}

/* MIDI in: the track a channel plays (0..15): G_ROUTE CH1-6 (0) the first track set to channel ch ((1.3) T1..T6
 * CHANNEL: channels 1..6 tracks 1..6 unless set; a channel no track is set to never gets here: midi_event drops it),
 * SEL (1) every channel the selected track */
static uint32_t midi_route;                /* G_ROUTE and the tracks' channels events_block last saw (a change: midi_route_drop) */
static int32_t midi_slot(uint32_t ch)      /* the track set to channel ch, -1 none */
{
    uint32_t i;
    for (i = 0; i < NPART; i++)
        if (trk_midi_ch(i) == ch)
            return (int32_t)i;
    return -1;
}
static track_t *midi_track(uint32_t ch)
{
    int32_t k = song.g[G_ROUTE] ? -1 : midi_slot(ch);
    return k >= 0 ? &trk[k] : TSEL;
}
static uint32_t midi_route_key(void)
{
    uint32_t i, k = song.g[G_ROUTE] ? 1u << 31 : 0u;
    for (i = 0; i < NPART; i++)
        k |= trk_midi_ch(i) << (4u * i);
    return k;
}

#include "midi_control.c"
#include "midi_clock.c"

/* the count-in: its first beat now (accented) */
static __attribute__((noinline)) void cin_begin(void)
{
    cin_total = cin_left = (uint8_t)(4u * cin_bars);
    cin_pos = 0;
    memset(cin_n, 0, sizeof cin_n);
    click_req = 2;
}
/* n samples on: a beat ends; the last one starts the sequencer (in this block, before seq_tick: step 0 with it) */
static __attribute__((noinline)) void cin_block(uint32_t n)
{
    uint32_t b = click_beat_len();
    cin_pos += n;
    if (cin_pos < b)
        return;
    cin_pos -= b;
    cin_pos = cin_pos < b ? cin_pos : 0u;
    if (--cin_left) {
        click_req = (uint8_t)((cin_total - cin_left) % 4u ? 1u : 2u);
        return;
    }
    seq_start();
    cin_flush = 1;
}
/* the count-in's kept notes onto step 1, which has just started (events_block, after seq_tick): as if played now */
static __attribute__((noinline)) void cin_record(void)
{
    uint32_t i, k;
    cin_flush = 0;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        for (k = 0; k < cin_n[i]; k++)
            if (rec_on(t)) {
                rec_note(t, cin_note[i][k], cin_vel[i][k]);
                if ((cin_up[i] >> k) & 1u)
                    rec_release(t, cin_note[i][k]);
            }
        cin_n[i] = 0;
    }
}
/* the metronome's beat: adv samples on while the transport runs (click.c sounds it in this block) */
static void click_tick(uint32_t adv)
{
    uint32_t p = div_samples(2), beat = 0;
    if (clk_pos == CLK_START) {
        clk_pos = 0;
        clk_step = 0;
        beat = 2;
    } else {
        clk_pos += adv;
        while (clk_pos >= p) {                      /* (as seq_tick: the remainder kept) */
            clk_pos -= p;
            clk_step = (clk_step + 1u) & 15u;
            if (!(clk_step & 3u))
                beat = clk_step ? 1u : 2u;
        }
    }
    if (beat && click_wanted())
        click_req = (uint8_t)beat;
}
/* the beat of the bar the click is on (0..3: 0 the accented first), -1 stopped: the count-in's, else the playing
 * transport's (clk_step, internal or external clock). It moves in the block a beat clicks in (the header's metronome,
 * ui_draw.c, swings on it; CLICK OFF it moves all the same) */
static int32_t click_beat_now(void)
{
    if (cin_left)
        return (int32_t)((uint32_t)(cin_total - cin_left) % 4u);
    if (!song.playing)
        return -1;
    return clk_pos == CLK_START ? 0 : (int32_t)(clk_step >> 2);
}

/* everything that happens between two rendered blocks */
static void events_block(uint32_t n)
{
    uint32_t i, pr, seq_n = n;
    uint32_t clock_mode = (uint32_t)song.g[G_CLOCK];
    if (midi_clock.mode != clock_mode) {
        seq_stop();
        memset(&midi_clock, 0, sizeof midi_clock);
        midi_clock.mode = (uint8_t)clock_mode;
        midi_beat_samples = 0;
    }
    if (cin_left && !clock_mode)
        cin_block(n);
    if (transport_req == 1u) {
        if (clock_mode)
            midi_clock_transport(0xFAu, fm1_ms);
        else if (cin_left)
            ;                                       /* counting in already (the UI's PLAY stops it: seq_counting) */
        else if (cin_bars && song.rec && !song.playing)
            cin_begin();                            /* recording from stop: the count-in first */
        else
            seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    } else if (transport_req == 3u) {                 /* GLO + PLAY: from the top without stopping (INT clock) */
        if (!clock_mode) {
            seq_stop();
            seq_start();
        }
        transport_req = 0;
    }
    if (midi_route != midi_route_key()) {              /* MIDI IN or a track's channel changed: CH1-6 lets go of the
                                                         * channels no track is set to */
        midi_route = midi_route_key();
        if (!song.g[G_ROUTE])
            midi_route_drop();
    }
    pr = panic_req;
    panic_req = 0;
    if (midi_in_overflow) {                            /* a lost note-off must never leave a held note */
        mi_r = mi_w;
        memset(midi_sel_on, 0, sizeof midi_sel_on);
        memset(midi_ch, 0, sizeof midi_ch);
        memset(midi_owners, 0, sizeof midi_owners);
        memset(mchord, 0, sizeof mchord);
        midi_hint = 0;
        for (i = 0; i < NTRK; i++) {
            trk[i].rh_n = trk[i].rskip_n = 0;
            trk[i].seq_n = trk[i].seq_hold = trk[i].slide_glide = trk[i].rat_left = 0;
        }
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            midi_forget_track(i);
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            arp_forget(t);
        }
        engine_block(t);                              /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            if (t->arp_note)
                arp_silence(t);
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    kb_ped_block(pr);                                 /* (the keys held: the hold's end, another track, a panic) */
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t st = status & 0xF0u, ch = status & 0x0Fu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        uint32_t source = midi_in_source[at] ? midi_in_source[at] : 1u, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (clock_mode == source) {
                if (status == 0xF8u)
                    midi_clock_pulse(ms);
                else
                    midi_clock_transport(status, ms);
            }
        } else
            midi_event(st, ch, d1, d2);
    }
    if (clock_mode) {
        seq_n = 0;
        if (song.playing) {
            uint32_t last = midi_clock.have_pulse ? midi_clock.last_ms : midi_clock.start_ms;
            if (fm1_ms - last > 500u) {
                seq_stop();                         /* cable loss must not leave a running held note */
                midi_clock.tempo_valid = 0;
            } else
                seq_n = midi_clock_advance(fm1_ms);
        }
    }
    if (song.playing)
        launch_clock(seq_n);                          /* (Skiff) the steps a BAR / BEAT launch waits for */
    if (aud_req)
        seq_audition();                               /* ((1.3) STEP PREVIEW: a step sounds, stopped) */
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], seq_n);
    if (cin_flush)
        cin_record();
    if (song.playing)
        click_tick(seq_n);
#if BOARD_SMK37
    smk_clk_block(n);
#endif
    for (i = 0; i < NPART; i++)
        arp_step(&trk[i], clock_mode ? seq_n : n, clock_mode && song.playing ? seq_n : n);
    beat_pos += clock_mode ? seq_n : n;               /* the beat the ARP LED flashes on (ui_leds) */
    if (beat_pos >= beat_samples()) {
        beat_pos -= beat_samples();
        beat_pos = beat_pos < beat_samples() ? beat_pos : 0u;
        beat_n = (beat_n + 1u) & 3u;
    }
    if (song.playing)
        song.tick++;
}
