/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of Skiff's SMK-37 Pro controls (BOARD_SMK37, design/SMK37_UX.md as picked): the UI of tests/ui_test.c built
 * for the SMK (its prelude, the input stood in by the SMK's, hal/smk_pads.h as it is), and smk_ui.c:
 *   PADS     on HOME the step taps, in a layer and the DRUM grid the white keys; a tap sets a step to the last note
 *            (its velocity), again clears it, one undo a run; DRUM: the lane; past LEN nothing; the colours (A).
 *   FADERS   the four tracks shown, taking over once they reach the value, FADER BANK (its LED), the window.
 *   ENTRIES  > and < the selected track's entry after and before, the first's end said.
 *   KEYS     the outer keys' notes and velocity (KEY VELOCITY), out of the TRS jack too; the curves.
 *   MENU     KEY VELOCITY in CONTROL, no LEDS row, the editor's rows; USR2 / USR3 not offered.
 *   WHEELS   the wheels and the pedal as MIDI in on the selected track's channel, back to rest on a switch.
 *   PEDAL    holding the keys let go (the 27 and the outer ones) to its release, a key struck again, another track
 *            selected, a preset loaded, a MONO track's note, a DRUM track not held.
 *   SHIFT    KNOB BANK: the map (the SMK's names), + BT TAKE, no MENU when let go.
 *   MASTER   said while turned, kept at rest, back at power-on.
 *   CLOCK    the TRS jack's MIDI clock: Start, 24 a beat, Stop; none with an external clock.
 * Built with -DBOARD_SMK37=1 and run by tests/run_tests.sh. Generated once from ui_test.c's prelude (keep them alike). */
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                                    /* (as src/felucca.c: the console built in, MENU > USB SERIAL) */
#endif
#include <stddef.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3 (zero: empty), as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
/* ---------------------------------------------------------------------- the SMK-37's input, stood in --- */
#include "../firmware/hal/smk_pads.h"            /* (as it is: the tables, queues and curves) */
#define HOST_FM1_IN 1                             /* (hostsim.c: this fm1_in, the SMK's fields the UI reads) */
static struct {
    volatile uint32_t notes, buttons;
    uint8_t key_vel[37];
    uint8_t fader[4];
    uint16_t wheel[2], pedal, batt;
    uint32_t frames;
} fm1_in;
#define HOST_UT 512u                              /* what goes out of the TRS jack (hal/smk_uart_tx.h) */
static uint8_t host_ut[HOST_UT];
static uint32_t host_ut_n;
static void smk_uart_tx_put(uint32_t b)
{
    if (host_ut_n < HOST_UT)
        host_ut[host_ut_n] = (uint8_t)b;
    host_ut_n++;
}
#define main hostsim_main
#include "hostsim.c"
#undef main

/* ------------------------------------------------------------ HAL stubs --- */
#define FM1_NCOL 16u
#define FM1_NBTN 18u
#define FM1_NENC 8u
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t host_dim_lo;                      /* hal/fm1_input.h: the glow MENU > LEDS asked for (1: DIM LO) */
static void fm1_led_dim_level(uint32_t lo) { host_dim_lo = lo; }
static int host_anim;                             /* hal/fm1_input.h: the power-on sweep running (it has the LEDs) */
static int fm1_led_anim_on(void) { return host_anim; }
static uint8_t host_led[SMK_NLED];               /* the button LEDs (smk_input.h fm1_led_key) */
static void fm1_led_key(uint32_t id, int on) { if (id < SMK_NLED) host_led[id] = (uint8_t)(on != 0); }
static uint32_t host_pad[SMK_NPAD];               /* the pads' colours, 0xRRGGBB (lcd.c padled_set) */
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad < SMK_NPAD)
        host_pad[pad] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}
