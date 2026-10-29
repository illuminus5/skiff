/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff's pattern pool (firmware/src/bank.c) against simulated NOR flash, with storage.c, user presets and projects
 * as the firmware has them:
 *   a pattern kept exactly as played (every step field, NUDGE, LEN DIV SWING GATE DIR, automation and locks) and
 *   loaded back whole, over a power cycle; the pool's sectors inside the store, apart from every other object and from
 *   the package's BTIF; a write torn at any page leaves the pattern as it was; a damaged newest copy falls back to the
 *   copy before; writes go round the spare sectors; the sequence wraps; nothing is written while the transport runs.
 *   (phase 5) Each track's list names pool patterns; what a track has is kept as the pattern it is on (waiting, then
 *   written at the quiet stop), a new one gets the first free id and the entry it is on (or the first empty one);
 *   PATTERNS FULL; an erased entry; a pattern without room for its automation keeps its own. Live switching: an entry
 *   staged while playing comes in at its launch point (LOOP BAR BEAT NOW) whole, with its automation, what went out
 *   kept; cancelled, replaced, at once when stopped first, never over a project loaded; a switch only with room to
 *   keep what goes out. Scenes.
 * The SAVE > PATTERNS page: tests/ui_test.c test_bank. Built and run by tests/run_tests.sh. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0                                   /* -DBOARD_SMK37=1: the pool on the SMK-37 Pro layout */
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t fm1_enc_take(uint32_t e) { (void)e; return 0; }
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s) { (void)s; }
static void lcd_wake_now(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{ (void)x; (void)y; (void)w; (void)h; (void)p; }
#define FELUCCA_FLASH 1
#include "../firmware/src/gfx.c"
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/menu_items.c"
static void panel_setup(void) {}

static uint8_t nor[0x100000], flash_ok = 1;
static int fail_after = -1, erase_fail;
static uint32_t erases[256];                      /* per sector (offset >> 12) */
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off)
{
    erases[(off >> 12) & 255u]++;
    if (erase_fail)
        return -8;
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)   /* (fail_after: the power goes after that many) */
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static void fm1_wdt_feed(void) {}
static uint32_t fl_jedec_ram(void) { return 0; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"
#include "../firmware/src/project.c"
#include "../firmware/src/bank.c"

static int check(const char *what, int ok)
{
    printf("bank: %-72s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static void fresh(void)                           /* a new unit: flash erased, the tracks at power-on, the lists empty */
{
    memset(nor, 0xFF, sizeof nor);
    memset(erases, 0, sizeof erases);
    memset(&song, 0, sizeof song);
    sel_set(0);
    memset(trk, 0, sizeof trk);
    memset(&ui, 0, sizeof ui);
    memset(&motion, 0, sizeof motion);
    memset(proj_slot, 0, sizeof proj_slot);
    host_tracks_init();
    undo_clear();
    flash_ok = 1;
    fail_after = -1;
    erase_fail = 0;
    bank_boot();
    lists_clear();
    motion_lost = 0;
    memset(launch_q, 0, sizeof launch_q);
    memset((void *)mute_arm, 0, sizeof mute_arm);
    transport_req = 0;
}
static void power_cycle(void) { bank_boot(); }    /* (the RAM gone: the pool read from flash again) */
static uint32_t erases_total(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < 256u; i++)
        n += erases[i];
    return n;
}

/* track k: a pattern using every field a step has (seed: another one) and a full share of motion */
static void rich(uint32_t k, uint32_t seed)
{
    track_t *t = &trk[k];
    uint32_t i, j;
    motion_clear(t);
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint32_t r = (i * 7u + seed * 13u) % 11u;
        memset(s, 0, sizeof *s);
        s->time = (uint8_t)(r == 9u ? ST_TIE : r == 10u ? ST_REST : ST_NOTE);
        if (s->time == ST_NOTE) {
            s->n = (uint8_t)(1u + (i + seed) % 4u);
            for (j = 0; j < s->n; j++)
                s->note[j] = (uint8_t)(24u + (i * 5u + j * 7u + seed) % 100u);
            s->vel = (uint8_t)(1u + (i * 37u + seed) % 127u);
            s->flags = (uint8_t)((i % 3u == 0u ? SF_ACCENT : 0u) | (i % 5u == 0u ? SF_SLIDE : 0u));
            s->hit = (uint8_t)(i % 6u == 0u ? 0x81u : i % 6u == 1u ? 0x24u : 0u);
            s->acc = (uint8_t)(s->hit & 0x80u);
        }
        step_set_chance(s, (i * 11u + seed) % 101u);
        if (i % 7u == 3u)
            step_set_rflam(s, 5);                     /* FLAM */
        else
            step_set_ratchet(s, 1u + (i + seed) % 4u);
        s->cond = (uint8_t)((i + seed) % TC_COUNT);
        s->nudge = (int8_t)((int32_t)((i * 13u + seed) % 64u) - 32);
    }
    t->p[P_SLEN] = (int16_t)(37 + (int32_t)seed % 20);
    t->p[P_SDIV] = 3;
    t->p[P_SSWING] = 61;
    t->p[P_SGATE] = 90;
    t->p[P_DIR] = DIR_PING;
    for (i = 0; i < 16u; i++) {                   /* 16 automation events, 16 locks (inside each value's range) */
        const param_desc_t *d = param_desc_of(eng_idx(t->eng_req), P_E0 + i % 8u);
        int32_t span = d->max - d->min + 1;
        motion_set_event(t, (i * 4u + seed) % NSTEP, P_REV, (int16_t)(i * 7u + seed % 9u));
        motion_set_lock(t, (i * 4u + 1u + seed) % NSTEP, P_E0 + i % 8u,
                        (int16_t)(d->min + (int32_t)((i * 5u + seed) % (uint32_t)(span > 64 ? 64 : span))));
    }
}
typedef struct { step_t step[NSTEP]; int16_t p[5]; motion_store_t m; } snap_t;
static void snap(snap_t *s, uint32_t k)           /* what bank.c keeps of track k */
{
    memset(s, 0, sizeof *s);
    memcpy(s->step, trk[k].step, sizeof s->step);
    for (uint32_t i = 0; i < 5u; i++)
        s->p[i] = trk[k].p[BK_TIME[i]];
    motion_snapshot_track(&trk[k], &s->m);
}
static int same(const snap_t *a, const snap_t *b) { return !memcmp(a, b, sizeof *a); }
static void wipe(uint32_t k)                      /* the track emptied: steps, timing, motion */
{
    track_t *t = &trk[k];
    motion_clear(t);
    track_defaults_steps(t);
    t->p[P_SLEN] = 16;
    t->p[P_SDIV] = 2;
    t->p[P_SSWING] = 0;
    t->p[P_SGATE] = 64;
    t->p[P_DIR] = DIR_FWD;
}
static void detach(uint32_t k) { trk_pid[k] = 0; wipe(k); }   /* emptied, on no pattern: nothing of it kept */

static int layout(void)
{
    int bad = 0, ok = 1;
    uint32_t i, o, c;
    for (i = 0; i < BK_NSECT; i++) {
        uint32_t s = bk_sector(i);
#if BOARD_SMK37                                  /* (-DBOARD_SMK37=1: the SMK-37 Pro's USRFLASH, fm1_flash_map.h) */
        ok &= s >= FL_BANK_LO && s + 0x1000u <= FL_BANK_HI && FL_GUARD(s, 0x1000u) &&
              (s >= SMP_USER_BASE + SMP_USER_FLASH_SLOTS * SMP_USER_SIZE || s + 0x1000u <= SMP_USER_BASE);
#else
        ok &= (s >= 0xE7000u && s + 0x1000u <= 0xE9000u) || (s >= 0xEA000u && s + 0x1000u <= 0xFC000u);
#endif
        for (o = 0; o < OBJ_COUNT; o++)           /* apart from every storage.c object's sectors */
            for (c = 0; c < 2u; c++)
                ok &= st_sector(o, c) != s;
        ok &= s != 0xE9000u || BOARD_SMK37;
        for (o = 0; o < i; o++)
            ok &= bk_sector(o) != s;
    }
    bad += check(BOARD_SMK37 ? "20 sectors (SMK-37 Pro): USRFLASH after the user slot, nor any other object's" :
                 "20 sectors: the VM area's last two, the package's USR region; not BTIF, nor any other object's", ok);
    bad += check("a pattern 888 bytes (steps, NUDGE, timing, 64 motion records), 4 a sector, 16 chunks + 4 spare",
                 BK_REC == 888u && BK_LEN <= 3840u && BK_CHUNKS == 16u && BK_NSECT == 20u);
    return bad;
}

static int fidelity(void)
{
    int bad = 0, ok;
    uint32_t e0;
    snap_t a, b, c;
    static uint8_t r[BK_REC], r0[BK_REC];
    static step_t st[NSTEP];
    static motion_store_t ms;
    int16_t v[5];
    fresh();
    rich(1, 3);
    snap(&a, 1);
    ok = bank_put(1, 5, 0) == 0 && bank_used(1, 5) && pool_len(ent_pat(1, 5)) == (uint32_t)trk[1].p[P_SLEN] && !bank_used(1, 4) &&
         !bank_used(0, 5) && a.m.count >= 24u && bank_cur(1) == 5u;
    detach(1);
    ok &= ent_go(1, 5) == 0;
    snap(&b, 1);
    bad += check("track 2's pattern into F and back: every step field, NUDGE, LEN DIV SWING GATE DIR, 16 events, "
                 "16 locks", ok && same(&a, &b));
    rich(2, 7);
    snap(&c, 2);
    ok = bank_put(2, 15, 0) == 0;
    power_cycle();
    detach(1);
    detach(2);
    ok &= bank_used(1, 5) && bank_used(2, 15) && ent_go(2, 15) == 0 && ent_go(1, 5) == 0;
    snap(&b, 1);
    ok &= same(&a, &b);
    snap(&b, 2);
    bad += check("  over a power cycle, another track's P beside it", ok && same(&c, &b));
    ok = ent_pat(1, 5) == 0u && ent_pat(2, 15) == 1u && ent_pat(1, 4) == PAT_NONE && pool_used(0) && pool_used(1) &&
         !pool_used(2) && trk_pat(1) == 0u && trk_pat(2) == 1u && !undo_n;
    bad += check("  (phase 5) the entries name pool ids, the first free ones; each track on its entry's; no undo copy",
                 ok);
    detach(0);
    lists.ent[0][0][0] = 1u;                      /* T1's A names T2's F too: one pattern, two entries */
    ok = ent_go(0, 0) == 2;                       /* T2 and T3 hold 64 records, all there is: AUTOMATION FULL */
    snap(&b, 0);
    bad += check("  a pattern two entries name, onto T1; the motion pool full: the steps load, the automation not",
                 ok && !memcmp(a.step, b.step, sizeof a.step) && !motion_count(&trk[0]) && (motion_lost & 1u));
    e0 = erases_total();
    ok = !pat_read(0, r0) && keep_all() == 0 && erases_total() == e0;   /* (what was loaded, kept: the same, nothing) */
    motion_set_event(&trk[0], 2, P_REV, 9);       /* (refused: the pattern's own did not fit) */
    trk[0].step[1].vel ^= 1u;                     /* an edit: kept, the pattern's own automation with it */
    ok &= !motion_count(&trk[0]) && keep_all() == 0 && !pat_decode(&trk[3], 0, st, v, &ms) &&
          st[1].vel == (a.step[1].vel ^ 1u) && ms.count == a.m.count;
    trk[0].step[1].vel ^= 1u;
    ok &= keep_all() == 0 && !pat_read(0, r) && !memcmp(r, r0, BK_REC);
    bad += check("  .. kept: the same bytes (nothing written); an edit kept, its stored automation with it; none taken",
                 ok);
    detach(2);                                    /* (T3's 32 records gone: room) */
    ok = ent_go(0, 0) == 0;
    snap(&b, 0);
    bad += check("  room again: on T1 whole, the automation placed on T1", ok && !memcmp(a.step, b.step, sizeof a.step) &&
                 !memcmp(a.p, b.p, sizeof a.p) && b.m.count == a.m.count && (b.m.event[0].place >> 6) == 0u &&
                 !(motion_lost & 1u));
    ok = ent_go(3, 0) == 1 && bank_put(1, 5, 1) == 0 && !bank_used(1, 5) && pool_used(0) && bank_used(0, 0) &&
         !trk_pid[1] && seq_is_empty(&trk[1]) && !motion_count(&trk[1]);   /* (T2 was on F: emptied) */
    ok &= bank_put(0, 0, 1) == 0 && !pool_used(0) && !bank_used(0, 0) && seq_is_empty(&trk[0]);
    power_cycle();
    bad += check("  an empty entry loads nothing; ERASE: the entry emptied, its track too; the pattern kept while another "
                 "entry names it, erased with the last", ok && !pool_used(0) && bank_used(2, 15));
    return bad;
}

/* (phase 5) keeping edits: records wait (PEND_N) for a quiet stop, reads see them first, flushes write them */
static int keep(void)
{
    int bad = 0, ok;
    static uint8_t r1[BK_REC], r2[BK_REC], r3[BK_REC], got[BK_REC], zero[BK_REC];
    uint32_t e0, e;
    fresh();
    rich(1, 3);
    ok = bk_pack(r1, &trk[1]);
    rich(2, 7);
    ok &= bk_pack(r2, &trk[2]);
    e0 = erases_total();
    ok &= pat_keep_rec(5, r1) == 0 && pool_used(5) && !bk.len[5] && pend_free() == PEND_N - 1u &&
          !pat_read(5, got) && !memcmp(got, r1, BK_REC) && erases_total() == e0;
    bad += check("keep: a record waits (not written), reads see it, the pool counts it", ok);
    ok = pat_keep_rec(5, r2) == 0 && pend_free() == PEND_N - 1u && !pat_read(5, got) && !memcmp(got, r2, BK_REC);
    bad += check("  the same id again: the waiting copy replaced, no second slot", ok);
    for (ok = 1, e = 6; e < 5u + PEND_N; e++)            /* (ids 5 .. 4 + PEND_N waiting) */
        ok &= pat_keep_rec(e, r1) == 0;
    ok &= !pend_free() && pat_keep_rec(5u + PEND_N, r1) == 1 && !pool_used(5u + PEND_N);
    bad += check("  PEND_N waiting (one a track): one more has no room (1), nothing taken", ok);
    ok = pat_flush_one() == 0 && pend_free() == 3u && bk.len[5] && bk.len[7] && !bk.len[8];   /* (5 6 7: one chunk) */
    ok &= pat_flush_one() == 0 && pend_free() == PEND_N && pat_flush_one() == 0 && erases_total() == e0 + 2u && bk.len[8];
    power_cycle();
    ok &= pool_used(5) && !pat_read(5, got) && !memcmp(got, r2, BK_REC) && pool_used(6) && pend_free() == PEND_N;
    bad += check("  flushed a chunk a call (its waiting patterns in one write), then kept over a power cycle", ok);
    e0 = erases_total();
    ok = pat_keep_rec(5, r2) == 0 && pend_free() == PEND_N && erases_total() == e0;
    bad += check("  the same as stored: nothing waits, nothing written", ok);
    ok = pat_keep_rec(6, zero) == 0 && !pool_used(6) && pat_flush_one() == 0 && !bk.len[6] && !pool_used(6);
    bad += check("  an empty record empties the pattern (at the flush)", ok);
    ok = pool_alloc() == 0u && pat_keep_rec(0, r1) == 0 && pool_alloc() == 1u;
    bad += check("  pool_alloc: the first empty id, a waiting one counts", ok);
    memcpy(r3, r2, BK_REC);
    r3[BK_STEPS] ^= 1u;
    ok = pool_find(r2) == 5u && pool_find(r1) == 0u && pool_find(r3) == NPAT;
    bad += check("  pool_find: the id with the same record (waiting or stored), else none", ok);
    song.playing = 1;
    ok = pat_flush_one() == 2 && pend_free() == PEND_N - 1u;
    song.playing = 0;
    ok &= pat_flush_one() == 0 && pend_free() == PEND_N;
    bad += check("  playing: no flush (it waits); stopped: written", ok);
    return bad;
}

/* ---- (phase 5) the lists: a track's steps kept as the pattern it is on; new ones a new pattern ---- */
static void simple(uint32_t k, uint32_t note, uint32_t len, uint32_t dir)   /* new steps (on no pattern): step i
                                                                             * plays note + i */
{
    track_t *t = &trk[k];
    uint32_t i;
    trk_pid[k] = 0;
    wipe(k);
    for (i = 0; i < len; i++)
        t->step[i] = (step_t){{(uint8_t)(note + i)}, 1, ST_NOTE, 0, 100};
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_DIR] = (int16_t)dir;
}
static int entries(void)
{
    int bad = 0, ok;
    uint32_t i, a, b;
    track_t *t = &trk[0];
    static uint8_t r[BK_REC], rb[BK_REC];
    static step_t st[NSTEP];
    static motion_store_t ms;
    int16_t v[5];
    fresh();
    simple(0, 30, 4, DIR_FWD);                    /* new steps on T1, its list empty */
    ok = !trk_pid[0] && keep_poll() == 1 && trk_pid[0] && ent_pat(0, 0) == trk_pat(0) && !lists.cur[0] &&
         pend_free() == PEND_N && pool_used(trk_pat(0)) && keep_poll() == 0;
    a = trk_pat(0);
    bad += check("entries: new steps kept at the quiet stop (one write a poll): the first free id, named by the entry "
                 "the track is on (A)", ok);
    simple(0, 50, 4, DIR_FWD);                    /* more new steps, A naming a */
    ok = keep_all() == 0 && trk_pat(0) != a && trk_pat(0) != PAT_NONE && ent_pat(0, 1) == trk_pat(0) &&
         lists.cur[0] == 1u && ent_pat(0, 0) == a;
    b = trk_pat(0);
    bad += check("  .. the entry it is on naming another: the first empty one (B), the track on it; A as it was", ok);
    lists.ent[0][1][0] = (uint8_t)(a + 1u);       /* B made to name a (the SONG page's PATTERN), the track still on b */
    t->step[0].note[0] = 52;                      /* an edit */
    ok = keep_all() == 0 && !pat_decode(t, b, st, v, &ms) && st[0].note[0] == 52 && !pat_decode(t, a, st, v, &ms) &&
         st[0].note[0] == 30;
    lists.ent[0][1][0] = (uint8_t)(b + 1u);
    bad += check("  an edit kept as the pattern the track is on (b), whatever its entry names now", ok);
    t->step[1].note[0] = 53;                      /* an edit, then the track to A */
    ok = ent_go(0, 0) == 0 && t->step[0].note[0] == 30 && pend_at(b) < PEND_N;
    ok &= ent_go(0, 1) == 0 && t->step[0].note[0] == 52 && t->step[1].note[0] == 53 && !undo_n;
    bad += check("  leaving a pattern keeps it first (waiting for the quiet stop); back on it: the edit there", ok);
    ok = keep_all() == 0 && pend_free() == PEND_N;
    t->step[2].note[0] = 54;                      /* an edit not kept yet when the power goes */
    power_cycle();
    detach(0);
    ok &= ent_go(0, 1) == 0 && t->step[1].note[0] == 53 && t->step[2].note[0] == 52;
    bad += check("  over a power cycle: what was kept (written stopped), not the edit after it (gone, as before)", ok);

    ok = bk_pack(r, &trk[3]);                     /* (an empty track's pattern) */
    for (i = 0; i < NPAT; i++)                    /* the pool full: every id holds a pattern */
        if (!pool_used(i))
            ok &= pool_put(i, r) == 0;
    simple(0, 70, 4, DIR_FWD);                    /* new steps: no id for them */
    ok &= pool_alloc() == NPAT && keep_all() == 1 && !trk_pid[0] && ent_go(0, 0) == 4 && t->step[0].note[0] == 70 &&
          bank_put(0, 5, 0) == 4 && !bank_used(0, 5) && project_save_as(1, 0) == 1 && str_eq(ui.msg, "PATTERNS FULL");
    song.playing = 1;
    ok &= bank_switch(0, 0) == 4 && bank_next(0) == BK_NONE;
    song.playing = 0;
    ok &= bank_put(0, 1, 0) == 0 && trk_pat(0) == b && !pat_decode(t, b, st, v, &ms) && st[0].note[0] == 70;
    bad += check("  the pool full: new steps get no id (PATTERNS FULL: no switch, no SAVE into an empty entry, no project "
                 "save; the track stays on them); SAVE over a named entry goes", ok);
    ok = bank_put(0, 0, 1) == 0 && !pool_used(a) && pool_alloc() == a;
    bad += check("  an entry erased (nothing else naming its pattern): its id free again", ok);

    ok = bk_pack(rb, t);
    for (i = 0; i < PEND_N - 1u; i++)             /* three waiting (playing: they wait) */
        ok &= pat_keep_rec(10u + i, rb) == 0;
    lists.ent[0][0][0] = (uint8_t)(b + 1u);       /* T1's A and T2's A: b */
    lists.ent[1][0][0] = (uint8_t)(b + 1u);
    song.playing = 1;
    ok &= pend_free() == 1u && bank_switch(0, 0) == 0 && bank_switch(1, 0) == 0 && bank_next(0) == 0u &&
          bank_next(1) == 0u;
    bank_cancel(0);
    bank_cancel(1);
    song.playing = 0;
    bad += check("  playing: switches staged with fewer waiting slots free than tracks staged (#29: none held for them)",
                 ok && keep_all() == 0 && pend_free() == PEND_N);
    return bad;
}

