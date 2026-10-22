/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU's rows and their settings: what ui_menu.c draws and steps, and what the editor reads and sets (editor_menu.c
 * MENU_DESC / MENU_SET). One apply path for both: menu_put. Grouped: the screen (COLOR, LARGE, ANIM, LEDS; 1.1.5:
 * SCREEN OFF), the
 * controls (HOLD: the layer threshold, KNOB ACCEL, FX LATCH, BPM LOCK; 1.2: SCALE LEDS, the keys), the sound (SPEAKER EQ: FLAT LOWCUT BASS+,
 * USB LEVEL; 1.1: CLICK, CLICK LEVEL, COUNT-IN), USB SERIAL, RESTORE LAST (1.2), then CALIBRATION (the setup screen: HARDWARE CALIBRATION) and ABOUT, the two rows with no
 * value (MI_VALUES: the rows before them hold one). 1.0.5: in four tabs (MI_TAB). */
#if BOARD_SMK37
/* (SMK-37 Pro, SMK37_UX.md) KEY VELOCITY in CONTROL (question 8); no LEDS row (button LEDs A: on or off only): MI_LEDS
 * past MI_COUNT, so its setting stays and no tab shows it */
enum { MI_COLOR, MI_PHEAD, MI_LARGE, MI_ANIM, MI_SCROFF, MI_HINTS, MI_HOLD, MI_ACCEL, MI_BPMLOCK, MI_SCLLED, MI_PREVIEW,
       MI_KVEL, MI_LOWCUT, MI_USB, MI_CLICK, MI_CLKLVL, MI_COUNTIN, MI_TUNE, MI_CLOCK, MI_ROUTE,
       MI_LEARN, MI_LCLEAR, MI_TCH1, MI_TCH2, MI_TCH3, MI_TCH4, MI_TCH5, MI_TCH6, MI_SERIAL, MI_RESTORE, MI_CPU, MI_CLK,
#if FELUCCA_USB_ROUTING
       MI_USB_LEFT, MI_USB_RIGHT,
#if FELUCCA_USB_6CH
       MI_USB_3, MI_USB_4, MI_USB_5, MI_USB_6,
#if UAC_CHANNELS > 6
       MI_USB_7, MI_USB_8,
#endif
#if UAC_CHANNELS > 8
       MI_USB_9, MI_USB_10,
#endif
#endif
#endif
       MI_PANEL,
       MI_RESET, MI_ABOUT, MI_COUNT, MI_LEDS };
static const char *const KVEL_N[SMK_KV_N] = {"DYNAMIC", "SOFT", "HARD", "FIXED 64", "FIXED 100", "FIXED 127"};
#else
enum { MI_COLOR, MI_PHEAD, MI_LARGE, MI_ANIM, MI_LEDS, MI_SCROFF, MI_HINTS, MI_HOLD, MI_ACCEL, MI_BPMLOCK, MI_SCLLED,
       MI_PREVIEW, MI_LOWCUT, MI_USB, MI_CLICK, MI_CLKLVL, MI_COUNTIN, MI_TUNE, MI_CLOCK, MI_ROUTE,
       MI_LEARN, MI_LCLEAR, MI_TCH1, MI_TCH2, MI_TCH3, MI_TCH4, MI_TCH5, MI_TCH6, MI_SERIAL, MI_RESTORE, MI_CPU, MI_CLK,
#if FELUCCA_USB_ROUTING
       MI_USB_LEFT, MI_USB_RIGHT,
#if FELUCCA_USB_6CH
       MI_USB_3, MI_USB_4, MI_USB_5, MI_USB_6,
#if UAC_CHANNELS > 6
       MI_USB_7, MI_USB_8,
#endif
#if UAC_CHANNELS > 8
       MI_USB_9, MI_USB_10,
#endif
#endif
#endif
       MI_PANEL,
       MI_RESET, MI_ABOUT, MI_COUNT };
#endif   /* ((Skiff U4, A3) HINTS; RESET SETTINGS, a row with no value: its dialog) */   /* ((Skiff U3, R8, R2) CLOCK, MIDI IN, TUNE: the unit's, GLOBAL's and SYSTEM's before; CPU: read) */   /* (1.1: the metronome's rows in
                                                                                             * AUDIO; 1.2: RESTORE LAST in SYSTEM, SCALE LEDS
                                                                                             * in CONTROL) */
