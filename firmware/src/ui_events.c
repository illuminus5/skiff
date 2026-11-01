/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca 1.1.5: SEQ > AUTO LIST, the selected track's parameter locks and automation events as a list ((Skiff U3, R5)
 * SEQ > AUTOMATION since, the one page; included by ui.c). A row: its kind (LOCK / AUTO), its step, its parameter, its
 * value; in the order of the steps, then as they were added (Skiff 1.3: KNOB 3 keeps the row in place); the last row is + ADD LOCK. (U3) The first row is the
 * page's own (ui.ev_top), above the records: PLAY ON / OFF on KNOB 2 (the track's automation plays, motion.c), CLEAR on
 * KNOB 4 (OCT+: its dialog; the AUTOMATION page's of before)
 *   KNOB 1 (or the PRESETS knob) the row
 *   KNOB 2 the step: the record moves to the next step where its parameter has no record (1 .. LEN)
 *   KNOB 3 the parameter: any one motion can record (motion.c motion_param) that the track's sound has, by name; it
 *          takes the next one with no record on that step, its value the sound's own (a lock that changes nothing yet)
 *   KNOB 4 the value
 *   OCT+   on + ADD LOCK: a lock on the step and parameter KNOB 2 / 3 set there (the SEQ cursor's step and the first
 *          parameter of the sound page shown last, to begin with), at the sound's own value; on a record: its kind
 *          LOCK <-> AUTO (an automation event holds its value until the next one, a lock sounds on its step only)
 *   KNOB 4 past the value's lowest: DELETE ((Skiff U2), was EDIT), OCT+ deletes the record;  OCT- goes HOME
 * Every edit is one undo (SAVE held: ui.c motion_undo_take; a knob turned on is one). Not while a song plays
 * (STOP TO EDIT). The 64 records are shared by all the tracks: AUTOMATION FULL when they are used */
static int32_t accel(uint32_t role, int32_t s, int32_t range);

/* id has a row's name on track t: motion records it, and the track's sound has it (DRUM's lane levels on a DRUM
 * track, the engine's own E1..E8 that it labels; not DIGITAL's operators, retired) */
static int ev_id_ok(const track_t *t, uint32_t id)
{
    const param_desc_t *d;
    if(vocal_control_dead(t,id))return 0;
    if (id >= P_COUNT || !motion_param(id) || id == P_ED_FX || (id >= P_FM1_ATK && id <= P_FM4_LEVEL))
        return 0;
    if (id >= P_LN0 && id <= P_LN7)
        return drum_track(t);
    if (id >= P_LTUNE0 && id <= P_LDECAY7)            /* ((Skiff phase 9) the lanes' TUNE and DECAY: DRUM's own) */
        return t->eng_req % NENGINES == ENGI_DRUM;
    d = track_desc(t, id);
    return d && d->label && d->label[0] && d->label[0] != '-' && d->max > d->min;
}
/* its name in the list and on the card (b holds 12): the page's word before the card's label where the label alone
 * would say two things (ENV ATK, ENV FLT: ENV DEST's, LFO FLT: LFO DEST's; (Skiff phase 9) ENV2 ATTACK, DRIVE TYPE;
 * a lane's: SNARE TUNE, CL HAT DCAY) */
static void ev_name(const track_t *t, uint32_t id, char *b)
{
    const char *pre = id >= P_ATK && id <= P_ED_SHP ? "ENV " : id >= P_LRATE && id <= P_LD_AMP ? "LFO " :
                      id >= P_EG2_ATK && id <= P_EG2_REL ? "ENV2 " : id == P_DTYPE || id == P_DTONE ? "DRIVE " :
                      id >= P_ITYPE && id <= P_IMIX ? "INS " : "";   /* ((1.3) the INSERT's: INS TYPE, INS DEPTH) */
    if (id >= P_LTUNE0 && id <= P_LDECAY7) {
        str_cpy(b, drum_lane_name(t, (id - P_LTUNE0) % NLANE), 12);
        str_cpy(b + str_len(b), id < P_LDECAY0 ? " TUNE" : " DCAY", 12 - str_len(b));
        return;
    }
    str_cpy(b, pre, 12);
    str_cpy(b + str_len(b), id == P_LEVEL ? "LEVEL" : id == P_GLIDE ? "GLIDE" :
                            id >= P_LN0 && id <= P_LN7 && t->eng_req % NENGINES == ENGI_DRUM ? drum_lane_name(t, id - P_LN0) :
                            track_desc(t, id)->label, 12 - str_len(b));   /* ((Skiff U4, L9) a lane: the kit's name, CONGA) */
}
/* the next parameter after id in direction dir (+1 / -1) that is free on track t's step (a record there: skipped);
 * id itself when there is none */
static uint32_t ev_id_step(const track_t *t, uint32_t step, uint32_t id, int32_t dir)
{
    int32_t i = (int32_t)id;
    for (;;) {
        i += dir;
        if (i < 0 || i >= (int32_t)P_COUNT)
            return id;
        if (ev_id_ok(t, (uint32_t)i) && (step >= NSTEP || motion_find(t, step, (uint32_t)i) < 0))
            return (uint32_t)i;
    }
}
static uint32_t ev_len(const track_t *t) { return (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP); }

/* the rows of the selected track (idx[MOTION_MAX]): their count; the row selected, inside 0..count */
static uint32_t ev_rows(uint8_t *idx) { return motion_rows(song.sel, idx); }
static uint32_t ev_row(uint32_t n) { return ui.ev_row < n ? ui.ev_row : n; }

/* the page opened, or the track or its sound changed: + ADD LOCK's step and parameter inside what the track has */
static void ev_fix(void)
{
    const track_t *t = TSEL;
    if (ui.ev_step >= ev_len(t))
        ui.ev_step = (uint8_t)(ev_len(t) - 1u);
    if (!ev_id_ok(t, ui.ev_id)) {
        uint32_t id = lock_id(0);
        ui.ev_id = (uint8_t)(id != 0xFFu && ev_id_ok(t, id) ? id : ev_id_step(t, NSTEP, (uint32_t)-1, 1));
    }
}
static void ev_enter(void)
{
    ui.ev_step = (uint8_t)ui.cursor;
    ui.ev_id = 0xFFu;
    ev_fix();
}

/* the row showing record i (after a move it may sit elsewhere in the order) */
static void ev_follow(uint32_t i)
{
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx), r;
    for (r = 0; r < n && idx[r] != i; r++)
        ;
    ui.ev_row = (uint8_t)r;
}

