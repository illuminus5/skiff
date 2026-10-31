/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff 2.1) Chord follow (SCL > FOLLOW: P_FOLLOW, P_FMODE, P_FNOTE; DESIGN.md). A track set to FOLLOW another plays
 * in that track's chords: every note it sounds (its steps, keys, MIDI IN, ARP) moves as it starts.
 *   The chord a track leads with (fol_src): the notes of its latest step as they sound (seq_step, after its QNT SEQ:
 *   fol_step), or the keys and MIDI notes it holds (input_on / input_off, before the ARP; a note with none held starts
 *   a new chord, the others join it: fol_key; QNT SOLO's black keys, a tune, are none of it). Every track keeps its own:
 *   any track leads, a muted one too, a kit never. STOP and a panic of the track clear it: its followers play as
 *   written until it plays again.
 *   MODE KEY: the note moves by the distance from the follower's ROOT to the chord's note (NOTE: its ROOT, LOW, HIGH
 *   or LAST), the nearest way (-5..+6); SCALE: as many steps of the follower's own scale (ROOT, SCALE), the nearest
 *   way, so it stays in key (CHR: as KEY); FIT: to the nearest note of the chord (a tie: the lower).
 *   A note moves as it starts (voice.c trk_note_on: fol_on) and fol_map keeps where it went, so its end (trk_note_off:
 *   fol_off) ends that note however the chord has moved since: notes sounding, or tied on, keep their pitch. Recording,
 *   TAKES and step entry keep the notes as played: a take moves again with the chords. A kit (chord_kit) neither
 *   follows nor leads. The tracks followed tick first (fol_order): on one step the chord comes before the note that
 *   follows it.
 * Included by seq.c after chord.c; runs in the audio ISR with the rest of the note input. Not inlined: out of the
 * audio ISR's own cost (tests/target_budget.txt) */
enum { FM_KEY, FM_SCALE, FM_FIT };              /* P_FMODE (params.c N_FMODE) */
enum { FN_ROOT, FN_LOW, FN_HIGH, FN_LAST };     /* P_FNOTE (N_FNOTE) */

/* each track's chord as it leads: its pitch classes (bit i: i semitones above C; 0: none), its lowest, highest and
 * last notes and its root (a pitch class); how many keys and MIDI notes hold it (none: the next note starts anew) */
typedef struct { uint16_t pc; uint8_t lo, hi, last, root, held; } fol_src_t;
static fol_src_t fol_src[NTRK] __attribute__((section(".pool")));
/* per follower, per note played: the note it sounds + 1 (0: none kept, as played) */
static uint8_t fol_map[NTRK][128] __attribute__((section(".pool")));

/* the root of the pitch classes pc, lo its lowest note: the one with a fifth above it wins, then one with a third,
 * then a flat fifth; on a tie the first from the lowest note up */
static uint32_t fol_root(uint32_t pc, uint32_t lo)
{
    uint32_t i, best = lo % 12u, top = 0;
    for (i = 0; i < 12u; i++) {
        uint32_t r = (lo + i) % 12u, m, s;
        if (!((pc >> r) & 1u))
            continue;
        m = (pc >> r | pc << (12u - r)) & 0xFFFu;      /* the chord from r: bit i, i semitones above it */
        s = ((m >> 7) & 1u) * 4u + (((m >> 3) | (m >> 4)) & 1u) * 2u + ((m >> 6) & 1u);
        if (s > top) {
            top = s;
            best = r;
        }
    }
    return best;
}

/* note into track k's chord: a new chord (fresh), or one more note of the one it plays */
static void fol_add(uint32_t k, uint32_t note, int fresh)
{
    fol_src_t *s = &fol_src[k];
    if (fresh || !s->pc) {
        s->pc = 0;
        s->lo = 127;
        s->hi = 0;
    }
    s->pc |= (uint16_t)(1u << (note % 12u));
    s->lo = (uint8_t)(note < s->lo ? note : s->lo);
    s->hi = (uint8_t)(note > s->hi ? note : s->hi);
    s->last = (uint8_t)note;
    s->root = (uint8_t)fol_root(s->pc, s->lo);
}

/* a step of track t starts its n notes (as they sound; not the lane hits): its chord now */
static __attribute__((noinline)) void fol_step(const track_t *t, const uint8_t *nn, uint32_t n)
{
    uint32_t i;
    if (chord_kit(t))
        return;
    for (i = 0; i < n; i++)
        fol_add(trk_index(t), nn[i], !i);
}

/* a key's or MIDI note's note of track t goes down (on) or up: the chord it holds */
static __attribute__((noinline)) void fol_key(const track_t *t, uint32_t note, int on)
{
    fol_src_t *s = &fol_src[trk_index(t)];
    if (!on) {
        s->held = (uint8_t)(s->held - (s->held != 0u));
        return;
    }
    if (chord_kit(t))
        return;
    fol_add(trk_index(t), note, !s->held);
    s->held = (uint8_t)(s->held + (s->held < 255u));
}

/* the notes of a scale (mask: bit j, j semitones above its root) at or below i semitones, and in all */
static void fol_count(uint32_t mask, int32_t i, int32_t *below, int32_t *all)
{
    uint32_t j;
    *below = *all = 0;
    for (j = 0; j < 12u; j++)
        if ((mask >> j) & 1u) {
            ++*all;
            *below += (int32_t)j <= i;
        }
}

/* how far track t moves for the chord s (its NOTE d semitones above t's ROOT): KEY semitones, SCALE scale steps (to
 * the scale note at or below it), the nearest way */
static int32_t fol_shift(const track_t *t, const fol_src_t *s)
{
    int32_t d = t->p[P_FNOTE] == FN_LOW ? s->lo : t->p[P_FNOTE] == FN_HIGH ? s->hi : t->p[P_FNOTE] == FN_LAST ? s->last : s->root;
    int32_t below, all;
    d = (d - t->p[P_ROOT] + 120) % 12;
    if (t->p[P_FMODE] != FM_SCALE)
        return d > 6 ? d - 12 : d;
    fol_count(scale_mask(t), d, &below, &all);
    return below - 1 > all / 2 ? below - 1 - all : below - 1;
}

/* note x moved along track t's scale (ROOT, SCALE) by steps; a note off the scale keeps its semitones above the scale
 * note below it */
static int32_t fol_walk(const track_t *t, int32_t x, int32_t steps)
{
    uint32_t mask = scale_mask(t);
    int32_t rel = x - 60 - t->p[P_ROOT], wo = rel >= 0 ? rel / 12 : -((11 - rel) / 12), pcr = rel - 12 * wo;
    int32_t off = 0, below, all;
    while (pcr > 0 && !((mask >> pcr) & 1u)) {          /* (the root is on every scale) */
        pcr--;
        off++;
    }
    fol_count(mask, pcr, &below, &all);
    return kb_degree(t, wo * all + below - 1 + steps) + off;
}

/* the note track t sounds for note: moved into the chord of the track it follows (FOLLOW, MODE, NOTE); as played
 * with FOLLOW OFF or itself, no chord there yet, or on a kit */
static uint32_t fol_note(const track_t *t, uint32_t note)
{
    uint32_t l = (uint32_t)t->p[P_FOLLOW] - 1u, pc, i;
    int32_t x = (int32_t)note;
    const fol_src_t *s;
    if (l >= NTRK || l == trk_index(t) || !(pc = fol_src[l].pc) || chord_kit(t))
        return note;
    s = &fol_src[l];
    if (t->p[P_FMODE] == FM_FIT) {
        for (i = 0; i <= 6u; i++) {                     /* the nearest of its notes; a tie: the lower */
            if ((pc >> ((note + 120u - i) % 12u)) & 1u)
                return (uint32_t)clamp(x - (int32_t)i, 0, 127);
            if ((pc >> ((note + i) % 12u)) & 1u)
                return (uint32_t)clamp(x + (int32_t)i, 0, 127);
        }
        return note;                                    /* (never: a note of it is 6 semitones away at most) */
    }
    x = t->p[P_FMODE] == FM_SCALE ? fol_walk(t, x, fol_shift(t, s)) : x + fol_shift(t, s);
    return (uint32_t)clamp(x, 0, 127);
}

/* (voice.c trk_note_on) a note track t plays starts: the note it sounds, kept for its end while it follows. Struck
 * again while what it sounded is elsewhere now: that ends first (unless another note of the track sounds it) */
static __attribute__((noinline)) uint32_t fol_on(track_t *t, uint32_t note)
{
    uint8_t *m = fol_map[trk_index(t)];
    uint32_t x = fol_note(t, note & 127u);
    if (m[note & 127u] && m[note & 127u] != x + 1u)
        trk_note_off(t, note);
    if (t->p[P_FOLLOW] || m[note & 127u])
        m[note & 127u] = (uint8_t)(x + 1u);
    return x;
}

/* (voice.c trk_note_off) a note track t plays ends: the note it sounded (the note itself when none is kept); 255
 * while another note of the track sounds it too (FIT: two onto one), which ends it */
static __attribute__((noinline)) uint32_t fol_off(track_t *t, uint32_t note)
{
    uint8_t *m = fol_map[trk_index(t)];
    uint32_t o = m[note & 127u], i;
    if (!o)
        return note;
    m[note & 127u] = 0;
    for (i = 0; i < 128u; i++)
        if (m[i] == o)
            return 255u;
    return o - 1u;
}

/* (voice.c trk_all_off) track t lets go of everything: nothing kept, and no chord to lead with */
static __attribute__((noinline)) void fol_drop(const track_t *t)
{
    memset(fol_map[trk_index(t)], 0, sizeof fol_map[0]);
    memset(&fol_src[trk_index(t)], 0, sizeof fol_src[0]);
}

/* the order the tracks tick in, 4 bits each from bit 0: the ones another track follows first (on one step their
 * chord comes before the note that follows it), then the others, each in its own order */
static __attribute__((noinline)) uint32_t fol_order(void)
{
    uint32_t i, l, led = 0, ord = 0, at = 0, pass;
    for (i = 0; i < NTRK; i++)
        if ((l = (uint32_t)trk[i].p[P_FOLLOW] - 1u) < NTRK && l != i)
            led |= 1u << l;
    for (pass = 0; pass < 2u; pass++)
        for (i = 0; i < NTRK; i++)
            if (((led >> i) & 1u) != pass) {
                ord |= i << at;
                at += 4u;
            }
    return ord;
}
