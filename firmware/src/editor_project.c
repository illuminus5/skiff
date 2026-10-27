/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff W3) the editor's transport and the projects' names (EDITOR_PROTOCOL.md "Transport, project names"); INFO
 * 57 01 caps (bit 0 TRANSPORT, bit 1 PROJ_NAME; bits 2..4 editor_song.c's). Included by editor.c.
 *   TRANSPORT: [op] 1 PLAY (playing: nothing), 2 STOP, 3 RESTART (from the top), 4 REC (the selected track armed or not,
 *     as its button: ui_input.c rec_tap) -> rc, playing, counting in, the request the audio has yet to take (seq.c
 *     transport_req: 1 start, 2 stop, 3 restart), the armed tracks, the selected track. No op: the state.
 *   PROJ_NAME: [op 1 SAVE AS / 2 RENAME, slot, name (0..12 of 32..126, upper-cased; its 0 optional)] -> op, rc (0 done,
 *     1 refused: playing, patterns full, an empty slot; 2 failed), the music's slot (127 none), changed since, its
 *     name, then each slot: used, its name. A save or a rename stops the transport first (ed_flash_stop) */
enum { ED_TRANSPORT = 76, ED_PROJ_NAME };
#define ED_SKIFF_CAPS 127u                      /* TRANSPORT, PROJ_NAME, POOL, LIST, PERFORM, (#21) SMP_MIX (editor.c),
                                                 * (2.0, #25) a step's ties (editor.c ed_step_put) */

/* a name from the editor -> b (PROJ_NAME_LEN + 1), upper case; 0: refused (too long, a byte outside 32..126) */
static int ed_proj_name_in(const uint8_t *a, uint32_t n, char *b)
{
    uint32_t i;
    if (n && !a[n - 1u])
        n--;
    if (n > PROJ_NAME_LEN)
        return 0;
    for (i = 0; i < n; i++) {
        if (a[i] < 32u || a[i] > 126u)
            return 0;
        b[i] = (char)(a[i] >= 'a' && a[i] <= 'z' ? a[i] - 32u : a[i]);
    }
    b[n] = 0;
    return 1;
}
static int ed_project_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    char b[PROJ_NAME_LEN + 1u];
    if (cmd == ED_TRANSPORT)
        return !n || (n == 1u && a[0] >= 1u && a[0] <= 4u);
    return !n || (n >= 2u && (a[0] == 1u || a[0] == 2u) && a[1] < 4u && ed_proj_name_in(a + 2, n - 2u, b));
}

static int ed_project_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    char b[PROJ_NAME_LEN + 1u];
    uint32_t k, rc = 0;
    if (cmd == ED_TRANSPORT) {
        if (n && a[0] == 4u)
            rec_tap();
        else if (n && a[0] == 1u && !song.playing && !seq_counting() && lists.song == SONG_ONCE)
            song_top();                                /* ((2.0) as the panel's PLAY: SONG ONCE from the top, */
        else if (n && a[0] == 1u && !song.playing && !seq_counting()) {
            chain_top();                               /* a chain from its first part) */
            transport_req = 1;
        }
        else if (n && a[0] >= 2u && a[0] <= 3u)
            transport_req = (uint8_t)a[0];
        ed_b(0);
        ed_b(song.playing != 0u);
        ed_b(seq_counting() != 0);
        ed_b(transport_req);
        ed_b(song.rec & 127u);
        ed_b(song.sel);
        return 1;
    }
    if (cmd != ED_PROJ_NAME)
        return 0;
    if (n) {
        ed_proj_name_in(a + 2, n - 2u, b);
        if (ed_flash_stop())
            rc = 1;
        else
            rc = (uint32_t)(a[0] == 1u ? project_save_as(a[1], b) : project_rename(a[1], b));
    }
    ed_b(n ? a[0] : 0u);
    ed_b(rc > 2u ? 2u : rc);
    ed_b(proj_cur < 4u ? proj_cur : 127u);
    ed_b(project_changed() != 0);
    project_cur_name(b);
    ed_str(b, PROJ_NAME_LEN);
    for (k = 0; k < 4u; k++) {
        ed_b(project_name(k, b));
        ed_str(b, PROJ_NAME_LEN);
    }
    return 1;
}