static const char *ev_act_name(void)
{
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx), r = ev_row(n);
    if (r == n)
        return "ADD";
    if (ui.ev_del)
        return "DELETE";
    return motion.event[idx[r]].lock ? "AUTO" : "LOCK";   /* what OCT+ turns it into */
}
static int ev_act_ready(void)
{
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx);
    return ev_row(n) < n || (motion.count < MOTION_MAX && ev_id_ok(TSEL, ui.ev_id));
}

/* KNOB slot turned s on AUTO LIST */
static void ev_knob(uint32_t slot, int32_t s)
{
    track_t *t = TSEL;
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx), r = ev_row(n), i, step, id;
    const motion_event_t *e;
    const param_desc_t *d;
    if (slot == 0u) {                                   /* the row; above the first, the page's own (U3) */
        int32_t p = clamp((ui.ev_top ? -1 : (int32_t)r) + s, -1, (int32_t)n);
        ui.ev_top = p < 0;
        ui.ev_row = (uint8_t)(p < 0 ? 0 : p);
        ui.ev_del = 0;
        ui.act = 0;
        return;
    }
    if (ui.ev_top) {                                    /* the top row: PLAY, CLEAR picked (right) or dropped (left) */
        if (slot == 1u)
            motion_set_enabled(t, s > 0);
        else if (slot == 3u)
            ui.act = s > 0 ? 4u : 0u;
        return;
    }
    if (slot != 3u)
        ui.ev_del = 0;
    if (r == n) {                                       /* + ADD LOCK: where it goes */
        ev_fix();
        if (slot == 1u)
            ui.ev_step = (uint8_t)clamp((int32_t)ui.ev_step + s, 0, (int32_t)ev_len(t) - 1);
        else if (slot == 2u)
            for (i = (uint32_t)(s > 0 ? s : -s); i; i--)
                ui.ev_id = (uint8_t)ev_id_step(t, NSTEP, ui.ev_id, s > 0 ? 1 : -1);
        return;
    }
    i = idx[r];
    e = &motion.event[i];
    step = e->place & 63u;
    id = MOTION_ID(e);
    if (slot == 1u) {                                   /* the next step where its parameter is free */
        uint32_t top = ev_len(t) > step ? ev_len(t) : step + 1u, m = (uint32_t)(s > 0 ? s : -s);
        int32_t to = (int32_t)step, at = (int32_t)step;
        while (m--) {                                   /* a detent each: the next free step */
            do
                at += s > 0 ? 1 : -1;
            while (at >= 0 && at < (int32_t)top && motion_find(t, (uint32_t)at, id) >= 0);
            if (at < 0 || at >= (int32_t)top)
                break;
            to = at;
        }
        if (to == (int32_t)step)
            return;
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, (uint32_t)to, id, e->value);
    } else if (slot == 2u) {                            /* another parameter, from the sound's own value */
        uint32_t to = id, m = (uint32_t)(s > 0 ? s : -s);
        while (m--)                                     /* a detent each: the next free parameter */
            to = ev_id_step(t, step, to, s > 0 ? 1 : -1);
        if (to == id)
            return;
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, step, to, motion_base_value(t, to));
        ui.ev_id = (uint8_t)to;
    } else {                                            /* the value; (Skiff U2) past its lowest, DELETE */
        d = track_desc(t, id);
        if (ui.ev_del) {
            if (s > 0)
                ui.ev_del = 0;
            return;
        }
        if (s < 0 && e->value <= d->min) {
            ui.ev_del = 1;
            return;
        }
        s = accel(EN_K1 + slot, s, d->fmt == F_ENUM ? 0 : d->max - d->min);
        motion_undo_take(t, 0x200u | slot | i << 4);   /* (a knob turned on, this record) */
        (void)motion_move(t, i, step, id, (int16_t)param_turn(d, e->value, s));
    }
    motion_undo_done(t);
    ev_follow(i);
}

