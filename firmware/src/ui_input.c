/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
#if BOARD_SMK37
/* (SMK-37 Pro) no key matrix LEDs: the picture is drawn in a plain id space, place = id (byte id / 8, bit id % 8),
 * the buttons 0..17, PLAY's red half 18, the 27 keys from LED_KEY0; smk_leds_out reads the buttons' back */
#define LED_KEY0 24u                                /* key k's LED id (the FM-1's 14 + k: its button ids end at 13) */
#define LED_NID (LED_KEY0 + 27u)
#else
#define LED_KEY0 14u
#define LED_NID 41u
#endif
static uint8_t led_pos[LED_NID];                   /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
#if BOARD_SMK37
    uint32_t id;
    for (id = 0; id < LED_NID; id++)
        led_pos[id] = (uint8_t)id;
#else
    uint32_t id, p, r;
    for (id = 0; id < LED_NID; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
#endif
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#if BOARD_SMK37
#define LED_PLAY_GREEN 16u                          /* (SMK) SEQ PLAY's green half, LED 16 (smk_input.h SMK_LEDC) */
#else
#define LED_PLAY_GREEN ((8u << 3) | 1u)
#endif
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_GLO};   /* GLO: mixer + global settings; REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
static uint32_t layer_open(void);
/* the OCT LEDs, bit 0 OCT- lit, bit 1 OCT+ lit, OCT_BREATH OCT+ breathing (can be pressed: dark .. ~60 %,
 * hal/fm1_input.h fm1_led_breath, #119). In the dialogs and on action pages OCT- (back) is lit and OCT+ breathes
 * while it would do something; the menu: on a value row OCT- and OCT+ both breathe while that way has a value to go to,
 * on CALIBRATION / ABOUT OCT+ breathing alone (opens), in ABOUT OCT- lit (back); elsewhere the octave shift */
#define OCT_BREATH 4u
#define OCT_BREATH_DN 8u                                /* OCT- breathing (the menu: the previous value) */
/* (Skiff 1.3, from Felucca 1.2, Discussions #72 / #73) MOMENTARY: LFO held, the knobs' changes are temporary. KNOB
 * 1..4 on HOME and on the sound and FX pages (a value of the selected track or a global sound / FX one, as edit_param
 * sets it; MIXER's LEVEL PAN REVERB MUTE too; not on the action pages, the lists, the steps, AUTOMATION, SONG or a
 * sheet, where the knobs pick) change their value as ever, and each value turned is put back as it was when LFO is
 * let go: hold LFO, sweep the filter, let go, it snaps back ((2.0, D 2A) comes back over MENU > CONTROL > RETURN, a bar
 * by default, as FX's macros and PERFORM's throws do: ui.c mom_poll; the footer says it). OCT+ while LFO is still held
 * KEEPS them (OCT+ breathes while something would go back). LFO's tap (its page) is dropped once any knob turned with
 * it; LFO alone still opens LFO. Temporary turns are not recorded as automation and take no undo; a sound or project
 * load meanwhile puts that track's values (and the global ones) back first (ui.c mom_back). The menu, a dialog, NAME
 * or a layer let them go too. Up to MOM_LFO values at a time ("MOMENTARY FULL" past them: the turn does nothing) */
static int mom_down(void)                              /* LFO held where its turns are momentary */
{
    return ((fm1_in.buttons >> panel.btn[B_LFO]) & 1u) && !ui.menu && !ui.confirm && !name_on() && !ui.layer && !ui.uboot;
}
/* KNOB k turned s with LFO held: 1 = taken (a momentary value, or refused full), 0 = not a momentary value (the turn
 * goes as ever: the column picks, steps, acts) */
static int mom_turn(uint32_t k, int32_t s)
{
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int16_t *vp = 0;
    uint32_t i, g;
    if (ui.home) {
        d = home_param(k, &vp);
    } else if (pg->scope == SC_TRK && !lock_held()) {  /* MIXER: the selected track's LEVEL PAN REVERB MUTE */
        static const uint8_t MIX_ID[4] = {P_LEVEL, P_PAN, P_REV, P_MUTE};   /* (tracks_edit's knobs) */
        vp = &TSEL->p[MIX_ID[k & 3u]];
        d = &TP[MIX_ID[k & 3u]];
    } else {
        if (act_cols() || lock_held() || !(pg->scope == SC_TRACK || pg->scope == SC_ENGINE || pg->scope == SC_GLOBAL) ||
            pg->graph == GR_EVENTS || pg->graph == GR_SONG || pg->graph == GR_BROWSE || pg->graph == GR_USER ||
            pg->graph == GR_BANK || sheet_of(pg->graph) || (pg->graph == GR_SLICES && k < 2u) ||
            (pg->graph == GR_MOD && k == 0u))
            return 0;
        d = page_desc(pg, k, &vp);
    }
    g = vp && vp >= song.g && vp < song.g + G_COUNT ? (uint32_t)(vp - song.g) : G_COUNT;
    if (!d || !vp || d->max == d->min ||
        !((vp >= TSEL->p && vp < TSEL->p + P_COUNT) || (g <= G_CDEPTH && g != G_CLOCK) || g == G_RTYPE ||
          g == G_CMODE || g == G_CFDBK))
        return 0;                                      /* (only the values a sound and the FX are: no CLK, no actions) */
    i = mom_held(0);
    if (!mom_hold(vp, d->fmt == F_ENUM)) {             /* (the first turn of this value: what it was) */
        ui_message("MOMENTARY FULL");
        return 1;
    }
    if (!i)
        ui_message("MOMENTARY");                       /* ((2.0) LET GO and OCT+: KEEP in the footer) */
    *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
    return 1;
}

/* (2.0, D 2A) OCT+ would keep something: MOMENTARY's values (LFO held), FX's macros (its layer open), PERFORM's throws */
static int keep_ready(void)
{
    uint32_t i, l = layer_open();
    for (i = 0; i < mom.n; i++)
        if (mom_fx(&mom.e[i]) ? l == LAYER_FX : mom_down())
            return 1;
    return l == LAYER_PERF && thr_out;
}

static uint32_t oct_leds(void)
{
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (breathing) writes */
        return 1u | OCT_BREATH;
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing ((2.0) */
        return 1u | (keep_ready() ? OCT_BREATH : 0u);   /* PERFORM: OCT+ keeps the throws) */
#if FELUCCA_TAPE
    if(ui.menu==3 && !ui.confirm)
        return OCT_BREATH | (tape_wanted && tape_status_frames ? OCT_BREATH_DN : 0u);
#endif
    if (ui.menu && !ui.confirm)
        return ui.menu >= 2u ? 1u :                    /* (ABOUT: OCT- back, lit) */
               menu_act(ui.menu_sel) ? (ui.menu_sel != MI_LCLEAR || ml_count() ? OCT_BREATH : 0u) :   /* CALIBRATION /
                                                        * ABOUT: OCT+ opens ((1.3) LEARN CLEAR: while CCs are learned) */
               (menu_step(ui.menu_sel, -1) != menu_get(ui.menu_sel) ? OCT_BREATH_DN : 0u) |   /* a value: each */
               (menu_step(ui.menu_sel, 1) != menu_get(ui.menu_sel) ? OCT_BREATH : 0u);         /* way it can go */
    if (ui.confirm || act_cols())
        return 1u | (ui.confirm || act_ready() ? OCT_BREATH : 0u);
    if (ui.ml.on)                                       /* ((1.3) MIDI LEARN: OCT+ done, OCT- with a CC to clear */
        return OCT_BREATH | (ui.ml.pick && ml_cc_of(ui.ml.trk, ui.ml.id) ? 1u : 0u);
    if (keep_ready())                                   /* ((1.3) MOMENTARY: OCT+ keeps the changes; (2.0) FX's macros) */
        return (song.octave < 0 ? 1u : 0u) | OCT_BREATH;
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* the key LEDs on the grid, bit k = key k: the white keys show where the selected lane hits on the page
 * shown (its accents while ACC is held), the step playing inverted (a light walks over them); the black keys
 * the lane selected, ACC while held, the page keys while there is more than one page */
static uint32_t grid_leds(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = 1u << ui.lane, acc = (uint32_t)black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            on = i < len && ((acc ? step_accents(&t->step[i]) : step_lanes(&t->step[i])) & b) != 0u;
            on ^= (uint32_t)(p == ph);
        } else {
            on = p < NLANE ? p == ui.lane : p == GK_ACC ? acc : len > 16u;
        }
        m |= on << k;
    }
    return m;
}

/* the DRUM grid's beats, bit k = key k (ui_leds: a steady brighter glow than the other steps', hal/fm1_input.h
 * fm1_led_mid): the first step of each group of four on the page shown, steps 1 5 9 13 of 16 ("5111 5111 5111 5111"),
 * within LEN, where grid_leds shows no hit (the lane's, its accents while ACC is held) and not the playhead. Always
 * groups of four from the pattern's first step, whatever LEN or DIV (a 3/4 or a DIV 1/8 pattern too): the steps are
 * counted, not the time */
static uint32_t grid_beats(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = 1u << ui.lane, acc = (uint32_t)black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), i = ui.bank * 16u + p;
        if (key_black(k) || p % 4u || i >= len || p == ph)
            continue;
        m |= (uint32_t)(((acc ? step_accents(&t->step[i]) : step_lanes(&t->step[i])) & b) == 0u) << k;
    }
    return m;
}

/* the DRUM grid's keys that do something, bit k = key k (they glow with the idle LEDs, ui_leds): the page's steps
 * within LEN, the lane keys, ACC, the page keys while there is more than one page */
static uint32_t grid_glow(void)
{
    uint32_t k, m = 0, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        m |= (uint32_t)(!key_black(k) ? ui.bank * 16u + p < len : p < NLANE || p == GK_ACC || len > 16u) << k;
    }
    return m;
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}

/* Discussion #81: the keys of the notes MIDI IN (USB and TRS, routed by ROUT) holds on track t, bit k = key k: as
 * play_leds, where the keys play that note at the octave now, the lowest key that gives it; a note no key plays is
 * not shown. Read from midi_control.c's own state (midi_note_held: the notes each channel holds for the track, the
 * pedal's too, and the tones of MIDI chords): no state here; nothing to do while MIDI holds none of the track's */
static uint32_t midi_leds(const track_t *t)
{
    uint32_t k, note, m = 0, seen[4] = {0, 0, 0, 0};
    if (!midi_owners[trk_index(t)])
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        if (note > 127u || ((seen[note >> 5] >> (note & 31u)) & 1u))
            continue;                                   /* (silent, or a lower key gives it) */
        seen[note >> 5] |= 1u << (note & 31u);
        m |= (uint32_t)midi_note_held(t, note) << k;
    }
    return m;
}

/* the keys of the notes the selected track's sequencer and ARP sound now (#38), bit k = key k: where the keys
 * play that note (kb_map: the octave, TRN, QNT, an engine's own map), the lowest key that gives it (QNT SNAP
 * rounds the keys above down onto it); a note no key plays is not shown. A snapshot of the ISR's seq_notes /
 * arp_note (arp_ch): no state of its own, nothing to do while nothing sounds */
static uint32_t play_leds(void)
{
    const track_t *t = TSEL;
    uint8_t s[4 + NLANE + 4];
    uint32_t n = t->seq_n < 4u + NLANE ? t->seq_n : 4u + NLANE, i, k, note, used = 0, m = 0, hit;
    for (i = 0; i < n; i++)
        s[i] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (i = 0; i < 3u; i++)                            /* (the ARP's CHORD) */
        if (t->arp_ch[i])
            s[n++] = t->arp_ch[i];
    for (k = 0; n && k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0, hit = 0; i < n; i++)
            if (s[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                hit = 1;
            }
        m |= hit << k;
    }
    return m | midi_leds(t);
}

/* Discussion #89: on SEQ > NOTES (the piano roll; the DRUM grid has its own map) while stopped, the keys of the
 * notes stored in the cursor step, bit k = key k: as play_leds, where the keys play that note now (kb_map: the
 * octave, TRN, QNT), the lowest key that gives it; a note no key plays is not shown, nor a REST step's. Playing,
 * the keys show what plays (play_leds) instead. Read from the step: no state of its own */
static uint32_t step_leds(void)
{
    const track_t *t = TSEL;
    const step_t *st;
    uint32_t i, k, note, used = 0, m = 0;
    if (ui.home || ui.menu || ui.confirm || !song.seq_mode || !step_page(cur_page()->graph) || song.playing ||
        ui.cursor >= NSTEP)
        return 0;
    st = &t->step[ui.cursor];
    if (!st->n || st->time == ST_REST)
        return 0;
    for (k = 0; k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0; i < st->n && i < 4u; i++)
            if (st->note[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                m |= 1u << k;
            }
    }
    return m;
}

/* 1: the keys show a map of their own (NAME, a layer's map: SCL's scale, FX; the DRUM grid, SLICES), lit or
 * dark; 0: the keys held and the notes playing, over the idle glow */
static int keys_own(void)
{
    return (name_on() && !ui.menu) || ui.layer || grid_on()
#if FELUCCA_SLICE
           || (!ui.menu && !name_on() && slice_page_on())
#endif
        ;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, the keys of the selected track's scale (ROOT, SCALE), bit k = key k, as
 * the keys play now: the note a key gives with TRN (QNT SNAP / SEQ: the key's own note; the others round down onto
 * the scale); QNT WHITE every white key (they walk the scale; the black ones are silent). *root: the keys of the
 * scale's root. None where the engine maps the keys itself (DRUM's GM map, the slices): 0 there, and OFF */
static uint32_t scale_leds(uint32_t *root)
{
    const track_t *t = TSEL;
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    uint32_t k, m = 0, r = 0, mask = scale_mask(t), note;
    int32_t pc;
    *root = 0;
    if (!(ui_rec_prefs & (PREF_SCALE_LEDS >> 8)))
        return 0;
    for (k = 0; k < 27u; k++) {
        if (e->keys && e->keys(t, k) >= 0)
            continue;
        if (t->p[P_QUANT] == QN_WHITE) {
            if ((note = kb_map(t, k)) > 127u)
                continue;                               /* (a black key: silent) */
            pc = (int32_t)note - t->p[P_TRANS];         /* (WHITE walks the scale before TRN) */
        } else {
            pc = 53 + (int32_t)k + t->p[P_TRANS];
        }
        pc = ((pc - t->p[P_ROOT]) % 12 + 12) % 12;
        m |= ((mask >> pc) & 1u) << k;
        r |= (uint32_t)(pc == 0) << k;
    }
    *root = r & m;
    return m;
}

/* the key LEDs, bit k = key k: NAME's keys, the layer's map, the DRUM grid, else the keys held and the notes
 * the selected track's sequencer, ARP and MIDI IN play, on NOTES while stopped the cursor step's notes (#89; and
 * on SLICES the keys of the selected slice) */
static uint32_t key_leds(uint32_t *br)                 /* (*br: a layer's keys that breathe, layer_leds) */
{
    uint32_t c;
    if (br)
        *br = 0;
    c = name_on() && !ui.menu ? name_leds(br) : ui.layer ? layer_leds(br) : grid_on() ? grid_leds() :
                 (fm1_in.notes & ~kb_layer) | play_leds() | step_leds();
#if FELUCCA_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    return c;
}

