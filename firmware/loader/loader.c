/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca update loader (device side). Runs from RAM, started by the SPL from
 * an update record, with interrupts off: USB is polled from the main loop.
 * It is a USB-MIDI device "Felucca Update" (1209:0002) that answers the
 * M-UPGRADE update protocol as "ota-FM-1_900" (hosts look for "ota-"), and
 * writes the app area only (ldr_core.c). The UBOOT soft key works here too.
 * Single compilation unit. */
#include <stdint.h>
#define FELUCCA_LOADER 1
#define FELUCCA_CDC 0
#define FELUCCA_OTA 1
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_USB_PID 0x0002
#ifndef FELUCCA_ID                     /* build.py passes "ota-" + its PRODUCT */
#define FELUCCA_ID "ota-FM-1_700"
#endif
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#include "fm1_time.h"
#include "fm1_sys.h"
/* the loader may write the app area and erase update records; never the head, nor Felucca's store
 * [0x97000, 0xE0000) (user data, never a record: ldr_core.c) */
#include "fm1_cc.h"                             /* BOARD_SMK37 */
#if BOARD_SMK37
/* SMK-37 Pro: app area 0x4000..0x9CFFF, records in the VM 0x9D000..0xBFFFF only: never BTIF / USRTRIM
 * 0xC0000..0xC1FFF, USR 0xF4000.., key_mac 0xFF000 (FL_LDR_RANGE_OK, asserted in fm1_flash_map.h; the RAM driver
 * refuses every protected range on its own as well, FL_GUARD) */
#define FL_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)
#else
#define FL_RANGE_OK(off, n) (FL_IN(off, n, 0x4000u, 0x97000u) || FL_IN(off, n, 0xE0000u, 0xFC000u))
#endif
#include "fm1_flash.h"
#define fm1_irq_off() ((void)0)          /* (lcd.c's, around port A: interrupts are off for the whole loader) */
#define fm1_irq_on() ((void)0)
#include "fm1_lcd_hw.h"
#include "../src/libc.c"
#include "../src/usb.c"
#include "../src/lcd.c"

#include "ldr_screen.c"                         /* (Skiff) the screen: UPDATE MODE, WRITING */
#include "fm1_clk_regs.h"                       /* (Skiff) the flash controller's divider as the app has it */
static uint32_t ldr_sfc_found;                  /* SFC_BAUD as this start found it (0x7C's last word on the SMK) */
#if BOARD_SMK37
#include "ldr_crumb.h"                          /* (Skiff) the start record: why it started, how far the last run got */
#define LDR_CRUMB ((ldr_crumb_t *)FM1_LDR_CRUMB_ADDR)
#endif

static uint32_t ldr_ms(void)   /* monotonic ms; survives the TIMER4 wrap (178.9 s) */
{
    static uint32_t last, acc, ms;
    uint32_t now = fm1_ticks();
    acc += now - last;
    last = now;
    ms += acc / (1000u * FM1_TICKS_PER_US);
    acc %= 1000u * FM1_TICKS_PER_US;
    return ms;
}

static void ldr_poll(void)                          /* USB at ~2 kHz, watchdog */
{
    static uint32_t last;
    uint32_t now = fm1_ticks();
    fm1_wdt_feed();
    if (now - last >= 500u * FM1_TICKS_PER_US) {
        last = now;
        usb_poll();
    }
}

/* ---- ota.c hooks (the SysEx frame hooks are in usb.c) ---- */
static uint32_t ota_now_ms(void) { return ldr_ms(); }
static void ota_idle(void) { ldr_poll(); }
/* step-1 hooks of ota.c: not used by the loader */
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../src/ota.c"
#if BOARD_SMK37
/* ota.c's queries (cmd 0x7B the start record, 0x7C the clock registers: ldr_crumb.h, fm1_clk_regs.h), 32 bytes each;
 * 0x7C's last word is SFC_BAUD as the loader found it at its start, before it set the app's (in place of USB_CLK) */
static void ota_reply_info(void) { ota_send_msg(0x7B, (const uint8_t *)LDR_CRUMB, 32); }
static void ota_reply_clocks(void)
{
    uint32_t r[8];
    fm1_clk_regs(r);
    r[7] = ldr_sfc_found;
    ota_send_msg(0x7C, (const uint8_t *)r, sizeof r);
}
static __attribute__((noinline)) void ldr_stage(uint32_t st, uint32_t sec)
{
    ldr_crumb_stage(LDR_CRUMB, st, sec, ldr_ms());
}
#define LDR_STAGE(st, sec) ldr_stage((st), (sec))
#endif

/* ---- ldr_core.c hooks: interrupts are off for the whole loader. Each feeds the watchdog first: no stretch without a
 * feed spans more than one flash operation ---- */
static int ldr_fread(uint32_t off, void *p, uint32_t n)
{
    fm1_wdt_feed();
    return fl_read_ram(off, p, n);
}
static int ldr_erase(uint32_t off)
{
    uint32_t took;
    fm1_wdt_feed();
#if BOARD_SMK37
    int rc;
    took = 0;                                       /* (a refused erase leaves it as it is) */
    rc = fl_erase4k_ram(off, &took);
    ldr_crumb_erase(LDR_CRUMB, took);
    return rc;
#else
    return fl_erase4k_ram(off, &took);
#endif
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    uint32_t took;
    fm1_wdt_feed();
    return fl_prog_ram(off, p, n, &took);
}
static void ldr_record_clear(void)
{
    fm1_updata_parm_clear();                        /* CRC 0: the SPL ignores it on a warm reset */
}
static void ldr_progress(uint32_t done, uint32_t total)
{
    ldr_bar(done, total);
    ldr_poll();
}
#include "ldr_core.c"

extern uint32_t _bss_start[], _bss_end[];

void ldr_main(void)
{
    fm1_wdt_arm(0x0D);                              /* ~8 s, fed by ldr_poll */
    fm1_time_init();
    ldr_screen();                                   /* (Skiff) before USB: see ldr_screen */
    fm1_wdt_feed();
    usb_start();
    for (;;) {
        ldr_poll();
        usb_retry(ldr_ms());
        if (usb.uboot_req) {                        /* soft key: mask-ROM UBOOT, as in Felucca */
            usb_detach();
            fm1_delay_ms(30);
            fm1_enter_uboot();
        }
        ota_service();                              /* handshake */
        if (usb.ota_req) {
            usb.ota_req = 0;
            if (ldr_session() == 0) {
                fm1_delay_ms(200);                  /* the "success" reply leaves */
                usb_detach();
                fm1_delay_ms(30);
                fm1_core_reset();                   /* the SPL boots the new app */
            }
            ldr_failed();                           /* (Skiff) the host may try again */
        }
    }
}

void ldr_cstart(void)
{
    uint32_t *p;
#if BOARD_SMK37
    uint32_t warm = (uint32_t)fm1_updata_parm_set();   /* (the app's RAM record still there: a warm start) */
#endif
    ldr_record_clear();                             /* first thing: a crash below cannot loop through a
                                                     * warm reset; resume after power loss uses the flash record */
    for (p = _bss_start; p < _bss_end; p++)
        *p = 0;
    ldr_sfc_found = fm1_sfc_baud_app();             /* (a power-on start: the boot ROM's slow divider, fm1_clk_regs.h) */
#if BOARD_SMK37
    fm1_reset_reason();
    ldr_crumb_start(LDR_CRUMB, warm, fm1_boot.p3_rst, fm1_boot.rst_src, fm1_boot.wdt_con);
#endif
    ldr_main();
}
