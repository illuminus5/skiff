/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: live pattern switching (DESIGN.md: the PERFORM layer, SAVE > PATTERNS). While playing, the main loop stages a
 * pattern from the bank for a track (bank.c bank_switch: launch_q, its steps, LEN DIV SWING GATE DIR, its automation and
 * locks) and arms it; the audio ISR swaps it in on a step boundary, at its launch point (the PERFORM layer's
 * KNOB 1, ui.c ui_launch):
 *   LOOP  where the track's pass ends (its last step in its direction: seq_pass_last)
 *   BAR   the step that begins on the next bar: the bars as the click counts them, four beats of four 1/16 steps as
 *         a 1/16 track counts its steps (launch_clock: launch_steps, counted before the tracks tick, so a 1/16 step on
 *         the bar line sees it). A track whose DIV rounds to steps a little shorter than 1/16's (1/32, the triplets:
 *         div_samples) runs a few samples a beat ahead of that: its step starting less than half a step before the
 *         bar line is the bar's too
 *   BEAT  likewise the next beat
 *   NOW   the next step, the playhead going on where it is
 * LOOP, BAR and BEAT start the pattern from its first step in its direction (a new pass). The swap exchanges: launch_q
 * then holds the pattern that played (done), which the main loop keeps as the pattern it was (bank_poll). The notes of
 * the pattern going out end; a NUDGE or RATCH part of it still to come is dropped.
 * Staged mutes (the PERFORM layer's black keys 1..4, bank.c mute_switch): a track's MUTE turned at its own launch
 * point the same way (LOOP: that track's pass end); the step starting there plays as the MUTE says.
 * Included by seq.c (after motion.c, before seq_tick); launch_step runs in the ISR, only with something staged, not
 * inlined. */
enum { LAUNCH_LOOP, LAUNCH_BAR, LAUNCH_BEAT, LAUNCH_NOW, LAUNCH_N };
static const uint8_t LAUNCH_TIME[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_DIR};
typedef struct {
    volatile uint8_t armed;                        /* staged, waiting for its launch point (set last; the ISR clears) */
    volatile uint8_t done;                         /* swapped in (the ISR; the main loop keeps what went out, clears it) */
    uint8_t slot, mode;                            /* the entry (the track's list: bank.c), LAUNCH_* */
    uint8_t adv;                                   /* a song's next one (bank.c song_poll): a stop drops it */
    uint8_t id, mlost;                             /* (phase 5) its pattern; 1: it comes without its automation */
    uint8_t reps;                                  /* .. the passes the one playing makes before it (a song's; 0 none) */
    uint8_t snd;                                   /* ((2.0, #30) 1: the entry's sound comes in with it, esnd) */
    uint8_t same;                                  /* ((2.0.3, #71) 1: the pattern the track is on (bank.c stage_entry):
                                                    * the swap leaves the track its own steps, timing and automation,
                                                    * edited since this copy was made; the entry's sound and a new pass */
    uint32_t at;                                   /* BAR / BEAT: the launch_steps (a beat's, a bar's) it comes in on */
    step_t step[NSTEP];                            /* the pattern (after the swap: the one it replaced) */
    int16_t time[5];                               /* .. its LEN DIV SWING GATE DIR */
    motion_store_t motion;                         /* .. its automation and locks (place: the track's), its on bit */
} launch_t;
static launch_t launch_q[NTRK] __attribute__((section(".pool")));
static motion_event_t launch_in[MOTION_MAX] __attribute__((section(".pool")));   /* (the swap's: off the ISR stack) */
/* a mute staged for track k: 0 none, 1 unmuted, 2 muted at its launch point (mode, at). One byte a track, set by the
 * main loop (mute_switch, after mode and at), cleared by the ISR as it lands or by the main loop */
static volatile uint8_t mute_arm[NTRK];
static uint8_t mute_mode[NTRK];
static uint32_t mute_at[NTRK];
/* (2.0, #30) a song entry's sound (bank.c esnd_make), as PRESETS loads one: every value but param_kept's, the engine,
 * preset and user slot, an FM6 patch (fm6 1) and a DRUM-X preset's kit (+ 1). Made by the main loop when the entry is
 * staged, put in by the swap (its first notes play it); stopped at once (bank.c esnd_go). The automation's base takes
 * its values; an engine change fades from this block (the swap's: its engine_block ran before it, so the old engine
 * renders on pe_old, the notes after the fade), not by a call of engine_block (its second caller tripled the ISR's
 * static cost) */
typedef struct {
    int16_t p[P_COUNT];
    uint8_t f6[FP_SIZE + 1u];
    uint8_t eng, preset, user, kit, fm6;
} esnd_t;
static esnd_t esnd[NTRK];
static __attribute__((noinline)) void esnd_put(track_t *t, const esnd_t *s)
{
    uint32_t k = trk_index(t), i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i))
            t->p[i] = s->p[i];
    t->eng_req = s->eng;
    t->preset = s->preset;
    t->user = s->user;
    if (s->kit)
        memcpy(dx_kit, DX_KITS[s->kit - 1u], sizeof dx_kit);
    if (s->fm6) {
        memcpy(fm6_patch[k], s->f6, sizeof s->f6);
        fm6_pgen[k]++;
        fm6_own_ok &= (uint8_t)~(1u << k);         /* (fm6_poll takes SLOT as it is now) */
    }
    drum_mute[k] = 0;                              /* (the lanes' mutes go with the sound) */
    trk_all_off(t);                                /* (MONO / POLY may change: what sounds let go) */
    memcpy(motion_base[k], t->p, sizeof t->p);
    if (t->eng_req != t->engine) {
        t->xf_on = 1;
        t->xf = XF_BLOCKS;
    }
}

