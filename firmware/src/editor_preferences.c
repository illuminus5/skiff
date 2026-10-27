/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Optional device preferences, advertised in INFO. No preset format changes. The font weight
 * (ED_UI_FONT) is gone: one weight; UI_SET of it answers rc 2 (not supported), UI_STATE 127. */
enum { ED_UI_PALETTE = 1, ED_UI_FONT = 2, ED_UI_MONITOR = 4, ED_UI_FAVORITES = 8, ED_UI_COLORS = 16 };
static uint32_t ed_ui_caps(void)
{
    uint32_t caps = ED_UI_PALETTE | ED_UI_COLORS;      /* ((Skiff #27) COLORS, cmd 39) */
#ifdef FELUCCA_MONITOR
    caps |= ED_UI_MONITOR;
#endif
#ifdef FELUCCA_FAVORITES
    caps |= ED_UI_FAVORITES;
#endif
    return caps;
}
static void ed_u28(uint32_t n)
{
    for (uint32_t i = 0; i < 4; i++) ed_b((n >> (i * 7)) & 127u);
}
static void ed_ui_state(void)
{
    uint32_t sig = 0;
    ed_b(ed_ui_caps());
    ed_b(menu_get(MI_COLOR));                          /* ((Skiff #27) NPALETTES: CUSTOM) */
    ed_b(127);                                         /* font: not supported */
#ifdef FELUCCA_MONITOR
    ed_b(settings.monitor);
#else
    ed_b(127);
#endif
#ifdef FELUCCA_FAVORITES
    ed_b(favorites.filter);
    sig = 2166136261u;
    for (uint32_t i = 0; i < sizeof favorites; i++)
        sig = (sig ^ ((const uint8_t *)&favorites)[i]) * 16777619u;
#else
    ed_b(127);
#endif
    ed_u28(sig);
    ed_u28(up_gen);
}
/* Writes use the same flash path as the panel. rc 3 means applied in RAM,
 * but not saved (absent flash or a failed write); rc 4 means queued until STOP.
 * Repeated unchanged writes
 * do not wear flash. */
static uint32_t ed_ui_save(void)
{
    settings_save();
#if FELUCCA_FLASH
    if (!flash_ok || persist_pending == 2u) return 3;
    if (persist_pending == 1u) return 4;
    {
        sks_t p = persist_saved;                   /* ((2.0) everything saved: the palette, the favourites, the colours) */
        settings_export(&p);
        return memcmp(&p, &persist_saved, sizeof p) ? 3u : 0u;
    }
#else
    return 3;
#endif
}
static uint32_t ed_ui_set(const uint8_t *a, uint32_t n)
{
    if (n != 2u || a[0] > 3u) return 1;
    if (!(ed_ui_caps() & (1u << a[0]))) return 2;
    if (a[1] >= (a[0] == 0 ? menu_n(MI_COLOR) : a[0] == 2 ? 3u : 2u)) return 1;
    switch (a[0]) {
    case 0:
        menu_put(MI_COLOR, a[1]); break;
#ifdef FELUCCA_MONITOR
    case 2:
        fm1_irq_off();
        settings.monitor = a[1]; monitor_mode = a[1]; monitor_event.valid = 0;
        fm1_irq_on();
        break;
#endif
#ifdef FELUCCA_FAVORITES
    case 3:
        favorites.filter = a[1]; break;
#endif
    }
    ui.force = 1;
    return ed_ui_save();
}
/* (Skiff #27, @tradiuz) COLORS: THEME CUSTOM's colours (ui.c CPAL_*), as Felucca 1.6's PALETTE_GET / PALETTE_SET (Leo
 * Kuroshita, editor_palette.c) with the six tracks' and a look; the 22 bytes (11 RGB565, low byte first, as stored) in
 * pack7 both ways. No args: the state; op 0 shown at once (RAM only), 1 stored and in use (saved as UI_SET), 2 removed
 * (the look's own colours again), 3 the preview dropped. Any colours are taken: the editor warns of those hard to read
 * (EDITOR_PROTOCOL.md) */