/* The LEDs: lit = active (the page's family, PLAY / REC running, the keys held or playing, a map's keys), the ARP
 * beat flashes, the ones that can be pressed breathe (#119: the open layer's button, OCT+ when it would act, a
 * layer's keys; dark .. ~60 % of lit and back, hal/fm1_input.h fm1_led_breath, in every mode: never a hard blink in
 * a dark room; DIM LO up to ~30 %, DIM HI OFF INV ~60 %; with LEDS OFF it is the only glow; INV leaves it as it is,
 * between the dark active ones and the lit idle ones), every other button and key glows dim (#35: the
 * buttons of the black FM-1 can be found in the dark; hal/fm1_input.h fm1_led_dim, a short pulse each frame).
 * MENU > LEDS: DIM HI (default) that glow, DIM LO a darker one (fm1_led_dim_level), OFF no glow (as 1.0); INV
 * turns it around, as the stock firmware: the idle ones fully lit, the active ones dark, no glow (the ARP beat:
 * lit / dark). The keys' own maps (keys_own) stay lit or dark in every mode: their dark keys read as dark; only the
 * DRUM grid's keys that do something (grid_glow) glow under it, so STEP on a DRUM track is never a dark
 * keyboard (the steps of an empty pattern); on it the first step of each four (grid_beats: 1 5 9 13 of a page, where
 * no hit shows) glows steadier and brighter, so a bar's beats can be counted: DIM HI / DIM LO the mid level
 * (hal/fm1_input.h fm1_led_mid: lit 1 frame in 8, DIM LO in 12, over the glow), OFF and INV the glow alone (no
 * other key glows there). 1.2, MENU > SCALE LEDS ON (scale_leds; not over a map of the keys' own, nor on a DRUM or
 * SLICE track): the keys of the selected track's scale glow and the others are dark, the root's at the mid level, in
 * DIM HI, DIM LO and OFF alike (asked for: OFF only takes the other glow away); INV turns it around as it does the
 * rest: the root's keys lit, the scale's at the mid level, the others glowing; the keys held and playing as ever
 * (lit, INV dark). Each picture is built off-line and copied one byte per column, the
 * glow first: an LED going from lit to dim never has a dark frame */
#if BOARD_SMK37
/* (SMK-37 Pro, SMK37_UX.md button LEDs A) the picture's button LEDs: on or off only, so lit or breathing = on, the glow
 * and the mid level dark; KNOB BANK lit while it is SHIFT, FADER BANK off the LEVEL bank (smk_ui.c). The keys' picture
 * (LED_KEY0 + k) goes to the pads (smk_ui.c smk_pads_out: no LEDs under the keys) */
static uint8_t smk_fbank;
static void smk_pads_out(const uint8_t *nl, const uint8_t *nd, const uint8_t *nb, const uint8_t *nm);
static void smk_leds_out(const uint8_t *nl, const uint8_t *nd, const uint8_t *nb, const uint8_t *nm)
{
    uint32_t i;
    for (i = 0; i < SMK_NLED; i++)
        fm1_led_key(i, (int)(((nl[i >> 3] | nb[i >> 3]) >> (i & 7u)) & 1u));
    fm1_led_key(SMK_BTN_KNOB_BANK, (int)((fm1_in.buttons >> SMK_BTN_KNOB_BANK) & 1u));
    fm1_led_key(SMK_BTN_FADER_BANK, smk_fbank != 0u);
    smk_pads_out(nl, nd, nb, nm);
}
#endif
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, og[FM1_NCOL] = {0}, nb[FM1_NCOL] = {0};
    uint8_t nm[FM1_NCOL] = {0}, sg[FM1_NCOL] = {0}, sm[FM1_NCOL] = {0};
    uint32_t k, c, g, br, beat, sc, sr = 0;
    uint32_t fam = cur_fam(), mode = settings_leds;
    int keys_map = keys_own();
    static uint8_t ready;
    if (fm1_led_anim_on())                              /* the power-on sweep has them (main.c boot_leds) */
        return;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (!ui.layer && (perf_latched || perf_k[0] || perf_k[1] || perf_k[2] || perf_k[3]))
        led_put(nl, panel.btn[B_FX], 1);                /* FX LATCH: lit while an effect or a macro is on */
    if (ui.layer)                                       /* the layer's button breathes while its map is up */
        led_put(nb, panel.btn[layer_btn()], 1);
    led_put(nl, panel.btn[B_REC], song.rec != 0u && ui.layer != LAYER_REC);   /* (its layer: it breathes) */
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)((k >> 1) & 1u));
    led_put(nb, panel.btn[B_OCTUP], (int)(k & OCT_BREATH));
    led_put(nb, panel.btn[B_OCTDN], (k & OCT_BREATH_DN) != 0u);
    c = key_leds(&br);
    g = !keys_map ? 0u : grid_on() && !ui.layer && !name_on() ? grid_glow() : 0u;   /* (key_leds: the grid's map) */
    beat = g ? grid_beats() & ~c : 0u;                  /* (the grid's empty beat starts: brighter than the glow) */
    sc = keys_map ? 0u : scale_leds(&sr);               /* SCALE LEDS: the scale's keys glow, the root's brighter */
    for (k = 0; k < 27u; k++) {
        uint32_t in = (sc >> k) & 1u, rt = (sr >> k) & 1u, idle = !((c >> k) & 1u);
        led_put(nm, LED_KEY0 + k, (int)((beat >> k) & 1u));
        led_put(keys_map ? own : nl, LED_KEY0 + k, (int)((c >> k) & 1u));
        led_put(keys_map ? og : nd, LED_KEY0 + k, keys_map ? (int)((g >> k) & 1u) : !sc || (mode == LEDS_INV && rt));
        led_put(nb, LED_KEY0 + k, (int)((br >> k) & 1u));
        if (!sc)
            continue;
        led_put(sg, LED_KEY0 + k, mode == LEDS_INV ? !in && idle : (int)in);   /* (INV: the active ones dark) */
        led_put(sm, LED_KEY0 + k, mode == LEDS_INV ? in && !rt && idle : (int)rt);
    }
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    if (song.playing || seq_counting())                 /* playing: PLAY's green, its own LED dark in every mode */
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++) {
        if (mode == LEDS_INV)                           /* INV: the active ones dark, the rest lit */
            nl[c] = (uint8_t)(nd[c] & ~nl[c]);
        nd[c] = mode == LEDS_DIM || mode == LEDS_DIM_LO ? (uint8_t)(nd[c] | og[c]) : nm[c];   /* OFF, INV: no glow but */
        if (mode != LEDS_DIM && mode != LEDS_DIM_LO)    /* the grid's beats (the one level above dark there); DIM HI */
            nm[c] = 0;                                  /* / LO: the beats at the mid level over the glow */
        nd[c] |= sg[c];                                 /* SCALE LEDS: in every mode (INV: inverted above) */
        nm[c] |= sm[c];
        nm[c] &= (uint8_t)~(nb[c] | own[c]);
        nl[c] |= own[c];
        nl[c] &= (uint8_t)~nb[c];                       /* breathing: neither lit (INV's idle) nor the glow */
        nd[c] &= (uint8_t)~nb[c];
    }
    if (song.playing || (seq_counting() && cin_pos < click_beat_len() / 4u))   /* counting in: green on each beat */
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
#if BOARD_SMK37
    smk_leds_out(nl, nd, nb, nm);
#else
    fm1_led_dim_level(mode == LEDS_DIM_LO);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led_dim[c] = nd[c];
        fm1_led_breath[c] = nb[c];
        fm1_led_mid[c] = nm[c];
    }
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
#endif
}

/* ---------------------------------------------------------- input --- */
/* Predictable hardware response (#23): each decoded detent is one value step; a fast turn keeps its full signed
 * detent count. MENU > KNOB ACCEL ON (#52, OFF by default) multiplies a fast turn of a wide value (range > 32, not a
 * list of names; the FX and GLO layers' knobs too, #126) by 2..4, by up to 8 over a range above 64. The main loop
 * reads the knobs many times a frame (main.c), so a read holds one detent as a rule: the speed is the time per
 * detent, ACC_RATE / ms -> 25 ms x2, 16 ms x3, 12 ms x4, 10 ms x5 .. 6 ms or less x8 (a flick: 12 detents in 120 ms
 * move a -100..100 FX macro ~50). Only the longer of this read's and the previous read's time counts, and only while
 * the turn goes on (both under ACC_GAP ms) in one direction: a slow turn, the first two detents of a turn, a single
 * quick detent (a bounce) and a reversal are one step per detent, and the sign is always the detents'.
 * ui.enc_t[role]: bits 0..23 the ms of its last read, bit 24 its direction (+1), 25..31 its ms per detent (127 slow) */
#define ACC_GAP 40u
#define ACC_RATE 50u
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ms & 0xFFFFFFu, st = ui.enc_t[role], up = s > 0, pi = st >> 25, a, i, m = 1;
    if (!(ui_prefs & PREF_ACCEL) || range <= 32 || !s)
        return s;
    a = (uint32_t)(s < 0 ? -s : s);
    i = ((now - st) & 0xFFFFFFu) / a;                   /* ms per detent of this read */
    if (!st || ((st >> 24) & 1u) != up || i >= ACC_GAP)
        i = 127u;                                       /* a new turn, or reversed */
    else if (pi < ACC_GAP) {
        m = ACC_RATE / (i > pi ? i : pi ? pi : 1u);
        a = range > 64 ? 8u : 4u;                       /* (the cap) */
        m = m < 1u ? 1u : m > a ? a : m;
    }
    ui.enc_t[role] = now | up << 24 | (i ? i : 1u) << 25;
    return s * (int32_t)m;
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 REV send, 4 MUTE of the selected track (right = ON, left = OFF: the
 * track itself is ALGORITHM's, on every page). Pattern length stays on SEQ. */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 3:
        t->p[P_MUTE] = (int16_t)(steps > 0);
        ml_knob(t, P_MUTE);                             /* ((1.3) MIDI LEARN: picked) */
        return;
    case 0:
        vp = &t->p[P_LEVEL];
        d = &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_REV];
        d = &TP[P_REV];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    motion_capture(t, (uint32_t)(vp - t->p), *vp);
    ml_knob(t, (uint32_t)(vp - t->p));
}

/* SEQ > NOTES' piano roll (not the DRUM grid, whose white keys stay its steps): where step recording writes */
static int roll_on(void)
{
    return !ui.home && !ui.menu && !ui.confirm && song.seq_mode && step_page(cur_page()->graph) && !drum_track(TSEL);
}

/* REC tap on every page: arm / disarm recording on the selected track without navigating; arming while stopped
 * starts the transport too (live recording), except on NOTES' piano roll (1.2, Discussion #133): there it arms step
 * recording and the transport stays stopped: the keys write the cursor step (seq_entry), PLAY then records live.
 * Armed and playing, STEP's keys record live (at the play head) instead of writing the cursor step.
 * (1.4.1) Several tracks can be armed (MIDI IN records each on its channel); REC on an armed track disarms them all,
 * as its LED (lit while any is armed) says: one track armed, another selected, REC no longer looked stuck */
static void rec_pass_undo(int armed);
static int rec_tap(void)                                /* 1: the arming changed */
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    song.rec = (uint8_t)(song.rec & bit ? 0u : song.rec | bit);
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    rec_pass_undo((song.rec & bit) != 0u);            /* ((Skiff U2) what this arming records: one undo) */
    if ((song.rec & bit) && !song.playing && !seq_counting() && roll_on()) {
        ui_message("KEYS WRITE STEPS");               /* step recording: stopped, at the cursor */
        return 1;
    }
    if ((song.rec & bit) && !song.playing && !seq_counting()) {
        chain_top();                                  /* ((2.0, #44) a chain from its first part) */
        transport_req = 1;
    }
    if ((song.rec & bit) && grid_on())
        ui_message("KEYS RECORD LANES");              /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && step_page(cur_page()->graph))
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
    return 1;
}

/* PLAY: start or stop (a count-in: stop); GLO held: RESTART. (Skiff U1) on every screen, the menu and a dialog too */
static int layer_play(void);
static void play_press(void)
{
    if (layer_play())
        return;
    if (lists.song == SONG_ONCE && !song.playing && !seq_counting()) {
        song_top();                                     /* ((2.0, #47) SONG ONCE: from every track's first entry) */
    } else if (song.playing || seq_counting()) {
        transport_req = 2;
    } else {
        chain_top();                                    /* ((2.0, #44) a chain from its first part) */
        transport_req = 1;
    }
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void)
{
    return ((song.rec >> song.sel) & 1u) && (song.playing || transport_req == 1u || seq_counting());
}

/* (Skiff phase 5) SEQ > SONG's EDIT: the entry picked cleared, after its dialog (bank.c song_clear; stopped: it may
 * erase a pattern, empty the track); (#28) a REST at once (nothing goes with it) */
static void song_edit(void)
{
    uint32_t k = song.sel % NTRK, e = sg.entry % NENT;
    char b[8] = {'T', (char)('1' + k), ' ', (char)('A' + e), 0};
    if (ent_rest(k, e))
        (void)song_clear_at(k, e);
    else if (ent_pat(k, e) == PAT_NONE)
        ui_say(b, " EMPTY");
    else if (transport_busy())
        ui_message("STOP TO CLEAR");
    else
        confirm_open(CF_CLEAR_ENTRY, e);
}

/* (Skiff 1.3, from Felucca 1.2, Discussion #169) MENU > CONTROL > STEP PREVIEW ON: on SEQ's NOTES (and the DRUM grid)
 * with the transport stopped, a move of the cursor by the PRESETS knob ((2.0) KNOB 1 too before) or the grid's page
 * keys sounds the step it lands on once, on the selected track (seq.c seq_audition). Not while playing, counting in or in a song; step
 * recording moving on after its keys does not; a grid key on a step neither (it edits the step, a lane key plays the
 * lane). OFF (the default): the cursor moves silently, as before */
static void cursor_audition(void)
{
    if (PREVIEW_SET && !song.playing && !seq_counting() && transport_req != 1u && !ui.home && song.seq_mode &&
        step_page(cur_page()->graph))
        aud_req = (uint16_t)((uint32_t)ui.cursor << 8 | (song.sel + 1u));
}
/* the grid's page down (-1) / up (+1): the cursor to the same place on it (at most the last step) */
static void page_go(int32_t d)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], pages = (len + 15u) / 16u, b;
    int32_t nb = (int32_t)ui.bank + d, c;
    if (nb < 0 || nb >= (int32_t)pages) {                 /* ((2.0, #44) stopped on a chain: the next part's first page,
                                                         * the one before's last) */
        c = chain_cursor(d < 0 ? -1 : (int32_t)len);
        if (c >= 0 && c < TSEL->p[P_SLEN]) {
            len = (uint32_t)TSEL->p[P_SLEN];
            pages = (len + 15u) / 16u;
            nb = d < 0 ? (int32_t)pages - 1 : 0;
        } else if (pages < 2u)
            return;
    }
    b = (uint32_t)(nb + (int32_t)pages) % pages;
    cursor_set((int32_t)(b * 16u + ui.cursor % 16u < len ? b * 16u + ui.cursor % 16u : len - 1u));
    cursor_audition();                                  /* ((1.3) STEP PREVIEW) */
}