#define MI_VALUES MI_PANEL
static const char *const MI_NAME[MI_COUNT] = {"THEME", "PLAYHEAD", "LARGE", "ANIM",
#if !BOARD_SMK37
                                              "LEDS",
#endif
                                              "SCREEN OFF", "HINTS", "HOLD", "KNOB ACCEL", "BPM LOCK",
                                              "SCALE LEDS", "STEP PREVIEW",
#if BOARD_SMK37
                                              "KEY VELOCITY",
#endif
                                              "SPEAKER EQ", "USB LEVEL", "CLICK", "CLICK LEVEL", "COUNT-IN", "TUNE",
                                              "CLOCK", "MIDI IN", "MIDI LEARN", "LEARN CLEAR",
                                              "T1 CHANNEL", "T2 CHANNEL", "T3 CHANNEL", "T4 CHANNEL", "T5 CHANNEL", "T6 CHANNEL",
                                              "USB SERIAL", "RESTORE LAST", "CPU", "CPU CLOCK",
#if FELUCCA_USB_ROUTING
#if FELUCCA_USB_6CH
                                              "USB CH 1", "USB CH 2", "USB CH 3", "USB CH 4", "USB CH 5", "USB CH 6",
#if UAC_CHANNELS > 6
                                              "USB CH 7", "USB CH 8",
#endif
#if UAC_CHANNELS > 8
                                              "USB CH 9", "USB CH 10",
#endif
#else
                                              "USB LEFT", "USB RIGHT",
#endif
#endif
                                              "CALIBRATION", "RESET SETTINGS",
                                              "ABOUT"};
/* 1.0.5: the MENU's tabs (ui_menu.c: ALGORITHM steps between them, PRESETS among one tab's rows; the editor gets a
 * row's tab after its MENU_DESC reply). A tab's rows follow each other in MI order (tests/ui_test.c checks it); the
 * page shows MTAB_ROWS of them (ui_menu.c fits them; (1.3) MIDI's ten scroll, the cursor's row the last shown). A new
 * row joins a tab here, a new tab is appended (its index is what the editor is told) */
enum { MTAB_DISPLAY, MTAB_CONTROL, MTAB_AUDIO, MTAB_SYSTEM, MTAB_MIDI, MTAB_COUNT };   /* ((Skiff 1.3) MIDI, appended: its
                                                         * rows sit before SYSTEM's in MI order, as value rows must) */