#define FM1_TICKS_PER_US 1u
static uint32_t host_ticks, host_pressed, host_notes;
static int32_t host_enc[8];
static uint32_t fm1_ticks(void) { return host_ticks; }
static uint32_t fm1_input_edges(int x) { uint32_t p = host_pressed; (void)x; host_pressed = 0; return p; }
static uint32_t fm1_input_note_edges(void) { uint32_t n = host_notes; host_notes = 0; return n; }
/* host_enc_late: detents the TIMER5 scan counts just after a read (#102: while ui_input runs); the next read has them */
static int32_t host_enc_late[8];
static int32_t fm1_enc_take(uint32_t e)
{
    int32_t s = host_enc[e % 8u];
    host_enc[e % 8u] = host_enc_late[e % 8u];
    host_enc_late[e % 8u] = 0;
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
#include "../firmware/src/smk_ui.c"
#include "phrases.h"                              /* (test material: the phrases the firmware no longer ships) */

static int check(const char *what, int ok)
{
    printf("smk ui: %-86s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* main.c felucca_init */
static void ui_power_on(void)
{
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song); sel_set(0);
    memset(&ui, 0, sizeof ui);
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

/* ------------------------------------------------------------------------------------------- the SMK's --- */
static void smk_power_on(void)
{
    ui_power_on();
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    fm1_in.frames = 1000u;                        /* (the readings settled) */
    memset(host_led, 0, sizeof host_led);
    memset(host_pad, 0, sizeof host_pad);
    smk_padq_r = smk_padq_w = smk_extq_r = smk_extq_w = 0;
    smk_fbank = 0;
    memset(smk_fd, 0xFF, sizeof smk_fd);
    smk_kv_mode = SMK_KV_DYN;
    smk_master = 724;
    host_ut_n = 0;
    smk_sh_t0 = 0;
    last_note = 60;
}
/* one UI frame as main.c runs it on the SMK: the input, the SMK's own controls, the LEDs (and pads), the screen */
static void frame(void)
{
    host_ticks += 16000u;
    fm1_ms += 16u;
    fm1_in.frames += 10u;
    ui_input();
    smk_ui_frame();
    ui_leds();
    ui_draw();
}
static int msg_is(const char *s) { return ui.msg_t && str_eq(ui.msg, s); }
static void count_wide_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
static void frames(uint32_t ms) { uint32_t i; for (i = 0; i < ms / 16u; i++) frame(); }
static void press_id(uint32_t id)                 /* a button by its SMK id: down, a frame, up, a frame */
{
    fm1_in.buttons |= 1u << id;
    host_pressed |= 1u << id;
    frame();
    fm1_in.buttons &= ~(1u << id);
    frame();
}
static void press(uint32_t label) { press_id(panel.btn[label]); }
static void pad_tap(uint32_t p, uint32_t vel)     /* what the scan queues for a step tap (smk_input.h smk__padq) */
{
    smk_padq[smk_padq_w] = (uint16_t)(p << 8 | vel);
    smk_padq_w = (uint8_t)((smk_padq_w + 1u) % SMK_PADQ);
    frame();
}
static void go_page(uint32_t graph)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].graph == graph)
            break;
    ui.home = 0;
    ui.page = (uint8_t)i;
    page_entered();
}
static uint32_t midi_msgs(uint32_t *st, uint32_t *d1, uint32_t *d2, uint32_t max)   /* the MIDI-in queue, taken */
{
    uint32_t n = 0;
    while (mi_r != mi_w && n < max) {
        uint32_t pkt = midi_in_q[mi_r % MQ];
        st[n] = (pkt >> 8) & 0xFFu;
        d1[n] = (pkt >> 16) & 0x7Fu;
        d2[n] = (pkt >> 24) & 0x7Fu;
        n++;
        mi_r++;
    }
    return n;
}

static int test_pad_modes(void)
{
    int bad = 0;
    smk_power_on();
    frame();
    bad += check("SMK pads: on HOME the steps (taps for the UI), not notes", smk_pad_steps == 1u);
    sel_set(3);                                 /* T4 DRUM KIT */
    go_page(GR_ROLL);
    frame();
    bad += check("SMK pads: the DRUM grid (keys_own): the white keys", grid_on() && smk_pad_steps == 0u);
    ui.home = 1;
    sel_set(0);
    fm1_in.buttons |= 1u << panel.btn[B_ARP];     /* a layer's button held: its keys */
    host_pressed |= 1u << panel.btn[B_ARP];     /* (its edge: armed; the ISR's mask the pass after) */
    frame();
    frame();
    bad += check("SMK pads: a layer's button held: the white keys", smk_pad_steps == 0u);
    fm1_in.buttons = 0;
    frames(200);
    bad += check("SMK pads: let go, HOME: the steps again", smk_pad_steps == 1u);
    return bad;
}

