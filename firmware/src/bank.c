/* SPDX-License-Identifier: GPL-3.0-only */
/* Skiff: the pattern pool (DESIGN.md SAVE > PATTERNS). Sixty-four patterns, kept in flash exactly as they were played:
 * every step whole (its notes and chord, velocity, accent, slide, chance, RATCH or FLAM, trig condition, NUDGE, lane
 * hits and their accents), LEN DIV SWING GATE and DIR, and the track's automation and locks. (Phase 5) Each track's
 * list (A..P, ui.c lists) names pool patterns, a project its lists (SKF2): the patterns are every project's. What a
 * track has is kept as the pattern it is on, by itself: waiting in RAM (PEND_N), written at the autosave's quiet stop.
 *
 * A pattern is BK_REC bytes:
 *   0          the version (0: empty)
 *   1          the parameter count its motion's ids are of (project.c proj_motion_ids): (Skiff phase 9) PROJ_NP,
 *              which firmware before reads, unless the motion names a parameter added since: then P_COUNT
 *   2..6       LEN DIV SWING GATE DIR, each + 64 (the sound's own values, not an automated one)
 *   7          its motion records (0..64), bit 7: the automation plays (motion.on)
 *   8..583     the 64 steps, 9 bytes each (project.c step_put9)
 *   584..631   their NUDGE (core.h nudge_pack)
 *   632..887   the motion, 64 x (step (bit 6: the id's bit 7), id (bit 7: a lock), value low, value high)
 * (Skiff 1.3) A record whose motion names an id from 128 on (P_COUNT passed 127: only one written with r[1] = P_COUNT,
 * a parameter added since phase 9 named) is version 2 (BK_VER2), its ids' bit 7 in bit 6 of their step byte; every
 * other one version 1 as before, which firmware before reads (it shows a version 2 as empty).
 * (Skiff 2.0, #25) A record with a step's per-note ties (core.h step_ties: byte 7 of a step without hits) is version 3
 * (BK_VER3, BK_VER2's ids too): 1.4 shows it as empty; one without any stays version 1 or 2, byte for byte as before
 * Phase 5: the records are one pool, ids 0..BK_POOL-1 (pool_put, pat_decode); id = track x 16 + slot, so a chunk is
 * still one track's A..D, E..H, I..L or M..P and every bank saved before reads the same by id.
 * Flash: BK_CHUNKS chunks of BK_PER patterns (ids 4c .. 4c+3), each written whole to one
 * of BK_NSECT sectors (fm1_flash.h FL_BANK, FL_USR: 0xE7000..0xE8FFF, 0xEA000..0xFBFFF) that holds no chunk's current
 * copy: the payload from offset 256, then the commit record at 0 (its own magic, the chunk, a sequence, the payload's
 * length and hash, the record's hash) last, so a write torn anywhere leaves the chunk's copy before in charge. At boot
 * each chunk's newest copy whose payload checks wins. Four sectors spare: saves go round the free ones (wear). No RAM
 * image: a pattern is read when it is loaded (SPI, short IRQ-off windows: playing goes on); a write reads its chunk,
 * changes its patterns and writes it, only while stopped (an erase mutes the audio).
 * Without the flash driver (FELUCCA_FLASH 0) BK_RAM keeps the sectors in RAM (the host's UI tests); else no bank. */
#define BK_SLOTS 16u
#define BK_VER 1u
#define BK_VER2 2u                                 /* (Skiff 1.3) an id from 128 on (see the top) */
#define BK_VER3 3u                                 /* (Skiff 2.0, #25) per-note ties (see the top) */
static int bk_ver(uint32_t v) { return v == BK_VER || v == BK_VER2 || v == BK_VER3; }
#define BK_PER 4u
#define BK_POOL NPAT                               /* the patterns, ids 0 .. 63 (every track's: phase 5) */
#define BK_CHUNKS (BK_POOL / BK_PER)
#define BK_STEPS 8u
#define BK_NUDGE (BK_STEPS + NSTEP * 9u)
#define BK_MOT (BK_NUDGE + NSTEP / 4u * 3u)
#define BK_REC (BK_MOT + MOTION_MAX * 4u)
#define BK_LEN (BK_PER * BK_REC)                   /* a chunk's payload */
#define BK_NSECT 20u
#define BK_NONE 0xFFu
#define BK_MAGIC 0x31424B53u                       /* "SKB1" */
_Static_assert(BK_REC == 888u && BK_LEN <= 4096u - 256u && BK_CHUNKS + 4u == BK_NSECT, "pattern bank layout");
typedef struct { uint32_t magic, chunk, seq, len, sum, rsv[2], hsum; } bk_hdr_t;   /* at a sector's 0 */
static const uint8_t BK_TIME[5] = {P_SLEN, P_SDIV, P_SSWING, P_SGATE, P_DIR};

static struct {
    uint8_t sect[BK_CHUNKS];                       /* each chunk's current sector + 1 (0: never saved, all empty) */
    uint32_t top;                                  /* the newest sequence (a save: + 1) */
    uint8_t next;                                  /* the sector a save tries first (round: wear) */
    uint8_t len[BK_POOL];                          /* each pattern's LEN, 0 = empty (the page) */
    uint32_t gen;                                  /* any change (the page redraws) */
} bk;
static uint8_t bk_buf[BK_LEN] __attribute__((section(".pool")));   /* a chunk read, changed and written */
static uint8_t bk_rec[BK_REC] __attribute__((section(".pool")));   /* a track's pattern packed (pool_put) */
/* (phase 5) keeping edits: a pattern changed while playing waits here for the next quiet stop (pat_flush_one), and every
 * read of it sees this copy first. PEND_N records, each its pool id (PAT_NONE: the slot free); a record all 0: the
 * pattern is to be emptied */
#define PEND_N NTRK                                /* (one a track: every track can have one staged, stage_entry) */
static uint8_t pend_id[PEND_N] __attribute__((section(".pool")));   /* (.pool: not among the globals the audio ISR reaches) */
static uint8_t pend_rec[PEND_N][BK_REC] __attribute__((section(".pool")));
/* each track's pattern as it came in or was last kept (a hash of it packed): a copy unchanged since is not kept, so
 * another track on the same pattern that changed it is not undone; the staged one's, the track's when it comes in */
static uint32_t trk_sig[NTRK] __attribute__((section(".pool")));
static uint32_t q_sig[NTRK] __attribute__((section(".pool")));
static uint32_t q_lsig[NTRK] __attribute__((section(".pool")));   /* ((2.0.3) track k's list as its next was staged */
_Static_assert(BK_POOL == NPAT, "the pool is the bank's records");
static uint8_t bk_bufc;                            /* the chunk + 1 bk_buf holds whole (0: none, or changed; a backup's
                                                    * LIST reads a chunk in, its GETs read on: bank_chunk) */

#if BOARD_SMK37
/* (SMK-37 Pro) twenty sectors in a row in USRFLASH, after the user sample slot (fm1_flash_map.h FL_BANK); the first
 * erase there releases the stock mapping's marker (storage.c st_stock_release) */
_Static_assert(FL_BANK_HI - FL_BANK_LO == BK_NSECT * 0x1000u && FL_BANK_LO >= FL_SMP_LO && FL_BANK_HI <= FL_SMP_HI,
               "SMK: the pattern pool's sectors");
static uint32_t bk_sector(uint32_t i) { return FL_BANK_LO + i % BK_NSECT * 0x1000u; }
#else
static uint32_t bk_sector(uint32_t i) { return i < 2u ? 0xE7000u + i * 0x1000u : 0xEA000u + (i - 2u) * 0x1000u; }
#endif
static uint32_t bk_chunk(uint32_t id) { return id % BK_POOL / BK_PER; }

#if FELUCCA_FLASH
#define BK_ON flash_ok
static int bk_read(uint32_t off, void *d, uint32_t n) { return st_read(off, d, n); }
static int bk_erase(uint32_t off) { return st_erase(off); }
static int bk_prog(uint32_t off, const void *s, uint32_t n) { return st_prog(off, s, n); }
#elif defined(BK_RAM)
#define BK_ON 1
static uint8_t bk_ram[BK_NSECT][0x1000];
static uint8_t *bk_at(uint32_t off)
{
#if BOARD_SMK37
    uint32_t i = (off - FL_BANK_LO) >> 12;
#else
    uint32_t i = off < 0xEA000u ? (off - 0xE7000u) >> 12 : ((off - 0xEA000u) >> 12) + 2u;
#endif
    return &bk_ram[i % BK_NSECT][off & 0xFFFu];
}
static int bk_read(uint32_t off, void *d, uint32_t n) { memcpy(d, bk_at(off), n); return 0; }
static int bk_erase(uint32_t off) { memset(bk_at(off), 0xFF, 0x1000u); return 0; }
static int bk_prog(uint32_t off, const void *s, uint32_t n)
{
    uint8_t *p = bk_at(off);
    for (uint32_t i = 0; i < n; i++) p[i] &= ((const uint8_t *)s)[i];
    return 0;
}
#else
#define BK_ON 0
static int bk_read(uint32_t off, void *d, uint32_t n) { (void)off; (void)d; (void)n; return -1; }
static int bk_erase(uint32_t off) { (void)off; return -1; }
static int bk_prog(uint32_t off, const void *s, uint32_t n) { (void)off; (void)s; (void)n; return -1; }
#endif

static int bk_head(uint32_t i, bk_hdr_t *h)        /* sector i's commit record valid: 0 */
{
    if (bk_read(bk_sector(i), h, sizeof *h))
        return -1;
    return h->magic == BK_MAGIC && h->chunk < BK_CHUNKS && h->len == BK_LEN &&
           h->hsum == proj_hash(h, sizeof *h - 4u) ? 0 : -1;
}
static int bk_body(uint32_t i, const bk_hdr_t *h)  /* its payload -> bk_buf, the hash right: 0 */
{
    return bk_read(bk_sector(i) + 256u, bk_buf, BK_LEN) || proj_hash(bk_buf, BK_LEN) != h->sum ? -1 : 0;
}
static int bk_newer(uint32_t a, uint32_t b) { return a != b && a - b < 0x80000000u; }   /* (wraps) */
static void bk_lens(uint32_t c)                    /* chunk c's patterns (in bk_buf): their LENs for the page */
{
    uint32_t r;
    for (r = 0; r < BK_PER; r++) {
        const uint8_t *p = bk_buf + r * BK_REC;
        int32_t len = (int32_t)p[2] - 64;
        bk.len[c % BK_CHUNKS * BK_PER + r] = (uint8_t)(!bk_ver(p[0]) ? 0 : len < 1 ? 1 : len > (int32_t)NSTEP ? NSTEP : len);
    }
}

/* persist_boot (main.c): each chunk's newest copy whose payload checks (a torn or damaged one: the copy before) */
static void bank_boot(void)
{
    bk_hdr_t h[BK_NSECT];
    uint8_t ok[BK_NSECT];
    uint32_t i, c;
    memset(&bk, 0, sizeof bk);
    memset(pend_id, PAT_NONE, sizeof pend_id);     /* (phase 5: nothing waiting to be kept) */
    bk_bufc = 0;
    if (!BK_ON)
        return;
    for (i = 0; i < BK_NSECT; i++) {
        ok[i] = bk_head(i, &h[i]) == 0;
        if (ok[i] && (bk.top == 0u || bk_newer(h[i].seq, bk.top)))
            bk.top = h[i].seq;
    }
    for (c = 0; c < BK_CHUNKS; c++)
        for (;;) {
            uint32_t best = BK_NONE;
            for (i = 0; i < BK_NSECT; i++)
                if (ok[i] && h[i].chunk == c && (best == BK_NONE || bk_newer(h[i].seq, h[best].seq)))
                    best = i;
            if (best == BK_NONE)
                break;
            if (bk_body(best, &h[best]) == 0) {
                bk.sect[c] = (uint8_t)(best + 1u);
                bk_lens(c);
                break;
            }
            ok[best] = 0;
        }
}

static uint32_t bk_free(void)                      /* a sector holding no chunk's current copy, from bk.next on */
{
    uint32_t i, j, used;
    for (i = 0; i < BK_NSECT; i++) {
        uint32_t s = (bk.next + i) % BK_NSECT;
        for (used = 0, j = 0; j < BK_CHUNKS; j++)
            used |= bk.sect[j] == s + 1u;
        if (!used)
            return s;
    }
    return BK_NONE;
}
/* bk_buf -> chunk c's next copy, the commit record last, then read back: 0 written */
static int bk_write(uint32_t c)
{
    uint32_t to = bk_free(), base, off;
    bk_hdr_t h, chk;
    int rc;
    if (to == BK_NONE)
        return -1;
    base = bk_sector(to);
    if ((rc = bk_erase(base)) != 0)
        return rc;
    for (off = 0; off < BK_LEN; off += 256u)
        if ((rc = bk_prog(base + 256u + off, bk_buf + off, BK_LEN - off > 256u ? 256u : BK_LEN - off)) != 0)
            return rc;
    h.magic = BK_MAGIC;
    h.chunk = c;
    h.seq = bk.top + 1u;
    h.len = BK_LEN;
    h.sum = proj_hash(bk_buf, BK_LEN);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hsum = proj_hash(&h, sizeof h - 4u);
    if ((rc = bk_prog(base, &h, sizeof h)) != 0)
        return rc;
    if (bk_head(to, &chk) || memcmp(&chk, &h, sizeof h) || bk_body(to, &chk))
        return -7;                                 /* (a part that does not keep what it is given: not SAVED) */
    bk.sect[c] = (uint8_t)(to + 1u);
    bk.top = h.seq;
    bk.next = (uint8_t)((to + 1u) % BK_NSECT);
    bk.gen++;
    return 0;
}

