/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA: one compilation unit (the HAL is header-only). Each file uses what the files above it
 * define, so the order matters. */

#include "fm1_cc.h"              /* BOARD_SMK37, SMK_MODEL: the board this build is for */

/* ---------------------------------------------------- build options --- */
/* build.py: FELUCCA_<NAME>=0 / 1 in the environment overrides these. Elsewhere: FELUCCA_SLICE, FELUCCA_FM4
 * (core.h: the SLICE engine, the DIGITAL engine), FELUCCA_ICONS (icons.c, the parameter icons; on by default),
 * FELUCCA_OTA_RAMONLY (ota.c). */
#ifndef FELUCCA_FLASH
#define FELUCCA_FLASH 1          /* flash driver + storage.c: settings, projects, user presets */
#endif
#ifndef FELUCCA_OTA
#define FELUCCA_OTA 1            /* M-UPGRADE update entry and the editor SysEx; needs FELUCCA_FLASH */
#endif
#ifndef FELUCCA_OTA_DRYRUN
#define FELUCCA_OTA_DRYRUN 0     /* 1 = stage, ask "success", then undo: no record, no reset */
#endif
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1            /* USB CDC-ACM serial console */
#endif
#ifndef FELUCCA_UAC
#define FELUCCA_UAC 1            /* USB audio input: the master output, 16-bit stereo 44.1 kHz (usb.c) */
#endif
#ifndef FELUCCA_UAC_TONE
#define FELUCCA_UAC_TONE 0       /* bench: the USB input sends test triangles instead of the music */
#endif
#ifndef FELUCCA_UART
#if BOARD_SMK37
#define FELUCCA_UART 0           /* SMK-37 Pro: the TRS jack is MIDI OUT (UART1 TX, PC7), no MIDI IN; PH8 is a key-matrix
                                  * row there and must never become UART RX */
#else
#define FELUCCA_UART 1           /* TRS MIDI IN on UART1 / PH8 */
#endif
#endif
#if BOARD_SMK37 && FELUCCA_UART
#error "BOARD_SMK37: midi_uart.c is the FM-1 MIDI IN driver (RX on PH8, a key-matrix row on the SMK)"
#endif
#ifndef FELUCCA_ID
#if BOARD_SMK37
#define FELUCCA_ID SMK_MODEL "_700"   /* package identity (build.py: the .fwsc marker string) */
#else
#define FELUCCA_ID "FM-1_700"    /* package identity (build.py: the .fwsc marker string) */
#endif
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "v2.0.3"     /* Skiff's; shown in the menu, the console and the editor; build.py --release X.Y */
#endif
#if FELUCCA_OTA && !FELUCCA_FLASH
#error "FELUCCA_OTA needs FELUCCA_FLASH"
#endif
#ifndef FELUCCA_CPU2
#define FELUCCA_CPU2 1           /* (Skiff 1.3, as Felucca 1.6.1) 1: the second core is built in and renders some tracks when
                                  * MENU > SYSTEM > CPU is 2 CORES (BETA; 1 CORE by default: as 1.2; hal/fm1_cpu1.h, fx.c
                                  * mix_block). 0 (FELUCCA_CPU2=0 build.py): none of it (no core 1 stack, waiting loop or
                                  * buffers). (The same default: core.h) */
#endif
#define FM1_CPU1_USED FELUCCA_CPU2   /* hal/fm1_sys.h: the second core is built in; every reset holds it first (its
                                      * waiting loop is in RAM the ROM and the SPL use at boot) */
/* (Skiff) audio code run from RAM (.ram_hot, app.ld), out of the XIP cache: the FX layer's (perform.c). From XIP it
 * shared the cache with the engines: on the unit, REPEAT 1/32 held cost +6 % CPU alone but +15 % under six busy
 * tracks (late halves, voices shed); 2 % by instruction count (the emulator). Never runs with the flash busy (the
 * audio ISR is off then), so it may call XIP code. Only here, the unit's build: core.h leaves it empty elsewhere.
 * RAM and XIP are over 4 MB apart, beyond a direct call's reach (fm1_flash.h FL_FAR): a call between them goes
 * through a pointer the compiler cannot fold back, AUDIO_FAR (the linker refuses a direct one). flatten: this clang
 * inlines nothing into a function of another section by itself, and the helpers (dsp.c clamp, perform.c pf_mix ..)
 * left as calls would be calls to XIP; flatten inlines them there only, the rest of the image as it was */
