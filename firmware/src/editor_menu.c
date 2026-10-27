/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* MENU's settings for the editor (EDITOR_PROTOCOL.md "MENU settings"): MENU_DESC describes one, MENU_SET sets one;
 * INFO 4E 01 count. The editor builds its settings from these; the values, names and the apply path are the menu's
 * own (menu_items.c), so the two cannot differ. CALIBRATION and ABOUT are not offered (no value). 1.0.5: each reply
 * ends with the row's MENU tab (MI_TAB: its index and name), so the editor can group the settings as the device does.
 * ED_MENU: {row, id}, the 1.0.4 settings in the menu's order, then the later ones as they came (1.1: CLICK, CLICK LEVEL,
 * COUNT-IN, rows of AUDIO; 1.2: RESTORE LAST, SYSTEM; SCALE LEDS, CONTROL; 1.1.5: SCREEN OFF, DISPLAY): an index keeps its
 * setting too, so an older editor lists what it knew where it was (the
 * editor groups them by their tab). An id keeps its meaning for good: a new setting takes the next free id
 * (append-only), wherever its row goes; ids 0..126 (127: none). */
enum { ED_MENU_DESC = 72, ED_MENU_SET };
enum { EDM_ENUM, EDM_INT };                            /* kind: names follow; or a unit (none yet) */
static const uint8_t ED_MENU[][2] = {
    {MI_COLOR, 0}, {MI_LARGE, 2},              /* ((Skiff U2) id 1, STYLE, retired) */
#if BOARD_SMK37
    {MI_ANIM, 3}, {MI_HOLD, 5}, {MI_ACCEL, 6},         /* (SMK-37: no LEDS row) */
#else
    {MI_ANIM, 3}, {MI_LEDS, 4}, {MI_HOLD, 5}, {MI_ACCEL, 6},
#endif
    {MI_BPMLOCK, 8},                   /* ((Skiff U2) id 7, FX LATCH, retired) */
    {MI_LOWCUT, 9}, {MI_USB, 10},
#if FELUCCA_CDC
    {MI_SERIAL, 11},                                   /* ((Skiff 2.0) none in a build without the console: a release) */
#endif
    {MI_CLICK, 12}, {MI_CLKLVL, 13}, {MI_COUNTIN, 14},
    {MI_RESTORE, 15},                                  /* 1.2 (Discussion #130), a row of SYSTEM */
    {MI_SCLLED, 16},                                   /* 1.2 (Discussion #127), a row of CONTROL */
    {MI_SCROFF, 17},                                   /* 1.1.5, a row of DISPLAY */
#if BOARD_SMK37
    {MI_KVEL, 18},                                     /* (SMK-37) KEY VELOCITY, a row of CONTROL */
#endif
    {MI_PHEAD, 19},                                    /* (Skiff #27) PLAYHEAD, a row of DISPLAY */
    {MI_RETURN, 20},                                   /* (2.0, D 2A) RETURN, a row of CONTROL */
};
#define ED_MENU_N NELEM(ED_MENU)

/* rc as UI_SET: 0 applied and saved, 3 applied in RAM only (no flash, a failed write), 4 queued until STOP */
static uint32_t ed_menu_save(void) { return ed_ui_save(); }   /* ((2.0) the same check: editor_preferences.c) */

static int ed_menu_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t i, row, k;
    int32_t v;
    if (cmd == ED_MENU_DESC) {                         /* index -> index, id, kind, value, min, max (v14), name, names,
                                                          * (1.0.5) its tab: index, name */
        ed_b(a[0]);
        if (a[0] >= ED_MENU_N) {
            ed_b(127);                                 /* no such item: the list ends */
            return 1;
        }
        row = ED_MENU[a[0]][0];
        k = menu_n(row);
        ed_b(ED_MENU[a[0]][1]);
        ed_b(EDM_ENUM);
        ed_v((int32_t)menu_get(row));
        ed_v(0);
        ed_v((int32_t)k - 1);
        ed_str(MI_NAME[row], 12);
        for (i = 0; i < k; i++)
            ed_str(menu_vname(row, i), 12);
        ed_b(MI_TAB[row]);                             /* (after the names: an older editor stops before it) */
        ed_str(MTAB_NAME[MI_TAB[row]], 12);
        return 1;
    }
    if (cmd != ED_MENU_SET)
        return 0;
    (void)n;                                           /* id, value v14 -> rc, id, value v14 (the device's, clamped) */
    v = ed_rv(a + 1);
    for (i = 0; i < ED_MENU_N && ED_MENU[i][1] != a[0]; i++)
        ;
    if (i == ED_MENU_N) {
        ed_b(1); ed_b(a[0]); ed_v(v);
        return 1;
    }
    row = ED_MENU[i][0];
    menu_put(row, (uint32_t)clamp(v, 0, (int32_t)menu_n(row) - 1));
    if (row == MI_SERIAL)
        menu_serial_at = fm1_ms | 1u;                  /* (applied 200 ms after this reply: usb_serial_apply) */
    ui.force = 1;                                      /* (the MENU page, if shown, redraws) */
    ed_b(ed_menu_save());
    ed_b(a[0]);
    ed_v((int32_t)menu_get(row));
    return 1;
}