/* a pattern -> record r: steps st, LEN DIV SWING GATE DIR tv (the sound's own), the events of ms placed on track k, the
 * automation on or not: 0 for a step no format can hold (none the UI makes) */
static int bk_pack_raw(uint8_t *r, uint32_t k, const step_t *st, const int16_t *tv, const motion_store_t *ms, int on)
{
    uint32_t i, n = 0, np = PROJ_NP, ties = 0;
    /* ((Skiff phase 9) the motion's ids as a store of PROJ_NP has them, which firmware before reads; today's when it
     * names a parameter added since) */
    for (i = 0; i < ms->count && i < MOTION_MAX; i++)
        if ((ms->event[i].place >> 6) == k && MOTION_ID(&ms->event[i]) >= PROJ_NP - 8u && MOTION_ID(&ms->event[i]) < P_E0)
            np = P_COUNT;
    memset(r, 0, BK_REC);
    r[0] = BK_VER;
    r[1] = (uint8_t)np;
    for (i = 0; i < 5u; i++)
        r[2u + i] = (uint8_t)(tv[i] + 64);
    for (i = 0; i < NSTEP; i++) {
        if (!step_put9(r + BK_STEPS + i * 9u, &st[i]))
            return 0;
        ties |= step_ties(&st[i]);
    }
    nudge_pack(r + BK_NUDGE, st, NSTEP);
    for (i = 0; i < ms->count && n < MOTION_MAX; i++) {
        const motion_event_t *e = &ms->event[i];
        uint8_t *m = r + BK_MOT + n * 4u;
        if ((e->place >> 6) != k)
            continue;
        uint32_t id = np == P_COUNT || MOTION_ID(e) < P_E0 ? MOTION_ID(e) : MOTION_ID(e) - PROJ_ESHIFT;
        m[0] = (uint8_t)((e->place & 63u) | (id >> 7) << 6);   /* ((1.3) the id's bit 7: BK_VER2) */
        m[1] = (uint8_t)((id & 127u) | (e->lock ? MOTION_LOCK : 0u));
        if (id > 127u)
            r[0] = BK_VER2;
        m[2] = (uint8_t)e->value;
        m[3] = (uint8_t)((uint16_t)e->value >> 8);
        n++;
    }
    r[7] = (uint8_t)(n | (on ? 128u : 0u));
    if (ties)
        r[0] = BK_VER3;                            /* ((2.0) its ids as BK_VER2's too) */
    return 1;
}
/* track t's pattern as it is (the sound's own timing, its automation and locks) -> record r */
static int bk_pack(uint8_t *r, const track_t *t)
{
    int16_t tv[5];
    uint32_t i;
    for (i = 0; i < 5u; i++)
        tv[i] = motion_base_value(t, BK_TIME[i]);
    return bk_pack_raw(r, trk_index(t), t->step, tv, &motion, motion_enabled(t));
}

/* chunk c as it is now -> bk_buf (never saved: all empty): 0, 2 a flash error */
static int bk_load(uint32_t c)
{
    bk_bufc = 0;
    if (!bk.sect[c]) {
        memset(bk_buf, 0, BK_LEN);
        return 0;
    }
    return bk_read(bk_sector(bk.sect[c] - 1u) + 256u, bk_buf, BK_LEN) ? 2 : 0;
}
/* record rec -> pattern id (rec 0: the pattern emptied): 0 done, 2 not (the transport runs, a flash error), 3 no flash */
static int pool_put(uint32_t id, const uint8_t *rec)
{
    uint32_t c = bk_chunk(id);
    uint8_t *r = bk_buf + id % BK_PER * BK_REC;
    bk_bufc = 0;
    if (!BK_ON)
        return 3;
    if (transport_busy() || bk_load(c))
        return 2;
    if (rec)
        memcpy(r, rec, BK_REC);
    else
        memset(r, 0, BK_REC);
    if (bk_write(c))
        return 2;
    bk_lens(c);
    return 0;
}
static uint32_t pend_at(uint32_t id)                /* id's waiting copy, PEND_N none */
{
    uint32_t i;
    for (i = 0; i < PEND_N; i++)
        if (pend_id[i] == id % BK_POOL)
            return i;
    return PEND_N;
}
static uint32_t pend_free(void)                    /* the slots free */
{
    uint32_t i, n = 0;
    for (i = 0; i < PEND_N; i++)
        n += pend_id[i] == PAT_NONE;
    return n;
}
static int pool_used(uint32_t id)
{
    uint32_t i = pend_at(id);
    return i < PEND_N ? bk_ver(pend_rec[i][0]) : bk.len[id % BK_POOL] != 0u;
}
/* pattern id's record as it is now -> r: its waiting copy, else flash's: 0, 1 none (empty, or one no firmware wrote) */
static int pat_read(uint32_t id, uint8_t *r)
{
    uint32_t i = pend_at(id), c = bk_chunk(id);
    if (i < PEND_N)
        memcpy(r, pend_rec[i], BK_REC);
    else if (!bk.len[id % BK_POOL] || !bk.sect[c] ||
             bk_read(bk_sector(bk.sect[c] - 1u) + 256u + id % BK_PER * BK_REC, r, BK_REC))
        return 1;
    return bk_ver(r[0]) && (r[7] & 127u) <= MOTION_MAX ? 0 : 1;
}
/* record r (all 0: empty it) to be pattern id: the same as it is now, nothing; else it waits (over id's waiting copy,
 * or in a free slot): 0 kept (or the same), 1 no room. Nothing is written here: pat_flush_one does, stopped */
static int pat_keep_rec(uint32_t id, const uint8_t *r)
{
    uint32_t i = pend_at(id);
    int now = pat_read(id, bk_buf);
    bk_bufc = 0;
    if (bk_ver(r[0]) ? !now && !memcmp(bk_buf, r, BK_REC) : now)
        return 0;                                  /* (the same, or empty and empty) */
    if (i == PEND_N)
        for (i = 0; i < PEND_N && pend_id[i] != PAT_NONE; i++)
            ;
    if (i == PEND_N)
        return 1;
    pend_id[i] = (uint8_t)(id % BK_POOL);
    memcpy(pend_rec[i], r, BK_REC);
    bk.gen++;
    return 0;
}
/* the waiting patterns of one chunk into flash, one write (the transport stopped; an erase mutes the audio): 0 done or
 * none waiting, 2 not (the transport runs, a flash error: they stay waiting) */
static int pat_flush_one(void)
{
    uint32_t i, c;
    for (i = 0; i < PEND_N && pend_id[i] == PAT_NONE; i++)
        ;
    if (i == PEND_N)
        return 0;
    c = bk_chunk(pend_id[i]);
    if (BK_ON) {                                   /* (no flash: nowhere to keep them, dropped) */
        if (transport_busy() || bk_load(c))
            return 2;
        for (i = 0; i < PEND_N; i++)
            if (pend_id[i] != PAT_NONE && bk_chunk(pend_id[i]) == c) {
                uint8_t *r = bk_buf + pend_id[i] % BK_PER * BK_REC;
                if (bk_ver(pend_rec[i][0]))
                    memcpy(r, pend_rec[i], BK_REC);
                else
                    memset(r, 0, BK_REC);
            }
        if (bk_write(c))
            return 2;
        bk_lens(c);
    }
    for (i = 0; i < PEND_N; i++)
        if (pend_id[i] != PAT_NONE && bk_chunk(pend_id[i]) == c)
            pend_id[i] = PAT_NONE;
    bk.gen++;
    return 0;
}
/* pat_keep_rec; no slot free and the transport stopped: a chunk written first to make room */
static int pat_keep_now(uint32_t id, const uint8_t *r)
{
    return pat_keep_rec(id, r) && (transport_busy() || pat_flush_one() || pat_keep_rec(id, r));
}
/* the autosave's round may wait for its gap (project.c): 0 nothing waiting, 1 some, 2 no slot free (not to wait) */
static uint32_t keep_waiting(void) { return pend_free() == PEND_N ? 0u : pend_free() ? 1u : 2u; }
/* the first empty id, none waiting either; NPAT: the pool full */
static uint32_t pool_alloc(void)
{
    uint32_t id;
    for (id = 0; id < BK_POOL; id++)
        if (!pool_used(id) && pend_at(id) == PEND_N)
            return id;
    return NPAT;
}
/* (2.0, #34, the user's pick 2A) a copy's number: the first free after its original's (06's copy is 08 when 07 is
 * used), round to the first free before it, ((#44) none of taken's: a chain's copies before it); NPAT: the pool full */
static uint32_t pool_free_after(uint32_t orig, uint64_t taken)
{
    uint32_t i, id;
    for (i = 1; i <= BK_POOL; i++) {
        id = (orig + i) % BK_POOL;
        if (!pool_used(id) && pend_at(id) == PEND_N && !((taken >> id) & 1u))
            return id;
    }
    return NPAT;
}
static uint32_t pool_alloc_after(uint32_t orig) { return pool_free_after(orig, 0); }
/* a pattern whose record is r (the LENs compared first); NPAT none */
static uint32_t pool_find(const uint8_t *r)
{
    uint32_t id, i;
    int32_t len = (int32_t)r[2] - 64;
    for (id = 0; id < BK_POOL; id++) {
        i = pend_at(id);
        if (!pool_used(id) || (i == PEND_N && bk.len[id] != (uint8_t)(len < 1 ? 1 : len > (int32_t)NSTEP ? NSTEP : len)))
            continue;
        if (!pat_read(id, bk_buf) && !memcmp(bk_buf, r, BK_REC)) {
            bk_bufc = 0;
            return id;
        }
    }
    bk_bufc = 0;
    return NPAT;
}

/* pattern id, read and checked, for track t: its steps, LEN DIV SWING GATE DIR (fitted to t's sound), its automation
 * and locks (t's places): 0 ok, 1 nothing there (or a record no firmware wrote) */
static int pat_decode(const track_t *t, uint32_t id, step_t *st, int16_t *v, motion_store_t *ms)
{
    uint8_t *r = bk_buf;
    uint32_t tk = trk_index(t), i, out;
    bk_bufc = 0;
    if (pat_read(id, r))                           /* (a waiting copy first) */
        return 1;
    memset(st, 0, NSTEP * sizeof *st);
    for (i = 0; i < NSTEP; i++)
        if (!step_get9(&st[i], r + BK_STEPS + i * 9u))
            return 1;
    nudge_unpack(st, r + BK_NUDGE, NSTEP);
    for (i = 0; i < 5u; i++)
        v[i] = (int16_t)param_fit(track_desc(t, BK_TIME[i]), (int32_t)r[2u + i] - 64);
    memset(ms, 0, sizeof *ms);
    ms->count = (uint8_t)(r[7] & 127u);
    ms->on = (uint8_t)(r[7] & 128u ? 1u << tk : 0u);
    for (i = out = 0; i < ms->count && i < MOTION_MAX; i++) {
        const uint8_t *m = r + BK_MOT + i * 4u;
        int32_t val = (int16_t)(m[2] | (uint16_t)m[3] << 8);   /* (stored 16 bits: checked before it is a byte) */
        ms->event[i].place = (uint16_t)(tk << 6 | (m[0] & 63u));
        ms->event[i].id = (uint8_t)((m[1] & 127u) | (m[0] & 64u) << 1);   /* ((1.3) bit 7: BK_VER2's; 0 before) */
        ms->event[i].lock = m[1] >> 7;
        ms->event[i].value = (int8_t)val;
        out |= val < -64 || val > 127;
    }
    if (out || !proj_motion_ids(ms, r[1]) || !motion_valid(ms))
        ms->count = 0;                             /* (motion no firmware wrote: the steps still load) */
    return 0;
}

/* ---- (phase 5) the lists (ui.c lists): a track plays the pattern of the entry it is on, and what it has is kept as
 * that pattern (trk_pid); a new one gets the first free id when it is first kept. The (track, entry) functions keep
 * the bank's names: PERFORM's keys, SAVE > PATTERNS, the scenes ---- */
static uint32_t pool_len(uint32_t id)               /* pattern id's LEN, 0 empty (a waiting copy's first) */
{
    uint32_t i = pend_at(id);
    int32_t len;
    if (i == PEND_N)
        return bk.len[id % BK_POOL];
    if (!bk_ver(pend_rec[i][0]))
        return 0;
    len = (int32_t)pend_rec[i][2] - 64;
    return (uint32_t)(len < 1 ? 1 : len > (int32_t)NSTEP ? (int32_t)NSTEP : len);
}
static int bank_used(uint32_t k, uint32_t e) { uint32_t id = ent_pat(k, e); return id != PAT_NONE && pool_used(id); }
static int ent_on(uint32_t k, uint32_t e) { return bank_used(k, e) || ent_rest(k, e); }   /* (#28) a pattern or a REST */
/* (2.0, #44, the user's pick 3A and the join sheet's A) a chain: track k's entries on CHAIN (REPS_CHAIN) into the
 * next, each a pattern or a REST: one pattern wherever the track plays them (song_poll, bank_switch), edited as one
 * (chain_cursor). Entry e chains into e + 1 */
