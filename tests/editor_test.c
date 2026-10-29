/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware editor handler, USB framing and UART recovery against RAM flash.
 * Build with the generated tables and the same flags as hostsim.c. */
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
#define FELUCCA_OTA 1
#define FELUCCA_FLASH 0
#define FELUCCA_VERSION "TEST"
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                                    /* (as the firmware: MENU > USB SERIAL switches the console) */
#endif
/* (Skiff 2.0) the MENU settings an editor is offered: 17; without the console (FELUCCA_CDC=0, a release) 16, no USB
 * SERIAL (the 10th): item i is the 17's MENU_I(i) */
#define MENU_NI (18u - !FELUCCA_CDC)
#define MENU_I(i) (FELUCCA_CDC || (i) < 9u ? (i) : (i) + 1u)
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint32_t host_progress = 1, host_erases, host_writes;
static uint8_t host_wire[4096];
static uint32_t host_wire_n;
static void host_drain(void)
{
    while (so_r != so_w) {
        uint32_t p = sx_out_q[so_r++ % SXQ], cin = p & 15u, i;
        uint32_t n = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : 1u;
        for (i = 0; i < n && host_wire_n < sizeof host_wire; i++)
            host_wire[host_wire_n++] = (uint8_t)(p >> (8u * (i + 1u)));
    }
}
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { host_drain(); fm1_ms++; }
static void fm1_wdt_feed(void)
{
    fm1_ms++;
    if (host_progress && transport_req == 2u) { seq_stop(); transport_req = 0; }
}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#define BK_RAM 1                                  /* (Skiff W3: the pattern bank's sectors in RAM, as ui_test's) */
#include "../firmware/src/bank.c"

