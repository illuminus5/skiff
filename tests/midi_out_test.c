/* SPDX-License-Identifier: GPL-3.0-only */
/* (2.2) MIDI OUT (seq.c mo_note, usb.c mo_put / mo_take): what the tracks play goes out on their channels (NOTES OUT
 * ALL), the keys as pressed (KEYS) or nothing (OFF); every note-on gets one note-off, on the channel it went on, through
 * chords, RATCH, FLAM, slides, ties, NUDGE, the ARP, chord follow, kits, mutes, a channel changed, panics, STOP and a
 * full ring; what MIDI IN plays is never sent back; each event is due when its block plays. The real sources (the
 * ui_test.c prelude), the audio ISR's events_block block by block, the ring read as a host would (drain) */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

typedef struct { uint8_t st, d1, d2; uint32_t due; } mev_t;
#define LOGN 60000u
static mev_t lg[LOGN];
static uint32_t lgn, lg_hold;                     /* events read; lg_hold: the host stops reading (ring fills) */

static void mo_drain(void)                       /* what a host reads: everything queued */
{
    if (lg_hold)
        return;
    while (mo_r != mo_w) {
        uint32_t at = mo_r % MQO, p = midi_out_q[at];
        if (lgn < LOGN)
            lg[lgn++] = (mev_t){(uint8_t)(p >> 8), (uint8_t)((p >> 16) & 127u), (uint8_t)((p >> 24) & 127u), mo.due[at]};
        mo_r++;
    }
}
static void blk(uint32_t n)
{
    while (n--) {
        events_block(CTL);
        mo_drain();
    }
}
static void out_reset(void)                       /* (CLOCK OUT off: the notes' checks read notes; test_clock its own) */
{
    ui_power_on();
    memset(&mn, 0, sizeof mn);
    memset(&mo, 0, sizeof mo);
    mo_w = mo_r = 0;
    usb.config = 1;
    lgn = lg_hold = 0;
    fm1_in.notes = kb_prev = 0;
    MIDI_BYTE = MO_CLOCK_OFF;
    events_block(CTL);
    mo_drain();
    lgn = 0;
}
static int gate_note(const track_t *t, uint32_t note)
{
    for (uint32_t i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static void play(void) { transport_req = 1; blk(1); }
static void stop(void) { transport_req = 2; blk(1); }
#define BAR_BLOCKS (4u * 22050u / CTL)            /* a bar at 120 BPM in 32-sample blocks */

/* the log from event `from`: every note-on followed by one note-off on its channel, no note-on twice, no note-off of a
 * note not on; *ons the note-ons, *open those still on at the end, *chans a bit per channel used */
static int paired(uint32_t from, uint32_t *ons, uint32_t *open, uint32_t *chans)
{
    static uint8_t on[16][128];
    uint32_t i, c, n, bad = 0;
    memset(on, 0, sizeof on);
    *ons = *open = *chans = 0;
    for (i = from; i < lgn; i++) {
        uint32_t s = lg[i].st & 0xF0u;
        c = lg[i].st & 15u;
        n = lg[i].d1;
        if (s == 0x90u && lg[i].d2) {
            bad += on[c][n];
            on[c][n] = 1;
            ++*ons;
            *chans |= 1u << c;
        } else if (s == 0x80u || s == 0x90u) {
            bad += !on[c][n];
            on[c][n] = 0;
        }
    }
    for (c = 0; c < 16u; c++)
        for (n = 0; n < 128u; n++)
            *open += on[c][n];
    if (bad)
        printf("  (paired: %u bad)\n", bad);
    return !bad;
}
static uint32_t count_st(uint32_t from, uint32_t st, uint32_t mask)   /* events whose status & mask == st */
{
    uint32_t i, n = 0;
    for (i = from; i < lgn; i++)
        n += (lg[i].st & mask) == st;
    return n;
}

/* the six tracks busy: T1 3-note chords (POLY), T2 RATCH x3 and FLAM, T3 the ARP over a held key, T4 kit hits, T5 a
 * slide and a TIE, T6 following T1 with NUDGE late and early */
static void busy_pattern(void)
{
    static const uint8_t CH3[3] = {60, 64, 67}, N48[1] = {48}, N55[1] = {55}, N57[1] = {57}, N62[1] = {62};
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        memset(trk[i].step, 0, sizeof trk[i].step);
        trk[i].p[P_SLEN] = 16;
        trk[i].p[P_VOICE] = trk[i].p[P_VOICE] == V_POLY || i == 0 ? V_POLY : trk[i].p[P_VOICE];
    }
    for (i = 0; i < 16u; i++) {
        put_step(&trk[0], i, i % 4u ? 0u : 3u, CH3, i % 4u ? ST_REST : ST_NOTE, 0);
        put_step(&trk[1], i, i % 2u ? 0u : 1u, N48, i % 2u ? ST_REST : ST_NOTE, i % 4u == 2u ? SF_FLAM : 0u);
        if (!(i % 4u))
            step_set_ratchet(&trk[1].step[i], 3);
        trk[3].step[i].time = ST_NOTE;
        trk[3].step[i].hit = (uint8_t)(1u << (i % 3u));
        put_step(&trk[5], i, i % 2u ? 0u : 1u, N62, i % 2u ? ST_REST : ST_NOTE, 0);
        trk[5].step[i].nudge = (int8_t)(i % 4u == 0u ? 8 : i % 4u == 2u ? -6 : 0);
    }
    put_step(&trk[4], 0, 1, N55, ST_NOTE, SF_SLIDE);
    put_step(&trk[4], 1, 1, N57, ST_NOTE, 0);
    put_step(&trk[4], 2, 1, N57, ST_TIE, 0);
    put_step(&trk[4], 8, 1, N55, ST_NOTE, 0);
    trk[2].p[P_AMODE] = 1;                        /* T3: the ARP */
    trk[5].p[P_FOLLOW] = 1;                       /* T6 follows T1's chords */
}

static int test_pairing(void)
{
    int bad = 0;
    uint32_t ons, open, chans, i;
    out_reset();
    busy_pattern();
    sel_set(2);
    key_down(7);                                  /* T3's ARP over C4 */
    play();
    blk(4u * BAR_BLOCKS);
    stop();
    blk(50);
    key_up(7);
    blk(BAR_BLOCKS / 4u);
    bad += check("(2.2) NOTES OUT ALL: chords, RATCH, FLAM, slide, TIE, NUDGE, ARP, FOLLOW, a kit: every note-on one note-off",
                 paired(0, &ons, &open, &chans) && open == 0u && ons > 200u);
    bad += check("  each track on its own channel (1..6), the key's track: the ARP's notes, not the key", chans == 0x3Fu);
    for (i = 0; i < lgn && !((lg[i].st & 0xF0u) == 0x90u && (lg[i].st & 15u) == 2u); i++)
        ;
    bad += check("  T3's first note: the ARP's (C4, then up), the key itself not sent twice", i < lgn && lg[i].d1 == 60u);
    return bad;
}

static int test_modes(void)
{
    int bad = 0;
    uint32_t ons, open, chans;
    out_reset();
    busy_pattern();
    trk[2].p[P_AMODE] = 0;
    MIDI_BYTE = NO_KEYS | MO_CLOCK_OFF;
    sel_set(0);
    play();
    blk(BAR_BLOCKS);
    key_down(7);
    blk(10);
    key_up(7);
    blk(10);
    stop();
    blk(10);
    bad += check("NOTES OUT KEYS: the keys as pressed (T1's C4 once, on and off), nothing the sequencer plays",
                 paired(0, &ons, &open, &chans) && ons == 1u && !open && chans == 1u);
    out_reset();
    busy_pattern();
    MIDI_BYTE = NO_OFF | MO_CLOCK_OFF;
    sel_set(0);
    play();
    blk(BAR_BLOCKS);
    key_down(7);
    blk(10);
    key_up(7);
    stop();
    blk(10);
    bad += check("NOTES OUT OFF: nothing", lgn == 0u);
    out_reset();
    busy_pattern();
    trk[1].p[P_MUTE] = 1;
    play();
    blk(BAR_BLOCKS);
    trk[1].p[P_MUTE] = 0;
    blk(BAR_BLOCKS / 4u + 3u);
    trk[1].p[P_MUTE] = 1;                         /* muted while its notes may sound: they still end */
    blk(BAR_BLOCKS / 2u);
    stop();
    blk(10);
    bad += check("a muted track sends nothing; muted mid-note, its notes still end", paired(0, &ons, &open, &chans) &&
                 !open && count_st(0, 0x91u, 0xFFu) > 0u);
    return bad;
}

static int test_midi_in(void)
{
    int bad = 0;
    uint32_t ons, open, chans, before;
    track_t *t = &trk[0];
    out_reset();
    t->p[P_VOICE] = V_POLY;
    midi_enqueue(0x09u | 0x90u << 8 | 64u << 16 | 100u << 24, 1);
    blk(2);
    bad += check("MIDI IN plays T1 (CH 1) and nothing goes back out", gate_note(t, 64) && lgn == 0u);
    midi_enqueue(0x08u | 0x80u << 8 | 64u << 16, 1);
    blk(2);
    bad += check("  its note-off: nothing out either", !gate_note(t, 64) && lgn == 0u);
    t->p[P_AMODE] = 1;                            /* the ARP over a MIDI note: the ARP's notes are T1's own */
    before = lgn;
    midi_enqueue(0x09u | 0x90u << 8 | 64u << 16 | 100u << 24, 1);
    blk(BAR_BLOCKS / 2u);
    midi_enqueue(0x08u | 0x80u << 8 | 64u << 16, 1);
    blk(BAR_BLOCKS / 4u);
    bad += check("  with T1's ARP on: the ARP's notes go out (paired), the MIDI note itself not",
                 paired(before, &ons, &open, &chans) && ons > 0u && !open && chans == 1u);
    return bad;
}

static int test_channel_change(void)
{
    int bad = 0;
    uint32_t ons, open, chans, i, on_ch = 99, off_ch = 99;
    out_reset();
    trk[0].p[P_VOICE] = V_POLY;
    sel_set(0);
    key_down(7);
    blk(2);
    ui_mch(0) = 4;                                /* T1 CHANNEL: CH 5 while C4 sounds */
    blk(2);
    key_up(7);
    blk(2);
    for (i = 0; i < lgn; i++) {
        if ((lg[i].st & 0xF0u) == 0x90u && lg[i].d1 == 60u)
            on_ch = lg[i].st & 15u;
        if ((lg[i].st & 0xF0u) == 0x80u && lg[i].d1 == 60u)
            off_ch = lg[i].st & 15u;
    }
    bad += check("a track's channel changed while its key sounds: the note-off on the channel the note-on went on",
                 paired(0, &ons, &open, &chans) && !open && on_ch == 0u && off_ch == 0u);
    key_down(7);
    blk(2);
    key_up(7);
    blk(2);
    bad += check("  the next note: on the new channel (CH 5)", count_st(0, 0x94u, 0xFFu) == 1u && count_st(0, 0x84u, 0xFFu) == 1u);
    sel_set(1);                                   /* (2.2 hardware) T2 holds a key while T1's channel changes again */
    trk[1].p[P_VOICE] = V_POLY;
    i = lgn;
    key_down(9);
    blk(2);
    ui_mch(0) = 6;
    blk(2);
    on_ch = count_st(i, 0x81u, 0xFFu);
    key_up(9);
    blk(2);
    bad += check("  another track's note through it: not ended by T1's change, its note-off when let go",
                 count_st(i, 0x91u, 0xFFu) == 1u && on_ch == 0u && count_st(i, 0x81u, 0xFFu) == 1u);
    i = lgn;
    key_down(9);
    blk(2);
    MIDI_BYTE = NO_OFF | MO_CLOCK_OFF;            /* NOTES OUT changed: every track's notes end at once */
    blk(2);
    on_ch = count_st(i, 0x81u, 0xFFu);
    key_up(9);
    blk(2);
    bad += check("  NOTES OUT changed while it sounds: its note-off at once, none again when let go",
                 on_ch == 1u && count_st(i, 0x81u, 0xFFu) == 1u);
    MIDI_BYTE = MO_CLOCK_OFF;
    ui_mch(0) = 0;
    sel_set(0);
    return bad;
}

static int test_panic_and_overflow(void)
{
    int bad = 0;
    uint32_t ons, open, chans, at;
    out_reset();
    trk[0].p[P_VOICE] = V_POLY;
    sel_set(0);
    key_down(7);                                  /* C4 held on T1, then a sound load (ui.c: panic_req) */
    blk(2);
    at = lgn;
    panic_req = 1u;
    blk(1);
    bad += check("a panic (a sound loaded) while a key holds a note: its note-off out at once, not when the key lets go",
                 count_st(at, 0x80u, 0xFFu) == 1u && lgn > at && lg[at].d1 == 60u);
    key_up(7);
    blk(2);
    bad += check("  .. and none more when the key lets go", count_st(at, 0x80u, 0xFFu) == 1u);
    out_reset();
    busy_pattern();
    play();
    blk(BAR_BLOCKS / 3u);
    panic_req = 1u;                               /* a sound load on T1 (ui.c: panic_req) while its chord sounds */
    blk(BAR_BLOCKS / 2u);
    lg_hold = 1;                                  /* the host stops reading: the ring fills, notes are refused */
    blk(3u * BAR_BLOCKS);
    bad += check("a full ring: nothing written over", mo_w - mo_r <= MQO && mo.drops > 0u);
    lg_hold = 0;
    blk(BAR_BLOCKS);
    stop();
    blk(BAR_BLOCKS / 4u);
    bad += check("a panic and a full ring on the way: still every note-on one note-off, none left on",
                 paired(0, &ons, &open, &chans) && !open && ons > 50u);
    return bad;
}

static int test_due(void)
{
    int bad = 0;
    uint8_t b[64];
    out_reset();
    trk[0].p[P_VOICE] = V_POLY;
    sel_set(0);
    mo.tick = 1000000u;                           /* the block being rendered plays at tick 1,000,000 */
    fm1_in.notes |= 1u << 7;
    host_notes |= 1u << 7;
    events_block(CTL);
    bad += check("an event waits for its block to play: none at tick 999,999, the note at 1,000,000",
                 mo_take(b, 64, 999999u) == 0u && mo_take(b, 64, 1000000u) == 4u && b[1] == 0x90u && b[2] == 60u);
    fm1_in.notes = 0;
    events_block(CTL);
    mo_r = mo_w;
    return bad;
}

/* T1..T6 CHANNEL OFF and EXT (seq.c trk_moff / trk_ext): an EXT track's notes go out and it sounds none of its own; an
 * OFF track neither sends nor hears */
static int test_ext(void)
{
    int bad = 0;
    uint32_t i, v, ext_voice = 0, t1_voice = 0, ons, open, chans;
    out_reset();
    busy_pattern();
    trk[2].p[P_AMODE] = 0;
    ui_mch(1) = MCH_EXT;                          /* T2 CH 2 EXT */
    ui_mch(4) = MCH_OFF;                          /* T5 OFF */
    play();
    for (i = 0; i < BAR_BLOCKS; i++) {
        blk(1);
        for (v = 0; v < NVOICE; v++) {
            ext_voice |= trk[1].v[v].active;
            t1_voice |= trk[0].v[v].active;
        }
    }
    stop();
    blk(10);
    bad += check("(2.2) T2 EXT: its RATCH notes out on CH 2 and not one voice of its own; T1 sounds as ever",
                 paired(0, &ons, &open, &chans) && !open && count_st(0, 0x91u, 0xFFu) > 8u && !ext_voice && t1_voice);
    bad += check("  T5 OFF: nothing out", count_st(0, 0x94u, 0xFFu) == 0u && count_st(0, 0x84u, 0xFFu) == 0u);
    midi_enqueue(0x09u | 0x94u << 8 | 60u << 16 | 100u << 24, 1);
    blk(2);
    bad += check("  .. nor heard: CH 5 plays no track", !gate_note(&trk[4], 60) && midi_slot(4) < 0);
    ui_mch(1) = ui_mch(4) = 0;
    return bad;
}

/* CC OUT (midi_learn.c cc_knob, midi_control.c cc_out_of / cc_value, seq.c mo_motion): a knob's CC, never one that came
 * in; the CCs a step's locks move on an EXT track, before its notes */
static int test_cc(void)
{
    int bad = 0;
    uint8_t cc[MO_CCN], id[MO_CCN], v0[MO_CCN];
    uint32_t i, n, c, ok = 1, cut = P_COUNT, before, at_cc = 0, at_on = 0, k;
    track_t *t = &trk[0];
    static const uint8_t N60[1] = {60};
    out_reset();
    n = mo_ccs(t, cc, id, v0);
    for (i = 0; i < n; i++)
        if (cc[i] == 74u)
            cut = id[i];
    bad += check("(2.2) CC OUT's map on T1 (ANALOG): CC7 LEVEL, CC10 PAN, CC74 its cutoff", n >= 9u && cut < P_COUNT &&
                 cc_out_of(t, P_LEVEL) == 7u && cc_out_of(t, P_PAN) == 10u);
    for (i = 0; i < n; i++) {                     /* a value out and that CC back: the value again */
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), id[i]);
        int32_t r = d->max - d->min, x;
        int16_t keep = t->p[id[i]];
        if (r < 127)
            for (x = d->min; x <= d->max; x++) {
                t->p[id[i]] = (int16_t)x;
                midi_cc_set(t, id[i], cc_value(t, id[i]));
                ok &= t->p[id[i]] == x;
            }
        else
            for (c = 0; c < 128u; c++) {
                midi_cc_set(t, id[i], c);
                ok &= cc_value(t, id[i]) == c;
            }
        t->p[id[i]] = keep;
    }
    bad += check("  every mapped parameter: what goes out comes back the same (cc_value: midi_cc_set's inverse)", ok);
    t->p[cut] = 40;
    ml_knob(t, cut);
    mo_drain();
    bad += check("  CC OUT OFF (the default): a knob sends nothing", lgn == 0u);
    MIDI_BYTE |= MO_CC_ON;
    t->p[cut] = 41;
    ml_knob(t, cut);
    ml_knob(t, cut);
    mo_drain();
    bad += check("  CC OUT ON: T1's cutoff knob, CC74 once on CH 1 (the same value again: none)",
                 lgn == 1u && lg[0].st == 0xB0u && lg[0].d1 == 74u && lg[0].d2 == cc_value(t, cut));
    lgn = 0;
    midi_enqueue(0x0Bu | 0xB0u << 8 | 74u << 16 | 100u << 24, 1);
    blk(2);
    bad += check("  a CC74 coming in sets the cutoff and goes nowhere", lgn == 0u && cc_value(t, cut) == 100u);
    (void)ml_learn(21, 0, cut);
    t->p[cut] = 20;
    ml_knob(t, cut);
    mo_drain();
    bad += check("  CC21 learned to T1's cutoff: the knob sends CC21, not CC74", lgn == 1u && lg[0].d1 == 21u);
    (void)ml_clear_all();
    MIDI_BYTE = MO_CLOCK_OFF;                     /* (CC OUT OFF: an EXT track sends them all the same) */
    lgn = 0;
    ui_mch(0) = MCH_EXT;
    memset(t->step, 0, sizeof t->step);
    t->p[P_SLEN] = 4;
    for (i = 0; i < 4u; i++)
        put_step(t, i, 1, N60, ST_NOTE, 0);
    t->p[cut] = 30;
    (void)motion_set_lock(t, 1, cut, 90);
    play();
    blk(4u * BAR_BLOCKS / 16u + 4u);               /* steps 0, 1 (locked), 2 */
    stop();
    blk(4);
    for (i = 0, k = 0; i < lgn; i++) {
        if (lg[i].st == 0xB0u && lg[i].d1 == 74u && !at_cc)
            at_cc = i + 1u;
        if (lg[i].st == 0x90u && ++k == 2u)
            at_on = i + 1u;
    }
    before = count_st(0, 0xB0u, 0xFFu);
    t->p[cut] = 90;
    c = cc_value(t, cut);                         /* (the lock's value as a CC) */
    t->p[cut] = 30;
    bad += check("  T1 EXT, CC OUT OFF: step 2's lock out as CC74 before its note, its base value back after it",
                 at_cc && at_on && at_cc < at_on && before >= 2u && lg[at_cc - 1u].d2 == c);
    ui_mch(0) = 0;
    motion_clear(t);
    return bad;
}

