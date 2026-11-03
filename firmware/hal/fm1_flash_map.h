/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * SMK-37 Pro port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The flash map: which ranges the RAM flash driver (fm1_flash.h) may erase / program, per board (BOARD_SMK37,
 * fm1_cc.h). Macros and compile-time checks only, so host tests can include it. */
#pragma once
#include <stdint.h>
#include "fm1_cc.h"

/* [off, off + n) inside [lo, hi), without wrapping: off + n can overflow, and
 * the 1 MiB part ignores the high address bits, so a wrapped range lands low. */
#define FL_IN(off, n, lo, hi) ((uint32_t)(off) >= (lo) && (uint32_t)(off) <= (hi) && \
                               (uint32_t)(n) <= (hi) - (uint32_t)(off))
/* the flash parts the storage accepts (JEDEC id, project.c persist_boot); else the unit runs without saving or updating.
 * (2.2, #96) 0xB36014: the UC25HQ80 of newer FM-1s, 1 MiB and the commands of the Puya P25Q80 (0x856014) */
#if BOARD_SMK37
#define FL_JEDEC_OK(id) ((id) == 0xEB6014u || (id) == 0x856014u || (id) == 0xB36014u)   /* the SMK-37's part, the FM-1's */
#else
#define FL_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)   /* (as project.c's FM-1 check) */
#endif

#if BOARD_SMK37
/* ---- SMK-37 Pro (hardware notes: github.com/genkigenki/fm1-to-smk37) ----
 *   0x00000-0x03FFF  head (SPL, isd_config)                                   NEVER
 *   0x04000-0x9CFFF  app area (the stock PRCT end)                            the update loader only (FL_APP)
 *   0x9D000-0xBAFFF  the stock VM: Skiff's store, nine A/B pairs and the owner tag (storage.c)      FL_DATA
 *   0xBB000-0xBFFFF  the stock VM's end: the staged update loader + update record (ota.c)           FL_OTA
 *   0xC0000-0xC0FFF  BTIF (pairing)                                           NEVER
 *   0xC1000-0xC1FFF  USRTRIM: the unit's key / pad calibration ("trimdat")    NEVER (read at boot)
 *   0xC2000-0xE9FFF  USRFLASH: the user sample slot USR1 (eng_sample.c) and the pattern pool (bank.c)  FL_SMP
 *                    (the stock firmware's mapping until the first write there)
 *   0xEA000-0xEAFFF  USRFLASH's last sector: the stock end marker "usrdata" at 0xEAFF8, erased once before the
 *                    first write into FL_SMP (FL_STOCKTAG, storage.c st_stock_release)
 *   0xEB000-0xF3FFF  not in the stock region table: unused
 *   0xF4000-0xFDFFF  USR (the stock firmware's resources: kept for the way back)   NEVER
 *   0xFE000-0xFEFFF  unused
 *   0xFF000-0xFFFFF  key_mac                                                  NEVER
 * An explicit allow-list; the asserts keep each window out of the protected ranges at compile time, and FL_GUARD
 * checks them again in the RAM driver. */
#define FL_HEAD_HI      0x00004000u                /* protected: [0, FL_HEAD_HI) */
#define FL_CAL_LO       0x000C0000u                /* protected: BTIF + USRTRIM */
#define FL_CAL_HI       0x000C2000u
#define FL_USRRES_LO    0x000F4000u                /* protected: the stock USR resources */
#define FL_USRRES_HI    0x000FE000u
#define FL_KEYMAC_LO    0x000FF000u                /* protected: key_mac and above */
#define FL_APP_LO       0x00004000u                /* the update loader: the app area */
#define FL_APP_HI       0x0009D000u
#define FL_REC_LO       0x0009D000u                /* the update loader: where it may drop records (the VM only) */
#define FL_REC_HI       0x000C0000u
#define FL_PLAIN_LO     0x0009D000u                /* the plain XIP window from the VM start */
#define FL_DATA_LO      0x0009D000u                /* Skiff's store (storage.c) */
#define FL_DATA_HI      0x000BB000u
#define FL_OTA_LO       0x000BB000u                /* the staged update loader, ota.c */
#define FL_OTA_HI       0x000C0000u
#define FL_SMP_LO       0x000C2000u                /* USRFLASH: USR1 (eng_sample.c) and the pattern pool (bank.c) */
#define FL_SMP_HI       0x000EA000u
#define FL_SLOT_LO      0x000C2000u                /*   USR1, one 80 KiB sample slot (SMP_USER_SIZE) */
#define FL_SLOT_HI      0x000D6000u
#define FL_BANK_LO      0x000D6000u                /*   the pattern pool's twenty sectors (bank.c BK_NSECT) */
#define FL_BANK_HI      0x000EA000u
/* USRFLASH is the stock firmware's mapping (its settings at 0xC2000, 8 presets from 0xC3000) with its end marker
 * "usrdata\0" at 0xEAFF8: while the marker is there the stock firmware trusts the area and never rebuilds it. Before
 * the first write into USRFLASH the marker's sector is erased (storage.c st_stock_release), so a return to stock
 * rebuilds the factory mapping. Exactly that one sector, nothing else above FL_SMP_HI. */
#define FL_STOCKTAG_LO  0x000EA000u
#define FL_STOCKTAG_HI  0x000EB000u
#define FL_STOCKTAG_OFF 0x000EAFF8u
#define FL_STOCKTAG_OK(off, n) ((uint32_t)(off) == FL_STOCKTAG_LO && (uint32_t)(n) == FL_STOCKTAG_HI - FL_STOCKTAG_LO)
/* a window [lo, hi) is allowed: sector aligned, not empty, clear of every protected range */
#define FL_WIN_SAFE(lo, hi) ((lo) % 0x1000u == 0u && (hi) % 0x1000u == 0u && (lo) < (hi) && \
                             (lo) >= FL_HEAD_HI && (hi) <= FL_KEYMAC_LO && \
                             ((hi) <= FL_CAL_LO || (lo) >= FL_CAL_HI) && \
                             ((hi) <= FL_USRRES_LO || (lo) >= FL_USRRES_HI))