static int ent_link(uint32_t k, uint32_t e)
{
    return e + 1u < NENT && lists.ent[k][e][1] == REPS_CHAIN && ent_on(k, e) && ent_on(k, e + 1u);
}
/* .. the chain track k's entry e is in: its first and last (*b0, *b1: e when in none); 1: two parts or more */
static int ent_block(uint32_t k, uint32_t e, uint32_t *b0, uint32_t *b1)
{
    uint32_t a = e % NENT, z = a;
    k %= NTRK;
    while (a && ent_link(k, a - 1u))
        a--;
    while (ent_link(k, z))
        z++;
    *b0 = a;
    *b1 = z;
    return z > a;
}
/* (2.0, #33) COPY+'s entries: the first empty ones in a row after track k's entry e's chain (e alone: one), as many as
 * it has parts (an empty entry between patterns, a rest, counts); NENT none */
static uint32_t ent_free_after(uint32_t k, uint32_t e)
{
    uint32_t b0, b1, f, i;
    ent_block(k, e, &b0, &b1);
    for (f = b1 + 1u; f + (b1 - b0) < NENT; f++) {
        for (i = 0; i <= b1 - b0 && !ent_on(k, f + i); i++)
            ;
        if (i > b1 - b0)
            return f;
    }
    return NENT;
}
/* (2.0, #33, #44) COPY+'s numbers for track k's entry e's chain (e alone: its one), part by part as song_put takes
 * them: ids[i] the id + 1 (a REST's 0; NPAT + 1: the pool full); how many parts */
static uint32_t copy_ids(uint32_t k, uint32_t e, uint8_t *ids)
{
    uint32_t b0, b1, j, n;
    uint64_t taken = 0;
    ent_block(k, e, &b0, &b1);
    for (j = b0; j <= b1; j++) {
        n = ent_pat(k, j);
        ids[j - b0] = 0;
        if (n == PAT_NONE)
            continue;
        if ((n = pool_free_after(n, taken)) < NPAT)
            taken |= (uint64_t)1 << n;
        ids[j - b0] = (uint8_t)(n + 1u);
    }
    return b1 - b0 + 1u;
}
/* (2.0, #44) the entry track k is on: its pattern's, or a REST it rests on; NENT none */
static uint32_t trk_ent(uint32_t k)
{
    uint32_t e = bank_cur(k), c = lists.cur[k % NTRK] % NENT;
    return e < NENT || trk_pid[k % NTRK] || !ent_rest(k % NTRK, c) ? e : c;
}
/* .. NOTES, FEEL, START: the steps before track k's entry e in its chain (its parts' LENs: the track's own for e and a
 * REST) and (*all) the whole chain's, in u's (1: steps; 16: the grid's pages, a part's last one whole); in none, 0
 * and the track's */
static uint32_t chain_off(uint32_t k, uint32_t e, uint32_t *all, uint32_t u)
{
    uint32_t b0, b1, j, l, off = 0, n = 0, id;
    k %= NTRK;
    ent_block(k, e, &b0, &b1);
    for (j = b0; j <= b1; j++) {
        id = ent_pat(k, j);
        l = ((j == e % NENT || id == PAT_NONE ? (uint32_t)trk[k].p[P_SLEN] : pool_len(id)) + u - 1u) / u;
        off += j < e % NENT ? l : 0u;
        n += l;
    }
    *all = n;
    return off;
}
/* .. a chain's place (the main loop's): the passes its last part has started it again (SONG: its last entry's REPS
 * repeat it, song_poll; bank_poll_trk counts them), and a LOOP launch waiting for its end (entry + 1, 0 none:
 * bank_switch); (ui.c chain_p0, chain_pl: the passes its first part came in on, TAKE's) */
static uint8_t chain_rep[NTRK] __attribute__((section(".pool")));
static uint8_t chain_go[NTRK] __attribute__((section(".pool")));

/* ---- (2.0, #44) a note going on from a chain's part into the next (as from a step into the next: seq.c, launch.c
 * launch_next): its tail is that part's first steps tying it on. NOTES' LENGTH sets it, PITCH takes it along, an edit
 * that leaves the note short of its part's end takes it out (ed_close); ui_take.c's TAKE writes one ---- */
#define CT_N NSTEP                                 /* ((2.0.2, #58) a note up to 128 steps: its tail through all of the
                                                    * next part; 16 before, when a note was 16 steps at most) */
/* the part after the one track k is on in its chain (its first after its last), both going forward: its pattern (the
 * record in bk_rec, *e2 its entry), its first steps unfolded into u (*un: CT_N, or its LEN); PAT_NONE: none (not on a
 * chain, a REST next or the same pattern again, a swap not kept yet) */
static uint32_t chain_next(uint32_t k, uint32_t *e2, step_t *u, uint32_t *un)
{
    uint32_t e = trk_ent(k), b0, b1, id, i;
    k %= NTRK;
    if (e >= NENT || !ent_block(k, e, &b0, &b1) || trk[k].p[P_DIR] != DIR_FWD || launch_q[k].done)
        return PAT_NONE;
    *e2 = e < b1 ? e + 1u : b0;
    id = ent_pat(k, *e2);
    if (id == PAT_NONE || id == trk_pat(k) || pat_read(id, bk_rec) || bk_rec[6] != DIR_FWD + 64u)
        return PAT_NONE;
    *un = bk_rec[2] - 64u < CT_N ? bk_rec[2] - 64u : CT_N;
    memset(u, 0, CT_N * sizeof *u);
    for (i = 0; i < *un; i++)
        if (!step_get9(&u[i], bk_rec + BK_STEPS + i * 9u))
            return PAT_NONE;
    for (i = 0; i < *un; i++)
        ed_unfold(&u[i], &u[i], i ? &u[i - 1u] : 0);
    return id;
}
/* .. how many of u's first steps tie pitch p on, in a row */
static uint32_t tail_len(const step_t *u, uint32_t un, uint32_t p)
{
    uint32_t j;
    int32_t x;
    for (j = 0; j < un && (x = note_at(&u[j], p)) >= 0 && step_tied(&u[j], (uint32_t)x); j++)
        ;
    return j;
}
static void tail_out(step_t *u, uint32_t un, uint32_t p)
{
    uint32_t j, n = tail_len(u, un, p);
    for (j = 0; j < n; j++)
        ed_del(&u[j], (uint32_t)note_at(&u[j], p));
}
/* the tails of cnt notes: o[i]'s out, p[i] tied on n[i] of the next part's first steps (CT_SAME: as many as o[i]'s);
 * last (the part's last step unfolded; 0 none): a tail of a pitch not sounding there goes too (a note deleted, moved,
 * shortened). Written: the record waiting to be kept (pat_keep_rec), its copy staged to come in the same. 0 done, 1 no
 * next part, 2 no room for all of a tail (it goes as far as it can) */
static int chain_tails(uint32_t k, const step_t *last, uint32_t cnt, const uint8_t *o, const uint8_t *p, const uint8_t *n)
{
    step_t u[CT_N];
    uint8_t l[4];
    uint32_t e2, un, id, i, j, ch = 0;
    int rc = 0;
    launch_t *q = &launch_q[k % NTRK];
    if ((id = chain_next(k, &e2, u, &un)) == PAT_NONE)
        return 1;
    for (i = 0; i < cnt && i < 4u; i++) {
        l[i] = (uint8_t)tail_len(u, un, o[i]);
        tail_out(u, un, o[i]);
        ch |= l[i];
    }
    for (i = 0; last && i < u[0].n;)               /* (a tail of a pitch not at the part's end) */
        if (step_tied(&u[0], i) && note_at(last, u[0].note[i]) < 0) {
            tail_out(u, un, u[0].note[i]);
            ch = 1;
        } else
            i++;
    for (i = 0; i < cnt && i < 4u; i++)
        for (j = 0; j < (n[i] == CT_SAME ? l[i] : n[i]); j++, ch = 1)
            if (j >= un || !ed_add(&u[j], p[i], 1)) {
                rc = 2;
                break;
            }
    if (!ch)
        return rc;
    for (i = 0; i < un; i++)
        if (!step_put9(bk_rec + BK_STEPS + i * 9u, &u[i]))
            return 2;
    bk_rec[0] = BK_VER3;                           /* (ties in it) */
    if (pat_keep_rec(id, bk_rec))
        return 2;
    if (q->armed && q->slot == e2 && !q->done) {   /* (playing: the copy coming in next, the same) */
        fm1_irq_off();
        for (i = 0; i < un; i++) {
            u[i].nudge = q->step[i].nudge;
            q->step[i] = u[i];
        }
        fm1_irq_on();
    }
    bk.gen++;
    return rc;
}
/* .. NOTES' LENGTH: the steps pitch p goes on into the next part */
static uint32_t chain_tail_len(uint32_t k, uint32_t p)
{
    step_t u[CT_N];
    uint32_t e2, un;
    return chain_next(k, &e2, u, &un) == PAT_NONE ? 0u : tail_len(u, un, p);
}
/* (2.0, #44) a knob move into store ms (a part's own automation, not the track's: as motion_put): 1 no room */
static uint32_t ms_put(motion_store_t *ms, const track_t *t, uint32_t step, uint32_t id, int16_t v)
{
    uint32_t i, place = trk_index(t) << 6 | step;
    const param_desc_t *d;
    if (step >= NSTEP || !motion_param(id))
        return 0;
    d = param_desc_of(eng_idx(t->eng_req), id);
    if (v < d->min || v > d->max || v < -64 || v > 127)
        return 0;
    for (i = 0; i < ms->count && !(ms->event[i].place == place && MOTION_ID(&ms->event[i]) == id); i++)
        ;
    if (i == MOTION_MAX)
        return 1;
    ms->event[i].place = (uint16_t)place;
    ms->event[i].id = (uint8_t)id;
    ms->event[i].lock = 0;
    ms->event[i].value = (int8_t)param_fit(d, v);
    if (i == ms->count)
        ms->count++;
    ms->on |= (uint8_t)(1u << trk_index(t));
    return 0;
}
/* .. a TAKE on a chain (ui_take.c take_commit): pass `pass` of track k's log into its part j (not the one it is on), as
 * take_commit puts one into the track: an undo copy of the part first (SAVE held on that part puts it back), its record
 * then waiting to be kept, the copy staged to come in the same; its notes and knob moves counted on (*full: a move
 * found no room); tmp: the caller's scratch */
static __attribute__((noinline)) void take_part(uint32_t k, uint32_t j, uint32_t pass, step_t *tmp, uint32_t *notes,
                                                uint32_t *moves, uint32_t *full)
{
    track_t *t = &trk[k % NTRK];
    launch_t *q = &launch_q[k % NTRK];
    uint32_t id = ent_pat(k, j), len, i, nn, mm = 0, tied = 0;
    int16_t v[5];
    int own;
    motion_store_t ms;
    k %= NTRK;
    if (id == PAT_NONE || pat_decode(t, id, tmp, v, &ms))
        return;                                    /* (a REST: nothing) */
    len = v[0] > 0 && v[0] < (int16_t)NSTEP ? (uint32_t)v[0] : NSTEP;
    own = trk_vmode(t) == V_POLY && v[4] == DIR_FWD && !drum_track(t);   /* (as rec_poly, the part's DIR) */
    for (i = 0; own && i < len; i++)
        ed_unfold(&tmp[i], &tmp[i], i ? &tmp[i - 1u] : 0);
    nn = take_notes(k, tmp, len, pass, own, 1, &tied);
    for (i = 0; i < (take_kv_n < TAKE_KV ? take_kv_n : TAKE_KV); i++)
        if (take_kv[i].trk == k && take_kv[i].pass == pass && take_kv[i].step < len) {
            mm++;
            *full |= ms_put(&ms, t, take_kv[i].step, take_kv[i].param, take_kv[i].value);
        }
    if (!nn && !mm && !tied)
        return;
    undo_label = "TAKE";                           /* (the part as it was: its own undo copy) */
    undo_push(UNDO_PAT);
    undo.trk = (uint8_t)(k + 1u);
    undo.what = UNDO_PAT;
    undo.keep = 0;
    memcpy(undo.p, t->p, sizeof undo.p);
    undo.pid = (uint8_t)(id + 1u);
    if (!bk_pack_raw(bk_rec, k, tmp, v, &ms, (ms.on >> k) & 1u) || pat_decode(t, id, undo.step, v, &undo.motion_backup) ||
        pat_keep_rec(id, bk_rec)) {
        undo.trk = 0;                              /* (not kept: nothing to undo) */
        return;
    }
    for (i = 0; i < 5u; i++)
        undo.p[BK_TIME[i]] = v[i];
    if (q->armed && q->slot == j && !q->done) {    /* (playing: the copy coming in next, the same) */
        fm1_irq_off();
        memcpy(q->step, tmp, sizeof q->step);
        q->motion = ms;
        fm1_irq_on();
        q_sig[k] = proj_hash(bk_rec, BK_REC);
    }
    *notes += nn;
    *moves += mm;
    bk.gen++;
}
static void rec_motion_of(uint8_t *r, uint32_t id)  /* pattern id's stored automation into record r (motion_lost) */
{
    if (!pat_read(id, bk_buf)) {
        r[7] = bk_buf[7];
        memcpy(r + BK_MOT, bk_buf + BK_MOT, BK_REC - BK_MOT);
    }
    bk_bufc = 0;
}
/* track k as a record (bk_rec): its steps, the sound's own timing, its automation (a pattern that came in without its
 * automation keeps its stored one: motion_lost). 0: a step no format holds */