/* (phase 6) every track staged at once; a scene that cannot stage a track (what went out at its last swap still waiting
 * to be kept: bank_poll_trk) says PATTERNS FULL, its other tracks staged all the same, whatever waiting slots are free */
static int staged_all(void)
{
    int bad = 0, ok;
    uint32_t k, b;
    static uint8_t r[BK_REC];
    fresh();
    simple(0, 60, 4, DIR_FWD);
    ok = keep_all() == 0 && trk_pid[0] != 0u;     /* T1's steps: a pattern, its entry A */
    b = trk_pid[0] - 1u;
    for (k = 0; k < NTRK; k++)                    /* every track's B: that pattern */
        lists.ent[k][1][0] = (uint8_t)(b + 1u);
    song.playing = 1;
    for (k = 0; k < NTRK; k++)
        ok &= bank_switch(k, 1) == 0 && bank_next(k) == 1u;
    bad += check("every track staged at once", ok && pend_free() == PEND_N);
    for (k = 0; k < NTRK; k++)
        bank_cancel(k);
    lists.scene[0] = 2u;                          /* scene 1: T1 to its B, T2 to its B, the rest left alone */
    lists.scene[0] |= 2u << 5;
    lists.scene[48] = 1u;
    ok = bk_pack(r, &trk[3]);
    for (k = 0; k < PEND_N - 1u; k++)             /* one slot free */
        ok &= pat_keep_rec(20u + k, r) == 0;
    launch_q[1].done = 2;                         /* T2: what went out still waiting to be kept */
    scene_key(0, 0);
    ok &= bank_next(0) == 1u && bank_next(1) == BK_NONE && str_eq(ui.msg2, "PATTERNS FULL");
    bank_cancel(0);
    launch_q[1].done = 0;
    scene_key(0, 0);
    ok &= bank_next(0) == 1u && bank_next(1) == 1u;
    bank_cancel(0);
    bank_cancel(1);
    song.playing = 0;
    bad += check("  a scene: a track still keeping what went out PATTERNS FULL, the others staged; one slot free, both",
                 ok && keep_all() == 0);
    return bad;
}

