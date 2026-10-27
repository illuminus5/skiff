/* SPDX-License-Identifier: GPL-3.0-only */
/* Shared PER4 layout: each feature updates only its own fields, preserving
 * the other feature's saved preferences when either is built independently.
 * PER1/PER2 are upstream; PER3 added bold; PER4 added favorites.
 * palette: UI_PAL_TAG + index; an old id (below 20, earlier firmware) is migrated on import.
 * bold: no longer used (one font weight); kept as it was saved, unless it holds the HOLD setting (panel.c).
 * zoom: no longer used (the large readout); kept as it was saved, unless it holds the LEDS setting (panel.c).
 * favorites.factory[15][28..31]: CLICK / CLICK LEVEL / COUNT-IN (1.1), STYLE (retired, Skiff U2: left as saved), MENU's flags, the layers seen (bytes no
 * engine uses; 0 in older settings = every one's default); [15][27]: SCREEN OFF (1.1.5; 0 = 30 MIN, the default);
 * [15][26]: Skiff's LAUNCH (ui.c ui_launch; 0 = LOOP, an unknown value read as LOOP); [15][25]: Skiff's PERFORM KEYS
 * (ui.c ui_perf_keys: 0 TRACK, 1 SCENE); [15][24]: Skiff's mark, 0xA5 (before 2.0 bank.c's, set once it had freed its
 * old scenes); [15][23]: (Skiff U1) ui.c ui_set_rev, the one-time changes done; [15][21]: (Skiff phase 9) PRESETS' LIST type (ui.c ui_ptag; 0 = none, an unknown value
 * read as none). ([14][0] .. [15][17] held Skiff's scenes before phase 5; row 14 is DRUM-X's since.)
 * MENU's flags (ui.c PREF_*) are append-only bits whose 0 is the default: BPM LOCK (16) is clear in every older setting
 * = unlocked, LARGE (32) = OFF, RESTORE LAST OFF (128, 1.2) = ON, read as saved (nothing to migrate, so importing twice changes nothing).
 * (Skiff 2.0, Tier B: design/TIER_B.md 6) Skiff writes SKS1 (sks_t, below) and reads PER4 still: Skiff's own (its mark
 * at [15][24]) as it is; another firmware's (Felucca's, JIANT's: no mark) for its own fields only, Skiff's at their
 * defaults. RAM keeps Skiff's settings in the favourites' spare bytes as before (ui.c's macros); nothing checks the
 * mark at run time: whatever reaches RAM is Skiff's or a default */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;
    panel_t panel;
    uint32_t bold;
    struct { uint8_t factory[16][32]; uint32_t user, filter; } favorites;
} persist_t;
#define PERSIST_MAGIC 0x50455234u
/* (2.0) "SKS1": PER4's own fields, the favourites of engines 0..14 alone (their stars: row 1's MIDI LEARN map and row
 * 14's THEME CUSTOM colours are named below, 0 there), Skiff's settings by name, and 32 bytes of room: zeros, for a
 * later 2.0's settings (kept as read, so one going back and forth keeps them). Firmware before 2.0 refuses it */