#define AUDIO_RAM __attribute__((section(".ram_hot"), flatten))
static inline __attribute__((always_inline)) void *audio_far(void *p) { void *volatile q = p; return q; }
#define AUDIO_FAR(fn) ((__typeof__(&fn))audio_far((void *)&fn))

/* ------------------------------------------------------------- HAL --- */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#if FELUCCA_CPU2
#include "fm1_cpu1.h"
#define C1_ON() fm1_c1_on                    /* fx.c mix_block: a second core is answering */
#define C1_RUN(fn, arg) fm1_cpu1_run(fn, arg)
#define C1_WAIT() fm1_cpu1_wait()
#endif
#if BOARD_SMK37
#include "smk_input.h"           /* SMK-37 Pro: keys, buttons, knobs, button LEDs, pads, faders, wheels, battery */
#include "smk_compat.h"          /* (the FM-1's LED glow and power-on sweep: none on the SMK) */
#include "smk_uart_tx.h"         /* the TRS jack: MIDI OUT (UART1 TX, PC7) */
#else
#include "fm1_input.h"
#endif
#include "fm1_timer.h"
#include "fm1_audio.h"
#if !BOARD_SMK37
#include "fm1_adc.h"             /* (the SMK's battery and MASTER: smk_input.h) */
#endif
#include "fm1_lcd_hw.h"
#include "fm1_clk.h"             /* (Skiff) the core clock; MENU > CPU CLOCK 320 MHZ's switch (main.c) */
#define CLK_MHZ() fm1_clk_mhz()  /* the core clock now (ui.c: 360 elsewhere) */
#include "felucca_tables.h"      /* generated by build.py (tools/gen_tables.py) */

/* ----------------------------------------------------- base, display --- */
#include "libc.c"
#include "lcd.c"
#include "gfx.c"

/* ----------------------------------------------------------- sound --- */
#include "core.h"
#include "engines.c"             /* dsp.c, the eng_*.c files, the factory patterns */
#include "params.c"
#include "mod.c"               /* the modulation matrix, used by voice.c and fx.c */
#include "voice.c"
#include "slicer.c"              /* per-track SLICER insert, used by fx.c */
#include "fx.c"

/* ------------------------------------------------- MIDI, sequencer --- */
#include "usb.c"
#if FELUCCA_UART
#include "midi_uart.c"
#endif
#include "seq.c"
#include "audio.c"

/* -------------------------------------------------------------- UI --- */
#include "panel.c"
#include "ui.c"
#include "menu_items.c"          /* MENU's rows and settings (ui_menu.c, editor_menu.c) */
#include "icons.c"               /* parameter icons (FELUCCA_ICONS), used by ui_draw.c */
#include "ui_graph.c"
#include "ui_draw.c"
#include "ui_menu.c"
#include "ui_input.c"
#include "ui_layer.c"              /* the quick layers (FX GLO SCL EDIT REC held; SEQ TOOLS: ui_tools.c) */

/* --------------------------------------------- storage, update, editor --- */
#if FELUCCA_FLASH
#include "fm1_flash.h"
#include "storage_hw.c"
#include "storage.c"
#endif
#include "upreset.c"             /* user presets (RAM mirror; flash with FELUCCA_FLASH) */
#include "project.c"
#include "bank.c"                /* Skiff: the pattern bank (SAVE > PATTERNS) */
#if FELUCCA_OTA
#include "ota.c"
#include "ota_hw.c"
#include "editor.c"              /* web editor SysEx (needs the OTA SysEx plumbing) */
#endif
#if FELUCCA_CDC
#include "console.c"
#endif
#if BOARD_SMK37
#include "smk_ui.c"              /* the SMK-37's own controls: pads, faders, wheels, pedal, < >, KEY VELOCITY */
#endif
#include "master.c"              /* the MASTER pot (#137: read before the audio starts) */
#include "main.c"
