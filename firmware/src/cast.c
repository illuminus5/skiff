/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff Studio) the cast: a copy of Skiff in the browser (the web build, as Try Skiff) replays what this unit takes,
 * in the order and at the moments it takes it, and so draws the same screen (web/EDITOR_PROTOCOL.md "Cast"; the copy:
 * web/emu/cast_follow.c). The hooks (CAST(..), core.h) put records into a ring: the audio ISR's
 * directly, the main loop's with the interrupts off. While the editor WATCHes, the main loop sends them as SysEx,
 * cmd 83, a record a frame: a sequence number (0..63; 127: the ring overflowed, the cast stopped), then the record's
 * four 32-bit words as u32 (5 bytes of 7 bits each, LSB first, as the backup's). A record:
 *   ticks         TIMER4 (24 MHz) when it was put
 *   blocks|type   the audio blocks rendered since power-on (events_block, 24 bits) | its type << 24
 *   a, b          its own:
 *   B  begin    ms, the MASTER knob (master.c) | 1 << 31 with the second core running
 *   F  frame    the battery's reading, MASTER's: a main-loop frame starts (main.c, after its readings)
 *   e  drawn    -: the frame is drawn, the main loop reads the panel again
 *   T  take     buttons | pressed << 14, keys: what ui_input saw and took (then U, keys struck, when it took any)
 *   K  knob     role, steps: the detents panel_enc took (REMOTE's come with their MIDI)
 *   N  keys     keys, buttons: what the audio ISR saw change (keyboard_block)
 *   M  MIDI     the USB-MIDI packet, its ms | source << 24: as the audio ISR took it (events_block)
 *   R  request  the editor command handled (the copy runs the same request there)
 *   C  check    a hash of the music and the UI, once a second, after a frame's F: the copy compares its own; the
 *               sound's (cast_sound, 0: not taken)
 *   s  shed, o  MIDI overflow (what the unit's load and its queue did, which the copy can't know)
 * The copy renders to a record's block count, sets its clock to the record's ticks and does what the record says. */
#define CAST_VER 4u                    /* (B's bits 24..30) the records' layout: 3 = F's cpu, P; 4 = C's sound (0: t2) */
static uint32_t cast_boot __attribute__((section(".noinit")));   /* CAST_BOOT: restarted for a cast (cast_cmd 2) */
#define CAST_BOOT 0x43535442u                                     /* "CSTB" */
static int cast_audio_late(void) { return cast_boot == CAST_BOOT; }   /* (core.h CAST_AUDIO_LATE: main.c's boot) */

static __attribute__((noinline)) void cast_rec(uint32_t type, uint32_t a, uint32_t b)
{
    uint32_t isr = felucca_dbg.in_audio, *r;
    if (cast.on != 1u)
        return;
    if (!isr)
        fm1_irq_off();
    if (cast.w - cast.r >= CAST_N) {
        cast.on = 2;                                   /* full: the copy can't follow from here */
    } else {
        r = &cast.q[cast.w++ % CAST_N * 4u];
        r[0] = fm1_ticks();
        r[1] = (cast.blocks & 0xFFFFFFu) | type << 24;
        r[2] = a;
        r[3] = b;
    }
    if (!isr)
        fm1_irq_on();
}
static __attribute__((noinline)) void cast_take(uint32_t pressed, uint32_t struck)   /* ui_input, every pass */
{
    uint32_t b = fm1_in.buttons, k = fm1_in.notes;
    cast.pass++;
    if (!(pressed | struck | (b ^ cast.lb) | (k ^ cast.ln)))
        return;
    cast.lb = b;
    cast.ln = k;
    cast_rec('T', b | pressed << 14, k | (uint32_t)cast.pass << 27);
    if (struck)
        cast_rec('U', struck, 0);
}
/* the music (each track's values and steps, the globals, the lists) and what the UI shows, hashed */
static __attribute__((noinline)) uint32_t cast_hash(void)
{
    uint32_t h = song.sel | (uint32_t)ui.page << 4 | (uint32_t)ui.home << 12 | (uint32_t)ui.menu << 14 |
                 (uint32_t)ui.layer << 16 | (uint32_t)ui.cursor << 24, k;
    for (k = 0; k < NTRK; k++)
        h = h * 31u + st_crc32(trk[k].p, sizeof trk[k].p) + st_crc32(trk[k].step, sizeof trk[k].step);
    return h * 31u + st_crc32(song.g, sizeof song.g) + st_crc32(&lists, sizeof lists);
}
/* (t5) the sound: the last SCOPE_N samples of the output (audio.c's scope), oldest first, hashed; 0: the audio ISR ran
 * since F (the copy compares at F's block), not checked */
static __attribute__((noinline)) uint32_t cast_sound(void)
{
    uint32_t w = scope_w, i, h = 2166136261u;
    for (i = 0; i < SCOPE_N; i++)
        h = (h ^ (uint16_t)scope_buf[(w + i) & (SCOPE_N - 1u)]) * 16777619u;
    return cast.blocks == cast.pb ? h | 1u : 0u;
}
static __attribute__((noinline)) void cast_frame(int32_t batt)
{
    cast_rec('F', ((uint32_t)batt & 0x7FFu) | ((uint32_t)cast.madc & 0x7FFu) << 11 | (uint32_t)ui_cpu << 22, fm1_ms);
    cast.px = 2166136261u;
    cast.pc = 0;
    cast.pb = cast.blocks;
    if (cast.on == 1u && fm1_ms - cast.chk >= 1000u) {
        uint32_t s = cast_sound();
        cast.chk = fm1_ms;
        cast_rec('C', cast_hash(), s);
    }
}

/* main.c before ui_draw (piece 0: the draw reads what changed) and gfx.c cv_begin once the last piece's pixels are out
 * (a canvas piece begins): stamped when the audio ran since the frame's F or the last stamp (P: its index in the
 * frame), so the copy draws from there after the same blocks; (t5) ui_graph.c's scope too, where it reads the sound */
static __attribute__((noinline)) void cast_piece(void)
{
    uint32_t k = cast.pc++;
    if (cast.on == 1u && cast.blocks != cast.pb) {
        cast.pb = cast.blocks;
        cast_rec('P', k, 0u);
    }
#ifdef ED_HDR0
    cast_flush();             /* (out between the pieces too: a MENU or EDIT frame while playing, ~95 ms, made ~10
                               * records and the main loop's one ed_service a frame sent ~3: the ring overflowed) */
#endif
}
/* lcd.c (and the web build's): what goes to the screen, hashed (FNV-1a) into the frame's e: a fill's place and colour, a
 * blit's place and pixels; the copy hashes its own the same way */
static __attribute__((noinline)) void cast_px(uint32_t xywh, uint32_t c, const uint16_t *p, uint32_t n)
{
    uint32_t h = cast.px;
    if (cast.on != 1u)
        return;
    h = (h ^ xywh) * 16777619u;
    h = (h ^ c) * 16777619u;
    while (n--)
        h = (h ^ *p++) * 16777619u;
    cast.px = h;
}

#ifdef ED_HDR0                                         /* (the unit: editor.c's SysEx) */
/* ED_CAST: op 0 stop, 1 start (while WATCHed), 2 restart the unit (its music stored first) and cast from its first frame
 * (cast_boot_hold); the reply: op, the cast on (1), rc (1: the music not stored yet, ask again) */
static __attribute__((noinline)) void cast_cmd(uint32_t op)
{
    uint32_t rc = 0;
    if (op == 1u && ed_w.on && cast.on != 1u) {
        cast.w = cast.r = 0;
        cast.lb = fm1_in.buttons;
        cast.ln = fm1_in.notes;
        cast.chk = fm1_ms;
        cast.on = 1;
        cast_rec('B', fm1_ms, (uint32_t)master_knob | CAST_VER << 24 | (uint32_t)C1_ON() << 31);
    } else if (op == 0u) {
        cast.on = 0;
    } else if (op == 2u) {
        rc = autosave_sig() != as.saved;              /* the copy starts from the stored music: store it first */
        if (rc)
            autosave_soon();
        else
            cast.reboot = fm1_ms | 1u;
    }
    ed_b(op);
    ed_b(cast.on == 1u);
    ed_b(rc);
}
/* main loop (ed_service) and between a draw's pieces (cast_piece): the records out as SysEx while there is room (about
 * three: ed_room); watching ended (3 s without a request, a USB
 * reset): the cast too; a restart asked for, once its reply has gone (the chip's reset lets go of USB) */
static __attribute__((noinline)) void cast_flush(void)
{
    if (cast.reboot && fm1_ms - (cast.reboot & ~1u) > 100u) {
        cast_boot = CAST_BOOT;
        bootguard.pending = 0;                         /* (an intentional reset: not a failed boot, main.c) */
        fm1_audio_stop();
        fm1_reboot();
    }
    if (!ed_w.on)
        cast.on = 0;
    while (cast.on && ed_room() && (cast.on == 2u || cast.r != cast.w)) {
        const uint32_t *q = &cast.q[cast.r % CAST_N * 4u];
        uint32_t i;
        ed_begin(ED_CAST);
        if (cast.on == 2u) {
            ed_b(127);
            cast.on = 0;
        } else {
            ed_b(cast.seq++ & 63u);
#pragma clang loop unroll(disable)
            for (i = 0; i < 20u; i++)                  /* (each word as the backup's u32: 5 x 7 bits, LSB first) */
                ed_b(q[i / 5u] >> (i % 5u * 7u));
            cast.r++;
        }
        ed_send();
    }
}
/* main.c, after the splash: restarted for a cast, the splash stays until the editor starts it (10 s at most: the
 * computer finds the unit's USB again first) */
static __attribute__((noinline)) void cast_boot_hold(void)
{
    uint32_t t0 = fm1_ms;
    if (cast_boot != CAST_BOOT)
        return;
    cast_boot = 0;
    while (cast.on != 1u && fm1_ms - t0 < 10000u) {
        fm1_wdt_feed();
        ed_service();
    }
}
#else
static void cast_boot_hold(void) {}                    /* (the web build: main.c's loop is not its own) */
#endif
