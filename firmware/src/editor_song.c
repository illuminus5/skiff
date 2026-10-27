/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff W3) the editor's song (EDITOR_PROTOCOL.md "Pool, lists, PERFORM"): the pattern pool (SAVE > PATTERNS), the
 * tracks' lists (SEQ > SONG), PERFORM's scenes, staged mutes and LAUNCH; INFO 57 01 caps bits 2, 3, 4. Each acts as the
 * device's own control does, through the same functions (bank.c), and says so on the device. Included by editor.c.
 *   POOL: [1, id: CLEAR], ((2.0) [2, id: EMPTY] (its steps, NUDGEs, automation; it and its entries stay), [3: CLEAR
 *     UNUSED]) -> op, rc (0 done, 1 nothing to clear / empty / unused, 2 in use, 3 playing, 4 failed), then each
 *     pattern: its LEN (0 empty), what uses it (bank.c pool_refs, at most 127)
 *   LIST: [op, track, entry ..]: 1 SET (pattern 1..64: a kept one; (#28) 65 a REST), 2 COPY, 3 PLAY (the track to it),
 *     4 CLEAR (stopped; its pattern goes when nothing else names it), 5 REPS (1..16; an entry naming a pattern; (2.0) 0
 *     CHAIN: one naming a pattern or a REST with an entry after it, else rc 1); [6, song]: SONG (0 off, 1 on, (2.0) 2
 *     ONCE); (2.0) 7 NEW (a new empty pattern in it), 8 COPY+ (its pattern (its chain: all of it) copied into the first
 *     empty entries after it) -> op, rc (0 done, 1 empty / nothing to copy / no empty entry after / can't chain, 2 done
 *     without its automation, 3 playing, 4 PATTERNS FULL, 5 failed), SONG (on: 0 / 1), then each track: the entry it
 *     is on (127: on no entry's pattern), the one staged (127 none), its pattern (127 none), its 16 entries (pattern
 *     1..64, 65 REST, 0 empty; REPS - 1, a CHAIN's 0)
 *   (2.0) LIST2 (82, INFO 58 01 bit 0): LIST, its reply's SONG 0..2 (2 ONCE) and each REPS byte as kept (0..15: REPS -
 *     1; 16: on CHAIN, REPS_CHAIN). An editor without it keeps LIST's reply as it was. ((2.0, #30) bit 1: each entry's
 *     two bytes then its sound, two more: 0 none; 1..63 an engine + 1, its preset; 64 + a user slot, 0; and LIST2's op
 *     9 SOUND [9, track, entry, those two] puts one on an entry (0 0 takes it off) -> rc 1: an empty entry, a sound it
 *     can't carry (an engine the track may not play, a preset or slot that isn't there))
 *   PERFORM: [op ..]: 1 STORE scene, 2 RECALL scene, 3 MUTE track (the other way; staged while playing, a staged one
 *     taken back), 4 CANCEL (all that is staged), 5 LAUNCH (0 LOOP, 1 BAR, 2 BEAT, 3 NOW) -> op, rc (RECALL: 0 done or
 *     staged, 1 empty, 2 without some automation, 4 PATTERNS FULL; LAUNCH: as UI_SET's), LAUNCH, the scene last
 *     recalled or stored (127 none), the scenes kept (16 bits, 7 a byte), then each track: muted, the mute staged (0
 *     none, 1 to unmute, 2 to mute) */
enum { ED_POOL = 78, ED_LIST, ED_PERFORM };
enum { ED_LIST2 = 82 };
#define ED_SONG2 3u                                 /* (2.0) INFO 58 01: bit 0 LIST2, LIST's ops 7 8, REPS 0, SONG 2,
                                                     * POOL's 2 3; ((#30) bit 1: LIST2's entry sounds, its op 9) */

static int ed_song_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    if (!n)
        return 1;
    if (cmd == ED_POOL)
        return (n == 2u && a[0] >= 1u && a[0] <= 2u && a[1] < NPAT) || (n == 1u && a[0] == 3u);
    if (cmd == ED_LIST || cmd == ED_LIST2) {
        if (a[0] == 6u)
            return n == 2u && a[1] <= SONG_ONCE;
        if (n < 3u || a[1] >= NTRK || a[2] >= NENT)
            return 0;
        if (a[0] == 1u || a[0] == 5u)
            return n == 4u && a[3] >= (a[0] == 1u ? 1u : 0u) && a[3] <= (a[0] == 1u ? PAT_REST : 16u);
        if (a[0] == 9u)                                /* ((2.0, #30) SOUND: LIST2's) */
            return cmd == ED_LIST2 && n == 5u;
        return n == 3u && ((a[0] >= 2u && a[0] <= 4u) || a[0] == 7u || a[0] == 8u);
    }
    if (a[0] == 4u)
        return n == 1u;
    return n == 2u && ((a[0] >= 1u && a[0] <= 2u && a[1] < SCENES) || (a[0] == 3u && a[1] < NTRK) ||
                       (a[0] == 5u && a[1] < LAUNCH_N));
}

