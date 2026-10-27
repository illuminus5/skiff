/* SPDX-License-Identifier: GPL-3.0-only */
/* (FM6 LAB) EDIT > OPERATOR and ALL OPS on an FM6 track: every value of the track's patch (eng_fm6.c fm6_patch), on
 * two sheets (ui.c sheet_t; ui_input.c SHEETS). Included by ui_input.c.
 *   PRESETS: the operator, OP1 .. OP6, on both. SELECT: the windows (past their ends, the pages either side).
 *   OPERATOR  TONE   OUTPUT RATIO FINE DETUNE          ALL OPS  ALGO     ALGO FEEDBACK TRANSP
 *             ENV 1  RATE 1 LEVEL 1 RATE 2 LEVEL 2              PITCH 1  RATE 1 PITCH 1 RATE 2 PITCH 2
 *             ENV 2  RATE 3 LEVEL 3 RATE 4 LEVEL 4              PITCH 2  RATE 3 PITCH 3 RATE 4 PITCH 4
 *             KEYS   L CURVE L DEPTH R DEPTH R CURVE            LFO      WAVE RATE FADE KEY SYNC
 *             MORE   BREAK KEY RATE VELOCITY LFO AMP            DEPTH    VIBRATO VIB SENS TREMOLO
 *   A knob edits a copy of the patch: fm6_set_patch (the audio ISR's copy follows), fm6_adopt (SLOT: OWN, or the
 *   factory patch it now is). ALGO and FEEDBACK show what plays (EDIT 1's ALGO and FEEDBACK on the patch) and turning
 *   them puts that in the patch, the macro back to neutral. OKS is not here: the engine never reads it.
 * The panel (design/mock_fm6.py, direction D, the user's pick; after an idea in a video by Floyd Steinberg, drawn our
 * own way): the operators' envelopes as the engine runs them (fm6_op_env_init, the macros through fm6_macro), each as
 * straight segments between its breakpoints on a log time axis (the held note, then its release); the modulators
 * thin in their own colours (the palette's six), each carrier thick and shaded along its length from cool (a sine) to
 * hot (the strongest modulator feeding it at that moment: FM's bite); the operator picked: its breakpoints as handles.
 * KEYS and MORE: its level across the keyboard. ALGO: the chart (ui_graph.c graph_fm6). PITCH: the pitch envelope;
 * LFO and DEPTH: the vibrato over two seconds. All drawn in the theme's colours (the ramp: blue to REC). */

enum { FMW_TONE, FMW_ENV1, FMW_ENV2, FMW_KEYS, FMW_MORE };          /* OPERATOR's windows */
enum { FMW_ALGO, FMW_PITCH1, FMW_PITCH2, FMW_LFO, FMW_DEPTH };      /* ALL OPS' */
enum { FMF_NUM, FMF_RATIO, FMF_FINE, FMF_DET, FMF_CURVE, FMF_NOTE, FMF_ALG, FMF_FB, FMF_TRNSP, FMF_PITCH, FMF_WAVE,
       FMF_HZ, FMF_ONOFF };
#define FM_NWIN 5u
typedef struct {
    const char *label;           /* 0: an empty card */
    uint8_t off, fmt;            /* OPERATOR: the byte in the operator (0..20); ALL OPS: in the patch */
} fm_field_t;
static const fm_field_t FM_F[2][FM_NWIN][4] = {
    {{{"OUTPUT", FP_OL, FMF_NUM}, {"RATIO", FP_FC, FMF_RATIO}, {"FINE", FP_FF, FMF_FINE}, {"DETUNE", FP_DET, FMF_DET}},
     {{"RATE 1", FP_R1, FMF_NUM}, {"LEVEL 1", FP_L1, FMF_NUM}, {"RATE 2", FP_R1 + 1, FMF_NUM}, {"LEVEL 2", FP_L1 + 1, FMF_NUM}},
     {{"RATE 3", FP_R1 + 2, FMF_NUM}, {"LEVEL 3", FP_L1 + 2, FMF_NUM}, {"RATE 4", FP_R1 + 3, FMF_NUM},
      {"LEVEL 4", FP_L1 + 3, FMF_NUM}},
     {{"L CURVE", FP_LC, FMF_CURVE}, {"L DEPTH", FP_LD, FMF_NUM}, {"R DEPTH", FP_RD, FMF_NUM}, {"R CURVE", FP_RC, FMF_CURVE}},
     {{"BREAK", FP_BP, FMF_NOTE}, {"KEY RATE", FP_RS, FMF_NUM}, {"VELOCITY", FP_KVS, FMF_NUM}, {"LFO AMP", FP_AMS, FMF_NUM}}},
    {{{"ALGO", FP_ALG, FMF_ALG}, {"FEEDBACK", FP_FB, FMF_FB}, {"TRANSP", FP_TRNSP, FMF_TRNSP}, {0, 0, 0}},
     {{"RATE 1", FP_PR1, FMF_NUM}, {"PITCH 1", FP_PL1, FMF_PITCH}, {"RATE 2", FP_PR1 + 1, FMF_NUM},
      {"PITCH 2", FP_PL1 + 1, FMF_PITCH}},
     {{"RATE 3", FP_PR1 + 2, FMF_NUM}, {"PITCH 3", FP_PL1 + 2, FMF_PITCH}, {"RATE 4", FP_PR1 + 3, FMF_NUM},
      {"PITCH 4", FP_PL1 + 3, FMF_PITCH}},
     {{"WAVE", FP_LFW, FMF_WAVE}, {"RATE", FP_LFS, FMF_HZ}, {"FADE", FP_LFD, FMF_NUM}, {"KEY SYNC", FP_LKS, FMF_ONOFF}},
     {{"VIBRATO", FP_LPMD, FMF_NUM}, {"VIB SENS", FP_LPMS, FMF_NUM}, {"TREMOLO", FP_LAMD, FMF_NUM}, {0, 0, 0}}},
};
static const char *const FM_CURVE[4] = {"-LIN", "-EXP", "+EXP", "+LIN"};
static const char *const FM_WAVE[6] = {"TRI", "SAW DN", "SAW UP", "SQUARE", "SINE", "RANDOM"};
static const char *const FM_HZ[4] = {"1", "10", "100", "1K"};          /* a fixed operator's range (FC & 3) */

static uint32_t fm_tr(void) { return trk_index(TSEL) % NTRK; }
/* field f's byte in the patch (operator fm_ui[0] on OPERATOR) */
static uint32_t fm_at(uint32_t pg, const fm_field_t *f)
{
    return pg ? f->off : (5u - fm_ui[0] % 6u) * FP_OP + f->off;
}
/* the RATIO knob's position: x0.5, x1 .. x31 (FC 0..31), then 1, 10, 100, 1K HZ (MODE fixed, FC 0..3) */
static int32_t fm_ratio_pos(const uint8_t *op) { return op[FP_MODE] ? 32 + (op[FP_FC] & 3) : op[FP_FC]; }