/* track t's playing step has just ended (period: its DIV's step; t->seq_pos: how long ago, at the block's end): a
 * launch point (mode, at) is here */
static int launch_here(const track_t *t, uint32_t len, uint32_t period, uint32_t mode, uint32_t at)
{
    if (mode == LAUNCH_LOOP)
        return seq_pass_last(t, len);
    if (mode == LAUNCH_NOW || launch_steps >= at)
        return 1;
    return (at - launch_steps) * div_samples(2) - launch_pos + t->seq_pos < period / 2u;   /* (its line < half a step on) */
}
/* .. its staged pattern comes in now (a song's: once the pattern playing has made its passes, take.c take_p0) */
static int launch_due(const track_t *t, uint32_t len, uint32_t period)
{
    uint32_t k = trk_index(t);
    const launch_t *q = &launch_q[k];
    if ((uint16_t)(take_pass[k] - take_p0[k]) < q->reps)
        return 0;
    return launch_here(t, len, period, q->mode, q->at);
}
/* (phase 5) the step track t enters may end where its staged pattern comes in (LOOP: its pass's last, the passes
 * made; BAR, BEAT, NOW: whenever one is staged, the point not known here): the step after it is not played early
 * (seq.c seq_enter: NUDGE), it would be the outgoing pattern's */
static int launch_ahead(const track_t *t, uint32_t len)
{
    uint32_t k = trk_index(t);
    const launch_t *q = &launch_q[k];
    if (!q->armed)
        return 0;
    return q->mode != LAUNCH_LOOP || (seq_pass_last(t, len) && (uint16_t)(take_pass[k] - take_p0[k]) >= q->reps);
}
/* (2.0, #44) the switch at a LOOP point, both patterns going forward (a chain's next part, bank.c song_poll): a note
 * goes on across it as across any step (launch_swap); ((#30) not into another sound) */
static int launch_carry(const track_t *t, const launch_t *q)
{
    return q->mode == LAUNCH_LOOP && t->p[P_DIR] == DIR_FWD && (q->same || q->time[4] == DIR_FWD) && !q->snd;
}
/* .. the step after the one track t enters, as its ties see it (seq.c seq_step): such a switch at its end, the staged
 * pattern's first step; else own */