static int test_pad_taps(void)
{
    int bad = 0, ok;
    track_t *t;
    smk_power_on();
    sel_set(0);
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    last_note = 62;
    frame();
    pad_tap(2, 90);
    ok = step_on(&t->step[2]) && t->step[2].n == 1u && t->step[2].note[0] == 62u && t->step[2].vel == 90u;
    bad += check("SMK pad tap, melodic: an empty step set to the last note played, the pad's velocity", ok);
    pad_tap(5, 100);
    pad_tap(2, 100);
    bad += check("SMK pad tap again: that step cleared", !step_on(&t->step[2]) && step_on(&t->step[5]));
    bad += check("SMK pad taps: one undo for the run (PAD STEPS)", undo_n == 1u && undo.name == SMK_TAPS);
    pad_tap(15, 100);
    pad_tap(0, 0);                                /* (a release: nothing) */
    bad += check("SMK pad taps: pad 16 is step 16; a release does nothing", step_on(&t->step[15]) && !step_on(&t->step[0]));
    t->p[P_SLEN] = 8;
    pad_tap(9, 100);
    bad += check("SMK pad tap past LEN: nothing", !step_on(&t->step[9]));
    sel_set(3);                                 /* T4 DRUM KIT, HOME: the selected lane toggled */
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    ui.lane = 1;
    pad_tap(4, 100);
    ok = ((step_lanes(&t->step[4]) >> 1) & 1u) != 0u;
    pad_tap(4, 100);
    bad += check("SMK pad tap, DRUM: the selected lane's hit toggled", ok && !((step_lanes(&t->step[4]) >> 1) & 1u));
    return bad;
}

static uint32_t rgb_of(uint16_t c565, uint32_t q) { return smk_rgb(c565, q); }
static int test_pad_colours(void)
{
    int bad = 0;
    track_t *t;
    uint32_t col;
    smk_power_on();
    sel_set(0);
    t = TSEL;
    track_defaults_steps(t);
    t->p[P_SLEN] = 12;
    t->step[3].n = 1;
    t->step[3].note[0] = 60;
    t->step[3].time = ST_NOTE;
    t->step[4].time = ST_TIE;
    frame();
    col = rgb_of(T_TRK(0), SMK_PAD_FULL);
    bad += check("SMK pad colours: a step the track's colour, empty faint, a tie half, past LEN dark",
                 host_pad[3] == col && host_pad[0] == SMK_PAD_FAINT && host_pad[4] == rgb_of(T_TRK(0), SMK_PAD_FULL / 2u) &&
                 host_pad[12] == 0u);
    song.playing = 1;
    t->seq_idx = 3;
    frame();
    bad += check("SMK pad colours: the step playing half way to white", host_pad[3] == smk_mixw(col, 128u));
    song.playing = 0;
    go_page(GR_ROLL);
    ui.cursor = 5;
    frame();
    bad += check("SMK pad colours: on NOTES, stopped: the edited step white",
                 host_pad[5] == (SMK_PAD_WHITE << 16 | SMK_PAD_WHITE << 8 | SMK_PAD_WHITE));
    return bad;
}

static int test_faders(void)
{
    int bad = 0;
    smk_power_on();
    trk[1].p[P_LEVEL] = 100;
    fm1_in.fader[1] = 20;
    frame();                                      /* (the first reading: the fader's place, nothing set) */
    fm1_in.fader[1] = 40;
    frame();
    bad += check("SMK fader: below the value, not reached: nothing set", trk[1].p[P_LEVEL] == 100);
    fm1_in.fader[1] = 104;
    frame();
    bad += check("SMK fader: past the value: it takes over, said (T2 LEVEL ..)", trk[1].p[P_LEVEL] == 104 && ui.msg_t &&
                 ui.msg[0] == 'T' && ui.msg[1] == '2' && ui.msg[3] == 'L');
    fm1_in.fader[1] = 90;
    frame();
    bad += check("SMK fader: then it follows", trk[1].p[P_LEVEL] == 90);
    trk[1].p[P_LEVEL] = 30;                       /* (a knob set it) */
    fm1_in.fader[1] = 88;
    frame();
    bad += check("SMK fader: a value set elsewhere is reached again first", trk[1].p[P_LEVEL] == 30);
    press_id(SMK_BTN_FADER_BANK);
    bad += check("SMK FADER BANK: PAN, said, its LED lit", smk_fbank == 1u && msg_is("FADERS PAN") &&
                 host_led[SMK_BTN_FADER_BANK]);
    trk[1].p[P_PAN] = 0;
    fm1_in.fader[1] = 60;
    frame();
    fm1_in.fader[1] = 70;
    frame();
    bad += check("SMK fader, PAN: 0..127 over -64..63", trk[1].p[P_PAN] == 6);
    press_id(SMK_BTN_FADER_BANK);
    press_id(SMK_BTN_FADER_BANK);
    press_id(SMK_BTN_FADER_BANK);
    bad += check("SMK FADER BANK: round to LEVEL, its LED off", smk_fbank == 0u && !host_led[SMK_BTN_FADER_BANK]);
    sel_set(5);                                 /* T6: the window T3..T6 */
    fm1_in.fader[0] = 0;
    frame();
    trk[2].p[P_LEVEL] = 50;
    fm1_in.fader[0] = 60;
    frame();
    bad += check("SMK faders follow the window: T6 selected, F1 is T3", trk[2].p[P_LEVEL] == 60);
    return bad;
}

