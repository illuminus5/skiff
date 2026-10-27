/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff on the SMK-37 Pro (BOARD_SMK37): the controls the FM-1 has no counterpart for, as design/SMK37_UX.md has them
 * (picked 2026-10-19, design/mockups/smk37_*.png):
 *   pads C   always a grid, never a second keyboard: where the FM-1's keys show a map of their own (a layer, the DRUM
 *            grid, NAME, SLICES; or a layer's button held) the pads are the 16 white keys in text order (smk_input.h),
 *            elsewhere the selected track's 16 steps of the page shown, a tap setting or clearing a step;
 *   colours A as the screen's step tiles: empty faint white, a step in the track's colour (a tie half), the edited step
 *            white, the one playing brighter (half way to white: a pad is one LED, it has no ring);
 *   faders A the LEVEL of the four tracks the screen shows, FADER BANK stepping LEVEL PAN REVERB DELAY, each fader
 *            taking over once it reaches the value;
 *   wheels A, the pedal: as MIDI in on the selected track's channel (bend, CC1: the MOD matrix's MODW; CC64 sustain);
 *            the pedal holds the track's keys too (seq.c);
 *   < >      the selected track's entry before and after (as PERFORM's keys: at once stopped, at LAUNCH playing);
 *   MASTER   K1, kept with the settings; KEY VELOCITY (MENU > CONTROL).
 * KNOB BANK is SHIFT (ui_input.c); the button LEDs on or off (ui_input.c smk_leds_out). Main loop only. */

/* (SMK_KV_BYTE, SMK_MASTER_BYTE: ui.c, settings bytes) */

/* ------------------------------------------------------------------------------------------------- pads --- */
static int smk_pads_keys(void)                     /* 1: the pads are the white keys (the keys' own map, a layer's) */
{
    return keys_own() || (fm1_in.buttons & kb_mask) || kb_lock;
}

#define SMK_PAD_FULL 120u                           /* a lit pad: /256 of the LED's full (they glare) */
#define SMK_PAD_WHITE 90u                           /* the edited step's white, per channel */
#define SMK_PAD_FAINT 0x0A0A0Au                     /* an empty step */
static uint32_t smk_rgb(uint16_t c565, uint32_t q)  /* a screen colour as a pad's 0xRRGGBB, q /256 of full */
{
    uint32_t r = (c565 >> 11) * 255u / 31u, g = ((c565 >> 5) & 63u) * 255u / 63u, b = (c565 & 31u) * 255u / 31u;
    return (r * q >> 8) << 16 | (g * q >> 8) << 8 | (b * q >> 8);
}
static uint32_t smk_mixw(uint32_t c, uint32_t t)   /* c toward the pads' white by t /256 */
{
    uint32_t i, o = 0;
    for (i = 0; i < 24u; i += 8u) {
        uint32_t v = (c >> i) & 255u;
        o |= (v + ((SMK_PAD_WHITE - v) * t >> 8)) << i;
    }
    return o;
}
static uint32_t smk_breath(uint32_t c)               /* a breathing pad: dark .. its colour and back, ~1.1 s */
{
    uint32_t ph = fm1_ms % 1100u, t = ph < 550u ? ph : 1100u - ph, q = 40u + t * 216u / 550u, i, o = 0;
    for (i = 0; i < 24u; i += 8u)
        o |= ((((c >> i) & 255u) * q) >> 8) << i;
    return o;
}