_Static_assert(FL_WIN_SAFE(FL_DATA_LO, FL_DATA_HI), "SMK: FL_DATA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_OTA_LO, FL_OTA_HI), "SMK: FL_OTA overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_SMP_LO, FL_SMP_HI), "SMK: FL_SMP overlaps a protected range");
_Static_assert(FL_SLOT_LO == FL_SMP_LO && FL_SLOT_HI == FL_BANK_LO && FL_BANK_HI == FL_SMP_HI &&
               FL_SLOT_HI - FL_SLOT_LO == 0x14000u && FL_BANK_HI - FL_BANK_LO == 20u * 0x1000u,
               "SMK: USRFLASH = one 80 KiB sample slot + the pattern pool's twenty sectors");
_Static_assert(FL_WIN_SAFE(FL_STOCKTAG_LO, FL_STOCKTAG_HI) && FL_STOCKTAG_LO == FL_SMP_HI &&
               FL_STOCKTAG_OFF + 8u == FL_STOCKTAG_HI, "SMK: the stock marker's sector");
_Static_assert(FL_WIN_SAFE(FL_APP_LO, FL_APP_HI), "SMK: FL_APP overlaps a protected range");
_Static_assert(FL_WIN_SAFE(FL_REC_LO, FL_REC_HI), "SMK: FL_REC overlaps a protected range");
_Static_assert(FL_REC_HI == FL_CAL_LO && FL_OTA_HI <= FL_REC_HI && FL_OTA_LO >= FL_REC_LO,
               "SMK: the update record (FL_OTA) must lie in the loader's record window, below BTIF");
_Static_assert(FL_DATA_HI <= FL_OTA_LO && FL_APP_HI <= FL_DATA_LO, "SMK: windows overlap");
/* the update loader (loader.c): the app area, and records only in the VM below BTIF */
#define FL_LDR_RANGE_OK(off, n) (FL_IN(off, n, FL_APP_LO, FL_APP_HI) || FL_IN(off, n, FL_REC_LO, FL_REC_HI))
/* Skiff's own store: FL_DATA and USRFLASH */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_SMP_LO, FL_SMP_HI))
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI) || FL_STOCKTAG_OK(off, n))
#endif
/* the second, independent net in the RAM driver: [off, off + n) touches a protected range (also when a caller
 * brings its own FL_RANGE_OK, as the update loader does) */
#define FL_HITS(off, n, lo, hi) ((uint32_t)(off) < (hi) && \
                                 ((uint32_t)(off) >= (lo) || (uint32_t)(n) > (lo) - (uint32_t)(off)))
#define FL_PROTECTED(off, n) (FL_HITS(off, n, 0u, FL_HEAD_HI) || FL_HITS(off, n, FL_CAL_LO, FL_CAL_HI) || \
                              FL_HITS(off, n, FL_USRRES_LO, FL_USRRES_HI) || (uint32_t)(off) >= FL_KEYMAC_LO || \
                              (uint32_t)(n) > FL_KEYMAC_LO - (uint32_t)(off))
#define FL_GUARD(off, n) (FL_RANGE_OK(off, n) && !FL_PROTECTED(off, n))
#else /* FM-1 */
#define FL_PLAIN_LO     0x00093000u                /* the plain XIP window from the FM-1's VM start */
#define FL_DATA_LO      0x00097000u                /* Felucca main store */
#define FL_DATA_HI      0x000E0000u
#define FL_GLOB_LO      0x000FC000u                /* Felucca superblock / globals */
#define FL_GLOB_HI      0x000FF000u
#define FL_OTA_LO       0x000E0000u                /* M-UPGRADE loader staging, ota.c */
#define FL_OTA_HI       0x000E5000u
#define FL_AUTO_LO      0x000E5000u                /* 1.2: the autosave (storage.c OBJ_AUTOSAVE), A/B, just above */
#define FL_AUTO_HI      0x000E7000u                /* the OTA staging */
#define FL_BANK_LO      0x000E7000u                /* Skiff: the pattern bank (bank.c), the VM area's last two */
#define FL_BANK_HI      0x000E9000u                /* sectors; 0xE9000 is the package's BTIF (tools/fm1pkg_make.py) */
#define FL_USR_LO       0x000EA000u                /* .. and its USR region, 18 sectors: the bank's other sectors */
#define FL_USR_HI       0x000FC000u
/* Felucca's own store (projects, user samples; settings; the autosave; Skiff's pattern bank) */
#define FL_STORE_OK(off, n) (FL_IN(off, n, FL_DATA_LO, FL_DATA_HI) || FL_IN(off, n, FL_GLOB_LO, FL_GLOB_HI) || \
                             FL_IN(off, n, FL_AUTO_LO, FL_AUTO_HI) || FL_IN(off, n, FL_BANK_LO, FL_BANK_HI) || \
                             FL_IN(off, n, FL_USR_LO, FL_USR_HI))
/* Where the RAM driver may erase / program. The app build allows only its own
 * data regions; the update loader (firmware/loader) defines its own window. */
#ifndef FL_RANGE_OK
#define FL_RANGE_OK(off, n) (FL_STORE_OK(off, n) || FL_IN(off, n, FL_OTA_LO, FL_OTA_HI))
#endif
#define FL_GUARD(off, n) FL_RANGE_OK(off, n)
#endif /* BOARD_SMK37 */