/* ------------------------------------------------ (Skiff 2.0, D) NOTES and FEEL --- */
/* The user's step editing (design/mockups/v2_notes.png): the knobs edit the selection (ui.c nsel) as a unit. A run of
 * turns of one knob is one undo, SAVE held puts it back (as STEP CLEARED's runs) */
static void tl_undo_take(track_t *t, const char *name);
static const char SN_PITCH[] = "PITCH", SN_MOVE[] = "MOVE", SN_LEN[] = "LENGTH", SN_VEL[] = "VELOCITY",
                  SN_HIT[] = "HITS", SN_PASTE[] = "PASTE", SN_DEL[] = "DELETE";
static const char *const SN_FEEL[4] = {"CHANCE", "RATCHET", "NUDGE", "TYPE"};
static void run_undo(track_t *t, const char *name)
{
    if (!(undo_n && !undo_back && undo.name == name && undo.trk == trk_index(t) + 1u && track_sig(t) == undo.after &&
          fm1_ms - undo.t_ms < 1500u))
        tl_undo_take(t, name);
}
static void run_done(track_t *t)
{
    undo.after = track_sig(t);
    undo.t_ms = fm1_ms;
}
static int step_empty(const step_t *s) { return s->time != ST_TIE && !s->n && !s->hit; }
static void sel_taken(uint32_t i)                         /* step i (0 based) in the way, past LEN: the end */
{
    char b[20];
    if (i >= (uint32_t)TSEL->p[P_SLEN] || i >= NSTEP) {
        ui_message("PATTERN END");
        return;
    }
    str_cpy(b, "STEP ", sizeof b);
    fmt_int(b + 5, (int32_t)i + 1);
    str_cpy(b + str_len(b), " TAKEN", 8);
    ui_message(b);
}

/* (2.0, #25) NOTES' edits note by note (design/TIER_B.md), on a copy of the track's steps 1..LEN unfolded (ed_open):
 * every TIE as the notes it holds, each tied, so a note is its start and one entry on each step it goes on, whatever
 * else is there. Folded back after (ed_close: a step not changed keeps its bytes; a changed one where the notes
 * sounding before all go on, and nothing else: a TIE again) and written back at once, the audio IRQ off. The copy is
 * on the main loop's stack (832 B). A TIE on step 1 (the loop's end's) stays as it is */
/* (ed_cont, ed_unfold, ed_del, ed_add: ui.c, (2.0, #44) bank.c's chain tails use them too) */
static void ed_open(step_t *w, const track_t *t, uint32_t len)
{
    uint32_t i;
    for (i = 0; i < len; i++)
        ed_unfold(&w[i], &t->step[i], i ? &w[i - 1u] : 0);
}
/* the last step the note of pitch p at step i sounds on */
static uint32_t ed_end(const step_t *w, uint32_t len, uint32_t i, uint32_t p)
{
    int32_t k;
    while (i + 1u < len && (k = note_at(&w[i + 1u], p)) >= 0 && step_tied(&w[i + 1u], (uint32_t)k))
        i++;
    return i;
}
/* the note of pitch p at step i out, with the steps it goes on: how many steps it was */
static uint32_t ed_lift(step_t *w, uint32_t len, uint32_t i, uint32_t p)
{
    uint32_t e = ed_end(w, len, i, p), j;
    for (j = i; j <= e; j++)
        ed_del(&w[j], (uint32_t)note_at(&w[j], p));
    return e - i + 1u;
}
/* a note of pitch p in at step i, n steps: 0, or where it stopped (step + 1: that one can't take it) */
static uint32_t ed_place(step_t *w, uint32_t len, uint32_t i, uint32_t p, uint32_t n)
{
    uint32_t j;
    for (j = 0; j < n; j++)
        if (i + j >= len || !ed_add(&w[i + j], p, j != 0u))
            return i + j + 1u;
    return 0;
}
/* step s's own values (velocity, accent, slide, RATCH, FLAM, CHANCE, condition, NUDGE; a REST) to d */
static void ed_vals(step_t *d, const step_t *s)
{
    d->time = s->time == ST_REST ? ST_REST : ST_NOTE;
    d->flags = s->flags;
    d->vel = s->vel;
    d->probability = s->probability;
    d->cond = s->cond;
    d->nudge = s->nudge;
}
/* the copy folded back into t's steps 1..len and written at once; the steps in keep (bit i) stay unfolded (step entry
 * goes on writing them). A step that can be a TIE is one (a pattern left without overlaps: as 1.4 has it) */
static void ed_close(track_t *t, step_t *w, uint32_t len, uint64_t keep)
{
    step_t o, po, pu, cur, last = w[len ? len - 1u : 0u];
    uint32_t i, k, m, held, same;
    for (i = 0; i < len; i++) {
        cur = w[i];
        ed_unfold(&o, &t->step[i], i ? &po : 0);
        for (held = i > 0u && !cur.hit && cur.time == ST_NOTE, m = held ? ed_cont(&pu) : 0u, k = 0; held && k < cur.n; k++)
            held = step_tied(&cur, k) && note_at(&pu, cur.note[k]) >= 0 && ((m >> note_at(&pu, cur.note[k])) & 1u);
        held = held && (uint32_t)__builtin_popcount(m) == cur.n && !((keep >> i) & 1u);   /* (what goes on: just what
                                                       * sounded before) */
        same = !memcmp(&cur, &o, sizeof o);
        if (same && t->step[i].time == ST_TIE && held)
            w[i] = t->step[i];                          /* (a TIE not changed: its bytes) */
        else if (held && cur.n) {
            w[i].time = ST_TIE;                         /* (its notes' bytes 0: a TIE ignores them) */
            w[i].n = w[i].acc = 0;
            memset(w[i].note, 0, sizeof w[i].note);
        } else if (same && t->step[i].time != ST_TIE)
            w[i] = t->step[i];                          /* (not changed: its bytes) */
        po = o;
        pu = cur;
    }
    fm1_irq_off();                                      /* the audio ISR must not see half a pattern */
    memcpy(t->step, w, len * sizeof *w);
    fm1_irq_on();
    if (len)                                            /* ((2.0, #44) a note no longer at the part's end: its tail */
        (void)chain_tails(trk_index(t), &last, 0, 0, 0, 0);   /* in a chain's next part goes) */
}
/* (2.0, #44) .. as ed_close's after an edit not through the copy (EDIT's STEP CLEARED): t's last step unfolded */
static void chain_sync(const track_t *t)
{
    uint32_t len = (uint32_t)t->p[P_SLEN], b;
    step_t last;
    if (!len)
        return;
    for (b = len - 1u; b && t->step[b].time == ST_TIE; b--)
        ;
    ed_unfold(&last, &t->step[len - 1u], b < len - 1u ? &t->step[b] : 0);
    (void)chain_tails(trk_index(t), &last, 0, 0, 0, 0);
}

/* note n moved d steps along the track's SCALE (CHROMATIC: semitones); -1: off the keyboard */
static int32_t scale_step(const track_t *t, int32_t n, int32_t d)
{
    uint32_t mask = scale_mask(t);
    int32_t u = d > 0 ? 1 : -1;
    for (; d && n >= 0 && n <= 127; d -= u)
        do
            n += u;
        while (n >= 0 && n <= 127 && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u));
    return n >= 1 && n <= 127 ? n : -1;
}
/* the notes selected (picked; none picked: every one starting in the selection), marked on copy w with the steps they go
 * on (mv[j] bit k: w[j]'s note k is one of them); 1: some */
static int ed_mark(const track_t *t, const step_t *w, uint32_t len, uint8_t *mv, int whole)
{
    uint32_t i, j, k, e, any = 0;
    memset(mv, 0, NSTEP);
    for (i = sel_lo(); i <= sel_hi() && i < len; i++)
        for (k = 0; sel_step(t, i) && k < w[i].n; k++)
            if ((whole || sel_voice(i, k)) && !step_tied(&w[i], k))
                for (any = 1, e = ed_end(w, len, i, w[i].note[k]), j = i; j <= e; j++)
                    mv[j] |= (uint8_t)(1u << note_at(&w[j], w[i].note[k]));
    return (int)any;
}
/* PITCH: every note selected d steps along the scale, with the steps it goes on (one off the keyboard: none moves; onto
 * a note that stays there: STEP n TAKEN); nothing selected: nothing written (notes go in with REC or a paste) */
static void sel_pitch(track_t *t, int32_t d)
{
    step_t w[NSTEP];
    uint8_t mv[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], j, k, x;
    int32_t q;
    ed_open(w, t, len);
    if (!ed_mark(t, w, len, mv, 0))
        return;
    for (j = 0; j < len; j++)
        for (k = 0; k < w[j].n; k++) {
            if (!((mv[j] >> k) & 1u))
                continue;
            if ((q = scale_step(t, w[j].note[k], d)) < 0)
                return;
            for (x = 0; x < w[j].n; x++)
                if (!((mv[j] >> x) & 1u) && w[j].note[x] == (uint32_t)q) {
                    sel_taken(j);
                    return;
                }
        }
    run_undo(t, SN_PITCH);
    {   /* ((2.0, #44) a note going on into a chain's next part: its tail moves with it) */
        uint8_t o[4], q[4], n[4] = {CT_SAME, CT_SAME, CT_SAME, CT_SAME}, c = 0;
        for (k = 0; len && k < w[len - 1u].n && c < 4u; k++)
            if ((mv[len - 1u] >> k) & 1u) {
                o[c] = w[len - 1u].note[k];
                q[c++] = (uint8_t)scale_step(t, w[len - 1u].note[k], d);
            }
        if (c)
            (void)chain_tails(trk_index(t), 0, c, o, q, n);
    }
    for (j = 0; j < len; j++)
        for (k = 0; k < w[j].n; k++)
            if ((mv[j] >> k) & 1u)
                last_note = w[j].note[k] = (uint8_t)scale_step(t, w[j].note[k], d);
    ed_close(t, w, len, 0);
    run_done(t);
}

/* steps x0 .. x1 of t one round, u > 0: each a step later (x1's to x0), their automation and locks along */
static void steps_rotate(track_t *t, uint32_t x0, uint32_t x1, int32_t u)
{
    step_t keep;
    uint32_t j, k = trk_index(t), f;
    if (u > 0) {
        keep = t->step[x1];
        for (j = x1; j > x0; j--)
            t->step[j] = t->step[j - 1u];
        t->step[x0] = keep;
    } else {
        keep = t->step[x0];
        for (j = x0; j < x1; j++)
            t->step[j] = t->step[j + 1u];
        t->step[x1] = keep;
    }
    f = motion_guard();
    for (j = 0; j < motion.count; j++) {
        motion_event_t *e = &motion.event[j];
        uint32_t p = e->place & 63u;
        if ((e->place >> 6) == k && p >= x0 && p <= x1)
            e->place = (uint16_t)(k << 6 | (u > 0 ? (p == x1 ? x0 : p + 1u) : (p == x0 ? x1 : p - 1u)));
    }
    motion_unguard(f);
}
/* nothing picked, or every chord of the range picked whole: the selection moves as whole steps */
static int sel_whole(const track_t *t)
{
    uint32_t i;
    for (i = sel_lo(); nsel.picked && i <= sel_hi(); i++) {
        uint32_t m = ((1u << t->step[i].n) - 1u) & ~step_ties(&t->step[i]);
        if (step_starts(&t->step[i]) && (nsel.vp[i] & m) != m)
            return 0;
    }
    return 1;
}
/* (2.0, #25) step i's automation and locks to step d (one at d of the same parameter goes) */
static void motion_step_move(track_t *t, uint32_t i, uint32_t d)
{
    uint32_t f = motion_guard(), k = trk_index(t), pi = k << 6 | i, pd = k << 6 | d, x, y, n = 0;
    motion_restore(t);
    for (x = 0; x < motion.count; x++)                  /* (the one moving there takes its place; 511: no track's) */
        for (y = 0; motion.event[x].place == pd && y < motion.count; y++)
            if (motion.event[y].place == pi && MOTION_ID(&motion.event[y]) == MOTION_ID(&motion.event[x]))
                motion.event[x].place = 511u;
    for (x = 0; x < motion.count; x++) {
        motion_event_t e = motion.event[x];
        if (e.place == 511u)
            continue;
        if (e.place == pi)
            e.place = (uint16_t)pd;
        motion.event[n++] = e;
    }
    memset(motion.event + n, 0, (MOTION_MAX - n) * sizeof motion.event[0]);
    motion.count = (uint8_t)n;
    motion_unguard(f);
}
/* START, a step (u: +1 later, -1 earlier). Whole steps (nothing picked, or whole chords): their notes, each as long as it
 * is, with the step's values, locks and automation; as before 2.0 (the steps from the first note to the last one's end
 * round, over the empty step beside them) when nothing else is on them. (2.0, #25) Else note by note, each into what is
 * there (notes going on there: the moved ones go with them; 4 a step, no pitch twice); a step where another note
 * starts stops whole steps. Picked notes of chords: each into the chord a step on or onto a step where none starts (its
 * values from where it came). Anything in the way: nothing moves, said. The selection and its picks go along */
