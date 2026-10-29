/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Editor protocol: SysEx for the web editor (web/EDITOR_PROTOCOL.md; v2 = user presets + live sync,
 * v3 = four tracks (Skiff phase 6: six, INFO's NTRK): the v1 / v2 commands act on the selected track, cmds 27-30
 * reach any track;
 * v4 = TRACK_PARAM (31) and the TRACK_CHANGED push (32), enabled by WATCH bit 1).
 *   F0 7D 46 4C cmd args.. F7     (7D = non-commercial ID, "FL")
 * Values are 14 bit, two 7-bit bytes LSB first, offset by 8192 (so -8192..8191).
 * Every request gets a reply with the same cmd; 23/24/26 are also pushed
 * while watched. Frames arrive through sx_frame (usb.c), replies leave
 * through ota_wire_send(). */
#define ED_HDR0 0x7D
#define ED_HDR1 0x46
#define ED_HDR2 0x4C
enum { ED_INFO = 1, ED_GET, ED_SET, ED_DUMP, ED_DESC, ED_STEP_GET, ED_STEP_SET, ED_PRESET, ED_PROJECT, ED_NAMES,
       ED_SMP_BEGIN, ED_SMP_WRITE, ED_SMP_END, ED_SMP_ERASE, ED_SMP_INFO,
       ED_UP_LIST, ED_UP_GET, ED_UP_PUT, ED_UP_STORE, ED_UP_LOAD, ED_UP_ERASE,   /* v2: user presets */
       ED_WATCH, ED_CHANGED, ED_RELOAD, ED_PING, ED_STEP_CHANGED,              /* v2: live sync */
       ED_TRACK, ED_TRACK_MIX, ED_TRACK_DUMP, ED_TRACK_STEP,                    /* v3: tracks */
       ED_TRACK_PARAM, ED_TRACK_CHANGED, ED_SONG,
       ED_UI_STATE, ED_UI_SET, ED_UI_PALETTES, ED_FAV_GET, ED_FAV_SET, ED_COLORS,   /* ((Skiff #27) 39: THEME CUSTOM) */
       ED_MOTION = 64, ED_BACKUP_LIST, ED_BACKUP_GET, ED_BACKUP_PUT };                              /* v6: song chain */

enum { ED_KIT_GET = 74, ED_KIT_PUT };                 /* (Skiff W3) the DRUM-X kit (INFO 4B 01 8) */
enum { ED_SMP_MIX = 81 };                             /* (Skiff, #21) a user slot's sounds' LEVEL REV DLY (INFO 57 01 bit 5) */
#if FELUCCA_USB_TRACE
enum { ED_USB_TRACE = 82 }; /* read-only diagnostic build extension */
#endif

static uint8_t ed_out[600];
static uint32_t ed_n;

static void ed_begin(uint32_t cmd)
{
    ed_out[0] = 0xF0;
    ed_out[1] = ED_HDR0;
    ed_out[2] = ED_HDR1;
    ed_out[3] = ED_HDR2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
}
static __attribute__((noinline)) void ed_b(uint32_t v)   /* ((#21) a call: smaller than its copies) */
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_bs(const uint8_t *p, uint32_t n) { while (n--) ed_b(*p++); }   /* (Skiff 2.0) a table's bytes */
static void ed_v(int32_t v)
{
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    ed_b(u);
    ed_b(u >> 7);
}
static void ed_str(const char *s, uint32_t max)   /* ASCII, 0-terminated */
{
    uint32_t i;
    for (i = 0; s && s[i] && i < max; i++)
        ed_b((uint8_t)s[i] & 0x7Fu);
    ed_b(0);
}
static void ed_send(void)
{
    ed_out[ed_n++] = 0xF7;
    ota_wire_send(ed_out, ed_n);
}
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }

/* ---- user sample slots (eng_sample.c): flash SMP_USER_BASE + k * SMP_USER_SIZE ----
 * BEGIN erases the header sector (the slot is invalid from then on), WRITE fills the data
 * (offset >= 512, erasing each further sector when the write reaches its start), END sends
 * the header: the device checks the data CRC and writes the header last. */
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;                                 /* groups: msb byte, then up to 7 bytes */
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        uint32_t k = na > 7u ? 7u : na;
        if (!k || n + k > max || (m >> k))
            return 0;                              /* incomplete group, overflow, or unused mask bits */
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return na ? 0u : n;
}
static uint8_t ed_smp_buf[512] __attribute__((aligned(4)));
static uint32_t ed_smp_slot(uint32_t k) { return SMP_USER_BASE + k * SMP_USER_SIZE; }
static void ed_smp_inval(uint32_t k)
{
    fm1_irq_off();
    fl_inval(ed_smp_slot(k), SMP_USER_SIZE);
    fm1_irq_on();
}
static int ed_smp_erase(uint32_t k, uint32_t all)  /* header sector, or the whole slot */
{
    uint32_t i, took;
    int rc = 0;
#if BOARD_SMK37
    if (!SMP_USER_HAS(k))
        return -8;                                 /* SMK: USR2 / USR3 have no flash */
    st_stock_release();                            /* before the first erase in USRFLASH: storage.c */
#endif
    usr_nz[k] = 0;
    for (i = 0; i < 16u; i++)
        usr_zone[k][i].n = 0;                     /* a sounding voice ends instead of reading 0xFF */
    for (i = 0; i < (all ? SMP_USER_SIZE / 0x1000u : 1u) && !rc; i++) {
        audio_silence();
        rc = fl_erase4k(ed_smp_slot(k) + i * 0x1000u, &took);
        fm1_wdt_feed();
    }
    ed_smp_inval(k);
    return rc;
}
static int ed_flash_stop(void);
static int ed_smp_end(uint32_t k, const uint8_t *a, uint32_t na)
{
    const smp_user_hdr_t *h = (const smp_user_hdr_t *)ed_smp_buf;
#if BOARD_SMK37
    if (!SMP_USER_HAS(k))
        return 4;                                  /* SMK: USR2 / USR3 have no flash */
#endif
    if (ed_unpack7(a, na, ed_smp_buf, sizeof(smp_user_hdr_t)) != sizeof(smp_user_hdr_t))
        return 1;
    if (h->magic != SMP_USER_MAGIC || h->version != 1 || !h->nz || h->nz > 16u ||
        h->data_len > SMP_USER_SIZE - SMP_USER_DATA)
        return 2;
    if (usr_nz[k])                                     /* published zones may still be read by live voices */
        return memcmp(h, smp_user_xip(k), sizeof *h) ? 2 : 0;
    if (ed_flash_stop())                               /* (only a header that will be written stops the transport) */
        return 1;
    ed_smp_inval(k);
    if (st_crc32(smp_user_xip(k) + SMP_USER_DATA, h->data_len) != h->crc)
        return 3;
    if (fl_write(ed_smp_slot(k), ed_smp_buf, sizeof(smp_user_hdr_t)))
        return 4;
    ed_smp_inval(k);
    smp_user_scan(k);
    return usr_nz[k] ? 0 : 5;
}

