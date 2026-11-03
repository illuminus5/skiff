/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff 1.3) MIDI LEARN, from Felucca 1.5 (its Discussion #170): a controller's CC set to any knob's parameter, on the
 * device. MENU > MIDI > MIDI LEARN (OCT+) turns it on and closes the MENU. On: a knob turned on any page (HOME, the
 * sound's pages, MIXER, GLO's levels: the parameter of the track it edits) picks that parameter, as it edits it; the
 * next CC that comes (midi_control.c midi_learned: any channel MIDI IN hears) is set to it, and stays until something
 * else is learned on it. Then turn the next knob. OCT- clears the picked parameter's CC, OCT+ is done. The footer says
 * what is picked and its CC (ml_line). The map: midi_control.c (ML_N entries in engine 1's favourites row, kept with
 * the settings, saved as each one changes); one CC sets one parameter, a parameter has one CC: learning one again
 * replaces both. MENU > MIDI > LEARN CLEAR clears them all. Its state: ui.ml (ui.c) */
static uint8_t *const ml_tab = favorites.factory[ENGI_DIGITAL] + 2;
static volatile uint8_t *const ml_io = ui.ml.io;         /* (midi_control.c ml_arm, ml_heard) */
typedef char ml_fits[2u + 3u * ML_N <= sizeof favorites.factory[0] ? 1 : -1];

static uint32_t ml_code(uint32_t id) { return id >= P_E0 ? ML_E0 + id - P_E0 : id + 1u; }
static void ml_put(uint32_t i, uint32_t code, uint32_t cc, uint32_t k)   /* (the ISR reads an entry: never half of one) */
{
    fm1_irq_off();
    ml_tab[3u * i] = (uint8_t)code;
    ml_tab[3u * i + 1u] = (uint8_t)cc;
    ml_tab[3u * i + 2u] = (uint8_t)k;
    fm1_irq_on();
}
static int ml_is(uint32_t i, uint32_t k, uint32_t id)    /* entry i is parameter id of track k */
{
    return ml_tab[3u * i] == ml_code(id) && ml_tab[3u * i + 2u] == k;
}
/* the CC of parameter id on track k, + 1; 0 none */
static uint32_t ml_cc_of(uint32_t k, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < ML_N; i++)
        if (ml_is(i, k, id))
            return ml_tab[3u * i + 1u] + 1u;
    return 0;
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += ml_tab[3u * i] != 0u;
    return n;
}
/* takes away cc's entry and parameter id of track k's (ML_NOCC: no CC to match, a learned one is 0..127); 1 = one went */
#define ML_NOCC 0x80u
static int ml_drop(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, gone = 0;
    for (i = 0; i < ML_N; i++)
        if (ml_tab[3u * i] && (ml_tab[3u * i + 1u] == cc || ml_is(i, k, id))) {
            ml_put(i, 0, 0, 0);
            gone = 1;
        }
    return gone;
}
/* cc -> parameter id of track k, in place of what either had; 1 learned, 0 no room (ML_N others), 2 a CC never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i;
    if (!ml_free_cc(cc) || k >= NTRK || id >= P_COUNT)
        return 2;
    (void)ml_drop(cc, k, id);
    for (i = 0; i < ML_N; i++)
        if (!ml_tab[3u * i]) {
            ml_put(i, ml_code(id), cc, k);
            return 1;
        }
    return 0;
}
/* every learned CC cleared (MENU > MIDI > LEARN CLEAR), the settings saved; 1 = there were */
static int ml_clear_all(void)
{
    uint32_t i, n = ml_count();
    for (i = 0; i < ML_N; i++)
        ml_put(i, 0, 0, 0);
    if (n) {
        ui.foot_sig = 0;
        settings_save();                         /* (deferred while playing) */
    }
    return n != 0u;
}
static void ml_off(void)
{
    ui.ml.on = ui.ml.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    ui.foot_sig = 0;
}
static void ml_start(void)                       /* MENU > MIDI > MIDI LEARN */
{
    ui.ml.on = 1;
    ui.ml.pick = 0;
    ml_heard = 0;
    ui.foot_sig = 0;
    ui_message("MIDI LEARN ON");
}
/* (2.2) MENU > MIDI > CC OUT: a knob turned a parameter with a CC (midi_control.c cc_out_of: learned, or the standard
 * map's): that CC out on the track's channel, with CC OUT ON, and always on an EXT track (its sound is out there);
 * none while MIDI LEARN picks, the same value twice none. Due at once (behind what the audio ISR queued) */
