/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "REPEAT",   /* (append-only: seq.c AM_*) */
                                       "DNUP", "UP+8", "CONV", "DIVG", "PINKY", "THUMB", "WALK", "CHORD"};
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
/* (Skiff 1.3, from Felucca 1.5.1) the delay's TIME (G_DTIME): N_DIV's ten (their values kept), then the dotted ones
 * appended (stored values: append-only; firmware before reads them as 4BAR). A list of its own: ARP RATE, SEQ DIV and
 * LFO SYNC keep N_DIV's ten (fx.c delay_samples) */
static const char *const N_DTIME[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR",
                                      "1/8D", "1/16D", "1/4D"};
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH"};
static const char *const N_ONOFF[] = {"OFF", "ON"};
/* seq.c kb_map; 1 = SNAP (stored projects: the former ON); 3 = SEQ: SNAP, and the sequencer's notes snap too as
 * they play (seq.c seq_step; the steps keep what was written). Append-only: older projects hold 0..2 */
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE", "SEQ", "SEQ+T"};   /* ((1.3) SEQ+T appended) */
/* chord keys (chord.c): OFF, the diatonic triad / seventh of the track's ROOT and SCALE on the key, fixed shapes */
static const char *const N_CHRD[] = {"OFF", "DIA3", "DIA7", "MAJ", "MIN", "DOM7", "MAJ7", "MIN7", "SUS4", "POW"};
static const char *const N_VOIC[] = {"CLOSE", "OPEN", "INV1", "INV2", "+OCT"};   /* VC_CLOSE .. VC_BASS */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_DIR[] = {"FWD", "RVRS", "PING", "RAND"};   /* ((Skiff U4, L2) RVRS: REV is reverb) */   /* (Skiff) DIR_* (seq.c seq_next) */
static const char *const N_DKSRC[] = {"OFF", "T1", "T2", "T3", "T4", "T5", "T6"};   /* (Skiff) P_DKSRC: the track ducked under */
_Static_assert(sizeof N_DKSRC / sizeof N_DKSRC[0] == NTRK + 1u, "DUCK's BY: OFF and every track");
static const char *const N_DTYPE[] = {"SOFT", "HARD", "FOLD", "CRUSH", "FUZZ"};   /* (Skiff phase 9) P_DTYPE: DT_* (fx.c) */
#define TRK_STR_(n) #n
#define TRK_STR(n) TRK_STR_(n)
static const char *const N_ORDER[] = {"NOTE", "PLAYED"};
/* (Skiff 1.3, from Felucca 1.2 / 1.5; append-only, as every list a project stores) */
static const char *const N_LSYNC[] = {"OFF", "4BAR", "2BAR", "1/1", "1/2", "1/4", "1/8", "8T", "1/16", "16T", "1/32"};
static const char *const N_LTRIG[] = {"NOTE", "FREE"};
static const char *const N_LPOL[] = {"BI", "UNI"};
static const char *const N_ITYPE[] = {"OFF", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR", "COMP"};
/* the INSERT's CRUSH: BITS 1..16 and RATE (the held sample's rate, fx.c INS_HOLD) over the range in 16 even parts */
/* (Skiff 1.3, from Felucca 1.5's #175) ENV SYNC (voice.c esync_coef): ATK DEC REL as note values of the tempo, shortest
 * first. 0..127 split evenly over these 25 (param_format's F_INT name list); ESYNC_UNITS: each in 1/384 of a bar of 4/4
 * (a quarter 96), 0 = the shortest time ATK / DEC / REL 0 has. 0-terminated (param_format) */
#define ESYNC_N 25u
static const char *const ESYNC_NAMES[ESYNC_N + 1u] = {
    "0", "1/64T", "1/64", "1/32T", "1/64D", "1/32", "1/16T", "1/32D", "1/16", "1/8T", "1/16D", "1/8", "1/4T", "1/8D",
    "1/4", "1/2T", "1/4D", "1/2", "1/1T", "1/2D", "1/1", "1/1D", "2BAR", "3BAR", "4BAR", 0};
static const uint16_t ESYNC_UNITS[ESYNC_N] = {0, 4, 6, 8, 9, 12, 16, 18, 24, 32, 36, 48, 64, 72,
                                              96, 128, 144, 192, 256, 288, 384, 576, 768, 1152, 1536};
static const param_desc_t ESYNC_DESC[3] = {   /* ATK DEC REL with ENV SYNC on: the range and defaults as ever */
    {"ATTACK", ESYNC_NAMES, 0, 127, 10, F_INT, 0},
    {"DECAY", ESYNC_NAMES, 0, 127, 70, F_INT, 0},
    {"RELEASE", ESYNC_NAMES, 0, 127, 60, F_INT, 0},
};
static uint32_t esync_idx(int32_t v) { return (uint32_t)(v & 127) * ESYNC_N >> 7; }   /* (param_format's split) */
static const char *const N_IBITS[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
                                      "16", 0};
static const char *const N_IRATE[] = {"689", "919", "1.1k", "1.4k", "1.8k", "2.2k", "2.8k", "3.7k", "4.4k", "5.5k",
                                      "7.4k", "8.8k", "11k", "15k", "22k", "44k", 0};
#if BOARD_SMK37
static const char *const N_CLOCK[] = {"INT", "USB"};   /* (SMK-37: no TRS MIDI IN; a saved TRS loads clamped, as USB) */
#else
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};
#endif
static const char *const N_MIDI_INPUT[] = {"USB", "TRS"};
static const char *const N_ROUTE[] = {"CH1-" TRK_STR(NTRK), "SEL"};   /* MIDI IN (seq.c): 1..NTRK -> parts, the rest
                                                                         * ignored / all -> selected */
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_RTYPE[] = {"ROOM", "SPRING", "HALL"};   /* G_RTYPE: the reverb bus's model (fx.c) */
static const char *const N_CMODE[] = {"CHORUS", "FLANGER", "PHASER"};   /* (Skiff phase 9) G_CMODE (fx.c cho_mode) */
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
/* modulation matrix (mod.c): sources, destinations (E1..E8 = P_E0..P_E7: shown with the engine's labels) */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RAND", "MODW", "AT", "EXPR", "ENV2",
                                    "S&H", "SLEW"};   /* ((phase 9) ENV2; (1.3) S&H SLEW) */