/* REMOTE (midi_control.c midi_event): CC 102..115 on its channel hold the panel's buttons (the HAL's virt, through the
 * debounce: tests/input_test.c); AUTO CHANNEL plays the selected track */
static int test_remote_auto(void)
{
    int bad = 0;
    uint32_t home, ok;
    out_reset();
    home = 1u << panel.btn[B_HOME];
    CPU2_BYTE = (uint8_t)((CPU2_BYTE & 7u) | 16u << 3);   /* REMOTE CH 16 */
    blk(1);
    midi_enqueue(0x0Bu | 0xBFu << 8 | 108u << 16 | 127u << 24, 1);
    blk(1);
    ok = (fm1_in.virt & home) && ((fm1_in.vtap ^ fm1_in.vack) & home);
    midi_enqueue(0x0Bu | 0xBFu << 8 | 108u << 16 | 0u << 24, 1);
    blk(1);
    ok &= !(fm1_in.virt & home);
    bad += check("(2.2) REMOTE CH 16: CC108 127 holds HOME (Sloop's map: 102 + its label), 0 lets go", ok);
    midi_enqueue(0x0Bu | 0xBFu << 8 | 112u << 16 | 127u << 24, 1);
    blk(1);
    ok = (fm1_in.virt >> panel.btn[B_PLAY]) & 1u;
    {   /* ((2.5) its encoders, relative (two's complement): CC 87 SELECT, 85 ALGORITHM, 86 PRESETS, 116..119 KNOB 1..4,
         * by role (panel.c EN_*); role r turned r + 1 clicks right; then KNOB 1 one back (127), KNOB 2 64 (nothing),
         * KNOB 3 63 back (65); panel_enc takes them as the panel's */
        static const uint8_t CC[NE] = {87, 85, 86, 116, 117, 118, 119};
        static const int8_t WANT[NE] = {1, 2, 3, 4 - 1, 5, 6 - 63, 7};
        uint32_t r, enc_ok = 1;
        for (r = 0; r < NE; r++)
            (void)panel_enc(r);
        for (r = 0; r < NE; r++)
            midi_enqueue(0x0Bu | 0xBFu << 8 | (uint32_t)CC[r] << 16 | (r + 1u) << 24, 1);
        midi_enqueue(0x0Bu | 0xBFu << 8 | 116u << 16 | 127u << 24, 1);
        midi_enqueue(0x0Bu | 0xBFu << 8 | 117u << 16 | 64u << 24, 1);
        midi_enqueue(0x0Bu | 0xBFu << 8 | 118u << 16 | 65u << 24, 1);
        blk(1);
        for (r = 0; r < NE; r++)
            enc_ok &= panel_enc(r) == WANT[r];
        midi_enqueue(0x0Bu | 0xB0u << 8 | 116u << 16 | 3u << 24, 1);   /* (CH 1: not REMOTE's) */
        blk(1);
        enc_ok &= panel_enc(EN_K1) == 0;
        bad += check("(2.5) REMOTE's encoders: CC 116..119 KNOB 1..4, 85 ALGORITHM, 86 PRESETS, 87 SELECT, relative (127 one "
                     "back, 64 nothing); another channel's: none", enc_ok);
    }
    CPU2_BYTE &= 7u;                                  /* REMOTE OFF: what it held, let go */
    blk(1);
    bad += check("  CC112 holds PLAY; REMOTE turned OFF lets it go", ok && !fm1_in.virt);
    midi_enqueue(0x0Bu | 0xB0u << 8 | 108u << 16 | 127u << 24, 1);   /* CH 1: not REMOTE's */
    blk(1);
    bad += check("  CC108 on another channel: no button", !fm1_in.virt);
    fm1_in.vtap = fm1_in.vack = 0;

    AUTO_BYTE = 16;                                   /* AUTO CHANNEL CH 16 */
    sel_set(2);
    trk[2].p[P_VOICE] = trk[0].p[P_VOICE] = V_POLY;
    blk(1);
    midi_enqueue(0x09u | 0x9Fu << 8 | 64u << 16 | 100u << 24, 1);
    blk(1);
    ok = gate_note(&trk[2], 64);
    sel_set(0);
    midi_enqueue(0x09u | 0x9Fu << 8 | 67u << 16 | 100u << 24, 1);
    midi_enqueue(0x08u | 0x8Fu << 8 | 64u << 16, 1);
    blk(2);
    ok &= gate_note(&trk[0], 67) && !gate_note(&trk[2], 64);
    midi_enqueue(0x09u | 0x90u << 8 | 72u << 16 | 100u << 24, 1);   /* CH 1: T1 as ever */
    blk(1);
    ok &= gate_note(&trk[0], 72);
    bad += check("(2.2) AUTO CHANNEL 16: plays the selected track (T3, then T1); a held note ends on its own; CH 1 T1",
                 ok);
    AUTO_BYTE = 0;
    return bad;
}

