/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff phase 5) web/emu/felucca_web.c built natively (cc, as native_check.c): a song through the device's own loop,
 * then a power cycle on the same flash.
 *   a: a list for every track ((Skiff phase 6) six: LEN 16 12 8 16 12 16, REPS 1..3, empty entries between), SONG
 *      on, PLAY: each track moves on at its own pass end once its entry's REPS are played, in its list's order, round
 *      again ((1.2.1, #12) an empty entry between a track's patterns a rest: one silent pass of its LEN); T4's one entry never swaps; an edit made in T1's B plays again the next time round. STOP, then the autosave's quiet stop writes the patterns
 *      and the project. The flash to FILE.
 *   b: a fresh device on FILE: the lists, each track on its entry with that entry's steps, the edit in T1's B.
 *   cc -O2 -ffp-contract=off -w -Ibuild/gen -Ifirmware/src -o build/emu/song_check web/emu/song_check.c -lm
 *   build/emu/song_check a FILE && build/emu/song_check b FILE */
#include "felucca_web.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("song_check: %-90s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static void render(uint32_t ms)
{
    uint32_t n = ms * 441u / 10u, k;
    for (k = 0; k < n; k += 128u)
        web_render(n - k < 128u ? n - k : 128u);
}
static void tap(uint32_t b)
{
    web_buttons(1u << b);
    render(60);
    web_buttons(0);
    render(60);
}

static const uint8_t LEN[6] = {16, 12, 8, 16, 12, 16};                 /* ((Skiff phase 6) T5, T6 too) */
static const uint8_t REPS[6][4] = {{2, 1, 0, 1}, {1, 3, 0, 0}, {3, 0, 2, 0}, {1, 0, 0, 0}, {1, 2, 0, 0}, {2, 0, 0, 1}};
                                                                       /* (0: empty) */
_Static_assert(NTRK <= 6, "a list for every track");
static uint8_t note_of(uint32_t k, uint32_t e) { return (uint8_t)(48u + 12u * (k % 3u) + 2u * e); }
static uint32_t reps_of(uint32_t k, uint32_t e) { return e < 4u ? REPS[k][e] : 0u; }
static uint32_t first_of(uint32_t k) { uint32_t e = 0; while (e < 3u && !REPS[k][e]) e++; return e; }
static uint32_t last_of(uint32_t k) { uint32_t e = 3; while (e && !REPS[k][e]) e--; return e; }
static int rest_of(uint32_t k, uint32_t e)        /* ((1.2.1, #12) an empty entry between the track's first and last) */
{
    return !reps_of(k, e) && e > first_of(k) && e < last_of(k);
}
static uint32_t passes_of(uint32_t k, uint32_t e) { return rest_of(k, e) ? 1u : reps_of(k, e); }   /* (a rest: REPS 1) */
static uint32_t next_of(uint32_t k, uint32_t e)   /* the next entry, a pattern or a rest; after the last, the first */
{
    return e >= first_of(k) && e < last_of(k) ? e + 1u : first_of(k);
}
static int steps_are(uint32_t k, uint32_t e)      /* track k plays entry e's steps (fill) */
{
    return trk[k].step[0].n == 1u && trk[k].step[0].note[0] == note_of(k, e) && trk[k].step[4].note[0] == note_of(k, e) &&
           trk[k].p[P_SLEN] == LEN[k];
}
static int steps_at(uint32_t k, uint32_t e)       /* .. or a rest's: none, the track's LEN */
{
    return rest_of(k, e) ? !trk[k].step[0].n && !trk[k].step[4].n && trk[k].p[P_SLEN] == LEN[k] : steps_are(k, e);
}
static void fill(uint32_t k, uint32_t e)        /* a note on every fourth step of the pass */
{
    uint32_t i;
    uint8_t n = note_of(k, e);
    for (i = 0; i < NSTEP; i++)
        step_clear(&trk[k].step[i]);
    for (i = 0; i < LEN[k]; i += 4u)
        put_step(&trk[k], i, 1, &n, ST_NOTE, 0);
}

static int run_a(FILE *f)
{
    uint32_t k, e, t, cur[NTRK], p0[NTRK], moves[NTRK] = {0}, wrong[NTRK] = {0}, edited = 0, again = -1u, made = 1;
    char b[96];
    web_nor_erase();
    web_boot();
    render(600);
    for (k = 0; k < NTRK; k++) {
        trk[k].p[P_SLEN] = LEN[k];
        for (e = 0; e < 4u; e++) {
            if (!REPS[k][e])
                continue;
            trk_pid[k] = 0;                        /* (off the pattern just made: these steps a new one) */
            fill(k, e);
            made &= bank_put(k, e, 0) == 0;
            lists.ent[k][e][1] = (uint8_t)(REPS[k][e] - 1u);
        }
        made &= ent_go(k, 0) == 0 && steps_are(k, 0);
    }
    lists.song = 1;
    check("the lists made, each track on its A with A's steps", made);
    tap(B_PLAY);
    check("PLAY", song.playing);
    for (k = 0; k < NTRK; k++)
        cur[k] = lists.cur[k], p0[k] = 0;
    for (t = 0; t < 24000u; t += 10u) {           /* 24 s: T1 three rounds, T2 four, T3 five */
        render(10);
        for (k = 0; k < NTRK; k++) {
            uint32_t c = lists.cur[k] % NENT;
            if (c == cur[k])
                continue;
            wrong[k] += c != next_of(k, cur[k]) || (uint16_t)(take_p0[k] - p0[k]) != passes_of(k, cur[k]) || !steps_at(k, c);
            moves[k]++;
            p0[k] = take_p0[k];
            cur[k] = c;
            if (k == 0u && c == 1u && !edited) {   /* T1 on B: a note put on its step 6 */
                uint8_t n = 72;
                put_step(&trk[0], 5, 1, &n, ST_NOTE, 0);
                edited = 1;
            } else if (k == 0u && c == 1u && again == -1u) {
                again = trk[0].step[5].n == 1u && trk[0].step[5].note[0] == 72u;
            }
        }
    }
    for (k = 0; k < NTRK; k++) {
        if (k == 3u)
            continue;
        snprintf(b, sizeof b, "T%u: %u moves, each to the next entry (a rest too) after its REPS passes", k + 1, moves[k]);
        check(b, moves[k] >= 6u && !wrong[k]);
    }
    check("T4: one entry, never swapped", moves[3] == 0u && steps_are(3, 0));
    check("the edit made in T1's B plays the next time round", again == 1u);
    tap(B_PLAY);
    check("STOP", !song.playing);
    render(16000);                                 /* the autosave's quiet stop: 2 s, then a round */
    check("the autosave's round: every pattern written, then the project", pend_free() == PEND_N && as.writes >= 1u &&
          as.saved == autosave_sig());
    for (k = 0; k < NTRK; k++)                     /* (where each stopped, for b) */
        fputc((int)lists.cur[k], f);
    fwrite(nor, 1, sizeof nor, f);
    return fails;
}

static int run_b(FILE *f)
{
    uint32_t k, e, ok = 1;
    uint8_t at[NTRK];
    if (fread(at, 1, NTRK, f) != NTRK || fread(nor, 1, sizeof nor, f) != sizeof nor)
        return 2;
    web_boot();
    render(600);
    check("a fresh device on that flash: SONG on", lists.song == 1u);
    for (k = 0; k < NTRK; k++)
        for (e = 0; e < NENT; e++)
            ok &= (lists.ent[k][e][0] != 0u) == (reps_of(k, e) != 0u) &&
                  (!reps_of(k, e) || lists.ent[k][e][1] + 1u == reps_of(k, e));
    check("its lists: the same entries, the same REPS", ok);
    for (k = 0, ok = 1; k < NTRK; k++)
        ok &= lists.cur[k] == at[k] && trk_pid[k] == lists.ent[k][at[k]][0] && steps_at(k, at[k]);
    check("each track on the entry it stopped on, with that entry's steps", ok);
    ok = ent_go(0, 1) == 0 && steps_are(0, 1) && trk[0].step[5].n == 1u && trk[0].step[5].note[0] == 72u;
    check("T1's B with the edit made while playing", ok);
    ok = ent_go(0, 0) == 0 && steps_are(0, 0) && trk[0].step[5].n == 0u;
    check("T1's A without it", ok);
    return fails;
}

int main(int argc, char **argv)
{
    FILE *f;
    int rc;
    if (argc < 3 || (argv[1][0] != 'a' && argv[1][0] != 'b') || !(f = fopen(argv[2], argv[1][0] == 'a' ? "wb" : "rb")))
        return 2;
    rc = argv[1][0] == 'a' ? run_a(f) : run_b(f);
    fclose(f);
    return rc != 0;
}