/* (phase 6) the last track's automation (with six tracks its place 5 << 6 | 63 = 0x17F: past a byte) kept in a pattern
 * and loaded back onto it; a record holding a value no firmware wrote (200: a byte in RAM would wrap it into range):
 * its automation dropped, the steps load */
static int last_track(void)
{
    int bad = 0, ok;
    uint32_t k = NTRK - 1u, i, id, n = 0;
    static uint8_t r[BK_REC];
    static step_t st[NSTEP];
    static motion_store_t ms;
    int16_t v[5];
    fresh();
    simple(k, 40, 64, DIR_FWD);
    ok = motion_put(&trk[k], 63, P_PAN, -64, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] != 0u;
    id = trk_pid[k] - 1u;
    detach(k);
    ok &= ent_go(k, 0) == 0;
    for (i = 0; i < motion.count; i++)
        n += motion.event[i].place == (k << 6 | 63u) && (motion.event[i].id == P_PAN && motion.event[i].lock) &&
             motion.event[i].value == -64;
    bad += check("the last track's lock on step 64 (its place past a byte with six) kept and loaded back",
                 ok && n == 1u && motion_count(&trk[k]) == 1u);
    ok = !pat_read(id, r) && (r[7] & 127u) == 1u;
    r[BK_MOT + 2u] = 200u;                            /* (its value: 200, 0) */
    r[BK_MOT + 3u] = 0u;
    ok &= pat_keep_rec(id, r) == 0 && !pat_decode(&trk[k], id, st, v, &ms) && !ms.count && st[5].note[0] == 45u;
    bad += check("  a record holding a value outside -64..127: its automation dropped, the steps load", ok);
    /* (Skiff phase 9) the motion's ids as a store of PROJ_NP has them (firmware before reads the record), unless the
     * motion names a parameter added since: then today's */
    fresh();
    simple(k, 40, 64, DIR_FWD);
    ok = motion_put(&trk[k], 3, P_E5, 20, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] != 0u;
    id = trk_pid[k] - 1u;
    ok &= !pat_read(id, r) && r[1] == PROJ_NP && r[BK_MOT + 1u] == ((P_E5 - PROJ_ESHIFT) | MOTION_LOCK) &&
          !pat_decode(&trk[k], id, st, v, &ms) && ms.count == 1u && (ms.event[0].id == P_E5 && ms.event[0].lock);
    ok &= r[0] == BK_VER;
    bad += check("  (phase 9) a lock on E5: the record of 103 parameters (E5 at 100), back at today's P_E5", ok);
    ok = motion_put(&trk[k], 4, P_DTONE, -9, MOTION_LOCK) == 0 && keep_all() == 0 && trk_pid[k] == id + 1u;
    ok &= !pat_read(id, r) && r[1] == P_COUNT && !pat_decode(&trk[k], id, st, v, &ms) && ms.count == 2u;
    for (i = n = 0; i < ms.count; i++)
        n += (ms.event[i].id == P_E5 && ms.event[i].lock) || (ms.event[i].id == P_DTONE && ms.event[i].lock);
    bad += check("  .. and one on DRIVE's TONE (phase 9's): the record of today's ids, both back", ok && n == 2u);
    ok = !pat_read(id, r) && P_E5 > 127u && r[0] == BK_VER2 && (r[BK_MOT] & 64u) &&
         r[BK_MOT + 1u] == ((P_E5 & 127u) | MOTION_LOCK) && !(r[BK_MOT + 4u] & 64u);
    bad += check("  (1.3) E5 is past 127 there: version 2, its id's bit 7 in its step byte's bit 6", ok);
    return bad;
}

static int torn(void)
{
    int bad = 0, ok = 1, n;
    snap_t a, b, c;
    fresh();
    rich(0, 1);
    snap(&a, 0);
    bank_put(0, 2, 0);                            /* the copy before: C, pattern 01 (ids 0..3's chunk) */
    rich(0, 9);
    snap(&c, 0);
    for (n = 0; n <= (int)(BK_LEN / 256u) + 1; n++) {   /* the power goes after n writes: the payload's pages, the header */
        rich(0, 9);
        fail_after = n;
        ok &= bank_put(0, 2, 0) != 0;
        fail_after = -1;
        power_cycle();
        detach(0);
        ok &= ent_go(0, 2) == 0;
        snap(&b, 0);
        ok &= same(&a, &b);
    }
    bad += check("a save torn after any write (each page, before the header): C as it was", ok);
    fail_after = -1;
    rich(0, 9);
    ok = bank_put(0, 2, 0) == 0;
    power_cycle();
    detach(0);
    ok &= ent_go(0, 2) == 0;
    snap(&b, 0);
    bad += check("  then a whole one: the new C", ok && same(&c, &b));
    nor[bk_sector(bk.sect[0] - 1u) + 256u + 1000u] ^= 0x10u;   /* the newest copy damaged */
    power_cycle();
    detach(0);
    ok = ent_go(0, 2) == 0;
    snap(&b, 0);
    bad += check("  its newest copy damaged: the copy before (the first C)", ok && same(&a, &b));
    rich(0, 5);                                   /* (changed: else there is nothing to write) */
    erase_fail = 1;
    ok = bank_put(0, 2, 0) != 0;
    erase_fail = 0;
    bad += check("  an erase that fails: not saved, said so", ok && bank_used(0, 2));
    return bad;
}

static int wear_and_wrap(void)
{
    int bad = 0, ok = 1;
    uint32_t i, most = 0;
    snap_t a, b;
    static uint8_t r[BK_REC];
    fresh();
    for (i = 0; i < BK_CHUNKS; i++) {              /* a full bank: every chunk has a copy, 4 sectors spare */
        rich(0, i);
        ok &= bk_pack(r, &trk[0]) && pool_put(i * BK_PER, r) == 0;
    }
    detach(0);
    memset(erases, 0, sizeof erases);
    for (i = 0; i < 40u; i++) {
        rich(3, i);
        ok &= bank_put(3, 9, 0) == 0;
    }
    snap(&a, 3);
    for (i = 0; i < BK_NSECT; i++)
        most = erases[bk_sector(i) >> 12] > most ? erases[bk_sector(i) >> 12] : most;
    power_cycle();
    detach(3);
    ok &= ent_go(3, 9) == 0;
    snap(&b, 3);
    bad += check("a full bank, 40 saves of one entry: round the spare sectors (each erased about 10 times), the last "
                 "kept", ok && same(&a, &b) && most <= 11u && pool_used(0) && pool_used(44) && ent_pat(3, 9) == 1u);
    fresh();
    bk.top = 0xFFFFFFF0u;                         /* the sequence about to wrap */
    for (i = 0; i < 30u; i++) {
        rich(0, i);
        ok &= bank_put(0, 0, 0) == 0;
    }
    snap(&a, 0);
    power_cycle();
    detach(0);
    ok &= ent_go(0, 0) == 0 && bk.top < 0x100u;
    snap(&b, 0);
    bad += check("  the sequence wraps: the newest copy still wins", ok && same(&a, &b));
    return bad;
}

static int playing(void)
{
    int bad = 0;
    uint32_t e;
    fresh();
    rich(0, 4);
    bank_put(0, 1, 0);
    e = erases_total();
    song.playing = 1;
    bad += check("playing: SAVE and ERASE write nothing (an erase mutes)", bank_put(0, 1, 1) == 2 && bank_put(0, 3, 0) == 2 &&
                 e == erases_total() && bank_used(0, 1) && !bank_used(0, 3));
    bad += check("  LOAD stages it (reads only)", bank_switch(0, 1) == 0 && bank_next(0) == 1u && e == erases_total());
    bank_cancel(0);
    song.playing = 0;
    flash_ok = 0;
    bank_boot();
    bad += check("no flash: an empty bank, SAVE says so", !bank_used(0, 1) && bank_put(0, 1, 0) == 3);
    flash_ok = 1;
    return bad;
}