/* the engine byte of DUMP / RELOAD / TRACK */
static uint32_t ed_eng(const track_t *t) { return t->eng_req % NENGINES; }

/* ---- live sync (v2): while the editor WATCHes, device-side changes are pushed.
 * Shadows of the selected track's p[] + song.g[] and of its steps are kept in step
 * with what the editor knows (its own SET / STEP_SET update them); the main loop
 * compares and pushes CHANGED / STEP_CHANGED, or RELOAD after a load or when another
 * track was selected (v3: RELOAD and STEP_CHANGED carry the selected track). Pushes
 * go out only into a half-empty SysEx ring, so they never wait. */
#define ED_PUSH_MAX 4u                                   /* frames per pass */
#define ED_NV (P_COUNT + G_COUNT)
/* (Skiff 1.3, INFO 49 01) a track parameter's address: scope 0 ids 0..127, scope 2 ids 128.. (id - 128): a SysEx byte
 * holds 7 bits and P_COUNT passed 127. ED_TID(scope, id): the P_* id of a track's; ED_SCOPE / ED_ID7: the address
 * of ed_w's value k (P_COUNT and up: a global) */
#define ED_TID(scope, id) ((scope) == 2u ? (id) + 128u : (id))
#define ED_SCOPE(k) ((k) < P_COUNT ? ((k) >> 7) << 1 : 1u)
#define ED_ID7(k) ((k) < P_COUNT ? (k) & 127u : (k) - P_COUNT)
/* v4: the parameters of the tracks that are not selected which TRACK_CHANGED follows (the mixer) */
static const uint8_t ED_TIDS[3] = {P_LEVEL, P_PAN, P_MUTE};
#define ED_NT (NTRK * 3u)
static struct {
    uint8_t on, eng, preset, sel;
    uint8_t v4;                                          /* WATCH bit 1: TRACK_CHANGED pushes too */
    uint32_t pos, last_ms, run_ms, resets;
    int16_t v[ED_NV];                                    /* TSEL->p[], then song.g[] */
    uint16_t t[ED_NV];                                   /* ms (low 16 bits) of the last push */
    uint32_t st[NSTEP];                                  /* step signatures */
    int16_t tv[ED_NT];                                   /* v4: trk[k].p[ED_TIDS[j]] at k * 3 + j */
    uint16_t tt[ED_NT];
    uint32_t tpos;
} ed_w;

static int16_t *ed_val(uint32_t i) { return i < P_COUNT ? &TSEL->p[i] : &song.g[i - P_COUNT]; }
static uint32_t ed_step_sig(const step_t *s)
{
    return ((uint32_t)s->note[0] | (uint32_t)s->note[1] << 7 | (uint32_t)s->note[2] << 14 | (uint32_t)s->note[3] << 21) ^
           ((uint32_t)s->n << 28) ^ ((uint32_t)s->time * 0x9E3779B1u) ^ ((uint32_t)s->flags * 0x85EBCA6Bu) ^
           ((uint32_t)s->vel * 0xC2B2AE35u) ^ ((uint32_t)(s->hit | s->acc << 8) * 0x27D4EB2Fu) ^ ((uint32_t)s->probability * 0x165667B1u) ^
           ((uint32_t)(s->cond | (uint8_t)s->nudge << 8) * 0x9E3779B9u);   /* ((Skiff W1) COND, NUDGE too) */
}
static void ed_shadow(void)                              /* the editor is in sync */
{
    uint32_t i;
    for (i = 0; i < ED_NV; i++)
        ed_w.v[i] = *ed_val(i);
    for (i = 0; i < NSTEP; i++)
        ed_w.st[i] = ed_step_sig(&TSEL->step[i]);
    for (i = 0; i < ED_NT; i++)
        ed_w.tv[i] = trk[i / 3u].p[ED_TIDS[i % 3u]];
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    ed_w.sel = song.sel;
    sync_reload = 0;
}
/* the editor's own sound load on the selected track (PRESET, SET of G_ENGSEL): the editor re-reads DUMP after
 * the reply, so the shadow takes the load (engine, preset, the parameters it changed) and no RELOAD echoes back
 * (INFO 53 01 bit 1). A RELOAD already due before it (a load or selection on the device) still goes out. */
typedef struct { int16_t p[P_COUNT]; uint8_t eng, preset, due; } ed_load_t;
static void ed_load_before(ed_load_t *b)
{
    memcpy(b->p, TSEL->p, sizeof b->p);
    b->eng = (uint8_t)ed_eng(TSEL);
    b->preset = TSEL->preset;
    b->due = sync_reload || b->eng != ed_w.eng || b->preset != ed_w.preset || song.sel != ed_w.sel;
}
static void ed_load_after(const ed_load_t *b)
{
    uint32_t i;
    if (!ed_w.on || b->due)
        return;
    sync_reload = 0;
    ed_w.eng = (uint8_t)ed_eng(TSEL);
    ed_w.preset = TSEL->preset;
    for (i = 0; i < P_COUNT; i++)                        /* the load's values; a pending push of another stays */
        if (TSEL->p[i] != b->p[i])
            ed_w.v[i] = TSEL->p[i];
}
static int ed_room(void) { return so_w - so_r + 8u <= SXQ / 2u; }
static void ed_known(uint32_t k, uint32_t id)            /* the editor's own change of trk[k].p[id]: no push */
{
    uint32_t j;
    if (k == song.sel) {
        ed_w.v[id] = trk[k].p[id];
        return;
    }
    for (j = 0; j < 3u; j++)
        if (ED_TIDS[j] == id)
            ed_w.tv[k * 3u + j] = trk[k].p[id];
}