static uint32_t flash_ok = 1;
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint8_t *host_flash_ptr(uint32_t off) { return &host_samples[0][0] + off - SMP_USER_BASE; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    memset(host_flash_ptr(off), 0xFF, 4096); *took = 0; host_erases++; return 0;
}
static int fl_write(uint32_t off, const void *p, uint32_t n)
{
    memcpy(host_flash_ptr(off), p, n); host_writes++; return 0;
}
static int st_read(uint32_t off, void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
#include "../firmware/src/storage.c"
static uint32_t host_rec_taps;                      /* (ui_input.c's REC, not built here; ui_test has it) */
static int rec_tap(void) { host_rec_taps++; song.rec ^= (uint8_t)(1u << song.sel); return 1; }
#include "../firmware/src/editor.c"

static int check(const char *what, int ok)
{
    printf("editor: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}
static void reset(void)
{
    uint32_t t, i;
    memset(&song, 0, sizeof song); sel_set(0); memset(trk, 0, sizeof trk);
    memset(&ed_w, 0, sizeof ed_w); memset(&ui, 0, sizeof ui);
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    memset(proj_slot, 0, sizeof proj_slot); memset(up_bank, 0, sizeof up_bank);
    memset(&um, 0, sizeof um); memset(usr_nz, 0, sizeof usr_nz);
    host_progress = 1; host_erases = host_writes = host_wire_n = 0;
    transport_req = panic_req = 0; sx_ready = sx_collect = sx_busy = 0;
    so_r = so_w = mi_r = mi_w = 0; midi_in_overflow = 0; usb.config = 1;
    for (i = 0; i < G_COUNT; i++) song.g[i] = GP[i].def;
    for (t = 0; t < NTRK; t++) {
        track_defaults(&trk[t]); set_engine_of(&trk[t], 0); apply_preset_to(&trk[t], 0);
        trk[t].engine = trk[t].eng_req; track_defaults_steps(&trk[t]);
    }
}
static uint32_t request(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i;
    host_wire_n = 0; ota_frame_done();
    sysex_byte(0xF0); sysex_byte(ED_HDR0); sysex_byte(ED_HDR1); sysex_byte(ED_HDR2); sysex_byte((uint8_t)cmd);
    for (i = 0; i < n; i++) sysex_byte(a[i]);
    sysex_byte(0xF7); ed_service(); host_drain();
    return host_wire_n;
}
static uint32_t pack7(const uint8_t *p, uint32_t n, uint8_t *a)
{
    uint32_t o = 0, i;
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = o++;
        a[m] = 0;
        for (i = 0; i < k; i++, n--) { a[m] |= (*p >> 7) << i; a[o++] = *p++ & 127u; }
    }
    return o;
}

/* 1.1 parameter locks over MOTION (64): ops 5 (set a lock), 6 (clear a step's locks, 127 every step's), 7 (the query
 * with the kinds); the query and ops 1..4 reply as before (a lock's id without bit 7) */
static int motion_locks(void)
{
    int bad = 0;
    uint32_t n;
    uint8_t a[8];
    reset();
    motion_clear(&trk[1]);
    a[0] = 1; a[1] = 3; a[2] = 2; a[3] = P_REV; a[4] = (uint8_t)((60 + 8192) & 127); a[5] = (uint8_t)((60 + 8192) >> 7);
    request(ED_MOTION, a, 6);                                           /* an automation event, step 2 */
    a[1] = 5; a[2] = 5; a[4] = (uint8_t)((100 + 8192) & 127); a[5] = (uint8_t)((100 + 8192) >> 7);
    n = request(ED_MOTION, a, 6);                                       /* a lock, step 5 */
    bad += check("MOTION op 5 sets a lock: rc 0, two records, then their kinds 0 1",
        host_wire[6] == 0 && host_wire[8] == 2 && host_wire[10] == 2 && host_wire[11] == P_REV &&
        host_wire[14] == 5 && host_wire[15] == P_REV && n == 5u + 5u + 2u * 4u + 2u + 1u &&
        host_wire[18] == 0 && host_wire[19] == 1 && motion_lock_count(&trk[1]) == 1u);
    n = request(ED_MOTION, a, 1);
    bad += check("MOTION query as before 1.1: no kinds, the lock's id without bit 7", n == 5u + 5u + 2u * 4u + 1u &&
        host_wire[15] == P_REV);
    a[1] = 7; n = request(ED_MOTION, a, 2);
    bad += check("MOTION op 7: the query with the kinds", n == 5u + 5u + 2u * 4u + 2u + 1u && host_wire[19] == 1);
    a[1] = 5; a[2] = 64; n = request(ED_MOTION, a, 6);
    bad += check("MOTION op 5 refuses step 64 (rc 1)", host_wire[6] == 1);
    a[1] = 6; a[2] = 2; request(ED_MOTION, a, 3);
    bad += check("MOTION op 6 on step 2: its automation event stays", host_wire[6] == 0 && host_wire[8] == 2);
    a[2] = 127; request(ED_MOTION, a, 3);
    bad += check("MOTION op 6, 127: every lock goes, the automation stays", host_wire[6] == 0 && host_wire[8] == 1 &&
        host_wire[14] == 0 && !motion_lock_count(&trk[1]));
    motion_clear(&trk[1]);
    return bad;
}

/* (Skiff 1.3, INFO 49 01) ids from 128 on: TRACK_PARAM's track byte bit 6, SET / GET scope 2, a MOTION op's and its
 * reply's step byte bit 6: each the id's bit 7 */
static int wide_ids(void)
{
    int bad = 0;
    uint32_t n, id = P_E5;
    int32_t v;
    uint8_t a[8];
    const param_desc_t *d;
    reset();
    sel_set(1);
    d = param_desc_of(trk[1].eng_req % NENGINES, id);
    v = d->max > d->min ? d->max : d->min;
    a[0] = 1u | 64u; a[1] = id & 127u; a[2] = (uint8_t)((v + 8192) & 127); a[3] = (uint8_t)((v + 8192) >> 7);
    n = request(ED_TRACK_PARAM, a, 4);
    bad += check("(1.3) TRACK_PARAM of an id past 127 (track byte bit 6): set, echoed", id > 127u && trk[1].p[id] == v &&
        host_wire[5] == (1u | 64u) && host_wire[6] == (id & 127u) && n == 5u + 4u + 1u);
    a[0] = 2; a[1] = id & 127u; v = d->min; a[2] = (uint8_t)((v + 8192) & 127); a[3] = (uint8_t)((v + 8192) >> 7);
    request(ED_SET, a, 4);
    bad += check("  SET scope 2: the selected track's id + 128", trk[1].p[id] == v && host_wire[5] == 2u);
    a[0] = 0; a[1] = id & 127u;
    request(ED_GET, a, 2);
    bad += check("  .. scope 0 is still the id below 128", trk[1].p[id & 127u] == (int16_t)((host_wire[7] | host_wire[8] << 7) - 8192));
    motion_clear(&trk[1]);
    v = d->max > d->min ? d->max : d->min;
    a[0] = 1; a[1] = 5; a[2] = 3u | 64u; a[3] = id & 127u; a[4] = (uint8_t)((v + 8192) & 127); a[5] = (uint8_t)((v + 8192) >> 7);
    n = request(ED_MOTION, a, 6);
    bad += check("  MOTION op 5 of an id past 127 (step byte bit 6): a lock on step 4 at that id, the reply's step byte so",
        host_wire[6] == 0 && motion.count == 1u && motion.event[0].id == id && motion.event[0].lock &&
        (motion.event[0].place & 63u) == 3u && host_wire[10] == (3u | 64u) && host_wire[11] == (id & 127u));
    motion_clear(&trk[1]);
    return bad;
}

static int preferences(void)
{
    int bad = 0;
    uint8_t a[4] = {0, 3, 0, 0};                          /* (THEME: 3, CRYPT) */
    reset();
    uint32_t n = request(ED_INFO, a, 0);
    bad += check("INFO (2.0) the song's tag last: 58 01 01 (LIST2, NEW, COPY+, CHAIN, ONCE, EMPTY, CLEAR UNUSED)",
        host_wire[n - 4] == 0x58 && host_wire[n - 3] == 1 && host_wire[n - 2] == ED_SONG2);
    n -= 3u;                                              /* (the tags before it, as they were) */
    bad += check("INFO (1.3) wide ids next: 49 01, P_COUNT and P_E0 in two 7-bit bytes each; the plain bytes at most 127",
        host_wire[n - 7] == 0x49 && host_wire[n - 6] == 1 && host_wire[n - 5] == (P_COUNT & 127u) &&
        host_wire[n - 4] == P_COUNT >> 7 && host_wire[n - 3] == (P_E0 & 127u) && host_wire[n - 2] == P_E0 >> 7 &&
        P_COUNT > 127u);
    n -= 6u;                                              /* (the tags before it, as they were) */
    bad += check("INFO explicitly tags display capabilities after SONG without changing command 33",
        ED_SONG == 33 && ED_UI_STATE == 34 && ED_FAV_SET == 38 &&
        host_wire[n - 9 - 37] == CHAIN_ROWS && host_wire[n - 9 - 36] == 0x55 &&
        host_wire[n - 9 - 35] == 1 && host_wire[n - 9 - 34] == 25 &&   /* ((Skiff #27) bit 4: COLORS) */
        host_wire[n - 9 - 33] == 0x4d && host_wire[n - 9 - 32] == 1 &&
        host_wire[n - 9 - 31] == MOTION_MAX && host_wire[n - 9 - 30] == 1 &&
        host_wire[n - 9 - 29] == 0x42 && host_wire[n - 9 - 28] == 1 && host_wire[n - 9 - 27] == 3 &&
        host_wire[n - 9 - 26] == 0x46 && host_wire[n - 9 - 25] == 1 && host_wire[n - 9 - 24] == FM6_NFACTORY &&
        host_wire[n - 9 - 23] == 0 &&                        /* (no bank since 1.0.3) */
        host_wire[n - 9 - 22] == 0x53 && host_wire[n - 9 - 21] == 1 && host_wire[n - 9 - 20] == 3 &&
        host_wire[n - 9 - 19] == 0x50 && host_wire[n - 9 - 18] == 1 && host_wire[n - 9 - 17] == 3 &&   /* FM6 v2: no bank, preset patches */
        host_wire[n - 9 - 16] == 0x4E && host_wire[n - 9 - 15] == 1 && host_wire[n - 9 - 14] == MENU_NI + (FELUCCA_USB_ROUTING ? (FELUCCA_USB_6CH?UAC_CHANNELS:2u):0u) &&   /* MENU settings: 18 items ((Skiff U2) STYLE, FX LATCH gone; #27 PLAYHEAD; (2.0) RETURN) */
        host_wire[n - 9 - 13] == 0x52 && host_wire[n - 9 - 12] == 1 && host_wire[n - 9 - 11] == 4 &&   /* RATCH */
        host_wire[n - 9 - 10] == 0x4C && host_wire[n - 9 - 9] == 1 && host_wire[n - 9 - 8] == 1 &&   /* 1.1 parameter locks */
        host_wire[n - 9 - 7] == 0x54 && host_wire[n - 9 - 6] == 1 && host_wire[n - 9 - 5] == 0x30 &&  /* (Skiff phase 6) T5, T6 */
        host_wire[n - 9 - 4] == 0 && host_wire[n - 9 - 3] == 6 && host_wire[n - 9 - 2] == 0 &&         /* light: no GRAIN, PHYS */
        host_wire[n - 10] == 0x43 && host_wire[n - 9] == 1 && host_wire[n - 8] == TC_COUNT &&
        host_wire[n - 7] == 0x4B && host_wire[n - 6] == 1 && host_wire[n - 5] == 8 &&    /* (Skiff W3) the kit, */
        host_wire[n - 4] == 0x57 && host_wire[n - 3] == 1 && host_wire[n - 2] == 127);   /* TRANSPORT .. PERFORM, (#21) SMP_MIX, (2.0) ties */   /* (Skiff W1) the step's FLAM
                                                                                          * COND NUDGE */
    request(ED_UI_SET, a, 2);
    bad += check("UI_SET updates the actual palette and reports RAM-only saving",
        host_wire[5] == 3 && settings.palette == 3 && T_BG == UI_PALETTES[3].bg && ux.crypt);
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    bad += check("out-of-range palette leaves the display unchanged", host_wire[5] == 1 && settings.palette == 3);
    a[0] = 1; a[1] = 1; request(ED_UI_SET, a, 2);
    bad += check("the retired font weight is not supported (rc 2), UI_STATE says 127",
        host_wire[5] == 2 && host_wire[8] == 25 && host_wire[10] == 127);
    a[0] = 2; request(ED_UI_SET, a, 2);
    bad += check("unsupported preference is reported without applying it", host_wire[5] == 2);
    a[0] = ENGI_DRUM; a[1] = 0; a[2] = 64; a[3] = 1;
    request(ED_FAV_SET, a, 4);
    bad += check("FAV_SET marks DRUM as a normal engine", host_wire[5] == 3 && favorite_has(ENGI_DRUM, 0));
    request(ED_FAV_GET, a, 4);
    bad += check("FAV_GET reads the bounded favorite range", host_wire[5] == 0 && host_wire[10] == 1);
    a[0] = NENGINES; a[1] = 31; request(ED_FAV_SET, a, 4);
    bad += check("empty user slot cannot be starred", host_wire[5] == 1 && !favorite_has(NENGINES, 31));
    up_store(31, "Saved"); request(ED_FAV_SET, a, 4);
    bad += check("saved user slot can be starred without changing its sound", host_wire[5] == 3 && favorite_has(NENGINES, 31));
    a[3] = 32; request(ED_FAV_GET, a, 4);
    bad += check("favorite range cannot cross the end of user slots", host_wire[5] == 1);
    request(ED_FAV_SET, a, 3);
    bad += check("short favorite writes return an error without reading absent bytes", host_wire[5] == 1);
    bad += check("UI_STATE rejects unexpected request bytes", request(ED_UI_STATE, a, 1) == 0);
    a[0] = 0; request(ED_SONG, a, 1);
    bad += check("SONG still responds through its original command (an empty song)", host_wire_n > 6 && host_wire[4] == 33 &&
                 host_wire[5] == 0 && !host_wire[7]);
    return bad;
}

static int framing(void)
{
    uint32_t i, bad = 0;
    static const uint8_t rt[] = {0xF0, 0x7D, 0xF8, 0x46, 0xFE, 0x4C, ED_PING, 0xFF, 0xF7};
    static const uint8_t aborted[] = {0xF0, 0x7D, 0x46, 0x4C, ED_SET, 0, P_LEVEL, 0x90, 0, 64, 0xF7};
    reset();
    for (i = 0; i < sizeof rt; i++) sysex_byte(rt[i]);
    ed_service(); host_drain();
    bad += check("realtime bytes interleaved in SysEx do not enter the editor frame",
                 host_wire_n == 7u && host_wire[4] == ED_PING && host_wire[5] == 0);
    ota_frame_done(); host_wire_n = 0;
    for (i = 0; i < sizeof aborted; i++) sysex_byte(aborted[i]);
    ed_service(); host_drain();
    bad += check("a non-realtime status aborts SysEx without changing parameters",
                 !sx_ready && !host_wire_n && TSEL->p[P_LEVEL] == TP[P_LEVEL].def);
    bad += check("a valid request works after an aborted frame", request(ED_PING, 0, 0) == 7u);
    ota_frame_done();
    midi_in_event(0x467DF004u);                          /* F0 7D 46 */
    midi_in_event(0x00004C04u);                          /* unfinished SysEx */
    midi_in_event(0x643C9009u);
    sysex_byte(0xF7);
    bad += check("a channel event in another USB packet aborts an unfinished SysEx",
                 !sx_ready && mi_w == 1u && midi_in_q[0] == 0x643C9009u);
    midi_in_event(0x643C8009u);                          /* wrong CIN */
    midi_in_event(0x643CFF09u);                          /* wrong status */
    midi_in_event(0xFF3C9009u);                          /* not a seven-bit velocity */
    midi_in_event(0x64FF9009u);                          /* not a seven-bit note */
    bad += check("USB channel packets reject mismatched CIN and high data bits", mi_w == 1u);
    midi_in_event(0xFF05C00Cu);                          /* one-byte status: padding is ignored */
    midi_in_event(0xFF07D00Du);
    bad += check("USB program and channel pressure keep their one-byte payload", mi_w == 3u);
    mi_w = UINT32_MAX - 5u; mi_r = mi_w;
    for (i = 0; i < MQ + 8u; i++) midi_in_event(0x643C9009u);
    bad += check("USB MIDI ring stops at capacity across counter wrap", mi_w - mi_r == MQ && midi_in_overflow);
    mi_r = mi_w; midi_in_event(0x643C9009u);
    bad += check("USB rejects new messages until the audio consumer clears overflow", mi_w == mi_r);
    midi_in_overflow = 0; midi_in_event(0x643C9009u);
    bad += check("USB resumes after the audio consumer clears overflow", mi_w - mi_r == 1u);
    return bad;
}

static int uart_recovery(void)
{
    uint32_t i;
    int bad = 0;
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 0;
    um_ring[126] = 0x90; um_ring[127] = 72; um_ring[0] = 99;
    uart_midi_take(UM_RING + 1u);
    bad += check("UART DMA overrun never combines a stale partial note with new data",
                 !mi_w && midi_in_overflow && um.drops == 2u && um.bytes == UM_RING && !um.pend);
    midi_in_overflow = 0; um_byte(0x90); um_byte(72); um_byte(99);
    bad += check("UART receives a complete fresh note after DMA recovery",
                 mi_w == 1u && midi_in_q[0] == 0x63489009u);
    reset();
    um_byte(0x90); um_byte(50);
    for (i = 0; i < UM_RING; i++) um_ring[i] = 60;
    uart_midi_take(UM_RING + 2u);
    bad += check("UART discards running-status data after lost bytes until a new status",
                 !mi_w && midi_in_overflow && um.drops == 2u && !um.st && !um.got);
    midi_in_overflow = 0;
    um_byte(0x91); um_byte(70); um_byte(100);
    bad += check("UART recovers immediately on a complete channel message", mi_w == 1u && midi_in_q[0] == 0x64469109u);
    reset(); mi_w = MQ;
    um_byte(0x90); um_byte(60); um_byte(100);
    bad += check("UART queue overflow latches the same recovery flag as USB", midi_in_overflow && um.drops == 1u && mi_w == MQ);
    mi_r = mi_w; um_byte(61); um_byte(101);
    bad += check("UART rejects a new stream until audio clears overflow", mi_w == mi_r && um.drops == 2u);
    midi_in_overflow = 0; um_byte(62); um_byte(102);
    bad += check("UART resumes valid running status after audio clears overflow",
                 mi_w - mi_r == 1u && midi_in_q[mi_r % MQ] == 0x663E9009u);
    return bad;
}

static int steps(void)
{
    uint8_t a[80] = {0, 1, 60, 0, 0, 0, ST_NOTE, SF_ACCENT, 100, 0x12, 0x02, 1};
    step_t before;
    uint32_t n, ok = 1;
    int bad = 0;
    reset();
    TSEL->step[0].hit = TSEL->step[0].acc = 0x80;
    bad += check("legacy 8-byte step writes preserve lane data",
                 request(ED_STEP_SET, a, 9) == 24u && TSEL->step[0].note[0] == 60 &&
                 TSEL->step[0].hit == 0x80 && TSEL->step[0].acc == 0x80);
    bad += check("full grid step writes preserve high lane bits and constrain accents",
                 request(ED_STEP_SET, a, 12) == 24u && TSEL->step[0].hit == 0x92 && TSEL->step[0].acc == 2);
    before = TSEL->step[0]; a[2] = 71;
    for (n = 2; n <= sizeof a; n++) {
        if (n == 9u || n == 12u || n == 13u) continue;   /* (14: a[13], the ratchet, 0 is refused) */
        ok &= !request(ED_STEP_SET, a, n) && !memcmp(&before, &TSEL->step[0], sizeof before);
    }
    bad += check("partial or oversized step payloads never mutate a valid step", ok);
    a[0] = 1; a[1] = 0; memcpy(a + 2, (const uint8_t[]){1,64,0,0,0,ST_NOTE,0,99}, 8);
    bad += check("TRACK_STEP accepts its legacy payload on an unselected track",
                 request(ED_TRACK_STEP, a, 10) == 25u && trk[1].step[0].note[0] == 64 && song.sel == 0);
    before = trk[1].step[0]; a[3] = 65;
    bad += check("TRACK_STEP rejects an incomplete grid extension",
                 !request(ED_TRACK_STEP, a, 11) && !memcmp(&before, &trk[1].step[0], sizeof before));
    /* RATCH (INFO 52 01 04): the hits 1..4 after the chance; the flags byte stays accent | slide both ways */
    memcpy(a, (const uint8_t[]){0, 1, 60, 0, 0, 0, ST_NOTE, SF_ACCENT, 100, 0, 0, 0, 80, 3}, 14);
    n = request(ED_STEP_SET, a, 14);
    bad += check("STEP_SET with the ratchet: x3 kept, replied after the chance, flags without it",
                 n == 24u && step_ratchet(&TSEL->step[0]) == 3u && step_chance(&TSEL->step[0]) == 80u &&
                 host_wire[n - 6] == 3 && host_wire[n - 7] == 80 && host_wire[12] == SF_ACCENT);
    a[7] = SF_SLIDE; n = request(ED_STEP_SET, a, 13);
    ok = n == 24u && step_ratchet(&TSEL->step[0]) == 3u && TSEL->step[0].flags == (SF_SLIDE | 2u << SF_RATCH_SH);
    n = request(ED_STEP_SET, a, 9);
    bad += check("STEP_SET without the ratchet (an older editor) keeps the step's own",
                 ok && n == 24u && step_ratchet(&TSEL->step[0]) == 3u);
    before = TSEL->step[0]; ok = 1;
    for (n = 0; n < 8u; n++) {
        a[13] = (uint8_t)(n < 4u ? 0u : 5u + n);
        ok &= !request(ED_STEP_SET, a, 14) && !memcmp(&before, &TSEL->step[0], sizeof before);
    }
    bad += check("STEP_SET refuses a ratchet outside 1..4 and leaves the step", ok);
    memcpy(a, (const uint8_t[]){1, 2, 1, 64, 0, 0, 0, ST_NOTE, 0, 99, 0, 0, 0, 100, 4}, 15);
    n = request(ED_TRACK_STEP, a, 15);
    ok = n == 25u && step_ratchet(&trk[1].step[2]) == 4u && host_wire[n - 6] == 4;
    a[14] = 1; n = request(ED_TRACK_STEP, a, 15);
    bad += check("TRACK_STEP sets the ratchet of any track (x4, then back to x1)",
                 ok && n == 25u && step_ratchet(&trk[1].step[2]) == 1u && !(trk[1].step[2].flags & SF_RATCH));
    /* (Skiff W1, INFO 43 01) FLAM, COND, NUDGE + 32 after the ratchet; an older editor's write keeps FLAM */
    memcpy(a, (const uint8_t[]){0, 1, 60, 0, 0, 0, ST_NOTE, 0, 100, 0, 0, 0, 100, 1}, 14);
    step_set_rflam(&TSEL->step[0], 5u);
    TSEL->step[0].cond = TC_1_2;
    TSEL->step[0].nudge = -7;
    n = request(ED_STEP_SET, a, 14);
    bad += check("STEP_SET x1 (an editor of before) keeps a FLAM step's FLAM, its COND and NUDGE; the reply carries them",
                 n == 24u && (TSEL->step[0].flags & SF_FLAM) && TSEL->step[0].cond == TC_1_2 && TSEL->step[0].nudge == -7 &&
                 host_wire[n - 5] == 1 && host_wire[n - 4] == TC_1_2 && host_wire[n - 3] == 25);
    memcpy(a, (const uint8_t[]){0, 1, 60, 0, 0, 0, ST_NOTE, 0, 100, 0, 0, 0, 100, 1, 0, TC_FILL, 32 + 9}, 17);
    n = request(ED_STEP_SET, a, 17);
    ok = n == 24u && !(TSEL->step[0].flags & SF_FLAM) && TSEL->step[0].cond == TC_FILL && TSEL->step[0].nudge == 9;
    a[14] = 1; a[15] = 0; a[16] = 32; n = request(ED_STEP_SET, a, 17);
    bad += check("STEP_SET writes FLAM, COND and NUDGE (a Clear: FLAM off, COND --, NUDGE 0)",
                 ok && n == 24u && (TSEL->step[0].flags & SF_FLAM) && !TSEL->step[0].cond && !TSEL->step[0].nudge);
    before = TSEL->step[0]; ok = 1;
    a[14] = 2; ok &= !request(ED_STEP_SET, a, 17);
    a[14] = 0; a[15] = TC_COUNT; ok &= !request(ED_STEP_SET, a, 17);
    a[15] = 0; a[16] = 64; ok &= !request(ED_STEP_SET, a, 17);
    bad += check("STEP_SET refuses FLAM past 1, COND past the list, NUDGE past 63 and leaves the step",
                 ok && !memcmp(&before, &TSEL->step[0], sizeof before));
    memcpy(a, (const uint8_t[]){1, 2, 1, 64, 0, 0, 0, ST_NOTE, 0, 99, 0, 0, 0, 100, 2, 1, TC_3_4, 0}, 18);
    n = request(ED_TRACK_STEP, a, 18);
    bad += check("TRACK_STEP writes COND and NUDGE on any track (FLAM only at x1)",
                 n == 25u && trk[1].step[2].cond == TC_3_4 && trk[1].step[2].nudge == -32 &&
                 !(trk[1].step[2].flags & SF_FLAM) && step_ratchet(&trk[1].step[2]) == 2u);
    /* (2.0, #25, INFO 57 01 bit 6) the ties after NUDGE: kept to the step's notes; none on a step with hits or a TIE; a
     * write without them (an editor of before) leaves none; past 15 refused */
    memcpy(a, (const uint8_t[]){3, 2, 60, 64, 0, 0, ST_NOTE, 0, 100, 0, 0, 0, 100, 1, 0, 0, 32, 1}, 18);
    n = request(ED_STEP_SET, a, 18);
    ok = n == 24u && step_ties(&TSEL->step[3]) == 1u && host_wire[n - 2] == 1;
    a[17] = 15; n = request(ED_STEP_SET, a, 18);
    ok &= n == 24u && TSEL->step[3].acc == 3u && host_wire[n - 2] == 3;
    before = TSEL->step[3];
    a[17] = 16; ok &= !request(ED_STEP_SET, a, 18) && !memcmp(&before, &TSEL->step[3], sizeof before);
    n = request(ED_STEP_SET, a, 17);
    ok &= n == 24u && !TSEL->step[3].acc && !host_wire[n - 2];
    bad += check("STEP_SET writes a step's ties (to its 2 notes), refuses 16, a write without them leaves none", ok);
    a[17] = 1; a[9] = 0x04; a[10] = 0x04; n = request(ED_STEP_SET, a, 18);
    ok = n == 24u && TSEL->step[3].hit == 0x04 && TSEL->step[3].acc == 0x04 && !host_wire[n - 2];
    a[9] = a[10] = 0; a[6] = ST_TIE; n = request(ED_STEP_SET, a, 18);
    ok &= n == 24u && !TSEL->step[3].acc;
    memcpy(a, (const uint8_t[]){1, 4, 2, 60, 64, 0, 0, ST_NOTE, 0, 99, 0, 0, 0, 100, 1, 0, 0, 32, 2}, 19);
    ok &= request(ED_TRACK_STEP, a, 19) == 25u && step_ties(&trk[1].step[4]) == 2u;
    n = request(ED_TRACK_STEP, a, 2);
    bad += check("no ties on a step with hits (its accents kept) or a TIE; TRACK_STEP writes and reads them on any track",
                 ok && n == 25u && host_wire[n - 2] == 2);
    {   /* (Skiff W3) the DRUM-X kit: KIT_GET, KIT_PUT, a lane's 15 values; a bad one refused, the kit as it was */
        static const uint8_t PUT[16] = {2, DXW_BELL, DXF_BP, 1, DXM_NOISE, 1, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100};
        dx_lane_t was = dx_kit[2];
        n = request(ED_KIT_PUT, PUT, 16);
        ok = n == 22u && (dx_kit[2].mode & 3u) == DXW_BELL && ((dx_kit[2].mode >> 2) & 3u) == DXF_BP && (dx_kit[2].mode & 0x10u) &&
             ((dx_kit[2].mode >> 5) & 3u) == DXM_NOISE && (dx_kit[2].mode & DX_DRIVE) && dx_kit[2].a[0] == 10 && dx_kit[2].b[4] == 100 &&
             !memcmp(host_wire + 5, PUT, 16);
        a[0] = 2; n = request(ED_KIT_GET, a, 1);
        ok &= n == 22u && !memcmp(host_wire + 5, PUT, 16);
        memcpy(a, PUT, 16); a[2] = 3;
        ok &= !request(ED_KIT_PUT, a, 16);
        memcpy(a, PUT, 16); a[15] = 128;
        ok &= !request(ED_KIT_PUT, a, 16);
        a[0] = 8; ok &= !request(ED_KIT_GET, a, 1);
        bad += check("KIT_PUT sets a DRUM-X lane (wave, filter, snap, pitch mod, drive, A, B), KIT_GET reads it; bad values refused",
                     ok && dx_kit_ok(dx_kit));
        dx_kit[2] = was;
    }
    return bad;
}

static int samples(void)
{
    uint8_t a[640] = {0}, data[257], short_group[] = {0, 0, 4, 0, 1};
    uint32_t n, i;
    int bad = 0;
    smp_user_hdr_t h;
    reset(); memset(host_samples, 0, sizeof host_samples);
    for (i = 0; i < sizeof data; i++) data[i] = (uint8_t)(i * 37u);
    song.playing = 1; host_progress = 0;
    bad += check("sample erase waits for STOP and refuses a stalled audio ISR",
                 request(ED_SMP_BEGIN, a, 1) == 8u && host_wire[6] != 0 && !host_erases && song.playing);
    host_progress = 1;
    bad += check("sample erase stops transport before touching flash",
                 request(ED_SMP_BEGIN, a, 1) == 8u && !host_wire[6] && host_erases == 1 && !song.playing);
    a[0] = 0; a[1] = 0; a[2] = 4; a[3] = 0;
    n = pack7(data, 257, a + 4) + 4u;
    bad += check("257 decoded sample bytes are rejected without a truncated write",
                 request(ED_SMP_WRITE, a, n) == 11u && host_wire[9] == 1 && !host_writes);
    bad += check("a dangling packed-data mask is rejected",
                 request(ED_SMP_WRITE, short_group, sizeof short_group) == 11u && host_wire[9] == 1 && !host_writes);
    n = pack7(data, 256, a + 4) + 4u;
    transport_req = 1;
    bad += check("sample writes cancel a pending PLAY before flash access",
                 request(ED_SMP_WRITE, a, n) == 11u && !host_wire[9] && transport_req != 1u && host_writes == 1);
    memset(&h, 0, sizeof h); h.magic = SMP_USER_MAGIC; h.version = 1; h.nz = 1;
    h.data_len = 256; h.crc = st_crc32(data, 256); h.zone[0].n = 512; h.zone[0].le = 511;
    h.zone[0].rate = 65536; h.zone[0].hi = 127;
    a[0] = 0; n = pack7((const uint8_t *)&h, sizeof h, a + 1) + 1u;
    a[n] = 0; a[n + 1u] = 0;
    bad += check("an oversized sample header is rejected without programming flash",
                 request(ED_SMP_END, a, n + 2u) == 8u && host_wire[6] == 1 && host_writes == 1);
    bad += check("complete sample header and CRC publish a valid slot",
                 request(ED_SMP_END, a, n) == 8u && !host_wire[6] && usr_nz[0] == 1 && host_writes == 2);
    bad += check("repeated END of the same published header does not rewrite live zones",
                 request(ED_SMP_END, a, n) == 8u && !host_wire[6] && host_writes == 2);
    h.zone[0].rate = 131072;
    n = pack7((const uint8_t *)&h, sizeof h, a + 1) + 1u;
    bad += check("END cannot change published sample zones without BEGIN",
                 request(ED_SMP_END, a, n) == 8u && host_wire[6] == 2 && host_writes == 2 &&
                 usr_zone[0][0].rate == 65536u);
    song.playing = 1; host_progress = 0; a[0] = 0; a[1] = 0;
    bad += check("user-preset STORE does not mutate RAM when audio cannot stop",
                 request(ED_UP_STORE, a, 2) == 8u && host_wire[6] == 2 && !up_used(0));
    bad += check("user-preset ERASE reports failure when audio cannot stop",
                 request(ED_UP_ERASE, a, 1) == 8u && host_wire[6] == 2);
    return bad;
}

/* (Skiff, #21) SMP_MIX (81): a slot's sounds' LEVEL REV DLY: read (slot, rc, nz, nz x (lo, hi), 16 + 16 + 16 values), set (the
 * 48 values: applied, the slot marked to save), refused on an empty slot (rc 1), set but not kept when the sample fills
 * the slot's last sector (rc 2); other arguments: no reply */
static int smp_mix(void)
{
    uint8_t a[64];
    uint32_t n, i;
    int bad = 0, ok;
    smp_user_hdr_t *h = (smp_user_hdr_t *)host_samples[0];
    reset(); memset(host_samples, 0, sizeof host_samples);
    smp_user_scan(0);
    a[0] = 0;
    n = request(ED_SMP_MIX, a, 1);
    bad += check("(#21) SMP_MIX of an empty slot: nz 0, every sound LEVEL 127, REV 0, DLY 0",
                 n == 5u + 3u + 48u + 1u && host_wire[4] == ED_SMP_MIX && !host_wire[5] && !host_wire[6] && !host_wire[7] &&
                 host_wire[8] == 127 && host_wire[8 + 15] == 127 && !host_wire[8 + 16] && !host_wire[8 + 47]);
    for (i = 0; i < 48u; i++)
        a[1 + i] = (uint8_t)(i + 1u);
    n = request(ED_SMP_MIX, a, 49);
    bad += check("  a set on it: rc 1, nothing changes", n == 57u && host_wire[6] == 1 && usr_mix[0][SMX_LVL][0] == 127 && !smx_save);
    memset(h, 0, sizeof *h); h->magic = SMP_USER_MAGIC; h->version = 1; h->nz = 2; h->data_len = 256;
    for (i = 0; i < 2u; i++) {
        h->zone[i].n = 256; h->zone[i].le = 255; h->zone[i].rate = 65536;
        h->zone[i].lo = h->zone[i].hi = (uint8_t)(60u + 2u * i);
    }
    smp_user_scan(0);
    a[0] = 0;
    n = request(ED_SMP_MIX, a, 1);
    bad += check("  a slot of two sounds: nz 2, their keys, the values as uploaded",
                 n == 5u + 3u + 4u + 48u + 1u && usr_nz[0] == 2 && host_wire[7] == 2 && host_wire[8] == 60 && host_wire[9] == 60 &&
                 host_wire[10] == 62 && host_wire[11] == 62 && host_wire[12] == 127 && host_wire[12 + 16] == 0);
    for (i = 0; i < 48u; i++)
        a[1 + i] = (uint8_t)(i * 2u + 1u);
    n = request(ED_SMP_MIX, a, 49);
    ok = n == 61u && !host_wire[6] && smx_save == 1u;
    for (i = 0; i < 48u; i++)
        ok &= usr_mix[0][i / 16u][i % 16u] == (int16_t)(i * 2u + 1u) && host_wire[12 + i] == i * 2u + 1u;
    bad += check("  a set: rc 0, the 48 values in, the slot marked to be kept, the reply the values now", ok);
    h->data_len = SMP_USER_SIZE - 0x1000u - SMP_USER_DATA + 1u;
    smx_save = 0; a[1] = 50;
    n = request(ED_SMP_MIX, a, 49);
    bad += check("  a sample into the slot's last sector: rc 2, set (until the power goes)",
                 n == 61u && host_wire[6] == 2 && usr_mix[0][SMX_LVL][0] == 50 && smx_save == 1u);
    a[0] = 3;
    ok = !request(ED_SMP_MIX, a, 1);
    a[0] = 0;
    ok &= !request(ED_SMP_MIX, a, 2) && !request(ED_SMP_MIX, a, 48) && !request(ED_SMP_MIX, a, 50) && !request(ED_SMP_MIX, a, 0);
    bad += check("  no reply to another slot or another length", ok);
    return bad;
}

static int song_protocol(void)
{
    uint8_t a[] = {1, 1, 0, 2};
    int bad = 0;
    reset();
    bad += check("(Skiff phase 5) SONG: no project-slot song: a set refused (rc 1), the song stays empty",
                 request(ED_SONG, a, sizeof a) && host_wire[5] == 1 && host_wire[6] == 1 && !host_wire[7]);
    a[0] = 2;
    bad += check("  PLAY refused too, nothing starts", request(ED_SONG, a, 1) && host_wire[6] == 1 && !transport_req);
    bad += check("  PLAY refuses extra argument bytes", !request(ED_SONG, a, 2) && !transport_req);
    return bad;
}

static int malformed_saves(void)
{
    static const uint8_t slot[] = {1, 4}, name[] = {0, 'X', 0, 'Y'};
    const uint8_t save[] = {1, 0};
    project_store_t old;
    int bad = 0;
    reset();
    bad += check("PROJECT refuses an invalid slot instead of wrapping to slot 0",
                 !request(ED_PROJECT, slot, sizeof slot) && !project_used(0));
    bad += check("UP_STORE rejects bytes after its name terminator without saving",
                 request(ED_UP_STORE, name, sizeof name) == 8u && host_wire[6] == 1 && !up_used(0));
    bad += check("a successful PROJECT save confirms its slot", request(ED_PROJECT, save, sizeof save) == 9u && project_used(0));
    old = proj_slot[0];
    TSEL->p[P_LEVEL] = 21; song.playing = 1; host_progress = 0;
    bad += check("PROJECT never confirms an old used slot when STOP timed out",
                 !request(ED_PROJECT, save, sizeof save) && !memcmp(&old, &proj_slot[0], sizeof old));
    return bad;
}

/* FM6 patches (cmds 68..71): a track's own patch, the retired bank ("no bank"), the factory patches, the user presets'
 * patches (target 3), malformed frames */
static int fm6_patches(void)
{
    int bad = 0;
    uint8_t a[2 + FM6_PACKED], pk[FM6_PACKED];
    uint32_t n, i;
    reset();
    upf_empty();
    a[0] = ED_FM6_FACTORY; a[1] = 3;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET factory 4: the packed record", n == 5u + 3u + FM6_PACKED + 1u && host_wire[7] == 0 &&
                 !memcmp(host_wire + 8, FM6_FACTORY[3], FM6_PACKED));
    memcpy(pk, FM6_FACTORY[3], FM6_PACKED);
    memcpy(pk + 118, "MY PATCH  ", 10);
    trk[2].eng_req = ENGI_FM6;
    trk[2].p[P_E7] = 3;
    a[0] = ED_FM6_TRACK; a[1] = 2; memcpy(a + 2, pk, FM6_PACKED);
    n = request(ED_FM6_PUT, a, sizeof a);
    bad += check("FM6_PUT track 3: rc 0, the track's patch, SLOT OWN (not F4: the name differs)", n == 9u &&
                 host_wire[7] == 0 && !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10) &&
                 trk[2].p[P_E7] == FM6_OWN && fm6_slot[2] == FM6_OWN);
    fm6_poll();
    bad += check("  .. the main loop keeps it", !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10));
    a[1] = 2;
    n = request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET track 3: as sent", host_wire[7] == 0 && !memcmp(host_wire + 8, pk, FM6_PACKED));
    a[0] = ED_FM6_TRACK; a[1] = 2; memcpy(a + 2, FM6_FACTORY[5], FM6_PACKED);
    request(ED_FM6_PUT, a, sizeof a);
    bad += check("FM6_PUT of a factory patch unchanged: SLOT shows it (F6)", trk[2].p[P_E7] == 5 && fm6_slot[2] == 5u);
    trk[2].p[P_E7] = FM6_OWN;                            /* (back to MY PATCH for the SLOT trip below) */
    a[0] = ED_FM6_TRACK; a[1] = 2; memcpy(a + 2, pk, FM6_PACKED);
    request(ED_FM6_PUT, a, sizeof a);
    trk[2].p[P_E7] = 1;
    fm6_poll();
    bad += check("SLOT OWN -> F2 loads the factory patch", !memcmp(fm6_patch[2] + FP_NAME, FM6_FACTORY[1] + 118, 10));
    trk[2].p[P_E7] = 6;
    fm6_poll();
    trk[2].p[P_E7] = FM6_OWN;
    fm6_poll();
    bad += check("  .. and back to OWN brings the own patch back", !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10) &&
                 fm6_slot[2] == FM6_OWN);
    trk[2].p[P_E7] = 20;                                 /* (a 1.0.2 B slot that escaped a clamp) */
    fm6_poll();
    bad += check("a SLOT past OWN is OWN: the patch stays, nothing reloads", trk[2].p[P_E7] == FM6_OWN &&
                 !memcmp(fm6_patch[2] + FP_NAME, "MY PATCH  ", 10));
    for (i = 0; i < 3u; i++) {                           /* the bank: GET, PUT, ERASE answer "no bank" (3) */
        a[0] = ED_FM6_BANK; a[1] = 4; memcpy(a + 2, pk, FM6_PACKED);
        if (i < 2u)
            request(i ? ED_FM6_PUT : ED_FM6_GET, a, i ? sizeof a : 2u);
        else {
            a[0] = 4;
            request(ED_FM6_ERASE, a, 1);
        }
        bad += check(i == 0u ? "FM6_GET of the bank: rc 3, no bank" : i == 1u ? "FM6_PUT to the bank: rc 3, no bank" :
                               "FM6_ERASE: rc 3, no bank", host_wire[i == 2u ? 6 : 7] == 3u);
    }
    n = request(ED_FM6_LIST, a, 0);
    {   /* factory 8, bank 0, then used + name per factory slot */
        uint32_t p = 7, k, ok = host_wire[5] == FM6_NFACTORY && host_wire[6] == 0;
        for (k = 0; k < FM6_NFACTORY && p < n; k++) {
            ok &= host_wire[p++] == 1u;
            while (host_wire[p]) p++;
            p++;
        }
        bad += check("FM6_LIST: 8 factory names, nbank 0", ok && k == FM6_NFACTORY && p == n - 1u);
    }
    /* target 3: an FM6 user preset's patch */
    sel_set(2);
    up_store(5, "MINE");                                 /* track 3 (MY PATCH, OWN) -> slot 6 */
    a[0] = ED_FM6_USER; a[1] = 5;
    request(ED_FM6_GET, a, 2);
    bad += check("UP_STORE of an FM6 track: FM6_GET user 6 gives its patch, named as the sound (FM6 LAB)",
                 host_wire[7] == 0 && !memcmp(host_wire + 8 + 118, "MINE      ", 10));
    memcpy(a + 2, pk, FM6_PACKED);
    memcpy(a + 2 + 118, "EDITOR    ", 10);
    a[2 + 14] = 120;                                     /* OP6 output level 120: stored as 99 */
    request(ED_FM6_PUT, a, sizeof a);
    i = host_wire[7];
    request(ED_FM6_GET, a, 2);
    bad += check("FM6_PUT user 6, then GET: stored in range (a level of 120 -> 99)", i == 0u && host_wire[7] == 0 &&
                 host_wire[8 + 14] == 99 && !memcmp(host_wire + 8 + 118, "EDITOR    ", 10));
    fm6_load_slot(2, 0);
    up_load(5);
    bad += check("  .. UP_LOAD 6 plays it, SLOT OWN", !memcmp(fm6_patch[2] + FP_NAME, "EDITOR    ", 10) &&
                 trk[2].p[P_E7] == FM6_OWN);
    up_rename(5, "RENAMED");
    a[0] = ED_FM6_USER; a[1] = 5;
    request(ED_FM6_GET, a, 2);
    bad += check("  .. a rename keeps it", host_wire[7] == 0 && !memcmp(host_wire + 8 + 118, "EDITOR    ", 10));
    trk[0].eng_req = ENGI_DRUM;
    sel_set(0);
    up_store(6, "KIT");
    a[0] = ED_FM6_USER; a[1] = 6; memcpy(a + 2, pk, FM6_PACKED);
    request(ED_FM6_GET, a, 2);
    i = host_wire[7];
    request(ED_FM6_PUT, a, sizeof a);
    bad += check("a DRUM user preset has no patch: GET rc 2, PUT rc 1", i == 2u && host_wire[7] == 1u);
    a[1] = 9;                                            /* an empty slot */
    request(ED_FM6_PUT, a, sizeof a);
    i = host_wire[7];
    a[1] = UP_SLOTS;
    request(ED_FM6_GET, a, 2);
    bad += check("an empty user slot: PUT rc 1; past the slots: GET rc 1", i == 1u && host_wire[7] == 1u);
    up_put(5, 0);
    a[1] = 5;
    request(ED_FM6_GET, a, 2);
    bad += check("an erased preset's patch is gone (GET rc 2)", host_wire[7] == 2u);
    a[0] = ED_FM6_TRACK; a[1] = NTRK;
    request(ED_FM6_PUT, a, sizeof a);
    i = host_wire[7];
    request(ED_FM6_PUT, a, 20);
    bad += check("FM6_PUT: a track past the last or a short record: rc 1", i == 1u && host_wire[7] == 1u);
    a[0] = 4; a[1] = 0;
    request(ED_FM6_GET, a, 2);
    bad += check("FM6_GET of an unknown target: rc 1", host_wire[7] == 1u);
    return bad;
}