/* ---- live switching (launch.c; bank.c bank_switch, bank_poll): track 0 stepped one step a block ---- */
#define W_ENT 15u                                 /* (W's entry: P) */
static void lv_next(uint32_t n)                   /* track 0 on n steps: each block to the end of its step exactly */
{
    track_t *t = &trk[0];
    while (n--)
        events_block(step_samples(t, div_samples((uint32_t)t->p[P_SDIV]), t->seq_idx) - t->seq_pos);
}
static int sounds(uint32_t note) { return trk[0].seq_n && trk[0].seq_notes[0] == note; }
static void lv_start(uint32_t mode)               /* stopped; W on track 0 (12 steps, notes 40.., an automation event),
                                                   * saved as P, the track on it; the launch point; PLAY: W's first step
                                                   * sounds */
{
    transport_req = 2;
    events_block(1);
    bank_poll();
    simple(0, 40, 12, DIR_FWD);
    motion_set_event(&trk[0], 3, P_REV, 10);
    bank_put(0, W_ENT, 0);
    ui_launch = (uint8_t)mode;
    transport_req = 1;
    events_block(1);
}
static int live(void)
{
    int bad = 0, ok;
    uint32_t i, bpm, w;
    track_t *t = &trk[0];
    snap_t c, b;
    static project_t pj;
    fresh();
    simple(0, 60, 8, DIR_FWD);                    /* B: 8 steps, notes 60.. */
    bank_put(0, 1, 0);
    simple(0, 80, 8, DIR_REV);                    /* D: 8 steps, notes 80.., backwards */
    bank_put(0, 3, 0);
    simple(0, 80, 8, DIR_FWD);                    /* C: 8 steps, notes 80.., two automation events */
    motion_set_event(t, 0, P_REV, 40);
    motion_set_event(t, 5, P_REV, 90);
    bank_put(0, 2, 0);
    snap(&c, 0);
    motion_set_event(&trk[2], 1, P_REV, 70);      /* (another track's automation: stays as it is) */

    lv_start(LAUNCH_LOOP);
    w = ent_pat(0, W_ENT);
    motion_set_lock(t, 11, P_SGATE, 20);           /* (an edit since: W's last step its own GATE, sounding at the swap) */
    lv_next(5);
    ok = bank_switch(0, 2) == 0 && bank_next(0) == 2u && sounds(45);
    lv_next(6);                                   /* W's last step */
    ok &= sounds(51) && t->p[P_SLEN] == 12 && bank_next(0) == 2u;
    lv_next(1);
    snap(&b, 0);
    ok &= sounds(80) && t->seq_idx == 0 && same(&c, &b) && bank_next(0) == BK_NONE && launch_q[0].done &&
          motion_count(&trk[2]) == 1;
    bad += check("live LOOP: staged mid-pass, the pass plays out, C from its step 1 (steps, LEN, automation)", ok);
    bank_poll();
    ok = !launch_q[0].done && bank_cur(0) == 2u && trk_pat(0) == ent_pat(0, 2) && pend_at(w) < PEND_N && !undo_n;
    transport_req = 2;
    events_block(1);
    ok &= ent_go(0, W_ENT) == 0 && t->p[P_SLEN] == 12 && t->step[0].note[0] == 40 && motion_count(t) == 2 &&
          motion_count(&trk[2]) == 1 && t->p[P_SGATE] == 64;   /* (W's GATE as made (wipe), not its last step's lock) */
    ok &= ent_go(0, 2) == 0;
    snap(&b, 0);
    bad += check("  what went out kept as W's pattern (its edit since: GATE not a lock's), no undo copy; P: W, C: C",
                 ok && same(&c, &b));

    lv_start(LAUNCH_BAR);
    lv_next(5);
    ok = bank_switch(0, 2) == 0;
    lv_next(10);                                  /* the run's step 16: W's 4th, its second pass */
    ok &= sounds(43) && bank_next(0) == 2u;
    lv_next(1);
    ok &= sounds(80) && t->seq_idx == 0;
    bad += check("live BAR: staged in bar 1, C on bar 2's first step, W (12 steps) mid-pass", ok);
    lv_start(LAUNCH_BEAT);
    lv_next(5);
    ok = bank_switch(0, 2) == 0;
    lv_next(2);
    ok &= sounds(47);
    lv_next(1);                                   /* the run's step 9: beat 3 */
    ok &= sounds(80);
    bad += check("live BEAT: on the next beat's step", ok);
    lv_start(LAUNCH_NOW);
    lv_next(5);
    ok = bank_switch(0, 2) == 0 && sounds(45);
    lv_next(1);
    ok &= sounds(86) && t->seq_idx == 6;
    lv_next(2);                                   /* (C's LEN 8: round to its step 1) */
    ok &= sounds(80);
    bad += check("live NOW: the next step, the playhead going on (C's 7th), C's LEN from there", ok);
    lv_start(LAUNCH_LOOP);
    ok = bank_switch(0, 3) == 0;
    lv_next(12);
    ok &= sounds(87) && t->seq_idx == 7;
    lv_next(1);
    ok &= sounds(86);
    bad += check("  a pattern playing backwards (D) starts from its last step", ok);

    for (bpm = 120; bpm < 240u; bpm++) {          /* a tempo where 1/32's steps round shorter than half 1/16's */
        song.g[G_BPM] = (int16_t)bpm;
        if (8u * div_samples(3) < 4u * div_samples(2))
            break;
    }
    lv_start(LAUNCH_BAR);
    t->p[P_SDIV] = 3;                             /* W at 1/32: ahead of the bar lines counted, 4 samples a beat */
    transport_req = 1;
    events_block(1);
    lv_next(40);
    ok = bpm < 240u && bank_switch(0, 2) == 0;
    lv_next(23);
    ok &= sounds(40 + 63 % 12) && bank_next(0) == 2u;
    lv_next(1);                                   /* 1/32 step 65: bar 3, 32 samples before its line */
    ok &= sounds(80);
    bad += check("  BAR: a 1/32 track (its steps rounded short: ahead of the bars counted) comes in on the bar", ok);
    song.g[G_BPM] = 120;

    lv_start(LAUNCH_LOOP);
    lv_next(2);
    ok = bank_switch(0, 1) == 0 && bank_switch(0, 2) == 0 && bank_next(0) == 2u;
    lv_next(10);
    ok &= sounds(80);
    bad += check("  staged again before it comes in: the last one comes in", ok);
    lv_start(LAUNCH_LOOP);
    lv_next(2);
    ok = bank_switch(0, 2) == 0 && bank_cancel(0) && bank_next(0) == BK_NONE && !bank_cancel(0);
    lv_next(12);
    ok &= sounds(42) && !launch_q[0].done;
    bad += check("  cancelled (OCT- in the PERFORM layer): W plays on", ok);
    lv_start(LAUNCH_LOOP);
    lv_next(2);
    ok = bank_switch(0, 2) == 0;
    transport_req = 2;
    events_block(1);
    bank_poll();
    ok &= !song.playing && t->step[0].note[0] == 80 && t->p[P_SLEN] == 8 && bank_next(0) == BK_NONE && bank_cur(0) == 2u;
    ok &= !undo_n && bank_switch(0, W_ENT) == 0 && t->step[0].note[0] == 40;
    bad += check("  stopped before its launch point: it comes in at once (no undo copy: W kept, P brings it back)", ok);

    lv_start(LAUNCH_LOOP);
    for (i = 0; motion.count < MOTION_MAX && i < NSTEP; i++)
        motion_set_event(&trk[1], i, P_REV, 10);
    ok = motion.count == MOTION_MAX && bank_switch(0, 2) == 2;
    lv_next(12);
    ok &= sounds(80) && motion_count(t) == 0 && motion.count == MOTION_MAX - 1u;
    bank_poll();
    motion_set_event(t, 2, P_REV, 33);            /* (refused: C's own did not come in) */
    ok &= (motion_lost & 1u) && !motion_count(t) && trk_rec(0) && (bk_rec[7] & 127u) == 2u;
    bad += check("  no room for its automation: the steps come in, not the automation (AUTOMATION FULL); C kept with "
                 "its own, the track takes none", ok);
    motion_clear(&trk[1]);

    lv_start(LAUNCH_LOOP);
    project_capture(&pj);
    ok = bank_switch(0, 2) == 0;
    project_restore_runtime(&pj);                 /* a project load (it stops) */
    bank_poll();
    ok &= !song.playing && t->step[0].note[0] == 40 && bank_next(0) == BK_NONE && bank_cur(0) == W_ENT;
    lv_start(LAUNCH_NOW);
    transport_req = 2;
    events_block(1);
    ok &= project_save_as(0, 0) == 0;             /* (project A: T1 on P) */
    transport_req = 1;
    events_block(1);
    t->step[1].note[0] = 77;                      /* an edit on W, playing */
    ok &= bank_switch(0, 2) == 0;
    lv_next(1);                                   /* NOW: C in, what went out (the edit with it) in launch_q, not kept */
    ok &= launch_q[0].done && sounds(81);
    project_load(0);                              /* .. a project load right then: it stops, keeps that first */
    ok &= !song.playing && !launch_q[0].done && !undo_n && bank_cur(0) == W_ENT && t->step[1].note[0] == 77;
    bad += check("  a project load: nothing staged comes in over it; right after a swap, what went out kept first", ok);

    /* staged mutes (mute_switch: the PERFORM layer's black keys) */
    lv_start(LAUNCH_LOOP);
    lv_next(5);
    mute_switch(0);
    ok = mute_arm[0] == 2u && !t->p[P_MUTE];
    lv_next(6);
    i = vage;
    ok &= sounds(51) && !t->p[P_MUTE];
    lv_next(1);                                   /* W's pass ends: muted, its step 1 starts no voice */
    ok &= t->p[P_MUTE] == 1 && !mute_arm[0] && vage == i;
    bad += check("live mute, LOOP: staged mid-pass, W plays out its pass, then silent from step 1", ok);
    lv_start(LAUNCH_BAR);
    t->p[P_MUTE] = 1;
    lv_next(5);
    mute_switch(0);
    ok = mute_arm[0] == 1u;
    lv_next(10);
    ok &= t->p[P_MUTE] == 1;
    i = vage;
    lv_next(1);                                   /* bar 2: back, its step sounds */
    ok &= !t->p[P_MUTE] && vage > i && sounds(44);
    bad += check("  unmute, BAR: on bar 2's first step, that step heard", ok);
    lv_start(LAUNCH_LOOP);
    lv_next(2);
    mute_switch(0);
    ok = bank_switch(0, 2) == 0 && mute_arm[0] == 2u;
    lv_next(9);
    i = vage;
    lv_next(1);                                   /* both on the same step: C in, muted */
    ok &= t->p[P_MUTE] == 1 && t->step[0].note[0] == 80 && vage == i;
    mute_switch(0);
    ok &= mute_arm[0] == 1u;
    transport_req = 2;
    events_block(1);
    bank_poll();                                  /* stopped before it: at once */
    ok &= !t->p[P_MUTE] && !mute_arm[0];
    bad += check("  with a pattern staged too: both on one step; stopped before its point: at once", ok);
    lv_start(LAUNCH_LOOP);
    mute_switch(0);
    mute_switch(1);
    ok = stage_cancel() && !mute_arm[0] && !mute_arm[1] && !stage_cancel();
    mute_switch(0);
    project_capture(&pj);
    project_restore_runtime(&pj);
    ok &= !mute_arm[0] && !t->p[P_MUTE];
    bad += check("  OCT- cancels them all; a song does not hold a mute back; a project load drops it", ok);
    return bad;
}

/* ---- scenes (bank.c scene_store / scene_recall; the PERFORM layer's KEYS SCENE) ---- */
static int scenes(void)
{
    int bad = 0, ok;
    uint32_t i;
    track_t *t = &trk[0];
    static project_t pj, got;
    static project_store_t w;
    fresh();
    memset(&favorites, 0, sizeof favorites);
    simple(0, 30, 12, DIR_FWD);                   /* T1: A (12 steps, notes 30..), C (8, 80..) */
    bank_put(0, 0, 0);
    simple(0, 80, 8, DIR_FWD);
    bank_put(0, 2, 0);
    simple(1, 60, 16, DIR_FWD);                   /* T2: B */
    bank_put(1, 1, 0);
    ent_go(0, 0);                                 /* T1 plays A, T2 B, T3 its own steps (no entry), T4 muted */
    simple(2, 50, 16, DIR_FWD);
    trk[3].p[P_MUTE] = 1;
    scene_store(2);
    ok = scene_used(2) && !scene_used(0) && scene_slot(2, 0) == 0u && scene_slot(2, 1) == 1u &&
         scene_slot(2, 2) == BK_NONE && scene_slot(2, 3) == BK_NONE && scene_bits(2) >> 20 == 8u && scene_now() == 2u;
    bad += check("scene 3 stored: T1 A, T2 B, T3 none (its own steps), T4 none; T4 muted", ok);
    ent_go(0, 2);                                 /* now: T1 C, T3 muted, T4 not */
    trk[2].p[P_MUTE] = 1;
    trk[3].p[P_MUTE] = 0;
    ok = scene_recall(2) == 0;
    ok &= t->step[0].note[0] == 30 && bank_cur(0) == 0u && bank_cur(1) == 1u && trk[2].step[0].note[0] == 50 &&
          !trk[2].p[P_MUTE] && trk[3].p[P_MUTE] == 1 && bank_next(0) == BK_NONE;
    bad += check("  recalled stopped: T1 A at once, T2 kept, T3's steps as they are, the MUTEs as stored", ok);
    ent_go(0, 2);
    trk[3].p[P_MUTE] = 0;
    lv_start(LAUNCH_BAR);                         /* (lv_start: W on T1, its P) */
    trk[3].p[P_MUTE] = 0;
    lv_next(5);
    ok = scene_recall(2) == 0 && bank_next(0) == 0u && bank_next(1) == BK_NONE && mute_arm[3] == 2u && stage_pending();
    lv_next(10);
    ok &= t->step[0].note[0] == 40 && !trk[3].p[P_MUTE];   /* (W still: A not yet) */
    lv_next(1);                                   /* bar 2: all of it on one step */
    bank_poll();
    ok &= t->step[0].note[0] == 30 && sounds(30) && bank_cur(0) == 0u && t->seq_idx == 0u && trk[3].p[P_MUTE] == 1 &&
          !stage_pending();
    bad += check("  recalled playing (BAR): staged, then all of it on bar 2's first step", ok);
    ok = scene_recall(5) == 1 && scene_recall(2) == 0 && bank_next(0) == BK_NONE;   /* (all as it says: nothing) */
    bad += check("  an empty scene: nothing; the scene it plays: nothing staged", ok);
    project_capture(&pj);                         /* (phase 5) the project's: in SKF2, back with a load */
    ok = proj_pack2(&w, &pj, PROJ_SKF) && proj_import(&got, &w, PROJ_SKF->size) && !memcmp(got.scene, lists.scene, PROJ_SCENES);
    memset(lists.scene, 0, sizeof lists.scene);   /* (another project's: none) */
    ok &= !scene_used(2) && !project_restore_runtime(&got) && scene_used(2) && scene_slot(2, 1) == 1u &&
          scene_bits(2) >> 20 == 8u && scene_now() == 0xFFu;
    bad += check("  kept with the project (SKF2): a load brings them (none lit yet)", ok);
    transport_req = 2;
    events_block(1);
    return bad;
}

