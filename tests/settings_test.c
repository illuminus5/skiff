/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#define NENGINES 9u
#define UP_SLOTS 32u
#define ENGI_SAMPLE 4u                           /* (core.h: SAMPLE, DRUM, the retired PERC set) */
#define ENGI_DRUM 10u
#define SMP_SET_PERC 4u
static uint8_t fx_lowcut;
static void fm1_led_key(unsigned k, int on) { (void)k; (void)on; }
static int fm1_enc_take(unsigned k) { (void)k; return 0; }
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }   /* (MENU > SCREEN OFF: lcd.c) */
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
static void settings_save(void) {}
static void theme_apply(void) { palette_set(settings.palette); }   /* (ui.c's: THEME CUSTOM is editor_test's) */
#if __has_include("../firmware/src/favorites.c")
#include "../firmware/src/favorites.c"
#endif
#include "../firmware/src/settings_persist.c"
int main(void)
{
    persist_t original = {0}, p;
    {
        persist_t old = {0};
        old.magic = 0x50455234u; old.panel = PANEL_DEFAULT; old.favorites.user = 7;
        assert(settings_import(&old, PERSIST_V4_SIZE) == 2);
        assert(old.usb_routes == USB_ROUTES_DEFAULT && old.favorites.user == 7);
        usb_routes = 6u | 9u << 8;
        settings_export(&old);
        usb_routes = 0;
        assert(settings_import(&old, sizeof old) == 1 && usb_routes == (6u | 9u << 8));
        old.usb_routes = 0xffffu;
        assert(settings_import(&old, sizeof old) == 1 && usb_routes == USB_ROUTES_DEFAULT);
    }
#if FELUCCA_USB_6CH
    {
        persist_t q={0};q.magic=0x50455237u;q.usb_six_routes=USB_SIX_DEFAULT;
        q.compact_track[0]=USB_CF_TRACK_DEFAULT;
        assert(settings_import(&q,PERSIST_V7_SIZE)==2);
        assert(usb_six_routes==USB_SIX_DEFAULT && usb_extra_routes==USB_EXTRA_DEFAULT);
        usb_extra_routes=4u|20u<<5|23u<<10|24u<<15;
        settings_export(&q);usb_extra_routes=0;
        assert(settings_import(&q,sizeof q)==1 && usb_extra_routes==q.usb_extra_routes);
        q.usb_extra_routes=0xffffffffu;
        assert(settings_import(&q,sizeof q)==1 && usb_extra_routes==0);
    }
#endif
    original.magic = PERSIST_MAGIC;
    original.palette = 4; original.bold = original.lowcut = original.zoom = 1;   /* old id 4: MONO, gone: SKIFF */
    original.panel = PANEL_DEFAULT; original.panel.enc[0] = 3;
    original.favorites.factory[8][0] = 1;
    original.favorites.user = 1u << 31; original.favorites.filter = 1;
    p = original;
    assert(settings_import(&p, sizeof p) == 1); settings_init();
    assert(settings.palette == UI_SKIFF_INDEX && fx_lowcut && settings.zoom && panel.enc[0] == 3);
    assert(p.palette == palette_to_stored(UI_SKIFF_INDEX));       /* migrated in place */
    {   /* (Skiff, R1) THEME: SKIFF HI-CON PAPER CRYPT NEON QUARK. Every old id maps to one (the eleven before THEME: PAL_TAG +
         * index; Felucca's twenty: below 20; a palette gone: SKIFF); THEME's ids round trip; anything else is SKIFF */
        persist_t q = original;
        assert(NPALETTES == 6u && !strcmp(UI_PALETTES[0].name, "SKIFF") && !strcmp(UI_PALETTES[1].name, "HI-CON") &&
               !strcmp(UI_PALETTES[2].name, "PAPER") && !strcmp(UI_PALETTES[UI_CRYPT_INDEX].name, "CRYPT"));
        for (uint32_t i = 0; i < 20u; i++) assert(palette_from_stored(i) < NPALETTES);
        assert(palette_from_stored(13) == 2u && palette_from_stored(11) == 1u && palette_from_stored(19) == 1u &&
               palette_from_stored(0) == UI_SKIFF_INDEX);
        assert(palette_from_stored(UI_PAL_TAG + 6u) == 2u && palette_from_stored(UI_PAL_TAG + 7u) == 1u &&   /* PAPER HI-CON */
               palette_from_stored(UI_PAL_TAG + 10u) == UI_SKIFF_INDEX && palette_from_stored(UI_PAL_TAG + 0u) == UI_SKIFF_INDEX &&
               palette_from_stored(UI_PAL_TAG + 9u) == UI_SKIFF_INDEX && palette_stored_ok(UI_PAL_TAG + 10u));   /* GREY MONO */
        for (uint32_t i = 0; i < NPALETTES; i++) assert(palette_from_stored(palette_to_stored(i)) == i);
        assert(palette_from_stored(40) == UI_SKIFF_INDEX && !palette_stored_ok(40) && palette_stored_ok(3) &&
               !palette_stored_ok(UI_THEME_TAG + NPALETTES));
        q.palette = palette_to_stored(UI_CRYPT_INDEX);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_CRYPT_INDEX);
        q.palette = palette_to_stored(UI_NEON_INDEX);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_NEON_INDEX);
        q.palette = palette_to_stored(UI_QUARK_INDEX);
        assert(settings_import(&q, sizeof q) == 1 && settings.palette == UI_QUARK_INDEX);
        settings.magic = SETTINGS_MAGIC_OLD; settings.palette = 13; settings_init();   /* retained SET3 */
        assert(settings.magic == SETTINGS_MAGIC && settings.palette == 2u);
        p = original; assert(settings_import(&p, sizeof p) == 1); settings_init();
    }