typedef struct {                               /* Skiff's settings: favorites.factory[15][0..31] in RAM, in its order */
    uint8_t restore_slot, restore_clean[4];    /* RESTORE LAST: the slot + 1 (0 none), the music's value then (project.c) */
    uint8_t cpu;                               /* bit 0 CPU 2 CORES, 1 STEP PREVIEW, 2 CPU CLOCK 320 MHZ (ui.c CPU2_BYTE) */
    uint8_t mch[6];                            /* T1..T6 CHANNEL (ui.c ui_mch) */
    uint8_t smk_kv, smk_master;                /* (SMK-37) KEY VELOCITY, MASTER's level / 4 + 1 (ui.c SMK_*_BYTE) */
    uint8_t cpal, playhead, ret, rsv17;        /* THEME CUSTOM's flags (CPAL_BYTE), PLAYHEAD LINE, RETURN (RET_BYTE) */
    uint8_t unit_g[3];                         /* the unit's CLOCK, MIDI IN, TUNE (ui.c ui_unit_g) */
    uint8_t ptag, hints_off, set_rev, rsv24;   /* PRESETS' LIST type, HINTS OFF, the one-time changes; (24: RAM's mark) */
    uint8_t perf_keys, launch, scr, rec;       /* PERFORM KEYS, LAUNCH, SCREEN OFF, CLICK .. FEEL (ui_rec_prefs) */
    uint8_t style, prefs, layers;              /* (STYLE, retired: as saved), MENU's flags (ui_prefs), the layers seen */
} sks_skiff_t;
typedef struct {
    uint32_t magic, palette, lowcut, zoom, bold;   /* as PER4's */
    panel_t panel;
    uint8_t fav[15][32];                       /* each engine's starred presets */
    uint32_t fav_user, fav_filter;
    sks_skiff_t s;
    uint8_t cpal_cols[22];                     /* THEME CUSTOM's colours (ui.c CPAL_COLS: favorites.factory[14][8..29]) */
    uint8_t ml[30];                            /* MIDI LEARN's map (midi_learn.c ml_tab: favorites.factory[1][2..31]) */
    uint8_t room[32];
} sks_t;
#define SKS_MAGIC 0x534B5331u                  /* "SKS1" */
_Static_assert(sizeof(sks_skiff_t) == 32u && ENGI_DIGITAL == 1u, "SKS1: favorites.factory[15], MIDI LEARN in row 1");

/* Normalize in place; 1 = current, 2 = migrated, 0 = invalid. */
static int settings_import_per(persist_t *p, int n)
{
    int current = n == (int)sizeof *p && p->magic == PERSIST_MAGIC;
#if FELUCCA_LEGACY                                 /* ((2.0) PER1..PER3, Felucca's before 1.0: the converter's) */
    int old3 = n == (int)(sizeof *p - sizeof p->favorites) && p->magic == 0x50455233u;
    int old2 = n == (int)(sizeof *p - sizeof p->favorites - sizeof p->bold) && p->magic == 0x50455232u;
    int old1 = n == (int)(8u + sizeof(panel_t)) && p->magic == 0x50455231u;
    if (!(current || old3 || old2 || old1)) return 0;
    if (old1) {
        panel_t old;
        memcpy(&old, (uint8_t *)p + 8, sizeof old);
        p->panel = old;
        p->lowcut = p->zoom = 0;
    }
    if (old1 || old2) p->bold = 0;
    if (!current) memset(&p->favorites, 0, sizeof p->favorites);
#else
    if (!current) return 0;
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
    if (p->favorites.factory[15][24] != 0xA5u) {   /* (no Skiff mark: Felucca's or JIANT's settings) [15][18..26]
                                                    * and bit 7 of [28] are theirs (Felucca 1.4's FX keys, LEDS INV, HOME
                                                    * view, AUTO SELECT; JIANT's SKIN, USB, LOAD..), not Skiff's CLOCK MIDI IN
                                                    * TUNE (keys 7 and 15 NONE: CLOCK TRS, PLAY dead), LIST, HINTS,
                                                    * ui_set_rev, KEYS, LAUNCH, FEEL. (2.0) Every byte of Skiff's at its
                                                    * default: row 14 (Skiff's old scenes, then THEME CUSTOM's colours),
                                                    * [15][0..26], MIDI LEARN's map (their numbers aren't Skiff's); [15][27..31]
                                                    * are Felucca's own (SCREEN OFF, CLICK .., MENU's flags), kept */
        memset((uint8_t *)p->favorites.factory + 14u * 32u, 0, 32u + 27u);
        memset(&p->favorites.factory[ENGI_DIGITAL][1], 0, 31);
        p->favorites.factory[15][28] &= 0x7Fu;
        p->favorites.factory[15][24] = 0xA5u;      /* (as Skiff's now: the record read again, the same) */
    }
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
#if defined(FM4_NPRESETS) && !FELUCCA_FM4 && FELUCCA_LEGACY
    {   /* DIGITAL's starred presets (engine 1, retired) -> the FM6 presets that cover them (fm4_convert.c) */
        uint32_t k;
        for (k = 0; k < FM4_NPRESETS; k++)
            if ((favorites.factory[ENGI_DIGITAL][0] >> k) & 1u)
                favorites.factory[ENGI_FM6][FM4_TO_FM6[k] / 8u] |= (uint8_t)(1u << (FM4_TO_FM6[k] % 8u));
        favorites.factory[ENGI_DIGITAL][0] = 0;  /* ((1.3) bytes 2..31: MIDI LEARN's map, midi_control.c ML_N) */
    }
#endif
#if defined(SMP_SET_PERC) && FELUCCA_LEGACY
    if ((favorites.factory[ENGI_SAMPLE][0] >> SMP_SET_PERC) & 1u) {   /* SAMPLE's PERC (preset 4, retired): DRUM's */
        favorites.factory[ENGI_DRUM][0] |= 1u;                         /* kit (core.h drum_from_perc) */
        favorites.factory[ENGI_SAMPLE][0] &= (uint8_t)~(1u << SMP_SET_PERC);
    }
#endif
#endif
    if (p->panel.magic == PANEL_MAGIC) panel = p->panel;
    return current ? 1 : 2;
}