#define MTAB_ROWS 6u
static const char *const MTAB_NAME[MTAB_COUNT] = {"DISPLAY", "CONTROL", "AUDIO", "SYSTEM", "MIDI"};
static const uint8_t MI_TAB[MI_COUNT] = {
    MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY, MTAB_DISPLAY,                 /* COLOR (Skiff #27) PLAYHEAD LARGE ANIM */
#if !BOARD_SMK37
    MTAB_DISPLAY,                                                           /* LEDS ((Skiff U2) STYLE gone: BLOCKS is the
                                                                             * only look) */
#endif
    MTAB_DISPLAY,                                                           /* SCREEN OFF (1.1.5) */
    MTAB_DISPLAY,                                                           /* (Skiff U4) HINTS */
    MTAB_CONTROL, MTAB_CONTROL, MTAB_CONTROL,                               /* HOLD KNOB ACCEL BPM LOCK ((Skiff U2) FX
                                                                             * LATCH gone: a locked FX layer latches) */
    MTAB_CONTROL,                                                           /* SCALE LEDS (1.2) */
    MTAB_CONTROL,                                                           /* (Skiff 1.3) STEP PREVIEW */
#if BOARD_SMK37
    MTAB_CONTROL,                                                           /* (SMK-37) KEY VELOCITY */
#endif
    MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO, MTAB_AUDIO,             /* SPEAKER EQ, USB LEVEL, CLICK, CLICK LEVEL,
                                                                             * COUNT-IN */
    MTAB_AUDIO,                                                             /* (Skiff U3) TUNE */
    MTAB_MIDI, MTAB_MIDI,                                                   /* (Skiff U3) CLOCK, MIDI IN ((1.3) from
                                                                             * CONTROL) */
    MTAB_MIDI, MTAB_MIDI,                                                   /* (1.3) MIDI LEARN, LEARN CLEAR */
    MTAB_MIDI, MTAB_MIDI, MTAB_MIDI, MTAB_MIDI, MTAB_MIDI, MTAB_MIDI,       /* (1.3) T1..T6 CHANNEL */
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,                     /* USB SERIAL, RESTORE LAST, (Skiff U3) CPU,
                                                                             * (Skiff) CPU CLOCK (SYSTEM's seven scroll) */
#if FELUCCA_USB_ROUTING
    MTAB_SYSTEM, MTAB_SYSTEM,                                             /* USB capture source selection */
#if FELUCCA_USB_6CH
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,
#if UAC_CHANNELS > 6
    MTAB_SYSTEM, MTAB_SYSTEM,
#endif
#if UAC_CHANNELS > 8
    MTAB_SYSTEM, MTAB_SYSTEM,
#endif
#endif
#endif
    MTAB_SYSTEM, MTAB_SYSTEM, MTAB_SYSTEM,                                  /* CALIBRATION, (U4) RESET SETTINGS, ABOUT */
};
typedef char mtab_fits_ui[sizeof ui.menu_row >= MTAB_COUNT ? 1 : -1];   /* (ui.c: the row last picked per tab) */
static uint32_t mtab_first(uint32_t t)                 /* a tab's first row */
{
    uint32_t i = 0;
    while (i < MI_COUNT && MI_TAB[i] != t)
        i++;
    return i;
}
static uint32_t mtab_rows(uint32_t t)                  /* .. and how many it has */
{
    uint32_t i, n = 0;
    for (i = 0; i < MI_COUNT; i++)
        n += MI_TAB[i] == t;
    return n;
}
/* a row OCT+ acts on, with no value to step: CALIBRATION, RESET SETTINGS, ABOUT; (1.3) MIDI LEARN (on, the MENU closed;
 * midi_learn.c), LEARN CLEAR */
static int menu_act(uint32_t row) { return row >= MI_VALUES || row == MI_LEARN || row == MI_LCLEAR; }
#define MI_TCH(row) ((row) >= MI_TCH1 && (row) <= MI_TCH6)   /* (1.3) T1..T6 CHANNEL: 0..15, seq.c trk_midi_ch */
/* the two-valued rows: a bit of ui_prefs (PREF_REC: of ui_rec_prefs, ui.c PREF_SCALE_LEDS) and its names (bit clear =
 * the default, bit set). A step up (a knob right,
 * OCT+) = ON on the ON / OFF rows (the switch's knob to the right; ANIM, USB SERIAL: their bit clears), else the
 * second name (STYLE LINE, USB LEVEL FIXED); a step down the other (menu_step) */
typedef struct { uint8_t row; uint16_t bit; const char *name[2]; } menu_flag_t;
static const menu_flag_t MENU_FLAGS[] = {
    {MI_LARGE, PREF_LARGE, {"OFF", "ON"}},            /* #15 / Discussion #80: ON, big knob labels and values (ui.c large_kind) */
    {MI_ANIM, PREF_ANIM_OFF, {"ON", "OFF"}},          /* #46: OFF, values snap (ui_draw.c roll_note, ui_graph.c pr_follow) */
    {MI_ACCEL, PREF_ACCEL, {"OFF", "ON"}},            /* #52: ui_input.c accel */
    {MI_USB, PREF_USB_FIXED, {"MASTER", "FIXED"}},     /* fx.c fx_usb_fixed: FIXED, USB at the full level */
    {MI_BPMLOCK, PREF_BPM_LOCK, {"OFF", "ON"}},        /* #58: ON, SELECT sets the tempo with GLO held only (ui_input.c) */
    {MI_SERIAL, PREF_SERIAL_OFF, {"ON", "OFF"}},       /* #67: OFF, no serial console (usb_serial_apply) */
    {MI_RESTORE, PREF_RESTORE_OFF, {"ON", "OFF"}},     /* 1.2, #130: OFF, no autosave, power-on as new (project.c) */
    {MI_SCLLED, PREF_SCALE_LEDS, {"OFF", "ON"}},      /* 1.2, Discussion #127: ON, the keys show the scale (ui_input.c) */
};
/* SPEAKER EQ (settings.lowcut, fx.c fx_lowcut): an EQ on the master for the small speaker, not a speaker switch
 * (#42: "OFF" read as the speaker off). FLAT is the old OFF (0, stored as before). The built-in speaker cannot be
 * turned off from the firmware (no amp enable or mute line is known); the EQ reaches
 * the headphone / line out and USB audio too (one DAC, uac_tap reads the master) */