static void ed_sync(void)                                /* main loop */
{
    uint32_t i, n = 0, now = fm1_ms;
    if (!ed_w.on || now - ed_w.run_ms < 5u)
        return;
    ed_w.run_ms = now;
    if (!usb.config || usb.resets != ed_w.resets || now - ed_w.last_ms > 3000u) {
        ed_w.on = 0;                                     /* no host, USB reset, or 3 s without a request */
        return;
    }
    if (sync_reload || ed_eng(TSEL) != ed_w.eng || TSEL->preset != ed_w.preset || song.sel != ed_w.sel) {
        if (!ed_room())
            return;
        ed_shadow();
        ed_begin(ED_RELOAD);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        ed_b(song.sel);
        ed_send();
        return;
    }
    for (i = 0; i < NSTEP && n < ED_PUSH_MAX; i++) {
        uint32_t h = ed_step_sig(&TSEL->step[i]);
        if (h == ed_w.st[i])
            continue;
        if (!ed_room())
            return;
        ed_w.st[i] = h;
        ed_begin(ED_STEP_CHANGED);
        ed_b(i);
        ed_b(song.sel);
        ed_send();
        n++;
    }
    for (i = 0; i < ED_NV && n < ED_PUSH_MAX; i++) {    /* round robin: nothing starves */
        uint32_t k = (ed_w.pos + i) % ED_NV;
        int16_t v = *ed_val(k);
        if (v == ed_w.v[k] || (uint16_t)(now - ed_w.t[k]) < 20u)
            continue;                                    /* coalesced: the latest value goes out later */
        if (!ed_room())
            return;
        ed_w.v[k] = v;
        ed_w.t[k] = (uint16_t)now;
        ed_begin(ED_CHANGED);
        ed_b(ED_SCOPE(k));
        ed_b(ED_ID7(k));
        ed_v(v);
        ed_send();
        n++;
        ed_w.pos = k + 1u;
    }
    for (i = 0; ed_w.v4 && i < ED_NT && n < ED_PUSH_MAX; i++) {   /* v4: the other tracks' mix */
        uint32_t k = (ed_w.tpos + i) % ED_NT, tr = k / 3u, id = ED_TIDS[k % 3u];
        int16_t v = trk[tr].p[id];
        if (tr == song.sel || v == ed_w.tv[k] || (uint16_t)(now - ed_w.tt[k]) < 20u)
            continue;                                    /* (the selected track: CHANGED above) */
        if (!ed_room())
            return;
        ed_w.tv[k] = v;
        ed_w.tt[k] = (uint16_t)now;
        ed_begin(ED_TRACK_CHANGED);
        ed_b(tr);
        ed_b(id);
        ed_v(v);
        ed_send();
        n++;
        ed_w.tpos = k + 1u;
    }
}

/* descriptor of parameter id of track t: the engine parameters of the engine it asked for
 * (t->engine follows in the audio ISR, after a short fade) */
static const param_desc_t *ed_tdesc(const track_t *t, uint32_t id) { return param_desc_of(t->eng_req % NENGINES, id); }
/* descriptor and value slot of (scope, id): scope 0 / 2 = the selected track, 1 = global */
static const param_desc_t *ed_desc(uint32_t scope, uint32_t id, int16_t **vp)
{
    if (scope != 1u) {
        if (scope > 2u || (id = ED_TID(scope, id)) >= P_COUNT)
            return 0;
        *vp = &TSEL->p[id];
        return ed_tdesc(TSEL, id);
    }
    if (scope == 1 && id < G_COUNT) {
        *vp = &song.g[id];
        return &GP[id];
    }
    return 0;
}

/* a step as STEP_SET / TRACK_STEP send it (na bytes): n, 4 notes, time, flags, vel, then (v5) the lane hits
 * and their accents: hit & 127, acc & 127, bit 7 of each (bit 0 hit, bit 1 acc), (v7) the chance, (RATCH, INFO
 * 52 01) the hits 1..4, (Skiff W1, INFO 43 01) FLAM 0/1, COND (TC_*), NUDGE + 32 (0..63); each value made valid.
 * 8 bytes (an editor of before the grid): the hits stay; without the chance, the ratchet or the rest the step keeps
 * its own (flags carry accent and slide only). (Skiff W1) The ratchet is set only when it changes: an editor of
 * before writes x1 back on a FLAM step, which kept its FLAM no longer. (2.0, #25, INFO 57 01 bit 6) Then its ties (bit
 * k: note k goes on from the step before; none on a step with hits or a TIE); a write without them (an editor of
 * before) leaves a step without hits none: that editor's step is whole */
static void ed_step_put(step_t *st, const uint8_t *a, uint32_t na)
{
    uint32_t i;
    st->n = (uint8_t)(a[0] > 4u ? 4u : a[0]);
    for (i = 0; i < 4u; i++)
        st->note[i] = a[1 + i] & 0x7Fu;
    st->time = (uint8_t)(a[5] > ST_REST ? ST_REST : a[5]);
    st->flags = (uint8_t)((a[6] & (SF_ACCENT | SF_SLIDE)) | (st->flags & (SF_RATCH | SF_FLAM)));   /* ((Skiff) FLAM kept) */
    st->vel = a[7] & 0x7Fu;
    if (na >= 11u) {
        st->hit = (uint8_t)((a[8] & 0x7Fu) | (a[10] & 1u) << 7);
        st->acc = (uint8_t)(((a[9] & 0x7Fu) | (a[10] & 2u) << 6) & st->hit);
    }
    if (na >= 12u) step_set_chance(st, a[11] <= 100u ? a[11] : 100u);
    if (na >= 13u && a[12] != step_ratchet(st)) step_set_ratchet(st, a[12]);
    if (na >= 16u) {                                  /* (Skiff W1) FLAM (on x1 only), COND, NUDGE */
        if (a[13] && step_ratchet(st) == 1u)
            st->flags |= SF_FLAM;
        else
            st->flags &= (uint8_t)~SF_FLAM;
        st->cond = (uint8_t)(a[14] < TC_COUNT ? a[14] : 0u);
        st->nudge = (int8_t)((a[15] > 63u ? 63 : (int32_t)a[15]) - 32);
    }
    if (!st->hit)                                     /* ((2.0, #25) acc there: the ties) */
        st->acc = (uint8_t)(na >= 17u && st->time != ST_TIE ? a[16] & ((1u << st->n) - 1u) : 0u);
    ui.force = 1;
}
static void ed_step_reply(const step_t *st)              /* the same 17 bytes */
{
    uint32_t i;
    ed_b(st->n);
    for (i = 0; i < 4u; i++)
        ed_b(st->note[i]);
    ed_b(st->time);
    ed_b(st->flags & (SF_ACCENT | SF_SLIDE));
    ed_b(st->vel);
    ed_b(st->hit & 0x7Fu);
    ed_b(st->acc & st->hit & 0x7Fu);                     /* ((2.0) the hits' accents, not the ties acc holds without */
    ed_b((uint32_t)(st->hit >> 7) | (uint32_t)(st->acc >> 7) << 1);   /* hits: the editor's step (#25)) */
    ed_b(step_chance(st));
    ed_b(step_ratchet(st));
    ed_b((st->flags & SF_FLAM) != 0u);                   /* (Skiff W1, INFO 43 01) FLAM, COND, NUDGE + 32 */
    ed_b(st->cond % TC_COUNT);
    ed_b((uint32_t)(st->nudge + 32) & 63u);
    ed_b(step_ties(st));                                 /* ((2.0, #25, INFO 57 01 bit 6) its ties */
}