static int trk_rec(uint32_t k)
{
    k %= NTRK;
    if (!bk_pack(bk_rec, &trk[k]))
        return 0;
    if (((motion_lost >> k) & 1u) && trk_pid[k])
        rec_motion_of(bk_rec, trk_pid[k] - 1u);
    return 1;
}
/* new pattern id (kept already) is track k's: named by the entry the track is on (that one naming another: the first
 * empty entry, the track on it; none: the pool keeps it, unnamed). (#28) A REST recorded into: as an empty entry */
static void trk_name(uint32_t k, uint32_t id)
{
    uint32_t e = lists.cur[k] % NENT;
    trk_pid[k] = (uint8_t)(id + 1u);
    if (lists.ent[k][e][0] && !ent_rest(k, e))
        for (e = 0; e < NENT && lists.ent[k][e][0]; e++)
            ;
    if (e < NENT) {
        lists.ent[k][e][0] = (uint8_t)(id + 1u);
        lists.cur[k] = (uint8_t)e;
    }
}
/* track k kept: its pattern, changed since it came in (trk_sig), waits to be written (pat_keep_rec); steps on no
 * pattern yet become a new one, the first free id (trk_name). 0 kept (or unchanged, or nothing there), 1 not: PATTERNS
 * FULL (no id, no waiting slot) */
static int trk_keep(uint32_t k)
{
    uint32_t h, id;
    k %= NTRK;
    if (!BK_ON)
        return 0;
    if (!trk_pid[k] && seq_is_empty(&trk[k]) && !motion_count(&trk[k]))
        return 0;                                  /* (nothing there: nothing to keep) */
    if (!trk_rec(k))
        return 1;
    h = proj_hash(bk_rec, BK_REC);
    if (trk_pid[k] && h == trk_sig[k])
        return 0;
    id = trk_pid[k] ? trk_pid[k] - 1u : pool_alloc();
    if (id == NPAT || pat_keep_now(id, bk_rec))
        return 1;
    if (!trk_pid[k])
        trk_name(k, id);
    trk_sig[k] = h;
    return 0;
}
/* stopped (a project save or load, a backup): every track kept, all of it written now: 0, 1 PATTERNS FULL, 2 a flash
 * error (what is not written still waits) */
static int keep_all(void)
{
    uint32_t k;
    int rc = 0;
    bank_poll();                                   /* (a swap the ISR made before the stop: what went out kept first) */
    for (k = 0; k < NTRK; k++)
        if (trk_keep(k))
            rc = 1;
    while (pend_free() < PEND_N)
        if (pat_flush_one())
            return 2;
    return rc;
}
/* the autosave's round (stopped and quiet: project.c autosave_poll): every track kept, one chunk of the waiting
 * patterns written (one erase a call): 0 nothing waits, 1 one written (more may wait), 2 a write failed */
static int keep_poll(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        (void)trk_keep(k);
    if (pend_free() == PEND_N)
        return 0;
    return pat_flush_one() ? 2 : 1;
}

/* pattern id into track t now (stopped: its steps, LEN DIV SWING GATE DIR, its automation and locks). No undo copy:
 * what the track had is kept. 0 loaded, 1 nothing there (or a record no firmware wrote), 2 loaded, its automation not
 * (AUTOMATION FULL: the pattern keeps it, motion_lost) */
static int pat_load(track_t *t, uint32_t id)
{
    static step_t st[NSTEP];
    static motion_store_t ms;
    uint32_t k = trk_index(t), i, full;
    int16_t v[5];
    if (pat_decode(t, id, st, v, &ms))
        return 1;
    motion_restore(t);
    fm1_irq_off();                                 /* the audio ISR must not see half a pattern */
    memcpy(t->step, st, sizeof st);
    for (i = 0; i < 5u; i++)
        t->p[BK_TIME[i]] = v[i];
    fm1_irq_on();
    full = motion_replace_track(t, &ms) != 0;
    if (full)
        motion_clear(t);                           /* (what it had goes with it, as at a launch: none) */
    motion_lost = (uint8_t)((motion_lost & ~(1u << k)) | (full ? 1u << k : 0u));
    motion_rebase(t);
    trk_pid[k] = (uint8_t)(id + 1u);
    trk_sig[k] = trk_rec(k) ? proj_hash(bk_rec, BK_REC) : 0u;
    bk.gen++;
    return full ? 2 : 0;
}
/* ---- (2.0, #30) a song entry's sound (b: lists.snd's two bytes: 0 0 none; engine + 1 and its preset; 0x80 + a user
 * slot, 0), as PRESETS or USER loads it: all but the track's own values (param_kept). Made into s for track k; 0: none
 * (none set, its user slot erased since, an engine the track may not play: the track's sound stays) ---- */
static int esnd_make(uint32_t k, const uint8_t *b, esnd_t *s)
{
    uint32_t eng = b[0] - 1u, u = b[0] & 127u, pi;
    s->kit = s->fm6 = 0;
    if (b[0] & 128u) {
        if (u >= UP_SLOTS || !up_used(u) || !eng_allowed(k, up_engine(u)))
            return 0;
        up_values(up_rec(u), s->p);
        s->eng = (uint8_t)up_engine(u);
        s->preset = 0;
        s->user = (uint8_t)(u + 1u);
        if (s->eng == ENGI_FM6) {                  /* (its own patch, SLOT as up_load shows it) */
            upf_patch(u, s->p[P_E7], s->f6);
            s->p[P_E7] = (int16_t)fm6_match(s->f6, s->p[P_E7]);
            s->fm6 = 1;
        }
        return 1;
    }
    if (!eng_ok(eng) || !ENGINES[eng]->npresets || !eng_allowed(k, eng))   /* (eng_ok: not DIGITAL's, retired) */
        return 0;
    pi = preset_orig(ENGINES[eng], b[1] % ENGINES[eng]->npresets);
    preset_fill(s->p, ENGINES[eng], pi);
    s->eng = (uint8_t)eng;
    s->preset = (uint8_t)pi;
    s->user = 0;
    if (eng == ENGI_DRUMX)
        s->kit = DRUMX_PKIT[pi % NELEM(DRUMX_PKIT)];
    if (eng == ENGI_FM6 && s->p[P_E7] >= 0 && s->p[P_E7] < (int32_t)FM6_NFACTORY) {
        fm6_unpack(FM6_FACTORY[s->p[P_E7]], s->f6);
        s->fm6 = 1;
    }
    return 1;
}
/* .. (the user's pick 2A) the track's sound as an entry's (b): its user sound or factory preset, as it was loaded (its
 * values, an FM6 patch, a DRUM-X preset's kit as they came); 0: none such (edited since: SAVE > USER keeps it as a user
 * sound; an FM6 patch of its own, a sample set of the user's: the same) */
static int snd_of(uint32_t k, uint8_t *b)
{
    esnd_t s;                                      /* (the main loop's stack: 448 B, once) */
    const track_t *t = &trk[k % NTRK];
    uint32_t i, u = user_of(t);
    b[0] = (uint8_t)(u < UP_SLOTS ? 0x80u | u : t->eng_req + 1u);
    b[1] = (uint8_t)(u < UP_SLOTS ? 0u : t->preset);
    if (!esnd_make(k, b, &s))
        return 0;
    for (i = 0; i < P_COUNT; i++)
        if (!param_kept(i) && motion_base_value(t, i) != s.p[i])
            return 0;
    if (s.fm6 && memcmp(fm6_patch[k % NTRK], s.f6, FP_SIZE))
        return 0;
    return !s.kit || !memcmp(dx_kit, DX_KITS[s.kit - 1u], sizeof dx_kit);
}
/* .. stopped: track k onto entry e brings its sound now. No undo step (the song's, as the pattern) */
static void esnd_go(uint32_t k, uint32_t e)
{
    track_t *t = &trk[k % NTRK];
    if (!esnd_make(k, lists.snd[k % NTRK][e % NENT], &esnd[k % NTRK]))
        return;
    mom_back(t);
    motion_restore(t);
    fm1_irq_off();
    esnd_put(t, &esnd[k % NTRK]);
    fm1_irq_on();
    motion_rebase(t);
    if (t == TSEL)
        sync_reload = 1;
}
/* track k on its entry e now (stopped): what the track had kept, then the entry's pattern ((#28) a REST: no steps, no
 * automation, on no pattern, its LEN DIV as they were: a song's rest, stage_entry), ((2.0, #30) its sound first). 0, 1
 * an empty entry, 2 without its automation, 4 PATTERNS FULL (nowhere to keep what the track had) */
static int ent_go(uint32_t k, uint32_t e)
{
    uint32_t id = ent_pat(k, e);
    k %= NTRK;
    if (!ent_on(k, e))
        return 1;
    if (trk_keep(k))
        return 4;
    lists.cur[k] = (uint8_t)(e % NENT);
    esnd_go(k, e);
    if (id != PAT_NONE)
        return pat_load(&trk[k], id);
    track_defaults_steps(&trk[k]);                 /* (its automation too, motion_lost with it: motion_clear) */
    trk[k].p[P_SGATE] = TP[P_SGATE].def;           /* ((2.0.2) notes put here start at 1 STEP, whatever pattern's 50 %
                                                    * GATE the track had; its LEN DIV stay: a rest's length) */
    trk_pid[k] = 0;
    bk.gen++;
    return 0;
}

/* the page's mark: the entry track k is on, its pattern the track's (BK_NONE: none) */
static uint32_t bank_cur(uint32_t k)
{
    uint32_t e;
    k %= NTRK;
    e = lists.cur[k] % NENT;
    return lists.ent[k][e][0] && lists.ent[k][e][0] == trk_pid[k] ? e : BK_NONE;
}
/* how many entries name pattern id: the lists now (but track k's entry e), the saved projects' */
static uint32_t pat_refs(uint32_t id, uint32_t k, uint32_t e)
{
    uint32_t n = proj_slot_refs(id), t, j;
    for (t = 0; t < NTRK; t++)
        for (j = 0; j < NENT; j++)
            n += (t != k || j != e) && lists.ent[t][j][0] == id + 1u;
    return n;
}
/* SAVE > PATTERNS (stopped): track k's pattern into its entry e (that entry's pattern, else a new one; the track is
 * on it then); erase: the entry emptied, its pattern erased when nothing else names it; the track on that entry
 * emptied too (its steps and automation: else the next keep would make them a pattern there again). 0 done, 2 not
 * (the transport runs, a flash error), 3 no flash, 4 PATTERNS FULL */
static int bank_put(uint32_t k, uint32_t e, int erase)
{
    uint32_t id, j;
    k %= NTRK;
    e %= NENT;
    if (!BK_ON)
        return 3;
    if (transport_busy())
        return 2;
    bank_poll();                                   /* (a swap made before the stop: kept first) */
    id = ent_pat(k, e);
    if (erase) {
        if (id != PAT_NONE && !pat_refs(id, k, e)) {
            memset(bk_rec, 0, BK_REC);
            if (pat_keep_now(id, bk_rec))
                return 4;
            for (j = 0; j < NTRK; j++)
                if (trk_pid[j] == id + 1u && (j != k || lists.cur[k] != e)) {
                    trk_pid[j] = 0;                /* (on it unnamed: its steps a new pattern when next kept) */
                    motion_lost &= (uint8_t)~(1u << j);
                }
        }
        if (id != PAT_NONE && trk_pid[k] == id + 1u && lists.cur[k] == e) {
            if (pat_refs(id, k, e))
                (void)trk_keep(k);                 /* (the pattern stays: what the track had, kept into it first) */
            trk_pid[k] = 0;
            track_defaults_steps(&trk[k]);         /* (its automation too: motion_clear, motion_lost with it) */
        }
        lists.ent[k][e][0] = lists.snd[k][e][0] = lists.snd[k][e][1] = 0;   /* ((2.0, #30) its sound with it) */
    } else {
        if (trk_pid[k] && trk_pid[k] != id + 1u && trk_keep(k))
            return 4;                              /* (the pattern it is on kept first) */
        if (id == PAT_NONE && (id = pool_alloc()) == NPAT)
            return 4;
        if (!trk_rec(k) || pat_keep_now(id, bk_rec))
            return 4;
        lists.ent[k][e][0] = (uint8_t)(id + 1u);
        lists.cur[k] = (uint8_t)e;
        trk_pid[k] = (uint8_t)(id + 1u);
        trk_sig[k] = proj_hash(bk_rec, BK_REC);
    }
    while (pend_free() < PEND_N)
        if (pat_flush_one())
            return 2;
    bk.gen++;
    return 0;
}