/* (2.0) an SKS1's favourites and Skiff's settings from the 16 rows ff as PER4 and RAM keep them */
static void sks_pack(sks_t *s, const uint8_t (*ff)[32], uint32_t user, uint32_t filter)
{
    memcpy(s->fav, ff, sizeof s->fav);
    memset(&s->fav[ENGI_DIGITAL][2], 0, sizeof s->ml);
    memset(&s->fav[14][8], 0, sizeof s->cpal_cols);
    s->fav_user = user;
    s->fav_filter = filter;
    memcpy(&s->s, ff[15], sizeof s->s);
    s->s.rsv24 = 0;
    memcpy(s->cpal_cols, &ff[14][8], sizeof s->cpal_cols);
    memcpy(s->ml, &ff[ENGI_DIGITAL][2], sizeof s->ml);
}
/* b, n bytes of a stored settings record: SKS1, or (before 2.0) PER4 (.. PER1 with FELUCCA_LEGACY). Normalized in place;
 * 1 = current, 2 = migrated, 0 = invalid. An SKS1 is read as the PER4 it names (Skiff's: its mark), its room kept */
static int settings_import(void *b, int n)
{
    sks_t *s = (sks_t *)b;
    persist_t p;
    if (n != (int)sizeof *s || s->magic != SKS_MAGIC)
        return settings_import_per((persist_t *)b, n);
    p.magic = PERSIST_MAGIC; p.palette = s->palette; p.lowcut = s->lowcut; p.zoom = s->zoom; p.bold = s->bold;
    p.panel = s->panel;
    memcpy(p.favorites.factory, s->fav, sizeof s->fav);
    memcpy(p.favorites.factory[15], &s->s, sizeof s->s);
    p.favorites.factory[15][24] = 0xA5u;
    memcpy(&p.favorites.factory[14][8], s->cpal_cols, sizeof s->cpal_cols);
    memcpy(&p.favorites.factory[ENGI_DIGITAL][2], s->ml, sizeof s->ml);
    p.favorites.user = s->fav_user; p.favorites.filter = s->fav_filter;
    (void)settings_import_per(&p, sizeof p);
    s->palette = p.palette;
    sks_pack(s, (const uint8_t (*)[32])p.favorites.factory, p.favorites.user, p.favorites.filter);
    return 1;
}

/* Start with the last imported/saved object, including fields owned by a
 * feature absent from this build. No save-on-boot or extra flash writes. (2.0) An SKS1 */
static void settings_export(sks_t *s)
{
    s->magic = SKS_MAGIC;
    s->palette = palette_to_stored(settings.palette);
    s->lowcut = settings.lowcut;
    s->zoom = settings.zoom = leds_to_stored(settings.zoom, settings_leds);
    s->panel = panel;
    s->bold = hold_to_stored(s->bold, settings_hold);
    sks_pack(s, (const uint8_t (*)[32])favorites.factory, favorites.user, favorites.filter);
}