/* a signed number: "+3", "-3", "0" */
static void fm_signed(char *b, int32_t v)
{
    if (v > 0)
        *b++ = '+';
    fmt_int(b, v);
}
/* tenths: "-1.5", "5.5", "+0.4" (sign: a plus too) */
static void fm_tenths(char *b, int32_t v, int sign)
{
    if (v < 0)
        *b++ = '-';
    else if (sign && v > 0)
        *b++ = '+';
    v = v < 0 ? -v : v;
    fmt_int(b, v / 10);
    b += str_len(b);
    *b++ = '.';
    *b++ = (char)('0' + v % 10);
    *b = 0;
}
/* hundredths: "14.00", "0.50" */
static void fm_hundredths(char *b, int32_t v)
{
    fmt_int(b, v / 100);
    b += str_len(b);
    *b++ = '.';
    *b++ = (char)('0' + v / 10 % 10);
    *b++ = (char)('0' + v % 10);
    *b = 0;
}
/* a fixed-frequency operator's Hz x 100 (fm6_osc_base: log2 Hz in 1/100 decades of FC & 3 and FF) */
static uint32_t fm_fixed_hz100(const uint8_t *op)
{
    int32_t x = (4458616 * ((op[FP_FC] & 3) * 100 + op[FP_FF] % 100u)) >> 3, i = x >> 24;   /* 1 .. 9772 Hz: 0 .. 13 */
    return (uint32_t)(((uint64_t)fm6_mant(x) * 100u) >> (30 - i));
}
/* the LFO's rate in tenths of a Hz (FM6_LFO_DELTA a block, 44100 / 32 blocks a second) */
static uint32_t fm_lfo_hz10(uint32_t v) { return (uint32_t)(((uint64_t)FM6_LFO_DELTA[v % 100u] * 13781u) >> 32); }

/* field f's card: its value, unit and gauge (0..1000) from patch p, track t */
static int32_t fm_val(const fm_field_t *f, uint32_t i, const uint8_t *p, const track_t *t, char *val, const char **unit)
{
    const uint8_t *op = p + (i / FP_OP) * FP_OP;
    uint32_t v = p[i], mx = fm6_max(i);
    *unit = "";
    switch (f->fmt) {
    case FMF_RATIO: {
        int32_t pos = fm_ratio_pos(op);
        if (op[FP_MODE]) {
            str_cpy(val, FM_HZ[op[FP_FC] & 3u], 8);
            *unit = "HZ";
        } else {
            if (op[FP_FC])
                fmt_int(val, op[FP_FC]);
            else
                str_cpy(val, "0.5", 8);
            *unit = "x";
        }
        return pos * 1000 / 35;
    }
    case FMF_FINE:
        if (op[FP_MODE]) {                              /* the frequency it plays */
            uint32_t h = fm_fixed_hz100(op);
            if (h < 1000u)
                fm_hundredths(val, (int32_t)h);
            else if (h < 10000u)
                fm_tenths(val, (int32_t)h / 10, 0);
            else
                fmt_int(val, (int32_t)(h / 100u));
            *unit = "HZ";
        } else                                          /* the ratio it plays */
            fm_hundredths(val, op[FP_FC] ? op[FP_FC] * (100 + (int32_t)op[FP_FF]) : (100 + (int32_t)op[FP_FF]) / 2);
        break;
    case FMF_DET:
        fm_signed(val, (int32_t)v - 7);
        break;
    case FMF_CURVE:
        str_cpy(val, FM_CURVE[v & 3u], 8);
        break;
    case FMF_NOTE:
        note_name(val, v + 21u);
        break;
    case FMF_ALG:
        v = fm6_alg_of(t);
        fmt_int(val, (int32_t)v + 1);
        break;
    case FMF_FB:
        v = (uint32_t)clamp((int32_t)p[FP_FB] + t->p[P_E1], 0, 7);
        fmt_int(val, (int32_t)v);
        break;
    case FMF_TRNSP:
        fm_signed(val, (int32_t)v - 24);
        *unit = "ST";
        break;
    case FMF_PITCH:
        fm_tenths(val, FM6_PENV_TAB[v % 100u] * 15 / 4, 1);   /* (1/32 octave a step: 3/8 of a semitone) */
        *unit = "ST";
        break;
    case FMF_WAVE:
        str_cpy(val, FM_WAVE[v % 6u], 8);
        break;
    case FMF_HZ:
        fm_tenths(val, (int32_t)fm_lfo_hz10(v), 0);
        *unit = "HZ";
        break;
    case FMF_ONOFF:
        str_cpy(val, v ? "ON" : "OFF", 8);
        break;
    default:
        fmt_int(val, (int32_t)v);
        break;
    }
    return (int32_t)(v * 1000u / (mx ? mx : 1u));
}

/* KNOB k (0..3) turned s on sheet pg (0 OPERATOR, 1 ALL OPS) */
static void fm_knob(uint32_t pg, uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    uint32_t tr = fm_tr(), w = fm_ui[1 + pg] % FM_NWIN, i, mx;
    const fm_field_t *f = &FM_F[pg & 1u][w][k & 3u];
    uint8_t v[FP_SIZE + 1u];
    int32_t d = s > 0 ? 1 : -1, x;
    if (!f->label || t->eng_req % NENGINES != ENGI_FM6)
        return;
    i = fm_at(pg, f);
    mx = fm6_max(i);
    memcpy(v, fm6_patch[tr], sizeof v);
    switch (f->fmt) {
    case FMF_RATIO: {                                   /* one knob over both modes: x0.5 .. x31, 1 .. 1K HZ */
        uint8_t *op = v + (i / FP_OP) * FP_OP;
        x = clamp(fm_ratio_pos(op) + accel(EN_K1 + k, s, 36), 0, 35);
        op[FP_MODE] = (uint8_t)(x >= 32);
        op[FP_FC] = (uint8_t)(x >= 32 ? x - 32 : x);
        break;
    }
    case FMF_ALG:                                       /* what plays, into the patch; ALGO (EDIT 1) to PAT */
        v[FP_ALG] = (uint8_t)clamp((int32_t)fm6_alg_of(t) + accel(EN_K1 + k, s, 32), 0, 31);
        t->p[P_E0] = 0;
        break;
    case FMF_FB:                                        /* the same: FEEDBACK (EDIT 1) to 0 */
        v[FP_FB] = (uint8_t)clamp(clamp((int32_t)v[FP_FB] + t->p[P_E1], 0, 7) + d, 0, 7);
        t->p[P_E1] = 0;
        break;
    default:
        v[i] = (uint8_t)clamp((int32_t)v[i] + (mx > 7u ? accel(EN_K1 + k, s, (int32_t)mx) : d), 0, (int32_t)mx);
        break;
    }
    fm6_set_patch(tr, v);
    fm6_adopt(tr);
}
static void fm_op_knob(uint32_t k, int32_t s) { fm_knob(0, k, s); }
static void fm_all_knob(uint32_t k, int32_t s) { fm_knob(1, k, s); }

/* the four cards: the window's */
static void fm_cards_of(uint32_t pg)
{
    const uint8_t *p = fm6_patch[fm_tr()];
    uint32_t w = fm_ui[1 + pg] % FM_NWIN, k;
    for (k = 0; k < 4u; k++) {
        const fm_field_t *f = &FM_F[pg & 1u][w][k];
        char val[12];
        const char *unit;
        int32_t g;
        if (!f->label) {
            draw_column(k, "", "", "", VR_THEME, -1, ICON_NONE);
            continue;
        }
        g = fm_val(f, fm_at(pg, f), p, TSEL, val, &unit);
        draw_column(k, f->label, val, unit, VR_LIVE, g, ICON_NONE);
    }
}
static void fm_op_cards(void) { fm_cards_of(0); }
static void fm_all_cards(void) { fm_cards_of(1); }