static const char *const N_MDST[] = {"OFF", "PITCH", "CUT", "SHP", "AMP", "PAN", "DIST", "CHO", "DLY", "REV", "RATE",
                                     "VIB", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",
                                     "DEPTH", "VEL", "TIME"};   /* ((1.3) the LFO's depth; at a note: VEL, TIME) */
static const char *const N_ENGNAME[] = {"ANALOG", FELUCCA_FM4 ? "DIGITAL" : "-", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS",
                                             "DRUM", "NOISE", "FM6",
#if FELUCCA_SLICE
                                             "SLICE",
#else
                                             "-",
#endif
                                             "DRUM-X",
};

#define PD(l, f, mn, mx, df) {l, 0, mn, mx, df, f, 0}
#define PE(l, n, df) {l, n, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, F_ENUM, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LEVEL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATTACK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DECAY", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUSTAIN", F_PCT, 0, 127, 90),
    [P_REL] = PD("RELEASE", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("CUTOFF", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PITCH", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHAPE", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("FX", F_BIPCT, -64, 63, 0),
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHASE", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PITCH", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("CUTOFF", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHAPE", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIV, 2),
    [P_AOCT] = PD("OCTAVE", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWING", F_PCT, 0, 100, 0),
    [P_APROB] = PD("CHANCE", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORDER", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCALE", N_SCALE, 0),
    [P_QUANT] = PE("QUANTIZE", N_QUANT, 0),
    [P_TRANS] = PD("TRANSP", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LENGTH", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIV, 2),
    [P_SSWING] = PD("SWING", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 127),   /* ((2.0) a note 1 STEP by default: NOTES' LENGTH; 64 before) */
    [P_DIST] = PD("DIST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHORUS", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DELAY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REVERB", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VOICE", N_VOICE, 0),
    [P_GLIDE] = PD("GLIDE", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GL MODE", N_GLMODE, 0),
    [P_PRIO] = PE("PRIORITY", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DETUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLICER", N_SLCR, 0),
    [P_SLPAT] = PD("PATTERN", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
#define MSLOT(k) [P_M##k##SRC] = PE("SRC" #k, N_MSRC, 0), [P_M##k##DST] = PE("DEST" #k, N_MDST, 0), \
                 [P_M##k##AMT] = PD("AMT" #k, F_BIPCT, -64, 63, 0)
    MSLOT(1), MSLOT(2), MSLOT(3), MSLOT(4),
#undef MSLOT
/* the OP ENV / OP LEVEL values of DIGITAL (ids 61..80): with FELUCCA_FM4 its operator envelopes; without it
 * inert, on no page and in no editor layout, read only when a DIGITAL sound converts (fm4_convert.c). Their labels
 * stay: the editor's library files key parameters by label ("ATK#2" ..), so an old file's values still find them */
#define FMOP(k) [P_FM##k##_ATK] = PD("ATK", F_TIME, 0, 127, 0), \
                [P_FM##k##_DEC] = PD("DEC", F_TIME, 0, 127, 0), \
                [P_FM##k##_SUS] = PD("SUS", F_PCT, 0, 127, 127), \
                [P_FM##k##_REL] = PD("REL", F_TIME, 0, 127, 0), \
                [P_FM##k##_LEVEL] = PD("LVL", F_PCT, 0, 127, 127)
    FMOP(1), FMOP(2), FMOP(3), FMOP(4),
#undef FMOP
    [P_CHRD] = PE("CHORD", N_CHRD, 0),
    [P_VOIC] = PE("VOICING", N_VOIC, 0),
/* the DRUM engine's lane levels (eng_drum.c, #97): 100 % = the kit as designed (the sound before 1.1) */
#define LANE(k, l) [P_LN0 + (k)] = PD(l, F_PCT, 0, 127, 127)
    LANE(0, "KICK"), LANE(1, "SNARE"), LANE(2, "CLAP"), LANE(3, "CL HAT"),
    LANE(4, "OP HAT"), LANE(5, "TOM"), LANE(6, "RIM"), LANE(7, "BELL"),
#undef LANE
    [P_DIR] = PE("DIR", N_DIR, 0),                 /* (Skiff) the play direction */
    [P_DKSRC] = PE("BY", N_DKSRC, 0),              /* (Skiff) ducking (duck.c): under that track's hits, */
    [P_DKAMT] = PD("DEPTH", F_PCT, 0, 127, 96),    /* this far down (100 %: silent), */
    [P_DKREL] = PD("RELEASE", F_TIME, 0, 127, 73),     /* back in this long (199 ms) */
    [P_DTYPE] = PE("TYPE", N_DTYPE, 0),            /* (Skiff phase 9) FX > DRIVE (fx.c track_dist): DIST's clipper, */
    [P_DTONE] = PD("TONE", F_BIPCT, -64, 63, 0),   /* its tone moved from where the drive puts it (0: as before) */
    [P_EG2_ATK] = PD("ATTACK", F_TIME, 0, 127, 0),   /* (Skiff phase 9) ENV 2 (voice.c env2_tick): MOD's ENV2 */
    [P_EG2_DEC] = PD("DECAY", F_TIME, 0, 127, 60),
    [P_EG2_SUS] = PD("SUSTAIN", F_PCT, 0, 127, 0),
    [P_EG2_REL] = PD("RELEASE", F_TIME, 0, 127, 40),
    [P_LSYNC] = PE("SYNC", N_LSYNC, 0),            /* (1.3, #16) OFF: RATE in Hz, as before */
    [P_LTRIG] = PE("TRIG", N_LTRIG, 0),            /* .. NOTE: from its PHASE at each note, as before */
    [P_LPOL] = PE("POLARITY", N_LPOL, 0),          /* .. BI: around the value, as before */
    [P_SPRD] = PD("SPREAD", F_PCT, 0, 127, 0),     /* (1.3) 0: every voice on PAN, as before */
    [P_ITYPE] = PE("TYPE", N_ITYPE, 0),            /* (1.3) the INSERT: OFF as before */
    [P_IA] = PD("A", F_PCT, 0, 127, 64),
    [P_IB] = PD("B", F_PCT, 0, 127, 96),
    [P_IC] = PD("C", F_PCT, 0, 127, 96),
    [P_IMIX] = PD("MIX", F_PCT, 0, 127, 127),
    [P_ESYNC] = PE("ENV SYNC", N_ONOFF, 0),        /* (1.3) ENV SYNC: OFF, ATK DEC REL in ms, as before ((2.0) on LFO 2:
                                                    * its whole name) */
/* (Skiff phase 9) the DRUM engine's lanes, each moved from its design (eng_drum.c drum_render): 64 = as designed */
#define LANE2(k) [P_LTUNE0 + (k)] = PD("TUNE", F_ST16, 0, 127, 64), [P_LDECAY0 + (k)] = PD("DECAY", F_BIPCT, -64, 63, 0)
    LANE2(0), LANE2(1), LANE2(2), LANE2(3), LANE2(4), LANE2(5), LANE2(6), LANE2(7),
#undef LANE2
/* (2.0, #36) DRUM's and DRUM-X's lanes, each panned as the track's PAN (voice.c lpan_render): C as before */
#define LANE3(k) [P_LPAN0 + (k)] = PD("PAN", F_BIPCT, -64, 63, 0)
    LANE3(0), LANE3(1), LANE3(2), LANE3(3), LANE3(4), LANE3(5), LANE3(6), LANE3(7),
#undef LANE3
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWING+", F_PCT, 0, 100, 0),   /* ((Skiff U4, R9) added to PATTERN's SWG) */
    [G_CLOCK] = PE("CLOCK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DTIME, 1),
    [G_DFDBK] = PD("FEEDBACK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLOR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("RATE", F_LFOHZ, 0, 127, 40),   /* ((Skiff U3) were CRT, CDP: GLO > CHORUS says CHORUS) */
    [G_CDEPTH] = PD("DEPTH", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_MIDI_INPUT, 0),
    [G_SYNC] = PE("SYNC", N_DASH, 0),
    [G_ROUTE] = PE("ROUTE", N_ROUTE, 0),          /* (was "--": stored 0 = CH1-4, as MIDI IN always was) */
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENGINE", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    /* the reverb's model on the REVERB page: the id of the old GM drum channel (G_DRCH, inert since 1.0) */
    [G_RTYPE] = PE("TYPE", N_RTYPE, 0),
    /* (Skiff phase 9) GLO > CHORUS's MODE and FEEDBACK, in the ids of the old GM drum part's level and reverb send
     * (inert since 1.0: core.h), so G_COUNT, which the project format and the editor protocol depend on, stays */
    [G_CMODE] = PE("MODE", N_CMODE, 0),
    [G_CFDBK] = PD("FEEDBACK", F_PCT, 0, 127, 0),
};

/* (Skiff 1.3, from Felucca 1.5) the INSERT's A B C as its TYPE means them (TP's ranges and defaults): the drives DRIVE
 * TONE LEVEL, CRUSH BITS RATE LPF, the swept ones RATE DEPTH FDBK; OFF TP's own */
#define PU(l, f, df, n, u) {l, n, 0, 127, df, f, u}
static const param_desc_t INS_DESC[4][3] = {
    {PU("DRIVE", F_PCT, 64, 0, 0), PU("TONE", F_CUTOFF, 96, 0, 0), PU("LEVEL", F_DB, 96, 0, 0)},
    {PU("BITS", F_INT, 64, N_IBITS, 0), PU("RATE", F_INT, 96, N_IRATE, U_HZ), PU("LPF", F_CUTOFF, 96, 0, 0)},
    {PU("RATE", F_LFOHZ, 64, 0, 0), PU("DEPTH", F_PCT, 96, 0, 0), PU("FDBK", F_PCT, 96, 0, 0)},
    {PU("THRESH", F_CMPTH, 64, 0, 0), PU("RATIO", F_CMPRATIO, 64, 0, 0), PU("RELEASE", F_CMPREL, 24, 0, 0)},
};
#undef PU
static const param_desc_t *ins_desc(int32_t type, uint32_t k)
{
    if (type <= 0 || type >= (int32_t)NELEM(N_ITYPE))
        return &TP[P_IA + k];
    return &INS_DESC[type == 9 ? 3 : type < 5 ? 0 : type == 5 ? 1 : 2][k];   /* (IT_SOFT .. IT_FUZZ, IT_CRUSH, the swept ones) */
}

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_IA && id <= P_IC)                     /* ((1.3) the INSERT's values as its TYPE names them) */
        return ins_desc(t->p[P_ITYPE], id - P_IA);
    if (t->p[P_ESYNC] && (id == P_ATK || id == P_DEC || id == P_REL))   /* ((1.3) ENV SYNC: note values) */
        return &ESYNC_DESC[id == P_ATK ? 0 : id == P_DEC ? 1 : 2];
    if (id >= P_E0 && id <= P_E7) {                   /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[eng_idx(t->eng_req)];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    return &TP[id];
}

/* the descriptor of parameter id for engine e without the engine's desc hook (the device display
 * only; range and default are the same): what the editor protocol, user presets and projects use */
static const param_desc_t *param_desc_of(uint32_t e, uint32_t id)
{
    return id >= P_E0 ? &ENGINES[e]->edit[id - P_E0] : &TP[id];
}

/* a retired F_ENUM value kept as an alias, so stored values stay valid: SAMPLE SET and GRAIN SRC 1, once
 * TRANH, and 4, once PERC (a SAMPLE sound of it loads as DRUM: core.h drum_from_perc), play PIANO
 * (tools/gen_samples.py SMP_SET_ORIG); DRUM KIT 1..3, once HAND CYM H+CYM, play 66 10 77 (eng_drum.c DK_PLAYS).
 * It shows the original's name; knobs step over it and the editor's SET lands on the original. -> the value v
 * stands for */
static int32_t enum_orig(const param_desc_t *d, int32_t v)
{
#if BOARD_SMK37 && FELUCCA_SLICE
    if (d->names == N_SLC_SRC && (v == 2 || v == 3))  /* (SMK-37: USR2, USR3 have no flash; the knob steps over them) */
        return 1;
#endif
    if (d->names == N_DRUM_KIT)
        return v >= 0 && v < DK_COUNT ? (int32_t)DK_PLAYS[v] : v;
    return d->names == SMP_ALL_NAMES && v >= 0 && v < SMP_NSETS ? SMP_SET_ORIG[v] : v;
}

/* a stored value as the parameter takes it (projects, user presets, motion): inside d's range, and a retired
 * DRUM KIT (1..3) as the kit it plays, so KIT never holds one again. (SAMPLE / GRAIN's aliases keep their
 * number: they are what the sound was saved with, and play the original anyway) */
static int32_t param_fit(const param_desc_t *d, int32_t v)
{
    v = clamp(v, d->min, d->max);
    return d->names == N_DRUM_KIT ? enum_orig(d, v) : v;
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT, (2.0, #30) a song entry's sound: launch.c esnd_put, in the audio ISR) leaves alone. The mix (LEVEL,
 * PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2), the scale and key map (SCL), the pattern parameters
 * (LEN, DIV, SWING, GATE) and the SLICER insert, which chops whatever the track plays in time with its sequencer */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC || i == P_DIR ||   /* ((Skiff) DIR: the pattern's; */
           (i >= P_DKSRC && i <= P_DKREL);           /* the ducking: the mix's) */
}

/* a knob moved an F_ENUM from `from` to v: past any alias in that direction (back to `from` at the end) */
static int32_t enum_step(const param_desc_t *d, int32_t from, int32_t v)
{
    int32_t dir = v > from ? 1 : -1;
    while (v != from && enum_orig(d, v) != v)
        v = v + dir > d->max || v + dir < d->min ? from : v + dir;
    return v;
}

/* #48: the note divisions in the order of their length, longest first (triplets between their neighbours), on the
 * knobs and the gauges; the stored values (N_DIV, N_SLDIV indices: projects, presets, the editor protocol) stay.
 * -> the shown order of d's values (index: position, entry: value), 0 = the values' own order */
static const uint8_t DIV_ORDER[10] = {9, 8, 7, 6, 0, 1, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4 1/8 8T 1/16 16T 1/32 */
static const uint8_t SLDIV_ORDER[6] = {0, 3, 1, 4, 2, 5};              /* 1/8 8T 1/16 16T 1/32 32T */
static const uint8_t DTIME_ORDER[13] = {9, 8, 7, 6, 12, 0, 10, 1, 11, 4, 2, 5, 3};   /* 4BAR 2BAR 1/1 1/2 1/4D 1/4 1/8D
                                                                                      * 1/8 1/16D 8T 1/16 16T 1/32 */
static const uint8_t *enum_order(const param_desc_t *d)
{
    return d->names == N_DIV ? DIV_ORDER : d->names == N_SLDIV ? SLDIV_ORDER : d->names == N_DTIME ? DTIME_ORDER : 0;
}
static int32_t enum_rank(const param_desc_t *d, int32_t v)   /* v's place in the shown order (+ min): the gauges */
{
    const uint8_t *o = enum_order(d);
    int32_t r;
    for (r = 0; o && r < d->max - d->min; r++)
        if (o[r] == v - d->min)
            break;
    return o ? r + d->min : v;
}
/* a knob turned `steps` (signed) from v: clamped, over the shown order, past aliases (enum_step) */
static int32_t param_turn(const param_desc_t *d, int32_t v, int32_t steps)
{
    if(d->fmt==F_CMPRATIO){
        int32_t current=clamp(v,0,127)>>4,next=clamp(current+steps,0,7);
        return next==current?v:next*16; /* one visible ratio per detent; retain stored 0..127 values */
    }
    const uint8_t *o = enum_order(d);
    if (o)
        return o[clamp(enum_rank(d, v) - d->min + steps, 0, d->max - d->min)] + d->min;
    if (d->names == ESYNC_NAMES) {                    /* ((1.3) ENV SYNC: a note value a step, the first value showing it) */
        int32_t k = clamp((int32_t)esync_idx(v) + steps, 0, ESYNC_N - 1);
        return (k * 128 + ESYNC_N - 1) / ESYNC_N;
    }
    return enum_step(d, v, clamp(v + steps, d->min, d->max));
}

static uint8_t fmt_named;        /* the last param_format was a name (F_ENUM, even "4"): ui_draw.c's digits do not roll it */

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    fmt_named = d->fmt == F_ENUM;
    switch (d->fmt) {
    case F_PCT:                                     /* a 0..100 range (SWG) is its value, others a share of 127 */
        fmt_int(val, d->max == 100 ? v : (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:                                   /* -64..63 as -100..+100 % (Felucca #199: each side over its own
                                                     * range; up to Skiff 1.2 / 64 both ways, the top read +98) */
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 63);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_CMPTH:
        fmt_fix(val,-480+(v*480+63)/127,1);*unit="dB";break;
    case F_CMPRATIO: {
        static const char *const names[8]={"1:1","1.5:1","2:1","3:1","4:1","6:1","8:1","12:1"};
        str_cpy(val,names[(uint32_t)clamp(v,0,127)>>4],8);fmt_named=1;break;
    }
    case F_CMPREL:
        fmt_int(val,20+(v*980+63)/127);*unit="ms";break;
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10(v), 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 8);   /* ((Skiff U4, V5) 7 letters: the
                                                       * card falls to M, then S, where V is too wide) */
        if (d->unit)
            *unit = PD_UNIT_OF(d);
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    case F_ST16: {                                  /* (Skiff U1) +-12 semitones in 3/16 steps, to 0.1: "+3.2" */
        int32_t t = (v - 64) * 30, a;               /* (tenths x 16, rounded half away from 0) */
        char *q = val;
        t = (t + (t < 0 ? -8 : 8)) / 16;
        a = t < 0 ? -t : t;
        if (t)
            *q++ = t < 0 ? '-' : '+';
        fmt_int(q, a / 10);
        q += str_len(q);
        q[0] = '.';
        q[1] = (char)('0' + a % 10);
        q[2] = 0;
        *unit = "st";
        break;
    }
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = PD_UNIT_OF(d);
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK };   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_MOD,
       GR_FEEL,                    /* (Skiff 2.0, D) SEQ > FEEL: CHANCE RATCHET NUDGE TYPE of the selection (ui.c nsel), on
                                    * the tiles (a DRUM track: the grid); CHANCE's and PLAY's before */
       GR_CHORD, GR_SLICES,
       GR_EVENTS,                  /* SEQ > AUTOMATION ((Skiff U3, R5) the list, AUTO LIST's, and its own row: ui_events.c;
                                    * SAVE > TOOLS (R3) and the AUTOMATION page of before are gone) */
       GR_BANK,                    /* (Skiff) SAVE > PATTERNS: the bank's 16 slots of the track (bank.c) */
       GR_DUCK,                    /* (Skiff) FX > DUCK: the track's level over a bar of its source's hits */
       GR_SONG,                    /* (Skiff phase 5) SEQ > SONG: the tracks' lists (bank.c), ENTRY PATTERN REPS SONG */
       GR_DXSND,                   /* (Skiff phase 7, 8) EDIT > SOUND: a sound of the DRUM-X kit, the sheet (ui_dx.c) */
       GR_LANES,                   /* (Skiff U3, R6) EDIT > LANES (DRUM): the eight lane levels, KNOB 1 the lane (ui.lane) */
       GR_DELAY, GR_REVERB, GR_CHORUS, GR_GLOBAL, GR_VOICE, GR_VOICE2,   /* ((2.0) ENV DEST LFO DEST: MOD) */
       GR_ARP2,                    /* (Skiff U5, V1) the pages' own graphs, the scope's before (ui_pgraph.c); ARP 2: ARP's */
       GR_FMOP, GR_FMALL,          /* (FM6 LAB) EDIT > OPERATOR and ALL OPS: FM6's patch, two sheets (ui_fm6.c) */
       GR_DRIVE,                   /* (Skiff phase 9) FX > DRIVE: the TYPE's transfer at the drive (ui_pgraph.c) */
       GR_INS,                     /* (Skiff 1.3) FX > INSERT: its TYPE's transfer or sweep (ui_pgraph.c graph_insert) */
       GR_SMIX,                    /* (Skiff, #21) EDIT > SAMPLES: a user sample's LEVEL, REVERB, DELAY (ui.c smx_zone) */
       GR_DCOMP, GR_LMIX };                  /* (2.0, #36, the user's pick 1A) EDIT > LANE MIX: a kit's lane levels and pans */

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;

/* (Skiff U3, the UX review's structure) the order a family's button and SELECT step its pages in. FX holds the track's
 * effects; the buses everyone shares are GLO's, after MIXER (N4); SEQ goes by task, SONG last (N2); SYSTEM (R2: ROUT and
 * CPU in the MENU, CLK and TUNE too, R8: the unit's settings), SAVE > TOOLS (R3), AUTOMATION's own page (R5: the list's
 * top row) and LANES 2 (R6: one LANES page) are gone */
static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV 2", FAM_ENV, SC_TRACK, GR_ADSR, {P_EG2_ATK, P_EG2_DEC, P_EG2_SUS, P_EG2_REL}},   /* (Skiff phase 9) MOD's ENV2 */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO 2", FAM_LFO, SC_TRACK, GR_LFO, {P_LSYNC, P_LTRIG, P_LPOL, P_ESYNC}},   /* (1.3, #16) SYNC TRIG POLARITY; (2.0)
                                                                               * ENV SYNC, ENV DEST's of before */
    {"MOD", FAM_LFO, SC_TRACK, GR_MOD, {0xFF, P_M1SRC, P_M1DST, P_M1AMT}},   /* KNOB 1: the route (mod_ui_route; (2.0,
                                                                         * D 1A) LFO DEST and ENV DEST are its first 7) */
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"DRIVE", FAM_FX, SC_TRACK, GR_DRIVE, {P_DIST, P_DTYPE, P_DTONE, 0xFF}},   /* (Skiff phase 9) DIST's TYPE, TONE */
    {"INSERT", FAM_FX, SC_TRACK, GR_INS, {P_ITYPE, P_IA, P_IB, P_IC}},   /* (1.3, from Felucca 1.5) the track's INSERT */
    {"INSERT 2", FAM_FX, SC_TRACK, GR_INS, {P_IMIX, 0xFF, 0xFF, 0xFF}},  /* .. its dry / wet */
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},
    {"DUCK", FAM_FX, SC_TRACK, GR_DUCK, {P_DKSRC, P_DKAMT, P_DKREL, 0xFF}},   /* (Skiff) ducking (duck.c) */
    {"SCALE", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},   /* ((#8) was SCL) */
    {"CHORD", FAM_SCL, SC_TRACK, GR_CHORD, {P_CHRD, P_VOIC, 0xFF, 0xFF}},   /* SEL again: the chord keys (chord.c) */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"OPERATOR", FAM_EDIT, SC_TRACK, GR_FMOP, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (FM6 LAB) FM6 only: an operator's values */
    {"ALL OPS", FAM_EDIT, SC_TRACK, GR_FMALL, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (FM6 LAB) FM6 only: the voice's (ui_fm6.c) */
    {"LANES", FAM_EDIT, SC_TRACK, GR_LANES, {0xFF, 0xFF, 0xFF, 0xFF}},   /* DRUM only: LANE, its LEVEL TUNE DECAY
                                                                         * (page_desc) */
    {"SOUND", FAM_EDIT, SC_TRACK, GR_DXSND, {0xFF, 0xFF, 0xFF, 0xFF}},     /* (Skiff phase 7, 8) DRUM-X only: a sound of the
                                                                         * project's kit on one sheet (ui_dx.c) */
    {"LANE MIX", FAM_EDIT, SC_TRACK, GR_LMIX, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (2.0, #36) DRUM and DRUM-X: LANE, its LEVEL
                                                                         * and PAN (page_desc) */
    {"DRUM COMP", FAM_EDIT, SC_TRACK, GR_DCOMP, {0xFF,0xFF,0xFF,0xFF}},
    {"SLICES", FAM_EDIT, SC_TRACK, GR_SLICES, {0xFF, 0xFF, 0xFF, 0xFF}},   /* SLICE only: the slices by hand (ui_slice.c) */
    {"SAMPLES", FAM_EDIT, SC_GLOBAL, GR_SMIX, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (#21) SAMPLE on a USR set only: KNOB 1 the
                                                                         * sample, 2..4 its LEVEL REVERB DELAY (page_desc;
                                                                         * SC_GLOBAL: no lock, no automation) */
    {"OP1 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM1_ATK, P_FM1_DEC, P_FM1_SUS, P_FM1_REL}},
    {"OP2 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM2_ATK, P_FM2_DEC, P_FM2_SUS, P_FM2_REL}},
    {"OP3 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM3_ATK, P_FM3_DEC, P_FM3_SUS, P_FM3_REL}},
    {"OP4 ENV", FAM_EDIT, SC_TRACK, GR_ADSR, {P_FM4_ATK, P_FM4_DEC, P_FM4_SUS, P_FM4_REL}},
    {"OP LEVEL", FAM_EDIT, SC_TRACK, GR_NONE, {P_FM1_LEVEL, P_FM2_LEVEL, P_FM3_LEVEL, P_FM4_LEVEL}},
    {"VOICE", FAM_EDIT, SC_TRACK, GR_VOICE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_VOICE2, {P_ALLOC, P_DETUNE, P_SPRD, 0xFF}},   /* ((U3, R4) PAN MUTE: MIXER's;
                                                                                     * (1.3) SPREAD) */
    {"MIXER", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* GLO button; LEVEL PAN REV MUTE */
    {"DELAY", FAM_GLO, SC_GLOBAL, GR_DELAY, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},   /* ((U3, N4) the buses: FX's before) */
    {"REVERB", FAM_GLO, SC_GLOBAL, GR_REVERB, {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF}},   /* TYPE: ROOM / SPRING / HALL */
    {"CHORUS", FAM_GLO, SC_GLOBAL, GR_CHORUS, {G_CRATE, G_CDEPTH, G_CMODE, G_CFDBK}},   /* ((phase 9) MODE, FEEDBACK) */
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_GLOBAL, {G_BPM, G_SWING, 0xFF, 0xFF}},   /* ((U3, R8) CLK TUNE: the MENU's) */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PATTERNS", FAM_SAVE, SC_GLOBAL, GR_BANK, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (Skiff phase 5) the pool: PATTERN LEN USED CLEAR */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_ARP2, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"NOTES", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},   /* (Skiff 2.0, D: STEP ROLL CHANCE PLAY as two pages) the roll:
                                                         * PITCH START LENGTH VELOCITY of the selection (a DRUM track:
                                                         * the grid, LANE HIT ACC VELOCITY); PRESETS the step */
    {"FEEL", FAM_SEQ, SC_STEP, GR_FEEL, {0xFF, 0xFF, 0xFF, 0xFF}},
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},   /* (2.0) PRESETS: the track's DIR */
    {"AUTOMATION", FAM_SEQ, SC_TRACK, GR_EVENTS, {0xFF, 0xFF, 0xFF, 0xFF}},   /* 1.1.5's AUTO LIST: the locks and events */
    {"SONG", FAM_SEQ, SC_TRACK, GR_SONG, {0xFF, 0xFF, 0xFF, 0xFF}},   /* (Skiff phase 5) the lists (ui_input.c song_knob) */
#if FELUCCA_TAPE
    {"TAPE SEND", FAM_FX, SC_TRACK, GR_NONE, {0xFF,0xFF,0xFF,0xFF}},
#endif
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))
/* (2.0, D 1A) the MOD page: the route KNOB 1 picks (0..10): the seven fixed routings (LFO DEST's and ENV DEST's of
 * before: the LFO to PITCH CUT SHAPE AMP, the ENV to PITCH CUT SHAPE), whose AMOUNT alone turns, then the matrix's
 * four slots (MOD_SLOT0 on), their SOURCE DEST AMOUNT */
#define MOD_ROUTES 11u
#define MOD_SLOT0 7u
static const uint8_t MOD_FIXED[MOD_SLOT0] = {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP, P_ED_PIT, P_ED_FLT, P_ED_SHP};
static uint8_t mod_ui_route;
static uint32_t page_lane(void);   /* (Skiff U3, R6) EDIT > LANES, (2.0) LANE MIX: the lane KNOB 1 picked (ui.c) */
static uint32_t smx_zone(uint32_t *k);   /* (#21) EDIT > SAMPLES: the sample (ui.c) */
static int16_t smx_sel __attribute__((section(".pool")));   /* (#21) .. KNOB 1's: its number, 1..16 */

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    uint32_t id = pg->graph == GR_LMIX ? (slot == 1u ? P_LN0 + page_lane() : slot == 2u ? P_LPAN0 + page_lane() : 0xFFu) :
                  pg->graph != GR_LANES ? pg->id[slot] : slot == 1u ? P_LN0 + page_lane() :   /* (LANES: its LEVEL, */
                  slot == 2u ? P_LTUNE0 + page_lane() : slot == 3u ? P_LDECAY0 + page_lane() : 0xFFu;   /* (phase 9)
                                                       * TUNE, DECAY; (2.0) LANE MIX: LEVEL, PAN) */
    if (pg->graph == GR_SMIX) {                       /* (#21) SAMPLES: KNOB 1 the sample, 2..4 its LEVEL, REVERB, DELAY */
        static const param_desc_t SMX_D[4] = {PD("SAMPLE", F_INT, 1, 16, 1), PD("LEVEL", F_PCT, 0, 127, 127),
                                              PD("REVERB", F_PCT, 0, 127, 0), PD("DELAY", F_PCT, 0, 127, 0)};
        uint32_t k, z = smx_zone(&k);
        *valp = z == 0xFFu ? 0 : slot ? &usr_mix[k][slot - 1u][z] : &smx_sel;
        smx_save = 7u;                                /* (kept once the page is left: slice_store.c, a slot only when it
                                                       * changed) */
        return *valp ? &SMX_D[slot] : 0;
    }
    if (id == 0xFFu) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    if (pg->graph == GR_MOD) {                        /* the route shown: a fixed one's AMOUNT, a slot's SRC DST AMT */
        if (mod_ui_route < MOD_SLOT0 && slot != 3u) {
            *valp = 0;
            return 0;
        }
        id = mod_ui_route < MOD_SLOT0 ? MOD_FIXED[mod_ui_route] : id + 3u * (mod_ui_route - MOD_SLOT0);
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
