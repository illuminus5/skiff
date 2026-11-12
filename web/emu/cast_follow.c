/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff Studio) the live screen's copy: Skiff itself, the web build Try Skiff runs (felucca_web.c), follows an FM-1's
 * cast records (firmware/src/cast.c; web/EDITOR_PROTOCOL.md "Cast") as they arrive, and so draws the unit's screen
 * with Skiff's own drawing code. Built as felucca_cast.wasm (web/emu/build.sh); the page is the editor's Screen tab:
 *   1. one instance: web_nor_erase, web_boot (an empty unit), then web_restore_object for each object of the unit's
 *      backup; its NOR (web_nor) is then the unit's flash;
 *   2. a fresh instance (clean RAM, as the unit's power-on after the cast's restart) gets that NOR, then the records:
 *      four words each into cf_buf(), cf_push(n), and cf_run() replays what has come: the boot at B, each frame once
 *      its e (drawn) has come, the takes between frames once their pass is complete; and every frame the page sends
 *      the unit after its CAST 1: cf_req_buf / cf_req_push(n), each run at the unit's R (cf_request);
 *   3. the screen: web_screen / web_screen_draws, as Try Skiff's.
 * The steps are those of a replayer first run on the computer, which recordings of the unit
 * checked: the main loop as the unit's (a frame at each F with its readings and takes, drawn with the audio run to
 * its pieces' stamps; ui_input passes between frames at the takes' blocks, idle ones at each ms and after each audio
 * half); the audio ISR's records (N keys, M MIDI, o overflow, s shed) at their block. A half's length and the unit's
 * ms boundaries are estimated as the records come (the replayer fits them over a whole recording). */
#ifndef FELUCCA_CAST
#define FELUCCA_CAST 1
#endif
static unsigned char cf_c1;                         /* B: the unit's second core runs (its voice budget, core.h) */
#define C1_ON() cf_c1                               /* (every track rendered here: bit for bit the two cores', */
#define C1_RUN(fn, arg) 0                           /* tests/dualcore_test.c) */
#define C1_WAIT() 0
static void cf_piece(void);
static void cf_read(void);
#define GFX_HOOKS 0
#define GFX_HOOK_TEXT(x0, y0, x1, y1, s, flags) ((void)0)
#define GFX_HOOK_BLIT(x, y, r0) ((void)0)
#define GFX_HOOK_BEGIN() cf_piece()                 /* gfx.c cv_begin: a piece of the frame begins */
#define GFX_HOOK_PIXELS(n) ((void)0)
#define CAST_READ() cf_read()                       /* ui_graph.c's scope reads the sound: a piece too (layout 4) */
#include "felucca_web.c"
/* the editor's SysEx (editor.c) as the unit runs it, behind the web build (no USB): at each R, the page's frame of
 * that request through ed_service, so the copy changes what the unit changed (after the Learn build's edlink.h) */
static uint8_t cf_sxf[700];                         /* the frame for ed_service: the bytes between F0 and F7 */
static uint32_t cf_sxn;
static uint8_t cf_sxr;
#define SXQ 64u
static uint32_t so_r, so_w;                         /* (usb.c's send queue: always room here) */
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!cf_sxr)
        return 0;
    *p = cf_sxf;
    *n = cf_sxn;
    return 1;
}
static void ota_frame_done(void) { cf_sxr = 0; }
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }   /* (the copy's replies: dropped) */
static void audio_silence(void) {}
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static int fl_erase4k(uint32_t off, uint32_t *took) { *took = 0; return st_erase(off); }
static int fl_write(uint32_t off, const void *p, uint32_t n) { return st_prog(off, p, n); }
static void cf_wdt_feed(void)                       /* (a flash write while playing stops the transport first: */
{                                                   /* ed_flash_stop waits for the audio ISR's stop, done here) */
    if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
}
#define fm1_wdt_feed() cf_wdt_feed()
#include "../../firmware/src/editor.c"
#undef fm1_wdt_feed
#include "../../firmware/src/cast.c"