static int test_entries(void)
{
    int bad = 0;
    uint32_t i;
    smk_power_on();
    sel_set(0);
    for (i = 0; i < 3u; i++) {                    /* T1's entries A, B, C: patterns of their own (SAVE > PATTERNS) */
        track_t *t = TSEL;
        track_defaults_steps(t);
        t->step[i].n = 1;
        t->step[i].note[0] = (uint8_t)(60 + i);
        t->step[i].time = ST_NOTE;
        bank_put(0, i, 0);
    }
    (void)entry_go(0, 0);
    frame();
    frame();
    press_id(SMK_BTN_RIGHT);
    bad += check("SMK >: the next entry, at once when stopped", lists.cur[0] == 1u);
    press_id(SMK_BTN_RIGHT);
    press_id(SMK_BTN_LEFT);
    bad += check("SMK < after >: back one", lists.cur[0] == 1u);
    press_id(SMK_BTN_LEFT);
    press_id(SMK_BTN_LEFT);
    bad += check("SMK < on A: FIRST ENTRY, nowhere", lists.cur[0] == 0u && msg_is("FIRST ENTRY"));
    return bad;
}

static int test_outer_keys(void)
{
    int bad = 0;
    smk_power_on();
    sel_set(0);
    song.octave = 0;
    trk[0].p[P_TRANS] = 0;
    trk[0].p[P_QUANT] = QN_OFF;
    fm1_in.key_vel[0] = 90u;                            /* C3, struck at 90 (the scan: key_vel, then the queue) */
    smk_extq[smk_extq_w] = (uint16_t)(0u << 8 | 90u);
    smk_extq_w = (uint8_t)((smk_extq_w + 1u) % SMK_EXTQ);
    host_ut_n = 0;
    usb.config = 1;
    keyboard_block();
    bad += check("SMK outer key C3: note 48 on T1 at its velocity, out of the TRS jack too",
                 last_note == 48u && host_ut_n >= 3u && host_ut[0] == 0x90u && host_ut[1] == 48u && host_ut[2] == 90u);
    smk_extq[smk_extq_w] = (uint16_t)(0u << 8);         /* let go */
    smk_extq_w = (uint8_t)((smk_extq_w + 1u) % SMK_EXTQ);
    host_ut_n = 0;
    keyboard_block();
    bad += check("SMK outer key let go: its note off", host_ut_n >= 3u && host_ut[0] == 0x80u && host_ut[1] == 48u);
    smk_kv_mode = SMK_KV_F100;
    smk_extq[smk_extq_w] = (uint16_t)(36u << 8 | 30u);  /* C6, soft, with KEY VELOCITY FIXED 100 */
    smk_extq_w = (uint8_t)((smk_extq_w + 1u) % SMK_EXTQ);
    host_ut_n = 0;
    keyboard_block();
    bad += check("SMK outer key C6: note 84, KEY VELOCITY FIXED 100", host_ut_n >= 3u && host_ut[1] == 84u && host_ut[2] == 100u);
    smk_kv_mode = SMK_KV_DYN;
    bad += check("SMK KEY VELOCITY curves: SOFT louder, HARD softer, both ends kept",
                 (smk_kv_mode = SMK_KV_SOFT, smk_vel(64) > 64u && smk_vel(127) == 127u) &&
                 (smk_kv_mode = SMK_KV_HARD, smk_vel(64) < 64u && smk_vel(1) == 1u) && (smk_kv_mode = SMK_KV_DYN, 1));
    return bad;
}