static void ev_delete(void);

/* OCT+: add a lock on + ADD LOCK, DELETE ((Skiff U2)) the record, else its kind LOCK <-> AUTO */
static void ev_oct(void)
{
    track_t *t = TSEL;
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx), r = ev_row(n);
    int32_t at;
    if (ui.ev_top) {                                    /* ((Skiff U3) the top row: CLEAR, picked (act_col), its dialog) */
        ui.act = 0;
        confirm_open(CF_CLEAR_MOTION, song.sel);
        return;
    }
    if (r < n && ui.ev_del) {                           /* (Skiff U2) DELETE */
        ui.ev_del = 0;
        ev_delete();
        return;
    }
    if (r == n) {
        ev_fix();
        if (!ev_id_ok(t, ui.ev_id))
            return;
        if ((at = motion_find(t, ui.ev_step, ui.ev_id)) >= 0) {   /* one is there already: show it */
            ev_follow((uint32_t)at);
            ui_message("ALREADY LISTED");
            return;
        }
        if (motion.count >= MOTION_MAX) {
            ui_way("AUTOMATION FULL", WAY_MOTION);
            return;
        }
        motion_undo_take(t, 0);
        (void)motion_set_lock(t, ui.ev_step, ui.ev_id, motion_base_value(t, ui.ev_id));
        motion_undo_done(t);
        if ((at = motion_find(t, ui.ev_step, ui.ev_id)) >= 0)
            ev_follow((uint32_t)at);
        ui_message("LOCK ADDED");
        return;
    }
    {
        const motion_event_t e = motion.event[idx[r]];
        uint32_t lock = !e.lock;
        motion_undo_take(t, 0);
        (void)motion_put(t, e.place & 63u, MOTION_ID(&e), e.value, lock ? MOTION_LOCK : 0u);
        motion_undo_done(t);
        ui_message(lock ? "NOW A LOCK" : "NOW AUTOMATION");
    }
}

/* OCT+ on DELETE ((Skiff U2) was EDIT): the record goes (the row below moves up) */
static void ev_delete(void)
{
    track_t *t = TSEL;
    uint8_t idx[MOTION_MAX];
    uint32_t n = ev_rows(idx), r = ev_row(n);
    const motion_event_t *e;
    if (r == n) {
        ui_message("NOTHING TO DELETE");
        return;
    }
    e = &motion.event[idx[r]];
    motion_undo_take(t, 0);
    motion_delete_event(t, e->place & 63u, MOTION_ID(e));
    motion_undo_done(t);
    ui.ev_row = (uint8_t)r;                              /* (ev_row clamps it to the new count) */
    ui_message("DELETED");
}
