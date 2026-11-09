/* SPDX-License-Identifier: GPL-3.0-only
 * Adapted from MIDI clock contributions by ChanceTheMaker and keremimo (2026); (2.5, #111) the tempo's mean of beats
 * after SLOOP 2.7.2 (isod89/sloop-fm1, GPL-3.0).
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Audio ISR state. Input timestamps keep USB/TRS jitter outside the render loop. */
#define MC_NB 8u                                   /* ((2.5, #111) the tempo: the mean of the last 8 beats */
static struct {
    uint32_t pos, rendered, last_ms, start_ms, rem, interval_ms;
    uint32_t pulse_samples, interp_q8, tempo_ms;
    uint16_t spp;                                  /* ((2.2) a Song Position + 1 waiting for Continue; 0 none) */
    uint16_t bdt[MC_NB];                           /* ((2.5, #111) the last beats in ms (nb of them), bi the next */
    uint8_t nb, bi, away;                          /* slot; away: beats in a row 5 BPM or more off the tempo) */
    uint8_t mode, have_pulse, tempo_valid, tempo_n;
    volatile uint8_t starved;                      /* ((2.2) PLAY stopped with no pulse since: ui.c clock_wait_poll says
                                                    * WAITING FOR .. CLOCK, and clears it) */
} midi_clock;

static __attribute__((noinline)) void midi_clock_transport(uint32_t status, uint32_t ms)
{
    if (status == 0xFAu) {                         /* Start: step zero */
        midi_clock.pos = midi_clock.rendered = midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        midi_clock.spp = 0;
        seq_start();
    } else if (status == 0xFBu) {                  /* Continue: preserve the step ((2.2) or go where SPP said) */
        midi_clock.pos = midi_clock.rendered;
        midi_clock.rem = 0;
        midi_clock.have_pulse = midi_clock.tempo_valid = 0;
        midi_clock.interval_ms = 0;
        midi_clock.start_ms = ms;
        if (midi_clock.spp) {
            seq_locate(midi_clock.spp - 1u);
            midi_clock.spp = 0;
            return;
        }
        if (!song.playing)
            motion_begin();
        song.playing = 1;
    } else if (status == 0xFCu) {
        seq_stop();
    }
}
/* (2.2) Song Position Pointer (F2, 16ths from the top: a DAW starting or looping mid-song): kept for the Continue that
 * follows; while playing, there at once (a loop's jump back) */
static __attribute__((noinline)) void midi_clock_spp(uint32_t pos)
{
    pos &= 0x3FFFu;
    if (song.playing)
        seq_locate(pos);
    else
        midi_clock.spp = (uint16_t)(pos + 1u);
}

/* (2.5, #111, after SLOOP 2.7.2) the tempo from a beat of dt ms: the mean of the last MC_NB beats. Taken a 16th at a
 * time (six pulses) it jumped: a master that swings its clock gave 16ths of two lengths (+-25 BPM at 60:40), and a
 * pulse's timestamp moves by a few ms (USB, the master, a render it waits for: +-3 BPM over a 16th). A beat holds a
 * swing's pair whole; one beat to the millisecond still reads +-1 BPM at random, so once there are MC_NB the tempo
 * moves only when the mean is 0.8 BPM away (a ramp still goes through). A beat half a pulse or more off the mean (a
 * pulse lost or doubled moves it a whole one; the jitter, a few ms, less at any tempo) is passed over; two in a row are
 * a new tempo, taken at once (the mean starts again). The steps follow the pulses either way: this is the BPM shown
 * and the beat what follows it uses (delay, SYNC, arp, slicer) */