static int test_menu(void)
{
    int bad = 0;
    uint32_t i, kv = 0, leds = 0;
    smk_power_on();
    for (i = 0; i < MI_COUNT; i++) {
        kv += str_eq(MI_NAME[i], "KEY VELOCITY") && MI_TAB[i] == MTAB_CONTROL;
        leds += str_eq(MI_NAME[i], "LEDS");
    }
    bad += check("SMK MENU: KEY VELOCITY in CONTROL, no LEDS row", kv == 1u && leds == 0u && MI_LEDS > MI_COUNT);
    bad += check("SMK MENU > CLOCK: INT and USB only (no TRS MIDI IN)", menu_n(MI_CLOCK) == 2u && GP[G_CLOCK].max == 1);
    menu_put(MI_KVEL, SMK_KV_HARD);
    bad += check("SMK MENU > KEY VELOCITY HARD: the settings byte and the mode", SMK_KV_BYTE == SMK_KV_HARD &&
                 smk_kv_mode == SMK_KV_HARD && str_eq(menu_vname(MI_KVEL, menu_get(MI_KVEL)), "HARD"));
    {
        sks_t p;
        settings_export(&p);
        p.s.smk_kv = 9;
        settings_import(&p, (int)sizeof p);
        bad += check("SMK settings: an unknown KEY VELOCITY reads as DYNAMIC", favorites.factory[15][12] == 0u);
    }
    bad += check("SMK USR2 / USR3: SAMPLE's SET and GRAIN's SOURCE end at USR1",
                 ENG_SAMPLE.edit[0].max == (int32_t)SMP_NSETS && str_eq(SMP_ALL_NAMES[SMP_NSETS], "USR1"));
    return bad;
}

static int test_wheels(void)
{
    int bad = 0;
    uint32_t st[16], d1[16], d2[16], n, i, bend = 0, mod = 0, ped = 0;
    smk_power_on();
    sel_set(1);
    fm1_in.wheel[0] = 535;
    fm1_in.wheel[1] = 629;
    fm1_in.pedal = 985;
    frame();
    frame();
    mi_r = mi_w;
    fm1_in.wheel[0] = 640;
    fm1_in.wheel[1] = 474;
    fm1_in.pedal = 40;
    frame();
    n = midi_msgs(st, d1, d2, 16);
    for (i = 0; i < n; i++) {
        bend |= st[i] == (0xE0u | trk_midi_ch(1)) && (d1[i] | d2[i] << 7) > 12000u;
        mod |= st[i] == (0xB0u | trk_midi_ch(1)) && d1[i] == 1u && d2[i] > 120u;
        ped |= st[i] == (0xB0u | trk_midi_ch(1)) && d1[i] == 64u && d2[i] == 127u;
    }
    bad += check("SMK wheels, pedal: a bend, CC1 and CC64 on the selected track's channel, as MIDI in", bend && mod && ped);
    sel_set(2);
    frame();
    n = midi_msgs(st, d1, d2, 16);
    for (i = 0, bend = mod = ped = 0; i < n; i++) {
        bend |= st[i] == (0xE0u | trk_midi_ch(1)) && d1[i] == 0u && d2[i] == 64u;
        mod |= st[i] == (0xB0u | trk_midi_ch(1)) && d1[i] == 1u && d2[i] == 0u;
        ped |= st[i] == (0xB0u | trk_midi_ch(1)) && d1[i] == 64u && d2[i] == 0u;
    }
    bad += check("SMK wheels: another track selected: the one before back to rest", bend && mod && ped);
    return bad;
}