/* Song Position (midi_clock.c midi_clock_spp, seq.c seq_locate): the clock from USB; Start, a few pulses, Stop, SPP 10,
 * Continue: each track's next step where 10 16ths put it (T1 1/16: its step 11; T2 1/8: 5 of LEN 4, its step 2; T3
 * 1/4: half way through its step 3, so its step 4 next) */
static void ext_pulses(uint32_t n)
{
    while (n--) {
        fm1_ms += 21u;
        midi_enqueue(0x0Fu | 0xF8u << 8, 1);
        blk(1);
    }
}
static uint32_t first_note(uint32_t from, uint32_t ch)   /* the first note-on on channel ch from event from, 0 none */
{
    uint32_t i;
    for (i = from; i < lgn; i++)
        if (lg[i].st == (0x90u | ch) && lg[i].d2)
            return lg[i].d1;
    return 0;
}
static int test_spp(void)
{
    int bad = 0;
    uint32_t i, at;
    out_reset();
    for (i = 0; i < NTRK; i++)
        memset(trk[i].step, 0, sizeof trk[i].step);
    trk[0].p[P_SDIV] = 2; trk[0].p[P_SLEN] = 16;   /* 1/16 */
    trk[1].p[P_SDIV] = 1; trk[1].p[P_SLEN] = 4;    /* 1/8 */
    trk[2].p[P_SDIV] = 0; trk[2].p[P_SLEN] = 8;    /* 1/4 */
    for (i = 0; i < 16u; i++) {
        uint8_t a[1] = {(uint8_t)(40u + i)}, b[1] = {(uint8_t)(60u + i)}, c[1] = {(uint8_t)(80u + i)};
        put_step(&trk[0], i, 1, a, ST_NOTE, 0);
        put_step(&trk[1], i, 1, b, ST_NOTE, 0);
        put_step(&trk[2], i, 1, c, ST_NOTE, 0);
    }
    song.g[G_CLOCK] = 1;                          /* CLOCK USB */
    blk(2);
    midi_enqueue(0x0Fu | 0xFAu << 8, 1);
    ext_pulses(30);
    midi_enqueue(0x0Fu | 0xFCu << 8, 1);
    blk(2);
    at = lgn;
    midi_enqueue(0x03u | 0xF2u << 8 | 10u << 16, 1);   /* SPP 10 (16ths) */
    midi_enqueue(0x0Fu | 0xFBu << 8, 1);
    blk(1);
    ext_pulses(40);
    bad += check("(2.2) SPP 10 + Continue: T1 (1/16) on with its step 11, T2 (1/8, LEN 4) its step 2, T3 (1/4) its step 4",
                 first_note(at, 0) == 50u && first_note(at, 1) == 61u && first_note(at, 2) == 83u);
    bad += check("  the click's and launch points' 1/16 there too", launch_steps >= 10u);
    midi_enqueue(0x0Fu | 0xFCu << 8, 1);
    blk(2);
    song.g[G_CLOCK] = 0;
    blk(2);
    return bad;
}