/* a flash write from the editor while the transport runs: stop it first (the erase silences the audio and
 * stalls the sequencer; the device's own SAVE refuses with "STOP TO SAVE", the editor's confirmed save stops) */
static int ed_flash_stop(void)
{
    uint32_t t0 = fm1_ms;
    if (!transport_busy())
        return 0;
    transport_req = 2;
    while (song.playing && fm1_ms - t0 < 100u)       /* the audio ISR stops it at its next block */
        fm1_wdt_feed();
    return song.playing;
}

#include "editor_preferences.c"
#include "editor_backup.c"
#include "editor_fm6.c"
#include "editor_menu.c"
#include "editor_project.c"                         /* (Skiff W3) TRANSPORT, PROJ_NAME */
#include "editor_song.c"                            /* (Skiff W3) POOL, LIST, PERFORM */

/* MOTION's reply: the records of track k as (step, id, v14), a lock's id without its MOTION_LOCK bit (a 7-bit SysEx
 * byte); kinds (1.1: the ops 5..7): then one byte per record, in the same order, 0 automation, 1 lock. The query and
 * the ops 1..4 reply as before 1.1 (an editor of before reads a lock as an automation event) */
#define ED_MID(a) ((a)[3] | ((a)[2] & 64u) << 1)      /* ((1.3) a MOTION op's id: bit 7 in its step byte's bit 6) */
static void ed_motion_reply(uint32_t k, uint32_t rc, uint32_t kinds)
{
    track_t *t = &trk[k];
    uint32_t i;
    ed_b(k); ed_b(rc); ed_b(motion_enabled(t)); ed_b(motion_count(t)); ed_b(MOTION_MAX);
    for (i = 0; i < motion.count; i++) {
        const motion_event_t *e = &motion.event[i];
        if ((e->place >> 6) != k) continue;
        ed_b((e->place & 63u) | (MOTION_ID(e) >> 7) << 6); ed_b(MOTION_ID(e)); ed_v(e->value);   /* ((1.3) id bit 7) */
    }
    for (i = 0; kinds && i < motion.count; i++)
        if ((motion.event[i].place >> 6) == k)
            ed_b(motion.event[i].lock);
}
static int ed_args_ok(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    switch (cmd) {
#if FELUCCA_USB_TRACE
    case ED_USB_TRACE:
        return !n;
#endif
    case ED_INFO: case ED_DUMP: case ED_SMP_INFO: case ED_PING:
    case ED_UI_STATE: case ED_UI_PALETTES:
        return !n;
    case ED_GET: case ED_DESC: case ED_PRESET: case ED_PROJECT: case ED_UP_LIST:
    case ED_TRACK_PARAM:
        return n == 2u || (cmd == ED_TRACK_PARAM && n == 4u);
    case ED_SET:
        return n == 4u;
    case ED_STEP_GET: case ED_NAMES: case ED_SMP_BEGIN: case ED_SMP_ERASE:
    case ED_UP_GET: case ED_UP_LOAD: case ED_UP_ERASE: case ED_WATCH: case ED_TRACK_DUMP: case ED_MENU_DESC:
        return n == 1u;
    case ED_MENU_SET:
        return n == 3u;
    case ED_TRANSPORT: case ED_PROJ_NAME:
        return ed_project_args_ok(cmd, a, n);
    case ED_POOL: case ED_LIST: case ED_PERFORM: case ED_LIST2:
        return ed_song_args_ok(cmd, a, n);
    case ED_KIT_GET:                                   /* (Skiff W3) a lane; a lane and its 15 values, each in range */
        return n == 1u && a[0] < 8u;
    case ED_KIT_PUT: {
        uint32_t i;
        if (n != 16u || a[0] >= 8u || a[1] > 3u || a[2] > DXF_HP || a[3] > 1u || a[4] > 3u || a[5] > 1u)
            return 0;
        for (i = 6; i < 16u; i++)
            if (a[i] > 127u)
                return 0;
        return 1;
    }
    case ED_STEP_SET:
        return n == 9u || n == 12u || (n == 13u && a[12] <= 100u) ||               /* notes only, or the complete grid extension, */
               (n == 14u && a[12] <= 100u && a[13] >= 1u && a[13] <= 4u) ||     /* its chance, its ratchet, */
               ((n == 17u || (n == 18u && a[17] <= 15u)) && a[12] <= 100u && a[13] >= 1u && a[13] <= 4u &&
                a[14] <= 1u && a[15] < TC_COUNT && a[16] <= 63u);               /* (Skiff W1) FLAM, COND, NUDGE,
                                                                                 * (2.0, #25) its ties */
    case ED_TRACK:
        return n <= 1u;
    case ED_TRACK_MIX:
        return n == 1u || n == 4u;
    case ED_TRACK_STEP:                                /* (a get, or the track and a STEP_SET's) */
        return n == 2u || ed_args_ok(ED_STEP_SET, a + 1, n - 1u);
    case ED_MOTION:
        return (n == 1u || (n == 2u && (a[1] == 2u || a[1] == 7u)) || (n == 3u && a[1] == 1u && a[2] <= 1u) ||
                (n == 6u && (a[1] == 3u || a[1] == 5u)) || (n == 4u && a[1] == 4u) || (n == 3u && a[1] == 6u)) &&
               a[0] < NTRK;
    case ED_SONG:
        return n && (a[0] == 1u || n == 1u);
    default:
        return 1;                                  /* variable payloads validate in their handler */
    }
}

/* (#21) SMP_MIX (ed_service: the frame f, n bytes between F0 and F7): slot [, 48 values: LEVEL x 16, REV x 16, DLY x 16]
 * -> slot, rc (0, 1 an empty slot: nothing set, 2 set but not kept: the sample fills the slot's last sector), nz, nz x
 * (lo, hi), the 48 values. Other arguments: no reply. (Out of ed_handle: a line there grows it by ~200 B) */