static void sel_start(track_t *t, int32_t u)
{
    step_t w[NSTEP];
    uint8_t mv[NSTEP], nvp[NSTEP], ps[4];
    uint64_t moved = 0;
    uint32_t len = (uint32_t)t->p[P_SLEN], hi = sel_hi() < len ? sel_hi() : len - 1u, b0 = NSTEP, b1 = 0, i, j, k, np,
             whole = (uint32_t)sel_whole(t), clean = 1;
    ed_open(w, t, len);
    if (!ed_mark(t, w, len, mv, (int)whole))
        return;
    for (j = 0; j < len; j++)
        if (mv[j]) {
            b0 = j < b0 ? j : b0;
            b1 = j;
        }
    for (j = b0; j <= b1; j++)                          /* (on its steps nothing but its notes; hits on its own) */
        clean &= mv[j] == (1u << w[j].n) - 1u && (!w[j].hit || sel_step(t, j));
    memset(nvp, 0, sizeof nvp);
    j = u > 0 ? b1 + 1u : b0 - 1u;                    /* (b0 0, earlier: j wraps past LEN) */
    if (whole && clean && j < len && step_empty(&t->step[j])) {
        run_undo(t, SN_MOVE);
        steps_rotate(t, u > 0 ? b0 : j, u > 0 ? j : b1, u);
        for (i = b0; nsel.picked && i <= b1; i++)       /* (the picks along) */
            nvp[i + (uint32_t)u] = nsel.vp[i];
    } else {
        for (i = u > 0 ? hi : b0; i >= b0 && i <= hi; i = (uint32_t)((int32_t)i - u)) {   /* (the far end first: what
                                                       * arrives is past what leaves) */
            step_t from = w[i];
            uint32_t d = i + (uint32_t)u, had;
            for (np = 0, k = 0; k < w[i].n; k++)
                if (((mv[i] >> k) & 1u) && !step_tied(&w[i], k))
                    ps[np++] = w[i].note[k];
            if (!np)
                continue;
            had = d < len ? (uint32_t)step_starts(&w[d]) : 0u;
            if (d >= len || (whole && had) || from.hit) {   /* (another note starts there; hits move whole, clean) */
                sel_taken(d);
                return;
            }
            for (k = 0; k < np; k++) {
                uint32_t f = ed_place(w, len, d, ps[k], ed_lift(w, len, i, ps[k]));
                if (f) {
                    sel_taken(f - 1u);
                    return;
                }
                if (nsel.picked)
                    nvp[d] |= (uint8_t)(1u << note_at(&w[d], ps[k]));
            }
            if (whole || !had)
                ed_vals(&w[d], &from);
            moved |= (uint64_t)1 << i;                  /* (what is left at i: notes going on only, no values: ed_del) */
        }
        run_undo(t, SN_MOVE);
        ed_close(t, w, len, 0);
        for (i = u > 0 ? hi : b0; whole && i >= b0 && i <= hi; i = (uint32_t)((int32_t)i - u))
            if ((moved >> i) & 1u)
                motion_step_move(t, i, i + (uint32_t)u);   /* (its automation and locks along) */
    }
    run_done(t);
    if (nsel.picked)
        memcpy(nsel.vp, nvp, sizeof nvp);
    ui.cursor = (uint8_t)clamp((int32_t)ui.cursor + u, 0, (int32_t)len - 1);   /* (the selection along) */
    if (nsel.a1)
        nsel.a1 = (uint8_t)clamp((int32_t)nsel.a1 + u, 1, (int32_t)len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
}

/* the notes starting at i in mask m (bit k: note k; 0: every one) c long (ui.c len_get): n steps each, going on into
 * what is there ((2.0, #25) 4 notes a step, no pitch twice, not onto hits; before: empty steps only), a GATE of its
 * own a lock (the step's: the track's, none). Where one can't go on: that step's index + 1 back (it ends before it),
 * 0 = every one */
static uint32_t len_set(track_t *t, uint32_t i, uint32_t m, int32_t c)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], n = c >= LEN_STEPS ? (uint32_t)(c - LEN_STEPS) + 2u : 1u, k, j, e, p, in = 0;
    uint8_t to[4], tn[4], cnt = 0;                       /* ((2.0, #44) tails in a chain's next part) */
    ed_open(w, t, len);
    for (k = 0; k < w[i].n; k++) {
        if (step_tied(&w[i], k) || (m && !((m >> k) & 1u)))
            continue;
        for (p = w[i].note[k], e = ed_end(w, len, i, p), j = i + 1u; j <= e; j++)
            ed_del(&w[j], (uint32_t)note_at(&w[j], p));   /* (its steps after the start out: (ed_del) not w[i]'s order) */
        for (j = 1; j < n && i + j < len; j++)
            if (!ed_add(&w[i + j], p, 1)) {
                in = in ? in : i + j + 1u;
                break;
            }
        if ((e + 1u == len || i + j == len) && cnt < 4u) {   /* (at the part's end before or now: its tail, the rest */
            to[cnt] = (uint8_t)p;                            /* of it, or none) */
            tn[cnt++] = (uint8_t)(i + j == len ? n - j : 0u);
        }
    }
    for (k = 0; k < cnt; k++)                                /* (no next part, no room there: the pattern's end) */
        if (chain_tails(trk_index(t), 0, 1, &to[k], &to[k], &tn[k]) && tn[k])
            in = in ? in : len + 1u;
    ed_close(t, w, len, 0);
    if (c >= LEN_STEPS || c == motion_base_value(t, P_SGATE))
        motion_delete_event(t, i, P_SGATE);
    else
        (void)motion_set_lock(t, i, P_SGATE, (int16_t)c);   /* (full: ui_notices says so) */
    return in;
}
/* LENGTH: every selected note as long as the first is turned to: 1 .. 100 % of its step (its GATE), then 2 .. 128
 * STEPS ((2.0.2, #58) 16 before: REC tied further; past its pattern's end a note goes on into a chain's next part,
 * bank.c chain_tails, never past that part), a step a detent (#31's long notes on a knob) */
static void sel_length(track_t *t, int32_t steps)
{
    uint32_t i, f = sel_first(t), in = 0;
    int32_t c;
    if (f == NSTEP)
        return;
    c = len_get(t, f);
    if (c < LEN_STEPS) {                                  /* (the GATE: at 100 % first, the next detent 2 STEPS; */
        int32_t g = c + accel(EN_K3, steps, 126);         /* back: at 2 STEPS first) */
        c = g < LEN_STEPS ? g : c == LEN_STEPS - 1 ? LEN_STEPS : LEN_STEPS - 1;
    } else {
        int32_t g = c + steps;
        c = g >= LEN_STEPS ? g : c == LEN_STEPS ? LEN_STEPS - 1 : LEN_STEPS;
    }
    c = clamp(c, 1, LEN_STEPS + 2 * (int32_t)NSTEP - 2);
    run_undo(t, SN_LEN);
    for (i = sel_lo(); i <= sel_hi(); i++)              /* ((2.0) the picked notes alone: their own length) */
        if (sel_step(t, i) && (f = len_set(t, i, nsel.picked ? nsel.vp[i] : 0u, c)) != 0u && !in)
            in = f;
    run_done(t);
    if (in)
        sel_taken(in - 1u);
}
/* VELOCITY: every selected step's by the same amount (0, a hit's or an old step's: seq.c's 96) */
static void sel_velocity(track_t *t, int32_t d)
{
    uint32_t i;
    if (sel_first(t) == NSTEP)
        return;
    run_undo(t, SN_VEL);
    for (i = sel_lo(); i <= sel_hi(); i++)
        if (sel_step(t, i))
            t->step[i].vel = (uint8_t)clamp((int32_t)step_vel(&t->step[i]) + d, 1, 127);
    run_done(t);
}

/* FEEL's KNOB k of step s to v (ui.c feel_get) */
static void feel_set(step_t *s, uint32_t k, int32_t v)
{
    if (k == 0u) {
        s->time = v ? ST_NOTE : ST_REST;
        if (v) {
            step_set_chance(s, v > 100 ? 100u : (uint32_t)v);
            s->cond = (uint8_t)(v > 100 ? v - 100 : 0);
        }
    } else if (k == 1u) {
        step_set_rflam(s, (uint32_t)v);
    } else if (k == 2u) {
        s->nudge = (int8_t)v;
    } else {
        s->flags = (uint8_t)((s->flags & ~(SF_ACCENT | SF_SLIDE)) | (v & 1 ? SF_ACCENT : 0u) | (v & 2 ? SF_SLIDE : 0u));
    }
}
/* FEEL's KNOB k: every selected step to the first one's value turned (the tiles show this knob's values after) */
static void feel_turn(track_t *t, uint32_t k, int32_t steps)
{
    static const int8_t LO[4] = {0, 1, NUDGE_MIN, 0}, HI[4] = {100 + TC_COUNT - 1, 5, NUDGE_MAX, 3};
    uint32_t i, f = sel_first(t);
    int32_t v;
    nsel.view = (uint8_t)k;
    if (f == NSTEP)
        return;
    v = feel_get(&t->step[f], k) + (k == 0u ? accel(EN_K1, steps, 110) : k == 2u ? accel(EN_K3, steps, 63) :
                                    k == 1u ? steps : steps > 0 ? 1 : -1);   /* (TYPE: one a turn, as FLAG) */
    v = clamp(v, LO[k], HI[k]);
    run_undo(t, SN_FEEL[k]);
    for (i = sel_lo(); i <= sel_hi(); i++)
        if (sel_step(t, i))
            feel_set(&t->step[i], k, v);
    run_done(t);
}

/* REC off on NOTES: a key's note picked in every selected step that has it (again: dropped); 1: a note was */
static int sel_keys(uint32_t pressed)
{
    const track_t *t = TSEL;
    uint32_t k, i, v, hit = 0;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u) || (note = kb_map(t, k)) == KB_SILENT)
            continue;
        for (i = sel_lo(); i <= sel_hi(); i++)
            for (v = 0; step_starts(&t->step[i]) && v < t->step[i].n; v++)
                if (t->step[i].note[v] == note && !step_tied(&t->step[i], v)) {   /* ((2.0) a note going on: */
                    nsel.vp[i] ^= (uint8_t)(1u << v);
                    hit = 1;
                }
    }
    for (nsel.picked = 0, i = sel_lo(); i <= sel_hi(); i++)
        nsel.picked |= nsel.vp[i];
    return (int)hit;
}

/* the grid's knobs ((2.0) PRESETS the cursor): 1 LANE, then the selected steps' 2 HIT and 3 ACC of the lane (right
 * on, left off; every step of the range, empty ones too) and 4 VELOCITY */
static void grid_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    uint32_t i;
    if (slot == 0u) {
        ui.lane = (uint8_t)clamp((int32_t)ui.lane + (steps > 0 ? 1 : -1), 0, NLANE - 1);
        return;
    }
    if (slot == 3u) {
        sel_velocity(t, accel(EN_K4, steps, 126));
        return;
    }
    run_undo(t, SN_HIT);
    for (i = sel_lo(); i <= sel_hi(); i++) {
        if (slot == 1u)
            grid_hit(t, i, ui.lane, steps > 0);
        else
            grid_acc(t, i, ui.lane, steps > 0);
    }
    run_done(t);
}

/* SHIFT + OCT- on NOTES or FEEL: the selected notes copied (picked: those; none: every one starting in it, with its
 * step's hits), each as long as it is, from the first one's step (two bars at most: longer ones cut), their steps'
 * values along; kept unfolded (a start, its tied steps). (2.0) On NOTES' DRUM grid: the LANE card's lane alone, its
 * hits and their accents, from its first hit; SHIFT + OCT- again on the same steps and lane: every lane (again: the
 * lane). SHIFT + OCT+: pasted at the cursor into what is there (the notes into the chords and the notes going on there:
 * 4 a step, no pitch twice, no tie onto hits; a step with no start of its own takes the copied one's values; a lane's
 * hits onto the grid's LANE); anything in the way: nothing pasted, said. One undo; the steps pasted are the selection
 * after. (Two bars: the pool's 8 KiB spare, tools/build.py) */
#define CLIP_N 32u
#define CLIP_ALL 0xFFu
static struct {
    uint8_t n, lane;                                  /* its steps; the lane it holds (CLIP_ALL: every lane, notes) */
    uint32_t at;                                      /* the grid's lane copy last: track, steps, lane + 1 (0: none) */
    step_t step[CLIP_N];
} clip __attribute__((section(".pool")));
static void sel_copy(const track_t *t)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], i, j, k, e, b0, n = 1, lane = CLIP_ALL;
    uint32_t at = song.sel | sel_lo() << 8 | sel_hi() << 16 | (ui.lane + 1u) << 24;
    char b[24];
    if (grid_on() && at != clip.at)
        lane = ui.lane;
    clip.at = lane != CLIP_ALL ? at : 0u;
    for (b0 = sel_lo(); b0 <= sel_hi() && b0 < NSTEP; b0++)
        if (sel_step(t, b0) && (lane == CLIP_ALL || ((t->step[b0].hit >> lane) & 1u)))
            break;
    if (b0 > sel_hi() || b0 >= NSTEP) {
        if (lane == CLIP_ALL) {
            ui_message("NOTHING TO COPY");
        } else {
            str_cpy(b, "NO ", sizeof b);
            str_cpy(b + 3, drum_lane_name(t, lane), 8);
            str_cpy(b + str_len(b), " TO COPY", 9);
            ui_message(b);
        }
        return;
    }
    ed_open(w, t, len);
    for (i = 0; i < CLIP_N; i++)
        step_clear(&clip.step[i]);
    for (i = b0; i <= sel_hi() && i < len && i - b0 < CLIP_N; i++) {
        step_t *c = &clip.step[i - b0];
        if (!sel_step(t, i) || (lane != CLIP_ALL && !((w[i].hit >> lane) & 1u)))
            continue;
        ed_vals(c, &w[i]);
        if (!nsel.picked && w[i].hit) {
            c->hit = lane == CLIP_ALL ? w[i].hit : (uint8_t)(1u << lane);
            c->acc = w[i].acc & c->hit;
        }
        for (k = 0; lane == CLIP_ALL && k < w[i].n; k++) {
            if (!sel_voice(i, k) || step_tied(&w[i], k))
                continue;
            (void)ed_add(c, w[i].note[k], 0);
            for (e = ed_end(w, len, i, w[i].note[k]), j = i + 1u; j <= e && j - b0 < CLIP_N; j++)
                (void)ed_add(&clip.step[j - b0], w[i].note[k], 1);
            n = j - b0 > n ? j - b0 : n;
        }
        n = i - b0 + 1u > n ? i - b0 + 1u : n;
    }
    clip.n = (uint8_t)n;
    clip.lane = (uint8_t)lane;
    str_cpy(b, "COPIED ", sizeof b);
    if (grid_on() && lane == CLIP_ALL) {
        str_cpy(b + 7, "ALL LANES", 10);
    } else {
        if (lane != CLIP_ALL) {                       /* COPIED KICK 4 STEPS */
            str_cpy(b + 7, drum_lane_name(t, lane), 8);
            str_cpy(b + str_len(b), " ", 2);
        }
        fmt_int(b + str_len(b), clip.n);
        str_cpy(b + str_len(b), clip.n > 1u ? " STEPS" : " STEP", 8);
    }
    ui_message(b);
}
static void sel_paste(track_t *t)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], at = ui.cursor, n = clip.n, i, k, on = clip.lane != CLIP_ALL && grid_on();
    char b[24];
    clip.at = 0;                                      /* (a copy after it is the lane's again) */
    if (!n) {
        ui_message("NOTHING COPIED");
        return;
    }
    if (at + n > len)
        n = len - at;                                 /* (cut at the pattern's end) */
    ed_open(w, t, len);
    for (i = 0; i < n; i++) {                         /* anything in the way: nothing */
        const step_t *c = &clip.step[i];
        step_t *s = &w[at + i];
        uint32_t had = (uint32_t)step_starts(s);
        for (k = 0; k < c->n; k++)
            if (!ed_add(s, c->note[k], step_tied(c, k)))
                break;
        if (k < c->n || (c->hit && step_ties(s))) {
            sel_taken(at + i);
            return;
        }
        if (c->hit) {                                 /* (a drum step's hits, their accents; (2.0) a lane's onto LANE) */
            uint32_t h = on ? 1u << ui.lane : c->hit;
            s->hit |= (uint8_t)h;
            s->acc |= (uint8_t)(on ? (c->acc ? h : 0u) : c->acc);
            s->time = ST_NOTE;
        }
        if (step_starts(c) && !had)
            ed_vals(s, c);
    }
    tl_undo_take(t, SN_PASTE);
    ed_close(t, w, len, 0);
    run_done(t);
    sel_drop();
    nsel.a1 = (uint8_t)(at + n);                       /* (the steps pasted: at .. at + n - 1) */
    str_cpy(b, "PASTED", sizeof b);
    if (on) {                                         /* PASTED ON CLAP */
        str_cpy(b + 6, " ON ", 5);
        str_cpy(b + 10, drum_lane_name(t, ui.lane), 8);
    }
    ui_message(b);
}

/* a TIE that holds no note (its note cleared before 2.0: an empty step to the eye) */
static int tie_alone(const track_t *t, uint32_t i)
{
    return t->step[i].time == ST_TIE && pr_src(t, i, (uint32_t)t->p[P_SLEN]) == NSTEP;
}
/* EDIT's clear (REC armed): step i silent, its locks gone: its notes and the ones going on through it out of it and of
 * the steps they go on after it ((2.0) a tie left behind would hold nothing, unseen, and PRESETS would jump it); a step
 * left with nothing loses its locks too */