/* CLOCK OUT (seq.c mo_block_begin / mo_block_end, seq_start / seq_stop): blocks as audio.c runs them, each with the
 * tick it plays at (mo.tick) */
static uint32_t blk_tick;
static void tblk(uint32_t n)
{
    while (n--) {
        mo.tick = blk_tick;
        blk_tick += CTL * DAC_TICKS;
        events_block(CTL);
        mo_drain();
    }
}
static int test_clock(void)
{
    int bad = 0;
    uint32_t i, k, n = 0, first = 0, last = 0, f8 = 0, fa = 0, note0 = 0, worst = 0, steps = 0, ok;
    uint32_t p;
    static const uint8_t N60[1] = {60};
    out_reset();
    MIDI_BYTE = 0;                                /* CLOCK OUT ON (the default) */
    p = div_samples(2) * DAC_TICKS;               /* a 1/16 in ticks at 120 (after the reset): six pulses */
    blk_tick = 0;
    tblk(10);
    lgn = 0;
    tblk(FS / CTL);                               /* a second, stopped */
    for (i = 0; i < lgn; i++)
        if (lg[i].st == 0xF8u) {
            if (n) {
                uint32_t d = lg[i].due - last, e = d > p / 6u ? d - p / 6u : p / 6u - d;
                worst = e > worst ? e : worst;
            } else
                first = lg[i].due;
            last = lg[i].due;
            n++;
        }
    bad += check("(2.2) CLOCK OUT, stopped at 120: 48 pulses a second, each 1/6 of a 1/16 after the last (within a sample)",
                 n >= 47u && n <= 49u && worst <= DAC_TICKS && last > first);
    for (i = 0; i < 16u; i++)
        put_step(&trk[0], i, 1, N60, ST_NOTE, 0);
    trk[0].p[P_SLEN] = 16;
    lgn = 0;
    transport_req = 1;
    tblk(1);
    ok = lgn >= 3u && lg[0].st == 0xFAu && lg[1].st == 0xF8u && (lg[2].st & 0xF0u) == 0x90u && lg[0].due == lg[2].due &&
         lg[1].due == lg[2].due;
    bad += check("  PLAY: Start, a pulse, then step 0's notes, all when that block plays", ok);
    tblk(64u * BAR_BLOCKS);                       /* 64 bars: pulse 6k with step k */
    for (i = 0, k = 0; i < lgn; i++) {
        if (lg[i].st == 0xF8u) {
            if (f8 % 6u == 0u)
                k = lg[i].due;                    /* (the pulse of a step's start) */
            f8++;
        } else if (lg[i].st == 0x90u) {
            note0 += k && (lg[i].due > k ? lg[i].due - k : k - lg[i].due) > CTL * DAC_TICKS;
            steps++;
        }
    }
    bad += check("  64 bars: every sixth pulse in its step's block, 24 a beat", note0 == 0u && steps >= 1023u &&
                 f8 >= 6u * 1023u && f8 <= 6u * 1025u);
    lgn = 0;
    transport_req = 2;
    tblk(1);
    for (i = 0, fa = 99; i < lgn; i++)
        if (lg[i].st == 0xFCu)
            fa = i;
    bad += check("  STOP: the last note's note-off, then Stop", fa < lgn && fa > 0u && (lg[fa - 1u].st & 0xF0u) == 0x80u);
    song.g[G_CLOCK] = 1;                          /* an external clock: none of ours */
    tblk(2);
    lgn = 0;
    tblk(FS / CTL);
    bad += check("  CLOCK USB (someone else's clock): no pulses out", count_st(0, 0xF8u, 0xFFu) == 0u);
    song.g[G_CLOCK] = 0;
    MIDI_BYTE = MO_CLOCK_OFF;
    lgn = 0;
    tblk(FS / CTL);
    bad += check("  CLOCK OUT OFF: none", count_st(0, 0xF8u, 0xFFu) == 0u);
    (void)fa;
    return bad;
}