static const step_t *launch_next(const track_t *t, uint32_t len, const step_t *own)
{
    const launch_t *q = &launch_q[trk_index(t)];
    return q->armed && !q->same && launch_carry(t, q) && launch_ahead(t, len) ? &q->step[0] : own;
}
/* the swap (the audio ISR): the track's steps, timing and motion for the staged ones, the staged ones for them */
static __attribute__((noinline)) void launch_swap(track_t *t)
{
    uint32_t k = trk_index(t), i, n;
    launch_t *q = &launch_q[k];
    motion_event_t *in = launch_in;
    uint32_t cin = q->motion.count, on = q->motion.on, carry = launch_carry(t, q);
    if (!carry || !(t->seq_hold || seq_k[k]))      /* (the notes going out end; held or tied, too; ((2.0, #44) at a */
        seq_release(t);                            /* LOOP switch going forward, what its last step held for the */
                                                   /* incoming first's ties or TIE goes on: launch_next) */
    motion_restore(t);                             /* (the automation's values: the sound's own again) */
    if (!q->same) {                                /* ((2.0.3, #71) the same pattern: the track's own goes on, not q's
                                                    * copy of it from before an edit; bank_poll_trk keeps nothing) */
        memcpy(in, q->motion.event, cin * sizeof in[0]);
        q->motion.count = 0;
        q->motion.on = (uint8_t)(motion.on & (1u << k));
        for (i = 0, n = 0; i < motion.count; i++)
            if ((motion.event[i].place >> 6) == k)
                q->motion.event[q->motion.count++] = motion.event[i];
            else
                motion.event[n++] = motion.event[i];
        for (i = 0; i < cin && n < MOTION_MAX; i++)
            motion.event[n++] = in[i];
        memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
        motion.count = (uint8_t)n;
        motion.on = (uint8_t)((motion.on & ~(1u << k)) | (on & (1u << k)));
        for (i = 0; i < NSTEP; i++) {
            step_t s = t->step[i];
            t->step[i] = q->step[i];
            q->step[i] = s;
        }
        for (i = 0; i < 5u; i++) {
            int16_t v = t->p[LAUNCH_TIME[i]];
            t->p[LAUNCH_TIME[i]] = q->time[i];
            q->time[i] = v;
        }
    }
    if (q->snd)
        esnd_put(t, &esnd[k]);                     /* ((2.0, #30) the entry's sound) */
    t->rat_left = 0;
    t->nud = 0;
    t->rskip_n = 0;
    if (carry) {                                   /* ((2.0, #44) a key held recording goes on into it, as into a */
        t->rh_start = 0xFFu;                       /* step: its first step ties it (seq.c rec_hold) */
        t->rh_ties = 0;
        memset(rec_n[k], 0, sizeof rec_n[k]);
    } else
        t->rh_n = 0;
    q->armed = 0;
    q->done = 1;
}
/* a project load (project.c, the transport stopped, the ISR off): nothing staged comes in over it (what went out at a
 * swap before it: kept first, project_load) */
static void launch_drop(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        launch_q[k].armed = launch_q[k].done = mute_arm[k] = 0;
}
/* track t's playing step has just ended, a mute or a pattern staged for it: at its launch point the mute turns; the
 * swap, and (but NOW) its first step next, a new pass. 1: swapped (seq_tick takes its DIV and LEN again). Not
 * inlined: seq_tick's step loop keeps its shape (tests/target_budget.py) */
static __attribute__((noinline)) int launch_step(track_t *t, uint32_t len, uint32_t period)
{
    uint32_t k = (uint32_t)(t - trk), now = launch_q[k].mode == LAUNCH_NOW;
    if (mute_arm[k] && launch_here(t, len, period, mute_mode[k], mute_at[k])) {
        t->p[P_MUTE] = (int16_t)(mute_arm[k] - 1u);
        mute_arm[k] = 0;
    }
    if (!launch_q[k].armed || !launch_due(t, len, period))
        return 0;
    launch_swap(t);
    take_p0[k] = take_pass[k];                     /* (phase 5) its passes from the next on (a song's repeats) */
    if (!now) {
        t->seq_idx = (uint16_t)(t->p[P_SLEN] > 1 ? t->p[P_SLEN] - 1 : 0);
        t->seq_cnt = 0;
    }
    return 1;
}
/* each block, before the tracks tick: the 1/16 steps as a 1/16 track (and the click: click_tick) counts them, PLAY's
 * first block step 0 (seq_start), the remainder kept (BAR / BEAT launches) */
static void launch_clock(uint32_t n)
{
    uint32_t p = div_samples(2);
    if (launch_pos == CLK_START) {
        launch_pos = 0;
        return;
    }
    launch_pos += n;
    while (launch_pos >= p) {
        launch_pos -= p;
        launch_steps++;
    }
}