static void step_clear_ties(track_t *t, uint32_t i)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], j, e, p;
    uint64_t gone = 0;
    ed_open(w, t, len);
    while (w[i].n) {
        for (p = w[i].note[0], e = ed_end(w, len, i, p), j = i; j <= e; j++) {
            ed_del(&w[j], (uint32_t)note_at(&w[j], p));
            gone |= (uint64_t)(!w[j].n && !w[j].hit) << j;
        }
    }
    step_clear(&w[i]);
    gone |= (uint64_t)1 << i;
    ed_close(t, w, len, 0);
    for (j = i; j < len; j++)
        if ((gone >> j) & 1u)
            (void)motion_clear_locks(t, j);
}
/* SHIFT + EDIT on NOTES or FEEL, REC armed or not: the picked notes deleted, none picked every note starting in the
 * selection (its hits too), each with the steps it goes on; a step left with nothing is cleared with its locks, and ties
 * holding no note in it go too. A run of them is one undo */
static void sel_delete(track_t *t)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], hi = sel_hi() < len ? sel_hi() : len - 1u, i, j, k, e, p, n = 0, any = 0;
    uint64_t gone = 0;
    char b[24];
    for (i = sel_lo(); i <= hi; i++)
        any |= sel_step(t, i) || tie_alone(t, i);
    if (!any) {
        ui_message("NO NOTES");
        return;
    }
    ed_open(w, t, len);
    for (i = sel_lo(); i <= hi; i++) {
        if (tie_alone(t, i)) {
            step_clear(&w[i]);
            gone |= (uint64_t)1 << i;
            continue;
        }
        if (!sel_step(t, i))
            continue;
        for (k = w[i].n; k-- > 0u;)                     /* (from the top: the notes above a deleted one move down) */
            if (sel_voice(i, k) && !step_tied(&w[i], k))
                for (n++, p = w[i].note[k], e = ed_end(w, len, i, p), j = i; j <= e; j++) {
                    ed_del(&w[j], (uint32_t)note_at(&w[j], p));
                    gone |= (uint64_t)(!w[j].n && !w[j].hit) << j;
                }
        if (!nsel.picked && w[i].hit) {
            n += (uint32_t)__builtin_popcount(w[i].hit);
            w[i].hit = 0;
            if (!w[i].n)
                step_clear(&w[i]);
            gone |= (uint64_t)(!w[i].n) << i;
        }
    }
    run_undo(t, SN_DEL);
    ed_close(t, w, len, 0);
    for (j = 0; j < len; j++)
        if ((gone >> j) & 1u)
            (void)motion_clear_locks(t, j);
    run_done(t);
    sel_drop();
    str_cpy(b, "DELETED ", sizeof b);
    fmt_int(b + 8, (int32_t)n);
    str_cpy(b + str_len(b), n == 1u ? " NOTE" : " NOTES", 8);
    ui_message(n ? b : "NO NOTES");
}

/* (#31) REC on, a chord held at the cursor (step entry): PRESETS its LENGTH, a step a detent */
static void entry_len(int32_t s)
{
    track_t *t = TSEL;
    int32_t n = clamp((int32_t)step_span(t, ui.cursor) + s, 1, 2 * (int32_t)NSTEP);   /* ((2.0.2, #58) 16 before) */
    uint32_t in = len_set(t, ui.cursor, 0, n > 1 ? LEN_STEPS + n - 2 : motion_base_value(t, P_SGATE));
    if (in)
        sel_taken(in - 1u);
}

/* the keys on the grid (presses): a white key toggles the selected lane at its step of the page (its accent
 * while ACC is held) and puts the cursor there; a lane key selects the lane (seq.c plays it); the page keys */
static void grid_keys(uint32_t pressed)
{
    uint32_t k, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        if (!((pressed >> k) & 1u))
            continue;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            if (i >= len)
                continue;                               /* past LEN: no step there */
            {   /* a hit (an accent) there goes when the key is let go, unless it was held for a lock (lock_keys) */
                const step_t *st = &TSEL->step[i];
                uint64_t b = (uint64_t)1 << i;
                uint32_t acc = (uint32_t)black_held(GK_ACC);
                if (((acc ? step_accents(st) : step_lanes(st)) >> ui.lane) & 1u) {
                    ui.plk_rm |= b;
                    ui.plk_acc = acc ? ui.plk_acc | b : ui.plk_acc & ~b;
                    ui.plk_lane = ui.lane;
                    ui.plk_trk = song.sel;
                } else if (acc) {
                    grid_acc(TSEL, i, ui.lane, 1);
                } else {
                    grid_hit(TSEL, i, ui.lane, 1);
                }
            }
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

/* ------------------------------------------------- parameter locks --- */
/* 1.1 (Discussions #75, #53): on SEQ > NOTES, hold a step and turn KNOB 1..4: the step gets a lock of the parameter
 * that knob edits on the sound page shown last (ui.plk_src: HOME's four, ENV, LFO, FX, EDIT 1 / 2, OP ENV, ..; the
 * cards show those four while a step is held, a locked value bright, the sound's own DIM). The step held: on the
 * DRUM grid its white keys (several at once: each gets the lock; a press on a hit takes it away only when let go,
 * and not at all when it was held for a lock), on the roll the step being entered (its note keys held). [EDIT]
 * tapped meanwhile clears the held steps' locks (EDIT alone still clears the step, its locks with it). Not while a
 * song plays (STOP TO EDIT). A lock is a motion record (motion.c): AUTOMATION's PLAY plays it, CLEAR clears it */
static uint64_t lock_held(void)
{
    const track_t *t = TSEL;
    uint32_t k, len = (uint32_t)t->p[P_SLEN], keys = fm1_in.notes & ~kb_layer;
    uint64_t m = 0;
    if (ui.home || ui.menu || ui.confirm || ui.layer || name_on() || !song.seq_mode || !step_page(cur_page()->graph) ||
        !keys)
        return 0;
    if (!grid_on())                                     /* the roll: the step being entered */
        return ui.entry_open && !live_rec_sel() && ui.cursor < len ? (uint64_t)1 << ui.cursor : 0u;
    for (k = 0; k < 27u; k++)
        if (((keys >> k) & 1u) && !key_black(k) && ui.bank * 16u + key_place(k) < len)
            m |= (uint64_t)1 << (ui.bank * 16u + key_place(k));
    return m;
}

/* each pass, before the grid's keys: a hit pressed on goes once its key is let go (plk_rm, grid_keys) */
static void lock_keys(void)
{
    uint64_t gone = ui.plk_rm & ~lock_held();
    uint32_t i;
    for (i = 0; gone && i < NSTEP; i++)
        if ((gone >> i) & 1u) {
            if ((ui.plk_acc >> i) & 1u)
                grid_acc(&trk[ui.plk_trk % NTRK], i, ui.plk_lane, 0);
            else
                grid_hit(&trk[ui.plk_trk % NTRK], i, ui.plk_lane, 0);
        }
    ui.plk_rm &= ~gone;
}

/* KNOB k turned s with the steps `held` held: each gets (or changes) its lock of lock_id(k), from its lock or the
 * sound's own value */
static void lock_turn(uint32_t k, int32_t s, uint64_t held)
{
    track_t *t = TSEL;
    uint32_t id = lock_id(k), i;
    const param_desc_t *d;
    if (id == 0xFFu)
        return;                                         /* (an empty card: nothing to lock) */
    ui.plk_rm &= ~held;                                 /* (held for a lock: its hit stays) */
    d = track_desc(t, id);
    s = accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
    motion_undo_take(t, 0x100u | k);                    /* (1.1.5: SAVE held takes the locks back) */
    for (i = 0; i < NSTEP; i++) {
        int16_t v;
        if (!((held >> i) & 1u))
            continue;
        if (!motion_lock_get(t, i, id, &v))
            v = motion_base_value(t, id);
        if (motion_set_lock(t, i, id, (int16_t)param_turn(d, v, s)) == 2)
            break;                                      /* (full: ui_notices says so) */
    }
    motion_undo_done(t);
}

/* [EDIT] with the steps `held` held: their locks go */
static void lock_clear(uint64_t held)
{
    uint32_t i, n = 0;
    ui.plk_rm &= ~held;
    if (!(motion_lock_steps(song.sel) & held)) {
        ui_message("NO LOCKS");
        return;
    }
    motion_undo_take(TSEL, 0);                          /* (1.1.5: SAVE held brings them back) */
    for (i = 0; i < NSTEP; i++)
        if ((held >> i) & 1u)
            n += motion_clear_locks(TSEL, i);
    motion_undo_done(TSEL);
    ui_message(n ? "LOCKS CLEARED" : "NO LOCKS");
}

/* NOTES' knobs on a melodic track ((2.0) PRESETS the cursor): PITCH START LENGTH VELOCITY of the selection */
static void step_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    if (drum_track(t)) {
        grid_edit(slot, steps);
        return;
    }
    follow_hold();                                        /* ((1.3) what is edited stays on the screen) */
    if (slot == 0u)
        sel_pitch(t, steps);
    else if (slot == 1u)
        sel_start(t, steps > 0 ? 1 : -1);
    else if (slot == 2u)
        sel_length(t, steps);
    else
        sel_velocity(t, accel(EN_K4, steps, 126));
}

/* (Skiff phase 5) SEQ > SONG's knobs (ui.c sg): 1 the selected track's entry (A..P), 2 a pattern for it, put in by
 * OCT+ (an empty entry's "--" first, (#28) REST, the used ones in order, then COPY: a copy of its own; (2.0) NEW,
 * COPY+: below), 3 its repeats (1..16), 4 SONG OFF, ON, ((2.0) ONCE). own: what the entry holds (0 empty, a pattern
 * id + 1, PAT_REST) */
static int song_pick_ok(uint32_t v, uint32_t own)
{
    if (!v)
        return !own;
    if (v == PAT_REST || v == SPICK_NEW)              /* ((2.0, #32) NEW: any entry's) */
        return 1;
    if (v == SPICK_COPY || v == SPICK_COPYP)          /* ((2.0, #33) COPY+: an empty entry after it) */
        return own && own <= NPAT && (v == SPICK_COPY || ent_free_after(song.sel % NTRK, sg.entry % NENT) < NENT);
    if (v == SPICK_CLEAR || v == SPICK_SOUND)         /* ((Skiff U2) CLEAR after COPY: was EDIT; (2.0, #30) SOUND: an */
        return own != 0u;                             /* entry's, a pattern's or a REST's) */
    if (v == SPICK_NOSND)                             /* (.. NO SOUND: one with a sound) */
        return lists.snd[song.sel % NTRK][sg.entry % NENT][0] != 0u;
    return v <= NPAT && pool_used(v - 1u);
}
/* .. KNOB 2's turn order: --, ((2.0, #32, the user's pick 1A) NEW: an empty entry's), REST, 01 .. 64, (NEW: a used
 * entry's), COPY, ((#33) COPY+), CLEAR, ((#30) SOUND, NO SOUND): a pick's place (own: an empty entry's NEW first), and
 * back; a place that is not its value's own (spick_pos) is passed over */
static uint32_t spick_pos(uint32_t v, uint32_t own)
{
    return v == SPICK_NEW && !own ? 1u : v == PAT_REST ? 2u : v ? v + 2u : 0u;
}
static uint32_t spick_val(uint32_t p) { return p == 1u ? SPICK_NEW : p == 2u ? PAT_REST : p ? p - 2u : 0u; }
static void song_knob(uint32_t slot, int32_t steps)
{
    uint32_t k = song.sel % NTRK, e = sg.entry % NENT, v, n, own = lists.ent[k][e][0];
    if (slot == 0u) {
        sg.entry = (uint8_t)clamp((int32_t)sg.entry + steps, 0, NENT - 1);
    } else if (slot == 1u) {
        v = spick_pos(song_pick() ? song_pick() : own, own);
        for (; steps; steps += steps > 0 ? -1 : 1) {
            n = v;
            do
                n = steps > 0 ? n + 1u : n - 1u;    /* (below 0: past CLEAR's, out) */
            while (n <= SPICK_NOSND + 2u && (!song_pick_ok(spick_val(n), own) || spick_pos(spick_val(n), own) != n));
            if (n > SPICK_NOSND + 2u)
                break;
            v = n;
        }
        v = spick_val(v);
        sg.pick = (uint8_t)(v == own ? 0u : v);
        sg.pick_at = (uint8_t)(1u + k * 16u + e);
    } else if (slot == 2u) {                              /* ((1.2.1, #12) an empty entry's too: a rest's length; (2.0,
                                                         * #44) CHAIN below 1x on an entry with one after it) */
        int32_t r = lists.ent[k][e][1] == REPS_CHAIN ? -1 : (int32_t)lists.ent[k][e][1];
        r = clamp(r + steps, ent_on(k, e) && e + 1u < NENT ? -1 : 0, 15);
        lists.ent[k][e][1] = (uint8_t)(r < 0 ? REPS_CHAIN : r);
    } else {
        lists.song = (uint8_t)clamp((int32_t)lists.song + (steps > 0 ? 1 : -1), SONG_OFF, SONG_ONCE);   /* ((2.0) ONCE) */
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
#if FELUCCA_TAPE
    if(pg->fam==FAM_FX && pg->graph==GR_NONE){
        if(slot==0)tape_send[song.sel%NTRK]=(uint32_t)clamp((int32_t)tape_send[song.sel%NTRK]+steps,0,100);
        return;
    }
#endif
    if (pg->graph == GR_FEEL) {                           /* (Skiff 2.0) FEEL: CHANCE RATCHET NUDGE TYPE of the selection */
        follow_hold();                                    /* ((1.3) the edited steps stay on the screen) */
        feel_turn(TSEL, slot, steps);
        return;
    }
    if (pg->graph == GR_EVENTS) {                         /* AUTO LIST (ui_events.c) */
        ev_knob(slot, steps);
        return;
    }
    if (pg->graph == GR_SONG) {                           /* (Skiff phase 5) SONG */
        song_knob(slot, steps);
        return;
    }
    if (sheet_of(pg->graph)) {                            /* (Skiff phase 7, 8; FM6 LAB) a sheet: its window (SHEETS) */
        sheet_of(pg->graph)->knob(slot, steps);
        return;
    }
    if ((pg->graph == GR_LANES || pg->graph == GR_LMIX) && slot == 0u) {   /* (Skiff U3, R6) LANES: KNOB 1 the lane (the
                                                         * grid's too), KNOB 2 its level (params.c page_desc); ((2.0, #36)
                                                         * LANE MIX the same, DRUM-X's: the sound sheet's) */
        uint8_t *l = page_lane_p();
        *l = (uint8_t)clamp((int32_t)(*l % 8u) + steps, 0, 7);
        return;
    }
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u) {
            preset_step(steps);
        } else if (slot == 1u) {
            select_engine(eng_step_for(song.sel, TSEL->eng_req, steps));   /* ((Skiff phase 6) what the track may play) */
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 3u) {                          /* LIST: ALL, FAV, (Skiff phase 9) then the types (ui.c) */
            uint32_t v = (uint32_t)clamp((int32_t)list_mode() + steps, 0, PT_N);
            if (v != list_mode()) {
                favorites.filter = v == 1u;
                ui_ptag = (uint8_t)(v > 1u ? v - 1u : 0u);
                ui.force = 1;
                settings_later();                         /* (Felucca #197: not on every detent; ui.c) */
            }
        }
        return;
    }
