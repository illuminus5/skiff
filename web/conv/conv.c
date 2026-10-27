/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* (Skiff 2.0, Tier B: design/TIER_B.md 10) The converter: an archive of before (web/fm1backup.js) into what 2.0
 * reads, in the browser. The firmware's own code does it, as a unit would, over a simulated flash:
 *   1. each object restored through the editor's backup path (editor_backup.c ed_bk_commit), in the restore's order:
 *      an old project is stored as SKF1, then at the music's commit (id 0, last) its steps, timing and automation
 *      become patterns in the pool and it becomes SKF4 (bank.c proj_migrate); the settings SKS1; 1.0-1.0.2's FM6 bank
 *      its patches into the user presets' (up_fm6.c upf_migrate);
 *   2. what 2.0 still can't read rewritten (conv_fix): an SKF2 project SKF4, a user preset of versions 1-3 or a
 *      DIGITAL sound version 6 (DIGITAL: FM6 with a patch of its own, fm4_convert.c);
 *   3. the objects taken back as a backup takes them (LIST, then GET).
 * conv_needs says which objects of an archive 2.0 can't read as they are: the page writes back only those (with
 * the pattern chunks and the user presets' patches the conversion changed) and reports the rest as kept.
 * What no Skiff reads, Felucca 1.2+'s FUNA projects and its user presets by parameter name, is felucca.c's.
 * Built with the old readers whatever the firmware is built with. The exports: conv_buf (an object's bytes both
 * ways), conv_reset, conv_source (felucca.c), conv_put, conv_fix, conv_get, conv_needs, conv_note (felucca.c).
 *   web/conv/build.sh -> build/conv/conv.wasm; tests/conv_test.c builds this on the host */
#include <stdint.h>

#define FELUCCA_FLASH 1
#define FELUCCA_LEGACY 1                           /* (the old readers, which 2.0's own build leaves out: core.h) */
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 0
#endif
static unsigned char host_samples[3][0x14000];     /* (the user sample slots: never touched here) */
#define SMP_USER_XIP(k) host_samples[k]
#define main hostsim_main
#include "../../tests/hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;        /* the SPI NOR (erased: 0xFF) */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off) return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off) return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off) return -8;
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"
#include "../../firmware/src/bank.c"

enum { ED_BACKUP_LIST = 65, ED_BACKUP_GET, ED_BACKUP_PUT };
static uint8_t rep[4096];                          /* the editor's reply (its 7-bit bytes) */
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j, k;
        na--;
        k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static void fm1_wdt_feed(void) {}
static int ed_flash_stop(void) { return transport_busy(); }
#include "../../firmware/src/editor_backup.c"

#undef __attribute__                               /* (hostsim.c's: empty, for the firmware's sections; as felucca_web.c) */
#define EXPORT __attribute__((used, visibility("default")))
static uint8_t conv_bytes[81920];                  /* an object, both ways (the largest: a user sample slot) */
EXPORT uint8_t *conv_buf(void) { return conv_bytes; }
#include "felucca.c"                               /* (Felucca 1.2+: FUNA, its presets by name) */

static uint32_t conv_call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup_handle(cmd, a, n);
    return rep_n;
}
static void conv_u32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }

/* an erased unit at its first power-on, as the editor finds one after an install */
EXPORT void conv_reset(void)
{
    memset(nor, 0xFF, sizeof nor);
    memset(&song, 0, sizeof song);
    sel_set(0);
    memset(trk, 0, sizeof trk);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    memset(&favorites, 0, sizeof favorites);
    memset(&ui, 0, sizeof ui);
    panel = PANEL_DEFAULT;
    settings_init();
    host_tracks_init();
    upf_empty();
    fm1_ms = 0;
    transport_req = 0;
    usb.resets = 0;
    ed_bk_valid = ed_bk_put = 0;
    bank_boot();
    lists_clear();
    motion_lost = 0;
    fel_reset();
}

/* object id (len bytes in conv_buf) restored as BACKUP PUT does it: begin, 256-byte pieces, commit. Its rc: 0 done,
 * 1 not one this object takes, 2 refused (a damaged or unknown record), 4 a write failed */
EXPORT uint32_t conv_put(uint32_t id, uint32_t len)
{
    uint8_t a[16 + 300];
    uint32_t off, rc;
    if (fel_is(conv_bytes, len))                   /* (Felucca's FUNA: no Skiff reads one) */
        return fel_put(id, len);
    a[0] = 0; a[1] = (uint8_t)id;
    conv_u32(a + 2, len);
    conv_u32(a + 7, st_crc32(conv_bytes, len));
    conv_call(ED_BACKUP_PUT, a, 12);
    rc = rep[2];
    for (off = 0; !rc && off < len; off += 256u) {
        const uint8_t *p = conv_bytes + off;
        uint32_t n = len - off > 256u ? 256u : len - off, k = 7;
        a[0] = 1; a[1] = (uint8_t)id;
        conv_u32(a + 2, off);
        while (n) {
            uint32_t g = n > 7u ? 7u : n, m = 0, i;
            for (i = 0; i < g; i++) m |= (uint32_t)(p[i] >> 7) << i;
            a[k++] = (uint8_t)m;
            for (i = 0; i < g; i++) a[k++] = p[i] & 127u;
            p += g; n -= g;
        }
        conv_call(ED_BACKUP_PUT, a, k);
        rc = rep[2];
    }
    if (rc)
        return rc;
    a[0] = 2; a[1] = (uint8_t)id;
    conv_call(ED_BACKUP_PUT, a, 2);
    return rep[2];
}

