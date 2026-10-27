/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware's parameter and engine tables as JSON, from the same sources (through hostsim.c):
 * TP, GP, every engine's page titles, EDIT descriptors and presets, their display order, the power-on sounds and
 * the protocol constants. web/test_web.mjs compares the editor's mock device
 * (web/editor.html makeMockDevice) with it, so the mock cannot drift from the firmware.
 *   build/host/descdump > build/host/desc.json      (run_tests.sh) */
#define FELUCCA_LEGACY 1                        /* ((2.0) its DIGITAL -> FM6 cases: the converter's fm4_convert.c) */
#define main hostsim_main
#include "hostsim.c"
#undef main

static void js_str(const char *s)
{
    putchar('"');
    for (; s && *s; s++) {
        if (*s == '"' || *s == '\\')
            putchar('\\');
        putchar(*s);
    }
    putchar('"');
}

static void js_ints(const int32_t *v, uint32_t n)
{
    uint32_t i;
    putchar('[');
    for (i = 0; i < n; i++)
        printf("%s%d", i ? "," : "", v[i]);
    putchar(']');
}

/* as the protocol's DESC reply has it: the value names only for F_ENUM */
static void js_desc(const param_desc_t *d)
{
    int32_t i;
    printf("{\"label\":");
    js_str(d->label);
    printf(",\"fmt\":%d,\"min\":%d,\"max\":%d,\"def\":%d,\"names\":", d->fmt, d->min, d->max, d->def);
    if (d->fmt == F_ENUM && d->names) {
        putchar('[');
        for (i = 0; i <= d->max - d->min; i++) {
            if (i)
                putchar(',');
            js_str(d->names[i]);
        }
        putchar(']');
    } else {
        printf("null");
    }
    printf(",\"unit\":");
    js_str(d->unit ? PD_UNIT_OF(d) : "");
    putchar('}');
}

static void js_descs(const param_desc_t *d, uint32_t n)
{
    uint32_t i;
    putchar('[');
    for (i = 0; i < n; i++) {
        if (i)
            printf(",\n  ");
        js_desc(&d[i]);
    }
    putchar(']');
}

