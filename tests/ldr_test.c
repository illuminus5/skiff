/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the Felucca update loader (firmware/loader/ldr_core.c): a fake
 * host serves a package over the SysEx protocol, the "device" flash starts as
 * another package's flash.bin (as if that firmware were installed).
 *   ldr_test OLD.fwsc NEW.fwsc
 * With -DBOARD_SMK37=1 (packages from fm1pkg_make.py --board smk37; after genkigenki's SMK-37 layer): the SMK-37 Pro
 * windows (app area up to 0x9D000, records 0x9D000..0xBFFFF) and the protected ranges BTIF / USRTRIM
 * 0xC0000..0xC1FFF, USR 0xF4000..0xFDFFF and key_mac 0xFF000 never touched, even with a valid-looking record in them.
 * Both boards: the app's header sector (0x4000) erased before any other write and written last, a write cut short
 * leaving it erased and the next session completing it (the SMK's boot stage prefers a valid app over the record).
 *   ldr_test OLD.fwsc STOCK.fwsc stock      M-VAVE's own package (V15 on the FM-1, V016 on the SMK: 36 interleave
 * blocks) the official flash.bin's app area written over OLD's, with the device head the stock package's (its chip
 * key): nothing outside the app area, the header last (the SMK app stages this loader for M-VAVE's packages, ota.c).
 * SMK: the start record (loader/ldr_crumb.h) through a cut and the next start, and the info queries 0x7B / 0x7C. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if BOARD_SMK37
#define FELUCCA_ID "ota-SMK-37 Pro_700"
#define T_APP_HI 0x9D000u                   /* end of the app area the loader writes */
#define T_REC 0xBFF00u                      /* the SMK build's update record (ota.c OTA_RES) */
#define T_REC2 0xAEF00u                     /* a record elsewhere in the VM (the stock app's staging is not fixed) */
#define T_ERASE_LO 0x9D000u                 /* records: the VM only */
#define T_ERASE_HI 0xC0000u                 /* nothing erased at or above (BTIF, USRTRIM, ...) */
#define T_DATA_LO 0u                        /* no store range skipped (the sample and the pool are in USRFLASH) */
#define T_DATA_HI 0u
#define T_USER 0xC2000u                     /* user sample data (USR1), outside the record window */
#else
#define FELUCCA_ID "ota-FM-1_900"
#define T_APP_HI 0x93000u
#define T_REC 0xE4F00u
#define T_REC2 0xE8F00u                     /* the stock firmware's */
#define T_ERASE_LO 0x4000u
#define T_ERASE_HI 0xFC000u
#define T_DATA_LO 0x97000u
#define T_DATA_HI 0xE0000u
#define T_USER 0xA0000u
#endif
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_LOADER 1                    /* (ota.c as the loader has it) */

static uint8_t nor[0x100000], *logical;
static size_t logical_len;
static uint8_t rx[1024];
static uint32_t rx_len, rx_full, now_ms, requests, erases, bad_range, record_cleared, f0_asked;
static uint32_t flip_addr = 0xFFFFFFFFu, flip_seen, flip_on;   /* serve addr's flip_on-th reply with a byte changed */
static uint32_t seq, head_erase, head_prog, other_first, other_last;   /* the order of the app area's erases and writes */
static uint32_t progs, cut_at;                     /* writes so far; cut_at: the flash stops taking them after that many */
static uint32_t cut_off;                           /* the write the cut refused */
static uint8_t info_reply[33], info_cmd;           /* (SMK) the last 0x7B / 0x7C reply */

static uint32_t pack7(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 8;
        while (nb >= 7) { out[o++] = acc & 0x7F; acc >>= 7; nb -= 7; }
    }
    if (nb) out[o++] = acc & 0x7F;
    return o;
}
static uint32_t unpack7h(const uint8_t *in, uint32_t n, uint8_t *out)
{
    uint32_t acc = 0, nb = 0, o = 0;
    while (n--) {
        acc |= (uint32_t)*in++ << nb;
        nb += 7;
        if (nb >= 8) { out[o++] = (uint8_t)acc; acc >>= 8; nb -= 8; }
    }
    return o;
}