/* ---- (phase 6) the last track in a scene (bank.c scene_trk: T5, T6 in 2 bytes a scene after the 50 of before); a
 * scene of four tracks (an SKF2's) leaves T5, T6 as they are; a pattern a saved SKF3 or SKF2 names is IN USE ---- */
static int scenes_six(void)
{
    int bad = 0, ok;
    uint32_t k = NTRK - 1u, id;
    static project_t pj;
    fresh();
    simple(k, 40, 8, DIR_FWD);                    /* the last track: A (40..), B (70..), on B, muted */
    bank_put(k, 0, 0);
    simple(k, 70, 8, DIR_FWD);
    bank_put(k, 1, 0);
    trk[k].p[P_MUTE] = 1;
    scene_store(6);
    ok = scene_slot(6, k) == 1u && scene_trk(6, k) == (2u | 1u << 5);
    ent_go(k, 0);
    trk[k].p[P_MUTE] = 0;
    ok &= scene_recall(6) == 0 && bank_cur(k) == 1u && trk[k].p[P_MUTE] && trk[k].step[0].note[0] == 70u;
    bad += check("(phase 6) the last track in a scene: its entry and MUTE stored and recalled", ok);
    lists.scene[50u + 2u * 6u] = lists.scene[51u + 2u * 6u] = 0;   /* (scene 7 as an SKF2 brings it) */
    ent_go(k, 0);
    trk[k].p[P_MUTE] = 0;
    ok = scene_trk(6, k) == SCENE_NOT && scene_recall(6) == 0 && bank_cur(k) == 0u && !trk[k].p[P_MUTE];
    bad += check("  a scene of four tracks (an SKF2's): T5, T6 left as they are", ok);
    id = ent_pat(k, 1);
    ok = project_save_as(0, 0) == 0;              /* (SKF3 with six tracks: the last track's list in it) */
    lists_clear();
    detach(k);
    ok &= pool_refs(id) == 1u;
    pool_clear(id);
    ok &= str_eq(ui.msg, "IN USE") && pool_used(id);
#if FELUCCA_LEGACY                                /* ((2.0) an SKF2: the converter's) */
    memset(proj_slot, 0, sizeof proj_slot);
    project_capture(&pj);
    pj.ent[0][0][0] = (uint8_t)(id + 1u);         /* (T1's entry A naming it, in an SKF2) */
    ok &= proj_pack2(&proj_slot[2], &pj, &PROJ_SKF2) && pool_refs(id) == 1u;
    pool_clear(id);
    ok &= str_eq(ui.msg, "IN USE") && pool_used(id);
#endif
    bad += check("  a pattern a saved project names (the last track's list in its format; T1's in an SKF2): IN USE", ok);
    return bad;
}

/* ---- (phase 5) the song (bank.c song_poll, song_top; launch.c launch_due's repeats, launch_ahead): every track
 * stepped one step a block, the main loop's frame before each ---- */
static void sg_frame(void) { bank_poll(); song_poll(); }
static void sg_steps(uint32_t n)
{
    while (n--) {
        sg_frame();
        lv_next(1);
    }
}
static int heard(uint32_t k, uint32_t note) { return trk[k].seq_n && trk[k].seq_notes[0] == note; }
static void sg_play(void) { transport_req = 1; events_block(1); }
static void sg_stop(void) { transport_req = 2; events_block(1); sg_frame(); }
static int song_test(void)
{
    int bad = 0, ok;
    uint32_t i;
    track_t *t = &trk[0];
    static uint8_t r[BK_REC];
    fresh();
    ui_launch = LAUNCH_LOOP;                      /* (a launch's: at the pass end) */
    simple(0, 30, 4, DIR_FWD);                    /* T1: A 4 steps (30..) x2, B 2 (50..), C empty, D 3 (70..) x3 */
    bank_put(0, 0, 0);
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 1, 0);
    simple(0, 70, 3, DIR_FWD);
    bank_put(0, 3, 0);
    lists.ent[0][0][1] = 1;
    lists.ent[0][3][1] = 2;
    simple(1, 90, 3, DIR_FWD);                    /* T2: X 3 steps (90..), Y 5 (100..) */
    bank_put(1, 0, 0);
    simple(1, 100, 5, DIR_FWD);
    bank_put(1, 1, 0);
    simple(2, 60, 4, DIR_FWD);                    /* T3: one entry */
    bank_put(2, 0, 0);
    ent_go(0, 0);
    ent_go(1, 0);
    lists.song = 1;
    sg_play();
    ok = heard(0, 30) && heard(1, 90);
    sg_steps(7);                                  /* step 7: A's last, its second pass; T2: Y's last */
    ok &= heard(0, 33) && bank_cur(0) == 0u && bank_next(0) == 1u && heard(1, 104) && !stage_pending();
    sg_steps(1);
    ok &= heard(0, 50) && heard(1, 90);
    bad += check("song: A x2, then B; each track at its own pass end (T2: X Y X ..); the next staged ahead", ok);
    t->step[1].note[0] = 55;                      /* (an edit on B as it plays) */
    sg_steps(1);
    ok = heard(0, 55);
    sg_steps(1);                                  /* ((1.2.1, #12) C, empty between B and D: a rest, B's LEN 2 x1) */
    ok &= !trk[0].seq_n;
    sg_steps(1);                                  /* (its frame took the swap in: on C, on no pattern, D staged) */
    ok &= !trk[0].seq_n && lists.cur[0] == 2u && !trk_pid[0] && bank_next(0) == 3u;
    sg_steps(1);
    ok &= heard(0, 70);
    sg_steps(8);                                  /* D x3 */
    ok &= heard(0, 72);
    sg_steps(1);
    ok &= heard(0, 30) && !launch_q[2].armed && heard(2, 60 + 21u % 4u);
    bad += check("  .. C (empty, between B and D) rests B's length, D x3, then A (the list loops); T3's one entry: never a swap", ok);
    sg_steps(8);                                  /* A x2 */
    ok = heard(0, 50);
    sg_steps(1);
    ok &= heard(0, 55);
    bad += check("  B again: its edit there (kept when it went out)", ok);
    ok = !stage_cancel() && launch_q[0].armed;    /* (C staged: the song's rest) */
    lists.song = 0;
    sg_frame();
    ok &= !launch_q[0].armed;
    lists.song = 1;
    sg_frame();
    ok &= launch_q[0].armed && bank_next(0) == 2u;
    bad += check("  OCT- leaves the song's next (none of the user's); SONG off drops it, on stages it again", ok);
    sg_stop();
    ok = !launch_q[0].armed && bank_cur(0) == 1u && heard(0, 55) == 0;
    sg_play();                                    /* PLAY: on B, the entry it is on, from its start */
    sg_steps(1);
    ok &= heard(0, 55);
    sg_steps(2);                                  /* (C's rest) */
    ok &= !trk[0].seq_n;
    sg_steps(1);
    ok &= heard(0, 70);
    bad += check("  STOP: the song's next dropped; PLAY: on from the entry each track is on", ok);
    ok = bank_switch(0, 1) == 0 && !launch_q[0].adv;   /* (a launch, LOOP: B at D's first pass end, not its third) */
    sg_steps(3);
    ok &= heard(0, 50);
    sg_steps(2);
    ok &= !trk[0].seq_n;                          /* (C's rest) */
    sg_steps(2);
    ok &= heard(0, 70);
    bad += check("  a launch moves the song: B at D's pass end, then on from B (C's rest, D)", ok);
    sg_steps(1);
    song_top();                                   /* GLO + PLAY: each track's first entry, from the top */
    events_block(1);
    ok = song.playing && heard(0, 30) && heard(1, 90) && heard(2, 60) && bank_cur(0) == 0u && bank_cur(1) == 0u;
    bad += check("  GLO + PLAY: from the top, every track on its first entry", ok);

    sg_stop();                                    /* (#29) no waiting slot free: the song goes on all the same */
    simple(0, 30, 4, DIR_FWD);
    bank_put(0, 0, 0);
    lists.ent[0][0][1] = 0;
    ok = bk_pack(r, &trk[3]);
    for (i = 0; i < PEND_N; i++)
        ok &= pat_keep_rec(40u + i, r) == 0;
    sg_play();
    sg_steps(4);
    ok &= heard(0, 50) && pend_free() == 0u;     /* (A unchanged: nothing of it to keep) */
    t->step[0].note[0] = 57;                      /* B edited as it plays: nowhere for it to wait */
    sg_steps(2);
    sg_frame();                                   /* (C's rest came in; B held, in launch_q) */
    ok &= !trk[0].seq_n && launch_q[0].done == 2u && str_eq(ui.msg2, "STOP TO SAVE") && lists.cur[0] == 1u;
    sg_steps(4);
    ok &= !trk[0].seq_n && !launch_q[0].armed;   /* (the track waits on the rest: D not staged) */
    sg_stop();                                    /* stopped: a chunk written to make room, B kept */
    ok &= !launch_q[0].done && lists.cur[0] == 2u && keep_all() == 0 && ent_go(0, 1) == 0 && t->step[0].note[0] == 57;
    bad += check("  (#29) no waiting slot free: the song goes on; an edit with nowhere to wait held, kept at STOP", ok);

    sg_stop();                                    /* a LEN 1 pattern at 1/32, x3: exactly three steps */
    simple(0, 40, 1, DIR_FWD);
    t->p[P_SDIV] = 3;
    bank_put(0, 0, 0);
    lists.ent[0][0][1] = 2;
    simple(0, 45, 1, DIR_FWD);
    t->p[P_SDIV] = 3;
    bank_put(0, 1, 0);
    lists.ent[0][1][1] = 0;
    lists.ent[0][3][0] = 0;                       /* (D out of the list) */
    ent_go(0, 0);
    sg_play();
    ok = heard(0, 40);
    for (i = 1; i < 12u; i++) {
        sg_steps(1);
        ok &= heard(0, i % 4u == 3u ? 45u : 40u);
    }
    bad += check("  LEN 1 at 1/32, x3: P P P Q P P P Q, exact", ok);

    sg_stop();                                    /* NUDGE: the next step early in this one, but not across a swap */
    simple(0, 30, 4, DIR_FWD);
    t->step[0].nudge = -16;
    bank_put(0, 0, 0);
    lists.ent[0][0][1] = 1;
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 1, 0);
    ent_go(0, 0);
    sg_play();
    sg_steps(3);                                  /* A's last step, pass 1: its step 1 set to play early in it */
    ok = (t->nud & NUD_EARLY) && t->early_idx == 0u;
    sg_steps(4);                                  /* pass 2's last (B comes in at its end): not */
    ok &= !(t->nud & NUD_EARLY);
    sg_steps(1);
    ok &= heard(0, 50);
    bad += check("  NUDGE: a step early in the one before, not where the next pattern comes in", ok);
    sg_stop();
    lists.song = 0;
    return bad;
}