/* the selected track's steps of the page shown as pads (colours A): the DRUM grid's selected lane, or the notes */
static void smk_pads_steps(void)
{
    const track_t *t = TSEL;
    uint32_t p, len = (uint32_t)t->p[P_SLEN], col = smk_rgb(T_TRK(song.sel), SMK_PAD_FULL);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    int edit = !ui.home && !ui.menu && step_page(cur_page()->graph), drum = drum_track(t);
    for (p = 0; p < 16u; p++) {
        uint32_t i = ui.bank * 16u + p, c;
        const step_t *st = &t->step[i % NSTEP];
        if (i >= len) {
            padled_set(p, 0, 0, 0);
            continue;
        }
        if (drum)
            c = (step_lanes(st) >> ui.lane) & 1u ? col : SMK_PAD_FAINT;
        else
            c = step_on(st) ? col : step_tie(st) ? smk_rgb(T_TRK(song.sel), SMK_PAD_FULL / 2u) : SMK_PAD_FAINT;
        if (edit && i >= sel_lo() && i <= sel_hi() && !song.playing)   /* ((2.0) the selection's range) */
            c = SMK_PAD_WHITE << 16 | SMK_PAD_WHITE << 8 | SMK_PAD_WHITE;
        if (p == ph)
            c = smk_mixw(c, 128u);
        padled_set(p, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
}

/* ui_input.c ui_leds: the pads' picture for the frame. The keys' own map (a layer's, NAME's, SLICES'): its levels for
 * the white keys (lit, breathing, the mid level, the glow) in the selected track's colour; the DRUM grid and the rest:
 * the steps */
static void smk_pads_out(const uint8_t *nl, const uint8_t *nd, const uint8_t *nb, const uint8_t *nm)
{
    uint32_t w, k, col = smk_rgb(T_TRK(song.sel), SMK_PAD_FULL);
    if (!smk_pads_keys() || grid_on()) {
        smk_pads_steps();
        return;
    }
    for (w = 0, k = 0; k < 27u && w < 16u; k++) {
        uint32_t q = LED_KEY0 + k, b = 1u << (q & 7u), c;
        if (key_black(k))
            continue;
        c = (nl[q >> 3] & b) ? col : (nb[q >> 3] & b) ? smk_breath(col) :
            (nm[q >> 3] & b) ? smk_rgb(T_TRK(song.sel), SMK_PAD_FULL / 3u) : (nd[q >> 3] & b) ? SMK_PAD_FAINT : 0u;
        padled_set(w++, (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
    }
}

/* a step tap (steps mode): the selected track's step at pad p of the page shown. A DRUM track: the selected lane's hit
 * there, toggled (as the grid's white keys); a melodic one: a step with notes cleared, an empty one set to the last
 * note played (at the pad's velocity). A run of taps is one undo (SMK_TAPS); on the step pages the cursor goes there */
static const char SMK_TAPS[] = "PAD STEPS";
static void smk_pad_tap(uint32_t p, uint32_t vel)
{
    track_t *t = TSEL;
    uint32_t i = ui.bank * 16u + p;
    step_t *st;
    if (ui.menu || ui.confirm || name_on() || i >= (uint32_t)t->p[P_SLEN] || i >= NSTEP)
        return;
    st = &t->step[i];
    if (!(undo_n && !undo_back && undo.name == SMK_TAPS && undo.trk == song.sel + 1u && track_sig(t) == undo.after &&
          fm1_ms - undo.t_ms < 1500u))
        tl_undo_take(t, SMK_TAPS);
    if (drum_track(t))
        grid_hit(t, i, ui.lane, 2);
    else if (step_on(st))
        step_clear(st);
    else {
        step_clear(st);
        rec_put(t, st, last_note, smk_vel(vel));
    }
    undo.after = track_sig(t);
    undo.t_ms = fm1_ms;
    if (!ui.home && step_page(cur_page()->graph))
        cursor_set((int32_t)i);
    ui.force = 1;
}

static void smk_pads(void)
{
    int32_t e;
    smk_pad_steps = (uint8_t)!smk_pads_keys();
    while ((e = smk_padq_take()) >= 0)
        if (e & 0xFF)
            smk_pad_tap(((uint32_t)e >> 8) & 15u, (uint32_t)e & 0xFFu);
}

/* ----------------------------------------------------------------------------------------------- faders --- */
static const uint8_t SMK_FB_P[4] = {P_LEVEL, P_PAN, P_REV, P_DLY};
static const char *const SMK_FB_N[4] = {"LEVEL", "PAN", "REVERB", "DELAY"};
static uint8_t smk_fbank;                            /* FADER BANK: SMK_FB_P's */
static struct { uint8_t tgt, last, picked; int16_t wrote; } smk_fd[4] = {{0xFF}, {0xFF}, {0xFF}, {0xFF}};
static void smk_faders(void)
{
    uint32_t top = trk_top(), j;
    if (fm1_in.frames < 300u)                        /* (the first readings settle) */
        return;
    for (j = 0; j < 4u; j++) {
        uint32_t k = top + j, id = SMK_FB_P[smk_fbank % 4u], v = fm1_in.fader[j], tgt = k | smk_fbank << 4;
        track_t *t = &trk[k % NTRK];
        int32_t cur = t->p[id] - TP[id].min;
        char b[24], val[8];
        const char *unit = "";
        if (smk_fd[j].tgt != tgt) {                  /* another track or bank: wait for it to reach the value */
            smk_fd[j].tgt = (uint8_t)tgt;
            smk_fd[j].picked = 0;
            smk_fd[j].last = (uint8_t)v;
            continue;
        }
        if (v == smk_fd[j].last)
            continue;
        if (smk_fd[j].picked && t->p[id] != smk_fd[j].wrote)
            smk_fd[j].picked = 0;                    /* (set elsewhere since: reach it again) */
        if (!smk_fd[j].picked && !((smk_fd[j].last <= cur && (int32_t)v >= cur) ||
                                   (smk_fd[j].last >= cur && (int32_t)v <= cur))) {
            smk_fd[j].last = (uint8_t)v;
            continue;
        }
        smk_fd[j].picked = 1;
        smk_fd[j].last = (uint8_t)v;
        t->p[id] = (int16_t)(TP[id].min + (int32_t)v);
        smk_fd[j].wrote = t->p[id];
        param_format(&TP[id], t->p[id], val, &unit);
        b[0] = 'T';
        b[1] = (char)('1' + k % NTRK);
        b[2] = ' ';
        str_cpy(b + 3, SMK_FB_N[smk_fbank % 4u], sizeof b - 3u);
        str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        str_cpy(b + str_len(b), val, sizeof b - str_len(b));
        ui_say(b, unit ? unit : "");
        ui.force = 1;
    }
}

/* ------------------------------------------------------------------------------------- wheels, pedal --- */
static void smk_midi(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)   /* as if it came in over USB-MIDI */
{
    fm1_irq_off();                                   /* (usb_poll fills the queue from the timer) */
    midi_enqueue((st >> 4) | (st | ch) << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
    fm1_irq_on();
}
/* the wheels, self-calibrating (after genkigenki's layer: the pitch wheel's rest is its centre, taken at power-on and
 * followed while it rests; each range grows to what it reached; the travel is short, raw ~445..640 pitch, ~474..629 mod,
 * up = lower). Sent on the selected track's channel when they change: a bend, CC1; the track before gets them back to
 * rest when another is selected. The pedal (mux C 6, ~985 open): its rest at power-on, a press well away from it;
 * down, it holds the selected track's keys too (smk_pedal: seq.c kb_up) */
static void smk_wheels(void)
{
    enum { DEAD = 12, SPAN0 = 80, MOD_LO = 485, MOD_HI = 615 };
    static int32_t pc, plo, phi, mlo, mhi, rest, prest, sb = 0, sm = 0, sp = 0, sch = -1;
    int32_t p = fm1_in.wheel[0], m = fm1_in.wheel[1], d, b, mw, pd, ch = (int32_t)trk_midi_ch(song.sel);
    if (fm1_in.frames < 300u)
        return;
    if (!pc) {
        pc = p;
        plo = p - SPAN0;
        phi = p + SPAN0;
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
        prest = fm1_in.pedal;
    }
    plo = p < plo ? p : plo;
    phi = p > phi ? p : phi;
    mlo = m < mlo ? m : mlo;
    mhi = m > mhi ? m : mhi;
    d = p - pc;
    if (d > -DEAD && d < DEAD) {
        b = 0;
        if (++rest > 200) {                          /* resting: the centre follows a slow drift */
            pc += d > 0 ? 1 : d < 0 ? -1 : 0;
            rest = 0;
        }
    } else {
        rest = 0;
        b = d > 0 ? (d - DEAD) * 8191 / (phi - pc - DEAD > 1 ? phi - pc - DEAD : 1)
                  : (d + DEAD) * 8192 / (pc - plo - DEAD > 1 ? pc - plo - DEAD : 1);
        b = clamp(b, -8192, 8191);
    }
    mw = mhi - m <= DEAD ? 0 : clamp((mhi - m - DEAD) * 127 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1), 0, 127);
    pd = sp ? ((int32_t)fm1_in.pedal - prest > 150 || prest - (int32_t)fm1_in.pedal > 150)
            : ((int32_t)fm1_in.pedal - prest > 250 || prest - (int32_t)fm1_in.pedal > 250);
    if (ch != sch) {                                 /* another track: the one before back to rest */
        if (sch >= 0) {
            if (sb)
                smk_midi(0xE0u, (uint32_t)sch, 0, 64);
            if (sm)
                smk_midi(0xB0u, (uint32_t)sch, 1, 0);
            if (sp)
                smk_midi(0xB0u, (uint32_t)sch, 64, 0);
        }
        sch = ch;
        sb = sm = sp = 0;
    }
    if (b != sb) {
        uint32_t v = (uint32_t)(b + 8192);
        smk_midi(0xE0u, (uint32_t)ch, v & 127u, v >> 7);
        sb = b;
    }
    if (mw != sm) {
        smk_midi(0xB0u, (uint32_t)ch, 1, (uint32_t)mw);
        sm = mw;
    }
    if (pd != sp) {
        smk_midi(0xB0u, (uint32_t)ch, 64, pd ? 127u : 0u);
        sp = pd;
    }
    smk_pedal = (uint8_t)pd;                         /* (the keys: seq.c kb_up) */
}

/* ------------------------------------------------------------------------------------------ < > , MASTER --- */
static void smk_buttons(void)
{
    static uint32_t prev;
    uint32_t now = fm1_in.buttons, pr = now & ~prev;
    prev = now;
    if (ui.menu || ui.confirm || name_on() || ui.layer)
        return;
    if ((pr >> SMK_BTN_FADER_BANK) & 1u) {
        smk_fbank = (uint8_t)((smk_fbank + 1u) % 4u);
        ui_say("FADERS ", SMK_FB_N[smk_fbank]);
    }
    if (pr & (1u << SMK_BTN_LEFT | 1u << SMK_BTN_RIGHT)) {   /* the selected track's entry before / after */
        uint32_t k = song.sel % NTRK, base = launch_q[k].armed && !launch_q[k].adv ? launch_q[k].slot : lists.cur[k];
        int32_t e = (int32_t)base % (int32_t)NENT + ((pr >> SMK_BTN_RIGHT) & 1u ? 1 : -1);
        if (e < 0 || e >= (int32_t)NENT)
            ui_message(e < 0 ? "FIRST ENTRY" : "LAST ENTRY");
        else
            (void)entry_go(k, (uint32_t)e);
    }
}

static void smk_master_poll(void)                    /* K1: its level said while it moves, kept once it rests */
{
    static int32_t last = -1;
    static uint32_t moved;
    if (last < 0)
        last = smk_master;
    if (smk_master != last) {
        char b[8];
        int32_t pct = smk_master * 100 / 1023;
        b[0] = (char)(pct >= 100 ? '1' : ' ');
        b[1] = (char)('0' + pct / 10 % 10);
        b[2] = (char)('0' + pct % 10);
        b[3] = '%';
        b[4] = 0;
        ui_say("MASTER ", pct >= 100 ? b : pct >= 10 ? b + 1 : b + 2);
        last = smk_master;
        moved = fm1_ms | 1u;
    } else if (moved && fm1_ms - moved > 1500u) {
        moved = 0;
        SMK_MASTER_BYTE = (uint8_t)(smk_master / 4 + 1 > 255 ? 255 : smk_master / 4 + 1);
        settings_save();                             /* (queued: written while stopped) */
    }
}

/* power-on, the settings read (persist_boot), before master_boot: MASTER where it was left */
static void smk_ui_boot(void)
{
    if (SMK_MASTER_BYTE)
        smk_master = ((int32_t)SMK_MASTER_BYTE - 1) * 4;
    smk_kv_mode = (uint8_t)(SMK_KV_BYTE % SMK_KV_N);
}

/* main.c: each UI frame, after ui_input */
static void smk_ui_frame(void)
{
    smk_kv_mode = (uint8_t)(SMK_KV_BYTE % SMK_KV_N);
    smk_pads();
    smk_faders();
    smk_wheels();
    smk_buttons();
    smk_master_poll();
}