static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    uint8_t u[64], m[600];
    uint32_t addr, len, i, s = 0;
    unpack7h(p + 1, n - 2, u);
    if (u[2] == 0x11) return 0;
    if (u[2] == 0x7B || u[2] == 0x7C) { info_cmd = u[2]; memcpy(info_reply, u + 6, 33); return 0; }
    addr = u[7] | u[8] << 8 | u[9] << 16 | (uint32_t)u[10] << 24;
    len = u[11] | u[12] << 8 | u[13] << 16;
    requests++;
    memcpy(m, "\x00\x59\x30", 3);
    m[3] = (uint8_t)(len + 8); m[4] = (uint8_t)((len + 8) >> 8); m[5] = 0; m[6] = 0;
    memcpy(m + 7, &addr, 4);
    m[11] = (uint8_t)len; m[12] = (uint8_t)(len >> 8); m[13] = 0;
    if (addr >= 0xE0000000u) {
        f0_asked += addr == 0xF0000000u;
        memset(m + 14, 0, len);
        memcpy(m + 14, "success", 8);
    } else {
        if (addr + len > logical_len) { printf("read past the package %#x\n", addr); exit(1); }
        memcpy(m + 14, logical + addr, len);
        if (addr == flip_addr && ++flip_seen == flip_on)
            m[14 + len / 2u] ^= 0x10;                /* a transfer error the frame checksum does not see */
    }
    for (i = 6; i < 14 + len; i++) s += m[i];
    m[14 + len] = (uint8_t)~s;
    rx_len = pack7(m, 15 + len, rx);
    rx_full = 1;
    return 0;
}
static int ota_frame_get(const uint8_t **p, uint32_t *n) { if (!rx_full) return 0; *p = rx; *n = rx_len; return 1; }
static void ota_frame_done(void) { rx_full = 0; }
static uint32_t ota_now_ms(void) { return now_ms; }
static void ota_idle(void) { now_ms++; }
static int ota_erase(uint32_t off) { (void)off; return -1; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return -1; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { (void)parm; }
#include "../firmware/src/ota.c"
#if BOARD_SMK37
#include "../firmware/loader/ldr_crumb.h"
static ldr_crumb_t crumb;                          /* (loader.c keeps it at FM1_LDR_CRUMB_ADDR) */
#define LDR_STAGE(st, sec) ldr_crumb_stage(&crumb, (st), (sec), now_ms)
static void ota_reply_info(void) { ota_send_msg(0x7B, (const uint8_t *)&crumb, 32); }   /* (as loader.c) */
static void ota_reply_clocks(void) { static const uint8_t z[32] = {0x5A}; ota_send_msg(0x7C, z, 32); }
static void host_ask(uint32_t cmd)                 /* the host's query: 00 59 cmd, no body */
{
    uint8_t m[7] = {0, 0x59, (uint8_t)cmd, 0, 0, 0, 0xFF};
    rx_len = pack7(m, 7, rx);
    rx_full = 1;
    info_cmd = 0;
    ota_service();
}
#endif

static int ldr_fread(uint32_t off, void *p, uint32_t n) { memcpy(p, nor + off, n); return 0; }
static int ldr_erase(uint32_t off)
{
    if (off < 0x4000u || off >= T_ERASE_HI || (off & 0xFFFu) || (off >= T_DATA_LO && off < T_DATA_HI) ||
        (off >= T_APP_HI && off < T_ERASE_LO)) bad_range++;
    if (off < 0x4000u) { printf("ERASE IN THE HEAD %#x\n", off); exit(1); }
    erases++;
    seq++;
    if (off == 0x4000u && !head_erase) head_erase = seq;
    if (off > 0x4000u && off < T_APP_HI) { other_first = other_first ? other_first : seq; other_last = seq; }
    memset(nor + off, 0xFF, 0x1000);
    return 0;
}
static int ldr_prog(uint32_t off, const void *p, uint32_t n)
{
    const uint8_t *s = p;
    uint32_t i;
    if (off < 0x4000u || off + n > T_APP_HI || ((off & 0xFFu) + n) > 256u) bad_range++;
    if (cut_at && progs >= cut_at) { cut_off = off; return -1; }   /* (the power gone: nothing more is written) */
    progs++;
    seq++;
    if (off < 0x5000u && !head_prog) head_prog = seq;
    if (off >= 0x5000u && off < T_APP_HI) { other_first = other_first ? other_first : seq; other_last = seq; }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static void ldr_record_clear(void) { record_cleared++; }
static void ldr_progress(uint32_t done, uint32_t total) { (void)done; (void)total; }
#include "../firmware/loader/ldr_core.c"
#define FM1_CLK_HOST 1
#include "../firmware/hal/fm1_clk_regs.h"          /* (fm1_sfc_baud_pick: the loader's start sets the app's flash divider) */

static uint8_t *load_logical_b(const char *path, size_t *len, int stock)
{
    FILE *f = fopen(path, "rb");
    uint8_t *raw = malloc(0x200000), *lg;
    size_t n, i, b = 20;
    if (!f) { perror(path); exit(2); }
    n = fread(raw, 1, 0x200000, f);
    fclose(f);
    if (n < 20 * 48 + 0x400) { printf("%s: too short for a package\n", path); exit(2); }
    while (stock && (b + 1) * 48 <= n && b < 64 && raw[b * 48 + 47] == 0x7D)
        b++;                                       /* (M-VAVE's: more blocks, the unused marker 0x7D) */
    lg = malloc(n);
    for (i = 0; i < b; i++) memcpy(lg + i * 47, raw + i * 48, 47);
    memcpy(lg + b * 47, raw + b * 48, n - b * 48);
    *len = n - b;
    free(raw);
    return lg;
}
static uint8_t *load_logical(const char *path, size_t *len) { return load_logical_b(path, len, 0); }

static uint32_t flash_off(const uint8_t *lg)        /* flash.bin offset in the logical image */
{
    uint8_t h[0x400];
    uint32_t i, n;
    memcpy(h, lg, sizeof h);
    ota_jl_enc(h, 0x40);
    n = ota_rd16(h + 8);
    for (i = 0; i < n; i++) {
        uint8_t *e = h + 0x40 + i * 0x50u;
        ota_jl_enc(e, 0x50);
        if (ota_rd16(e) == 0) return ota_rd32(e + 8);
    }
    return 0;
}

static int check(const char *what, int ok) { printf("%-56s %s\n", what, ok ? "ok" : "FAIL"); return ok ? 0 : 1; }

static void put_record_at(uint32_t at)              /* a valid update record (the SPL runs the loader) */
{
    uint8_t r[112] = {0};
    r[2] = 0x0D; r[3] = 0x5A; r[4] = 0x01; r[5] = 0x5A; r[6] = 0x41; r[7] = 0x54;
    ota_wr16(r, ota_crc16(r + 2, 78, 0));
    memcpy(nor + at, r, sizeof r);
}

static uint8_t prot_vm[T_ERASE_HI - T_APP_HI];     /* (stock_test: what lies above the app area, before) */
static uint32_t nfo_stock;
#if BOARD_SMK37
/* the SMK's protected ranges: a pattern, and a valid-looking record in BTIF, USRTRIM and USR */
static const uint32_t PROT[][2] = {{0xC0000u, 0xC2000u}, {0xF4000u, 0xFE000u}, {0xFF000u, 0x100000u}};
static uint8_t prot_img[sizeof nor];
static void protect_fill(void)
{
    uint32_t i, k;
    for (k = 0; k < sizeof PROT / sizeof PROT[0]; k++)
        for (i = PROT[k][0]; i < PROT[k][1]; i++)
            nor[i] = (uint8_t)(i * 13u + 5u);
    memcpy(nor + 0xC1224u, "trimdat", 8);
    put_record_at(0xC1F00u);                       /* USRTRIM sector: must never be erased */
    put_record_at(0xC0F00u);                       /* BTIF */
    put_record_at(0xF4F00u);                       /* USR */
    memcpy(prot_img, nor, sizeof nor);
}
static int protect_ok(void)
{
    uint32_t k;
    for (k = 0; k < sizeof PROT / sizeof PROT[0]; k++)
        if (memcmp(nor + PROT[k][0], prot_img + PROT[k][0], PROT[k][1] - PROT[k][0]))
            return 0;
    return 1;
}
#else
static void protect_fill(void) {}
static int protect_ok(void) { return 1; }
#endif

/* M-VAVE's own package through this loader (the SMK app stages it for one, ota.c): its flash.bin's app area over
 * OLD's, the head the stock package's own (so its chip key), a record pending */
static int stock_test(const char *old_path, const char *stock_path)
{
    size_t olen;
    uint8_t *old = load_logical(old_path, &olen), head[0x4000];
    uint32_t ofo, i;
    int bad = 0, rc;
    logical = load_logical_b(stock_path, &logical_len, 1);
    ofo = flash_off(old);
    nfo_stock = flash_off(logical);
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, logical + nfo_stock, 0x4000);
    memcpy(nor + 0x4000, old + ofo + 0x4000, T_APP_HI - 0x4000);
    protect_fill();
    memcpy(head, nor, sizeof head);
    put_record_at(T_REC);
    memcpy(prot_vm, nor + T_APP_HI, sizeof prot_vm);
    seq = head_erase = head_prog = other_first = other_last = 0;
    erases = 0; bad_range = 0; record_cleared = 0;
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("stock: M-VAVE's package installs", rc == 0);
    bad += check("stock: app area == its flash.bin's",
                 !memcmp(nor + 0x4000, logical + nfo_stock + 0x4000, T_APP_HI - 0x4000));
    bad += check("stock: flash head untouched", !memcmp(nor, head, sizeof head));
    bad += check("stock: the header erased first, written last",
                 head_erase && head_erase < other_first && head_prog > other_last);
    bad += check("stock: no write outside the app area", bad_range == 0);
    for (i = T_APP_HI; i < T_ERASE_HI && (nor[i] == prot_vm[i - T_APP_HI] || (i >= T_REC - 0xF00u && i < T_REC + 0x100u));
         i++) {}
    bad += check("stock: above the app area as it was but the record's sector", i == T_ERASE_HI && nor[T_REC + 6u] == 0xFF);
    bad += check("stock: protected ranges untouched (BTIF, USRTRIM, USR, key_mac)", protect_ok());
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}

int main(int argc, char **argv)
{
    size_t olen;
    static uint8_t user[0x1000];
    uint8_t *old, head[0x4000];
    uint32_t ofo, nfo, i;
    int bad = 0, rc;
    if (argc < 3) { printf("usage: ldr_test OLD.fwsc NEW.fwsc [stock]\n"); return 2; }
    if (argc > 3 && !strcmp(argv[3], "stock"))
        return stock_test(argv[1], argv[2]);
    old = load_logical(argv[1], &olen);
    logical = load_logical(argv[2], &logical_len);
    ofo = flash_off(old);
    nfo = flash_off(logical);
    /* device: OLD installed, plus a valid update record at T_REC (Felucca's step 1; FM-1 0xE4F00) and T_REC2 (FM-1:
     * the stock firmware's 0xE8F00, a first install from it); user sample data that looks like a record */
    memset(nor, 0xFF, sizeof nor);
    memcpy(nor, old + ofo, T_APP_HI);
    protect_fill();
    memcpy(head, nor, sizeof head);
    put_record_at(T_REC);
    put_record_at(T_REC2);
    for (i = 0; i < 0x1000u; i++) nor[T_USER + i] = (uint8_t)(i * 7u);
    put_record_at(T_USER + 0xF00u);
    memcpy(user, nor + T_USER, sizeof user);
    /* a damaged package (one byte deep in the app area): refused before anything is erased */
    {
        size_t at = nfo + 0x50123u;
        logical[at] ^= 0x01;
        erases = 0;
        rc = ldr_session();
        bad += check("damaged package (flash.bin CRC) refused, nothing erased", rc == -12 && erases == 0 &&
                     !memcmp(nor + 0x4000, old + ofo + 0x4000, T_APP_HI - 0x4000) && nor[T_REC + 6u] == 0x41);
        logical[at] ^= 0x01;
        requests = 0;
    }
    /* a sector that comes in different the second time (a transfer error after the check) is read again */
    flip_addr = nfo + 0x20000u; flip_on = 2; flip_seen = 0;
    rc = ldr_session();
    printf("  rc %d, %u requests, %u sector erases\n", rc, requests, erases);
    bad += check("install completes", rc == 0);
    bad += check("a sector changed in transfer after the check is read again", flip_seen == 3);
    flip_addr = 0xFFFFFFFFu;
    bad += check("app area == the new package's flash.bin", !memcmp(nor + 0x4000, logical + nfo + 0x4000, T_APP_HI - 0x4000));
    bad += check("flash head [0, 0x4000) untouched", !memcmp(nor, head, sizeof head));
    bad += check("finish asked (0xF0000000)", f0_asked >= 1);
    bad += check("update records cleared (RAM + flash: ours, the other one)",
                 record_cleared && nor[T_REC + 6u] == 0xFF && nor[T_REC2 + 6u] == 0xFF);
    bad += check("user sample data that looks like a record is kept", !memcmp(nor + T_USER, user, sizeof user));
    bad += check("no write outside the allowed windows", bad_range == 0);
    /* same package again: nothing to erase */
    erases = 0; requests = 0;
    rc = ldr_session();
    bad += check("re-run with the same package erases nothing", rc == 0 && erases == 0);
    /* a package for another chip key is refused before any erase */
    {
        uint8_t save = logical[nfo + 0x4000 + 5];
        logical[nfo + 0x4000 + 5] ^= 0x5A;           /* app area head no longer decrypts */
        memcpy(nor, old + ofo, T_APP_HI);
        erases = 0;
        rc = ldr_session();
        bad += check("foreign key / damaged app head refused, nothing erased", rc == -6 && erases == 0);
        logical[nfo + 0x4000 + 5] = save;
    }
    /* a write cut short must leave no app to start (the SMK's boot stage starts a valid app over a pending record): the
     * header goes first and comes back last, on both boards */
    memcpy(nor, old + ofo, T_APP_HI);
    put_record_at(T_REC);
    seq = head_erase = head_prog = other_first = other_last = 0;
    rc = ldr_session();
    bad += check("the app's header sector erased before any other write",
                 rc == 0 && head_erase && other_first && head_erase < other_first);
    bad += check("... and written last", head_prog > other_last);
    memcpy(nor, old + ofo, T_APP_HI);
    put_record_at(T_REC);
#if BOARD_SMK37
    memset(&crumb, 0xA5, sizeof crumb);            /* (a power-on: RAM as it comes) */
    ldr_crumb_start(&crumb, 0, 0x01, 0, 0);
    bad += check("SMK record: a power-on starts it at 1, nothing before", crumb.starts == 1 && crumb.prev.stage == 0 &&
                 crumb.now.stage == 1 && !crumb.now.warm && crumb.magic == LDR_CRUMB_MAGIC);
#endif
    progs = 0; cut_at = 100;
    rc = ldr_session();
    cut_at = 0;
    for (i = 0x4000u; i < 0x5000u && nor[i] == 0xFFu; i++) {}
    bad += check("a write cut short: the header erased, the record kept",
                 rc == -10 && progs == 100u && i == 0x5000u && nor[T_REC + 6u] == 0x41);
#if BOARD_SMK37
    bad += check("SMK record: the cut run stopped writing, at the sector it was writing",
                 crumb.now.stage == 3 && crumb.now.sector == (cut_off & ~0xFFFu));
    ldr_crumb_start(&crumb, 0, 0x04, 0x20, 0x5D);  /* the next start: a watchdog reset */
    host_ask(0x7B);
    bad += check("SMK record: the next start keeps where the run before stopped (0x7B)",
                 info_cmd == 0x7B && info_reply[0] == 0x04 && info_reply[1] == 0x5D && info_reply[2] == 2 &&
                 ota_rd32(info_reply + 4) == 0x20 && info_reply[8] == 1 && info_reply[20] == 3 &&
                 ota_rd32(info_reply + 24) == (cut_off & ~0xFFFu));
#endif
    rc = ldr_session();
    bad += check("the next session completes it, the record gone", rc == 0 && nor[T_REC + 6u] == 0xFF &&
                 !memcmp(nor + 0x4000, logical + nfo + 0x4000, T_APP_HI - 0x4000));
#if BOARD_SMK37
    bad += check("SMK record: done (stage 5)", crumb.now.stage == 5);
    host_ask(0x7C);
    bad += check("SMK info: the clock query answers its 32 bytes (0x7C)", info_cmd == 0x7C && info_reply[0] == 0x5A);
    ldr_crumb_erase(&crumb, 45000);
    ldr_crumb_erase(&crumb, 310000);
    ldr_crumb_erase(&crumb, 2000);
    bad += check("SMK record: the slowest erase kept (310 ms)", crumb.now.erase_max_ms == 310);
#endif
    bad += check("protected ranges untouched (SMK: BTIF, USRTRIM, USR, key_mac)", protect_ok());
    /* a power-on start finds the boot ROM's flash divider (0xB9 on the SMK, the app's 1): the app's set back */
    bad += check("start: the boot ROM's flash divider 0xB9 under the app's clocks -> the app's 1",
                 fm1_sfc_baud_pick(0xB9u, 0x6u, 0x00010200u) == 1u);
    bad += check("start: already the app's (1), or 0: left", !fm1_sfc_baud_pick(1u, 0x6u, 0x00010200u) &&
                 !fm1_sfc_baud_pick(0u, 0x6u, 0x00010200u));
    bad += check("start: another clock tree (SYS not on PLL2 x 2/3, or HSB not SYS / 2): left",
                 !fm1_sfc_baud_pick(0xB9u, 0x5u, 0x00010200u) && !fm1_sfc_baud_pick(0xB9u, 0x6u, 0x00020200u) &&
                 !fm1_sfc_baud_pick(0xB9u, 0x6u, 0x00000200u));
    printf("%s\n", bad ? "LOADER TEST FAILED" : "loader test passed");
    return bad != 0;
}