#ifdef FELUCCA_FAVORITES
    assert(favorite_has(8, 0) && favorite_has(NENGINES, 31) && favorites.filter);
    {   /* SAMPLE's PERC starred (preset 4, retired): DRUM's kit (preset 0) instead; SAMPLE's others kept; twice the same */
        persist_t q = original;
        q.favorites.factory[ENGI_SAMPLE][0] = 1u << SMP_SET_PERC | 1u << 2;
        assert(settings_import(&q, sizeof q) == 1 && favorites.factory[ENGI_SAMPLE][0] == 1u << 2 &&
               favorites.factory[ENGI_DRUM][0] == 1u);
        settings_export(&q);
        assert(settings_import(&q, sizeof q) == 1 && favorites.factory[ENGI_SAMPLE][0] == 1u << 2 &&
               favorites.factory[ENGI_DRUM][0] == 1u);
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
#endif
    settings.lowcut = 0;
    settings_export(&p);
    assert(!p.lowcut && p.bold == 1 && p.favorites.user == (1u << 31));   /* bold: kept as saved */
    assert(p.favorites.factory[8][0] == 1 && p.favorites.filter == 1);
    assert(p.panel.enc[0] == 3); /* saving one feature preserves the other */
    p = original; p.magic = 0x50455233u;
    assert(settings_import(&p, PERSIST_V4_SIZE - sizeof p.favorites) == 2);
    assert(p.bold == 1 && !p.favorites.user && !p.favorites.filter);
    settings_export(&p); assert(p.bold == 1); /* favorites-only preserves PER3 font */
    p = original; p.magic = 0x50455232u;
    assert(settings_import(&p, PERSIST_V4_SIZE - sizeof p.favorites - sizeof p.bold) == 2);
    assert(!p.bold && !p.favorites.user && settings.zoom && panel.enc[0] == 3);
    p = original; p.magic = 0x50455231u;
    memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
    assert(settings_import(&p, 8 + sizeof(panel_t)) == 2);
    assert(!p.bold && !p.zoom && !p.lowcut && !p.favorites.user && panel.magic == PANEL_MAGIC);
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        settings_export(&p);
        assert(hold_stored_ok(p.bold) && p.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&p);
        assert(p.bold == 0u && settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
        p = original; p.magic = 0x50455231u;
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_hold = 2;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_hold == HOLD_DEF);
    }
    {   /* LEDS: in the retired zoom field; older records (zoom 0 / 1) are DIM, and stay as saved; idempotent */
        persist_t q;
        p = original;                                   /* zoom 1: an older record's large readout */
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        q = p; settings_export(&q); assert(q.zoom == 1u);                  /* DIM: kept as saved */
        settings_leds = LEDS_INV;
        settings_export(&p);
        assert(p.zoom == (LEDS_TAG | LEDS_INV) && leds_stored_ok(p.zoom));
        q = p; settings_export(&q); assert(!memcmp(&q, &p, sizeof q));   /* a second save: the same record */
        settings_leds = LEDS_DIM;
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_INV);
        q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));   /* import twice: the same */
        settings_leds = LEDS_DIM;
        settings_export(&p);
        assert(p.zoom == 0u && settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        assert(leds_stored_ok(0u) && leds_stored_ok(1u) && !leds_stored_ok(2u) && leds_stored_ok(LEDS_TAG | 2u) &&
               leds_stored_ok(LEDS_TAG | 3u) && !leds_stored_ok(LEDS_TAG + 4u) && leds_from_stored(LEDS_TAG + 4u) == LEDS_DIM &&
               leds_from_stored(7u) == LEDS_DIM && leds_from_stored(2u) == LEDS_DIM);
        /* the modes appended (append-only: DIM 0, INV 1 as before): OFF 2, DIM LO 3, each saved and read back,
         * twice the same */
        {
            uint32_t m;
            static const uint32_t ADDED[2] = {LEDS_OFF, LEDS_DIM_LO};
            assert(LEDS_DIM == 0 && LEDS_INV == 1 && LEDS_OFF == 2 && LEDS_DIM_LO == 3 && LEDS_COUNT == 4);
            for (m = 0; m < 2u; m++) {
                settings_leds = (uint8_t)ADDED[m];
                settings_export(&p);
                assert(p.zoom == (LEDS_TAG | ADDED[m]) && leds_stored_ok(p.zoom));
                q = p; settings_export(&q); assert(!memcmp(&q, &p, sizeof q));
                settings_leds = LEDS_DIM;
                assert(settings_import(&p, sizeof p) == 1 && settings_leds == ADDED[m]);
            }
            settings_leds = LEDS_DIM;
            settings_export(&p);
            assert(p.zoom == 0u);
        }
        p = original; p.magic = 0x50455231u;           /* PER1: no zoom field, DIM */
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_leds = LEDS_INV;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_leds == LEDS_DIM);
    }