/* ---- live switching (launch.c): the main loop's side ---- */
static const char *const LAUNCH_AT[LAUNCH_N] = {" AT LOOP END", " AT NEXT BAR", " AT NEXT BEAT", " NEXT STEP"};
/* the launch_steps a BAR / BEAT launch staged now comes in on: the next bar's, beat's */
static uint32_t launch_at(uint32_t m)
{
    return m == LAUNCH_BAR ? (launch_steps / 16u + 1u) * 16u : (launch_steps / 4u + 1u) * 4u;
}
static int q_empty(const launch_t *q)               /* the pattern in q: no step sounds, no automation */
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (q->step[i].time == ST_NOTE && (q->step[i].n || q->step[i].hit))
            return 0;
    return !q->motion.count;
}
/* each frame (ui_input): a swap the ISR made (done): what played (now in launch_q) is kept as the pattern it was (a new
 * one: a new id, named by the entry it was on), and the track is on the entry that came in; a pattern or a mute still
 * staged when the transport stopped comes in now, as while stopped (a song's next not: PLAY stages it again; a
 * project load drops them: launch.c launch_drop). (#29) What went out changed and no waiting slot free (playing: they
 * are written stopped): it waits in q (done 2: nothing staged for the track meanwhile, stage_entry), the track counted
 * on the entry it left, until stopped (a chunk written then to make room) */
static void bank_poll_trk(uint32_t k)
{
    launch_t *q = &launch_q[k];
    track_t *t = &trk[k];
    uint32_t id;
    int rc = 0;
    if (mute_arm[k] && !song.playing) {
        t->p[P_MUTE] = (int16_t)(mute_arm[k] - 1u);
        mute_arm[k] = 0;
    }
    if (q->armed && !song.playing) {
        q->armed = 0;
        if (!q->adv)
            (void)ent_go(k, q->slot);
        q->adv = 0;
        return;
    }
    if (!q->done)
        return;
    if (q->snd) {                                  /* ((2.0, #30) the entry's sound came in: MOMENTARY's values for */
        q->snd = 0;                                /* the sound before stay out of it) */
        mom_drop(t);
        if (t == TSEL)
            sync_reload = 1;
    }
    if (q->done == 2u && transport_busy())
        return;
    if (!q->same && (trk_pid[k] || !q_empty(q)) &&     /* ((2.0.3, #71) the same pattern: nothing went out) */
        bk_pack_raw(bk_rec, k, q->step, q->time, &q->motion, (q->motion.on >> k) & 1u)) {
        id = trk_pid[k] ? trk_pid[k] - 1u : pool_alloc();   /* (a new pattern went out: the first free id) */
        if (trk_pid[k] && ((motion_lost >> k) & 1u))
            rec_motion_of(bk_rec, id);
        if (trk_pid[k] && proj_hash(bk_rec, BK_REC) == trk_sig[k])
            ;                                      /* (unchanged since it came in) */
        else if (id == NPAT || (rc = pat_keep_now(id, bk_rec)) != 0) {
            if (rc && transport_busy()) {
                if (q->done == 1u)
                    ui_way("PATTERNS FULL", "STOP TO SAVE");
                q->done = 2;
                return;
            }
            ui_way("PATTERNS FULL", WAY_PATTERNS);   /* (no id for it; stopped, a flash error) */
        } else if (!trk_pid[k])
            trk_name(k, id);
    }
    q->done = 0;
    {   /* ((2.0, #44) a chain's own next from one of its parts: its passes go on (its first after its last: one more);
         * else in a chain anew. Its first coming in: the pass it starts on, and (round from its last) the one its pass
         * before started on: TAKE's) */
        uint32_t b0, b1, e = q->slot % NENT, o = lists.cur[k] % NENT, in = (uint32_t)ent_block(k, e, &b0, &b1);
        uint32_t own = in && q->adv && o >= b0 && o <= b1;
        if (!own)
            chain_rep[k] = 0;
        else if (e == b0 && o == b1 && chain_rep[k] < 255u)
            chain_rep[k]++;
        if (in && e == b0) {
            chain_pl[k] = own && o == b1 ? chain_p0[k] : 0u;
            chain_p0[k] = (uint16_t)(take_p0[k] + 1u);
        }
    }
    q->adv = 0;
    trk_pid[k] = (uint8_t)(q->id == PAT_NONE ? 0u : q->id + 1u);   /* ((1.2.1, #12) a rest: on no pattern) */
    lists.cur[k] = (uint8_t)(q->slot % NENT);
    if (!q->same) {                                /* ((2.0.3, #71) the same pattern: its copy and automation as they
                                                    * came in before, so an edit since is still kept when it goes out */
        trk_sig[k] = q_sig[k];
        motion_lost = (uint8_t)((motion_lost & ~(1u << k)) | (q->mlost ? 1u << k : 0u));
    }
    bk.gen++;
    ui.force = 1;
}
static void bank_poll(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        bank_poll_trk(k);
}
/* (2.0.3) track k's list as it stands: its entries' patterns, REPS (chains) and sounds (song_poll: staged again when
 * it changed) */
static uint32_t list_sig(uint32_t k)
{
    return proj_hash(lists.ent[k], sizeof lists.ent[k]) * 31u + proj_hash(lists.snd[k], sizeof lists.snd[k]);
}
/* playing, nothing staged for track k (q not armed): entry e staged, launch mode m; a song's (adv) after reps passes of
 * the one playing. 0 staged, 1 an empty entry, 2 .. without its automation (AUTOMATION FULL), 4 PATTERNS FULL (new
 * steps there and no id for them; what went out at its last swap still waiting in q, bank_poll_trk). (#29) No waiting
 * slot is held for the swap: one a track was, and the song stages its tracks T1 first, so a pattern waiting to be
 * written kept the last track on its entry for good. (1.2.1, #12) The song's on an
 * empty entry (song_poll: a rest), (#28) any on a REST: silence, no automation, the track's own LEN DIV SWING GATE DIR,
 * so the rest's REPS count passes of the length the track has; on no pattern (trk_pid 0), so nothing is kept unless it
 * was recorded into */
static int stage_entry(uint32_t k, uint32_t e, uint32_t m, uint32_t adv, uint32_t reps)
{
    track_t *t = &trk[k];
    launch_t *q = &launch_q[k];
    uint32_t id = ent_pat(k, e), full = 0, i, same;
    if (q->done || (!trk_pid[k] && !seq_is_empty(t) && pool_alloc() == NPAT))
        return 4;
    if ((adv == 2u || !bank_used(k, e)) && (adv || ent_rest(k, e))) {   /* ((2.0) adv 2: SONG ONCE's end, a rest on e
                                                                            * whatever it names: song_poll) */
        for (i = 0; i < NSTEP; i++)
            step_clear(&q->step[i]);
        for (i = 0; i < 5u; i++)                   /* ((2.0.2) GATE the default, as ent_go's) */
            q->time[i] = BK_TIME[i] == P_SGATE ? TP[P_SGATE].def : motion_base_value(t, BK_TIME[i]);
        memset(&q->motion, 0, sizeof q->motion);
        id = PAT_NONE;
    } else if (id == PAT_NONE || pat_decode(t, id, q->step, q->time, &q->motion))
        return 1;
    same = id != PAT_NONE && id == trk_pat(k);     /* ((2.0.3, #71) the pattern the track is on: it goes on as the
                                                    * track has it, edits made before or after this copy (launch_swap) */
    if (!same && motion.count - motion_count(t) + q->motion.count > MOTION_MAX) {
        q->motion.count = 0;                       /* (no room: the steps come in, not the automation) */
        full = 1;
    }
    q_sig[k] = 0;                                  /* (the track's sig once it is in: as trk_keep packs it then) */
    if (bk_pack_raw(bk_rec, k, q->step, q->time, &q->motion, (q->motion.on >> k) & 1u)) {
        if (full)
            rec_motion_of(bk_rec, id);
        q_sig[k] = proj_hash(bk_rec, BK_REC);
    }
    q->slot = (uint8_t)e;
    q->id = (uint8_t)id;
    q->mlost = (uint8_t)full;
    q->mode = (uint8_t)m;
    q->at = launch_at(m);
    q->adv = (uint8_t)adv;
    q->reps = (uint8_t)reps;
    q->done = 0;
    q->snd = (uint8_t)(adv != 2u && esnd_make(k, lists.snd[k][e % NENT], &esnd[k]));   /* ((2.0, #30) its sound, made
                                                                                       * now: in at the swap) */
    q->same = (uint8_t)same;
    q_lsig[k] = list_sig(k);
    RING_PUBLISH();
    q->armed = 1;
    bk.gen++;
    return full ? 2 : 0;
}
/* (2.0, #44) a LOOP launch of entry e for track k, playing: on a chain's part before its last, it waits for the chain's
 * end as for a pattern's (song_poll stages it on the last part), one of the user's staged before dropped, the chain's
 * own next kept; 1 waiting */
static int chain_wait(uint32_t k, uint32_t e)
{
    uint32_t b0, b1, c;
    bank_poll_trk(k);                              /* (a swap just made: the track on what came in) */
    c = lists.cur[k] % NENT;
    if (!ent_block(k, c, &b0, &b1) || c == b1)
        return 0;
    if (launch_q[k].armed && !launch_q[k].adv) {
        launch_q[k].armed = 0;
        RING_PUBLISH();
    }
    chain_go[k] = (uint8_t)(e + 1u);
    bk.gen++;
    return 1;
}
/* entry e of track k ((#28) a REST too): stopped (a count-in too), at once (ent_go); playing, staged for its launch
 * point (ui.c launch_mode), one staged before replaced (an empty entry leaves it staged). 0 done or staged, 1 an empty
 * entry, 2 .. without its automation (AUTOMATION FULL), 4 PATTERNS FULL (stage_entry). ((2.0, #44) A chain: from its
 * first part; LOOP, the track on a chain: at the chain's end, chain_wait) */
static int bank_switch(uint32_t k, uint32_t e)
{
    launch_t *q;
    uint32_t b0, b1;
    k %= NTRK;
    e %= NENT;
    q = &launch_q[k];
    if (!ent_on(k, e))
        return 1;
    if (ent_block(k, e, &b0, &b1))                 /* ((2.0, #44) a chain: from its first part) */
        e = b0;
    chain_go[k] = 0;
    if (song.playing && launch_mode() == LAUNCH_LOOP && chain_wait(k, e))
        return 0;
    q->armed = 0;                                  /* (from here the ISR leaves it alone) */
    RING_PUBLISH();
    bank_poll();                                   /* (a swap just made: kept first) */
    if (!song.playing)
        return ent_go(k, e);
    if (launch_mode() == LAUNCH_LOOP && chain_wait(k, e))   /* (a chain's first came in meanwhile) */
        return 0;
    return stage_entry(k, e, launch_mode(), 0, 0);
}
/* ---- (phase 5) the song (SONG on: lists.song): each track plays its list in turn, after the last the first; an entry's
 * repeats its passes (launch.c launch_due). (1.2.1, #12) An empty entry between a track's first and last pattern is a
 * rest (silence for its REPS passes of the track's LEN); the empty ones after its last end the list (were: every empty
 * entry passed over). (#28) A REST (SONG's PATTERN knob) is such a rest wherever it is: the list runs from its first
 * pattern or REST to its last, so a track can start later or end sooner. Each track on its own: its next comes in at
 * its own pass end. PLAY goes on from the entry each track is on; GLO + PLAY from the first ones (song_top); a launch
 * (PERFORM, a scene) moves a track's place in it ---- */
/* each frame (ui_input, after bank_poll): SONG on and playing, each track's next entry staged for its pass end (one
 * there; a list of one entry: none), waiting while there is no room to keep what would go out; SONG off: a song's
 * staging dropped */
/* (2.0, #47, the user's pick 4A) SONG ONCE: each track plays its list once, from its first entry (PLAY starts there:
 * ui_input.c play_press), its last entry's REPS too; then its end, a rest (on the empty entry after its last, or on its
 * last when its list is full: stage_entry adv 2), silent; when every track with a list has ended, the transport stops
 * and each is on its first entry again. song_end: the tracks whose end is staged or in (this playing) */