/* UP_PUT -> UP_GET: every value round-trips through the record ((2.0) version 6: by stored id, a byte each), negative
 * ones too; the pattern UP_PUT sends is not kept (nothing plays it since Skiff phase 5) */
static int user_preset_roundtrip(void)
{
    static uint8_t a[16 + 2u * P_COUNT + 32u];
    int16_t want[P_COUNT], got[P_COUNT];
    uint32_t k = 0, i, p, ok;
    int bad = 0;
    reset();
    a[k++] = 3; a[k++] = 0;                                /* slot U04, ANALOG */
    memcpy(a + k, "ROUND", 5); k += 5; a[k++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        const param_desc_t *d = param_desc_of(0, i);
        int16_t v = d->def;
        uint32_t u;
        if (i == P_LEVEL) v = 90;
        if (i == P_PAN) v = -20;
        if (i == P_ED_FLT) v = -40;
        if (i == P_LD_FLT) v = d->min;
        if (i == P_E1) v = d->max;
        want[i] = v; u = (uint32_t)(v + 8192);
        a[k++] = u & 127u; a[k++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) { a[k++] = i & 1u ? 0u : (uint8_t)(48u + i); a[k++] = 0; }
    request(ED_UP_PUT, a, k);
    bad += check("UP_PUT stores a version 6 record (no flash here: kept in RAM)",
                 host_wire[6] != 1u && up_used(3) && up_rec(3)->ver == UP_VER_SID && up_rec(3)->np == SID_COUNT);
    up_values(up_rec(3), got);
    for (i = 0, ok = 1; i < P_COUNT; i++) ok &= got[i] == want[i];
    bad += check("UP_PUT keeps every value (PAN -20, FLT -40, min and max)", ok);
    a[0] = 3;
    request(ED_UP_GET, a, 1);
    ok = host_wire[5] == 3 && host_wire[6] == 1 && host_wire[7] == 0 && !memcmp(host_wire + 8, "ROUND", 6);
    for (i = 0, p = 14; i < P_COUNT; i++, p += 2u)
        ok &= (int32_t)(host_wire[p] | host_wire[p + 1] << 7) - 8192 == want[i];
    for (i = 0; i < 16u; i++, p += 2u)
        ok &= !host_wire[p] && !host_wire[p + 1u];
    bad += check("UP_GET returns the values; (2.0) no pattern: 16 rests, kind 0", ok && !host_wire[p]);
    return bad;
}

/* the frames of cmd in host_wire: how many, and the args of the last (into *args) */
static uint32_t wire_frames(uint32_t cmd, const uint8_t **args)
{
    uint32_t i, k = 0;
    for (i = 0; i + 5u < host_wire_n; i++)
        if (host_wire[i] == 0xF0 && host_wire[i + 1] == ED_HDR0 && host_wire[i + 4] == cmd) {
            k++;
            if (args) *args = host_wire + i + 5;
        }
    return k;
}
static uint32_t sync_pass(void)                          /* one main-loop pass of the pushes */
{
    host_wire_n = 0;
    fm1_ms += 30u;
    ed_sync();
    host_drain();
    return host_wire_n;
}

/* #65: WATCH while watching keeps what is not pushed yet; the editor's own sound load is not echoed as RELOAD */
static int live_sync(void)
{
    int bad = 0;
    uint8_t a[4] = {1, 0, 0, 0};
    uint32_t i, other = 0;
    const uint8_t *x = 0;
    reset();
    usb.resets = 0;
    request(ED_WATCH, a, 1);
    bad += check("WATCH 1 starts watching", host_wire[5] == 1 && ed_w.on && !sync_pass());
    TSEL->p[P_LEVEL] = 77;                               /* a device change, not pushed yet */
    request(ED_WATCH, a, 1);
    bad += check("WATCH 1 while watching keeps a pending change: CHANGED still goes out",
                 sync_pass() && wire_frames(ED_CHANGED, &x) == 1u && x[1] == P_LEVEL && ed_rv(x + 2) == 77);
    TSEL->p[P_LEVEL] = 66;
    a[0] = 0; request(ED_WATCH, a, 1);
    a[0] = 1; request(ED_WATCH, a, 1);
    bad += check("WATCH 0 then WATCH 1 starts from the values as they are (no push)", !sync_pass());
    TSEL->p[P_LEVEL] = 55; trk[1].p[P_PAN] = 3;
    a[0] = 3; request(ED_WATCH, a, 1);
    sync_pass();
    bad += check("WATCH 3 while watching: CHANGED kept, TRACK_CHANGED from the mix as it is",
                 host_wire_n && wire_frames(ED_CHANGED, 0) == 1u && !wire_frames(ED_TRACK_CHANGED, 0) && ed_w.v4);
    usb.resets++;
    TSEL->p[P_LEVEL] = 44;
    request(ED_WATCH, a, 1);
    bad += check("WATCH after a USB reset starts over", !sync_pass());

    /* PRESET */
    TSEL->p[P_LEVEL] = 33;                               /* pending; a sound load keeps P_LEVEL */
    a[0] = 0; a[1] = 2;
    request(ED_PRESET, a, 2);
    sync_pass();
    bad += check("the editor's own PRESET: no RELOAD, no CHANGED for what it loaded, a pending CHANGED stays",
                 TSEL->preset == 2 && !wire_frames(ED_RELOAD, 0) && wire_frames(ED_CHANGED, &x) == 1u &&
                 x[1] == P_LEVEL && !sync_pass());
    for (i = 0; i < 3u; i++) {                           /* any preset of any engine */
        a[0] = (uint8_t)(i ? 12u : 4u); a[1] = (uint8_t)i;
        request(ED_PRESET, a, 2);
        other += sync_pass() != 0;
    }
    bad += check("PRESET of other engines: no push at all", !other && ed_w.eng == ed_eng(TSEL));
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + 5) & 127; a[3] = (8192 + 5) >> 7;
    request(ED_SET, a, 4);
    bad += check("SET of G_ENGSEL: no RELOAD", ed_eng(TSEL) == 5u && !sync_pass());
    sync_reload = 1;                                     /* a load on the device, not pushed yet */
    a[0] = 0; a[1] = 1;
    request(ED_PRESET, a, 2);
    sync_pass();
    bad += check("a RELOAD due before the editor's PRESET still goes out", wire_frames(ED_RELOAD, 0) == 1u);
    a[0] = 0; request(ED_WATCH, a, 1);
    sync_reload = 0;
    a[0] = 0; a[1] = 3;
    request(ED_PRESET, a, 2);
    a[0] = 1; request(ED_WATCH, a, 1);
    bad += check("not watching: PRESET as before, WATCH then starts from it", !sync_pass() && TSEL->preset == 3);
    return bad;
}