typedef struct { uint32_t w[4], ty; uint64_t t, b; } cf_rec_t;   /* t, b: the ticks and blocks, unwrapped */
#define CF_N 16384u
static cf_rec_t cf_r[CF_N];
static uint32_t cf_nr, cf_ri, cf_xi;               /* held; the next to replay; the next ISR one (web_cast_block) */
static uint32_t cf_in[64u * 4u];
static uint32_t cf_pt, cf_pb;                       /* the last record's raw ticks and blocks */
static uint64_t cf_lt, cf_lb, cf_bt, cf_bb;         /* the last record's unwrapped ticks and blocks; B's */
static int cf_seen, cf_booted;
static uint32_t cf_ver;                             /* B's layout: 3 F's CPU load and P, 4 C's sound and the scope's P */
static uint32_t n_frames, n_frames_off, n_chk, n_chk_off, n_late, n_shed, n_req, n_req_lost, n_req_missing;
/* the page's requests, as it sent them after the cast's start (cf_req_push): each runs here at its R */
typedef struct { uint16_t n; uint8_t b[700]; } cf_frame_t;
#define CF_RQ 256u                                  /* (the editor's load right after the restart: a burst) */
static cf_frame_t cf_rq[CF_RQ];
static uint32_t cf_rq_r, cf_rq_w;
static uint8_t cf_rq_in[702];

/* ----------------------------------------------------------------- the copy's clock --- */
static uint64_t now_t, half_n;                      /* the copy's 24 MHz time; halves rendered */
static double hp = 24e6 * 128.0 / 44100.0, hph;     /* a half's ticks, its phase: half k ends at hph + k * hp */
static double isr_t;                                /* the audio ISR's ticks a half, by F's CPU load */
static int64_t ms_off;                              /* fm1_ms = (t - ms_off) / 24000 */
static double sx, sy, sxx, sxy, sn;                 /* the half's length: a line through the main loop's (b / 4, t) */
static int in_frame;
static uint32_t fm_start;                           /* the last F's ms: main.c's wait loop passes while < 15 ms on */
static uint8_t pend_shed;
static int idle_ok(void) { return !in_frame && fm1_ms - fm_start < 15u; }
static void drain(void) { cast.r = cast.w; if (cast.on == 2u) cast.on = 1; }   /* (the copy's own records: dropped) */
static void set_clock(uint64_t t)
{
    now_t = t;
    web_ticks = (uint32_t)t;
    fm1_ms = (uint32_t)(((int64_t)t - ms_off) / 24000);
}
static uint64_t next_ms_t(void) { return (uint64_t)(ms_off + ((((int64_t)now_t - ms_off) / 24000) + 1) * 24000); }
static void pass(void) { ui_input(); drain(); }
static void half(void)
{
    if (pend_shed) {                                /* (audio.c fm1_alnk0_irq: shed_req, then shed_voice first) */
        pend_shed = 0;
        shed_req = 1;
    }
    fm1_alnk0_irq();
    web_half ^= 1u;
    half_n++;
    drain();
}
static int is_take(uint32_t ty) { return ty == 'T' || ty == 'U' || ty == 'K'; }
static int is_isr(uint32_t ty) { return ty == 'N' || ty == 'M' || ty == 's' || ty == 'o'; }
static uint32_t pass_of(const cf_rec_t *r) { return r->ty == 'T' ? r->w[3] >> 27 : r->ty == 'K' ? (r->w[2] >> 8) & 31u : 99u; }
/* (core.h CAST_BLOCK) the unit's ISR records of this block */
static void web_cast_block(void)
{
    for (; cf_xi < cf_nr; cf_xi++) {
        const cf_rec_t *r = &cf_r[cf_xi];
        if (!is_isr(r->ty))
            continue;
        if (r->b > cast.blocks)
            break;
        if (r->b < cast.blocks)
            n_late++;
        if (r->ty == 'N') {
            fm1_in.notes = r->w[2];
            fm1_in.buttons = r->w[3];
        } else if (r->ty == 'M') {
            uint32_t at = mi_w % MQ;
            midi_in_q[at] = r->w[2];
            midi_in_ms[at] = r->w[3] & 0xFFFFFFu;
            midi_in_source[at] = (uint8_t)(r->w[3] >> 24);
            mi_w++;
        } else if (r->ty == 'o')
            midi_in_overflow = 1;
        else {
            n_shed++;
            pend_shed = 1;                          /* (s: put before the half's blocks: at the last one's count) */
        }
    }
}
/* the copy up to a main-loop record's moment: the halves (by its block count) and the milliseconds in time order,
 * idle passes after each outside a frame */
