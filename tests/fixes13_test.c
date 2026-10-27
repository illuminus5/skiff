/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Skiff 1.3: the fixes taken from Felucca 1.4 .. 1.6.1, on the real UI sources (with the stubs of tests/ui_test.c):
 *   PAN         a bipolar value (-64..63, F_BIPCT) reads -100..+100 %: +100 at the top (was +98) (Felucca #199)
 *   AUTOMATION  KNOB 3 keeps its row in place: a step's records in the order they were added (Felucca #199)
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

/* ---------------------------------------------------------------- PAN --- */
static int fmt_is(const param_desc_t *d, int32_t v, const char *want)
{
    char val[12];
    const char *unit;
    param_format(d, v, val, &unit);
    if (!str_eq(val, want))
        printf("  %s %d: \"%s\", want \"%s\"\n", d->label, (int)v, val, want);
    return str_eq(val, want) && str_eq(unit, "%");
}
static int pan_range(void)
{
    int bad = 0, ok = 1;
    int32_t v, last = -1000;
    char val[12];
    const char *unit;
    ok &= fmt_is(&TP[P_PAN], 63, "+100") && fmt_is(&TP[P_PAN], -64, "-100") && fmt_is(&TP[P_PAN], 0, "0");
    ok &= fmt_is(&TP[P_PAN], 32, "+50") && fmt_is(&TP[P_PAN], -32, "-50") && fmt_is(&TP[P_PAN], 1, "+1") &&
          fmt_is(&TP[P_PAN], -1, "-1");
    for (v = -64; v <= 63; v++) {                       /* every value: rising, never twice the same number */
        int32_t n = 0, i;
        param_format(&TP[P_PAN], v, val, &unit);
        for (i = val[0] == '+' || val[0] == '-'; val[i]; i++)
            n = n * 10 + (val[i] - '0');
        if (val[0] == '-')
            n = -n;
        ok &= n > last && n >= -100 && n <= 100;
        last = n;
    }
    bad += check("PAN: -64..63 read -100..+100 %, +100 at the top (was +98), rising, each value its own", ok);
    ok = fmt_is(&TP[P_M1AMT], 63, "+100") && fmt_is(&TP[P_ED_FLT], -64, "-100") &&
         TP[P_PAN].min == -64 && TP[P_PAN].max == 63 && TP[P_PAN].def == 0;
    bad += check("  every bipolar value the same (MOD AMT, ENV FLT); the stored range -64..63 as before", ok);
    return bad;
}

/* --------------------------------------------- AUTOMATION's KNOB 3 --- */
static int auto_k3(void)
{
    int bad = 0, ok = 1, i;
    track_t *t;
    uint8_t idx[MOTION_MAX];
    uint32_t p0, a, b, c0, c1;
    ui_power_on();
    sel_set(0);
    t = TSEL;
    p0 = ev_id_step(t, NSTEP, (uint32_t)-1, 1);         /* the list's first three parameters */
    a = ev_id_step(t, NSTEP, p0, 1);
    b = ev_id_step(t, NSTEP, a, 1);
    ok &= p0 < a && a < b && !motion_set_event(t, 0, a, motion_base_value(t, a)) &&
          !motion_set_event(t, 0, b, motion_base_value(t, b));
    c0 = (uint32_t)motion_find(t, 0, a);
    c1 = (uint32_t)motion_find(t, 0, b);
    go_page(GR_EVENTS); frame();
    ui.ev_top = 0;
    ui.ev_row = 1;                                      /* the second record (the later parameter) */
    frame();
    ok &= ev_rows(idx) == 2u && idx[0] == c0 && idx[1] == c1;
    bad += check("AUTOMATION: two locks on step 1, the second (a later parameter) selected", ok);
    for (i = 0; i < 2; i++) {                           /* KNOB 3 left, past the first one's parameter, and back */
        memset(ui.enc_t, 0, sizeof ui.enc_t);
        turn(EN_K3, i ? 1 : -1);
        frame();
        ok &= MOTION_ID(&motion.event[c1]) == (i ? b : p0);
        ok &= ui.ev_row == 1u && ev_rows(idx) == 2u && idx[0] == c0 && idx[1] == c1;
    }
    bad += check("  KNOB 3: the row stays where it is and selected while its parameter goes past the other's", ok);
    motion_clear(t);
    return bad;
}

int main(void)
{
    int bad = 0;
    bad += pan_range();
    bad += auto_k3();
    printf(bad ? "fixes13: %d failed\n" : "fixes13: all ok\n", bad);
    return bad != 0;
}