static void fm_op_turn(int32_t s) { fm_ui[0] = (uint8_t)clamp((int32_t)(fm_ui[0] % 6u) + s, 0, 5); }
static const char *fm_op_title(void)                 /* the header's: "OPERATOR 3" */
{
    static char t[12];
    str_cpy(t, "OPERATOR ", sizeof t);
    t[9] = (char)('1' + fm_ui[0] % 6u);
    t[10] = 0;
    return t;
}
static const char *fm_all_title(void) { return "ALL OPS"; }
/* what the cards and the panel show besides the track's values (ui_graph.c graph_signature hashes those) */
static uint32_t fm_sig(void)
{
    return (fm6_pgen[fm_tr()] + 1u) * 2246822519u ^ (fm_ui[0] + fm_ui[1] * 7u + fm_ui[2] * 49u) * 40503u ^
           (ui.hot_t ? ui.hot_col + 1u : 0u) * 65537u;
}

/* ------------------------------------------------------------------ the panel: the note as the engine plays it --- */
#define FM_X0 10                                        /* the graph: x 10 .. 229, y 26 .. 119 (the canvas's) */
#define FM_XW 220
#define FM_HW 160                                       /* the held note's columns; the release the rest */
#define FM_GY 26
#define FM_GH 94
#define FM_CY 124                                       /* the caption's row, 12 high */
#define FM_TOP (15 << 16)                               /* levels Q16 log2 (6 dB a unit): full scale, */
#define FM_SPAN (10 << 16)                              /* the 60 dB drawn, */
#define FM_HEAT (10 << 16)                              /* a modulator's 60 dB of heat */
#define FM_TAU 7056u                                    /* the log time axis' knee: 20 ms in 1/256 blocks */
#define FM_NBP 12                                       /* an envelope's breakpoints at most: start, stages, key up, end */
#define FM_GUIDE (ux.crypt ? T_DIM : T_LINE)            /* (CRYPT's stone eats LINE) */
typedef struct {
    int32_t i[FM_NBP], v[FM_NBP];                      /* the block it is reached at; the level (Q16 log2 / semitone Q16) */
    int16_t x[FM_NBP];                                  /* its column */
    uint8_t n, end[4];                                  /* how many; the point each stage ended at (0xFF: none) */
} fm_env_line_t;
typedef struct {
    fm_env_line_t op[6], pitch;                         /* OP1 .. OP6, the pitch envelope */
    int32_t held, n, modlvl;                            /* the key up's block, the last; MOD LVL on the modulators (Q16) */
    uint32_t car, live, alg, fb;                        /* bit k: OP k+1 a carrier, sounding (OUTPUT > 0) */
    uint8_t q[FP_SIZE + 1u];                            /* the patch through the macros */
} fm_sim_t;

/* log2 x, Q16 (x > 0): the exponent, the mantissa's log2 ~ f + 0.34 f (1 - f) (within 0.004) */
static int32_t fm_log2(uint32_t x)
{
    int32_t e = 0;
    uint32_t f;
    while (x >= 2u << e && e < 31)
        e++;
    f = (e > 16 ? x >> (e - 16) : x << (16 - e)) - 65536u;
    return (e << 16) + (int32_t)f + (int32_t)(((f >> 1) * ((65536u - f) >> 1) >> 14) * 22u >> 6);
}
/* block b's column: the held part, the release, each on a log axis (the attack gets room) */
static int32_t fm_warp(const fm_sim_t *s, int32_t b)
{
    int32_t l0 = fm_log2(FM_TAU);
    if (b <= s->held)
        return (FM_HW - 1) * (fm_log2((uint32_t)b * 256u + FM_TAU) - l0) / (fm_log2((uint32_t)s->held * 256u + FM_TAU) - l0);
    return FM_HW + (FM_XW - FM_HW - 1) * (fm_log2((uint32_t)(b - s->held) * 256u + FM_TAU) - l0) /
                       (fm_log2((uint32_t)(s->n - s->held) * 256u + FM_TAU) - l0);
}
static void fm_bp(fm_env_line_t *l, int32_t i, int32_t v)
{
    if (l->n < FM_NBP) {
        l->i[l->n] = i;
        l->v[l->n++] = v;
    }
}
/* the note (MIDI 60 + TRANSP, velocity 100), its key up at `held` (< 0: found: once every sounding carrier holds,
 * a quarter more, at least 0.35 s; none by 4 s: at 4 s) and its release (the carriers 60 dB down, at most 2 s) */
static void fm_run(fm_sim_t *s, const track_t *t, int32_t held)
{
    fm6_env_t e[6];
    fm6_penv_t pe;
    uint32_t k, prev[6], pp = 0;
    int32_t b, note, cap = held >= 0 ? held : 4 * 1378, last[7];
    note = 60 + s->q[FP_TRNSP] - 24;
    for (k = 0; k < 6u; k++) {
        fm6_op_env_init(&e[k], s->q + (5u - k) * FP_OP, note, 100);
        prev[k] = 0;
        if (held >= 0) {
            memset(&s->op[k], 0, sizeof s->op[k]);
            memset(s->op[k].end, 0xFF, 4);
            fm_bp(&s->op[k], 0, 0);
        }
    }
    fm6_penv_init(&pe, s->q);
    if (held >= 0) {
        memset(&s->pitch, 0, sizeof s->pitch);
        memset(s->pitch.end, 0xFF, 4);
        fm_bp(&s->pitch, 0, pe.level >> 8);
    }
    for (b = 0;; b++) {
        uint32_t quiet = 1, hold = 1;
        if (b == cap) {
            if (held < 0) {                             /* none held by 4 s */
                fm_run(s, t, cap);
                return;
            }
            for (k = 0; k < 6u; k++)
                fm6_env_key(&e[k], s->q + (5u - k) * FP_OP, 0);
            fm6_penv_key(&pe, s->q, 0);
        }
        for (k = 0; k < 6u; k++) {
            int32_t lv = last[k] = fm6_env_get(&e[k], s->q + (5u - k) * FP_OP) >> 8;
            if ((s->car >> k & 1u) && (s->live >> k & 1u)) {
                hold &= e[k].ix >= 3u;
                quiet &= lv < FM_TOP - FM_SPAN;
            }
            if (held >= 0 && (e[k].ix != prev[k] || b + 1 == held)) {
                fm_env_line_t *l = &s->op[k];
                uint32_t j;
                fm_bp(l, b + 1, lv);
                for (j = prev[k]; j < e[k].ix && j < 4u; j++)
                    l->end[j] = (uint8_t)(l->n - 1u);
                prev[k] = e[k].ix;
            }
        }
        {
            int32_t pv = last[6] = fm6_penv_get(&pe, s->q) >> 8;
            if (held >= 0 && (pe.ix != pp || b + 1 == held)) {
                uint32_t j;
                fm_bp(&s->pitch, b + 1, pv);
                for (j = pp; j < pe.ix && j < 4u; j++)
                    s->pitch.end[j] = (uint8_t)(s->pitch.n - 1u);
                pp = pe.ix;
            }
        }
        if (held < 0 && hold) {                         /* found: run it again with the key up there */
            b++;
            fm_run(s, t, b + b / 4 > 482 ? b + b / 4 : 482);
            return;
        }
        if (held >= 0 && b >= cap && (quiet || b - cap >= 2 * 1378)) {
            s->n = b + 1;
            break;
        }
    }
    s->held = cap;
    for (k = 0; k < 7u; k++) {                          /* the end of each line, and every point's column */
        fm_env_line_t *l = k < 6u ? &s->op[k] : &s->pitch;
        uint32_t j;
        if (l->n && l->i[l->n - 1u] < s->n) {
            if (l->n == FM_NBP)
                l->n--;
            fm_bp(l, s->n, last[k]);
        }
        for (j = 0; j < l->n; j++)
            l->x[j] = (int16_t)fm_warp(s, l->i[j]);
    }
}
/* the patch as it plays (the macros), its algorithm, carriers and sounding operators */
static void fm_ops(fm_sim_t *s, const track_t *t)
{
    uint32_t k;
    memcpy(s->q, fm6_patch[trk_index(t) % NTRK], FP_SIZE);
    s->alg = fm6_alg_of(t);
    s->fb = (uint32_t)clamp((int32_t)s->q[FP_FB] + t->p[P_E1], 0, 7);
    fm6_macro(s->q, s->alg, &t->p[P_E0]);
    s->car = fm6_car_ops(s->alg);
    s->live = 0;
    for (k = 0; k < 6u; k++)
        s->live |= (uint32_t)(s->q[(5u - k) * FP_OP + FP_OL] != 0) << k;
    s->modlvl = clamp(t->p[P_E2], -64, 63) * 24 << 8;   /* (fm6_render: microsteps of 1/32 of an outlevel step) */
}
static void fm_sim(fm_sim_t *s, const track_t *t)
{
    fm_ops(s, t);
    s->held = 0;
    fm_run(s, t, -1);
}
/* line l's level at column x: between its breakpoints */
static int32_t fm_at_x(const fm_env_line_t *l, int32_t x)
{
    uint32_t j;
    for (j = 1; j < l->n; j++)
        if (x <= l->x[j]) {
            int32_t xa = l->x[j - 1u], xb = l->x[j];
            return xb <= xa ? l->v[j] : l->v[j - 1u] + (l->v[j] - l->v[j - 1u]) * (x - xa) / (xb - xa);
        }
    return l->n ? l->v[l->n - 1u] : 0;
}
static int32_t fm_lvy(int32_t v)
{
    return FM_GY + 4 + clamp(FM_TOP - v, 0, FM_SPAN) * (FM_GH - 9) / FM_SPAN;
}
/* the operators feeding OP c+1, directly or through others (bit k: OP k+1) */
static uint32_t fm_feeders(uint32_t alg, uint32_t c)
{
    uint8_t m[6];
    uint32_t seen = 0, todo = 1u << c, k;
    fm6_routes(alg, m);
    while (todo) {
        for (k = 0; !(todo >> k & 1u); k++)
            ;
        todo &= ~(1u << k);
        todo |= m[k] & ~seen;
        seen |= m[k];
    }
    return seen;
}
/* carrier c's heat at column x, 0 .. 11: the strongest modulator feeding it (and the feedback, by FB / 7, where its
 * operator is c or one of them), its level over its top 60 dB, the square root (a little FM is heard at once) */