/* (2.0, #30) a song entry's sound (lists.snd; bank.c esnd_make, launch.c esnd_put): in at the switch, the entry's
 * first step on it (staged with the entry), stopped at once; an entry without one leaves the sound; the track's own
 * values (LEVEL PAN) kept; no undo step; MOMENTARY's held values not put back over it; FM6 its preset's patch, the
 * engine switched by the fade; a user sound (#20's name), none once its slot is erased; saved and loaded with the
 * project (SKF4), COPY+ copies it, CLEAR clears it */
static int32_t ss_buf[2 * CTL];
static int ss_mixed;                                  /* a block after a swap rendered T1's old engine on the new one's */
static void ss_mix(uint32_t blocks)                   /* blocks rendered (mix_block), the main loop's frame each 8 */
{
    while (blocks--) {
        if (!(blocks & 7u))
            sg_frame();
        mix_block(ss_buf, CTL);
        ss_mixed |= launch_q[0].done && trk[0].eng_req != trk[0].engine && !trk[0].xf_on;   /* (values: engine_block) */
    }
}
static int same_preset(const track_t *t, uint32_t e, uint32_t pi)   /* t's values preset pi of engine e's */
{
    const preset_t *p = &ENGINES[e]->presets[pi];
    uint32_t i;
    int ok = t->eng_req == e && t->preset == pi && t->p[P_ATK] == p->env[0] && t->p[P_DEC] == p->env[1] &&
             t->p[P_SUS] == p->env[2] && t->p[P_REL] == p->env[3];
    for (i = 0; i < 8u; i++)
        ok &= t->p[P_E0 + i] == (int16_t)p->e[i];
    return ok;
}
static int song_sound(void)
{
    int bad = 0, ok;
    uint32_t u0, i, e0;
    track_t *t = &trk[0];
    uint8_t v[FP_SIZE + 1u];
    fresh();
    ui_launch = LAUNCH_LOOP;
    e0 = t->eng_req;
    simple(0, 30, 2, DIR_FWD);                        /* A: 2 steps (30..), B: 2 steps (50..) */
    bank_put(0, 0, 0);
    simple(0, 50, 2, DIR_FWD);
    motion_set_event(t, 1, P_DEC, 5);                 /* (B's second step: DECAY 5, its automation) */
    bank_put(0, 1, 0);
    ent_go(0, 0);
    t->p[P_LEVEL] = 77;
    t->p[P_PAN] = -20;
    lists.snd[0][1][0] = (uint8_t)(e0 + 1u);          /* B: the track's engine, its preset 3 */
    lists.snd[0][1][1] = 2;
    u0 = undo_n;
    lists.song = 1;
    sg_play();
    sg_steps(1);                                      /* A's last step: B staged, its sound made */
    ok = heard(0, 31) && launch_q[0].armed && launch_q[0].snd && !same_preset(t, e0, 2);
    (void)mom_hold(&t->p[P_ATK], 0);                  /* (LFO held, a knob turned: held to come back) */
    t->p[P_ATK] = 99;
    sg_steps(1);                                      /* B's first step */
    ok &= heard(0, 50) && same_preset(t, e0, 2) && t->p[P_LEVEL] == 77 && t->p[P_PAN] == -20 && undo_n == u0 &&
          motion_base_value(t, P_E0) == t->p[P_E0];
    sg_frame();
    ok &= !mom.n && t->p[P_ATK] == ENGINES[e0]->presets[2].env[0];
    bad += check("SOUND: B's (its engine's preset 3) in at the switch, B's first step on it; LEVEL PAN kept; no undo "
                 "step; MOMENTARY's held value not back over it", ok);
    sg_steps(1);                                      /* B's last (its DECAY 5), then A (no sound): B's stays */
    ok = t->p[P_DEC] == 5 && ENGINES[e0]->presets[2].env[1] != ENGINES[e0]->presets[4].env[1];
    sg_steps(1);
    ok &= heard(0, 30) && same_preset(t, e0, 2) && !launch_q[0].snd;
    bad += check("  A, without a sound: the sound as it was (B's automation back to B's sound's value)", ok);
    lists.snd[0][1][0] = ENGI_FM6 + 1u;               /* B: FM6's preset 2, the engine changes */
    lists.snd[0][1][1] = 1;
    ss_mix(420);                                      /* (A's two steps, B in: rendered, the fade between engines) */
    fm6_unpack(FM6_FACTORY[t->p[P_E7] % FM6_NFACTORY], v);
    ok = lists.cur[0] == 1u && same_preset(t, ENGI_FM6, 1) && t->engine == ENGI_FM6 && !t->xf_on &&
         !memcmp(fm6_patch[0], v, FP_SIZE) && t->p[P_LEVEL] == 77;
    for (i = 0; i < NVOICE && !(t->v[i].active && t->v[i].gate); i++)
        ;
    bad += check("  FM6's preset 2 (another engine): its patch, the engine switched by the fade (from the swap's own "
                 "block), B's note on it", ok && i < NVOICE && !ss_mixed);
    sg_stop();
    lists.snd[0][0][0] = (uint8_t)(e0 + 1u);          /* stopped: onto A (preset 5) at once */
    lists.snd[0][0][1] = 4;
    ok = bank_switch(0, 0) == 0 && lists.cur[0] == 0u && same_preset(t, e0, 4) && t->p[P_LEVEL] == 77 && undo_n == u0;
    bad += check("  stopped: A's sound (preset 5) at once, LEVEL kept, no undo step", ok);
    up_store(3, "ENTRY U4");                          /* (the sound now, A's: a user preset, slot 4) */
    apply_preset_to(t, 0);
    lists.snd[0][1][0] = 0x80u | 3u;                  /* B: user slot 4 */
    lists.snd[0][1][1] = 0;
    ok = bank_switch(0, 1) == 0 && t->eng_req == e0 && t->user == 4u && !t->preset &&
         t->p[P_E0] == ENGINES[e0]->presets[4].e[0];
    up_put(3, 0);                                     /* (the slot erased since) */
    apply_preset_to(t, 1);
    ok &= bank_switch(0, 0) == 0 && bank_switch(0, 1) == 0 && same_preset(t, e0, 4);   /* (A's, then none at B) */
    bad += check("  a user sound (slot 4): its values, the slot named (#20); its slot erased since: none, as it was", ok);
    lists.snd[0][1][0] = ENGI_FM6 + 1u;
    lists.snd[0][1][1] = 1;
    project_save(2);
    memset(lists.snd, 0, sizeof lists.snd);
    project_load(2);
    ok = lists.snd[0][0][0] == e0 + 1u && lists.snd[0][0][1] == 4u && lists.snd[0][1][0] == ENGI_FM6 + 1u &&
         same_preset(t, e0, 4);
    bad += check("  saved and loaded with the project (SKF4); a load plays the tracks' sounds as saved", ok);
    ok = song_put(0, 1, SPICK_COPYP) == 0 && lists.ent[0][2][0] && !memcmp(lists.snd[0][2], lists.snd[0][1], 2);
    ok &= song_clear_at(0, 2) == 0 && !lists.ent[0][2][0] && !lists.snd[0][2][0] && !lists.snd[0][2][1];
    bad += check("  COPY+ copies it with the entry, CLEAR clears it", ok);
    sg_stop();                                        /* ((the unit, 2026-10-24) the track on A (preset 5), another */
    ok = bank_switch(0, 0) == 0 && same_preset(t, e0, 4);   /* sound put on it stopped (for another entry's) */
    apply_preset_to(t, 1);
    chain_top();                                      /* (PLAY: ui_input.c play_press) */
    ok &= same_preset(t, e0, 4) && t->p[P_LEVEL] == 77;
    t->p[P_ATK] = (int16_t)(t->p[P_ATK] + 3);         /* (A's sound, a knob turned) */
    chain_top();
    ok &= t->eng_req == e0 && t->preset == 4u && t->p[P_ATK] == ENGINES[e0]->presets[4].env[0] + 3;
    bad += check("  PLAY from stopped: the entry's sound, not one put on the track since; the same sound tweaked: kept", ok);
    lists.song = 0;
    return bad;
}

/* (2.0.3, #71) one pattern on two entries in a row (A A B): the second plays its edits at once, made stopped or as the
 * first plays (after the second's copy was staged: launch.c same), with its own sound; kept when it goes out */
static int song_same(void)
{
    int bad = 0, ok;
    uint32_t e0;
    track_t *t = &trk[0];
    fresh();
    ui_launch = LAUNCH_LOOP;
    e0 = t->eng_req;
    simple(0, 30, 2, DIR_FWD);                        /* A: 2 steps (30..) on entries 1 and 2; B: 2 steps (50..) */
    bank_put(0, 0, 0);
    lists.ent[0][1][0] = lists.ent[0][0][0];
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 2, 0);
    lists.snd[0][1][0] = (uint8_t)(e0 + 1u);          /* (the second A: its own sound, the engine's preset 3) */
    lists.snd[0][1][1] = 2;
    ent_go(0, 0);
    t->step[0].note[0] = 37;                          /* an edit, stopped (not kept yet) */
    lists.song = 1;
    sg_play();
    ok = heard(0, 37);
    sg_frame();                                       /* (the second A staged: a copy from before the edits) */
    ok &= launch_q[0].armed && bank_next(0) == 1u;
    t->step[1].note[0] = 38;                          /* an edit as A plays, after that */
    lv_next(1);
    ok &= heard(0, 38);
    sg_steps(1);                                      /* the second A */
    ok &= heard(0, 37) && same_preset(t, e0, 2);
    sg_steps(1);                                      /* (its frame took the swap in: on the second entry) */
    ok &= heard(0, 38) && lists.cur[0] == 1u;
    bad += check("(#71) one pattern on two entries in a row: the second plays its edits at once (made stopped, or as the "
                 "first played), on its own sound", ok);
    sg_steps(1);                                      /* B: A kept as it went out */
    ok = heard(0, 50);
    sg_steps(2);                                      /* A again (the list loops) */
    ok &= heard(0, 37);
    sg_steps(1);
    ok &= heard(0, 38) && lists.cur[0] == 0u;
    sg_stop();
    ok &= keep_all() == 0 && ent_go(0, 1) == 0 && t->step[0].note[0] == 37 && t->step[1].note[0] == 38;
    bad += check("  .. kept as it went out: B, then the first A with them; stopped, the second A: them", ok);
    lists.song = 0;
    return bad;
}

/* (2.0.3, found making the 2.0 video) the song's next entry staged, then the list changed: it comes in as the list is
 * now (a pattern put into the empty entry after the one playing: that pattern, not a rest; its REPS turned after the
 * entry after it was staged: as many passes) */
