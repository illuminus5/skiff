/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 Pro port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro); Skiff's layout */
/* The SMK-37 Pro flash map (build with -DBOARD_SMK37=1): every 4 KiB sector of the 1 MiB part through the app's
 * allow-list (FL_RANGE_OK / FL_GUARD, fm1_flash_map.h), the update loader's (loader.c) and the RAM driver's own net:
 * none may ever reach the head 0x0-0x3FFF, BTIF / USRTRIM 0xC0000-0xC1FFF, USR 0xF4000-0xFDFFF or key_mac 0xFF000-.
 * Every stored object by its OBJ_* (storage.c, A and B), the owner tag, the pattern pool (bank.c) and the user sample
 * slot (eng_sample.c) inside the allowed windows, none sharing a sector (but the FM6 pair, by design); ranges that wrap
 * or straddle a boundary are refused. bank_test runs the pool itself on this layout (-DBOARD_SMK37=1). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fm1_cc.h"
#if !BOARD_SMK37
#error "build with -DBOARD_SMK37=1"
#endif
#include "fm1_flash_map.h"

static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off) { (void)off; return -1; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { (void)off; (void)src; (void)n; return -1; }
#include "../firmware/src/storage.c"
#define LDR_RANGE_OK(off, n) FL_LDR_RANGE_OK(off, n)       /* what loader.c uses on the SMK */

static int bad;
static void check(const char *what, int ok) { printf("smk flash: %-86s %s\n", what, ok ? "ok" : "FAIL"); bad += !ok; }

static int protected_sector(uint32_t s)
{
    return s < 0x4000u || (s >= 0xC0000u && s < 0xC2000u) || (s >= 0xF4000u && s < 0xFE000u) || s >= 0xFF000u;
}