static const char *const SPK_EQ[3] = {"FLAT", "LOWCUT", "BASS+"};
static const char *const HOLD_N[4] = {"0.3 s", "0.4 s", "0.5 s", "0.6 s"};   /* (the editor's names; the menu draws its own) */
/* 1.1 (Discussion #131): the metronome (click.c) and the count-in (seq.c); ui.c ui_rec_prefs holds them */
static const char *const CLICK_N[3] = {"OFF", "REC", "ON"};          /* REC: while a track is armed and playing */
static const char *const CLKLVL_N[3] = {"LOW", "MID", "HIGH"};
static const char *const COUNTIN_N[3] = {"OFF", "1 BAR", "2 BARS"};
/* 1.1.5: SCREEN OFF (ui.c scr_*: the screen and backlight off after this long without panel input; 30 MIN the default) */
static const char *const SCROFF_N[5] = {"NEVER", "5 MIN", "15 MIN", "30 MIN", "60 MIN"};

/* MENU > USB SERIAL (#67). The serial console is a developer tool (README: FELUCCA_CDC). ON (the default) presents it,
 * the descriptors byte for byte as before; OFF re-enumerates as audio + MIDI only, device class 0 (the bytes of a
 * FELUCCA_CDC=0 build): macOS 13-15 then attach their USB audio driver (with the console Apple's CDC composite driver
 * takes the device and the audio input never appears). USB-MIDI stays, and with it the editor, the update installer
 * and the soft key (SysEx on EP1). Applied at boot before USB starts (main.c: enumerated with it from the start, never
 * on then off) and while the menu is closed (ui_input: one re-enumeration when it closes, not one per KNOB 1 step).
 * A change from the editor (MENU_SET) waits 200 ms (menu_serial_at) so its reply leaves before the device drops off
 * the bus: the whole device re-enumerates, USB-MIDI and audio too */
static uint32_t menu_serial_at;                        /* fm1_ms | 1 of the editor's change; 0 none */
static void usb_serial_apply(void)
{
#if FELUCCA_CDC
    if (menu_serial_at && fm1_ms - menu_serial_at < 200u)
        return;
    menu_serial_at = 0;
    usb_cdc_switch(FELUCCA_CDC_DEFAULT && !(ui_prefs & PREF_SERIAL_OFF));   /* (a FELUCCA_CDC_DEFAULT=0 build: off) */
#endif
}
static const menu_flag_t *menu_flag(uint32_t row)
{
    uint32_t i;
    for (i = 0; i < NELEM(MENU_FLAGS); i++)
        if (MENU_FLAGS[i].row == row)
            return &MENU_FLAGS[i];
    return 0;
}