static int ed_song_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 0, k, e, r;
    if (cmd == ED_POOL) {
        if (n && a[0] == 3u) {                         /* ((2.0) CLEAR UNUSED) */
            if (!pool_unused()) rc = 1;
            else if (transport_busy()) rc = 3;
            else { pool_clear_unused(); rc = pool_unused() ? 4u : 0u; }
        } else if (n) {
            if (!pool_used(a[1])) rc = 1;
            else if (a[0] == 1u && pool_refs(a[1])) rc = 2;
            else if (transport_busy()) rc = 3;
            else if (a[0] == 2u) pool_empty(a[1]);   /* ((2.0) EMPTY: in use too) */
            else { pool_clear(a[1]); rc = pool_used(a[1]) ? 4u : 0u; }
        }
        ed_b(n ? a[0] : 0u);
        ed_b(rc);
        for (k = 0; k < NPAT; k++) {
            r = pool_refs(k);
            ed_b(pool_used(k) ? pool_len(k) : 0u);
            ed_b(r > 127u ? 127u : r);
        }
        return 1;
    }
    if (cmd == ED_LIST || cmd == ED_LIST2) {
        if (n) {
            k = a[1];
            e = a[2];
            if (a[0] == 1u)
                rc = a[3] == PAT_REST || pool_used(a[3] - 1u) ? (uint32_t)song_put(k, e, a[3]) : 1u;
            else if (a[0] == 2u)
                rc = (uint32_t)song_put(k, e, SPICK_COPY);
            else if (a[0] == 3u)
                rc = (uint32_t)entry_go(k, e);
            else if (a[0] == 4u) {
                r = transport_busy();                      /* (bank_put: 2 the transport runs or a flash error, 3 */
                rc = (uint32_t)song_clear_at(k, e);        /* no flash, 4 PATTERNS FULL) */
                rc = rc == 2u ? (r ? 3u : 5u) : rc == 3u ? 5u : rc;
            } else if (a[0] == 5u) {                       /* ((1.2.1, #12) an empty entry's too: a rest's length; */
                if (!a[3] && !(ent_on(k, e) && e + 1u < NENT))   /* (2.0) CHAIN as KNOB 3's: one with an entry after */
                    rc = 1;
                else
                    lists.ent[k][e][1] = (uint8_t)(a[3] ? a[3] - 1u : REPS_CHAIN);
            } else if (a[0] == 7u || a[0] == 8u) {      /* ((2.0) NEW, COPY+) */
                rc = (uint32_t)song_put(k, e, a[0] == 7u ? SPICK_NEW : SPICK_COPYP);
            } else if (a[0] == 9u) {                    /* ((2.0, #30) SOUND: the wire's two bytes as lists.snd's */
                uint8_t b[2] = {(uint8_t)(a[3] < 64u ? a[3] : 0x80u | (a[3] - 64u)), (uint8_t)(a[3] < 64u ? a[4] : 0u)};
                rc = (uint32_t)song_snd_put(k, e, b);
            } else
                lists.song = a[1];
            bk.gen++;
            ui.force = 1;
        }
        ed_b(n ? a[0] : 0u);
        ed_b(rc);
        ed_b(cmd == ED_LIST2 ? lists.song : lists.song != 0u);
        for (k = 0; k < NTRK; k++) {
            r = bank_cur(k);
            ed_b(r < NENT ? r : 127u);
            r = bank_next(k);
            ed_b(r < NENT ? r : 127u);
            ed_b(trk_pid[k] && trk_pid[k] <= NPAT ? trk_pid[k] - 1u : 127u);
            for (e = 0; e < NENT; e++) {
                ed_b(lists.ent[k][e][0] <= PAT_REST ? lists.ent[k][e][0] : 0u);
                ed_b(cmd == ED_LIST2 && lists.ent[k][e][1] == REPS_CHAIN ? REPS_CHAIN : lists.ent[k][e][1] & 15u);
                if (cmd == ED_LIST2) {                     /* ((2.0, #30) its sound: a user slot as 64 + it */
                    r = lists.snd[k][e][0];
                    ed_b(r & 128u ? 64u + (r & 63u) : r);
                    ed_b(r & 128u ? 0u : lists.snd[k][e][1]);
                }
            }
        }
        return 1;
    }
    if (cmd != ED_PERFORM)
        return 0;
    if (n) {
        if (a[0] == 1u)
            scene_saved(a[1]);
        else if (a[0] == 2u)
            rc = (uint32_t)scene_go(a[1]);
        else if (a[0] == 3u)
            mute_switch(a[1]);
        else if (a[0] == 4u)
            (void)stage_cancel();
        else {
            ui_launch = a[1];
            ui_say("LAUNCH ", LAUNCH_NAME[a[1]]);
            rc = ed_menu_save();
        }
        ui.force = 1;
    }
    ed_b(n ? a[0] : 0u);
    ed_b(rc);
    ed_b(launch_mode());
    ed_b(scene_now() < SCENES ? scene_now() : 127u);
    for (k = 0, r = 0; k < SCENES; k++)
        r |= (uint32_t)scene_used(k) << k;
    ed_b(r & 127u);
    ed_b(r >> 7 & 127u);
    ed_b(r >> 14 & 3u);
    for (k = 0; k < NTRK; k++) {
        ed_b(trk[k].p[P_MUTE] != 0);
        ed_b(mute_arm[k]);
    }
    return 1;
}
