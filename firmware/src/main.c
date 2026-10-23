/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA boot and main loop. Boot order: WDT first, boot-loop guard, fatal vectors,
 * guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[], _ramx_start[], _ramx_end[];


/* TIMER5 outranks ALNK0, so the scan keeps its 100 us pace while a half buffer renders: before,
 * the ticks stopped for the whole render (0.7 ms idle, several ms loaded), the column lit when it
 * began stayed lit that long (a ~16 Hz flicker over all LEDs, beating with the scan) and the
 * encoders lost frames. Nested in ALNK0 it only scans (GPIO + fm1_in, nothing the audio ISR touches)
 * and counts ms; USB and UART polls wait for the first tick after the render, as they always did,
 * and the time spent nested is handed to the audio ISR so its load figures stay render-only.
 * The USB audio stream cannot wait for the render (one packet per 1 ms frame, a render takes up to
 * ~5 ms): uac_service also runs nested. It touches only EP4 (INDEX is set on every access) and the
 * consumer side of the audio ring, and usb_poll never runs nested, so the two never interleave. */
#if FELUCCA_DISPLAY_MIRROR
#include "../../fm1-display-mirror/firmware/skiff/ui_adapter.h"
#endif

void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = FELUCCA_DISPLAY_BULK || sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
#if BOARD_SMK37
    smk_uart_tx_tick();                         /* (the TRS jack's MIDI OUT: a byte once the last has gone) */
#endif
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
#if FELUCCA_UAC
        if (usb_due)
            uac_service();
#endif
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    scr_wake_now();                                   /* (MENU > SCREEN OFF: the panel on first) */
    lcd_fill(0, 0, 240, 240, UI_CRASH_BG);            /* fixed, outside the palettes */
    draw_text_line(0, 8, 240, &AF_M, "SKIFF CRASH", UI_CRASH_INK, UI_CRASH_BG, 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->emu);
    draw_text_line(10, 84, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->dbg);
    draw_text_line(10, 102, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->rets);
    draw_text_line(10, 120, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: the parts with their default sounds (TRK_DEF); the sequencers empty */
static void felucca_init(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
    }
    lists_clear();                            /* (Skiff phase 5) no lists, no patterns on the tracks */
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;                   /* (the pot's level replaces it before the audio starts: master_boot) */
    ui.home = 1;
    ui.force = 1;
}

/* The power-on LED sweep (1.1, hal/fm1_led_anim.h: a soft light over the keys left to right, then the buttons swell
 * and settle on the glow; ~0.70 s), started before the scan and run by it: nothing waits for it, the splash's 430 ms
 * and the first ~0.27 s of the UI go by under it (the UI's LEDs from its end, or at once on a key or a button down).
 * Every LEDS setting: it is no idle glow; with DIM HI / DIM LO it ends on that glow (and is drawn over that glow
 * level), with OFF / INV dark. Not with MENU > ANIM OFF (no motion: the LEDs as the UI has them at once), not after a crash (a crash record newer than the last boot saw: the crash screen's
 * reset), a hang (a WDT reset) or a failed boot (bootguard: the last one died within its first 30 s): straight to the
 * UI. The UBOOT paths never get here. */
static uint32_t crash_seen __attribute__((section(".noinit")));   /* fm1_crash.count at the last boot */
static uint8_t boot_clean;                    /* no crash, hang (WDT) or failed boot before this one (boot_leds):
                                               * the sweep runs and the autosave is restored (project.c autosave_boot) */
static void boot_leds(void)
{
    uint32_t crashed = fm1_crash.magic == FM1_CRASH_MAGIC && fm1_crash.count != crash_seen
#if FELUCCA_CPU2
                       && fm1_crash.core != 1u    /* ((Skiff 1.3) the second core's own fault left core 0 playing) */
#endif
                       ;
    crash_seen = fm1_crash.count;
    boot_clean = !crashed && !bootguard.failed && !(fm1_boot.p3_rst & 4u);
    fm1_led_dim_level(settings_leds == LEDS_DIM_LO);
    if (boot_clean && !(ui_prefs & PREF_ANIM_OFF))
        fm1_led_anim_start(settings_leds == LEDS_DIM || settings_leds == LEDS_DIM_LO);
}