/* a row's value as 0..menu_n(row) - 1 in the order the menu steps it (LEDS: OFF DIM LO DIM HI INV, LEDS_MENU) */
static uint32_t menu_n(uint32_t row)
{
#if FELUCCA_USB_ROUTING
    if (row >= MI_USB_LEFT && row <= (FELUCCA_USB_6CH ? MI_USB_LEFT + UAC_CHANNELS - 1u : MI_USB_RIGHT)) return USB_SOURCE_N;
#endif
    return row == MI_COLOR ? NPALETTES + (uint32_t)cpal_stored() :   /* ((Skiff #27) CUSTOM, while stored) */
           row == MI_LOWCUT || (row >= MI_CLICK && row <= MI_COUNTIN) ? 3u :
           row == MI_HOLD ? 4u : row == MI_LEDS ? LEDS_COUNT : row == MI_SCROFF ? NELEM(SCROFF_N) :
           row == MI_CLOCK ? NELEM(N_CLOCK) : row == MI_TUNE ? 101u : row == MI_CPU ? 1u + FELUCCA_CPU2 :
           MI_TCH(row) ? 16u : row == MI_LEARN || row == MI_LCLEAR ? 1u :
#if BOARD_SMK37
           row == MI_KVEL ? SMK_KV_N :
#endif
           2u;
}
static uint32_t menu_get(uint32_t row)
{
#if FELUCCA_USB_6CH
    if(row>=MI_USB_LEFT && row<=MI_USB_LEFT+UAC_CHANNELS-1u) return usb_six_get(row-MI_USB_LEFT);
#endif
    const menu_flag_t *f = menu_flag(row);
    uint32_t i = 0;
    if (f)
        return ((ui_prefs | (uint32_t)ui_rec_prefs << 8) & f->bit) != 0;
    switch (row) {
#if FELUCCA_USB_ROUTING
#if !FELUCCA_USB_6CH
    case MI_USB_LEFT: case MI_USB_RIGHT: return usb_route_get(row - MI_USB_LEFT);
#endif
#endif
    case MI_COLOR: return cpal_on() ? NPALETTES : settings.palette % NPALETTES;   /* ((Skiff #27) CUSTOM: last) */
    case MI_PHEAD: return ui_ph_line != 0;
    case MI_LOWCUT: return settings.lowcut % 3u;
    case MI_HOLD: return settings_hold % 4u;
    case MI_SCROFF: return scr_get();
    case MI_CLICK: case MI_CLKLVL: case MI_COUNTIN: return rp_get(row - MI_CLICK);
    case MI_CLOCK: return (uint32_t)song.g[G_CLOCK] % NELEM(N_CLOCK);
    case MI_ROUTE: return song.g[G_ROUTE] != 0;
    case MI_TCH1: case MI_TCH2: case MI_TCH3: case MI_TCH4: case MI_TCH5: case MI_TCH6: return trk_midi_ch(row - MI_TCH1);
    case MI_HINTS: return ui_hints_off != 0;
    case MI_PREVIEW: return PREVIEW_SET ? 1u : 0u;
    case MI_TUNE: return (uint32_t)clamp(song.g[G_TUNE] + 50, 0, 100);
#if FELUCCA_CPU2
    case MI_CPU: return CPU2_SET ? 1u : 0u;            /* ((Skiff 1.3) 1 CORE, 2 CORES (BETA): from the next power-on) */
#endif
    case MI_CLK: return CLK320_SET ? 1u : 0u;          /* ((Skiff) 360 MHZ, 320 MHZ: from the next power-on) */
#if BOARD_SMK37
    case MI_KVEL: return SMK_KV_BYTE % SMK_KV_N;
#endif
    case MI_LEDS:
        while (i + 1u < LEDS_COUNT && LEDS_MENU[i] != settings_leds)
            i++;
        return i;
    }
    return 0;
}
static const char *menu_vname(uint32_t row, uint32_t v)
{
#if FELUCCA_USB_ROUTING
    if (row >= MI_USB_LEFT && row <= (FELUCCA_USB_6CH ? MI_USB_LEFT + UAC_CHANNELS - 1u : MI_USB_RIGHT)) {
        static const char *const names[USB_SOURCE_N] = {"OFF", "MASTER L", "MASTER R", "MASTER MONO",
            "T1 L", "T1 R", "T1 MONO", "T2 L", "T2 R", "T2 MONO", "T3 L", "T3 R", "T3 MONO",
            "T4 L", "T4 R", "T4 MONO", "T5 L", "T5 R", "T5 MONO", "T6 L", "T6 R", "T6 MONO"
#if FELUCCA_USB_6CH
            , "REV", "DELAY", "CHOR"
#endif
            };
        return names[v < USB_SOURCE_N ? v : 0];
    }
#endif
    const menu_flag_t *f = menu_flag(row);
    if (f)
        return f->name[v & 1u];
    switch (row) {
    case MI_COLOR: return v < NPALETTES ? UI_PALETTES[v].name : "CUSTOM";
    case MI_PHEAD: return v & 1u ? "LINE" : "THEME";
    case MI_LOWCUT: return SPK_EQ[v % 3u];
    case MI_HOLD: return HOLD_N[v & 3u];
    case MI_CLICK: return CLICK_N[v % 3u];
    case MI_CLKLVL: return CLKLVL_N[v % 3u];
    case MI_COUNTIN: return COUNTIN_N[v % 3u];
    case MI_SCROFF: return SCROFF_N[v % NELEM(SCROFF_N)];
    case MI_LEDS: return LEDS_NAME[LEDS_MENU[v % LEDS_COUNT]];
    case MI_CLOCK: return N_CLOCK[v % NELEM(N_CLOCK)];
    case MI_ROUTE:                                     /* ((1.3) CH1-6 with a track set elsewhere: TRACK) */
        if (!(v & 1u) && (ui_mch(0) | ui_mch(1) | ui_mch(2) | ui_mch(3) | ui_mch(4) | ui_mch(5)))
            return "TRACK";
        return N_ROUTE[v & 1u];
    case MI_LCLEAR: case MI_TCH1: case MI_TCH2: case MI_TCH3: case MI_TCH4: case MI_TCH5: case MI_TCH6: {
        static char c[8] __attribute__((section(".pool")));   /* ((1.3) "CH 10"; LEARN CLEAR: "3 CC", "NONE") */
        if (row == MI_LCLEAR && !ml_count())
            return "NONE";
        str_cpy(c, row == MI_LCLEAR ? "" : "CH ", sizeof c);
        fmt_int(c + str_len(c), row == MI_LCLEAR ? (int32_t)ml_count() : (int32_t)v + 1);
        str_cpy(c + str_len(c), row == MI_LCLEAR ? " CC" : "", sizeof c - str_len(c));
        return c;
    }
    case MI_LEARN: return ui.ml.on ? "ON" : "";
#if BOARD_SMK37
    case MI_KVEL: return KVEL_N[v % SMK_KV_N];
#endif
    case MI_HINTS: return v & 1u ? "OFF" : "ON";
    case MI_PREVIEW: return v & 1u ? "ON" : "OFF";
    case MI_TUNE: case MI_CPU: {                       /* "+12 CT"; the audio's share of each half-buffer ("14 %") */
        static char b[20] __attribute__((section(".pool")));
        int32_t c = row == MI_TUNE ? (int32_t)v - 50 : (int32_t)(song.cpu_q8 * 100u / 256u);
        b[0] = row == MI_TUNE && c > 0 ? '+' : 0;
        b[1] = 0;
#if FELUCCA_CPU2
        if (row == MI_CPU) {                           /* ((Skiff 1.3) the cores, then the load: "2 CORES 9 %"; set
                                                        * another way than this power-on runs: "1 CORE: RESTART", and
                                                        * playing "1 CORE: STOP" (the settings are written stopped:
                                                        * a restart before would lose it) */
            str_cpy(b, v ? "2 CORES" : "1 CORE", sizeof b);
            if ((v != 0u) != (C1_ON() != 0)) {
                str_cpy(b + str_len(b), transport_busy() ? ": STOP" : ": RESTART", sizeof b - str_len(b));
                return b;
            }
            str_cpy(b + str_len(b), " ", sizeof b - str_len(b));
        }
#endif
        fmt_int(b + str_len(b), c);
        str_cpy(b + str_len(b), row == MI_TUNE ? " CT" : " %", sizeof b - str_len(b));
        return b;
    }
    case MI_CLK: {                                     /* ((Skiff) "320 MHZ"; set another way than this power-on runs, as
                                                        * CPU's: ": RESTART" (playing ": STOP"), and 320 refused at boot
                                                        * (main.c; its header message said why): ": REFUSED" */
        static char m[20] __attribute__((section(".pool")));
        str_cpy(m, v ? "320 MHZ" : "360 MHZ", sizeof m);
        if (CLK_MHZ() != (v ? 320u : 360u))
            str_cpy(m + str_len(m), transport_busy() ? ": STOP" : v && clk320.state >= CLK_REFUSED ? ": REFUSED" :
                    ": RESTART", sizeof m - str_len(m));
        return m;
    }
    }
    return "";
}
/* set a row (v below menu_n(row)) and do what the menu does on a change: the palette, the EQ and USB LEVEL at once;
 * LARGE, ANIM, LEDS, HOLD, the flags are read where they are used, every frame;
 * USB SERIAL when the menu has closed (usb_serial_apply). Saved by the caller (menu_close, the editor) */