/* the pedal holding the keys (seq.c kb_up): its state from a UI frame (smk_wheels), the keys' from the audio blocks */
static void pedal_to(int down)                    /* the pedal pressed or let go (985: its rest), a frame, a block */
{
    fm1_in.pedal = down ? 40u : 985u;
    frame();
    events_block(CTL);
}
static void key_to(uint32_t k, int down)          /* key k (of the FM-1's 27) down or up, a block */
{
    fm1_in.notes = down ? fm1_in.notes | 1u << k : fm1_in.notes & ~(1u << k);
    events_block(CTL);
}
static void outer_to(uint32_t bed, uint32_t vel)  /* an outer key (keybed 0..4, 32..36) at vel, 0 let go, a block */
{
    smk_extq[smk_extq_w] = (uint16_t)(bed << 8 | vel);
    smk_extq_w = (uint8_t)((smk_extq_w + 1u) % SMK_EXTQ);
    events_block(CTL);
}
static int sounding(const track_t *t, uint32_t note)   /* a voice of t holds note (gated: not in its release) */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t note_offs(uint32_t note)          /* note-offs of note out of the TRS jack since host_ut_n was 0 */
{
    uint32_t i, n = 0;
    for (i = 0; i + 1u < host_ut_n && i + 1u < HOST_UT; i++)
        n += (host_ut[i] & 0xF0u) == 0x80u && host_ut[i + 1u] == note;
    return n;
}
static int test_pedal(void)
{
    int bad = 0, ok;
    uint32_t i, drum;
    track_t *t = &trk[0];
    smk_power_on();
    song.sel = 0;                                 /* T1, C4 (key 7) note 60, C3 (outer key 0) note 48 */
    song.octave = 0;
    for (i = 0; i < 2u; i++) {
        trk[i].p[P_TRANS] = 0;
        trk[i].p[P_QUANT] = QN_OFF;
        trk[i].p[P_VOICE] = V_POLY;               /* (T1's ACID is MONO) */
    }
    fm1_in.wheel[0] = 535;                        /* the wheels at rest */
    fm1_in.wheel[1] = 629;
    fm1_in.key_vel[7u + SMK_KEY_F3] = fm1_in.key_vel[0] = 100u;
    pedal_to(0);
    pedal_to(1);
    key_to(7, 1);
    outer_to(0, 100);
    host_ut_n = 0;
    key_to(7, 0);
    outer_to(0, 0);
    for (i = 0; i < 20u; i++)
        events_block(CTL);
    bad += check("SMK pedal down: a key and an outer key let go keep sounding (no note-off out)",
                 smk_pedal && sounding(t, 60) && sounding(t, 48) && !note_offs(60) && !note_offs(48));
    key_to(7, 1);
    ok = note_offs(60) == 1u && sounding(t, 60);
    key_to(7, 0);
    bad += check("SMK pedal: a held key struck again: its held note ends first, then it plays", ok && sounding(t, 60));
    host_ut_n = 0;
    pedal_to(0);
    bad += check("SMK pedal up: the notes it held end (note-offs out), the keys keep nothing",
                 !sounding(t, 60) && !sounding(t, 48) && note_offs(60) == 1u && note_offs(48) == 1u && !kb_chn[7] &&
                 !kb_chn[27]);

    pedal_to(1);                                  /* T1's C4 held, then T2 selected with the pedal still down */
    key_to(7, 1);
    key_to(7, 0);
    host_ut_n = 0;
    song.sel = 1;
    frame();
    events_block(CTL);
    ok = !sounding(t, 60) && note_offs(60) == 1u && !kb_chn[7] && !kb_ped[7];
    key_to(7, 1);
    key_to(7, 0);
    ok = ok && sounding(&trk[1], 60);
    fm1_in.pedal = 985u;                          /* let go in the frame T1 is selected again */
    song.sel = 0;
    frame();
    events_block(CTL);
    bad += check("SMK pedal, another track selected while it holds: the keys before end cleanly, it holds the new one's",
                 ok && !sounding(&trk[1], 60) && !kb_chn[7] && !smk_pedal);

    pedal_to(1);                                  /* a preset loaded while it holds: its keys let go, nothing kept */
    key_to(7, 1);
    key_to(7, 0);
    host_ut_n = 0;
    apply_preset(2);
    events_block(CTL);
    ok = !sounding(t, 60) && note_offs(60) == 1u && !kb_chn[7] && !kb_ped[7];
    pedal_to(0);
    bad += check("SMK pedal, a preset loaded while it holds (panic_req): the keys end, nothing left for its release",
                 ok && note_offs(60) == 1u);

    t->p[P_VOICE] = V_MONO;                       /* MONO: C4 then F3, both let go held; F3 sounds */
    t->p[P_PRIO] = 0;
    t->p[P_GLIDE] = 0;
    fm1_in.key_vel[0u + SMK_KEY_F3] = 100u;
    pedal_to(1);
    key_to(7, 1);
    key_to(0, 1);
    key_to(7, 0);
    key_to(0, 0);
    ok = sounding(t, 53);
    pedal_to(0);
    bad += check("SMK pedal up on a MONO track: its sounding note ends as it is (no fall back to another one held)",
                 ok && !t->v[0].gate && t->v[0].note == 53u);
    t->p[P_VOICE] = V_POLY;

    song.sel = 3;                                 /* T4 DRUM KIT: a hit's key ends as it is let go */
    frame();
    pedal_to(1);
    host_ut_n = 0;
    key_to(7, 1);
    drum = kb_note[7];
    key_to(7, 0);
    bad += check("SMK pedal on a DRUM track: ignored (the key's note-off at once, nothing held)",
                 smk_pedal && drum != KB_SILENT && note_offs(drum) == 1u && !kb_chn[7] && !kb_ped[7]);
    pedal_to(0);
    return bad;
}