static void ed_smp_mix(const uint8_t *f, uint32_t n)
{
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u, k = a[0], rc = 0, i;
    int16_t *x;
    if (k >= SMP_USER_SLOTS || (na != 1u && na != 49u))
        return;
    ed_begin(ED_SMP_MIX);
    x = usr_mix[k][0];
    if (na > 1u) {
        rc = !usr_nz[k];
        if (!rc) {
            for (i = 0; i < 48u; i++)
                x[i] = a[1u + i];                          /* (SysEx bytes: 0..127) */
            smx_save |= (uint8_t)(1u << k);                /* (slice_store.c, once stopped) */
            rc = ((const smp_user_hdr_t *)smp_user_xip(k))->data_len > SMP_USER_SIZE - 0x1000u - SMP_USER_DATA ? 2u : 0u;
            ui.force = 1;
        }
    }
    ed_b(k);
    ed_b(rc);
    ed_b(usr_nz[k]);
    for (i = 0; i < usr_nz[k]; i++) {
        ed_b(usr_zone[k][i].lo);
        ed_b(usr_zone[k][i].hi);
    }
    for (i = 0; i < 48u; i++)
        ed_b(x[i]);
    ed_send();
}

static void ed_handle(const uint8_t *f, uint32_t n)   /* f: the bytes between F0 and F7 */
{
    if (n < 4u)
        return;
    uint32_t cmd = f[3], i;
    const uint8_t *a = f + 4;
    uint32_t na = n - 4u;
    int16_t *vp;
    const param_desc_t *d;
    if (!ed_args_ok(cmd, a, na))
        return;
    if ((cmd >= ED_SMP_BEGIN && cmd <= ED_SMP_INFO) || (cmd >= ED_BACKUP_LIST && cmd <= ED_BACKUP_PUT))
        autosave_hold();                                /* (a transfer: no autosave meanwhile, project.c) */
    ed_begin(cmd);
    if (ed_ui_handle(cmd, a, na)) { ed_send(); return; }
    if (ed_backup_handle(cmd, a, na)) { ed_send(); return; }
    if (ed_fm6_handle(cmd, a, na)) { ed_send(); return; }
    if (ed_menu_handle(cmd, a, na)) { ed_send(); return; }
    if (ed_project_handle(cmd, a, na)) { ed_send(); return; }
    if (ed_song_handle(cmd, a, na)) { ed_send(); return; }
    switch (cmd) {
#if FELUCCA_USB_TRACE
    case ED_USB_TRACE: {
        _Static_assert(sizeof(usb_trace_entry_t) == 9u && sizeof(usb_trace_t) == 296u,
                       "USB trace wire layout is fixed for pi32v2");
        _Static_assert(1u + 294u + 294u / 7u <= sizeof ed_out - 6u,
                       "USB trace reply fits editor SysEx buffer");
        /* Main-loop reader: USB writes only from interrupt service. Copy with
         * IRQs off, then encode/send outside the critical section. */
        static usb_trace_t snapshot;
        fm1_irq_off();
        snapshot = usb_trace;
        fm1_irq_on();
        ed_b(2); /* fixed wire layout; omit two trailing alignment bytes */
        ed_bk_pack((const uint8_t *)&snapshot, 294u);
        break;
    }
#endif
    case ED_MOTION: {
        track_t *t = &trk[a[0]]; uint32_t rc = 0;
        if (na > 1u && a[1] == 7u) {
            /* (the query with the kinds) */
        } else if (na > 1u) {
            if (a[1] == 1u) motion_set_enabled(t, a[2]);
            else if (a[1] == 2u) { load_begin(t, UNDO_PAT); motion_clear(t); load_end(t); }
            else if (a[1] == 3u) rc = motion_set_event(t, a[2] & 63u, ED_MID(a), (int16_t)ed_rv(a + 4));
            else if (a[1] == 4u && motion_param(ED_MID(a))) motion_delete_event(t, a[2] & 63u, ED_MID(a));
            else if (a[1] == 5u) rc = motion_set_lock(t, a[2] & 63u, ED_MID(a), (int16_t)ed_rv(a + 4));
            else if (a[1] == 6u && (a[2] < NSTEP || a[2] == 127u)) (void)motion_clear_locks(t, a[2]);
            else rc = 1;
            ui.force = 1;
        }
        ed_motion_reply(a[0], rc, na > 1u && a[1] >= 5u);
        break;
    }
    case ED_INFO: {
        /* the tags after the engines (tag, version, its bytes): (Skiff 2.0) their constant runs as tables */
        static const uint8_t TAGS1[] = {
            0x4d, 1, MOTION_MAX, 1,                     /* motion + chance v1 */
            0x42, 1, 3,                                 /* bounded full-backup read + restore */
            0x46, 1, FM6_NFACTORY, 0,                   /* FM6 patches: cmds 68..71 (no bank since 1.0.3) */
            0x53, 1, 3,                                 /* live sync: bit 0 WATCH while on keeps the shadow, bit 1 no
                                                         * RELOAD echo */
            0x50, 1, 3,                                 /* FM6 patches v2: bit 0 no bank (SLOT F1..F8, 8 OWN), bit 1
                                                         * user preset patches (FM6 target 3, backup id 9) */
            0x4E, 1, ED_MENU_N,                         /* MENU settings: cmds 72, 73; the items MENU_DESC offers */
            0x52, 1, 4,                                 /* RATCH: a step's ratchet (1..4 hits) after its chance */
            0x4C, 1, 1};                                /* 1.1 parameter locks: MOTION ops 5..7, the kinds after the
                                                         * records */
        static const uint8_t TAGS2[] = {
            0x43, 1, TC_COUNT,                          /* (Skiff W1) a step's FLAM, COND (TC_COUNT of them), NUDGE
                                                         * after its ratchet */
            0x4B, 1, 8,                                 /* (Skiff W3) the DRUM-X kit: KIT_GET, KIT_PUT, its 8 lanes */
            0x57, 1, ED_SKIFF_CAPS,                     /* (Skiff W3) TRANSPORT, PROJ_NAME (editor_project.c) */
            0x49, 1, P_COUNT & 127u, P_COUNT >> 7, P_E0 & 127u, P_E0 >> 7,   /* (Skiff 1.3) wide ids: P_COUNT and
                                         * P_E0 in two bytes; a track id from 128 on as scope 2 (ED_TID), a motion
                                         * record's from 128 on with bit 6 of its step byte */
            0x58, 1, ED_SONG2};                         /* (2.0) the song's LIST2, NEW, COPY+, CHAIN, ONCE, the pool's
                                                         * EMPTY, CLEAR UNUSED (editor_song.c; last: an editor before
                                                         * reads up to 49 01 only) */
        ed_str("FELUCCA " FELUCCA_VERSION, 24);
        ed_b(NENGINES);
        ed_b(P_COUNT > 127u ? 127u : P_COUNT);            /* ((1.3) more: INFO 49 01) */
        ed_b(G_COUNT);
        ed_b(NSTEP);
        ed_b(P_E0 > 127u ? 127u : P_E0);
        for (i = 0; i < NENGINES; i++)
            ed_str(ENGINES[i]->name, 8);
        ed_b(NTRK);                                       /* v3 */
        ed_b(CHAIN_ROWS);                                  /* v6 (an editor reads the tags after it only then) */
        ed_b(0x55); ed_b(1); ed_b(ed_ui_caps());             /* tagged preferences v1: commands 34..38 */
        ed_bs(TAGS1, sizeof TAGS1);
        {   /* (Skiff phase 6) light tracks: 54 01, the tracks past NHEAVY (a bit each), then the engines they may not
             * play (21 bits, 7 a byte: engines.c eng_allowed) */
            uint32_t tm = 0, em = 0;
            for (i = NHEAVY; i < NTRK; i++)
                tm |= 1u << i;
            for (i = 0; i < NENGINES; i++)
                em |= (uint32_t)!eng_allowed(NHEAVY, i) << i;
            ed_b(0x54); ed_b(1); ed_b(tm); ed_b(em & 127u); ed_b(em >> 7 & 127u); ed_b(em >> 14 & 127u);
        }
        ed_bs(TAGS2, sizeof TAGS2);
        break;
    }
    case ED_GET:
    case ED_SET:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        if (cmd == ED_SET && na >= 4u) {
            if (a[0] == 1 && a[1] == G_ENGSEL) {          /* engine change: the safe path (1 without FELUCCA_FM4:
                                                           * DIGITAL's first preset, as FM6) */
                ed_load_t b;
                ed_load_before(&b);
                set_engine((uint32_t)clamp(ed_rv(a + 2), 0, NENGINES - 1));
                ed_load_after(&b);
            } else if (d->max > d->min) {
                *vp = (int16_t)enum_orig(d, clamp(ed_rv(a + 2), d->min, d->max));
                if (a[0] != 1u) {
                    (void)motion_capture(TSEL, ED_TID(a[0], a[1]), *vp);
                    load_extend(TSEL);                      /* (ui.c undo: an audition's values after G_ENGSEL) */
                }
            }
            ui.force = 1;
            ed_w.v[a[0] == 1u ? P_COUNT + a[1] : ED_TID(a[0], a[1])] = *vp;   /* the editor's own change: no push */
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_v(*vp);
        break;
    case ED_DUMP:
        for (i = 0; i < ED_NV; i++)                       /* the editor gets them all here */
            ed_w.v[i] = *ed_val(i);
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        for (i = 0; i < P_COUNT; i++)
            ed_v(motion_base_value(TSEL, i));
        for (i = 0; i < G_COUNT; i++)
            ed_v(song.g[i]);
        break;
    case ED_DESC:
        if (na < 2u || !(d = ed_desc(a[0], a[1], &vp)))
            return;
        ed_b(a[0]);
        ed_b(a[1]);
        ed_b(d->fmt);
        ed_v(d->min);
        ed_v(d->max);
        ed_v(d->def);
        ed_str(d->label, 8);
        ed_str(PD_UNIT_OF(d), 8);
        if (d->fmt == F_ENUM && d->names)                  /* up to 24 (the matrix DST has 20) */
            for (i = 0; i <= (uint32_t)(d->max - d->min) && i < 24u; i++)
                ed_str(d->names[i], 8);
        break;
    case ED_STEP_GET:
    case ED_STEP_SET: {
        step_t *st;
        if (na < 1u || a[0] >= NSTEP)
            return;
        st = &TSEL->step[a[0]];
        if (cmd == ED_STEP_SET && na >= 9u) {
            ed_step_put(st, a + 1, na - 1u);
        }
        ed_w.st[a[0]] = ed_step_sig(&TSEL->step[a[0]]);
        ed_b(a[0]);
        ed_step_reply(&TSEL->step[a[0]]);
        break;
    }
    case ED_PRESET: {                                      /* engine, preset */
        ed_load_t b;
        if (na < 2u || a[0] >= NENGINES)
            return;
        ed_load_before(&b);
#if !FELUCCA_FM4 && FELUCCA_LEGACY
        if (a[0] == ENGI_DIGITAL)                          /* a DIGITAL preset (retired): its sound, as FM6 ((2.0) */
            fm4_load_preset(TSEL, a[1]);                   /* FM6's preset a[1]: set_engine_of) */
        else
#endif
        {
            if (a[0] == TSEL->eng_req || !set_engine(a[0]))   /* ((Skiff phase 6) refused: not its preset either) */
                apply_preset(a[1]);
        }
        ed_load_after(&b);
        ui.force = 1;
        ed_b(ed_eng(TSEL));
        ed_b(TSEL->preset);
        break;
    }
    case ED_PROJECT:                                       /* 0 = load, 1 = save, 2 = query; slot 0..3 */
        if (na < 2u || a[0] > 2u || a[1] >= 4u)
            return;
        if (a[0] == 1u) {
            if (ed_flash_stop() || project_save(a[1]))
                return;                              /* the legacy reply has no failure bit: do not confirm a failed save */
        }
        else if (a[0] == 0u)
            project_load(a[1] & 3u);
        ed_b(a[0]);
        ed_b(a[1] & 3u);
        ed_b(project_used(a[1] & 3u));
        break;
    case ED_NAMES:                                         /* preset names of an engine */
        if (na < 1u || a[0] >= NENGINES)
            return;
        ed_b(a[0]);
        ed_b(ENGINES[a[0]]->npresets);
        for (i = 0; i < ENGINES[a[0]]->npresets; i++)
            ed_str(ENGINES[a[0]]->presets[i].name, 12);
        for (i = 0; i < 2u; i++)                           /* then the two edit-page titles */
            ed_str(ENGINES[a[0]]->page_title[i], 8);
        break;
    case ED_SMP_BEGIN:                                     /* slot -> slot, rc */
    case ED_SMP_ERASE:
        if (na < 1u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b(ed_flash_stop() || ed_smp_erase(a[0], cmd == ED_SMP_ERASE) ? 1u : 0u);
        break;
    case ED_SMP_WRITE: {                                   /* slot, off (3 x 7 bit), pack7 data -> slot, off, rc */
        uint32_t off, len, rc = 0, took;
        if (na < 5u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        off = (uint32_t)a[1] | (uint32_t)a[2] << 7 | (uint32_t)a[3] << 14;
        len = ed_unpack7(a + 4, na - 4u, ed_smp_buf, 256u);
        if (off < SMP_USER_DATA || (off & 0xFFu) || !len || off + len > SMP_USER_SIZE)
            rc = 1;
        else if (usr_nz[a[0]] || !SMP_USER_HAS(a[0]))
            rc = 4;                                        /* slot in use: SMP_BEGIN first (voices read it) */
        else if (ed_flash_stop())
            rc = 1;
        else {
            if (!(off & 0xFFFu)) {                         /* first write into a sector: erase it */
                audio_silence();
#if BOARD_SMK37
                st_stock_release();                        /* (storage.c: the stock mapping's marker) */
#endif
                rc = fl_erase4k(ed_smp_slot(a[0]) + off, &took) ? 2u : 0u;
            }
            if (!rc && fl_write(ed_smp_slot(a[0]) + off, ed_smp_buf, len))
                rc = 3;
        }
        ed_b(a[0]);
        ed_b(off);
        ed_b(off >> 7);
        ed_b(off >> 14);
        ed_b(rc);
        break;
    }
    case ED_SMP_END:                                       /* slot, pack7 header -> slot, rc */
        if (na < 2u || a[0] >= SMP_USER_SLOTS || !flash_ok)
            return;
        ed_b(a[0]);
        ed_b((uint32_t)ed_smp_end(a[0], a + 1, na - 1u));
        break;
    case ED_SMP_INFO:                                      /* -> per slot: zones (0 = empty), name, data KiB */
        ed_b(SMP_SHOWN);
        ed_b(SMP_USER_SIZE / 1024u);
        for (i = 0; i < SMP_SHOWN; i++) {
            const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(i);
            char nm[9];
            ed_b(usr_nz[i]);
            smp_user_name(i, nm);
            ed_str(nm, 8);
            ed_b(usr_nz[i] ? (h->data_len + 1023u) / 1024u : 0u);
        }
        break;
    case ED_UP_LIST: {                                     /* start, count -> start, count, total, per slot: used, engine, name */
        uint32_t s0, cnt;
        if (na < 2u)
            return;
        s0 = a[0];
        cnt = a[1] > 16u ? 16u : a[1];
        if (s0 >= UP_SLOTS)
            cnt = 0;
        else if (s0 + cnt > UP_SLOTS)
            cnt = UP_SLOTS - s0;
        ed_b(s0);
        ed_b(cnt);
        ed_b(UP_SLOTS);
        for (i = s0; i < s0 + cnt; i++) {
            char nm[13] = {0};
            uint32_t j, u = (uint32_t)up_used(i);
            for (j = 0; u && j < 12u; j++)
                nm[j] = up_rec(i)->name[j];
            ed_b(u);
            ed_b(u ? up_rec(i)->engine : 0u);
            ed_str(nm, 12);
        }
        break;
    }
    case ED_UP_GET: {                                      /* slot -> slot, used, engine, name, P_COUNT x v14, 16 x (note,
                                                            * flags), (v5) kind [, 16 x hi] (upreset.c up_parse) */
        int16_t v[P_COUNT];
        char nm[13] = {0};
        const up_rec_t *r;
        uint32_t u;
        if (na < 1u || a[0] >= UP_SLOTS)
            return;
        r = up_rec(a[0]);
        u = (uint32_t)up_used(a[0]);
        if (u)
            up_values(r, v);                               /* today's P_* order, clamped */
        for (i = 0; u && i < 12u; i++)
            nm[i] = r->name[i];
        ed_b(a[0]);
        ed_b(u);
        ed_b(u ? r->engine : 0u);
        ed_str(nm, 12);
        for (i = 0; i < P_COUNT; i++)
            ed_v(u ? v[i] : 0);
        for (i = 0; i < 16u; i++) {                        /* ((2.0) a version 6 record: none, rests) */
            ed_b(u && up_pat(r) ? r->note[i] & 0x7Fu : 0u);
            ed_b(u && up_pat(r) ? r->flags[i] & 0x7Fu : 0u);
        }
        ed_b(u && up_grid(r));                  /* kind: 1 = a drum grid, then bit 7 of each step's */
        for (i = 0; u && up_grid(r) && i < 16u; i++)   /* hits (bit 0) and accents (bit 1) */
            ed_b((uint32_t)(r->note[i] >> 7) | (uint32_t)(r->flags[i] >> 7) << 1);
        break;
    }
    case ED_UP_PUT: {                                      /* slot, engine, name, values, pattern -> slot, rc */
        static up_rec_t r;
        uint32_t slot = 0, rc;
        if (na < 1u)
            return;
        rc = (uint32_t)up_parse(a, na, &r, &slot);
        if (!rc && ed_flash_stop())
            rc = 2;
        if (!rc) {
            int16_t v[P_COUNT];
            up_values(&r, v);                              /* each value inside its range */
            up_set_sound(&r, v);                           /* ((2.0) stored as version 6: by id, its pattern not) */
            rc = up_put(slot, &r) ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_STORE: {                                    /* slot, name -> slot, rc */
        char nm[13] = {0};
        uint32_t n0, rc = 1;
        if (na < 1u)
            return;
        for (n0 = 0; 1u + n0 < na && a[1 + n0] && n0 < 13u; n0++)
            ;
        if (a[0] < UP_SLOTS && (!n0 || up_name_ok(a + 1, n0)) &&
            (1u + n0 == na || (2u + n0 == na && !a[1u + n0]))) {   /* "" = automatic name */
            for (i = 0; i < n0; i++)
                nm[i] = (char)a[1 + i];
            int r;
            r = ed_flash_stop() ? 2 : up_store(a[0], nm);
            rc = r ? 2u : 0u;
        }
        ed_b(a[0]);
        ed_b(rc);
        break;
    }
    case ED_UP_LOAD:                                       /* slot -> slot, rc */
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] < UP_SLOTS && !up_load(a[0]) ? 0u : 1u);
        break;
    case ED_UP_ERASE:
        if (na < 1u)
            return;
        ed_b(a[0]);
        ed_b(a[0] >= UP_SLOTS ? 1u : ed_flash_stop() || up_put(a[0], 0) ? 2u : 0u);
        break;
    case ED_WATCH: {                                       /* on -> on */
        uint32_t keep, v4was = ed_w.v4;
        if (na < 1u)
            return;
        keep = ed_w.on && ed_w.resets == usb.resets && (a[0] & 1u);   /* already watching: the changes not yet */
        ed_w.on = a[0] & 1u;                                          /* pushed stay pending (INFO 53 01 bit 0) */
        ed_w.v4 = (uint8_t)(ed_w.on && (a[0] & 2u));     /* v4: also TRACK_CHANGED; the reply says it is known */
        ed_w.resets = usb.resets;
        if (ed_w.on && !keep)
            ed_shadow();
        else if (keep && ed_w.v4 && !v4was)
            for (i = 0; i < ED_NT; i++)                    /* TRACK_CHANGED newly asked for: from the mix as it is */
                ed_w.tv[i] = trk[i / 3u].p[ED_TIDS[i % 3u]];
        ed_b(ed_w.on | ed_w.v4 << 1);
        break;
    }
    case ED_PING:
        ed_b(0);
        break;
    case ED_TRACK:                                         /* [track] -> selected, NTRK, per track: engine, preset, level, mute, armed */
        if (na >= 1u && a[0] < NTRK && a[0] != song.sel) {
            track_select(a[0]);
            if (ed_w.on)
                ed_shadow();                               /* the editor re-reads it: no RELOAD for this */
        }
        ed_b(song.sel);
        ed_b(NTRK);
        for (i = 0; i < NTRK; i++) {
            ed_b(ed_eng(&trk[i]));
            ed_b(trk[i].preset);
            ed_v(trk[i].p[P_LEVEL]);
            ed_b(trk[i].p[P_MUTE] != 0);
            ed_b((song.rec >> i) & 1u);
        }
        break;
    case ED_TRACK_MIX: {                                   /* track [, level v14, mute] -> track, level, mute */
        track_t *t;
        int16_t *lv;
        if (na < 1u || a[0] >= NTRK)
            return;
        t = &trk[a[0]];
        lv = &t->p[P_LEVEL];
        if (na >= 4u) {
            *lv = (int16_t)clamp(ed_rv(a + 1), 0, 127);
            t->p[P_MUTE] = (int16_t)(a[3] ? 1 : 0);
            ed_known(a[0], P_LEVEL);                       /* the editor's own change: no push */
            ed_known(a[0], P_MUTE);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_v(*lv);
        ed_b(t->p[P_MUTE] != 0);
        break;
    }
    case ED_TRACK_DUMP:                                    /* track -> track, engine, preset, P_COUNT x v14 */
        if (na < 1u || a[0] >= NTRK)
            return;
        ed_b(a[0]);
        ed_b(ed_eng(&trk[a[0]]));
        ed_b(trk[a[0]].preset);
        for (i = 0; i < P_COUNT; i++)
            ed_v(motion_base_value(&trk[a[0]], i));
        break;
    case ED_KIT_GET:                                       /* (Skiff W3) lane -> lane, wave, filter, snap, pitch mod, */
    case ED_KIT_PUT: {                                     /* drive, A's five, B's five (KIT_PUT: those set, then the same) */
        dx_lane_t *L = &dx_kit[a[0] & 7u];
        uint32_t i;
        if (cmd == ED_KIT_PUT) {
            L->mode = DX_MODE(a[1], a[2], a[3]) | (uint8_t)(a[4] << 5) | (a[5] ? DX_DRIVE : 0u);
            for (i = 0; i < DXP_N; i++) {
                L->a[i] = a[6 + i];
                L->b[i] = a[6 + DXP_N + i];
            }
            ui.force = 1;
        }
        ed_b(a[0] & 7u);
        ed_b(L->mode & 3u); ed_b((L->mode >> 2) & 3u); ed_b((L->mode >> 4) & 1u); ed_b((L->mode >> 5) & 3u);
        ed_b((L->mode & DX_DRIVE) != 0u);
        for (i = 0; i < DXP_N; i++)
            ed_b(L->a[i]);
        for (i = 0; i < DXP_N; i++)
            ed_b(L->b[i]);
        break;
    }
    case ED_TRACK_STEP: {                                  /* track, index [, step] -> track, index, step (as STEP_GET) */
        step_t *st;
        if (na < 2u || a[0] >= NTRK || a[1] >= NSTEP)
            return;
        st = &trk[a[0]].step[a[1]];
        if (na >= 10u)
            ed_step_put(st, a + 2, na - 2u);
        if (a[0] == song.sel)
            ed_w.st[a[1]] = ed_step_sig(st);
        ed_b(a[0]);
        ed_b(a[1]);
        ed_step_reply(st);
        break;
    }
    case ED_TRACK_PARAM: {                                 /* track, id [, v14] -> track, id, v14 ((1.3) an id from
                                                            * 128 on: bit 6 of the track byte is its bit 7) */
        track_t *t;
        uint32_t id = a[1] | (a[0] & 64u) << 1;
        if (na < 2u || (a[0] & 63u) >= NTRK || id >= P_COUNT)
            return;
        t = &trk[a[0] & 63u];
        d = ed_tdesc(t, id);
        if (na >= 4u) {
            if (d->max > d->min)                           /* as SET: clamped; a fixed value stays */
                t->p[id] = (int16_t)enum_orig(d, clamp(ed_rv(a + 2), d->min, d->max));
            (void)motion_capture(t, id, t->p[id]);
            ed_known(a[0] & 63u, id);
            ui.force = 1;
        }
        ed_b(a[0]);
        ed_b(a[1]);
        ed_v(t->p[id]);
        break;
    }
    case ED_SONG:                                          /* (Skiff phase 5: no project-slot song) always empty: */
        if (!na || a[0] > 3u) return;                      /* a set or PLAY refused (rc 1), STOP stops */
        if (a[0] == 3u) transport_req = 2;
        ed_b(a[0]); ed_b(a[0] == 1u || a[0] == 2u); ed_b(0);
        ed_b(0); ed_b(0); ed_b(0);
        break;
    default:
        return;
    }
    ed_send();
}

/* main loop: editor frames first; anything else stays for ota_service() */
static void ed_service(void)
{
    const uint8_t *p;
    uint32_t n;
    ed_sync();                                             /* v2 pushes (while watched) */
    if (!ota_frame_get(&p, &n) || n < 4u || p[0] != ED_HDR0 || p[1] != ED_HDR1 || p[2] != ED_HDR2)
        return;
    ed_w.last_ms = fm1_ms;                                 /* any request keeps WATCH alive */
    if (p[3] >= ED_SMP_BEGIN) {                            /* large frames: handled in place, then freed */
        if (p[3] == ED_SMP_MIX && n > 4u)
            ed_smp_mix(p, n);                              /* ((#21) ed_smp_mix) */
        else
            ed_handle(p, n);
        ota_frame_done();
        return;
    }
    {
        static uint8_t f[64];
        uint32_t k = n > sizeof f ? sizeof f : n, i;
        for (i = 0; i < k; i++)
            f[i] = p[i];
        ota_frame_done();                                  /* free the frame buffer before replying */
        ed_handle(f, k);
    }
}
