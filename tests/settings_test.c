/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __attribute__(x)
#define NENGINES 9u
#define UP_SLOTS 32u
#define ENGI_DIGITAL 1u                          /* (core.h: DIGITAL, retired: its row MIDI LEARN's; SAMPLE, DRUM, the retired PERC set) */
#define ENGI_SAMPLE 4u
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
    sks_t s, t;                                         /* ((2.0) what a save writes: SKS1) */
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
    {   /* SAMPLE's PERC starred (preset 4, retired): DRUM's kit (preset 0) instead; SAMPLE's others kept; twice the same.
         * ((2.0) The converter's build alone: 2.0 keeps the stars as they are, preset 4 a hidden PIANO) */
        persist_t q = original;
        q.favorites.factory[ENGI_SAMPLE][0] = 1u << SMP_SET_PERC | 1u << 2;
#if FELUCCA_LEGACY
        assert(settings_import(&q, sizeof q) == 1 && favorites.factory[ENGI_SAMPLE][0] == 1u << 2 &&
               favorites.factory[ENGI_DRUM][0] == 1u);
        memset(&s, 0, sizeof s); settings_export(&s);
        assert(settings_import(&s, sizeof s) == 1 && favorites.factory[ENGI_SAMPLE][0] == 1u << 2 &&
               favorites.factory[ENGI_DRUM][0] == 1u);
#else
        assert(settings_import(&q, sizeof q) == 1 && favorites.factory[ENGI_SAMPLE][0] == (1u << SMP_SET_PERC | 1u << 2) &&
               !favorites.factory[ENGI_DRUM][0]);
#endif
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
#endif
    settings.lowcut = 0;
    memset(&s, 0, sizeof s); s.bold = p.bold;          /* ((2.0) an SKS1 over a record holding PER4's bold) */
    settings_export(&s);
    assert(s.magic == SKS_MAGIC && !s.lowcut && s.bold == 1 && s.fav_user == (1u << 31));   /* bold: kept as saved */
    assert(s.fav[8][0] == 1 && s.fav_filter == 1);
    assert(s.panel.enc[0] == 3); /* saving one feature preserves the other */
#if FELUCCA_LEGACY                                      /* ((2.0) PER1..PER3, Felucca's before 1.0: the converter's) */
    p = original; p.magic = 0x50455233u;
    assert(settings_import(&p, sizeof p - sizeof p.favorites) == 2);
    assert(p.bold == 1 && !p.favorites.user && !p.favorites.filter);
    s.bold = p.bold; settings_export(&s); assert(s.bold == 1); /* favorites-only preserves PER3 font */
    p = original; p.magic = 0x50455232u;
    assert(settings_import(&p, sizeof p - sizeof p.favorites - sizeof p.bold) == 2);
    assert(!p.bold && !p.favorites.user && settings.zoom && panel.enc[0] == 3);
    p = original; p.magic = 0x50455231u;
    memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
    assert(settings_import(&p, 8 + sizeof(panel_t)) == 2);
    assert(!p.bold && !p.zoom && !p.lowcut && !p.favorites.user && panel.magic == PANEL_MAGIC);
#else
    p = original; p.magic = 0x50455233u;                 /* (2.0: refused, the defaults stay) */
    assert(!settings_import(&p, sizeof p - sizeof p.favorites));
    p = original; p.magic = 0x50455232u;
    assert(!settings_import(&p, sizeof p - sizeof p.favorites - sizeof p.bold));
    p = original; p.magic = 0x50455231u;
    memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
    assert(!settings_import(&p, 8 + sizeof(panel_t)));
#endif
    {   /* HOLD: in the retired bold field; older records (bold 0 / 1) are 0.4 s, and stay as saved */
        p = original; p.bold = 0;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF && HOLD_MS[settings_hold] == 400u);
        p = original;
        assert(settings_import(&p, sizeof p) == 1 && settings_hold == HOLD_DEF);
        settings_hold = 3;
        memset(&s, 0, sizeof s); s.bold = p.bold;
        settings_export(&s);
        assert(hold_stored_ok(s.bold) && s.bold != 1u);
        settings_hold = 0;
        assert(settings_import(&s, sizeof s) == 1 && settings_hold == 3u && HOLD_MS[settings_hold] == 600u);
        settings_hold = HOLD_DEF;
        settings_export(&s);
        assert(s.bold == 0u && settings_import(&s, sizeof s) == 1 && settings_hold == HOLD_DEF);
        assert(!hold_stored_ok(2u) && !hold_stored_ok(HOLD_TAG + 4u) && hold_stored_ok(HOLD_TAG | 2u));
#if FELUCCA_LEGACY
        p = original; p.magic = 0x50455231u;
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_hold = 2;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_hold == HOLD_DEF);
#endif
    }
    {   /* LEDS: in the retired zoom field; older records (zoom 0 / 1) are DIM, and stay as saved; idempotent */
        p = original;                                   /* zoom 1: an older record's large readout */
        assert(settings_import(&p, sizeof p) == 1 && settings_leds == LEDS_DIM);
        memset(&s, 0, sizeof s); s.zoom = p.zoom;      /* ((2.0) the SKS1 written over it) */
        t = s; settings_export(&t); assert(t.zoom == 1u);                  /* DIM: kept as saved */
        settings_leds = LEDS_INV;
        settings_export(&s);
        assert(s.zoom == (LEDS_TAG | LEDS_INV) && leds_stored_ok(s.zoom));
        t = s; settings_export(&t); assert(!memcmp(&t, &s, sizeof t));   /* a second save: the same record */
        settings_leds = LEDS_DIM;
        assert(settings_import(&s, sizeof s) == 1 && settings_leds == LEDS_INV);
        t = s; assert(settings_import(&t, sizeof t) == 1 && !memcmp(&t, &s, sizeof t));   /* import twice: the same */
        settings_leds = LEDS_DIM;
        settings_export(&s);
        assert(s.zoom == 0u && settings_import(&s, sizeof s) == 1 && settings_leds == LEDS_DIM);
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
                settings_export(&s);
                assert(s.zoom == (LEDS_TAG | ADDED[m]) && leds_stored_ok(s.zoom));
                t = s; settings_export(&t); assert(!memcmp(&t, &s, sizeof t));
                settings_leds = LEDS_DIM;
                assert(settings_import(&s, sizeof s) == 1 && settings_leds == ADDED[m]);
            }
            settings_leds = LEDS_DIM;
            settings_export(&s);
            assert(s.zoom == 0u);
        }