static void ed_bk_pack(const uint8_t *p, uint32_t n);   /* (editor_backup.c) */
static uint8_t cpal_prev[22] __attribute__((section(".pool")));
static void ed_colors_state(void)        /* stored, in use; the look; then the set's 22 bytes, pack7 */
{
    uint32_t b = cpal_bits();                          /* (none stored: 0) */
    ed_b(b & 3u);
    ed_b(b >> 2 & 7u);
    ed_bk_pack(CPAL_COLS, sizeof cpal_prev);
}
static uint32_t ed_colors(const uint8_t *a, uint32_t n)   /* rc: 0 done, 1 arguments; store, remove: 3, 4 as UI_SET */
{
    if (n == 1u && a[0] == 2u)                         /* 2: removed */
        CPAL_BYTE = 0;
    else if (n != 1u || a[0] != 3u) {                  /* (3: the preview dropped, nothing saved) */
        if (n < 2u || a[0] > 1u || a[1] >= NPALETTES ||
            ed_unpack7(a + 2, n - 2u, cpal_prev, sizeof cpal_prev) != sizeof cpal_prev)
            return n != 0u;                            /* (none: the state) */
        ui.force = 1;
        if (!a[0]) {                                   /* (THEME, op 3 or a restart: the theme in use again) */
            ux_cpal = cpal_prev;
            palette_set(a[1]);
            return 0;
        }
        memcpy(CPAL_COLS, cpal_prev, sizeof cpal_prev);
        CPAL_BYTE = (uint8_t)(3u | a[1] << 2);
        settings.palette = a[1];
    }
    theme_apply();
    ui.force = 1;
    return a[0] == 3u ? 0u : ed_ui_save();
}
static int ed_ui_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    switch (cmd) {
    case ED_COLORS:
        ed_b(ed_colors(a, n));
        ed_colors_state();
        return 1;
    case ED_UI_STATE:
        ed_ui_state(); return 1;
    case ED_UI_SET:
        ed_b(ed_ui_set(a, n));
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u);
        ed_ui_state(); return 1;
    case ED_UI_PALETTES: {
        uint32_t k = menu_n(MI_COLOR);                 /* ((Skiff #27) and CUSTOM, last, while one is stored) */
        ed_b(k);
        for (uint32_t i = 0; i < k; i++) ed_str(menu_vname(MI_COLOR, i), 12);
        return 1;
    }
    case ED_FAV_GET:
#ifdef FELUCCA_FAVORITES
        if (n == 4u && a[0] <= NENGINES) {
            int32_t start = ed_rv(a + 1);
            uint32_t limit = a[0] == NENGINES ? UP_SLOTS : ENGINES[a[0]]->npresets;
            if (start >= 0 && a[3] && a[3] <= 32u && (uint32_t)start + a[3] <= limit) {
                ed_b(0); ed_b(a[0]); ed_v(start); ed_b(a[3]);
                for (uint32_t i = 0; i < a[3]; i++) ed_b(favorite_has(a[0], (uint32_t)start + i));
                return 1;
            }
        }
        ed_b(1);
#else
        ed_b(2);
#endif
        return 1;
    case ED_FAV_SET:
#ifdef FELUCCA_FAVORITES
        if (n == 4u && a[0] <= NENGINES && a[3] <= 1u) {
            int32_t preset = ed_rv(a + 1);
            uint32_t limit = a[0] == NENGINES ? UP_SLOTS : ENGINES[a[0]]->npresets;
            if (preset >= 0 && (uint32_t)preset < limit &&
                (a[0] != NENGINES || !a[3] || up_used((uint32_t)preset))) {
                favorite_set(a[0], (uint32_t)preset, a[3]);
                ui.force = 1;
                ed_b(ed_ui_save()); ed_b(a[0]); ed_v(preset); ed_b(a[3]);
                return 1;
            }
        }
        ed_b(1);
#else
        ed_b(2);
#endif
        return 1;
    default: return 0;
    }
}