static uint8_t song_end __attribute__((section(".pool")));
static uint8_t sp_was __attribute__((section(".pool")));   /* ((2.0, #44) playing at the last poll) */
static void song_rewind(void);
static void song_poll(void)
{
    uint32_t k, e, cur, first, last, b0, b1, rp, live = 0, ended = 0;
    int blk, wait;
    if (!song.playing)
        song_end = 0;
    for (k = 0; k < NTRK; k++) {
        launch_t *q = &launch_q[k];
        if (song.playing && q->armed && q->adv && !q->done && q_lsig[k] != list_sig(k)) {
            q->armed = 0;                          /* ((2.0.3) the song's next staged, then the list changed (COPY+ */
            RING_PUBLISH();                        /* into that entry, another pattern, its REPS, a sound): staged */
            if (q->adv == 2u)                      /* again below from the list as it is; SONG ONCE's end too */
                song_end &= (uint8_t)~(1u << k);
            bk.gen++;
        }
        blk = ent_block(k, cur = lists.cur[k] % NENT, &b0, &b1);
        wait = chain_go[k] && (!blk || cur == b1);  /* ((2.0, #44) a LOOP launch waiting for the chain's end: here */
        if (song.playing && !sp_was) {             /* (PLAY: a chain on pass 1, none whole yet; kept stopped: TAKE) */
            chain_pl[k] = 0;
            chain_p0[k] = 1;
        }
        if (!song.playing) {                       /* (stopped: it comes in now, as a staged one; a chain anew) */
            if (chain_go[k])
                (void)ent_go(k, chain_go[k] - 1u);
            chain_go[k] = chain_rep[k] = 0;
        } else if (wait && !q->done) {             /* (on the chain's last part: it comes in at its end, not the
                                                    * chain's first) */
            q->armed = 0;
            RING_PUBLISH();
            bank_poll_trk(k);
            if (lists.cur[k] % NENT == cur && stage_entry(k, chain_go[k] - 1u, LAUNCH_LOOP, 0u, 0u) != 4)
                chain_go[k] = 0;
            bk.gen++;
            blk = ent_block(k, cur = lists.cur[k] % NENT, &b0, &b1);
            wait = chain_go[k] && (!blk || cur == b1);
        }
        if (!lists.song) {                         /* SONG off: a song's next dropped; ((2.0, #44) a chain loops: its
                                                    * next part, its first after its last) */
            e = blk ? (cur < b1 ? cur + 1u : b0) : NENT;
            if (q->armed && q->adv && q->slot != e) {
                q->armed = 0;
                bk.gen++;
            }
            if (song.playing && e < NENT && !wait && !q->armed && !q->done)
                (void)stage_entry(k, e, LAUNCH_LOOP, 1u, 1u);
            continue;
        }
        for (first = 0; first < NENT && !ent_on(k, first); first++)
            ;
        if (first == NENT)
            continue;
        live |= 1u << k;
        if (q->armed && !q->adv)                   /* (a launch of its own staged: its list goes on from there) */
            song_end &= (uint8_t)~(1u << k);
        ended |= (uint32_t)((song_end >> k) & 1u && !q->armed && !q->done) << k;
        if (!song.playing || wait || q->armed || q->done || ((song_end >> k) & 1u))
            continue;
        for (last = NENT - 1u; !ent_on(k, last); last--)
            ;
        if (blk && (cur < b1 || chain_rep[k] + 1u < ent_reps(k, b1))) {   /* ((2.0, #44) a chain: its next part; after
                                                                          * its last, its first again: its last's REPS
                                                                          * repeat it */
            (void)stage_entry(k, cur < b1 ? cur + 1u : b0, LAUNCH_LOOP, 1u, 1u);
            continue;
        }
        rp = blk ? 1u : ent_reps(k, cur);          /* (the passes before what comes after it: a chain's last, one) */
        if (lists.song == SONG_ONCE && cur >= last) {   /* (its end: a rest after its last REPS) */
            if (stage_entry(k, cur > last || last + 1u == NENT ? cur : last + 1u, LAUNCH_LOOP, 2u,
                            cur > last ? 1u : rp) != 4)   /* (4: no room yet, as below) */
                song_end |= (uint8_t)(1u << k);
            continue;
        }
        e = cur >= first && cur < last ? cur + 1u : first;   /* (the entries between: a pattern or a rest) */
        if (e != cur)
            (void)stage_entry(k, e, LAUNCH_LOOP, 1u, rp);
    }
    sp_was = song.playing;
    if (lists.song == SONG_ONCE && song.playing && live && ended == live)
        song_rewind();                             /* (the last track's end came in: the song is over) */
}
/* every track on its list's first entry, stopped (what is staged dropped, a mute staged turned) */
static void song_rewind(void)
{
    uint32_t k, e;
    transport_stop_now();
    for (k = 0; k < NTRK; k++)
        launch_q[k].armed = 0;
    bank_poll();                                   /* (a swap made: kept first; a mute staged: now) */
    for (k = 0; k < NTRK; k++) {
        for (e = 0; e < NENT && !ent_on(k, e); e++)
            ;
        if (e < NENT)
            (void)ent_go(k, e);
    }
    song_end = 0;
}
/* GLO + PLAY with SONG on ((2.0) PLAY with SONG ONCE): from the top, each track on its list's first entry (playing:
 * stopped there and then); then PLAY */
static void song_top(void)
{
    song_rewind();
    transport_req = 1;
}
/* (2.0, #44) PLAY from stopped: a track on a chain's part past its first (NOTES' cursor went there) plays the chain
 * from its first, as a pattern from its start. ((#30, the unit 2026-10-24: A's sound a user sound, ARCADE then put on
 * the track for D, PLAY on A: ARCADE until the song came round) a track on an entry with a sound plays that sound from
 * the start, unless it is on it already (its knobs turned since kept) */
static void chain_top(void)
{
    uint32_t k, e, b0, b1, u;
    const uint8_t *s;
    for (k = 0; k < NTRK; k++) {
        if ((e = trk_ent(k)) >= NENT)
            continue;
        if (ent_block(k, e, &b0, &b1) && e != b0) {
            (void)ent_go(k, b0);
            continue;
        }
        s = lists.snd[k][e];
        u = user_of(&trk[k]);
        if (s[0] && (u < UP_SLOTS ? s[0] != (0x80u | u) : s[0] != trk[k].eng_req + 1u || s[1] != trk[k].preset))
            esnd_go(k, e);
    }
}
/* .. NOTES, FEEL, the grid, stopped: the cursor at c, past the end of the pattern the selected track is on (c >= its
 * LEN) or before its start, on a chain: the track on the chain's next part (its first after its last) or the one
 * before (its last before its first), c the same place counted on into it; else c as it was */
static int32_t chain_cursor(int32_t c)
{
    uint32_t k = song.sel % NTRK, e = trk_ent(k), b0, b1;
    int32_t len = trk[k].p[P_SLEN];
    if (song.playing || seq_counting() || e >= NENT || !ent_block(k, e, &b0, &b1))
        return c;
    if (ent_go(k, c < 0 ? (e > b0 ? e - 1u : b1) : (e < b1 ? e + 1u : b0)) & ~2)   /* (0, 2: on it) */
        return c;
    return c < 0 ? c + trk[k].p[P_SLEN] : c - len;
}
/* the entry staged for track k ((2.0, #44) a launch waiting for a chain's end first); BK_NONE: none */
static uint32_t bank_next(uint32_t k)
{
    k %= NTRK;
    return chain_go[k] ? chain_go[k] - 1u : launch_q[k].armed ? launch_q[k].slot : BK_NONE;
}
/* .. one of the user's: a launch, not a song's or a chain's own next */
static int user_staged(uint32_t k) { return chain_go[k % NTRK] || (launch_q[k % NTRK].armed && !launch_q[k % NTRK].adv); }
/* track k's staged pattern not to come in after all ((2.0) a song's or a chain's own next stays: song_poll would stage
 * it again): 1 there was one */
static int bank_cancel(uint32_t k)
{
    int was = user_staged(k);
    k %= NTRK;
    if (!launch_q[k].adv)
        launch_q[k].armed = 0;
    chain_go[k] = 0;
    bk.gen++;
    return was;
}
/* OCT- in the PERFORM layer: nothing staged comes in after all, no track's pattern, no mute: 1 there was some (a song's
 * next not: it is the song, staged again at once) */
static int stage_cancel(void)
{
    uint32_t k;
    int was = 0;
    for (k = 0; k < NTRK; k++) {
        if (user_staged(k))
            was |= bank_cancel(k);
        was |= mute_arm[k] != 0u;
        mute_arm[k] = 0;
    }
    bk.gen++;
    return was;
}
/* track k's MUTE to m (0, 1): stopped (a count-in too) at once; playing, staged for the launch point (ui.c launch_mode;
 * LOOP: that track's pass end), what was staged before dropped (as it is already: nothing staged) */
static void mute_to(uint32_t k, uint32_t m)
{
    uint32_t l = launch_mode();
    k %= NTRK;
    mute_arm[k] = 0;
    bk.gen++;
    if (!song.playing) {
        trk[k].p[P_MUTE] = (int16_t)m;
        return;
    }
    if ((trk[k].p[P_MUTE] != 0) == (m != 0u))
        return;
    mute_mode[k] = (uint8_t)l;
    mute_at[k] = launch_at(l);
    RING_PUBLISH();
    mute_arm[k] = (uint8_t)(m ? 2u : 1u);
}
/* the PERFORM layer's black key k (1..4): track k's MUTE the other way (mute_to); a second press before a staged one
 * lands takes it back. What happened, said (at once: nothing, as the GLO layer) */
static void mute_switch(uint32_t k)
{
    char b[24] = {'T', (char)('1' + k % NTRK), ' ', 0};
    k %= NTRK;
    if (mute_arm[k]) {
        str_cpy(b + 3, mute_arm[k] == 2u ? "MUTE" : "UNMUTE", sizeof b - 3u);
        mute_arm[k] = 0;
        bk.gen++;
        ui_say(b, " CANCELLED");
        return;
    }
    mute_to(k, !trk[k].p[P_MUTE]);
    if (mute_arm[k]) {
        str_cpy(b + 3, mute_arm[k] == 2u ? "MUTE" : "UNMUTE", sizeof b - 3u);
        ui_say(b, LAUNCH_AT[launch_mode()]);
    }
}

/* ---- scenes: the PERFORM layer's KEYS SCENE (KNOB 2) ----
 * A scene: the entry each track is on (A..P; none: that track left as it is) and the MUTEs. Recalled, each track's
 * pattern switches and its MUTE turns as the keys do: at once stopped, staged for the launch point playing (an entry a
 * track is on already: nothing staged for it). 16 of them, the project's (phase 5: ui.c lists.scene, SKF2; kept by the
 * autosave as the rest of it): 3 bytes a scene, 5 bits a track of T1..T4 (0 none, 1..16 the entry), then their four
 * MUTE bits; then 2 bytes, a bit for each scene kept; (phase 6) then 2 bytes a scene for T5 and T6: 5 bits each, their
 * MUTEs at bits 10 and 11, bit 15 set when the scene holds them (one stored with four tracks, SKF2's: T5 and T6 left as
 * they are). (Before phase 5 they were the settings', in favorites bytes no engine uses, factory[14][0] ..
 * factory[15][17]; (2.0) settings_persist.c clears them from a record of before Skiff's mark at [15][24]) */
#define SCENES 16u
#define SCENE_MEM(i) (lists.scene[(i)])
_Static_assert(5u * SCENES + 2u == PROJ_SCENES && 3u * SCENES + 2u == PROJ_SC2, "the scenes' bytes");
static uint8_t scene_last = 0xFFu;                 /* the scene recalled or stored last (its key lit); 0xFF none */
static void scene_forget(void) { scene_last = 0xFFu; }   /* (a project load: its own scenes) */
static uint32_t scene_bits(uint32_t s)
{
    s %= SCENES;
    return SCENE_MEM(3u * s) | (uint32_t)SCENE_MEM(3u * s + 1u) << 8 | (uint32_t)SCENE_MEM(3u * s + 2u) << 16;
}
static int scene_used(uint32_t s) { return ((SCENE_MEM(48u) | (uint32_t)SCENE_MEM(49u) << 8) >> (s % SCENES)) & 1u; }
/* track k in scene s: its entry's code (0 none, 1..16) | its MUTE << 5; SCENE_NOT: a scene not holding it */
#define SCENE_NOT 64u
static uint32_t scene_trk(uint32_t s, uint32_t k)
{
    uint32_t b;
    s %= SCENES;
    if (k < 4u) {
        b = scene_bits(s);
        return ((b >> (5u * k)) & 31u) | ((b >> (20u + k)) & 1u) << 5;
    }
    b = SCENE_MEM(50u + 2u * s) | (uint32_t)SCENE_MEM(51u + 2u * s) << 8;
    return b >> 15 ? ((b >> (5u * (k - 4u))) & 31u) | ((b >> (6u + k)) & 1u) << 5 : SCENE_NOT;
}
/* track k's entry in scene s: 0..15; BK_NONE: none (or a code no firmware wrote) */
static uint32_t scene_slot(uint32_t s, uint32_t k)
{
    uint32_t c = scene_trk(s, k % NTRK) & 31u;
    return c && c <= BK_SLOTS ? c - 1u : BK_NONE;
}
static uint32_t scene_now(void) { return scene_last; }
/* the tracks as they are now into scene s: the entry each is on (bank_cur; none: on no entry's pattern), each MUTE */
static void scene_store(uint32_t s)
{
    uint32_t v = 0, h = 1u << 15, k, m;
    s %= SCENES;
    for (k = 0; k < NTRK; k++) {
        uint32_t c = bank_cur(k), mu = trk[k].p[P_MUTE] != 0;
        c = c < BK_SLOTS ? c + 1u : 0u;
        if (k < 4u)
            v |= c << (5u * k) | mu << (20u + k);
        else
            h |= c << (5u * (k - 4u)) | mu << (6u + k);
    }
    SCENE_MEM(3u * s) = (uint8_t)v;
    SCENE_MEM(3u * s + 1u) = (uint8_t)(v >> 8);
    SCENE_MEM(3u * s + 2u) = (uint8_t)(v >> 16);
    SCENE_MEM(50u + 2u * s) = (uint8_t)h;
    SCENE_MEM(51u + 2u * s) = (uint8_t)(h >> 8);
    m = SCENE_MEM(48u) | (uint32_t)SCENE_MEM(49u) << 8 | 1u << s;
    SCENE_MEM(48u) = (uint8_t)m;
    SCENE_MEM(49u) = (uint8_t)(m >> 8);
    scene_last = (uint8_t)s;
    bk.gen++;
}
/* scene s recalled: 0 done or staged, 1 empty, 2 .. a pattern without its automation (AUTOMATION FULL), 4 .. a track
 * that could not go (PATTERNS FULL: nowhere to keep what it has; its mute turns all the same) */