#if FELUCCA_LEGACY
        p = original; p.magic = 0x50455231u;           /* PER1: no zoom field, DIM */
        memcpy((uint8_t *)&p + 8, &PANEL_DEFAULT, sizeof(panel_t));
        settings_leds = LEDS_INV;
        assert(settings_import(&p, 8 + sizeof(panel_t)) == 2 && settings_leds == LEDS_DIM);
#endif
    }
#ifdef FELUCCA_FAVORITES
    {   /* 1.1 CLICK / CLICK LEVEL / COUNT-IN (ui.c ui_rec_prefs, favorites.factory[15][28]): 0 in every older record
         * (the defaults: OFF / MID / OFF); each field 0..2 kept, 3 (no value) back to 0, bits 6-7 kept; twice the same */
        persist_t q;
        uint32_t b;
#if FELUCCA_LEGACY
        p = original; p.magic = 0x50455233u;            /* PER3 (no favorites): 0 */
        assert(settings_import(&p, sizeof p - sizeof p.favorites) == 2 && favorites.factory[15][28] == 0u);
#endif
        for (b = 0; b < 256u; b++) {
            uint32_t f, want = b;
            for (f = 0; f < 3u; f++)
                if (((b >> (2u * f)) & 3u) == 3u) want &= ~(3u << (2u * f));
            p = original; p.favorites.factory[15][28] = (uint8_t)b;
            p.favorites.factory[15][24] = 0xA5u;        /* (Skiff's mark: Skiff's record; Felucca's bit 7 is not FEEL) */
            assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][28] == want && p.favorites.factory[15][28] == want);
            q = p; assert(settings_import(&q, sizeof q) == 1 && !memcmp(&q, &p, sizeof q));
            memset(&s, 0, sizeof s); settings_export(&s); assert(s.s.rec == want);   /* ((2.0) SKS1's) */
            s.s.rec = (uint8_t)b;
            assert(settings_import(&s, sizeof s) == 1 && s.s.rec == want && favorites.factory[15][28] == want);
        }
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* 1.2 RESTORE LAST (ui.c PREF_RESTORE_OFF, bit 7 of MENU's flags, favorites.factory[15][30]): clear in every
         * older record = ON; set = OFF, kept as saved with the other flags */
#if FELUCCA_LEGACY
        p = original; p.magic = 0x50455233u;
        assert(settings_import(&p, sizeof p - sizeof p.favorites) == 2 && !(favorites.factory[15][30] & 128u));
#endif
        p = original; p.favorites.factory[15][30] = 128u | 64u | 1u;
        assert(settings_import(&p, sizeof p) == 1 && favorites.factory[15][30] == (128u | 64u | 1u));
        memset(&s, 0, sizeof s); settings_export(&s); assert(s.s.prefs == (128u | 64u | 1u));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
    {   /* (2.0, Tier B 6) SKS1: Skiff's settings by name, the favourites alone (row 1's MIDI LEARN map and row 14's THEME
         * CUSTOM colours named), the room kept as read; Skiff's PER4 (bank.c's mark) read as it is, another firmware's
         * (no mark) for its own fields only */
        uint32_t i;
        p = original;
        for (i = 0; i < 32u; i++)
            p.favorites.factory[15][i] = (uint8_t)(i + 1u);
        p.favorites.factory[15][21] = 3; p.favorites.factory[15][24] = 0xA5u;   /* (LIST: a type; Skiff's mark) */
        p.favorites.factory[15][27] = 7; p.favorites.factory[15][28] = 0xD5u;   /* (SCREEN OFF 4; CLICK .. FEEL) */
        for (i = 0; i < 22u; i++)
            p.favorites.factory[14][8u + i] = (uint8_t)(0x40u + i);
        for (i = 0; i < 30u; i++)
            p.favorites.factory[ENGI_DIGITAL][2u + i] = (uint8_t)(0x80u + i);
        p.favorites.factory[14][0] = 0x0F;              /* (DRUM-X's stars) */
        assert(settings_import(&p, sizeof p) == 1 && !memcmp(favorites.factory[15], p.favorites.factory[15], 32) &&
               favorites.factory[14][8] == 0x40u && favorites.factory[ENGI_DIGITAL][2] == 0x80u &&
               favorites.factory[14][0] == 0x0Fu);
        memset(&s, 0, sizeof s);
        memset(s.room, 0x33, sizeof s.room);            /* (a later 2.0's settings) */
        settings_export(&s);
        assert(s.magic == SKS_MAGIC && s.s.restore_slot == 1u && s.s.restore_clean[3] == 5u && s.s.cpu == 6u &&
               s.s.mch[0] == 7u && s.s.mch[5] == 12u && s.s.cpal == 15u && s.s.ret == 17u && s.s.unit_g[2] == 21u &&
               s.s.ptag == 3u && s.s.set_rev == 24u && !s.s.rsv24 && s.s.launch == 27u && s.s.scr == 7u &&
               s.s.rec == 0xD5u && s.s.prefs == 31u && s.s.layers == 32u);
        assert(s.cpal_cols[0] == 0x40u && s.cpal_cols[21] == 0x40u + 21u && s.ml[0] == 0x80u && s.ml[29] == 0x80u + 29u &&
               s.fav[14][0] == 0x0Fu && !s.fav[14][8] && !s.fav[14][29] && !s.fav[ENGI_DIGITAL][2] &&
               !s.fav[ENGI_DIGITAL][31] && s.fav[8][0] == 1u && s.room[0] == 0x33u && s.room[31] == 0x33u);
        memset(favorites.factory, 0, sizeof favorites.factory);
        t = s;
        assert(settings_import(&t, sizeof t) == 1 && !memcmp(&t, &s, sizeof t) &&
               !memcmp(favorites.factory[15], p.favorites.factory[15], 32) && favorites.factory[14][8] == 0x40u &&
               favorites.factory[14][29] == 0x40u + 21u && favorites.factory[ENGI_DIGITAL][31] == 0x80u + 29u &&
               favorites.factory[14][0] == 0x0Fu && favorites.factory[8][0] == 1u);
        p.favorites.factory[15][24] = 0;                /* another firmware's PER4 */
        assert(settings_import(&p, sizeof p) == 1);
        for (i = 0; i < 27u; i++)
            assert(favorites.factory[15][i] == (i == 24u ? 0xA5u : 0u));
        assert(favorites.factory[15][27] == 7u && favorites.factory[15][28] == 0x55u && favorites.factory[15][29] == 30u &&
               favorites.factory[15][30] == 31u && favorites.factory[15][31] == 32u && favorites.factory[8][0] == 1u);
        for (i = 0; i < 32u; i++)
            assert(!favorites.factory[14][i] && (!i || !favorites.factory[ENGI_DIGITAL][i]));
        t = s; t.magic = PERSIST_MAGIC;
        assert(!settings_import(&t, sizeof t));
        t = s;
        assert(!settings_import(&t, sizeof t - 1));
        p = original; assert(settings_import(&p, sizeof p) == 1);
    }
#endif
    assert(settings_import(&p, 3) == 0 && settings_import(&p, -1) == 0);
    assert(settings_import(&p, sizeof p - 1) == 0);
    puts("Settings: PER1/PER2/PER3 migration, palette ids, calibration, HOLD, LEDS, CLICK / COUNT-IN and independent feature preservation passed.");
}