int main(void)
{
    uint32_t e, i, k;
    int32_t v[16];
    printf("{\"P_COUNT\":%d,\"G_COUNT\":%d,\"NSTEP\":%d,\"P_E0\":%d,\"G_ENGSEL\":%d,\"P_SLCR\":%d,\"NTRK\":%d,\n",
           P_COUNT, G_COUNT, NSTEP, P_E0, G_ENGSEL, P_SLCR, NTRK);
    printf("\"TP\":");
    js_descs(TP, P_E0);                                  /* P_E0.. are the engine's */
    printf(",\n\"GP\":");
    js_descs(GP, G_COUNT);
    printf(",\n\"ENG\":[");
    for (e = 0; e < NENGINES; e++) {
        const engine_t *en = ENGINES[e];
        printf("%s\n {\"name\":", e ? "," : "");
        js_str(en->name);
        printf(",\"titles\":[");
        js_str(en->page_title[0]);
        putchar(',');
        js_str(en->page_title[1]);
        printf("],\"edit\":");
        js_descs(en->edit, 8);
        printf(",\"presets\":[");
        for (k = 0; k < en->npresets; k++) {
            const preset_t *p = &en->presets[k];
            printf("%s\n  {\"name\":", k ? "," : "");
            js_str(p->name);
            for (i = 0; i < 8u; i++)
                v[i] = p->e[i];
            printf(",\"e\":");
            js_ints(v, 8);
            for (i = 0; i < 4u; i++)
                v[i] = p->env[i];
            printf(",\"env\":");
            js_ints(v, 4);
            printf(",\"mono\":%d,\"more\":[", p->mono);   /* (Skiff) its other values: [id, value] pairs */
            for (i = 0; i < NELEM(p->more) && p->more[i].id; i++)
                printf("%s[%d,%d]", i ? "," : "", p->more[i].id, p->more[i].v);
            printf("]}");
        }
        printf("]}");
    }
    printf("],\n\"ORDER\":[");                      /* the engines as shown (engines.c ENGINE_ORDER), by name */
    for (k = 0; k < NENG_SHOWN; k++) {
        if (k)
            putchar(',');
        js_str(ENGINES[eng_vis(k)]->name);
    }
    printf("],\n\"TRK_DEF\":[");
    for (k = 0; k < NPART; k++)
        printf("%s[%d,%d]", k ? "," : "", TRK_DEF[k][0], TRK_DEF[k][1]);
    printf("],\n\"FM6\":{\"bank\":0,\"own\":%u,\"init\":", (unsigned)FM6_OWN);   /* FM6: the packed patches (no bank since 1.0.3) */
    for (i = 0; i < FM6_PACKED; i++)
        printf("%s%d", i ? "," : "[", FM6_INIT[i]);
    printf("],\"factory\":[");
    for (k = 0; k < FM6_NFACTORY; k++)
        for (i = 0; i < FM6_PACKED; i++)
            printf("%s%d%s", i ? "," : k ? ",[" : "[", FM6_FACTORY[k][i], i == FM6_PACKED - 1u ? "]" : "");
    printf("]}");
    printf(",\n\"TC_NAME\":[");                      /* (Skiff W1) the trig conditions (core.h), a step's COND */
    for (k = 0; k < TC_COUNT; k++) {
        if (k)
            putchar(',');
        js_str(TC_NAME[k]);
    }
    printf("]");
    printf(",\n\"LANE_NOTE\":[");                    /* the DRUM grid: each lane's GM note, the lane of GM 35..81 */
    for (k = 0; k < NLANE; k++)
        printf("%s%d", k ? "," : "", DRUM_LANE_NOTE[k]);
    printf("],\n\"DX_KIT\":[");                      /* (Skiff W3) the DRUM-X factory kit {mode, A, B}: the editor's */
    for (k = 0; k < 8u; k++) {
        printf("%s[%d,[", k ? "," : "", DX_KIT_DEF[k].mode);
        for (i = 0; i < DXP_N; i++)
            printf("%s%d", i ? "," : "", DX_KIT_DEF[k].a[i]);
        printf("],[");
        for (i = 0; i < DXP_N; i++)
            printf("%s%d", i ? "," : "", DX_KIT_DEF[k].b[i]);
        printf("]]");
    }
    printf("],\n\"DX_KITS\":[");                     /* (Skiff phase 9) the kits a DRUM-X preset loads (eng_drumx.c) */
    for (e = 0; e < NELEM(DX_KITS); e++) {
        printf("%s[", e ? "," : "");
        for (k = 0; k < 8u; k++) {
            printf("%s[%d,[", k ? "," : "", DX_KITS[e][k].mode);
            for (i = 0; i < DXP_N; i++)
                printf("%s%d", i ? "," : "", DX_KITS[e][k].a[i]);
            printf("],[");
            for (i = 0; i < DXP_N; i++)
                printf("%s%d", i ? "," : "", DX_KITS[e][k].b[i]);
            printf("]]");
        }
        printf("]");
    }
    printf("],\n\"DRUMX_PKIT\":[");
    for (k = 0; k < NELEM(DRUMX_PKIT); k++)
        printf("%s%d", k ? "," : "", DRUMX_PKIT[k]);
    printf("],\n\"FM4\":{\"presets\":[");             /* DIGITAL (retired): its presets and conversions (fm4_convert.c) */
    for (k = 0; k < FM4_NPRESETS; k++) {
        const preset_t *p = &DIGITAL_PRESETS[k];
        printf("%s\n {\"name\":", k ? "," : "");
        js_str(p->name);
        for (i = 0; i < 8u; i++)
            v[i] = p->e[i];
        printf(",\"e\":");
        js_ints(v, 8);
        for (i = 0; i < 4u; i++)
            v[i] = p->env[i];
        printf(",\"env\":");
        js_ints(v, 4);
        for (i = 0; i < 4u; i++)
            v[i] = p->fx[i];
        printf(",\"fenv\":%d,\"mono\":%d,\"fx\":", p->fenv, p->mono);
        js_ints(v, 4);
        putchar('}');
    }
    printf("],\"to_fm6\":");
    for (k = 0; k < FM4_NPRESETS; k++)
        v[k] = FM4_TO_FM6[k];
    js_ints(v, FM4_NPRESETS);
    printf(",\"cases\":[");
    {   /* the presets, every algorithm with and without feedback, then pseudo-random values (OP ENV ones too) */
        uint32_t c, seed = 12345u;
        for (c = 0; c < 48u; c++) {
            int16_t p[P_COUNT];
            int32_t w[FP_SIZE + P_COUNT];
            uint8_t fv[FP_SIZE + 1u];
            uint32_t pr;
            for (i = 0; i < P_COUNT; i++)
                p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
            if (c < FM4_NPRESETS) {
                fm4_preset_values(p, c);
            } else if (c < 24u) {
                p[P_E0] = (int16_t)((c - 8u) & 7u);
                p[P_E1] = 1; p[P_E2] = 3; p[P_E3] = (int16_t)(c & 1 ? 5 : 1);
                p[P_E4] = 64; p[P_E5] = 70; p[P_E6] = (int16_t)(c < 16u ? 12 : 30);
            } else {
                for (i = 0; i < P_COUNT; i++) {
                    const param_desc_t *d = param_desc_of(ENGI_DIGITAL, i);
                    if (i == P_ATK || i == P_DEC || i == P_SUS || i == P_REL || i == P_ED_FLT ||
                        (i >= P_FM1_ATK && i <= P_FM4_LEVEL && (c & 1u)) || (i >= P_E0 && i < P_E7)) {
                        seed = seed * 1103515245u + 12345u;
                        p[i] = (int16_t)(d->min + (int32_t)((seed >> 8) % (uint32_t)(d->max - d->min + 1)));
                    }
                }
            }
            for (i = 0; i < P_COUNT; i++)
                w[i] = p[i];
            printf("%s\n {\"p\":", c ? "," : "");
            js_ints(w, P_COUNT);
            pr = fm4_convert(p, fv);
            for (i = 0; i < P_COUNT; i++)
                w[i] = p[i];
            printf(",\"preset\":%u,\"out\":", pr);
            js_ints(w, P_COUNT);
            for (i = 0; i < FP_SIZE; i++)
                w[i] = fv[i];
            printf(",\"voice\":");
            js_ints(w, FP_SIZE);
            putchar('}');
        }
    }
    printf("]}");
    printf(",\n\"LANE_OF\":\"");
    for (k = 35; k <= 81; k++)
        printf("%u", (unsigned)drum_lane(k));
    printf("\",\n\"PCT\":[");                          /* F_PCT as the firmware shows it (#31: SWG 0..100) */
    for (k = 0, e = 0; k < (uint32_t)P_E0 + G_COUNT; k++) {
        const param_desc_t *d = k < (uint32_t)P_E0 ? &TP[k] : &GP[k - P_E0];
        int32_t x[3] = {d->min, (d->min + d->max) / 2, d->max};
        if (d->fmt != F_PCT)
            continue;
        for (i = 0; i < 3u; i++) {
            char val[12];
            const char *unit;
            param_format(d, x[i], val, &unit);
            printf("%s[%d,%d,%d,%d,", e++ ? "," : "", k < (uint32_t)P_E0 ? 0 : 1, k < (uint32_t)P_E0 ? (int)k : (int)(k - P_E0),
                   x[i], d->max);
            js_str(val);
            putchar(']');
        }
    }
    printf("],\n\"SHOWN\":[");                         /* #48: the values as a knob steps them (params.c param_turn), */
    {                                                   /* every F_ENUM: [scope, index, the names from the left end] */
        for (k = 0, e = 0; k < (uint32_t)P_E0 + G_COUNT; k++) {
            const param_desc_t *d = k < (uint32_t)P_E0 ? &TP[k] : &GP[k - P_E0];
            int32_t v = d->def, n;
            if (d->fmt != F_ENUM || !d->names || d->max <= d->min)
                continue;
            for (n = 0; n < 64; n++)
                v = param_turn(d, v, -1);
            printf("%s[%d,%d,[", e++ ? "," : "", k < (uint32_t)P_E0 ? 0 : 1, k < (uint32_t)P_E0 ? (int)k : (int)(k - P_E0));
            for (n = 0; n < 64; n++) {
                int32_t w = param_turn(d, v, 1);
                js_str(d->names[v - d->min]);
                if (w == v)
                    break;
                putchar(',');
                v = w;
            }
            printf("]]");
        }
    }
    printf("]}\n");
    return 0;
}