#if FELUCCA_SLICE
    if (pg->graph == GR_SLICES && slot < 2u) {           /* SLICES: KNOB 1 the marker, 2 moves it (ui_slice.c) */
        if (slice_page_ok())                              /* (the engine changed before ui_draw left the page) */
            slice_knob(slot, steps);
        return;
    }
#endif
    if ((pg->graph == GR_USER && slot == 2u) || (pg->graph == GR_SLOTS && slot == 1u)) {   /* (Skiff U2) USER's KNOB 3:
                                                           * ERASE, one step further NAME; (1.2.1, #6) PROJECT's KNOB 2:
                                                           * DELETE, then NAME */
        int32_t st = ui.act == slot + 1u ? 1 + (int32_t)ui.act2 : 0;
        st = clamp(st + steps, 0, 2);
        ui.act = st ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;
        ui.act2 = st == 2;
        return;
    }
    if (pg->graph == GR_BANK && slot == 3u) {             /* ((2.0, #46) PATTERNS' KNOB 4: the ACTION, OCT+ does it */
        ui.bact = (uint8_t)clamp((int32_t)ui.bact + (steps > 0 ? 1 : -1), 0, BA_N - 1);
        return;
    }
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;   /* OCT+ does it (act_do) */
        ui.act2 = 0;
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
        return;
    }
    if (pg->graph == GR_BANK) {                           /* (Skiff phase 5) PATTERNS: KNOB 1 the pattern, 01 .. 64 */
        if (slot == 0u)
            ui.bslot = (uint8_t)clamp((int32_t)ui.bslot + steps, 0, NPAT - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 0u) {             /* MOD: KNOB 1 the route ((2.0) the fixed seven, then the slots), */
        mod_ui_route = (uint8_t)clamp((int32_t)mod_ui_route + (steps > 0 ? 1 : -1), 0, MOD_ROUTES - 1u);   /* 2..4 its */
        return;                                                                                            /* SRC DST AMT */
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = param_turn(d, *vp, accel(EN_K1 + slot, steps, d->fmt == F_ENUM ? 0 : d->max - d->min));
    if (vp == &TSEL->p[P_DKSRC] && v == (int32_t)(song.sel % NTRK) + 1)   /* (Skiff) DUCK BY: never the track itself */
        v = v == d->max ? *vp : v + (steps > 0 ? 1 : -1);
    *vp = (int16_t)v;
    if (pg->scope != SC_GLOBAL) {
        motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        ml_knob(TSEL, (uint32_t)(vp - TSEL->p));        /* ((1.3) MIDI LEARN: picked) */
    }
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again; (Skiff U1) a project's asks
 * first, and the pick goes with its dialog); the others are dropped once done. Flash writes only while stopped; over
 * the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id, k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!c--) {                                           /* ((Skiff U4, F7) nothing picked: said, not silence) */
        ui_message("PICK WITH A KNOB FIRST");
        return;
    }
    if (cur_page()->graph == GR_EVENTS) {
        ev_oct();
        return;
    }
    if (cur_page()->graph == GR_SONG) {                   /* (Skiff phase 5) the pick in, or the entry played */
        if (song_pick() == SPICK_CLEAR) {                 /* ((Skiff U2) CLEAR: the entry's dialog) */
            sg.pick = 0;
            song_edit();
            return;
        }
        song_oct();
        return;
    }
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_BANK) {                   /* (Skiff phase 5) the pool view: CLEAR the pattern picked,
                                                         * after its dialog, when nothing uses it (bank.c pool_clear);
                                                         * ((2.0, #46) KNOB 4's ACTION: or EMPTY it, or CLEAR UNUSED) */
        uint32_t id = ui.bslot % NPAT, n;
        char b[24];
        if (ui.bact == BA_UNUSED) {
            if (!(n = pool_unused()))
                ui_message("NOTHING UNUSED");
            else if (transport_busy())
                ui_message("STOP TO CLEAR");
            else
                confirm_open(CF_CLEAR_UNUSED, n);
        } else if (!pool_used(id)) {
            ui_message(ui.bact == BA_EMPTY ? "NOTHING TO EMPTY" : "NOTHING TO CLEAR");
        } else if (ui.bact == BA_EMPTY) {
            if (transport_busy())
                ui_message("STOP TO EMPTY");
            else
                confirm_open(CF_EMPTY_BANK, id);
        } else if ((n = pool_refs(id)) != 0u) {          /* "USED BY 3 ENTRIES" */
            str_cpy(b, "USED BY ", sizeof b);
            fmt_int(b + 8, (int32_t)n);
            str_cpy(b + str_len(b), n == 1u ? " ENTRY" : " ENTRIES", sizeof b - str_len(b));
            ui_message(b);
        } else if (transport_busy()) {
            ui_message("STOP TO CLEAR");
        } else {
            confirm_open(CF_ERASE_BANK, id);
        }
        return;
    }
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE (or NAME), 3 SAVE (the NAME screen first) */
        if (c == 2u && ui.act2) {                         /* ((Skiff U2) NAME: was EDIT) */
            ui.act = ui.act2 = 0;
            name_rename();
            return;
        }
        if (c > 1u)
            ui.act = 0;
        if (c == 2u && up_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            up_ui(c - 1u, ui.uslot);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (up_used(ui.uslot) || up_old(ui.uslot))     /* ((2.0) an OLD FORMAT one asks too) */
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    if (cur_page()->graph == GR_SLOTS && c == 1u) {       /* ((Skiff U2) PROJECT's NAME: was EDIT) */
        int nm = ui.act2;
        ui.act = ui.act2 = 0;
        if (nm)
            name_rename();
        else if (project_used(k) && !transport_busy())  /* ((1.2.1, #6) DELETE: the dialog first) */
            confirm_open(CF_ERASE_PROJ, k);
        else
            (void)project_erase(k);                       /* (says why not: STOP TO DELETE, EMPTY SLOT) */
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
    switch (id) {
    case G_LOAD:                                          /* (Skiff U1) over the session: the dialog first ((U3, A2)
                                                           * when it changed since its last save or load) */
        if (!project_used(k) && project_changed())        /* ((1.2.1, #6) an empty slot: NEW, over changed music */
            confirm_open(CF_NEW_PROJ, k);                 /* the dialog first) */
        else if (!project_used(k))
            project_new();
        else if (project_changed())
            confirm_open(CF_LOAD_PROJ, k);
        else
            project_load(k);
        break;
    case G_SAVE:                                          /* the NAME screen writes it */
        if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (project_used(k))
            confirm_open(CF_OVR_PROJ, k);
        else
            name_open(NK_PROJ_SAVE, k);
        break;
    }
}

/* OCT- / OCT+ where they answer (the dialogs, the menu, action pages): on release, and only a press
 * that began there; both down together (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord;
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap;
    if (here)
        down |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    if (now == 3u)
        chord = 1;
    tap = down & ~now;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    return tap;
}

/* (2.0, #25) step entry opening on step i where notes go on (through it, or its own per note): its own notes go, with
 * the steps they go on (the keys write it); the notes going on through it stay (a note entered over a long one overlaps
 * it). Kept unfolded for the keys */
static void entry_open(track_t *t, uint32_t i)
{
    step_t w[NSTEP];
    uint32_t len = (uint32_t)t->p[P_SLEN], k, j;
    uint64_t keep = (uint64_t)1 << i;
    ed_open(w, t, len);
    for (k = w[i].n; k-- > 0u;)
        if (!step_tied(&w[i], k))
            (void)ed_lift(w, len, i, w[i].note[k]);
    w[i].time = ST_NOTE;
    for (j = i + 1u; j < len && step_ties(&w[j]); j++)  /* (the steps the notes going on go on: unfolded too, so */
        keep |= (uint64_t)1 << j;                      /* what the keys write here isn't held by a TIE after it) */
    ed_close(t, w, len, keep);
}
/* SEQ step entry = step recording (1.2, Discussion #133: only with the track armed and the transport stopped), acid
 * style: the keys pressed together (POLY: up to 4 notes, MONO: the last one) become the cursor step; releasing all
 * keys moves on past it ((2.0, #31) PRESETS meanwhile its LENGTH, its ties: past them; wrapping inside LEN). With CHRD
 * on a key writes what it sounds, as live recording does: POLY its chord, MONO the chord's root */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint8_t ch[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        uint32_t note, n, i, j;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            follow_hold();                                /* ((1.3) the roll stays on the step entered) */
            if (!(t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) && (step_tie(st) || step_ties(st) ||
                (ui.cursor + 1u < (uint32_t)t->p[P_SLEN] && step_ties(&t->step[ui.cursor + 1u]))))
                entry_open(t, ui.cursor);                 /* ((2.0, #25) POLY, notes going on here or its own: per note) */
            else {
                st->n = 0;                                /* (the TIEs after it hold what the keys write: its length) */
                st->time = ST_NOTE;
            }
        }
        n = chord_make(t, note, ch, &r, &mask);        /* (CHRD OFF, a kit: the note alone; MONO: the root) */
        if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* (drums: hits stack as a chord) */
            st->note[0] = (uint8_t)clamp(r, 0, 127);      /* (the chord's root: (2.0.3) with the ARP on, ch is the chord) */
            st->n = 1;
            if (!st->hit)
                st->acc = 0;                              /* ((2.0) one note, a new one: not tied) */
        } else {
            for (i = 0; i < n && st->n < 4u; i++) {
                for (j = 0; j < st->n && st->note[j] != ch[i]; j++)
                    ;
                if (j == st->n)
                    st->note[st->n++] = ch[i];
            }
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !(fm1_in.notes & ~kb_layer))
        cursor_set(ui.cursor + (int32_t)step_span(t, ui.cursor));   /* ((2.0, #31) past its ties) */
}

/* (Skiff 2.0) SHIFT + PRESETS on NOTES or FEEL: the selection stretched from where it was to where the cursor goes
 * (inside the pattern, no wrap); 1: taken (a SHIFT combo) */
static int sel_shift(int32_t s)
{
    uint32_t g = cur_page()->graph, len = (uint32_t)TSEL->p[P_SLEN], i;
    if (ui.home || ui.menu || ui.confirm || !song.seq_mode || !(step_page(g) || g == GR_FEEL))
        return 0;
    if (!nsel.a1)
        nsel.a1 = (uint8_t)(ui.cursor + 1u);
    ui.cursor = (uint8_t)clamp((int32_t)ui.cursor + s, 0, (int32_t)len - 1);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
    for (nsel.picked = 0, i = sel_lo(); i <= sel_hi(); i++)   /* (no pick in it now: every note) */
        nsel.picked |= nsel.vp[i];
    follow_hold();
    return 1;
}

/* (Skiff 2.0) PRESETS on NOTES and FEEL: the cursor s steps on, a long note's ties jumped (back: onto its first step; on:
 * past its end, wrapping inside LEN), so it stops on a note's first step or an empty one (a tie holding no note is
 * one: tie_alone; (#25) a step where notes only go on is a tie) */
static void cursor_turn(int32_t s)
{
    const track_t *t = TSEL;
    int32_t len = t->p[P_SLEN] > 0 ? t->p[P_SLEN] : 1, c = (int32_t)ui.cursor + s;
    if (c >= len || c < 0) {                              /* ((2.0, #44) stopped on a chain: into its next part or the
                                                         * one before, bank.c chain_cursor) */
        c = chain_cursor(c);
        len = t->p[P_SLEN] > 0 ? t->p[P_SLEN] : 1;
    }
    c = (c % len + len) % len;
    if (step_tie(&t->step[c]) && !tie_alone(t, (uint32_t)c)) {
        if (s < 0)
            while (c > 0 && step_tie(&t->step[c]))
                c--;
        else
            while (c < len && step_tie(&t->step[c]))
                c++;
    }
    cursor_set(c);
}

/* Discussions #92 / #94: the PRESETS knob by page (the menu, a dialog, NAME and the layers: ui_input before this).
 *   SEQ's NOTES and FEEL ((2.0) and the DRUM grid): the step cursor, over a long note's ties (cursor_turn; a sound load
 *     there would drop the track's motion: never); SHIFT held: the selection (sel_shift); (#31) REC on, a chord held:
 *     its LENGTH; PATTERN: the track's DIR
 *   the list / action pages USER, PATTERNS, PROJECT, SONG: the selection, as their KNOB 1; TOOLS and SLICES: nothing
 *   HOME, PRESETS and every other page (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, GLOBAL, ...): the selected track's
 *     sound, one list over every engine, then the used user presets (preset_step). A sound load keeps the steps
 *     (SAVE held undoes it). Up to 1.0.3 it did that on HOME and PRESETS only */
static void presets_turn(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t g = pg->graph;
    if (ui.home || g == GR_BROWSE) {
        preset_step(s);
    } else if (step_page(g) || g == GR_FEEL) {
        if (roll_on() && ui.entry_open) {
            entry_len(s);
        } else {
            cursor_turn(s);
            cursor_audition();                            /* ((1.3) STEP PREVIEW) */
        }
    } else if (g == GR_USER || g == GR_BANK || g == GR_SLOTS || g == GR_EVENTS || g == GR_SONG) {
        edit_param(0, s);                                 /* KNOB 1's */
        ui.hot_col = 0;
        ui.hot_t = 40;
    } else if (pg->fam == FAM_SEQ && g == GR_STEPS) {     /* ((2.0) PATTERN: DIR, PLAY's KNOB 3 before) */
        track_t *t = TSEL;
        t->p[P_DIR] = (int16_t)param_turn(track_desc(t, P_DIR), t->p[P_DIR], s);
        motion_capture(t, P_DIR, t->p[P_DIR]);
        ml_knob(t, P_DIR);
    } else if (sheet_of(g)) {                             /* (Skiff phase 8; FM6 LAB) a sheet's pick: DRUM-X's sound */
        sheet_of(g)->pick(s);                             /* (BD .. CB), FM6's operator */
    } else if (g == GR_SLICES) {                          /* ((Skiff U4, F7) SLICES: KNOB 1's, the slice; was nothing) */
#if FELUCCA_SLICE
        if (slice_page_ok())
            slice_knob(0, s);
#endif
    } else {
        preset_step(s);
    }
}

/* (Skiff phase 8, DESIGN.md rule 3) SELECT turned s on a page: a detent a step, either way. A sheet (DRUM-X's, FM6's)
 * moves its knobs' window first, then the page beside it; any other page steps to the page before or after in its
 * family (ui.c page_step: the ends where they are). A sheet entered from the page after it: its foot, as SELECT left
 * it */
static void select_turn(int32_t s)
{
    int32_t d = s > 0 ? 1 : -1, n = s > 0 ? s : -s;
    const sheet_t *sh;
    while (n-- > 0) {
        if ((sh = sheet_of(cur_page()->graph)) != 0 && sheet_rows(sh, d))
            continue;
        if (!page_step(d)) {
            ui.edge_t = 24;                             /* ((Skiff U4, F7) the end: the footer's n/m flashes) */
            break;
        }
        if ((sh = sheet_of(cur_page()->graph)) != 0)
            *sh->win = (uint8_t)(d > 0 ? 0u : sh->nwin - 1u);
    }
}

