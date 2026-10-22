/* SPDX-License-Identifier: GPL-3.0-only */
#include "usb_routes.h"
/* Shared PER5 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites; PER5 adds routing.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory[15][28..31]: CLICK / CLICK LEVEL / COUNT-IN (1.1), STYLE (retired, Skiff U2: left as saved), MENU's flags, the layers seen (bytes no
 * engine uses; 0 in older settings = every one's default); [15][27]: SCREEN OFF (1.1.5; 0 = 30 MIN, the default);
 * [15][26]: Skiff's LAUNCH (ui.c ui_launch; 0 = LOOP, an unknown value read as LOOP); [15][25]: Skiff's PERFORM KEYS
 * (ui.c ui_perf_keys: 0 TRACK, 1 SCENE); [15][24]: bank.c SCENE_FREED; [15][23]: (Skiff U1) ui.c ui_set_rev, the
 * one-time changes done; [15][21]: (Skiff phase 9) PRESETS' LIST type (ui.c ui_ptag; 0 = none, an unknown value
 * read as none). ([14][0] .. [15][17] held Skiff's scenes before phase 5; row 14 is DRUM-X's since.)
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM LOCK (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, RESTORE LAST OFF (128, 1.2) = ON, read as saved (nothing to migrate, so importing twice changes nothing). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
    uint32_t usb_routes;
#if FELUCCA_USB_6CH
    uint32_t usb_six_routes, compact_config, compact_track[6];
    uint32_t usb_extra_routes;
#endif
} persist_t;
#if FELUCCA_USB_6CH
#define PERSIST_MAGIC 0x50455238u
#define PERSIST_V7_SIZE (sizeof(persist_t) - 4u)
#define PERSIST_V6_SIZE (sizeof(persist_t) - 32u)
#define PERSIST_V5_SIZE (sizeof(persist_t) - 36u)
#else
#define PERSIST_MAGIC 0x50455235u
#define PERSIST_V5_SIZE sizeof(persist_t)
#define PERSIST_V6_SIZE sizeof(persist_t)
#define PERSIST_V7_SIZE sizeof(persist_t)
#endif
#define PERSIST_V4_SIZE (PERSIST_V5_SIZE - 4u)

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
    int old7 = FELUCCA_USB_6CH && n == (int)PERSIST_V7_SIZE && p->magic == 0x50455237u;
    int old6 = FELUCCA_USB_6CH && n == (int)PERSIST_V6_SIZE && p->magic == 0x50455236u;
    int old5 = FELUCCA_USB_6CH && n == (int)PERSIST_V5_SIZE && p->magic == 0x50455235u;
    int old4 = n == (int)PERSIST_V4_SIZE && p->magic == 0x50455234u;
    int old3 = n == (int)(PERSIST_V4_SIZE - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(PERSIST_V4_SIZE - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old7 || old6 || old5 || old4 || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (old1 || old2 || old3) memset(&p->favorites, 0, sizeof p->favorites);
    p->usb_routes = (current || old7 || old5 || old6) ? usb_routes_valid(p->usb_routes) : USB_ROUTES_DEFAULT;
    usb_routes = p->usb_routes;
#if FELUCCA_USB_6CH
    p->usb_six_routes = (current || old7 || old6) ? usb_six_valid(p->usb_six_routes) : USB_SIX_DEFAULT;
    usb_six_routes = p->usb_six_routes;
    p->usb_extra_routes = current ? usb_extra_valid(p->usb_extra_routes) : USB_EXTRA_DEFAULT;
    usb_extra_routes = p->usb_extra_routes;
    p->compact_config=USB_COMPACT_DEFAULT; /* retired compact mode stays disabled */
    usb_compact_config=p->compact_config;
    for(uint32_t t=0;t<6;t++) {p->compact_track[t]=(current || old7)?usb_cf_track_valid(p->compact_track[t]):USB_CF_TRACK_DEFAULT;usb_cf_track[t]=p->compact_track[t];}
#endif
    {   /* CLICK, CLICK LEVEL, COUNT-IN (ui.c ui_rec_prefs): two bits each, 3 is no value: the default (0) */
        uint32_t b = p->favorites.factory[15][28], f;
        for (f = 0; f < 3u; f++)
            if (((b >> (2u * f)) & 3u) == 3u)
                b &= ~(3u << (2u * f));
        p->favorites.factory[15][28] = (uint8_t)b;
    }
    if ((p->favorites.factory[15][27] ^ 3u) > 4u)  /* SCREEN OFF (1.1.5, ui.c ui_scr): stored ^ 3, unknown = 0 (30 MIN) */
        p->favorites.factory[15][27] = 0;
    if (p->favorites.factory[15][21] >= 8u)        /* (Skiff phase 9) PRESETS' LIST type (ui.c ui_ptag, below PT_N) */
        p->favorites.factory[15][21] = 0;
#if BOARD_SMK37
    if (p->favorites.factory[15][12] >= SMK_KV_N)  /* (SMK-37) KEY VELOCITY (ui.c SMK_KV_BYTE): unknown = DYNAMIC */
        p->favorites.factory[15][12] = 0;
#endif
    p->magic = PERSIST_MAGIC;
    p->palette = palette_to_stored(palette_from_stored(p->palette));
    settings.magic = SETTINGS_MAGIC;
    settings.palette = palette_from_stored(p->palette);
    settings.lowcut = p->lowcut;
    settings.zoom = p->zoom;
    settings_hold = (uint8_t)hold_from_stored(p->bold);
    settings_leds = (uint8_t)leds_from_stored(p->zoom);
#ifdef FELUCCA_FAVORITES
    memcpy(&favorites, &p->favorites, sizeof favorites);
    favorites.filter = favorites.filter == 1u;
#if defined(FM4_NPRESETS) && !FELUCCA_FM4
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        favorites.factory[ENGI_DIGITAL][0] = 0;  /* ((1.3) bytes 2..31: MIDI LEARN's map, midi_control.c ML_N) */
    }
#endif
#ifdef SMP_SET_PERC
    if ((favorites.factory[ENGI_SAMPLE][0] >> SMP_SET_PERC) & 1u) {   /* SAMPLE's PERC (preset 4, retired): DRUM's */
        favorites.factory[ENGI_DRUM][0] |= 1u;                         /* kit (core.h drum_from_perc) */
        favorites.factory[ENGI_SAMPLE][0] &= (uint8_t)~(1u << SMP_SET_PERC);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. */
static void settings_export(persist_t *p)
{
    p->magic = PERSIST_MAGIC;
    p->usb_routes = usb_routes;
#if FELUCCA_USB_6CH
    p->usb_six_routes = usb_six_routes;
    p->usb_extra_routes = usb_extra_routes;
    p->compact_config=USB_COMPACT_DEFAULT;
    for(uint32_t t=0;t<6;t++)p->compact_track[t]=usb_cf_track[t];
#endif
    p->palette = palette_to_stored(settings.palette);
    p->lowcut = settings.lowcut;
    p->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    p->panel = panel;
    p->bold = hold_to_stored(p->bold, settings_hold);
#ifdef FELUCCA_FAVORITES
    memcpy(&p->favorites, &favorites, sizeof favorites);
#endif
}
