/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the UI (firmware/src/ui*.c with the sound sources of hostsim.c;
 * the display and the HAL are stubs, the buttons and knobs are driven through them):
 *   SOUNDS     a sound load (PRESETS knob / page, TOOLS INIT, user preset) changes the sound only: the
 *              steps, LEN DIV SWING GATE, ARP, SCL, the SLICER and the mix stay the track's. Power-on:
 *              every sequencer empty.
 *   UNDO       a load keeps a copy of the track; SAVE held swaps back what the loads changed (again:
 *              redo); browsing keeps the copy from before the first load; power-on and projects: none.
 *   REC        a tap on every screen arms the selected track and starts PLAY, without navigation.
 *              Long REC never clears. On STEP, armed and playing: keys do not write the
 *              cursor step. With the ARP on, the arp's notes are recorded, not the keys.
 *   MIDI IN    GLO > SYSTEM ROUT: CH1-4 (channels 5..16 ignored) / SEL, note-offs follow their note-ons.
 *   SAVE       refused while playing ("STOP TO SAVE"); a used slot asks OVERWRITE? (OCT+ / OCT-).
 *   ACTIONS    SAVE > PATTERNS, USER, PROJECT, TOOLS: the knobs pick, OCT+ does it, OCT- cancels or goes
 *              HOME, on release, never both together; the OCT LEDs (OCT- lit, OCT+ blinks).
 *   TRACKS     the PRESETS knob does nothing there; KNOB 1 is MUTE.
 *   GRID       SEQ > STEP on a DRUM track: white keys toggle the selected lane's steps of the page, black
 *              keys 1..8 select the lane (and only they play), 9 held = ACC, 10 / 11 the page; KNOB 1..4 STEP
 *              LANE HIT ACC; the key LEDs (hits, the playhead inverted, the lane, ACC, the page keys); a step's
 *              GM notes shown on their lanes and made the lane's own by an edit; live recording into the grid
 *              (lane keys on the grid, GM keys elsewhere; other GM drums as notes; no TIE holds); a BEAT
 *              (tests/phrases.h) fills the grid; sound loads keep it, and other engines play it as GM notes.
 *   ROLL       the rolling digits of the card values and the header BPM: strips only, the last frame static,
 *              direction, retarget, the snaps (fast turn, names, shape, page, track, palette, force).
 *   SOAK       #63: 150000 random frames (turns, pages, transport, REC / motion, wrapping clocks): no roll
 *              outlives ROLL_FRAMES, the cards always equal a forced redraw once the rolls are over.
 *   USB SERIAL #67: MENU > USB SERIAL ON (default, the descriptors as before) / OFF (audio + MIDI only, class 0):
 *              the menu, the settings, the descriptor at boot (built with the console, as the firmware: FELUCCA_CDC).
 *   SCREEN OFF (1.1.5): MENU > DISPLAY > SCREEN OFF against a simulated clock and the panel's commands (lcd_power):
 *              dark after the time, nothing drawn, any input wakes it (SLPOUT, 120 ms, redraw, DISPON) and is
 *              swallowed; dialogs / count-in / UPDATE MODE keep it on; saved; the sound bit for bit while dark.
 * Built and run by tests/run_tests.sh. */
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                                    /* (as src/felucca.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* hal/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* hal/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int fm1_led_anim_on(void) { return host_anim; }
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[7];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[7];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 7u];
    host_enc[e % 7u] = host_enc_late[e % 7u];
    host_enc_late[e % 7u] = 0;
    return s;
}
static void (*host_wdt_hook)(void);                /* (a test's: time passing in a blocking loop, as panel_setup's) */
static void fm1_wdt_feed(void) { if (host_wdt_hook) host_wdt_hook(); }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint16_t host_screen[240 * 240];
static void count_wide_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* (#15: test_mixer_no_flash) */
#define UI_FILL_HOOK count_wide_fill
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
#ifdef UI_FILL_HOOK
    UI_FILL_HOOK(x, y, w, h);                     /* (tests/ui_render.c: what a fill covers is gone) */
#endif
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            host_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
}
static void lcd_sync(void) {}
/* MENU > SCREEN OFF (lcd.c lcd_power / lcd_wake_now): what the panel was told, in order (0 off: backlight off,
 * DISPOFF, SLPIN; 1 SLPOUT; 2 DISPON, backlight on; 3 lcd_wake_now), with the time; host_bl the backlight */
static uint32_t host_pw_n, host_pw[256], host_pw_ms[256];
static int host_bl = 1;
static void host_pw_log(uint32_t s)
{
    if (host_pw_n < 256u) { host_pw[host_pw_n] = s; host_pw_ms[host_pw_n] = fm1_ms; }
    host_pw_n++;
    host_bl = s != 0u;
}
static void lcd_power(uint32_t s) { host_pw_log(s); }
static void lcd_wake_now(void) { host_pw_log(3u); }
#define SCOPE_N 512u                              /* audio.c: the HOME oscilloscope */
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
static uint32_t host_blit_rows;                    /* rows blitted (what a frame draws) */
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    host_blit_rows += h;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++) host_screen[(y + j) * 240u + x + i] = p[j * w + i];
}
static struct { uint32_t stage; } felucca_dbg;
#define FELUCCA_FLASH 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "TEST"
#endif
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
#include "../firmware/src/icons.c"
#include "../firmware/src/ui_graph.c"
#include "../firmware/src/ui_draw.c"
#include "../firmware/src/ui_menu.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_layer.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#define BK_RAM 1                                  /* (Skiff: the pattern bank's sectors in RAM: no flash here) */
#include "../firmware/src/bank.c"
#include "phrases.h"                              /* (test material: the phrases the firmware no longer ships) */

static int check(const char *what, int ok)
{
    printf("ui: %-74s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c felucca_init */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song); sel_set(0);
    memset(&ui, 0, sizeof ui);
    memset(&nsel, 0, sizeof nsel);                /* ((Skiff 2.0) no selection, FEEL's tiles on CHANCE) */
    clip.n = 0;                                   /* (.. nothing copied) */
    cin_left = cin_total = 0;                     /* (no count-in) */
    memset(&nm, 0, sizeof nm);                    /* NAME closed, no project name */
    proj_name[0] = 0;
    proj_cur = PROJ_NO_SLOT;
    memset(&favorites, 0, sizeof favorites);
    memset(undo_ring, 0, sizeof undo_ring);   /* (Skiff: the undo ring, ui.c) */
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);   /* ((Skiff phase 9) the factory kit: a DRUM-X preset loads others) */
    memset(bk_ram, 0xFF, sizeof bk_ram);         /* (Skiff: the pattern bank erased, as from a new unit) */
    bank_boot();
    memset(launch_q, 0, sizeof launch_q);        /* (.. nothing staged: launch.c) */
    memset((void *)mute_arm, 0, sizeof mute_arm);
    scene_last = 0xFFu;
    memset(thr, 0, sizeof thr);
    thr_out = thr_on = 0;
    memset(&mom, 0, sizeof mom);                  /* ((2.0, D 2A) nothing held or on its way back: ui.c mom_*) */
    thr_beats = 4u;
#if FELUCCA_SLICE
    memset(&sp, 0, sizeof sp);                    /* (SLICES: the marker, ui_slice.c) */
#endif
    undo_clear();
    lists_clear();                                /* (Skiff phase 5: no lists, no patterns on the tracks) */
    motion_lost = 0;
    memset(&sg, 0, sizeof sg);                    /* (.. SONG's entry A, no pick) */
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);
        t->engine = t->eng_req;
        track_defaults_steps(t);
    }
    undo_depth--;
    sel_set(0);
    ui.home = 1;
    ui.force = 1;
    panel = PANEL_DEFAULT;
    settings.palette = UI_SKIFF_INDEX;
    palette_set(UI_SKIFF_INDEX);
    transport_req = 0;
    panic_req = 0;
    mi_r = mi_w = midi_in_overflow = 0;
    memset(midi_sel_on, 0, sizeof midi_sel_on);
    memset(midi_ch, 0, sizeof midi_ch);
    memset(midi_owners, 0, sizeof midi_owners);
    memset(midi_bend_q8, 0, sizeof midi_bend_q8);
    memset(midi_bend_target, 0, sizeof midi_bend_target);
    memset(kb_chn, 0, sizeof kb_chn);                   /* chord.c: no key, MIDI chord or last chord */
    memset(mchord, 0, sizeof mchord);
    memset(chord_last, 0, sizeof chord_last);
    perf_held = perf_act = kb_layer = perf_mask = 0;      /* the FX layer: nothing held */
    perf_kill = 0;
    perf_mask = 1u << panel.btn[B_FX];                  /* (ui_input sets them every frame) */
    kb_mask = layer_bits();
    perf_solo = 0;
    memset(&lys, 0, sizeof lys);
    perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;
    settings_hold = HOLD_DEF;
    fm1_in.buttons = fm1_in.notes = 0;
    memset(&motion, 0, sizeof motion);                  /* (main.c: no motion at power-on) */
    memset(motion_active, 0, sizeof motion_active);
    memset(motion_locked, 0, sizeof motion_locked);
    motion_base_valid = motion_full = 0;
    memset(snd_said, 0, sizeof snd_said);               /* (.bss at power-on: nothing said yet) */
    memset(&scrn, 0, sizeof scrn);                        /* MENU > SCREEN OFF: on, idle from now (main.c) */
    scrn.idle = fm1_ms;
    kb_asleep = 0;
    panel_moved = 0;
}
/* card slot drawn with MOTION's mark (ui_draw.c card_mot: its icon, or a long label in the theme colour) */
static int card_motion(uint32_t slot) { return (card_mot >> slot) & 1u; }

/* one UI frame (input, then the drawing on the stub display) */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    ui_input();
    ui_draw();
}
static void press(uint32_t label)                 /* a tap: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
/* (2.0, D 2A) MENU > CONTROL > RETURN set */
static void ret_set(uint32_t m) { menu_put(MI_RETURN, m); }
/* ((2.0) Tier B 6) a record from before a setting existed, holding what SKS1 s holds of PER4's own fields: the PER3 of
 * before (no favourites), or ((2.0, Tier B 10) 2.0 reads no PER3: the converter's build alone does) Skiff's PER4 with
 * its favourites 0 */
static persist_t per3_of(const sks_t *s)
{
    persist_t o;
    memset(&o, 0, sizeof o);
    o.magic = FELUCCA_LEGACY ? 0x50455233u : PERSIST_MAGIC;
    o.palette = s->palette; o.lowcut = s->lowcut; o.zoom = s->zoom; o.panel = s->panel;
    o.bold = s->bold;
    o.favorites.factory[15][24] = FELUCCA_LEGACY ? 0u : 0xA5u;   /* (Skiff's mark: its own record) */
    return o;
}
/* .. imported: nonzero (2 a PER3: migrated) */
static int per3_import(persist_t *q)
{
    return settings_import(q, FELUCCA_LEGACY ? (int)(sizeof *q - sizeof q->favorites) : (int)sizeof *q);
}
static uint32_t beat_ms(void) { return beat_samples() / 441u * 10u; }   /* (as ui.c mom_poll counts it) */
static void a_bar(void) { frames(4u * beat_ms() + 64u); }                 /* .. RETURN's default: all back */
static void btn_down(uint32_t label) { fm1_in.buttons |= 1u << panel.btn[label]; host_pressed |= 1u << panel.btn[label]; }
static void btn_up(uint32_t label) { fm1_in.buttons &= ~(1u << panel.btn[label]); }
/* (Skiff U2) SHIFT (HOME held) + a layer's button: locked open (it was a double tap, #83) */
static void shift_lock(uint32_t label) { btn_down(B_HOME); frame(); press(label); btn_up(B_HOME); frame(); }
static void hold(uint32_t label)                  /* held 0.8 s, then let go */
{
    uint32_t k;
    fm1_in.buttons |= 1u << panel.btn[label];
    host_pressed |= 1u << panel.btn[label];
    for (k = 0; k < 52u; k++)
        frame();
    fm1_in.buttons &= ~(1u << panel.btn[label]);
    frame();
}
static void turn(uint32_t role, int32_t s)
{
    host_enc[panel.enc[role]] += s * panel.dir[role];
    frame();
    host_ticks += 200000u;                        /* (no knob acceleration between the turns) */
}
static void stop_transport(void) { transport_req = 0; song.playing = 0; }
static void user_rename(void) { turn(EN_K3, 2); press(B_OCTUP); }      /* (U2) USER: KNOB 3 past ERASE, NAME */
static void proj_rename(void) { turn(EN_K2, 2); press(B_OCTUP); }      /* (U2) PROJECT: KNOB 2 past DELETE (1.2.1), NAME */
static void song_pick_clear(void)                  /* (U2) SONG: KNOB 2 to CLEAR ((2.0, #30) its end but SOUND and NO */
{                                                  /* SOUND), OCT+ */
    turn(EN_K2, 40);
    turn(EN_K2, lists.snd[song.sel % NTRK][sg.entry % NENT][0] ? -2 : -1);
    press(B_OCTUP);
}
static int16_t stored_param(uint32_t slot, uint32_t track, uint32_t id)
{
    project_t p;
    return proj_import(&p, &proj_slot[slot], sizeof proj_slot[slot]) ? p.t[track].p[id] : -32768;
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
/* "UNDO T1 SOUND": (U2: BLOCKS only) the undo box, "UNDO SOUND" on track 1 (ui_take.c take_box_undo), else said */
static int undo_is(const char *s)
{
    char t[24];
    if (msg_is(s))
        return 1;
    if (!ui.take_t || take_box.kind != 1u || str_len(s) < 9u || s[5] != 'T' || s[7] != ' ')
        return 0;
    memcpy(t, s, 5);
    str_cpy(t + 5, s + 8, sizeof t - 5);
    return str_eq(take_box.title, t) && take_box.trk == (uint8_t)(s[6] - '1');
}
static void my_steps(track_t *t)                  /* "recorded" steps */
{
    uint32_t i;
    track_defaults_steps(t);
    for (i = 0; i < 32u; i += 3u) {
        t->step[i].note[0] = (uint8_t)(40 + i);
        t->step[i].n = 1;
        t->step[i].time = ST_NOTE;
        t->step[i].vel = 99;
    }
    t->p[P_SLEN] = 32;
}

static void go_page(uint32_t graph)               /* the page that draws graph */
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static void auto_top(void) { go_page(GR_EVENTS); turn(EN_K1, -64); }   /* (U3, R5) AUTOMATION's top row */
static int steps_are(const track_t *t, uint32_t n)  /* steps 1..16 = PATTERNS[n], 17..64 empty, LEN 16 */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        uint8_t want = i < 16u ? PATTERNS[n].note[i] : 0u;
        if (t->step[i].note[0] != want || t->step[i].n != (want ? 1u : 0u))
            return 0;
    }
    return t->p[P_SLEN] == 16;
}
/* the sound of a track: engine, preset and the parameters that are not the track's own (param_kept) */
static int same_sound(const track_t *a, const track_t *b)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && a->p[i] != b->p[i])
            return 0;
    return a->eng_req == b->eng_req && a->preset == b->preset;
}

/* the track's E values are DRUM's default kit (core.h DRUM_KIT_E: what a SAMPLE PERC sound loads as) */
static int perc_kit(const track_t *t)
{
    static const int16_t KIT[8] = DRUM_KIT_E;
    return !memcmp(&t->p[P_E0], KIT, sizeof KIT);
}
/* the GM drums a DRUM track plays: a kick, a snare and a closed hat each strike their lane and sound */
static int gm_hits_play(track_t *t)
{
    static const uint8_t N[3] = {36, 38, 42}, L[3] = {DV_KICK, DV_SNARE, DV_HATC};
    int32_t o[2u * CTL];
    uint32_t k, i, b, ok = 1, m = t->p[P_MUTE], mq = song.master_q12;
    t->p[P_MUTE] = 0;
    song.master_q12 = 4096;                       /* (the MASTER knob: main.c; 0 in this test) */
    for (b = 0; b < 256u && (t->xf_on || t->eng_req != t->engine); b++)   /* the engine switch done (its fade) */
        mix_block(o, CTL);
    for (k = 0; k < 3u; k++) {
        uint64_t e = 0;
        int lane = 0;
        trk_note_on(t, N[k], 100);
        for (b = 0; b < 16u; b++) {
            memset(o, 0, sizeof o);
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++)
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            for (i = 0; i < NVOICE; i++)
                lane |= t->v[i].active && t->v[i].note == N[k] && (uint32_t)t->v[i].s[0] == L[k];
        }
        trk_note_off(t, N[k]);
        ok &= ENGINES[t->engine] == &ENG_DRUM && drum_lane(N[k]) == L[k] && lane && e > 0;
    }
    t->p[P_MUTE] = (int16_t)m;
    song.master_q12 = mq;
    return (int)ok;
}

/* sounds never touch the steps; the arp, the scale, the SLICER and the pattern parameters are the track's */
static int test_sound_loads(void)
{
    int bad = 0;
    uint32_t i, empty = 1;
    track_t *t = &trk[0], before;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        empty &= (uint32_t)seq_is_empty(&trk[i]);
    bad += check("power-on: the four sounds, every sequencer empty, no undo copy", empty && undo.trk == 0 && undo_depth == 0 &&
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && trk[0].preset == TRK_DEF[0][1]);
    my_steps(t);
    t->p[P_E0 + 1] = 77;                          /* a sound edit */
    t->p[P_AMODE] = 2;                            /* and the track's own settings */
    t->p[P_AOCT] = 3;
    t->p[P_SCALE] = 2;
    t->p[P_TRANS] = 5;
    t->p[P_SLCR] = SL_GATE;
    t->p[P_SLPAT] = 4;
    t->p[P_SDIV] = 1;
    t->p[P_LEVEL] = 90;
    before = *t;
    turn(EN_PRESET, 1);                           /* HOME: the next preset */
    bad += check("PRESETS on HOME loads the sound: steps, LEN, DIV untouched", t->preset == (uint8_t)((before.preset + 1u) % ENGINES[0]->npresets) &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_SLEN] == 32 && t->p[P_SDIV] == 1);
    bad += check("..ARP, SCL, the SLICER and the mix stay the track's", t->p[P_AMODE] == 2 && t->p[P_AOCT] == 3 && t->p[P_SCALE] == 2 &&
                 t->p[P_TRANS] == 5 && t->p[P_SLCR] == SL_GATE && t->p[P_SLPAT] == 4 && t->p[P_LEVEL] == 90);
    bad += check("..and no warning about the sequence", !msg_is("T1 SEQ REPLACED"));
    for (i = 0; i < ENGINES[0]->npresets; i++)
        if (str_eq(ENGINES[0]->presets[i].name, "RAVE"))
            break;
    t->p[P_AMODE] = 0;
    t->preset = (uint8_t)(i - 1u);
    turn(EN_PRESET, 1);
    bad += check("RAVE (an ARP preset) leaves the arp off", t->preset == i && t->p[P_AMODE] == 0);
    t->p[P_AMODE] = 2;
    before = *t;
    for (i = t->preset; i < ENGINES[0]->npresets; i++)   /* several loads, into the next engine */
        turn(EN_PRESET, 1);
    bad += check("browsing on, into another engine: the steps still untouched", t->eng_req != before.eng_req &&
                 !memcmp(t->step, before.step, sizeof t->step) && t->p[P_AMODE] == 2);
    bad += check("browsing keeps the copy from before the first load", undo.trk == 1u && undo.what == UNDO_SOUND &&
                 undo.p[P_E0 + 1] == before.p[P_E0 + 1] && undo.eng == before.eng_req && undo.preset == before.preset);
    t->step[0].note[0] = 99;                      /* recorded after the loads */
    hold(B_SAVE);
    bad += check("SAVE held: the sound back (UNDO T1 SOUND); steps recorded since stay", same_sound(t, &before) &&
                 t->step[0].note[0] == 99 && undo_is("UNDO T1 SOUND"));
    bad += check("SAVE held does not open the SAVE pages", ui.home);
    press(B_OCTUP);                               /* (Skiff: OCT+ in the undo window) */
    bad += check("OCT+ in the undo window: the loads again (redo), the octave kept", t->eng_req != before.eng_req &&
                 t->step[0].note[0] == 99 && song.octave == 0);
    hold(B_SAVE);
    t->p[P_LEVEL] = 50;                           /* a mix change after the undo */
    turn(EN_PRESET, 1);                           /* a load after an undo copies the track as it is now */
    hold(B_SAVE);
    bad += check("a load after the undo: its own copy; the mix (LEVEL) is never swapped", same_sound(t, &before) && t->p[P_LEVEL] == 50);
    press(B_SAVE);
    bad += check("SAVE tap opens the SAVE pages (on release)", !ui.home && cur_page()->fam == FAM_SAVE);
    /* TOOLS INIT, user preset load: the sound only, undoable; one copy for all tracks */
    ui_power_on();
    my_steps(&trk[1]);
    trk[1].p[P_SLCR] = SL_STUT;
    before = trk[1];
    track_select(1);
    set_engine(trk[1].eng_req);                   /* TOOLS INIT */
    bad += check("TOOLS INIT: the sound only (steps and SLICER kept), a copy of T2", undo.trk == 2u &&
                 !memcmp(trk[1].step, before.step, sizeof before.step) && trk[1].p[P_SLCR] == SL_STUT);
    track_select(0);
    my_steps(&trk[0]);
    trk[0].p[P_SDIV] = 2;
    up_store(3, "MINE");
    bad += check("(Skiff) a user preset is the sound: no pattern in the record ((2.0) version 6: by stored id)",
                 up_used(3) && !up_pat(up_rec(3)) && up_rec(3)->ver == UP_VER_SID && up_rec(3)->np == SID_COUNT);
    trk[0].p[P_SDIV] = 0;
    trk[0].user = 0;                              /* ((2.0) a store puts the track on the slot: off it again, so the */
    track_defaults_steps(&trk[0]);                /* load's undo has a slot to take back) */
    trk[0].step[5].note[0] = 33;
    trk[0].step[5].n = 1;
    before = trk[0];
    up_load(3);
    bad += check("a user preset load: the sound only (steps, DIV kept); the copy is now T1's", trk[0].user == 4u && undo.trk == 1u &&
                 !memcmp(trk[0].step, before.step, sizeof before.step) && trk[0].p[P_SDIV] == 0);
    undo_step(0);
    bad += check("undo of a user preset load", trk[0].user == 0 && !memcmp(trk[0].step, before.step, sizeof before.step));
    /* project load: no copy, and the old one is gone */
    project_save(1);
    up_load(3);
    project_load(1);
    bad += check("a project load drops the copies and takes none", undo_n == 0 && undo_depth == 0);
    {   /* (2.0, #20) a sound from a user preset keeps its name over a save and a load (SKF4's user slot); the slot
         * erased since: none (the engine's preset name, as before) */
        char nm[16], un[16];
        up_rec_t keep = *up_rec(3);
        int ok20;
        up_load(3);
        up_name(3, un);
        project_save(1);
        trk[0].user = 0;                             /* (as every load before 2.0 left it) */
        project_load(1);
        sound_name(&trk[0], nm);
        ok20 = trk[0].user == 4u && str_eq(nm, un) && un[0];
        up_put(3, 0);
        project_load(1);
        ok20 &= trk[0].user == 0u;
        up_put(3, &keep);
        bad += check("(2.0, #20) a user sound's name over a save and a load; its slot erased since: none", ok20);
    }
    ui_power_on();
    bad += check("SAMPLE factory browsing: PIANO .. SAX, (phase 9) PERC's slot a hidden PIANO, the variations after",
                 ENGINES[4]->npresets == 10u && str_eq(ENGINES[4]->presets[0].name, "PIANO") &&
                 str_eq(ENGINES[4]->presets[3].name, "SAX") && str_eq(ENGINES[4]->presets[4].name, "PIANO") &&
                 str_eq(ENGINES[4]->presets[5].name, "LOFI PIANO") && preset_shown(4) == 8u &&
                 preset_orig(ENGINES[4], SMP_SET_PERC) == 0u && preset_orig(ENGINES[4], 5u) == 5u);
    {   /* TRANH (SET 1, preset 1) is gone: both are PIANO aliases, kept for old data, never offered */
        const param_desc_t *sd = &ENGINES[4]->edit[0];
        uint32_t all, pos, k, e, pos0, shown = 0;
        set_engine_of(TSEL, 4);
        pos0 = preset_all_pos(&all);
        TSEL->preset = 1;
        TSEL->p[P_E0] = 1;
        pos = preset_all_pos(&all);
        for (k = 0; k < all; k++)
            if (preset_all_at(k, &e) == 4u)
                shown++;
        e = preset_all_at(pos0 + 1u, &k);
        apply_preset_to(TSEL, 1);
        bad += check("SAMPLE: old SET 1 / preset 1 (TRANH) play PIANO; browsing and knobs skip them",
                     SMP_SETS[1].z0 == SMP_SETS[0].z0 && SMP_SETS[1].nz == SMP_SETS[0].nz && pos == pos0 &&
                     shown == 8u && e == 4u && k == 2u && TSEL->preset == 0u && TSEL->p[P_E0] == 0 &&
                     str_eq(sd->names[1], "PIANO") && enum_step(sd, 0, 1) == 2 && enum_step(sd, 2, 1) == 0 &&
                     enum_step(sd, 1, 2) == 2 && enum_orig(sd, 1) == 0 && enum_orig(sd, 5) == 5 &&
                     enum_orig(&ENGINES[8]->edit[0], 1) == 0);
        bad += check("SAMPLE / GRAIN SET 4 (PERC, retired): a PIANO alias, knobs skip it, USR1..3 stay 5..7",
                     str_eq(sd->names[SMP_SET_PERC], "PIANO") && enum_orig(sd, SMP_SET_PERC) == 0 &&
                     enum_step(sd, 3, 4) == 5 && enum_step(sd, 5, 4) == 3 && str_eq(sd->names[5], "USR1") &&
                     str_eq(sd->names[7], "USR3") && sd->max == 7 &&
                     enum_orig(&ENGINES[8]->edit[0], SMP_SET_PERC) == 0 && SMP_SETS[SMP_SET_PERC].z0 == SMP_SETS[0].z0);
    }
#if FELUCCA_LEGACY                             /* ((2.0) the converter's: 2.0 reads no record of before) */
    host_legacy_sample_perc(t);                /* SAMPLE PERC (SET 4, retired after 1.0.2): every load gives DRUM */
    t->preset = 4;                             /* a project written before the factory removal */
    my_steps(t);
    t->p[P_LEVEL] = 71;
    t->p[P_REV] = 33;
    before = *t;
    project_save(0);
    apply_preset_to(t, 0);
    trk_pid[t - trk] = 0;                      /* (phase 5: new steps; the saved project's pattern as saved) */
    track_defaults_steps(t);
    project_load(0);
    bad += check("saved SAMPLE PERC project: a DRUM track (its kit), the rest of the sound and every step kept",
                 t->eng_req == ENGI_DRUM && t->preset == 0u && perc_kit(t) &&
                 !memcmp(t->p, before.p, P_E0 * sizeof t->p[0]) && !memcmp(t->step, before.step, sizeof t->step));
    bad += check("  its GM hits play as drums (kick, snare, closed hat on their lanes)", gm_hits_play(t));
    {   /* a user preset of SAMPLE PERC (UP_PUT from an older editor, or a bank read): DRUM, its kit */
        up_rec_t r;
        int16_t keep[P_COUNT];
        uint32_t k;
        memset(&r, 0, sizeof r);
        r.used = UP_USED; r.ver = UP_VER; r.engine = ENGI_SAMPLE; r.np = P_COUNT;
        memcpy(r.name, "OLD PERC", 8);
        host_legacy_sample_perc(t);
        for (k = 0; k < P_COUNT; k++)
            up_set_value(&r, k, t->p[k]);
        up_migrate(&r);
        *up_rec(7) = r;
        set_engine_of(t, 0);
        memcpy(keep, t->p, sizeof keep);
        sel_set((uint8_t)(t - trk));
        up_load(7);
        bad += check("SAMPLE PERC user preset: loads as a DRUM track (its kit)", t->eng_req == ENGI_DRUM && perc_kit(t) &&
                     t->p[P_LEVEL] == keep[P_LEVEL]);
        bad += check("  its GM hits play as drums", gm_hits_play(t));
        memset(up_rec(7), 0, sizeof r);
    }
#endif
    set_engine_of(t, 0);
    apply_preset_to(t, SMP_SET_PERC);             /* (engine 0: an ANALOG preset 4, not PERC) */
    bad += check("factory preset 4 of another engine stays that engine's", t->eng_req == 0u && t->preset == 4u);
    set_engine_of(t, ENGI_SAMPLE);
    apply_preset_to(t, SMP_SET_PERC);             /* SAMPLE preset 4 (once PERC): the editor's PRESET, a favourite */
#if !FELUCCA_LEGACY
    bad += check("(2.0) SAMPLE preset 4 (once PERC): PIANO, its alias", t->eng_req == ENGI_SAMPLE && t->preset == 0u);
#else
    bad += check("SAMPLE preset 4 (once PERC): DRUM's kit", t->eng_req == ENGI_DRUM && t->preset == 0u && perc_kit(t) &&
                 gm_hits_play(t));
    ui_power_on();
    host_legacy_sample_perc(&trk[3]);
    my_steps(&trk[3]);
    trk[3].p[P_SDIV] = 3;
    before = trk[3];
    project_v6_t legacy = {0};
    project_t decoded;
    project_capture(&decoded);                            /* (the music, its steps: a project saved now holds none) */
    legacy.magic = PROJ_MAGIC_V6; legacy.size = sizeof legacy;
    legacy.parts = 0; legacy.phys = PROJ_PHYS;
    memcpy(legacy.g, decoded.g, sizeof legacy.g);
    for (uint32_t ti = 0; ti < PROJ_NT; ti++) {      /* (FUN6: four tracks) */
        memcpy(legacy.t[ti].p, decoded.t[ti].p, 61u * sizeof(int16_t));
        memcpy(legacy.t[ti].p + 61, decoded.t[ti].p + P_E0, 8u * sizeof(int16_t));
        legacy.t[ti].engine = decoded.t[ti].engine; legacy.t[ti].preset = decoded.t[ti].preset;
        for (uint32_t st = 0; st < NSTEP; st++) memcpy(&legacy.t[ti].step[st], &decoded.t[ti].step[st], sizeof(step10_t));
    }
    legacy.t[3].engine = 0;
    legacy.g[G_CMODE] = 71; legacy.g[G_CFDBK] = 43;      /* (the drum level and reverb send then: G_DRLVL / G_DRREV) */
    legacy.g[G_RTYPE] = 10;                               /* (id 24 was the GM drum part's MIDI channel: 10) */
    legacy.sum = proj_hash(&legacy, offsetof(project_v6_t, sum));
    memcpy(&proj_slot[1], &legacy, sizeof legacy);
    project_load(1);
    bad += check("old GM projects (the drum part) load as DRUM's kit, their mix and steps kept",
                 trk[3].eng_req == ENGI_DRUM && trk[3].preset == 0u && perc_kit(&trk[3]) &&
                 trk[3].p[P_LEVEL] == 71 && trk[3].p[P_REV] == 43 && trk[3].p[P_SDIV] == 3 &&
                 !memcmp(trk[3].step, before.step, sizeof trk[3].step));
    bad += check("  their GM hits play as drums", gm_hits_play(&trk[3]));
    bad += check("  its old drum channel (10, in id 24) loads as REVERB TYPE ROOM", song.g[G_RTYPE] == 0);
#endif
    printf("ui: undo copy %u bytes\n", (unsigned)sizeof undo);
    return bad;
}

static int test_rec(void)
{
    static const uint8_t FAMS[3] = {FAM_TRK, FAM_SEQ, FAM_ARP};
    int bad = 0;
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        ui_power_on();
        track_select(2);
        open_family(FAMS[k]);
        press(B_REC);
        bad += check(k == 0 ? "REC on TRACKS: arms the selected track, PLAY starts" :
                     k == 1 ? "#133 REC on SEQ > STEP: arms the selected track for step recording, PLAY does not start" :
                     "REC on ARP: arms the selected track, PLAY starts",
                     song.rec == 4u && (k == 1u ? transport_req == 0u && msg_is("KEYS WRITE STEPS") : transport_req == 1u));
        song.playing = 1;
        transport_req = 0;
        frames(400);                                   /* (not a double tap: that locks the REC layer) */
        press(B_REC);
        bad += check("REC again disarms (the transport runs on)", song.rec == 0u && transport_req == 0u);
        frames(400);
        btn_down(B_REC); frames(800);                  /* (Skiff U2) held: armed on the press, no layer */
        bad += check("(U2) REC held: armed on its press, no layer, no dialog",
                     ui.confirm == CF_NONE && song.rec == 4u && !ui.layer);
        btn_up(B_REC); frame();
        ui.confirm = 0;
    }
    ui_power_on();
    press(B_REC);
    bad += check("REC on HOME records without changing the screen", ui.home && song.rec == 1u && transport_req == 1u);
    track_select(1);
    frames(400);
    press(B_REC);
    bad += check("(1.4.1) REC on track 2 with track 1 armed: both armed", song.rec == 3u);
    frames(400);
    press(B_REC);
    bad += check("(1.4.1) REC on an armed track disarms every track (its LED was lit for any)", song.rec == 0u);
    /* STEP page: armed + playing -> the keys record live only */
    ui_power_on();
    open_family(FAM_SEQ);
    bad += check("SEQ opens STEP", cur_page()->scope == SC_STEP);
    press(B_REC);
    song.playing = 1;
    transport_req = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("STEP, armed and playing: a key does not write the cursor step", !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, not armed: the key only plays (nothing written, the cursor stays)",
                 !trk[0].step[0].n && ui.cursor == 0);
    song.rec = 1;
    song.playing = 0;
    fm1_in.notes = host_notes = 1u << 7;
    frame();
    fm1_in.notes = 0;
    frame();
    bad += check("#133 STEP, armed and stopped: the key writes the cursor step (and moves on)",
                 trk[0].step[0].n == 1 && ui.cursor == 1);
    /* ARP: the arp's notes are recorded, not the key */
    ui_power_on();
    {
        track_t *t = &trk[0];
        uint32_t i, n = 0, notes = 0;
        t->engine = t->eng_req;
        t->p[P_AMODE] = 1;
        t->p[P_AOCT] = 2;
        t->p[P_APROB] = 127;
        song.rec = 1;
        song.playing = 1;
        input_on(t, 60, 100);
        for (i = 0; i < NSTEP; i++)
            n += t->step[i].n;
        bad += check("ARP on: the key held is not recorded", n == 0);
        arp_tick(t, 1);
        for (i = 0; i < NSTEP; i++)
            if (t->step[i].n)
                notes = t->step[i].note[0];
        bad += check("ARP on: the note the arp plays is recorded", notes == 60u);
        t->p[P_AMODE] = 0;
        input_off(t, 60);
    }
    return bad;
}

static uint32_t gated_notes(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += t->v[i].gate != 0;
    return n;
}

static int test_midi(void)
{
    int bad = 0;
    track_t *a, *b;
    ui_power_on();
    sel_set(2);
    midi_hint = 0;
    midi_event(0x90, 0, 60, 100); midi_event(0x90, 9, 61, 100);
    bad += check("ROUT CH1-4 (default): channel 1 -> part 1, channel 10 ignored",
                 song.g[G_ROUTE] == 0 && midi_sel_on[0][60] == 1u && !midi_sel_on[9][61] && !gated_notes(&trk[2]));
    bad += check("a note into another track tells the UI", midi_hint == 1u);
    frame(); bad += check("(U4, F4) MIDI IN into T1: its mark on HOME (no message)", midi_mark(0) && !msg_is("MIDI IN -> T1"));
    song.g[G_ROUTE] = 1;
    midi_event(0x80, 0, 60, 0); midi_event(0x90, 0, 62, 100);
    bad += check("ROUT changes: note-off releases original owner, new note uses selection",
                 !gated_notes(&trk[0]) && midi_sel_on[0][62] == 3u && midi_track(3) == &trk[2]);
    sel_set(1); midi_event(0x80, 0, 62, 0);
    bad += check("note-off after selection follows the note-on's track", !midi_sel_on[0][62]);
    midi_event(0x80, 9, 61, 0);
    ui_power_on();
    song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 60, 100);
    sel_set(1);
    midi_event(0x90, 0, 60, 100);
    bad += check("same MIDI channel/pitch on a new selected track releases its old owner",
                 !gated_notes(&trk[0]) && gated_notes(&trk[1]) && midi_sel_on[0][60] == 2u);
    midi_event(0x80, 0, 60, 0);
    midi_event(0x80, 0, 60, 0);
    bad += check("both note-offs after a MIDI ownership change leave neither track held",
                 !gated_notes(&trk[0]) && !gated_notes(&trk[1]) && !midi_sel_on[0][60]);
    ui_power_on();
    sel_set(2); song.g[G_ROUTE] = 1;
    midi_event(0x90, 0, 61, 100);
    song.g[G_ROUTE] = 0;
    midi_event(0x90, 0, 61, 100);
    midi_event(0x80, 0, 61, 0);
    bad += check("same channel/pitch across a ROUT change cannot leave its previous part held",
                 !gated_notes(&trk[2]) && !gated_notes(&trk[0]) && !midi_sel_on[0][61]);
    {
        uint32_t hold;
        for (hold = 0; hold < 2u; hold++) {
            ui_power_on();
            song.g[G_ROUTE] = 1;
            trk[0].p[P_AMODE] = 1; trk[0].p[P_AHOLD] = (int16_t)hold;
            midi_event(0x90, 0, 60, 100);
            sel_set(1);
            midi_event(0x90, 0, 60, 100);
            bad += check("MIDI ownership transfer releases old ARP keys and respects explicit HOLD",
                         !trk[0].arp_phys && trk[0].nheld == hold && midi_sel_on[0][60] == 2u);
        }
    }
    ui_power_on();
    trk[1].p[P_AMODE] = trk[1].p[P_AHOLD] = 1;
    midi_event(0x90, 0, 60, 100);
    midi_event(0x90, 1, 64, 100);
    song.playing = 1;
    {
        uint32_t i, held = 0;
        for (i = 0; i < MQ; i++) midi_in_event(0x643C9909u);
        midi_in_event(0x003C8908u);                      /* note-off lost to a full ring */
        events_block(CTL);
        for (i = 0; i < NTRK * NVOICE; i++) held += trk[i / NVOICE].v[i % NVOICE].gate;
        bad += check("MIDI overflow releases held notes and latched ARP, keeps transport running",
                     !held && !trk[1].nheld && !trk[1].arp_phys && !midi_sel_on[0][60] &&
                     !midi_sel_on[1][64] && !midi_in_overflow && mi_r == mi_w && song.playing);
        midi_in_event(0x643D9009u);
        events_block(CTL);
        bad += check("MIDI accepts a fresh note after overflow recovery", midi_sel_on[0][61] == 1u && trk[0].v[0].gate);
    }
    menu_put(MI_ROUTE, 1);                               /* ((Skiff U3, R8) the unit's, MENU > CONTROL; GLO > SYSTEM's before) */
    bad += check("MENU > MIDI IN: CH1-6 / SEL (song.g[G_ROUTE])", song.g[G_ROUTE] == 1 && str_eq(menu_vname(MI_ROUTE, 1), "SEL") &&
                 str_eq(menu_vname(MI_ROUTE, 0), "CH1-" TRK_STR(NTRK)));
    menu_put(MI_ROUTE, 0);
    return bad;
}

static int test_save(void)
{
    int bad = 0;
    uint32_t i;
    ui_power_on();
    trk[0].p[P_E0] = 5;
    press(B_SAVE);
    bad += check("SAVE opens USER with SAVE selected, without writing a slot",
                 !ui.home && cur_page()->graph == GR_USER && ui.act == 4u && !up_used(0));
    press(B_OCTUP);
    bad += check("SAVE then OCT+ opens NAME (the automatic name), writes nothing yet",
                 name_on() && nm.kind == NK_USER_SAVE && str_eq(nm.s, nm.ph) && nm.cur == nm.len && !up_used(0));
    press(B_OCTUP);
    {
        char nmb[16], au[16];
        up_name(0, nmb);
        up_auto_name(au, trk[0].eng_req, 0);
        bad += check("..OCT+ again stores the edited sound with that name (SAVE, OCT+, OCT+)",
                     !name_on() && up_used(0) && up_value(up_rec(0), p_sid(P_E0)) == 5 && ui.act == 0u && str_eq(nmb, au));
    }
    press(B_SAVE);
    bad += check("SAVE again cycles to (Skiff) PATTERNS, then PROJECT", cur_page()->graph == GR_BANK &&
                 (press(B_SAVE), cur_page()->graph == GR_SLOTS));
    go_home();
    press(B_SAVE);
    int ok = cur_page()->graph == GR_SLOTS;                /* ((U3, N3) the page it was left on) */
    press(B_SAVE); press(B_SAVE);                       /* (PRESETS, USER) */
    go_home();
    press(B_SAVE);
    bad += check("(U3) SAVE from HOME reopens the page it was left on: PROJECT; then USER, ready (SAVE picked)",
                 ok && cur_page()->graph == GR_USER && ui.act == 4u);
    press(B_OCTUP);
    bad += check("direct SAVE still asks before overwriting an occupied slot",
                 ui.confirm == CF_OVR_USER && up_value(up_rec(0), p_sid(P_E0)) == 5);
    press(B_OCTDN);
    ui_power_on();
    song.playing = 1;
    project_save(0);
    bad += check("project save while playing: refused, STOP TO SAVE", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("user preset save while playing: refused", up_store(0, "X") == 2 && !up_used(0));
    song.playing = 0;
    transport_req = 1;
    project_save(0);
    bad += check("a pending PLAY cannot save a project", !project_used(0) && msg_is("STOP TO SAVE"));
    bad += check("a pending PLAY cannot save a user preset", up_store(0, "X") == 2 && !up_used(0));
    transport_req = 0;
    song.playing = 1;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_SLOTS)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
    ui.msg_t = 0;
    turn(EN_K4, 1);                               /* SAVE picked */
    bad += check("PROJECT KNOB 4 picks SAVE, saves nothing", ui.act == 4u && !project_used(0) && !ui.msg_t);
    press(B_OCTUP);
    bad += check("OCT+ while playing: STOP TO SAVE, no NAME", msg_is("STOP TO SAVE") && !project_used(0) &&
                 song.g[G_SAVE] == 0 && !name_on());
    stop_transport();
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("stopped, an empty slot: OCT+ opens NAME (no name yet: PROJECT A)", name_on() && nm.kind == NK_PROJ_SAVE &&
                 nm.len == 0u && str_eq(nm.ph, "PROJECT A") && !project_used(0));
    press(B_OCTUP);
    bad += check("..OCT+ saves it unnamed, SAVE dropped", !name_on() && project_used(0) && msg_is("SAVED UNTIL POWER OFF") &&
                 ui.act == 0u);
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_PROJ && ui.confirm_trk == 0);
    trk[0].p[P_E0] = 5;
    press(B_OCTDN);
    bad += check("OCT- keeps the slot", ui.confirm == CF_NONE && stored_param(0, 0, P_E0) != 5);
    turn(EN_K4, 1);
    press(B_OCTUP);
    press(B_OCTUP);
    bad += check("OCT+ (YES): NAME, the slot not written yet", name_on() && stored_param(0, 0, P_E0) != 5);
    press(B_OCTUP);
    bad += check("OCT+ overwrites it", ui.confirm == CF_NONE && !name_on() && stored_param(0, 0, P_E0) == 5);
    trk[0].p[P_LEVEL] = 11;
    turn(EN_K3, 1);
    press(B_OCTUP);
    bad += check("KNOB 3 LOAD, OCT+: (U1) the LOAD PROJECT A? dialog first, nothing loaded",
                 ui.confirm == CF_LOAD_PROJ && ui.confirm_trk == 0 && trk[0].p[P_LEVEL] == 11);
    press(B_OCTDN);
    bad += check("..OCT- keeps the session (and drops the pick, as the other dialogs)",
                 ui.confirm == CF_NONE && trk[0].p[P_LEVEL] == 11);
    turn(EN_K3, 1);
    press(B_OCTUP);
    press(B_OCTUP);
    bad += check("..KNOB 3, OCT+ twice: the project back (the pick goes with the dialog)", trk[0].p[P_LEVEL] != 11 &&
                 trk[0].p[P_LEVEL] == stored_param(0, 0, P_LEVEL) && msg_is("LOADED") && ui.confirm == CF_NONE);
    /* user presets */
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == GR_USER)
            break;
    ui.page = (uint8_t)i;
    page_entered();
    up_store(4, "OLD");
    apply_preset_to(&trk[0], 1);                  /* ((2.0) the track then on another sound, not the slot's) */
    ui.uslot = 4;
    turn(EN_K4, 1);
    press(B_OCTUP);
    bad += check("USER SAVE over a used slot: the OVERWRITE? dialog", ui.confirm == CF_OVR_USER && ui.confirm_trk == 4u);
    press(B_OCTUP);
    bad += check("YES: NAME with the sound's name (not the old slot's)", name_on() && !str_eq(nm.s, "OLD"));
    press(B_OCTUP);
    {
        char nm[16];
        up_name(4, nm);
        bad += check("OCT+ stores the sound there", ui.confirm == CF_NONE && !str_eq(nm, "OLD"));
    }
    song.playing = 1;
    ui.msg_t = 0;
    turn(EN_K3, 1);                               /* ERASE */
    press(B_OCTUP);
    bad += check("USER ERASE while playing: STOP TO ERASE (U4)", msg_is("STOP TO ERASE") && up_used(4));
    stop_transport();
    return bad;
}

static void go_title(const char *title)
{
    uint32_t i;
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, title); i++)
        ;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t oct_leds_seen(int all)            /* over 1 s: the OCT LED bits lit at some point (all: throughout) */
{
    uint32_t k, any = 0, every = 3u | OCT_BREATH;
    for (k = 0; k < 64u; k++) {
        frame();
        any |= oct_leds();
        every &= oct_leds();
    }
    return all ? every : any;
}

/* the action pages: the knobs pick, OCT+ does it, OCT- cancels / goes HOME; the LEDs; chords */
static int test_actions(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    ui_power_on();
    press(B_OCTUP);
    bad += check("HOME: OCT+ shifts the octave (lit)", song.octave == 1 && oct_leds() == 2u);
    my_steps(t);
    go_page(GR_BANK);                             /* (Skiff phase 5) SAVE > PATTERNS, the pool view: CLEAR always picked */
    bad += check("PATTERNS: CLEAR picked, OCT+ dark on an empty pattern, OCT- lit", act_col() == 4u &&
                 oct_leds_seen(0) == 1u && oct_leds_seen(1) == 1u);
    {
        char a[16], b[16];
        foot_hint(a, b);
        bad += check("..the footer: OCT+ CLEAR / (U2) OCT- HOME", str_eq(a, "OCT+ CLEAR") && str_eq(b, "OCT- HOME"));
    }
    press(B_OCTUP);
    bad += check("..OCT+ on it: NOTHING TO CLEAR, the octave stays", msg_is("NOTHING TO CLEAR") && song.octave == 1 &&
                 !ui.home);
    {
        static uint8_t r[BK_REC];
        bk_pack(r, t);
        pool_put(1, r);                           /* (02 kept, nothing using it) */
    }
    turn(EN_K1, 1);
    bad += check("..KNOB 1 the next pattern (02, nothing uses it): OCT+ breathes", ui.bslot == 1u &&
                 oct_leds_seen(0) == (1u | OCT_BREATH));
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* both together (the UPDATE MODE hold): nothing */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    fm1_in.buttons |= 1u << panel.btn[B_OCTDN];
    host_pressed |= 1u << panel.btn[B_OCTDN];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTDN]);
    frame();
    bad += check("OCT- + OCT+ together: no clear, no HOME, no octave change", pool_used(1) && !ui.confirm && !ui.home &&
                 song.octave == 1);
    go_home();
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];   /* pressed on HOME, let go on PATTERNS: no clear */
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    go_page(GR_BANK);
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("a press that began elsewhere does not act (no dialog)", pool_used(1) && !ui.confirm && song.octave == 2);
    press(B_OCTDN);
    bad += check("PATTERNS: OCT- goes HOME (CLEAR is always picked; the octave stays)", ui.home && song.octave == 2);
    /* the dialogs and the menu */
    open_family(FAM_SEQ);
    my_steps(t);
    confirm_open(CF_CLEAR_SEQ, song.sel);
    frame();
    bad += check("a dialog: OCT+ breathes, OCT- lit", ui.confirm == CF_CLEAR_SEQ && oct_leds_seen(0) == (1u | OCT_BREATH) &&
                 oct_leds_seen(1) == (1u | OCT_BREATH));
    fm1_in.buttons |= 1u << panel.btn[B_OCTUP];
    host_pressed |= 1u << panel.btn[B_OCTUP];
    frame();
    bad += check("..it answers on release, not on the press", ui.confirm == CF_CLEAR_SEQ && !seq_is_empty(t));
    fm1_in.buttons &= ~(1u << panel.btn[B_OCTUP]);
    frame();
    bad += check("..OCT+ let go: cleared", ui.confirm == CF_NONE && seq_is_empty(t));
    bad += check("back on SEQ: the octave LEDs again", oct_leds() == 2u);
    return bad;
}

static int test_tracks(void)
{
    int bad = 0;
    track_t before;
    ui_power_on();
    open_family(FAM_TRK);
    before = trk[0];
    turn(EN_PRESET, 1);
    bad += check("#94 MIXER: the PRESETS knob loads the next sound; the steps and the mix stay",
                 trk[0].preset != before.preset && !memcmp(trk[0].step, before.step, sizeof before.step) &&
                 trk[0].p[P_LEVEL] == before.p[P_LEVEL] && cur_page()->graph == GR_TRK);
    hold(B_SAVE);
    bad += check("#94 MIXER: SAVE held undoes it", trk[0].preset == before.preset && !memcmp(trk[0].p, before.p, sizeof before.p));
    turn(EN_K4, 1);
    bad += check("MIXER KNOB 4 right: MUTE ON (the selected track stays)", trk[0].p[P_MUTE] == 1 && song.sel == 0);
    turn(EN_K1, -3);
    bad += check("KNOB 1 changes LEVEL only (MUTE stays)", trk[0].p[P_MUTE] == 1 && trk[0].p[P_LEVEL] == before.p[P_LEVEL] - 3);
    turn(EN_K2, 2);
    bad += check("KNOB 2 changes PAN", trk[0].p[P_PAN] == before.p[P_PAN] + 2);
    turn(EN_K4, -1);
    bad += check("MIXER KNOB 4 left: MUTE OFF", trk[0].p[P_MUTE] == 0);
    turn(EN_ALGO, 1);
    bad += check("ALGORITHM selects the track", song.sel == 1);
    return bad;
}

/* key k of colour black (1) / white (0) at place p (seq.c key_place) */
static uint32_t key_at(int black, uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) == black && key_place(k) == p)
            return k;
    return 0;
}
static void tap_key(uint32_t k)                   /* a key down for a frame, then up */
{
    fm1_in.notes |= 1u << k;
    host_notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << k);
    frame();
}
static uint32_t lane_steps(const track_t *t, uint32_t l)   /* bit i: lane l strikes at step i (0..31) */
{
    uint32_t i, m = 0;
    for (i = 0; i < 32u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}

/* (issue #3) HOME after SEQ's first page (STEP: the tiles; a drum track's grid): its footer as after any other page.
 * The tiles' PRESETS: STEP and REC: WRITE were drawn under HOLD [HOME] SHIFT (draw_foot asked the page under HOME) */
static int test_home_footer(void)
{
    static uint16_t ref[240 * 240];
    uint32_t d, ok = 1;
    for (d = 0; d < 2u; d++) {                          /* a melodic track, then a drum track (the report's) */
        ui_power_on();
        if (d) {
            set_engine_of(&trk[0], ENGI_DRUM);
            trk[0].engine = trk[0].eng_req;
        }
        open_family(FAM_ENV);
        frame();
        press(B_HOME);
        frame();
        memcpy(ref, host_screen, sizeof ref);
        open_family(FAM_SEQ);
        frame();
        ok &= cur_page()->graph == GR_ROLL && grid_on() == (int)d;
        press(B_HOME);
        frame();
        ok &= ui.home && !memcmp(ref + foot_y() * 240, host_screen + foot_y() * 240, 240u * foot_h() * sizeof *ref);
    }
    return check("#3 HOME after SEQ's first page (NOTES, a drum grid): the footer as after any other page", ok);
}

/* (#22) HOME without its lanes (MENU > LARGE's tall cards; the scope, HOME tapped on HOME): the footer names the track's
 * engine and sound, as with HINTS OFF, not HOLD [HOME] SHIFT; with the lanes the hint stays */
static int test_home_names(void)
{
    static uint16_t named[240 * 240];
    uint32_t at = foot_y() * 240u, sz = 240u * foot_h() * sizeof *named, k, ok = 1;
    for (k = 0; k < 3u; k++) {                          /* LARGE, the scope, the lanes */
        ui_power_on();
        ui_prefs = k == 0u ? PREF_LARGE : 0u;
        if (k == 1u)
            press(B_HOME);                              /* (on HOME: the scope) */
        ui_hints_off = 1;
        ui.force = 1;
        frame();
        memcpy(named, host_screen, sizeof named);
        ui_hints_off = 0;
        ui.force = 1;
        frame();
        ok &= ui.home && ui.home_scope == (k == 1u) && !memcmp(named + at, host_screen + at, sz) == (k < 2u);
    }
    ui_prefs = 0;
    return check("#22 HOME with LARGE or the scope: the footer names the sound (HINTS ON as OFF); the lanes keep the hint",
                 ok);
}

/* (a Learn video comment, 2026-10-19) PLAY on CLOCK USB with no clock coming: a second on, WAITING FOR USB CLOCK, then
 * where CLOCK is; once a start. With the clock's pulses: nothing */
static int test_clock_wait(void)
{
    uint32_t ok;
    ui_power_on();
    song.g[G_CLOCK] = 1;
    press(B_PLAY); events_block(32);
    frames(800);
    ok = song.playing && !msg_is("WAITING FOR USB CLOCK");
    frames(300);
    ok &= msg_is("WAITING FOR USB CLOCK") && str_eq(ui.msg2, "MENU > MIDI > CLOCK");
    ui.msg_t = 0; ui.msg2[0] = 0;
    frames(2000);
    ok &= !msg_is("WAITING FOR USB CLOCK");             /* (once) */
    stop_transport(); frame();
    press(B_PLAY); events_block(32);
    midi_clock_pulse(fm1_ms);                           /* (the clock comes) */
    frames(1500);
    ok &= song.playing && !msg_is("WAITING FOR USB CLOCK");
    song.g[G_CLOCK] = 0;
    stop_transport(); frame();
    return check("PLAY on CLOCK USB with no clock: WAITING FOR USB CLOCK, MENU > MIDI > CLOCK, once; with pulses, quiet", ok);
}

/* (#10) a drum track's grid at one height on SEQ's pages: STEP drew it from the panel's top (cv_oy 0), CHANCE and PLAY
 * centred (pad lower) */
static int test_grid_rows(void)
{
    static uint16_t ref[240 * 240];
    uint32_t ok, at = graph_y() * 240u, sz = 240u * graph_h() * sizeof *ref;
    ui_power_on();
    set_engine_of(&trk[0], ENGI_DRUM);
    trk[0].engine = trk[0].eng_req;
    open_family(FAM_SEQ);
    frame();
    memcpy(ref, host_screen, sizeof ref);
    ok = cur_page()->graph == GR_ROLL && grid_on();
    go_page(GR_FEEL); frame();
    ok &= !memcmp(ref + at, host_screen + at, sz);      /* (the same grid, cursor and lane: the same panel) */
    return check("#10 a drum track's grid at one height on NOTES and FEEL", ok);
}

/* (#15) GLO's MIXER: ALGORITHM picks the track without blanking the faders (draw_tracks filled the whole panel on
 * every ui.force, straight to the screen) */
static uint32_t wide_fills;
static void count_wide_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    (void)x;
    (void)h;
    if (w > 8u && y >= graph_y() && y < graph_y() + graph_h())
        wide_fills++;
}
static int test_mixer_no_flash(void)
{
    uint32_t ok;
    ui_power_on();
    go_page(GR_TRK); frame(); frame();
    wide_fills = 0;
    turn(EN_ALGO, 1);
    ok = song.sel == 1u && !wide_fills && swap16(host_screen[(graph_y() + 2u) * 240u + (uint32_t)MS_X(1) + 2u]) == T_TRK(1);
    return check("#15 MIXER: ALGORITHM moves the band, no fill blanks the faders", ok);
}

static int test_grid(void)
{
    int bad = 0;
    track_t *t = &trk[0];
    uint32_t i, k, leds, ok;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    open_family(FAM_SEQ);
    frame();
    bad += check("SEQ > STEP on a DRUM track is the grid (keys to the UI), GRID in the footer", grid_on() && song.grid == 1u &&
                 cur_page()->scope == SC_STEP);
    bad += check("the lanes: KICK SNARE CLAP HATCL HATOP TOM RIM BELL on GM 36 38 39 42 46 45 37 56",
                 str_eq(drum_lane_name(t, 0), "KICK") && str_eq(drum_lane_name(t, 7), "BELL") && DRUM_LANE_NOTE[3] == 42 &&
                 DRUM_LANE_NOTE[6] == 37 && drum_lane(41) == DV_TOM && drum_lane(49) == DV_BELL && drum_lane(35) == DV_KICK);
    tap_key(key_at(1, 1));
    bad += check("black key 2: lane 2 (SNARE) selected", ui.lane == 1u);
    tap_key(key_at(0, 4));
    bad += check("white key 5: SNARE on step 5, the cursor there", t->step[4].hit == 1u << DV_SNARE && t->step[4].time == ST_NOTE &&
                 !t->step[4].n && ui.cursor == 4u);
    leds = grid_leds();
    bad += check("LEDs: white key 5 lit, black key 2 lit, ACC and the page keys dark (LEN 16)",
                 (leds >> key_at(0, 4) & 1u) && (leds >> key_at(1, 1) & 1u) && !(leds >> key_at(0, 3) & 1u) &&
                 !(leds >> key_at(1, 0) & 1u) && !(leds >> key_at(1, GK_ACC) & 1u) && !(leds >> key_at(1, GK_PGUP) & 1u));
    song.playing = 1;
    t->seq_idx = 4;
    leds = grid_leds();
    ok = !(leds >> key_at(0, 4) & 1u);
    t->seq_idx = 6;
    leds = grid_leds();
    ok &= (leds >> key_at(0, 6) & 1u) && !(leds >> key_at(0, 4) & 1u) == 0u;
    song.playing = 0;
    bad += check("LEDs: the step playing inverted (a hit goes dark, an empty step lights)", ok);
    tap_key(key_at(0, 4));
    bad += check("white key 5 again: off, an empty step (REST)", !t->step[4].hit && t->step[4].time == ST_REST);
    /* ACC held: the white keys set accents, their LEDs show them */
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    frame();
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok = t->step[0].hit == 1u << DV_SNARE && t->step[0].acc == 1u << DV_SNARE && (leds >> key_at(0, 0) & 1u) &&
         (leds >> key_at(1, GK_ACC) & 1u);
    tap_key(key_at(0, 0));
    fm1_in.notes |= 1u << key_at(1, GK_ACC);
    leds = grid_leds();
    ok &= t->step[0].hit == 1u << DV_SNARE && !t->step[0].acc && !(leds >> key_at(0, 0) & 1u);
    fm1_in.notes = 0;
    frame();
    leds = grid_leds();
    ok &= (leds >> key_at(0, 0) & 1u) != 0u;
    bad += check("ACC held: a tap adds the hit accented, again drops the accent only; the LEDs show accents", ok);
    /* the knobs ((2.0) PRESETS the cursor): LANE HIT ACC VELOCITY */
    turn(EN_PRESET, 2);
    turn(EN_K1, 1);
    turn(EN_K2, 1);
    ok = ui.cursor == 2u && ui.lane == 2u && t->step[2].hit == 1u << DV_CLAP;
    turn(EN_K3, 1);
    ok &= t->step[2].acc == 1u << DV_CLAP;
    turn(EN_K2, -1);
    ok &= !t->step[2].hit && !t->step[2].acc && t->step[2].time == ST_REST;
    turn(EN_K3, 1);
    ok &= t->step[2].hit == 1u << DV_CLAP && t->step[2].acc == 1u << DV_CLAP;
    turn(EN_K4, -6);
    ok &= t->step[2].vel == 90u;                         /* (from 96, a hit's) */
    bad += check("(2.0) PRESETS the step, KNOB 1 LANE, 2 HIT on / off, 3 ACC (an accent adds the hit), 4 VELOCITY", ok);
    song.rec = 1u << song.sel;                       /* (U2: armed, step entry) */
    press(B_EDIT);
    song.rec = 0;
    bad += check("EDIT, armed: the cursor step cleared, on to the next", t->step[2].time == ST_REST && !t->step[2].hit && ui.cursor == 3u &&
                 grid_on());
    /* pages: LEN 32, the page keys */
    t->p[P_SLEN] = 32;
    leds = grid_leds();
    tap_key(key_at(1, GK_PGUP));
    ok = ui.bank == 1u && ui.cursor == 19u && (leds >> key_at(1, GK_PGUP) & 1u) && (leds >> key_at(1, GK_PGDN) & 1u);
    tap_key(key_at(0, 15));
    ok &= t->step[31].hit == 1u << DV_CLAP;
    tap_key(key_at(1, GK_PGDN));
    ok &= ui.bank == 0u && ui.cursor == 15u;
    t->p[P_SLEN] = 12;
    frame();
    tap_key(key_at(0, 13));
    ok &= !t->step[13].hit && !t->step[13].n;
    bad += check("pages: black keys 11 / 10 up / down (the cursor along); past LEN a key does nothing", ok);
    t->p[P_SLEN] = 16;
    /* a step's GM notes on their lanes */
    t->step[8] = (step_t){{41, 49, 36, 0}, 3, ST_NOTE, SF_ACCENT, 96, 0, 0};
    ok = step_lanes(&t->step[8]) == ((1u << DV_TOM) | (1u << DV_BELL) | (1u << DV_KICK)) &&
         step_accents(&t->step[8]) == step_lanes(&t->step[8]);
    grid_hit(t, 8, DV_TOM, 0);
    ok &= t->step[8].n == 1 && t->step[8].note[0] == 49 && t->step[8].hit == 1u << DV_KICK && (t->step[8].flags & SF_ACCENT);
    grid_acc(t, 8, DV_KICK, 0);
    ok &= !t->step[8].acc && !(t->step[8].flags & SF_ACCENT) && t->step[8].n == 1 && (step_lanes(&t->step[8]) >> DV_BELL & 1u);
    grid_acc(t, 8, DV_BELL, 1);
    ok &= !t->step[8].n && t->step[8].hit == ((1u << DV_KICK) | (1u << DV_BELL)) && t->step[8].acc == 1u << DV_BELL;
    bad += check("GM notes on the grid: a low tom / crash on their lanes; an edit makes the lane its hit", ok);
    /* the keys in the audio ISR: on the grid only the lane keys play */
    kb_prev = 0;
    fm1_in.notes = (1u << key_at(0, 3)) | (1u << key_at(1, 3)) | (1u << key_at(1, GK_ACC));
    keyboard_block();
    ok = kb_note[key_at(0, 3)] == KB_SILENT && kb_note[key_at(1, 3)] == 42u && kb_note[key_at(1, GK_ACC)] == KB_SILENT;
    fm1_in.notes = 0;
    keyboard_block();
    bad += check("grid keys in the ISR: white keys and ACC silent, black key 4 plays HAT CL (42)", ok);
    /* live recording on the grid: armed and playing, a lane key records its hit at the play head */
    track_defaults_steps(t);
    song.rec = 1;
    song.playing = 1;
    t->seq_idx = 5;
    t->seq_pos = 0;
    fm1_in.notes = 1u << key_at(1, 4);
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = t->step[5].hit == 1u << DV_HATO && !t->step[5].n && t->rskip_n == 0u;
    t->seq_pos = step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), 5) - 10u;   /* late in step 6: into step 7 */
    fm1_in.notes = 1u << key_at(1, 0);
    keyboard_block();
    ok &= t->step[6].hit == 1u << DV_KICK && t->rskip_n == 1u && t->rskip[0] == 36u;
    rec_hold(t, 7, 16);
    fm1_in.notes = 0;
    keyboard_block();
    ok &= t->step[7].time == ST_REST && !t->rh_n;
    bad += check("REC on the grid: lane keys record hits, quantised (the next step late), no TIE holds", ok);
    /* live recording elsewhere (HOME): the GM keys; a lane's note a hit, another GM drum a note */
    go_home();
    frame();
    t->seq_idx = 9;
    t->seq_pos = 0;
    fm1_in.notes = (1u << 7) | (1u << 13) | (1u << 6);   /* C 36 KICK, F# 42 HAT CL, E 35 (a kick 2 st down) */
    keyboard_block();
    fm1_in.notes = 0;
    keyboard_block();
    ok = !song.grid && t->step[9].hit == ((1u << DV_KICK) | (1u << DV_HATC)) && t->step[9].n == 1 && t->step[9].note[0] == 35 &&
         step_lanes(&t->step[9]) == ((1u << DV_KICK) | (1u << DV_HATC));
    song.rec = 0;
    song.playing = 0;
    bad += check("REC on HOME: GM keys quantised into the grid (35 a note on the KICK lane)", ok);
    /* the BEAT phrase (tests/phrases.h) into the DRUM track fills the grid */
    track_defaults_steps(t);
    load_pat16(t, PATTERNS[11].note, PATTERNS[11].flags);
    ok = t->p[P_SLEN] == 16 && lane_steps(t, DV_KICK) == ((1u << 0) | (1u << 6) | (1u << 8) | (1u << 11)) &&
         lane_steps(t, DV_SNARE) == ((1u << 4) | (1u << 12)) && lane_steps(t, DV_HATO) == 1u << 14 &&
         lane_steps(t, DV_HATC) == 0xA6AEu && t->step[0].acc == 1u << DV_KICK && t->step[4].acc == 1u << DV_SNARE &&
         t->step[1].acc == 0u && t->step[16].time == ST_REST;
    for (i = 0; i < 16u; i++)
        ok &= t->step[i].n == 0u && !(t->step[i].flags & SF_ACCENT);
    bad += check("BEAT into a DRUM track: the grid (hits, the accents on 1 5 9 13)", ok);
    open_family(FAM_SEQ);
    ui.lane = DV_KICK;
    cursor_set(0);
    leds = grid_leds();
    ok = 1;
    for (i = 0; i < 16u; i++)
        ok &= (leds >> key_at(0, i) & 1u) == ((0x0941u >> i) & 1u);
    bad += check("..its KICK lane on the white key LEDs (1 7 9 12)", ok);
    {   /* the sound changes, the grid stays and plays as GM notes on another engine */
        step_t keep[NSTEP];
        memcpy(keep, t->step, sizeof keep);
        go_home();
        turn(EN_PRESET, 1);
        set_engine_of(t, 0);
        t->engine = t->eng_req;
        ok = !memcmp(keep, t->step, sizeof keep) && !grid_on();
        t->seq_hold = 0;
        t->seq_n = 0;
        seq_step(t, &t->step[0], div_samples((uint32_t)t->p[P_SDIV]), 0);
        ok &= t->seq_n == 1u && t->seq_notes[0] == 36u;
        seq_release(t);
        bad += check("a sound load keeps the grid; on ANALOG its hits play as their GM notes", ok);
        hold(B_SAVE);                             /* undo the sound loads: DRUM again */
        set_engine_of(t, ENGI_DRUM);
        t->engine = t->eng_req;
    }
    /* the menu: the keys play again */
    go_page(GR_ROLL);
    frame();
    k = song.grid;
    hold(B_HOME);
    frame();
    bad += check("HOME held (the menu): the keys are no longer the grid's", k == 1u && ui.menu && !song.grid);
    menu_close();
    return bad;
}

static int test_screen(void)
{
    int bad = 0;
    ui_power_on();
    ui_say("STOP TO SAVE", "");
    ui.force = 1;
    ui_draw();
    bad += check("a message keeps the header's transport and BPM (drawn right of x 80)", ui.msg_t != 0);
    ui.uboot = 3;
    ui.menu = 1;
    ui.force = 1;
    ui_draw();
    bad += check("UPDATE MODE countdown drawn over the menu", ui.force == 0);
    ui.uboot = 0;
    ui.menu = 0;
    return bad;
}

static int test_favorites(void)
{
    int bad = 0;
    uint32_t total, pos;
    track_t before;
    ui_power_on();
    my_steps(TSEL);
    TSEL->p[P_AMODE] = 2;
    go_page(GR_BROWSE);
    before = *TSEL;
    turn(EN_K3, 1);
    turn(EN_K4, 1);
    pos = preset_pos(&total);
    bad += check("FAV and LIST mark/filter the current sound without loading or touching its steps",
                 preset_favorite() && favorites.filter && total == 1 && pos == 0 &&
                 !memcmp(TSEL, &before, sizeof before));
    bad += check("one favorite occupies one display row, without repeated copies",
                 preset_visible(pos, total, 0) == 0 && preset_visible(pos, total, 1) == total);
    select_engine(ENGI_DRUM);
    before = *TSEL;
    turn(EN_K3, 1);
    bad += check("a DRUM sound can be a favorite on any track", preset_favorite() &&
                 favorite_has(ENGI_DRUM, 0) && !memcmp(TSEL, &before, sizeof before));
    turn(EN_PRESET, 1);
    bad += check("filtered browsing crosses DRUM and synth sounds while retaining the track's pattern and ARP",
                 TSEL->eng_req == 0 && preset_favorite() && !memcmp(TSEL->step, before.step, sizeof before.step) &&
                 TSEL->p[P_AMODE] == 2);
    turn(EN_K3, -1);
    pos = preset_pos(&total);
    bad += check("unmarking the current sound leaves it loaded even when outside the filtered list",
                 !preset_favorite() && total == 1 && pos == total && TSEL->eng_req == 0);
    turn(EN_PRESET, -1);
    bad += check("browsing from a nonfavorite selects the last favorite", TSEL->eng_req == ENGI_DRUM);
    turn(EN_K3, -1);
    before = *TSEL;
    turn(EN_PRESET, 1);
    bad += check("empty favorites explains LIST ALL and retains the current sound and steps",
                 msg_is("NO FAVORITES") && !memcmp(TSEL, &before, sizeof before));
    turn(EN_K4, -1);
    preset_pos(&total);
    bad += check("LIST ALL restores the full browser without an implicit load", !favorites.filter && total > 1 &&
                 !memcmp(TSEL, &before, sizeof before));
    up_store(31, "Favorite");
    up_load(31);
    turn(EN_K3, 1);
    bad += check("user slot 32 has its own favorite reference", favorite_has(NENGINES, 31) && preset_favorite());
    up_store(31, "Renamed");
    bad += check("overwriting or renaming a user slot retains its star", favorite_has(NENGINES, 31));
    up_put(31, 0);
    bad += check("successful erasure removes the user slot star and source label",
                 !favorite_has(NENGINES, 31) && !up_used(31) && !TSEL->user);
    up_store(31, "New sound");
    bad += check("reusing the erased slot does not restore its old star", !favorite_has(NENGINES, 31));
    return bad;
}

/* (Skiff phase 9) PRESETS' LIST by type: KNOB 4 steps ALL, FAV, then the types (ui.c list_mode); a type lists that
 * type's factory sounds of every engine (no user preset), the PRESETS knob browses them; it stops at FX; the type
 * survives the settings' save and load; every factory sound the browser shows has a type */
static int test_preset_tags(void)
{
    int bad = 0, ok = 1;
    uint32_t total, all, k, e, i, r, engs = 0;
    ui_power_on();
    favorites.filter = 0;
    ui_ptag = 0;
    go_page(GR_BROWSE);
    for (r = 0; r < NENG_SHOWN; r++)
        for (e = eng_vis(r), k = 0; k < ENGINES[e]->npresets; k++)
            ok &= preset_orig(ENGINES[e], k) != k ||
                  (ENGINES[e]->presets[k].tag > PT_NONE && ENGINES[e]->presets[k].tag < PT_N);
    bad += check("every factory sound the browser shows has a type", ok);
    {   /* (phase 9) and a name of its own: a type's list mixes the engines (two WINDs read as one sound twice); the
         * retired DIGITAL of a FELUCCA_FM4 build keeps its names (PAD, MARIMBA: FM6's, PHYS's) */
        const char *nm[256];
        uint32_t n = 0, j;
        for (r = 0, ok = 1; r < NENG_SHOWN; r++)
            for (e = eng_vis(r), k = 0; k < ENGINES[e]->npresets; k++)
                if (preset_orig(ENGINES[e], k) == k && n < 256u && !(FELUCCA_FM4 && e == ENGI_DIGITAL)) {
                    for (j = 0; j < n; j++)
                        if (str_eq(nm[j], ENGINES[e]->presets[k].name)) {
                            printf("ui:   %s twice\n", nm[j]);
                            ok = 0;
                        }
                    nm[n++] = ENGINES[e]->presets[k].name;
                }
        bad += check("  every one its own name, across the engines", ok && n > 100u && n < 256u);
    }
    up_store(30, "Mine");
    preset_pos(&all);
    turn(EN_K4, 1);
    turn(EN_K4, 1);
    preset_pos(&total);
    ok = list_mode() == 2u && ui_ptag == PT_BASS && !favorites.filter && total > 4u && total < all;
    for (i = 0; i < total; i++) {
        e = preset_at(i, &k);
        ok &= e < NENGINES && ENGINES[e]->presets[k].tag == PT_BASS;
        engs |= e < NENGINES ? 1u << e : 0u;
    }
    bad += check("LIST BASS: the factory basses only (several engines, no user preset)", ok && (engs & (engs - 1u)));
    for (i = 0, ok = 1; i < total; i++) {
        turn(EN_PRESET, 1);
        ok &= !TSEL->user && ENGINES[TSEL->eng_req]->presets[TSEL->preset].tag == PT_BASS;
    }
    bad += check("the PRESETS knob browses the basses, every one in turn", ok);
    for (i = 0; i < 10u; i++)
        turn(EN_K4, 1);
    bad += check("LIST stops at its last type, FX", list_mode() == PT_N && ui_ptag == PT_FX && !favorites.filter);
    {
        sks_t p;
        settings_export(&p);
        ui_ptag = 0;
        settings_import(&p, (int)sizeof p);
        bad += check("the LIST type survives the settings' save and load", ui_ptag == PT_FX);
        p.s.ptag = 0x7F;
        settings_import(&p, (int)sizeof p);
        bad += check("an unknown stored type reads as none (LIST ALL)", !ui_ptag && !list_mode());
    }
    for (i = 0; i < 12u; i++)
        turn(EN_K4, -1);
    preset_pos(&total);
    bad += check("LIST ALL again: the whole list", !list_mode() && total == all);
    up_put(30, 0);
    return bad;
}

static int test_display_preferences(void)
{
    int bad = 0, k;
    track_t sounds[NTRK];
    uint16_t before[240 * 240];
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_COLOR;
    ui.force = 1; ui_draw();
    memcpy(before, host_screen, sizeof before);
    settings.palette = 1; palette_set(1);
    press(B_OCTUP);
    bad += check("THEME OCT+ previews the next palette (PAPER, light) without closing the menu",
                 settings.palette == 2 && T_BG == UI_PALETTES[2].bg && ux.light && ui.menu == 1 && !song.octave &&
                 memcmp(before, host_screen, sizeof before) && !memcmp(sounds, trk, sizeof sounds));
    bad += check("THEME KNOB 1 steps on to CRYPT, NEON (both the pixel faces), QUARK (Barlow), then wraps to SKIFF (R1: "
                 "SKIFF HI-CON PAPER CRYPT NEON QUARK)",
                 str_eq(MI_NAME[MI_COLOR], "THEME") && (turn(EN_K1, 1), settings.palette == UI_CRYPT_INDEX) && ux.crypt &&
                 str_eq(UI_PALETTES[UI_CRYPT_INDEX].name, "CRYPT") && &AF_S == &AF_S_PX &&
                 (turn(EN_K1, 1), settings.palette == UI_NEON_INDEX) && ux.neon && !ux.crypt && &AF_S == &AF_S_PX &&
                 str_eq(UI_PALETTES[UI_NEON_INDEX].name, "NEON") &&
                 (turn(EN_K1, 1), settings.palette == UI_QUARK_INDEX) && ux.quark && !ux.neon && &AF_S != &AF_S_PX &&
                 str_eq(UI_PALETTES[UI_QUARK_INDEX].name, "QUARK") &&
                 (turn(EN_K1, 1), settings.palette == UI_SKIFF_INDEX) && !ux.quark && !ux.crypt && !ux.neon && &AF_S != &AF_S_PX &&
                 str_eq(UI_PALETTES[UI_SKIFF_INDEX].name, "SKIFF"));
    settings.lowcut = 2;
    ui.menu_sel = MI_LOWCUT;
    press(B_OCTUP);
    k = settings.lowcut == 2u;                         /* (OCT+ stops at the end, as the knobs) */
    press(B_OCTDN);
    k &= settings.lowcut == 1u && fx_lowcut == 1u;
    press(B_OCTDN);
    bad += check("the expanded menu retains all three SPEAKER modes (OCT+ / OCT- step them, stopping at the ends)",
                 k && settings.lowcut == 0 && !fx_lowcut && ui.menu == 1);
    {   /* #42: SPEAKER EQ FLAT (the old OFF, stored 0) LOWCUT BASS+; KNOB 1 steps them, the audio follows at once */
        int ok = str_eq(MI_NAME[MI_LOWCUT], "SPEAKER EQ") && str_eq(SPK_EQ[0], "FLAT") && str_eq(SPK_EQ[1], "LOWCUT") &&
                 str_eq(SPK_EQ[2], "BASS+");
        turn(EN_K1, 1); ok &= settings.lowcut == 1u && fx_lowcut == 1u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u && fx_lowcut == 2u;
        turn(EN_K1, 1); ok &= settings.lowcut == 2u;                  /* (KNOB 1 stops at the ends) */
        turn(EN_K1, -1); turn(EN_K1, -1); ok &= settings.lowcut == 0u && !fx_lowcut;
        bad += check("#42 SPEAKER EQ: FLAT (old OFF) / LOWCUT / BASS+ on KNOB 1, fx_lowcut follows", ok);
    }
    press(B_OCTDN);
    k = ui.menu == 1 && settings.lowcut == 0u;         /* (OCT- at the first value: nothing, the menu stays) */
    press(B_HOME);
    bad += check("HOME leaves display preferences without changing musical state (OCT- no longer closes)",
                 k && !ui.menu && settings.palette == UI_SKIFF_INDEX && !memcmp(sounds, trk, sizeof sounds));
    return bad;
}

/* 1.0.5: the MENU in tabs (ui_menu.c, menu_items.c MI_TAB): every tab 1..MTAB_ROWS rows in a run, each fitting the
 * page in S and LARGE; ALGORITHM between the tabs (wraps, back at a tab's last row), PRESETS within one (wraps), any of
 * KNOB 1..4 and OCT+ / OCT- change a value, OCT+ opens CALIBRATION and ABOUT, HOME closes (OCT- not); reopened at the
 * same tab and row; the tabs slide with ANIM ON (only their strip drawn again), snap with ANIM OFF */
static void wdt_ms(void) { fm1_ms++; }
static void wdt_key(void) { fm1_ms++; if (fm1_ms % 50u == 0u) fm1_in.notes = 1u; }   /* a key pressed after a moment */
static int test_menu_tabs(void)
{
    int bad = 0, ok = 1, drawn;
    uint32_t t, i, n;
    for (t = 0; t < MTAB_COUNT; t++) {
        uint32_t f = mtab_first(t);
        n = mtab_rows(t);
        ok &= n >= 1u && f < MI_COUNT;
        for (i = 0; i < MI_COUNT; i++)                  /* its rows in one run */
            ok &= (MI_TAB[i] == t) == (i >= f && i < f + n);
        ui.menu_sel = (uint8_t)f;
        ui_prefs = 0;                                   /* ((1.3) the rows shown: MTAB_ROWS at most, MIDI's scroll) */
        ok &= mr_n() == (n < MTAB_ROWS ? n : MTAB_ROWS) && mr_y(mr_n()) - mr_gap() + MP_PAD <= MK_Y - 6;
        ui_prefs = PREF_LARGE;
        ok &= mr_y(mr_n()) - mr_gap() + MP_PAD <= MK_Y - 6;
    }
    ok &= MP_Y + MP_PAD + (int32_t)MTAB_ROWS * 25 - 1 + MP_PAD <= MK_Y - 6;   /* (room for MTAB_ROWS: 24 px rows 1 px apart) */
    for (i = 0; i < MI_COUNT; i++) ok &= MI_TAB[i] < MTAB_COUNT;
    {   /* the tab bar: MT_GAP between every two tabs at every place of the slide, inside MT_X0..MT_X1 (S and LARGE) */
        int32_t a[MTAB_COUNT], x[MTAB_COUNT], w[MTAB_COUNT], p;
        for (n = 0; n < 2u; n++)
            for (p = 0; p <= (int32_t)(MTAB_COUNT - 1u) * MT_ONE; p++) {
                ui_prefs = n ? PREF_LARGE : 0u;
                mt.pos = (int16_t)p;
                mt_layout(a, x, w);
                ok &= x[0] >= MT_X0 && x[MTAB_COUNT - 1u] + w[MTAB_COUNT - 1u] <= MT_X1;
                for (t = 1; t < MTAB_COUNT; t++) ok &= x[t] - (x[t - 1u] + w[t - 1u]) == MT_GAP;
            }
    }
    ui_prefs = 0;
    bad += check("MENU tabs: DISPLAY CONTROL AUDIO SYSTEM MIDI ((1.3) MIDI appended), rows in a run, 6 shown fit the page; "
                 "tab gaps constant (S, LARGE)",
                 ok && MTAB_COUNT == 5u + FELUCCA_TAPE && str_eq(MTAB_NAME[0], "DISPLAY") && str_eq(MTAB_NAME[MTAB_MIDI], "MIDI") &&
                 mtab_rows(MTAB_DISPLAY) == 7u && mtab_rows(MTAB_CONTROL) == 6u && mtab_rows(MTAB_AUDIO) == 6u &&   /* (#27; (2.0) RETURN) */
                 mtab_rows(MTAB_SYSTEM) == 6u + FELUCCA_CDC + (FELUCCA_USB_ROUTING ? (FELUCCA_USB_6CH ? UAC_CHANNELS : 2u) : 0u) && mtab_rows(MTAB_MIDI) == 10u);   /* ((Skiff) SYSTEM: CPU
                                                         * CLOCK; (Skiff 2.0) USB SERIAL with the console only) */

    ui_power_on();
    hold(B_HOME);
    ok = ui.menu == 1 && ui.menu_sel == MI_COLOR && menu_tab() == MTAB_DISPLAY;
    for (i = 0; i < 6u; i++) turn(EN_PRESET, 1);       /* ((Skiff #27) PLAYHEAD after COLOR: DISPLAY's seven scroll) */
    ok &= ui.menu_sel == MI_HINTS;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_COLOR;  /* (wraps within the tab) */
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_HINTS;
    turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_SCROFF;
    bad += check("MENU: PRESETS moves within the tab and wraps there (COLOR .. HINTS)", ok);
    turn(EN_ALGO, 1); ok = ui.menu_sel == MI_HOLD && menu_tab() == MTAB_CONTROL;
    turn(EN_PRESET, 1); turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_BPMLOCK;
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_LOWCUT && menu_tab() == MTAB_AUDIO;
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_BPMLOCK;   /* (back at the row left there) */
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_SCROFF;
#if FELUCCA_TAPE
    turn(EN_ALGO,-1);ok &= ui.menu_sel==MI_TAPE && menu_tab()==MTAB_TAPE;
#endif
    turn(EN_ALGO, -1); ok &= ui.menu_sel == MI_CLOCK && menu_tab() == MTAB_MIDI;   /* (wraps; (1.3) MIDI last) */
#if FELUCCA_TAPE
    turn(EN_ALGO,1);ok &= ui.menu_sel==MI_TAPE;
#endif
    turn(EN_ALGO, 1); ok &= ui.menu_sel == MI_SCROFF;
    bad += check("MENU: ALGORITHM steps between the tabs and wraps, each tab back at its last row", ok && ui.menu == 1);
    turn(EN_ALGO, 1); turn(EN_PRESET, -1);            /* CONTROL: from FX LATCH up to KNOB ACCEL */
    ok = ui.menu_sel == MI_ACCEL && !(ui_prefs & PREF_ACCEL);
    for (i = 0; i < 4u; i++) {                         /* any of KNOB 1..4: right ON, left OFF */
        turn(EN_K1 + i, 1); ok &= (ui_prefs & PREF_ACCEL) != 0u;
        turn(EN_K1 + i, -1); ok &= !(ui_prefs & PREF_ACCEL);
    }
    turn(EN_ALGO, 1); turn(EN_PRESET, 1);             /* AUDIO: USB LEVEL */
    ok &= ui.menu_sel == MI_USB;
    turn(EN_K4, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    turn(EN_K2, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;
    bad += check("MENU: any of KNOB 1..4 changes the selected row's value in any tab, the menu stays", ok && ui.menu == 1);

    /* OCT+ / OCT-: the next / previous value on every kind of row (as the knobs: stopping at the ends; COLOR wraps); each
     * new value draws the menu again (drawn: the value in the menu's signature) */
    ok = drawn = 1;
    for (i = 0; i < MI_VALUES; i++) {
        uint32_t n = menu_n(i), v, j, seen, want, s0;
        if (i == MI_TUNE || i == MI_CPU || menu_act(i))   /* ((U3) TUNE's 101 values: below; CPU: below; (1.3) MIDI
                                                        * LEARN, LEARN CLEAR: actions, test_midi_learn) */
            continue;
        ui.menu_sel = (uint8_t)i;
        for (j = 0; j <= n; j++)                       /* down to the first value (COLOR: round) */
            press(B_OCTDN);
        seen = 1u << menu_get(i);
        for (j = 0; j <= n; j++) {                     /* up through every value */
            want = menu_step(i, 1);
            v = menu_get(i);
            s0 = ui.menu_sig;
            press(B_OCTUP);
            ok &= menu_get(i) == want && ui.menu == 1;
            drawn &= menu_get(i) == v || ui.menu_sig != s0;
            seen |= 1u << menu_get(i);
        }
        ok &= seen == (1u << n) - 1u;
        for (j = 0; j <= n; j++) {                     /* and down again */
            want = menu_step(i, -1);
            v = menu_get(i);
            s0 = ui.menu_sig;
            press(B_OCTDN);
            ok &= menu_get(i) == want && ui.menu == 1;
            drawn &= menu_get(i) == v || ui.menu_sig != s0;
        }
        v = menu_get(i);                               /* the ends: COLOR wraps, the rest stop */
        if (i == MI_COLOR) {
            menu_put(i, n - 1u); press(B_OCTUP); ok &= menu_get(i) == 0u;
            press(B_OCTDN); ok &= menu_get(i) == n - 1u;
        } else {
            press(B_OCTDN); ok &= menu_get(i) == v;
            for (j = 0; j < n; j++) press(B_OCTUP);
            v = menu_get(i);
            press(B_OCTUP); ok &= menu_get(i) == v && v != menu_step(i, -1);
        }
        want = menu_step(i, -1);                       /* a knob steps as OCT does */
        turn(EN_K3, -1); ok &= menu_get(i) == want && ui.menu == 1;
    }
    ui_prefs = 0; settings_hold = HOLD_DEF; settings_leds = LEDS_DIM;
    settings.lowcut = 0; fx_lowcut = 0; menu_put(MI_COLOR, UI_SKIFF_INDEX);
    song.g[G_CLOCK] = song.g[G_ROUTE] = 0;
    menu_put(MI_PREVIEW, 0);
    memset(&favorites.factory[15][6], 0, 6);            /* ((1.3) T1..T6 on channels 1..6 again) */
    bad += check("MENU: OCT+ / OCT- step every value row up / down (THEME wraps, the rest stop), the menu stays", ok);
    bad += check("MENU: every value row's new value draws the menu again (its value in the signature; 1.3.1's STEP "
                 "PREVIEW and SCREEN OFF stayed as drawn)", drawn);
    ui.menu_sel = MI_TUNE;                              /* ((Skiff U3, R8) TUNE: cents, the unit's) */
    press(B_OCTUP); press(B_OCTUP);
    ok = song.g[G_TUNE] == 2 && str_eq(menu_vname(MI_TUNE, menu_get(MI_TUNE)), "+2 CT") &&
         str_eq(menu_vname(MI_TUNE, 40), "-10 CT") && str_eq(menu_vname(MI_TUNE, 50), "0 CT");
    ui.menu_sel = MI_CPU;
#if FELUCCA_CPU2
    song.cpu_q8 = 36;
    ok &= str_eq(menu_vname(MI_CPU, 0), "1 CORE 14 %");
    {
        uint32_t s0;
        frame();
        s0 = ui.menu_sig;
        press(B_OCTUP);
        ok &= menu_n(MI_CPU) == 2u && song.g[G_TUNE] == 2 && CPU2_SET && str_eq(menu_vname(MI_CPU, 1), "2 CORES: RESTART") &&
              ui.menu == 1u && ui.menu_sig != s0;        /* (the row drawn again: its value is in the menu's signature) */
        s0 = ui.menu_sig;
        song.cpu_q8 = 72;                               /* (the load it shows changed: drawn again too) */
        frame();
        ok &= ui.menu_sig != s0;
        song.playing = 1;                               /* (playing: the settings wait for STOP, a restart would lose */
        frame();                                        /* it: the row says so, and is drawn again at STOP) */
        s0 = ui.menu_sig;
        ok &= str_eq(menu_vname(MI_CPU, 1), "2 CORES: STOP");
        song.playing = 0;
        frame();
        ok &= ui.menu_sig != s0 && str_eq(menu_vname(MI_CPU, 1), "2 CORES: RESTART");
    }
    press(B_OCTDN);
    song.cpu_q8 = 36;
    ok &= !CPU2_SET && str_eq(menu_vname(MI_CPU, 0), "1 CORE 14 %");
    song.cpu_q8 = 0;
    song.g[G_TUNE] = 0;
    bad += check("(U3) MENU > AUDIO > TUNE steps in cents (+2 CT); (Skiff 1.3) SYSTEM > CPU 1 CORE / 2 CORES (from the next "
                 "power-on: RESTART), the load after it", ok);
#else
    press(B_OCTUP);
    ok &= menu_n(MI_CPU) == 1u && song.g[G_TUNE] == 2;
    song.g[G_TUNE] = 0;
    bad += check("(U3) MENU > AUDIO > TUNE steps in cents (+2 CT); SYSTEM > CPU is read only", ok);
#endif
    {   /* (Skiff) SYSTEM > CPU CLOCK: 360 MHZ (bit clear) / 320 MHZ, bit 2 of the CPU byte, from the next power-on (the host
         * runs at 360: RESTART; playing STOP), REFUSED after a boot that refused it; RESET SETTINGS: 360 */
        uint32_t s0, cpu = CPU2_BYTE & 3u;
        ui.menu_sel = MI_CLK;
        frame();
        s0 = ui.menu_sig;
        ok = !CLK320_SET && str_eq(menu_vname(MI_CLK, 0), "360 MHZ") && MI_TAB[MI_CLK] == MTAB_SYSTEM;
        press(B_OCTUP);
        ok &= CLK320_SET && (CPU2_BYTE & 3u) == cpu && str_eq(menu_vname(MI_CLK, 1), "320 MHZ: RESTART") &&
              ui.menu_sig != s0;
        song.playing = 1;
        ok &= str_eq(menu_vname(MI_CLK, 1), "320 MHZ: STOP");
        song.playing = 0;
        clk320.state = CLK_REFUSED;
        ok &= str_eq(menu_vname(MI_CLK, 1), "320 MHZ: REFUSED");
        clk320.state = CLK_NONE;
        press(B_OCTDN);
        ok &= !CLK320_SET && (CPU2_BYTE & 3u) == cpu && str_eq(menu_vname(MI_CLK, 0), "360 MHZ");
        bad += check("(Skiff) MENU > SYSTEM > CPU CLOCK: 360 / 320 MHZ, a bit of its own (RESTART, STOP while playing, "
                     "REFUSED)", ok);
    }
    {   /* (1.2.1) a step of TUNE, CLOCK, MIDI IN draws the row again at once, and the CPU row follows the load (their
         * values were not in the menu's signature: they stayed as drawn until the cursor moved) */
        static const uint8_t RW[] = {MI_TUNE, MI_CLOCK, MI_ROUTE};
        uint32_t s0;
        ok = ui.menu == 1u;
        for (i = 0; i < NELEM(RW); i++) {
            ui.menu_sel = RW[i];
            frame();
            s0 = ui.menu_sig;
            press(B_OCTUP);
            ok &= ui.menu_sig != s0;
            press(B_OCTDN);
        }
        ui.menu_sel = MI_CPU;
        frame();
        s0 = ui.menu_sig;
        song.cpu_q8 = 72;
        frame();
        ok &= ui.menu_sig != s0;
        song.cpu_q8 = 0;
        song.g[G_TUNE] = song.g[G_CLOCK] = song.g[G_ROUTE] = 0;
        bad += check("(1.2.1) MENU: a step of TUNE, CLOCK or MIDI IN redraws its row; CPU follows the load", ok);
    }
    {   /* the ON / OFF rows: up (a knob right, OCT+) is ON, the switch's knob to the right, whichever bit it is */
        static const uint8_t SW[] = {MI_LARGE, MI_ANIM, MI_ACCEL, MI_BPMLOCK,
#if FELUCCA_CDC
                                     MI_SERIAL,
#endif
                                     MI_RESTORE};
        ok = 1;
        for (i = 0; i < NELEM(SW); i++) {
            ui.menu_sel = SW[i];
            press(B_OCTDN); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            press(B_OCTUP); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
            turn(EN_K2, -1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "OFF");
            turn(EN_K2, 1); ok &= str_eq(menu_vname(SW[i], menu_get(SW[i])), "ON");
        }
        ui.menu_sel = MI_LOWCUT;
        press(B_OCTUP); ok &= settings.lowcut == 1u;   /* (the others: up the second, LOWCUT) */
        press(B_OCTDN); ok &= settings.lowcut == 0u;
        ui_prefs = 0;
        bad += check("MENU: on an ON / OFF row OCT+ / a knob right is ON, OCT- / left OFF (ANIM, USB SERIAL, RESTORE LAST too)", ok);
    }
    ui.menu_sel = MI_USB;
    press(B_HOME);
    ok = !ui.menu;
    hold(B_HOME);
    ok &= ui.menu == 1 && ui.menu_sel == MI_USB && menu_tab() == MTAB_AUDIO;
    frames(500);
    ok &= ui.menu == 1;                                 /* (the hold's release: no tap, the menu stays) */
    hold(B_HOME);
    ok &= !ui.menu;
    frames(1000);
    ok &= !ui.menu;                                     /* (the hold that closed it: no reopen) */
    hold(B_HOME);
    bad += check("MENU: HOME pressed closes it, held too (no reopen); held opens it again at the same tab and row",
                 ok && ui.menu == 1 && ui.menu_sel == MI_USB);
    turn(EN_ALGO, 1); turn(EN_PRESET, -1);            /* SYSTEM: ABOUT */
    ok = ui.menu_sel == MI_ABOUT;
    frame();
    ok &= oct_leds() == OCT_BREATH;                     /* (OCT+ opens, OCT- does nothing: dark) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (no close, no back) */
    turn(EN_K1, 1); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    press(B_OCTUP); ok &= ui.menu == 2;
    ok &= oct_leds() == 1u;                             /* (ABOUT: OCT- back, lit) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;
    turn(EN_PRESET, -1); turn(EN_PRESET, -1); ok &= ui.menu_sel == MI_PANEL;   /* ((U4) past RESET SETTINGS) */
    press(B_OCTDN); ok &= ui.menu == 1 && ui.menu_sel == MI_PANEL;
    {   /* CALIBRATION: OCT+ starts the setup (nothing pressed here: it times out and keeps the table) */
        panel_t p0 = panel;
        host_wdt_hook = wdt_ms;
        press(B_OCTUP);
        host_wdt_hook = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1;
    }
    {   /* CALIBRATION: a piano key cancels at once (no 30 s wait), the table kept */
        panel_t p0 = panel;
        uint32_t t = fm1_ms;
        host_wdt_hook = wdt_key;
        press(B_OCTUP);
        host_wdt_hook = 0;
        fm1_in.notes = 0;
        ok &= !memcmp(&p0, &panel, sizeof panel) && msg_is("SETUP CANCELLED") && ui.menu == 1 && fm1_ms - t < 1000u;
    }
    bad += check("MENU SYSTEM: OCT+ opens ABOUT (OCT- back to the tab) and CALIBRATION; OCT- does nothing on them", ok);
    turn(EN_PRESET, 1); turn(EN_PRESET, 1); press(B_OCTUP);   /* ABOUT, HOME: closed, from the document */
    ok = ui.menu == 2;
    press(B_HOME);
    ok &= !ui.menu && ui.home;
    hold(B_HOME); ok &= ui.menu == 1 && ui.menu_sel == MI_ABOUT;   /* (opened at the tabs, ABOUT's row) */
    turn(EN_ALGO, -1); frame();
    {   /* a value row: OCT- and OCT+ both breathe, each while that way has a value to go to; none lit */
        uint32_t r = ui.menu_sel, want = (menu_step(r, -1) != menu_get(r) ? OCT_BREATH_DN : 0u) |
                                         (menu_step(r, 1) != menu_get(r) ? OCT_BREATH : 0u);
        ok &= r < MI_VALUES && oct_leds() == want && want != 0u;
        menu_put(MI_HOLD, 0u); ui.menu_sel = MI_HOLD;   /* HOLD at its first value: OCT- dark, OCT+ breathing */
        ok &= oct_leds() == OCT_BREATH;
        menu_put(MI_HOLD, 1u);                          /* in between: both breathe */
        ok &= oct_leds() == (OCT_BREATH | OCT_BREATH_DN);
    }
    menu_close();
    bad += check("MENU: HOME closes ABOUT too; reopened at the tabs; a value row breathes OCT- / OCT+ where they can go", ok);

    /* the slide: ANIM ON, a few frames from DISPLAY to CONTROL drawing only the tab strip; ANIM OFF, at once */
    ui_power_on();
    hold(B_HOME);
    frame();
    turn(EN_ALGO, 1);
    ok = mt.pos > 0 && mt.pos < MT_ONE;                 /* (a step on the way) */
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == (uint32_t)(MP_Y - H_HEAD);  /* (only the strip) */
    for (i = 0; i < 8u && mt.pos != MT_ONE; i++) frame();
    ok &= mt.pos == MT_ONE && i <= 6u;
    host_blit_rows = 0; frame();
    ok &= host_blit_rows == 0u;                         /* (settled: nothing drawn) */
    ui_prefs |= PREF_ANIM_OFF;
    turn(EN_ALGO, 1);
    ok &= mt.pos == 2 * MT_ONE;
    ui_prefs &= ~PREF_ANIM_OFF;
    menu_close();
    bad += check("MENU tabs slide with ANIM ON (their strip only, settled within 6 frames), snap with ANIM OFF", ok);
    return bad;
}

static int test_information(void)
{
    uint16_t header[240 * H_HEAD], footer[240 * 19], battery[240 * H_HEAD];
    track_t sounds[NTRK];
    int bad = 0, quiet = 1;
    uint32_t x, y;
    ui_power_on();
    memcpy(sounds, trk, sizeof sounds);
    hold(B_HOME);
    ui.menu_sel = MI_ABOUT;
    press(B_OCTUP);
    ui_draw();
    bad += check("ABOUT opens one document with scrollable license text and credits", ui.menu == 2 && !ui.menu_scroll && menu_scroll_max() > MENU_DOC_H);
    for (y = 146; y < 220; y++)
        for (x = 158; x < 232; x++)
            if (x < 166 || x >= 224 || y < 154 || y >= 212)
                quiet &= host_screen[y * 240 + x] == swap16(UI_QR_LIGHT);
    bad += check("ABOUT QR keeps its complete white quiet zone above the fixed footer", quiet);
    memcpy(header, host_screen, sizeof header);
    memcpy(footer, host_screen + 221 * 240, sizeof footer);
    turn(EN_PRESET, 3);
    ui_draw();
    bad += check("PRESETS scrolls ABOUT with its header and controls fixed",
                 ui.menu == 2 && ui.menu_scroll > 0 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    turn(EN_PRESET, 12);
    ui_draw();
    bad += check("PRESETS continues from ABOUT into CREDITS without changing pages",
                 ui.menu == 2 && ui.menu_scroll > 216 && !memcmp(header, host_screen, sizeof header) &&
                 !memcmp(footer, host_screen + 221 * 240, sizeof footer));
    uint16_t position = ui.menu_scroll;
    press(B_OCTUP);
    bad += check("OCT+ does not switch pages or alter the octave in the document",
                 ui.menu == 2 && ui.menu_scroll == position && song.octave == 0);
    turn(EN_PRESET, 10000);
    turn(EN_PRESET, 10000);
    ui_draw();
    bad += check("the continuous document stops at its final credit",
                 ui.menu == 2 && ui.menu_scroll == menu_scroll_max());
    turn(EN_PRESET, -10000);
    bad += check("scrolling back through CREDITS returns to ABOUT's top", ui.menu == 2 && !ui.menu_scroll);
    press(B_OCTDN);
    bad += check("OCT- returns to the menu without changing sounds or patterns",
                 ui.menu == 1 && !memcmp(sounds, trk, sizeof sounds));
    press(B_OCTUP);
    bad += check("reopening ABOUT starts the continuous document at the top", ui.menu == 2 && !ui.menu_scroll);
    menu_close();
    ui.msg_t = 0;
    usb.config = 1; usb.suspended = 0;
    song.batt_raw = 530;
    ui.force = 1; draw_head(); ui.force = 0;
    memcpy(battery, host_screen, sizeof battery);
    uint32_t signature = ui.head_sig;
    for (x = 0; x < 4; x++) { fm1_ms += 600; draw_head(); }
    bad += check("USB power shows a steady bolt inside the battery without animation",
                 batt_shown() == 4 && ui.head_sig == signature && !memcmp(battery, host_screen, sizeof battery) &&
                 host_screen[6 * 240 + 217] != 0 && host_screen[9 * 240 + 225] != 0);
    usb.suspended = 1;
    draw_head();
    bad += check("USB suspension restores the battery outline and measured level",
                 batt_shown() == 0 && ui.head_sig != signature && host_screen[6 * 240 + 217] != 0);
    usb.config = 0; usb.suspended = 0;
    song.batt_raw = 531; int levels = batt_shown() == 1;
    song.batt_raw = 561; levels &= batt_shown() == 2;
    song.batt_raw = 591; levels &= batt_shown() == 3;
    bad += check("battery operation retains all three ADC level thresholds", levels);
    {   /* (#5) the battery at every level, not only when low or on USB: its icon (THEME) in its cell */
        static const uint16_t RAW[2] = {561, 600};
        uint32_t i, seen, all = 1;
        for (i = 0; i < 2u; i++) {
            song.batt_raw = RAW[i];
            ui.force = 1; draw_head(); ui.force = 0;
            for (seen = 0, y = 0; y < H_HEAD; y++)
                for (x = HEAD_BAT_X; x < HEAD_BAT_X + 24u; x++)
                    seen |= swap16(host_screen[y * 240 + x]) == T_THEME;
            all &= seen;
        }
        bad += check("#5 the header shows the battery at levels 2 and 3 too", all);
    }
    return bad;
}

/* ROLL: the rolling digits of the four cards and the header BPM (ui_draw.c roll_*). The GLOBAL page: BPM (also
 * in the header), SWG, CLK (a name), TUNE (signed). A roll frame differs from the static render only inside the
 * value strip, its last frame is the static render; unchanged digits stay; the direction follows the sign; a
 * change mid-roll retargets; a fast turn, another shape, a page / track / palette change and ui.force snap. */
static uint16_t roll_shot[240 * 240];
static void roll_settle(void)                     /* GLOBAL, nothing rolling, no knob hot */
{
    uint32_t k;
    go_title("GLOBAL");
    ui.hot_t = 0; ui.bpm_t = 0;
    for (k = 0; k < 12u; k++) frame();
}
/* host_screen against roll_shot: the pixels that differ inside the box (in = 1) or outside it */
static uint32_t roll_diff(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int in)
{
    uint32_t n = 0;
    int32_t x, y;
    for (y = 0; y < 240; y++)
        for (x = 0; x < 240; x++) {
            int inside = x >= x0 && x < x1 && y >= y0 && y < y1;
            if (inside == in && host_screen[y * 240 + x] != roll_shot[y * 240 + x]) n++;
        }
    return n;
}
/* the mean row (x16) of the ink (not the card surface) in columns x0..x1-1, rows y0..y1-1 of a screen */
static int32_t roll_ink_row(const uint16_t *s, int32_t x0, int32_t x1, int32_t y0, int32_t y1)
{
    int32_t x, y, n = 0, sum = 0;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            if (s[y * 240 + x] != swap16(T_SURF)) { n++; sum += y * 16; }
    return n ? sum / n : -1;
}
static int roll_static_now(void)                  /* the screen is the static render (a forced redraw changes nothing) */
{
    memcpy(roll_shot, host_screen, sizeof roll_shot);
    ui.force = 1;
    ui_draw();
    return roll_diff(0, 0, 0, 0, 0) == 0;
}
#define STRIP(c) CARD_X(c), Y_LABEL + ROLL_Y, CARD_X(c) + COL_W, Y_LABEL + ROLL_Y + ROLL_H
static int test_roll(void)
{
    int bad = 0, ok;
    uint32_t k, n, e, id;
    int32_t up, down, st;
    static const struct { const char *a, *b; int d; } DIRS[] = {
        {"120", "121", 1}, {"121", "120", -1}, {"129", "130", 1}, {"-12", "-13", -1}, {"-13", "-12", 1}, {"+5", "+6", 1},
        {"1.25", "1.30", 1}, {"0.9", "0.8", -1}, {"9", "10", 0}, {"0", "-1", 0}, {"+5", "-5", 0}, {"C3", "C4", 0},
        {"1/4", "1/8", 0}, {"OFF", "ON", 0}, {"2X", "4X", 0}, {"120", "120", 0}, {"123456", "123457", 1},
        {"1234567", "1234568", 0}, {"12%", "13%", 0}};

    ok = 1;
    for (k = 0; k < sizeof DIRS / sizeof DIRS[0]; k++)
        if (roll_dir(DIRS[k].a, DIRS[k].b) != DIRS[k].d) { printf("  roll_dir %s -> %s\n", DIRS[k].a, DIRS[k].b); ok = 0; }
    bad += check("roll: numbers of one shape roll by the sign of the change; names, length, sign changes snap", ok);
    /* ((Skiff U2) BLOCKS, the only look, snaps its cards' values and the header's BPM: the rolling digits are LARGE's
     * tall cards' only (draw_column), checked there) */
    ui_power_on();
    return bad;
}

/* #63 ("parameters moving on their own, fixed by a page change"): a soak of the cards and the rolling digits.
 * ~40 min of UI frames: random turns of KNOB 1..4 and SELECT (bursts back and forth), page / HOME / track
 * changes, the transport and REC (motion recorded and played back), LFO rates, uneven frame times, ui.frame
 * and fm1_ms wrapping. After every frame no roll is older than ROLL_FRAMES; at random quiet moments (no
 * roll, nothing hot) the cards are pixel-identical to a forced redraw (what a page change would show). */
static int test_soak_cards(void)
{
    uint32_t seed = 63u, f, quiet = 0, old = 0, stale = 0, cmp = 0, c;
    static uint16_t shot[240 * CARD_H];
#define SOAK_RND(n) ((seed = seed * 1103515245u + 12345u) >> 8) % (n)
    ui_power_on();
    ui.frame = 0xFFFFFF00u;
    fm1_ms = 0xFFFFFFFFu - 60000u;
    for (f = 0; f < 150000u; f++) {
        uint32_t r = SOAK_RND(1000u);
        if (r < 25u) {                                  /* a knob, 1..3 detents either way */
            uint32_t role = SOAK_RND(5u) ? EN_K1 + SOAK_RND(4u) : EN_SELECT;
            int32_t s = (int32_t)SOAK_RND(3u) + 1;
            host_enc[panel.enc[role]] += (SOAK_RND(2u) ? s : -s) * panel.dir[role];
            quiet = 0;
        } else if (r < 29u) {                           /* a page */
            uint32_t i = SOAK_RND(NPAGES);
            if (page_visible(i)) { ui.home = 0; ui.page = (uint8_t)i; page_entered(); }
            quiet = 0;
        } else if (r < 30u) { ui.home = 1; page_entered(); quiet = 0; }
        else if (r < 31u) transport_req = song.playing ? 2u : 1u;
        else if (r == 31u && SOAK_RND(3u) == 0u) rec_tap();
        else if (r == 32u) { host_enc[panel.enc[EN_ALGO]] += SOAK_RND(2u) ? 1 : -1; quiet = 0; }
        else if (r == 33u) trk[SOAK_RND(NTRK)].p[P_LRATE] = (int16_t)SOAK_RND(128u);
        fm1_ms += SOAK_RND(50u) ? 16u : 1u + SOAK_RND(200u);
        host_ticks += 16000u + SOAK_RND(3u) * 4000u;
        ui_input();
        if (song.playing || transport_req) events_block(FS * 16u / 1000u);
        ui_draw();
        quiet++;
        if (ui.menu || ui.layer || ui.confirm || name_on() || ui.uboot)
            continue;                                   /* (the cards are not drawn) */
        for (c = 0; c < 5u; c++)
            old += ui.roll[c].from[0] && (uint8_t)(ui.frame - ui.roll[c].t0) >= ROLL_FRAMES;
        if (quiet > 45u && !ui.hot_t && !ui.bpm_t && !ui.msg_t && SOAK_RND(8u) == 0u) {
            for (c = 0; c < 5u && !ui.roll[c].from[0]; c++) {}
            if (c < 5u)
                continue;
            memcpy(shot, &host_screen[Y_LABEL * 240], sizeof shot);
            ui.force = 1;
            ui_draw();
            cmp++;
            stale += memcmp(shot, &host_screen[Y_LABEL * 240], sizeof shot) != 0;
        }
    }
#undef SOAK_RND
    if (old || stale || cmp < 500u) printf("  soak: %u old rolls, %u stale cards in %u checks\n", old, stale, cmp);
    stop_transport();
    memset(&motion, 0, sizeof motion);                  /* (what it recorded: the tests below start without) */
    memset(motion_active, 0, sizeof motion_active);
    motion_base_valid = motion_full = 0;
    ui_power_on();
    return check("#63 soak: 150000 random frames, wrapping clocks, motion: no roll lasts, cards = a forced redraw",
                 !old && !stale && cmp >= 500u);
}

/* Product controls are exercised through the actual button/encoder handlers
 * and canvas, including all pages, both fonts and every shipped palette. */
/* Discussions #92 / #94: the PRESETS knob on every page. SEQ's step pages ((2.0) NOTES and the DRUM grid, FEEL): the
 * step cursor, the sound and the steps untouched; (2.0) PATTERN: the track's DIR; USER, PATTERNS, PROJECT: the
 * selection, as KNOB 1; TOOLS: nothing; HOME, PRESETS and every other page: the next sound, the steps untouched, SAVE
 * held undoes it */
static int test_presets_knob(void)
{
    int bad = 0, cur_ok = 1, sel_ok = 1, snd_ok = 1, undo_ok = 1, dir_ok = 0, grid_ok;
    uint32_t i, n_snd = 0, n_cur = 0;
    for (i = 0; i < NPAGES; i++) {
        track_t before;
        uint32_t g = PAGES[i].graph, uslot;
        int16_t slot;
        ui_power_on();
        sel_set(0);
        my_steps(TSEL);
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        frame();
        before = *TSEL;
        uslot = ui.uslot; slot = song.g[G_SLOT];
        turn(EN_PRESET, 1);
        {
            int same_snd = TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p);
            int same_steps = !memcmp(TSEL->step, before.step, sizeof before.step);
            if (PAGES[i].fam == FAM_SEQ && (step_page(g) || g == GR_FEEL)) {
                cur_ok &= ui.cursor == 1u && same_snd && same_steps && ui.page == i;
                n_cur++;
            } else if (g == GR_STEPS) {                /* ((2.0) PATTERN: DIR, FWD to RVRS) */
                dir_ok = TSEL->p[P_DIR] == before.p[P_DIR] + 1 && same_steps && ui.cursor == 0u && ui.page == i;
            } else if (g == GR_USER) {
                sel_ok &= ui.uslot == uslot + 1u && same_snd && same_steps;
            } else if (g == GR_BANK) {                 /* (Skiff) PATTERNS: the slot */
                sel_ok &= ui.bslot == 1u && same_snd && same_steps;
            } else if (g == GR_SONG) {                 /* (Skiff phase 5) SONG: the entry */
                sel_ok &= sg.entry == 1u && same_snd && same_steps;
            } else if (g == GR_SLOTS) {
                sel_ok &= song.g[G_SLOT] == slot + 1 && same_snd && same_steps;
            } else if (g == GR_EVENTS) {   /* (AUTOMATION, U3: the list; its row) */
                sel_ok &= same_snd && same_steps && ui.page == i;
            } else {
                snd_ok &= !same_snd && same_steps && ui.page == i && !ui.home;
                hold(B_SAVE);
                undo_ok &= TSEL->preset == before.preset && TSEL->eng_req == before.eng_req &&
                           !memcmp(TSEL->p, before.p, sizeof before.p) && !memcmp(TSEL->step, before.step, sizeof before.step);
                n_snd++;
            }
        }
    }
    bad += check("#92 PRESETS on NOTES, FEEL: the step cursor; the sound and the steps stay", cur_ok && n_cur == 2u);
    bad += check("(2.0) PRESETS on PATTERN: the track's DIR, the steps and the cursor stay", dir_ok);
    bad += check("#94 PRESETS on USER, PATTERNS, PROJECT, AUTOMATION, SONG: the selection (KNOB 1's); nothing loaded", sel_ok);
    bad += check("#94 PRESETS elsewhere (EDIT, ENV, LFO, FX, SCL, ARP, MIXER, GLOBAL, ...): the next sound, the steps stay",
                 snd_ok && n_snd >= 15u);
    bad += check("#94   SAVE held undoes each of those loads", undo_ok);
    ui_power_on();
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    TSEL->p[P_SLEN] = 32;
    go_page(GR_ROLL);
    frame();
    turn(EN_PRESET, 17);
    grid_ok = grid_on() && ui.cursor == 17u && ui.bank == 1u && TSEL->eng_req == ENGI_DRUM;
    turn(EN_PRESET, -18);
    grid_ok &= ui.cursor == 31u;
    bad += check("#92 the DRUM grid: PRESETS moves the cursor (and the page), as KNOB 1; wraps", grid_ok);
    ui_power_on();
    hold(B_HOME);
    i = ui.menu_sel;
    turn(EN_PRESET, 1);
    bad += check("#94 the menu: PRESETS still moves the selection", ui.menu == 1 && ui.menu_sel == i + 1u);
    return bad;
}

static int test_product_ux(void)
{
    int bad = 0, ok = 1;
    uint32_t i, p, b;
    for (i = 0; i < NPAGES; i++) {
        ui_power_on(); set_engine_of(TSEL, 1);         /* (DIGITAL; without FELUCCA_FM4 FM6: no OP pages) */
        ui.home = 0; ui.page = (uint8_t)i; page_entered();
        if (!page_visible(i))
            continue;
        press(B_REC);
        if (step_page(PAGES[i].graph))            /* #133: STEP (its tiles, ROLL) arms step recording, stopped */
            ok &= song.rec == 1 && !transport_req && ui.page == i && !ui.home;
        else ok &= song.rec == 1 && transport_req == 1 && ui.page == i && !ui.home;
    }
    bad += check("REC stays on every page (STEP: step recording, stopped)", ok);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the MENU does nothing (the menu stays, no arm, no transport start)",
                 ui.menu == 1 && song.rec == 0 && !transport_req);
    ui.menu_sel = MI_ABOUT; press(B_OCTUP); press(B_REC);
    bad += check("REC in ABOUT does nothing (the document and its scroll stay)",
                 ui.menu == 2 && !ui.menu_scroll && song.rec == 0 && !transport_req);
    ui_power_on(); press(B_GLO);
    bad += check("GLO directly opens the four-channel MIXER", cur_page()->graph == GR_TRK);
    int16_t len = TSEL->p[P_SLEN], send = TSEL->p[P_REV];
    turn(EN_K3, 1);
    bad += check("MIXER K3 edits reverb, never sequence length", TSEL->p[P_SLEN] == len && TSEL->p[P_REV] == send + 1);
    frames(320);                                       /* (taps further apart than a double tap: #83) */
    press(B_GLO); frames(320); ok = cur_page()->fam == FAM_GLO && str_eq(cur_page()->title, "DELAY");
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "REVERB");
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "CHORUS");
    press(B_GLO); frames(320); ok &= str_eq(cur_page()->title, "GLOBAL");
    press(B_GLO); ok &= cur_page()->graph == GR_TRK;
    bad += check("(U3) GLO cycles MIXER > DELAY > REVERB > CHORUS > GLOBAL > MIXER", ok);
    go_home(); hold(B_SEQ);
    bad += check("(U2) long SEQ anywhere: the SEQ TOOLS map (a peek), no page", ui.home && !ui.layer);
    ui_power_on(); open_family(FAM_EDIT);
    ok = 1;
    for (i = 0; i < 8u; i++) { open_family(FAM_EDIT); ok &= page_visible(ui.page) && !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); }
    bad += check("operator envelope pages are hidden on non-DIGITAL instruments", ok);
#if FELUCCA_FM4
    set_engine_of(TSEL, 1); open_family(FAM_EDIT); ok = 0;
    for (i = 0; i < 12u; i++) { if (cur_page()->id[0] == P_FM1_ATK) ok = 1; open_family(FAM_EDIT); }
    bad += check("DIGITAL exposes four envelopes and independent operator levels", ok);
    go_title("OP1 ENV"); turn(EN_K1, 4);
    bad += check("operator envelope K1 edits only OP1 attack", TSEL->p[P_FM1_ATK] == 4 && !TSEL->p[P_FM2_ATK]);
    track_select(2); frame();
    bad += check("changing to a non-FM track leaves the stale operator page", page_visible(ui.page) && cur_page()->id[0] == P_E0);
#else
    set_engine_of(TSEL, ENGI_DIGITAL); open_family(FAM_EDIT); ok = TSEL->eng_req == ENGI_FM6;
    for (i = 0; i < 12u; i++) { ok &= !(cur_page()->id[0] >= P_FM1_ATK && cur_page()->id[0] <= P_FM4_LEVEL); open_family(FAM_EDIT); }
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK && PAGES[i].id[0] <= P_FM4_LEVEL)
            ok &= !page_visible(i);
    bad += check("DIGITAL retired: engine 1 asked for loads FM6; the OP ENV / OP LEVEL pages never show", ok);
#endif
    ui_power_on(); TSEL->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100}; go_page(GR_FEEL); turn(EN_K1, -35);
    bad += check("CHANCE (FEEL's KNOB 1) is per step and starts at backward-compatible 100 percent", step_chance(&TSEL->step[0]) == 65 && step_chance(&TSEL->step[1]) == 100);
    turn(EN_K1, -1000); ok = TSEL->step[0].time == ST_REST;
    turn(EN_K1, 1000);
    ok &= TSEL->step[0].time == ST_NOTE && step_chance(&TSEL->step[0]) == 100 && TSEL->step[0].cond == TC_COUNT - 1u;
    bad += check("(2.0) CHANCE runs OFF (a REST) .. 100 % .. the conditions (COND's), adjacent steps untouched",
                 ok && step_chance(&TSEL->step[1]) == 100 && !TSEL->step[1].cond);
    ui_power_on(); song.rec = 1; seq_start(); go_home();
    int16_t baseline = TSEL->p[P_E0]; turn(EN_K1, 1);
    bad += check("REC plus a sound knob records motion through the UI", motion_count(TSEL) == 1 && motion_enabled(TSEL));
    press(B_PLAY); events_block(32);
    bad += check("stopping returns the original sound after a recorded knob gesture", TSEL->p[P_E0] == baseline);
    go_page(GR_EVENTS); turn(EN_K1, -20); turn(EN_K2, -1);   /* ((U3, R5) the list's top row: PLAY OFF) */
    bad += check("#93 the page is AUTOMATION (U3: the list, its top row)", str_eq(cur_page()->title, "AUTOMATION") && ui.ev_top);
    bad += check("MOTION playback OFF preserves its stored events", !motion_enabled(TSEL) && motion_count(TSEL) == 1);
    turn(EN_K4, 1); press(B_OCTUP);
    bad += check("MOTION clear always asks confirmation", ui.confirm == CF_CLEAR_MOTION && motion_count(TSEL) == 1);
    {
        char qa[24], qb[24];
        confirm_text(qa, qb);
        bad += check("#93   the dialog asks CLEAR T1 AUTOMATION?", str_eq(qa, "CLEAR T1 AUTOMATION?"));
    }
    press(B_OCTDN); turn(EN_K4, 1); press(B_OCTUP); press(B_OCTUP);
    bad += check("confirmed MOTION clear removes events and SAVE hold restores them", motion_count(TSEL) == 0 &&
                 msg_is("AUTOMATION CLEARED"));
    hold(B_SAVE);
    bad += check("motion-clear UNDO restores the recorded events", motion_count(TSEL) == 1);
    motion_full = 1; frame();
    bad += check("#93 the events used up: AUTOMATION FULL", msg_is("AUTOMATION FULL") && !motion_full);
    {   /* #63: a card shows MOTION's icon while the track's motion changes its parameter */
        uint32_t id = ENGINES[TSEL->eng_req]->knob[0];
        ui_power_on(); go_home(); frame();
        ok = !card_motion(0) && !card_motion(1);
        song.rec = 1; seq_start(); go_home(); turn(EN_K1, 1); frame();
        ok &= card_motion(0) && !card_motion(1) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a recorded knob marks its HOME card (and no other)", ok && motion_count(TSEL) == 1);
        song.rec = 0; press(B_PLAY); events_block(32); frame();
        ok = card_motion(0);
        auto_top(); turn(EN_K2, -1); go_home(); frame();
        ok &= !card_motion(0) && motion_count(TSEL) == 1;
        bad += check("MOTION: PLAY OFF takes the icon off the card, the events stay", ok);
        auto_top(); turn(EN_K2, 1); go_home(); frame();
        ok = card_motion(0);
        track_select(1); frame();
        ok &= !card_motion(0);                           /* (another track: its own motion, none) */
        track_select(0); frame();
        ok &= card_motion(0);
        bad += check("MOTION: PLAY ON again shows it; only on the track that has the motion", ok);
        go_page(GR_TRK); frame();
        ok = !card_motion(0) && !card_motion(1) && !card_motion(2);
        motion_set_event(TSEL, 3, P_PAN, 20); frame();
        ok &= card_motion(1) && !card_motion(0) && !card_motion(2) && !card_motion(3);
        bad += check("MOTION: a mix parameter's event marks its MIXER card (PAN)", ok);
        auto_top(); turn(EN_K4, 1); press(B_OCTUP); press(B_OCTUP);
        ok = motion_count(TSEL) == 0;
        go_home(); frame();
        ok &= !card_motion(0) && !card_motion(1);
        go_page(GR_TRK); frame();
        ok &= !card_motion(1);
        bad += check("MOTION: CLEAR takes the icon off every card", ok && id < P_COUNT);
    }
    ui_power_on(); ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_PLAY]] = 1; led_pos[panel.btn[B_REC]] = 2;
    song.playing = song.rec = 1; ok = 1;
    for (i = 0; i < 120u; i++) { fm1_ms += 17; ui_leds(); ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u); }
    bad += check("PLAY's green and REC LEDs stay lit across time and audio block phase", ok);
    {   /* the power-on sweep (hal/fm1_input.h fm1_led_anim_on) has the LEDs: the UI writes none of them while it runs,
         * all of its own picture on the next frame after */
        uint8_t l0[FM1_NCOL], d0[FM1_NCOL];
        memset(fm1_led, 0x5A, sizeof fm1_led);
        memset(fm1_led_dim, 0xA5, sizeof fm1_led_dim);
        memcpy(l0, fm1_led, sizeof l0);
        memcpy(d0, fm1_led_dim, sizeof d0);
        host_anim = 1;
        ui_leds();
        ok = !memcmp(l0, fm1_led, sizeof l0) && !memcmp(d0, fm1_led_dim, sizeof d0);
        host_anim = 0;
        ui_leds();
        ok &= (fm1_led[0] & 6u) == 4u && (fm1_led[8] & 2u) && fm1_led[1] != 0x5A;
        bad += check("the power-on LED sweep: the UI leaves the LEDs alone while it runs, takes them after", ok);
    }
    led_pos_init();
    ok = 1;
    for (p = 0; p < NPALETTES; p++) for (b = 0; b < 1u; b++) {
        uint16_t graph[240 * H_GRAPH], columns[240 * (Y_SEP_END - Y_LABEL)];
        ui_power_on(); palette_set(p); settings.zoom = 1;
        open_family(FAM_LFO); frame();                 /* ((U2) BLOCKS: ENV's graph marks the knob turned; LFO's not) */
        memcpy(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        memcpy(columns, host_screen + Y_LABEL * 240, sizeof columns);
        ui.hot_col = 1; ui.hot_t = 40; ui.force = 1; ui_draw();
        ok &= !memcmp(graph, host_screen + Y_GRAPH * 240, sizeof graph);
        int changed = 0;
        for (uint32_t y = 0; y < Y_SEP_END - Y_LABEL; y++) for (uint32_t x = 0; x < 240; x++) {
            uint16_t old = columns[y * 240 + x], now = host_screen[(Y_LABEL + y) * 240 + x];
            if (x >= (uint32_t)CARD_X(1) && x < (uint32_t)(CARD_X(1) + CARD_W)) changed |= old != now;   /* (the tile) */
            else ok &= old == now;
        }
        ok &= changed;
    }
    bad += check("all palettes: active column is subtle, stable-size, no zoom over graph", ok);
    ui_power_on(); set_engine_of(TSEL, 4); open_family(FAM_EDIT);
    memset(&sample_wave, 0, sizeof sample_wave); last_note = 60;
    voice_t voices[NVOICE]; memcpy(voices, TSEL->v, sizeof voices);
    ok = 1;
    for (i = 0; i < 2000u && !sample_wave.ready; i++) {
        uint32_t old = sample_wave.pos; sample_wave_tick(TSEL);
        ok &= sample_wave.pos - old <= 512u;
    }
    ok &= sample_wave.ready && !memcmp(voices, TSEL->v, sizeof voices);
    int16_t lo[SAMPLE_WAVE_COLS] = {0}, hi[SAMPLE_WAVE_COLS] = {0};
    voice_t probe = {0}; const smp_zone_t *zone = sample_wave.zone;
    if (zone) for (i = 0; i < zone->n; i++) {
        int32_t v = sample_next(zone, &probe, 0); uint32_t col = i * SAMPLE_WAVE_COLS / zone->n;
        if (v < lo[col]) lo[col] = (int16_t)v;
        if (v > hi[col]) hi[col] = (int16_t)v;
    }
    ok &= !memcmp(lo, sample_wave.lo, sizeof lo) && !memcmp(hi, sample_wave.hi, sizeof hi);
    bad += check("sample waveform is bounded, matches audio IMA decode and leaves voices intact", ok);
    ui_power_on(); return bad;
}

static int test_panel(void)
{
    int bad = 0;
    panel = PANEL_DEFAULT;
    panel.btn[B_PLAY] = panel.btn[B_REC];
    panel_init();
    bad += check("duplicate button mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.enc[EN_K1] = panel.enc[EN_K2];
    panel_init();
    bad += check("duplicate knob mappings recover to the default panel", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.btn[B_PLAY] = 255;
    panel_init();
    bad += check("out-of-range panel data recovers without an invalid shift", !memcmp(&panel, &PANEL_DEFAULT, sizeof panel));
    panel.dir[EN_K1] = -1;
    panel_init();
    bad += check("a valid reversed knob mapping is retained", panel.dir[EN_K1] == -1);
    return bad;
}

/* ------------------------------------------------- the FX hold layer --- */
static void key_down(uint32_t k) { fm1_in.notes |= 1u << k; host_notes |= 1u << k; keyboard_block(); }
static void key_up(uint32_t k) { fm1_in.notes &= ~(1u << k); keyboard_block(); }
static uint32_t gates(void)                        /* voices of every track with their key down */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].gate != 0;
    return n;
}
static uint32_t white(uint32_t w) { return key_at(0, w); }

/* a layer's key LEDs as the old 250 ms blink read them: lit, and the breathing ones (#119) "on" at even quarters,
 * so (a ^ b) over ms 0 and 250 = the ones that can be pressed, (a & b) = lit */
static uint32_t leds_at(uint32_t ms)
{
    uint32_t br, m;
    fm1_ms = ms;
    m = layer_leds(&br);
    return m | (((ms / 250u) & 1u) ? 0u : br);
}

static int test_layer(void)
{
    int bad = 0, ok, flash;
    uint32_t i, mo, k;
    /* tap: on release, the page; held briefly: no map ever */
    ui_power_on();
    btn_down(B_FX); frame();
    ok = ui.home;
    frames(64); btn_up(B_FX); frame();
    bad += check("FX tap: nothing on press, the FX page when let go", ok && !ui.home && cur_page()->fam == FAM_FX);
    frames(320);                                       /* (not a double tap: #83) */
    press(B_FX);
    bad += check("  a second tap: the next page of the family ((phase 9) DRIVE)", str_eq(cur_page()->title, "DRIVE"));
    ui_power_on();
    btn_down(B_FX);
    for (flash = 0, i = 0; i < 22u; i++) { frame(); flash |= ui.layer; }      /* 0.35 s */
    btn_up(B_FX); frame();
    bad += check("  held 0.35 s (HOLD 0.4): still a tap, the map never shown", !flash && !ui.home);
    /* peek: past HOLD alone, the map; letting go does nothing */
    ui_power_on();
    btn_down(B_FX); frames(368);
    ok = !ui.layer;
    frames(64);
    ok &= ui.layer == LAYER_FX;
    btn_up(B_FX); frame();
    bad += check("FX held past 0.4 s: the map (not before); let go: no page, the map gone",
                 ok && ui.home && !ui.layer);
    /* the HOLD setting: 0.6 s */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_HOLD; turn(EN_K1, 1); turn(EN_K1, 1); ok = settings_hold == 3u; hold(B_HOME);
    btn_down(B_FX); frames(512); flash = ui.layer; btn_up(B_FX); frame();
    bad += check("menu HOLD 0.6 s (KNOB 1): FX held 0.5 s is a tap, no map", ok && !flash && !ui.home && !ui.menu);
    {
        sks_t p = {0};
        p.magic = SKS_MAGIC; p.panel = panel;
        settings_export(&p); settings_hold = HOLD_DEF;
        bad += check("  HOLD is saved with the settings and read back", settings_import(&p, sizeof p) && settings_hold == 3u);
    }
    /* the LEDS setting: KNOB 1 / OCT+ up to INV, left / OCT- down to OFF; saved in the retired zoom field */
    ui_power_on();
    hold(B_HOME); ui.menu_sel = MI_LEDS;
    ok = settings_leds == LEDS_DIM;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV;
    turn(EN_K1, 1); ok &= settings_leds == LEDS_INV;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_DIM_LO;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    turn(EN_K1, -1); ok &= settings_leds == LEDS_OFF;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM_LO && ui.menu == 1u;
    press(B_OCTUP); ok &= settings_leds == LEDS_DIM;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV;
    press(B_OCTUP); ok &= settings_leds == LEDS_INV && ui.menu == 1u;   /* (stops at the end) */
    for (k = 0; k < 4u; k++) press(B_OCTDN);
    ok &= settings_leds == LEDS_OFF && ui.menu == 1u;
    ok &= !strcmp(LEDS_NAME[LEDS_OFF], "OFF") && !strcmp(LEDS_NAME[LEDS_DIM_LO], "DIM LO") &&
          !strcmp(LEDS_NAME[LEDS_DIM], "DIM HI") && !strcmp(LEDS_NAME[LEDS_INV], "INV");
    {   /* a setting changed from elsewhere (the editor's MENU_SET: menu_put) while the MENU is shown: redrawn */
        static uint16_t before[240 * 240];
        frame();
        memcpy(before, host_screen, sizeof before);
        menu_put(MI_LEDS, 3);                          /* (OFF -> INV, the row shown) */
        frame();
        ok &= settings_leds == LEDS_INV && memcmp(before, host_screen, sizeof before) && menu_get(MI_LEDS) == 3u;
        menu_put(MI_LEDS, 0);
        frame();
        ok &= !memcmp(before, host_screen, sizeof before);
    }
    hold(B_HOME);
    bad += check("menu LEDS: DIM HI by default; KNOB 1 OFF < DIM LO < DIM HI < INV (stops at the ends), OCT+ / OCT- the same; "
                 "a change from the editor redraws the shown MENU", ok && !ui.menu);
    {
        sks_t p = {0};
        uint32_t m;
        p.magic = SKS_MAGIC; p.panel = panel;
        ok = 1;
        settings.zoom = 0;                              /* (DIM HI keeps an older record's 0 / 1 as saved) */
        for (m = 0; m < LEDS_COUNT; m++) {
            settings_leds = (uint8_t)m;
            settings_export(&p); settings_leds = LEDS_DIM;
            ok &= settings_import(&p, sizeof p) && settings_leds == m && leds_stored_ok(p.zoom) &&
                  p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
            settings_export(&p);                    /* (idempotent: saved again, the same field) */
            ok &= p.zoom == (m == LEDS_DIM ? 0u : LEDS_TAG | m);
        }
        settings_leds = LEDS_DIM;
        settings_export(&p);
        bad += check("  LEDS is saved with the settings and read back (all four); DIM HI saves the field as before (0)",
                     ok && p.zoom == 0u && settings_import(&p, sizeof p) && settings_leds == LEDS_DIM);
    }
    /* combo: a key with FX: at once, silent, no MIDI, no recording, no step */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    go_page(GR_ROLL); my_steps(TSEL); song.rec = 1; song.playing = 1;
    {
        step_t before[NSTEP];
        uint32_t cur = ui.cursor;
        memcpy(before, TSEL->step, sizeof before);
        btn_down(B_FX); key_down(white(2)); frame();
        ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && (kb_layer >> white(2)) & 1u &&
             (perf_held & PF_BIT(PF_R32));                    /* white key 3 (A3): REPEAT 1/32 */
        for (i = 0; i < 40u; i++) events_block(CTL);
        ok &= !memcmp(before, TSEL->step, sizeof before) && ui.cursor == cur;
        bad += check("FX + a key: the map at once; no voice, no MIDI, no recording, no step written", ok);
        key_up(white(2)); frame();
        bad += check("  the key let go: its effect off, no note-off sent", !perf_held && !kb_layer && mo_w == mo);
        btn_up(B_FX); frame();
        bad += check("  FX let go after a combo: no tap, the map gone", !ui.layer && cur_page()->graph == GR_ROLL);
    }
    song.playing = 0; song.rec = 0;
    /* FX let go first: the effect stays with its key, the map too; a key pressed now is a note */
    ui_power_on();
    btn_down(B_FX); key_down(white(4)); frame();          /* white key 5 (C4): LPF */
    btn_up(B_FX); frame();
    ok = (perf_held & PF_BIT(PF_LPF)) && ui.layer == LAYER_FX;
    key_down(white(0)); frame();
    ok &= gates() > 0 && !((kb_layer >> white(0)) & 1u);
    key_up(white(0)); key_up(white(4)); frame();
    bad += check("FX let go first: the key holds its effect and the map; a new key is a note", ok && !ui.layer && !perf_held);
    /* OCT UP (G4) and OCT DN (A4), the 9th and 10th white keys: held effects, silent; KNOB 4 the shimmer while
     * held, back to 0 with FX; ((Skiff phase 9) B4 GATE .. E5 VINYL) F5 on does nothing */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    btn_down(B_FX); key_down(white(8)); frame();
    ok = ui.layer == LAYER_FX && !gates() && mo_w == mo && perf_held == PF_BIT(PF_OUP);
    perf_begin(CTL);                                    /* (an audio block: OCT UP runs, KNOB 4 is SHIMR) */
    turn(EN_K4, 40);
    ok &= perf_k[3] == 40;
    key_up(white(8)); key_down(white(9)); frame();
    ok &= perf_held == PF_BIT(PF_ODN) && !gates() && mo_w == mo;
    key_up(white(9)); key_down(white(10)); frame();
    ok &= perf_held == PF_BIT(PF_GATE) && !gates() && mo_w == mo;
    key_up(white(10)); key_down(white(13)); frame();
    ok &= perf_held == PF_BIT(PF_VIN) && !gates() && mo_w == mo;
    key_up(white(13)); key_down(white(14)); frame();
    ok &= !perf_held && (kb_layer >> white(14)) & 1u && !gates();
    key_up(white(14)); btn_up(B_FX); frame();
    ok &= !perf_held && !ui.layer && perf_k[3] > 0 && perf_k[3] <= 40;   /* ((2.0, D 2A) on its way back) */
    a_bar();
    bad += check("FX + G4 / A4: OCT UP / DN held, silent, no MIDI; K4 turns; B4 GATE, E5 VINYL; F5 nothing; FX let go: K4 "
                 "back to 0 over a bar", ok && !perf_k[3] && mo_w == mo);
    /* REVERB > TYPE ((U3, N4) GLO's family, global): ROOM / SPRING on KNOB 1, kept by a project */
    ui_power_on();
    go_title("REVERB");
    ok = cur_page()->fam == FAM_GLO && cur_page()->id[0] == G_RTYPE && cur_page()->id[1] == G_RSIZE &&
         cur_page()->id[2] == G_RDAMP && song.g[G_RTYPE] == 0;
    turn(EN_K1, 1);
    ok &= song.g[G_RTYPE] == 1 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "SPRING");
    turn(EN_K1, 5);
    ok &= song.g[G_RTYPE] == 2 && str_eq(GP[G_RTYPE].names[song.g[G_RTYPE]], "HALL");   /* ((phase 9) the last) */
    turn(EN_K1, -1);
    ok &= song.g[G_RTYPE] == 1;
    song.playing = 0;
    project_save(2);
    turn(EN_K1, -1);
    ok &= song.g[G_RTYPE] == 0;
    project_load(2);
    bad += check("REVERB page: TYPE ROOM -> SPRING -> HALL on KNOB 1 (SIZE, DAMP beside it); a project keeps SPRING",
                 ok && song.g[G_RTYPE] == 1);
    go_title("CHORUS");
    bad += check("  CHORUS page (GLO's): RATE DEPTH (U3: were CRT CDP), (phase 9) MODE FEEDBACK",
                 cur_page()->fam == FAM_GLO && cur_page()->id[0] == G_CRATE && cur_page()->id[1] == G_CDEPTH &&
                 cur_page()->id[2] == G_CMODE && cur_page()->id[3] == G_CFDBK && str_eq(GP[G_CRATE].label, "RATE") &&
                 str_eq(GP[G_CDEPTH].label, "DEPTH") && str_eq(GP[G_CMODE].label, "MODE") &&
                 str_eq(GP[G_CFDBK].label, "FEEDBACK"));
    /* chording: a key held before FX stays a note, its note-off arrives */
    ui_power_on();
    mo = mo_w;
    key_down(white(4)); frame(); btn_down(B_FX); frame(); key_up(white(4)); frame(); btn_up(B_FX); frame();
    for (i = 0; i < 4u; i++) events_block(CTL);
    bad += check("a key held before FX stays a note: note-on, note-off, nothing left held", mo_w == mo + 2u && !gates() &&
                 !kb_layer);
    /* knob macros: not the page's, not recorded, back to 0 when FX is let go */
    ui_power_on();
    go_title("ENV"); song.rec = 1; song.playing = 1;
    {
        int16_t atk = TSEL->p[P_ATK];
        uint32_t ev = motion.count;
        btn_down(B_FX); frame();
        turn(EN_K1, -12); turn(EN_K2, 30);
        ok = perf_k[0] == -12 && perf_k[1] == 30 && TSEL->p[P_ATK] == atk && motion.count == ev && ui.layer == LAYER_FX;
        turn(EN_PRESET, 3);
        ok &= user_of(TSEL) == UP_SLOTS && TSEL->preset == trk[0].preset;
        btn_up(B_FX); frame();
        a_bar();
        bad += check("FX + KNOB 1 / 2: the macros, not ATK, not recorded; FX let go: back to 0 (over a bar), no tap",
                     ok && !perf_k[0] && !perf_k[1] && str_eq(cur_page()->title, "ENV"));
    }
    song.rec = 0; song.playing = 0;
    /* buttons in the layer: PLAY works, SAVE, HOME, a page button are swallowed */
    ui_power_on();
    btn_down(B_FX); frame();
    press(B_PLAY);
    ok = transport_req == 1u && ui.layer == LAYER_FX;
    transport_req = 0;
    press(B_SAVE); ok &= ui.home;
    hold(B_SAVE); ok &= !ui.undo_w && !msg_is("NOTHING TO UNDO") && ui.home;
    press(B_ENV); ok &= ui.home;
    hold(B_HOME); ok &= !ui.menu && ui.home && ui.lock == LAYER_FX;   /* ((#13) HOME: the layer locked, no menu) */
    btn_up(B_FX); frame();
    ok &= ui.layer == LAYER_FX;                        /* (FX let go: still open) */
    press(B_HOME); frame();
    bad += check("in the layer: PLAY plays; SAVE (tap and hold), ENV do nothing; (#13) HOME locks it, a HOME tap closes it",
                 ok && ui.home && !ui.menu && !ui.lock && !ui.layer);
    btn_down(B_FX); frame(); btn_down(B_ENV); frame(); btn_up(B_FX); frame(); btn_up(B_ENV); frame();
    bad += check("  a page button pressed with FX, let go after it: still nothing", ui.home);
    /* release-act: every page button acts when let go */
    {
        static const uint8_t PB[6] = {B_ENV, B_LFO, B_SCL, B_ARP, B_GLO, B_EDIT};
        static const uint8_t PF[6] = {FAM_ENV, FAM_LFO, FAM_SCL, FAM_ARP, FAM_GLO, FAM_EDIT};
        ok = 1;
        for (i = 0; i < 6u; i++) {
            ui_power_on();
            btn_down(PB[i]); frame();
            ok &= ui.home;
            btn_up(PB[i]); frame();
            ok &= !ui.home && (PB[i] == B_GLO ? cur_page()->graph == GR_TRK : cur_page()->fam == PF[i]);
            if (PB[i] == B_SCL || PB[i] == B_ARP || PB[i] == B_GLO || PB[i] == B_EDIT)
                continue;                             /* (a layer of their own: held long is a peek; ARP: PERFORM) */
            ui_power_on();
            hold(PB[i]);                              /* (no layer of their own: a long press is a tap too) */
            ok &= !ui.home && cur_page()->fam == PF[i];
        }
        bad += check("ENV LFO SCL ARP GLO EDIT act when let go, not on press (ENV LFO held long: the same)", ok);
    }
    ui_power_on();
    go_page(GR_ROLL); my_steps(TSEL); ui.cursor = 0;
    song.rec = 1u << song.sel;                       /* (U2: armed, step entry) */
    btn_down(B_EDIT); frame();
    ok = step_on(&TSEL->step[0]) && ui.cursor == 0;
    btn_up(B_EDIT); frame();
    bad += check("EDIT on STEP, armed, clears the step when let go (not on press)", ok && !step_on(&TSEL->step[0]) && ui.cursor == 1);
    song.rec = 0;
    ui_power_on(); hold(B_HOME);
    ok = ui.menu == 1; hold(B_HOME); ok &= !ui.menu;
    go_home(); hold(B_SEQ); ok &= ui.home;
    bad += check("HOME held: the MENU; (U2) SEQ held: its TOOLS (no page)", ok);
    /* no layer in the menu or a dialog: FX + a key is a note */
    ui_power_on(); hold(B_HOME);
    btn_down(B_FX); frame(); key_down(white(3)); frame();
    ok = !ui.layer && !perf_mask && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_FX); frame();
    bad += check("in the menu: no layer, FX + a key plays the key; FX let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_CLEAR_SEQ;
    btn_down(B_FX); frames(512);
    ok = !ui.layer;
    btn_up(B_FX); frame();
    bad += check("in a dialog: FX held shows no map and taps nothing", ok && ui.confirm == CF_CLEAR_SEQ);
    /* UBOOT: the countdown closes the map and stops every effect; OCT- + OCT+ are still seen */
    ui_power_on();
    btn_down(B_FX); key_down(white(5)); frame();
    btn_down(B_OCTDN); btn_down(B_OCTUP); frame();
    ok = ui.layer == LAYER_FX && (fm1_in.buttons & 3u << panel.btn[B_OCTDN]) != 0u;
    ui.uboot = 3; frame();                          /* (main.c: OCT- + OCT+ held 2 s) */
    ok &= !ui.layer && perf_kill && !perf_mask && perf_begin(CTL) >= 0;
    ok &= (fm1_in.buttons & ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]))) ==
          ((1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]));
    ui.uboot = 0;
    btn_up(B_OCTDN); btn_up(B_OCTUP); key_up(white(5)); btn_up(B_FX); frame();
    bad += check("UBOOT countdown over the layer: the map closes, effects off; OCT- + OCT+ untouched", ok && !perf_kill && !kb_layer);
    /* the LEDs of the map */
    ui_power_on();
    btn_down(B_FX); key_down(white(1)); frame();
    {
        uint32_t a, b2;
        song.g[G_BPM] = 120;
        a = leds_at(0); b2 = leds_at(250);
        k = white(1);
        ok = ((a >> k) & 1u) && ((b2 >> k) & 1u);                    /* held: lit */
        for (i = 0; i < 14u; i++)                                     /* the other effects (F3 .. E5): breathe */
            ok &= i == 1u || ((a ^ b2) >> white(i)) & 1u;
        for (i = 14; i < 16u; i++)                                    /* F5, G5: no effect, dark */
            ok &= !((a | b2) >> white(i) & 1u);
        ok &= ((a & b2) >> key_at(1, 0)) & 1u && !((a | b2) >> key_at(1, NTRK) & 1u);   /* ((2.0, D 1A) a black key as GLO's: its
                                                                                        * track sounding lit), a spare dark */
        song.g[G_BPM] = 72;
        a = leds_at(0); b2 = leds_at(250);
        ok &= !((a | b2) >> white(0) & 1u) && !((a | b2) >> white(3) & 1u);   /* 1/8, REVERSE: too long at 72 */
        ok &= ((a ^ b2) >> white(2)) & 1u;                            /* 1/32 still blinks */
        song.g[G_BPM] = 120;
    }
    key_up(white(1)); btn_up(B_FX); frame();
    bad += check("map LEDs: held lit, the 14 effects breathe, the black keys GLO's, F5 G5 and a too-long REPEAT dark", ok);
    usb.config = 0;
    return bad;
}

/* ------------------------------------------------------------ NAME --- */
static void nm_tap(uint32_t k)                    /* a key tapped in NAME: down a frame, up a frame (32 ms) */
{
    fm1_in.notes |= 1u << k; host_notes |= 1u << k; frame();
    fm1_in.notes &= ~(1u << k); frame();
}
static uint32_t nm_black_key(uint32_t f, uint32_t octave)   /* the black key of function f (NB_*: F# G# A# C# D#), octave 0 / 1 */
{
    return key_at(1, f + 5u * octave);
}
static int test_name(void)
{
    int bad = 0, ok;
    uint32_t i, mo;
    char b[16];
    ui_power_on();
    go_page(GR_USER);
    ui.uslot = 2;
    press(B_OCTUP);                               /* (SAVE picked on entry) */
    up_auto_name(b, TSEL->eng_req, 2);
    bad += check("NAME: USER SAVE into an empty slot opens NAME, prefilled with the automatic name",
                 name_on() && nm.kind == NK_USER_SAVE && nm.slot == 2u && str_eq(nm.s, b) && nm.cur == nm.len);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    bad += check("  DELETE removes the character before the cursor, down to empty", nm.len == 0u && nm.cur == 0u && !nm.s[0]);
    /* multi-tap: the same key cycles, another key commits, 0.8 s commits */
    nm_tap(white(0));
    ok = nm.len == 1u && nm.s[0] == 'A' && nm.key == 1u && nm.cur == 0u;
    nm_tap(white(0));
    ok &= nm.s[0] == 'B' && nm.len == 1u;
    nm_tap(white(0));
    bad += check("  a white key types its first letter; tapped again within 0.8 s: the next one, cycling (A B A)",
                 ok && nm.s[0] == 'A' && nm.len == 1u && nm.cur == 0u);
    nm_tap(white(1));
    bad += check("  another key keeps the letter and types its own (A, C)", nm.len == 2u && str_eq(nm.s, "AC") && nm.cur == 1u &&
                 nm.key == 2u);
    frames(800);
    bad += check("  0.8 s without a tap keeps it: the cursor moves on", nm.key == 0u && nm.cur == 2u && str_eq(nm.s, "AC"));
    nm_tap(white(1));
    frames(400);
    nm_tap(white(1));
    bad += check("  a second tap within 0.8 s (0.43 s later) still cycles (C -> D)", str_eq(nm.s, "ACD") && nm.key == 2u);
    frames(800);
    nm_tap(white(4)); nm_tap(white(4)); nm_tap(white(4));   /* IJK: K */
    nm_tap(nm_black_key(NB_SPACE, 1));                       /* SPACE (the upper octave's G#) keeps K first */
    bad += check("  a three-letter key (IJK) cycles to K; SPACE keeps it and adds a space", str_eq(nm.s, "ACDK ") && nm.cur == 5u &&
                 !nm.key);
    nm_tap(nm_black_key(NB_DEL, 1));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(nm_black_key(NB_LEFT, 0));
    nm_tap(white(2));                             /* E inserted before D */
    frames(800);
    bad += check("  DELETE, cursor left twice, a letter inserted at the cursor", str_eq(nm.s, "ACEDK") && nm.cur == 3u);
    nm_tap(nm_black_key(NB_RIGHT, 1));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(nm_black_key(NB_RIGHT, 0));
    bad += check("  cursor right stops at the end", nm.cur == 5u);
    /* 123 */
    nm_tap(nm_black_key(NB_MODE, 0));
    nm_tap(white(1)); nm_tap(white(9)); nm_tap(white(15));
    bad += check("  D# switches to 123: one tap, one character (2, 0, +), no cycling", nm.num && str_eq(nm.s, "ACEDK20+") &&
                 nm.cur == 8u && !nm.key);
    nm_tap(nm_black_key(NB_MODE, 1));
    nm_tap(white(15)); nm_tap(white(15)); nm_tap(white(15));
    frames(800);
    bad += check("  back to ABC: the last key (0-.) gives 0, then -, then .", !nm.num && str_eq(nm.s, "ACEDK20+.") && nm.cur == 9u);
    /* the knobs */
    turn(EN_K1, -2);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 moves the cursor, KNOB 2 changes the character there (+ -> SPACE, wrapping)",
                 nm.cur == 7u && str_eq(nm.s, "ACEDK20 ."));
    turn(EN_K2, 5);
    bad += check("  KNOB 2 on: SPACE + 5 = E", nm.s[7] == 'E');
    turn(EN_K1, 9);
    turn(EN_K2, 1);
    bad += check("  KNOB 1 to the end, KNOB 2 adds a character there (A)", str_eq(nm.s, "ACEDK20E.A") && nm.cur == 9u && nm.len == 10u);
    turn(EN_K2, -2);
    bad += check("  .. and turns it back past SPACE to the last symbol (+)", nm.s[9] == '+');
    /* full */
    nm_tap(nm_black_key(NB_RIGHT, 0));
    nm_tap(white(7)); frames(800); nm_tap(white(7)); frames(800);
    ui.msg_t = 0;
    nm_tap(white(7));
    bad += check("  12 characters at most: NAME FULL, nothing typed", nm.len == 12u && msg_is("NAME FULL") && str_eq(nm.s, "ACEDK20E.+PP"));
    /* held DELETE repeats */
    fm1_in.notes |= 1u << nm_black_key(NB_DEL, 0); host_notes |= 1u << nm_black_key(NB_DEL, 0);
    frames(16 * 40);
    fm1_in.notes &= ~(1u << nm_black_key(NB_DEL, 0)); frame();
    bad += check("  DELETE held 0.64 s: one, then it repeats after 0.45 s, every 0.09 s (3 in all)", nm.len == 9u);
    /* the keys never sound, record or send MIDI */
    usb.config = 1; mo = mo_w; song.rec = 1;
    {
        step_t before[NSTEP];
        memcpy(before, TSEL->step, sizeof before);
        key_down(white(3)); key_down(nm_black_key(NB_SPACE, 0)); frame();
        for (i = 0; i < 8u; i++) events_block(CTL);
        ok = !gates() && mo_w == mo && !memcmp(before, TSEL->step, sizeof before) && song.grid == 2u;
        key_up(white(3)); key_up(nm_black_key(NB_SPACE, 0)); frame();
        bad += check("  keys in NAME: no voice, no MIDI, nothing recorded (seq.c: song.grid 2)", ok && mo_w == mo);
    }
    song.rec = 0; usb.config = 0;
    {   /* LEDs: all 27 keys do something; the cycling key breathes (U6: never a blink) */
        uint32_t a, c, br = 0, br2 = 0;
        nm_tap(nm_black_key(NB_DEL, 0)); nm_tap(nm_black_key(NB_DEL, 0));
        nm_tap(white(5));
        a = name_leds(&br); fm1_ms += 250u; c = name_leds(&br2);
        nm.t = fm1_ms;                            /* (the letter still cycling) */
        bad += check("  LEDs: every key lit, the cycling key (LM) breathes, steady", a == c && br == br2 &&
                     br == (1u << white(5)) && (a | br) == 0x7FFFFFFu);
    }
    /* cancel */
    {
        uint32_t page = ui.page;
        press(B_OCTDN);
        bad += check("  OCT- cancels: nothing written, back on the USER page", !name_on() && !up_used(2) && ui.page == page && !ui.home);
    }
    /* typing a name and saving it */
    ui.act = 4;
    press(B_OCTUP);
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(1)); nm_tap(white(0)); nm_tap(white(0)); nm_tap(nm_black_key(NB_RIGHT, 0));    /* C B */
    nm_tap(nm_black_key(NB_SPACE, 0)); nm_tap(nm_black_key(NB_MODE, 0)); nm_tap(white(1));        /* " 2" */
    nm_tap(nm_black_key(NB_SPACE, 0));                                                           /* a trailing space */
    song.playing = 1;
    press(B_OCTUP);
    bad += check("  OCT+ while playing: STOP TO SAVE, NAME stays", name_on() && msg_is("STOP TO SAVE") && !up_used(2));
    stop_transport();
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+ stopped: saved as typed, the trailing space dropped (CB 2)", !name_on() && up_used(2) && str_eq(b, "CB 2") &&
                 msg_is("SAVED UNTIL POWER OFF"));
    /* SAVE held does nothing in NAME; the FX layer neither */
    ui.act = 4; press(B_OCTUP); press(B_OCTUP);   /* (U03 used: OVERWRITE?, YES) */
    bad += check("  overwrite: the dialog, then NAME", name_on() && nm.kind == NK_USER_SAVE);
    ui.msg_t = 0;
    hold(B_SAVE);
    btn_down(B_FX); frames(600); ok = !ui.layer; btn_up(B_FX); frame();
    bad += check("  SAVE held, FX held: nothing in NAME (no undo, no map, no page)", name_on() && ok && cur_page()->graph == GR_USER &&
                 !ui.msg_t);
    press(B_OCTDN);
    /* rename: (U2) KNOB 3's NAME on USER, was EDIT */
    ui.uslot = 5;
    user_rename();
    bad += check("  NAME on an empty slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT") && cur_page()->graph == GR_USER);
    ui.uslot = 2;
    up_load(2);                                   /* (the sound now comes from U03) */
    trk[0].p[P_E0] = 33;                          /* an edit: a rename must not store it */
    user_rename();
    bad += check("  NAME on a used slot: NAME (rename) with its name", name_on() && nm.kind == NK_USER_RENAME && str_eq(nm.s, "CB 2"));
    nm_tap(nm_black_key(NB_DEL, 0));
    nm_tap(white(10)); nm_tap(white(10));          /* VW: W */
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  OCT+: renamed (CB W), the sound stored there unchanged", !name_on() && str_eq(b, "CB W") &&
                 up_value(up_rec(2), p_sid(P_E0)) != 33 && msg_is("RENAMED UNTIL POWER OFF"));
    user_rename();
    for (i = 0; i < 12u; i++) nm_tap(nm_black_key(NB_DEL, 0));
    press(B_OCTUP);
    {
        char au[16];
        up_name(2, b);
        up_auto_name(au, up_rec(2)->engine, 2);
        bad += check("  a name deleted to nothing: the automatic one", str_eq(b, au));
    }
    /* the sound's own name is the prefill */
    trk[0].user = 3;
    ui.uslot = 7; ui.act = 4;
    press(B_OCTUP);
    up_name(2, b);
    bad += check("  SAVE of a sound that came from U03: prefilled with U03's name", name_on() && str_eq(nm.s, b));
    press(B_OCTDN);
    /* projects */
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 2;
    turn(EN_K4, 1);
    press(B_OCTUP);
    for (i = 0; i < 4u; i++) { nm_tap(white(7)); nm_tap(white(6)); frames(800); }   /* P N P N .. */
    press(B_OCTUP);
    {
        char pn[16];
        int named = project_name(1, pn);
        bad += check("PROJECT SAVE: NAME, typed, saved: the slot has the name, the project too", named && str_eq(pn, "PNPNPNPN") &&
                     str_eq(proj_name, "PNPNPNPN"));
    }
    proj_name[0] = 0;
    project_load(1);
    bad += check("  loading it brings its name back (the next save's prefill)", str_eq(proj_name, "PNPNPNPN"));
    trk[0].p[P_LEVEL] = 7;
    proj_rename();
    for (i = 0; i < 4u; i++) nm_tap(nm_black_key(NB_DEL, 1));
    press(B_OCTUP);
    {
        char pn[16];
        project_name(1, pn);
        bad += check("  NAME (KNOB 2) renames a project (PNPN); its music unchanged", str_eq(pn, "PNPN") && stored_param(1, 0, P_LEVEL) != 7 &&
                     msg_is("RENAMED UNTIL POWER OFF"));
    }
    song.g[G_SLOT] = 4;
    proj_rename();
    bad += check("  NAME on an empty project slot: EMPTY SLOT", !name_on() && msg_is("EMPTY SLOT"));
    song.g[G_SLOT] = 2;
    proj_rename();
    hold(B_HOME);
    bad += check("  (U2) HOME held in NAME: nothing (no menu, NAME and its typing stay)", !ui.menu && name_on());
    press(B_OCTDN);
    return bad;
}

/* the EDIT cycle (no ENGINE page: engines are the EDIT layer's) and its memory */
static int engine_cycle(const char *const *want, uint32_t n)   /* EDIT tapped from HOME, then n - 1 times more */
{
    uint32_t i;
    int ok = 1;
    frames(320);
    go_home(); frame();
    for (i = 0; i < n; i++) {
        press(B_EDIT);
        frames(320);                                  /* (taps further apart than a double tap: #83 locks) */
        ok &= str_eq(cur_page()->title, want[i]);
    }
    return ok;
}
/* (FM6 LAB) EDIT > OPERATOR and ALL OPS: every byte of an FM6 track's patch from the knobs (ui_fm6.c) */
static int test_fm6_lab(void)
{
    int bad = 0, ok;
    uint32_t tr, i, pos;
    uint8_t was[FP_SIZE + 1u], gen;
    ui_power_on();
    ui_prefs |= PREF_BPM_LOCK;                          /* (SELECT: the pages, the sheets' rows) */
    tr = trk_index(TSEL);
    set_engine_of(TSEL, ENGI_FM6);                      /* TINE EP: SLOT F1 */
    ok = TSEL->p[P_E7] == 0;
    go_title("OPERATOR"); frame();
    fm_ui[0] = 0; fm_ui[1] = 0;
    turn(EN_PRESET, 2);                                 /* OP3 (no sound load) */
    ok &= fm_ui[0] == 2u && TSEL->preset == 0u && TSEL->eng_req == ENGI_FM6 && str_eq(page_head(), "OPERATOR 3");
    memcpy(was, fm6_patch[tr], sizeof was);
    gen = fm6_pgen[tr];
    turn(EN_K1, -3);                                    /* OUTPUT */
    for (i = 0; i < FP_SIZE; i++)
        ok &= i == (5u - 2u) * FP_OP + FP_OL ? fm6_patch[tr][i] < was[i] : fm6_patch[tr][i] == was[i];
    ok &= fm6_pgen[tr] != gen && TSEL->p[P_E7] == FM6_OWN && ui.hot_col == 0u;
    bad += check("OPERATOR: PRESETS picks OP3 (no load), K1 turns its OUTPUT byte only, the patch is OWN now", ok);
    turn(EN_K1, 3);
    bad += check("OUTPUT back where it was: the patch is TINE EP's again, SLOT F1", TSEL->p[P_E7] == 0 &&
                 !memcmp(fm6_patch[tr], was, FP_SIZE));
    {   /* RATIO: x0.5 .. x31, then 1 10 100 1K HZ, and back */
        uint8_t *op = fm6_patch[tr] + (5u - 2u) * FP_OP;
        turn(EN_K2, -40);
        ok = !op[FP_MODE] && op[FP_FC] == 0;
        for (pos = 1; pos < 36u; pos++) {
            turn(EN_K2, 1);
            ok &= op[FP_MODE] == (pos >= 32u) && op[FP_FC] == (pos >= 32u ? pos - 32u : pos);
        }
        turn(EN_K2, 1);
        ok &= op[FP_MODE] && op[FP_FC] == 3;
        turn(EN_K2, -40);
        ok &= !op[FP_MODE] && op[FP_FC] == 0;
        bad += check("RATIO walks x0.5, x1 .. x31, 1 10 100 1K HZ (MODE fixed) and back, the ends where they are", ok);
    }
    turn(EN_SELECT, 1);                                 /* ENV 1: RATE 1 LEVEL 1 RATE 2 LEVEL 2 */
    memcpy(was, fm6_patch[tr], sizeof was);
    turn(EN_K3, -2);
    ok = fm_ui[1] == 1u && str_eq(cur_page()->title, "OPERATOR") && fm6_patch[tr][(5u - 2u) * FP_OP + FP_R1 + 1] < was[(5u - 2u) * FP_OP + FP_R1 + 1];
    turn(EN_SELECT, 3);                                 /* MORE: BREAK KEY RATE VELOCITY LFO AMP */
    turn(EN_K1, 1);
    ok &= fm_ui[1] == 4u && fm6_patch[tr][(5u - 2u) * FP_OP + FP_BP] == was[(5u - 2u) * FP_OP + FP_BP] + 1u;
    turn(EN_SELECT, 1);                                 /* past MORE: ALL OPS, its top */
    ok &= str_eq(cur_page()->title, "ALL OPS") && fm_ui[2] == 0u && str_eq(page_head(), "ALL OPS");
    turn(EN_SELECT, -1);                                /* back: OPERATOR's foot */
    ok &= str_eq(cur_page()->title, "OPERATOR") && fm_ui[1] == 4u;
    bad += check("SELECT: OPERATOR's windows (ENV 1 RATE 2, MORE BREAK), past its foot ALL OPS's top, back its foot", ok);
    go_title("ALL OPS"); fm_ui[2] = 0; frame();
    TSEL->p[P_E0] = 5; TSEL->p[P_E1] = 2;               /* EDIT 1's ALGO and FEEDBACK over the patch's 5 and 3 */
    turn(EN_K1, 1);
    turn(EN_K2, 1);
    ok = fm6_patch[tr][FP_ALG] == 5u && TSEL->p[P_E0] == 0 && fm6_patch[tr][FP_FB] == 6u && TSEL->p[P_E1] == 0 &&
         fm6_alg_of(TSEL) == 5u;
    bad += check("ALL OPS: ALGO and FEEDBACK step from what plays (ALG 5 by EDIT 1, FB 3 + 2) into the patch, EDIT 1's to "
                 "neutral", ok);
    turn(EN_SELECT, 3);                                 /* LFO: WAVE RATE FADE KEY SYNC */
    memcpy(was, fm6_patch[tr], sizeof was);
    turn(EN_K1, 1); turn(EN_K4, -1);
    ok = fm_ui[2] == 3u && fm6_patch[tr][FP_LFW] == was[FP_LFW] + 1u && fm6_patch[tr][FP_LKS] == 0u;
    bad += check("ALL OPS LFO: WAVE on, KEY SYNC off", ok);
    ui.fam_last[FAM_EDIT] = ui.page;
    set_engine_of(TSEL, 0); frame();
    bad += check("ALL OPS of a track no longer FM6 falls back to EDIT 1 (the sheets are FM6's)", str_eq(cur_page()->title, "EDIT 1"));
    {   /* the panel's brightness (Steinberg's idea): INIT's sine cold everywhere; TINE EP's OP3 (fed by OP4, x14) hot
         * at its attack, cooler at the key up; six carriers (ALG 32) with FEEDBACK 7: heat from the feedback alone */
        static fm_sim_t s;
        uint8_t v[FP_SIZE + 1u];
        uint32_t x, h0, h1, any = 0;
        set_engine_of(TSEL, ENGI_FM6);
        fm_sim(&s, TSEL);
        h0 = fm_heat(&s, 2, fm_feeders(s.alg, 2), 1);
        h1 = fm_heat(&s, 2, fm_feeders(s.alg, 2), FM_HW);
        ok = h0 >= 6u && h1 < h0 && s.held > 0 && s.n > s.held && s.op[2].n >= 4u && s.op[2].x[s.op[2].n - 1u] == FM_XW - 1;
        fm6_unpack(FM6_INIT, v);
        fm6_set_patch(tr, v);
        fm_sim(&s, TSEL);
        for (x = 0; x < FM_XW; x++)
            any |= fm_heat(&s, 0, fm_feeders(s.alg, 0), (int32_t)x);
        ok &= !any && s.live == 1u && (s.car & 1u);
        v[FP_ALG] = 31;                                 /* ALG 32: OP6 a carrier with the feedback */
        v[FP_FB] = 7;
        v[FP_OL] = 99;                                  /* (OP6: the patch's first operator) */
        fm6_set_patch(tr, v);
        fm_sim(&s, TSEL);
        ok &= fm_heat(&s, 5, fm_feeders(s.alg, 5), 2) > 0u && !fm_heat(&s, 0, fm_feeders(s.alg, 0), 2);
        bad += check("brightness: TINE EP's OP3 hot at the attack, cooler at the key up; INIT cold; ALG 32 FB 7 warm", ok);
    }
    {   /* INIT on FM6: the bare sine to build from (OWN, the macros at 0, the footer INIT VOICE); SAVE held: back */
        uint8_t ini[FP_SIZE + 1u], tine[FP_SIZE + 1u];
        char nm[16];
        set_engine_of(TSEL, ENGI_FM6); go_home(); frame();
        TSEL->p[P_E2] = 20;                             /* (MOD LVL turned since the load) */
        memcpy(tine, fm6_patch[tr], sizeof tine);
        fm6_unpack(FM6_INIT, ini);
        confirm_open(CF_INIT_SOUND, song.sel); frame();
        press(B_OCTUP);
        sound_name(TSEL, nm);
        ok = !memcmp(fm6_patch[tr], ini, FP_SIZE) && TSEL->p[P_E7] == FM6_OWN && !TSEL->p[P_E0] && !TSEL->p[P_E2] &&
             str_eq(nm, "INIT VOICE") && msg_is("SOUND INITIALIZED");
        hold(B_SAVE);
        sound_name(TSEL, nm);
        ok &= !memcmp(fm6_patch[tr], tine, FP_SIZE) && TSEL->p[P_E2] == 20 && str_eq(nm, "TINE EP");
        bad += check("INIT on FM6: the bare sine (OP1, ALG 1), SLOT OWN, macros 0, named INIT VOICE; SAVE held: TINE EP back", ok);
    }
    ui_prefs = 0;                                       /* BPM LOCK OFF: on the sheet SELECT is still its rows */
    set_engine_of(TSEL, ENGI_FM6);
    go_page(GR_FMOP);
    fm_ui[1] = 0;
    {
        int16_t b0 = song.g[G_BPM];
        turn(EN_SELECT, 1);
        bad += check("BPM LOCK OFF: OPERATOR's rows still on SELECT", fm_ui[1] == 1u && song.g[G_BPM] == b0);
    }
    return bad;
}

static int test_edit_cycle(void)
{
    static const char *const CYC_A[] = {"EDIT 1", "EDIT 2", "VOICE", "VOICE 2", "EDIT 1"};
    static const char *const CYC_F[] = {"EDIT 1", "EDIT 2", "OPERATOR", "ALL OPS", "VOICE", "VOICE 2", "EDIT 1"};
    static const char *const CYC_D[] = {"EDIT 1", "EDIT 2", "OP1 ENV", "OP2 ENV", "OP3 ENV", "OP4 ENV",
                                        "OP LEVEL", "VOICE", "VOICE 2", "EDIT 1"};
    int bad = 0, ok;
    uint32_t i;
    int16_t *vp;
    ui_power_on();
    btn_down(B_EDIT); frame();
    ok = ui.home;
    btn_up(B_EDIT); frame();
    bad += check("EDIT opens EDIT 1, when let go (not on press)", ok && str_eq(cur_page()->title, "EDIT 1"));
    for (i = 0; i < NPAGES; i++)
        ok &= !str_eq(PAGES[i].title, "ENGINE");
    bad += check("no ENGINE page (engines are the EDIT layer's)", ok);
    set_engine_of(TSEL, 0);
    bad += check("EDIT cycle (ANALOG): EDIT 1 EDIT 2 VOICE VOICE 2 EDIT 1", engine_cycle(CYC_A, NELEM(CYC_A)));
#if FELUCCA_FM4
    set_engine_of(TSEL, 1);
    bad += check("EDIT cycle (DIGITAL): EDIT 1 EDIT 2 OP1..OP4 ENV OP LEVEL VOICE VOICE 2 EDIT 1",
                 engine_cycle(CYC_D, NELEM(CYC_D)));
#else
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("EDIT cycle (engine 1 asked for: FM6, DIGITAL retired): EDIT 1 EDIT 2 OPERATOR ALL OPS VOICE VOICE 2 EDIT 1",
                 TSEL->eng_req == ENGI_FM6 && engine_cycle(CYC_F, NELEM(CYC_F)));
    (void)CYC_D;
#endif
    {   /* #97: a DRUM track's lane levels on EDIT > LANES ((Skiff U3, R6) one page: KNOB 1 the lane, KNOB 2 its level;
         * N5: VOICE and VOICE 2 change nothing on the hits, not there) */
        static const char *const CYC_K[] = {"EDIT 1", "EDIT 2", "LANES", "LANE MIX", "DRUM COMP", "EDIT 1"};
        set_engine_of(TSEL, ENGI_DRUM);
        bad += check("EDIT cycle (DRUM): EDIT 1 EDIT 2 LANES LANE MIX DRUM COMP, EDIT 1", engine_cycle(CYC_K, NELEM(CYC_K)));
        go_title("LANES"); frame();
        ok = TSEL->p[P_LN5] == 127;
        turn(EN_K1, 5 - (int32_t)ui.lane);
        turn(EN_K2, -3);
        ok &= ui.lane == 5u && TSEL->p[P_LN5] == 124 && TSEL->p[P_LN4] == 127 && TSEL->p[P_LN6] == 127;
        bad += check("LANES: KNOB 1 TOM, KNOB 2 its level (100 % by default), the other lanes stay", ok);
        go_title("LANE MIX"); frame();               /* ((2.0, #36, the user's pick 1A) the same lane; KNOB 3 its PAN */
        ok = ui.lane == 5u && str_eq(page_desc(cur_page(), 1, &vp)->label, TP[P_LN5].label) && vp == &TSEL->p[P_LN5];
        turn(EN_K1, -3);
        turn(EN_K3, -10);
        turn(EN_K2, -2);
        ok &= ui.lane == 2u && TSEL->p[P_LPAN0 + 2] == -10 && TSEL->p[P_LN2] == 125 && !TSEL->p[P_LPAN0 + 5] &&
              TSEL->p[P_LN5] == 124 && lock_id(1) == P_LN0 + 2u && lock_id(2) == P_LPAN0 + 2u && lock_id(3) == 0xFFu;
        key_down(13); frame(); key_up(13); frame();  /* (13: F#, HAT CL) */
        ok &= ui.lane == DV_HATC;
        turn(EN_K1, -1);
        key_down(13); frame(); key_up(13); frame();  /* (the same key again: its lane again) */
        ok &= ui.lane == DV_HATC;
        bad += check("(2.0, #36) LANE MIX: LANES' lane; KNOB 1 CLAP, 3 its PAN, 2 its LEVEL, a held step's lock them; a "
                     "key struck picks its lane, the same key again too", ok);
        apply_preset_to(TSEL, 0);
        bad += check("  a factory preset load: every lane at C again, as their levels at 100 %", !TSEL->p[P_LPAN0 + 2]);
        go_title("LANES"); frame();
        apply_preset_to(TSEL, 0);
        bad += check("a factory preset load: every lane 100 % again", TSEL->p[P_LN5] == 127);
        ui.fam_last[FAM_EDIT] = ui.page;
        set_engine_of(TSEL, 0); frame();
        bad += check("LANES of a track no longer DRUM falls back to EDIT 1, and says so (U3: NO LANES ON ..)",
                     str_eq(cur_page()->title, "EDIT 1") && !memcmp(ui.msg, "NO LANES ON ", 12));
    }
    {   /* (Skiff phase 7, 8) DRUM-X: its lane levels, and SOUND, the sheet of a sound of the project's kit (ui_dx.c) */
        static const char *const CYC_X[] = {"EDIT 1", "EDIT 2", "SOUND", "LANE MIX", "DRUM COMP", "EDIT 1"};   /* ((U3) LANES: the
                                                                                                * sheet's LEVL) */
        static dx_lane_t keep[DV_NLANE];
        uint32_t pr;
        int16_t lv;
        memcpy(keep, dx_kit, sizeof keep);
        set_engine_of(TSEL, ENGI_DRUMX);
        go_title("EDIT 1"); ui.fam_last[FAM_EDIT] = ui.page;   /* (EDIT reopens its last page: from the first) */
        bad += check("EDIT cycle (DRUM-X): EDIT 1 EDIT 2 SOUND LANE MIX DRUM COMP, EDIT 1", engine_cycle(CYC_X, NELEM(CYC_X)));
        ui_prefs |= PREF_BPM_LOCK;                    /* (SELECT: the pages, the sheet's rows) */
        dx_ui_lane = DV_KICK; dx_ui_win = 0;
        go_title("SOUND"); frame();
        pr = TSEL->preset;
        turn(EN_PRESET, 1);                           /* the sound: SD (no sound load) */
        turn(EN_K2, 2);                               /* PTCH B */
        turn(EN_K3, -1);                              /* PMOD A */
        ok = dx_ui_lane == DV_SNARE && TSEL->eng_req == ENGI_DRUMX && TSEL->preset == pr &&
             dx_kit[DV_SNARE].b[DXP_PITCH] > keep[DV_SNARE].b[DXP_PITCH] && dx_kit[DV_SNARE].a[DXP_PITCH] == keep[DV_SNARE].a[DXP_PITCH] &&
             dx_kit[DV_SNARE].a[DXP_PMOD] < keep[DV_SNARE].a[DXP_PMOD] && dx_kit[DV_SNARE].b[DXP_PMOD] == keep[DV_SNARE].b[DXP_PMOD] &&
             ui.hot_col == 2u && str_eq(page_head(), "SOUND SD");
        bad += check("SOUND: PRESETS picks SD (no load); K2 PTCH B, K3 PMOD A, the others stay; the header SOUND SD", ok);
        turn(EN_SELECT, 1);                           /* DCAY A B, NOIS A B */
        turn(EN_K1, 1);
        ok = dx_ui_win == 1u && str_eq(cur_page()->title, "SOUND") && dx_kit[DV_SNARE].a[DXP_DECAY] > keep[DV_SNARE].a[DXP_DECAY];
        turn(EN_SELECT, 1);                           /* COLR A B, LEVL, DRV */
        lv = TSEL->p[P_LN0 + DV_SNARE];
        turn(EN_K3, -3);
        turn(EN_K4, 1);
        ok &= dx_ui_win == 2u && TSEL->p[P_LN0 + DV_SNARE] < lv && TSEL->p[P_LN0 + DV_KICK] == 127 &&
              (dx_kit[DV_SNARE].mode & DX_DRIVE);
        turn(EN_SELECT, 1);                           /* WAVE FILT SNAP PSHP */
        turn(EN_K1, 1); turn(EN_K2, 1); turn(EN_K3, 1); turn(EN_K4, 1);
        ok &= dx_ui_win == 3u && (dx_kit[DV_SNARE].mode & 3u) == DXW_METAL && ((dx_kit[DV_SNARE].mode >> 2) & 3u) == DXF_HP &&
              (dx_kit[DV_SNARE].mode & 0x10u) && ((dx_kit[DV_SNARE].mode >> 5) & 3u) == DXM_LONG;
        bad += check("SELECT down the sheet: DCAY A; LEVL (the lane's, its own only), DRV ON; WAVE FILT SNAP PSHP the next", ok);
        turn(EN_SELECT, 1);                           /* ((2.0, #36) past the sheet's foot: LANE MIX, the family's last */
        ok = str_eq(cur_page()->title, "LANE MIX");   /* ((U3) no VOICE on the hits), its lane the sheet's */
        ok &= page_lane() == DV_SNARE && lock_id(2) == P_LPAN0 + DV_SNARE;
        turn(EN_SELECT, 1);
        ok &= str_eq(cur_page()->title, "DRUM COMP") && page_lane()==DV_SNARE;
        turn(EN_SELECT, 1);ok &= str_eq(cur_page()->title,"DRUM COMP");
        turn(EN_SELECT, -1);ok &= str_eq(cur_page()->title,"LANE MIX");
        turn(EN_SELECT, -1);
        ok &= str_eq(cur_page()->title, "SOUND") && dx_ui_win == 3u;
        turn(EN_SELECT, -3);
        ok &= str_eq(cur_page()->title, "SOUND") && dx_ui_win == 0u;
        turn(EN_SELECT, -1);
        ok &= str_eq(cur_page()->title, "EDIT 2");
        turn(EN_SELECT, 1);
        ok &= str_eq(cur_page()->title, "SOUND") && dx_ui_win == 0u;
        bad += check("SELECT past the sheet's foot: (2.0) LANE MIX then DRUM COMP on the same sound; clamp at last page; back: the "
                     "sheet's foot; up past its top: EDIT 2; on: its top", ok);
        key_down(13); frame(); key_up(13); frame();   /* (the first C is key 7: 13 is F#, HAT CL) */
        bad += check("a key struck on the track picks the sound edited (HAT CL)", dx_ui_lane == DV_HATC && str_eq(page_head(), "SOUND CH"));
        ui.fam_last[FAM_EDIT] = ui.page;
        set_engine_of(TSEL, ENGI_DRUM); frame();
        bad += check("SOUND of a track now DRUM falls back to EDIT 1 (SOUND is DRUM-X's)", str_eq(cur_page()->title, "EDIT 1"));
        ui_prefs &= ~PREF_BPM_LOCK;
        TSEL->p[P_LN0 + DV_SNARE] = 127;
        {   /* INIT on DRUM-X: the factory kit too (the dialog says so), one undo; on DRUM the kit stays */
            static dx_lane_t edited[DV_NLANE];
            char a[24], b[40];
            set_engine_of(TSEL, ENGI_DRUMX); go_home(); frame();
            TSEL->p[P_E2] = 90;                       /* (a knob turned since the load: INIT takes its own copy) */
            dx_kit[DV_KICK].a[DXP_DECAY] = 7;
            memcpy(edited, dx_kit, sizeof edited);
            confirm_open(CF_INIT_SOUND, song.sel); frame();
            confirm_text(a, b);
            ok = str_eq(a, "INITIALIZE SOUND?") && str_eq(b, "and the DRUM-X kit");
            press(B_OCTUP);
            ok &= !memcmp(dx_kit, DX_KIT_DEF, sizeof dx_kit) && msg_is("SOUND INITIALIZED") && TSEL->p[P_E2] != 90;
            hold(B_SAVE);
            ok &= !memcmp(dx_kit, edited, sizeof edited) && TSEL->eng_req == ENGI_DRUMX && TSEL->p[P_E2] == 90;
            press(B_OCTUP);                           /* (in the undo window: redo) */
            ok &= !memcmp(dx_kit, DX_KIT_DEF, sizeof dx_kit);
            bad += check("INIT on DRUM-X: the factory kit too (INITIALIZE SOUND? and the DRUM-X kit); SAVE held: the "
                         "kit back; OCT+: again", ok);
            set_engine_of(TSEL, ENGI_DRUM); go_home(); frame();
            memcpy(dx_kit, edited, sizeof edited);
            confirm_open(CF_INIT_SOUND, song.sel); frame();
            confirm_text(a, b);
            ok = !b[0];
            press(B_OCTUP);
            bad += check("INIT on DRUM: the DRUM-X kit stays, the dialog plain", ok && !memcmp(dx_kit, edited, sizeof edited));
        }
        {   /* (Skiff phase 9) a DRUM-X preset loads its kit as the project's (eng_drumx.c DRUMX_PKIT): a switch to DRUM-X
             * (its first preset) and HADRON KIT keep the kit; one undo copy holds the kit across a browse; a kit edited
             * between two loads gets a copy of its own (ui.c track_sig hashes it) */
            static dx_lane_t edited[DV_NLANE];
            uint32_t k;
            set_engine_of(TSEL, ENGI_DRUM); go_home(); frame();
            undo_clear();                             /* (no copy of the INIT tests before: this browse's own) */
            dx_kit[DV_KICK].a[DXP_DECAY] = 9;
            memcpy(edited, dx_kit, sizeof edited);
            set_engine_of(TSEL, ENGI_DRUMX); frame();
            ok = !memcmp(dx_kit, edited, sizeof edited) && TSEL->preset == 0u;
            apply_preset(1u); frame();
            bad += check("phase 9: a switch to DRUM-X (DRUM KIT) and HADRON KIT keep the project's kit",
                         ok && !memcmp(dx_kit, edited, sizeof edited));
            ok = DRUMX_PKIT[2] == 1u && !memcmp(DX_KITS[0], DX_KIT_DEF, sizeof dx_kit);
            for (k = 2; k < ENGINES[ENGI_DRUMX]->npresets; k++) {
                apply_preset(k); frame();
                ok &= DRUMX_PKIT[k] && !memcmp(dx_kit, DX_KITS[DRUMX_PKIT[k] - 1u], sizeof dx_kit) && dx_kit_ok(dx_kit);
            }
            bad += check("phase 9: every kit preset loads its own kit, FACTORY KIT the factory's", ok);
            hold(B_SAVE);
            bad += check("phase 9: SAVE held after the browse: the kit (and DRUM) as before it, one undo copy",
                         !memcmp(dx_kit, edited, sizeof edited) && TSEL->eng_req == ENGI_DRUM);
            set_engine_of(TSEL, ENGI_DRUMX); frame();
            apply_preset(3u); frame();                /* DEEP KIT */
            dx_kit[DV_SNARE].a[DXP_PITCH] = 77;       /* (an edit on EDIT > SOUND) */
            memcpy(edited, dx_kit, sizeof edited);
            apply_preset(4u); frame();                /* CLICK KIT */
            hold(B_SAVE);
            bad += check("phase 9: a kit edited after a load: the next load takes a copy of its own (SAVE held: the edit)",
                         !memcmp(dx_kit, edited, sizeof edited) && TSEL->eng_req == ENGI_DRUMX && TSEL->preset == 3u);
            set_engine_of(TSEL, ENGI_DRUM); go_home(); frame();
        }
        memcpy(dx_kit, keep, sizeof keep);
    }
    go_title("OP2 ENV"); ui.fam_last[FAM_EDIT] = ui.page;
    set_engine_of(TSEL, 0); frame();
    bad += check("an OP page of a track no longer DIGITAL falls back to EDIT 1", str_eq(cur_page()->title, "EDIT 1"));
    go_title("EDIT 2"); ui.fam_last[FAM_EDIT] = ui.page;
    go_home(); press(B_EDIT);
    bad += check("EDIT remembers its last page (as every family)", str_eq(cur_page()->title, "EDIT 2"));
    return bad;
}

/* a missing sample (an empty user slot; a set this build lacks): it plays a sine (eng_sample.c smp_sine) and the UI
 * says NO SAMPLE, led by the no-file icon (ui.c MSG_NO_SAMPLE), once per track and source (ui_input.c
 * sample_notice): when it is chosen, not on its notes; again for another source; after a project load (behind
 * LOADED); the icon and the words fit the header */
static int test_sample_alert(void)
{
    const char *nf = MSG_NO_SAMPLE;
    int bad = 0, ok, seen;
    uint32_t i;
    char b[48];
    ui_power_on();
    frames(100);
    bad += check("missing sample: the power-on sounds have their samples (no alert)", !snd_missing(&trk[0], &i) &&
                 !snd_missing(&trk[1], &i) && !snd_missing(&trk[2], &i) && !snd_missing(&trk[3], &i) &&
                 !str_eq(ui.msg, nf));
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);      /* SET USR2: empty */
    frame();
    ok = msg_is(nf);
    frames(1500);
    trk_note_on(TSEL, 60, 100);
    frames(100);
    trk_note_off(TSEL, 60);
    frames(100);
    ok &= !ui.msg_t;
    bad += check("SAMPLE on an empty USR2: NO SAMPLE once (not again on a note)", ok);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);      /* USR3: said for it */
    frame();
    ok = msg_is(nf);
    TSEL->p[P_E0] = 0;                              /* PIANO: nothing; then USR3 again: said again */
    frames(1500);
    ok &= !ui.msg_t;
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 2u);
    frame();
    ok &= msg_is(nf);
    bad += check("SAMPLE: another empty slot is said; back to it after a sample: said again", ok);
    frames(1500);
    set_engine_of(TSEL, 8u);                         /* GRAIN, SRC USR2 */
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frame();
    bad += check("GRAIN on an empty USR2: said", msg_is(nf));
    frames(1500);
#if FELUCCA_SLICE
    set_engine_of(TSEL, 13u);                        /* SLICE, SRC USR3 */
    TSEL->p[P_E0] = 3;
    frame();
    ok = msg_is(nf);
    frames(1500);
    TSEL->p[P_E0] = 4;                               /* PIANO: there (the CC0 build) */
    frame();
    ok &= !ui.msg_t && !snd_missing(TSEL, &i) == (SLC_PIANO.len != 0u);
    bad += check("SLICE on an empty USR3: said; PIANO: not (it has its sample)", ok);
    frames(1500);
#endif
    /* a project with an empty slot: said after LOADED */
    set_engine_of(TSEL, ENGI_SAMPLE);
    TSEL->p[P_E0] = (int16_t)(SMP_NSETS + 1u);
    frames(1500);
    project_save(2);
    frames(1500);
    project_load(2);
    frame();
    ok = msg_is("LOADED") && str_eq(ui.msg2, nf);
    for (seen = 0, i = 0; i < 200u && !seen; i++) {
        frame();
        seen = msg_is(nf);
    }
    bad += check("a project load with an empty slot: LOADED, then NO SAMPLE", ok && seen);
    bad += check("missing sample: the icon and NO SAMPLE fit the header",
                 nf[0] == MSG_NOFILE[0] && !text_fit(b, sizeof b, nf + 1, &AF_S, 236 - 106 - 16 - KH_GAP));
    ui_power_on();
    return bad;
}

#if FELUCCA_SLICE
/* SLICES (EDIT family, a SLICE track: ui_slice.c; the MAN slices of eng_slice.c, ported from hugelton/Felucca#27 by
 * andreahaku): in the EDIT cycle after EDIT 2 on SLICE only; BREAK shows its slices, edits need a user slot; the first
 * edit takes the slices shown (8 equal) as MAN and sets DIV MAN; KNOB 1 the marker (then END), KNOB 2 moves it,
 * KNOB 3 / 4 pick SPLIT / JOIN and OCT+ does it (it stays picked), OCT- drops the pick, then goes HOME; a key picks
 * its slice and the keys of the selected slice are lit; the slot is marked for the store; another engine: EDIT 1 */
static void host_slot_make(uint32_t k)                /* USR k + 1: 2 s at 22.05 kHz, a noise burst every 0.25 s */
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)((uint8_t *)host_slots + k * SMP_USER_SIZE);
    uint8_t *d = (uint8_t *)h + SMP_USER_DATA;
    uint32_t n = 44100u, i, r = 12345u;
    memset(h, 0, SMP_USER_SIZE);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 1;
    memcpy(h->name, "TEST", 4);
    h->data_len = n / 2u;
    for (i = 0; i < n / 2u; i++) {                   /* (IMA codes: a burst of random ones, then +-1/8 steps) */
        r = r * 1103515245u + 12345u;
        d[i] = i % 2756u < 400u ? (uint8_t)(r >> 16) : 0x80u;
    }
    h->zone[0].n = n;
    h->zone[0].le = n - 1u;
    h->zone[0].rate = 32768u;                        /* 22050 / 44100, Q16 */
    h->zone[0].root16 = 60 * 16;
    h->zone[0].hi = 127;
}
static int test_slices(void)
{
    static const char *const CYC_S[] = {"EDIT 1", "EDIT 2", "SLICES", "VOICE", "VOICE 2", "EDIT 1"};
    int bad = 0, ok;
    uint32_t n, j, p0, k, note;
    ui_power_on();
    set_engine_of(TSEL, 13u);                        /* SLICE CHOP: BREAK, 16 */
    bad += check("SLICES: SLICE's EDIT cycle EDIT 1 EDIT 2 SLICES VOICE VOICE 2 EDIT 1", engine_cycle(CYC_S, NELEM(CYC_S)));
    go_page(GR_SLICES); frame();
    n = slice_count();
    p0 = TSEL->p[P_E1];
    turn(EN_K1, 2); turn(EN_K2, 3);
    ok = n == 16u && slice_sel() == 2u && TSEL->p[P_E1] == p0 && msg_is("PICK A USR SAMPLE FIRST") && !slice_act_ready(2);
    bad += check("SLICES on BREAK: its 16 slices shown, KNOB 1 picks; edits need USR1-3 (DIV unchanged)", ok);
    TSEL->p[P_E0] = 2;                               /* SRC USR2, empty: no slices (a sine plays): NO SAMPLE */
    frame();
    ok = slice_count() == 0u && (msg_is(MSG_NO_SAMPLE) || str_eq(ui.msg2, MSG_NO_SAMPLE));
    turn(EN_K2, 1);
    ok &= TSEL->p[P_E1] == p0 && msg_is("USR2 EMPTY");
    TSEL->p[P_E0] = 0;
    bad += check("SLICES on an empty USR2: NO SAMPLE, no slices; edits: \"USR2 EMPTY\"", ok);
    TSEL->p[P_E0] = 4;                               /* SRC PIANO: its 16 slices shown, no edits */
    frame();
    turn(EN_K2, 1);
    ok = slice_count() == 16u && TSEL->p[P_E1] == p0 && msg_is("PICK A USR SAMPLE FIRST");
    TSEL->p[P_E0] = 0;
    frame();
    bad += check("SLICES on PIANO: its 16 slices shown; edits need USR1-3 (DIV unchanged)", ok);

    host_slot_make(0);
    smp_user_scan(0);
    TSEL->p[P_E0] = 1;                               /* SRC USR1, DIV 8 */
    TSEL->p[P_E1] = 1;
    frame();
    n = slice_count();
    p0 = slice_mark(2);
    turn(EN_K2, 5);
    ok = n == 8u && TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("SLICES NOW MANUAL") && slice_count() == 8u && slice_sel() == 2u &&
         slice_mark(2) > p0 && slc_man_of(slc_get(1)) && (slc_man_save & 1u);
    bad += check("SLICES on USR1: the first KNOB 2 turn takes the 8 slices as MAN (DIV MAN), moves slice 3's start", ok);
    turn(EN_K3, 1);
    ok = ui.act == 3u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u && ui.act == 3u;
    press(B_OCTUP);
    ok &= slice_count() == 10u && slice_sel() == 4u;
    bad += check("SLICES: KNOB 3 picks SPLIT, OCT+ splits the slice (again: it stays picked)", ok);
    turn(EN_K4, 1);
    ok = ui.act == 4u && act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && slice_sel() == 3u;
    turn(EN_K1, -20);
    ok &= slice_sel() == 0u && !act_ready();
    press(B_OCTUP);
    ok &= slice_count() == 9u && msg_is("NOTHING BEFORE TO JOIN");
    bad += check("SLICES: KNOB 4 picks JOIN, OCT+ joins to the slice before; not the first slice", ok);
    turn(EN_K1, 20);
    p0 = slice_mark(slice_count());
    turn(EN_K2, -4);
    ok = slice_sel() == slice_count() && slice_mark(slice_count()) < p0 && !act_ready();
    bad += check("SLICES: after the last slice the END marker: KNOB 2 trims the tail", ok);
    {   /* DIV set back to a grid: the first touch shows the slot's MAN slices (DIV MAN), it does not replace them */
        slc_man_t keep = *slc_man_of(slc_get(1));
        TSEL->p[P_E1] = 2;                           /* DIV 16 */
        frame();
        turn(EN_K1, -1); turn(EN_K2, 1);
        ok = TSEL->p[P_E1] == SLC_DIV_MAN && msg_is("SLICES NOW MANUAL") && slc_man_of(slc_get(1))->n == keep.n &&
             !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        turn(EN_K1, -20); turn(EN_K2, 1);            /* then KNOB 2 edits that table */
        ok &= slc_man_of(slc_get(1))->n == keep.n && memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: DIV 16 over a slot's MAN slices: KNOB 2 goes back to them (DIV MAN), keeps them", ok);
    }

    note = 5u;                                       /* (the 6th key: SLICE maps every key) */
    key_down(note); frame(); key_up(note); frame();
    j = slc_note_slice(TSEL->p, kb_map(TSEL, note), slice_count());
    ok = slice_sel() == j && ((slice_leds() >> note) & 1u);
    for (k = 0; k < 27u; k++)
        ok &= ((slice_leds() >> k) & 1u) == (slc_note_slice(TSEL->p, kb_map(TSEL, k), slice_count()) == j);
    bad += check("SLICES: a key picks the slice it plays; the keys of the selected slice are lit", ok);
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok = ui.act == 0u && !ui.home;
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= ui.home;
    bad += check("SLICES: OCT- drops the pick, then goes HOME", ok);
    go_page(GR_SLICES); frame();
    set_engine_of(TSEL, 0); frame();
    bad += check("SLICES: on another engine the page is not there (EDIT 1)", str_eq(cur_page()->title, "EDIT 1"));
    {   /* the engine changed (the editor's SET between two ui_input passes) while the page is still the current one:
         * its knobs and actions do nothing to the track or the slot */
        track_t before;
        slc_man_t keep;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = SLC_DIV_MAN;
        go_page(GR_SLICES); frame();
        keep = *slc_man_of(slc_get(1));
        set_engine(3u);                              /* LOFI, as ed_service's SET G_ENGSEL */
        TSEL->p[P_E0] = 1;
        before = *TSEL;
        slc_man_save = 0;
        host_enc[panel.enc[EN_K2]] += 2 * panel.dir[EN_K2];
        host_ticks += 16000u; fm1_ms += 16u;
        ui_input();                                  /* (no ui_draw yet) */
        ok = cur_page()->graph == GR_SLICES && !slice_page_ok() && !act_cols();
        ui.act = 3u;
        act_do();
        ui.act = 0;
        ok &= !memcmp(&before, TSEL, sizeof before) && !slc_man_save && slc_man_of(slc_get(1))->n == keep.n &&
              !memcmp(slc_man_of(slc_get(1))->pos, keep.pos, keep.n * sizeof keep.pos[0]);
        bad += check("SLICES: left by an engine change not yet drawn: KNOB 2 and OCT+ change nothing", ok);
        frame();
    }
    {   /* another sample of the same length uploaded into the slot: the waveform is decoded again */
        int8_t lo0[SP_COLS], hi0[SP_COLS];
        uint8_t *d = (uint8_t *)host_slots + SMP_USER_DATA;
        smp_user_hdr_t *h = (smp_user_hdr_t *)host_slots;
        set_engine_of(TSEL, 13u);
        TSEL->p[P_E0] = 1;
        TSEL->p[P_E1] = 2;
        go_page(GR_SLICES); ui.force = 1; frame();
        memcpy(lo0, sp.lo, sizeof lo0);
        memcpy(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < h->data_len; k++)
            d[k] = 0x80u;                            /* silence, the same length */
        h->crc ^= 1u;
        smp_user_scan(0);                            /* (the upload's END) */
        ui.force = 1; frame();
        ok = memcmp(lo0, sp.lo, sizeof lo0) || memcmp(hi0, sp.hi, sizeof hi0);
        for (k = 0; k < SP_COLS; k++)
            ok &= sp.lo[k] >= -1 && sp.hi[k] <= 1;   /* (near silence) */
        bad += check("SLICES: a re-upload of the same length redraws the waveform", ok);
        set_engine_of(TSEL, 0); frame();
    }
    memset(host_slots, 0, sizeof host_slots);        /* (the other tests: empty slots) */
    smp_user_scan(0);
    slc_man_save = 0;
    return bad;
}

/* (Skiff, #21) EDIT > SAMPLES (ui.c smx_zone, params.c page_desc): a SAMPLE track's on a USR set with samples only, after
 * EDIT 2; KNOB 1 the sample (its number, kept within the slot's), a key played picks its sample; KNOB 2..4 its LEVEL,
 * REVERB, DELAY (eng_sample.c usr_mix), marked to keep (slice_store.c waits while the page is on); never a lock; another
 * set: the page goes */
static int test_samples_page(void)
{
    static const char *const CYC_B[] = {"EDIT 1", "EDIT 2", "VOICE", "VOICE 2", "EDIT 1"};
    static const char *const CYC_U[] = {"EDIT 1", "EDIT 2", "SAMPLES", "VOICE", "VOICE 2", "EDIT 1"};
    smp_user_hdr_t *h = (smp_user_hdr_t *)host_slots;
    int bad = 0, ok;
    uint32_t z;
    ui_power_on();
    set_engine_of(TSEL, ENGI_SAMPLE);                /* SET PIANO */
    bad += check("SAMPLES: not on a built-in set (EDIT 1 EDIT 2 VOICE VOICE 2)", engine_cycle(CYC_B, NELEM(CYC_B)));
    host_slot_make(0);                               /* USR1: three sounds on C4 C#4 D4 */
    for (h->nz = 3, z = 0; z < 3u; z++) {
        h->zone[z] = h->zone[0];
        h->zone[z].off = z * (h->data_len / 3u);
        h->zone[z].n = h->data_len / 2u;
        h->zone[z].le = h->zone[z].n - 1u;
        h->zone[z].root16 = (int16_t)((60 + z) * 16);
        h->zone[z].lo = h->zone[z].hi = (uint8_t)(60u + z);
    }
    smp_user_scan(0);
    TSEL->p[P_E0] = (int16_t)SMP_NSETS;              /* SET USR1 */
    bad += check("SAMPLES: on USR1 with samples, after EDIT 2", engine_cycle(CYC_U, NELEM(CYC_U)));
    go_page(GR_SMIX);
    last_note = 61;
    frame();
    ok = smx_sel == 2 && smx_page_on() && !lock_page_ok(ui.page);
    turn(EN_K1, 5); frame();
    ok &= smx_sel == 3;                              /* (kept within the slot's three) */
    turn(EN_K1, -1); frame();
    ok &= smx_sel == 2;
    bad += check("SAMPLES: a key played picks its sample; KNOB 1 the one beside, within the slot's; no lock", ok);
    smx_save = 0;
    turn(EN_K2, -3); turn(EN_K3, 4); turn(EN_K4, 2); frame();
    ok = usr_mix[0][SMX_LVL][1] < 127 && usr_mix[0][SMX_REV][1] > 0 && usr_mix[0][SMX_DLY][1] > 0 && smx_save &&
         usr_mix[0][SMX_LVL][0] == 127 && !usr_mix[0][SMX_REV][2];
    bad += check("SAMPLES: KNOB 2..4 the picked sample's LEVEL REVERB DELAY (the others as they were), to keep", ok);
    last_note = 60; frame();
    turn(EN_K2, -1); frame();
    ok = smx_sel == 1 && usr_mix[0][SMX_LVL][0] < 127;
    TSEL->p[P_E0] = 0; frame();                      /* SET PIANO: no samples of its own */
    ok &= cur_page()->graph != GR_SMIX && !smx_page_on();
    bad += check("SAMPLES: another key, another sample; a built-in set: the page goes", ok);
    memset(host_slots, 0, sizeof host_slots);        /* (the other tests: empty slots) */
    smp_user_scan(0);
    smx_save = 0;
    set_engine_of(TSEL, 0); frame();
    return bad;
}
#endif

/* ------------------------------------------------ the GLO SCL EDIT layers --- */
static uint32_t black(uint32_t b) { return key_at(1, b); }
static void lay_combo(uint32_t btn, uint32_t k) { btn_down(btn); key_down(k); frame(); }
static void oct_back(void) { btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame(); }

static int test_quick_layers(void)
{
    static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[3] = {LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const char *const HINT[3] = {"HOLD [GLO]: SHORTCUTS", "HOLD [SEL]: SHORTCUTS", "HOLD [EDIT]: SHORTCUTS"};
    int bad = 0, ok, flash, hint;
    uint32_t i, k, a, b2, mo;
    track_t before;
    /* tap / peek / combo, the hint once */
    ok = 1; hint = 1;
    for (i = 0; i < 3u; i++) {
        ui_power_on();
        btn_down(LB[i]);
        for (flash = 0, k = 0; k < 20u; k++) { frame(); flash |= ui.layer; }      /* 0.32 s */
        btn_up(LB[i]); frame();
        ok &= !flash && !ui.home && (LB[i] == B_GLO ? cur_page()->graph == GR_TRK :
                                     LB[i] == B_SCL ? cur_page()->fam == FAM_SCL : str_eq(cur_page()->title, "EDIT 1"));
        hint &= msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); frames(368);
        ok &= !ui.layer;
        frames(64);
        ok &= ui.layer == LL[i] && ((layer_seen >> LL[i]) & 1u);
        btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
        ui.msg_t = 0;
        press(LB[i]);
        hint &= !msg_is(HINT[i]);
        go_home(); frame();
        btn_down(LB[i]); key_down(white(9)); frame();
        ok &= ui.layer == LL[i] && !gates();
        key_up(white(9)); btn_up(LB[i]); frame();
        ok &= ui.home && !ui.layer;
    }
    bad += check("GLO SCL EDIT: a tap opens the page, held past HOLD the map (no page), a key: the map at once", ok);
    bad += check("  after a tap \"HOLD [BTN] QUICK\" until the layer has been opened once", hint);
    {
        sks_t p = {0};
        p.magic = SKS_MAGIC; p.panel = panel;
        layer_seen = 0x1Cu;
        settings_export(&p); layer_seen = 0;
        bad += check("  the seen bits are kept with the settings (a spare favorites byte)",
                     settings_import(&p, sizeof p) && layer_seen == 0x1Cu);
    }

    /* GLO: mutes latch (lit = sounding), SOLO while held, UNMUTE ALL, TAP, levels, OCT- */
    ui_power_on();
    usb.config = 1; mo = mo_w;
    lay_combo(B_GLO, black(1));
    ok = !trk[1].p[P_MUTE] && trk_cut(1) && ui.layer == LAYER_GLO && !gates() && mo_w == mo;   /* ((2.0, D 1A) down: cut */
    {
        uint32_t t0 = fm1_ms;                                         /* (leds_at sets the clock: the press's put back) */
        a = leds_at(0); b2 = leds_at(250);
        fm1_ms = t0;
    }
    ok &= ((a & b2) >> black(0)) & 1u && !(((a | b2) >> black(1)) & 1u);           /* T1 sounding lit, T2 muted dark */
    ok &= ((a ^ b2) >> white(0)) & 1u && ((a ^ b2) >> white(8)) & 1u && ((a ^ b2) >> white(7)) & 1u &&
          ((a ^ b2) >> white(4)) & 1u;          /* (Skiff: (#11) F4 is FILL, C4 SOLO T5, G4 TAP: they breathe) */
    key_up(black(1)); btn_up(B_GLO); frame();
    bad += check("GLO + black key 2: cut while down, a tap: T2 MUTE latched (SET), silent, no MIDI; LEDs: sounding lit, "
                 "muted dark", ok && trk[1].p[P_MUTE] == 1 && !trk_cut(1) && !ui.layer);
    lay_combo(B_GLO, black(1)); key_up(black(1)); frame();
    key_down(white(6)); key_up(white(6)); frame();
    lay_combo(B_GLO, black(2)); key_up(black(2)); frame();
    ok = trk[1].p[P_MUTE] == 0 && trk[2].p[P_MUTE] == 1;
    key_down(white(6)); frame(); key_up(white(6)); frame();
    btn_up(B_GLO); frame();
    bad += check("  again: unmuted; (#11) E4: UNMUTE ALL", ok && !trk[2].p[P_MUTE] && !trk[1].p[P_MUTE]);
    lay_combo(B_GLO, white(2));
    perf_begin(CTL);
    ok = perf_solo == 4u && ((perf_act >> PF_M1) & 15u) == 0xBu;   /* T3 solo: T1 T2 T4 muted */
    btn_up(B_GLO); frame();
    perf_begin(CTL);
    ok &= perf_solo == 4u && ui.layer == LAYER_GLO;                 /* GLO let go first: the solo stays with its key */
    key_up(white(2)); frame();
    perf_begin(CTL);
    bad += check("GLO + A3 held: SOLO T3 (the others muted), lasts while the key is held, not P_MUTE",
                 ok && !perf_solo && !((perf_act >> PF_M1) & 15u) && !trk[0].p[P_MUTE] && !ui.layer);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(8)); frame(); key_up(white(8)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    song.g[G_CLOCK] = 1;
    lay_combo(B_GLO, white(8)); key_up(white(8)); frame();
    ok &= msg_is("TAP: CLOCK IS EXTERNAL") && song.g[G_BPM] == 120;
    btn_up(B_GLO); frame(); song.g[G_CLOCK] = 0;
    bad += check("GLO + G4 x3 at 0.5 s: TAP 120 BPM; with CLK EXT: dimmed, it says why", ok);
    {
        int16_t l2 = trk[2].p[P_LEVEL], atk = TSEL->p[P_ATK];
        go_title("ENV");
        btn_down(B_GLO); frame();
        turn(EN_K3, -10);
        ok = trk[2].p[P_LEVEL] == l2 - 10 && TSEL->p[P_ATK] == atk && ui.layer == LAYER_GLO;
        key_down(black(0)); frame(); key_up(black(0)); frame();
        ok &= trk[0].p[P_MUTE] == 1 && song.octave == 0;
        oct_back();
        ok &= trk[2].p[P_LEVEL] == l2 && !trk[0].p[P_MUTE] && song.octave == 0 && ui.layer == LAYER_GLO;
        btn_up(B_GLO); frame();
        bad += check("GLO KNOB 3: T3 LEVEL from any page; OCT-: mutes and levels as it opened, no octave", ok &&
                     str_eq(cur_page()->title, "ENV"));
    }
    song.playing = 1; transport_req = 0;
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok = transport_req == 3u;
    events_block(CTL);
    ok &= song.playing && transport_req == 0u;
    btn_up(B_GLO); frame();
    stop_transport();
    btn_down(B_GLO); frame(); press(B_PLAY);
    ok &= transport_req == 1u;
    btn_up(B_GLO); frame(); transport_req = 0;
    bad += check("GLO + PLAY: RESTART playing (from the top, not stopped); stopped: PLAY", ok);
    {   /* (Skiff phase 5) SONG on: each track on its list's first entry (bank.c song_top), from the top */
        track_defaults_steps(&trk[0]);
        trk_pid[0] = 0;
        trk[0].step[0] = (step_t){{50}, 1, ST_NOTE, 0, 90};
        bank_put(0, 0, 0);                              /* T1: A (50), B (60), the track on B */
        trk[0].step[0].note[0] = 60;
        trk_pid[0] = 0;
        bank_put(0, 1, 0);
        lists.song = 1;
        song.playing = 1; transport_req = 0;
        btn_down(B_GLO); frame(); press(B_PLAY);
        ok = bank_cur(0) == 0u && trk[0].step[0].note[0] == 50 && transport_req == 1u;
        btn_up(B_GLO); frame(); transport_req = 0;
        lists.song = 0;
        stop_transport();
        bad += check("  SONG on: every track from its list's first entry (T1 on B: A), then PLAY", ok);
        (void)ent_go(0, 1);                             /* ((2.0, #47) SONG ONCE: PLAY alone from the top; playing, */
        lists.song = SONG_ONCE;                         /* a stop) */
        press(B_PLAY);
        ok = bank_cur(0) == 0u && trk[0].step[0].note[0] == 50 && transport_req == 1u;
        song.playing = 1; transport_req = 0;
        press(B_PLAY);
        ok &= transport_req == 2u && bank_cur(0) == 0u;
        lists.song = 0;
        transport_req = 0;
        stop_transport();
        bad += check("  SONG ONCE: PLAY from every track's first entry (T1 on B: A); playing, PLAY stops", ok);
    }
    usb.config = 0;

    /* SCL: a key is ROOT, KNOB 2 the scale, the LEDs, OCT- */
    ui_power_on();
    TSEL->p[P_ROOT] = 0; TSEL->p[P_SCALE] = 1;                     /* C MAJ */
    lay_combo(B_SCL, black(4));                                    /* D#4 */
    ok = TSEL->p[P_ROOT] == 3 && ui.layer == LAYER_SCL && !gates();
    key_up(black(4)); frame();
    key_down(white(1)); key_up(white(1)); frame();                  /* G3 */
    ok &= TSEL->p[P_ROOT] == 7;
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 2;
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a & b2) >> white(1)) & 1u && ((a & b2) >> white(8)) & 1u;   /* G3 G4: the root, lit */
    ok &= ((a ^ b2) >> white(2)) & 1u && ((a ^ b2) >> black(4)) & 1u;   /* A, A# (G minor): blink */
    ok &= !(((a | b2) >> black(0)) & 1u);                               /* F#: not in G minor, dark */
    btn_up(B_SCL); frame();
    bad += check("SCL + D#4, then G3: ROOT D#, G (latched); KNOB 2: SCL; LEDs: root lit, the scale breathes", ok &&
                 TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2 && ui.home);
    btn_down(B_SCL); frame(); key_down(white(4)); key_up(white(4)); frame(); turn(EN_K2, 3);
    ok = TSEL->p[P_ROOT] == 0 && TSEL->p[P_SCALE] == 5;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("  OCT- in SCL: ROOT and SCL as the layer opened", ok && TSEL->p[P_ROOT] == 7 && TSEL->p[P_SCALE] == 2);

    /* EDIT: the engines from F3 (NENGINES of them), sound loads with UNDO, INIT with the dialog */
    ui_power_on();
    set_engine_of(TSEL, 0); go_home(); frame();
    my_steps(TSEL); TSEL->p[P_SLCR] = SL_STUT; song.playing = 1;
    before = *TSEL;
    sync_reload = 0;
    lay_combo(B_EDIT, white(eng_rank(2)));             /* (the keys: the engines one can pick, engines.c eng_vis) */
    ok = TSEL->eng_req == 2u && TSEL->preset == 0u && ui.layer == LAYER_EDIT && sync_reload && !gates();
    key_up(white(eng_rank(2))); frame();
    key_down(white(1)); key_up(white(1)); frame();
    ok &= TSEL->eng_req == ENGI_FM6;                    /* (G3: FM6, second in ENGINE_ORDER) */
    key_down(white(NENG_SHOWN - 2u)); key_up(white(NENG_SHOWN - 2u)); frame();
    ok &= TSEL->eng_req == ENGI_DRUM;                   /* (the key before the last: DRUM) */
    key_down(white(NENG_SHOWN - 1u)); key_up(white(NENG_SHOWN - 1u)); frame();
    ok &= TSEL->eng_req == ENGI_DRUMX;                  /* (the last key: (Skiff) DRUM-X) */
    ok &= !memcmp(TSEL->step, before.step, sizeof before.step) && TSEL->p[P_SLEN] == before.p[P_SLEN] &&
          TSEL->p[P_SLCR] == SL_STUT;
    btn_up(B_EDIT); frame();
    bad += check("EDIT + white key n: the n-th engine shown (FM6 2nd, DRUM, DRUM-X last), while playing; steps, LEN, SLICER stay", ok);
    hold(B_SAVE);
    bad += check("  SAVE held: UNDO back to before the layer's loads, the steps untouched",
                 TSEL->eng_req == 0u && TSEL->preset == before.preset && !memcmp(TSEL->step, before.step, sizeof before.step));
    a = 0;
    ok = 1;
    btn_down(B_EDIT); frames(480);
    for (i = 0; i < LY_INIT; i++) {
        key_down(white(i)); key_up(white(i)); frame();
        a += i < NENG_SHOWN ? TSEL->eng_req == eng_vis(i) : 0u;
        ok &= eng_ok(TSEL->eng_req);
    }
    btn_up(B_EDIT); frame();
    bad += check("  every engine one can pick has its white key from F3 (NENG_SHOWN, not a fixed count; never DIGITAL)",
                 ok && a == NENG_SHOWN);
    set_engine_of(TSEL, 0);
    lay_combo(B_EDIT, white(eng_rank(3))); key_up(white(eng_rank(3))); frame();
    turn(EN_K2, 1);
    ok = TSEL->eng_req == 3u && TSEL->preset == 1u;
    turn(EN_K3, 1);
    ok &= preset_favorite();
    oct_back();
    ok &= TSEL->eng_req == 0u && !memcmp(TSEL->step, before.step, sizeof before.step);
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 2: the engine's next sound, KNOB 3 FAV; OCT-: the sound as the layer opened", ok && ui.home);
    btn_down(B_EDIT); frame(); turn(EN_K1, 1);
    ok = TSEL->eng_req == eng_step(0, 1);
    btn_up(B_EDIT); frame();
    bad += check("  KNOB 1: the next engine", ok);
    {                                                   /* #124: SAMPLE's alias (1 = PIANO) is not a stop on KNOB 2 */
        uint32_t tot, n, c0, c1, c2, seen = 0, alias = 0;
        set_engine_of(TSEL, ENGI_SAMPLE); go_home(); frame();
        eng_list_pos(&tot);
        btn_down(B_EDIT); frame();
        c0 = eng_list_pos(&tot);
        turn(EN_K2, 1);
        ok = TSEL->preset == 2u && user_of(TSEL) >= UP_SLOTS;
        c1 = eng_list_pos(&tot);
        turn(EN_K2, 1);
        ok &= TSEL->preset == 3u;
        c2 = eng_list_pos(&tot);
        ok &= c0 == 0u && c1 == 1u && c2 == 2u && tot >= SMP_NPRESETS - SMP_NALIAS;
        apply_preset(0);
        for (n = 0; n < tot; n++) {                     /* right: every sound once, back to the first */
            turn(EN_K2, 1);
            seen |= 1u << eng_list_pos(&tot);
            alias |= user_of(TSEL) >= UP_SLOTS && TSEL->preset == 1u;
        }
        ok &= seen == (1u << tot) - 1u && TSEL->preset == 0u && user_of(TSEL) >= UP_SLOTS;
        seen = 0;
        for (n = 0; n < tot; n++) {                     /* left: the same, backwards */
            turn(EN_K2, -1);
            seen |= 1u << eng_list_pos(&tot);
            alias |= user_of(TSEL) >= UP_SLOTS && TSEL->preset == 1u;
        }
        ok &= seen == (1u << tot) - 1u && TSEL->preset == 0u && !alias;
        btn_up(B_EDIT); frame();
        bad += check("  #124 SAMPLE: KNOB 2 right PIANO, FLUTE, SAX (No. 1 2 3), both ways round the list, never the alias", ok);
    }
    song.playing = 0;
    lay_combo(B_EDIT, white(LY_INIT));
    ok = ui.confirm == CF_INIT_SOUND;
    frame();
    ok &= !ui.layer;
    key_up(white(LY_INIT)); btn_up(B_EDIT); frame();
    ok &= ui.home && ui.confirm == CF_INIT_SOUND;
    TSEL->p[P_E0] = (int16_t)(TSEL->p[P_E0] + 5);
    press(B_OCTUP);
    bad += check("EDIT + the key after the engines: INITIALIZE SOUND? dialog (closes the layer, no tap); OCT+ inits", ok && !ui.confirm &&
                 msg_is("SOUND INITIALIZED") && TSEL->p[P_E0] == ENGINES[eng_step(0, 1)]->presets[0].e[0]);

    /* no layer in the menu or a dialog: GLO + a key plays */
    ui_power_on(); hold(B_HOME);
    btn_down(B_GLO); frame(); key_down(white(3)); frame();
    ok = !ui.layer && gates() > 0 && !kb_layer;
    key_up(white(3)); btn_up(B_GLO); frame();
    bad += check("in the menu: no GLO layer, GLO + a key plays it; GLO let go: no page", ok && ui.menu == 1);
    ui_power_on(); ui.confirm = CF_CLEAR_SEQ;
    btn_down(B_SCL); frames(512);
    ok = !ui.layer;
    btn_up(B_SCL); frame();
    bad += check("in a dialog: SCL held shows no map and taps nothing", ok && ui.confirm == CF_CLEAR_SEQ);
    /* one layer at a time: a second layer button is ignored, its keys stay notes after */
    ui_power_on();
    btn_down(B_GLO); frame(); btn_down(B_EDIT); frame(); key_down(white(1)); frame();
    ok = ui.layer == LAYER_GLO && TSEL->eng_req == trk[0].eng_req && perf_solo == 2u;
    key_up(white(1)); btn_up(B_GLO); frame();
    key_down(white(1)); frame();
    ok &= gates() > 0 && !ui.layer;
    key_up(white(1)); btn_up(B_EDIT); frame();
    bad += check("GLO then EDIT: GLO's layer; EDIT ignored (no layer, no page, its keys notes)", ok && ui.home);
    return bad;
}

/* bug fixes (UI): one regression each */
static int test_bughunt_ui(void)
{
    int bad = 0, ok;
    /* 1: REC in NAME, the menu or a dialog: nothing (PLAY cannot stop the transport there) */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 3;
    press(B_OCTUP);                                     /* NAME opens */
    press(B_REC);
    bad += check("REC in NAME does nothing (no arm, no transport start); NAME stays",
                 name_on() && !song.rec && !transport_req && !song.playing);
    ui_power_on(); hold(B_HOME); press(B_REC);
    bad += check("REC in the menu does nothing", ui.menu && !song.rec && !transport_req);
    press(B_PLAY);                                      /* (Skiff U1) PLAY works over the menu */
    bad += check("..PLAY there starts the music (U1), the menu stays", ui.menu && transport_req == 1u);
    frames(4); press(B_PLAY);
    bad += check("..and stops it", ui.menu && (transport_req == 2u || !song.playing));
    ui_power_on();
    up_ui(2, 5);
    go_page(GR_USER); ui.uslot = 5; press(B_OCTUP);
    press(B_REC);
    ok = ui.confirm == CF_OVR_USER && !song.rec && !transport_req;
    press(B_OCTUP);
    bad += check("REC in the OVERWRITE? dialog does nothing; OCT+ then opens NAME", ok && name_on());
    {   /* 2: UNDO of a sound load on an FM6 track: the track's own (edited) patch, not PTCH's factory one */
        uint8_t mine[FP_SIZE + 1u], next[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;   /* (no undo copy of this) */
        frame();
        memcpy(mine, fm6_patch[1], sizeof mine);
        mine[0] ^= 0x15; mine[5] ^= 0x22; mine[40] ^= 0x07;   /* an edited patch (the editor, a project) */
        fm6_set_patch(1, mine);
        memcpy(mine, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1);                             /* browse one sound */
        memcpy(next, fm6_patch[1], FP_SIZE);
        hold(B_SAVE);                                   /* UNDO */
        frame();
        ok = TSEL->eng_req == ENGI_FM6 && !memcmp(mine, fm6_patch[1], FP_SIZE);
        press(B_OCTUP);                                 /* REDO (Skiff: OCT+ in the undo window) */
        frame();
        bad += check("UNDO of a sound load on FM6: the track's edited patch back; REDO: the load's",
                     ok && memcmp(mine, next, FP_SIZE) && !memcmp(next, fm6_patch[1], FP_SIZE));
        hold(B_SAVE); frame();                          /* (the edited patch again) */
        btn_down(B_EDIT); frames(500);                  /* EDIT layer: another engine, then OCT- */
        turn(EN_K1, 1);
        ok = ui.layer == LAYER_EDIT && TSEL->eng_req != ENGI_FM6;
        oct_back();
        btn_up(B_EDIT); frame();
        bad += check("  the EDIT layer's OCT- after an engine pick: FM6 with the track's edited patch",
                     ok && TSEL->eng_req == ENGI_FM6 && !memcmp(mine, fm6_patch[1], FP_SIZE));
    }
    {   /* 2b: a patch the editor sends between two preset loads is a new starting point: UNDO brings it back */
        uint8_t ed[FP_SIZE + 1u];
        ui_power_on();
        track_select(1); frame();
        undo_depth++; set_engine_of(TSEL, ENGI_FM6); undo_depth--;
        frame();
        turn(EN_PRESET, 1); frame();                    /* load 1 */
        memcpy(ed, fm6_patch[1], sizeof ed);
        ed[0] ^= 0x15; ed[40] ^= 0x07;
        fm6_set_patch(1, ed);                           /* the editor's FM6_PUT */
        memcpy(ed, fm6_patch[1], FP_SIZE);
        turn(EN_PRESET, 1); frame();                    /* load 2 */
        hold(B_SAVE); frame();                          /* UNDO */
        bad += check("UNDO after load, editor patch, load: the editor's patch back",
                     !memcmp(ed, fm6_patch[1], FP_SIZE));
    }
    {   /* the ARP button flashes on the beat while an ARP plays (the bar's first beat longer); dark otherwise */
        uint32_t q, b, r[5];
        ui_power_on(); frame();
        b = beat_samples();
        r[0] = arp_led();                               /* no ARP playing */
        trk[2].p[P_AMODE] = 1; trk[2].nheld = 1; trk[2].held[0] = 60;
        beat_n = 1; beat_pos = 0; r[1] = arp_led();
        beat_pos = b / 3u; r[2] = arp_led();
        beat_n = 0; r[3] = arp_led();
        beat_pos = b * 3u / 4u; r[4] = arp_led();
        bad += check("ARP LED: none without an ARP; flashes on the beat, the bar's first beat longer",
                     r[0] == 2u && r[1] == 1u && r[2] == 0u && r[3] == 1u && r[4] == 0u);
        for (q = 0; q < 4u * b / 128u + 4u; q++) events_block(128);
        bad += check("  the beat counter runs (a bar of blocks wraps the beat of the bar)", beat_n < 4u && beat_pos < b);
        trk[2].p[P_AMODE] = 0; trk[2].nheld = 0;
    }
    /* 3: USER ERASE asks first (ERASE U02?): OCT- keeps the preset, OCT+ erases it */
    ui_power_on();
    go_page(GR_USER); ui.uslot = 1;
    up_ui(2, 1);
    turn(EN_K1 + 2, 1);                                 /* KNOB 3: ERASE picked */
    press(B_OCTUP);
    ok = ui.confirm == CF_ERASE_USER && ui.confirm_trk == 1u && up_used(1);
    {
        char a[24], b[24];
        confirm_text(a, b);
        ok &= str_eq(a, "ERASE U02?") && b[0];
    }
    press(B_OCTDN);
    ok &= !ui.confirm && up_used(1);
    turn(EN_K1 + 2, 1); press(B_OCTUP); press(B_OCTUP);
    bad += check("USER ERASE: the ERASE U02? dialog; OCT- keeps the preset, OCT+ erases it",
                 ok && !ui.confirm && !up_used(1));
    turn(EN_K1 + 2, 1); press(B_OCTUP);
    bad += check("  an empty slot: no dialog, EMPTY SLOT", !ui.confirm && msg_is("EMPTY SLOT"));
    {   /* 4: FEEL is not NOTES: no grid, no key entry, no EDIT clear (its knobs only) */
        uint32_t before;
        step_t s0;
        ui_power_on();
        track_select(3); frame();                       /* T4 DRUM */
        go_page(GR_FEEL); frame();
        before = lane_steps(TSEL, ui.lane);
        tap_key(white(2));
        bad += check("FEEL on a DRUM track: no grid keys (LEDs), a white key toggles no hit",
                     drum_track(TSEL) && !grid_on() && !keys_mode() && lane_steps(TSEL, ui.lane) == before);
        ui_power_on(); go_page(GR_FEEL); frame();
        my_steps(TSEL);
        s0 = TSEL->step[0];
        tap_key(white(5)); frame();
        ok = !memcmp(&s0, &TSEL->step[0], sizeof s0) && ui.cursor == 0u;
        press(B_EDIT);
        ok &= !memcmp(&s0, &TSEL->step[0], sizeof s0) && !msg_is("STEP CLEARED");
        go_page(GR_FEEL); frame();                      /* (EDIT opened its pages: back) */
        turn(EN_K1, -10);                               /* (100 % by default) */
        bad += check("FEEL on a melodic track: a key writes no step, EDIT clears none; KNOB 1 the chance",
                     ok && step_chance(&TSEL->step[0]) != step_chance(&s0));
        ui_power_on(); go_page(GR_ROLL); frame(); song.rec = 1;   /* (#133: armed, stopped) */
        tap_key(white(5)); frame();
        bad += check("  NOTES still: armed, a key writes the cursor step", step_on(&TSEL->step[0]) || TSEL->step[0].n);
        song.rec = 0;
    }
    {   /* 5: ALGORITHM does nothing while a layer's button is held (as PRESETS); after it, the track again */
        int16_t r0;
        ui_power_on();
        r0 = trk[0].p[P_ROOT];
        btn_down(B_SCL); frames(500);
        turn(EN_ALGO, 1);
        ok = ui.layer == LAYER_SCL && song.sel == 0u;
        key_down(white(1)); frame(); key_up(white(1)); frame();   /* ROOT on T1 */
        ok &= trk[0].p[P_ROOT] != r0;
        oct_back();
        ok &= trk[0].p[P_ROOT] == r0;
        btn_up(B_SCL); frame();
        btn_down(B_EDIT); turn(EN_ALGO, 1);             /* armed, not open yet: ignored too */
        ok &= song.sel == 0u;
        btn_up(B_EDIT); frame(); go_home(); frame();
        turn(EN_ALGO, 1);
        bad += check("ALGORITHM ignored with a layer's button held (OCT- puts back all); then T2",
                     ok && song.sel == 1u);
    }
    /* 7: REC works in the layers, as PLAY: it arms the track (PLAY starts); the layer stays, no page */
    ok = 1;
    {
        static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 4u; i++) {
            ui_power_on();
            btn_down(LB[i]); frame();
            press(B_REC);
            ok &= song.rec == 1u && transport_req == 1u && ui.layer != 0u;
            btn_up(LB[i]); frame();
            ok &= ui.home;
        }
    }
    bad += check("REC in every layer: arms the track and starts PLAY; the layer stays, no page", ok);
    /* 8: the OCT- LED lit in the SET layers (OCT- = UNDO), OCT+ dark; FX (HOLD): the octave as usual */
    ok = 1;
    {
        static const uint8_t LB[3] = {B_GLO, B_SCL, B_EDIT};
        uint32_t i;
        for (i = 0; i < 3u; i++) {
            ui_power_on();
            song.octave = 1;                            /* (outside: OCT+ lit) */
            btn_down(LB[i]); frames(500);
            ok &= layer_set_open() && oct_leds_seen(1) == 1u && oct_leds_seen(0) == 1u;
            btn_up(LB[i]); frame();
            ok &= oct_leds() == 2u;
        }
        ui_power_on();
        btn_down(B_FX); frames(500);
        ok &= ui.layer == LAYER_FX && oct_leds() == 0u;
        btn_up(B_FX); frame();
    }
    bad += check("SET layers: the OCT- LED lit (UNDO), OCT+ dark; after: the octave; FX: the octave", ok);
    /* 9: (U2) EDIT tapped on USER is the EDIT pages as anywhere (NAME is KNOB 3's), with the hint */
    ui_power_on();
    layer_seen = 0;
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    ui.msg_t = 0;
    press(B_EDIT);
    bad += check("(U2) EDIT on USER: the EDIT pages (not NAME), the layer hint as elsewhere",
                 !name_on() && cur_page()->fam == FAM_EDIT && msg_is("HOLD [EDIT]: SHORTCUTS"));
    return bad;
}

/* the chord keys on the device (chord.c): SCL tapped again is the CHORD page, KNOB 1 CHRD, KNOB 2 VOIC, the graph
 * names the last chord (MONO gray); a sound load keeps them; the SCL layer's KNOB 3 / 4 are CHRD / VOIC, OCT-
 * puts them back */
static int test_chord_page(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    go_home(); frame();
    press(B_SCL); frames(400);
    ok = str_eq(cur_page()->title, "SCALE");
    press(B_SCL); frames(400);
    ok &= str_eq(cur_page()->title, "CHORD") && cur_page()->graph == GR_CHORD && cur_page()->fam == FAM_SCL;
    bad += check("SCL tapped again: the CHORD page (CHRD VOIC, the chord graph)", ok && cur_page()->id[0] == P_CHRD &&
                 cur_page()->id[1] == P_VOIC);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  CHRD OFF: the page draws", cur_page()->graph == GR_CHORD && ui.graph_sig != 0u);
    turn(EN_K1, 1); frame();
    turn(EN_K2, 1); frame();
    bad += check("  KNOB 1: CHRD DIA3, KNOB 2: VOIC OPEN", t->p[P_CHRD] == CH_DIA3 && t->p[P_VOIC] == VC_OPEN);
    key_down(7); frame();                                          /* C4 in C (SCALE CHR: the major of C) */
    ok = gates() == 3u && chord_last[song.sel].root == 60;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    key_up(7); frame();
    bad += check("  a key plays the chord, the graph shows it (C)", ok && !gates());
    t->p[P_CHRD] = CH_MIN7; t->p[P_VOIC] = VC_BASS;
    apply_preset_to(t, 1);
    bad += check("  a sound load keeps CHRD and VOIC (SCL settings)", t->p[P_CHRD] == CH_MIN7 && t->p[P_VOIC] == VC_BASS);
    set_engine_of(t, ENGI_DRUM); t->engine = t->eng_req; frame();
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  a kit (DRUM): the page says so", chord_kit(t));
    ui_power_on();
    t = TSEL;
    t->p[P_CHRD] = CH_OFF; t->p[P_VOIC] = VC_CLOSE;
    go_home(); frame();
    btn_down(B_SCL); frames(800);
    ok = ui.layer == LAYER_SCL;
    turn(EN_K3, 2); frame();
    turn(EN_K4, 1); frame();
    ok &= t->p[P_CHRD] == CH_DIA7 && t->p[P_VOIC] == VC_OPEN && t->p[P_QUANT] == 0 && t->p[P_TRANS] == 0;
    turn(EN_K1, 2); frame();
    ok &= t->p[P_ROOT] == 2;
    oct_back();
    btn_up(B_SCL); frame();
    bad += check("SCL layer: KNOB 1 ROOT, 3 CHRD, 4 VOIC (QNT TRN untouched); OCT- puts them back", ok &&
                 t->p[P_CHRD] == CH_OFF && t->p[P_VOIC] == VC_CLOSE && t->p[P_ROOT] == 0 && ui.home);
    btn_down(B_SCL); frames(800);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = ui.layer == LAYER_SCL;
    btn_up(B_SCL); frame();
    bad += check("  its map and cards draw", ok);
    return bad;
}

/* the STEP page's piano roll (ui_graph.c graph_roll), read back from the screen: per column the rows whose centre
 * pixel (the bar's middle, x + 6) is a bar colour, against the notes the step sounds (its own; a TIE: the note
 * before's; a REST: none) */
static int pr_is_bar(uint32_t x, uint32_t y)
{
    uint16_t c = swap16(host_screen[y * 240u + x]);
    return c == T_THEME || c == T_ACCENT || c == T_TEXT || c == T_TRK(song.sel);   /* ((U2) BLOCKS: the track's) */
}
static int pr_columns_match(const track_t *t, uint32_t *nbars)
{
    uint32_t i, r, j, len = (uint32_t)t->p[P_SLEN], base = ui.bank * 16u;
    int ok = 1;
    for (i = 0; i < 16u && base + i < len; i++) {
        uint32_t want = 0, got = 0, s = pr_src(t, base + i, len);
        for (j = 0; s < NSTEP && j < 4u + NLANE; j++) {
            int32_t n = pr_note(&t->step[s], j), rr = (int32_t)proll.lo + PR_ROWS - 1 - n;
            if (n >= 0 && rr >= 0 && rr < PR_ROWS) want |= 1u << rr;
        }
        for (r = 0; r < PR_ROWS; r++)
            if (pr_is_bar((uint32_t)(PR_X0 + (int32_t)i * PR_CW + 6), Y_GRAPH + (uint32_t)graph_pad() + PR_Y0 + r * PR_RH + 2u))
                got |= 1u << r;   /* ((U2) BLOCKS: the panel's pad) */
        if (got != want) {
            printf("ui:   roll column %u: rows %05x drawn, %05x wanted\n", base + i, got, want);
            ok = 0;
        }
        for (r = 0; r < PR_ROWS; r++) *nbars += (got >> r) & 1u;
    }
    return ok;
}
/* 1.2, Discussion #133: step recording on SEQ > STEP. Not armed, the keys only play (being on SEQ writes nothing);
 * REC there arms step recording without starting the transport; armed and stopped a key (keys held together: a
 * chord) writes the cursor step and letting go moves the cursor on one step, wrapping inside LEN; PLAY records live
 * (at the play head, not the cursor). The DRUM grid keeps its own keys (white keys = steps, REC starts PLAY) */
static int test_step_rec(void)
{
    int bad = 0, ok;
    track_t *t;
    step_t before[NSTEP];
    ui_power_on();
    t = TSEL;
    t->p[P_VOICE] = V_POLY;
    track_defaults_steps(t);
    go_page(GR_ROLL);
    cursor_set(3);
    frame();
    memcpy(before, t->step, sizeof before);
    key_down(7); frame();
    ok = kb_chn[7] != 0u;                                             /* it sounds */
    key_up(7); frame();
    ok &= !memcmp(before, t->step, sizeof before) && ui.cursor == 3u && !song.rec && !transport_req;
    bad += check("#133 STEP, not armed, stopped: a key sounds, writes nothing, the cursor stays", ok);
    song.playing = 1;
    key_down(7); frame(); key_up(7); frame();
    song.playing = 0;
    bad += check("#133 STEP, not armed, playing: nothing written either", !memcmp(before, t->step, sizeof before) &&
                 ui.cursor == 3u);
    song.rec = 2u;                                                    /* T2 armed, T1 selected */
    key_down(7); frame(); key_up(7); frame();
    bad += check("#133 another track armed: the selected track's STEP writes nothing",
                 !memcmp(before, t->step, sizeof before) && ui.cursor == 3u);
    song.rec = 0;
    press(B_REC);
    bad += check("#133 REC on STEP arms step recording: stopped, KEYS WRITE STEPS",
                 song.rec == 1u && !transport_req && !song.playing && !seq_counting() && msg_is("KEYS WRITE STEPS"));
    key_down(7); frame();
    ok = t->step[3].n == 1u && t->step[3].note[0] == kb_map(t, 7) && t->step[3].time == ST_NOTE && ui.cursor == 3u;
    key_up(7); frame();
    ok &= ui.cursor == 4u && !memcmp(before, t->step, 3u * sizeof before[0]) &&
          !memcmp(before + 4, t->step + 4, (NSTEP - 4u) * sizeof before[0]);
    bad += check("#133 armed, stopped: a key writes its note into the cursor step (only), letting go moves on one", ok);
    key_down(7); frame(); key_down(11); frame(); key_down(14); frame();
    ok = t->step[4].n == 3u && ui.cursor == 4u;
    key_up(7); frame();
    ok &= ui.cursor == 4u;                                            /* (two keys still down: the step stays open) */
    key_up(11); key_up(14); frame();
    ok &= ui.cursor == 5u && t->step[4].note[0] == kb_map(t, 7) && t->step[4].note[1] == kb_map(t, 11) &&
          t->step[4].note[2] == kb_map(t, 14);
    bad += check("#133 keys held together: one chord step (POLY), the cursor moves on once all are up", ok);
    t->p[P_VOICE] = V_MONO;
    key_down(7); frame(); key_down(9); frame(); key_up(7); key_up(9); frame();
    bad += check("#133 MONO: one note, the last key", t->step[5].n == 1u && t->step[5].note[0] == kb_map(t, 9) &&
                 ui.cursor == 6u);
    t->p[P_SLEN] = 8;
    cursor_set(7);
    frame();
    key_down(9); frame(); key_up(9); frame();
    bad += check("#133 the last step of LEN 8: written, the cursor wraps to step 1", t->step[7].n == 1u && ui.cursor == 0u);
    cursor_set(4);                                                    /* (an empty step, away from the play head) */
    step_clear(&t->step[4]);
    frame();
    press(B_PLAY);
    events_block(CTL);
    ok = song.playing && live_rec_sel() && t->seq_idx < 2u;
    key_down(7); frame(); key_up(7); frame();
    ok &= ui.cursor == 4u && !t->step[4].n && !t->step[4].hit && (t->step[0].n || t->step[1].n);
    bad += check("#133 PLAY with the track armed: live recording at the play head (the cursor and its step stay)", ok);
    stop_transport();
    press(B_REC);                                                     /* disarm: the keys only play again */
    memcpy(before, t->step, sizeof before);
    key_down(7); frame(); key_up(7); frame();
    bad += check("#133 REC again disarms: the keys only play, nothing written",
                 !song.rec && !memcmp(before, t->step, sizeof before) && ui.cursor == 4u);
    /* the DRUM grid as before: white keys are the steps (armed or not), REC from stop starts PLAY */
    ui_power_on();
    t = TSEL;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    go_page(GR_ROLL);
    frame();
    tap_key(key_at(0, 4));
    ok = grid_on() && t->step[4].hit != 0u;
    press(B_REC);
    ok &= song.rec == 1u && transport_req == 1u && msg_is("KEYS RECORD LANES");
    transport_req = 0;                                                /* (armed, still stopped) */
    tap_key(key_at(0, 6));
    tap_key(key_at(0, 4));
    ok &= t->step[6].hit != 0u && !t->step[4].hit && ui.cursor == 4u;
    bad += check("#133 the DRUM grid unchanged: white keys toggle steps armed or not, REC from stop starts PLAY", ok);
    song.rec = 0;
    return bad;
}

static int test_piano_roll(void)
{
    int bad = 0, ok;
    uint32_t i, nb = 0, lo0, steps = 0;
    track_t *t;
    ui_power_on();
    t = TSEL;
    load_pat16(t, PATTERNS[0].note, PATTERNS[0].flags);               /* ACID: A2..A3, ties, accents, slides */
    t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;
    go_page(GR_ROLL); ui.cursor = 0; ui.bank = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    ok = pr_columns_match(t, &nb);
    bad += check("piano roll: ACID's bars drawn at their notes' rows, ties carried, rests empty", ok && nb >= 10u);
    bad += check("  the view holds the page's notes (A2..A3 in 21 rows)", proll.lo <= 45 && proll.lo + PR_ROWS - 1 >= 57);
    t->p[P_VOICE] = V_POLY;                                           /* chords: up to 4 bars in a column */
    for (i = 0; i < 16u; i += 4u) {
        step_t *s = &t->step[i];
        s->time = ST_NOTE; s->n = 4; s->note[0] = 57; s->note[1] = 60; s->note[2] = 64; s->note[3] = 67;
        t->step[i + 1u].time = ST_TIE;
    }
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  4-note chords stacked, their ties carry all four", pr_columns_match(t, &nb));
    for (i = 0; i < NSTEP; i++) {                                     /* LEN 32, page 2 */
        step_clear(&t->step[i]);
        if (i >= 16u && i % 2u == 0u) { t->step[i].time = ST_NOTE; t->step[i].n = 1; t->step[i].note[0] = (uint8_t)(72 + i % 7u); }
    }
    t->p[P_SLEN] = 32; cursor_set(18);
    nb = 0;
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  LEN 32, the cursor on page 2: its 16 steps drawn", ui.bank == 1u && pr_columns_match(t, &nb) && nb == 8u);
    lo0 = proll.lo;                                                   /* two octaves up: the view follows in steps */
    for (i = 16; i < 32u; i++) if (t->step[i].n) t->step[i].note[0] += 24;
    for (i = 0; i < 20u && proll.lo != lo0 + 24u; i++) { frame(); steps++; }
    bad += check("  notes moved two octaves: the view follows over frames (not at once), then rests",
                 proll.lo == lo0 + 24u && steps >= 3u && steps < 20u);
    {
        uint32_t sig = ui.graph_sig;
        frame(); frame();
        bad += check("  nothing changed: the graph is not drawn again", ui.graph_sig == sig);
    }
    nb = 0;
    bad += check("  after the follow the bars match again", pr_columns_match(t, &nb));
    song.rec = 1;                                                     /* (#133: armed, the key writes the step) */
    key_down(7); frames(200);                                         /* a key held: its row lit on the strip */
    {
        int32_t r = (int32_t)proll.lo + PR_ROWS - 1 - (int32_t)kb_chord[7][0];
        uint16_t c = r >= 0 && r < PR_ROWS ? swap16(host_screen[(Y_GRAPH + (uint32_t)graph_pad() + PR_Y0 + (uint32_t)r * PR_RH + 1u) * 240u + PR_KX + 7]) : 0;
        bad += check("  a key held: its row on the keyboard strip is the accent", kb_chn[7] && c == T_ACCENT);
    }
    key_up(7); frames(100);
    song.rec = 0;
    return bad;
}

/* bug fixes, second round (UI) */
static uint32_t accent_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    uint32_t n = 0;
    int32_t x, y;
    uint16_t a = (uint16_t)((T_ACCENT >> 8) | (T_ACCENT << 8));
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++)
            n += host_screen[y * 240 + x] == a;
    return n;
}
static int test_bughunt_ui2(void)
{
    int bad = 0;
    uint32_t i;
    {   /* 1. a layer button pressed under a dialog and still held after it closes: dead, its keys stay notes */
        static const uint32_t BTN[2] = {B_FX, B_GLO};
        for (i = 0; i < 2u; i++) {
            uint32_t k = white(0), ok;
            ui_power_on(); TSEL->p[P_VOICE] = V_POLY;
            go_home(); frame();
            confirm_open(CF_CLEAR_SEQ, 0); frame();
            btn_down(BTN[i]); frame(); frames(100);
            btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();   /* the dialog closes, the button still held */
            frames(800);
            ok = !ui.confirm && (ui.ly_t0 & LY_DEAD) && !ui.layer && !kb_mask && !perf_mask;
            key_down(k); frame();
            ok = ok && kb_note[k] != KB_SILENT && gates() == 1u && !((kb_layer >> k) & 1u) && !perf_held && !ui.layer;
            key_up(k); frame();
            btn_up(BTN[i]); frame();
            bad += check(i ? "dead GLO held from a dialog: no map, the key plays a note"
                           : "dead FX held from a dialog: no map, the key plays a note, no effect", ok);
        }
    }
    {   /* 2. STEP entry with CHRD on writes what the key sounds, as live recording does */
        static const uint32_t KEY[2] = {7u, 8u};                      /* C4, C#4 (out of C major) */
        uint32_t v, j, ok = 1;
        for (v = 0; v < 2u; v++)
            for (j = 0; j < 2u; j++) {
                track_t *t;
                step_t e, r;
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_page(GR_ROLL); cursor_set(0); frame(); song.rec = 1;   /* (#133: armed) */
                key_down(KEY[j]); frame();
                e = t->step[0];
                key_up(KEY[j]); frame(); song.rec = 0;
                ok &= e.n == (v ? 1u : 3u) && e.note[0] == 60u && (v || (e.note[1] == 64u && e.note[2] == 67u));
                ui_power_on(); t = TSEL; t->p[P_VOICE] = v ? V_MONO : V_POLY; t->p[P_CHRD] = CH_DIA3; t->p[P_SCALE] = 1;
                track_defaults_steps(t); go_home(); frame();
                song.rec = 1; song.playing = 1; t->seq_idx = 3; t->seq_pos = 0;
                fm1_in.notes = 1u << KEY[j]; keyboard_block();
                r = t->step[3];
                fm1_in.notes = 0; keyboard_block();
                song.rec = 0; song.playing = 0;
                ok &= r.n == e.n && !memcmp(r.note, e.note, e.n);
            }
        bad += check("STEP entry with CHRD: POLY the chord, MONO its root, as live recording writes", ok);
    }
    {   /* 3. MIXER ((Skiff U3, N1) six strips): the selected track's strip a band of its colour, all on it INK; the knob
         * just turned lights its card */
        uint32_t k, ok = 1;
        for (k = 0; k < 4u; k++) {
            ui_power_on();
            go_page(GR_TRK); frame(); frame();
            turn(EN_K1 + k, 1);
            ui.force = 1; frame();
            ok &= ui.hot_t && ui.hot_col == k && swap16(host_screen[(Y_GRAPH + 2u) * 240u + (uint32_t)MS_X(0) + 2u]) == T_TRK(0) &&
                  swap16(host_screen[(Y_GRAPH + 2u) * 240u + (uint32_t)MS_X(1) + 2u]) == T_SURF;
        }
        bad += check("MIXER (U3: six strips): the selected track's strip a band of its colour; the knob turned lights its card", ok);
    }
    {   /* 4. renaming the slot the music came from renames the music: the next SAVE prefills the new name */
        char pn[16];
        uint32_t ok;
        ui_power_on();
        go_page(GR_SLOTS); song.g[G_SLOT] = 2;
        project_save_as(1, "OLD");
        project_save_as(2, "OTHER");
        project_load(1); frame();
        ok = str_eq(proj_name, "OLD");
        project_rename(2, "ELSE");
        ok &= str_eq(proj_name, "OLD");                               /* another slot: the music's name stays */
        proj_rename();                                                /* rename B on the slot list (U2: KNOB 2 NAME) */
        while (nm.len) { nm_do(NB_DEL); }
        nm_white(13); frames(900);
        str_cpy(nm.s, "NEW", sizeof nm.s); nm.len = nm.cur = 3; nm.key = 0;
        press(B_OCTUP);
        project_name(1, pn);
        ok &= str_eq(pn, "NEW") && str_eq(proj_name, "NEW");
        turn(EN_K4, 1); press(B_OCTUP);                               /* SAVE to B: OVERWRITE? */
        press(B_OCTUP);                                               /* YES: NAME */
        ok &= name_on() && str_eq(nm.s, "NEW");
        press(B_OCTDN);
        bad += check("rename of the loaded slot: the music's name follows, SAVE prefills it", ok);
    }
    {   /* 5. NAME: PLAY stops a transport started meanwhile (so the name can be saved), never starts it; REC ignored */
        uint32_t ok;
        ui_power_on(); go_page(GR_USER); ui.uslot = 3; press(B_OCTUP);
        ok = name_on();
        press(B_PLAY); frame();
        ok &= !transport_req && !song.playing && name_on();          /* stopped: PLAY starts nothing */
        press(B_REC); frame();
        ok &= !song.rec && !transport_req && name_on();
        song.playing = 1;                                             /* an external MIDI Start / the editor */
        press(B_OCTUP);
        ok &= name_on() && msg_is("STOP TO SAVE");
        press(B_REC); frame();
        ok &= !song.rec && !transport_req;
        press(B_PLAY); frame();
        ok &= transport_req == 2u && name_on();                       /* PLAY stops it, NAME stays */
        stop_transport();
        press(B_OCTUP);
        ok &= !name_on() && up_used(3);
        bad += check("NAME: PLAY stops a running transport (never starts), REC ignored, then OCT+ saves", ok);
    }
    {   /* 6. the piano roll's lowest C name ("C-1") stays inside the panel: nothing drawn left of x 3 */
        uint32_t p, x, y, out = 0;
        uint16_t bg;
        for (p = 0; p < NPALETTES; p++) {
            track_t *t;
            uint8_t lo = 0, hi = 127;
            ui_power_on(); t = TSEL; track_defaults_steps(t); t->p[P_SLEN] = 16;
            settings.palette = (uint8_t)p; palette_set(p);
            t->step[0].time = t->step[5].time = ST_NOTE; t->step[0].n = t->step[5].n = 1;
            t->step[0].note[0] = lo; t->step[5].note[0] = hi;
            go_page(GR_ROLL); cursor_set(0); ui.force = 1; frame(); frame();
            bg = (uint16_t)((T_BG >> 8) | (T_BG << 8));
            for (y = Y_GRAPH; y < Y_GRAPH + H_GRAPH; y++)
                for (x = 0; x < 3u; x++)
                    out += host_screen[y * 240u + x] != bg;
        }
        bad += check("piano roll: the C-1 label stays inside the panel (every palette)", !out);
    }
    {   /* 7. EDIT: INIT is drawn in the cell after the engines, and that cell's white key is the one that inits */
        uint32_t p, ok = LY_INIT == NENG_SHOWN && LY_INIT < 16u;      /* (layer_edit: INIT in cell NENG_SHOWN) */
        for (p = NENG_SHOWN; p < 16u; p++) {
            ui_power_on(); go_home(); frame();
            btn_down(B_EDIT); key_down(white(p)); frame(); frames(50);
            ok &= (ui.confirm == CF_INIT_SOUND) == (p == LY_INIT);
            key_up(white(p)); btn_up(B_EDIT); frame();
        }
        bad += check("EDIT: INIT's map cell (after the engines) and its key agree", ok);
    }
    return bad;
}

#ifndef UI_TEST_NO_MAIN
/* L is a sparse face (tools/gen_aa_font.py L_CHARS): every string drawn in it has all its glyphs */
static int test_large_face(void)
{
    static const char *const FIXED[] = {"SKIFF", "0123456789"};   /* main.c, ui_menu.c; ui_draw.c draw_uboot */
    uint32_t i, missing = 0;
    const char *s;
    for (i = 0; i < NB + NE + 2u; i++)                              /* ui_input.c setup_show: the control names */
        for (s = i < NB ? B_NAME[i] : i < NB + NE ? E_NAME[i - NB] : FIXED[i - NB - NE]; *s; s++)
            missing += glyph_at(&AF_L, (uint8_t)*s) < 0;
    return check("the L face holds every glyph of the strings drawn in it", !missing);
}

#if FELUCCA_FM4
/* The DIGITAL algorithm charts (ui_graph.c FM_CELL / FM_MOD) against the DSP: src/eng_digital.c's switch (alg)
 * in digital_render_legacy and digital_render_custom, written out here as "source>destination" routes and the
 * operators mixed to the output. Op 4's feedback is on every algorithm (drawn always). Also: the carriers sit
 * on the bottom row and only they, a modulator one row above what it modulates, no two operators in a cell,
 * every operator heard, and the eight charts distinct. */
static int test_fm_charts(void)
{
    static const char *const DSP[8][2] = {
        {"4>3 3>2 2>1", "1"},          /* o3 = op(o4), o2 = op(o3), s = op(o2) */
        {"3>2 4>2 2>1", "1"},          /* o2 = op((o3 + o4) / 2), s = op(o2) */
        {"3>2 2>1 4>1", "1"},          /* o2 = op(o3), s = op((o2 + o4) / 2) */
        {"4>3 2>1 3>1", "1"},          /* o3 = op(o4), s = op((o2 + o3) / 2) */
        {"4>3 2>1", "13"},             /* o3 = op(o4), o1 = op(o2), s = (o1 + o3) / 2 */
        {"4>1 4>2 4>3", "123"},        /* o1, o2, o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"4>3", "123"},                /* o3 = op(o4), s = (o1 + o2 + o3) / 3 */
        {"", "1234"}};                 /* s = (o1 + o2 + o3 + o4) / 4 */
    uint32_t a, b, k, j, routes = 1, layout = 1, distinct = 1;
    for (a = 0; a < 8u; a++) {
        uint32_t want = 0, car = 0, ok = 1;
        const char *s;
        for (s = DSP[a][0]; *s; s++)
            if (*s == '>') want |= 1u << (4u * (uint32_t)(s[1] - '1') + (uint32_t)(s[-1] - '1'));
        for (s = DSP[a][1]; *s; s++) car |= 1u << (*s - '1');
        if (FM_MOD[a] != want) { printf("ui: algorithm %u: routes %04x, the DSP %04x\n", a + 1u, FM_MOD[a], want); routes = 0; }
        for (k = 0; k < 4u; k++) {
            uint32_t row = FM_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u)) ok = 0;
            for (j = 0; j < 4u; j++) {
                if (j != k && FM_CELL[a][j] == FM_CELL[a][k]) ok = 0;
                if (FM_MOD[a] >> (4u * j + k) & 1u) { outs++; if (row != (FM_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;
        }
        if (!ok) { printf("ui: algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
        for (b = 0; b < a; b++)
            if (FM_MOD[a] == FM_MOD[b] && !memcmp(FM_CELL[a], FM_CELL[b], 4)) distinct = 0;
    }
    return check("DIGITAL charts: the 8 algorithms' routes and carriers equal eng_digital.c's", routes) +
           check("DIGITAL charts: carriers on the bottom row, modulators a row up, one per cell, distinct", layout && distinct);
}
#endif

/* The FM6 algorithm charts (ui_graph.c graph_fm6: FM6_CELL, its routes fm6_routes and feedback operator
 * fm6_fb_op read from fm6_core.c FM6_ALG) against the 32 algorithms of 6-operator FM written out here as
 * "source>destination" routes, the carriers and the operator with feedback (fm6_core.c's: on algorithms 4 and
 * 6 the loop is the sixth operator's own, as msfa plays it). Also against the core itself: the carriers
 * fm6_carriers (fm6_car_ops), the feedback flags. The layout: the carriers on the bottom row and only they, a modulator one
 * row above everything it modulates, one operator per cell, columns 0..5, at most 4 rows. (The charts as drawn:
 * tests/ui_render.c's lint, fm6_lint.) */
static int test_fm6_charts(void)
{
    static const struct { const char *r, *car; char fb; } ALGS[32] = {
        {"2>1 4>3 5>4 6>5", "13", '6'}, {"2>1 4>3 5>4 6>5", "13", '2'}, {"2>1 3>2 5>4 6>5", "14", '6'},
        {"2>1 3>2 5>4 6>5", "14", '6'}, {"2>1 4>3 6>5", "135", '6'}, {"2>1 4>3 6>5", "135", '6'},
        {"2>1 4>3 5>3 6>5", "13", '6'}, {"2>1 4>3 5>3 6>5", "13", '4'}, {"2>1 4>3 5>3 6>5", "13", '2'},
        {"2>1 3>2 5>4 6>4", "14", '3'}, {"2>1 3>2 5>4 6>4", "14", '6'}, {"2>1 4>3 5>3 6>3", "13", '2'},
        {"2>1 4>3 5>3 6>3", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '6'}, {"2>1 4>3 5>4 6>4", "13", '2'},
        {"2>1 3>1 5>1 4>3 6>5", "1", '6'}, {"2>1 3>1 5>1 4>3 6>5", "1", '2'}, {"2>1 3>1 4>1 5>4 6>5", "1", '3'},
        {"2>1 3>2 6>4 6>5", "145", '6'}, {"3>1 3>2 5>4 6>4", "124", '3'}, {"3>1 3>2 6>4 6>5", "1245", '3'},
        {"2>1 6>3 6>4 6>5", "1345", '6'}, {"3>2 6>4 6>5", "1245", '6'}, {"6>3 6>4 6>5", "12345", '6'},
        {"6>4 6>5", "12345", '6'}, {"3>2 5>4 6>4", "124", '6'}, {"3>2 5>4 6>4", "124", '3'},
        {"2>1 4>3 5>4", "136", '5'}, {"4>3 6>5", "1235", '6'}, {"4>3 5>4", "1236", '5'},
        {"6>5", "12345", '6'}, {"", "123456", '6'}};
    uint32_t a, k, j, routes = 1, layout = 1, core = 1;
    for (a = 0; a < 32u; a++) {
        uint8_t m[6], want[6] = {0, 0, 0, 0, 0, 0};
        uint32_t car = 0, ok = 1, fbs = 0;
        const char *s;
        for (s = ALGS[a].r; *s; s++)
            if (*s == '>') want[s[1] - '1'] |= (uint8_t)(1u << (s[-1] - '1'));
        for (s = ALGS[a].car; *s; s++) car |= 1u << (*s - '1');
        fm6_routes(a, m);
        if (memcmp(m, want, 6) || fm6_fb_op(a) != (uint32_t)(ALGS[a].fb - '1')) {
            printf("ui: FM6 algorithm %u: the chart's routes or feedback differ from the written ones\n", a + 1u);
            routes = 0;
        }
        for (k = 0; k < 6u; k++)                                    /* the core: who writes the output, who has FB */
            fbs += (FM6_ALG[a][k] & 0xC0u) == 0xC0u;
        if (fm6_car_ops(a) != car || fbs != 1u) {
            printf("ui: FM6 algorithm %u: fm6_core.c's carriers / feedback differ\n", a + 1u);
            core = 0;
        }
        for (k = 0; k < 6u; k++) {
            uint32_t row = FM6_CELL[a][k] >> 4, outs = 0;
            if ((row == 0u) != ((car >> k) & 1u) || (FM6_CELL[a][k] & 0x0Fu) > 5u || row > 3u) ok = 0;
            for (j = 0; j < 6u; j++) {
                if (j != k && FM6_CELL[a][j] == FM6_CELL[a][k]) ok = 0;
                if (m[j] >> k & 1u) { outs++; if (FM6_CELL[a][k] >> 4 != (FM6_CELL[a][j] >> 4) + 1u) ok = 0; }
            }
            if (!outs && !((car >> k) & 1u)) ok = 0;               /* every operator heard */
        }
        if (!ok) { printf("ui: FM6 algorithm %u: the chart's layout is off\n", a + 1u); layout = 0; }
    }
    return check("FM6 charts: the 32 algorithms' routes, carriers and feedback operator equal fm6_core.c's", routes && core) +
           check("FM6 charts: carriers on the bottom row, modulators a row up, one per cell, at most 4 rows", layout);
}

#if !FELUCCA_FM4
/* DIGITAL retired (src/fm4_convert.c): engine 1 is on no track and in no list; its sounds arrive as FM6 with a patch
 * of their own: a user preset of it, its preset numbers (set_engine_of / apply_preset_to), a track that got engine 1
 * any other way (the main loop's net); the favourites of its presets move to FM6's; the power-on pad is FM6 PAD.
 * ((2.0) The conversions with FELUCCA_LEGACY alone: 2.0 keeps such a user preset as OLD FORMAT, engine 1 asked for is FM6) */
static int test_fm4_retired(void)
{
    int bad = 0, ok = 1;
    uint32_t i, k, e, total, seen = 0, all = ((1u << NENGINES) - 1u) & ~(1u << ENGI_DIGITAL);
    int16_t p[P_COUNT];
    uint8_t v[FP_SIZE + 1u];
    ui_power_on();
    bad += check("power-on: track 2 is FM6 PAD (TRK_DEF, was DIGITAL PAD)", trk[1].eng_req == ENGI_FM6 &&
                 str_eq(ENGINES[ENGI_FM6]->presets[trk[1].preset].name, "PAD"));
    preset_all_pos(&total);
    for (i = 0; i < total; i++) {
        e = preset_all_at(i, &k);
        if (e < NENGINES)
            seen |= 1u << e;
    }
    bad += check("PRESETS: the list holds every engine's presets but DIGITAL's", seen == all && NENG_SHOWN == NENGINES - 1u);
    go_page(GR_BROWSE);
    set_engine_of(TSEL, 0);
    for (i = 0, seen = 0; i < NENG_SHOWN; i++) {
        turn(EN_K2, 1);
        seen |= 1u << TSEL->eng_req;
    }
    bad += check("PRESETS KNOB 2: the engines in order, DIGITAL skipped, back to the first",
                 seen == all && TSEL->eng_req == 0u && eng_step(0, 1) == ENGI_FM6 && eng_step(ENGI_FM6, 1) == 2u &&
                 eng_step(ENGI_FM6, -1) == 0u && eng_step(0, -1) == ENGI_DRUMX && eng_step(ENGI_DRUMX, -1) == ENGI_DRUM);
    {   /* the display order (engines.c ENGINE_ORDER): every engine one can pick once; the PRESETS list follows it */
        static const char *const ORDER[] = {"ANALOG", "FM6", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
                                            "PHYS", "NOISE", "SLICE", "DRUM", "DRUM-X"};
        uint32_t last = 0xFFu, r = 0, n = 0;
        ok = NENG_SHOWN == NELEM(ORDER);
        for (i = 0; ok && i < NENG_SHOWN; i++)
            ok &= str_eq(ENGINES[eng_vis(i)]->name, ORDER[i]) && eng_rank(eng_vis(i)) == i && eng_ok(eng_vis(i));
        preset_all_pos(&total);
        for (i = 0; i < total; i++) {                  /* the list's engines, each once, in that order */
            e = preset_all_at(i, &k);
            if (e >= NENGINES || e == last)
                continue;
            ok &= n < NENG_SHOWN && e == eng_vis(n);
            n++;
            last = e;
            r++;
        }
        bad += check("engines shown ANALOG FM6 PHASE ... NOISE SLICE DRUM DRUM-X (ENGINE_ORDER); PRESETS lists them so",
                     ok && r == NENG_SHOWN);
    }
#if FELUCCA_LEGACY                                     /* ((2.0) the converter's: fm4_convert.c) */
    /* a user preset stored with engine 1: kept as it is, it loads as FM6 with the converted patch */
    for (i = 0; i < P_COUNT; i++)
        p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(p, 5);                           /* DIGITAL PAD */
    {
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_DIGITAL;
        r.np = P_COUNT;
        memcpy(r.name, "OLD PAD", 7);
        for (i = 0; i < P_COUNT; i++)
            up_set_value(&r, i, p[i]);
        up_put(7, &r);
    }
    fm4_convert(p, v);
    up_load(7);
    bad += check("a DIGITAL user preset loads as FM6: the converted patch its own (SLOT OWN), preset = FM6 PAD",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 4u && TSEL->p[P_E7] == FM6_OWN && !TSEL->p[P_E0] &&
                 !memcmp(fm6_patch[song.sel], v, FP_SIZE) && fm6_slot[song.sel] == FM6_OWN && TSEL->user == 8u);
    frame();
    bad += check("  .. and the main loop keeps that patch (not a factory one)", !memcmp(fm6_patch[song.sel], v, FP_SIZE));
    eng_list_pos(&total);
    bad += check("  the record stays DIGITAL in the bank, listed with FM6's sounds (EDIT KNOB 2)",
                 up_rec(7)->engine == ENGI_DIGITAL && up_engine(7) == ENGI_FM6 &&
                 total == ENGINES[ENGI_FM6]->npresets + 1u);
#else
    {   /* (2.0) a user preset stored with engine 1 (an editor of before's UP_PUT, a bank of before): OLD FORMAT, kept */
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_DIGITAL;
        r.np = P_COUNT;
        memcpy(r.name, "OLD PAD", 7);
        for (i = 0; i < P_COUNT; i++)
            up_set_value(&r, i, param_desc_of(ENGI_DIGITAL, i)->def);
        up_put(7, &r);
        bad += check("(2.0) a DIGITAL user preset: OLD FORMAT, not loaded or listed, its bytes kept",
                     !up_used(7) && up_old(7) && up_load(7) == 1 && !memcmp(up_rec(7), &r, sizeof r));
        up_put(7, 0);
    }
    set_engine_of(TSEL, ENGI_FM6);                     /* (below: a patch of the track's own, SLOT OWN) */
    memcpy(v, fm6_patch[song.sel], sizeof v);
    v[0] = (uint8_t)(v[0] == 99u ? 98u : 99u);
    fm6_set_patch(song.sel, v);
    fm6_adopt(song.sel);
    memcpy(v, fm6_patch[song.sel], sizeof v);
    (void)p; (void)ok;
#endif
    up_store(8, "AGAIN");
    {   /* ((FM6 LAB) the typed name goes into the patch's own too: the rest as converted) */
        uint8_t pk[FM6_PACKED], w[FP_SIZE + 1u], f[FP_SIZE + 1u];
        char an[16];
        bad += check("  saved again: an FM6 user preset with SLOT OWN and the converted patch beside it, named AGAIN",
                     up_rec(8)->engine == ENGI_FM6 && up_value(up_rec(8), p_sid(P_E7)) == FM6_OWN && !upf_get(8, pk) &&
                     (fm6_unpack(pk, w), !memcmp(w, v, FP_NAME)) && !memcmp(w + FP_NAME, "AGAIN     ", 10));
        fm6_load_slot(song.sel, 2);                    /* another patch on the track, then the preset again */
        up_load(8);
        frame();
        bad += check("  .. which loads back: the same patch (named AGAIN), SLOT OWN",
                     !memcmp(fm6_patch[song.sel], v, FP_NAME) && !memcmp(fm6_patch[song.sel] + FP_NAME, "AGAIN     ", 10) &&
                     TSEL->p[P_E7] == FM6_OWN && fm6_slot[song.sel] == FM6_OWN);
        memcpy(w, fm6_patch[song.sel], sizeof w);
        up_auto_name(an, ENGI_FM6, 9);
        up_store(9, an);                               /* the automatic name ("FM6 10"): the patch keeps its own */
        ok = !memcmp(fm6_patch[song.sel], w, FP_SIZE);
        fm6_load_slot(song.sel, 2);                    /* a factory patch, F3: a typed name leaves it F3 */
        fm6_adopt(song.sel);
        memcpy(f, fm6_patch[song.sel], sizeof f);
        up_store(10, "TYPED");
        ok &= TSEL->p[P_E7] == 2 && !memcmp(fm6_patch[song.sel], f, FP_SIZE);
        bad += check("  (FM6 LAB) the automatic name, or a factory patch (F3): the patch's own name stays", ok);
        up_put(9, 0); up_put(10, 0);                   /* (erased: as the tests after it found them) */
    }
#if !FELUCCA_LEGACY
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("(2.0) engine 1 asked for (the editor's SET G_ENGSEL or PRESET): FM6, its first preset",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 0u);
#else
    /* preset numbers of engine 1 */
    set_engine_of(TSEL, ENGI_DIGITAL);
    bad += check("engine 1 asked for: DIGITAL E.PIANO converted (FM6, PTCH TINE EP, the patch named E.PIANO)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 0u && !memcmp(fm6_patch[song.sel] + FP_NAME, "E.PIANO", 7));
    TSEL->eng_req = ENGI_DIGITAL;                      /* (the ISR's view: no other path does this) */
    apply_preset_to(TSEL, 1);
    bad += check("a DIGITAL preset number (apply_preset_to): its sound converted (BELL)",
                 TSEL->eng_req == ENGI_FM6 && TSEL->preset == 1u && !memcmp(fm6_patch[song.sel] + FP_NAME, "BELL", 4));
    for (i = 0; i < P_COUNT; i++)
        trk[2].p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
    fm4_preset_values(trk[2].p, 2);
    trk[2].eng_req = ENGI_DIGITAL;
    frame();
    bad += check("a track left on engine 1 any other way: the main loop converts it (BASS -> FM BASS)",
                 trk[2].eng_req == ENGI_FM6 && trk[2].preset == 2u);
    {   /* favourites of DIGITAL presets (settings PER4) -> the FM6 presets that cover them */
        persist_t pe;
        memset(&pe, 0, sizeof pe);
        pe.magic = PERSIST_MAGIC;
        pe.panel = PANEL_DEFAULT;
        pe.favorites.factory[ENGI_DIGITAL][0] = 1u << 5 | 1u << 7;   /* PAD, FUNK KEY */
        settings_import(&pe, (int)sizeof pe);
        bad += check("favourites: DIGITAL PAD / FUNK KEY -> FM6 PAD / PLUCK, engine 1's cleared",
                     favorite_has(ENGI_FM6, 4) && favorite_has(ENGI_FM6, 7) && !favorites.factory[ENGI_DIGITAL][0]);
        memset(&favorites, 0, sizeof favorites);
    }
#endif
    return bad;
}
#endif

/* #38: the keys of the notes the selected track's sequencer and ARP sound light while it plays */
static int test_play_leds(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    uint32_t i;
    ui_power_on();
    sel_set(0);
    t->step[0] = (step_t){.note = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.note = {100}, .n = 1, .time = ST_NOTE};
    trk[1].step[0] = (step_t){.note = {67}, .n = 1, .time = ST_NOTE};
    t->seq_active = trk[1].seq_active = 1;
    bad += check("stopped, nothing held: no key lit", key_leds(0) == 0u && play_leds() == 0u);
    seq_start();
    events_block(CTL);
    bad += check("playing: C4 and E4 of step 1 light keys 8 and 12 (from F3), track 2's G4 does not",
                 t->seq_n == 2u && trk[1].seq_n == 1u && key_leds(0) == (1u << 7 | 1u << 11));
    sel_set(1);
    bad += check("the selected track's notes: track 2's G4 on key 15", play_leds() == 1u << 14);
    sel_set(0);
    fm1_in.notes = 1u << 0;
    bad += check("a key held still lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    song.octave = -1;
    ok = play_leds() == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= play_leds() == 0u;                           /* (C4 and E4 below the keys an octave up) */
    song.octave = 0;
    t->p[P_TRANS] = 2;
    ok &= play_leds() == (1u << 5 | 1u << 9);
    t->p[P_TRANS] = 0;
    bad += check("the keys follow the octave and TRN; notes off the keyboard are not shown", ok);
    t->p[P_QUANT] = 1;                                 /* SNAP in C major: C#4 rounds down to C4 too */
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    ok = kb_map(t, 8) == 60u && play_leds() == (1u << 7 | 1u << 11);
    t->p[P_QUANT] = t->p[P_SCALE] = 0;
    bad += check("QNT SNAP: only the key of the note itself lights, not the keys rounding onto it", ok);
    t->arp_note = 65;
    bad += check("the ARP's note lights its key too", play_leds() == (1u << 7 | 1u << 11 | 1u << 12));
    t->arp_note = 0;
    events_block(div_samples((uint32_t)t->p[P_SDIV]) + CTL);
    bad += check("a note above the keyboard (G#7) lights nothing", t->seq_n == 1u && t->seq_notes[0] == 100u &&
                 play_leds() == 0u);
    transport_req = 2;
    events_block(CTL);
    bad += check("stopped: the notes end, their keys go dark", !song.playing && key_leds(0) == 0u);
    /* the layer, the grid and NAME keep their keys */
    seq_start();
    events_block(CTL);
    ui.layer = 1;
    ok = key_leds(0) == layer_leds(0);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    transport_req = 2; events_block(CTL);
    seq_start(); events_block(CTL);
    i = play_leds();
    ok &= t->seq_n == 1u && i != 0u && kb_map(t, (uint32_t)__builtin_ctz(i)) == DRUM_LANE_NOTE[DV_KICK];
    open_family(FAM_SEQ);
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("the layer's map and the DRUM grid keep their keys; elsewhere a kit's hits light the keys that play them", ok);
    transport_req = 2; events_block(CTL);
    return bad;
}

/* Discussion #89: on SEQ > STEP (the piano roll) while stopped the keys of the cursor step's notes light, at the
 * octave now (the lowest key that gives each); with the keys held; playing, what plays instead (#38); not on
 * other pages, not for a REST step; the DRUM grid keeps its own map */
static int test_cursor_step_leds(void)
{
    int bad = 0, ok;
    track_t *t;
    ui_power_on();
    sel_set(0);
    t = TSEL;
    t->step[0] = (step_t){.note = {60, 64}, .n = 2, .time = ST_NOTE};
    t->step[1] = (step_t){.note = {100}, .n = 1, .time = ST_NOTE};
    t->step[2] = (step_t){.note = {62}, .n = 1, .time = ST_REST};
    t->step[3] = (step_t){.note = {67}, .n = 1, .time = ST_TIE};
    t->seq_active = 1;
    bad += check("#89 HOME, stopped: no step's keys lit", key_leds(0) == 0u);
    go_page(GR_ROLL);
    frame();
    bad += check("#89 NOTES, stopped: the cursor step's C4 and E4 light keys 8 and 12",
                 ui.cursor == 0u && key_leds(0) == (1u << 7 | 1u << 11));
    song.octave = -1;
    ok = key_leds(0) == (1u << 19 | 1u << 23);
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (below the keys an octave up) */
    song.octave = 0;
    bad += check("#89 the step's keys follow the octave; notes off the keyboard are not shown", ok);
    fm1_in.notes = 1u << 0;
    bad += check("#89 a key held lights with them", key_leds(0) == (1u << 0 | 1u << 7 | 1u << 11));
    fm1_in.notes = 0;
    turn(EN_PRESET, 1);
    ok = ui.cursor == 1u && key_leds(0) == 0u;          /* G#7: off the keyboard */
    turn(EN_PRESET, 1);
    ok &= ui.cursor == 2u && key_leds(0) == 0u;         /* a REST */
    turn(EN_PRESET, 1);
    ok &= ui.cursor == 3u && key_leds(0) == 1u << 14;   /* a TIE carries its G4 ((2.0) after a REST it holds no
                                                         * note: PRESETS stops on it) */
    turn(EN_PRESET, 1);
    ok &= ui.cursor == 4u && key_leds(0) == 0u;         /* empty */
    bad += check("#89 PRESETS moves the cursor and the keys follow its step (REST, empty, off the keys: dark)", ok);
    turn(EN_PRESET, -4);
    seq_start();
    events_block(CTL);
    bad += check("#89 playing: the keys show what plays, not the cursor step", song.playing && ui.cursor == 0u &&
                 key_leds(0) == play_leds());
    turn(EN_PRESET, 3);
    bad += check("#89   (the cursor on G4's step changes nothing while playing)", key_leds(0) == play_leds() &&
                 !((key_leds(0) >> 14) & 1u));
    transport_req = 2;
    events_block(CTL);
    frame();
    bad += check("#89 stopped again: the cursor step's keys", !song.playing && key_leds(0) == 1u << 14);
    go_page(GR_FEEL);
    frame();
    bad += check("#89 FEEL (same cursor): no step keys", key_leds(0) == 0u);
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    t->step[0] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};
    go_page(GR_ROLL);
    frame();
    bad += check("#89 the DRUM grid keeps its map", grid_on() && key_leds(0) == grid_leds());
    return bad;
}

/* Discussion #81: the notes MIDI IN holds on the selected track light their keys as the sequencer's do: USB and TRS,
 * routed by ROUT; at the octave now, the lowest key; off the keyboard nothing; the pedal holds them; the layer's map
 * and the DRUM grid keep their keys */
static void usb_note(uint32_t ch, uint32_t note, uint32_t vel)   /* a USB-MIDI packet, as usb.c queues it */
{
    uint32_t st = (vel ? 0x90u : 0x80u) | ch;
    midi_in_event((st >> 4) | st << 8 | note << 16 | vel << 24);
    events_block(CTL);
}
static void trs_note(uint32_t a, uint32_t b, uint32_t c)        /* TRS MIDI IN: midi_uart.c's parser */
{
    um_byte(a); um_byte(b); um_byte(c);
    events_block(CTL);
}
static int test_midi_leds(void)
{
    int bad = 0, ok;
    ui_power_on();
    memset(&um, 0, sizeof um);
    sel_set(0);
    usb_note(0, 60, 100);                              /* channel 1 -> T1 (ROUT CH1-4) */
    bad += check("#81 USB MIDI C4 into the selected track lights key 8 (C4)", midi_sel_on[0][60] == 1u &&
                 key_leds(0) == 1u << 7 && play_leds() == 1u << 7);
    usb_note(1, 67, 100);                              /* channel 2 -> T2: not the selected track */
    ok = key_leds(0) == 1u << 7;
    sel_set(1);
    ok &= key_leds(0) == 1u << 14;
    sel_set(0);
    bad += check("  another track's MIDI notes light nothing; selected, its G4 lights key 15", ok);
    usb_note(1, 67, 0);
    usb_note(0, 60, 0);
    bad += check("  note-off: dark", key_leds(0) == 0u && !midi_owners[0]);
    trs_note(0x90, 64, 90);                           /* TRS: E4 on channel 1 */
    ok = key_leds(0) == 1u << 11;
    trs_note(0x80, 64, 0);
    bad += check("  TRS MIDI IN: E4 lights key 12 while held", ok && key_leds(0) == 0u);
    song.g[G_ROUTE] = 1;                               /* ROUT SEL: every channel to the selected track */
    sel_set(2);
    usb_note(9, 62, 100);
    ok = midi_sel_on[9][62] == 3u && key_leds(0) == 1u << 9;
    usb_note(9, 62, 0);
    song.g[G_ROUTE] = 0;
    sel_set(0);
    bad += check("  ROUT SEL: channel 10 into the selected track lights its key", ok && key_leds(0) == 0u);
    usb_note(0, 60, 100);
    song.octave = -1;
    ok = key_leds(0) == 1u << 19;
    song.octave = 1;
    ok &= key_leds(0) == 0u;                            /* (C4 below the keys an octave up) */
    song.octave = 0;
    usb_note(0, 60, 0);
    usb_note(0, 100, 100);                             /* G#7: above the keyboard */
    usb_note(0, 40, 100);                              /* E2: below it */
    ok &= key_leds(0) == 0u && midi_owners[0] == 2u;
    usb_note(0, 100, 0);
    usb_note(0, 40, 0);
    bad += check("  the keys follow the octave; notes off the keyboard are not shown", ok);
    trk[0].p[P_QUANT] = 1;                             /* SNAP in C major: C#4 rounds down to C4 too */
    trk[0].p[P_SCALE] = 1;
    trk[0].p[P_ROOT] = 0;
    usb_note(0, 60, 100);
    ok = kb_map(&trk[0], 8) == 60u && key_leds(0) == 1u << 7;
    usb_note(0, 60, 0);
    trk[0].p[P_QUANT] = trk[0].p[P_SCALE] = 0;
    bad += check("  QNT SNAP: only the lowest key giving the note lights", ok && key_leds(0) == 0u);
    midi_event(0xB0, 0, 64, 127);                      /* the pedal down: a released note still sounds */
    usb_note(0, 65, 100);
    usb_note(0, 65, 0);
    ok = key_leds(0) == 1u << 12;
    midi_event(0xB0, 0, 64, 0);
    bad += check("  a note the pedal holds stays lit until the pedal is up", ok && key_leds(0) == 0u);
    fm1_in.notes = 1u << 0;                            /* with a key held and the sequencer */
    usb_note(0, 60, 100);
    ok = key_leds(0) == (1u << 0 | 1u << 7);
    fm1_in.notes = 0;
    trk[0].step[0] = (step_t){.note = {64}, .n = 1, .time = ST_NOTE};
    trk[0].seq_active = 1;
    seq_start();
    events_block(CTL);
    ok &= key_leds(0) == (1u << 7 | 1u << 11);
    bad += check("  with a key held and the sequencer's notes: all of them lit", ok);
    ui.layer = LAYER_SCL;
    {
        uint32_t b1, b2;
        ok = key_leds(&b1) == layer_leds(&b2) && b1 == b2 && b1;
    }
    ui.layer = 0;
    open_family(FAM_SEQ);                              /* STEP on a melodic track: the keys as elsewhere */
    frame();
    ok &= !grid_on() && (key_leds(0) & (1u << 7));
    set_engine_of(&trk[0], ENGI_DRUM);
    trk[0].engine = trk[0].eng_req;
    frame();
    ok &= grid_on() && key_leds(0) == grid_leds();
    bad += check("  the layer's map and the DRUM grid keep their keys over the MIDI notes", ok);
    transport_req = 2;
    events_block(CTL);
    usb_note(0, 60, 0);
    return bad;
}

/* #119: what can be pressed breathes (hal/fm1_input.h fm1_led_breath: dark .. ~60 % of lit), never a hard blink: every
 * layer, held and locked (double tap), in every MENU > LEDS mode: its keys that can be pressed and its button in the
 * breath set only (neither lit nor the glow), the ones in effect lit, the picture the same at any time (no 250 ms
 * toggle left); OCT+ in a dialog likewise; closing the layer leaves nothing breathing; the glow's level and
 * the breath's peak follow LEDS (fm1_led_dim_level: DIM LO the darker glow and ~30 %, else DIM HI's and ~60 %) */
static uint32_t breath_keys(void)                     /* the breathing keys, bit k = key k (the real positions) */
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((fm1_led_breath[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static uint32_t lit_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++) {
        uint8_t q = led_pos[14u + k];
        m |= (uint32_t)(q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u)) << k;
    }
    return m;
}
static int led_of(const uint8_t *l, uint32_t id)
{
    uint8_t q = led_pos[id];
    return q != 0xFF && ((l[q >> 3] >> (q & 7u)) & 1u);
}
static int test_breath(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static const uint8_t MODES[4] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0, ok = 1, okl = 1, okm = 1, oko = 1;
    uint32_t i, m, lk, t, held;
    ui_power_on();
    ui_leds();
    for (i = 0; i < 41u; i++)                           /* (the stub key map has none: 4 LEDs a column) */
        led_pos[i] = (uint8_t)((i / 4u) << 3 | (1u + i % 4u));
    for (i = 0; i < 4u; i++)
        for (lk = 0; lk < 2u; lk++)
            for (m = 0; m < 4u; m++) {
                uint32_t br0 = 0, lit0 = 0, want_br, want_lit, hk = LL[i] == LAYER_GLO ? white(1) : white(2);
                ui_power_on();
                settings_leds = MODES[m];
                go_title("ENV"); frame();
                if (lk) {                                   /* locked (#83), then a key held */
                    shift_lock(LB[i]); frames(100);
                    okl &= ui.lock == LL[i];
                } else {
                    btn_down(LB[i]);
                }
                key_down(hk); frame();
                okl &= ui.layer == LL[i];
                want_lit = layer_leds(&want_br);
                held = LL[i] == LAYER_FX || LL[i] == LAYER_GLO;  /* (HOLD keys: lit while held) */
                for (t = 0; t < 8u; t++) {                  /* 0 .. 1.75 s: the same picture */
                    fm1_ms += 250u;
                    ui_leds();
                    if (!t) {
                        br0 = breath_keys();
                        lit0 = lit_keys(fm1_led);
                    }
                    ok &= breath_keys() == br0 && lit_keys(fm1_led) == lit0 && !lit_keys(fm1_led_dim) &&
                          led_of(fm1_led_breath, panel.btn[LB[i]]) && !led_of(fm1_led, panel.btn[LB[i]]) &&
                          !led_of(fm1_led_dim, panel.btn[LB[i]]);
                }
                ok &= br0 == want_br && lit0 == want_lit && br0 && !(br0 & lit0);
                ok &= !held || (((lit0 >> hk) & 1u) && !((br0 >> hk) & 1u));   /* held: lit, not breathing */
                okm &= host_dim_lo == (MODES[m] == LEDS_DIM_LO);
                key_up(hk); frame();
                if (lk)
                    press(LB[i]);
                else
                    btn_up(LB[i]);
                frames(64);
                ui_leds();
                for (t = 0; t < FM1_NCOL; t++)
                    okl &= !ui.layer && !fm1_led_breath[t];
            }
    settings_leds = LEDS_DIM;
    bad += check("#119 FX GLO SCL EDIT, held and locked, every LEDS mode: the keys that can be pressed breathe", ok);
    bad += check("  ..held / in effect lit, never the glow, the layer's button breathes, steady over time", ok);
    bad += check("  ..closed: nothing breathes", okl);
    bad += check("  ..the breath's peak follows LEDS (DIM LO ~30 % of lit, else ~60 %)", okm);
    /* OCT+ in a dialog: breathing, OCT- lit, in every mode */
    for (m = 0; m < 4u; m++) {
        ui_power_on();
        settings_leds = MODES[m];
        go_title("ENV"); frame();
        ui.confirm = CF_CLEAR_SEQ;
        for (t = 0; t < 4u; t++) {
            fm1_ms += 250u;
            ui_leds();
            oko &= led_of(fm1_led_breath, panel.btn[B_OCTUP]) && !led_of(fm1_led, panel.btn[B_OCTUP]) &&
                   !led_of(fm1_led_dim, panel.btn[B_OCTUP]) && led_of(fm1_led, panel.btn[B_OCTDN]) ==
                   (MODES[m] != LEDS_INV) && !led_of(fm1_led_breath, panel.btn[B_OCTDN]);
        }
        ui.confirm = 0;
        ui_leds();
        oko &= !led_of(fm1_led_breath, panel.btn[B_OCTUP]);
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    bad += check("#119 a dialog: OCT+ breathes (not lit, not the glow), OCT- lit (INV: dark), every LEDS mode", oko);
    ui_power_on();
    return bad;
}

/* #35: every idle button and key glows dim, the active ones are lit; a map of the keys' own stays lit / dark */
static int test_idle_glow(void)
{
    int bad = 0, ok;
    ui_power_on();
    ui_leds();
    memset(led_pos, 0xFF, sizeof led_pos);
    led_pos[panel.btn[B_HOME]] = 0u << 3 | 1u;
    led_pos[panel.btn[B_ENV]] = 0u << 3 | 2u;
    led_pos[panel.btn[B_PLAY]] = 0u << 3 | 3u;
    led_pos[14u + 0u] = 1u << 3 | 1u;              /* keys 1 and 2 */
    led_pos[14u + 1u] = 1u << 3 | 2u;
    go_home();
    ui_leds();
    bad += check("HOME: its button lit, ENV and PLAY (stopped) glow, the keys glow, none lit",
                 fm1_led[0] == 2u && fm1_led_dim[0] == 14u && fm1_led[1] == 0u && fm1_led_dim[1] == 6u);
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok = fm1_led[0] == 2u && (fm1_led_dim[0] & 8u) == 0u && fm1_led[8] == 2u && fm1_led[1] == 2u && fm1_led_dim[1] == 6u;
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("a key held lit over the glow; PLAY running: its green, its own LED dark", ok);
    ui.layer = 1;
    ui_leds();
    ok = fm1_led_dim[1] == 0u && fm1_led_dim[0] == 14u;
    ui.layer = 0;
    ui_leds();
    ok &= fm1_led_dim[1] == 6u;
    bad += check("a layer's map: the keys lit or dark (no glow), the buttons still glow", ok);
    /* MENU > LEDS INV: the active ones dark, the idle ones lit (stock), no glow; a breath as in DIM (#119); a map
     * of the keys' own as in DIM */
    settings_leds = LEDS_INV;
    ui_leds();
    ok = fm1_led[0] == 12u && fm1_led_dim[0] == 0u && fm1_led[1] == 6u && fm1_led_dim[1] == 0u;   /* HOME dark */
    fm1_in.notes = 1u;
    song.playing = 1;
    ui_leds();
    ok &= fm1_led[0] == 4u && fm1_led_dim[0] == 0u && fm1_led[1] == 4u && fm1_led_dim[1] == 0u && fm1_led[8] == 2u;   /* PLAY, key 1 dark; green */
    fm1_in.notes = 0;
    song.playing = 0;
    bad += check("LEDS INV: the page's button, PLAY running and a key held go dark, the rest lit, no glow", ok);
    ui.layer = 1;
    led_pos[panel.btn[layer_btn()]] = 0u << 3 | 4u;   /* the layer's button */
    {
        uint32_t seen = 0, t;
        for (t = 0; t < 4u; t++) {                  /* the layer's button breathes: never lit (inverted), never the glow */
            fm1_ms = t * 250u;
            ui_leds();
            seen |= ((fm1_led[0] >> 4) & 1u ? 1u : 2u) | ((fm1_led_dim[0] >> 4) & 1u ? 4u : 0u) |
                    ((fm1_led_breath[0] >> 4) & 1u ? 8u : 0u);
        }
        ok = seen == 10u && fm1_led_dim[1] == 0u && fm1_led[1] == (uint8_t)((layer_leds(0) & 3u) << 1);
    }
    ui.layer = 0;
    fm1_ms = 0;
    bad += check("LEDS INV: the layer's button breathes (not inverted); a layer's map stays lit / dark as in DIM", ok);
    settings_leds = LEDS_DIM;
    led_pos_init();
    return bad;
}

/* "On the step sequencer all the LEDs go out": SEQ > STEP, the piano roll and the DRUM grid, stopped and playing,
 * in every LEDS mode, with the LEDs where the hardware has them (hal/fm1_input.h FM1_KEYMAP). The buttons keep
 * their glow and their active states; on the piano roll the keys glow under the held and playing ones; on the
 * grid its map is lit and its keys that do something glow (DIM LO / HI). Before: keys_own() took the grid's keys
 * out of the glow, so in DIM 26 of 27 keys of an empty 16-step grid went fully dark */
/* the white keys of the grid's steps (bit p = step p of the page shown), bit k = key k */
static uint32_t step_keys(uint32_t steps)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        if (!key_black(k) && key_place(k) < 16u && ((steps >> key_place(k)) & 1u))
            m |= 1u << k;
    return m;
}
static int test_step_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    int bad = 0;
    uint32_t id, p, r, mi, drum, run, k;
    ui_power_on();
    ui_leds();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (drum = 0; drum < 2u; drum++)
        for (run = 0; run < 2u; run++)
            for (mi = 0; mi < LEDS_COUNT; mi++) {
                uint32_t m = MODES[mi], glow = m == LEDS_DIM || m == LEDS_DIM_LO, keys_lit = 0, keys_dim = 0, btn_bad = 0;
                uint32_t want_dim = 0, kc, keys;
                char what[128];
                ui_power_on();
                sel_set(0);
                if (drum) {
                    set_engine_of(TSEL, ENGI_DRUM);
                    TSEL->engine = TSEL->eng_req;
                    TSEL->p[P_SLEN] = 16;
                    TSEL->step[2] = (step_t){.hit = 1u << DV_KICK, .time = ST_NOTE};   /* one hit, on step 3 */
                } else {
                    TSEL->step[0] = (step_t){.note = {60}, .n = 1, .time = ST_NOTE};   /* C4 on step 1 */
                }
                TSEL->seq_active = 1;
                settings_leds = (uint8_t)m;
                open_family(FAM_SEQ);
                frame();
                for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) {   /* ((2.0) NOTES: a drum
                                                         * track's grid) */
                    open_family(FAM_SEQ);
                    frame();
                }
                if (run) {
                    seq_start();
                    events_block(CTL);
                }
                ui_leds();
                for (k = 0; k < NB; k++) {                  /* a button: lit if active, else glow / dark / lit */
                    uint8_t q = led_pos[panel.btn[k]];
                    uint32_t lit = (fm1_led[q >> 3] >> (q & 7u)) & 1u, dim = (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u;
                    int active = k == B_SEQ || (k == B_PLAY && run);
                    btn_bad += m == LEDS_INV ? lit == (uint32_t)active || dim :
                               active ? (k == B_PLAY ? lit || dim : !lit) : lit || dim != glow;
                }
                keys = key_leds(0);
                for (k = 0; k < 27u; k++) {
                    uint8_t q = led_pos[14u + k];
                    keys_lit += (fm1_led[q >> 3] >> (q & 7u)) & 1u;
                    keys_dim += ((fm1_led_dim[q >> 3] & ~fm1_led[q >> 3]) >> (q & 7u)) & 1u;
                }
                kc = (uint32_t)__builtin_popcount(keys);
                if (glow)
                    want_dim = (uint32_t)__builtin_popcount((drum ? grid_glow() : 0x7FFFFFFu) & ~keys);
                else if (drum)                              /* (OFF, INV: the grid's beats alone glow) */
                    want_dim = (uint32_t)__builtin_popcount(grid_beats() & ~keys);
                snprintf(what, sizeof what, "STEP %s, %s, LEDS %s: buttons and keys as the mode says (%u lit, %u glow)",
                         drum ? "DRUM grid" : "piano roll", run ? "playing" : "stopped", LEDS_NAME[m],
                         (unsigned)keys_lit, (unsigned)keys_dim);
                bad += check(what, !btn_bad && host_dim_lo == (m == LEDS_DIM_LO) &&
                                       grid_on() == (int)drum && (!run || kc) &&
                                       keys_dim == want_dim && keys_lit == (m == LEDS_INV && !drum ? 27u - kc : kc) &&
                                       (!glow || keys_lit + keys_dim >= 25u) &&
                                       ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) == run);
                bad += check("  the mid level: the grid's beats in DIM HI / LO only, never a lit key",
                             lit_keys(fm1_led_mid) == (glow && drum ? grid_beats() & ~keys : 0u) &&
                             !(lit_keys(fm1_led_mid) & lit_keys(fm1_led)));
                if (run) {
                    transport_req = 2;
                    events_block(CTL);
                }
            }
    {   /* the DRUM grid's beats (ui_input.c grid_beats): "5111 5111 5111 5111", the first step of each four brighter
         * where no hit shows, the hits lit and the playhead as before; LEN cuts them, a page shows its own; groups of
         * four whatever LEN / DIV; DIM HI / LO: fm1_led_mid over the glow, OFF / INV: the glow alone */
        uint32_t want, beats, keys, kick = 1u << DV_KICK, ok = 1, mi;
        for (mi = 0; mi < LEDS_COUNT; mi++) {
            uint32_t m = MODES[mi], glow = m == LEDS_DIM || m == LEDS_DIM_LO;
            char what[128];
            ui_power_on();
            sel_set(0);
            set_engine_of(TSEL, ENGI_DRUM);
            TSEL->engine = TSEL->eng_req;
            TSEL->p[P_SLEN] = 16;
            TSEL->step[4] = (step_t){.hit = kick, .time = ST_NOTE};   /* a kick on step 5 (a beat) and on step 3 */
            TSEL->step[2] = (step_t){.hit = kick, .time = ST_NOTE};
            TSEL->seq_active = 1;
            settings_leds = (uint8_t)m;
            open_family(FAM_SEQ);
            frame();
            for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) {
                open_family(FAM_SEQ);
                frame();
            }
            ui.lane = 0;
            ui_leds();
            keys = key_leds(0);
            beats = grid_beats();
            want = step_keys(1u << 0 | 1u << 8 | 1u << 12);   /* steps 1, 9, 13 (5 holds a hit: lit) */
            ok = beats == want && (keys & step_keys(1u << 4 | 1u << 2)) == step_keys(1u << 4 | 1u << 2) &&
                 lit_keys(fm1_led_mid) == (glow ? want : 0u) && (lit_keys(fm1_led_dim) & want) == want &&
                 (glow || !(lit_keys(fm1_led_dim) & ~want)) && host_dim_lo == (m == LEDS_DIM_LO);
            snprintf(what, sizeof what, "DRUM grid beats, LEDS %s: steps 1 9 13 %s, the hits 3 5 lit", LEDS_NAME[m],
                     glow ? "at the mid level over the glow" : "glow (nothing else does)");
            bad += check(what, ok);
            if (m != LEDS_DIM)
                continue;
            ui.lane = 1;                                /* another lane: none of its hits there, 1 5 9 13 */
            ok = grid_beats() == step_keys(0x1111u);
            ui.lane = 0;
            TSEL->p[P_SLEN] = 12;                       /* LEN 12 (3/4): 1 5 9; 13 is past it */
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 8);
            TSEL->p[P_SLEN] = 16; TSEL->p[P_SDIV] = 4;  /* (DIV changes nothing: steps are counted) */
            ok &= grid_beats() == want;
            song.playing = 1; TSEL->seq_idx = 8;        /* the playhead on step 9: it lights it, no mid there */
            ui_leds();
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 12) && (lit_keys(fm1_led) & step_keys(1u << 8)) &&
                  !(lit_keys(fm1_led_mid) & step_keys(1u << 8));
            song.playing = 0;
            TSEL->p[P_SLEN] = 32;                       /* page 2: its own steps 17 21 25 29 */
            TSEL->step[20] = (step_t){.hit = kick, .time = ST_NOTE};
            ui.bank = 1; ui.cursor = 16;
            ok &= grid_beats() == step_keys(1u << 0 | 1u << 8 | 1u << 12);
            bad += check("  another lane, LEN 12, DIV, the playhead, page 2: the beats as the steps show", ok);
        }
    }
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* Discussion #127 (1.2): MENU > SCALE LEDS ON, outside the keys' own maps the keys of the selected track's scale glow
 * (the others dark), the root's at the mid level; the keys held / playing lit over it; LEDS OFF / DIM LO / DIM HI
 * alike, INV turned round (root lit, scale mid, the others glowing, the active dark); TRN; QNT WHITE: the white
 * keys; a DRUM track, a layer, the default OFF: as before */
/* MENU > DISPLAY > SCREEN OFF (1.1.5, ui.c scr_*): the screen and its backlight off after NEVER / 5 / 15 / 30 (default)
 * / 60 minutes without panel input; the press, key or turn that wakes it does nothing else; the dialogs, the count-in
 * and the UPDATE MODE countdown keep it on and wake it; the sound goes on as if it were on (forked sessions, bit for
 * bit); the setting saved (a spare favorites byte, 0 = 30 MIN) and offered in the MENU */
static void scr_idle(uint32_t ms)                  /* nobody touches it for ms (a frame a minute, then frames) */
{
    while (ms > 60000u) { fm1_ms += 60000u - 16u; host_ticks += 60000u * 1000u; frame(); ms -= 60000u; }
    frames(ms);
}
static int scr_woke(uint32_t from)                 /* since host_pw_n was `from`: SLPOUT >= 120 ms after the SLPIN,
                                                    * DISPON + backlight >= 120 ms after the SLPOUT, nothing else */
{
    return host_pw_n == from + 2u && host_pw[from] == 1u && host_pw[from + 1u] == 2u && host_bl &&
           host_pw_ms[from] - host_pw_ms[from - 1u] >= 120u && host_pw_ms[from + 1u] - host_pw_ms[from] >= 120u &&
           scrn.st == SCR_ON;
}
static uint64_t scr_session(uint32_t setting, uint32_t *flags)
{
    int32_t o[2u * CTL];
    uint64_t h = 1469598103934665603ull, e = 0;
    uint32_t f, b, i, off = 0;
    ui_power_on();
    scr_put(setting);
    song.master_q12 = 4096;
    my_steps(&trk[0]);
    transport_req = 1;
    for (f = 0; f < 400u; f++) {
        if (f == 50u)
            fm1_ms += 6u * 60000u;                 /* six minutes untouched: 5 MIN goes dark */
        frame();
        off |= !host_bl;
        for (b = 0; b < 4u; b++) {
            mix_block(o, CTL);
            for (i = 0; i < 2u * CTL; i++) {
                h = (h ^ (uint32_t)o[i]) * 1099511628211ull;
                e += (uint64_t)(o[i] < 0 ? -o[i] : o[i]);
            }
        }
    }
    *flags = off | (song.playing ? 2u : 0u) | (e ? 4u : 0u);
    stop_transport();
    return h;
}
static uint64_t scr_session_fork(uint32_t setting, uint32_t *flags)   /* from the same state each time */
{
    int fd[2];
    uint64_t r[2] = {0, 0};
    pid_t p;
    if (pipe(fd))
        return 0;
    p = fork();
    if (!p) {
        uint32_t fl = 0;
        r[0] = scr_session(setting, &fl);
        r[1] = fl;
        if (write(fd[1], r, sizeof r) != sizeof r) _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], r, sizeof r) != sizeof r) r[0] = 0;
    close(fd[0]);
    waitpid(p, 0, 0);
    *flags = (uint32_t)r[1];
    return r[0];
}
static int test_screen_off(void)
{
    int bad = 0, ok;
    uint32_t n, rows, i;
    int16_t *vp, v0;

    ui_power_on();
    ok = ui_scr == 0u && scr_get() == 3u && SCR_MIN[scr_get()] == 30u && str_eq(MI_NAME[MI_SCROFF], "SCREEN OFF") &&
         MI_TAB[MI_SCROFF] == MTAB_DISPLAY && menu_n(MI_SCROFF) == 5u && str_eq(menu_vname(MI_SCROFF, 0), "NEVER") &&
         str_eq(menu_vname(MI_SCROFF, 1), "5 MIN") && str_eq(menu_vname(MI_SCROFF, 2), "15 MIN") &&
         str_eq(menu_vname(MI_SCROFF, 3), "30 MIN") && str_eq(menu_vname(MI_SCROFF, 4), "60 MIN") &&
         menu_get(MI_SCROFF) == 3u;
    bad += check("SCREEN OFF: MENU > DISPLAY row, NEVER 5 15 30 60 MIN, 30 MIN by default (stored 0)", ok);

    host_pw_n = 0;
    scr_idle(30u * 60000u - 2000u);
    ok = host_pw_n == 0u && host_bl && scrn.st == SCR_ON;
    scr_idle(2000u);
    ok &= host_pw_n == 1u && host_pw[0] == 0u && !host_bl && scrn.st == SCR_OFF && kb_asleep;
    rows = host_blit_rows;
    ui_message("SAVED");
    scr_idle(5000u);
    ok &= host_blit_rows == rows && host_pw_n == 1u && !ui.msg_t;
    bad += check("SCREEN OFF: 30 MIN untouched -> backlight off, DISPOFF + SLPIN once; dark: nothing drawn", ok);

    n = host_pw_n;
    ok = ui.home && !ui.menu;
    btn_down(B_ENV); frame();                         /* a page button wakes it, does not open its page */
    ok &= scrn.st == SCR_SLPOUT && host_pw_n == n + 1u && host_pw[n] == 1u;   /* (SLPIN long ago: SLPOUT at once) */
    frames(100u);
    ok &= host_pw_n == n + 1u && host_blit_rows == rows;   /* (dark until 120 ms after it) */
    rows = host_blit_rows;
    frames(200u);
    ok &= scr_woke(n) && host_blit_rows - rows >= 200u;   /* (the frame before DISPON redrew it all) */
    btn_up(B_ENV); frame(); frames(100u);
    ok &= ui.home && !ui.menu && !ui.layer && !scrn.eat && !kb_asleep;
    press(B_ENV);                                     /* awake: the button works again */
    ok &= !ui.home && cur_page()->fam == FAM_ENV;
    bad += check("SCREEN OFF: a button wakes it (SLPOUT, 120 ms, the page redrawn, DISPON + backlight), swallowed", ok);

    go_home();
    for (i = 0; i < 3u; i++) {                        /* HOME, SAVE, REC held across the wake: no menu, undo, clear */
        static const uint32_t B[3] = {B_HOME, B_SAVE, B_REC};
        scr_idle(31u * 60000u);
        n = host_pw_n;
        ok = !host_bl;
        hold(B[i]); frames(300u);
        ok &= scr_woke(n) && ui.home && !ui.menu && !ui.confirm && !song.rec && !song.playing && !transport_req;
        bad += check(i == 0 ? "SCREEN OFF: HOME held to wake: no menu, no HOME tap after" :
                     i == 1 ? "SCREEN OFF: SAVE held to wake: no undo, no SAVE page" :
                              "SCREEN OFF: REC held to wake: nothing armed, no clear, no transport", ok);
    }

    scr_idle(31u * 60000u);
    n = host_pw_n;
    ok = !host_bl && !gates();
    key_down(white(0)); frame();                      /* a key: wakes it, no note (seq.c kb_asleep) */
    ok &= !gates();
    frames(300u);
    ok &= scr_woke(n) && !gates();
    key_up(white(0)); frame(); frames(50u);
    ok &= !gates() && !kb_asleep;
    key_down(white(0)); frame();
    ok &= gates() == 1u;                              /* awake: the key plays */
    key_up(white(0)); frame();
    bad += check("SCREEN OFF: a key wakes it silently (no note on, none off), the next one plays", ok);

    scr_idle(31u * 60000u);
    n = host_pw_n;
    home_param(0, &vp);
    v0 = *vp;
    for (i = 0; i < 20u; i++) turn(EN_K1, 1);        /* a knob turned to wake it, still turning (on past the wake) */
    ok = scr_woke(n) && *vp == v0;
    turn(EN_PRESET, 1); turn(EN_ALGO, 1); turn(EN_SELECT, 1); turn(EN_K4, -1);
    ok &= *vp == v0 && song.sel == 0u && song.g[G_BPM] == GP[G_BPM].def && scrn.eat;
    frames(400u);                                     /* at rest SCR_EAT_MS: the knobs are the page's again */
    turn(EN_K1, 1);
    ok &= *vp != v0;
    bad += check("SCREEN OFF: a knob wakes it; turns until it rests 0.3 s change nothing (K1..4, PRESETS, ALGO, SELECT)", ok);

    menu_put(MI_SCROFF, 0u);                         /* NEVER */
    n = host_pw_n;
    scr_idle(3u * 3600000u);
    ok = host_pw_n == n && host_bl && ui_scr == 3u;
    menu_put(MI_SCROFF, 1u);                         /* 5 MIN */
    scr_idle(4u * 60000u + 50000u);
    ok &= host_pw_n == n && host_bl;
    scr_idle(15000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    press(B_PLAY); frames(300u);                      /* PLAY wakes it, does not start the transport */
    ok &= scr_woke(n + 1u) && !song.playing && !transport_req;
    bad += check("SCREEN OFF: NEVER stays on for hours; 5 MIN goes dark after 5; PLAY to wake starts nothing", ok);

    n = host_pw_n;
    ui.confirm = CF_CLEAR_SEQ; ui.force = 1;          /* a dialog: never dark */
    scr_idle(20u * 60000u);
    ok = host_pw_n == n && host_bl;
    ui.confirm = 0; ui.force = 1;
    cin_left = 1000u;                                 /* the count-in */
    scr_idle(20u * 60000u);
    ok &= host_pw_n == n && host_bl;
    cin_left = 0;
    scr_idle(6u * 60000u);
    ok &= host_pw_n == n + 1u && !host_bl;
    ui.confirm = CF_CLEAR_SEQ; ui.force = 1;          /* one opening while dark (the editor, MIDI): it wakes */
    frames(300u);
    ok &= scr_woke(n + 1u) && ui.confirm == CF_CLEAR_SEQ;
    ui.confirm = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    ui.uboot = 3; ui.force = 1;                       /* the UPDATE MODE countdown */
    frames(300u);
    ok &= scr_woke(n);
    ui.uboot = 0; ui.force = 1;
    scr_idle(6u * 60000u);
    n = host_pw_n;
    scr_wake_now();                                   /* main.c: the crash screen, UPDATE, UBOOT */
    ok &= host_pw_n == n + 1u && host_pw[n] == 3u && host_bl && scrn.st == SCR_ON;
    bad += check("SCREEN OFF: a dialog, the count-in, UPDATE MODE keep it on and wake it; the crash screen at once", ok);

    {   /* the setting with the settings: stored ^ 3 in favorites.factory[15][27], 0 (older settings) = 30 MIN */
        sks_t p = {0};
        p.magic = SKS_MAGIC; p.panel = panel;
        menu_put(MI_SCROFF, 4u);
        settings_export(&p); ui_scr = 0;
        ok = p.s.scr == 7u && settings_import(&p, sizeof p) && scr_get() == 4u;
        p.s.scr = 0;
        ok &= settings_import(&p, sizeof p) && scr_get() == 3u;
        p.s.scr = 6;              /* unknown: 30 MIN */
        ok &= settings_import(&p, sizeof p) && p.s.scr == 0u && scr_get() == 3u;
        ui_scr = 0x55; ok &= scr_get() == 3u;
        bad += check("SCREEN OFF: saved with the settings (0 and unknown = 30 MIN)", ok);
    }

    ui_power_on();                                    /* in the MENU: OCT+ / a knob step it */
    hold(B_HOME);
    for (i = 0; i < 5u; i++) turn(EN_PRESET, 1);       /* ((Skiff #27) past PLAYHEAD) */
    ok = ui.menu == 1 && ui.menu_sel == MI_SCROFF;
    press(B_OCTUP); ok &= scr_get() == 4u;
    press(B_OCTUP); ok &= scr_get() == 4u;           /* (stops at the end) */
    for (i = 0; i < 4u; i++) turn(EN_K2, -1);
    ok &= scr_get() == 0u;
    press(B_OCTDN); ok &= scr_get() == 0u && ui.menu == 1;
    press(B_OCTUP); press(B_OCTUP); press(B_OCTUP); ok &= scr_get() == 3u;
    press(B_HOME); ok &= ui.menu == 0;
    bad += check("SCREEN OFF: the MENU row steps NEVER .. 60 MIN (OCT-/+, any knob), stops at the ends", ok);

    {
        uint32_t f0 = 0, f1 = 0;
        uint64_t a = scr_session_fork(0u, &f0), b = scr_session_fork(1u, &f1);
        bad += check("SCREEN OFF: (U1) never dark while the music plays; the sound bit for bit as with NEVER",
                     a && a == b && (f0 & 6u) == 6u && (f1 & 6u) == 6u && !(f0 & 1u) && !(f1 & 1u));
    }
    ui_power_on();
    return bad;
}

static int test_scale_leds(void)
{
    static const int8_t MAP[6][FM1_NCOL] = {     /* (hal/fm1_input.h FM1_KEYMAP; the host stub is zero) */
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
        { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
        {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
        {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
        { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
        {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    };
    static const uint8_t MODES[LEDS_COUNT] = {LEDS_OFF, LEDS_DIM_LO, LEDS_DIM, LEDS_INV};
    const uint32_t ALL = 0x7FFFFFFu, C_KEYS = 1u << 7 | 1u << 19;
    int bad = 0, ok;
    uint32_t id, p, r, k, mi, root = 0, sc, white = 0;
    track_t *t;
    ui_power_on();
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (MAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)(p << 3 | r);
    }
    for (k = 0; k < 27u; k++)
        white |= (uint32_t)!key_black(k) << k;
    sel_set(0);
    t = TSEL;
    t->p[P_SCALE] = 1;                                  /* C major */
    t->p[P_ROOT] = 0;
    settings_leds = LEDS_DIM;
    ui_leds();
    ok = !menu_get(MI_SCLLED) && str_eq(MI_NAME[MI_SCLLED], "SCALE LEDS") && MI_TAB[MI_SCLLED] == MTAB_CONTROL &&
         !scale_leds(&root) && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    bad += check("#127 SCALE LEDS OFF (the default): every key glows as before", ok);
    menu_put(MI_SCLLED, menu_step(MI_SCLLED, 1));
    ok = menu_get(MI_SCLLED) == 1u && (ui_rec_prefs & 0x40u) && !ui_prefs && str_eq(menu_vname(MI_SCLLED, 1), "ON");
    sc = scale_leds(&root);
    bad += check("#127 MENU > CONTROL > SCALE LEDS ON (a bit of its own); C major: the white keys, the root C4 C5",
                 ok && sc == white && root == C_KEYS);
    for (mi = 0; mi < LEDS_COUNT; mi++) {
        uint32_t m = MODES[mi], held = 1u << 1 | 1u << 7;   /* F#3 (out of the scale) and C4 (the root) held */
        char what[128];
        settings_leds = (uint8_t)m;
        fm1_in.notes = 0;
        ui_leds();
        if (m == LEDS_INV)
            ok = lit_keys(fm1_led) == C_KEYS && lit_keys(fm1_led_mid) == (white & ~C_KEYS) &&
                 lit_keys(fm1_led_dim) == (ALL & ~white);
        else
            ok = !lit_keys(fm1_led) && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        fm1_in.notes = held;
        ui_leds();
        if (m == LEDS_INV)
            ok &= lit_keys(fm1_led) == (C_KEYS & ~held) && !(lit_keys(fm1_led_mid) & held) &&
                  !(lit_keys(fm1_led_dim) & held) && lit_keys(fm1_led_mid) == (white & ~C_KEYS);
        else
            ok &= lit_keys(fm1_led) == held && lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == C_KEYS;
        snprintf(what, sizeof what, "#127 LEDS %s: %s; the keys held %s", LEDS_NAME[m],
                 m == LEDS_INV ? "root lit, the scale mid, the others glow" : "the scale glows, the root brighter",
                 m == LEDS_INV ? "dark" : "lit over it");
        bad += check(what, ok);
    }
    fm1_in.notes = 0;
    settings_leds = LEDS_DIM;
    t->p[P_ROOT] = 2;                                   /* D major: F# and C# in, F and C out, D the root */
    sc = scale_leds(&root);
    ok = (sc >> 1 & 1u) && (sc >> 8 & 1u) && !(sc & 1u) && !(sc >> 7 & 1u) && root == (1u << 9 | 1u << 21) &&
         __builtin_popcount(sc) == 16;
    t->p[P_ROOT] = 0;
    t->p[P_TRANS] = 2;                                  /* C major, TRN +2: F3 plays G3 (in), A#3 plays C4 (the root) */
    sc = scale_leds(&root);
    ok &= (sc & 1u) && !(sc >> 1 & 1u) && root == (1u << 5 | 1u << 17);
    t->p[P_TRANS] = 0;
    bad += check("#127 the keys as they play: ROOT D, TRN +2", ok);
    t->p[P_QUANT] = QN_SNAP;                            /* SNAP: the keys' own notes (the others round down) */
    sc = scale_leds(&root);
    ok = sc == white && root == C_KEYS;
    t->p[P_QUANT] = QN_WHITE;                           /* WHITE, PEN: every white key; a root every 5 white keys */
    t->p[P_SCALE] = 5;
    sc = scale_leds(&root);
    ok &= sc == white && root == (1u << 7 | 1u << 16 | 1u << 24);
    ui_leds();
    ok &= lit_keys(fm1_led_dim) == white && lit_keys(fm1_led_mid) == root;
    t->p[P_QUANT] = QN_OFF;
    t->p[P_SCALE] = 0;                                  /* CHR: every key, the C keys brighter */
    sc = scale_leds(&root);
    ok &= sc == ALL && root == C_KEYS;
    bad += check("#127 QNT SNAP: the keys of the scale; QNT WHITE: all the white keys, its roots; CHR: every key", ok);
    t->p[P_SCALE] = 1;
    ui.layer = LAYER_SCL;                               /* a layer: its own map, no scale glow under it */
    ui_leds();
    ok = !lit_keys(fm1_led_mid) && !lit_keys(fm1_led_dim);
    ui.layer = 0;
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    sc = scale_leds(&root);
    ui_leds();
    ok &= !sc && !root && lit_keys(fm1_led_dim) == ALL && !lit_keys(fm1_led_mid);
    open_family(FAM_SEQ);
    frame();
    for (k = 0; k < NPAGES && cur_page()->graph != GR_ROLL; k++) {
        open_family(FAM_SEQ);
        frame();
    }
    ui_leds();
    ok &= grid_on() && lit_keys(fm1_led_dim) == grid_glow() &&
          lit_keys(fm1_led_mid) == (grid_beats() & ~key_leds(0));
    bad += check("#127 a layer, a DRUM track (its keys as before) and the DRUM grid keep their own LEDs", ok);
    menu_put(MI_SCLLED, 0);
    ok = !(ui_rec_prefs & 0x40u) && !scale_leds(&root);
    bad += check("#127 SCALE LEDS OFF again: the bit clears", ok);
    ui_rec_prefs = 0;
    settings_leds = LEDS_DIM;
    led_pos_init();
    ui_power_on();
    return bad;
}

/* #37: QNT SEQ on the SCL page: the keys as SNAP, the sequence snapped as it plays, saved with the project */
static int test_seq_quant(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    char val[16];
    const char *unit = 0;
    ui_power_on();
    sel_set(0);
    open_family(FAM_SCL);
    frame();
    ok = cur_page()->id[2] == P_QUANT && t->p[P_QUANT] == 0;
    turn(EN_K3, 10);
    param_format(&TP[P_QUANT], t->p[P_QUANT], val, &unit);
    ok &= t->p[P_QUANT] == 4 && str_eq(val, "SEQ+T");  /* ((1.3) SEQ+T appended) */
    t->p[P_QUANT] = 3;
    param_format(&TP[P_QUANT], t->p[P_QUANT], val, &unit);
    bad += check("SCL KNOB 3 QNT: OFF SNAP WHITE SEQ SEQ+T, SEQ+T last (default OFF)", ok && str_eq(val, "SEQ"));
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    ok = kb_map(t, 8) == 60u && kb_map(t, 7) == 60u;   /* C#4 key -> C4, as SNAP */
    t->step[0] = (step_t){.note = {61, 66}, .n = 2, .time = ST_NOTE};
    t->seq_active = 1;
    seq_start();
    events_block(CTL);
    ok &= t->seq_n == 2u && t->seq_notes[0] == 60u && t->seq_notes[1] == 65u && t->step[0].note[0] == 61u &&
          t->step[0].note[1] == 66u;
    transport_req = 2;
    events_block(CTL);
    bad += check("QNT SEQ: the keys snap as SNAP; C#4 F#4 of a step play C4 F4 in C major, the step keeps C#4 F#4", ok);
    project_save(1);
    t->p[P_QUANT] = 0;
    project_load(1);
    bad += check("QNT SEQ saved and loaded with the project", trk[0].p[P_QUANT] == 3);
    return bad;
}

/* #39: KNOB 1..4 with a layer's button held are the layer's only, also as it is let go: a knob turned in the frame
 * the button is let go, while its map still shows (an effect key held after FX), or just after: the page under the
 * layer (ARP: ARP MODE on) or HOME kept its values. GLO / SCL / EDIT change only what their own knobs edit */
static int16_t lk_p[NTRK][P_COUNT], lk_g[G_COUNT];
static void lk_snap(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        memcpy(lk_p[i], trk[i].p, sizeof lk_p[i]);
    memcpy(lk_g, song.g, sizeof lk_g);
}
static int lk_same(uint32_t l)                      /* nothing changed but the layer's own (l: the layer) */
{
    uint32_t i, j;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < P_COUNT; j++) {
            int own = (l == LAYER_GLO && j == P_LEVEL) ||
                      (l == LAYER_SCL && i == 0u && (j == P_ROOT || j == P_SCALE || j == P_CHRD || j == P_VOIC)) ||
                      (l == LAYER_EDIT && i == 0u && (j < P_AMODE || j > P_AORDER));   /* (a load keeps the ARP) */
            if (lk_p[i][j] != trk[i].p[j] && !own)
                return 0;
        }
    for (j = 0; j < G_COUNT; j++)
        if (lk_g[j] != song.g[j] && l != LAYER_EDIT)
            return 0;
    return 1;
}
static void lk_turn(int32_t s)                       /* KNOB 1..4 together, no frame */
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        host_enc[panel.enc[EN_K1 + k]] += s * panel.dir[EN_K1 + k];
}
static int test_layer_knob_leak(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    uint32_t b, sc, pg, fails = 0, arp_fx = 0;
    int bad = 0, ok;
    for (b = 0; b < 4u; b++)
        for (sc = 0; sc < 4u; sc++)
            for (pg = 0; pg <= NPAGES; pg++) {          /* every page, HOME last */
                ui_power_on();
                ret_set(RET_SNAP);                      /* ((2.0) FX's macros back at once: they are not this test's) */
                if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
                frames(100);
                lk_snap();
                btn_down(LB[b]);
                if (sc == 3u) key_down(white(1));
                frame();
                lk_turn(5); frame();
                if (sc == 0u) { lk_turn(2); btn_up(LB[b]); frame(); }          /* turning as it is let go */
                if (sc == 1u) { btn_up(LB[b]); frame(); lk_turn(2); frame(); } /* .. the next frame */
                if (sc == 2u) { btn_up(LB[b]); frames(160); lk_turn(2); frame(); }   /* .. 160 ms after */
                if (sc == 3u) { btn_up(LB[b]); frame(); lk_turn(3); frame(); key_up(white(1)); frame(); }   /* the map */
                frames(400);
                ok = lk_same(LL[b]) && !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                if (!ok) {
                    fails++;
                    if (fails < 6u)
                        printf("ui:   leak: %s scenario %u page %s\n", B_NAME[LB[b]], sc,
                               pg < NPAGES ? PAGES[pg].title : "HOME");
                }
                if (b == 0u && pg < NPAGES && PAGES[pg].graph == GR_ARP && trk[0].p[P_AMODE] == lk_p[0][P_AMODE])
                    arp_fx++;
            }
    bad += check("#39 FX / GLO / SCL / EDIT + KNOB 1..4, let go while turning: no page under the layer changes",
                 !fails && arp_fx == 4u);
    /* the quiet time is short: KNOB 1 edits the page again 0.3 s after FX */
    ui_power_on();
    ret_set(RET_SNAP);
    go_page(GR_ARP);
    {
        int16_t m = TSEL->p[P_AMODE];
        btn_down(B_FX); frame(); turn(EN_K1, 4); btn_up(B_FX); frame();
        frames(320);
        turn(EN_K1, 1);
        ok = TSEL->p[P_AMODE] == m + 1 && !perf_k[0] && str_eq(cur_page()->title, "ARP");
    }
    bad += check("  0.3 s after FX is let go KNOB 1 is ARP MODE again; a knob turned with FX opened no FX page", ok);
    /* a tap of FX with a knob turned in that frame: a combo, no FX page */
    ui_power_on();
    go_page(GR_ARP);
    btn_down(B_FX); frame(); lk_turn(1); btn_up(B_FX); frame(); frames(100);
    bad += check("  FX tapped while a knob turns: no FX page, no ARP change", str_eq(cur_page()->title, "ARP") &&
                 TSEL->p[P_AMODE] == 0);
    return bad;
}

/* #102: the TIMER5 scan counts the knobs' detents while ui_input runs; one that came after the layer's read of
 * KNOB 1..4 (host_enc_late) was read again by the page's: the layer's control and the page under it both moved
 * (HOME's KNOB 1 with FX's FILTER). Every layer (FX GLO SCL EDIT) x KNOB 1..4 x held past HOLD / a combo / locked
 * by a double tap, over every page and HOME, with late detents on the knob, PRESETS and ALGORITHM while it is open
 * and on the knob as it closes: the same state as with the detents read in time (the layer gets them all, one a
 * frame later), the layer's own control moved, and nothing under the layer changed (lk_same) */
typedef struct {
    int16_t p[NTRK][P_COUNT], g[G_COUNT];
    int16_t k[4];                                       /* (perform.c perf_k: (2.0) int16) */
    uint32_t sel, snd[NTRK], mot[NTRK];
    uint8_t home, page;
} rc_state_t;
static uint32_t rc_snd(const track_t *t) { return t->eng_req | (uint32_t)t->preset << 8 | (uint32_t)t->user << 16; }
static uint32_t rc_snd0;
static void rc_take(rc_state_t *st)
{
    uint32_t i;
    memset(st, 0, sizeof *st);
    for (i = 0; i < NTRK; i++) {
        memcpy(st->p[i], trk[i].p, sizeof st->p[i]);
        st->snd[i] = rc_snd(&trk[i]);
        st->mot[i] = motion_count(&trk[i]);
    }
    memcpy(st->g, song.g, sizeof st->g);
    memcpy(st->k, perf_k, sizeof st->k);
    st->sel = song.sel;
    st->home = ui.home;
    st->page = ui.page;
}
static void rc_enc(uint32_t role, int32_t s, int late)
{
    if (late)
        host_enc_late[panel.enc[role]] += s * panel.dir[role];
    else
        host_enc[panel.enc[role]] += s * panel.dir[role];
}
/* one run: layer button lb, mode (0 held past HOLD, 1 a combo, 2 locked), knob k, page pg (NPAGES: HOME); late:
 * the detents come after the reads. a: the state with the layer open, b: after it closed */
static void rc_run(uint32_t lb, uint32_t mode, uint32_t k, uint32_t pg, int late, rc_state_t *a, rc_state_t *b)
{
    int32_t d = (lb == B_GLO || (lb == B_FX && k == 3u)) ? -1 : 1;   /* (GLO LEVEL, FX DEPTH: room to move) */
    uint32_t i;
    ui_power_on();
    ret_set(RET_SNAP);                                  /* ((2.0) FX's macros back at once: not this test's) */
    if (pg < NPAGES) { ui.home = 0; ui.page = (uint8_t)pg; page_entered(); }
    frames(64);
    lk_snap();
    rc_snd0 = rc_snd(TSEL);
    if (mode == 2u) {
        shift_lock(lb); frames(64);
    } else {
        btn_down(lb);
        frame();
        if (mode == 0u)
            frames(560);                                /* (past HOLD: the map, a peek) */
    }
    for (i = 0; i < 3u; i++) {
        rc_enc(EN_K1 + k, d, late && (mode != 1u || i));   /* (a combo: the first in time, it opens the map) */
        if (i == 1u) {
            rc_enc(EN_PRESET, 1, late);                 /* (in time: the layer's read drops them) */
            rc_enc(EN_ALGO, 1, late);
        }
        frame();
    }
    frame();                                            /* (the last late one) */
    rc_take(a);
    if (mode == 2u) {
        press(lb);
    } else {
        btn_up(lb);
        frame();
    }
    for (i = 0; i < 3u; i++) {                          /* turning on as it closes (#39: KNOB 1..4 quiet) */
        rc_enc(EN_K1 + k, d, late);
        frame();
    }
    frames(400);
    rc_take(b);
}
static int test_layer_knob_race(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    static rc_state_t a0, b0, a1, b1;
    uint32_t l, mode, k, pg, fails = 0, runs = 0;
    int bad = 0, ok;
    for (l = 0; l < 4u; l++)
        for (mode = 0; mode < 3u; mode++)
            for (k = 0; k < 4u; k++)
                for (pg = 0; pg <= NPAGES; pg++) {
                    if (pg < NPAGES && !page_visible(pg))
                        continue;
                    if (mode == 2u && LB[l] == B_EDIT && pg < NPAGES &&   /* (EDIT's tap acts there: no lock; */
                        (PAGES[pg].graph == GR_USER || PAGES[pg].graph == GR_SLOTS || step_page(PAGES[pg].graph) ||
                         PAGES[pg].graph == GR_FEEL ||      /* (2.0) FEEL: SHIFT + EDIT deletes) */
                         PAGES[pg].graph == GR_EVENTS || PAGES[pg].graph == GR_SONG))
                        continue;
                    rc_run(LB[l], mode, k, pg, 0, &a0, &b0);
                    rc_run(LB[l], mode, k, pg, 1, &a1, &b1);
                    ok = !memcmp(&a0, &a1, sizeof a0) && !memcmp(&b0, &b1, sizeof b0) &&
                         lk_same(LL[l] == LAYER_EDIT && k == 2u ? 0u : LL[l]) &&
                         song.sel == (LL[l] == LAYER_GLO ? 1u : 0u) &&   /* ((U1) GLO held or locked: ALGORITHM */
                         !perf_k[0] && !perf_k[1] && !perf_k[2] && !perf_k[3];
                    if (LL[l] == LAYER_FX)                  /* the layer's own control moved */
                        ok &= a1.k[k] == 3;
                    else if (LL[l] == LAYER_GLO)
                        ok &= a1.p[k][P_LEVEL] == lk_p[k][P_LEVEL] - 3;
                    else if (LL[l] == LAYER_SCL)
                        ok &= a1.p[0][LY_SCL.id[k]] != lk_p[0][LY_SCL.id[k]];
                    else if (k < 2u)                        /* (EDIT KNOB 3 FAV: a mark, KNOB 4 nothing) */
                        ok &= a1.snd[0] != rc_snd0;
                    runs++;
                    if (!ok) {
                        fails++;
                        if (fails < 6u)
                            printf("ui:   race: %s mode %u KNOB %u page %s\n", B_NAME[LB[l]], mode, k + 1u,
                                   pg < NPAGES ? PAGES[pg].title : "HOME");
                    }
                }
    bad += check("#102 FX GLO SCL EDIT (held, combo, locked) x KNOB 1..4, detents counted mid-pass: the layer's only",
                 !fails && runs > 400u);
    /* HOME, REC armed and playing: FX's macros and GLO's T2..T4 levels record nothing into T1's automation */
    ok = 1;
    for (l = 0; l < 2u; l++)
        for (k = l; k < 4u; k++) {
            int16_t *vp, c;
            uint32_t i;
            ui_power_on();
            song.rec = 1; seq_start(); go_home(); frames(64);
            home_param(k, &vp);
            c = *vp;
            btn_down(LB[l]); frames(560);
            for (i = 0; i < 4u; i++) { rc_enc(EN_K1 + k, l ? -1 : 1, 1); frame(); }
            frame();
            btn_up(LB[l]); frames(400);
            ok &= !motion_count(TSEL) && *vp == c;
            stop_transport();
            song.rec = 0;
        }
    bad += check("  HOME with REC: FX's KNOB 1..4, GLO's KNOB 2..4 record no automation, HOME's sound stays", ok);
    /* (U2) FX locked, latching: the macro stays, HOME's KNOB 1 never moved; OCT- turns it off */
    ui_power_on();
    go_home(); frames(64);
    {
        int16_t *vp, c;
        uint32_t i;
        home_param(0, &vp);
        c = *vp;
        shift_lock(B_FX); frames(100);
        for (i = 0; i < 5u; i++) { rc_enc(EN_K1, 1, 1); frame(); }
        frame(); frames(400);
        ok = perf_k[0] == 5 && *vp == c;
        press(B_OCTDN); frames(100);
        ok &= !perf_k[0] && *vp == c;
        press(B_FX); frames(400);
    }
    bad += check("  FX locked (latching): FILTER stays, HOME's KNOB 1 never moves; OCT- turns it off", ok);
    /* no layer: a late detent is HOME's KNOB 1's, once (the next pass) */
    ui_power_on();
    go_home(); frames(64);
    {
        int16_t *vp, c;
        home_param(0, &vp);
        c = *vp;
        rc_enc(EN_K1, 1, 1); frame(); frame();
        ok = *vp == c + 1;
    }
    bad += check("  no layer: a detent counted mid-pass is HOME's KNOB 1, once", ok);
    return bad;
}

/* Discussion #83: the layer lock. A double tap of FX / GLO / SCL / EDIT (within LY_DTAP_MS, both let go before HOLD)
 * opens the map with no button held, over the page the first tap left (put back); the keys and KNOB 1..4 act as held;
 * a tap closes it (no page), so does another page button (which opens its page), HOME, the menu, a dialog; OCT- puts
 * back as in the layer; a single tap, a peek, a combo and two slow taps are as before; the header shows the lock */
/* ------------------------------------------------------ SEQ TOOLS (1.2) --- */
static void tl_note(step_t *s, uint32_t n) { memset(s, 0, sizeof *s); s->time = ST_NOTE; s->n = 1; s->note[0] = (uint8_t)n; s->vel = 96; }
static void tl_tie(step_t *s) { memset(s, 0, sizeof *s); s->time = ST_TIE; }
/* the selected track: LEN 8, C4 tied, E4, G4 (steps 1 2, 4, 6), a note past LEN (21), a lock on step 4 */
static void tl_melody(void)
{
    uint32_t i;
    track_defaults_steps(TSEL);
    for (i = 0; i < NSTEP; i++)
        step_clear(&TSEL->step[i]);
    tl_note(&TSEL->step[0], 60);
    tl_tie(&TSEL->step[1]);
    tl_note(&TSEL->step[3], 64);
    tl_note(&TSEL->step[5], 67);
    tl_note(&TSEL->step[20], 72);
    TSEL->p[P_SLEN] = 8;
    motion_set_lock(TSEL, 3, P_REV, 50);
}
static int tl_lock_at(uint32_t step) { int16_t v; return motion_lock_get(TSEL, step, P_REV, &v) && v == 50; }
static void tl_key(uint32_t w) { key_down(white(w)); frame(); key_up(white(w)); frame(); }
static uint32_t tl_count(const track_t *t, uint32_t l)
{
    uint32_t i, c = 0;
    for (i = 0; i < (uint32_t)t->p[P_SLEN]; i++)
        c += (step_lanes(&t->step[i]) >> l) & 1u;
    return c;
}
static uint32_t tl_lanes_of(const track_t *t, uint32_t l)   /* lane l over steps 1..16, a bit each */
{
    uint32_t i, m = 0;
    for (i = 0; i < 16u; i++)
        m |= ((step_lanes(&t->step[i]) >> l) & 1u) << i;
    return m;
}
static int tl_in_key(const track_t *t)                       /* every note of steps 1..LEN in the track's scale */
{
    uint32_t i, k;
    for (i = 0; i < (uint32_t)t->p[P_SLEN]; i++)
        for (k = 0; k < t->step[i].n; k++)
            if (t->step[i].time == ST_NOTE && !tl_in_scale(t, t->step[i].note[k]))
                return 0;
    return 1;
}
static int tl_times_are(const track_t *t, const step_t *ref, uint32_t len)   /* the rhythm: the same NOTE / TIE / REST */
{
    uint32_t i;
    for (i = 0; i < len; i++)
        if (t->step[i].time != ref[i].time || (t->step[i].n != 0) != (ref[i].n != 0))
            return 0;
    return 1;
}
static void tl_drum(void)
{
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
}

static int test_seq_tools(void)
{
    static const uint8_t PG[4] = {GR_ROLL, GR_STEPS, GR_FEEL, GR_EVENTS};
    static const uint8_t NOT[4] = {GR_EVENTS, GR_TRK, GR_ADSR, 0};
    int bad = 0, ok, okc, flash;
    uint32_t i, k, a, b2, pg, seed;
    step_t ref[NSTEP], mid[NSTEP];

    /* where it opens: the SEQ pages that show the pattern; elsewhere SEQ held opens SONG as before */
    ok = 1;
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        go_page(PG[i]); frame();
        pg = ui.page;
        btn_down(B_SEQ); frames(500);
        ok &= ui.layer == LAYER_SEQ && ui.page == pg;
        btn_up(B_SEQ); frame();
        ok &= !ui.layer && ui.page == pg && !ui.home;
    }
    bad += check("SEQ TOOLS: SEQ held on NOTES, PATTERN, FEEL, AUTOMATION opens the map (no SONG, the page stays)", ok);
    ok = 1;
    for (i = 0; i < 4u; i++) {
        ui_power_on();
        if (i == 3u) go_home(); else go_page(NOT[i]);
        frame();
        btn_down(B_SEQ);
        for (flash = 0, k = 0; k < 60u; k++) { frame(); flash |= ui.layer; }
        btn_up(B_SEQ); frame();
        ok &= flash == LAYER_SEQ && (i == 3u ? ui.home : cur_page()->graph == NOT[i]);
    }
    ui_power_on();
    go_title("ENV"); frame(); hold(B_SEQ);
    ok &= str_eq(cur_page()->title, "ENV") && !ui.layer;
    bad += check("  (U2) elsewhere (AUTO LIST, MIXER, ENV, HOME) SEQ held: its TOOLS map too, no page", ok);
    ui_power_on();
    go_page(GR_ROLL); frame();
    press(B_SEQ); frames(320);
    ok = cur_page()->graph == GR_FEEL && msg_is("HOLD [SEQ]: SHORTCUTS");   /* ((2.0) by task: NOTES FEEL PATTERN */
    press(B_SEQ); frames(320);                                          /* AUTOMATION SONG) */
    ok &= cur_page()->graph == GR_STEPS;
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_EVENTS && str_eq(cur_page()->title, "AUTOMATION");
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_SONG;
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_ROLL;
    bad += check("  a tap on NOTES: the next SEQ page ((2.0) FEEL PATTERN AUTOMATION SONG NOTES; the hint first)", ok);

    /* the whole sequence: REVERSE (a note's ties stay after it), SHIFT, CLEAR, locks, LEN, silent keys, undo */
    ui_power_on();
    tl_melody();
    memcpy(ref, TSEL->step, sizeof ref);
    go_page(GR_ROLL); frame();
    usb.config = 1;
    {
        uint32_t mo = mo_w;
        btn_down(B_SEQ); frame();
        tl_key(1);                                      /* G3: REVERSE */
        ok = ui.layer == LAYER_SEQ && !gates() && mo_w == mo && msg_is("REVERSED");
    }
    usb.config = 0;
    ok &= TSEL->step[6].time == ST_NOTE && TSEL->step[6].note[0] == 60 && TSEL->step[7].time == ST_TIE &&
          TSEL->step[4].note[0] == 64 && TSEL->step[2].note[0] == 67 && TSEL->step[0].time == ST_REST &&
          TSEL->step[20].note[0] == 72 && tl_lock_at(4) && !tl_lock_at(3);
    bad += check("  G3 REVERSE: steps 1..LEN mirrored, C4 + its tie as C4 then the tie, the lock moves with E4", ok);
    btn_up(B_SEQ); frame();
    hold(B_SAVE);
    bad += check("  SAVE held: the reverse undone (steps and the lock)", !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3));
    btn_down(B_SEQ); frame();
    tl_key(3);                                          /* B3: SHIFT > */
    ok = TSEL->step[1].note[0] == 60 && TSEL->step[2].time == ST_TIE && TSEL->step[4].note[0] == 64 && tl_lock_at(4) &&
         msg_is("SHIFTED RIGHT");
    tl_key(3);
    tl_key(2); tl_key(2);                               /* A3: SHIFT < twice */
    ok &= !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3);
    TSEL->p[P_SLEN] = 6;
    tl_key(2);
    ok &= TSEL->step[5].note[0] == 60 && TSEL->step[2].note[0] == 64 && TSEL->step[6].time == ST_REST &&
          TSEL->step[20].note[0] == 72;                 /* (inside LEN 6: step 7 and 21 stay) */
    tl_key(3);
    TSEL->p[P_SLEN] = 8;
    ok &= !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  B3 / A3 SHIFT > <: one step round inside LEN (the lock with it); back again: as it was", ok);
    tl_key(0);                                          /* F3: CLEAR */
    for (ok = msg_is("SEQUENCE CLEARED"), i = 0; i < 8u; i++)
        ok &= TSEL->step[i].time == ST_REST && !TSEL->step[i].n;
    ok &= TSEL->step[20].note[0] == 72 && !motion_lock_count(TSEL);
    bad += check("  F3 CLEAR: steps 1..LEN empty (no dialog), past LEN kept, the locks of the steps cleared go", ok);
    oct_back();
    ok = !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3) && TSEL->p[P_SLEN] == 8 && msg_is("UNDONE: SEQUENCE") &&
         ui.layer == LAYER_SEQ && song.octave == 0;
    turn(EN_K1, -2);
    ok &= TSEL->p[P_SLEN] == 6;                         /* KNOB 1: LEN (the PATTERN page's four) */
    oct_back();
    ok &= TSEL->p[P_SLEN] == 8;
    btn_up(B_SEQ); frame();
    hold(B_SAVE);                                       /* (OCT-'s put back is one undo too: the tools again) */
    ok &= TSEL->step[0].time == ST_REST;
    hold(B_SAVE);
    ok &= !memcmp(TSEL->step, ref, sizeof ref);
    bad += check("  OCT-: the track as the layer opened (steps, locks, LEN); KNOB 1..4 LEN DIV SWING GATE", ok);
    ui_power_on();                                      /* ((2.0) the put-back from the undo ring: past its 8 copies */
    tl_melody();                                        /* the first change's copy kept, ui.c upin) */
    memcpy(ref, TSEL->step, sizeof ref);
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frame();
    for (i = 0; i < UNDO_N + 4u; i++)
        tl_key(3);                                      /* B3: SHIFT > twelve times (LEN 8: four round) */
    ok = memcmp(TSEL->step, ref, sizeof ref) != 0 && undo_n == UNDO_N;
    oct_back();
    ok &= !memcmp(TSEL->step, ref, sizeof ref) && tl_lock_at(3) && msg_is("UNDONE: SEQUENCE");
    btn_up(B_SEQ); frame();
    hold(B_SAVE);                                       /* (the put back undone: four round again) */
    ok &= TSEL->step[4].note[0] == 60 && tl_lock_at(7) && !upin.pin && !upin.pin_next;
    bad += check("  (2.0) OCT- after twelve SHIFT > (the undo ring holds 8): the track as the layer opened; SAVE held: "
                 "the shifts again", ok);

    /* RANDOM / COOK on a melodic track (fixed seeds): the rhythm stays, the notes in the scale, near the middle */
    ok = okc = 1;
    for (seed = 1; seed <= 40u; seed++) {
        ui_power_on();
        tl_melody();
        TSEL->p[P_ROOT] = 2; TSEL->p[P_SCALE] = 2;
        memcpy(ref, TSEL->step, sizeof ref);
        go_page(GR_ROLL); frame();
        rng_state = 0x9E3779B9u * seed;
        btn_down(B_SEQ); frame();
        tl_key(4);                                      /* C4: RANDOM */
        ok &= msg_is("RANDOM NOTES") && tl_times_are(TSEL, ref, NSTEP) && tl_in_key(TSEL) && TSEL->step[20].note[0] == 72;
        for (i = 0; i < 8u; i++)
            if (TSEL->step[i].n)
                ok &= TSEL->step[i].note[0] >= 54 && TSEL->step[i].note[0] <= 72;
        for (k = 0; k < 12u; k++) {                     /* D4: COOK, again and again */
            memcpy(mid, TSEL->step, sizeof mid);
            tl_key(5);
            okc &= tl_times_are(TSEL, ref, NSTEP) && tl_in_key(TSEL) && TSEL->step[20].note[0] == 72 &&
                   memcmp(mid, TSEL->step, sizeof mid) != 0;
        }
        okc &= msg_is("COOKED 12");
        btn_up(B_SEQ); frame();
    }
    bad += check("  C4 RANDOM (melodic): the rhythm kept, new notes in the scale near the middle (40 seeds)", ok);
    bad += check("  D4 COOK x12: each press changes it, the rhythm and the scale kept, \"COOKED 12\"", okc);
    hold(B_SAVE);
    bad += check("  SAVE held after COOKs: the last one undone only", !memcmp(TSEL->step, mid, sizeof mid));
    ui_power_on();                                      /* ((1.4.1, a YouTube report) a 16-step line, LEN turned up to 64 */
    for (i = 0; i < NSTEP; i++)                         /* : RANDOM fills every bar, not the first only) */
        step_clear(&TSEL->step[i]);
    for (i = 0; i < 16u; i += 2u)
        TSEL->step[i] = (step_t){{60}, 1, ST_NOTE, 0, 96};
    TSEL->p[P_SLEN] = 64;
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frame();
    tl_key(4);
    for (ok = msg_is("RANDOM NOTES"), i = 0; i < NSTEP; i++)
        ok &= (TSEL->step[i].time == ST_NOTE && TSEL->step[i].n) == (i % 2u == 0u);
    btn_up(B_SEQ); frame();
    bad += check("  C4 RANDOM on a 16-step line with LEN 64: every bar gets the line's rhythm and new notes", ok);
    ui_power_on();
    track_defaults_steps(TSEL);
    TSEL->p[P_SLEN] = 16;
    go_page(GR_ROLL); frame();
    rng_state = 12345u;
    btn_down(B_SEQ); frame();
    tl_key(5);
    ok = msg_is("NOTHING TO COOK") && !undo.trk;
    tl_key(4);
    for (k = 0, i = 0; i < 16u; i++) {
        ok &= i % 2u ? TSEL->step[i].time == ST_REST : 1;
        k += (uint32_t)tl_gate(&TSEL->step[i]);
    }
    ok &= tl_gate(&TSEL->step[0]) && k >= 3u && tl_in_key(TSEL);
    btn_up(B_SEQ); frame();
    bad += check("  an empty pattern: COOK says NOTHING TO COOK; RANDOM writes 8ths with rests, in the scale", ok);

    /* DRUM: RANDOM (kick 3..5 with one on step 1, snare 2 .. 3 on 2 and 4, closed hat 8..12), COOK keeps the 1 */
    ok = okc = 1;
    for (seed = 1; seed <= 60u; seed++) {
        ui_power_on();
        tl_drum();
        go_page(GR_ROLL); frame();
        rng_state = 0x2545F491u * seed;
        btn_down(B_SEQ); frame();
        tl_key(4);
        a = tl_count(TSEL, DV_KICK); b2 = tl_count(TSEL, DV_SNARE);
        ok &= msg_is("RANDOM BEAT") && a >= 3u && a <= 5u && (tl_lanes_of(TSEL, DV_KICK) & 1u) && b2 >= 2u && b2 <= 3u &&
              (tl_lanes_of(TSEL, DV_SNARE) & 0x1010u) == 0x1010u && tl_count(TSEL, DV_HATC) >= 8u &&
              tl_count(TSEL, DV_HATC) <= 12u;
        for (k = 0; k < 10u; k++) {
            memcpy(mid, TSEL->step, sizeof mid);
            tl_key(5);
            okc &= (tl_lanes_of(TSEL, DV_KICK) & 1u) && memcmp(mid, TSEL->step, sizeof mid) != 0 && tl_count(TSEL, DV_HATC);
        }
        btn_up(B_SEQ); frame();
    }
    bad += check("  DRUM RANDOM: kick 3..5 (one on 1), snare on 2 and 4 (+ a ghost), hat 8..12; 60 seeds", ok);
    bad += check("  DRUM COOK x10: each press changes it, the kick on 1 stays", okc);
    ui_power_on();
    tl_drum();
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frame();
    tl_key(6);                                          /* E4: BEAT */
    ok = msg_is("BASIC BEAT") && tl_lanes_of(TSEL, DV_KICK) == 0x1111u && tl_lanes_of(TSEL, DV_SNARE) == 0x1010u &&
         tl_lanes_of(TSEL, DV_HATC) == 0x5555u;
    key_down(black(2)); frame(); key_up(black(2)); frame();   /* black key 3: the CLAP lane */
    ok &= ui.lane == DV_CLAP && !gates();
    tl_key(10);                                         /* B4: FILL, four times round */
    ok &= tl_lanes_of(TSEL, DV_CLAP) == 0xFFFFu && msg_is("CLAP EVERY STEP");
    tl_key(10);
    ok &= tl_lanes_of(TSEL, DV_CLAP) == 0x5555u;
    tl_key(10);
    ok &= tl_lanes_of(TSEL, DV_CLAP) == 0x1111u;
    tl_key(10);
    ok &= !tl_lanes_of(TSEL, DV_CLAP) && msg_is("CLAP CLEARED") && tl_lanes_of(TSEL, DV_KICK) == 0x1111u;
    bad += check("  E4 BEAT: four on the floor, 2 and 4, 8th hats; black key 3 the lane, B4 FILL 1 / 2 / 4 / none", ok);
    ui.lane = DV_SNARE;
    grid_acc(TSEL, 4, DV_SNARE, 1);
    tl_key(9);                                          /* A4: REVERSE the lane */
    ok = tl_lanes_of(TSEL, DV_SNARE) == 0x0808u && ((step_accents(&TSEL->step[11]) >> DV_SNARE) & 1u) &&
         !((step_accents(&TSEL->step[3]) >> DV_SNARE) & 1u) && tl_lanes_of(TSEL, DV_KICK) == 0x1111u;
    tl_key(8);                                          /* G4: CLEAR the lane */
    ok &= !tl_lanes_of(TSEL, DV_SNARE) && tl_lanes_of(TSEL, DV_HATC) == 0x5555u && msg_is("SNARE CLEARED");
    ui.lane = DV_TOM;
    rng_state = 77u;
    tl_key(11);                                         /* C5: RANDOM the lane: at least one hit */
    ok &= tl_count(TSEL, DV_TOM) >= 1u && tl_lanes_of(TSEL, DV_KICK) == 0x1111u;
    bad += check("  A4 / G4 / C5: the lane reversed (its accents with it), cleared, at random; the others stay", ok);
    a = leds_at(0); b2 = leds_at(250);
    ok = ((a ^ b2) >> white(0)) & 1u && ((a ^ b2) >> white(11)) & 1u && ((a ^ b2) >> white(7)) & 1u &&   /* ((1.3)
                                                         * F4 EUCLID, D5 DOUBLE, E5 HALF too) */
         ((a ^ b2) >> white(12)) & 1u && ((a ^ b2) >> white(13)) & 1u &&
         ((a & b2) >> black(DV_TOM)) & 1u && ((a ^ b2) >> black(0)) & 1u && !(((a | b2) >> black(8)) & 1u);
    btn_up(B_SEQ); frame();
    ui_power_on();
    tl_melody();
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frames(500);                       /* (the map: a peek) */
    memcpy(ref, TSEL->step, sizeof ref);
    a = leds_at(0); b2 = leds_at(250);
    ok &= ((a ^ b2) >> white(5)) & 1u && !(((a | b2) >> white(6)) & 1u) && !(((a | b2) >> black(0)) & 1u) &&
          ((a ^ b2) >> white(7)) & 1u;                 /* ((1.3) EUCLID: melodic too) */
    tl_key(6); tl_key(10);
    key_down(black(2)); frame(); key_up(black(2)); frame();
    ok &= !memcmp(TSEL->step, ref, sizeof ref) && ui.lane == 0u && !gates();
    btn_up(B_SEQ); frame();
    bad += check("  LEDs: the tools breathe, the lane lit (DRUM); a melodic track: no BEAT, no lane keys", ok);

    /* the lock (a double tap), a song playing */
    ui_power_on();
    tl_melody();
    go_page(GR_ROLL); frame();
    pg = ui.page;
    shift_lock(B_SEQ); frames(500);
    ok = ui.lock == LAYER_SEQ && ui.layer == LAYER_SEQ && ui.page == pg;
    tl_key(1);
    ok &= TSEL->step[6].note[0] == 60;
    press(B_SEQ); frames(16);
    ok &= !ui.lock && !ui.layer && ui.page == pg;
    bad += check("  double tapped on STEP: locked over STEP, the keys act; a tap closes it", ok);
    return bad;
}

static int test_layer_lock(void)
{
    static const uint8_t LB[4] = {B_FX, B_GLO, B_SCL, B_EDIT};
    static const uint8_t LL[4] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT};
    int bad = 0, ok, okk;
    uint32_t i, pg;
    ok = 1; okk = 1;
    for (i = 0; i < 4u; i++) {                          /* (U2) SHIFT + the button on ENV: locked, ENV under it */
        ui_power_on();
        go_title("ENV"); frame();
        pg = ui.page;
        shift_lock(LB[i]);
        frames(500);                                    /* (no button held, long past HOLD) */
        ok &= ui.lock == LL[i] && ui.layer == LL[i] && layer_locked() && !ui.home && ui.page == pg && !ui.menu &&
              !fm1_in.buttons && ((layer_seen >> LL[i]) & 1u) && !msg_is("HOLD [GLO]: SHORTCUTS");
        draw_head();
        okk &= layer_locked();
        press(LB[i]);                                   /* a tap: closed, no page */
        frames(16);
        okk &= !ui.lock && !ui.layer && !ui.home && ui.page == pg;
    }
    bad += check("(U2) SHIFT + FX GLO SCL EDIT: the map stays open with no button held, over the page as it is, no MENU", ok);
    bad += check("  a tap of the button closes it, no page opens", okk);
    ok = 1;
    for (i = 0; i < 4u; i++) {                          /* (#13) the button held first, then HOME: locked as well */
        ui_power_on();
        go_title("ENV"); frame();
        pg = ui.page;
        btn_down(LB[i]); frame();
        press(B_HOME);
        btn_up(LB[i]); frame();
        frames(500);
        ok &= ui.lock == LL[i] && ui.layer == LL[i] && !ui.home && ui.page == pg && !ui.menu && !ui.shift;
    }
    ui_power_on();                                      /* REC + HOME: no lock (REC arms on its press), no menu */
    go_title("ENV"); frame();
    btn_down(B_REC); frame();
    press(B_HOME);
    btn_up(B_REC); frames(100);
    bad += check("(#13) FX GLO SCL EDIT held, then HOME: locked open over the page; REC + HOME: no lock",
                 ok && !ui.lock && !ui.layer && !ui.menu);
    /* the keys and KNOB 1..4 act as held */
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_GLO); frames(100);
    usb.config = 1;
    {
        uint32_t mo = mo_w;
        int16_t l0 = trk[0].p[P_LEVEL];
        key_down(black(1)); frame(); key_up(black(1)); frame();
        ok = trk[1].p[P_MUTE] == 1 && !gates() && mo_w == mo;
        turn(EN_K1, 3);
        ok &= trk[0].p[P_LEVEL] == l0 + 3 && str_eq(cur_page()->title, "ENV");
        press(B_OCTDN);                                 /* OCT-: the mix put back, still locked */
        ok &= !trk[1].p[P_MUTE] && trk[0].p[P_LEVEL] == l0 && msg_is("UNDONE: MIX") && ui.lock == LAYER_GLO;
    }
    bad += check("  GLO locked: a black key mutes (silent, no MIDI), KNOB 1 T1 LEVEL, OCT- puts back, still locked", ok);
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_FX); frames(100);
    key_down(white(4)); frame();                        /* C4: LPF latched ((U2) a locked FX layer latches) */
    ok = (perf_latched & PF_BIT(PF_LPF)) && !gates();
    key_up(white(4)); frame();
    ok &= (perf_latched & PF_BIT(PF_LPF)) != 0u;
    turn(EN_K2, 20);
    ok &= perf_k[1] == 20;
    press(B_FX); frames(16);
    ok &= !ui.lock && perf_k[1] > 0 && !perf_latched;  /* (closed: the latched effects off, (2.0, D 2A) the macros on */
    a_bar();                                            /* their way back over RETURN: a bar) */
    bad += check("  FX locked: a key latches its effect, KNOB 2 CRUSH; closed: the effects off, the macros back over a bar",
                 ok && !perf_k[1]);
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_SCL); frames(100);
    key_down(white(1)); frame(); key_up(white(1)); frame();   /* G3: ROOT G */
    ok = TSEL->p[P_ROOT] == 7 && !gates();
    turn(EN_K2, 1);
    ok &= TSEL->p[P_SCALE] == 1 && str_eq(cur_page()->title, "ENV");
    bad += check("  SCL locked: a key sets ROOT, KNOB 2 SCL, the page stays ENV", ok);
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_EDIT); frames(100);
    turn(EN_K1, 1);
    ok = TSEL->eng_req != trk[1].eng_req || TSEL->eng_req != TRK_DEF[0][0];
    ok &= str_eq(cur_page()->title, "ENV") && ui.lock == LAYER_EDIT;
    key_down(white(LY_INIT)); frame(); key_up(white(LY_INIT)); frame();   /* INIT: the dialog closes it */
    ok &= ui.confirm == CF_INIT_SOUND && !ui.lock && !ui.layer;
    press(B_OCTDN);
    bad += check("  EDIT locked: KNOB 1 the engine; INIT's dialog closes the lock", ok);
    /* closing: another page button opens its page, HOME goes home, another layer's tap its page, the menu */
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_GLO); frames(100);
    press(B_LFO);
    ok = !ui.lock && !ui.layer && str_eq(cur_page()->title, "LFO");
    shift_lock(B_SCL); frames(100);
    ok &= ui.lock == LAYER_SCL && str_eq(cur_page()->title, "LFO");
    press(B_HOME); frames(16);
    ok &= !ui.lock && !ui.layer && ui.home;
    shift_lock(B_GLO); frames(100);
    press(B_SCL); frames(400);
    ok &= !ui.lock && !ui.layer && cur_page()->fam == FAM_SCL;
    shift_lock(B_FX); frames(100);
    hold(B_HOME);
    ok &= ui.menu && !ui.lock && !ui.layer;
    hold(B_HOME);
    bad += check("  LFO, HOME, another layer's button, the menu close it (and act as always)", ok);
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_GLO); frames(100);
    btn_down(B_GLO); frames(500);                       /* its button held: the map stays (a peek), let go: closed */
    ok = !ui.lock && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && str_eq(cur_page()->title, "ENV");
    frames(400);
    {
        int16_t a = TSEL->p[P_ATK];
        turn(EN_K1, 1);
        ok &= TSEL->p[P_ATK] == a + 1;                  /* (the page's knobs again after the quiet time) */
    }
    bad += check("  its button held after the lock: a peek, let go closes it; KNOB 1 is ENV's again", ok);
    /* unchanged: a single tap, two slow taps, a tap and a peek, a tap and a combo, PLAY / REC in the lock */
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_GLO); frames(400);
    ok = str_eq(cur_page()->title, "DELAY") && !ui.lock;
    press(B_GLO); frames(400);
    ok &= str_eq(cur_page()->title, "REVERB") && !ui.lock && !ui.layer;
    press(B_GLO); frames(400); press(B_GLO); frames(400); press(B_GLO);   /* (CHORUS, GLOBAL, MIXER) */
    btn_down(B_GLO); frames(500);
    ok &= ui.layer == LAYER_GLO && !ui.lock;
    btn_up(B_GLO); frame();
    ok &= !ui.layer && !ui.lock && cur_page()->graph == GR_TRK;
    press(B_SCL);
    lay_combo(B_SCL, white(2));
    key_up(white(2)); btn_up(B_SCL); frame(); frames(400);
    ok &= !ui.layer && !ui.lock && TSEL->p[P_ROOT] == 9 && cur_page()->fam == FAM_SCL;
    bad += check("  unchanged: a tap opens the page, slow taps cycle, a tap then a peek or a combo: no lock", ok);
    ui_power_on();
    go_page(GR_TRK); frame();
    press(B_GLO); frames(16);
    press(B_LFO); frames(16);
    press(B_GLO); frames(400);
    ok = !ui.lock && str_eq(cur_page()->title, "DELAY");   /* (another button between the taps: two taps; (U3, N3) GLO
                                                         * back to the page it was left on) */
    ui_power_on();
    go_title("ENV"); frame();
    shift_lock(B_FX); frames(100);
    press(B_PLAY);
    ok &= song.playing || transport_req;
    ok &= ui.lock == LAYER_FX;
    stop_transport();
    bad += check("  another button between the taps: no lock; PLAY in a locked layer plays, the lock stays", ok);
    {   /* (Felucca #183) in every lock PLAY starts and stops (GLO's lock no RESTART: that needs GLO held) */
        static const uint8_t PB[5] = {B_FX, B_GLO, B_SCL, B_EDIT, B_SEQ};
        static const uint8_t PL[5] = {LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_SEQ};
        uint32_t i, k;
        int okk;
        ok = 1;
        for (i = 0; i < 5u; i++) {
            ui_power_on();
            go_title("ENV"); frame();
            shift_lock(PB[i]); frames(100);
            transport_req = 0;
            okk = ui.lock == PL[i];
            press(B_PLAY);                              /* stopped: PLAY */
            okk &= transport_req == 1u;
            events_block(CTL);
            okk &= song.playing != 0;
            for (k = 0; k < 2u; k++) {                  /* playing: PLAY stops, twice over */
                press(B_PLAY);
                okk &= transport_req == 2u;
                events_block(CTL);
                okk &= !song.playing && ui.lock == PL[i];
                press(B_PLAY);
                okk &= transport_req == 1u;
                events_block(CTL);
                okk &= song.playing != 0;
            }
            stop_transport();
            transport_req = 0;
            if (!okk)
                printf("    #183: %s locked, PLAY does not start / stop\n", LAYERS[PL[i]].head);
            ok &= okk;
        }
        bad += check("  FX GLO SCL EDIT SEQ locked: PLAY starts and stops (no RESTART without GLO held)", ok);
    }
    {   /* EDIT on STEP clears a step: two quick taps clear two (no lock) */
        track_t *t;
        ui_power_on();
        t = TSEL;
        my_steps(t);
        go_page(GR_ROLL); frame();
        ui.cursor = 0;
        song.rec = 1u << song.sel;                   /* (U2: armed, step entry) */
        press(B_EDIT); press(B_EDIT); frames(100);
        bad += check("  EDIT on STEP, armed (it clears the step): two quick taps clear two steps, no lock",
                     !ui.lock && !ui.layer && ui.cursor == 2u && t->step[0].time == ST_REST);
    }
    return bad;
}

/* 1.1.5: the REC layer (ui_layer.c); (Skiff U2) REC arms on its press, a transport button again (held: nothing more,
 * keys with it play), and its layer opens locked with SHIFT + REC on any page, a REC tap closing it. F3 CLEAR held for
 * the hold time (SAVE held undoes it), G3 CLICK, A3 COUNT-IN, B3 CLICK LEVEL (each the next value, the same store as
 * MENU > AUDIO, saved when the layer closes), KNOB 1..3 the same; OCT- puts back; the LEDs; REC in another layer arms */
static void rec_open(void) { shift_lock(B_REC); }
static void rec_close(void) { press(B_REC); frame(); }
static void rec_clear_held(void) { key_down(white(0)); frames(HOLD_MS[settings_hold % 4u] + 64u); key_up(white(0)); frame(); }
static uint32_t menu_rec(uint32_t r) { return menu_get(r == 0u ? MI_CLICK : r == 1u ? MI_COUNTIN : MI_CLKLVL); }
static int test_rec_layer(void)
{
    static const uint8_t G[8] = {GR_ROLL, GR_STEPS, GR_FEEL, GR_EVENTS, GR_SCALE, GR_SONG, GR_LFO, GR_TRK};   /* ((U3)
                                                         * T2's FM6 has no ENV page: LFO) */
    int bad = 0, ok = 1;
    uint32_t k, i, n;
    step_t before[NSTEP];
    for (k = 0; k < 9u; k++) {                          /* every page (and HOME): SHIFT + REC, locked; REC: closed */
        ui_power_on();
        sel_set(1);
        my_steps(&trk[1]);
        if (k < 8u)
            go_page(G[k]);
        frame();
        memcpy(before, trk[1].step, sizeof before);
        n = ui.page;
        rec_open(); frames(100);
        ok &= ui.layer == LAYER_REC && ui.lock == LAYER_REC && !ui.confirm && song.rec == 0u && !transport_req &&
              !ui.menu && ui.page == n;
        rec_close();
        ok &= !ui.layer && !ui.lock && !ui.confirm && song.rec == 0u && !transport_req && ui.page == n &&
              (k < 8u ? !ui.home : ui.home) && !memcmp(trk[1].step, before, sizeof before);
    }
    bad += check("(U2) REC layer: SHIFT + REC on STEP PATTERN CHANCE AUTO LIST SONG AUTOMATION ENV MIXER HOME opens it locked", ok);
    bad += check("  nothing armed, cleared or started; a REC tap closes it, nothing armed", ok);
    ui_power_on();
    btn_down(B_REC); frame();
    ok = ui.home && song.rec == 1u && transport_req == 1u && !ui.layer;   /* (on the press) */
    frames(800);
    ok &= !ui.layer && song.rec == 1u;                  /* (held: nothing more) */
    btn_up(B_REC); frame();
    frames(400); transport_req = 0; song.playing = 1;
    press(B_REC);
    ok &= song.rec == 0u && !ui.layer && song.playing;
    song.playing = 0;
    go_page(GR_ROLL); frame(); frames(400);
    press(B_REC);
    ok &= song.rec == 1u && transport_req == 0u && msg_is("KEYS WRITE STEPS");
    bad += check("  (U2) REC arms / disarms on its press (HOME starts PLAY, STEP: step recording); held, no layer", ok);
    ui_power_on();
    btn_down(B_REC); frame();
    press(B_PLAY);                                      /* REC + PLAY: armed, PLAY stops what REC started */
    ok = song.rec == 1u && !ui.layer;
    btn_up(B_REC); frame();
    ok &= song.rec == 1u && !ui.layer;
    bad += check("  REC held + PLAY: armed, no layer", ok);
    ui_power_on();
    sel_set(2);
    frames(400);
    btn_down(B_GLO); frame();
    press(B_REC);                                       /* REC in GLO's layer: armed */
    ok = song.rec == 4u && ui.ly == LAYER_GLO;
    btn_up(B_GLO); frame();
    frames(400);
    btn_down(B_GLO); frames(512); press(B_REC);
    ok &= song.rec == 0u && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    bad += check("  REC in another layer arms / disarms", ok);

    /* F3 CLEAR: held for the hold time, no dialog, SAVE held undoes it; a short press nothing; empty: NOTHING TO CLEAR */
    ui_power_on();
    sel_set(1);
    my_steps(&trk[1]);
    go_page(GR_ROLL); frame();
    memcpy(before, trk[1].step, sizeof before);
    rec_open(); frame();
    key_down(white(0)); frame(); key_up(white(0)); frame();
    ok = msg_is("HOLD: CLEAR") && !memcmp(trk[1].step, before, sizeof before);
    key_down(white(0)); frames(HOLD_MS[settings_hold % 4u] + 64u);
    ok &= ui.layer == LAYER_REC && seq_is_empty(&trk[1]) && !ui.confirm && msg_is("SEQUENCE CLEARED") && !gates();
    key_up(white(0)); frame();
    rec_close();
    ok &= !ui.layer && song.rec == 0u && !transport_req && seq_is_empty(&trk[1]);
    bad += check("(U2) REC + F3 held: CLEAR T2's sequence (no dialog, the key silent); a short press: HOLD: CLEAR, nothing", ok);
    frames(400);
    hold(B_SAVE);
    bad += check("  SAVE held undoes the clear", !memcmp(trk[1].step, before, sizeof before));
    track_defaults_steps(&trk[1]);
    rec_open(); frame(); key_down(white(0)); frames(HOLD_MS[settings_hold % 4u] + 64u);
    ok = msg_is("NOTHING TO CLEAR");
    key_up(white(0)); frame(); rec_close();
    my_steps(&trk[1]);
    song.playing = 1; song.rec = 2u;                    /* recording live: a retake, the arming and PLAY stay */
    rec_open(); frame(); rec_clear_held();
    ok &= seq_is_empty(&trk[1]) && song.rec == 2u && song.playing;
    rec_close();
    song.playing = 0;
    bad += check("  empty: NOTHING TO CLEAR; armed and playing: clears, records on", ok && song.rec == 2u);
    song.rec = 0;

    /* G3 A3 B3: the settings, the next value each press, MENU > AUDIO's values; KNOB 1..3 */
    ui_power_on();
    ui_rec_prefs = 0;
    rp_apply();
    ok = 1;
    rec_open(); frames(100);
    for (k = 0; k < 3u; k++) {
        static const char *const NM[3][3] = {{"CLICK REC", "CLICK ON", "CLICK OFF"},
                                             {"COUNT-IN 1 BAR", "COUNT-IN 2 BARS", "COUNT-IN OFF"},
                                             {"CLICK LEVEL HIGH", "CLICK LEVEL LOW", "CLICK LEVEL MID"}};
        static const uint32_t WANT[3][3] = {{1, 2, 0}, {1, 2, 0}, {2, 0, 1}};
        for (i = 0; i < 3u; i++) {
            key_down(white(k + 1u)); frame(); key_up(white(k + 1u)); frame();
            ok &= menu_rec(k) == WANT[k][i] && msg_is(NM[k][i]) && !gates() &&
                  str_eq(menu_vname(k == 0u ? MI_CLICK : k == 1u ? MI_COUNTIN : MI_CLKLVL, menu_rec(k)),
                         rl_name(k, rp_get(RL_F[k])));
        }
    }
    ok &= ui_rec_prefs == 0u;                           /* (round: as it was) */
    bad += check("REC + G3 CLICK OFF>REC>ON, A3 COUNT-IN OFF>1 BAR>2 BARS, B3 LEVEL MID>HIGH>LOW: MENU's values", ok);
    key_down(white(1)); frame(); key_up(white(1)); frame();        /* CLICK REC */
    key_down(white(2)); frame(); key_up(white(2)); frame();        /* COUNT-IN 1 BAR */
    key_down(white(3)); frame(); key_up(white(3)); frame();        /* LEVEL HIGH */
    ok = click_mode == 1u && cin_bars == 1u && click_lvl == 2u && lys.rp_dirty;
    rec_close();
    ok &= !ui.layer && !lys.rp_dirty;                  /* (closed: settings_save) */
    {
        sks_t p = {0};
        uint8_t was = ui_rec_prefs;
        p.magic = SKS_MAGIC; p.panel = panel;
        settings_export(&p); ui_rec_prefs = 0;
        ok &= settings_import(&p, sizeof p) && ui_rec_prefs == was && menu_get(MI_CLICK) == 1u &&
              menu_get(MI_COUNTIN) == 1u && menu_get(MI_CLKLVL) == 2u;
    }
    bad += check("  applied at once (click.c, seq.c), saved when the layer closes, kept with the settings", ok);
    rec_open(); frames(100);
    turn(EN_K1, 1);                                     /* CLICK REC -> ON */
    turn(EN_K2, -1);                                    /* COUNT-IN 1 BAR -> OFF */
    turn(EN_K3, -5);                                    /* LEVEL HIGH -> LOW (stops at the end) */
    turn(EN_K4, 3);                                     /* nothing */
    ok = menu_get(MI_CLICK) == 2u && menu_get(MI_COUNTIN) == 0u && menu_get(MI_CLKLVL) == 0u && ui.home;
    rec_close();
    bad += check("  KNOB 1..3 CLICK COUNT-IN LEVEL, stopping at the ends; KNOB 4 nothing; the page untouched", ok);

    /* OCT- puts back the settings and the track as the layer opened */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    sel_set(1);
    my_steps(&trk[1]);
    memcpy(before, trk[1].step, sizeof before);
    rec_open(); frames(100);
    key_down(white(1)); frame(); key_up(white(1)); frame();
    rec_clear_held();
    ok = seq_is_empty(&trk[1]) && click_mode == 1u;
    press(B_OCTDN);
    ok &= !memcmp(trk[1].step, before, sizeof before) && ui_rec_prefs == 0u && click_mode == 0u &&
          msg_is("UNDONE: REC") && song.octave == 0 && ui.layer == LAYER_REC;
    rec_close();
    bad += check("  OCT- puts back CLICK and the cleared track; no octave shift", ok);

    /* the LEDs: CLEAR breathes when there is something; a setting on lit (LEVEL: with the click on), else breathing */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    my_steps(TSEL);
    rec_open(); frames(100);
    {
        uint32_t a = leds_at(0), b = leds_at(250), w0 = 1u << white(0), w1 = 1u << white(1), w2 = 1u << white(2),
                 w3 = 1u << white(3);
        ok = ((a ^ b) & (w0 | w1 | w2 | w3)) == (w0 | w1 | w2 | w3) && !(a & b) && !((a | b) & ~(w0 | w1 | w2 | w3));
        rp_put(RP_CLICK, 2); rp_put(RP_COUNTIN, 2);
        a = leds_at(0); b = leds_at(250);
        ok &= (a & b) == (w1 | w2 | w3) && (a ^ b) == w0;
        track_defaults_steps(TSEL);
        a = leds_at(0); b = leds_at(250);
        ok &= !((a | b) & w0);
    }
    rec_close();
    bad += check("  LEDs: CLEAR breathes (dark when empty), CLICK / COUNT-IN lit when on, LEVEL with the click", ok);

    /* locked: PLAY works in it; another layer locked: REC arms, the lock stays */
    ui_power_on();
    ui_rec_prefs = 0; rp_apply();
    go_title("ENV"); frame();
    n = ui.page;
    rec_open(); frames(100);
    press(B_PLAY);
    ok = transport_req == 1u && ui.lock == LAYER_REC && ui.page == n && layer_locked();
    transport_req = 0;
    rec_close();
    ok &= !ui.lock && !ui.layer && song.rec == 0u;
    shift_lock(B_GLO); frames(100);
    press(B_REC);
    ok &= ui.lock == LAYER_GLO && song.rec == 1u;
    bad += check("  locked: PLAY plays; in GLO's lock a REC press arms, GLO stays locked", ok);
    return bad;
}

/* #40: KNOB 4's DEPTH rises to the right (it fell). (Skiff U2, the review's R11) FX locked (SHIFT + FX) latches: a key
 * turns its effect on, again off; the macros stay while it is locked; FX + OCT- all off; its tap (closed) turns them off,
 * so do the menu, a dialog, NAME; FX's LED shows them. FX held: as before (an effect while its key is held). MENU >
 * FX LATCH is gone */
static int test_fx_latch(void)
{
    int bad = 0, ok;
    uint32_t i, lpf = white(4), m1 = black(0);
    ui_power_on();
    btn_down(B_FX); frame();
    turn(EN_K4, 5);  ok = perf_k[3] == 0;                         /* DEPTH 100 %: right stays at 100 */
    turn(EN_K4, -30); ok &= perf_k[3] == 30;                      /* left: 70 % */
    turn(EN_K4, 10); ok &= perf_k[3] == 20;                       /* right: 80 % */
    key_down(white(8)); frame(); perf_begin(CTL);                 /* OCT UP: KNOB 4 is SHIMR, right is more */
    turn(EN_K4, 10); ok &= perf_k[3] == 30;
    key_up(white(8)); frame(); btn_up(B_FX); frame();
    a_bar();                                                      /* ((2.0, D 2A) FX let go: back over a bar) */
    bad += check("#40 FX + KNOB 4: DEPTH rises to the right, falls to the left; SHIMR (OCT UP) rises to the right",
                 ok && !perf_k[3]);
    ui_power_on();
    btn_down(B_FX); key_down(lpf); frame(); key_up(lpf); frame(); btn_up(B_FX); frame();
    ok = !perf_latched && !perf_held && !perf_latch_on;
    for (i = 0; i < MI_COUNT; i++)
        ok &= !str_eq(MI_NAME[i], "FX LATCH");
    bad += check("(U2) FX held: a key's effect ends when it is let go; no MENU > FX LATCH", ok);
    ui_power_on();
    shift_lock(B_FX);
    key_down(lpf); frame(); key_up(lpf); frame();
    turn(EN_K2, 30);
    ok = perf_latch_on && perf_latched == PF_BIT(PF_LPF) && !perf_held && ui.layer == LAYER_FX &&
         str_eq(layer_head(), "[FX] LATCH") && (layer_leds(0) >> lpf) & 1u && perf_k[1] == 30;
    ok &= perf_begin(CTL) && (perf_act & PF_BIT(PF_LPF));                 /* it runs with nothing held */
    bad += check("(U2) SHIFT + FX: locked, it latches: C4 latches LPF, CRUSH stays at 30, nothing held", ok);
    key_down(m1); frame(); key_up(m1); frame();                   /* ((2.0, D 1A) a black key's tap: T1's MUTE, */
    ok = perf_latched == PF_BIT(PF_LPF) && trk[0].p[P_MUTE] == 1; /* not a cut latched) */
    key_down(lpf); frame(); key_up(lpf); frame();                 /* C4 again: LPF off */
    ok &= !perf_latched;
    turn(EN_K1, -40);
    btn_down(B_OCTDN); frame(); btn_up(B_OCTDN); frame();
    ok &= !perf_latched && !perf_k[0] && !perf_k[1] && song.octave == 0 && msg_is("FX ALL OFF") && ui.lock == LAYER_FX &&
          trk[0].p[P_MUTE] == 1;
    bad += check("  a black key's tap: T1's MUTE (no cut latched); C4 again: LPF off; OCT-: every latched effect and macro "
                 "off (no octave), the MUTE stays", ok);
    trk[0].p[P_MUTE] = 0;
    key_down(lpf); frame(); key_up(lpf); frame(); turn(EN_K2, 20);
    press(B_FX); frame();                                         /* its tap: closed, the effects off ((2.0, D 2A) */
    ok = !ui.lock && !ui.layer && !perf_latched && perf_k[1] > 0 && !perf_latch_on;   /* the macros on their way back) */
    a_bar();
    ok &= !perf_k[1];
    shift_lock(B_FX);
    key_down(lpf); frame(); key_up(lpf); frame();
    hold(B_HOME);
    ok &= ui.menu && !perf_latched && !ui.lock;
    press(B_HOME);
    bad += check("  its tap closes it and turns them off; so does the menu", ok && !ui.menu);
    return bad;
}

/* #48: the note divisions step by their length, longest first (triplets between their neighbours), on every list
 * that has them (SEQ DIV, ARP RATE, the delay's TIME, the SLICER's RATE); the stored values and their sound stay */
static int test_div_order(void)
{
    int bad = 0, ok = 1;
    uint32_t id, c, k, found = 0;
    static const param_desc_t *const LISTS[] = {&TP[P_SDIV], &TP[P_ARATE], &GP[G_DTIME], &TP[P_SLRATE]};
    ui_power_on();
    for (k = 0; k < NELEM(LISTS); k++) {           /* left end -> right end: each one shorter than the one before */
        const param_desc_t *d = LISTS[k];
        int32_t v = d->def, n, seen = 0;
        uint32_t prev = 0xFFFFFFFFu, len;
        for (n = 0; n < 40; n++) v = param_turn(d, v, -1);
        for (n = 0; n <= d->max - d->min; n++) {
            len = d->names == N_SLDIV ? 1000000u / SL_DEN[v] :
                  d->names == N_DTIME && v >= 10 ? (beat_samples() * 3u) >> DIV_DOT[v - 10] :   /* ((1.3) dotted) */
                  div_samples((uint32_t)v);
            if (!(len < prev && enum_rank(d, v) == d->min + n && !((seen >> v) & 1))) {
                printf("  %s: %s (value %d, place %d, length %u) after %u\n", d->label, d->names[v], (int)v,
                       (int)enum_rank(d, v), len, prev);
                ok = 0;
            }
            seen |= 1 << v;
            prev = len;
            v = param_turn(d, v, 1);
        }
        ok &= seen == (1 << (d->max - d->min + 1)) - 1;
    }
    ok &= str_eq(N_DIV[2], "1/16") && str_eq(N_DIV[4], "8T") && str_eq(N_DIV[9], "4BAR") && TP[P_SDIV].def == 2;
    ok &= param_turn(&TP[P_SDIV], 2, 1) == 5 && param_turn(&TP[P_SDIV], 2, -1) == 4 &&   /* 1/16: right 16T, left 8T */
          param_turn(&TP[P_SDIV], 3, 1) == 3 && param_turn(&TP[P_SDIV], 9, -1) == 9 &&   /* the ends hold */
          param_turn(&TP[P_SDIV], 2, -6) == 8 && param_turn(&TP[P_SDIV], 2, 40) == 3;    /* 6 left: 2BAR; far: clamped */
    bad += check("#48 DIV / RATE / TIME / SLICER RATE step longest -> shortest, every value once, the ends hold", ok);
    ok = 1;                                         /* the real knob on the pages that show them, the gauge follows */
    for (id = 0; id < NPAGES; id++)
        for (c = 0; c < 4u; c++) {
            int16_t *vp;
            const param_desc_t *d;
            ui_power_on();
            ui.home = 0; ui.page = (uint8_t)id; page_entered();
            if (!page_visible(id) || !(d = page_desc(cur_page(), c, &vp)) || !vp || !enum_order(d))
                continue;
            found++;
            *vp = d->names == N_DIV || d->names == N_DTIME ? 2 : 1;   /* 1/16 */
            turn(EN_K1 + c, 1);
            ok &= *vp == (d->names == N_DIV || d->names == N_DTIME ? 5 : 4);  /* 16T */
            turn(EN_K1 + c, -2);
            ok &= *vp == (d->names == N_DIV || d->names == N_DTIME ? 4 : 3);  /* 8T */
        }
    bad += check("#48 the pages' knobs step the divisions by length (SEQ, ARP, DLY, SLICER)", ok && found >= 4u);
    return bad;
}

/* #52 MENU > KNOB ACCEL (OFF by default, #23): ON, a sustained fast turn of a wide value goes x2..x4 (x8 over a range
 * above 64, #126); a slow turn, the start of a turn, a lone quick detent and a reversal are one step per detent; the
 * sign is always the detents' */
static uint32_t acc_ms(uint32_t role, const uint16_t *gaps, uint32_t n, int32_t dir, int32_t range, int32_t *out)
{
    uint32_t k, sum = 0;
    for (k = 0; k < n; k++) {
        fm1_ms += gaps[k];
        out[k] = accel(role, dir, range);
        sum += (uint32_t)(out[k] < 0 ? -out[k] : out[k]);
    }
    return sum;
}
static int test_knob_accel(void)
{
    int bad = 0, ok;
    uint32_t k, seed = 12345u;
    int32_t o[16];
    static const uint16_t SLOW[8] = {500, 60, 60, 45, 41, 60, 80, 60}, MID[8] = {500, 30, 30, 30, 26, 30, 30, 30},
                          F20[8] = {500, 20, 20, 20, 20, 20, 20, 20}, F10[8] = {500, 10, 10, 10, 10, 10, 10, 10},
                          BOUNCE[8] = {500, 2, 300, 1, 300, 3, 300, 2};
    ui_power_on();
    ok = acc_ms(EN_K1, F10, 8, 1, 127, o) == 8u;                  /* OFF (the default) */
    ui_prefs = PREF_ACCEL;
    memset(ui.enc_t, 0, sizeof ui.enc_t);
    ok &= acc_ms(EN_K1, SLOW, 8, 1, 127, o) == 8u && acc_ms(EN_K2, MID, 8, -1, 127, o) == 8u && o[7] == -1;
    ok &= acc_ms(EN_K3, BOUNCE, 8, 1, 127, o) == 8u;
    bad += check("#52 KNOB ACCEL: OFF by default; ON, slow (>= 26 ms a detent) and lone quick detents: 1 step each", ok);
    fm1_ms += 1000;
    acc_ms(EN_K1, F20, 8, 1, 64, o);
    ok = o[0] == 1 && o[1] == 1 && o[2] == 2 && o[7] == 2;          /* 20 ms a detent: x2 from the third */
    acc_ms(EN_K1, F10, 8, 1, 64, o);                               /* (a pause of 500 ms first: a new turn) */
    ok &= o[0] == 1 && o[1] == 1 && o[2] == 4 && o[7] == 4;         /* 10 ms: x4 (the cap of a range <= 64) */
    fm1_ms += 15; ok &= accel(EN_K1, 1, 64) == 3;                  /* 15 ms: x3 */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* reversed: one step, the right sign */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -1;                /* (the turn starts again) */
    fm1_ms += 10; ok &= accel(EN_K1, -1, 64) == -4;
    fm1_ms += 30; ok &= accel(EN_K1, -3, 64) == -12;               /* 3 detents in one read (10 ms each): x4 */
    bad += check("#52 KNOB ACCEL ON: 20 ms a detent x2, 15 ms x3, 10 ms x4 from a turn's third detent; reversal x1", ok);
    {   /* #126 a range above 64: 10 ms x5, 8 ms x6, 6 ms and less x8; a flick of 12 detents in ~120 ms, faster in
         * the middle, moves a -100..100 value ~50 (was 38); 12 even ones 52 */
        static const uint16_t FLICK[12] = {500, 16, 12, 10, 8, 7, 7, 8, 10, 12, 16, 20}, F6[8] = {500, 6, 6, 5, 4, 6, 6, 6};
        uint32_t f;
        fm1_ms += 1000;
        f = acc_ms(EN_K2, FLICK, 12, 1, 200, o);
        ok = f == 47u && o[0] == 1 && o[1] == 1;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F10, 8, -1, 200, o) == 2u + 6u * 5u && o[7] == -5;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 127, o) == 2u + 6u * 8u;
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 64, o) == 2u + 6u * 4u;      /* (a range <= 64: x4 at most) */
        fm1_ms += 1000;
        ok &= acc_ms(EN_K2, F6, 8, 1, 0, o) == 8u;                  /* (a list) */
        printf("  #126 a flick of 12 detents over %u ms on -100..100: %u steps\n", 126u, (unsigned)f);
        bad += check("#126 KNOB ACCEL ON over a range above 64: 10 ms x5 .. 6 ms x8; a 12-detent flick ~120 ms: 47", ok);
    }
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 32, o);
    ok = o[7] == 1;                                                /* a narrow range (<= 32): never */
    fm1_ms += 1000; acc_ms(EN_K4, F10, 8, 1, 0, o);
    ok &= o[7] == 1;                                               /* a list of names (range 0 at the callers) */
    for (k = 0; k < 20000u && ok; k++) {                           /* random turns: the sign is the detents', at */
        int32_t s, r, gap;                                         /* most x8, a slow detent exactly one step */
        seed = seed * 1103515245u + 12345u;
        s = (int32_t)((seed >> 16) % 7u) - 3;
        gap = (int32_t)((seed >> 8) % 90u);
        fm1_ms += (uint32_t)gap;
        r = accel(EN_K1 + (seed & 3u), s, 127);
        ok &= s ? (r > 0) == (s > 0) && r * s <= 8 * s * s && r * s >= s * s : r == 0;
        if (gap >= 40 * (s < 0 ? -s : s) && r != s) ok = 0;
    }
    bad += check("#52 KNOB ACCEL: never on narrow values or lists; 20000 random reads keep the sign, x1..x8, slow = x1", ok);
    {   /* the real knob: LEVEL on MIXER, a detent a frame (16 ms): 1 + 1 + 3 x 6 = 20 for 8 detents; OFF: 8 */
        int16_t v0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_title("MIXER");
        TSEL->p[P_LEVEL] = 40; v0 = TSEL->p[P_LEVEL];
        fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok = TSEL->p[P_LEVEL] - v0 == 20;
        ui_prefs = 0; TSEL->p[P_LEVEL] = 40; fm1_ms += 1000;
        for (k = 0; k < 8u; k++) turn(EN_K1, 1);
        ok &= TSEL->p[P_LEVEL] == 48;
        bad += check("#52 KNOB ACCEL on MIXER LEVEL: a fast turn of 8 detents moves 20 (OFF: 8)", ok);
    }
    {   /* #126 the FX layer's KNOB 1 (FILTER -100..100): a flick of 12 detents 10 ms apart ~52 (was 12: the layer's
         * knobs went round accel); OFF: 12; slow (60 ms): 12; 6 right then 6 left fast: back to 0; GLO's LEVEL too */
        static const uint32_t GAP[4] = {10u, 10u, 60u, 10u};
        static const uint32_t ON[4] = {1u, 0u, 1u, 1u};
        int32_t got[4], g;
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            ui_power_on();
            ui_prefs = ON[c] ? PREF_ACCEL : 0u;
            go_home(); frames(64);
            btn_down(B_FX); frames(560);
            for (k = 0; k < 12u; k++) {
                host_enc[panel.enc[EN_K1]] += (c == 3u && k >= 6u ? -1 : 1) * panel.dir[EN_K1];
                fm1_ms += GAP[c] - 16u;                         /* (frame() adds 16) */
                frame();
            }
            got[c] = perf_k[0];
            btn_up(B_FX); frames(400);
        }
        printf("  #126 FX FILTER, 12 detents: ON 10 ms %d, OFF %d, ON 60 ms %d, ON 6 right 6 left %d\n", (int)got[0],
               (int)got[1], (int)got[2], (int)got[3]);
        ok = got[0] >= 35 && got[0] <= 60 && got[1] == 12 && got[2] == 12 && got[3] == 0;
        ui_power_on(); ui_prefs = PREF_ACCEL;
        go_home(); frames(64);
        trk[1].p[P_LEVEL] = 0;
        btn_down(B_GLO); frames(560);
        for (k = 0; k < 12u; k++) {
            host_enc[panel.enc[EN_K2]] += panel.dir[EN_K2];
            fm1_ms -= 6u;
            frame();
        }
        g = trk[1].p[P_LEVEL];
        btn_up(B_GLO); frames(400);
        ok &= g > 12 + 12;
        bad += check("#126 KNOB ACCEL on the FX layer (FILTER: a 12-detent flick >= 35, OFF 12, slow 12) and GLO LEVEL", ok);
    }
    {   /* saved with the settings; settings from before it: OFF */
        sks_t p;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        ui_prefs = PREF_ACCEL;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_ACCEL;
        q = per3_of(&p);
        ok &= per3_import(&q) && !ui_prefs;
        bad += check("#52 KNOB ACCEL saved with the settings; settings from before it: OFF", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* #51 ("a melody appears when I change presets"): browsing every sound in the list (every engine's presets, then user
 * presets), on HOME and the PRESETS page, stopped and playing, on an empty track: no step is written, the ARP stays
 * off, nothing is held, the transport is not started, no message about a pattern. (In 0.9 a preset carried a 16-step
 * pattern and replaced the steps; no preset carries one now.) */
static int test_browse_no_pattern(void)
{
    int bad = 0, ok = 1;
    uint32_t pass, n, total, loads = 0;
    track_t *t;
    for (pass = 0; pass < 4u; pass++) {
        ui_power_on();
        t = TSEL;
        my_steps(t); t->p[P_SDIV] = 2;
        up_store(3, "MINE");                          /* (stored over steps: still the sound only) */
        up_store(5, "PLAIN");
        track_defaults_steps(t);
        ok &= seq_is_empty(t) && up_used(3);
        if (pass & 1u) song.playing = 1;              /* playing: the other tracks run, this one is empty */
        if (pass & 2u) go_page(GR_BROWSE);
        preset_pos(&total);
        for (n = 0; n < total + 2u; n++) {
            turn(EN_PRESET, 1);
            loads++;
            if (!seq_is_empty(t) || t->p[P_AMODE] || t->nheld || (!(pass & 1u) && (song.playing || transport_req))) {
                printf("  pass %u, sound %u (%s %u): steps %d ARP %d held %u playing %d\n", pass, n,
                       ENGINES[t->eng_req % NENGINES]->name, t->preset, !seq_is_empty(t), t->p[P_AMODE], t->nheld,
                       song.playing);
                ok = 0;
                break;
            }
        }
    }
    if (!ok || loads < 200u) printf("  ok %d loads %u\n", ok, loads);
    bad += check("#51 every sound browsed (HOME, PRESETS; stopped, playing): no step, ARP off, nothing held, no start",
                 ok && loads >= 200u);
    return bad;
}

/* MENU > USB LEVEL: MASTER (the default, every older setting) / FIXED; the audio follows at once (audio_test: the levels) */
static int test_usb_level(void)
{
    int bad = 0, ok;
    ui_power_on();
    frame();
    ok = !fx_usb_fixed && str_eq(MI_NAME[MI_USB], "USB LEVEL") && str_eq(menu_flag(MI_USB)->name[0], "MASTER");
    hold(B_HOME);
    ui.menu_sel = MI_USB;
    ui.force = 1; ui_draw();
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;      /* right: FIXED */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed;   /* left: MASTER */
    press(B_OCTUP); ok &= (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed && ui.menu;
    press(B_OCTDN); ok &= !(ui_prefs & PREF_USB_FIXED) && !fx_usb_fixed && ui.menu;
    press(B_OCTUP);
    press(B_HOME);
    ok &= !ui.menu && fx_usb_fixed;
    {
        sks_t p;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        settings_export(&p);
        ui_prefs = 0;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_USB_FIXED;
        q = per3_of(&p);
        ok &= per3_import(&q) && !ui_prefs;
        frame();
        ok &= !fx_usb_fixed;
    }
    bad += check("MENU > USB LEVEL: MASTER by default, KNOB 1 / OCT+ FIXED, left / OCT- MASTER, saved, the audio follows", ok);
    ui_prefs = 0;
    frame();
    return bad;
}

/* 1.1 (Discussion #131) MENU > AUDIO CLICK (OFF REC ON), CLICK LEVEL (LOW MID HIGH), COUNT-IN (OFF 1 BAR 2 BARS): OFF / MID /
 * OFF by default and in every older setting; a knob applies at once (click.c, seq.c); saved and read back, a stored 3
 * (no value) the default. The count-in from the UI: REC while stopped arms and counts in, the screen counts 4 3 2 1 (the
 * beat's cell moves), PLAY's green LED on each beat (its own dark), the STEP keys' entry paused, PLAY stops it (the page
 * back); at its end the sequencer plays and the page is back */
static uint16_t px_at(uint32_t x, uint32_t y) { return host_screen[y * 240u + x]; }
static void cin_beat(void)                       /* the audio until the count-in's next beat (or its end) */
{
    uint32_t left = cin_left, k;
    for (k = 0; k < 4000u && cin_left == left && seq_counting(); k++)
        events_block(CTL);
}
static int test_click_menu(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    frame();
    ok = !ui_rec_prefs && click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars &&
         str_eq(MI_NAME[MI_CLICK], "CLICK") && str_eq(MI_NAME[MI_CLKLVL], "CLICK LEVEL") && str_eq(MI_NAME[MI_COUNTIN], "COUNT-IN") &&
         MI_TAB[MI_CLICK] == MTAB_AUDIO && MI_TAB[MI_CLKLVL] == MTAB_AUDIO && MI_TAB[MI_COUNTIN] == MTAB_AUDIO &&
         menu_get(MI_CLICK) == 0u && menu_get(MI_CLKLVL) == 1u && menu_get(MI_COUNTIN) == 0u;
    for (i = 0; i < 3u; i++)
        ok &= str_eq(menu_vname(MI_CLICK, i), (const char *[]){"OFF", "REC", "ON"}[i]) &&
              str_eq(menu_vname(MI_CLKLVL, i), (const char *[]){"LOW", "MID", "HIGH"}[i]) &&
              str_eq(menu_vname(MI_COUNTIN, i), (const char *[]){"OFF", "1 BAR", "2 BARS"}[i]);
    bad += check("MENU > AUDIO: CLICK OFF / REC / ON, CLICK LEVEL LOW / MID / HIGH, COUNT-IN OFF / 1 BAR / 2 BARS; OFF MID OFF by default", ok);
    hold(B_HOME);
    ui.menu_sel = MI_LOWCUT; ui.force = 1; ui_draw();
    turn(EN_PRESET, 1); turn(EN_PRESET, 1);
    ok = ui.menu_sel == MI_CLICK;
    turn(EN_K1, 1); ok &= click_mode == CLICK_REC;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;
    turn(EN_K2, 1); ok &= click_mode == CLICK_ON;      /* (stops at the end) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_CLKLVL;
    press(B_OCTDN); ok &= click_lvl == 0u;
    press(B_OCTUP); press(B_OCTUP); ok &= click_lvl == 2u;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_COUNTIN;
    turn(EN_K4, 1); ok &= cin_bars == 1u;
    turn(EN_K4, 1); ok &= cin_bars == 2u;
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_TUNE;     /* ((U3) TUNE, AUDIO's last) */
    turn(EN_PRESET, 1); ok &= ui.menu_sel == MI_LOWCUT;   /* (wraps within AUDIO) */
    press(B_HOME);
    ok &= !ui.menu;
    bad += check("MENU > AUDIO: PRESETS to CLICK, CLICK LEVEL, COUNT-IN; knobs / OCT step them, applied at once", ok);
    {
        sks_t p;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        settings_export(&p);
        ui_rec_prefs = 0; frame();
        ok = settings_import(&p, (int)sizeof p) == 1 && (frame(), click_mode == CLICK_ON && click_lvl == 2u && cin_bars == 2u);
        p.s.rec = 3u | 3u << 2 | 1u << 4;   /* CLICK and LEVEL no value, COUNT-IN 1 BAR */
        ok &= settings_import(&p, (int)sizeof p) == 1 && p.s.rec == 1u << 4;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && cin_bars == 1u;
        q = per3_of(&p);   /* settings from before the favorites (PER3): the defaults */
        ok &= per3_import(&q) && !ui_rec_prefs;
        frame();
        ok &= click_mode == CLICK_OFF && click_lvl == 1u && !cin_bars;
    }
    bad += check("CLICK / CLICK LEVEL / COUNT-IN saved and read back; a stored 3 the default; older settings OFF / MID / OFF", ok);

    /* the count-in from the UI */
    ui_power_on();
    menu_put(MI_COUNTIN, 1u);
    go_page(GR_ROLL);
    frame();
    press(B_REC);                                       /* (#133: on STEP REC arms step recording, stopped) */
    ok = (song.rec & 1u) && transport_req == 0u && !live_rec_sel();
    press(B_PLAY);
    ok &= transport_req == 1u;
    events_block(CTL);
    ok &= seq_counting() && !song.playing && cin_left == 4u && live_rec_sel();
    frame();
    {
        uint16_t now0 = px_at(16, CI_Y + 4), next0 = px_at(16 + 56, CI_Y + 4);
        ok &= now0 != next0;                            /* the first beat's cell lit, the next not */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;                  /* PLAY's green: on the beat */
        for (i = 0; i < 400u; i++) events_block(CTL);    /* (~0.3 s: past the first quarter of the beat) */
        ui_leds();
        ok &= !(fm1_led[8] & 2u);
        cin_beat();
        frame();
        ok &= cin_left == 3u && px_at(16 + 56, CI_Y + 4) == now0 && px_at(16, CI_Y + 4) == next0;   /* the cell moved */
        ui_leds();
        ok &= (fm1_led[8] & 2u) != 0u;
    }
    bad += check("count-in from the UI: REC on STEP arms, PLAY counts in (STEP entry paused); 4 3 2 1 on screen; PLAY's green on the beats", ok);
    press(B_PLAY);
    ok = transport_req == 2u;
    events_block(CTL);
    frame();
    ok &= !seq_counting() && !song.playing && (song.rec & 1u) && px_at(16, CI_Y + 4) == px_at(16 + 56, CI_Y + 4);
    bad += check("PLAY while counting in stops it: nothing plays, still armed, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    ok = seq_counting() && cin_left == 4u;
    for (i = 0; i < 4u; i++) cin_beat();
    frame();
    ok &= !seq_counting() && song.playing && trk[0].seq_idx == 0u && px_at(16, CI_Y + 4) == px_at(16 + 56, CI_Y + 4);
    bad += check("PLAY armed and stopped: the count-in again; at its end the sequencer plays from step 1, the page back", ok);
    press(B_PLAY);
    events_block(CTL);
    menu_put(MI_COUNTIN, 0u);
    stop_transport();
    return bad;
}

/* #67 MENU > USB SERIAL: ON by default (and in every older setting): the console presented, the descriptors as
 * before. OFF: audio + MIDI only, device class 0, bcdUSB 1.10 (CFG_DESC_PLAIN: a FELUCCA_CDC=0 build's bytes), applied
 * when the menu closes (one re-enumeration); saved; at boot the saved choice before USB starts (never on then off) */
static int dev_class(void)                              /* the device class GET_DESCRIPTOR sends now (-1: none) */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u ? d[4] : -1;
}
static int dev_is(const uint8_t *want)                  /* the device descriptor sent now = want */
{
    const uint8_t *d;
    uint16_t n;
    return get_desc(0x0100, &d, &n) && n == 18u && !memcmp(d, want, 18);
}
static int plain_desc(void)                             /* device + configuration = the console-less bytes */
{
    const uint8_t *d, *c;
    uint16_t n, m;
    return get_desc(0x0100, &d, &n) && get_desc(0x0200, &c, &m) && n == 18u && !memcmp(d, DEV_DESC_PLAIN, 18) &&
           m == sizeof CFG_DESC_PLAIN && !memcmp(c, CFG_DESC_PLAIN, m);
}
#if FELUCCA_CDC
static int test_usb_serial(void)
{
    int bad = 0, ok;
    ui_power_on();
    usb.up = 0;                                          /* (no SIE here: usb_cdc_switch only flips the descriptors) */
    frame();
    ok = usb_cdc_on && dev_class() == 0xEF && !(ui_prefs & PREF_SERIAL_OFF) && str_eq(MI_NAME[MI_SERIAL], "USB SERIAL") &&
         str_eq(menu_flag(MI_SERIAL)->name[0], "ON") && str_eq(menu_flag(MI_SERIAL)->name[1], "OFF");
    ok &= dev_is(DEV_DESC);
    bad += check("#67 MENU > USB SERIAL: ON by default, the console presented (misc / IAD, as before)", ok);
    hold(B_HOME);
    ui.menu_sel = MI_SERIAL;
    ui.force = 1; ui_draw();
    turn(EN_K1, -1);
    ok = (ui_prefs & PREF_SERIAL_OFF) && usb_cdc_on;    /* left: OFF; applied when the menu closes */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_SERIAL_OFF);   /* right: ON (the switch's knob right) */
    press(B_OCTDN); ok &= (ui_prefs & PREF_SERIAL_OFF) && ui.menu && usb_cdc_on;   /* OCT-: OFF */
    press(B_HOME);
    ok &= ui.menu == 0 && usb_cdc_on;                 /* (HOME acts as it is let go: closed this frame) */
    frame();
    ok &= !ui.menu && !usb_cdc_on && !usb.config && plain_desc() && dev_class() == 0;
    bad += check("  KNOB 1 left / OCT- OFF, right ON; the menu closed (HOME): audio + MIDI only, class 0", ok);
    {
        sks_t p;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;
        ok &= settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_SERIAL_OFF;     /* (again: the same) */
        /* boot (main.c): the settings, usb_serial_apply, then usb_start: the console-less descriptors from the start */
        usb_cdc_on = 1;
        usb.up = 0;
        usb_serial_apply();
        ok &= !usb_cdc_on && !usb.up && plain_desc();
        q = per3_of(&p);   /* settings from before it (PER3): ON */
        ok &= per3_import(&q) && !(ui_prefs & PREF_SERIAL_OFF);
        usb_serial_apply();
        ok &= usb_cdc_on && dev_is(DEV_DESC);
    }
    bad += check("  saved with the settings (older ones: ON); at boot the device enumerates with the saved choice", ok);
    {   /* OFF: the soft key and the update frames still come in (USB-MIDI SysEx, EP1) */
        static const uint8_t KEY[8] = {0x04, 0xF0, 0x22, 0x24, 0x07, 0x35, 0x7D, 0xF7};
        ui_prefs = PREF_SERIAL_OFF;
        frame();
        usb.uboot_req = 0;
        ok = !usb_cdc_on && ep1_take(KEY, sizeof KEY) && usb.uboot_req;
        usb.uboot_req = 0;
        bad += check("  OFF: the soft key (SysEx on the MIDI endpoint) still asks for UPDATE MODE", ok);
    }
    ui_prefs = 0;
    frame();
    return bad;
}
#else
static int test_usb_serial(void)                       /* ((Skiff 2.0) a release: no console, no USB SERIAL row) */
{
    uint32_t r;
    int ok = MI_SERIAL >= MI_COUNT && dev_class() == 0;
    for (r = 0; r < MI_COUNT; r++)
        ok &= !str_eq(MI_NAME[r], "USB SERIAL");
    return check("(Skiff 2.0) FELUCCA_CDC=0 (release): no USB SERIAL row, audio + MIDI only (class 0)", ok);
}
#endif

/* #58 MENU > BPM LOCK: OFF by default (and in every older setting): SELECT sets the tempo on every page. ON: SELECT
 * leaves it on the pages ("BPM LOCKED" once per turn burst); GLO > GLOBAL's BPM knob, GLO + SELECT (a combo: the map
 * opens, no GLO page; OCT- puts it back) and GLO's G4 TAP still set it; the menu never takes SELECT as the tempo */
static int test_bpm_lock(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t i, page;
    ui_power_on();
    ui_prefs = 0;
    frame();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 2);
    ok = song.g[G_BPM] == b0 + 2 && !msg_is("BPM LOCKED") && str_eq(menu_flag(MI_BPMLOCK)->name[0], "OFF");
    bad += check("#58 BPM LOCK OFF (the default): SELECT sets the tempo as before", ok);

    hold(B_HOME);
    ui.menu_sel = MI_BPMLOCK;
    ui.force = 1; ui_draw();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 3);
    ok = str_eq(MI_NAME[MI_BPMLOCK], "BPM LOCK") && ui.menu == 1u && song.g[G_BPM] == b0;   /* (the menu: no tempo) */
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_BPM_LOCK) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_BPM_LOCK);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_BPM_LOCK) && ui.menu;      /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_BPM_LOCK) && ui.menu;     /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_BPM_LOCK;                                  /* (no other setting moved) */
    press(B_HOME);
    bad += check("#58 MENU > BPM LOCK: KNOB 1 right / OCT+ ON, left / OCT- OFF; SELECT in the menu is no tempo", ok && !ui.menu);

    /* ON: no tempo from SELECT; on HOME "BPM LOCKED" once per burst; (Skiff phase 8) on a page it turns the pages */
    frame();
    ui.msg_t = 0; fm1_ms += 1200u;
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 1);
    ok = ui.home && song.g[G_BPM] == b0 && msg_is("BPM LOCKED") && !ui.bpm_t;
    ui.msg_t = 0;
    turn(EN_SELECT, -4);                                              /* (the same burst: said once) */
    ok &= song.g[G_BPM] == b0 && !ui.msg_t;
    fm1_ms += 1200u;
    turn(EN_SELECT, 1);
    ok &= msg_is("BPM LOCKED");                                       /* (a new burst: said again) */
    ok &= text_w(&AF_S, "BPM LOCKED") <= 236 - 106;                   /* (fits the header's message zone) */
    bad += check("#58 BPM LOCK ON: SELECT leaves the tempo; on HOME \"BPM LOCKED\" once per turn burst", ok);
    for (i = 0, ok = 1; i < 2u; i++) {                                /* (Skiff phase 8, rule 3) the pages instead */
        go_title(i ? "NOTES" : "ENV"); frame();
        ui.msg_t = 0; fm1_ms += 1200u;
        b0 = song.g[G_BPM];
        turn(EN_SELECT, 1);
        ok &= str_eq(cur_page()->title, i ? "FEEL" : "ENV 2") && song.g[G_BPM] == b0 && !ui.msg_t && !ui.bpm_t;
    }
    bad += check("(Skiff phase 8) BPM LOCK ON: on a page SELECT turns the pages (ENV 2, FEEL), no tempo, no message", ok);

    /* ON: GLO > GLOBAL's BPM knob */
    go_title("GLOBAL"); frame();
    b0 = song.g[G_BPM];
    turn(EN_K1, 1);
    ok = str_eq(cur_page()->title, "GLOBAL") && cur_page()->id[0] == G_BPM && song.g[G_BPM] == b0 + 1;
    bad += check("#58 BPM LOCK ON: GLO > GLOBAL KNOB 1 still sets the tempo", ok);

    /* ON: GLO + SELECT (a quick turn: a combo, no GLO page), OCT- puts it back, G4 TAP */
    go_title("ENV"); frame();
    page = ui.page;
    b0 = song.g[G_BPM];
    ui.msg_t = 0;
    btn_down(B_GLO); frame();
    turn(EN_SELECT, 3);
    ok = song.g[G_BPM] == b0 + 3 && ui.bpm_t && !msg_is("BPM LOCKED") && ui.layer == LAYER_GLO;
    btn_up(B_GLO); frame();
    ok &= ui.page == page && !ui.home;                                /* (no tap: still ENV) */
    btn_down(B_GLO); frame();
    turn(EN_SELECT, -5);
    ok &= song.g[G_BPM] == b0 - 2;
    oct_back();
    ok &= song.g[G_BPM] == b0 + 3;                                    /* (OCT-: the tempo as this layer opened) */
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + SELECT sets the tempo (a combo: the map, no GLO page; OCT- puts it back)",
                 ok && ui.page == page);
    song.g[G_BPM] = 100;
    btn_down(B_GLO); frame();
    for (i = 0; i < 3u; i++) { key_down(white(8)); frame(); key_up(white(8)); frames(480); }
    ok = song.g[G_BPM] == 120;
    btn_up(B_GLO); frame();
    bad += check("#58 BPM LOCK ON: GLO + G4 TAP still sets the tempo", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        sks_t p, t;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_BPM_LOCK;
        t = p;
        ok &= settings_import(&t, (int)sizeof t) == 1 && ui_prefs == PREF_BPM_LOCK && !memcmp(&p, &t, sizeof p);
        q = per3_of(&p);
        ok &= per3_import(&q) && !ui_prefs;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        frame();
        b0 = song.g[G_BPM];
        go_home(); frame();
        turn(EN_SELECT, 1);
        ok &= song.g[G_BPM] == b0 + 1;
        bad += check("#58 BPM LOCK saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* (Skiff 2.0, D: design/mockups/v2_notes.png, the user's step editing) NOTES' selection: SHIFT + PRESETS a range (a SHIFT
 * combo: no HOME, no MENU), a key (REC off) picks its note; PITCH along the scale, VELOCITY by the same amount, START
 * with the ties (refused onto a note), LENGTH a GATE then ties; a picked note out of its chord; FEEL sets every step;
 * SHIFT + OCT- / OCT+ copy and paste (into a chord; refused past 4 notes); (#31) REC on, a chord held: PRESETS its
 * LENGTH, let go: past it */
static uint32_t key_of(uint32_t note)              /* the key that plays note now (27: none) */
{
    uint32_t k;
    for (k = 0; k < 27u && kb_map(TSEL, k) != note; k++)
        ;
    return k;
}
static int notes_are(const step_t *s, uint32_t n, const uint8_t *want)
{
    uint32_t k;
    for (k = 0; k < n && s->note[k] == want[k]; k++)
        ;
    return s->n == n && k == n;
}
static void shift_turn(int32_t s) { btn_down(B_HOME); turn(EN_PRESET, s); btn_up(B_HOME); frame(); }
static void shift_press(uint32_t label) { btn_down(B_HOME); press(label); btn_up(B_HOME); frame(); }
static int test_notes_sel(void)
{
    static const uint8_t AM7[4] = {57, 60, 64, 67}, BM[4] = {59, 62, 65, 69}, FM7[4] = {53, 57, 60, 64},
                         GM[4] = {55, 59, 62, 65}, FD[4] = {53, 57, 62, 64}, F3[3] = {53, 57, 64}, D4[1] = {62},
                         D4F[4] = {62, 53, 57, 64};
    track_t *t;
    int bad = 0, ok;
    int16_t v;
    ui_power_on();
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->p[P_ROOT] = 9; t->p[P_SCALE] = 2;            /* A MINOR */
    t->p[P_SGATE] = 64;
    t->step[0] = (step_t){{57, 60, 64, 67}, 4, ST_NOTE, 0, 100};   /* Am7, two steps (a TIE on 2) */
    t->step[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[4] = (step_t){{53, 57, 60, 64}, 4, ST_NOTE, 0, 90};    /* Fmaj7 on 5 */
    go_page(GR_ROLL); frame();
    shift_turn(4);
    ok = sel_lo() == 0u && sel_hi() == 4u && str_eq(page_head(), "NOTES 1-5") && !ui.menu && !ui.home &&
         cur_page()->graph == GR_ROLL;
    bad += check("(2.0) SHIFT + PRESETS: steps 1-5 selected (the header says so); a SHIFT combo: no HOME, no MENU", ok);
    turn(EN_K1, 1);
    ok = notes_are(&t->step[0], 4u, BM) && notes_are(&t->step[4], 4u, GM);
    turn(EN_K1, -1);
    ok &= notes_are(&t->step[0], 4u, AM7) && notes_are(&t->step[4], 4u, FM7);
    turn(EN_K4, -10);
    ok &= t->step[0].vel == 90u && t->step[4].vel == 80u;
    bad += check("  PITCH: both chords a step up A minor (A C E G: B D F A) and back; VELOCITY: each 10 down", ok);
    turn(EN_K2, 1);
    ok = notes_are(&t->step[1], 4u, AM7) && t->step[2].time == ST_TIE && notes_are(&t->step[5], 4u, FM7) &&
         step_empty(&t->step[0]) && sel_lo() == 1u && sel_hi() == 5u;
    turn(EN_K2, -1);
    ok &= notes_are(&t->step[0], 4u, AM7) && t->step[1].time == ST_TIE && notes_are(&t->step[4], 4u, FM7);
    t->step[5] = (step_t){{40}, 1, ST_NOTE, 0, 100};
    turn(EN_K2, 1);
    ok &= msg_is("STEP 6 TAKEN") && notes_are(&t->step[4], 4u, FM7) && t->step[5].note[0] == 40u;
    t->step[5] = (step_t){{0}, 0, ST_REST, 0, 0};
    bad += check("  START: the range a step later with its tie, and back; onto a note: nothing moves, said", ok);
    turn(EN_PRESET, -4);                               /* (the cursor alone: the range goes) */
    ok = !nsel.a1 && ui.cursor == 0u;
    turn(EN_K3, 1);
    ok &= step_span(t, 0) == 3u && t->step[2].time == ST_TIE;
    turn(EN_K3, -2);                                   /* (2 STEPS first) */
    ok &= step_span(t, 0) == 2u;
    turn(EN_K3, -1);
    ok &= step_span(t, 0) == 1u && step_empty(&t->step[1]) && motion_lock_get(t, 0, P_SGATE, &v) && v == 127;
    turn(EN_K3, -63);
    ok &= !motion_lock_get(t, 0, P_SGATE, &v);         /* (the track's GATE: no lock) */
    bad += check("  LENGTH: 2 STEPS to 3 (a tie more), back to 100 % (its ties gone, a GATE lock), the track's (none)", ok);
    turn(EN_PRESET, 4);
    tap_key(key_of(60));                               /* C4 picked in Fmaj7 */
    ok = nsel.picked && nsel.vp[4] == 1u << 2;
    turn(EN_K1, 1);
    ok &= notes_are(&t->step[4], 4u, FD);
    turn(EN_K2, 1);
    ok &= notes_are(&t->step[4], 3u, F3) && notes_are(&t->step[5], 1u, D4) && ui.cursor == 5u && nsel.vp[5] == 1u &&
          t->step[5].vel == 80u;
    bad += check("  a key picks its note: PITCH moves C4 alone (D4), START takes it out of the chord (its values along)", ok);
    turn(EN_PRESET, -1);                               /* (the picks go with the cursor) */
    shift_press(B_OCTDN);
    ok = clip.n == 1u && msg_is("COPIED 1 STEP") && !ui.menu && !ui.home;
    turn(EN_PRESET, 4);
    shift_press(B_OCTUP);
    ok &= notes_are(&t->step[8], 3u, F3) && sel_lo() == 8u && sel_hi() == 8u;
    turn(EN_PRESET, -3);
    shift_press(B_OCTUP);
    ok &= notes_are(&t->step[5], 4u, D4F);             /* (into D4's step) */
    turn(EN_PRESET, -5);
    shift_press(B_OCTUP);
    ok &= msg_is("STEP 1 TAKEN") && notes_are(&t->step[0], 4u, AM7);
    hold(B_SAVE);
    ok &= notes_are(&t->step[5], 1u, D4);              /* (SAVE held: the last paste back) */
    bad += check("  SHIFT + OCT- copies, SHIFT + OCT+ pastes (an empty step, into a chord; past 4 notes refused, said)", ok);
    go_page(GR_FEEL); frame();
    cursor_set(0);
    shift_turn(4);
    turn(EN_K4, 1);
    turn(EN_K1, -50);
    ok = (t->step[0].flags & SF_ACCENT) && (t->step[4].flags & SF_ACCENT) && step_chance(&t->step[0]) == 50u &&
         step_chance(&t->step[4]) == 50u && nsel.view == 0u;
    bad += check("  FEEL: TYPE ACC and CHANCE 50 % on every selected step", ok);
    go_page(GR_ROLL); frame();
    cursor_set(10);
    song.rec = 1u << song.sel;                         /* (#133: armed, stopped: step entry) */
    key_down(key_of(60)); frame();
    turn(EN_PRESET, 2);
    key_up(key_of(60)); frame();
    song.rec = 0;
    ok = t->step[10].note[0] == 60u && t->step[11].time == ST_TIE && t->step[12].time == ST_TIE && ui.cursor == 13u;
    bad += check("  (#31) REC on, a key held: PRESETS its LENGTH (two ties); let go, the cursor past it", ok);
    ok = str_eq(page_head(), "NOTES 14");
    turn(EN_PRESET, -1);
    ok &= ui.cursor == 10u && str_eq(page_head(), "NOTES 11") && sel_first(t) == 10u;
    turn(EN_PRESET, 1);
    ok &= ui.cursor == 13u && step_empty(&t->step[13]);
    turn(EN_K1, 1);
    ok &= step_empty(&t->step[13]) && sel_first(t) == NSTEP;
    bad += check("  PRESETS over a note's ties (back: its first step, on: past its end), the header the cursor's step; "
                 "PITCH on an empty step writes nothing", ok);
    return bad;
}

/* (2.0, the user's step 1 of #25's plan) deleting on NOTES: EDIT (REC armed) clears the cursor step with its notes'
 * ties; PRESETS lands on a TIE that holds no note; SHIFT + EDIT deletes the picked notes, or the selection's, REC off,
 * one undo, and locks no layer there (elsewhere it still locks EDIT's) */
static int test_notes_delete(void)
{
    static const uint8_t AM7[4] = {57, 60, 64, 67}, AM7_NO_C[3] = {57, 64, 67};
    track_t *t;
    int bad = 0, ok;
    ui_power_on();
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->step[0] = (step_t){{57, 60, 64, 67}, 4, ST_NOTE, 0, 100};   /* Am7, three steps long */
    t->step[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[2] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[4] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[6] = (step_t){{0}, 0, ST_TIE, 0, 0};                    /* (after an empty step: it holds no note) */
    go_page(GR_ROLL); frame();
    cursor_set(4);
    turn(EN_PRESET, 1);
    ok = ui.cursor == 5u;
    turn(EN_PRESET, 1);
    ok &= ui.cursor == 6u;
    bad += check("(2.0) PRESETS lands on a TIE that holds no note (an empty step to the eye)", ok);
    cursor_set(0);
    song.rec = 1u << song.sel;
    press(B_EDIT);
    ok = step_empty(&t->step[0]) && step_empty(&t->step[1]) && step_empty(&t->step[2]) && msg_is("STEP CLEARED") &&
         ui.cursor == 1u;
    hold(B_SAVE);
    ok &= notes_are(&t->step[0], 4u, AM7) && t->step[1].time == ST_TIE && t->step[2].time == ST_TIE;
    song.rec = 0;
    bad += check("  EDIT, REC armed: the cursor step cleared with its notes' ties; SAVE held puts them back", ok);
    cursor_set(0);
    tap_key(key_of(60));                               /* C4 picked in Am7 (REC off) */
    shift_press(B_EDIT);
    ok = notes_are(&t->step[0], 3u, AM7_NO_C) && t->step[1].time == ST_TIE && msg_is("DELETED 1 NOTE") && !ui.lock &&
         !ui.home && cur_page()->graph == GR_ROLL;
    hold(B_SAVE);
    ok &= notes_are(&t->step[0], 4u, AM7);
    bad += check("  SHIFT + EDIT, REC off: the picked C4 deleted (its chord and ties stay), no layer locked; undone", ok);
    cursor_set(0);
    shift_turn(4);
    shift_press(B_EDIT);
    ok = step_empty(&t->step[0]) && step_empty(&t->step[1]) && step_empty(&t->step[2]) && step_empty(&t->step[4]) &&
         t->step[6].time == ST_TIE && msg_is("DELETED 5 NOTES") && !nsel.a1;
    cursor_set(6);
    shift_press(B_EDIT);
    ok &= step_empty(&t->step[6]) && msg_is("NO NOTES");
    bad += check("  SHIFT + EDIT on steps 1-5: every note starting there with its ties; a TIE holding no note goes too",
                 ok);
    go_home(); frame();
    shift_press(B_EDIT);
    ok = ui.lock == LAYER_EDIT;
    shift_press(B_EDIT);
    ok &= !ui.lock;
    bad += check("  elsewhere (HOME) SHIFT + EDIT still locks EDIT's layer, and again closes it", ok);
    return bad;
}

/* (2.0, #25 plan step 3) NOTES on notes that overlap: C4 held four steps (TIEs on 2..4), E4 on step 5. START takes E4
 * back past C4's end (C4 keeps its length: step 4, then 3, hold both, C4 tied), and forward again leaves the steps as
 * they were, byte for byte; LENGTH gives a picked note of a chord its own; the 4-note limit, a pitch twice and lane
 * hits stop a note going on; PITCH takes a note's tied steps along; SHIFT + EDIT, copy and paste, step entry over a
 * long note; unfolded and folded back, a pattern is as it was */
static int test_notes_overlap(void)
{
    static step_t was[NSTEP], w[NSTEP];
    static const uint8_t AM7[4] = {57, 60, 64, 67};
    track_t *t;
    int bad = 0, ok;
    uint32_t c4;
    ui_power_on();
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->p[P_SGATE] = 64;
    t->p[P_VOICE] = V_POLY;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    t->step[1] = t->step[2] = t->step[3] = (step_t){{0}, 0, ST_TIE, 0, 0};
    t->step[4] = (step_t){{64}, 1, ST_NOTE, 0, 90};
    memcpy(was, t->step, sizeof was);
    ed_open(w, t, 16);
    ed_close(t, w, 16, 0);
    bad += check("(2.0, #25) a pattern unfolded and folded back: byte for byte as it was", !memcmp(was, t->step, sizeof was));
    go_page(GR_ROLL); frame();
    cursor_set(4);
    turn(EN_K2, -1);
    ok = note_span(t, 0, 0) == 4u && t->step[1].time == ST_TIE && t->step[2].time == ST_TIE &&
         t->step[3].time == ST_NOTE && t->step[3].n == 2u && step_tied(&t->step[3], (uint32_t)note_at(&t->step[3], 60)) &&
         !step_tied(&t->step[3], (uint32_t)note_at(&t->step[3], 64)) && step_empty(&t->step[4]) && ui.cursor == 3u &&
         t->step[3].vel == 90u;
    turn(EN_K2, -1);
    ok &= note_span(t, 0, 0) == 4u && t->step[2].n == 2u && t->step[3].time == ST_NOTE && t->step[3].n == 1u &&
          step_tied(&t->step[3], 0) && t->step[3].note[0] == 60u && ui.cursor == 2u;
    bad += check("  START: E4 back past C4's end, one step and two: C4 four steps still, E4 starting beside it (values "
                 "along)", ok);
    turn(EN_K2, 1);
    turn(EN_K2, 1);
    bad += check("  .. and forward again: the steps byte for byte as they were", !memcmp(was, t->step, sizeof was));

    cursor_set(4);                                     /* (E4 in C4 again, on step 3) */
    turn(EN_K2, -1); turn(EN_K2, -1);
    cursor_set(0);
    turn(EN_K1, 1);
    c4 = (uint32_t)scale_step(t, 60, 1);
    ok = t->step[0].note[0] == c4 && t->step[1].time == ST_TIE && note_at(&t->step[2], c4) >= 0 &&
         note_at(&t->step[2], 64) >= 0 && note_at(&t->step[3], c4) >= 0 && note_span(t, 0, 0) == 4u;
    turn(EN_K1, -1);
    ok &= t->step[0].note[0] == 60u && note_at(&t->step[3], 60) >= 0;
    bad += check("  PITCH: C4 with the steps it goes on (through E4's), and back", ok);
    cursor_set(2);
    shift_press(B_EDIT);
    ok = msg_is("DELETED 1 NOTE") && !memcmp(was, t->step, 4u * sizeof *was) && step_empty(&t->step[4]);
    bad += check("  SHIFT + EDIT on E4's step: E4 gone, C4 as it was (its TIEs again)", ok);

    t->step[8] = (step_t){{57, 60, 64, 67}, 4, ST_NOTE, 0, 100};   /* Am7, its C4 picked: two steps of its own */
    cursor_set(8);
    tap_key(key_of(60));
    turn(EN_K3, 64);
    turn(EN_K3, 1);
    ok = notes_are(&t->step[8], 4u, AM7) && t->step[9].time == ST_NOTE && t->step[9].n == 1u &&
         t->step[9].note[0] == 60u && step_tied(&t->step[9], 0) && note_span(t, 8, 1) == 2u && note_span(t, 8, 0) == 1u &&
         len_get(t, 8) == LEN_STEPS;
    bad += check("  LENGTH: a picked note of a chord its own (C4 two steps, A E G one)", ok);
    t->step[7] = (step_t){{62}, 1, ST_NOTE, 0, 100};
    cursor_set(7);
    turn(EN_K3, 64);
    turn(EN_K3, 1);
    ok = msg_is("STEP 9 TAKEN") && note_span(t, 7, 0) == 1u;
    t->step[10] = (step_t){{50}, 1, ST_NOTE, 0, 100};
    t->step[11] = (step_t){{50}, 1, ST_NOTE, 0, 100};
    cursor_set(10);
    turn(EN_K3, 64);
    turn(EN_K3, 1);
    ok &= msg_is("STEP 12 TAKEN") && note_span(t, 10, 0) == 1u;
    t->step[12] = (step_t){{52}, 1, ST_NOTE, 0, 100};
    t->step[13] = (step_t){{40}, 1, ST_NOTE, 0, 100, 1u};   /* (a lane hit on a melodic step) */
    cursor_set(12);
    turn(EN_K3, 64);
    turn(EN_K3, 1);
    ok &= msg_is("STEP 14 TAKEN") && note_span(t, 12, 0) == 1u;
    bad += check("  a note goes on only where there is room: not into 4 notes, a pitch there already, lane hits", ok);

    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    memcpy(t->step, was, sizeof was);                  /* (C4 four steps, E4 on 5) */
    cursor_set(4);
    turn(EN_K2, -1); turn(EN_K2, -1);                 /* (E4 on 3 in C4) */
    cursor_set(0);
    shift_turn(2);
    shift_press(B_OCTDN);                              /* steps 1-3: C4 (four steps) and E4 */
    cursor_set(8);
    shift_press(B_OCTUP);
    ok = msg_is("PASTED") && t->step[8].note[0] == 60u && note_span(t, 8, 0) == 4u && note_at(&t->step[10], 64) >= 0 &&
         !step_tied(&t->step[10], (uint32_t)note_at(&t->step[10], 64)) && note_at(&t->step[11], 60) >= 0;
    bad += check("  copy C4 with E4 in it, paste on step 9: the same notes, each as long as it was", ok);

    cursor_set(1);                                     /* (on C4's second step: REC, a key) */
    song.rec = 1u << song.sel;
    key_down(key_of(67)); frame();
    key_up(key_of(67)); frame();
    song.rec = 0;
    ok = note_span(t, 0, 0) == 4u && note_at(&t->step[1], 67) >= 0 && !step_tied(&t->step[1], (uint32_t)note_at(&t->step[1], 67)) &&
         note_at(&t->step[1], 60) >= 0 && step_tied(&t->step[1], (uint32_t)note_at(&t->step[1], 60));
    bad += check("  step entry over a long note: G4 on C4's second step, C4 going on through it", ok);

    ui_power_on();                                     /* (2.0, the user's standard) a new pattern's notes: 1 STEP */
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 100};
    go_page(GR_ROLL); frame();
    cursor_set(0);
    ok = t->p[P_SGATE] == 127 && len_get(t, 0) == LEN_STEPS - 1;
    turn(EN_K3, 1);
    ok &= note_span(t, 0, 0) == 2u && len_get(t, 0) == LEN_STEPS;
    turn(EN_K3, -1);
    ok &= note_span(t, 0, 0) == 1u && len_get(t, 0) == LEN_STEPS - 1 && !motion_lock_get(t, 0, P_SGATE, &(int16_t){0});
    bad += check("  LENGTH on a new pattern starts at 1 STEP (GATE 100 %): a detent up 2 STEPS, down again 1 STEP, no lock",
                 ok);
    return bad;
}

/* (2.0, the user's ask) NOTES' DRUM grid: SHIFT + OCT- copies the LANE card's lane, SHIFT + OCT+ pastes it onto the
 * LANE shown then (a KICK figure onto the CLAP); SHIFT + OCT- again on the same steps and lane copies every lane, pasted
 * as it was; FEEL (no LANE card) copies every lane */
static int test_drum_lane_copy(void)
{
    track_t *t = &trk[0];
    int bad = 0, ok;
    uint32_t i, h;
    ui_power_on();
    set_engine_of(t, ENGI_DRUM);
    t->engine = t->eng_req;
    track_defaults_steps(t);
    t->p[P_SLEN] = 32;
    for (i = 0; i < 16u; i++)                          /* KICK on the beats (1 accented), CL HAT on the eighths */
        if ((h = (i % 4u ? 0u : 1u << DV_KICK) | (i % 2u ? 0u : 1u << DV_HATC)) != 0u)
            t->step[i] = (step_t){{0}, 0, ST_NOTE, 0, 100, (uint8_t)h, (uint8_t)(i ? 0u : 1u << DV_KICK)};
    open_family(FAM_SEQ);
    frame();
    ui.lane = DV_KICK;
    cursor_set(0);
    shift_turn(15);
    shift_press(B_OCTDN);
    ok = grid_on() && clip.lane == DV_KICK && clip.n == 13u && msg_is("COPIED KICK 13 STEPS") &&
         clip.step[0].hit == 1u << DV_KICK && clip.step[0].acc == 1u << DV_KICK && clip.step[4].hit == 1u << DV_KICK &&
         !clip.step[2].hit;
    bad += check("  DRUM grid: SHIFT + OCT- copies the LANE's hits alone (KICK, its accent), from its first", ok);
    ui.lane = DV_CLAP;
    cursor_set(0);
    shift_press(B_OCTUP);
    ok = msg_is("PASTED ON CLAP") && ((t->step[0].acc >> DV_CLAP) & 1u) && !((t->step[4].acc >> DV_CLAP) & 1u);
    for (i = 0; i < 16u; i++)
        ok &= ((t->step[i].hit >> DV_CLAP) & 1u) == (i % 4u == 0u && i <= 12u) &&
              (t->step[i].hit & (1u << DV_KICK | 1u << DV_HATC)) == ((i % 4u ? 0u : 1u << DV_KICK) | (i % 2u ? 0u : 1u << DV_HATC));
    bad += check("  SHIFT + OCT+ pastes them onto the LANE shown (CLAP, the accent too); KICK and CL HAT as they were", ok);
    ui.lane = DV_KICK;
    cursor_set(0);
    shift_turn(15);
    shift_press(B_OCTDN);
    shift_press(B_OCTDN);
    ok = clip.lane == CLIP_ALL && msg_is("COPIED ALL LANES") && clip.n == 15u && clip.step[2].hit == 1u << DV_HATC;
    ui.lane = DV_TOM;
    cursor_set(16);
    shift_press(B_OCTUP);
    ok &= msg_is("PASTED") && t->step[16].hit == t->step[0].hit && t->step[16].acc == t->step[0].acc &&
          t->step[18].hit == t->step[2].hit && !t->step[17].hit && !((t->step[16].hit >> DV_TOM) & 1u);
    bad += check("  SHIFT + OCT- again on the same steps and lane: every lane (COPIED ALL LANES), pasted as it was", ok);
    ui.lane = DV_BELL;
    cursor_set(0);
    shift_turn(15);
    shift_press(B_OCTDN);
    ok = msg_is("NO BELL TO COPY") && clip.lane == CLIP_ALL;
    shift_press(B_OCTDN);
    ok &= msg_is("COPIED ALL LANES");
    go_page(GR_FEEL); frame();
    shift_press(B_OCTDN);
    ok &= clip.lane == CLIP_ALL && msg_is("COPIED 15 STEPS");   /* (the same steps 1-16 selected) */
    bad += check("  a lane with nothing there: NO BELL TO COPY, again every lane; FEEL (no LANE card) every lane", ok);
    return bad;
}

/* (2.0, #25) SEQ TOOLS on notes that overlap: REVERSE note by note (each starting where it ended, its ties after it,
 * its step's velocity and lock along); RANDOM and COOK move a note with the steps it goes on */
static int test_tools_overlap(void)
{
    track_t *t;
    int bad = 0, ok;
    uint32_t i, p, moved;
    int32_t x;
    ui_power_on();
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_VOICE] = V_POLY;
    t->p[P_SLEN] = 8;
    t->step[0] = (step_t){{60}, 1, ST_NOTE, 0, 90};          /* C4 on steps 1-4 */
    t->step[1] = (step_t){{0}, 0, ST_TIE};
    t->step[2] = (step_t){{60, 64}, 2, ST_NOTE, 0, 70, 0, 1};   /* E4 on 3 beside it */
    t->step[3] = (step_t){{60}, 1, ST_NOTE, 0, 0, 0, 1};
    t->step[5] = (step_t){{67}, 1, ST_NOTE, 0, 100};         /* G4 on 6-7 */
    t->step[6] = (step_t){{0}, 0, ST_TIE};
    motion_set_lock(t, 0, P_REV, 50);                        /* (C4's step's lock) */
    go_page(GR_ROLL); frame();
    btn_down(B_SEQ); frame();
    tl_key(1);                                               /* G3: REVERSE */
    x = note_at(&t->step[5], 64);
    ok = msg_is("REVERSED") && note_at(&t->step[1], 67) == 0 && t->step[1].vel == 100u && note_span(t, 1, 0) == 2u &&
         t->step[4].n == 1u && t->step[4].note[0] == 60u && t->step[4].vel == 90u && note_span(t, 4, 0) == 4u &&
         x >= 0 && !step_tied(&t->step[5], (uint32_t)x) && t->step[5].vel == 70u && note_span(t, 5, (uint32_t)x) == 1u &&
         step_empty(&t->step[0]) && step_empty(&t->step[3]) && t->step[2].time == ST_TIE && t->step[7].time == ST_TIE &&
         tl_lock_at(4) && !tl_lock_at(0) && !tl_lock_at(7);
    bad += check("  REVERSE, notes overlapping: each from where it ended (C4 5-8, E4 6 beside it, G4 2-3), velocity and lock along",
                 ok);
    for (ok = 1, moved = 0, i = 0; i < 8u; i++) {
        tl_key(i % 2u ? 5u : 4u);                            /* C4 RANDOM, D4 COOK */
        moved |= (p = t->step[4].note[0]) != 60u;
        x = note_at(&t->step[5], p);
        ok &= t->step[4].n == 1u && note_span(t, 4, 0) == 4u && x >= 0 && step_tied(&t->step[5], (uint32_t)x) &&
              t->step[5].n == 2u && t->step[5].note[x ^ 1] != p && !step_tied(&t->step[5], (uint32_t)x ^ 1u) &&
              t->step[6].n == 1u && t->step[6].note[0] == p && t->step[7].time == ST_TIE && t->step[2].time == ST_TIE;
    }
    btn_up(B_SEQ); frame();
    bad += check("  RANDOM and COOK x4 each: the long note's new pitch on every step it goes on, the one beside it apart",
                 ok && moved);
    return bad;
}

/* (2.0, D 1A, design/mockups/v2_mutes.png) the black keys 1..6 in GLO, FX and PERFORM, alike: a tap toggles the track's
 * MUTE (PERFORM's staged, stopped: at once), a hold cuts it while held (its MUTE as it was); a MUTE is silent in the mix
 * at once (2.9 ms), and back */
static int test_mutes_1a(void)
{
    int bad = 0, ok;
    int32_t b[CTL];
    uint32_t i, j;
    ui_power_on();
    lay_combo(B_GLO, black(0));                         /* GLO: held past HOLD */
    frames(600);
    ok = trk_cut(0) && !trk[0].p[P_MUTE];
    key_up(black(0)); btn_up(B_GLO); frame();
    ok &= !trk_cut(0) && !trk[0].p[P_MUTE];
    bad += check("  GLO: a black key held past HOLD cuts T1 while held, its MUTE as it was", ok);
    btn_down(B_FX); frame();                            /* (the button a frame first: its keys the layer's) */
    key_down(black(1)); frame(); key_up(black(1)); frame();
    ok = trk[1].p[P_MUTE] == 1 && !perf_held;
    key_down(black(1));
    frames(600);
    ok &= ((perf_held >> (PF_M1 + 1u)) & 1u) && trk[1].p[P_MUTE] == 1;
    key_up(black(1)); btn_up(B_FX); frame();
    ok &= !perf_held && trk[1].p[P_MUTE] == 1;
    bad += check("  FX: a tap toggles T2's MUTE (latched), a hold cuts while held and leaves it", ok);
    btn_down(B_ARP); frame();
    key_down(black(2)); frame(); key_up(black(2)); frame();
    ok = trk[2].p[P_MUTE] == 1;                         /* (stopped: at once, as mute_switch) */
    key_down(black(3)); frame();
    frames(200);
    ok &= !trk_cut(3);                                  /* (not yet a hold) */
    frames(400);
    ok &= trk_cut(3) && !trk[3].p[P_MUTE] && !mute_arm[3];
    key_up(black(3)); btn_up(B_ARP); frame();
    ok &= !trk_cut(3) && !trk[3].p[P_MUTE] && !mute_arm[3];
    bad += check("  PERFORM: a tap toggles T3's MUTE (stopped: at once), a hold cuts T4 from HOLD on, no MUTE staged", ok);
    trk[4].p[P_MUTE] = 1;
    for (j = 0; j < 6u; j++) {                          /* (a block 32 samples; the ramp 128) */
        for (i = 0; i < CTL; i++)
            b[i] = 10000;
        perf_mute(4, b, CTL);
    }
    ok = pf.mg[4] == 0 && b[CTL - 1u] == 0;
    trk[4].p[P_MUTE] = 0;
    for (j = 0; j < 6u; j++) {
        for (i = 0; i < CTL; i++)
            b[i] = 10000;
        perf_mute(4, b, CTL);
    }
    ok &= pf.mg[4] == 32768 && b[CTL - 1u] == 10000;
    bad += check("  the mix: a MUTE silent at once (2.9 ms), back as it goes", ok);
    for (i = 0; i < NTRK; i++)
        trk[i].p[P_MUTE] = 0;
    return bad;
}

/* (2.0, D 2A, design/mockups/v2_mutes.png) one way back, MENU > CONTROL > RETURN (after STEP PREVIEW; SNAP 1 BEAT 1 BAR,
 * 1 BAR the default): MOMENTARY's values come back over it once LFO is let go (half way at half of it; a list's value at
 * once; an edit meanwhile stays), FX's macros once FX is let go, PERFORM's throws once the knob rests (throw.c
 * thr_beats); OCT+ keeps each, held (FX, PERFORM: the thrown value the parameter's own) */
static int test_way_back_2a(void)
{
    int bad = 0, ok;
    int16_t *p0, v0, w0;
    int32_t s;
    uint32_t id;
    int out;
    ui_power_on();
    ok = MI_RETURN == MI_PREVIEW + 1u && MI_TAB[MI_RETURN] == MTAB_CONTROL && menu_get(MI_RETURN) == RET_BAR &&
         menu_n(MI_RETURN) == 3u && str_eq(menu_vname(MI_RETURN, RET_BAR), "1 BAR") &&
         str_eq(menu_vname(MI_RETURN, RET_SNAP), "SNAP");
    ret_set(RET_BEAT);
    ok &= menu_get(MI_RETURN) == RET_BEAT && ret_beats() == 1u;
    bad += check("RETURN: a row of CONTROL after STEP PREVIEW, SNAP 1 BEAT 1 BAR (1 BAR the default)", ok);
    ui_power_on();
    go_home();
    (void)home_param(0, &p0);
    v0 = *p0;
    s = v0 > 64 ? -1 : 1;
    btn_down(B_LFO); frame();
    turn(EN_K1, 24 * s);
    w0 = *p0;
    btn_up(B_LFO); frame();
    ok = abs(w0 - v0) >= 8 && mom.n == 1u && msg_is("BACK") && *p0 != v0;
    frames(2u * beat_ms());
    ok &= abs(*p0 - (v0 + w0) / 2) <= abs(w0 - v0) / 8 + 1;
    a_bar();
    ok &= *p0 == v0 && !mom.n;
    bad += check("  MOMENTARY: LFO let go, the value comes back over a bar, half way at half a bar", ok);
    go_title("REVERB"); frame();
    btn_down(B_LFO); frame();
    turn(EN_K1, 1);
    ok = song.g[G_RTYPE] == 1;
    btn_up(B_LFO); frame();
    bad += check("  .. a list's value (REVERB TYPE) at once", ok && song.g[G_RTYPE] == 0 && !mom.n);
    go_home(); frame();
    btn_down(B_LFO); frame();
    turn(EN_K1, 24 * s);
    btn_up(B_LFO); frame();
    frames(200);
    turn(EN_K1, -2 * s);                                /* (an edit, no LFO, while it comes back) */
    w0 = *p0;
    a_bar();
    bad += check("  .. an edit while it comes back: the edit stays", *p0 == w0 && w0 != v0 && !mom.n);
    *p0 = v0;
    ret_set(RET_SNAP);
    btn_down(B_LFO); frame(); turn(EN_K1, 10 * s); btn_up(B_LFO); frame();
    ok = *p0 == v0 && !mom.n;
    ret_set(RET_BEAT);
    btn_down(B_LFO); frame(); turn(EN_K1, 10 * s); btn_up(B_LFO); frame();
    ok &= *p0 != v0;
    frames(beat_ms() + 32u);
    bad += check("  RETURN SNAP: back at once; 1 BEAT: over a beat", ok && *p0 == v0 && !mom.n);
    ui_power_on();
    btn_down(B_FX); frame();
    turn(EN_K2, 40);                                    /* CRUSH */
    ok = perf_k[1] > 0 && mom_held(1) == 1u && ui.layer == LAYER_FX;
    btn_up(B_FX); frame();
    ok &= perf_k[1] > 0 && !mom_held(1);                /* (on its way back) */
    a_bar();
    bad += check("  FX's macros: back over a bar once FX is let go", ok && !perf_k[1] && !mom.n);
    btn_down(B_FX); frame();
    turn(EN_K2, 30);
    w0 = perf_k[1];
    press(B_OCTUP);
    ok = msg_is("KEPT") && !mom.n && song.octave == 0;
    btn_up(B_FX); frame();
    a_bar();
    bad += check("  .. OCT+ with FX held keeps them (no octave)", ok && perf_k[1] == w0 && w0 > 0);
    perf_k[1] = 0;
    ui_power_on();
    ret_set(RET_BEAT);
    frame();
    ok = thr_beats == 1u;                               /* (mom_poll, each pass) */
    ret_set(RET_SNAP);
    frame();
    ok &= thr_beats == 0u;
    ret_set(RET_BAR);
    frame();
    ok &= thr_beats == 4u;
    btn_down(B_ARP);
    frames(800);
    id = throw_id(TSEL, 1);                             /* KNOB 2: REVERB */
    v0 = TSEL->p[id];
    turn(EN_K2, 5);
    w0 = (int16_t)throw_value(song.sel, 1, &out);
    ok &= ui.layer == LAYER_PERF && out && w0 != v0 && TSEL->p[id] == v0 && (oct_leds() & OCT_BREATH);
    press(B_OCTUP);
    ok &= msg_is("KEPT") && !thr_out && TSEL->p[id] == w0;
    btn_up(B_ARP); frame();
    bad += check("  PERFORM's throws: their glide as RETURN (throw.c thr_beats); OCT+ keeps a throw as the value", ok);
    return bad;
}

/* (2.0, D 1A) MOD's eleven routes: KNOB 1 the route (LFO DEST's and ENV DEST's seven, then the four slots); a fixed
 * one's AMOUNT alone turns (its SOURCE and DEST fixed), a slot's SOURCE DEST AMOUNT as before; MOD is a lock source, its
 * KNOB 4 the route's AMOUNT */
static int test_mod_routes(void)
{
    track_t *t;
    int bad = 0, ok;
    int16_t was, p[P_COUNT];
    uint32_t i;
    ui_power_on();
    t = TSEL;
    go_title("MOD"); frame();
    for (i = 0; i < 12u; i++)
        turn(EN_K1, -1);
    ok = mod_ui_route == 0u;
    turn(EN_K1, 1);                                     /* route 2: the LFO to CUT */
    was = t->p[P_LD_FLT];
    turn(EN_K4, 3);
    ok &= mod_ui_route == 1u && t->p[P_LD_FLT] > was;
    memcpy(p, t->p, sizeof p);
    turn(EN_K2, 1);
    turn(EN_K3, 1);
    ok &= !memcmp(p, t->p, sizeof p);                   /* (its SOURCE and DEST fixed) */
    ok &= ui.plk_src == ui.page + 1u && lock_id(3) == P_LD_FLT && lock_id(0) == 0xFFu && lock_id(1) == 0xFFu;
    bad += check("  MOD: KNOB 1 the route (2: LFO to CUT), KNOB 4 its AMOUNT (LFO DEST's CUTOFF), 2 and 3 nothing; a lock "
                 "source, KNOB 4 the route's", ok);
    for (i = 0; i < 6u; i++)
        turn(EN_K1, 1);                                 /* route 8: the matrix's first slot */
    was = t->p[P_M1SRC];
    turn(EN_K2, 1);
    ok = mod_ui_route == MOD_SLOT0 && t->p[P_M1SRC] == was + 1 && lock_id(3) == 0xFFu;
    for (i = 0; i < 6u; i++)
        turn(EN_K1, 1);
    ok &= mod_ui_route == MOD_ROUTES - 1u;              /* (the last, 11: no further) */
    bad += check("  MOD: routes 8 .. 11 the slots (SOURCE as before; their AMOUNT no lock, as before), 11 the last", ok);
    return bad;
}

/* (2.0, #25) REC, playing: on a POLY track a key held while another starts goes on as its own pitch (C4 held from step
 * 1, E4 on 3: C4 1-4, E4 3-6, each let go early in the step after); MONO as before 2.0 (E4 ends C4) */
static void rec_steps(track_t *t, uint32_t a, uint32_t b)   /* the sequencer entering steps a .. b (rec_hold) */
{
    for (; a <= b; a++) {
        t->seq_idx = (uint16_t)a;
        t->seq_pos = 0;
        rec_hold(t, a, (uint32_t)t->p[P_SLEN]);
    }
}
static void rec_keys(uint32_t notes) { fm1_in.notes = notes; keyboard_block(); }
static int test_rec_overlap(void)
{
    track_t *t;
    int bad = 0, ok, m;
    int32_t x;
    uint32_t c4 = 1u << key_of(60), e4 = 1u << key_of(64);
    for (m = 0; m < 2; m++) {
        ui_power_on();
        t = TSEL;
        track_defaults_steps(t);
        t->p[P_VOICE] = m ? V_MONO : V_POLY;
        t->p[P_SLEN] = 16;
        song.rec = (uint8_t)(1u << song.sel);
        song.playing = 1;
        t->seq_idx = 0;
        t->seq_pos = 0;
        rec_keys(c4);                                  /* C4 down on step 1 */
        rec_steps(t, 1, 2);
        rec_keys(c4 | e4);                             /* E4 down on step 3, C4 held */
        rec_steps(t, 3, 4);
        rec_keys(e4);                                  /* C4 up early in step 5 */
        rec_steps(t, 5, 6);
        rec_keys(0);                                   /* E4 up early in step 7 */
        song.playing = 0;
        song.rec = 0;
        x = note_at(&t->step[2], 64);
        if (!m)
            bad += check("  REC, POLY: E4 played while C4 is held goes in beside it; each goes on as its own (C4 1-4, E4 3-6)",
                         note_span(t, 0, 0) == 4u && x >= 0 && !step_tied(&t->step[2], (uint32_t)x) &&
                         note_span(t, 2, (uint32_t)x) == 4u && step_empty(&t->step[6]) && !t->rh_n);
        else
            bad += check("  REC, MONO: as before 2.0, E4 ends C4 (C4 1-2, E4 3-6)",
                         note_span(t, 0, 0) == 2u && t->step[2].n == 1u && x == 0 && note_span(t, 2, 0) == 4u &&
                         !step_ties(&t->step[2]) && !t->rh_n);
    }
    return bad;
}

/* (Skiff phase 8, DESIGN.md rule 3) SELECT turns a family's pages either way, as its button steps them, the ends where
 * they are; GLO's MIXER, GLOBAL and SYSTEM one family; HOME and a layer held: not the pages; BPM LOCK OFF: the tempo */
/* Skiff U1 (the UX review's safety net): PLAY over a dialog, EDIT's step clear undone (a run of them one copy), the
 * step keys' REC hint, ALGORITHM in PERFORM, the sound sheet's rows with BPM LOCK OFF, drum TUNE in semitones */
static int test_u1(void)
{
    int bad = 0, ok;
    char v[12];
    const char *u;
    const param_desc_t *d;
    ui_power_on();
    confirm_open(CF_CLEAR_SEQ, 0);
    press(B_PLAY);
    bad += check("U1: PLAY over a dialog starts the music, the dialog stays", transport_req == 1u && ui.confirm == CF_CLEAR_SEQ);
    press(B_OCTDN); stop_transport();

    ui_power_on();
    my_steps(TSEL);
    go_page(GR_ROLL);
    ui.cursor = 0; ui.msg_t = 0;
    song.rec = 1u << song.sel;                       /* (U2: armed, step entry) */
    press(B_EDIT);
    ok = msg_is("STEP CLEARED") && !step_on(&TSEL->step[0]) && ui.cursor == 1u;
    ui.cursor = 3;
    frames(100);
    press(B_EDIT);
    ok &= !step_on(&TSEL->step[3]) && undo_n == 1u;
    hold(B_SAVE);
    ok &= TSEL->step[0].note[0] == 40u && TSEL->step[3].note[0] == 43u && step_on(&TSEL->step[3]);
    bad += check("U1: EDIT's step clear is undone by SAVE held; two in a run are one undo", ok);

    ui_power_on();
    go_page(GR_ROLL);
    ui.msg_t = 0;
    fm1_in.notes = host_notes = 1u << 7; frame(); fm1_in.notes = 0; frame();
    ok = msg_is("[REC] WRITE STEPS");
    ui.msg_t = 0;
    fm1_in.notes = host_notes = 1u << 7; frame(); fm1_in.notes = 0; frame();
    ok &= !ui.msg_t;                                     /* (now and then: not every key) */
    bad += check("U1: NOTES's keys not armed: \"[REC] WRITE STEPS\" once a while", ok);

    ui_power_on();
    btn_down(B_ARP); frames(560);
    turn(EN_ALGO, 1);
    ok = ui.layer == LAYER_PERF && song.sel == 1u;
    btn_up(B_ARP); frames(300);
    bad += check("U1: PERFORM held, ALGORITHM picks the track", ok && song.sel == 1u);

    ui_power_on();
    ui_prefs = 0;                                       /* BPM LOCK OFF: SELECT the tempo, but on the sheet its rows */
    set_engine_of(TSEL, ENGI_DRUMX);
    go_page(GR_DXSND);
    dx_ui_win = 0;
    {
        int16_t b0 = song.g[G_BPM];
        turn(EN_SELECT, 1);
        ok = dx_ui_win == 1u && song.g[G_BPM] == b0;
        go_title("ENV"); turn(EN_SELECT, 1);
        ok &= song.g[G_BPM] != b0;
    }
    bad += check("U1: BPM LOCK OFF: the DRUM-X sheet's rows still on SELECT, the tempo elsewhere", ok);

    d = param_desc_of(ENGI_DRUMX, P_E1);
    param_format(d, 64, v, &u); ok = str_eq(v, "0.0") && str_eq(u, "st");
    param_format(d, 0, v, &u); ok &= str_eq(v, "-12.0");
    param_format(d, 127, v, &u); ok &= str_eq(v, "+11.8");
    param_format(d, 80, v, &u); ok &= str_eq(v, "+3.0");
    param_format(d, 63, v, &u); ok &= str_eq(v, "-0.2");
    ok &= param_desc_of(ENGI_DRUM, P_E1)->fmt == F_ST16;
    bad += check("U1: DRUM and DRUM-X TUNE in semitones (0.0 st, -12.0 .. +11.8)", ok);
    ui_power_on();
    return bad;
}

/* Skiff U2 (the review's gestures): a recording pass is one undo (an empty one leaves none); PERFORM's scene stored
 * over asks first */
static int test_u2c(void);
static int test_u3(void);
static int test_u4(void);
static int test_u5(void);
static int test_u2(void)
{
    int bad = 0, ok;
    uint32_t n0;
    ui_power_on();
    my_steps(TSEL);
    go_page(GR_ROLL); frame();
    ui.cursor = 1;
    n0 = undo_n;
    press(B_REC);                                       /* armed (step recording): a copy */
    ok = song.rec == 1u && undo_n == n0 + 1u;
    press(B_REC);                                       /* nothing recorded: the copy goes again */
    ok &= song.rec == 0u && undo_n == n0;
    press(B_REC);
    ui.cursor = 1;
    key_down(7); frame(); key_up(7); frame();
    ok &= step_on(&TSEL->step[1]);
    press(B_REC);
    frames(400);
    hold(B_SAVE);
    ok &= !step_on(&TSEL->step[1]) && step_on(&TSEL->step[0]);
    bad += check("U2: a recording pass is one undo (RECORDING), an empty pass none", ok);

    ui_power_on();
    ui_perf_keys = 1;                                   /* PERFORM's KEYS SCENE */
    btn_down(B_ARP); frames(560);
    btn_down(B_SAVE); frame();
    key_down(white(0)); frame(); key_up(white(0)); frame();
    ok = ui.layer == LAYER_PERF && scene_used(0) && ui.confirm == CF_NONE;
    key_down(white(0)); frame(); key_up(white(0)); frame();
    ok &= ui.confirm == CF_STORE_SCENE && ui.confirm_trk == 0u;
    btn_up(B_SAVE); btn_up(B_ARP); frame(); frame();
    press(B_OCTUP);
    ok &= ui.confirm == CF_NONE && msg_is("SCENE 1 SAVED");
    bad += check("U2: PERFORM, SAVE + a white key over a used scene: OVERWRITE SCENE 1? first", ok);
    ui_perf_keys = 0;
    ui_power_on();
    return bad;
}

/* (Skiff U4, the review's words) */
static int test_u4(void)
{
    static const char *const M[] = {"SAVE > PATTERNS: CLEAR", "AUTOMATION: DELETE ONE", "GLO + SELECT: TEMPO",
        "UNDO NEEDS PATTERN 07", "DISARM REC FOR A TAKE", "PLAY ONE LOOP FIRST", "NOTHING BEFORE TO JOIN", "STOP TO RENAME",
        "TAP: CLOCK IS EXTERNAL", "RESTART: CLOCK EXTERNAL", "SLICES NOW MANUAL", "PICK A USR SAMPLE FIRST",
        "SAVED UNTIL POWER OFF", "RENAMED UNTIL POWER OFF", "ERASED UNTIL POWER OFF", "STORAGE ERROR", "KEYS RECORD LANES",
        "HOLD [GLO]: SHORTCUTS", "HOLD [EDIT]: SHORTCUTS", "SOUND INITIALIZED", "PATTERN 06 CLEARED", "UNDONE: SEQUENCE",
        "SESSION NOT RESTORED", "IT STOPPED BEFORE", "HOLD [SAVE]: UNDO", "PICK WITH A KNOB FIRST", "ONE LAYER AT A TIME",
        "TAP 2 MORE", "SETTINGS RESET", "WAITING FOR USB CLOCK", "WAITING FOR TRS CLOCK", "MENU > MIDI > CLOCK"};
    int bad = 0, ok = 1;
    uint32_t i;
    const char *u;
    char v[12], nm[12];
    for (i = 0; i < NELEM(M); i++)
        ok &= head_hint_w(M[i], 999) <= 236 - HB_MSG_X && str_len(M[i]) < sizeof ui.msg2;
    bad += check("U4 F3 L6: every new message fits the header (from x 106) and the second line", ok);
    ui_power_on();
    param_format(&GP[G_RTYPE], 1, v, &u);
    ok = str_eq(v, "SPRING");
    set_engine_of(TSEL, 7u);                            /* (WHEEL) */
    param_format(track_desc(TSEL, P_E0), 1, v, &u);
    ok &= str_eq(v, "MELLOW");
    bad += check("U4 V5: values in full (SPRING, MELLOW; were SPRIN, MELLO)", ok);
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    TSEL->p[P_E0] = DK_66;                              /* (66: a conga on TOM) */
    ev_name(TSEL, P_LN0 + DV_TOM, nm);
    bad += check("U4 L9: AUTOMATION names a lane as the kit plays it (66: CONGA)", str_eq(nm, "CONGA"));
    bad += check("U4 R9: the REC layer says CLICK, COUNT-IN, CLICK LVL; GLOBAL's swing is SWING+",
                 str_eq(RL_LABEL[1], "COUNT-IN") && str_eq(GP[G_SWING].label, "SWING+"));
    {   /* L5: the header the page (EDIT 1 on ANALOG: OSC), the footer where it is */
        char ti[20];
        ui_power_on();
        go_title("EDIT 1"); frame();
        page_pos(ti);
        ok = str_eq(page_head(), "OSC") && str_eq(ti, "1/4");
        go_title("ENV"); frame();
        page_pos(ti);
        bad += check("U4 L5: the header names the page (EDIT 1 on ANALOG: OSC), the footer only where (1/4, ENV 1/2)",
                     ok && str_eq(page_head(), "ENV") && str_eq(ti, "1/2"));   /* ((2.0) ENV ENV 2: ENV DEST is MOD's) */
        set_engine_of(TSEL, ENGI_FM6);                  /* (#7) a sheet counts as any page (was SELECT: ROWS) */
        TSEL->engine = TSEL->eng_req;
        go_page(GR_FMOP); frame();
        page_pos(ti);
        ok = str_eq(ti, "3/6");
        go_page(GR_FMALL); frame();
        page_pos(ti);
        ok &= str_eq(ti, "4/6");
        set_engine_of(TSEL, ENGI_DRUMX);
        TSEL->engine = TSEL->eng_req;
        go_page(GR_DXSND); frame();
        page_pos(ti);
        bad += check("#7 the sheets show where they are: FM6 OPERATOR 3/6, ALL OPS 4/6, DRUM-X SOUND 3/5 (LANE MIX and DRUM COMP "
                     "after it)", ok && str_eq(ti, "3/5"));
    }
    ui_power_on();                                      /* F7: no silent dead ends */
    ui_prefs |= PREF_BPM_LOCK;                          /* (SELECT: the pages) */
    go_title("ENV"); frame();
    turn(EN_SELECT, -1);
    ok = ui.edge_t && str_eq(cur_page()->title, "ENV");
    go_page(GR_SLOTS); frame();
    ui.act = 0;
    press(B_OCTUP);
    ok &= msg_is("PICK WITH A KNOB FIRST");
    go_home(); frame();
    btn_down(B_GLO); frames(800);
    press(B_FX);
    ok &= msg_is("ONE LAYER AT A TIME") && ui.layer == LAYER_GLO;
    key_down(white(8)); frame(); key_up(white(8)); frame();   /* (G4: TAP) */
    ok &= msg_is("TAP 2 MORE");
    btn_up(B_GLO); frame();
    ui_prefs = 0;
    bad += check("U4 F7: SELECT at an end flashes the footer; OCT+ with nothing picked, a second layer, TAP's first taps say so", ok);
    ui_power_on();                                      /* A3: HINTS OFF, RESET SETTINGS */
    menu_put(MI_HINTS, 1);
    layer_seen = 0;
    ui.msg_t = 0;
    press(B_EDIT);
    ok = ui_hints_off && !msg_is("HOLD [EDIT]: SHORTCUTS");
    menu_put(MI_TUNE, 60);
    menu_put(MI_LEDS, 0);
    menu_put(MI_CLK, 1);
    ok &= CLK320_SET;
    hold(B_HOME);
    for (i = 0; i < 12u && ui.menu_sel != MI_RESET; i++) {
        turn(EN_ALGO, 1);
        if (menu_tab() == MTAB_SYSTEM)
            while (ui.menu_sel != MI_RESET) turn(EN_PRESET, 1);
    }
    press(B_OCTUP);
    ok &= !ui.menu && ui.confirm == CF_RESET_SET;
    press(B_OCTUP);
    bad += check("U4 A3: HINTS OFF hides the layer's first-tap hint; RESET SETTINGS asks, then the MENU's defaults (TUNE 0, LEDS DIM HI, HINTS ON, "
                 "(Skiff) CPU CLOCK 360 MHZ)",
                 ok && !ui_hints_off && song.g[G_TUNE] == 0 && settings_leds == LEDS_DIM && !CLK320_SET && msg_is("SETTINGS RESET"));
    ui_power_on();
    return bad;
}

/* (Skiff U5, the review's screens) V1: the pages that drew the output's scope draw their own graphs, drawn again only
 * when what they show changes (the scope: every other frame); A1: HOME held alone a moment shows SHIFT's map of the
 * page, a combo or letting go ends it, HINTS OFF shows none */
static int test_u5(void)
{
    static const char *const T[] = {"MOD", "VOICE", "VOICE 2", "EDIT 1", "EDIT 2", "DELAY", "REVERB",
                                    "CHORUS", "GLOBAL", "ARP", "ARP 2", "ENV 2", "DRIVE"};
    int bad = 0, ok = 1;
    uint32_t i, h;
    smap_t m[12];
    ui_power_on();
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        go_title(T[i]); frame();
        h = graph_signature();
        ui.frame += 3u;
        ok &= str_eq(cur_page()->title, T[i]) && graph_signature() == h &&
              (cur_page()->graph != GR_NONE || analog_page(cur_page(), TSEL));
    }
    go_title("DELAY"); frame();
    h = graph_signature();
    song.g[G_DFDBK] = (int16_t)(song.g[G_DFDBK] + 10);
    ok &= graph_signature() != h;
    go_title("ARP"); frame();
    h = graph_signature();
    TSEL->held[0] = 60; TSEL->nheld = 1;
    ok &= graph_signature() != h;
    TSEL->nheld = 0;
    bad += check("U5 V1: MOD, VOICE .. ARP 2 and ANALOG's EDIT pages draw their own graphs (no scope), again when GLO's "
                 "settings or the notes held change", ok);
    {   /* (Skiff) the LFO graphs over 2 s as it runs: RATE's cycles, FADE's rise, PHASE's start; now walks, then scrolls */
        track_t *t = TSEL;
        int32_t x, n1 = 0, n2 = 0, p1, p2;
        t->p[P_LWAVE] = 0; t->p[P_LFADE] = 0; t->p[P_LPHASE] = 0; t->lfo_t = 0;
        t->p[P_LRATE] = 40;
        for (x = 1, p1 = lfo_view_at(t, 0, 216); x < 216; x++, p1 = p2)
            n1 += ((p2 = lfo_view_at(t, x, 216)) >= 0) != (p1 >= 0);
        t->p[P_LRATE] = 70;
        for (x = 1, p1 = lfo_view_at(t, 0, 216); x < 216; x++, p1 = p2)
            n2 += ((p2 = lfo_view_at(t, x, 216)) >= 0) != (p1 >= 0);
        ok = n2 > n1 && n1 >= 1;
        p1 = lfo_view_at(t, 0, 216);
        t->p[P_LPHASE] = 32;
        ok &= lfo_view_at(t, 0, 216) != p1;
        t->p[P_LFADE] = 100;
        ok &= lfo_view_at(t, 0, 216) == 0;
        t->lfo_t = LFO_WIN / 2u;
        ok &= lfo_view_now(t, 216) == 108 && lfo_view_t0(t) == 0u;
        t->lfo_t = LFO_WIN * 3u;
        ok &= lfo_view_now(t, 216) == 216 && lfo_view_t0(t) == LFO_WIN * 2u;
        t->p[P_LFADE] = 0; t->p[P_LPHASE] = 0; t->lfo_t = 0;
        bad += check("LFO graphs: 2 s as it runs (RATE more cycles, PHASE its start, FADE from nothing), now walks it then "
                     "scrolls", ok);
    }
    ui_power_on();
    go_title("ENV"); frame();
    btn_down(B_HOME); frames(400);
    shift_map(m);
    ok = ui.shift && !msg_is("LET GO: MENU") && str_eq(m[0].job, "SOUND") && !m[0].kind && m[9].kind == 2u && str_eq(m[9].job, "TAKE");
    frames(600);
    ok &= ui.shift && !msg_is("LET GO: MENU");          /* (held on past the hold: still the map, which says it) */
    btn_up(B_HOME); frame();
    ok &= !ui.shift && ui.menu;                         /* let go: the MENU, as before */
    press(B_HOME); frame();
    go_page(GR_ROLL); frame();
    btn_down(B_HOME); frames(400);
    shift_map(m);
    ok &= ui.shift && str_eq(m[0].job, "STEP") && m[0].kind == 1u && str_eq(m[3].job, "PICK") &&
          str_eq(m[6].job, "RANGE") && m[6].kind == 2u && str_eq(m[7].job, "COPY") && str_eq(m[8].job, "PASTE");
    press(B_SAVE);                                      /* SHIFT + SAVE: TAKE; the map goes */
    ok &= !ui.shift;
    btn_up(B_HOME); frame();
    ok &= !ui.shift && !ui.menu;
    menu_put(MI_HINTS, 1);
    btn_down(B_HOME); frames(400);
    ok &= !ui.shift;
    btn_up(B_HOME); frame();
    bad += check("U5 A1: HOME held shows SHIFT's map (ENV: PRESETS SOUND; STEP: PRESETS STEP, KEYS STEPS; + SAVE TAKE lit), "
                 "held on it stays, let go the MENU, a combo ends it; HINTS OFF: none", ok);
    ui_hints_off = 0;
    ui_power_on();
    return bad;
}

/* (Skiff U3, the review's structure) */
static int test_u3(void)
{
    int bad = 0, ok;
    char ti[20];
    ui_power_on();                                      /* N5: a page that changes nothing on the engine is not there */
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    press(B_ENV);
    ok = ui.home && msg_is("NO ENV ON DRUM");
    press(B_SCL); frames(320);
    ok &= ui.home && msg_is("NO SCALE ON DRUM");
    go_title("MOD"); frame();                          /* ((2.0) LFO DEST's PIT FLT SHP, the ENV's: MOD's routes) */
    ok &= mod_dead(TSEL, 0) && mod_dead(TSEL, 1) && mod_dead(TSEL, 2) && !mod_dead(TSEL, 3) && mod_dead(TSEL, 5);
    go_title("NOTES"); frame();
    press(B_SEQ); frames(320);
    ok &= cur_page()->graph == GR_FEEL;                 /* ((2.0) NOTES: a drum track's grid) */
    bad += check("U3 N5: DRUM: no ENV, no SCL (their buttons say so), MOD's LFO to PIT CUT SHAPE and ENV routes dimmed (LFO "
                 "AMP not), NOTES then FEEL", ok);
    ui_power_on();                                      /* N3: a page the track no longer has */
    go_title("ENV"); frame();
    set_engine_of(TSEL, ENGI_FM6);
    TSEL->engine = TSEL->eng_req;
    frame();
    bad += check("U3 N3: ENV on a track now FM6 (its operators the envelope): HOME, NO ENV ON FM6", ui.home && msg_is("NO ENV ON FM6"));
    ui_power_on();                                      /* N3: the MENU back onto its page */
    go_title("LFO"); frame();
    hold(B_HOME);
    ok = ui.menu == 1;
    press(B_HOME);
    bad += check("U3 N3: the MENU closes back onto the page it was opened over (LFO)", ok && !ui.menu && !ui.home &&
                 str_eq(cur_page()->title, "LFO"));
    ui_power_on();                                      /* N6: GLO's count */
    press(B_GLO); frame();
    page_title(ti);
    ok = str_eq(ti, "MIXER 1/5");
    press(B_GLO); frame();
    page_title(ti);
    bad += check("U3 N6: GLO's pages counted as one: MIXER 1/5, DELAY 2/5", ok && str_eq(ti, "DELAY 2/5"));
    ui_power_on();                                      /* R8: the unit's TUNE */
    song.g[G_TUNE] = 7;
    project_save(1);
    song.g[G_TUNE] = -3;
    frame();
    project_load(1);
    ok = song.g[G_TUNE] == -3 && (int8_t)ui_unit_g(2) == -3;
    song.g[G_TUNE] = 0;
    ui_unit_g(2) = (uint8_t)-5;
    unit_g_boot();
    ok &= song.g[G_TUNE] == -5;
    unit_g_adopt = 1;
    project_load(1);
    ok &= song.g[G_TUNE] == 7;
    unit_g_boot();
    ok &= (int8_t)ui_unit_g(2) == 7 && ui_set_rev == 2u && !unit_g_adopt;
    bad += check("U3 R8: TUNE is the unit's: a project load leaves it, boot puts it back; the first boot takes the session's", ok);
    song.g[G_TUNE] = 0;
    ui_power_on();                                      /* R5: AUTOMATION's top row */
    motion_set_event(TSEL, 2, P_PAN, 20);
    go_page(GR_EVENTS); frame();
    turn(EN_K1, -1);
    ok = ui.ev_top && act_cols() == 8u && !act_col() && str_eq(act_name(3), "CLEAR");
    turn(EN_K2, -1);
    ok &= !motion_enabled(TSEL);
    turn(EN_K2, 1);
    ok &= motion_enabled(TSEL);
    turn(EN_K4, 1);
    ok &= act_col() == 4u && act_ready();
    press(B_OCTUP);
    ok &= ui.confirm == CF_CLEAR_MOTION;
    press(B_OCTDN);
    turn(EN_K1, 1);
    bad += check("U3 R5: AUTOMATION's top row: PLAY on KNOB 2, CLEAR on KNOB 4 (its dialog); KNOB 1 down: the records",
                 ok && !ui.ev_top && act_col() == 5u && motion_count(TSEL) == 1u);
    ui_power_on();                                      /* R6: one LANES page */
    set_engine_of(TSEL, ENGI_DRUM);
    TSEL->engine = TSEL->eng_req;
    go_title("LANES"); frame();
    turn(EN_K1, 3);
    bad += check("U3 R6: LANES: KNOB 1 the lane (the grid's too); a held step's KNOB 2 locks that lane's level",
                 ui.lane == 3u && lock_id(1) == P_LN0 + 3u && lock_id(0) == 0xFFu);
    turn(EN_K3, 5);                                     /* ((Skiff phase 9) KNOB 3 4: the lane's TUNE, DECAY) */
    turn(EN_K4, -7);
    bad += check("  (phase 9) KNOB 3 TUNE, KNOB 4 DECAY: that lane's, the others untouched; a held step's locks them",
                 TSEL->p[P_LTUNE0 + 3u] == 69 && TSEL->p[P_LDECAY0 + 3u] == -7 && TSEL->p[P_LTUNE0 + 2u] == 64 &&
                 !TSEL->p[P_LDECAY0 + 4u] && lock_id(2) == P_LTUNE0 + 3u && lock_id(3) == P_LDECAY0 + 3u);
    ui_power_on();                                      /* A2: the project in the header, changed since */
    ok = !project_mark();
    project_save(2);
    ok &= project_mark() == 'C';
    trk[0].p[P_LEVEL] = 9;
    frames(300);
    ok &= project_mark() == ('C' | 1u << 8);
    project_load(2);
    ok &= project_mark() == 'C';
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 3;
    turn(EN_K3, 1);
    press(B_OCTUP);                                     /* (unchanged: LOAD at once) */
    ok &= ui.confirm == CF_NONE && project_mark() == 'C';
    trk[0].p[P_LEVEL] = 3;
    press(B_OCTUP);                                     /* (changed: it asks; LOAD stays picked) */
    ok &= ui.confirm == CF_LOAD_PROJ;
    press(B_OCTDN);
    bad += check("U3 A2: the project's letter (C) after a save or load, a dot while changed; LOAD asks only over a change", ok);
    ui_power_on();                                      /* V2: KNOB 1's throw, a tone on every engine */
    set_engine_of(TSEL, ENGI_SAMPLE);
    ok = throw_id(TSEL, 0) == P_E4 && throw_id(TSEL, 1) == P_REV;
    set_engine_of(TSEL, 3);
    ok &= throw_id(TSEL, 0) == P_E7;
    set_engine_of(TSEL, ENGI_FM6);
    ok &= throw_id(TSEL, 0) == ENGINES[ENGI_FM6]->knob[0];
    bad += check("U3 V2: PERFORM's KNOB 1 throws SAMPLE's CUT, LOFI's TONE; FM6 its HOME KNOB 1 (MLVL) as before", ok);
    ui_power_on();                                      /* N1: HOME, every track; the selected one's lane the band */
    go_home(); ui.force = 1; frame();
    ok = swap16(host_screen[(Y_GRAPH + 16u) * 240u + 234u]) == T_TRK(0) &&
         swap16(host_screen[(Y_GRAPH + 128u) * 240u + 234u]) == T_SURF;
    track_select(NTRK - 1u); ui.force = 1; frame();
    ok &= swap16(host_screen[(Y_GRAPH + 16u) * 240u + 234u]) == T_SURF &&
          swap16(host_screen[(Y_GRAPH + 128u) * 240u + 234u]) == T_TRK(NTRK - 1u);
    bad += check("U3 N1: HOME shows all six, the selected one's lane the band (T1 at the top; T6 at the foot)", ok);
    ui_power_on();
    return bad;
}

/* (Skiff U2c, the review's G3) EDIT means the EDIT pages; the pages' own actions are their cards */
static int test_u2c(void)
{
    int bad = 0, ok;
    ui_power_on();
    my_steps(TSEL);
    go_page(GR_ROLL); frame();
    ui.cursor = 0;
    press(B_EDIT);
    bad += check("U2c: EDIT on ROLL, not armed: the EDIT pages, the step kept", cur_page()->fam == FAM_EDIT && step_on(&TSEL->step[0]));
    ui_power_on();
    up_ui(2, 0);
    go_page(GR_USER); ui.uslot = 0; frame();
    turn(EN_K3, 1);
    ok = act_col() == 3u && str_eq(act_name(2), "ERASE");
    turn(EN_K3, 1);
    ok &= act_col() == 3u && str_eq(act_name(2), "NAME");
    turn(EN_K3, 1);
    ok &= str_eq(act_name(2), "NAME");                  /* (its end) */
    turn(EN_K3, -1);
    ok &= act_col() == 3u && str_eq(act_name(2), "ERASE");
    turn(EN_K3, -1);
    ok &= !act_col();
    turn(EN_K3, 2); turn(EN_K4, 1);                     /* another card: NAME goes */
    ok &= act_col() == 4u && !ui.act2;
    bad += check("U2c: USER KNOB 3: ERASE, one further NAME, back: ERASE, none; KNOB 4 drops it", ok);
    song.playing = 1;
    user_rename();
    bad += check("U2c: NAME while playing: STOP TO RENAME", !name_on() && msg_is("STOP TO RENAME"));
    stop_transport();
    ui_power_on();
    go_page(GR_SLOTS);
    song.g[G_SLOT] = 1;
    frame();
    turn(EN_K2, 1);
    ok = act_col() == 2u && str_eq(act_name(1), "DELETE");
    turn(EN_K2, 1);
    ok &= act_col() == 2u && str_eq(act_name(1), "NAME") && ui.act2;
    turn(EN_K2, -2);
    ok &= !act_col();
    bad += check("(#6) PROJECT KNOB 2: DELETE, one further NAME, back to none", ok);
    {   /* (#6) DELETE: the dialog, then the slot empty in flash and RAM; the music stays, on no slot, changed */
        step_t s0;
        ui_power_on();
        trk[0].step[0].time = ST_NOTE; trk[0].step[0].n = 1;
        project_save(1);
        ok = project_used(1) && proj_cur == 1u;
        s0 = trk[0].step[0];
        go_page(GR_SLOTS);
        song.g[G_SLOT] = 2;
        frame();
        turn(EN_K2, 1);
        press(B_OCTUP);
        ok &= ui.confirm == CF_ERASE_PROJ && ui.confirm_trk == 1u;
        {
            char a[24], b[40];
            confirm_text(a, b);
            ok &= str_eq(a, "DELETE PROJECT B?");
        }
        press(B_OCTDN);
        ok &= !ui.confirm && project_used(1);
        turn(EN_K2, 1);
        press(B_OCTUP);
        press(B_OCTUP);
        ok &= !ui.confirm && !project_used(1) && proj_cur == PROJ_NO_SLOT && project_changed() &&
              !memcmp(&trk[0].step[0], &s0, sizeof s0) && msg_is("DELETED UNTIL POWER OFF");   /* (flash: persistence_test) */
        turn(EN_K2, 1);
        press(B_OCTUP);
        bad += check("(#6) DELETE PROJECT B?: OCT- keeps it, OCT+ empties the slot, the music stays (on no slot, changed); "
                     "an empty slot: no dialog", ok && !ui.confirm && msg_is("EMPTY SLOT"));
    }
    {   /* (#6) NEW on an empty slot: KNOB 3 says NEW; over changed music the dialog first; the power-on project, the
         * unit's CLOCK MIDI IN TUNE kept, unnamed, on no slot, clean */
        ui_power_on();
        song.g[G_TUNE] = 5;
        song.g[G_BPM] = 140;
        trk[0].step[0].time = ST_NOTE; trk[0].step[0].n = 1;
        trk[2].p[P_LEVEL] = 11;
        str_cpy(proj_name, "OLD", sizeof proj_name);
        go_page(GR_SLOTS);
        song.g[G_SLOT] = 4;
        frame();
        turn(EN_K3, 1);
        ok = str_eq(act_name(2), "NEW") && act_ready();
        press(B_OCTUP);
        ok &= ui.confirm == CF_NEW_PROJ;
        press(B_OCTDN);
        ok &= !ui.confirm && trk[2].p[P_LEVEL] == 11;
        turn(EN_K3, 1);
        press(B_OCTUP);
        press(B_OCTUP);
        frames(100);
        ok &= !ui.confirm && trk[0].step[0].time == ST_REST && trk[2].p[P_LEVEL] == TP[P_LEVEL].def &&
              song.g[G_BPM] == GP[G_BPM].def && song.g[G_TUNE] == 5 && song.g[G_SLOT] == 4 && !proj_name[0] &&
              proj_cur == PROJ_NO_SLOT && !project_changed() && trk[1].eng_req == TRK_DEF[1][0] &&
              msg_is("NEW PROJECT") && !project_used(3);
        turn(EN_K3, 1);
        press(B_OCTUP);
        bad += check("(#6) NEW on an empty slot: asks over changed music; the power-on project, TUNE and the slot kept; "
                     "unchanged: at once", ok && !ui.confirm && msg_is("NEW PROJECT"));
    }
    ui_power_on();
    return bad;
}

static int test_select_pages(void)
{
    int bad = 0, ok;
    int16_t b0;
    uint32_t cur;
    ui_power_on();
    ui_prefs = PREF_BPM_LOCK;
    set_engine_of(TSEL, 0);
    go_title("ENV"); frame();
    b0 = song.g[G_BPM];
    ui.msg_t = 0;
    turn(EN_SELECT, 1);
    ok = str_eq(cur_page()->title, "ENV 2");                         /* ((Skiff phase 9) ENV 2; (2.0) ENV DEST: MOD) */
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "ENV 2");                        /* (the last: no wrap) */
    turn(EN_SELECT, -2);
    ok &= str_eq(cur_page()->title, "ENV");
    turn(EN_SELECT, -1);
    ok &= str_eq(cur_page()->title, "ENV") && song.g[G_BPM] == b0 && !ui.msg_t;
    bad += check("SELECT: ENV 2, back to ENV, the ends where they are (no wrap), no tempo, no message", ok);
    go_title("FX"); frame();
    turn(EN_SELECT, 5);
    ok = str_eq(cur_page()->title, "DUCK");                          /* ((U3) FX (phase 9: DRIVE; 1.3: INSERT, INSERT 2)
                                                                      * SLICER DUCK: the buses are GLO's) */
#if FELUCCA_TAPE
    press(B_FX);ok &= str_eq(cur_page()->title,"TAPE SEND");
#endif
    press(B_FX);
    ok &= str_eq(cur_page()->title, "FX");                           /* (FX's button steps on from there: round) */
    turn(EN_SELECT, 5);
    ok &= str_eq(cur_page()->title, "DUCK");
    turn(EN_SELECT, -6);
    ok &= str_eq(cur_page()->title, "FX");
    bad += check("SELECT: five FX pages on (DUCK), the FX button round (FX), SELECT on and six back (FX)", ok);
    press(B_GLO);
    ok = cur_page()->fam == FAM_TRK;                                 /* (MIXER) */
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "DELAY");
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "REVERB");
    turn(EN_SELECT, -2);
    ok &= cur_page()->fam == FAM_TRK;
    bad += check("SELECT on GLO's pages: MIXER, DELAY, REVERB and back, as GLO's button steps them", ok);
    go_title("NOTES"); frame();
    cur = ui.cursor;
    turn(EN_SELECT, 1);
    ok = str_eq(cur_page()->title, "FEEL") && ui.cursor == cur;
    turn(EN_SELECT, -1);
    ok &= str_eq(cur_page()->title, "NOTES") && ui.cursor == cur;
    bad += check("SELECT on the SEQ pages: NOTES, FEEL and back, the cursor kept", ok);
    go_home(); frame();
    ui.msg_t = 0; fm1_ms += 1200u;
    turn(EN_SELECT, 1);
    bad += check("SELECT on HOME: no page (BPM LOCKED, as before)", ui.home && msg_is("BPM LOCKED") && song.g[G_BPM] == b0);
    go_title("ENV"); frame();
    btn_down(B_FX); frames(900);                                     /* (past the hold time: the map) */
    ok = ui.layer == LAYER_FX;
    turn(EN_SELECT, 1);
    ok &= str_eq(cur_page()->title, "ENV");
    btn_up(B_FX); frames(320);
    bad += check("SELECT with a layer held (FX): not the page under it", ok && str_eq(cur_page()->title, "ENV"));
    ui_prefs = 0;
    go_title("ENV"); frame();
    b0 = song.g[G_BPM];
    turn(EN_SELECT, 1);
    bad += check("BPM LOCK OFF: SELECT is the tempo as before (Felucca's way), the page stays",
                 str_eq(cur_page()->title, "ENV") && song.g[G_BPM] == b0 + 1);
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* #15 / Discussion #80 MENU > LARGE: OFF by default (and in every older setting); KNOB 1 right ON / left OFF, OCT+
 * toggles, nothing else moves. ON: HOME and the value pages draw tall cards (a K1..K4 pill over each knob's column,
 * the value in L) and the strip under them; the list / graph pages and the layers keep the layout (their labels in
 * M); the footer stays; a knob turned redraws its own card only; saved with the settings, older settings OFF
 * (idempotent) */
static uint16_t lg_px(int32_t x, int32_t y) { return swap16(host_screen[y * 240 + x]); }
static int lg_value_rows(uint32_t c)                    /* rows of card c's value band holding the value's colour */
{
    int32_t x, y, n = 0;
    for (y = Y_LABEL + 40; y < Y_LABEL + 76; y++)
        for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
            if (lg_px(x, y) == T_THEME) { n++; break; }
    return n;
}
static int test_large(void)
{
    int bad = 0, ok;
    uint32_t c;
    static uint16_t off[240 * 240];
    ui_power_on();
    song.playing = 0;
    frame();
    ok = !(ui_prefs & PREF_LARGE) && large_kind() == LK_OFF && card_h() == CARD_H && graph_y() == Y_GRAPH;
    ok &= MI_LARGE == MI_PHEAD + 1u && str_eq(MI_NAME[MI_LARGE], "LARGE") && menu_flag(MI_LARGE) &&   /* (#27) */
          str_eq(menu_flag(MI_LARGE)->name[0], "OFF") && str_eq(menu_flag(MI_LARGE)->name[1], "ON");
    ui.force = 1; frame();
    memcpy(off, host_screen, sizeof off);
    hold(B_HOME); ui.menu_sel = MI_LARGE;
    ok &= ui.menu == 1u;
    turn(EN_K1, 1); ok &= (ui_prefs & PREF_LARGE) != 0u;          /* right: ON */
    turn(EN_K1, -1); ok &= !(ui_prefs & PREF_LARGE);              /* left: OFF */
    press(B_OCTUP); ok &= (ui_prefs & PREF_LARGE) && ui.menu;     /* OCT+: ON */
    press(B_OCTDN); ok &= !(ui_prefs & PREF_LARGE) && ui.menu;    /* OCT-: OFF */
    press(B_OCTUP);
    ok &= ui_prefs == PREF_LARGE;                                  /* (no other setting moved) */
    press(B_HOME);
    bad += check("#15 MENU > LARGE: OFF by default, under COLOR ((Skiff #27) and PLAYHEAD); KNOB 1 right / OCT+ ON, left / OCT- OFF", ok && !ui.menu);

    /* HOME: tall cards (rows 28..132) and the strip (136..198); (#22) no lanes, so the footer names the sound, not the hint */
    frame();
    ok = ui.home && large_kind() == LK_TALL && card_h() == LG_CARD_H && graph_y() == LG_Y_GRAPH && strip_kind() == SK_SCOPE;
    ok &= lg_px(30, Y_SEP_END + 1) == T_SURF && off[(Y_SEP_END + 1) * 240 + 30] == swap16(T_BG);     /* a card where the gap was */
    ok &= lg_px(30, LG_Y_GRAPH + 1) == T_SURF;
    for (c = 0; c < 4u; c++)                                      /* each column's K pill, over its knob */
        ok &= lg_px(CARD_X(c) + 8, Y_LABEL + LG_MY + LG_MH / 2) == T_KEY;
    ok &= memcmp(off + foot_y() * 240, host_screen + foot_y() * 240, 240u * foot_h() * sizeof *off) != 0;   /* (its
                                                                   * pixels as HINTS OFF's: test_home_names) */
    bad += check("#15 LARGE ON: HOME's cards tall (a K1..K4 pill over each column), the scope a strip, the footer the sound's",
                 ok);

    /* the value in L: GLOBAL's BPM 124 stands about twice as tall as in M */
    go_title("GLOBAL"); frame();
    ok = large_kind() == LK_TALL && strip_kind() == SK_TITLE && cur_page()->id[0] == G_BPM && lg_value_rows(0) >= AF_L_CAP_H - 3;
    ui_prefs = 0; ui.force = 1; frame();
    ok &= lg_value_rows(0) <= AF_M_CAP_H + 2;
    ui_prefs = PREF_LARGE; ui.force = 1; frame();
    bad += check("#15 LARGE: a card value in L (GLOBAL's BPM 120: about 21 rows of figures, M's 11)", ok);

    /* a knob turned: its own card redrawn, the other three not */
    go_title("ENV"); frame(); frames(600);
    memcpy(off, host_screen, sizeof off);
    turn(EN_K2, 3);
    {
        int32_t x, y, diff[4] = {0, 0, 0, 0};
        for (y = Y_LABEL; y < Y_LABEL + LG_CARD_H; y++)
            for (c = 0; c < 4u; c++)
                for (x = CARD_X(c); x < CARD_X(c) + CARD_W; x++)
                    diff[c] += off[y * 240 + x] != host_screen[y * 240 + x];
        ok = large_kind() == LK_TALL && strip_kind() == SK_ADSR && diff[1] && !diff[0] && !diff[2] && !diff[3];
    }
    bad += check("#15 LARGE: ENV tall with its ADSR in the strip; KNOB 2 turned redraws card 2 only", ok);

    /* per page type: lists and rolls keep the layout, the layers too; MIXER's tracks in the strip */
    go_page(GR_BROWSE); frame();
    ok = large_kind() == LK_LABEL && card_h() == CARD_H && graph_y() == Y_GRAPH && graph_h() == H_GRAPH + foot_less();
    go_page(GR_ROLL); frame();
    ok &= large_kind() == LK_LABEL;
    go_page(GR_TRK); frame();
    ok &= large_kind() == LK_TALL && strip_kind() == SK_TRK && lg_px(MS_X(1) + 8, LG_Y_GRAPH + 60) == T_SURF &&
          lg_px(MS_X(0) + 8, LG_Y_GRAPH + 60) == T_TRK(0);   /* ((U3, N1) six strips, the selected one a band) */
    go_title("ENV"); frame();
    btn_down(B_GLO); frames(800);
    ok &= ui.layer && large_kind() == LK_LABEL && card_h() == CARD_H;
    btn_up(B_GLO); frame(); frame();
    ok &= !ui.layer && large_kind() == LK_TALL && lg_px(30, LG_Y_GRAPH - 2) == T_BG;
    bad += check("#15 LARGE: PRESETS and the roll keep the layout (labels in M), the layers too; MIXER tall, its tracks in the strip", ok);

    {   /* saved with the settings; settings from before it: OFF (and so again: idempotent) */
        sks_t p, t;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && ui_prefs == PREF_LARGE;
        t = p;
        ok &= settings_import(&t, (int)sizeof t) == 1 && ui_prefs == PREF_LARGE && !memcmp(&p, &t, sizeof p);
        q = per3_of(&p);
        ok &= per3_import(&q) && !ui_prefs;
        ok &= settings_import(&q, (int)sizeof q) == 1 && !ui_prefs;
        ui.force = 1; frame();
        ok &= large_kind() == LK_OFF && lg_px(30, Y_SEP_END + 1) == T_BG;
        bad += check("#15 LARGE saved with the settings; settings from before it: OFF (idempotent)", ok);
    }
    ui_prefs = 0;
    ui_power_on(); frame();
    return bad;
}

/* MENU (1.0.5: in tabs): every row reachable with ALGORITHM and PRESETS, each tab its own page; #46 MENU > ANIM: ON by
 * default (and in every older setting), OFF snaps the rolling digits, the piano roll and the menu's tabs */
static int test_menu_prefs(void)
{
    int bad = 0, ok = 1;
    uint32_t k,j; uint64_t seen=0;
    static uint16_t shot[MTAB_COUNT][240 * 240];
    ui_power_on();
    ui_prefs = 0;
    hold(B_HOME);
    ui.menu_sel = 0;
    for (k = 0; k < MTAB_COUNT; k++) {              /* ALGORITHM right through the tabs, PRESETS through each one's rows */
        ok &= menu_tab() == k && ui.menu_sel == mtab_first(k);
        for (j = 0; j < mtab_rows(k); j++) {
            seen |= 1ull << ui.menu_sel;
            turn(EN_PRESET, 1);
        }
        ok &= ui.menu_sel == mtab_first(k);         /* (round the tab, back at its first row) */
        ui.force = 1; ui_draw();
        memcpy(shot[k], host_screen, sizeof shot[k]);
        for (j = 0; j < k; j++) ok &= memcmp(shot[j], shot[k], sizeof shot[k]) != 0;
        turn(EN_ALGO, 1);
    }
    ok &= seen == (1ull << MI_COUNT) - 1ull && ui.menu_sel == 0u;
    bad += check("MENU: ALGORITHM and PRESETS reach every row, each tab a page of its own", ok);
    /* ANIM */
    ui.menu_sel = MI_ANIM;
    ok = str_eq(MI_NAME[MI_ANIM], "ANIM") && !(ui_prefs & PREF_ANIM_OFF);
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF);            /* right: ON (stays: the default) */
    turn(EN_K1, -1); ok &= (ui_prefs & PREF_ANIM_OFF) != 0;       /* left: OFF */
    turn(EN_K1, 1); ok &= !(ui_prefs & PREF_ANIM_OFF);            /* right: ON (the switch's knob right) */
    press(B_OCTDN); ok &= (ui_prefs & PREF_ANIM_OFF) && ui.menu;  /* OCT-: OFF */
    ok &= !(ui_prefs & ~(uint32_t)PREF_ANIM_OFF);                 /* (no other setting moved) */
    press(B_HOME);
    bad += check("#46 MENU > ANIM: ON by default, KNOB 1 right / OCT+ ON, left / OCT- OFF", ok && !ui.menu);
    ui_prefs = 0;
    ui_power_on(); roll_settle();
    turn(EN_K2, 1);
    ok = song.g[G_SWING] == 1;                                    /* ON: ((U2) BLOCKS' cards snap either way; (U3) SWING:
                                                                   * GLOBAL's TUNE is the MENU's) */
    ui_power_on(); ui_prefs = PREF_ANIM_OFF; roll_settle();      /* (ui_power_on clears the favorites record) */
    turn(EN_K2, 1);
    ok &= song.g[G_SWING] == 1 && !ui.roll[1].from[0] && roll_static_now();   /* OFF: the value at once, static */
    turn(EN_SELECT, 1);
    ok &= !ui.roll[ROLL_BPM].from[0];
    bad += check("#46 ANIM OFF: the card values and the header BPM snap (no rolling digits), the frame is static", ok);
    {   /* the piano roll's view: ON glides toward the notes over frames, OFF jumps there at once */
        track_t *t = TSEL;
        uint32_t lo_on;
        ui_power_on();
        for (k = 0; k < NSTEP; k++) step_clear(&t->step[k]);
        t->step[0].n = 1; t->step[0].note[0] = 30; t->step[0].time = ST_NOTE;
        ui.force = 0;
        proll.init = 0; pr_follow(t); ui.frame++;
        t->step[0].note[0] = 90; pr_follow(t); lo_on = proll.lo;
        ui_prefs = PREF_ANIM_OFF;
        proll.init = 0; t->step[0].note[0] = 30; pr_follow(t); ui.frame++;
        t->step[0].note[0] = 90; pr_follow(t);
        ok = proll.moving == 0 && proll.lo > lo_on && lo_on != 0u;
        if (!ok) printf("  piano roll view: ON %u, OFF %u (moving %u)\n", lo_on, proll.lo, proll.moving);
        bad += check("#46 ANIM OFF: the piano roll's view jumps to the notes (ON: it glides)", ok);
    }
    {   /* saved with the settings; settings from before it: ON */
        sks_t p;
        persist_t q;                                    /* ((2.0) a record of before: PER3) */
        ui_prefs = PREF_ANIM_OFF;
        settings_export(&p);
        ui_prefs = 0;
        ok = settings_import(&p, (int)sizeof p) == 1 && (ui_prefs & PREF_ANIM_OFF);
        q = per3_of(&p);
        ok &= per3_import(&q) && !ui_prefs;
        bad += check("#46 ANIM saved with the settings; settings from before it: ON", ok);
    }
    ui_prefs = 0;
    return bad;
}

/* (Skiff U2) STYLE retired: BLOCKS is the only look; the MENU has no STYLE row (DISPLAY: COLOR LARGE ANIM LEDS
 * SCREEN OFF; the editor's id 1 retired); a saved STYLE byte (FLAT, LINE, the old PIXEL) is left as it was and changes
 * nothing */
static int test_style(void)
{
    int bad = 0, ok = 1;
    uint32_t i;
    sks_t p = {0};
    ui_power_on();
    for (i = 0; i < MI_COUNT; i++)
        ok &= !str_eq(MI_NAME[i], "STYLE");
    ok &= mtab_rows(MTAB_DISPLAY) == 7u && MI_LARGE == MI_PHEAD + 1u;   /* ((U4) HINTS, (#27) PLAYHEAD) */
    p.magic = SKS_MAGIC; p.panel = panel;
    settings_export(&p);
    for (i = 0; i < 4u; i++) {
        p.s.style = (uint8_t)i;     /* FLAT, LINE, PIXEL, BLOCKS as saved */
        ok &= settings_import(&p, (int)sizeof p) == 1 && p.s.style == i;
        ui.force = 1; frame();
        ok &= ux.surf != ux.bg;                        /* (BLOCKS: SURF tiles on the BG) */
    }
    bad += check("(U2) STYLE retired: no MENU row, a saved STYLE byte kept and ignored, BLOCKS drawn", ok);
    ui_power_on(); frame();
    return bad;
}

/* Skiff STYLE BLOCKS (ui_blocks.c, direction B): the knob just turned a whole accent tile (not its label row), a value
 * set in V, the gauge the track's colour along the tile's bottom; the header's transport left, bar.beat from the
 * selected track's step (stopped: 1.1 in MID), the track's cushion not there but in the footer; GLO's per-track cards;
 * the TAKE box (ui_take.c) and the dialog's buttons */
static int px_in(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)   /* c anywhere in x0..x1, y0..y1 */
{
    int32_t x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (lg_px(x, y) == c)
                return 1;
    return 0;
}
static int test_blocks(void)
{
    int bad = 0, ok;
    int32_t x, y, rows = 0;
    ui_power_on();
    settings.palette = UI_SKIFF_INDEX; palette_set(UI_SKIFF_INDEX);
    ui.force = 1; frame();
    turn(EN_K1, 1);                                    /* HOME: CUT just turned */
    ok = ui.home && ui.hot_t && ui.hot_col == 0u;
    ok &= lg_px(CARD_X(0) + 1, Y_LABEL + 30) == T_ACCENT && lg_px(CARD_X(1) + 1, Y_LABEL + 30) == T_SURF;
    ok &= lg_px(CARD_X(1), Y_LABEL + CARD_H - 1) == T_TRK(song.sel);   /* RES's gauge */
    for (y = Y_LABEL + 14; y < Y_LABEL + 40; y++)      /* RES's value: V's capitals, not M's */
        rows += px_in(CARD_X(1), y, CARD_X(1) + CARD_W - 1, y, T_THEME);
    ok &= rows >= AF_M_CAP_H + 3;
    bad += check("BLOCKS cards: the knob just turned a whole accent tile, values in V, the gauge the track's colour", ok);

    ui.force = 1; draw_head(); ui.force = 0;           /* stopped: 1.1 in MID */
    ok = px_in(22, 0, 50, H_HEAD - 1, T_MID) && !px_in(22, 0, 50, H_HEAD - 1, T_TEXT);
    song.playing = 1; TSEL->seq_idx = 20;              /* playing the track's step 20: 2.2 */
    ui.force = 1; draw_head(); ui.force = 0;
    ok &= px_in(22, 0, 50, H_HEAD - 1, T_TEXT) && !px_in(0, 0, 239, H_HEAD - 1, T_TRK(song.sel));
    song.playing = 0;
    press(B_ENV);
    ok &= !ui.home && px_in(8, Y_FOOT + 16, 30, H_FOOT + Y_FOOT - 1, T_TRK(song.sel));   /* the cushion, footer left */
    bad += check("BLOCKS header: bar.beat left (1.1 in MID stopped, the track's step playing), the track in the footer", ok);

    ui.hot_t = 0;                                      /* GLO's T1..T4 LEVEL: each card its track's cushion and colour */
    btn_down(B_GLO);
    for (y = 0; y < 40; y++)
        frame();
    ok = ui.layer == LAYER_GLO;
    for (y = 0; y < 4; y++)                            /* ((phase 6) the four shown: trk_top's) */
        ok &= lg_px(CARD_X(y), Y_LABEL + CARD_H - 1) == T_TRK(trk_top() + (uint32_t)y) &&
              px_in(CARD_X(y) + 4, Y_LABEL + 2, CARD_X(y) + 16, Y_LABEL + 15, T_TRK(trk_top() + (uint32_t)y));
    btn_up(B_GLO); frame();
    bad += check("BLOCKS GLO: the four LEVEL cards keep their track's cushion, their gauges its colour", ok);

    press(B_HOME);                                     /* a take (one note, pass 1): the TAKE box, the header quiet */
    song.rec = 0;
    take_clear();
    take_pass[0] = 2;
    take_ev[0] = (take_ev_t){1, 0, 0, 60, 100};
    take_ev_n = 1;
    take_commit(0);
    ok = ui.home && ui.take_t == TB_FRAMES && !ui.msg_t && str_eq(take_box.title, "TAKE KEPT") &&
         str_eq(take_box.line, "1 NOTE");
    frame();
    ok &= lg_px(TB_X + TB_W - 4, Y_GRAPH + TB_Y + 4) == T_TEXT;   /* the white box over the panel */
    undo_step(0);                                      /* SAVE held: the take undone, the undo box in its place */
    frame();
    ok &= ui.take_t && take_box.kind == 1u && str_eq(take_box.title, "UNDO TAKE") && str_eq(take_box.line, "1 OF 1 UNDONE") &&
          ui.undo_w && !ui.msg_t && lg_px(TB_X + TB_W - 4, Y_GRAPH + TB_Y + 4) == T_TEXT;
    ui.take_t = ui.undo_w = 0;
    bad += check("BLOCKS TAKE box: over the panel, the header quiet, 1 NOTE; undo: the undo box (UNDO TAKE, 1 OF 1 UNDONE)", ok);

    /* the dialog: CANCEL filled, the action outlined in REC */
    confirm_open(CF_CLEAR_SEQ, 0); frame();
    x = BD_X + BD_W - 12 - BD_BW;
    ok = ui.confirm == CF_CLEAR_SEQ && lg_px(BD_X + 14, BD_Y + BD_BY + 2) == T_TEXT &&
         lg_px(x, BD_Y + BD_BY + 10) == T_REC && lg_px(x + 3, BD_Y + BD_BY + 3) == T_SURF &&
         !px_in(BD_X, BD_Y + 36, BD_X + BD_W - 1, BD_Y + 70, T_REC);   /* (SAVE undoes it: no warning) */
    press(B_OCTDN);
    ok &= !ui.confirm;
    confirm_open(CF_OVR_PROJ, 0); frame();
    ok &= px_in(BD_X, BD_Y + 36, BD_X + BD_W - 1, BD_Y + 70, T_REC);   /* CAN'T BE UNDONE */
    press(B_OCTDN);
    ok &= !ui.confirm;
    bad += check("BLOCKS dialog: CANCEL filled, the action outlined in REC's colour, CAN'T BE UNDONE where SAVE can't", ok);
    ui_power_on();
    return bad;
}

/* (phase 5) track k on no pattern, one note: new steps (kept as a new pattern) */
static void fresh_pattern(uint32_t k, uint8_t note)
{
    track_defaults_steps(&trk[k]);
    trk_pid[k] = 0;
    trk[k].step[0] = (step_t){{note}, 1, ST_NOTE, 0, 90};
}
/* Skiff phase 5: SAVE > PATTERNS, the pool view (bank.c pool_refs, pool_clear; the pool itself: tests/bank_test.c):
 * KNOB 1 the pattern (01 .. 64), its LEN and how many use it on the cards; CLEAR always picked: OCT+ clears a pattern
 * nothing uses, after its dialog; on a used one it says how many use it; playing: STOP TO CLEAR; OCT- back HOME */
static int test_pool(void)
{
    int bad = 0, ok;
    uint32_t a, c, d;
    ui_power_on();
    fresh_pattern(song.sel, 50);                     /* 01: T1's A; 02: kept, then nothing names it */
    bank_put(song.sel, 0, 0);
    fresh_pattern(song.sel, 60);
    bank_put(song.sel, 2, 0);
    a = ent_pat(song.sel, 0);
    c = ent_pat(song.sel, 2);
    lists.ent[song.sel][2][0] = 0;
    trk_pid[song.sel] = 0;                           /* (the track on new steps) */
    go_page(GR_BANK);
    frame();
    ok = act_col() == 4u && a == 0u && c == 1u && pool_refs(a) == 1u && !pool_refs(c) && !act_ready();
    turn(EN_K1, 1);
    ok &= ui.bslot == c && act_ready();
    bad += check("PATTERNS (the pool): KNOB 1 the pattern; CLEAR ready on one nothing uses (02), not on T1's A's (01)",
                 ok);
    turn(EN_K1, -1);
    press(B_OCTUP);
    ok = !ui.confirm && msg_is("USED BY 1 ENTRY") && pool_used(a);
    turn(EN_K1, 1);
    song.playing = 1;
    press(B_OCTUP);
    ok &= !ui.confirm && msg_is("STOP TO CLEAR");
    song.playing = 0;
    bad += check("  OCT+ on a used one: USED BY 1 ENTRY; playing: STOP TO CLEAR", ok);
    press(B_OCTUP);
    ok = ui.confirm == CF_ERASE_BANK && ui.confirm_trk == c;
    press(B_OCTUP);
    ok &= !pool_used(c) && msg_is("CLEARED 02") && pool_used(a);
    press(B_OCTUP);
    ok &= msg_is("NOTHING TO CLEAR");
    bad += check("  OCT+ on 02: its dialog, then cleared (CLEARED 02); again: NOTHING TO CLEAR", ok);
    fresh_pattern(song.sel, 61);                     /* ((2.0, #46) 02 and 03 kept, then nothing names them */
    bank_put(song.sel, 2, 0);
    c = ent_pat(song.sel, 2);
    fresh_pattern(song.sel, 62);
    bank_put(song.sel, 3, 0);
    d = ent_pat(song.sel, 3);
    lists.ent[song.sel][2][0] = lists.ent[song.sel][3][0] = 0;
    trk_pid[song.sel] = 0;
    (void)entry_go(song.sel, 0);                     /* (T1 on A, 01; its new steps kept as one more pattern) */
    go_page(GR_BANK); frame();
    ui.bslot = (uint8_t)a;
    turn(EN_K4, 1);
    ok = ui.bact == BA_EMPTY && act_ready() && trk_pid[song.sel] == a + 1u && TSEL->step[0].time == ST_NOTE;
    press(B_OCTUP);
    ok &= ui.confirm == CF_EMPTY_BANK && ui.confirm_trk == a;
    press(B_OCTUP);
    ok &= msg_is("EMPTIED 01") && pool_used(a) && ent_pat(song.sel, 0) == a && TSEL->step[0].time == ST_REST;
    bad += check("  (2.0, #46) KNOB 4 EMPTY: 01's steps gone, T1 on it too; the pattern and its entry kept", ok);
    turn(EN_K4, 1);
    ok = ui.bact == BA_UNUSED && c == 1u && d == 2u && pool_unused() == 2u && act_ready();
    press(B_OCTUP);
    ok &= ui.confirm == CF_CLEAR_UNUSED && ui.confirm_trk == 2u;
    press(B_OCTUP);
    ok &= msg_is("CLEARED 2 PATTERNS") && !pool_used(c) && !pool_used(d) && pool_used(a) && !pool_unused();
    press(B_OCTUP);
    bad += check("  KNOB 4 CLEAR UNUSED: 02 and 03 after its dialog; again: NOTHING UNUSED", ok && msg_is("NOTHING UNUSED"));
    ok = pool_used(0u) && !pool_used(1u) && !pool_used(2u) && pool_used(3u) && pool_alloc() == 1u &&
         pool_alloc_after(3u) == 4u && pool_alloc_after(0u) == 1u && pool_alloc_after(NPAT - 1u) == 1u;
    bad += check("  (2.0, #34) a copy's number: the first free after its original's (04's copy 05, though 02 is free; "
                 "round from 64)", ok);
    press(B_OCTDN);
    bad += check("  OCT- back HOME", ui.home != 0u);
    return bad;
}

/* Skiff: the PERFORM layer (ARP held: ui_layer.c layer_perf; the switch itself: tests/bank_test.c live): the selected
 * track's entries A..P on the white keys; stopped a key puts the track on it at once (what it had kept: no dialog),
 * playing it stages it for its launch point (said; its key lit with the one playing, the other named ones breathe);
 * an empty entry says so; OCT- cancels; a stop brings the staged one in */
static void ly_tap(uint32_t k)                     /* a key in a layer: down (the ISR's, keyboard_block), up */
{
    key_down(k);
    frame();
    key_up(k);
    frame();
}
static int test_perform(void)
{
    int bad = 0, ok;
    uint32_t br, m, i, blacks = 0;
    track_t *t;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        blacks |= 1u << key_at(1, i);                /* (the tracks sounding: lit) */
    t = TSEL;
    fresh_pattern(song.sel, 50);
    bank_put(song.sel, 0, 0);                        /* A */
    fresh_pattern(song.sel, 55);
    bank_put(song.sel, 2, 0);                        /* C, the track on it */
    btn_down(B_ARP);
    frames(800);
    ok = ui.layer == LAYER_PERF;
    ly_tap(white(0));
    ok &= t->step[0].note[0] == 50 && msg_is("LOADED T1 A") && !ui.confirm && bank_cur(song.sel) == 0u;
    m = layer_leds(&br);
    ok &= m == (1u << white(0) | blacks) && br == 1u << white(2);
    bad += check("PERFORM (ARP held): entries on the white keys; stopped, A at once; A lit, C breathes", ok);
    ly_tap(key_at(1, 1));
    m = layer_leds(&br);
    ok = trk[1].p[P_MUTE] == 1 && !mute_arm[1] && !(m & 1u << key_at(1, 1)) && (br & 1u << key_at(1, 1));
    ly_tap(key_at(1, 1));
    bad += check("  stopped, black key 2: T2 muted at once (its key breathes), again: back", ok && !trk[1].p[P_MUTE]);
    song.playing = 1;
    ly_tap(white(2));
    ok = t->step[0].note[0] == 50 && bank_next(song.sel) == 2u && msg_is("T1 C AT LOOP END");
    m = layer_leds(&br);
    ok &= m == (1u << white(0) | 1u << white(2) | blacks) && !br;
    ly_tap(white(5));
    ok &= msg_is("T1 F EMPTY") && bank_next(song.sel) == 2u;
    bad += check("  playing: C staged, AT LOOP END, its key lit too; an empty entry says so, C stays staged", ok);
    ly_tap(key_at(1, 3));
    ok = !trk[3].p[P_MUTE] && mute_arm[3] == 2u && msg_is("T4 MUTE AT LOOP END") && !(layer_leds(&br) & 1u << key_at(1, 3));
    ly_tap(key_at(1, 3));
    ok &= !mute_arm[3] && msg_is("T4 MUTE CANCELLED");
    ly_tap(key_at(1, 3));
    bad += check("  playing, black key 4: T4's MUTE staged (said, its key breathes); again: cancelled; again: staged",
                 ok && mute_arm[3] == 2u);
    press(B_OCTDN);
    ok = bank_next(song.sel) == BK_NONE && !mute_arm[3] && msg_is("CANCELLED");
    press(B_OCTDN);
    ok &= msg_is("NOTHING STAGED");
    turn(EN_SELECT, 1);
    ok &= launch_mode() == LAUNCH_BAR && msg_is("LAUNCH BAR");
    ly_tap(white(2));
    ok &= msg_is("T1 C AT NEXT BAR");
    turn(EN_SELECT, 5);
    ok &= launch_mode() == LAUNCH_NOW;
    bad += check("  OCT- cancels all of it (again: NOTHING STAGED); SELECT LAUNCH: BAR said, .. NOW the last", ok);
    {   /* KNOB 1: a throw of the HOME's first parameter: the sound's own value stays, the card the thrown one */
        uint32_t id = ENGINES[t->eng_req % NENGINES]->knob[0];
        int16_t own = t->p[id];
        int out;
        turn(EN_K1, -3);
        ok = t->p[id] == own && throw_value(song.sel, 0, &out) == own - 3 && out && (thr_out & 1u);
        bad += check("  KNOB 1: a throw of HOME's first parameter (its own value kept; the card: the thrown one)", ok);
    }
    ly_tap(key_at(1, 2));                             /* (T3's mute staged, NOW) */
    btn_up(B_ARP);
    frame();
    song.playing = 0;
    frame();
    ok = !ui.layer && t->step[0].note[0] == 55 && bank_cur(song.sel) == 2u && bank_next(song.sel) == BK_NONE &&
         trk[2].p[P_MUTE] == 1 && !mute_arm[2];
    load_begin(t, UNDO_PAT);                          /* an edit on C, its undo copy (pid C) */
    t->step[0].note[0] = 58;
    load_end(t);
    bank_switch(song.sel, 0);                         /* A: C's edit kept as C */
    hold(B_SAVE);
    ok &= t->step[0].note[0] == 50 && !memcmp(ui.msg, "UNDO NEEDS PATTERN ", 19);
    bank_switch(song.sel, 2);                         /* back on C: its edit there; the undo works again */
    ok &= t->step[0].note[0] == 58;
    hold(B_SAVE);
    ok &= t->step[0].note[0] == 55;
    bad += check("  stopped: C comes in, T3's mute too; an edit on C kept over a switch; its undo refused on A, works on C", ok);
    return bad;
}

/* Skiff: scenes in the PERFORM layer (bank.c; the logic: tests/bank_test.c scenes): KNOB 2 KEYS SCENE puts the 16
 * scenes on the white keys (PRESETS turns KEYS); SAVE held + a key stores the tracks as they are, a key alone
 * recalls (stopped: at once); the one recalled or stored last lit, the other kept ones breathe; back: the entries */
static int test_scenes(void)
{
    int bad = 0, ok;
    uint32_t br, m;
    track_t *t;
    ui_power_on();
    t = TSEL;
    fresh_pattern(song.sel, 50);
    bank_put(song.sel, 0, 0);                        /* T1: A */
    btn_down(B_ARP);
    frames(800);
    turn(EN_PRESET, 1);
    ok = ui.layer == LAYER_PERF && perf_scenes() && msg_is("KEYS SCENE");
    btn_down(B_SAVE);
    frame();
    ly_tap(white(4));                                /* SAVE held + the 5th: scene 5 stored */
    btn_up(B_SAVE);
    frame();
    ok &= msg_is("SCENE 5 SAVED") && scene_used(4) && scene_slot(4, 0) == 0u && !ui.undo_w && ui.layer == LAYER_PERF;
    m = layer_leds(&br);
    ok &= (m >> white(4)) & 1u;
    bad += check("SCENES (PERFORM, PRESETS: KEYS SCENE): SAVE held + a key stores (T1's A), its key lit; no undo", ok);
    fresh_pattern(song.sel, 60);                     /* the track on new steps (kept as B when it leaves them) */
    ly_tap(white(4));
    ok = msg_is("LOADED SCENE 5") && t->step[0].note[0] == 50 && bank_cur(song.sel) == 0u && bank_used(song.sel, 1);
    ly_tap(white(9));
    ok &= msg_is("SCENE 10 EMPTY");
    bad += check("  a key: recalled (stopped: T1's A at once; its new steps kept as B); an empty one says so", ok);
    turn(EN_PRESET, -1);
    ok = !perf_scenes();
    ly_tap(white(2));
    ok &= msg_is("T1 C EMPTY");
    btn_up(B_ARP);
    frame();
    bad += check("  PRESETS back to TRACK: the white keys the entries again", ok);
    shift_lock(B_ARP); frames(500);   /* locked (a double tap) */
    turn(EN_PRESET, 1);
    btn_down(B_SAVE);
    frame();
    ly_tap(white(6));
    btn_up(B_SAVE);
    frame();
    ok = ui.lock == LAYER_PERF && msg_is("SCENE 7 SAVED") && scene_used(6) && !ui.undo_w;
    turn(EN_PRESET, -1);
    press(B_SAVE);
    frames(16);
    ok &= !ui.lock && !ui.layer;
    bad += check("  locked: SAVE + a key stores, the lock stays; KEYS TRACK: SAVE closes it, as any layer's", ok);
    return bad;
}

/* Skiff phase 5: SEQ > SONG (ui_input.c song_knob; bank.c song_oct, song_edit, song_clear_text; the song itself:
 * tests/bank_test.c song_test): KNOB 1 the selected track's entry, KNOB 2 a pattern for it (the used ones, then COPY;
 * OCT+ puts it in), KNOB 3 its repeats, KNOB 4 SONG; OCT+ with no pick plays the entry (stopped: at once; playing:
 * staged); OCT- drops a pick; EDIT clears the entry after its dialog (what happens to its pattern, the track on it
 * going empty); STEP's header names the pattern edited */
static int test_song_page(void)
{
    int bad = 0, ok, gone;
    track_t *t;
    char mk[16], l[3][32];
    uint32_t a, n;
    ui_power_on();
    t = TSEL;
    fresh_pattern(song.sel, 50);                     /* T1: A (01, 50), C (02, 60), the track on C */
    bank_put(song.sel, 0, 0);
    fresh_pattern(song.sel, 60);
    bank_put(song.sel, 2, 0);
    a = ent_pat(song.sel, 0);
    go_page(GR_SONG);
    frame();
    ok = str_eq(cur_page()->title, "SONG") && sg.entry == 0u && str_eq(song_act_name(), "PLAY");
    press(B_OCTUP);                                  /* OCT+ on A: the track to A, at once */
    ok &= t->step[0].note[0] == 50 && bank_cur(song.sel) == 0u && msg_is("LOADED T1 A");
    turn(EN_K1, 2);                                  /* C, its repeats 2, SONG ON */
    turn(EN_K3, 1);
    turn(EN_K4, 1);
    ok &= sg.entry == 2u && lists.ent[song.sel][2][1] == 1u && lists.song;
    bad += check("SONG: KNOB 1 the entry, 3 its repeats, 4 SONG; OCT+ plays the entry (stopped: at once)", ok);
    turn(EN_K1, -1);                                 /* ((1.2.1, #12) B, empty between A and C: a rest, its REPS too) */
    turn(EN_K3, 2);
    ok = sg.entry == 1u && ent_pat(song.sel, 1) == PAT_NONE && lists.ent[song.sel][1][1] == 2u;
    turn(EN_K3, -2);
    turn(EN_K1, 1);
    bad += check("  (#12) KNOB 3 sets an empty entry's repeats too (a rest's length)", ok && sg.entry == 2u);
    turn(EN_K2, -1);                                 /* C's 02 -> 01: OCT+ puts A's pattern into C too */
    ok = song_pick() == a + 1u && str_eq(song_act_name(), "SET 01");
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 2) == a && !song_pick() && msg_is("T1 C SET 01");
    bad += check("  KNOB 2 a pattern for C (the used ones), OCT+ puts it in", ok);
    turn(EN_K2, 2);                                  /* 01 -> 02 -> ((2.0) NEW: a used entry's after the patterns) */
    ok = song_pick() == SPICK_NEW && str_eq(song_act_name(), "NEW 03");
    turn(EN_K2, 1);                                  /* -> COPY */
    ok &= song_pick() == SPICK_COPY && str_eq(song_act_name(), "COPY 01 TO 03");
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 2) == 2u && pool_used(2) && msg_is("COPIED 01 TO 03");
    bad += check("  .. KNOB 2 to COPY: OCT+ makes a new pattern (03), a copy of C's, for C", ok);
    turn(EN_K2, -1);
    press(B_OCTDN);
    bad += check("  OCT- drops the pick", !song_pick() && msg_is("CANCELLED"));
    song.playing = 1;
    press(B_OCTUP);                                  /* playing: C staged */
    ok = bank_next(song.sel) == 2u && msg_is("T1 C AT LOOP END");
    turn(EN_K2, 40);                                 /* (U2) KNOB 2's last but ((2.0, #30) SOUND): CLEAR (was EDIT) */
    turn(EN_K2, -1);
    ok &= song_pick() == SPICK_CLEAR && str_eq(song_act_name(), "CLEAR");
    press(B_OCTUP);
    ok &= msg_is("STOP TO CLEAR") && !ui.confirm && !song_pick();
    song.playing = 0;
    frame();                                         /* (stopped before its launch point: C now) */
    ok &= bank_cur(song.sel) == 2u && bank_next(song.sel) == BK_NONE;
    bad += check("  playing: OCT+ stages the entry; CLEAR says STOP TO CLEAR; stopped: it comes in", ok);
    song_pick_clear();                                    /* C (03, C's only): it goes too, T1 on C goes empty */
    n = song_clear_text(2, l, &gone);
    ok = ui.confirm == CF_CLEAR_ENTRY && ui.confirm_trk == 2u && n == 2u && gone &&
         str_eq(l[0], "erases pattern 03 too") && str_eq(l[1], "T1 goes empty");
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 2) == PAT_NONE && !pool_used(2) && seq_is_empty(t) && !trk_pid[song.sel] &&
          msg_is("T1 C CLEARED");
    bad += check("  CLEAR: its dialog (03 goes too, T1 on C goes empty); OCT+: C cleared, 03 erased, the track empty", ok);
    lists.ent[song.sel][1][0] = (uint8_t)(a + 1u);  /* B names 01 too: CLEAR on A */
    turn(EN_K1, -2);
    song_pick_clear();
    n = song_clear_text(0, l, &gone);
    ok = ui.confirm == CF_CLEAR_ENTRY && n == 2u && !gone && str_eq(l[0], "pattern 01 stays:") &&
         str_eq(l[1], "entry B uses it");
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 0) == PAT_NONE && pool_used(a) && ent_pat(song.sel, 1) == a;
    bad += check("  CLEAR on an entry whose pattern another uses: 01 stays (entry B uses it), the entry cleared", ok);
    lists.ent[song.sel][3][0] = (uint8_t)(a + 1u);  /* D names 01 too: the track on B */
    ent_go(song.sel, 1);
    go_page(GR_ROLL);
    frame();
    ok = head_mark(mk) && str_eq(mk, "B \xB7 01 \xB7 2x");
    go_page(GR_SONG);
    frame();
    ok &= !head_mark(mk);
    song.playing = 1;
    ok &= head_mark(mk) && str_eq(mk, "SONG ON");
    song.playing = 0;
    bad += check("  STEP's header: the entry, the pattern, the entries naming it (B 01 2x); SONG, playing: SONG ON", ok);
    turn(EN_K1, 4);                                  /* (#28) E: empty, after D (the list's last) */
    turn(EN_K2, 1);                                  /* --, then ((2.0) NEW: an empty entry's first), REST */
    ok = sg.entry == 4u && song_pick() == SPICK_NEW;
    turn(EN_K2, 1);
    ok &= song_pick() == PAT_REST && str_eq(song_act_name(), "SET REST");
    press(B_OCTUP);
    ok &= ent_rest(song.sel, 4) && ent_pat(song.sel, 4) == PAT_NONE && !song_pick() && msg_is("T1 E SET REST");
    turn(EN_K2, 1);                                  /* REST, then the used ones */
    ok &= song_pick() == a + 1u;
    turn(EN_K2, -2);                                 /* back to its own (-- is an empty entry's): no pick */
    ok &= !song_pick();
    bad += check("  (#28) KNOB 2: --, REST, then the patterns; OCT+ SET REST", ok);
    press(B_OCTUP);                                  /* OCT+ with no pick: the track to the REST, at once */
    ok = lists.cur[song.sel] == 4u && !trk_pid[song.sel] && seq_is_empty(t) && msg_is("LOADED T1 E");
    song.playing = 1;
    song_pick_clear();                               /* CLEAR: no dialog (nothing goes with it), playing too */
    ok &= !ui.confirm && !ent_rest(song.sel, 4) && !lists.ent[song.sel][4][0] && msg_is("T1 E CLEARED");
    song.playing = 0;
    bad += check("  .. OCT+ plays it (the track silent on it); CLEAR it at once, playing too", ok);
    t->p[P_SLEN] = 12;                               /* ((2.0, #32, the user's pick 1A) NEW on E, the track on it */
    turn(EN_K2, 1);                                  /* (01, 02 kept: 03 the first free) */
    ok = song_pick() == SPICK_NEW && str_eq(song_act_name(), "NEW 03") && act_ready();
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 4) == 2u && pool_used(2) && trk_pid[song.sel] == 3u && seq_is_empty(t) &&
          t->p[P_SLEN] == 12 && pool_len(2) == 12u && !song_pick() && msg_is("LOADED T1 E");
    bad += check("  (2.0) NEW: an empty entry's first pick; OCT+: a new empty pattern (03, the track's LEN) in it", ok);
    lists.ent[song.sel][3][1] = 1;                   /* (#33) COPY+ on D (01 x2): E is 03, so to F */
    turn(EN_K1, -1);
    turn(EN_K2, 3);                                  /* 01 -> 02 -> 03 -> NEW */
    ok = song_pick() == SPICK_NEW;
    turn(EN_K2, 2);                                  /* -> COPY -> COPY+ */
    ok &= song_pick() == SPICK_COPYP && str_eq(song_act_name(), "COPY 01 TO 04 AT F");
    press(B_OCTUP);
    ok &= ent_pat(song.sel, 5) == 3u && lists.ent[song.sel][5][1] == 1u && pool_used(3) && ent_pat(song.sel, 3) == a &&
          lists.cur[song.sel] == 4u && msg_is("COPIED 01 TO 04 AT F");
    bad += check("  (#33) COPY+: the copy (04, the first free after 01) into F, the first empty entry after D, its REPS "
                 "too; D as it was", ok);
    for (n = 6; n < NENT; n++)                       /* (no empty entry after D: no COPY+) */
        lists.ent[song.sel][n][0] = (uint8_t)(a + 1u);
    turn(EN_K2, 40);
    turn(EN_K2, -2);
    ok = song_pick() == SPICK_COPY;
    turn(EN_K2, -1);
    ok &= song_pick() == SPICK_NEW;
    press(B_OCTDN);
    bad += check("  no empty entry after it: KNOB 2 passes COPY+ by (SOUND, CLEAR, COPY, NEW)", ok);
    {   /* (2.0, #30, the user's pick 2A) past CLEAR: SOUND, the track's sound (as loaded) onto the entry, and NO SOUND
         * on an entry with one; an edited sound refused (SAVE > USER); a user sound by its slot */
        char m[24], sn[16];
        uint32_t e0 = t->eng_req, k = song.sel;
        apply_preset_to(t, 2);
        sound_name(t, sn);
        str_cpy(m, "T1 D SOUND ", sizeof m);
        str_cpy(m + 11, sn, sizeof m - 11u);
        turn(EN_K2, 40);                             /* (D, 01 x2) */
        ok = sg.entry == 3u && song_pick() == SPICK_SOUND && !memcmp(song_act_name(), "SET SOUND ", 10) &&
             str_eq(song_act_name() + 10, sn) && act_ready();
        press(B_OCTUP);
        ok &= lists.snd[k][3][0] == e0 + 1u && lists.snd[k][3][1] == 2u && !song_pick() && msg_is(m);
        bad += check("  (2.0, #30) KNOB 2 past CLEAR: SOUND; OCT+ puts the track's sound (its preset 3) on D, said", ok);
        turn(EN_K2, 40);
        ok = song_pick() == SPICK_NOSND && str_eq(song_act_name(), "SET NO SOUND");
        turn(EN_K2, -1);
        ok &= song_pick() == SPICK_SOUND;
        turn(EN_K2, -1);
        ok &= song_pick() == SPICK_CLEAR;
        turn(EN_K2, 2);
        press(B_OCTUP);
        ok &= !lists.snd[k][3][0] && !lists.snd[k][3][1] && msg_is("T1 D NO SOUND");
        bad += check("  .. an entry with a sound: NO SOUND last (SOUND, CLEAR before it); OCT+ takes it off, said", ok);
        t->p[P_DEC] = (int16_t)(t->p[P_DEC] + 5);    /* (a knob turned since the load) */
        turn(EN_K2, 40);
        ok = song_pick() == SPICK_SOUND && !act_ready();
        press(B_OCTUP);
        ok &= !lists.snd[k][3][0] && msg_is("SOUND EDITED") && str_eq(ui.msg2, "SAVE > USER FIRST");
        bad += check("  an edited sound: OCT+ doesn't breathe, refused, the way said (SAVE > USER FIRST)", ok);
        up_store(5, "ENTRY SND");                    /* (the edited one as a user sound, slot 6, loaded) */
        up_load(5);
        turn(EN_K2, 40);
        press(B_OCTUP);
        ok = lists.snd[k][3][0] == (0x80u | 5u) && !lists.snd[k][3][1] && msg_is("T1 D SOUND ENTRY SND");
        turn(EN_K1, -1);                             /* (C, cleared: empty) */
        turn(EN_K2, 40);
        ok &= sg.entry == 2u && song_pick() != SPICK_SOUND && song_pick() != SPICK_NOSND;
        press(B_OCTDN);
        bad += check("  a user sound: its slot on the entry; an empty entry: no SOUND to pick", ok);
        up_put(5, 0);
    }
    lists.song = 0;
    return bad;
}

/* (2.0, #44, the user's pick 3A and the join sheet's A) a chain on SONG and NOTES (playing it: tests/bank_test.c
 * song_chain): KNOB 3 has CHAIN below 1x on an entry with one after it, right again a number (SPLIT); NOTES' header
 * names the chain and the part (A-B · 02), its title and START count the steps on through it; stopped, PRESETS past
 * the part's end puts the track on the next part (the first after the last), back past its start the one before;
 * playing, it wraps inside the part; COPY+ on a chain says its entries */
static int test_song_chain(void)
{
    int bad = 0, ok;
    uint32_t k;
    char mk[16], w[16];
    ui_power_on();
    lists_clear();
    k = song.sel;
    fresh_pattern(k, 50);                            /* T1: A (8 steps, 50 first), B (8 steps, 60 first) */
    TSEL->p[P_SLEN] = 8;
    bank_put(k, 0, 0);
    fresh_pattern(k, 60);
    TSEL->p[P_SLEN] = 8;
    bank_put(k, 1, 0);                               /* (the track on B) */
    go_page(GR_SONG);
    frame();
    turn(EN_K1, -16);
    turn(EN_K3, -1);                                 /* A: 1x -> CHAIN, the lowest */
    ok = sg.entry == 0u && lists.ent[k][0][1] == REPS_CHAIN;
    turn(EN_K3, -1);
    ok &= lists.ent[k][0][1] == REPS_CHAIN;
    lists.ent[k][15][0] = lists.ent[k][0][0];        /* (P: no entry after it) */
    turn(EN_K1, 15);
    turn(EN_K3, -1);
    ok &= sg.entry == 15u && !lists.ent[k][15][1];
    lists.ent[k][15][0] = 0;
    bad += check("(2.0, #44) SONG's KNOB 3: CHAIN below 1x (not on the last entry)", ok);
    go_page(GR_ROLL);
    cursor_set(4);
    frame();
    str_cpy(w, "A-B \xB7 ", sizeof w);               /* (A-B \xB7 and the part's number) */
    pat_num(w + 6, ent_pat(k, 1));
    ok = head_mark(mk) && str_eq(mk, w) && str_eq(page_head(), "NOTES 13");
    turn(EN_PRESET, -5);                             /* stopped: back past B's start, onto A's step 8 */
    pat_num(w + 6, ent_pat(k, 0));
    ok &= lists.cur[k] == 0u && ui.cursor == 7u && TSEL->step[0].note[0] == 50 && str_eq(page_head(), "NOTES 8") &&
          head_mark(mk) && str_eq(mk, w);
    turn(EN_PRESET, 1);                              /* on past A's end: B's first */
    ok &= lists.cur[k] == 1u && ui.cursor == 0u && TSEL->step[0].note[0] == 60 && str_eq(page_head(), "NOTES 9");
    turn(EN_PRESET, 8);                              /* past B's end, the chain's last: A's first again */
    ok &= lists.cur[k] == 0u && ui.cursor == 0u && TSEL->step[0].note[0] == 50;
    bad += check("  NOTES: A-B \xB7 02, steps counted on (NOTES 13); stopped, PRESETS goes on into the next part, back "
                 "into the one before", ok);
    song.playing = 1;
    turn(EN_PRESET, -1);
    ok = lists.cur[k] == 0u && ui.cursor == 7u;
    song.playing = 0;
    bad += check("  playing: PRESETS wraps inside the part playing", ok);
    go_page(GR_SONG);
    sg.entry = 0;
    sg.pick = SPICK_COPYP;
    sg.pick_at = (uint8_t)(1u + k * 16u);
    ok = str_eq(song_act_name(), "COPY A-B TO C-D");
    sg.pick = 0;
    turn(EN_K3, 1);                                  /* SPLIT: A 1x again */
    go_page(GR_ROLL);
    frame();
    str_cpy(w, "A \xB7 ", sizeof w);
    pat_num(w + 4, ent_pat(k, 0));
    ok &= !lists.ent[k][0][1] && head_mark(mk) && str_eq(mk, w);
    bad += check("  COPY+ on a chain: COPY A-B TO C-D; KNOB 3 right: a number again, the chain split", ok);
    set_engine_of(TSEL, ENGI_DRUM);                  /* the DRUM grid: its page keys through the parts too */
    TSEL->engine = TSEL->eng_req;
    lists.ent[k][0][1] = REPS_CHAIN;
    (void)ent_go(k, 0);
    go_page(GR_ROLL);
    frame();
    tap_key(key_at(1, GK_PGUP));                     /* (A's one page: on into B's) */
    ok = grid_on() && lists.cur[k] == 1u && ui.bank == 0u;
    tap_key(key_at(1, GK_PGDN));
    ok &= lists.cur[k] == 0u;
    bad += check("  the DRUM grid's page keys go through a chain's parts, stopped", ok);
    lists_clear();
    return bad;
}

/* (Skiff phase 9) FX > DRIVE after FX: KNOB 2 its TYPE, KNOB 3 its TONE; ENV 2 after ENV, hidden where ENV is
 * (DRUM's one-shots, FM6's own envelopes) */
static int test_p9_pages(void)
{
    int bad = 0, ok;
    uint32_t i, e2 = NPAGES;
    ui_power_on();
    go_title("DRIVE"); frame();
    ok = cur_page()->fam == FAM_FX && cur_page()->id[0] == P_DIST && cur_page()->id[1] == P_DTYPE &&
         cur_page()->id[2] == P_DTONE && str_eq(PAGES[ui.page - 1u].title, "FX");
    turn(EN_K2, 2);
    turn(EN_K3, -5);
    bad += check("(phase 9) FX > DRIVE after FX: KNOB 2 TYPE (FOLD after two), KNOB 3 TONE",
                 ok && TSEL->p[P_DTYPE] == 2 && TSEL->p[P_DTONE] == -5);
    for (i = 0; i < NPAGES; i++)
        if (str_eq(PAGES[i].title, "ENV 2"))
            e2 = i;
    ok = e2 < NPAGES && str_eq(PAGES[e2 - 1u].title, "ENV") && PAGES[e2].graph == GR_ADSR && page_visible(e2);
    set_engine_of(TSEL, ENGI_DRUM);
    ok &= !page_visible(e2);
    set_engine_of(TSEL, ENGI_FM6);
    ok &= !page_visible(e2);
    bad += check("  ENV 2 after ENV (its ADSR), hidden where ENV is (DRUM, FM6)", ok);
    return bad;
}

/* Skiff: FX > DUCK (duck.c; the sound itself: tests/duck_test.c): after SLICER; KNOB 1 BY steps over the track itself
 * (T2: OFF T1 T3 T4; T4: up to T3; T1: down to OFF), KNOB 2 DEPTH, KNOB 3 REL; a sound load keeps all three (the mix's);
 * DEPTH and REL can be automation, BY not (routing) */
static int test_duck_page(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    for (i = 0; i < NPAGES && !str_eq(PAGES[i].title, "SLICER"); i++)
        ;
    ok = i + 1u < NPAGES && str_eq(PAGES[i + 1u].title, "DUCK") && PAGES[i + 1u].fam == FAM_FX && PAGES[i + 1u].graph == GR_DUCK;
    sel_set(1);
    go_title("DUCK");
    frame();
    turn(EN_K1, 1);
    ok &= TSEL->p[P_DKSRC] == 1;
    turn(EN_K1, 1);
    ok &= TSEL->p[P_DKSRC] == 3;                    /* (not T2) */
    for (i = 0; i < NTRK; i++)                      /* (up to the last track, there it stays) */
        turn(EN_K1, 1);
    ok &= TSEL->p[P_DKSRC] == NTRK;
    for (i = 0; i + 2u < NTRK; i++)                 /* (down past T3: T1) */
        turn(EN_K1, -1);
    ok &= TSEL->p[P_DKSRC] == 1;
    bad += check("FX > DUCK after SLICER; on T2 KNOB 1 BY steps OFF T1 T3 .. the last track, over T2", ok);
    sel_set((uint8_t)(NTRK - 1u));
    frame();
    for (i = 0; i < NTRK; i++)
        turn(EN_K1, 1);
    ok = TSEL->p[P_DKSRC] == NTRK - 1u;
    sel_set(0);
    frame();
    TSEL->p[P_DKSRC] = 2;
    turn(EN_K1, -1);
    ok &= TSEL->p[P_DKSRC] == 0;
    bad += check("  on the last track it stops at the one before; on T1 down from T2 to OFF", ok);
    TSEL->p[P_DKSRC] = 4;
    turn(EN_K2, -10);
    turn(EN_K3, 5);
    {
        int16_t a = TSEL->p[P_DKAMT], r = TSEL->p[P_DKREL];
        ok = a < 96 && r > 73;
        turn(EN_PRESET, 1);                          /* a sound load */
        ok &= TSEL->p[P_DKSRC] == 4 && TSEL->p[P_DKAMT] == a && TSEL->p[P_DKREL] == r;
    }
    ok &= !motion_param(P_DKSRC) && motion_param(P_DKAMT) && motion_param(P_DKREL);
    bad += check("  KNOB 2 DEPTH, 3 REL; a sound load keeps the three; DEPTH and REL can be automation, BY not", ok);
    return bad;
}

/* Skiff: multi-level undo (ui.c undo_ring, undo_step). Each act: its own copy (step i of track t gets note n) */
static void undo_act(uint32_t k, uint32_t i, uint32_t n, const char *name)
{
    track_t *t = &trk[k];
    undo.keep = 0;                                     /* (as an action: its own copy, no browse sharing) */
    undo_label = name;
    load_begin(t, UNDO_PAT);
    t->step[i] = (step_t){{(uint8_t)n}, 1, ST_NOTE, 0, 100};
    load_end(t);
}
static int test_undo_levels(void)
{
    int bad = 0, ok;
    uint32_t i;
    ui_power_on();
    for (i = 0; i < NTRK; i++)
        track_defaults_steps(&trk[i]);
    undo_act(0, 0, 40, "A");
    undo_act(1, 1, 41, "B");
    undo_act(0, 2, 42, "C");
    hold(B_SAVE);                                      /* back one: C */
    ok = !trk[0].step[2].n && trk[1].step[1].n && trk[0].step[0].n && undo_is("UNDO T1 C") && ui.undo_w;
    press(B_OCTDN);                                    /* the window: OCT- further (B, on track 2), the octave kept */
    ok &= !trk[1].step[1].n && trk[0].step[0].n && undo_is("UNDO T2 B") && song.octave == 0;
    press(B_OCTDN);
    ok &= !trk[0].step[0].n && undo_is("UNDO T1 A") && undo_back == 3u;
    press(B_OCTDN);
    ok &= msg_is("NOTHING TO UNDO") && ui.undo_w && song.octave == 0;
    press(B_OCTUP);                                    /* OCT+: forward again, in order */
    ok &= trk[0].step[0].n && !trk[1].step[1].n && undo_is("REDO T1 A");
    press(B_OCTUP);
    ok &= trk[1].step[1].n && undo_is("REDO T2 B") && undo_back == 1u;
    bad += check("undo levels: SAVE held back one (UNDO T1 C), OCT- further (T2 B, T1 A) across tracks, OCT+ redo in order", ok);
    undo_act(1, 3, 43, "D");                           /* a new edit after an undo: its redo (C) is gone */
    ok = undo_n == 3u && undo_back == 0u && !trk[0].step[2].n;
    undo_step(1);
    ok &= msg_is("NOTHING TO REDO") && !trk[0].step[2].n;
    undo_step(0);
    ok &= !trk[1].step[3].n && undo_is("UNDO T2 D");
    bad += check("  a new edit after undos drops their redo (NOTHING TO REDO); its own undo works", ok);
    ui_power_on();
    for (i = 0; i < 10u; i++)                          /* more acts than levels: the oldest go */
        undo_act(0, i, 50u + i, "X");
    for (i = 0; i < 10u; i++)
        undo_step(0);
    ok = undo_n == UNDO_N && undo_back == UNDO_N && trk[0].step[0].n && trk[0].step[1].n && !trk[0].step[2].n;
    bad += check("  10 acts, 8 levels: the newest 8 undo, the oldest 2 stay", ok);
    for (i = 0; i < UNDO_WIN; i++)                     /* the window closes: OCT+ is the octave again */
        frame();
    press(B_OCTUP);
    bad += check("  the undo window closed (~2.7 s): OCT+ moves the octave, no redo", !ui.undo_w && song.octave == 1 &&
                 undo_back == UNDO_N);
    song.octave = 0;
    ui_power_on();
    return bad;
}

/* (Skiff phase 6) six tracks on the layers: GLO's SOLO T5 and T6 on C4 and D4 (#11) and their MUTEs on D#4 and F#4; T5
 * selected, KNOB 4 its LEVEL (the four shown: T2 .. T5), OCT- every track's mute and level back; PERFORM's black keys 5
 * and 6; EDIT on T5: PHYS's key says why, KNOB 1 never lands on PHYS or GRAIN */
static int test_six_tracks(void)
{
    int bad = 0, ok;
    uint32_t i, p, n = 0, e0;
    int16_t lv[NTRK];
    ui_power_on();
    lay_combo(B_GLO, white(4));                       /* C4 held */
    perf_begin(CTL);
    ok = perf_solo == 1u << 4 && ((perf_act >> PF_M1) & 63u) == 0x2Fu;
    key_up(white(4)); frame();
    key_down(white(5)); frame();                      /* D4 */
    perf_begin(CTL);
    ok &= perf_solo == 1u << 5;
    key_up(white(5)); btn_up(B_GLO); frame();
    perf_begin(CTL);
    bad += check("(#11) GLO + C4 held: SOLO T5 (the five others muted); D4: SOLO T6", ok && !perf_solo);
    lay_combo(B_GLO, black(5)); key_up(black(5)); frame();
    ok = trk[5].p[P_MUTE] == 1 && !trk[4].p[P_MUTE];
    key_down(black(4)); frame(); key_up(black(4)); frame();
    btn_up(B_GLO); frame();
    bad += check("  GLO + F#4: MUTE T6 latched; D#4: T5", ok && trk[4].p[P_MUTE] == 1 && trk[5].p[P_MUTE] == 1);
    for (i = 0; i < NTRK; i++) {
        trk[i].p[P_MUTE] = 0;
        lv[i] = trk[i].p[P_LEVEL];
    }
    sel_set(4);
    btn_down(B_GLO); frame();
    turn(EN_K4, -10);
    ok = trk[4].p[P_LEVEL] == lv[4] - 10 && trk[3].p[P_LEVEL] == lv[3];
    key_down(black(5)); frame(); key_up(black(5)); frame();
    ok &= trk[5].p[P_MUTE] == 1;
    oct_back();
    for (i = 0; i < NTRK; i++)
        ok &= trk[i].p[P_LEVEL] == lv[i] && !trk[i].p[P_MUTE];
    btn_up(B_GLO); frame();
    bad += check("  T5 selected: KNOB 4 is T5's LEVEL (T2 .. T5 shown); OCT-: every track's mute and level back", ok);
    ui_power_on();
    btn_down(B_ARP);
    frames(800);
    ok = ui.layer == LAYER_PERF;
    ly_tap(black(4));
    ok &= trk[4].p[P_MUTE] == 1 && !mute_arm[4];
    song.playing = 1;
    ly_tap(black(5));
    ok &= !trk[5].p[P_MUTE] && mute_arm[5] == 2u && msg_is("T6 MUTE AT LOOP END");
    press(B_OCTDN);
    ok &= !mute_arm[5];
    btn_up(B_ARP); frame();
    song.playing = 0;
    bad += check("  PERFORM: black keys 5 and 6, T5's and T6's MUTE (stopped: at once; playing: staged)", ok);
    ui_power_on();
    sel_set(4);
    e0 = TSEL->eng_req;
    for (p = 0; p < NENG_SHOWN && eng_vis(p) != ENGI_PHYS; p++)
        ;
    btn_down(B_EDIT); frame();
    ly_tap(white(p));                                 /* PHYS's key */
    ok = p < NENG_SHOWN && TSEL->eng_req == e0 && msg_is("PHYS: T1-T4 ONLY");
    for (i = 0; i < 2u * NENG_SHOWN; i++) {           /* KNOB 1 up through every engine, then down */
        turn(EN_K1, i < NENG_SHOWN ? 1 : -1);
        ok &= eng_allowed(4, TSEL->eng_req);
        n += TSEL->eng_req != e0;
    }
    btn_up(B_EDIT); frame();
    bad += check("  EDIT on T5: PHYS's key says why (the engine stays); KNOB 1 never lands on PHYS or GRAIN", ok && n);
    return bad;
}

/* (Skiff #27, @tradiuz) MENU > DISPLAY > PLAYHEAD: THEME (the figure on HOME's selected lane at the playhead: CRYPT's
 * torch, NEON's car, QUARK's collision) or LINE (the playhead's line only); SKIFF, HI-CON and PAPER draw none either way */
static int test_playhead_line(void)
{
    static uint16_t fig[240 * 240];
    int bad = 0, ok;
    uint32_t p, f0;
    ui_power_on();
    ok = str_eq(MI_NAME[MI_PHEAD], "PLAYHEAD") && MI_PHEAD == MI_COLOR + 1u && MI_TAB[MI_PHEAD] == MTAB_DISPLAY &&
         menu_n(MI_PHEAD) == 2u && str_eq(menu_vname(MI_PHEAD, 0), "THEME") && str_eq(menu_vname(MI_PHEAD, 1), "LINE") &&
         menu_get(MI_PHEAD) == 0u && !ui_ph_line && menu_step(MI_PHEAD, 1) == 1u && menu_step(MI_PHEAD, -1) == 0u;
    bad += check("(#27) PLAYHEAD: a MENU > DISPLAY row after THEME, THEME (the default, stored 0) or LINE", ok);
    ok = ui.home && !ui.home_scope;
    song.playing = 1;
    TSEL->seq_idx = 6;
    for (p = 0; p < NPALETTES; p++) {
        int figure = p == UI_CRYPT_INDEX || p == UI_NEON_INDEX || p == UI_QUARK_INDEX;
        menu_put(MI_COLOR, p);
        f0 = ui.frame;
        menu_put(MI_PHEAD, 0); ui.force = 1; ui_draw();
        memcpy(fig, host_screen, sizeof fig);
        ui.frame = f0;
        menu_put(MI_PHEAD, 1); ui.force = 1; ui_draw();
        ok &= !memcmp(fig, host_screen, sizeof fig) == !figure;
        if (!ok) { printf("  PLAYHEAD LINE on %s: %s\n", UI_PALETTES[p].name, figure ? "the same" : "changed"); break; }
    }
    menu_put(MI_PHEAD, 0);
    menu_put(MI_COLOR, UI_SKIFF_INDEX);
    song.playing = 0;
    bad += check("(#27) PLAYHEAD LINE: HOME without the torch, the car, the collision; SKIFF, HI-CON, PAPER as they were", ok);
    return bad;
}

#include "tape_ui_test.inc"
#if FELUCCA_TAPE
#include "tape_fx_ui_test.inc"
#endif
#ifndef UI_PORT_ONLY
int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int bad = 0;
#if FELUCCA_TAPE
    bad += test_tape_aux_page();
    bad += test_tape_fx_pages();
#endif
    bad += test_large_face();
    bad += test_sound_loads();
    bad += test_rec();
    bad += test_step_rec();
    bad += test_step_rec();
    bad += test_presets_knob();
    bad += test_midi();
    bad += test_save();
    bad += test_actions();
    bad += test_tracks();
    bad += test_grid();
    bad += test_home_footer();
    bad += test_home_names();
    bad += test_clock_wait();
    bad += test_grid_rows();
    bad += test_mixer_no_flash();
    bad += test_screen();
    bad += test_favorites();
    bad += test_preset_tags();
    bad += test_display_preferences();
    bad += test_menu_tabs();
    bad += test_information();
    bad += test_product_ux();
    bad += test_roll();
    bad += test_soak_cards();
    bad += test_panel();
    bad += test_layer();
    bad += test_layer_knob_leak();
    bad += test_layer_knob_race();
    bad += test_fx_latch();
    bad += test_layer_lock();
    bad += test_rec_layer();
    bad += test_seq_tools();
    bad += test_menu_prefs();
    bad += test_style();
    bad += test_blocks();
    bad += test_undo_levels();
    bad += test_pool();                               /* Skiff phase 5: SAVE > PATTERNS, the pool view */
    bad += test_perform();                            /* Skiff: the PERFORM layer (live switching) */
    bad += test_scenes();                             /* Skiff: scenes (the PERFORM layer's KEYS SCENE) */
    bad += test_song_page();                          /* Skiff phase 5: SEQ > SONG */
    bad += test_song_chain();                         /* (2.0, #44) a chain on SONG and NOTES */
    bad += test_duck_page();                          /* Skiff: FX > DUCK */
    bad += test_p9_pages();                           /* (Skiff phase 9) FX > DRIVE, ENV 2 */
    bad += test_six_tracks();                         /* Skiff phase 6: six tracks on the layers */
    bad += test_div_order();
    bad += test_knob_accel();
    bad += test_usb_level();
    bad += test_click_menu();
    bad += test_usb_serial();
    bad += test_bpm_lock();
    bad += test_select_pages();
    bad += test_notes_sel();
    bad += test_notes_delete();
    bad += test_notes_overlap();
    bad += test_drum_lane_copy();
    bad += test_tools_overlap();
    bad += test_rec_overlap();
    bad += test_mod_routes();
    bad += test_mutes_1a();
    bad += test_way_back_2a();
    bad += test_u1();
    bad += test_u2();
    bad += test_u2c();
    bad += test_u3();
    bad += test_u4();
    bad += test_u5();
    bad += test_large();
    bad += test_browse_no_pattern();
    bad += test_name();
    bad += test_edit_cycle();
    bad += test_fm6_lab();
#if FELUCCA_SLICE
    bad += test_slices();
    bad += test_samples_page();
#endif
    bad += test_sample_alert();
    bad += test_quick_layers();
    bad += test_chord_page();
    bad += test_bughunt_ui();
    bad += test_bughunt_ui2();
    bad += test_piano_roll();
    bad += test_play_leds();
    bad += test_cursor_step_leds();
    bad += test_midi_leds();
    bad += test_seq_quant();
    bad += test_idle_glow();
    bad += test_breath();
    bad += test_step_leds();
    bad += test_scale_leds();
    bad += test_screen_off();
    bad += test_playhead_line();
    bad += test_fm6_charts();
#if FELUCCA_FM4
    bad += test_fm_charts();                        /* (DIGITAL's charts: built with FELUCCA_FM4=1 only) */
#else
    bad += test_fm4_retired();
#endif

    printf(bad ? "ui: %d FAILED\n" : "ui: all passed\n", bad);
    return bad ? 1 : 0;
}
#endif

#endif /* UI_PORT_ONLY */