static uint32_t fm_heat(const fm_sim_t *s, uint32_t c, uint32_t fed, int32_t x)
{
    static const uint8_t SQ[11] = {1, 5, 13, 26, 43, 64, 89, 119, 153, 191, 233};   /* 256 ((i + 0.5) / 11)^2 */
    int32_t best = 0, n;
    uint32_t k, f = fm6_fb_op(s->alg), i;
    for (k = 0; k < 6u; k++)
        if ((fed >> k & 1u) && (s->live >> k & 1u)) {
            n = clamp((fm_at_x(&s->op[k], x) + s->modlvl - (FM_TOP - FM_HEAT)) * 16 / (FM_HEAT >> 4), 0, 256);
            best = n > best ? n : best;
        }
    if (s->fb && (s->live >> f & 1u) && (f == c || (fed >> f & 1u))) {
        int32_t lv = fm_at_x(&s->op[f], x) + ((s->car >> f & 1u) ? 0 : s->modlvl);
        n = clamp((lv - (FM_TOP - FM_HEAT)) * 16 / (FM_HEAT >> 4), 0, 256) * (int32_t)s->fb / 7;
        best = n > best ? n : best;
    }
    for (i = 0; i < 11u && best >= SQ[i]; i++)
        ;
    return i;
}
/* the 12 shades, a sine to FM's bite: cool blue to REC; (FM6 LAB T) CRYPT fire (ash, blood, ember, gold, flame), NEON
 * the night to the sunset (cyan, violet, pink, orange, yellow), QUARK energy (dim cyan, cyan, white, gold) */
static void fm_ramp(uint16_t *r)
{
    uint16_t st[5];
    uint32_t n = 2, i;
    if (ux.crypt) {
        st[0] = T_DIM, st[1] = CR_BLOOD, st[2] = CR_EMBER, st[3] = CR_GOLD, st[4] = CR_FLAME, n = 5;
    } else if (ux.neon) {
        st[0] = T_ACCENT, st[1] = NE_VIOLET, st[2] = T_THEME, st[3] = NE_ORANGE, st[4] = NE_YELLOW, n = 5;
    } else if (ux.quark) {
        st[0] = ux_mix(T_SURF, T_THEME, 40), st[1] = T_THEME, st[2] = T_TEXT, st[3] = T_ACCENT, n = 4;
    } else {
        st[0] = ux_mix(T_SURF, RGB(40, 92, 232), 80), st[1] = T_REC;
    }
    for (i = 0; i < 12u; i++)
        r[i] = ne_ramp(st, n, (int32_t)i * 1000 / 11);
}
/* a round brush w px wide (2..5) at x, y ((FM6 LAB T) CRYPT: square, its ASCII strokes' chunky pixels) */
static void fm_brush(int32_t x, int32_t y, int32_t w, uint16_t c)
{
    int32_t a = -(w / 2);
    if (w > 3 && !ux.crypt) {
        cv_rect(x + a, y + a + 1, w, w - 2, c);
        cv_rect(x + a + 1, y + a, w - 2, w, c);
    } else
        cv_rect(x + a, y + a, w, w, c);
}
/* a straight segment with the brush; its colour per column from sh[shade[column]] (or c when shade is 0); w < 0: dotted
 * (QUARK: a bubble chamber's track), a dot every 3 px */
static void fm_seg(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t w, uint16_t c, const uint8_t *shade,
                   const uint16_t *sh)
{
    int32_t n = 2 * ((xb > xa ? xb - xa : xa - xb) > (yb > ya ? yb - ya : ya - yb) ? (xb > xa ? xb - xa : xa - xb)
                                                                                   : (yb > ya ? yb - ya : ya - yb)), j;
    int dot = w < 0;
    if (dot)
        w = -w;
    if (n < 1)
        n = 1;
    for (j = 0; j <= n; j++) {
        int32_t x = xa + (xb - xa) * j / n, y = ya + (yb - ya) * j / n;
        if (!dot || j / 2 % 3 == 0)
            fm_brush(x, y, w, shade ? sh[shade[clamp(x - FM_X0, 0, FM_XW - 1)]] : c);
    }
}
/* a line in c over its shadow ((FM6 LAB T) NEON: over its glow, a tube's) */
static void fm_seg2(int32_t xa, int32_t ya, int32_t xb, int32_t yb, int32_t w, uint16_t c)
{
    if (ux.neon)
        fm_seg(xa, ya, xb, yb, w + 2, ux_mix(c, T_SURF, 62), 0, 0);
    else
        fm_seg(xa, ya + 1, xb, yb + 1, w, T_BG, 0, 0);
    fm_seg(xa, ya, xb, yb, w, c, 0, 0);
}
/* (FM6 LAB T) the sound under a line (PITCH, LFO; CRYPT and QUARK's D: the loudest carrier), column x from y to the
 * foot: RAISE; CRYPT the ASCII shade (a dot every 4 px, every 6th column), NEON the sun's bands, QUARK a Monte Carlo
 * fill */