static void advance(const cf_rec_t *r)
{
    uint64_t want = r->b / 4u;                      /* (a half is 4 blocks: the main loop sees whole halves) */
    for (;;) {
        double th = hph + (double)(half_n + 1u) * hp;
        uint64_t tm = next_ms_t();
        if (half_n < want && (th <= (double)tm || tm > r->t)) {
            set_clock(th < (double)now_t ? now_t : th > (double)r->t ? r->t : (uint64_t)th);
            half();
            if (isr_t > 0.0) {                      /* (the main loop runs again once the ISR is done) */
                double te = th + isr_t;
                set_clock(te < (double)now_t ? now_t : te > (double)r->t ? r->t : (uint64_t)te);
            }
            if (idle_ok())
                pass();
        } else if (tm <= r->t) {
            set_clock(tm);
            if (idle_ok())
                pass();
        } else
            break;
    }
    set_clock(r->t);
    /* re-anchored: half `want` done by r->t, the next not yet (the audio stops for a flash erase: the phase moves) */
    if (hph + (double)want * hp > (double)r->t)
        hph = (double)r->t - (double)want * hp;
    if (hph + (double)(want + 1u) * hp <= (double)r->t)
        hph = (double)r->t - (double)(want + 1u) * hp + 1.0;
}
static void apply_take(const cf_rec_t *r)
{
    if (r->ty == 'T') {
        fm1_in.buttons = r->w[2] & 0x3FFFu;
        web_pressed |= (r->w[2] >> 14) & 0x3FFFu;
        fm1_in.notes = r->w[3] & 0x7FFFFFFu;
    } else if (r->ty == 'U')
        web_notes_pressed |= r->w[2];
    else if (r->ty == 'K') {
        uint32_t role = r->w[2] & 0xFFu;
        if (role < NE)
            web_enc_steps[panel.enc[role] % 7u] += (int32_t)r->w[3] * panel.dir[role];
    }
}

/* an R: the unit handled an editor request here (editor.c ed_service, cmd in a): the page's next frame of that cmd
 * (those before it never reached the unit) through the copy's own ed_service */
static void cf_request(const cf_rec_t *r)
{
    uint32_t cmd = r->w[2] & 0x7Fu;
    while (cf_rq_r != cf_rq_w && cf_rq[cf_rq_r % CF_RQ].b[3] != cmd) {
        cf_rq_r++;
        n_req_lost++;
    }
    n_req++;
    if (cf_rq_r == cf_rq_w) {                       /* (not in the page's log: a request of another page) */
        n_req_missing++;
        return;
    }
    memcpy(cf_sxf, cf_rq[cf_rq_r % CF_RQ].b, cf_rq[cf_rq_r % CF_RQ].n);
    cf_sxn = cf_rq[cf_rq_r % CF_RQ].n;
    cf_rq_r++;
    cf_sxr = 1;
    ed_w.last_ms = fm1_ms;                          /* (the page's keep-alives come as Rs: WATCH stays on) */
    ed_service();
    cf_sxr = 0;
    drain();
}

/* ------------------------------------------------------------- the draw in pieces --- */
static const cf_rec_t *fp[256];                     /* the frame's P records: its stamped pieces */
static uint32_t fpn, fpi, fpc;
static void cf_piece(void)                          /* a stamped piece: the audio ran before it began */
{
    uint32_t k = fpc++;
    if (fpi < fpn && (fp[fpi]->w[2] & 0xFFFFu) == k) {
        const cf_rec_t *p = fp[fpi++];
        while (half_n < p->b / 4u) {
            double th = hph + (double)(half_n + 1u) * hp;
            set_clock(th < (double)now_t ? now_t : th > (double)p->t ? p->t : (uint64_t)th);
            half();
        }
        set_clock(p->t);
    }
}
static void cf_read(void) { if (cf_ver >= 4u) cf_piece(); }