static void menu_put(uint32_t row, uint32_t v)
{
#if FELUCCA_USB_ROUTING
    if (row >= MI_USB_LEFT && row <= (FELUCCA_USB_6CH ? MI_USB_LEFT + UAC_CHANNELS - 1u : MI_USB_RIGHT)) {
#if FELUCCA_USB_6CH
        usb_six_put(row-MI_USB_LEFT,v);
#else
        usb_route_put(row-MI_USB_LEFT,v);
#endif
        return;
    }
#endif
    const menu_flag_t *f = menu_flag(row);
    if (f) {
        if (f->bit & PREF_REC)
            ui_rec_prefs = (uint8_t)(v ? ui_rec_prefs | f->bit >> 8 : ui_rec_prefs & ~(f->bit >> 8));
        else
            ui_prefs = (uint8_t)(v ? ui_prefs | f->bit : ui_prefs & ~f->bit);
        fx_usb_fixed = (ui_prefs & PREF_USB_FIXED) != 0u;   /* (at once; every frame too: ui_input) */
        return;
    }
    switch (row) {
    case MI_COLOR:                                     /* ((Skiff #27) CUSTOM: the stored colours in their look) */
        CPAL_BYTE = (uint8_t)(v >= NPALETTES ? CPAL_BYTE | 2u : CPAL_BYTE & ~2u);
        settings.palette = v >= NPALETTES ? CPAL_BYTE >> 2 & 7u : v;   /* (a look past QUARK: palette_set wraps it) */
        theme_apply();                                 /* (the menu signature redraws) */
        ui.force = 1;
        break;
    case MI_PHEAD: ui_ph_line = (uint8_t)(v & 1u); ui.force = 1; break;
    case MI_LOWCUT: settings.lowcut = v; fx_lowcut = (uint8_t)v; break;
    case MI_HOLD: settings_hold = (uint8_t)v; break;
    case MI_SCROFF: scr_put(v); break;                 /* (read every frame: ui.c scr_frame) */
    case MI_LEDS: settings_leds = LEDS_MENU[v]; break;
    case MI_CLICK: case MI_CLKLVL: case MI_COUNTIN: rp_put(row - MI_CLICK, v); break;   /* (at once: click.c, seq.c) */
    case MI_CLOCK: song.g[G_CLOCK] = (int16_t)v; break;   /* ((Skiff U3) the unit's: ui.c unit_g_poll keeps them) */
    case MI_ROUTE: song.g[G_ROUTE] = (int16_t)v; break;
    case MI_TCH1: case MI_TCH2: case MI_TCH3: case MI_TCH4: case MI_TCH5: case MI_TCH6:   /* ((1.3) the ISR reads it */
        ui_mch(row - MI_TCH1) = (uint8_t)((v - (row - MI_TCH1)) & 15u);                 /* at its next block) */
        break;
    case MI_TUNE: song.g[G_TUNE] = (int16_t)((int32_t)v - 50); break;
#if BOARD_SMK37
    case MI_KVEL: SMK_KV_BYTE = (uint8_t)(v % SMK_KV_N); smk_kv_mode = SMK_KV_BYTE; break;
#endif
    case MI_HINTS: ui_hints_off = (uint8_t)(v & 1u); ui.force = 1; break;
    case MI_PREVIEW:                                   /* ((Skiff 1.3) a bit of CPU2_BYTE, the byte bank.c freed) */
        if (favorites.factory[15][24] == 0xA5u)
            CPU2_BYTE = (uint8_t)(v & 1u ? CPU2_BYTE | 2u : CPU2_BYTE & ~2u);
        break;
#if FELUCCA_CPU2
    case MI_CPU:                                       /* ((Skiff 1.3) the second core: started at power-on only, main.c;
                                                        * a byte bank.c freed, once it is: SCENE_FREED) */
        if (favorites.factory[15][24] == 0xA5u)
            CPU2_BYTE = (uint8_t)(v ? CPU2_BYTE | 1u : CPU2_BYTE & ~1u);
        settings_save();                               /* (at once, not at the menu's close: a restart follows; playing,
                                                        * at STOP, and the row says STOP till then) */
        break;
#endif
    case MI_CLK:                                       /* ((Skiff) the clock: switched at boot only, main.c; saved as CPU) */
        if (favorites.factory[15][24] == 0xA5u)
            CPU2_BYTE = (uint8_t)(v ? CPU2_BYTE | 4u : CPU2_BYTE & ~4u);
        settings_save();
        break;
    }
}
/* (Skiff U4, the review's A3) MENU > SYSTEM > RESET SETTINGS, after its dialog (ui_input.c): every MENU setting as a new
 * unit has it (Skiff's palette, BPM LOCK ON, the rest their defaults: CLOCK INT, MIDI IN CH1-6, TUNE 0 ct, HINTS ON);
 * the calibration, the favourites and the music stay. Saved at once */