/* #64: SysEx through the USB packet path (ep1_take) at full speed: a 256-byte BACKUP_PUT piece (293 pack7 bytes, 101
 * event packets, 7 USB packets) arrives whole; one request at a time always works, a frame sent before the reply
 * to the one before is dropped whole (no reply; never a partial frame) */
static uint32_t usb_frame(const uint8_t *f, uint32_t n, uint8_t *usbp)   /* F0..F7 -> USB-MIDI event packets */
{
    uint32_t i = 0, o = 0;
    while (i < n) {
        uint32_t k = n - i >= 3u ? 3u : n - i, cin = k == 3u && i + 3u < n ? 4u : k == 3u ? 7u : 4u + k;
        usbp[o++] = (uint8_t)cin;
        usbp[o++] = f[i];
        usbp[o++] = k > 1u ? f[i + 1] : 0;
        usbp[o++] = k > 2u ? f[i + 2] : 0;
        i += k;
    }
    return o;
}
static uint32_t usb_feed(const uint8_t *p, uint32_t n)   /* whole 64-byte USB packets, as the host sends them */
{
    uint32_t o, refused = 0;
    for (o = 0; o < n; o += 64u)
        while (!ep1_take(p + o, n - o > 64u ? 64u : n - o))
            refused++;                                  /* NAK: the host sends it again */
    return refused;
}
static uint32_t put_frame(uint8_t *f, uint32_t op, uint32_t off, uint32_t count)
{
    static const uint8_t zero[256];
    uint32_t n = 0, i;
    f[n++] = 0xF0; f[n++] = ED_HDR0; f[n++] = ED_HDR1; f[n++] = ED_HDR2; f[n++] = ED_BACKUP_PUT;
    f[n++] = (uint8_t)op; f[n++] = 2;
    if (op == 0u) {
        for (i = 0; i < 5u; i++) f[n++] = (uint8_t)((sizeof(project_store_t) >> (7u * i)) & (i == 4u ? 15u : 127u));
        for (i = 0; i < 5u; i++) f[n++] = 0;
    } else if (op == 1u) {
        for (i = 0; i < 5u; i++) f[n++] = (uint8_t)((off >> (7u * i)) & (i == 4u ? 15u : 127u));
        n += pack7(zero, count, f + n);
    }
    f[n++] = 0xF7;
    return n;
}
static int usb_burst(void)
{
    static uint8_t f[700], u[1000], u2[2000];
    uint32_t off, n, k, ok = 1, max = 0;
    const uint8_t *x = 0;
    int bad = 0;
    reset();
    usb.rx_pend = 0;
    n = put_frame(f, 0, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("BACKUP_PUT begin through ep1_take", wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2]);
    for (off = 0; off < sizeof(project_store_t); off += 256u) {
        uint32_t c = sizeof(project_store_t) - off > 256u ? 256u : sizeof(project_store_t) - off;
        n = put_frame(f, 1, off, c);
        if (n > max) max = n;
        k = usb_frame(f, n, u);
        usb_feed(u, k);
        host_wire_n = 0; ed_service(); host_drain();
        ok &= wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && sx_ready == 0;
    }
    bad += check("256-byte pieces in 64-byte USB packets at full speed: every piece taken (rc 0)",
                 ok && ed_bk_pos == sizeof(project_store_t) && max == 4u + 1u + 7u + 293u + 1u && max <= sizeof sx_frame);
    n = put_frame(f, 3, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("abort through ep1_take", wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && !ed_bk_put);

    /* two frames back to back, before the reply: the second is dropped whole, the first answered intact */
    n = put_frame(f, 0, 0, 0);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    k = usb_frame(f, put_frame(f, 1, 0, 256), u2);
    k += usb_frame(f, put_frame(f, 1, 256, 256), u2 + k);
    usb_feed(u2, k);
    host_wire_n = 0; ed_service(); ed_service(); host_drain();
    bad += check("pipelined: the first piece is taken, the second (sent before its reply) dropped whole",
                 wire_frames(ED_BACKUP_PUT, &x) == 1u && !x[2] && ed_bk_pos == 256u && !sx_ready);
    n = put_frame(f, 1, 512, 256);
    usb_feed(u, usb_frame(f, n, u)); host_wire_n = 0; ed_service(); host_drain();
    bad += check("... so the next piece is refused (rc 1, offset), as a host that waits never sees",
                 wire_frames(ED_BACKUP_PUT, &x) == 1u && x[2] == 1u && ed_bk_pos == 256u);
    request(ED_BACKUP_PUT, (const uint8_t[]){3, 2}, 2);
    return bad;
}

/* ---- MENU_DESC / MENU_SET (72, 73): the menu's settings over the editor ---- */
typedef struct { uint32_t index, id, kind, nnames, tab, rest; int32_t value, min, max; char name[16], names[12][12], tabname[16]; } menu_item_t;
static uint32_t menu_desc(uint32_t index, menu_item_t *it)   /* the reply's payload length; it parsed */
{
    uint8_t a[1] = {(uint8_t)index};
    uint32_t n = request(ED_MENU_DESC, a, 1), p = 14, k;
    memset(it, 0, sizeof *it);
    if (n < 8u || host_wire[4] != ED_MENU_DESC || host_wire[n - 1] != 0xF7) return 0;
    it->index = host_wire[5]; it->id = host_wire[6];
    if (n < 15u) return n - 6u;
    it->kind = host_wire[7];
    it->value = ed_rv(host_wire + 8); it->min = ed_rv(host_wire + 10); it->max = ed_rv(host_wire + 12);
    for (k = 0; p < n - 1u && host_wire[p] && k < 15u; ) it->name[k++] = (char)host_wire[p++];
    p++;
    while (p < n - 1u && it->nnames < 12u && (int32_t)it->nnames < it->max - it->min + 1) {   /* (kind 0: max - min + 1) */
        for (k = 0; p < n - 1u && host_wire[p] && k < 11u; ) it->names[it->nnames][k++] = (char)host_wire[p++];
        p++; it->nnames++;
    }
    it->tab = 127;                                          /* 1.0.5: then the row's tab, index and name */
    if (p < n - 1u) {
        it->tab = host_wire[p++];
        for (k = 0; p < n - 1u && host_wire[p] && k < 15u; ) it->tabname[k++] = (char)host_wire[p++];
        p++;
    }
    it->rest = p < n - 1u ? n - 1u - p : 0u;                /* (bytes left over: none) */
    return n - 6u;
}
static uint32_t menu_set(uint32_t id, int32_t v)            /* -> rc; host_wire[6], [7..8]: id, value */
{
    uint8_t a[3] = {(uint8_t)id, (uint8_t)((v + 8192) & 127), (uint8_t)(((v + 8192) >> 7) & 127)};
    uint32_t n = request(ED_MENU_SET, a, 3);
    return n == 10u && host_wire[4] == ED_MENU_SET ? host_wire[5] : 99u;
}
static int menu_protocol(void)
{
    static const char *const WANT[18][2] = {   /* ((Skiff U2) STYLE, id 1, and FX LATCH, id 7, retired) */
        {"THEME", 0}, {"LARGE", "OFF,ON"}, {"ANIM", "ON,OFF"}, {"LEDS", "OFF,DIM LO,DIM HI,INV"},
        {"HOLD", "0.3 s,0.4 s,0.5 s,0.6 s"}, {"KNOB ACCEL", "OFF,ON"}, {"BPM LOCK", "OFF,ON"},
        {"SPEAKER EQ", "FLAT,LOWCUT,BASS+"}, {"USB LEVEL", "MASTER,FIXED"}, {"USB SERIAL", "ON,OFF"},
        {"CLICK", "OFF,REC,ON"}, {"CLICK LEVEL", "LOW,MID,HIGH"}, {"COUNT-IN", "OFF,1 BAR,2 BARS"},   /* (1.1: appended) */
        {"RESTORE LAST", "ON,OFF"}, {"SCALE LEDS", "OFF,ON"},                                        /* (1.2) */
        {"SCREEN OFF", "NEVER,5 MIN,15 MIN,30 MIN,60 MIN"}, {"PLAYHEAD", "THEME,LINE"},   /* ((Skiff #27) id 19) */
        {"RETURN", "SNAP,1 BEAT,1 BAR"}};                                                   /* ((2.0, D 2A) id 20) */
    static const int32_t DEF[18] = {-1, 0, 0, 2, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 3, 0, 2};   /* (COLOR: the default palette) */
    static const uint8_t TAB[18] = {0, 0, 0, 0, 1, 1, 1, 2, 2, 3, 2, 2, 2, 3, 1, 0, 0, 1};   /* DISPLAY CONTROL AUDIO SYSTEM */
    static const uint8_t ID[18] = {0, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20};   /* (1, 7, 18: none here) */
    static const char *const TABN[4] = {"DISPLAY", "CONTROL", "AUDIO", "SYSTEM"};
    int bad = 0, ok = 1;
    uint32_t i, k, n;
    menu_item_t it;
    char joined[96];
    const char *json = getenv("MENU_JSON");
    FILE *jf = json ? fopen(json, "w") : 0;
    reset();
    if (jf) fprintf(jf, "[");
    for (i = 0; i < MENU_NI; i++) {
        uint32_t w = MENU_I(i);
        n = menu_desc(i, &it);
        joined[0] = 0;
        for (k = 0; k < it.nnames; k++) { if (k) strcat(joined, ","); strcat(joined, it.names[k]); }
        ok &= n > 10u && it.index == i && it.id == ID[w] && it.kind == 0 && it.min == 0 &&
              it.max + 1 == (int32_t)it.nnames && !strcmp(it.name, WANT[w][0]) &&
              (WANT[w][1] ? !strcmp(joined, WANT[w][1]) : it.nnames == NPALETTES) &&
              it.value == (DEF[w] < 0 ? (int32_t)settings.palette : DEF[w]) &&
              it.tab == TAB[w] && !strcmp(it.tabname, TABN[TAB[w]]) && !it.rest;
        if (i == 0)
            for (k = 0; k < NPALETTES; k++) ok &= !strcmp(it.names[k], UI_PALETTES[k].name);
        if (!ok) { printf("  MENU_DESC %u: %s [%s] value %d max %d tab %u %s\n", i, it.name, joined, it.value, it.max, it.tab, it.tabname); break; }
        if (jf) {
            fprintf(jf, "%s\n {\"id\":%u,\"kind\":%u,\"min\":%d,\"max\":%d,\"value\":%d,\"name\":\"%s\",\"names\":[", i ? "," : "",
                    it.id, it.kind, it.min, it.max, it.value, it.name);
            for (k = 0; k < it.nnames; k++) fprintf(jf, "%s\"%s\"", k ? "," : "", it.names[k]);
            fprintf(jf, "],\"tab\":%u,\"tabName\":\"%s\"}", it.tab, it.tabname);
        }
    }
    if (jf) { fprintf(jf, "]\n"); fclose(jf); }
    bad += check("MENU_DESC: 18 items (1.0.4's 12 in the menu's order less STYLE and FX LATCH, then 1.1's CLICK, CLICK LEVEL, COUNT-IN, 1.2's RESTORE LAST, SCALE LEDS, SCREEN OFF, (Skiff #27) PLAYHEAD, (2.0) RETURN), ids 0, 2..6, 8..17, 19, 20, names, defaults", ok);
    bad += check("MENU_DESC (1.0.5): after the names each item's tab, index and name (DISPLAY CONTROL AUDIO SYSTEM)", ok);
    {   /* an older editor reads the names and stops: the tab is past them, nothing it reads moved */
        uint32_t m = menu_desc(3, &it), p = 14, q;
        for (q = 0; q < 1u + it.nnames; q++) { while (host_wire[p]) p++; p++; }   /* name, the names */
        ok = m > 10u && host_wire[p] == 0 && !strcmp((const char *)host_wire + p + 1, "DISPLAY") &&
             p + 1u + 8u == m + 6u - 1u;
        bad += check("MENU_DESC: the tab comes after every byte of the 1.0.4 reply (older editors ignore it)", ok);
    }
    ok = 1;
    for (i = 0; i < MENU_NI; i++) {
        menu_desc(i, &it);
        ok &= strcmp(it.name, "CALIBRATION") && strcmp(it.name, "ABOUT");
    }
    n = menu_desc(ED_MENU_N, &it);
    ok &= n == 2u && it.index == ED_MENU_N && it.id == 127;
    n = menu_desc(127, &it);
    bad += check("MENU_DESC: no CALIBRATION / ABOUT; an index past the list answers index, 127 (no item)",
                 ok && n == 2u && it.index == 127 && it.id == 127);
    {
        uint8_t a[3] = {0, 0, 0};
        ok = request(ED_MENU_DESC, a, 0) == 0 && request(ED_MENU_DESC, a, 2) == 0 && request(ED_MENU_SET, a, 2) == 0 &&
             request(ED_MENU_SET, a, 1) == 0;
        bad += check("MENU_DESC / MENU_SET with the wrong length: no reply", ok);
    }

    /* MENU_SET: each setting, applied as the menu applies it; RAM only here (no flash: rc 3) */
    reset();
    ui.force = 0;
    ok = menu_set(0, 2) == 3 && host_wire[6] == 0 && ed_rv(host_wire + 7) == 2 && settings.palette == 2u &&
         T_BG == UI_PALETTES[2].bg && ui.force;
    ok &= menu_set(1, 1) == 1u;                        /* ((Skiff U2) id 1, STYLE: retired, no such setting) */
    ok &= menu_set(2, 1) == 3 && (ui_prefs & PREF_LARGE);
    ok &= menu_set(3, 1) == 3 && (ui_prefs & PREF_ANIM_OFF);
    ok &= menu_set(4, 0) == 3 && settings_leds == LEDS_OFF && menu_set(4, 1) == 3 && settings_leds == LEDS_DIM_LO;
    ok &= menu_set(5, 3) == 3 && settings_hold == 3u;
    ok &= menu_set(6, 1) == 3 && (ui_prefs & PREF_ACCEL);
    ok &= menu_set(7, 1) == 1u;                        /* ((Skiff U2) id 7, FX LATCH: retired) */
    ok &= menu_set(8, 1) == 3 && (ui_prefs & PREF_BPM_LOCK);
    ok &= menu_set(9, 2) == 3 && settings.lowcut == 2u && fx_lowcut == 2u;
    ok &= menu_set(10, 1) == 3 && (ui_prefs & PREF_USB_FIXED) && fx_usb_fixed;
    ok &= menu_set(12, 1) == 3 && click_mode == CLICK_REC && menu_set(12, 2) == 3 && click_mode == CLICK_ON;
    ok &= menu_set(13, 0) == 3 && click_lvl == 0u;
    ok &= menu_set(14, 2) == 3 && cin_bars == 2u;
    ok &= menu_set(15, 1) == 3 && (ui_prefs & PREF_RESTORE_OFF);
    ok &= menu_set(16, 1) == 3 && (ui_rec_prefs & 0x40u) && click_mode == CLICK_ON && cin_bars == 2u;   /* (its own bit) */
    ok &= menu_set(17, 0) == 3 && scr_get() == 0u && menu_set(17, 9) == 3 && scr_get() == 4u &&   /* (clamped) */
          menu_set(17, 2) == 3 && scr_get() == 2u && ui_scr == 1u;
    ok &= menu_set(19, 1) == 3 && ui_ph_line == 1u;    /* ((Skiff #27) PLAYHEAD LINE) */
    favorites.factory[15][24] = 0xA5u;                  /* ((2.0) RETURN: a byte bank.c freed, a unit past its first */
    ok &= menu_set(20, 0) == 3 && ret_mode() == RET_SNAP;   /* boot; SNAP) */
    for (i = 0; i < MENU_NI; i++) {                     /* MENU_DESC reads them back */
        static const int32_t SET[18] = {2, 1, 1, 1, 3, 1, 1, 2, 1, 0, 2, 0, 2, 1, 1, 2, 1, 0};
        menu_desc(i, &it);
        ok &= it.value == SET[MENU_I(i)];
    }
    bad += check("MENU_SET: every setting applied as the menu does (palette, EQ, USB LEVEL, CLICK, COUNT-IN at once, RESTORE LAST, SCALE LEDS, SCREEN OFF, PLAYHEAD, (2.0) RETURN), read back", ok);
    ok = menu_set(3, 0) == 3 && !(ui_prefs & PREF_ANIM_OFF) && (ui_prefs & PREF_LARGE) && menu_set(10, 0) == 3 &&
         !fx_usb_fixed;
    bad += check("MENU_SET: a flag back to its default leaves the other flags", ok);

    /* clamping: the reply carries the device's value */
    ok = menu_set(0, 50) == 3 && ed_rv(host_wire + 7) == (int32_t)NPALETTES - 1 && settings.palette == NPALETTES - 1u;
    ok &= menu_set(0, -5) == 3 && ed_rv(host_wire + 7) == 0 && settings.palette == 0u;
    ok &= menu_set(4, 99) == 3 && ed_rv(host_wire + 7) == 3 && settings_leds == LEDS_INV;
    ok &= menu_set(5, 8191) == 3 && ed_rv(host_wire + 7) == 3 && settings_hold == 3u;
    ok &= menu_set(2, 7) == 3 && ed_rv(host_wire + 7) == 1 && (ui_prefs & PREF_LARGE);
    ok &= menu_set(14, 9) == 3 && ed_rv(host_wire + 7) == 2 && cin_bars == 2u;
    ok &= menu_set(12, -1) == 3 && ed_rv(host_wire + 7) == 0 && click_mode == CLICK_OFF;
    bad += check("MENU_SET: out-of-range values clamped, the reply says the value the device took", ok);

    /* an id nobody has */
    {
        static uint8_t fav0[sizeof favorites], set0[sizeof settings];
        uint8_t hold0 = settings_hold, leds0 = settings_leds;
        memcpy(fav0, &favorites, sizeof favorites); memcpy(set0, &settings, sizeof settings);
        ok = menu_set(18, 1) == 1 && host_wire[6] == 18 && ed_rv(host_wire + 7) == 1;
        ok &= menu_set(126, -3) == 1 && host_wire[6] == 126 && ed_rv(host_wire + 7) == -3;
        ok &= menu_set(127, 0) == 1 && host_wire[6] == 127;
        ok &= !memcmp(fav0, &favorites, sizeof favorites) && !memcmp(set0, &settings, sizeof settings) &&
              hold0 == settings_hold && leds0 == settings_leds;
        bad += check("MENU_SET: an unknown id answers rc 1, id and value echoed, nothing changed", ok);
    }

    /* the MENU page shown: the editor's change redraws it */
    ui.menu = 1; ui.force = 0;
    menu_set(6, 0);
    bad += check("MENU_SET while the MENU page is shown: the page redraws (ui.force)", ui.force && ui.menu == 1);
    ui.menu = 0;

#if FELUCCA_CDC
    /* USB SERIAL: the reply first, the re-enumeration 200 ms later (usb_serial_apply, called every frame). (usb.up
     * stays 0 here: the host has no USB registers; usb.config 0 = the device off the bus, enumerated afresh) */
    {
        uint32_t t0;
        reset();
        usb_cdc_on = 1;
        usb_serial_apply();
        ok = usb_cdc_on && usb.config;
        ok &= menu_set(11, 1) == 3 && host_wire[6] == 11 && ed_rv(host_wire + 7) == 1 && (ui_prefs & PREF_SERIAL_OFF);
        t0 = fm1_ms;
        usb_serial_apply();
        ok &= usb_cdc_on && usb.config;                 /* the reply has left; USB as it was */
        fm1_ms = t0 + 150u; usb_serial_apply();
        ok &= usb_cdc_on && usb.config;
        fm1_ms = t0 + 201u; usb_serial_apply();
        ok &= !usb_cdc_on && !usb.config;               /* off the bus: usb_retry attaches again without the console */
        bad += check("MENU_SET USB SERIAL OFF: reply first, applied ~200 ms later (the device re-enumerates)", ok);
        usb.config = 1;
        ok = menu_set(11, 0) == 3 && !(ui_prefs & PREF_SERIAL_OFF);
        t0 = fm1_ms;
        ui.menu = 1;                                    /* the MENU shown: applied when it closes, as the menu's own */
        fm1_ms = t0 + 500u;                             /* (ui_input calls usb_serial_apply only with the menu closed) */
        ok &= !usb_cdc_on && usb.config;
        ui.menu = 0; usb_serial_apply();
        ok &= usb_cdc_on && !usb.config;
        usb.config = 1;
        ok &= menu_set(11, 0) == 3;                     /* unchanged: no re-enumeration */
        fm1_ms += 300u; usb_serial_apply();
        ok &= usb_cdc_on && usb.config;
        bad += check("MENU_SET USB SERIAL ON: applied later (with the MENU open: when it closes); unchanged: no drop", ok);
    }
#else
    bad += check("(Skiff 2.0) FELUCCA_CDC=0 (release): MENU_SET of USB SERIAL (id 11): rc 1, no such setting",
                 menu_set(11, 1) == 1u);
#endif
    reset();
    return bad;
}

/* DRUM KIT 1..3 (HAND CYM H+CYM until 1.0.4, retired): DESC names them as the kit they play (66 10 77), a SET of
 * one lands on that kit (the reply says so), a user preset sent with one loads as that kit */
static int drum_kit_retired(void)
{
    static const uint8_t MAP[4] = {0, 6, 5, 8};
    static uint8_t a[16 + 2u * P_COUNT + 32u];
    int16_t got[P_COUNT];
    uint32_t r, i, k = 0, ok = 1;
    int bad = 0;
    reset();
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192 + ENGI_DRUM) & 127; a[3] = (8192 + ENGI_DRUM) >> 7;
    request(ED_SET, a, 4);
    a[0] = P_E0 > 127u ? 2 : 0; a[1] = P_E0 & 127u;     /* ((2.0, #36) P_E0 135: scope 2, wide) */
    request(ED_DESC, a, 2);
    {   /* scope, id, fmt, min, max, def (v14 each), "KIT", "", then the 9 names */
        const char *n = (const char *)host_wire + 5 + 3 + 6;
        static const char *const WANT[9] = {"STD", "66", "10", "77", "80", "10", "66", "55", "77"};
        n += strlen(n) + 1; n += strlen(n) + 1;
        for (i = 0; i < 9u; i++, n += strlen(n) + 1)
            ok &= !strcmp(n, WANT[i]);
    }
    bad += check("DESC of DRUM KIT: STD 66 10 77 80 10 66 55 77 (1..3 as the kits they play)", ok);
    for (r = 1, ok = ed_eng(TSEL) == ENGI_DRUM; r < 4u; r++) {
        uint32_t u = r + 8192u;
        a[0] = P_E0 > 127u ? 2 : 0; a[1] = P_E0 & 127u; a[2] = u & 127u; a[3] = u >> 7;
        request(ED_SET, a, 4);
        ok &= TSEL->p[P_E0] == MAP[r] && (int32_t)(host_wire[7] | host_wire[8] << 7) - 8192 == MAP[r];
    }
    bad += check("SET of DRUM KIT 1..3 lands on 66 10 77 (and replies it)", ok);
    a[k++] = 5; a[k++] = ENGI_DRUM;                        /* slot U06, DRUM, KIT 3 (H+CYM) */
    memcpy(a + k, "OLDKIT", 6); k += 6; a[k++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        uint32_t u = (uint32_t)((i == P_E0 ? 3 : param_desc_of(ENGI_DRUM, i)->def) + 8192);
        a[k++] = u & 127u; a[k++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) { a[k++] = 0; a[k++] = 0; }
    request(ED_UP_PUT, a, k);
    up_values(up_rec(5), got);
    bad += check("a user preset of DRUM KIT 3 (H+CYM) loads as 77", up_used(5) && got[P_E0] == 8);
    {   /* a project whose DRUM tracks hold KIT 0..3: loaded (and bounded, as from flash) as STD 66 10 77 */
        static project_t p;
        for (r = 0; r < NTRK; r++) {
            set_engine_of(&trk[r], ENGI_DRUM); apply_preset_to(&trk[r], 0);
            trk[r].engine = trk[r].eng_req;
        }
        project_capture(&p);
        for (r = 0; r < NTRK; r++) p.t[r].p[P_E0] = (int16_t)r;
        p.sum = proj_sum(&p);
        ok = !project_restore_runtime(&p);
        for (r = 0; r < NTRK; r++) ok &= trk[r].p[P_E0] == (r < 4u ? MAP[r] : r);   /* (4..: kits as they are) */
        for (r = 0; r < NTRK; r++) p.t[r].p[P_E0] = (int16_t)r;
        proj_bound(&p);
        for (r = 0; r < NTRK; r++) ok &= p.t[r].p[P_E0] == (r < 4u ? MAP[r] : r);
        bad += check("a project of DRUM KIT 1..3 loads as 66 10 77 (STD stays)", ok);
    }
    return bad;
}

/* (Skiff phase 6) the light tracks (past NHEAVY: 5 and 6 of six) play no PHYS and no GRAIN, whichever way it is asked:
 * the editor's SET of G_ENGSEL and PRESET, a user preset (UP_LOAD), the PRESETS list, the ENG knob; nothing changes and
 * it is said. The last track is light only with six: with four, it plays them all */
static int light_tracks(void)
{
    static uint8_t a[4];
    uint32_t k = NTRK - 1u, light = k >= NHEAVY, e0, p0, i, n, total, e, ok;
    int bad = 0;
    reset();
    sel_set((uint8_t)k);
    e0 = TSEL->eng_req; p0 = TSEL->preset;
    a[0] = 1; a[1] = G_ENGSEL; a[2] = (8192u + ENGI_PHYS) & 127u; a[3] = (8192u + ENGI_PHYS) >> 7;
    request(ED_SET, a, 4);
    ok = light ? TSEL->eng_req == e0 && TSEL->preset == p0 && strstr(ui.msg, "PHYS") && strstr(ui.msg, "T1-T4 ONLY")
               : TSEL->eng_req == ENGI_PHYS;
    bad += check("the last track: SET G_ENGSEL PHYS refused when light (the sound unchanged, said)", ok);
    reset();
    sel_set((uint8_t)k);
    e0 = TSEL->eng_req; p0 = TSEL->preset;
    a[0] = ENGI_GRAIN; a[1] = 1;
    request(ED_PRESET, a, 2);
    bad += check("  PRESET of GRAIN: refused, not its preset either (light)",
                 light ? TSEL->eng_req == e0 && TSEL->preset == p0 : TSEL->eng_req == ENGI_GRAIN && TSEL->preset == 1u);
    reset();                                               /* a GRAIN sound stored from track 1, loaded on the last */
    set_engine_of(&trk[0], ENGI_GRAIN);
    (void)up_store(5, "GRN");                             /* (3: RAM only, this build has no flash) */
    ok = up_used(5) && up_engine(5) == ENGI_GRAIN;
    sel_set((uint8_t)k);
    e0 = TSEL->eng_req;
    a[0] = 5;
    request(ED_UP_LOAD, a, 1);
    bad += check("  a user preset of GRAIN: refused (light)", ok && (light ? TSEL->eng_req == e0 : TSEL->eng_req == ENGI_GRAIN));
    reset();
    set_engine_of(&trk[0], ENGI_PHYS);
    (void)up_store(6, "PHY");
    sel_set((uint8_t)k);
    preset_pos(&total);
    for (n = 0, ok = 1; n < total; n++) {
        uint32_t pk;
        e = preset_at(n, &pk);
        ok &= eng_allowed(k, e == NENGINES ? up_engine(pk) : e);
    }
    for (i = 0, e = TSEL->eng_req; i < 2u * NENG_SHOWN; i++)
        ok &= eng_allowed(k, e = eng_step_for(k, e, i < NENG_SHOWN ? 1 : -1));
    for (i = 0; i < NTRK; i++)
        ok &= eng_allowed(i, trk_def_engine(i));
    bad += check("  the PRESETS list and the ENG knob there: only what it may play; every power-on sound allowed", ok);
    return bad;
}

/* (Skiff) a factory preset's other values (core.h preset_t more): sound parameters only (below P_E0, never the
 * track's own: param_kept), in range, the list ending at the first id 0; the editor's PRESET (ui.c apply_preset_to)
 * sets them, and the next preset, one without any, puts them back to their defaults */
static int preset_more(void)
{
    static uint8_t a[4];
    const preset_t *w = 0, *p;
    uint32_t e, k, i, n = 0, ok = 1, we = 0, wk = 0;
    int bad = 0;
    for (e = 0; e < NENGINES; e++)
        for (k = 0; k < ENGINES[e]->npresets; k++)
            for (p = &ENGINES[e]->presets[k], i = 0; i < NELEM(p->more); i++) {
                uint32_t id = p->more[i].id;
                if (!id)
                    ok &= i + 1u == NELEM(p->more) || !p->more[i + 1u].id;   /* nothing after the end */
                else {
                    ok &= id < P_E0 && !param_kept(id) && p->more[i].v >= TP[id].min && p->more[i].v <= TP[id].max;
                    n++;
                    if (!w && id == P_LD_PIT)            /* the first with a vibrato */
                        w = p, we = e, wk = k;
                }
            }
    bad += check("factory presets' other values: sound parameters, not the track's own, in range", ok && n && w);
    if (!w)
        return bad;
    reset();
    a[0] = (uint8_t)we; a[1] = (uint8_t)wk;
    request(ED_PRESET, a, 2);
    ok = TSEL->eng_req == we && TSEL->preset == wk;
    for (i = 0; i < NELEM(w->more) && w->more[i].id; i++)
        ok &= TSEL->p[w->more[i].id] == w->more[i].v;
    bad += check("  a load sets them (the first with a vibrato)", ok);
    a[0] = 0; a[1] = 0;                                   /* ANALOG SAW LEAD: none */
    request(ED_PRESET, a, 2);
    ok = TSEL->preset == 0u && !ENGINES[0]->presets[0].more[0].id;
    for (i = 0; i < NELEM(w->more) && w->more[i].id; i++)
        ok &= TSEL->p[w->more[i].id] == TP[w->more[i].id].def;
    bad += check("  the next preset (none of its own) puts them back to their defaults", ok);
    return bad;
}

#ifndef EDITOR_TEST_NO_MAIN                             /* (robust_test.c, fuzz_*.c: this file is their base) */
/* (Skiff W3) TRANSPORT: the state, PLAY (nothing while playing), STOP, REC as its button; PROJ_NAME: the names, SAVE
 * AS (upper case), RENAME (the music's name too), an empty slot refused, refused while the transport won't stop,
 * bad names and ops unanswered */
static int transport_names(void)
{
    static const uint8_t play[] = {1}, stop[] = {2}, rec[] = {4}, two[] = {4, 0}, op5[] = {5}, op0[] = {0},
        save[] = {1, 1, 'm', 'y', ' ', 's', 'o', 'n', 'g'}, ren[] = {2, 1, 't', 'a', 'k', 'e', ' ', '2', 0},
        ren3[] = {2, 3, 'X'}, nil[] = {1, 0}, far[] = {1, 4, 'X'}, ctl[] = {1, 0, 'A', 31},
        lng[] = {1, 0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M'};
    int bad = 0, ok;
    uint32_t n;
    reset();
    ok = request(ED_TRANSPORT, 0, 0) == 12u && !host_wire[5] && !host_wire[6] && !host_wire[8] && !host_wire[9];
    ok &= request(ED_TRANSPORT, play, 1) == 12u && host_wire[8] == 1 && transport_req == 1;
    transport_req = 0; song.playing = 1;
    ok &= request(ED_TRANSPORT, play, 1) == 12u && host_wire[6] == 1 && !host_wire[8] && !transport_req;
    ok &= request(ED_TRANSPORT, stop, 1) == 12u && host_wire[8] == 2 && transport_req == 2;
    transport_req = 0; song.playing = 0; sel_set(2);
    ok &= request(ED_TRANSPORT, rec, 1) == 12u && host_rec_taps == 1 && host_wire[9] == 4 && host_wire[10] == 2;
    ok &= !request(ED_TRANSPORT, op5, 1) && !request(ED_TRANSPORT, op0, 1) && !request(ED_TRANSPORT, two, 2);
    bad += check("TRANSPORT: the state; PLAY (none while playing), STOP, REC as its button; bad ops unanswered", ok);
    song.rec = 0;
    n = request(ED_PROJ_NAME, 0, 0);
    ok = n == 19u && !host_wire[5] && !host_wire[6] && host_wire[7] == 127u && !host_wire[9] && !host_wire[10];
    n = request(ED_PROJ_NAME, save, sizeof save);
    ok &= n == 33u && host_wire[5] == 1 && !host_wire[6] && host_wire[7] == 1 && !host_wire[8] &&
          !memcmp(host_wire + 9, "MY SONG", 8) && !host_wire[17] && !host_wire[18] && host_wire[19] == 1 &&
          !memcmp(host_wire + 20, "MY SONG", 8) && project_used(1);
    TSEL->p[P_LEVEL] = 21;
    ok &= request(ED_PROJ_NAME, 0, 0) == 33u && host_wire[8] == 1;
    n = request(ED_PROJ_NAME, ren, sizeof ren);
    ok &= n == 31u && !host_wire[6] && host_wire[8] == 1 && !memcmp(host_wire + 9, "TAKE 2", 7) && !memcmp(host_wire + 19, "TAKE 2", 7);
    ok &= request(ED_PROJ_NAME, ren3, sizeof ren3) && host_wire[6] == 1;
    ok &= request(ED_PROJ_NAME, nil, sizeof nil) && !host_wire[6] && host_wire[7] == 0 && !host_wire[9] && host_wire[10] == 1 && !host_wire[11];
    bad += check("PROJ_NAME: SAVE AS (upper case), changed since, RENAME (the music's too), an empty slot refused", ok);
    song.playing = 1; host_progress = 0;
    ok = request(ED_PROJ_NAME, save, sizeof save) && host_wire[6] == 1 && host_wire[7] == 0;
    song.playing = 0; host_progress = 1;
    ok &= !request(ED_PROJ_NAME, far, sizeof far) && !request(ED_PROJ_NAME, ctl, sizeof ctl) &&
          !request(ED_PROJ_NAME, lng, sizeof lng) && !request(ED_PROJ_NAME, op0, 1);
    bad += check("PROJ_NAME: refused while the transport won't stop; bad slot, byte, length, op unanswered", ok);
    return bad;
}

/* (Skiff W3) POOL, LIST, PERFORM as the device's controls: the pool's LENs and uses, CLEAR (only unused, stopped);
 * the lists (the entry each track is on, its pattern, the entries), SET, REPS, PLAY, COPY, SONG, CLEAR (stopped); the
 * scenes kept, STORE, RECALL, MUTE (at once stopped, staged playing), CANCEL, LAUNCH; bad requests unanswered */
static void ed_fresh(uint32_t k, uint8_t note)       /* (ui_test's fresh_pattern) */
{
    track_defaults_steps(&trk[k]);
    trk_pid[k] = 0;
    trk[k].step[0] = (step_t){{note}, 1, ST_NOTE, 0, 90};
}
static int song_cmds(void)
{
    uint8_t a[4];
    int bad = 0, ok;
    uint32_t n, p1, p2, p3, p4;
    reset();
    memset(bk_ram, 0xFF, sizeof bk_ram);
    bank_boot();
    lists_clear();
    scene_forget();
    memset(lists.scene, 0, sizeof lists.scene);
    ed_fresh(0, 50); ok = !bank_put(0, 0, 0);        /* T1: A (01), C (02), the track on C */
    ed_fresh(0, 60); ok &= !bank_put(0, 2, 0);
    p1 = ent_pat(0, 0); p2 = ent_pat(0, 2);
    n = request(ED_POOL, 0, 0);
    ok &= n == 136u && host_wire[7 + 2 * p1] == trk[0].p[P_SLEN] && host_wire[8 + 2 * p1] == 1 && host_wire[8 + 2 * p2] == 1 &&
          !host_wire[7 + 2 * 63];
    n = request(ED_LIST, 0, 0);
    ok &= n == 219u && !host_wire[7] && host_wire[8] == 2 && host_wire[9] == 127 && host_wire[10] == p2 &&
          host_wire[11] == p1 + 1u && host_wire[15] == p2 + 1u && !host_wire[13] && host_wire[8 + 35] == 127;
    bad += check("POOL: each pattern's LEN and uses; LIST: the entry each track is on, its pattern, the entries", ok);
    a[0] = 1; a[1] = 0; a[2] = 1; a[3] = (uint8_t)(p1 + 1u);
    ok = request(ED_LIST, a, 4) && !host_wire[6] && host_wire[13] == p1 + 1u;              /* SET B 01 */
    a[0] = 5; a[3] = 3;
    ok &= request(ED_LIST, a, 4) && !host_wire[6] && host_wire[14] == 2;                    /* REPS B 3 */
    a[2] = 3;
    ok &= request(ED_LIST, a, 4) && !host_wire[6] && lists.ent[0][3][1] == 2;               /* REPS on an empty entry: a rest's
                                                                                               * (1.2.1, #12) */
    a[0] = 1; a[3] = 64;
    ok &= request(ED_LIST, a, 4) && host_wire[6] == 1 && !lists.ent[0][3][0];               /* SET a pattern not kept */
    a[0] = 3; a[2] = 0;
    ok &= request(ED_LIST, a, 3) && !host_wire[6] && host_wire[8] == 0 && trk[0].step[0].note[0] == 50;   /* PLAY A */
    a[0] = 2;
    ok &= request(ED_LIST, a, 3) && !host_wire[6] && (p3 = host_wire[11] - 1u) != p1 && pool_used(p3);   /* COPY A */
    a[0] = 6; a[1] = 1;
    ok &= request(ED_LIST, a, 2) && host_wire[7] == 1 && lists.song;                        /* SONG ON */
    bad += check("LIST: SET, REPS (an empty entry's too: a rest), PLAY (at once stopped), COPY, SONG", ok);
    song.playing = 1;
    a[0] = 4; a[1] = 0; a[2] = 1;
    ok = request(ED_LIST, a, 3) && host_wire[6] == 3 && lists.ent[0][1][0];                 /* CLEAR B: playing */
    song.playing = 0;
    ok &= request(ED_LIST, a, 3) && !host_wire[6] && !host_wire[13];
    a[0] = 1; a[1] = 6; a[2] = 0; a[3] = 1;
    ok &= !request(ED_LIST, a, 4);
    a[1] = 0; a[2] = 16;
    ok &= !request(ED_LIST, a, 4);
    a[0] = 5; a[2] = 0; a[3] = 17;
    ok &= !request(ED_LIST, a, 4);
    a[0] = 6; a[1] = 3;                                                                      /* ((2.0) 2: ONCE) */
    ok &= !request(ED_LIST, a, 2);
    a[0] = 9; a[1] = 0;                                                                      /* ((2.0) 7 NEW, 8 COPY+) */
    ok &= !request(ED_LIST, a, 3);
    bad += check("LIST: CLEAR (refused playing, then done); bad track, entry, REPS, SONG, op unanswered", ok);
    a[0] = 1; a[1] = 0; a[2] = 5; a[3] = PAT_REST;
    ok = request(ED_LIST, a, 4) && !host_wire[6] && host_wire[21] == PAT_REST && ent_rest(0, 5);   /* SET F REST */
    song.playing = 1;
    a[0] = 4;
    ok &= request(ED_LIST, a, 3) && !host_wire[6] && !host_wire[21] && !lists.ent[0][5][0];        /* CLEAR it, playing */
    song.playing = 0;
    a[0] = 1; a[3] = PAT_REST + 1u;
    ok &= !request(ED_LIST, a, 4);
    bad += check("(#28) LIST: SET a REST (65), CLEAR it at once (playing too); 66 unanswered", ok);
    ed_fresh(0, 70); ok = !bank_put(0, 4, 0);        /* E: 04, the track on it; then E names A's: 04 unused */
    p4 = ent_pat(0, 4);
    a[0] = 1; a[1] = 0; a[2] = 4; a[3] = (uint8_t)(p3 + 1u);
    ok &= request(ED_LIST, a, 4) && !host_wire[6] && pool_used(p4) && !pool_refs(p4);
    a[0] = 1; a[1] = (uint8_t)p3;
    ok &= request(ED_POOL, a, 2) && host_wire[6] == 2;                                       /* in use */
    song.playing = 1; a[1] = (uint8_t)p4;
    ok &= request(ED_POOL, a, 2) && host_wire[6] == 3 && pool_used(p4);                     /* playing */
    song.playing = 0;
    ok &= request(ED_POOL, a, 2) && !host_wire[6] && !pool_used(p4) && !host_wire[7 + 2 * p4];
    ok &= request(ED_POOL, a, 2) && host_wire[6] == 1;                                       /* nothing to clear */
    a[1] = 64;
    ok &= !request(ED_POOL, a, 2);
    a[0] = 4; a[1] = 0;                                                                      /* ((2.0) 2 EMPTY, 3 CLEAR UNUSED) */
    ok &= !request(ED_POOL, a, 2);
    bad += check("POOL: CLEAR only an unused pattern, stopped; nothing to clear said; bad ids, ops unanswered", ok);
    n = request(ED_PERFORM, 0, 0);
    ok = n == 25u && host_wire[8] == 127 && !host_wire[9] && !host_wire[10] && !host_wire[11];
    a[0] = 1; a[1] = 2;
    ok &= request(ED_PERFORM, a, 2) && host_wire[8] == 2 && host_wire[9] == 4 && scene_used(2);   /* STORE 3 */
    a[0] = 2; a[1] = 4;
    ok &= request(ED_PERFORM, a, 2) && host_wire[6] == 1;                                    /* RECALL 5: empty */
    a[1] = 2;
    ok &= request(ED_PERFORM, a, 2) && !host_wire[6];                                        /* RECALL 3 */
    a[0] = 3; a[1] = 1;
    ok &= request(ED_PERFORM, a, 2) && host_wire[14] == 1 && !host_wire[15] && trk[1].p[P_MUTE];   /* MUTE T2: at once */
    song.playing = 1; a[1] = 2;
    ok &= request(ED_PERFORM, a, 2) && !host_wire[16] && host_wire[17] == 2;                 /* T3: staged */
    a[0] = 4;
    ok &= request(ED_PERFORM, a, 1) && !host_wire[17];                                       /* CANCEL */
    song.playing = 0;
    a[0] = 5; a[1] = 1;
    ok &= request(ED_PERFORM, a, 2) && host_wire[7] == 1 && launch_mode() == 1u;            /* LAUNCH BAR */
    a[1] = 4;
    ok &= !request(ED_PERFORM, a, 2);
    a[0] = 1; a[1] = 16;
    ok &= !request(ED_PERFORM, a, 2);
    a[0] = 3; a[1] = 6;
    ok &= !request(ED_PERFORM, a, 2);
    a[0] = 4; a[1] = 0;
    ok &= !request(ED_PERFORM, a, 2);
    bad += check("PERFORM: scenes kept, STORE, RECALL (empty said), MUTE (at once, staged), CANCEL, LAUNCH; bad unanswered", ok);
    ui_launch = 0;
    return bad;
}

/* (2.0, INFO 58 01 bit 0) LIST2 (82): SONG 0..2 and each REPS byte as kept (16: CHAIN); LIST's REPS 0 CHAIN (one with
 * an entry after it, else rc 1), SONG 2 ONCE, 7 NEW, 8 COPY+ (a chain: all of it); 79's reply as before (SONG on, a
 * chain's REPS 1x); POOL 2 EMPTY (in use too: it and its entries stay), 3 CLEAR UNUSED; bad requests unanswered */
static int song2_cmds(void)
{
    uint8_t a[4];
    int bad = 0, ok;
    uint32_t n, p1, p2, c;
    step_t s;
    reset();
    memset(bk_ram, 0xFF, sizeof bk_ram);
    bank_boot();
    lists_clear();
    ed_fresh(0, 50); ok = !bank_put(0, 0, 0);        /* T1: A (50), B (60), the track on B */
    ed_fresh(0, 60); ok &= !bank_put(0, 1, 0);
    p1 = ent_pat(0, 0); p2 = ent_pat(0, 1);
    a[0] = 5; a[1] = 0; a[2] = 0; a[3] = 0;
    ok &= request(ED_LIST, a, 4) && !host_wire[6] && lists.ent[0][0][1] == REPS_CHAIN && !host_wire[12];   /* A on CHAIN: */
    n = request(ED_LIST2, 0, 0);                                                                            /* 79 says 1x */
    ok &= n == 411u && host_wire[12] == REPS_CHAIN && !host_wire[16] && !host_wire[7];   /* ((#30) 4 bytes an entry) */
    a[2] = 2;
    ok &= request(ED_LIST, a, 4) && host_wire[6] == 1 && !lists.ent[0][2][1];             /* an empty entry can't chain */
    lists.ent[0][15][0] = (uint8_t)(p1 + 1u);
    a[2] = 15;
    ok &= request(ED_LIST, a, 4) && host_wire[6] == 1 && !lists.ent[0][15][1];            /* nor P: none after it */
    lists.ent[0][15][0] = 0;
    a[0] = 6; a[1] = 2;
    ok &= request(ED_LIST, a, 2) && host_wire[7] == 1 && lists.song == SONG_ONCE &&         /* SONG ONCE: 79 says on, */
          request(ED_LIST2, 0, 0) && host_wire[7] == SONG_ONCE;                              /* 82 ONCE */
    bad += check("(2.0) LIST REPS 0 CHAIN (not an empty entry, nor P), SONG 2 ONCE; LIST2 says them as kept, LIST as before",
                 ok);
    a[0] = 7; a[1] = 0; a[2] = 2;
    ok = request(ED_LIST, a, 3) && !host_wire[6] && (c = host_wire[15]) != 0u && pool_used(c - 1u) && c - 1u != p1 &&
         c - 1u != p2;                                                                       /* NEW in C */
    a[0] = 8; a[2] = 0;
    ok &= request(ED_LIST2, a, 3) && !host_wire[6] && host_wire[23] && host_wire[27] && host_wire[24] == REPS_CHAIN &&
          host_wire[23] - 1u != p1 && host_wire[27] - 1u != p2 && ent_pat(0, 0) == p1;       /* COPY+ on A: A-B to D-E */
    a[2] = 5;
    ok &= request(ED_LIST, a, 3) && host_wire[6] == 1;                                       /* COPY+ on an empty entry */
    bad += check("(2.0) LIST NEW (a new pattern in C); COPY+ on a chain (A-B to D-E, a chain again); on an empty one rc 1",
                 ok);
    a[0] = 2; a[1] = (uint8_t)p1;
    ok = request(ED_POOL, a, 2) && !host_wire[6] && pool_used(p1) && !pat_read(p1, bk_rec) &&
         step_get9(&s, bk_rec + BK_STEPS) && !s.n && ent_pat(0, 0) == p1;                   /* EMPTY A's 01: in use */
    lists.ent[0][2][0] = 0;                                                                  /* (C's NEW: unused now) */
    a[0] = 3;
    ok &= request(ED_POOL, a, 1) && !host_wire[6] && !pool_used(c - 1u) && request(ED_POOL, a, 1) && host_wire[6] == 1;
    song.playing = 1;
    a[0] = 2; a[1] = (uint8_t)p2;
    ok &= request(ED_POOL, a, 2) && host_wire[6] == 3;                                       /* playing */
    song.playing = 0;
    a[0] = 3;
    ok &= !request(ED_POOL, a, 2);
    a[0] = 2;
    ok &= !request(ED_POOL, a, 1);
    a[0] = 8; a[1] = 0; a[2] = 0; a[3] = 1;
    ok &= !request(ED_LIST, a, 4);
    bad += check("(2.0) POOL EMPTY (in use too), CLEAR UNUSED (then nothing unused), playing refused; bad unanswered", ok);
    {   /* ((2.0, #30) INFO 58 01 bit 1) LIST2: each entry's sound after its REPS byte; op 9 SOUND puts one on (an engine
         * + 1 and its preset; 64 + a user slot), 0 0 takes it off; rc 1 for an empty entry or a sound it can't carry */
        uint8_t b[5] = {9, 0, 0, 1, 2};               /* (T1 A: ANALOG's preset 3) */
        ok = request(ED_LIST2, b, 5) && !host_wire[6] && lists.snd[0][0][0] == 1u && lists.snd[0][0][1] == 2u &&
             host_wire[13] == 1u && host_wire[14] == 2u && strstr(ui.msg, "T1 A SOUND SQR BASS");
        up_store(3, "EDSND");                         /* (a user sound in slot 4) */
        b[2] = 1; b[3] = 64 + 3; b[4] = 0;
        ok &= request(ED_LIST2, b, 5) && !host_wire[6] && lists.snd[0][1][0] == (0x80u | 3u) && !lists.snd[0][1][1] &&
              host_wire[17] == 64u + 3u && !host_wire[18] && strstr(ui.msg, "T1 B SOUND EDSND");
        bad += check("(2.0, #30) LIST2: an entry's sound after its REPS; op 9 puts a preset (1 2) or a user sound (64 + "
                     "slot) on, said", ok);
        b[2] = 5; b[3] = 1; b[4] = 0;                 /* F: empty */
        ok = request(ED_LIST2, b, 5) && host_wire[6] == 1u && !lists.snd[0][5][0];
        b[2] = 0;
        b[3] = 63;                                    /* (no engine 62) */
        ok &= request(ED_LIST2, b, 5) && host_wire[6] == 1u;
        b[3] = 2;                                     /* (DIGITAL, retired) */
        ok &= request(ED_LIST2, b, 5) && host_wire[6] == 1u;
        b[3] = 1; b[4] = 127;                         /* (no preset 128) */
        ok &= request(ED_LIST2, b, 5) && host_wire[6] == 1u;
        b[3] = 64 + 20; b[4] = 0;                     /* (slot 21 empty) */
        ok &= request(ED_LIST2, b, 5) && host_wire[6] == 1u;
        lists.ent[4][0][0] = (uint8_t)(p1 + 1u);      /* (T5 A: a light track's entry, PHYS) */
        b[1] = 4; b[3] = ENGI_PHYS + 1u;
        ok &= request(ED_LIST2, b, 5) && host_wire[6] == 1u && lists.snd[0][0][0] == 1u && !lists.snd[4][0][0];
        b[1] = 0; b[3] = 0; b[4] = 0;
        ok &= request(ED_LIST2, b, 5) && !host_wire[6] && !lists.snd[0][0][0] && !lists.snd[0][0][1] && strstr(ui.msg, "T1 A NO SOUND");
        ok &= !request(ED_LIST2, b, 4) && !request(ED_LIST, b, 5) && request(ED_LIST, 0, 0) == 219u;
        bad += check("  refused (rc 1): an empty entry, no such engine, DIGITAL, no such preset or user sound, PHYS on a "
                     "light track; 0 0 takes it off; LIST has no op 9, its reply as before", ok);
        lists.ent[4][0][0] = 0;
        up_put(3, 0);
    }
    lists.song = 0;
    return bad;
}

/* ---- (Skiff #27) COLORS (39): THEME CUSTOM's colours; CUSTOM in THEME's list (UI_PALETTES, UI_SET, UI_STATE, MENU) ---- */
#define CPAL_N 11u
static const uint16_t CC[CPAL_N] = {0x0841, 0x2104, 0xFFFF, 0xF800, 0x07E0,                  /* BG SURF TEXT THEME ACCENT */
                                    0x001F, 0xF81F, 0xFFE0, 0x07FF, 0x8410, 0xFC00};         /* the six tracks' */
static uint32_t colors_req(uint32_t op, uint32_t look, const uint16_t *c, uint32_t n)   /* -> rc (99: no reply / wrong) */
{
    uint8_t a[2 + 26], raw[22];
    uint32_t i, k = 0;
    if (op < 128u) a[k++] = (uint8_t)op;
    if (op <= 1u) {
        a[k++] = (uint8_t)look;
        for (i = 0; i < CPAL_N; i++) { raw[2 * i] = (uint8_t)c[i]; raw[2 * i + 1] = (uint8_t)(c[i] >> 8); }
        k += pack7(raw, sizeof raw, a + k);
    }
    k = request(ED_COLORS, a, n < k ? n : k);
    return k == 5u + 3u + 26u + 1u && host_wire[4] == ED_COLORS ? host_wire[5] : 99u;
}
static int colors_are(const uint16_t *c)                    /* the reply's colours (pack7: 22 bytes, low byte first) */
{
    uint8_t raw[22];
    uint32_t i;
    if (ed_unpack7(host_wire + 8, 26, raw, sizeof raw) != sizeof raw) return 0;
    for (i = 0; i < CPAL_N; i++)
        if ((raw[2 * i] | raw[2 * i + 1] << 8) != c[i]) return 0;
    return 1;
}
static int tokens_are(const uint16_t *c)                    /* the screen's tokens: c, the rest derived from it */
{
    uint32_t k, ok = T_BG == c[0] && T_SURF == c[1] && T_TEXT == c[2] && T_THEME == c[3] && T_ACCENT == c[4] &&
        T_MID == ux_mix(c[0], c[2], UI_MID_PCT) && T_SEL == ux_mix(c[0], c[3], UI_SEL_PCT) && T_INK == c[0] &&
        T_RAISE == ux_mix(c[1], c[2], UI_RAISE_PCT) && T_REC == (ux_luma(c[0]) > 128u ? UI_REC_LIGHT : UI_REC_DARK);
    for (k = 0; k < UI_NTRK; k++)
        ok &= T_TRK(k) == c[5 + k];
    return ok;
}
static int tokens_builtin(uint32_t p)                       /* palette p's own */
{
    uint16_t c[CPAL_N];
    uint32_t k;
    c[0] = UI_PALETTES[p].bg; c[1] = UI_PALETTES[p].surf; c[2] = UI_PALETTES[p].text; c[3] = UI_PALETTES[p].theme;
    c[4] = UI_PALETTES[p].accent;
    for (k = 0; k < UI_NTRK; k++) c[5 + k] = UI_TRACKS[p][k];
    return tokens_are(c) && ux.crypt == (p == UI_CRYPT_INDEX) && ux.neon == (p == UI_NEON_INDEX) && ux.quark == (p == UI_QUARK_INDEX);
}
static uint32_t palettes_listed(void)                       /* UI_PALETTES: the count; the last name CUSTOM or not */
{
    uint32_t n = request(ED_UI_PALETTES, 0, 0), p = 6, k = 0;
    if (n < 8u) return 99u;
    while (p < n - 1u && k + 1u < host_wire[5]) { while (host_wire[p]) p++; p++; k++; }
    return host_wire[5] | (uint32_t)!strcmp((const char *)host_wire + p, "CUSTOM") << 8;
}
static int custom_colors(void)
{
    int bad = 0, ok;
    uint16_t bad_c[CPAL_N];
    uint8_t a[4] = {0, 0, 0, 0};
    menu_item_t it;
    sks_t p;
    uint32_t i;
    reset();
    ok = colors_req(255, 0, 0, 0) == 0 && host_wire[6] == 0 && host_wire[7] == 0;
    ok &= palettes_listed() == NPALETTES && menu_n(MI_COLOR) == NPALETTES && (request(ED_UI_STATE, 0, 0), host_wire[5] & ED_UI_COLORS);
    bad += check("(#27) COLORS: none stored (flags 0), the 22 bytes in pack7; THEME lists its six; UI_STATE's caps say COLORS", ok);

    ok = colors_req(0, UI_QUARK_INDEX, CC, 99) == 0 && tokens_are(CC) && ux.quark && !ux.neon && settings.palette == 0u &&
         host_wire[6] == 0 && !CPAL_BYTE && !favorites.factory[14][8] && ui.force;
    bad += check("(#27) COLORS op 0: shown at once in its look (QUARK's widgets, the colours as sent), nothing stored", ok);
    ok = colors_req(3, 0, 0, 1) == 0 && tokens_builtin(0) && settings.palette == 0u;
    bad += check("(#27) COLORS op 3: the preview dropped, SKIFF again", ok);

    ok = colors_req(1, UI_NEON_INDEX, CC, 99) == 3 && host_wire[6] == 3 && host_wire[7] == UI_NEON_INDEX && colors_are(CC) &&
         tokens_are(CC) && ux.neon && settings.palette == UI_NEON_INDEX && CPAL_BYTE == (3u | UI_NEON_INDEX << 2) &&
         favorites.factory[14][8] == (CC[0] & 255u) && favorites.factory[14][9] == CC[0] >> 8 &&
         favorites.factory[14][28] == (CC[10] & 255u) && favorites.factory[14][29] == CC[10] >> 8 &&
         !favorites.factory[14][30] && !favorites.factory[14][7];
    bad += check("(#27) COLORS op 1: stored ([14][8..29], low byte first; [15][14] 3 | look << 2) and in use (rc 3: RAM only here)", ok);
    ok = palettes_listed() == (NPALETTES + 1u | 256u) && (request(ED_UI_STATE, 0, 0), host_wire[6] == NPALETTES);
    menu_desc(0, &it);
    ok &= it.max == (int32_t)NPALETTES && it.value == (int32_t)NPALETTES && !strcmp(it.names[NPALETTES], "CUSTOM") &&
          menu_get(MI_COLOR) == NPALETTES && !strcmp(menu_vname(MI_COLOR, NPALETTES), "CUSTOM");
    bad += check("(#27) THEME CUSTOM, last while stored: UI_PALETTES 7 names, UI_STATE and MENU_DESC say it is in use", ok);

    a[0] = 0; a[1] = 1; request(ED_UI_SET, a, 2);           /* HI-CON */
    ok = host_wire[5] == 3 && settings.palette == 1u && tokens_builtin(1) && cpal_stored() && !cpal_on() &&
         colors_req(255, 0, 0, 0) == 0 && host_wire[6] == 1 && host_wire[7] == UI_NEON_INDEX && colors_are(CC) &&
         menu_n(MI_COLOR) == NPALETTES + 1u;
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);             /* CUSTOM again: in its look */
    ok &= host_wire[5] == 3 && settings.palette == UI_NEON_INDEX && cpal_on() && tokens_are(CC) && ux.neon;
    menu_put(MI_COLOR, UI_QUARK_INDEX);                     /* the MENU's THEME: QUARK, then a step on is CUSTOM */
    ok &= settings.palette == UI_QUARK_INDEX && tokens_builtin(UI_QUARK_INDEX) && menu_step(MI_COLOR, 1) == NPALETTES;
    menu_put(MI_COLOR, menu_step(MI_COLOR, 1));
    ok &= cpal_on() && settings.palette == UI_NEON_INDEX && tokens_are(CC) && menu_step(MI_COLOR, 1) == 0u &&
          menu_step(MI_COLOR, -1) == UI_QUARK_INDEX;
    bad += check("(#27) THEME: CUSTOM and back by UI_SET and the MENU (it wraps round seven), the set kept, CUSTOM in its look", ok);

    for (i = 0; i < CPAL_N; i++) bad_c[i] = (uint16_t)~CC[i];
    ok = colors_req(1, 0, bad_c, 1) == 1 && colors_req(1, 0, bad_c, 2 + 26 - 1) == 1 &&
         colors_req(1, NPALETTES, bad_c, 99) == 1 && colors_req(4, 0, 0, 1) == 1;
    {
        uint8_t big[2 + 26 + 1] = {2, 0};
        ok &= request(ED_COLORS, big, 2) && host_wire[5] == 1;   /* (op 2 with a byte more: not a remove) */
        big[0] = 1;
        ok &= request(ED_COLORS, big, sizeof big) && host_wire[5] == 1;   /* (a byte more than 22 in pack7) */
        big[2] = 0x7F;                                       /* (the last group's mask: a top bit with no byte) */
        big[2 + 24] = 0x7F;
        ok &= request(ED_COLORS, big, 2 + 26) && host_wire[5] == 1;
    }
    ok &= colors_are(CC) && cpal_on() && tokens_are(CC) && CPAL_BYTE == (3u | UI_NEON_INDEX << 2);
    bad += check("(#27) COLORS: a wrong length, look, op or pack7 answers rc 1, nothing changed", ok);

    settings_export(&p);                                    /* the settings record a unit keeps */
    ok = p.palette == palette_to_stored(UI_NEON_INDEX) && p.s.cpal == CPAL_BYTE &&
         !memcmp(p.cpal_cols, &favorites.factory[14][8], 22);   /* ((2.0) SKS1: CPAL_BYTE and the colours by name) */
    memset(&favorites, 0, sizeof favorites); memset(&settings, 0, sizeof settings); settings_init();
    ok &= tokens_builtin(0) && settings_import(&p, sizeof p) == 1 && settings_init() == 0 && cpal_on() && tokens_are(CC) &&
          ux.neon;
    bad += check("(#27) the set kept with the settings: its THEME field the look, read back in use", ok);

    ok = colors_req(2, 0, 0, 1) == 3 && host_wire[6] == 0 && !CPAL_BYTE && settings.palette == UI_NEON_INDEX &&
         tokens_builtin(UI_NEON_INDEX) && palettes_listed() == NPALETTES && menu_n(MI_COLOR) == NPALETTES;
    a[1] = NPALETTES; request(ED_UI_SET, a, 2);
    ok &= host_wire[5] == 1 && settings.palette == UI_NEON_INDEX;
    bad += check("(#27) COLORS op 2: removed, the look's own colours; CUSTOM gone from THEME (UI_SET 6: rc 1)", ok);

    colors_req(1, UI_CRYPT_INDEX, CC, 99);
    ui_ph_line = 1;
    settings_reset();
    ok = settings.palette == UI_SKIFF_INDEX && tokens_builtin(0) && cpal_stored() && !cpal_on() && !ui_ph_line;
    bad += check("(#27) RESET SETTINGS: THEME SKIFF, the set kept (CUSTOM still listed), PLAYHEAD THEME", ok);

    {   /* ((2.0) another firmware's settings: its bytes where the set lives are no set; settings_persist.c clears them) */
        static persist_t f;
        memset(&f, 0, sizeof f);
        f.magic = PERSIST_MAGIC; f.palette = palette_to_stored(UI_QUARK_INDEX); f.panel = panel;
        f.favorites.factory[15][14] = 3u | UI_QUARK_INDEX << 2;   /* (no Skiff mark at [15][24]) */
        memset(&f.favorites.factory[14][8], 0x5A, 22);
        settings_import(&f, (int)sizeof f);
        theme_apply();
        ok = !cpal_stored() && !CPAL_COLS[0] && tokens_builtin(UI_QUARK_INDEX) && menu_n(MI_COLOR) == NPALETTES &&
             colors_req(255, 0, 0, 0) == 0 && host_wire[6] == 0;
    }
    bad += check("(#27) another firmware's settings: no set read from its bytes", ok);
    return bad;
}

int main(void)
{
    int bad = preferences() + motion_locks() + wide_ids() + framing() + uart_recovery() + steps() + samples() + smp_mix() + song_protocol() + malformed_saves() +
              fm6_patches() + user_preset_roundtrip() + live_sync() + usb_burst() + menu_protocol() + drum_kit_retired() +
              light_tracks() + preset_more() + transport_names() + song_cmds() + song2_cmds() + custom_colors();
    printf("%s\n", bad ? "EDITOR TEST FAILED" : "editor test passed");
    return bad != 0;
}
#endif