/* after every object: what 2.0 still can't read, rewritten. An SKF2 project (four tracks) becomes SKF4; a user
 * preset of versions 1-3 version 6, a DIGITAL sound FM6 with its own patch (fm4_apply on a track, then its patch the
 * slot's, as SAVE > USER keeps one), one Felucca 1.2+ wrote version 6 by name (felucca.c fel_up). Each saved as a unit
 * saves it. Then the FUNAs' songs (fel_songs) */
EXPORT void conv_fix(void)
{
    uint32_t s, k;
    for (s = 0; s < 4u; s++)
        if (proj_fmt_of(&proj_slot[s], sizeof proj_slot[s]) == &PROJ_SKF2 &&
            proj_import(&proj_scratch, &proj_slot[s], sizeof proj_slot[s]) &&
            proj_pack2(&proj_wire, &proj_scratch, PROJ_SKF) && !st_save(OBJ_PROJECT0 + s, &proj_wire, PROJ_SKF->size))
            memcpy(&proj_slot[s], &proj_wire, sizeof proj_wire);
    for (k = 0; k < UP_SLOTS; k++) {
        up_rec_t *r = up_rec(k), n;
        int16_t v[P_COUNT];
        uint8_t pk[FM6_PACKED];
        uint32_t was;
        int own;
        if (!up_used(k) || (r->ver >= 4u && r->engine != ENGI_DIGITAL && !fel_up(r)))
            continue;
        was = r->engine;
        own = was == ENGI_FM6 && !upf_get(k, pk);   /* (an FM6 sound's patch: tied to the record as it was, upf_tag) */
        if (!fel_up_values(r, v, k))
            up_values(r, v);
        n = *r;
        if (was == ENGI_DIGITAL) {
            fm4_apply(&trk[0], v);                 /* (DIGITAL's values -> FM6's, the patch track 1's) */
            memcpy(v, trk[0].p, sizeof v);
            fm6_pack(fm6_patch[0], pk);
            own = 1;
            n.engine = ENGI_FM6;
        }
        up_set_sound(&n, v);
        if (!up_put(k, &n) && own) {               /* (the patch tied to the record as it is now) */
            upf_set(k, pk);
            (void)upf_save();
        }
    }
    fel_songs();
}

/* object id as a backup takes it now (LIST: the music packed again, editor_backup.c ed_bk_capture; then GET), into
 * conv_buf: its length; 0 empty */
EXPORT uint32_t conv_get(uint32_t id)
{
    uint32_t i, len = 0, off;
    conv_call(ED_BACKUP_LIST, 0, 0);
    if (rep[1])
        return 0;
    for (i = 0; i < rep[2]; i++)
        if (rep[3 + i * 11u] == id)
            len = ed_bk_r32(rep + 3 + i * 11u + 1u);
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        uint8_t a[8] = {(uint8_t)id};
        conv_u32(a + 1, off);
        a[6] = (uint8_t)(n & 127u);
        a[7] = (uint8_t)(n >> 7);
        conv_call(ED_BACKUP_GET, a, sizeof a);
        if (rep[1] || ed_unpack7(rep + 9, rep_n - 9u, conv_bytes + off, n) != n)
            return 0;
    }
    return len;
}

/* 1 when 2.0 can't read object id (len bytes in conv_buf) as it is, so it must be converted: a project of a format
 * before SKF3 (a FUNA too), a user preset bank holding a record of versions 1-3, a DIGITAL sound or one Felucca 1.2+
 * wrote (felucca.c fel_up), 1.0-1.0.2's FM6 bank. 0: as it
 * is (2.0 reads it), empty, or nothing any firmware reads (the unit ignores it as before). Settings never: an archive's
 * are PER4 (every firmware with complete backups, 1.0.3 on) or SKS1, and 2.0 reads both */
EXPORT uint32_t conv_needs(uint32_t id, uint32_t len)
{
    uint32_t i;
    if (!len)
        return 0;
    if (id == 0u || (id >= 2u && id <= 5u)) {
        const proj_fmt_t *f = proj_fmt_of(conv_bytes, (int)len);
        return f != &PROJ_SKF3 && f != &PROJ_SKF4;
    }
    if (id == 6u || id == 7u) {
        const up_bank_t *b = (const up_bank_t *)conv_bytes;
        if (len != sizeof *b || b->magic != UP_BANK_MAGIC || b->rsize != sizeof(up_rec_t) || b->nslot != UP_PER_BANK)
            return 0;                              /* (not a bank any firmware reads: empty, as on the unit) */
        for (i = 0; i < UP_PER_BANK; i++)
            if (up_valid(&b->r[i]) && (b->r[i].ver < 4u || b->r[i].engine == ENGI_DIGITAL || fel_up(&b->r[i])))
                return 1;
        return 0;
    }
    return id == 8u;
}