static void settings_reset(void)
{
    uint32_t i;
    usb_routes = USB_ROUTES_DEFAULT;
#if FELUCCA_USB_6CH
    usb_six_routes = USB_SIX_DEFAULT;
    usb_extra_routes=USB_EXTRA_DEFAULT;
    usb_compact_config=USB_COMPACT_DEFAULT;
    for(uint32_t t=0;t<6;t++)usb_cf_track[t]=USB_CF_TRACK_DEFAULT;
    usb_cf_edit=0;
#endif
    menu_put(MI_COLOR, UI_SKIFF_INDEX);
    ui_prefs = PREF_BPM_LOCK;
    fx_usb_fixed = 0;
    ui_rec_prefs &= (uint8_t)~(PREF_SCALE_LEDS >> 8);
    menu_put(MI_CLICK, 0);
    menu_put(MI_CLKLVL, 1);
    menu_put(MI_COUNTIN, 0);
    menu_put(MI_LEDS, 2);                              /* (LEDS_MENU: DIM HI) */
#if BOARD_SMK37
    menu_put(MI_KVEL, SMK_KV_DYN);
#endif
    menu_put(MI_HOLD, HOLD_DEF);
    menu_put(MI_LOWCUT, 0);
    menu_put(MI_SCROFF, 3);                            /* (30 MIN) */
    menu_put(MI_CLOCK, 0);
    menu_put(MI_ROUTE, 0);
    for (i = 0; i < NTRK; i++)                         /* ((1.3) T1..T6 on channels 1..6; nothing learned) */
        menu_put(MI_TCH1 + i, i);
    ml_off();
    (void)ml_clear_all();
    menu_put(MI_TUNE, 50);
    menu_put(MI_HINTS, 0);
    menu_put(MI_PHEAD, 0);
    menu_put(MI_PREVIEW, 0);
#if FELUCCA_CPU2
    menu_put(MI_CPU, 0);                               /* ((Skiff 1.3) 1 CORE) */
#endif
    menu_put(MI_CLK, 0);                               /* ((Skiff) 360 MHZ) */
    settings_save();
}
/* the menu's step, any of KNOB 1..4 or OCT+ (s > 0) / OCT- (s < 0): the next / previous value, stopping at the ends;
 * THEME (MI_COLOR) wraps round its palettes. On an ON / OFF row up is ON (MENU_FLAGS) */
static uint32_t menu_step(uint32_t row, int32_t s)
{
    const menu_flag_t *f = menu_flag(row);
    uint32_t v = menu_get(row), n = menu_n(row);
    if ((f && str_eq(f->name[0], "ON")) || row == MI_HINTS)   /* (ANIM, USB SERIAL, RESTORE LAST, HINTS: ON is their
                                                                * value 0) */
        s = -s;
    if (s > 0)
        return v + 1u < n ? v + 1u : row == MI_COLOR ? 0u : v;
    if (s < 0)
        return v ? v - 1u : row == MI_COLOR ? n - 1u : 0u;
    return v;
}