int main(void)
{
    static uint8_t owner[256];                      /* each sector's owner: 0 none, 1 + OBJ_*, 100 tag, 101 pool, 102 slot */
    uint32_t s, o, c, leak_app = 0, leak_ldr = 0, leak_guard = 0, allowed = 0, ok;
    for (s = 0; s < 0x100000u; s += 0x1000u) {
        if (!protected_sector(s))
            continue;
        leak_app += FL_RANGE_OK(s, 0x1000u) || FL_STORE_OK(s, 0x1000u) || FL_RANGE_OK(s, 256u) || FL_RANGE_OK(s + 0xF00u, 256u);
        leak_ldr += LDR_RANGE_OK(s, 0x1000u) || LDR_RANGE_OK(s + 0xF00u, 256u);
        leak_guard += !FL_PROTECTED(s, 0x1000u) || !FL_PROTECTED(s + 0xF00u, 256u);
    }
    for (s = 0; s < 0x100000u; s += 0x1000u)
        allowed += FL_GUARD(s, 0x1000u);
    check("no protected sector passes the app's allow-list (erase, page)", leak_app == 0);
    check("no protected sector passes the loader's allow-list", leak_ldr == 0);
    check("the RAM driver's own net (FL_PROTECTED) covers every protected sector", leak_guard == 0);
    check("straddling ranges refused: 0xBF000+0x2000, 0xE9000+0x2000, 0x3000+0x2000",
          !FL_GUARD(0xBF000u, 0x2000u) && !FL_GUARD(0xE9000u, 0x2000u) && !LDR_RANGE_OK(0xBF000u, 0x2000u) &&
          !LDR_RANGE_OK(0x3000u, 0x2000u) && FL_PROTECTED(0xBF000u, 0x2000u) && FL_PROTECTED(0x3F00u, 0x200u));
    check("wrapping ranges refused (off + n past 4 GiB)",
          !FL_GUARD(0xBB000u, 0xFFFFF000u) && !LDR_RANGE_OK(0x9D000u, 0xFFFFFF00u) && FL_PROTECTED(0xBB000u, 0xFFFFF000u));
    check("app windows: FL_DATA 0x9D000-0xBAFFF, FL_OTA 0xBB000-0xBFFFF, USRFLASH 0xC2000-0xE9FFF, the marker",
          FL_GUARD(0x9D000u, 0x1000u) && FL_GUARD(0xBA000u, 0x1000u) && FL_GUARD(0xBF000u, 0x1000u) &&
          FL_GUARD(0xC2000u, 0x1000u) && FL_GUARD(0xE9000u, 0x1000u) && FL_GUARD(0xEA000u, 0x1000u) &&
          !FL_GUARD(0xEB000u, 0x1000u) && !FL_GUARD(0x9C000u, 0x1000u) && !FL_GUARD(0xFE000u, 0x1000u) &&
          allowed == (0xC0000u - 0x9D000u + 0xEA000u - 0xC2000u) / 0x1000u + 1u);
    check("the stock marker's sector 0xEA000: the whole sector only (its erase), never a page or a range into it",
          FL_STOCKTAG_OK(0xEA000u, 0x1000u) && !FL_GUARD(0xEA000u, 256u) && !FL_GUARD(0xEAF00u, 256u) &&
          !FL_GUARD(0xE9000u, 0x2000u) && !FL_GUARD(0xEA000u, 0x2000u) && FL_STOCKTAG_OFF == 0xEAFF8u);
    check("loader: app area 0x4000-0x9CFFF, records 0x9D000-0xBFFFF only",
          LDR_RANGE_OK(0x4000u, 0x1000u) && LDR_RANGE_OK(0x9C000u, 0x1000u) && LDR_RANGE_OK(0xBF000u, 0x1000u) &&
          !LDR_RANGE_OK(0xC0000u, 0x1000u) && !LDR_RANGE_OK(0xC2000u, 0x1000u) && FL_REC_HI == 0xC0000u);

    /* every object's sectors, then the tag, the pool and the slot: in their window, each sector one owner */
    ok = 1;
    for (o = 0; o < OBJ_COUNT; o++)
        for (c = 0; c < 2u; c++) {
            uint32_t a = st_sector(o, c), i = a >> 12, mine = o == OBJ_UPFM6 ? 1u + OBJ_FM6BANK : 1u + o;
            ok &= a % 0x1000u == 0u && FL_IN(a, 0x1000u, FL_DATA_LO, ST_OWNER_OFF) && FL_GUARD(a, 0x1000u);
            ok &= !owner[i] || owner[i] == mine;      /* (the retired FM6 bank and OBJ_UPFM6 share a pair, as on the FM-1) */
            owner[i] = (uint8_t)mine;
        }
    for (o = 0; o < OBJ_COUNT; o++)                /* A and B apart */
        ok &= st_sector(o, 0) != st_sector(o, 1);
    check("storage.c: every OBJ_* (A and B) in FL_DATA below the owner tag, no two objects in one sector", ok);
    ok = !owner[ST_OWNER_OFF >> 12] && FL_GUARD(ST_OWNER_OFF, 0x1000u) && ST_OWNER_OFF == 0xAF000u;
    owner[ST_OWNER_OFF >> 12] = 100;
    check("the owner tag 0xAF000: in FL_DATA, its own sector", ok);
    for (ok = 1, s = FL_BANK_LO; s < FL_BANK_HI; s += 0x1000u) {
        ok &= !owner[s >> 12] && FL_GUARD(s, 0x1000u) && FL_IN(s, 0x1000u, FL_SMP_LO, FL_SMP_HI);
        owner[s >> 12] = 101;
    }
    check("the pattern pool: 20 sectors 0xD6000-0xE9FFF in USRFLASH, nobody else's",
          ok && FL_BANK_LO == 0xD6000u && FL_BANK_HI - FL_BANK_LO == 20u * 0x1000u);
    for (ok = 1, s = FL_SLOT_LO; s < FL_SLOT_HI; s += 0x1000u) {
        ok &= !owner[s >> 12] && FL_GUARD(s, 0x1000u) && FL_IN(s, 0x1000u, FL_SMP_LO, FL_SMP_HI);
        owner[s >> 12] = 102;
    }
    check("the user sample slot USR1: 80 KiB 0xC2000-0xD5FFF in USRFLASH, nobody else's",
          ok && FL_SLOT_LO == 0xC2000u && FL_SLOT_HI - FL_SLOT_LO == 0x14000u);
    for (ok = 1, s = 0; s < 0x100000u; s += 0x1000u)  /* every allowed sector has an owner, but the spare and staging */
        if (FL_GUARD(s, 0x1000u) && !owner[s >> 12])
            ok &= (s >= ST_OWNER_OFF + 0x1000u && s < FL_DATA_HI) || FL_IN(s, 0x1000u, FL_OTA_LO, FL_OTA_HI) ||
                  s == FL_STOCKTAG_LO;
    check("the rest of the windows: 11 spare store sectors 0xB0000-0xBAFFF, the OTA staging, the marker", ok);
    check("storage.c: projects from 0x9D000, settings 0xAD000/0xAE000, the autosave 0xA9000/0xAA000",
          st_sector(OBJ_PROJECT0, 0) == 0x9D000u && st_sector(OBJ_SETTINGS, 0) == 0xAD000u &&
          st_sector(OBJ_SETTINGS, 1) == 0xAE000u && st_sector(OBJ_AUTOSAVE, 0) == 0xA9000u);
    check("FL_PLAIN_LO = the VM start 0x9D000", FL_PLAIN_LO == 0x9D000u);
    check("FL_JEDEC_OK: the SMK part 0xEB6014, the FM-1's 0x856014 and (2.2, #96) the UC25HQ80 0xB36014 only",
          FL_JEDEC_OK(0xEB6014u) && FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0u) &&
          !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0xEB6015u) && !FL_JEDEC_OK(0xB36015u));
    printf("%s\n", bad ? "SMK FLASH TEST FAILED" : "smk flash test passed");
    return bad != 0;
}