static int scene_recall(uint32_t s)
{
    uint32_t k;
    int rc = 0, r;
    s %= SCENES;
    if (!scene_used(s))
        return 1;
    for (k = 0; k < NTRK; k++) {
        uint32_t c = scene_slot(s, k), e = scene_trk(s, k), b0, b1, on = trk_ent(k);
        if (e == SCENE_NOT)
            continue;                              /* ((phase 6) T5, T6: a scene of before holds nothing of them) */
        if (c == BK_NONE)
            ;                                      /* (the track left as it is, what is staged for it too) */
        else if (c == bank_cur(k) || (ent_block(k, c, &b0, &b1) && on >= b0 && on <= b1))
            (void)bank_cancel(k);                  /* (it plays that one ((2.0, #44) or its chain) already: nothing else
                                                    * to come) */
        else if ((r = bank_switch(k, c)) == 4 || (r == 2 && rc != 4))
            rc = r;
        mute_to(k, e >> 5);
    }
    scene_last = (uint8_t)s;
    bk.gen++;
    return rc;
}
/* scene s stored, said ((Skiff W3) the dialog's OCT+, the editor's PERFORM too) */
static void scene_saved(uint32_t s)
{
    char b[24] = "SCENE ";
    fmt_int(b + 6, (int32_t)(s % SCENES) + 1);
    scene_store(s);
    ui_say(b, " SAVED");
}
/* scene s recalled, said: scene_recall's rc ((Skiff W3) the editor's PERFORM too) */
static int scene_go(uint32_t s)
{
    char b[24] = "SCENE ";
    int staged = song.playing, rc;
    fmt_int(b + 6, (int32_t)(s % SCENES) + 1);
    rc = scene_recall(s);
    if (rc == 1)
        ui_say(b, " EMPTY");
    else if (staged)
        ui_say(b, LAUNCH_AT[launch_mode()]);
    else
        ui_say("LOADED ", b);
    str_cpy(ui.msg2, rc == 2 ? "AUTOMATION FULL" : rc == 4 ? "PATTERNS FULL" : "", sizeof ui.msg2);
    return rc;
}
/* a SCENE key: scene s recalled, or (SAVE held) stored; what happened, said */
static void scene_key(uint32_t s, int store)
{
    if (store && scene_used(s))                       /* ((Skiff U2) over a scene: the dialog first, no undo) */
        confirm_open(CF_STORE_SCENE, s % SCENES);
    else if (store)
        scene_saved(s);
    else
        (void)scene_go(s);
}
/* something staged: a track's pattern or MUTE still to come (a song's next not: it always is) */
static int stage_pending(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        if (user_staged(k) || mute_arm[k])
            return 1;
    return 0;
}

/* track k to its entry e (SONG's OCT+, PERFORM's keys: the selected track; (Skiff W3) the editor's LIST PLAY): at once
 * stopped, staged playing (LAUNCH says when it comes in); what happened, said (a load takes no undo copy: what the track
 * had is kept); bank_switch's rc */
static int entry_go(uint32_t k, uint32_t e)
{
    uint32_t b0, b1;
    char b[8] = {'T', (char)('1' + k % NTRK), ' ', (char)('A' + (ent_block(k, e, &b0, &b1) ? b0 : e % NENT)), 0};
    int staged = song.playing, rc = bank_switch(k, e);   /* ((2.0, #44) a chain: its first part's, said) */
    if (rc == 1) {
        ui_say(b, " EMPTY");
        return rc;
    }
    if (rc == 4) {
        ui_way("PATTERNS FULL", WAY_PATTERNS);
        return rc;
    }
    if (staged)
        ui_say(b, LAUNCH_AT[launch_mode()]);
    else
        ui_say(rc == 2 ? "AUTOMATION FULL " : "LOADED ", b);
    str_cpy(ui.msg2, staged && rc == 2 ? "AUTOMATION FULL" : "", sizeof ui.msg2);
    return rc;
}
static void entry_ui(uint32_t e) { (void)entry_go(song.sel % NTRK, e); }
/* (phase 5) SAVE > PATTERNS, the pool view: how many use pattern id: the entries naming it (the lists, the saved
 * projects), and a track on it that no entry of its names (0: it can be cleared) */
static uint32_t pool_refs(uint32_t id)
{
    uint32_t n, k;
    id %= BK_POOL;
    n = pat_refs(id, NTRK, 0);
    for (k = 0; k < NTRK; k++)
        n += trk_pid[k] == id + 1u && bank_cur(k) == BK_NONE;
    return n;
}
/* .. pattern id cleared (its dialog's OCT+: nothing uses it; stopped: an erase); said */
/* the waiting copies written, stopped (a clear's, (2.0) an empty's): 0, else said (rc 4: the keep found no room) */
static int pool_flush_said(int rc)
{
    while (!rc && pend_free() < PEND_N)
        if (pat_flush_one())
            rc = 2;
    bk.gen++;
    ui.force = 1;
    if (rc)
        ui_way(rc == 4 ? "PATTERNS FULL" : "CLEAR FAILED", rc == 4 ? WAY_PATTERNS : "");
    return rc;
}
static void pool_clear(uint32_t id)
{
    char b[16];
    id %= BK_POOL;
    if (!BK_ON) {
        ui_message("STORAGE ERROR");
        return;
    }
    if (transport_busy() || pool_refs(id)) {
        ui_message(transport_busy() ? "STOP TO CLEAR" : "IN USE");
        return;
    }
    memset(bk_rec, 0, BK_REC);
    if (pool_flush_said(pat_keep_now(id, bk_rec) ? 4 : 0))
        return;
    str_cpy(b, "CLEARED ", sizeof b);
    pat_num(b + 8, id);
    ui_message(b);
}
/* (2.0, #46) SAVE > PATTERNS' ACTION: the patterns nothing uses (CLEAR UNUSED's count) */
static uint32_t pool_unused(void)
{
    uint32_t id, n = 0;
    for (id = 0; id < BK_POOL; id++)
        n += pool_used(id) && !pool_refs(id);
    return n;
}
/* (2.0) record r emptied: no steps, NUDGEs or automation, the version every firmware reads; its LEN DIV SWING GATE DIR
 * kept, or track t's (NEW) */
static void rec_empty(uint8_t *r, const track_t *t)
{
    step_t e;
    uint32_t i;
    for (i = 0; t && i < 5u; i++)                  /* ((2.0.2) GATE the default, 1 STEP: the track's can be a 1.x
                                                    * pattern's 50 %) */
        r[2u + i] = (uint8_t)((BK_TIME[i] == P_SGATE ? TP[P_SGATE].def : motion_base_value(t, BK_TIME[i])) + 64);
    memset(&e, 0, sizeof e);
    step_clear(&e);
    (void)step_put9(r + BK_STEPS, &e);
    for (i = 1; i < NSTEP; i++)
        memcpy(r + BK_STEPS + i * 9u, r + BK_STEPS, 9u);
    memset(r + BK_NUDGE, 0, BK_REC - BK_NUDGE);
    r[0] = BK_VER;
    r[1] = PROJ_NP;
    r[7] = 0;
}
/* .. EMPTY (its dialog's OCT+, stopped): pattern id's steps, NUDGEs and automation gone, its number, LEN DIV SWING GATE
 * DIR and every entry naming it kept; a track on it now empties too (its steps are what is kept of it). Said */
static void pool_empty(uint32_t id)
{
    char b[16];
    uint32_t k;
    id %= BK_POOL;
    if (!BK_ON || transport_busy() || pat_read(id, bk_rec)) {
        ui_message(!BK_ON ? "STORAGE ERROR" : transport_busy() ? "STOP TO EMPTY" : "NOTHING TO EMPTY");
        return;
    }
    rec_empty(bk_rec, 0);
    for (k = 0; k < NTRK; k++)
        if (trk_pid[k] == id + 1u) {
            track_defaults_steps(&trk[k]);
            trk_sig[k] = 0;
        }
    if (pool_flush_said(pat_keep_now(id, bk_rec) ? 4 : 0))
        return;
    str_cpy(b, "EMPTIED ", sizeof b);
    pat_num(b + 8, id);
    ui_message(b);
}
/* .. CLEAR UNUSED (its dialog's OCT+, stopped): every pattern nothing uses, cleared. Said: how many */
static void pool_clear_unused(void)
{
    char b[24];
    uint32_t id, n = 0;
    int rc = 0;
    if (!BK_ON || transport_busy()) {
        ui_message(!BK_ON ? "STORAGE ERROR" : "STOP TO CLEAR");
        return;
    }
    memset(bk_rec, 0, BK_REC);
    for (id = 0; id < BK_POOL && !rc; id++)
        if (pool_used(id) && !pool_refs(id)) {
            if (pat_keep_now(id, bk_rec))
                rc = 4;
            else
                n++;
        }
    if (pool_flush_said(rc))
        return;
    str_cpy(b, "CLEARED ", sizeof b);
    fmt_int(b + 8, (int32_t)n);
    str_cpy(b + str_len(b), n == 1u ? " PATTERN" : " PATTERNS", sizeof b - str_len(b));
    ui_message(b);
}
static uint32_t bank_gen(void) { return bk.gen; }

/* ---- (phase 5) SEQ > SONG (ui.c sg; ui_input.c sends OCT+ OCT- EDIT here): the selected track's entry picked ---- */
/* "T2 C" (b: 8 bytes) */
static void song_tag(char *b, uint32_t k, uint32_t e)
{
    b[0] = 'T';
    b[1] = (char)('1' + k % NTRK);
    b[2] = ' ';
    b[3] = (char)('A' + e % NENT);
    b[4] = 0;
}
/* OCT+: KNOB 2's pick into the entry (a pattern; the track on the entry: it comes in, at once stopped, at LAUNCH
 * playing), COPY: a new pattern, a copy of the entry's (the track's edits in it first), the entry's then (the track on
 * it: on the copy); no pick: the entry played (entry_ui). What happened, said. (2.0, #32, the user's pick 1A) NEW: a
 * new empty pattern (the first free number, the track's LEN .. DIR), then as a pattern put in; (#33) COPY+: the copy
 * into the first empty entry after the entry (its REPS too), the entry as it was; ((#44) on a chain: every part's,
 * into as many empty entries in a row after it, a chain again) */
/* (Skiff W3) .. into track k's entry e (the editor's LIST SET, COPY too): 0 done, else entry_go's rc (the track on the
 * entry), 1 NOTHING TO COPY (COPY+: NO EMPTY ENTRY AFTER), 4 PATTERNS FULL */
static int song_put(uint32_t k, uint32_t e, uint32_t p)
{
    uint32_t id = ent_pat(k, e), n = NPAT, f = p == SPICK_COPYP ? ent_free_after(k, e) : e, b0, b1, j;
    uint8_t nid[NENT];
    char b[24], c[4];
    if (p == SPICK_NEW) {
        bank_poll();
        rec_empty(bk_rec, &trk[k]);
        if ((n = pool_alloc()) == NPAT || pat_keep_now(n, bk_rec)) {
            ui_way("PATTERNS FULL", WAY_PATTERNS);
            return 4;
        }
    }
    if (p != SPICK_COPY && p != SPICK_COPYP) {
        if (p == SPICK_NEW)
            p = n + 1u;
        lists.ent[k][e][0] = (uint8_t)p;           /* (p: the id + 1, as the entry keeps it; (#28) PAT_REST) */
        bk.gen++;
        if (lists.cur[k] == e)
            return entry_go(k, e);
        song_tag(b, k, e);
        pat_num(c, p - 1u);
        str_cpy(b + 4, n < NPAT ? " NEW " : " SET ", sizeof b - 4u);
        str_cpy(b + 9, p == PAT_REST ? "REST" : c, sizeof b - 9u);
        ui_message(b);
        return 0;
    }
    if (f == NENT) {
        ui_message("NO EMPTY ENTRY AFTER");
        return 1;
    }
    if (id == PAT_NONE) {
        ui_way("NOTHING TO COPY", "");
        return 1;
    }
    bank_poll();
    if (p == SPICK_COPYP) {                        /* ((2.0, #44) COPY+ on a chain: all of it, a chain again */
        ent_block(k, e, &b0, &b1);
        (void)copy_ids(k, e, nid);
    } else {
        b0 = b1 = e;
        nid[0] = (uint8_t)(pool_alloc_after(id) + 1u);
    }
    for (j = b0; j <= b1; j++) {                   /* (each part's copy, a REST as it is; the entries named after) */
        uint32_t o = ent_pat(k, j);
        if (!nid[j - b0]) {
            nid[j - b0] = (uint8_t)PAT_REST;
            continue;
        }
        n = nid[j - b0] - 1u;
        if ((trk_pid[k] == o + 1u && trk_keep(k)) || n == NPAT || pat_read(o, bk_rec) || pat_keep_now(n, bk_rec)) {
            ui_way("PATTERNS FULL", WAY_PATTERNS);
            return 4;
        }
    }
    for (j = b0; j <= b1; j++) {
        lists.ent[k][f + j - b0][0] = nid[j - b0];
        lists.ent[k][f + j - b0][1] = lists.ent[k][j][1];
        memcpy(lists.snd[k][f + j - b0], lists.snd[k][j], 2);   /* ((2.0, #30) its sound too) */
    }
    if (trk_pid[k] == id + 1u && lists.cur[k] == f)
        trk_pid[k] = nid[0];                       /* (the copy the track's from here: the same steps, the same sig) */
    bk.gen++;
    str_cpy(b, "COPIED ", sizeof b);
    if (b1 > b0) {                                 /* "COPIED A-B TO F-G" */
        b[7] = (char)('A' + b0), b[8] = '-', b[9] = (char)('A' + b1);
        str_cpy(b + 10, " TO ", sizeof b - 10u);
        b[14] = (char)('A' + f), b[15] = '-', b[16] = (char)('A' + f + b1 - b0), b[17] = 0;
    } else {
        pat_num(b + 7, id);
        str_cpy(b + 9, " TO ", sizeof b - 9u);
        pat_num(b + 13, n);
        if (f != e) {                              /* "COPIED 06 TO 22 AT F" */
            str_cpy(b + 15, " AT ", sizeof b - 15u);
            b[19] = (char)('A' + f);
            b[20] = 0;
        }
    }
    ui_message(b);
    return 0;
}
/* (2.0, #30) .. sound b (lists.snd's two bytes; 0 0 none) onto track k's entry e, said ("T2 C SOUND STRINGS", "T2 C NO
 * SOUND"); 1 refused (a sound on an empty entry, or one it can't carry: esnd_make's none, a preset past the engine's) */
