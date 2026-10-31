/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff 2.1) Chord follow (firmware/src/follow.c; SCL > FOLLOW: FOLLOW, MODE, NOTE) on the real sources with
 * tests/ui_test.c's stubs: T1 follows T2 (a lower track than its leader: the order the tracks tick in), C major.
 *   ROOT     the chord's root from its notes: C E G, F/A, B dim either way up, sus4, a power chord, Am7 / C6 by its
 *            lowest note, a single note
 *   KEY      a bass on C3 under C F G Am: C F G A, the nearest way (G and A below); a tritone up
 *   NOTE     ROOT, LOW (F/A: A), HIGH; LAST from keys played on the leader
 *   SCALE    C E G under Am: A C E (in key); under F: F A C; a note off the scale keeps its semitone
 *   FIT      to the nearest note of the chord, a tie the lower; two notes onto one: one voice, ending with the last
 *   TIE      a note tied over a chord change keeps its pitch, one note-on, nothing left gated
 *   AGAIN    a note struck again where the chord has moved: the old pitch ends
 *   LIVE     the keys, MIDI IN and the ARP on a follower follow; the leader moving while they are held, their release
 *            leaves nothing sounding
 *   KEYS     the leader's keys: a note with none held starts a chord, the others join it (LAST: the last one)
 *   REC      recording on a follower keeps the key pressed (it sounds moved)
 *   STOP     clears the chords: a follower as played
 *   KITS     a DRUM follower plays its lanes as they are; a muted leader still leads; FOLLOW on itself: as played
 *   STORE    the three settings by stored id (143..145), a record of before without them: OFF; a sound load keeps them
 *   PAGE     SCL a third time: FOLLOW; KNOB 1 never the track itself; the graph drawn anew as the chord changes
 * Built and run by tests/run_tests.sh. */
#define UI_TEST_NO_MAIN 1
#include "ui_test.c"

#define K(n) ((n) - 53u)                                /* the key of note n (QNT OFF, no octave: F3 is key 0) */
static track_t *F, *Ld;                                 /* the follower T1, its leader T2 */
static uint32_t at, L;                                  /* samples since PLAY, a step's */