#ifdef FELUCCA_FAVORITES
    {   /* 1.1 CLICK / CLICK LEVEL / COUNT-IN (ui.c ui_rec_prefs, favorites.factory[15][28]): 0 in every older record
         * (the defaults: OFF / MID / OFF); each field 0..2 kept, 3 (no value) back to 0, bits 6-7 kept; twice the same */
        persist_t q;
        uint32_t b;
        p = original; p.magic = 0x50455233u;            /* PER3 (no favorites): 0 */
        assert(settings_import(&p, PERSIST_V4_SIZE - sizeof p.favorites) == 2 && favorites.factory[15][28] == 0u);
        for (b = 0; b < 256u; b++) {
            uint32_t f, want = b;
            for (f = 0; f < 3u; f++)
                if (((b >> (2u * f)) & 3u) == 3u) want &= ~(3u << (2u * f));
            p = original; p.favorites.factory[15][28] = (uint8_t)b;
            assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][28] == want && p.favorites.factory[15][28] == want);
            q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));
            settings_export(&q); assert(q.favorites.factory[15][28] == want);
        }
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* 1.2 RESTORE LAST (ui.c PREF_RESTORE_OFF, bit 7 of MENU's flags, favorites.factory[15][30]): clear in every
         * older record = ON; set = OFF, kept as saved with the other flags */
        persist_t q;
        p = original; p.magic = 0x50455233u;
        assert(settings_import(&p, PERSIST_V4_SIZE - sizeof p.favorites) == 2 && !(favorites.factory[15][30] & 128u));
        p = original; p.favorites.factory[15][30] = 128u | 64u | 1u;
        assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][30] == (128u | 64u | 1u));
        q = p; settings_export(&q); assert(q.favorites.factory[15][30] == (128u | 64u | 1u));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
#endif
#if FELUCCA_USB_6CH
    {
        persist_t q=original;
        q.magic=0x50455235u; q.usb_routes=4u | 5u<<8;
        assert(settings_import(&q,PERSIST_V5_SIZE)==2);
        assert(usb_routes==(4u|5u<<8) && usb_six_routes==USB_SIX_DEFAULT);
        q.magic=0x50455236u;q.usb_six_routes=USB_SIX_DEFAULT;
        assert(settings_import(&q,PERSIST_V6_SIZE)==2 && usb_compact_config==USB_COMPACT_DEFAULT);
        usb_compact_config=1u;
        usb_cf_track[3]=185u|5u<<8|99u<<16|10u<<24;
        usb_six_put(0,3);usb_six_put(5,20);
        settings_export(&q);
        assert(q.usb_six_routes==usb_six_routes && q.compact_config==USB_COMPACT_DEFAULT && q.compact_track[3]==usb_cf_track[3]);
        usb_six_routes=USB_SIX_DEFAULT;
        assert(settings_import(&q,sizeof q)==1 && usb_six_get(0)==3 && usb_six_get(5)==20);
        assert(usb_compact_config==USB_COMPACT_DEFAULT);
        q.usb_six_routes=0xffffffffu;
        assert(settings_import(&q,sizeof q)==1 && usb_six_routes==0);
    }
#endif
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    puts("Settings: PER1/PER2/PER3 migration, palette ids, calibration, HOLD, LEDS, CLICK / COUNT-IN and independent feature preservation passed.");
}