static void fm1_main(void)
{
    uint32_t ms;
#if FELUCCA_CPU2
    uint8_t c1_told = 0;                                /* (Skiff 1.3) CORE 2 FAILED said (and the setting cleared) */
#endif
    persist_boot();
    bank_boot();                                        /* (Skiff) the pattern bank (bank.c) */
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    if (settings_init())                                /* a new unit: the SKIFF palette, and BPM LOCK on (DESIGN.md */
        ui_prefs |= PREF_BPM_LOCK;                      /* rule 3: the tempo with GLO held, #58) */
    usb_serial_apply();                                 /* (#67: the saved USB SERIAL before usb_start) */
    lcd_init();
    draw_splash();                                      /* ui_draw.c (1.1.5) */
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    if (CLK320_SET && !bootguard.failed && clk320.state != CLK_TRIPPED) {
        /* (Skiff) MENU > CPU CLOCK 320 MHZ: from the boot stage's 360 to JieLi's rated 320 (hal/fm1_clk.h, curist's
         * switch), the screen up, on one core, before the scan, the second core, the audio and USB. Not after a failed
         * boot, nor again before a power-off after a reset during it (fm1_cstart: TRIPPED); a soft reset that kept 320
         * is not switched again (the check: EALREADY). Any other register not as expected: refused, said (below) */
        uint32_t t[2];
        lcd_sync();                                     /* (no LCD DMA through it) */
        clk320.state = CLK_ARMED;
        fm1_wdt_feed();
        clk320.why = (uint8_t)fm1_clk320_go(t);
        fm1_wdt_feed();
        clk320.state = !clk320.why ? CLK_ON : clk320.why == FM1_CLK320_EALREADY ? CLK_KEPT : CLK_REFUSED;
        if (t[1])                                       /* (the loop timed before and after: the clock it runs at) */
            clk320.loop_mhz = (uint16_t)((360u * t[0] + t[1] / 2u) / t[1]);
    }
    fm1_input_init();
    fm1_adc_init();
#if BOARD_SMK37 && FELUCCA_FLASH
    if (flash_ok)                                   /* the unit's key / pad velocity curves (USRTRIM, read only) */
        smk_trim_load(fm1_xip_ptr(SMK_TRIM_FLASH));
#endif
    panel_init();
    felucca_init();
#if BOARD_SMK37
    smk_ui_boot();                            /* (MASTER where it was left, KEY VELOCITY: smk_ui.c) */
#endif
    master_boot();
    kb_boot_hold = 1;                         /* #137: no notes from the keys before the UI (seq.c keyboard_block) */
#if FELUCCA_CPU2
    if (!bootguard.failed && CPU2_SET) {
        /* (Skiff 1.3, as Felucca 1.6.1) the second core (hal/fm1_cpu1.h; fx.c mix_block hands it tracks), at power-on,
         * before the audio and the timers (started later it never reaches its entry) and before fm1_guard_lock_top
         * (its entry word is at the top of RAM). It starts in the chip's ROM: the PC limits open meanwhile. MENU > CPU
         * 1 CORE (the default), a crash in the last boot's first 30 s, or no answer: never started, one core */
        fm1_guard_pc_open();
        fm1_cpu1_start();
        fm1_guard_enable(FM1_GUARD_PC);
    }
#endif
    audio_init();
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
#if BOARD_SMK37
    smk_uart_tx_init();                       /* the TRS jack: MIDI OUT */
#endif
    boot_leds();
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
#ifndef FM1_OCT_BOTH
#define FM1_OCT_BOTH 3u                       /* (host builds: the FM-1's OCT- and OCT+, ids 0 and 1) */
#endif
    if ((fm1_in.buttons & FM1_OCT_BOTH) == FM1_OCT_BOTH) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    if (ui_set_rev < 1u) {                    /* (Skiff U1) SELECT turns pages on every unit: a unit from Felucca */
        ui_prefs |= PREF_BPM_LOCK;            /* had BPM LOCK OFF (SELECT the tempo). Once: the MENU can turn it */
        ui_set_rev = 1;                       /* off again */
        settings_save();
    }
    proj_migrate(boot_clean);                 /* (Skiff phase 5) projects of before: SKF2, their tracks patterns */
    unit_g_adopt = ui_set_rev < 2u;           /* (Skiff U3, R8) the first boot: the session's CLOCK MIDI IN TUNE .. */
    autosave_boot(boot_clean);                /* (1.2) the last session's music, under the splash */
    unit_g_boot();                            /* .. become the unit's; later the unit's are put back over it (ui.c) */
    for (ms = 0; ms < 400u; ms += 10u) {      /* the splash; the pot followed (MIDI IN plays under it) */
        fm1_delay_ms(10);
        master_poll();
    }
    kb_boot_hold = 0;
    lcd_fill(0, 0, 240, 240, T_BG);
    scrn.idle = fm1_ms;                        /* (MENU > SCREEN OFF: from here) */
    if (clk320.state == CLK_REFUSED)           /* (Skiff) MENU > CPU CLOCK 320 MHZ not switched, then why */
        ui_way("NO 320 MHZ", CLK_WHY[clk320.why % NELEM(CLK_WHY)]);
    else if (clk320.state == CLK_TRIPPED && CLK320_SET)
        ui_way("NO 320 MHZ", "RESET IN SWITCH");

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
#if FELUCCA_CPU2
        if (*(volatile uint8_t *)&fm1_c1_gave_up && !c1_told) {   /* ((Skiff 1.3) the ISR's: core 1 misread or
                                                         * stopped answering on this FM-1: one core from now on, the
                                                         * setting back to 1 CORE (MENU > CPU 2 CORES tries again at the
                                                         * next power-on); said once */
            c1_told = 1;
            if (CPU2_SET) {
                CPU2_BYTE &= (uint8_t)~1u;
                settings_save();
            }
            ui_message("CORE 2 FAILED: 1 CORE");
        }
#endif
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        master_poll();
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s over the whole screen (the menu and the dialogs too: ui_draw), letting
             * go cancels it */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (ui.uboot) {
                    ui.uboot = 0;
                    ui.force = 1;                       /* what the countdown covered */
                    ui_message("UPDATE CANCELLED");
                }
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != ui.uboot) {
                    ui.uboot = (uint8_t)left;
                    ui.force = 1;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                scr_wake_now();
                lcd_fill(0, 0, 240, 240, T_BG);
                draw_text_box(0, 110, 240, &AF_M, "UPDATE MODE", T_THEME, 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            scr_wake_now();                             /* (its progress on the screen) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            scr_wake_now();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UPDATE MODE (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = 1;
        ui_input();
#if BOARD_SMK37
        smk_ui_frame();                               /* the SMK-37's own controls (smk_ui.c) */
#endif
        settings_poll();                              /* queued settings save: only while stopped */
        autosave_poll();                              /* (1.2) the music, stopped and idle: project.c */
#if FELUCCA_DISPLAY_BULK
        /* Give the completed journal a short opportunity to drain before a
         * new LCD DMA wait blocks the main-loop ACK. Inputs/editor stay live;
         * a stalled host cannot defer the local UI beyond this deadline. */
        if (dm_ui_wait()) continue;
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.stage = 2;
        ui_leds();
#if BOARD_SMK37
        padled_flush();                               /* the pads' colours, between LCD transfers (lcd.c) */
#endif
#if FELUCCA_DISPLAY_MIRROR
        dm_ui_begin();
#endif
        ui_draw();
#if FELUCCA_DISPLAY_MIRROR
        dm_end();
#if FELUCCA_DISPLAY_BULK
        cdc_task();
#endif
#endif
        felucca_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#if FELUCCA_DISPLAY_BULK
            cdc_task();
#endif
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt;
#if FELUCCA_CPU2
    fm1_cpu1_halt();                           /* ((Skiff 1.3, after Jangada, X0X) a second core still running from
                                                * before this reset (a watchdog, a fault, another firmware) would read
                                                * the RAM cleared below) */
#endif
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if (bootguard.magic != BOOTGUARD_MAGIC) {
        bootguard.magic = BOOTGUARD_MAGIC;
        bootguard.failed = 0;
        bootguard.pending = 0;
    }
    if (((p3 & 1u) && !(p3 & 0x44u)) || clk320.magic != CLK320_MAGIC)   /* ((Skiff) a power-on, no watchdog or soft
                                                                         * reset: the boot stage's 360 MHz again) */
        clk320.state = CLK_NONE;
    clk320.magic = CLK320_MAGIC;
    if (clk320.state == CLK_ARMED)              /* a reset during the 320 MHz switch (fm1_main): not a failed boot, and
                                                 * no switch till power-off (curist's probe does the same) */
        bootguard.pending = 0;
    clk320.state = clk320.state == CLK_ARMED || clk320.state == CLK_TRIPPED ? CLK_TRIPPED : CLK_NONE;
    clk320.why = 0;
    clk320.loop_mhz = 0;
    if (bootguard.pending)
        bootguard.failed++;
    bootguard.pending = 1;
    if (bootguard.failed >= 2u) {
        bootguard.failed = 0;
        bootguard.pending = 0;
        fm1_enter_uboot();
    }
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (d = _ramx_start; d < _ramx_end; d++)    /* (Skiff: the data after the RAM code, app.ld) */
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    fm1_main();
    for (;;)
        ;
}