static void cc_knob(const track_t *t, uint32_t id)
{
    uint32_t k = trk_index(t), c, v;
    if (ui.ml.on || id >= P_COUNT || trk_moff(k) || (!(MIDI_BYTE & MO_CC_ON) && !trk_ext(k)) || !(c = cc_out_of(t, id)))
        return;
    v = cc_value(t, id);
    if (ui.cc_last == (k << 16 | c << 8 | v) + 1u)
        return;
    ui.cc_last = (k << 16 | c << 8 | v) + 1u;
    fm1_irq_off();
    (void)mo_put(0x0Bu | (0xB0u | trk_midi_ch(k)) << 8 | c << 16 | v << 24, mo.now);
    fm1_irq_on();
}
/* a knob turned parameter id of track t (ui_input.c, ui_layer.c): picked while learning; (2.2) its CC out */
static void ml_knob(const track_t *t, uint32_t id)
{
    cc_knob(t, id);
    if (!ui.ml.on || id >= P_COUNT)
        return;
    ui.ml.trk = (uint8_t)trk_index(t);
    ui.ml.id = (uint8_t)id;
    ui.ml.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
    ui.foot_sig = 0;
}
/* the picked parameter's name: "T2 CUTOFF" (b: 12 bytes) */
static void ml_name(char *b)
{
    b[0] = 'T';
    b[1] = (char)('1' + ui.ml.trk % NTRK);
    b[2] = ' ';
    str_cpy(b + 3, track_desc(&trk[ui.ml.trk % NTRK], ui.ml.id)->label, 9);
}
/* each frame: a CC heard for the picked parameter is learned */
static void ml_poll(void)
{
    uint32_t cc = ml_heard, r;
    char a[12], b[12];
    if (!cc)
        return;
    ml_heard = 0;
    if (!ui.ml.on || !ui.ml.pick)
        return;
    cc--;
    r = ml_learn(cc, ui.ml.trk, ui.ml.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL");
        return;
    }
    str_cpy(a, "CC", 3);
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " -> ", 5);
    ml_name(b);
    ui_say(a, b);
    ui.ml.pick = 0;
    ml_arm = 0;
    ui.foot_sig = 0;
    settings_save();                             /* (deferred while playing) */
}
/* OCT- / OCT+ while learning (not on an action page): OCT- clears the picked parameter's CC, OCT+ is done */
static void ml_oct(uint32_t oct)
{
    char b[12];
    if (oct & 2u) {
        ml_off();
        ui_message("LEARN DONE");
    } else if (oct & 1u) {
        if (!ui.ml.pick || !ml_drop(ML_NOCC, ui.ml.trk, ui.ml.id)) {
            ui_message(ui.ml.pick ? "NO CC TO CLEAR" : "TURN A KNOB");
            return;
        }
        ml_name(b);
        ui_say(b, " CC CLEARED");
        ui.foot_sig = 0;
        settings_save();
    }
}
/* the footer's line while learning: "LEARN: TURN A KNOB", "T2 CUTOFF: SEND A CC", "T2 CUTOFF = CC74" (b: 24 bytes) */
static void ml_line(char *b)
{
    uint32_t cc;
    if (!ui.ml.pick) {
        str_cpy(b, "LEARN: TURN A KNOB", 24);
        return;
    }
    ml_name(b);
    cc = ml_cc_of(ui.ml.trk, ui.ml.id);
    if (!cc) {
        str_cpy(b + str_len(b), ": SEND A CC", 12);
        return;
    }
    str_cpy(b + str_len(b), " = CC", 6);
    fmt_int(b + str_len(b), (int32_t)cc - 1);
}