static int song_next_edit(void)
{
    int bad = 0, ok;
    uint32_t d;
    fresh();
    ui_launch = LAUNCH_LOOP;
    simple(0, 30, 2, DIR_FWD);                        /* A (30..) on entry 1, C (70..) on 3, entry 2 empty (a rest); */
    bank_put(0, 0, 0);                                /* D (50..) in the pool, on no entry */
    simple(0, 70, 2, DIR_FWD);
    bank_put(0, 2, 0);
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 5, 0);
    d = ent_pat(0, 5);
    lists.ent[0][5][0] = 0;
    ent_go(0, 0);
    lists.song = 1;
    sg_play();
    ok = heard(0, 30);
    sg_frame();                                       /* (entry 2 staged: the rest) */
    ok &= launch_q[0].armed && bank_next(0) == 1u;
    lists.ent[0][1][0] = (uint8_t)(d + 1u);           /* D into entry 2 as A plays (as COPY+ puts one) */
    lv_next(1);
    ok &= heard(0, 31);
    sg_steps(1);                                      /* (its frame staged entry 2 again) */
    ok &= heard(0, 50);
    bad += check("(2.0.3) SONG: the next entry staged, then a pattern put into it (empty, a rest): that pattern comes in",
                 ok);
    sg_frame();                                       /* (C staged after D's one pass) */
    ok = launch_q[0].armed && bank_next(0) == 2u;
    lists.ent[0][1][1] = 1;                           /* D x2, after that */
    lv_next(1);
    sg_steps(1);
    ok &= heard(0, 50);                               /* D's second pass */
    sg_steps(2);
    ok &= heard(0, 70);
    bad += check("  .. its REPS turned after the entry after it was staged: D twice, then C", ok);
    sg_stop();
    lists.song = 0;
    return bad;
}

/* (#29) a song where every track has a list (T6 DRUM, its lane hits), all of one length, so all move on together, and
 * a pattern waiting to be written (an edit kept, stopped, that the autosave's quiet stop has not written yet): every
 * track on to its next entry at its pass end, the last one too, round and round */
static int song_six(void)
{
    int bad = 0, ok = 1;
    uint32_t k, e, i;
    fresh();
    ui_launch = LAUNCH_LOOP;
    for (k = 0; k < NTRK; k++) {                  /* each track: A (20 + 3k ..), B (40 + 3k ..), 4 steps */
        for (e = 0; e < 2u; e++) {
            simple(k, 20u + 20u * e + 3u * k, 4, DIR_FWD);
            trk[k].step[0].hit = (uint8_t)(k == NTRK - 1u ? 1u << e : 0u);   /* (T6: KICK on A, SNARE on B) */
            ok &= bank_put(k, e, 0) == 0;
        }
        ok &= ent_go(k, 0) == 0;
    }
    trk[NTRK - 1u].step[2].hit = 4u;              /* T6's A edited, then B and back: A waits to be written */
    ok &= ent_go(NTRK - 1u, 1) == 0 && ent_go(NTRK - 1u, 0) == 0 && pend_free() == PEND_N - 1u;
    lists.song = 1;
    sg_play();
    for (i = 1; i <= 4u; i++) {
        sg_steps(4);                              /* (a pass: every track's next came in at its end) */
        sg_frame();
        for (k = 0; k < NTRK; k++)
            ok &= lists.cur[k] == i % 2u && trk[k].step[0].note[0] == 20u + 20u * (i % 2u) + 3u * k &&
                  trk[k].step[0].hit == (k == NTRK - 1u ? 1u << i % 2u : 0u);
    }
    ok &= trk[NTRK - 1u].step[2].hit == 4u;       /* (A again: its edit) */
    sg_stop();
    lists.song = 0;
    bad += check("(#29) a song, a pattern waiting to be written: every track on at its pass end, T6 (DRUM) too", ok);
    return bad;
}

/* (#28) RESTs (SONG's PATTERN knob): one before a track's first pattern (it starts later), one after its last (it ends
 * sooner), each its REPS passes of the track's length; GLO + PLAY from the REST; the empty entries at either end of a
 * list of before still passed over; a REST launched; stopped on one, recorded into; an SKF3 keeps them */
static int song_rest(void)
{
    int bad = 0, ok;
    track_t *t = &trk[0];
    static project_t pj, got;
    static project_store_t w;
    fresh();
    ui_launch = LAUNCH_LOOP;
    simple(0, 30, 4, DIR_FWD);                    /* T1: B 4 steps (30..), C 2 (50..), A a REST x2, D a REST x1 */
    bank_put(0, 1, 0);
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 2, 0);
    lists.ent[0][0][0] = PAT_REST;
    lists.ent[0][0][1] = 1;
    lists.ent[0][3][0] = PAT_REST;
    lists.song = 1;
    song_top();                                   /* GLO + PLAY: on A, silent, the track's LEN (C's, 2) */
    events_block(1);
    ok = song.playing && lists.cur[0] == 0u && !trk_pid[0] && !t->seq_n && t->p[P_SLEN] == 2 && !bank_used(0, 0) &&
         ent_pat(0, 0) == PAT_NONE;
    sg_steps(3);
    ok &= !t->seq_n;                              /* (A x2: four silent steps) */
    sg_steps(1);
    ok &= heard(0, 30);
    sg_steps(4);
    ok &= heard(0, 50);
    sg_steps(2);
    sg_frame();
    ok &= !t->seq_n && lists.cur[0] == 3u;        /* D: C's length, once */
    sg_steps(2);
    ok &= !t->seq_n;                              /* A again (the list loops), x2 */
    sg_steps(4);
    ok &= heard(0, 30);
    bad += check("(#28) a REST before the first pattern (starts later), one after the last (ends sooner), its REPS "
                 "passes of the track's length; GLO + PLAY from it", ok);
    sg_stop();
    lists.ent[0][0][0] = lists.ent[0][3][0] = 0;  /* (a list of before: A and D empty) */
    song_top();
    events_block(1);
    ok = heard(0, 30) && lists.cur[0] == 1u;
    sg_steps(4);
    ok &= heard(0, 50);
    sg_steps(2);
    ok &= heard(0, 30);
    bad += check("  a list of before: the empty entries before its first pattern and after its last passed over", ok);
    lists.ent[0][3][0] = PAT_REST;
    ok = bank_switch(0, 3) == 0 && bank_next(0) == 3u && !launch_q[0].adv;   /* (a launch, LOOP: at B's pass end) */
    sg_steps(4);
    ok &= !t->seq_n;                              /* (D, B's length) */
    sg_steps(4);
    ok &= heard(0, 30);
    bad += check("  a REST launched (PERFORM, SONG's OCT+): the track drops out at its launch point, on from it", ok);
    sg_stop();
    ok = ent_go(0, 3) == 0 && lists.cur[0] == 3u && !trk_pid[0] && seq_is_empty(t) && t->p[P_SLEN] == 4;
    t->step[0] = (step_t){{77}, 1, ST_NOTE, 0, 100};   /* recorded into */
    ok &= keep_all() == 0 && !ent_rest(0, 3) && trk_pid[0] && lists.ent[0][3][0] == trk_pid[0];
    bad += check("  stopped on a REST: silent, the track's LEN as it was; recorded into: a pattern in that entry", ok);
    project_capture(&pj);
    pj.ent[0][0][0] = PAT_REST;
    pj.ent[0][0][1] = 3;
    ok = proj_pack2(&w, &pj, &PROJ_SKF3) && w.raw[68u + NTRK * (PROJ_NP + 2u)] == PAT_REST &&
         proj_unpack2(&got, w.raw, &PROJ_SKF3) && got.ent[0][0][0] == PAT_REST && got.ent[0][0][1] == 3u;
    pj.ent[0][0][0] = PAT_REST + 1u;
    ok &= !proj_pack2(&w, &pj, &PROJ_SKF3);
    bad += check("  an SKF3 keeps a REST (65, which firmware before refuses); past it refused", ok);
    lists.song = 0;
    return bad;
}

/* ---- (phase 5) projects of before SKF2 (bank.c proj_migrate): their tracks pool patterns, SKF2 ---- */
static uint32_t pool_count(void)
{
    uint32_t id, n = 0;
    for (id = 0; id < BK_POOL; id++)
        n += (uint32_t)pool_used(id);
    return n;
}
static int migrate(void)
{
#if !FELUCCA_LEGACY
    return 0;                                     /* ((2.0) the converter's: run_tests.sh's FELUCCA_LEGACY build runs it) */
#else
    int bad = 0, ok, n;
    uint32_t k, cuts = 0;
    static uint8_t before[sizeof nor];
    static project_t pj, got;
    static project_store_t w, ref;
    static uint8_t r[BK_REC];
    snap_t a[NTRK], b;
    fresh();
    rich(0, 5);                                   /* T1 every field; T2 the same pattern (on its own track); T3 empty; */
    rich(1, 5);                                   /* T4 8 steps */
    simple(3, 50, 8, DIR_FWD);
    for (k = 0; k < NTRK; k++)
        snap(&a[k], k);
    project_capture(&pj);
    ok = proj_pack(&w, &pj) && !st_save(OBJ_PROJECT0, &w, sizeof w) && !st_save(OBJ_PROJECT0 + 2u, &w, sizeof w) &&
         !st_save(OBJ_AUTOSAVE, &w, sizeof w);    /* (SKF1: slots A and C, the autosave) */
    memcpy(before, nor, sizeof nor);
    ok &= proj_migrate(0) == 0 && !pool_count();
    bad += check("migrate: not after a crash, a hang or a failed boot (nothing written)", ok);
    ok = proj_migrate(1) == 1 && pool_count() == 2u;
    for (k = 0; k < 5u; k++) {
        uint32_t obj = k < 4u ? OBJ_PROJECT0 + k : OBJ_AUTOSAVE;
        n = st_load(obj, &w, sizeof w);
        if (k == 1u || k == 3u) {
            ok &= n <= 0;                         /* (empty slots: none) */
            continue;
        }
        ok &= n == (int)PROJ_SKF->size && proj_import(&got, &w, n) && got.ent[0][0][0] && got.ent[1][0][0] == got.ent[0][0][0] &&
              !got.ent[2][0][0] && got.ent[3][0][0] && got.ent[3][0][0] != got.ent[0][0][0] && !got.cur[0] && !got.ent[0][1][0];
    }
    ok &= proj_migrate(1) == 0;                   /* (again: all SKF2 (SKF3 with six tracks), nothing to do) */
    bad += check("  slots A C and the autosave: SKF2 (SKF3), each track's entry A its pattern (T1 T2 one, shared), T3 none",
                 ok);
    favorite_set(ENGI_DRUMX, 0, 1);               /* ((Skiff phase 7) row 14 is DRUM-X's favourites) */
    ok = proj_migrate(1) == 0 && favorite_has(ENGI_DRUMX, 0);
    favorite_set(ENGI_DRUMX, 0, 0);
    bad += check("  a DRUM-X favourite (row 14) stays", ok);
    for (k = 0; k < NTRK; k++)
        detach(k);
    project_load(0);
    ok = 1;
    for (k = 0; k < NTRK; k++) {
        snap(&b, k);
        ok &= same(&a[k], &b);
    }
    bad += check("  the same music: every track's steps, timing, automation and locks as before", ok);
    st_load(OBJ_PROJECT0, &ref, sizeof ref);
    ok = 1;
    for (n = 0; n < 400; n++) {                   /* the power goes after n writes (each page, each commit) */
        memcpy(nor, before, sizeof nor);
        power_cycle();
        fail_after = n;
        proj_migrate(1);
        k = fail_after > 0;                       /* (it ended before the cut: done) */
        cuts += !k;
        fail_after = -1;
        power_cycle();
        proj_migrate(1);                          /* (the next boot) */
        ok &= st_load(OBJ_PROJECT0, &w, sizeof w) == (int)PROJ_SKF->size && !memcmp(&w, &ref, PROJ_SKF->size) &&
              pool_count() == 2u && st_load(OBJ_AUTOSAVE, &w, sizeof w) == (int)PROJ_SKF->size;
        if (k)
            break;
    }
    bad += check("  a power cut after any write: the next boot ends it the same (the same patterns, none twice)",
                 ok && cuts > 10u && n < 400);
    memcpy(nor, before, sizeof nor);
    power_cycle();
    for (k = 0; k < NTRK; k++)
        detach(k);
    ok = bk_pack(r, &trk[2]);                     /* (an empty track's pattern: none of the project's) */
    for (k = 0; k < BK_POOL; k++)
        ok &= pool_put(k, r) == 0;
    ok &= proj_migrate(1) == 0 && st_load(OBJ_PROJECT0, &w, sizeof w) == (int)sizeof w;
    memset(proj_slot, 0, sizeof proj_slot);       /* (the RAM copies lost: from flash) */
    proj_fetch(0);
    project_load(0);
    for (k = 0; k < NTRK; k++) {
        snap(&b, k);
        ok &= same(&a[k], &b);
    }
    bad += check("  the pool full: the projects as they were (SKF1), loading with their own steps", ok);
    return bad;
#endif
}