/* MENU > MIDI's settings with the unit's (settings_persist.c SKS1: [15][17], [15][5] bits 3..7, [1][1], the channels'
 * flags), and their defaults all 0 */
static int test_settings_kept(void)
{
    static sks_t s;
    int ok;
    out_reset();
    MIDI_BYTE = NO_KEYS | MO_CC_ON | MO_THRU_ON;
    AUTO_BYTE = 16;
    CPU2_BYTE = (uint8_t)((CPU2_BYTE & 7u) | 5u << 3);
    ui_mch(3) = MCH_EXT | 6u;                         /* T4 CH 10 EXT */
    ui_mch(5) = MCH_OFF;
    settings_export(&s);
    memset(&favorites, 0, sizeof favorites);
    ok = settings_import(&s, sizeof s) == 1 && MIDI_BYTE == (NO_KEYS | MO_CC_ON | MO_THRU_ON) && AUTO_BYTE == 16u &&
         (CPU2_BYTE >> 3) == 5u && ui_mch(3) == (MCH_EXT | 6u) && ui_mch(5) == MCH_OFF && trk_ext(3) && trk_moff(5) &&
         trk_midi_ch(3) == 9u && notes_out() == NO_KEYS;
    MIDI_BYTE = AUTO_BYTE = 0;
    CPU2_BYTE &= 7u;
    ui_mch(3) = ui_mch(5) = 0;
    ok &= notes_out() == NO_ALL && !(MSET(15, 17) & MO_CLOCK_OFF) && !mset_ch(MSET(1, 1) & 31u) &&
          !mset_ch(MSET(15, 5) >> 3) && !trk_ext(3) && !trk_moff(5);
    return check("(2.2) MENU > MIDI kept with the settings (NOTES OUT, CC OUT, THRU, AUTO, REMOTE, OFF, EXT); 0: the defaults",
                 ok);
}