static int song_snd_put(uint32_t k, uint32_t e, const uint8_t *b)
{
    esnd_t s;                                      /* (the main loop's stack: 448 B, once) */
    char m[28], n[16];
    k %= NTRK;
    e %= NENT;
    if (b[0] && (!ent_on(k, e) || (!(b[0] & 128u) && b[1] >= ENGINES[(b[0] - 1u) % NENGINES]->npresets) ||
                 !esnd_make(k, b, &s)))
        return 1;
    lists.snd[k][e][0] = b[0];
    lists.snd[k][e][1] = b[1];
    bk.gen++;
    song_tag(m, k, e);
    if (b[0]) {
        esnd_name(b, n);
        str_cpy(m + 4, " SOUND ", sizeof m - 4u);
        str_cpy(m + 11, n, sizeof m - 11u);
    } else {
        str_cpy(m + 4, " NO SOUND", sizeof m - 4u);
    }
    ui_message(m);
    return 0;
}
/* (the user's pick 2A) .. SOUND or NO SOUND picked: the selected track's sound onto entry e, or the entry's taken off;
 * 1 refused (the sound edited since its load: the way, SAVE > USER) */
static int song_snd(uint32_t k, uint32_t e, int set)
{
    uint8_t b[2] = {0, 0};
    if (set && !snd_of(k, b)) {
        ui_way("SOUND EDITED", "SAVE > USER FIRST");
        return 1;
    }
    return song_snd_put(k, e, b);
}
static void song_oct(void)
{
    uint32_t p = song_pick();
    sg.pick = 0;
    if (!p)
        entry_ui(sg.entry % NENT);
    else if (p >= SPICK_SOUND)
        (void)song_snd(song.sel, sg.entry, p == SPICK_SOUND);
    else
        (void)song_put(song.sel % NTRK, sg.entry % NENT, p);
}
/* OCT-: the pick dropped; none, what is staged for the track (a song's: no) */
static void song_back(void)
{
    uint32_t k = song.sel % NTRK;
    if (song_pick()) {
        sg.pick = 0;
        ui_message("CANCELLED");
    } else if (user_staged(k) && bank_cancel(k)) {
        ui_message("CANCELLED");
    }
}
/* the CLEAR ENTRY dialog's lines for the selected track's entry e (l: 3 x 32 bytes): 1 .. 3 of them; *gone: its pattern
 * goes with it (the dialog: it can't be undone) */
static uint32_t song_clear_text(uint32_t e, char l[3][32], int *gone)
{
    uint32_t k = song.sel % NTRK, id = ent_pat(k, e), t, j, m = 0, n = 0, w[2] = {0, 0};
    char c[4];
    e %= NENT;
    pat_num(c, id);
    for (t = 0; t < NTRK; t++)                     /* (the other entries naming it: the first two) */
        for (j = 0; j < NENT; j++)
            if ((t != k || j != e) && lists.ent[t][j][0] == id + 1u && m++ < 2u)
                w[m - 1u] = t * 16u + j;
    *gone = id != PAT_NONE && !pat_refs(id, k, e);
    if (*gone) {
        str_cpy(l[n], "erases pattern ", 32);
        str_cpy(l[n] + 15, c, 17);
        str_cpy(l[n++] + 17, " too", 15);
    } else {
        str_cpy(l[n], "pattern ", 32);
        str_cpy(l[n] + 8, c, 24);
        str_cpy(l[n++] + 10, " stays:", 22);
        if (!m) {
            str_cpy(l[n++], "a saved project uses it", 32);
        } else if (m > 2u) {
            fmt_int(l[n], (int32_t)m);
            str_cpy(l[n] + str_len(l[n]), " entries use it", 32 - str_len(l[n]));
            n++;
        } else {                                   /* "entry D uses it", "entries D and T1 A use it" */
            char *p = l[n];
            str_cpy(p, m == 1u ? "entry " : "entries ", 32);
            for (j = 0; j < m; j++) {
                if (j)
                    str_cpy(p + str_len(p), " and ", 32 - str_len(p));
                if (w[j] / 16u != k) {
                    char tt[4] = {'T', (char)('1' + w[j] / 16u), ' ', 0};
                    str_cpy(p + str_len(p), tt, 32 - str_len(p));
                }
                c[0] = (char)('A' + w[j] % 16u);
                c[1] = 0;
                str_cpy(p + str_len(p), c, 32 - str_len(p));
            }
            str_cpy(p + str_len(p), m == 1u ? " uses it" : " use it", 32 - str_len(p));
            n++;
        }
    }
    if (trk_pid[k] == id + 1u && lists.cur[k] == e) {   /* (the user's choice: say the track empties) */
        str_cpy(l[n], "T1 goes empty", 32);
        l[n++][1] = (char)('1' + k);
    }
    return n;
}
/* the dialog's OCT+: the entry cleared (bank_put); said. (Skiff W3) track k's (the editor's LIST CLEAR): bank_put's rc.
 * (#28) A REST (no dialog: nothing goes with it): at once, playing too */
static int song_clear_at(uint32_t k, uint32_t e)
{
    char b[8];
    int rc = 0;
    if (ent_rest(k, e))
        lists.ent[k][e][0] = lists.snd[k][e][0] = lists.snd[k][e][1] = 0;   /* ((2.0, #30) its sound with it) */
    else
        rc = bank_put(k, e, 1);
    song_tag(b, k, e);
    if (rc == 0)
        ui_say(b, " CLEARED");
    else if (rc == 3)
        ui_message("STORAGE ERROR");
    else if (rc == 4)
        ui_way("PATTERNS FULL", WAY_PATTERNS);
    else
        ui_message(transport_busy() ? "STOP TO CLEAR" : "CLEAR FAILED");
    ui.force = 1;
    return rc;
}
static void song_clear(uint32_t e) { (void)song_clear_at(song.sel % NTRK, e); }

/* ---- the editor's backup (editor_backup.c): each chunk an object, ids 16..31 ---- */
/* chunk c, read into bk_buf: *len BK_LEN, 0 when every pattern in it is empty (or never saved) */
static const uint8_t *bank_chunk(uint32_t c, uint32_t *len)
{
    uint32_t r;
    c %= BK_CHUNKS;
    *len = 0;
    if (!bk.sect[c])
        return bk_buf;
    if (bk_bufc != c + 1u) {
        bk_bufc = 0;
        if (bk_read(bk_sector(bk.sect[c] - 1u) + 256u, bk_buf, BK_LEN))
            return bk_buf;
        bk_bufc = (uint8_t)(c + 1u);
    }
    for (r = 0; r < BK_PER; r++)
        if (bk_ver(bk_buf[r * BK_REC]))
            *len = BK_LEN;
    return bk_buf;
}
/* a chunk from a backup (len 0: all empty): each pattern empty or as some firmware with this record wrote it */
static int bank_chunk_ok(const uint8_t *b, uint32_t len)
{
    uint32_t r, i;
    step_t s;
    if (!len)
        return 1;
    if (len != BK_LEN)
        return 0;
    for (r = 0; r < BK_PER; r++) {
        const uint8_t *p = b + r * BK_REC;
        if (!p[0])
            continue;
        if (!bk_ver(p[0]) || p[1] < 8u || p[1] > P_COUNT || (p[7] & 127u) > MOTION_MAX)
            return 0;
        for (i = 0; i < NSTEP; i++)
            if (!step_get9(&s, p + BK_STEPS + i * 9u))
                return 0;
    }
    return 1;
}
/* a backup's chunk c into the bank (len 0: its patterns emptied): 0 done (or nothing to do), 1 not written */
static int bank_restore(uint32_t c, const uint8_t *b, uint32_t len)
{
    uint32_t k;
    c %= BK_CHUNKS;
    bk_bufc = 0;
    if (!len && !bk.sect[c])
        return 0;                                  /* (empty here too) */
    if (!BK_ON)
        return 1;
    if (len)
        memcpy(bk_buf, b, BK_LEN);
    else
        memset(bk_buf, 0, BK_LEN);
    if (bk_write(c))
        return 1;
    bk_lens(c);
    for (k = 0; k < PEND_N; k++)
        if (pend_id[k] != PAT_NONE && bk_chunk(pend_id[k]) == c)
            pend_id[k] = PAT_NONE;                 /* (the backup's patterns there now: not what waited) */
    for (k = 0; k < NTRK; k++)
        if (trk_pid[k] && bk_chunk(trk_pid[k] - 1u) == c) {
            trk_pid[k] = 0;                        /* (its steps: a new pattern when next kept) */
            motion_lost &= (uint8_t)~(1u << k);
        }
    return 0;
}

#if FELUCCA_LEGACY
/* ---- (phase 5) the projects of before SKF2 (the four slots, the autosave) become SKF2 ((phase 6) SKF3 with six
 * tracks): each track's steps, timing and automation a pool pattern (one the same there already: that one; an
 * empty track: none), its entry A; their old scenes none (the settings' old scene bytes freed). After a clean boot
 * (main.c: never after a crash, a hang or a failed boot) and after a backup's restore (editor_backup.c). The patterns
 * are written before the project naming them: a power cut on the way leaves the record before, and the next time
 * finds those patterns again. The pool full: that project stays as it is (it loads as before, its steps its own). 1:
 * something written. (2.0) The converter's alone (FELUCCA_LEGACY, web/conv): 2.0 reads none of them ---- */
#if FELUCCA_FLASH
static void fm1_wdt_feed(void);                    /* (fm1_sys.h; the host tests' own) */
#endif
static int proj_migrate(int allowed)
{
    int wrote = 0;
#if FELUCCA_FLASH
    project_t *p = &proj_scratch;
    uint32_t o, k, i, id, used;
    int16_t tv[5];
    int len;
    if (!allowed || !BK_ON)
        return 0;
    for (o = 0; o < 5u; o++) {
        uint32_t obj = o < 4u ? OBJ_PROJECT0 + o : OBJ_AUTOSAVE;
        fm1_wdt_feed();
        proj_wire_gen++;
        len = st_load(obj, &proj_wire, sizeof proj_wire);
        if (len <= 0 || proj_fmt_of(&proj_wire, len) || !proj_import(p, &proj_wire, len))
            continue;                              /* (none, SKF2 .. SKF4 already, or no project) */
        proj_bound(p);
        proj_drums_to_part(p);                     /* (what a load converts, here first: a pattern's automation */
        proj_phys(p);                              /* comes as it is stored) */
        proj_fm4(p);
        proj_perc(p);
        for (k = 0; k < NTRK; k++) {
            for (i = used = 0; i < NSTEP; i++)
                used |= p->t[k].step[i].n | p->t[k].step[i].hit;
            for (i = 0; i < p->motion.count; i++)
                used |= (p->motion.event[i].place >> 6) == k;
            if (!used || p->t[k].preset == PROJ_DEF_SOUND)
                continue;                          /* (empty, or its steps cleared when it loads: no pattern) */
            for (i = 0; i < 5u; i++)
                tv[i] = p->t[k].p[BK_TIME[i]];
            if (!bk_pack_raw(bk_rec, k, p->t[k].step, tv, &p->motion, (p->motion.on >> k) & 1u))
                break;
            if ((id = pool_find(bk_rec)) == NPAT && ((id = pool_alloc()) == NPAT || pat_keep_now(id, bk_rec)))
                break;                             /* (the pool full) */
            p->ent[k][0][0] = (uint8_t)(id + 1u);
        }
        while (pend_free() < PEND_N)
            if (pat_flush_one())
                return wrote;                      /* (a flash error: the rest another time) */
        if (k < NTRK || !proj_pack2(&proj_wire, p, PROJ_SKF) || st_save(obj, &proj_wire, PROJ_SKF->size))
            continue;                              /* (this one as it is) */
        if (o < 4u)
            memcpy(&proj_slot[o], &proj_wire, sizeof proj_wire);
        wrote = 1;
    }
#else
    (void)allowed;
#endif
    return wrote;
}
#endif