/* HOME / REC / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > (uint32_t)HOLD_MS[settings_hold % 4u] * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static int layer_knobs_quiet(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void perf_encoders(int32_t ls, int32_t lp); /* (Skiff) ui_layer.c */
static void layer_knob(uint32_t k, int32_t s);
static int layer_play(void);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static void layer_oct_open(uint32_t pressed);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);
static void layer_lock_input(uint32_t pressed);
static void layer_shift_lock(uint32_t pressed, uint32_t now);
static uint32_t layer_bits_all(void);
static void layer_unlock(void);
static void layer_home_lock(void);
static void rec_clear_poll(void);
static void tl_undo_take(track_t *t, const char *name);
/* (2.0.3, #69) the FX layer's macros held (ui.c mom_poll, mom_keep): FX held (or locked), or let go with keys pressed
 * with it still held, its map up and their effects playing */
static int fx_map(void) { return (ui.ly == LAYER_FX && layer_held()) || (ui.layer == LAYER_FX && kb_layer); }

/* (Skiff U1) EDIT's step clear on NOTES / the grid: an undo copy first, one for a run of them (the same track,
 * nothing else changed since, under 1.5 s apart); page_tap keeps undo.after / t_ms for the next */
static const char STEP_CLR[] = "STEP CLEAR";
static void step_clear_undo(track_t *t)
{
    if (undo_n && !undo_back && undo.name == STEP_CLR && undo.trk == trk_index(t) + 1u && track_sig(t) == undo.after &&
        fm1_ms - undo.t_ms < 1500u)
        return;
    tl_undo_take(t, STEP_CLR);
}

/* (Skiff U2, the review's S6) a recording pass is one undo: armed, the track as it is (RECORDING, SAVE held puts it
 * back: live REC, step entry, what the keys wrote); disarmed with nothing recorded, that copy goes again (no empty undo) */
static const char REC_PASS[] = "RECORDING";
static void rec_pass_undo(int armed)
{
    track_t *t = TSEL;
    if (armed) {
        tl_undo_take(t, REC_PASS);
        undo.after = track_sig(t);
    } else if (undo_n && !undo_back && undo.name == REC_PASS && undo.trk == trk_index(t) + 1u &&
               track_sig(t) == undo.after) {
        undo_top = (uint8_t)((undo_top + UNDO_N - 1u) % UNDO_N);
        undo_n--;
    }
}

/* a page button let go (they act on release; a layer's own button: layer_gesture). 1: it acted (EDIT on NOTES, USER,
 * PROJECT) instead of opening a page */
static int page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_GLO) {
        open_global();                                  /* MIXER -> GLOBAL -> SYSTEM -> MIXER */
        return 0;
    }
    if (b == B_EDIT && song.seq_mode && !ui.home && step_page(cur_page()->graph) &&
        (lock_held() || ((song.rec >> song.sel) & 1u))) {   /* STEP: a step held, its locks; (Skiff U2) armed, EDIT
                                                         * deletes the cursor step (step entry); else the EDIT pages */
        uint64_t held = lock_held();
        if (held) {                                     /* a step held: its locks (the step stays) */
            lock_clear(held);
            return 1;
        }
        step_clear_undo(TSEL);                          /* (Skiff U1) SAVE held puts it back */
        step_clear_ties(TSEL, ui.cursor);               /* ((2.0) its notes' ties too; (#44) a chain's tail) */
        chain_sync(TSEL);
        undo.after = track_sig(TSEL);
        undo.t_ms = fm1_ms;
        cursor_set(ui.cursor + 1);
        ui_message("STEP CLEARED");
        return 1;
    }
    /* ((Skiff U2, the review's G3) AUTO LIST's delete, SONG's clear, USER / PROJECT's rename: their cards now) */
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
        if (FAM_BTN[f] == b) {
            open_family(f);
            return 0;
        }
    return 0;
}

/* a track whose sound's sample is missing (engines.c snd_missing: an empty user slot, a built-in set this build lacks;
 * it plays a sine): the no-file icon and NO SAMPLE (ui.c MSG_NO_SAMPLE). Once per track and source: at power-on, a sound
 * or project load, SRC / SET turned, a slot erased; again after another source or a project load. One a frame; after
 * a message already showing (LOADED) as the second message. Not on notes: nothing in the audio path */
static void sample_notice(void)
{
    uint32_t k, code;
    for (k = 0; k < NTRK; k++) {
        code = 0;
        if (!snd_missing(&trk[k], &code)) {
            snd_said[k] = 0;
            continue;
        }
        if (snd_said[k] == code)
            continue;
        snd_said[k] = (uint8_t)code;
        if (ui.msg_t)
            str_cpy(ui.msg2, MSG_NO_SAMPLE, sizeof ui.msg2);
        else
            ui_message(MSG_NO_SAMPLE);
        return;
    }
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    sample_notice();
    if (motion_full) { motion_full = 0; ui_way("AUTOMATION FULL", WAY_MOTION); }
    if (midi_hint) {                                    /* MIDI notes into a track that is not selected: ((Skiff U4,
                                                         * F4) its number marked on HOME, ui.c midi_at) */
        midi_at[(midi_hint - 1u) % NTRK] = fm1_ms | 1u;
        midi_hint = 0;
    }
}

#if BOARD_SMK37
static uint32_t smk_sh_t0;                              /* KNOB BANK as SHIFT (ui_input) */
#endif
/* (Skiff 2.0) SHIFT held (HOME; the SMK's KNOB BANK too), and a combo of it taken: no HOME tap, no MENU after (the
 * caller drops ui_input's home_long) */
static int shift_held(void)
{
#if BOARD_SMK37
    if (smk_sh_t0)
        return 1;
#endif
    return ui.home_t0 != 0u;
}
static void shift_mark(void)
{
#if BOARD_SMK37
    if (smk_sh_t0)
        smk_sh_t0 |= 2u;
#endif
    if (ui.home_t0)
        ui.home_t0 |= 2u;
}
static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home, save;
    int rec_own;
    uint32_t oct;
    uint32_t lay, combo = 0, lytap, lkeys, glo, kq = 0;
    int32_t s, sel = 0, ks[4] = {0, 0, 0, 0};
    static uint32_t lock_ms;                            /* BPM LOCK: the last locked SELECT turn (fm1_ms | 1; 0 none) */
    static uint8_t home_long;                           /* Skiff: HOME held past 0.7 s, the menu closed: MENU when let go */
    fm6_poll();                                         /* FM6: PTCH turned -> its patch */
    unit_g_poll();                                      /* ((Skiff U3, R8) the unit's CLOCK MIDI IN TUNE) */
    clock_wait_poll();                                  /* (PLAY on an external clock that sends nothing: say so) */
    bank_poll();                                        /* (Skiff) a pattern switched in live: what went out kept */
    song_poll();                                        /* (Skiff phase 5) SONG: each track's next entry staged */
#if !FELUCCA_FM4 && FELUCCA_LEGACY
    for (k = 0; k < NTRK; k++)                          /* a DIGITAL sound any other way (the paths convert it */
        if (trk[k].eng_req == ENGI_DIGITAL)             /* already): FM6 (fm4_convert.c) */
            fm4_track(&trk[k]);
#endif
    perf_latch_on = ui.lock == LAYER_FX;                /* ((Skiff U2) a locked FX layer latches: MENU > FX LATCH retired) */
    fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (MENU > USB LEVEL: fx.c, audio.c) */
    rp_apply();                                         /* (MENU > CLICK, CLICK LEVEL, COUNT-IN: click.c, seq.c) */
    if (!ui.menu)
        usb_serial_apply();                             /* (MENU > USB SERIAL: when the menu has closed) */
    settings_later_poll();                              /* (Felucca #197: LIST / FAV written once the view is left) */
    follow_poll();                                      /* ((1.3, Felucca #242) the step pages follow the play position) */
    if (mom_poll(mom_down(), fx_map()))                 /* ((1.3) MOMENTARY: LFO let go (or the menu, a layer ..): back */
        ui_message("BACK");                             /* ((2.0, D 2A) over RETURN; FX's macros too, (2.0.3, #69) once
                                                         * its map has gone) */
    midi_pc_poll();                                     /* ((1.3) MIDI Program Change) */
    ml_poll();                                          /* ((1.3) MIDI LEARN: a CC came for the picked parameter) */
    if (scr_input(pressed, notes)) {                    /* MENU > SCREEN OFF: dark, or the wake gesture: swallowed */
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || seq_counting()))
            transport_req = 2;                          /* (Skiff U1) .. but PLAY still stops the music */
        return;                                         /* (before the buttons' timers: no tap or hold after it) */
    }
    if (((pressed >> panel.btn[B_OCTUP]) & 1u) &&      /* ((1.3) MOMENTARY: OCT+ with LFO held: kept (no octave); (2.0, */
        (mom_keep(mom_down(), fx_map()) | (layer_open() == LAYER_PERF && throw_keep()))) {   /* D 2A) */
        pressed &= ~(1u << panel.btn[B_OCTUP]);         /* FX's macros, PERFORM's throws too) */
        ui_message("KEPT");
    }
    home = btn_hold(&ui.home_t0, B_HOME, now, !ui.confirm && !name_on());   /* ((Skiff U2) held over a dialog or NAME:
                                                         * no MENU, which threw them away) */
    if (home_long && !((fm1_in.buttons >> panel.btn[B_HOME]) & 1u)) {   /* (Skiff) HOME let go after its hold: the */
        home_long = 0;                                  /* MENU (first, so the layers see it this frame) */
        ui.msg_t = 0;
        ui.menu = 1;                                    /* (at the tab and row it was left at) */
        ui.confirm = 0;                                 /* (a clear dialog is cancelled, NAME too) */
        name_close();
        ui.force = 1;
        song.seq_mode = 0;
    }
    save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    if (ui.home_t0 && ((pressed >> panel.btn[B_SAVE]) & 1u) && !ui.menu && !ui.confirm && !name_on()) {
        ui.home_t0 |= 2u;                               /* (Skiff) SHIFT (HOME held) + SAVE: TAKE (ui_take.c); no */
        ui.save_t0 |= 2u;                               /* HOME tap, no MENU, no SAVE page, no undo */
        home_long = 0;
        take_commit(song.sel);
    }
#if BOARD_SMK37
    /* (SMK-37 Pro, SMK37_UX.md extra buttons A) KNOB BANK is SHIFT: HOME held, as a button of its own. Its combos are
     * HOME's (+ SAVE TAKE, + a layer's button its lock, pressed with a layer's button held: that layer locked) and its
     * map comes after the same moment; let go it does nothing (HOME still taps home, and held opens the MENU).
     * smk_sh_t0: its press time | 1, | 2 once a combo used it */
    if (!((fm1_in.buttons >> SMK_BTN_KNOB_BANK) & 1u))
        smk_sh_t0 = 0;
    else if (!smk_sh_t0)
        smk_sh_t0 = (now | 1u) & ~2u;
    if (smk_sh_t0 && !ui.home_t0 && ((pressed >> panel.btn[B_SAVE]) & 1u) && !ui.menu && !ui.confirm && !name_on()) {
        smk_sh_t0 |= 2u;
        ui.save_t0 |= 2u;
        take_commit(song.sel);
    }
#endif
    rec_own = ui.lock == LAYER_REC;                     /* (its press closes the REC layer: no arming) */
    if (shift_held() && ((pressed >> panel.btn[B_EDIT]) & 1u) && !ui.menu && !ui.confirm && !name_on() &&
        song.seq_mode && !ui.home && (step_page(cur_page()->graph) || cur_page()->graph == GR_FEEL)) {
        pressed &= ~(1u << panel.btn[B_EDIT]);          /* ((2.0) SHIFT + EDIT on NOTES, FEEL: the selection deleted, */
        shift_mark();                                   /* not EDIT's layer locked: EDIT held + HOME still locks it) */
        home_long = 0;
        sel_delete(TSEL);
    }
    if (ui.home_t0 && !ui.menu && !ui.confirm && !name_on() && (pressed & layer_bits_all())) {
        uint32_t lb = pressed & layer_bits_all();       /* (Skiff U2) SHIFT + a layer's button: locked open (again: */
        ui.home_t0 |= 2u;                               /* closed); no HOME tap, no MENU, the button no tap */
        home_long = 0;
        layer_shift_lock(lb, now);
        pressed &= ~lb;
    }
#if BOARD_SMK37
    if (smk_sh_t0 && !ui.home_t0 && !ui.menu && !ui.confirm && !name_on() && (pressed & layer_bits_all())) {
        uint32_t lb = pressed & layer_bits_all();       /* (KNOB BANK as SHIFT + a layer's button: locked open) */
        smk_sh_t0 |= 2u;
        layer_shift_lock(lb, now);
        pressed &= ~lb;
    }
#endif
    layer_lock_input(pressed);                          /* (a button closes a locked layer) */
    layer_arm(pressed, now);
    layer_oct_open(pressed);                            /* (OCT± with a SET layer's button down: it opens now) */
    oct = oct_taps(pressed, ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open() || ui.ml.on);
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    glo = lay && ui.ly == LAYER_GLO;                    /* GLO held: SELECT is the tempo, BPM LOCK or not (#58) */
    if (!layer_allowed()) {
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
        perf_latched = 0;                               /* (FX LATCH: the latched ones and the macros off) */
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
        (void)mom_keep(0, 1);                           /* ((2.0) at once: nothing of them to come back) */
    }
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly) &            /* (REC + PLAY: PLAY plays and REC let go soon is still */
                  ~(ui.ly == LAYER_REC ? 1u << panel.btn[B_PLAY] : 0u)) != 0u;   /* a tap, it arms: as before) */
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if ((pressed & (1u << panel.btn[B_HOME])) && !ui.lock) {   /* no menu, no HOME ((Skiff U2) locked: HOME */
            ui.home_t0 |= 2u;                          /* as always, SHIFT, its tap closing the lock); (#13) the layer */
            layer_home_lock();                         /* locked, as SHIFT + its button */
        }
#if BOARD_SMK37
        if (((pressed >> SMK_BTN_KNOB_BANK) & 1u) && !ui.lock) {   /* (KNOB BANK: as HOME there) */
            smk_sh_t0 |= 2u;
            layer_home_lock();
        }