static void fm_under(int32_t x, int32_t y, int32_t foot)
{
    if (ux.crypt) {
        if ((x - FM_X0) % 6 == 0)
            for (y = (y + 4) & ~3; y < foot; y += 4)
                cv_pset(x, y, T_DIM);
    } else
        pg_under(x, y, FM_GY, foot, T_RAISE);
}
static void fm_line(const fm_env_line_t *l, int32_t dy, int32_t w, uint16_t c, const uint8_t *shade, const uint16_t *sh)
{
    uint32_t j;
    for (j = 1; j < l->n; j++)
        fm_seg(FM_X0 + l->x[j - 1u], fm_lvy(l->v[j - 1u]) + dy, FM_X0 + l->x[j], fm_lvy(l->v[j]) + dy, w, c, shade, sh);
}
/* a breakpoint's handle: a ring 9 px across on a dark halo; filled: the one the knob just turned. (FM6 LAB T) CRYPT a
 * skull (the one turned aflame, its eyes blood), NEON a sparkle (the one turned cyan and twice the size), QUARK a
 * nucleus (the one turned gold, larger) */
static void fm_ring(int32_t x, int32_t y, int filled)
{
    int32_t dx, dy;
    if (ux.crypt) {
        for (dx = 0; dx < 4; dx++)                      /* (in a dark outline: on the lines) */
            cr_skull(x - 4 + (dx == 0) - (dx == 1), y - 4 + (dx == 2) - (dx == 3), T_BG, T_BG, 0);
        cr_skull(x - 4, y - 4, filled ? CR_FLAME : T_TEXT, filled ? CR_BLOOD : T_BG, 0);
        return;
    }
    if (ux.neon) {
        ne_spark(x, y, filled ? T_ACCENT : T_THEME, filled ? 2 : 1);
        return;
    }
    if (ux.quark) {
        qk_disc(x, y, 3, T_SURF);
        qk_nucleus(x, y, filled ? T_ACCENT : T_THEME, filled ? 9 : 7);
        return;
    }
    for (dy = -5; dy <= 5; dy++)
        for (dx = -5; dx <= 5; dx++) {
            int32_t d2 = dx * dx + dy * dy;
            if (d2 <= 30)
                cv_pset(x + dx, y + dy, d2 > 20 ? T_BG : d2 >= 7 || filled ? T_TEXT : T_SURF);
        }
}
/* "12 MS", "0.67 S", "4.3 S" (blocks of 32 samples at 44.1 kHz) */
static void fm_secs(char *b, int32_t blocks)
{
    int32_t ms = blocks * 1000 / 1378;
    if (ms < 100) {
        fmt_int(b, ms < 1 ? 1 : ms);
        str_cpy(b + str_len(b), " MS", 4);
    } else if (ms < 1000) {
        fm_hundredths(b, ms / 10);
        str_cpy(b + str_len(b), " S", 3);
    } else {
        fm_tenths(b, ms / 100, 0);
        str_cpy(b + str_len(b), " S", 3);
    }
}
/* the caption's right: the length; and the key (SINE, the ramp, FM) */
static void fm_caption(const fm_sim_t *s, const uint16_t *sh)
{
    char b[16];
    uint32_t j;
    fm_secs(b, s->n);
    GFX_HOOK_ALIGN(0, FM_CY, 0, FM_CY + 12, AL_V, "FM6 graph caption on its row");
    cv_text_r(229, FM_CY + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
    if (!sh)
        return;
    cv_text_r(129, FM_CY + CAP_IN(S, 12), &AF_S, "SINE", T_DIM, T_SURF);
    for (j = 0; j < 12u; j++)
        cv_rect(132 + (int32_t)j * 3, FM_CY + 3, 3, 7, sh[j]);
    cv_text(171, FM_CY + CAP_IN(S, 12), &AF_S, "FM", T_DIM);
}
/* the operator tabs: OP1 .. OP6 outlined, the one picked in TEXT; under each its line's colour (a carrier's: the
 * ramp: it shows the heat); TONE: the ratio beside the number */
static void fm_tabs(const fm_sim_t *s, const uint16_t *sh, int ratios)
{
    uint32_t k, sel = fm_ui[0] % 6u;
    for (k = 0; k < 6u; k++) {
        int32_t x = 8 + (int32_t)k * 38;
        const uint8_t *op = fm6_patch[fm_tr()] + (5u - k) * FP_OP;
        int on = k == sel, live = s->live >> k & 1u;
        uint16_t ink = on ? T_TEXT : live ? T_MID : T_DIM;
        char b[8];
        if (ux.crypt || ux.neon) {                      /* ((FM6 LAB T) stone or glass; the one picked: a gold frame, */
            cv_rrect(x, 4, 34, 17, 3, T_SURF, T_SURF);  /* a laser; QUARK: a cell's brackets, the one picked gold) */
            if (on && ux.crypt)
                cr_frame2(x - 2, 2, 38, 21, CR_GOLD);
            else if (on)
                ne_laser(x, 4, 34, 17, T_SURF);
        } else if (ux.quark) {
            qk_corners(x, 4, 34, 17, on ? T_ACCENT : T_DIM, 5);
        } else {
            cv_frame(x, 4, 34, 17, on ? T_TEXT : T_DIM);
            if (on)
                cv_frame(x + 1, 5, 32, 15, T_TEXT);
        }
        if (ratios) {
            b[0] = (char)('1' + k);
            b[1] = 0;
            cv_text_on(x + 5, 4 + CAP_IN(S, 17), &AF_S, b, ink, T_SURF);
            if (op[FP_MODE])
                str_cpy(b, FM_HZ[op[FP_FC] & 3u], sizeof b);
            else if (op[FP_FC])
                fmt_int(b, op[FP_FC]);
            else
                str_cpy(b, ".5", sizeof b);
            str_cpy(b + str_len(b), op[FP_MODE] ? "" : "x", sizeof b - str_len(b));
            cv_text_r(x + 30, 4 + CAP_IN(S, 17), &AF_S, b, live ? T_MID : T_DIM, T_SURF);
            if (ux.crypt || ux.neon)                    /* ((FM6 LAB T) the pixel letters run together: a divider) */
                cv_rect(x + 12, 8, 1, 9, T_DIM);
        } else {
            b[0] = 'O', b[1] = 'P', b[2] = (char)('1' + k), b[3] = 0;
            GFX_HOOK_ALIGN(x, 4, x + 34, 21, AL_HV, "FM6 operator tab name centred");
            cv_text_in(x, 4 + CAP_IN(S, 17), 34, &AF_S, b, ink, T_SURF);
        }
        if (!live)
            continue;
        if (s->car >> k & 1u) {
            uint32_t j;
            for (j = 0; j < 12u; j++)
                cv_rect(x + 3 + (int32_t)j * 2 + (j > 5u) * 4, 23, 2, 2, sh[j]);
        } else
            cv_rect(x + 3, 23, 28, 2, T_TRK(k));
    }
}
/* the handles on line l's stage ends (stages 0..3); filled: hot (0xFF none) */
static void fm_handles(const fm_env_line_t *l, uint32_t hot, int32_t (*y)(int32_t))
{
    uint32_t j;
    for (j = 0; j < 4u; j++)
        if (l->end[j] != 0xFFu)
            fm_ring(FM_X0 + l->x[l->end[j]], y(l->v[l->end[j]]), j == hot);
}
/* the stage a window's hot knob moves (ENV 1: K1 K2 stage 1, K3 K4 stage 2; ENV 2: 3, 4), 0xFF: none */
static uint32_t fm_hot_stage(uint32_t w)
{
    if (!ui.hot_t || (w != 1u && w != 2u))
        return 0xFFu;
    return (w - 1u) * 2u + ui.hot_col / 2u;
}
/* the loudest sounding carrier's level at column x */
static int32_t fm_loud(const fm_sim_t *s, int32_t x)
{
    int32_t lv = 0, v;
    uint32_t k;
    for (k = 0; k < 6u; k++)
        if ((s->car & s->live) >> k & 1u && (v = fm_at_x(&s->op[k], x)) > lv)
            lv = v;
    return lv;
}
/* D: the envelopes, the carriers shaded by their heat, the picked operator's handles */
static void fm_lines(const fm_sim_t *s, const uint16_t *sh, uint32_t w)
{
    uint8_t shade[FM_XW];
    uint16_t glow[12];
    uint32_t k, sel = fm_ui[0] % 6u, pass, best = 0, bk = 0;
    int32_t x, bx = 0;
    char b[24];
    for (k = 0; k < 6u; k++)                            /* where it bites most (the carrier, the column) */
        if ((s->car & s->live) >> k & 1u) {
            uint32_t fed = fm_feeders(s->alg, k), h;
            for (x = 0; x < FM_XW; x++)
                if ((h = fm_heat(s, k, fed, x)) > best)
                    best = h, bx = x, bk = k;
        }
    if (ux.neon) {                                      /* ((FM6 LAB T) NEON: the sun and the grid floor behind; */
        ne_sun(FM_X0 + 172, FM_GY + FM_GH - 28, 24, 62);   /* the brightest moment a laser) */
        ne_grid(FM_X0, FM_X0 + FM_XW, FM_GY + FM_GH - 28, FM_GY + FM_GH, NE_VIOLET, T_SURF);
        if (best) {
            cv_rect(FM_X0 + bx - 1, FM_GY, 3, FM_GH, ux_mix(T_ACCENT, T_SURF, 70));
            cv_rect(FM_X0 + bx, FM_GY, 1, FM_GH, T_ACCENT);
        }
    } else if (ux.crypt || ux.quark) {                  /* (CRYPT's shade, QUARK's energy under the sound) */
        for (x = 0; x < FM_XW; x++)
            fm_under(FM_X0 + x, fm_lvy(fm_loud(s, x)), FM_GY + FM_GH);
        if (ux.quark && best)                           /* (QUARK: a collision where it bites most) */
            qk_burst(FM_X0 + bx, fm_lvy(fm_at_x(&s->op[bk], bx)), 5, 0x9E3779B9u, 18, T_SURF);
    }
    for (x = FM_GY; x < FM_GY + FM_GH; x += 2)          /* the key up: dotted */
        cv_pset(FM_X0 + FM_HW, x, T_DIM);
    for (k = 0; k < 12u; k++)
        glow[k] = ux_mix(sh[k], T_SURF, 62);
    for (pass = 0; pass < 3u; pass++)                   /* the modulators, the carriers, the one picked */
        for (k = 0; k < 6u; k++) {
            int car = s->car >> k & 1u, wd = k == sel ? 3 : 2;
            if (!(s->live >> k & 1u) || (pass == 2u) != (k == sel) || (pass < 2u && (uint32_t)car != pass))
                continue;
            if (!car) {
                fm_env_line_t l = s->op[k];
                uint32_t j;
                uint16_t c = k == sel ? T_TRK(k) : ux_mix(T_SURF, T_TRK(k), 70);
                for (j = 0; j < l.n; j++)
                    l.v[j] += s->modlvl;
                if (ux.neon)                            /* (NEON: a tube's glow; QUARK: a bubble chamber's dots) */
                    fm_line(&l, 0, wd + 2, ux_mix(c, T_SURF, 62), 0, 0);
                fm_line(&l, 0, ux.quark ? -wd : wd, c, 0, 0);
                continue;
            }
            {
                uint32_t fed = fm_feeders(s->alg, k);
                for (x = 0; x < FM_XW; x++)
                    shade[x] = (uint8_t)fm_heat(s, k, fed, x);
            }
            if (ux.neon)
                fm_line(&s->op[k], 0, 7, 0, shade, glow);   /* (its glow) */
            else
                fm_line(&s->op[k], 1, 5, T_BG, 0, 0);       /* a shadow under it */
            fm_line(&s->op[k], 0, k == sel ? 5 : 4, 0, shade, sh);
        }
    if (s->live >> sel & 1u) {
        fm_env_line_t l = s->op[sel];
        uint32_t j;
        if (!(s->car >> sel & 1u))
            for (j = 0; j < l.n; j++)
                l.v[j] += s->modlvl;
        fm_handles(&l, fm_hot_stage(w), fm_lvy);
    }
    if (best) {                                         /* where it bites most: a mark under it, the caption */
        int32_t r, lo = 0, hi = s->n;                   /* (the block at that column: the axis rises) */
        if (ux.crypt) {                                 /* ((FM6 LAB T) CRYPT: a flame; NEON, QUARK: theirs behind) */
            cr_sprite(FM_X0 + bx - 2, FM_GY + FM_GH - 3, SPR_FM_FLAME, CR_EMBER, CR_FLAME);
        } else if (!ux.neon && !ux.quark)
            for (r = 0; r < 4; r++)
                cv_rect(FM_X0 + bx - r, FM_GY + FM_GH + 1 + r, 2 * r + 1, 1, T_TEXT);
        while (lo < hi) {
            int32_t mid = (lo + hi) / 2;
            if (fm_warp(s, mid) < bx)
                lo = mid + 1;
            else
                hi = mid;
        }
        str_cpy(b, "BRIGHTEST ", sizeof b);
        fm_secs(b + 10, lo);
    } else {
        uint32_t live = s->car & s->live, n = 0;
        for (; live; live &= live - 1u)
            n++;
        str_cpy(b, n == 1u ? "PURE SINE" : n ? "NO MODULATION" : "NO CARRIER SOUNDS", sizeof b);
    }
    cv_text_on(10, FM_CY + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
    fm_caption(s, sh);
}

/* KEYS and MORE: the picked operator's level across the keyboard (fm6_scale_level, MIDI 21 .. 108): 0 dB at the top
 * third, -48 dB at the bottom; BREAK a dotted line with its note, C4 a dot; the octaves under it */
static int32_t fm_keys_y(const uint8_t *op, uint32_t note, int32_t zy, int32_t rng)
{
    int32_t q = fm6_scale_level((int32_t)note, op) * 3;  /* an outlevel step 3/4 dB: in 1/4 dB */
    return zy + clamp(-q, -48, 192) * rng / 192;
}
static void fm_keys(uint16_t c)
{
    const uint8_t *op = fm6_patch[fm_tr()] + (5u - fm_ui[0] % 6u) * FP_OP;
    int32_t zy = FM_GY + 21, kb = FM_GY + 91, rng = kb - 6 - zy, o, x, bx;
    uint32_t n;
    char b[16];
    cv_rect(FM_X0, zy, FM_XW, 1, FM_GUIDE);
    cv_text_r(229, zy - 13 + CAP_IN(S, 12), &AF_S, "0 DB", T_DIM, T_SURF);
    cv_text_r(229, kb - 14 + CAP_IN(S, 12), &AF_S, "-48 DB", T_DIM, T_SURF);
    for (o = 1; o < 8; o++) {                           /* the octaves C1 .. C7: bands, C2 C4 C6 named */
        int32_t xa = FM_X0 + (12 * o + 12 - 21) * (FM_XW - 1) / 87, xb = FM_X0 + (12 * o + 24 - 21) * (FM_XW - 1) / 87;
        cv_rect(xa, kb + 2, (xb > FM_X0 + FM_XW - 1 ? FM_X0 + FM_XW - 1 : xb) - xa, 4, o & 1 ? T_RAISE : T_DIM);
        if (!(o & 1)) {
            b[0] = 'C', b[1] = (char)('0' + o), b[2] = 0;
            cv_text_on(xa, kb + 6 + CAP_IN(S, 12), &AF_S, b, T_DIM, T_SURF);
        }
    }
    bx = FM_X0 + clamp((int32_t)op[FP_BP] * (FM_XW - 1) / 87, 0, FM_XW - 1);   /* BREAK: MIDI BP + 21 */
    if (ux.crypt || ux.neon || ux.quark)               /* ((FM6 LAB T) the theme's fill under the curve) */
        for (x = 0; x < FM_XW; x++)
            fm_under(FM_X0 + x, fm_keys_y(op, 21u + (uint32_t)x * 87u / (FM_XW - 1), zy, rng), kb);
    if (ux.neon) {                                      /* (NEON: BREAK a laser) */
        cv_rect(bx - 1, FM_GY + 4, 3, kb - 6 - FM_GY, ux_mix(T_ACCENT, T_SURF, 70));
        cv_rect(bx, FM_GY + 4, 1, kb - 6 - FM_GY, T_ACCENT);
    } else
        for (x = FM_GY + 4; x < kb - 2; x += 2)
            cv_pset(bx, x, T_TEXT);
    str_cpy(b, "BREAK ", sizeof b);
    note_name(b + 6, op[FP_BP] + 21u);
    cv_text_on(bx + 4 > 160 ? bx - 4 - text_w(&AF_S, b) : bx + 4, FM_GY + CAP_IN(S, 12), &AF_S, b, T_TEXT, T_SURF);
    for (n = 21; n < 108u; n++) {
        int32_t xa = FM_X0 + (int32_t)(n - 21u) * (FM_XW - 1) / 87, xn = FM_X0 + (int32_t)(n - 20u) * (FM_XW - 1) / 87;
        fm_seg2(xa, fm_keys_y(op, n, zy, rng), xn, fm_keys_y(op, n + 1u, zy, rng), 2, c);
    }
    if (ux.crypt)                                       /* (CRYPT: a dagger planted where the curve breaks) */
        cr_dagger(bx - 4, fm_keys_y(op, op[FP_BP] + 21u, zy, rng) - 14, CR_GOLD);
    x = FM_X0 + (60 - 21) * (FM_XW - 1) / 87;
    if (ux.crypt || ux.neon || ux.quark)                /* C4: the theme's handle (a skull, a sparkle, a nucleus) */
        fm_ring(x, fm_keys_y(op, 60, zy, rng), 0);
    else
        cv_rect(x - 2, fm_keys_y(op, 60, zy, rng) - 2, 5, 5, T_TEXT);
    str_cpy(b, "C7 ", sizeof b);
    fm_signed(b + 3, fm6_scale_level(96, op) * 3 / 4);
    str_cpy(b + str_len(b), " DB", sizeof b - str_len(b));
    cv_text_on(FM_X0, FM_GY + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
}

/* ALGO: the chart (graph_fm6, the picked operator lit) and what the operator does in it */
static void fm_algo(const track_t *t, uint16_t c)
{
    uint8_t m[6];
    uint32_t alg = fm6_alg_of(t), sel = fm_ui[0] % 6u, k, n = 0;
    char b[32];
    cv_oy = GOY + graph_pad();
    graph_fm6(t, c);
    cv_oy = 0;
    fm6_routes(alg, m);
    str_cpy(b, "OP", sizeof b);
    b[2] = (char)('1' + sel);
    b[3] = 0;
    if (!fm6_patch[fm_tr()][(5u - sel) * FP_OP + FP_OL])
        str_cpy(b + 3, " IS SILENT: OUTPUT 0", sizeof b - 3u);
    else if (fm6_car_ops(alg) >> sel & 1u)
        str_cpy(b + 3, " GOES TO THE OUTPUT", sizeof b - 3u);
    else {
        str_cpy(b + 3, " MODULATES", sizeof b - 3u);
        for (k = 0; k < 6u; k++)
            if (m[k] >> sel & 1u) {
                str_cpy(b + str_len(b), n++ ? ", OP" : " OP", sizeof b - str_len(b));
                b[str_len(b) + 1u] = 0;
                b[str_len(b)] = (char)('1' + k);
            }
    }
    cv_text_on(10, FM_CY + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
}

/* PITCH: the pitch envelope over the note (the sound's outline behind, on its time axis); LFO, DEPTH: the vibrato
 * over two seconds from the key, the tremolo under it */
static int32_t fm_prng;                                 /* (fm_pitch_y's range, 1/100 semitone) */
static int32_t fm_pitch_y(int32_t v)                    /* a semitone Q16 (from octaves Q16 x 12) */
{
    return FM_GY + FM_GH / 2 - clamp(v, -fm_prng * 65536 / 100, fm_prng * 65536 / 100) / 64 * (FM_GH / 2 - 6) /
                               (fm_prng * 1024 / 100);
}
static void fm_range_labels(void)
{
    char b[12];
    b[0] = '+';
    if (fm_prng < 100)
        fm_hundredths(b + 1, fm_prng);
    else
        fmt_int(b + 1, fm_prng / 100);
    str_cpy(b + str_len(b), " ST", sizeof b - str_len(b));
    cv_text_on(10, FM_GY + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
    b[0] = '-';
    cv_text_on(10, FM_GY + FM_GH - 12 + CAP_IN(S, 12), &AF_S, b, T_MID, T_SURF);
}
static int32_t fm_range(int32_t cents)
{
    static const int16_t R[9] = {10, 25, 50, 100, 200, 400, 1200, 2400, 4800};
    uint32_t i;
    for (i = 0; i < 8u && R[i] < cents; i++)
        ;
    return R[i];
}
static void fm_pitch(const fm_sim_t *s, uint16_t c, uint32_t w)
{
    fm_env_line_t l = s->pitch;
    int32_t x, m = 0;
    uint32_t j, flat = 1;
    for (x = 0; x < FM_XW; x++)                         /* the sound behind, its outline (the theme's fill) */
        fm_under(FM_X0 + x, fm_lvy(fm_loud(s, x)), FM_GY + FM_GH);
    for (j = 0; j < l.n; j++) {                         /* octaves -> semitones; the range */
        l.v[j] *= 12;
        x = (l.v[j] < 0 ? -l.v[j] : l.v[j]) * 100 / 65536;
        m = x > m ? x : m;
        flat &= l.v[j] == 0;
    }
    fm_prng = fm_range(m);
    cv_rect(FM_X0, FM_GY + FM_GH / 2, FM_XW, 1, FM_GUIDE);
    for (j = 1; j < l.n; j++)
        fm_seg2(FM_X0 + l.x[j - 1u], fm_pitch_y(l.v[j - 1u]), FM_X0 + l.x[j], fm_pitch_y(l.v[j]), 2, c);
    fm_handles(&l, ui.hot_t && (w == 1u || w == 2u) ? (w - 1u) * 2u + ui.hot_col / 2u : 0xFFu, fm_pitch_y);
    fm_range_labels();
    cv_text_on(10, FM_CY + CAP_IN(S, 12), &AF_S, flat ? "NO PITCH MOVEMENT" : "PITCH OVER THE NOTE", T_MID, T_SURF);
    fm_caption(s, 0);
}
static void fm_lfo(const fm_sim_t *s, uint16_t c)
{
    const uint8_t *q = s->q;
    fm6_lfo_t lf;
    fm6_env_t e[6];
    int16_t vib[FM_XW];
    uint8_t loud[FM_XW], trem[FM_XW];
    int32_t b, x = 0, m = 0, in = -1, note = 60 + q[FP_TRNSP] - 24;
    uint32_t k, pmd = (uint32_t)(q[FP_LPMD] % 100u) * 165u >> 6, amd = (uint32_t)(q[FP_LAMD] % 100u) * 165u >> 6;
    char t[40];
    memset(&lf, 0, sizeof lf);
    fm6_lfo_reset(&lf, q);
    fm6_lfo_key(&lf);
    for (k = 0; k < 6u; k++)
        fm6_op_env_init(&e[k], q + (5u - k) * FP_OP, note, 100);
    for (b = 0; b < 2 * 1378 && x < FM_XW; b++) {       /* two seconds, a column every 12.5 blocks */
        int32_t v = fm6_lfo_sample(&lf), d = fm6_lfo_delay(&lf), lv = 0;
        int32_t sens = FM6_PMS[q[FP_LPMS] & 7u] * (v - (1 << 23));
        int32_t pm = (int32_t)(((int64_t)(pmd * (uint32_t)d) * sens) >> 39);   /* Q24 octaves (fm6_note_compute) */
        uint32_t am = (uint32_t)(((int64_t)((amd * (uint32_t)d) >> 8) * ((1 << 24) - v)) >> 24);
        for (k = 0; k < 6u; k++) {
            int32_t ev = fm6_env_get(&e[k], q + (5u - k) * FP_OP) >> 8;
            if ((s->car & s->live) >> k & 1u)
                lv = ev > lv ? ev : lv;
        }
        if (in < 0 && d >= 1 << 22 && (pmd || amd))
            in = b;
        if (b >= x * (2 * 1378 - 1) / (FM_XW - 1)) {
            vib[x] = (int16_t)clamp(pm / 13981, -32767, 32767);   /* cents (a cent: 13981 in Q24 octaves) */
            loud[x] = (uint8_t)fm_lvy(lv);
            trem[x] = (uint8_t)clamp((int32_t)(am >> 19), 0, 24);   /* (Q24: up to 24 px of dip) */
            m = (vib[x] < 0 ? -vib[x] : vib[x]) > m ? (vib[x] < 0 ? -vib[x] : vib[x]) : m;
            x++;
        }
    }
    for (b = 0; b < x; b++)
        fm_under(FM_X0 + b, loud[b], FM_GY + FM_GH);
    fm_prng = fm_range(m);
    cv_rect(FM_X0, FM_GY + FM_GH / 2, FM_XW, 1, FM_GUIDE);
    for (b = 1; b < x; b++)
        if (amd)
            cv_rect(FM_X0 + b, FM_GY + FM_GH - 3 - trem[b], 1, 2, T_MID);
    if (ux.crypt && x > 24) {                           /* ((FM6 LAB T) CRYPT: the vibrato a snake, as the LFO's: its */
        int32_t e = x - 12;                             /* belly under it, gold scales, its head at the end) */
        for (b = 1; b < e; b++)
            fm_seg(FM_X0 + b - 1, fm_pitch_y(vib[b - 1] * 65536 / 100) + 2, FM_X0 + b,
                   fm_pitch_y(vib[b] * 65536 / 100) + 2, 2, ux_mix(c, T_BG, 55), 0, 0);
        for (b = 1; b < e; b++)
            fm_seg(FM_X0 + b - 1, fm_pitch_y(vib[b - 1] * 65536 / 100), FM_X0 + b, fm_pitch_y(vib[b] * 65536 / 100),
                   2, c, 0, 0);
        for (b = 6; b < e - 4; b += 8)
            cv_rect(FM_X0 + b, fm_pitch_y(vib[b] * 65536 / 100), 2, 1, CR_GOLD);
        cr_head(FM_X0 + e, fm_pitch_y(vib[e] * 65536 / 100) + 1, c);
    } else
        for (b = 1; b < x; b++)
            fm_seg2(FM_X0 + b - 1, fm_pitch_y(vib[b - 1] * 65536 / 100), FM_X0 + b, fm_pitch_y(vib[b] * 65536 / 100), 2, c);
    fm_range_labels();
    if (!pmd && !amd)
        str_cpy(t, "VIBRATO AND TREMOLO AT 0", sizeof t);
    else {
        str_cpy(t, pmd ? "VIBRATO " : "TREMOLO ", sizeof t);
        fm_tenths(t + 8, (int32_t)fm_lfo_hz10(q[FP_LFS]), 0);
        str_cpy(t + str_len(t), " HZ, IN AFTER ", sizeof t - str_len(t));
        fm_secs(t + str_len(t), in < 0 ? 0 : in);
    }
    cv_text_on(10, FM_CY + CAP_IN(S, 12), &AF_S, t, T_MID, T_SURF);
    cv_text_r(229, FM_CY + CAP_IN(S, 12), &AF_S, "2.0 S", T_MID, T_SURF);
}

/* the scroll bar: where the knobs are among the windows */
static void fm_scroll(uint32_t w)
{
    int32_t h = 132 / (int32_t)FM_NWIN;
    cv_rect(236, 4, 2, 132, T_RAISE);
    cv_rect(236, 4 + (int32_t)w * h, 2, h, T_MID);
}
static void fm_draw(uint32_t pg, const track_t *t, uint16_t c)
{
    static fm_sim_t s __attribute__((section(".pool")));   /* (1.2 KB: off the stack and the ISR's globals) */
    uint16_t sh[12];
    uint32_t w = fm_ui[1 + pg] % FM_NWIN;
    fm_ramp(sh);
    if (pg && w == FMW_ALGO) {
        fm_algo(t, c);
    } else if (!pg && (w == FMW_KEYS || w == FMW_MORE)) {
        fm_ops(&s, t);
        fm_tabs(&s, sh, 0);
        fm_keys(c);
    } else {
        fm_sim(&s, t);
        if (pg && (w == FMW_LFO || w == FMW_DEPTH))
            fm_lfo(&s, c);
        else if (pg)
            fm_pitch(&s, c, w);
        else {
            fm_tabs(&s, sh, w == FMW_TONE);
            fm_lines(&s, sh, w);
        }
    }
    fm_scroll(w);
}
static void graph_fm_op(const track_t *t, uint16_t c) { fm_draw(0, t, c); }
static void graph_fm_all(const track_t *t, uint16_t c) { fm_draw(1, t, c); }