static int sounds(const track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t nsound(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    midi_enqueue(st >> 4 | st << 8 | d1 << 16 | d2 << 24, 1);
    events_block(CTL);
}
/* three melodic tracks in C major, POLY, LEN 4, GATE half a step, no swing, no steps; T1 follows T2 (KEY, ROOT) */
static void setup(void)
{
    uint32_t k, i;
    ui_power_on();
    song.g[G_SWING] = 0;
    for (k = 0; k < 3u; k++) {
        track_t *t = &trk[k];
        set_engine_of(t, 0);
        track_defaults_steps(t);
        for (i = 0; i < NSTEP; i++)
            t->step[i] = (step_t){{0}, 0, ST_REST, 0, 0};
        t->p[P_SLEN] = 4; t->p[P_SGATE] = 64; t->p[P_SSWING] = 0; t->p[P_AMODE] = 0; t->p[P_CHRD] = 0;
        t->p[P_VOICE] = V_POLY; t->p[P_SCALE] = 1; t->p[P_ROOT] = 0; t->p[P_QUANT] = 0; t->p[P_MUTE] = 0;
    }
    F = &trk[0];
    Ld = &trk[1];
    F->p[P_FOLLOW] = 2;
    F->p[P_FMODE] = FM_KEY;
    F->p[P_FNOTE] = FN_ROOT;
    events_block(CTL);
}
static void play(void)
{
    seq_start();
    at = 0;
    L = step_samples(F, div_samples((uint32_t)F->p[P_SDIV]), 0);
}
static void run_to(uint32_t pos)
{
    while (at + CTL <= pos) {
        events_block(CTL);
        at += CTL;
    }
}
#define IN(s) ((s) * L + L / 4u)                        /* a quarter into step s */
static void chord(track_t *t, uint32_t s, uint32_t a, uint32_t b, uint32_t c)
{
    t->step[s] = (step_t){{(uint8_t)a, (uint8_t)b, (uint8_t)c}, c ? 3u : b ? 2u : 1u, ST_NOTE, 0, 100};
}
/* the leader's four steps: C, F, G, Am (root position) */
static void cfga(void)
{
    chord(Ld, 0, 60, 64, 67);
    chord(Ld, 1, 65, 69, 72);
    chord(Ld, 2, 67, 71, 74);
    chord(Ld, 3, 69, 72, 76);
}

static int roots(void)
{
    int bad = 0;
#define PC(a, b, c) ((1u << ((a) % 12u)) | (1u << ((b) % 12u)) | (1u << ((c) % 12u)))
    bad += check("ROOT: C E G -> C, F/A (A C F) -> F", fol_root(PC(60, 64, 67), 60) == 0u && fol_root(PC(57, 60, 65), 57) == 5u);
    bad += check("  B D F -> B, as D F B too (a third and a flat fifth)",
                 fol_root(PC(59, 62, 65), 59) == 11u && fol_root(PC(62, 65, 71), 62) == 11u);
    bad += check("  C F G (sus4) -> C, C G -> C, a single E -> E",
                 fol_root(PC(60, 65, 67), 60) == 0u && fol_root(PC(60, 67, 67), 60) == 0u && fol_root(1u << 4, 64) == 4u);
    bad += check("  A C E G -> A over A, C over C (C6)",
                 fol_root(PC(57, 60, 64) | 1u << 7, 57) == 9u && fol_root(PC(57, 60, 64) | 1u << 7, 60) == 0u);
#undef PC
    return bad;
}

static int key_note(void)
{
    int bad = 0, ok;
    uint32_t s;
    static const uint8_t WANT[4] = {48, 53, 43, 45};   /* C, F above, G below, A below */
    setup();
    cfga();
    for (s = 0; s < 4u; s++)
        F->step[s] = (step_t){{48}, 1, ST_NOTE, 0, 100};
    play();
    ok = 1;
    for (s = 0; s < 4u; s++) {
        run_to(IN(s));
        ok &= sounds(F, WANT[s]) && nsound(F) == 1u;
        if (!sounds(F, WANT[s]))
            printf("follow:   step %u: want %u\n", s + 1u, WANT[s]);
    }
    bad += check("KEY: C3 under C F G Am plays C3 F3 G2 A2 (the nearest way; T1 after its leader T2 on each step)", ok);
    run_to(4u * L + L - CTL);
    bad += check("  nothing left sounding", !nsound(F));
    seq_stop();
    setup();
    chord(Ld, 0, 66, 70, 73);                           /* F# */
    F->step[0] = (step_t){{48}, 1, ST_NOTE, 0, 100};
    play();
    run_to(IN(0));
    bad += check("  a tritone: up (C3 under F#: F#3)", sounds(F, 54));
    seq_stop();

    setup();
    chord(Ld, 0, 57, 60, 65);                           /* F/A */
    F->step[0] = (step_t){{48}, 1, ST_NOTE, 0, 100};
    F->p[P_FNOTE] = FN_LOW;
    play();
    run_to(IN(0));
    ok = sounds(F, 45);                                 /* LOW: A, below */
    seq_stop();
    F->p[P_FNOTE] = FN_ROOT;
    play();
    run_to(IN(0));
    ok &= sounds(F, 53);                                /* ROOT: F */
    seq_stop();
    F->p[P_FNOTE] = FN_HIGH;
    play();
    run_to(IN(0));
    ok &= sounds(F, 53);                                /* HIGH: F (65) */
    seq_stop();
    bad += check("NOTE: F/A moves C3 to A2 by LOW, to F3 by ROOT and by HIGH", ok);
    return bad;
}

static int scale_fit(void)
{
    int bad = 0, ok;
    setup();
    chord(Ld, 0, 57, 60, 64);                           /* Am */
    chord(Ld, 1, 65, 69, 72);                           /* F */
    chord(F, 0, 60, 64, 67);
    chord(F, 1, 60, 64, 67);
    F->step[2] = (step_t){{61}, 1, ST_NOTE, 0, 100};   /* C#: off the scale, under F still */
    Ld->step[2] = Ld->step[1];
    F->p[P_FMODE] = FM_SCALE;
    play();
    run_to(IN(0));
    ok = sounds(F, 57) && sounds(F, 60) && sounds(F, 64) && nsound(F) == 3u;
    bad += check("SCALE: C E G under Am plays A C E (down two scale steps, in key)", ok);
    run_to(IN(1));
    bad += check("  under F: F A C (up three)", sounds(F, 65) && sounds(F, 69) && sounds(F, 72) && nsound(F) == 3u);
    run_to(IN(2));
    bad += check("  a note off the scale keeps its semitone (C# under F: F#)", sounds(F, 66) && nsound(F) == 1u);
    seq_stop();

    setup();
    chord(Ld, 0, 65, 69, 72);                           /* F A C */
    F->step[0] = (step_t){{62, 59, 64}, 3, ST_NOTE, 0, 100};   /* D B E: C C F */
    F->p[P_FMODE] = FM_FIT;
    play();
    run_to(IN(0));
    ok = sounds(F, 60) && sounds(F, 65) && nsound(F) == 2u;
    bad += check("FIT: D B E under F A C play C C F (each to the nearest), C once", ok);
    fol_src[1].pc = 1u << 0 | 1u << 4;                  /* (C E: D two from either) */
    bad += check("  a tie goes to the lower (D between C and E: C)", fol_note(F, 62) == 60u && fol_note(F, 63) == 64u);
    run_to(L - CTL);
    bad += check("  their ends: nothing left sounding", !nsound(F));
    seq_stop();
    {   /* two notes onto one, played by hand: it ends with the last of them */
        events_block(CTL);
        sel_set(0);
        fol_src[1].pc = 1u << 0 | 1u << 5 | 1u << 9;    /* (F A C, as the leader left it) */
        fol_src[1].root = 5;
        key_down(K(62));
        key_down(K(59));
        ok = sounds(F, 60) && nsound(F) == 1u;
        key_up(K(62));
        ok &= sounds(F, 60);
        key_up(K(59));
        bad += check("  D and B held (both C): D let go, C goes on; B let go, it ends", ok && !nsound(F));
    }
    return bad;
}

static int tie_again(void)
{
    int bad = 0, ok;
    uint32_t v0, ons, i;
    setup();
    cfga();
    F->step[0] = (step_t){{48}, 1, ST_NOTE, 0, 100};
    F->step[1] = (step_t){{0}, 0, ST_TIE, 0, 0};       /* C3 tied on into step 2, where the chord is F */
    play();
    run_to(IN(0));
    v0 = vage;
    run_to(IN(1));
    ons = 0;
    for (i = 0; i < NVOICE; i++)
        ons += F->v[i].active && F->v[i].age > v0;
    ok = sounds(F, 48) && !sounds(F, 53) && !ons;
    bad += check("TIE: a note tied over a chord change keeps its pitch (C3 on, no note-on)", ok);
    run_to(3u * L);
    bad += check("  it ends: nothing left sounding", !nsound(F));
    seq_stop();

    setup();
    events_block(CTL);
    fol_src[1].pc = 1u << 0; fol_src[1].root = 0;      /* C */
    trk_note_on(F, 48, 100);
    ok = sounds(F, 48);
    fol_src[1].pc = 1u << 5; fol_src[1].root = 5;      /* F */
    trk_note_on(F, 48, 100);                           /* (the sequencer's "plays again": no note-off between) */
    ok &= sounds(F, 53) && !sounds(F, 48) && nsound(F) == 1u;
    trk_note_off(F, 48);
    bad += check("AGAIN: struck again where the chord moved (C to F): C3 ends, F3 sounds; its end ends F3", ok && !nsound(F));
    return bad;
}

static int live(void)
{
    int bad = 0, ok;
    uint32_t i;
    setup();
    cfga();
    sel_set(0);
    play();
    run_to(IN(0));
    key_down(K(60));
    ok = sounds(F, 60);                                 /* (C: as played) */
    run_to(IN(1));                                      /* (the chord now F) */
    key_up(K(60));
    ok &= !nsound(F);
    key_down(K(60));
    ok &= sounds(F, 65);
    key_up(K(60));
    bad += check("LIVE keys: C4 under C sounds C4; held into F, its release ends it; pressed under F: F4", ok && !nsound(F));
    midi(0x90, 60, 100);                                /* (CH1: T1) */
    ok = sounds(F, 65);
    run_to(IN(2));                                      /* (G) */
    midi(0x80, 60, 0);
    bad += check("  MIDI IN: note 60 under F sounds F4; the chord moving, its note-off ends it", ok && !nsound(F));
    F->p[P_AMODE] = 1;                                  /* ARP UP */
    key_down(K(60));
    key_down(K(64));
    ok = 0;
    for (i = 0; i < 100u; i++) {                        /* (G, the nearest way: the arp's notes G3 B3) */
        events_block(CTL);
        at += CTL;
        ok |= sounds(F, 55) || sounds(F, 59);
    }
    key_up(K(60));
    key_up(K(64));
    for (i = 0; i < 200u; i++)
        events_block(CTL);
    bad += check("  the ARP: C4 E4 held play G3 B3 under G; let go, nothing left sounding", ok && !nsound(F));
    F->p[P_AMODE] = 0;
    seq_stop();
    return bad;
}

static int lead_keys(void)
{
    int bad = 0;
    setup();
    sel_set(1);                                         /* the keys play the leader */
    key_down(K(64));
    key_down(K(60));
    bad += check("KEYS: E4 then C4 held on the leader: one chord (E C), LAST C, LOW C, HIGH E",
                 fol_src[1].pc == (1u << 4 | 1u << 0) && fol_src[1].last == 60 && fol_src[1].lo == 60 &&
                 fol_src[1].hi == 64 && fol_src[1].held == 2u);
    key_up(K(64));
    key_up(K(60));
    key_down(K(67));
    bad += check("  all let go, G4: a new chord (G alone)", fol_src[1].pc == 1u << 7 && fol_src[1].root == 7u);
    key_up(K(67));
    bad += check("  its chord stays when let go (for the followers)", fol_src[1].pc == 1u << 7 && !fol_src[1].held);
    return bad;
}

static int rec_stop(void)
{
    int bad = 0, ok;
    uint32_t s, got = 0, moved = 0;
    setup();
    for (s = 0; s < 4u; s++)
        chord(Ld, s, 65, 69, 72);                       /* F throughout */
    sel_set(0);
    song.rec = 1u;                                      /* T1 armed */
    play();
    run_to(IN(1));
    key_down(K(60));
    ok = sounds(F, 65);
    run_to(IN(1) + L / 4u);
    key_up(K(60));
    song.rec = 0;
    for (s = 0; s < 4u; s++) {
        got |= F->step[s].n && F->step[s].note[0] == 60;
        moved |= F->step[s].n && F->step[s].note[0] == 65;
    }
    bad += check("REC: C4 pressed on the armed follower sounds F4, its step keeps C4", ok && got && !moved);
    seq_stop();
    events_block(CTL);
    key_down(K(60));
    bad += check("STOP: the chords cleared, a follower's key as played (C4)", sounds(F, 60) && !fol_src[1].pc);
    key_up(K(60));
    return bad;
}

static int kits_self(void)
{
    int bad = 0, ok;
    setup();
    cfga();
    set_engine_of(F, ENGI_DRUM);
    F->step[1] = (step_t){{0}, 0, ST_NOTE, 0, 100, 1u};   /* KICK on step 2, under F */
    play();
    run_to(IN(1));
    ok = 0;
    for (uint32_t i = 0; i < NVOICE; i++)
        ok |= F->v[i].active && F->v[i].note == DRUM_LANE_NOTE[0];
    bad += check("KITS: a DRUM follower's KICK plays its lane note as it is", ok);
    seq_stop();

    setup();
    cfga();
    Ld->p[P_MUTE] = 1;
    F->step[1] = (step_t){{48}, 1, ST_NOTE, 0, 100};
    play();
    run_to(IN(1));
    bad += check("  a muted leader still leads (C3 under F: F3)", sounds(F, 53) && !nsound(Ld));
    seq_stop();
    F->p[P_FOLLOW] = 1;                                 /* itself */
    play();
    run_to(IN(1));
    bad += check("  FOLLOW on itself: as played", sounds(F, 48));
    seq_stop();
    return bad;
}

static int store(void)
{
    int bad = 0, ok;
    static uint8_t b[1u + SID_COUNT];
    static int16_t q[P_COUNT];
    uint32_t n;
    setup();
    F->p[P_FOLLOW] = 3; F->p[P_FMODE] = FM_FIT; F->p[P_FNOTE] = FN_LAST;
    n = sid_put(b, F->p);
    memset(q, 0, sizeof q);
    ok = n == 1u + SID_COUNT && sid_get(q, b, n) == n && !memcmp(q, F->p, sizeof q);
    ok &= p_sid(P_FOLLOW) == 143u && p_sid(P_FNOTE) == 145u && p_sid(P_E0) == 127u && p_sid(P_LPAN0) == 135u &&
          sid_p(143) == P_FOLLOW && sid_p(127) == P_E0 && sid_p(135) == P_LPAN0;
    bad += check("STORE: FOLLOW MODE NOTE by stored id 143..145, the rest as ever", ok);
    b[0] = 143;                                         /* (2.0's count: no chord follow) */
    memset(q, 0, sizeof q);
    ok = sid_get(q, b, 1u + 143u) == 144u && !q[P_FOLLOW] && !q[P_FMODE] && !q[P_FNOTE] && q[P_E0] == F->p[P_E0] &&
         q[P_LPAN7] == F->p[P_LPAN7];
    bad += check("  a record of before: its values, OFF", ok);
    apply_preset_to(F, 2);
    bad += check("  a sound load keeps them", F->p[P_FOLLOW] == 3 && F->p[P_FMODE] == FM_FIT && F->p[P_FNOTE] == FN_LAST &&
                 param_kept(P_FOLLOW) && param_kept(P_FMODE) && param_kept(P_FNOTE) && !motion_param(P_FOLLOW));
    return bad;
}

static int page(void)
{
    int bad = 0, ok;
    uint32_t sig;
    ui_power_on();
    sel_set(0);
    go_home(); frame();
    press(B_SCL); frames(400);
    press(B_SCL); frames(400);
    press(B_SCL); frames(400);
    ok = str_eq(cur_page()->title, "FOLLOW") && cur_page()->graph == GR_FOLLOW && cur_page()->fam == FAM_SCL &&
         cur_page()->id[0] == P_FOLLOW && cur_page()->id[1] == P_FMODE && cur_page()->id[2] == P_FNOTE;
    bad += check("PAGE: SCL a third time: FOLLOW (FOLLOW, MODE, NOTE)", ok);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    bad += check("  OFF: the page draws", ui.graph_sig != 0u);
    turn(EN_K1, 1); frame();
    ok = TSEL->p[P_FOLLOW] == 2;
    turn(EN_K1, -1); frame();
    bad += check("  KNOB 1 on T1: OFF, T2 (never itself), back to OFF", ok && !TSEL->p[P_FOLLOW]);
    turn(EN_K1, 1); frame();
    turn(EN_K2, 1); frame();
    turn(EN_K3, 2); frame();
    bad += check("  KNOB 2: MODE SCALE, KNOB 3: NOTE HIGH", TSEL->p[P_FMODE] == FM_SCALE && TSEL->p[P_FNOTE] == FN_HIGH);
    memset(host_screen, 0, sizeof host_screen); ui.force = 1; ui_draw();
    sig = ui.graph_sig;
    fol_step(&trk[1], (const uint8_t[]){57, 60, 65}, 3u);
    frame();
    bad += check("  T2 plays a chord: the graph draws it anew", ui.graph_sig != sig);
    press(B_SCL); frames(400);
    bad += check("  SCL again: SCALE", str_eq(cur_page()->title, "SCALE"));
    return bad;
}

int main(void)
{
    int bad = roots() + key_note() + scale_fit() + tie_again() + live() + lead_keys() + rec_stop() + kits_self() + store() +
              page();
    printf("%s\n", bad ? "FOLLOW TEST FAILED" : "chord follow test passed");
    return bad != 0;
}