static int test_shift(void)
{
    int bad = 0, ok, i;
    smk_power_on();
    fm1_in.buttons |= 1u << SMK_BTN_KNOB_BANK;
    frames(400);
    bad += check("SMK KNOB BANK held: SHIFT's map, its LED lit", ui.shift && host_led[SMK_BTN_KNOB_BANK]);
    {
        smap_t m[12];
        shift_map(m);
        for (ok = 0, i = 0; i < 12; i++)
            ok += str_eq(m[i].key, "K4") + str_eq(m[i].key, "K3") + str_eq(m[i].key, "+ BT") + str_eq(m[i].key, "PLAY");
        bad += check("SMK SHIFT's map: the SMK's printed names (K4, K3, + BT; PLAY as PLAY: the SMK prints a play sign)", ok == 4);
    }
    fm1_in.buttons |= 1u << panel.btn[B_SAVE];
    host_pressed |= 1u << panel.btn[B_SAVE];
    frame();
    fm1_in.buttons &= ~(1u << panel.btn[B_SAVE]);
    frame();
    bad += check("SMK KNOB BANK + BT (SAVE): TAKE, the map gone, no SAVE page", !ui.shift && ui.home);
    fm1_in.buttons &= ~(1u << SMK_BTN_KNOB_BANK);
    frames(100);
    bad += check("SMK KNOB BANK let go: no MENU", !ui.menu && !ui.shift);
    return bad;
}

static int test_master(void)
{
    int bad = 0;
    smk_power_on();
    frame();
    smk_master = 512;
    frame();
    bad += check("SMK MASTER (K1) turned: said", msg_is("MASTER 50%"));
    frames(1700);
    bad += check("SMK MASTER at rest: kept in the settings", SMK_MASTER_BYTE == 512u / 4u + 1u);
    smk_master = 724;
    smk_ui_boot();
    bad += check("SMK power-on: MASTER where it was left", smk_master == 512);
    return bad;
}

static int test_clock_out(void)
{
    int bad = 0;
    uint32_t i, pulses = 0, starts = 0, stops = 0;
    smk_power_on();
    song.g[G_CLOCK] = 0;
    song.g[G_BPM] = 120;
    host_ut_n = 0;
    seq_start();
    song.playing = 1;
    for (i = 0; i < (uint32_t)FS / 64u; i++)      /* one second in 64-sample blocks: 2 beats */
        smk_clk_block(64u);
    seq_stop();
    for (i = 0; i < host_ut_n && i < HOST_UT; i++) {
        pulses += host_ut[i] == 0xF8u;
        starts += host_ut[i] == 0xFAu;
        stops += host_ut[i] == 0xFCu;
    }
    bad += check("SMK TRS MIDI OUT clock: Start, 24 pulses a beat (48 in 1 s at 120), Stop",
                 starts == 1u && stops == 1u && pulses >= 48u && pulses <= 49u);
    song.g[G_CLOCK] = 1;
    host_ut_n = 0;
    seq_start();
    song.playing = 1;
    smk_clk_block(4096u);
    seq_stop();
    bad += check("SMK TRS MIDI OUT clock: none with an external clock", host_ut_n == 0u);
    return bad;
}

int main(void)
{
    int bad = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    bad += test_pad_modes();
    bad += test_pad_taps();
    bad += test_pad_colours();
    bad += test_faders();
    bad += test_entries();
    bad += test_outer_keys();
    bad += test_menu();
    bad += test_wheels();
    bad += test_pedal();
    bad += test_shift();
    bad += test_master();
    bad += test_clock_out();
    printf("%s\n", bad ? "SMK UI TEST FAILED" : "smk ui test passed");
    return bad != 0;
}