/* a frame: F, then up to its e (or the next F) */
static uint32_t cf_frame(uint32_t i)
{
    const cf_rec_t *r = &cf_r[i];
    uint32_t j, k = 0, fms;
    int32_t b;
    advance(r);
    in_frame = 1;                                   /* (no idle passes from here: the unit is in its frame) */
    fms = fm1_ms = fm_start = r->w[3];
    if (cf_ver >= 3u) {                             /* (11-bit readings, the MENU's CPU load) */
        web_master_adc = (uint32_t)((int32_t)(r->w[2] << 10) >> 21);
        web_batt_adc = (int32_t)(r->w[2] << 21) >> 21;
        ui_cpu = (uint8_t)(r->w[2] >> 22);
        isr_t = (double)ui_cpu / 100.0 * hp;
    } else {
        web_master_adc = (uint32_t)(int16_t)(r->w[2] >> 16);
        web_batt_adc = (int16_t)(r->w[2] & 0xFFFFu);
    }
    b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    master_poll();
    cast.px = 2166136261u;
    fpn = fpi = fpc = 0;
    for (j = i + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
        if (cf_r[j].ty == 'P' && fpn < 256u)
            fp[fpn++] = &cf_r[j];
    for (j = i + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
        if (cf_r[j].ty == 'C') {
            n_chk++;
            n_chk_off += cast_hash() != cf_r[j].w[2];
            break;
        }
    for (j = i + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
        if (cf_r[j].ty == 'R') {                    /* (main.c: ed_service before the frame's ui_input) */
            advance(&cf_r[j]);
            cf_request(&cf_r[j]);
        }
    for (j = i + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
        if (is_take(cf_r[j].ty)) {
            if (!k) {
                advance(&cf_r[j]);
                k = j;
                fm1_ms = fms;
            }
            apply_take(&cf_r[j]);
        }
    felucca_dbg.ui_frames++;
    ui_input();
    settings_poll();
    autosave_poll();
    ui_leds();
    for (j = i + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
        ;
    if (j >= cf_nr || cf_r[j].ty != 'e') {         /* (no e: the next F came first) */
        ui_draw();
        drain();
        return i;
    }
    n_frames++;
    fms = fm1_ms;
    cf_piece();                                     /* (main.c's stamp before ui_draw: piece 0) */
    ui_draw();
    fm1_ms = fms;
    drain();
    advance(&cf_r[j]);
    n_frames_off += cast.px != cf_r[j].w[2];
    in_frame = 0;
    fpn = 0;                                        /* (pieces drawn between frames: no stamps of this frame's) */
    return j;
}

/* B: the unit's boot, then its blocks up to B (the splash held for the cast) */
static void cf_boot(void)
{
    const cf_rec_t *B = &cf_r[0];
    cf_c1 = (unsigned char)(B->w[3] >> 31);
    web_boot();
    hph = (double)B->t - (double)(B->b / 4u) * hp;
    while (half_n < B->b / 4u)
        half();
    set_clock(B->t);
    fm1_ms = B->w[2];
    cf_ver = (B->w[3] >> 24) & 0x7Fu;
    master_knob = (int32_t)(B->w[3] & 0xFFFFFFu);
    song.master_q12 = master_of_pot((uint32_t)(master_knob / 16));
    kb_boot_hold = 0;                               /* (main.c after cast_boot_hold) */
    lcd_fill(0, 0, 240, 240, T_BG);
    scrn.idle = fm1_ms;
    usb.config = 1;                                 /* (the cast comes over USB: on USB power, ui_draw.c's bolt) */
    usb.suspended = 0;
    cast.on = 1;
    cast.w = cast.r = 0;
    ed_w.on = 1;                                    /* (the unit casts only while WATCHed: cast_cmd) */
    ed_w.last_ms = fm1_ms;
    cf_xi = cf_ri = 1;
    in_frame = 1;                                   /* (from B to the first frame: no passes, as main.c) */
    cf_booted = 1;
}

/* -------------------------------------------------------------------- the page's --- */
EXPORT uint32_t *cf_buf(void) { return cf_in; }
/* a request the page sent the unit after the cast's start: its whole frame (F0 .. F7, n bytes) in cf_req_buf();
 * 0 = kept, -1 = not an editor frame or no room */
EXPORT uint8_t *cf_req_buf(void) { return cf_rq_in; }
EXPORT int32_t cf_req_push(uint32_t n)
{
    cf_frame_t *f;
    if (n < 6u || n > sizeof cf_rq_in || cf_rq_in[0] != 0xF0u || cf_rq_in[n - 1u] != 0xF7u || cf_rq_w - cf_rq_r >= CF_RQ)
        return -1;
    f = &cf_rq[cf_rq_w++ % CF_RQ];
    f->n = (uint16_t)(n - 2u);
    memcpy(f->b, cf_rq_in + 1, n - 2u);
    return 0;
}
/* n records (<= 64) from cf_buf, four words each, in the order they came; -1: no room (the copy fell far behind) */
EXPORT int32_t cf_push(uint32_t n)
{
    uint32_t k;
    if (n > 64u)
        return -1;
    if (cf_nr + n > CF_N) {                         /* the replayed ones out (ISR ones until web_cast_block took them) */
        uint32_t lo = cf_ri < cf_xi ? cf_ri : cf_xi;
        memmove(cf_r, cf_r + lo, (cf_nr - lo) * sizeof cf_r[0]);
        cf_nr -= lo;
        cf_ri -= lo;
        cf_xi -= lo;
        if (cf_nr + n > CF_N)
            return -1;
    }
    for (k = 0; k < n; k++) {
        cf_rec_t *r = &cf_r[cf_nr];
        const uint32_t *w = &cf_in[4u * k];
        memcpy(r->w, w, sizeof r->w);
        r->ty = w[1] >> 24;
        if (cf_seen) {                              /* (24 MHz ticks wrap every 179 s, blocks every 3.4 h: unwrapped) */
            r->t = cf_lt + (uint32_t)(w[0] - cf_pt);
            r->b = cf_lb + ((w[1] - cf_pb) & 0xFFFFFFu);
        } else {                                    /* (B) */
            r->t = cf_bt = w[0];
            r->b = cf_bb = w[1] & 0xFFFFFFu;
            ms_off = (int64_t)r->t - (int64_t)w[2] * 24000;   /* (B's ms: the first estimate) */
            cf_seen = 1;
        }
        cf_lt = r->t;
        cf_lb = r->b;
        cf_pt = w[0];
        cf_pb = w[1] & 0xFFFFFFu;
        if (r->ty == 'F' && (int64_t)r->t - (int64_t)w[3] * 24000 < ms_off)
            ms_off = (int64_t)r->t - (int64_t)w[3] * 24000;   /* (the reads lag: the lowest is the boundary) */
        if (!is_isr(r->ty)) {                       /* (a half's length from the main loop's records, B's on) */
            double x = (double)(r->b - cf_bb) / 4.0, y = (double)(r->t - cf_bt);
            sx += x, sy += y, sxx += x * x, sxy += x * y, sn += 1.0;
            if (sn >= 200.0 && sn * sxx - sx * sx > 0.0)
                hp = (sn * sxy - sx * sy) / (sn * sxx - sx * sx);
        }
        cf_nr++;
    }
    return (int32_t)n;
}
/* replays what has come; the frames replayed so far */
EXPORT uint32_t cf_run(void)
{
    if (!cf_booted) {
        if (!cf_nr || cf_r[0].ty != 'B')
            return 0;
        cf_boot();
    }
    while (cf_ri < cf_nr) {
        const cf_rec_t *r = &cf_r[cf_ri];
        uint32_t j, k, ps;
        if (is_isr(r->ty)) {
            cf_ri++;
            continue;
        }
        if (r->ty == 'F') {
            for (j = cf_ri + 1; j < cf_nr && cf_r[j].ty != 'e' && cf_r[j].ty != 'F'; j++)
                ;
            if (j >= cf_nr)
                break;                              /* the frame isn't all here yet */
            cf_ri = cf_frame(cf_ri) + 1u;
            continue;
        }
        if (is_take(r->ty)) {                       /* a wait-loop pass that took something: once it is all here */
            ps = pass_of(r);
            for (j = cf_ri + 1; j < cf_nr; j++) {
                const cf_rec_t *q = &cf_r[j];
                if (is_isr(q->ty))
                    continue;
                if (!is_take(q->ty) || (q->ty != 'U' && pass_of(q) != ps) || q->b != r->b)
                    break;
            }
            if (j >= cf_nr)
                break;
            advance(r);
            for (k = cf_ri; k < j; k++)
                if (!is_isr(cf_r[k].ty))
                    apply_take(&cf_r[k]);
            pass();
            cf_ri = j;
            continue;
        }
        if (r->ty == 'R') {                         /* (main.c's wait loop: ed_service between ui_input passes) */
            advance(r);
            cf_request(r);
        } else if (r->ty == 'C') {
            n_chk++;
            n_chk_off += cast_hash() != r->w[2];
        }
        cf_ri++;
    }
    return n_frames;
}
/* 0 booted, 1 frames replayed, 2 of them unlike the unit's, 3 checks of the music and UI, 4 of them unlike, 5 records
 * waiting, 6 ISR records taken late, 7 voices shed, 8 editor requests handled (R), 9 the second core (B), 10 the
 * page's requests the unit never took, 11 Rs the page's log didn't have */
EXPORT uint32_t cf_stat(uint32_t k)
{
    const uint32_t v[12] = {(uint32_t)cf_booted, n_frames, n_frames_off, n_chk, n_chk_off, cf_nr - cf_ri, n_late,
                            n_shed, n_req, cf_c1, n_req_lost, n_req_missing};
    return k < 12u ? v[k] : 0u;
}