#endif
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        if (ui.ly == LAYER_PERF) {                      /* (Skiff) PERFORM: SELECT the LAUNCH point, PRESETS the */
            int32_t ls = panel_enc(EN_SELECT), lp = panel_enc(EN_PRESET);   /* KEYS (ui_layer.c) */
            if (ls || lp) {
                combo = 1;
                perf_encoders(ls, lp);
            }
        } else {
            panel_enc(EN_PRESET);                       /* (a stray turn would load another sound) */
        }
        if ((s = panel_enc(EN_ALGO)) != 0 && (ui.ly == LAYER_PERF || ui.ly == LAYER_GLO)) {   /* (one read: #102) */
            track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
            combo = 1;                                  /* (Skiff U1) PERFORM, GLO: ALGORITHM the track (its keys,
                                                         * GLO's four LEVELs follow; their OCT-: no one track's).
                                                         * The others drop it (OCT- puts back the layer's track) */
        }
        if (glo && (sel = panel_enc(EN_SELECT)) != 0)   /* GLO + SELECT: the tempo, a combo (OCT- puts it back) */
            combo = 1;
    } else if ((kq = (uint32_t)layer_knobs_quiet()) != 0) {   /* a layer letting go: KNOB 1..4 are nobody's (#39) */
        for (k = 0; k < 4u; k++)
            if (panel_enc(EN_K1 + k) != 0)
                combo = 1;                              /* (with the button let go this frame: no tap) */
    }
    lytap = layer_gesture(now, combo);
    layer_show();
    layer_keys(lkeys);
    rec_clear_poll();                                   /* ((Skiff U2) REC's CLEAR: held) */
    for (k = 0; k < 4u; k++)
        if (ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (home == BT_TAP && ui.menu) {                    /* HOME (pressed, or held) closes the menu, from ABOUT too; */
        menu_close();                                   /* the release of the hold that opened it is no tap */
        home = BT_NONE;                                 /* (menu_close went HOME already) */
    } else if (home == BT_HOLD) {                       /* HOME held: leave the menu; or (Skiff, DESIGN.md: HOME held */
        if (ui.menu)                                    /* is SHIFT) the menu opens when HOME is let go, so a SHIFT */
            menu_close();                               /* combo never races it */
        else
            home_long = 1;
    }
    {                                                   /* (Skiff U5, A1) HOME held alone a moment: SHIFT's map of the
                                                         * page (not over a layer; HINTS OFF: none); past the hold too
                                                         * (home_long: let go, the MENU), a combo ends it */
        uint8_t sh = ui.home_t0 && (!(ui.home_t0 & 2u) || home_long) && !ui.menu && !ui.confirm && !name_on() && !ui.layer &&
                     !ui_hints_off && now - (ui.home_t0 & ~3u) > 250u * 1000u * FM1_TICKS_PER_US;
#if BOARD_SMK37
        sh = sh || (smk_sh_t0 && !(smk_sh_t0 & 2u) && !ui.menu && !ui.confirm && !name_on() && !ui.layer &&
                    !ui_hints_off && now - (smk_sh_t0 & ~3u) > 250u * 1000u * FM1_TICKS_PER_US);
#endif
        if (sh != ui.shift) {
            ui.shift = sh;
            ui.force = 1;
        }
    }
    if (!ui.shift && home_long && ((fm1_in.buttons >> panel.btn[B_HOME]) & 1u) && !ui.menu && !ui.confirm && !name_on())
        ui_say("LET GO: MENU", "");                     /* (held on: the hint stays; (U5) the map's footer says it) */
    if (((pressed >> panel.btn[B_PLAY]) & 1u) && (ui.menu || ui.confirm))
        play_press();                                   /* (Skiff U1) PLAY over the menu and a dialog too (they return
                                                         * before the buttons below) */
#if FELUCCA_TAPE
    if(ui.menu==3){tape_page_on=1;tape_ui_input(notes|lkeys,pressed,oct);return;}
    tape_page_on=0;
#endif
    if (ui.menu) {                                   /* SAVE / REC taps do nothing here (HOME: above) */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || seq_counting()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD)                                /* SAVE held: undo, one level back (ui.c undo_step) */
        undo_step(0);                                   /* (Skiff: one level back; OCT- / OCT+ then: ui.undo_w) */
    else if (save == BT_TAP && !ui.confirm)             /* SAVE acts on release (a hold is the undo) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (oct & 2u) {
            uint32_t kind = ui.confirm;
            ui.confirm = 0;
            ui.force = 1;
            if (kind == CF_OVR_PROJ) {                  /* overwrite: the NAME screen writes it */
                name_open(NK_PROJ_SAVE, ui.confirm_trk & 3u);
            } else if (kind == CF_OVR_USER) {
                name_open(NK_USER_SAVE, ui.confirm_trk);
            } else if (kind == CF_ERASE_USER) {
                up_ui(1u, ui.confirm_trk);
            } else if (kind == CF_ERASE_BANK) {         /* (Skiff phase 5) the pool view's CLEAR */
                pool_clear(ui.confirm_trk);
            } else if (kind == CF_EMPTY_BANK) {         /* ((2.0, #46) .. EMPTY, CLEAR UNUSED */
                pool_empty(ui.confirm_trk);
            } else if (kind == CF_CLEAR_UNUSED) {
                pool_clear_unused();
            } else if (kind == CF_CLEAR_ENTRY) {        /* (Skiff phase 5) SONG */
                song_clear(ui.confirm_trk);
            } else if (kind == CF_CLEAR_MOTION) {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                undo_label = "AUTOMATION"; load_begin(t, UNDO_PAT); motion_clear(t); load_end(t); ui_message("AUTOMATION CLEARED");
            } else if (kind == CF_INIT_SOUND) {
                undo_label = "INIT"; sound_init();   /* ((Skiff phase 7) DRUM-X: the factory kit too) */
            } else if (kind == CF_LOAD_PROJ) {
                project_load(ui.confirm_trk & 3u);
            } else if (kind == CF_ERASE_PROJ) {         /* ((1.2.1, #6) SAVE > PROJECT's DELETE, NEW) */
                (void)project_erase(ui.confirm_trk & 3u);
            } else if (kind == CF_NEW_PROJ) {
                project_new();
            } else if (kind == CF_RESET_SET) {          /* ((Skiff U4, A3) MENU > SYSTEM > RESET SETTINGS) */
                settings_reset();
                ui_message("SETTINGS RESET");
            } else if (kind == CF_STORE_SCENE) {        /* (Skiff U2) PERFORM: SAVE + a white key over a scene */
                scene_saved(ui.confirm_trk);
            } else {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                undo_label = "CLEAR";
                char b[24] = "PATTERN 00 CLEARED";     /* ((Skiff U4) which one: its number) */
                uint32_t id = trk_pat(ui.confirm_trk % NTRK);
                load_begin(t, UNDO_PAT);
                track_defaults_steps(t);
                load_end(t);
                t->nheld = 0;                           /* and the latched arp chord */
                t->arp_phys = 0;
                if (id < NPAT) {
                    pat_num(b + 8, id);
                    b[10] = ' ';
                } else {
                    str_cpy(b, "PATTERN CLEARED", sizeof b);
                }
                ui_message(b);
            }
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (home == BT_TAP)
        layer_unlock();                                 /* ((Skiff U2) HOME tapped: a locked layer closes, as before) */
    if (home == BT_TAP && ui.home) {                    /* Skiff: HOME on HOME, the tracks <-> the scope (ui_home.c) */
        ui.home_scope ^= 1u;
        ui.force = 1;
    } else if (home == BT_TAP)                          /* HOME acts on release: a hold opens the menu */
        go_home();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < FM1_NBTN; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            play_press();
            break;
        case B_REC:                                     /* (Skiff U2) arms / disarms on the press, in a layer too; */
            if (!rec_own)                               /* SHIFT + REC: its layer, locked (above) */
                rec_tap();
            break;
        case B_SEQ:
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (act_cols() || layer_set_open() || ui.ml.on)   /* action pages: enter / back (below); SET layers: OCT-;
                                                         * ((1.3) MIDI LEARN: below) */
                break;
            if (shift_held() && song.seq_mode && !ui.home &&   /* ((2.0) SHIFT + OCT- / OCT+ on NOTES, FEEL: COPY, */
                (step_page(cur_page()->graph) || cur_page()->graph == GR_FEEL)) {   /* PASTE (a SHIFT combo) */
                shift_mark();
                home_long = 0;
                if (b == B_OCTDN)
                    sel_copy(TSEL);
                else
                    sel_paste(TSEL);
                break;
            }
            if (ui.undo_w) {                            /* (Skiff) the undo window: OCT- further back, OCT+ redo */
                undo_step(b == B_OCTUP);
                break;
            }
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (act_cols() && oct && ui.undo_w) {               /* ((Skiff U2) the undo window first, as its box says:
                                                         * OCT- further back, OCT+ redo) */
        undo_step((oct & 2u) != 0u);
    } else if (act_cols() && (oct & 2u)) {              /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (cur_page()->graph == GR_SONG)               /* ((Skiff phase 5) SONG: the pick, or the entry staged) */
            song_back();
        else if (ui.act)
            ui.act = ui.act2 = 0;
        else if (ui.ev_del)                             /* ((Skiff U2) AUTO LIST's DELETE) */
            ui.ev_del = 0;
        else
            go_home();
    } else if (ui.ml.on && oct) {                       /* ((1.3) MIDI LEARN: OCT- clears the picked CC, OCT+ done) */
        ml_oct(oct);
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: the keys are the grid's) */
#if FELUCCA_SLICE
    if (notes && slice_page_on())                       /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif
    lock_keys();                                        /* (a grid key on a hit let go: the hit goes) */
    if (song.grid) {
        grid_keys(notes);
    } else if (song.seq_mode && step_page(cur_page()->graph)) {   /* NOTES (not FEEL: its knobs only) */
        static uint32_t wr_hint_ms;                     /* (Skiff U1) "REC: WRITE STEPS" said last (0: never) */
        int armed = (song.rec >> song.sel) & 1u;
        if (live_rec_sel() || !armed) {                 /* armed and playing: the keys record live, not
                                                         * into the cursor step too; not armed (1.2, #133): they
                                                         * play and write nothing, (2.0) a note of the selection's
                                                         * they pick (sel_keys) */
            ui.entry_open = 0;
            if (!armed && notes && sel_keys(notes))
                ui.force = 1;
            else if (!armed && notes && !ui.msg_t && (!wr_hint_ms || fm1_ms - wr_hint_ms > 10000u)) {
                ui_message("[REC] WRITE STEPS");        /* (Skiff U1) .. so say how they write, now and then */
                wr_hint_ms = fm1_ms | 1u;
            }
        } else
            seq_entry(notes);                           /* armed and stopped: step recording at the cursor */
    }

    /* #102: the knobs a layer took above are not read again here. The TIMER5 scan (10 kHz) counts detents while this
     * pass runs, so one landing after the layer's read went to the page as well: KNOB 1..4 edited the layer's control
     * and the page under it (HOME's sound, motion recorded), PRESETS could load a sound. They stay for the next pass */
    kq |= lay;
    if (!lay && (s = panel_enc(EN_PRESET)) != 0) {
        if (shift_held() && sel_shift(s)) {             /* ((2.0) SHIFT + PRESETS on NOTES, FEEL: the selection, a */
            shift_mark();                               /* SHIFT combo) */
            home_long = 0;
        } else {
            presets_turn(s);
        }
    }
    if (!lay && (s = panel_enc(EN_ALGO)) != 0)     /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = glo ? sel : panel_enc(EN_SELECT)) != 0) {   /* SELECT: the tempo with GLO held (or BPM LOCK OFF, Felucca's
                                                         * way); (Skiff phase 8, rule 3) else the pages */
        int sheet = !lay && !ui.home && sheet_of(cur_page()->graph);   /* (Skiff U1) a sheet's rows: SELECT is their
                                                                        * only way, BPM LOCK or not */
        if (glo || (!(ui_prefs & PREF_BPM_LOCK) && !sheet)) {
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
            ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
        } else if (lay) {
            ;                                           /* (another layer held: not the page under it) */
        } else if (!ui.home) {
            select_turn(s);
        } else {                                        /* MENU > BPM LOCK ON (#58), on HOME (no pages): only with GLO
                                                         * held (and on GLO > GLOBAL, GLO's G4 TAP); says so once a burst */
            if (!lock_ms || fm1_ms - lock_ms > 1000u)
                ui_way("BPM LOCKED", "GLO + SELECT: TEMPO");   /* ((Skiff U4) and how) */
            lock_ms = fm1_ms | 1u;
        }
    }
    for (k = 0; k < 4u && !kq; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        uint64_t held;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (mom_down())                                 /* ((1.3) LFO + a knob, MOMENTARY or not: LFO's release opens no
                                                         * page) */
            ui.pg_down &= (uint16_t)~(1u << panel.btn[B_LFO]);
        if ((held = lock_held()) != 0u) {               /* a step held on NOTES: KNOB k locks (parameter locks) */
            lock_turn(k, s, held);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (mom_down() && mom_turn(k, s)) {             /* ((1.3) MOMENTARY: LFO held; no automation, no undo) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
            continue;
        }
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_BANK || pg->graph == GR_MOD) && k == 0u)
            || (pg->graph == GR_SLICES && k < 2u) || pg->graph == GR_SONG || sheet_of(pg->graph)) {   /* (not an empty
                                                         * column; (Skiff) SONG's four; a sheet's four) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)param_turn(d, *vp, accel(EN_K1 + k, s, d->fmt == F_ENUM ? 0 : d->max - d->min));
            motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
            ml_knob(TSEL, (uint32_t)(vp - TSEL->p));
        } else {
            edit_param(k, s);
        }
    }
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot). A piano key cancels at
 * any time: the keys are not part of what is taught, so one works whatever the buttons' table says */
#define SETUP_IDLE_MS 30000u
#define SETUP_GIVE_UP(t0) (fm1_ms - (t0) > SETUP_IDLE_MS || fm1_in.notes != 0u)
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        GFX_HOOK_ALIGN(0, 0, 240, 0, AL_H | AL_N(2), "calibration title centred");
        GFX_HOOK_ALIGN(0, HEAD_MY + AF_M_CAP_Y, 0, HEAD_MY + AF_M_CAP_Y + AF_M_CAP_H, AL_V | AL_PASS,
                       "header icon on its title's line");
        cv_icon_mid(x, 12, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, HEAD_MY, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "teach each button and knob", T_MID, 1);   /* ((Skiff) the mockup's case) */
    draw_text_box(0, 206, 240, &AF_S, "ANY KEY: CANCEL", T_DIM, 1);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (SETUP_GIVE_UP(t0))
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (SETUP_GIVE_UP(t0))
                goto timeout;
        }
        for (id = 0; id < FM1_NBTN; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < FM1_NENC; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (SETUP_GIVE_UP(t0))
                goto timeout;
            for (e = 0; e < FM1_NENC; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < FM1_NENC)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}

#include "ui_dx.c"                                      /* (Skiff phase 7, 8) EDIT > SOUND: the DRUM-X sound sheet */
#include "ui_fm6.c"                                     /* (FM6 LAB) EDIT > OPERATOR, ALL OPS: FM6's patch */

/* (FM6 LAB) the sheets (ui.c sheet_t): their pages' every hook (the knobs, cards, panel, header, PRESETS, SELECT,
 * SHIFT's map) goes through here */
static const sheet_t SHEETS[] = {
    {GR_DXSND, ENGI_DRUMX, DX_NWIN, &dx_ui_win, dx_knob, dx_cards, dx_sig, graph_dx, dx_title, dx_lane_turn, "DRUM"},
    {GR_FMOP, ENGI_FM6, FM_NWIN, &fm_ui[1], fm_op_knob, fm_op_cards, fm_sig, graph_fm_op, fm_op_title, fm_op_turn,
     "OPERATOR"},
    {GR_FMALL, ENGI_FM6, FM_NWIN, &fm_ui[2], fm_all_knob, fm_all_cards, fm_sig, graph_fm_all, fm_all_title, fm_op_turn,
     "OPERATOR"},
};
static const sheet_t *sheet_of(uint32_t g)
{
    uint32_t i;
    for (i = 0; i < sizeof SHEETS / sizeof SHEETS[0]; i++)
        if (SHEETS[i].graph == g)
            return &SHEETS[i];
    return 0;
}