static __attribute__((noinline)) void midi_clock_tempo(uint32_t dt)
{
    uint32_t k, sum = 0, n = midi_clock.nb, t10, old_beat, new_beat;
    int32_t cur = song.g[G_BPM];
    for (k = 0; k < n; k++)                        /* (slots 0 .. nb - 1 while filling: bi starts at 0) */
        sum += midi_clock.bdt[k];
    if (n && (dt * n > sum ? dt * n - sum : sum - dt * n) * 48u >= sum) {
        if (++midi_clock.away < 2u)
            return;
        midi_clock.nb = midi_clock.bi = 0;
        n = sum = 0;
    }
    midi_clock.away = 0;
    if (n == MC_NB)
        sum -= midi_clock.bdt[midi_clock.bi];      /* (the oldest goes) */
    else
        midi_clock.nb = (uint8_t)++n;
    midi_clock.bdt[midi_clock.bi] = (uint16_t)dt;
    midi_clock.bi = (uint8_t)((midi_clock.bi + 1u) % MC_NB);
    sum += dt;
    t10 = (600000u * n + sum / 2u) / sum;          /* (tenths of a BPM; <= 8 x 600000 + 6000: 32 bits) */
    if (n == MC_NB && (int32_t)t10 - cur * 10 < 8 && cur * 10 - (int32_t)t10 < 8)
        return;
    song.g[G_BPM] = (int16_t)clamp((int32_t)((t10 + 5u) / 10u), 40, 240);
    old_beat = beat_samples();
    new_beat = QUARTER24((uint32_t)FS * sum / (1000u * n));   /* ((2.5, #107) core.h) */
    if (song.playing && new_beat != old_beat) {
        uint32_t i, ratio = (new_beat << 12) / old_beat;
        /* Preserve each track's fractional step phase when the master
         * changes tempo. The next pulse then lands on its new boundary. */
        for (i = 0; i < NTRK; i++) {
            if (trk[i].seq_pos < (uint32_t)FS * 2u)
                trk[i].seq_pos = (uint32_t)(((uint64_t)trk[i].seq_pos * ratio + 2048u) >> 12);
            if (trk[i].seq_off)
                trk[i].seq_off = (uint32_t)(((uint64_t)trk[i].seq_off * ratio + 2048u) >> 12);
        }
        if (clk_pos < (uint32_t)FS * 2u)           /* (the metronome's beat as the steps) */
            clk_pos = (uint32_t)(((uint64_t)clk_pos * ratio + 2048u) >> 12);
        if (launch_pos < (uint32_t)FS * 2u)        /* (Skiff: the launch points' too, launch.c) */
            launch_pos = (uint32_t)(((uint64_t)launch_pos * ratio + 2048u) >> 12);
    }
    midi_beat_samples = new_beat;
}

static __attribute__((noinline)) void midi_clock_pulse(uint32_t ms)
{
    uint32_t q;
    if (!midi_clock.tempo_valid) {
        midi_clock.tempo_valid = 1;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
        midi_clock.nb = midi_clock.bi = midi_clock.away = 0;
    } else if (++midi_clock.tempo_n == 24u) {      /* ((2.5, #111) a beat, was a 16th) */
        uint32_t dt = ms - midi_clock.tempo_ms;
        midi_clock.tempo_ms = ms;
        midi_clock.tempo_n = 0;
        if (dt >= 250u && dt <= 1500u)             /* (40..240 BPM: a gap or a burst is not a tempo) */
            midi_clock_tempo(dt);
    }
    if (song.playing) {
        if (midi_clock.have_pulse) {
            uint32_t interval = ms - midi_clock.last_ms;
            if (interval >= 8u && interval <= 80u)   /* ((2.5, #111) smoothed, as SLOOP's) */
                midi_clock.interval_ms = midi_clock.interval_ms ? (midi_clock.interval_ms * 3u + interval + 2u) / 4u
                                                                : interval;
            q = beat_samples() + midi_clock.rem;
            midi_clock.pos += q / 24u;
            midi_clock.rem = q % 24u;
        }
        midi_clock.have_pulse = 1;
    }
    midi_clock.pulse_samples = beat_samples() / 24u;
    if (!midi_clock.interval_ms)
        midi_clock.interval_ms = beat_samples() * 1000u / ((uint32_t)FS * 24u);
    midi_clock.interp_q8 = midi_clock.pulse_samples * 256u / midi_clock.interval_ms;
    midi_clock.last_ms = ms;
}

static uint32_t midi_clock_advance(uint32_t now)
{
    uint32_t target, elapsed, offset, n;
    if (!midi_clock.have_pulse)
        return 0;
    elapsed = now - midi_clock.last_ms;
    /* Interpolate to the next pulse, never across it before it arrives. */
    if (elapsed > midi_clock.interval_ms)
        elapsed = midi_clock.interval_ms;
    offset = elapsed * midi_clock.interp_q8 >> 8;
    if (offset >= midi_clock.pulse_samples)
        offset = midi_clock.pulse_samples - 1u;
    target = midi_clock.pos + offset;
    n = (int32_t)(target - midi_clock.rendered) > 0 ? target - midi_clock.rendered : 0u;
    if (n > (uint32_t)FS / 8u)
        n = (uint32_t)FS / 8u;
    midi_clock.rendered += n;
    return n;
}