static int test_fuzz(void)
{
    int bad = 0;
    uint32_t ons, open, chans, i, r = 12345u, k;
    out_reset();
    busy_pattern();
    play();
    for (i = 0; i < 20000u; i++) {
        r = r * 1103515245u + 12345u;
        k = (r >> 16) % 1000u;
        if (k < 20u) {                            /* a key down / up on the selected track */
            uint32_t key = (r >> 8) % 27u;
            fm1_in.notes ^= 1u << key;
            host_notes |= fm1_in.notes;
        } else if (k < 24u)
            sel_set((r >> 4) % NTRK);
        else if (k < 27u)
            trk[(r >> 4) % NTRK].p[P_MUTE] ^= 1;
        else if (k < 29u)
            ui_mch((r >> 4) % NTRK) = (uint8_t)((r >> 12) & 15u);
        else if (k < 30u)
            MIDI_BYTE = (uint8_t)((r >> 12) % 3u);
        else if (k < 31u)
            panic_req = (uint8_t)(1u << ((r >> 4) % NTRK));
        else if (k < 32u)
            transport_req = song.playing ? 2u : 1u;
        else if (k < 34u)
            lg_hold = !lg_hold;
        else if (k < 36u)
            trk[(r >> 4) % NTRK].p[P_AMODE] ^= 1;
        events_block(CTL);
        mo_drain();
    }
    lg_hold = 0;
    MIDI_BYTE = NO_ALL;
    fm1_in.notes = 0;
    stop();
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_AMODE] = 0;
    blk(BAR_BLOCKS);
    bad += check("20,000 blocks of keys, tracks, mutes, channels, NOTES OUT, panics, STOP and a host not reading: "
                 "every note-on one note-off on its channel, none left on", paired(0, &ons, &open, &chans) && !open && ons > 100u);
    for (i = 0; i < NTRK; i++)
        ui_mch(i) = 0;
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += test_pairing();
    bad += test_modes();
    bad += test_midi_in();
    bad += test_channel_change();
    bad += test_panic_and_overflow();
    bad += test_due();
    bad += test_ext();
    bad += test_cc();
    bad += test_remote_auto();
    bad += test_spp();
    bad += test_clock();
    bad += test_settings_kept();
    bad += test_fuzz();
    puts(bad ? "MIDI OUT TEST FAILED" : "midi out test passed");
    return bad != 0;
}