/* ---- (Skiff phase 7) the DRUM-X kit is the project's: saved with it (SKF3), back when loaded, the factory's for a project
 * without one; an edit of it is something an autosave keeps ---- */
static int dx_kit_project(void)
{
    int bad = 0, ok;
    uint32_t sig;
    fresh();
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    sig = autosave_sig();
    dx_kit[DV_KICK].a[DXP_DECAY] = 5;
    ok = autosave_sig() != sig && project_save_as(1, "KIT") == 0;
    dx_kit[DV_KICK].a[DXP_DECAY] = 99;
    project_load(1);
    if (NTRK > PROJ_NT) {
        ok &= dx_kit[DV_KICK].a[DXP_DECAY] == 5;
        bad += check("(phase 7) the DRUM-X kit: saved with the project (SKF3), back when it loads; an edit an autosave keeps", ok);
    } else {
        ok &= !memcmp(dx_kit, DX_KIT_DEF, sizeof dx_kit);
        bad += check("(phase 7) the DRUM-X kit with four tracks (SKF2 holds none): the factory's when a project loads", ok);
    }
    memcpy(dx_kit, DX_KIT_DEF, sizeof dx_kit);
    return bad;
}

/* ---- (2.0, #47) SONG ONCE (bank.c song_poll): each track its list once, its last entry's REPS too, then silent; the
 * transport stops when the last track's end comes in, each on its first entry again; a full list ends on its last;
 * a launch of its own goes on from there ---- */
static int song_once(void)
{
    int bad = 0, ok;
    uint32_t e;
    fresh();
    ui_launch = LAUNCH_LOOP;
    simple(0, 30, 2, DIR_FWD);                    /* T1: A 2 steps (30..) x2, B 2 (50..); T2: X 3 (90..), one entry */
    bank_put(0, 0, 0);
    lists.ent[0][0][1] = 1;
    simple(0, 50, 2, DIR_FWD);
    bank_put(0, 1, 0);
    simple(1, 90, 3, DIR_FWD);
    bank_put(1, 0, 0);
    lists.song = SONG_ONCE;
    song_top();                                   /* PLAY (ui_input.c play_press): from every track's first entry */
    events_block(1);
    ok = song.playing && heard(0, 30) && heard(1, 90) && bank_cur(0) == 0u;
    sg_steps(2);
    ok &= heard(0, 30) && heard(1, 92) && bank_next(0) == 1u && bank_next(1) == 1u;   /* (T2's end: its empty B) */
    sg_steps(1);
    ok &= heard(0, 31) && !trk[1].seq_n && song.playing;
    sg_steps(2);
    ok &= heard(0, 51) && bank_next(0) == 2u && song.playing;
    sg_steps(1);
    ok &= !trk[0].seq_n && song.playing;          /* (T1's end came in: its frame stops the song) */
    sg_frame();
    ok &= !song.playing && lists.cur[0] == 0u && lists.cur[1] == 0u && bank_cur(0) == 0u && bank_cur(1) == 0u &&
          trk[0].step[0].note[0] == 30u && trk[1].step[0].note[0] == 90u;
    bad += check("(2.0, #47) SONG ONCE: each list once (A x2, B; X), silent at its end; stopped after the last, on the "
                 "first entries", ok);
    for (e = 1; e < NENT; e++)                    /* T2: X in all 16 entries: its end on the last one */
        lists.ent[1][e][0] = lists.ent[1][0][0];
    song_top();
    events_block(1);
    sg_steps(3u * NENT - 1u);
    ok = heard(1, 92) && lists.cur[1] == NENT - 1u && song.playing;
    sg_steps(1);
    ok &= !trk[1].seq_n && lists.cur[1] == NENT - 1u && song.playing;
    sg_frame();
    ok &= !song.playing && lists.cur[1] == 0u && trk[1].step[0].note[0] == 90u;
    bad += check("  a full list: its end a rest on its last entry; then stopped, on the first", ok);
    for (e = 1; e < NENT; e++)
        lists.ent[1][e][0] = 0;
    song_top();
    events_block(1);
    sg_steps(4);                                  /* (T2 ended at 3; T1 on B) */
    ok = !trk[1].seq_n && heard(0, 50) && bank_switch(1, 0) == 0;   /* (a launch of T2's own: X at its rest's end) */
    sg_steps(2);
    ok &= song.playing && !trk[0].seq_n && heard(1, 90);   /* (T1's end in, T2's X too) */
    sg_steps(3);
    ok &= !trk[1].seq_n && song.playing;
    sg_frame();
    ok &= !song.playing;
    bad += check("  a launch after a track's end: it plays, then its end again; the song stops after that", ok);
    song_top();
    events_block(1);
    sg_steps(4);
    sg_stop();                                    /* STOP mid-song: T1 on B, T2 on its end */
    ok = !song.playing && lists.cur[0] == 1u && lists.cur[1] == 1u;
    song_top();                                   /* PLAY: from the top, the whole song again */
    events_block(1);
    ok &= song.playing && heard(0, 30) && heard(1, 90);
    sg_steps(6);
    sg_frame();
    ok &= !song.playing && lists.cur[0] == 0u;
    bad += check("  STOP mid-song, PLAY: from the top, to its end again", ok);
    ok = project_save_as(1, 0) == 0;              /* (a project with SONG ONCE: saved, loaded, ONCE again) */
    lists.song = 0;
    project_load(1);
    ok &= lists.song == SONG_ONCE;
    bad += check("  a project saved with SONG ONCE loads with it", ok);
    lists.song = 0;
    return bad;
}

/* ---- (2.0, #44, the user's pick 3A and the join sheet's A) a chain: entries on CHAIN into the next play as one
 * pattern: SONG off it loops; a LOOP launch waits for its end; SONG on, its last entry's REPS repeat it; ONCE ends
 * after it; a launch onto any part starts it from its first, PLAY too; COPY+ copies all of it; a project keeps it ---- */
static int song_chain(void)
{
    int bad = 0, ok;
    uint32_t a, b;
    static uint8_t r1[BK_REC], r2[BK_REC];
    fresh();
    ui_launch = LAUNCH_LOOP;
    simple(0, 30, 2, DIR_FWD);                    /* T1: A 2 steps (30..) on CHAIN into B 3 (50..); C 2 (70..) */
    bank_put(0, 0, 0);
    simple(0, 50, 3, DIR_FWD);
    bank_put(0, 1, 0);
    simple(0, 70, 2, DIR_FWD);
    bank_put(0, 2, 0);
    lists.ent[0][0][1] = REPS_CHAIN;
    (void)ent_go(0, 0);
    sg_play();                                    /* SONG off: A B A .. */
    ok = heard(0, 30);
    sg_steps(2);
    ok &= heard(0, 50);
    sg_steps(3);
    ok &= heard(0, 30);
    bad += check("(2.0, #44) a chain, SONG off: A on CHAIN into B, one pattern looping (A B A ..)", ok);
    sg_steps(1);                                  /* (on A, B staged) */
    ok = bank_switch(0, 2) == 0 && chain_go[0] == 3u && bank_next(0) == 2u && launch_q[0].armed &&
         launch_q[0].slot == 1u;
    sg_steps(1);
    ok &= heard(0, 50);
    sg_steps(3);
    ok &= heard(0, 70) && !chain_go[0];
    bad += check("  a LOOP launch on its first part waits for the chain's end (B first, then C)", ok);
    sg_stop();
    lists.song = SONG_ON;
    lists.ent[0][1][1] = 1;                       /* (B x2: the chain twice, then C) */
    (void)ent_go(0, 0);
    sg_play();
    sg_steps(5);
    ok = heard(0, 30);
    sg_steps(5);
    ok &= heard(0, 70);
    sg_steps(2);
    ok &= heard(0, 30) && !chain_rep[0];
    bad += check("  SONG on: the chain's last entry's REPS repeat it (A B A B, C, A ..)", ok);
    sg_stop();
    lists.song = SONG_ONCE;
    lists.ent[0][2][0] = 0;                       /* (C out: the chain the list's end) */
    song_top();
    events_block(1);
    sg_steps(9);
    ok = heard(0, 52) && song.playing;
    sg_steps(1);
    ok &= !trk[0].seq_n && song.playing;
    sg_frame();
    ok &= !song.playing && lists.cur[0] == 0u;
    bad += check("  SONG ONCE: the chain its REPS times (A B A B), then the end", ok);
    lists.song = SONG_OFF;
    (void)ent_go(0, 1);
    ok = lists.cur[0] == 1u && bank_switch(0, 1) == 0 && lists.cur[0] == 0u && trk[0].step[0].note[0] == 30u;
    (void)ent_go(0, 1);
    chain_top();
    ok &= lists.cur[0] == 0u;
    bad += check("  a launch onto a chain's part plays it from its first; PLAY too (chain_top)", ok);
    a = ent_pat(0, 0);
    b = ent_pat(0, 1);
    ok = song_put(0, 1, SPICK_COPYP) == 0 && str_eq(ui.msg, "COPIED A-B TO C-D") && ent_pat(0, 2) == 3u &&
         ent_pat(0, 3) == 4u && lists.ent[0][2][1] == REPS_CHAIN && lists.ent[0][3][1] == 1u && ent_pat(0, 0) == a &&
         ent_pat(0, 1) == b && !pat_read(a, r1) && !pat_read(3u, r2) && !memcmp(r1, r2, BK_REC) &&
         !pat_read(b, r1) && !pat_read(4u, r2) && !memcmp(r1, r2, BK_REC);
    bad += check("  COPY+ on a chain: every part copied (04, 05: 03 kept), a chain again at C-D, its REPS too", ok);
    ok = project_save_as(1, 0) == 0;
    lists.ent[0][0][1] = 0;
    project_load(1);
    ok &= lists.ent[0][0][1] == REPS_CHAIN && lists.ent[0][2][1] == REPS_CHAIN;
    bad += check("  a project with chains saves and loads with them", ok);
    return bad;
}

int main(void)
{
    int bad = layout() + fidelity() + keep() + entries() + staged_all() + last_track() + torn() + wear_and_wrap() +
              playing() + live() + scenes() + scenes_six() + song_test() + song_six() + song_rest() + song_once() +
              song_chain() + song_sound() + song_same() + song_next_edit() + migrate() + dx_kit_project();
    printf("%s\n", bad ? "BANK TEST FAILED" : "pattern bank tests passed");
    return bad != 0;
}
