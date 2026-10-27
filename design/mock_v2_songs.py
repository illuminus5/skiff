#!/usr/bin/env python3
"""Skiff 2.0: PATTERNS AND SONGS, the asks on the pattern lists and the song as a sheet of options to answer with a
letter for each (#32 NEW, #33 COPY+, #34 a copy's number, #44 chains, #47 the song once through, #46 the pool's actions;
#45 longer patterns as a question). Screens start from the firmware's renders (tests/ui_render.c, SKIFF: song,
song_copy, song_playing, pool, confirm_pool); what changes is drawn in their geometry (ui_graph.c graph_song, graph_bank)
and their own colours, sampled.
Writes out/V2_s<area><letter>_<screen>.png (1x) and mockups/v2_songs.png.
  P6_RENDERS=~/skiff-v2-quick/build/ui_new/ppm python3 mock_v2_songs.py
"""
from mock_v2 import kit, save, cards, header_way, Sheet


def at(s, x, y):
    return s.img.getpixel((x, y))


def foot(s, t):
    """the footer's hint, left (its right side, the page's icon and count, kept)"""
    s.box(0, 219, 150, 239, fill=s.c["bg"])
    s.text((8, 234), t, "lab", "mid")


# ---------------------------------------------------------------- SEQ > SONG, as graph_song draws it
# row r: a band 20 tall at y 80 + 22 r (the canvas from y 76, SG_Y 4 + 22 r); entry p: a tile 13 x 15 at x SG_X(p) =
# 22 + (134 p + 5) / 10, 2 px into the band, its neighbours touching it; its number S, centred by its advance
def sg_x(p):
    return 22 + (p * 134 + 5) // 10


def sg_y(r):
    return 80 + 22 * r


def song_colours(s, r):
    """row r's colours from the render: its ground (the band's, else the black), a tile of it not playing, its INK"""
    y = sg_y(r)
    return dict(band=at(s, 18, y + 10), tile=at(s, sg_x(0) + 1, y + 3), ink=s.c["ink"], text=s.c["text"])


def sg_clear(s, r, p, ground):
    """the tile's own 13 x 15 and the ticks' row under it"""
    x, ty = sg_x(p), sg_y(r) + 2
    s.g.rectangle([x, ty, x + 12, ty + 15], fill=ground)


def sg_tile(s, r, p, fill, text, ink, ground, ticks=0):
    """entry p of row r: a tile, its number (or a REST's dash: text '-'), a tick under it for each repeat past 1"""
    x, ty = sg_x(p), sg_y(r) + 2
    sg_clear(s, r, p, ground)
    s.g.rectangle([x, ty, x + 12, ty + 14], fill=fill)
    if text == "-":
        s.g.rectangle([x + 3, ty + 6, x + 9, ty + 7], fill=ink)
    elif text:
        s.text((x + (13 - s.tw(text, "lab")) // 2, ty + 12), text, "lab", ink, anchor="ls")
    for k in range(min(ticks, 3)):
        s.g.rectangle([x + 1 + 3 * k, ty + 16, x + 2 + 3 * k, ty + 16], fill=s.c["ink"] if ground != s.c["bg"] else ink)


def sg_frame(s, r, p, col):
    """the frame of a staged entry, or (white) of an empty one KNOB 1 picked (cv_frame x - 1 .. x + 13, ty - 1 .. 15)"""
    x, ty = sg_x(p), sg_y(r) + 2
    s.g.rectangle([x - 1, ty - 1, x + 13, ty + 15], outline=col)


def sg_dot(s, r, p, col, ground):
    """an empty entry: a 2 x 2 dot in the tile's middle"""
    x, ty = sg_x(p), sg_y(r) + 2
    sg_clear(s, r, p, ground)
    s.g.rectangle([x + 5, ty + 6, x + 6, ty + 7], fill=col)


def sg_bracket(s, r, p0, p1, col):
    """(new) a chain: a bar over entries p0 .. p1, the band's two rows above the tiles"""
    s.g.rectangle([sg_x(p0) + 1, sg_y(r), sg_x(p1) + 11, sg_y(r) + 1], fill=col)


def song_cards(s, entry, pat, reps, song, hot):
    cards(s, [("ENTRY", entry, "", (ord(entry) - 64.5) / 16), ("PATTERN", pat, "", 6 / 64 if pat == "06" else None),
              ("REPS", reps, "x" if reps[0].isdigit() else "", 2 / 16 if reps == "2" else None),
              ("SONG", song, "", 1.0)], hot=hot, k=1)


def unpick_c(s, c):
    """track 2's entry C (06, two passes) as a tile of the band, not KNOB 1's any more"""
    sg_tile(s, 1, 2, c["tile"], "06", c["ink"], c["band"], ticks=1)


def check_tiles():
    """a tile redrawn as graph_song draws it must equal the render's: track 2's B (05 on its band)"""
    s = kit("song")
    c = song_colours(s, 1)
    before = s.img.copy()
    sg_tile(s, 1, 1, c["tile"], "05", c["ink"], c["band"])
    box = (sg_x(1) - 1, sg_y(1) + 1, sg_x(1) + 14, sg_y(1) + 19)
    a, b = before.crop(box), s.img.crop(box)
    off = sum(1 for p, q in zip(a.getdata(), b.getdata()) if max(abs(i - j) for i, j in zip(p, q)) > 24)
    print(f"song tile redrawn: {off} px off (the digits' anti-aliasing)")


# ---------------------------------------------------------------- 1  NEW AND COPY IN SONG (#32, #33)
def s1a_new():
    s = kit("song")
    c = song_colours(s, 1)
    unpick_c(s, c)
    sg_frame(s, 1, 5, c["text"])                     # F, empty, KNOB 1's
    song_cards(s, "F", "NEW", "1", "ON", 1)
    foot(s, "OCT+: NEW 22")
    return save(s, "s1a_new")


def copy_after(name, label):
    s = kit("song")
    c = song_colours(s, 1)
    sg_tile(s, 1, 5, c["tile"], "22", c["ink"], c["band"])   # where the copy goes: F, after the block C D E
    sg_frame(s, 1, 5, c["text"])
    song_cards(s, "C", label, "2", "ON", 1)
    foot(s, "OCT+: COPY 06 TO 22 AT F")
    return save(s, name)


def s1a_copyplus():
    return copy_after("s1a_copyplus", "COPY+")


def s1b_copy():
    return copy_after("s1b_copy", "COPY")


# ---------------------------------------------------------------- 2  A COPY'S NUMBER (#34)
# SAVE > PATTERNS (graph_bank): pattern i's tile 28 x 15 at x 1 + 30 (i % 8), y 78 + 17 (i / 8), its number centred
def pv_xy(i):
    return 1 + 30 * (i % 8), 78 + 17 * (i // 8)


def pv_tile(s, i, fill, text, ink, outline=None):
    x, y = pv_xy(i)
    s.g.rectangle([x, y, x + 27, y + 14], fill=fill, outline=outline)
    s.text((x + (28 - s.tw(text, "lab")) // 2, y + 12), text, "lab", ink, anchor="ls")


def s2a_song():
    s = kit("song_copy")
    foot(s, "OCT+: COPY 06 TO 08")
    return save(s, "s2a_song")


def s2a_pool():
    s = kit("pool")
    surf = at(s, pv_xy(0)[0] + 1, pv_xy(0)[1] + 1)
    pv_tile(s, 22, surf, "23", s.c["mid"], outline=s.c["dim"])   # 23: nothing uses it, not picked now
    pv_tile(s, 7, s.c["text"], "08", s.c["ink"])                  # the copy of 06: 08, the first free after it
    cards(s, [("PATTERN", "08", "", 8 / 64), ("LENGTH", "16", "", None), ("USED", "1", "x", None),
              ("CLEAR", "OCT+", "", None)], hot=0)
    return save(s, "s2a_pool")


def s2b_confirm():
    """the pool's dialog (confirm_pool) said for a copy that renumbers"""
    s = kit("confirm_pool")
    surf = s.c["surf"]
    s.box(17, 66, 222, 132, fill=surf)
    s.text((119.5, 85), "COPY 06 AS 07?", "m", "text", anchor="ms")
    s.text((119.5, 100), "07 to 24 move up one,", "lab", "mid", anchor="ms")
    s.text((119.5, 113), "in every list and project", "lab", "mid", anchor="ms")
    s.text((119.5, 127), "can't be undone", "lab", s.c["rec"], anchor="ms")
    s.box(124, 137, 210, 163, fill=surf)
    s.text((167, 155), "COPY", "m", s.c["rec"], anchor="ms")
    return save(s, "s2b_confirm")


# ---------------------------------------------------------------- 3  CHAINS (#44)
def s3a_chain():
    s = kit("song")
    c = song_colours(s, 1)
    unpick_c(s, c)
    sg_tile(s, 1, 0, c["text"], "05", c["ink"], c["band"])          # A: KNOB 1's, on CHAIN
    sg_bracket(s, 1, 0, 2, c["ink"])                                # A B C one block, played twice (C's REPS)
    song_cards(s, "A", "05", "CHAIN", "ON", 2)
    foot(s, "OCT+: PLAY")
    return save(s, "s3a_chain")


def s3b_block():
    s = kit("song")
    c = song_colours(s, 1)
    sg_bracket(s, 1, 0, 2, c["ink"])
    song_cards(s, "C", "06", "2", "ON", 2)
    header_way(s, "CHAIN A-C 3x")
    foot(s, "OCT+: PLAY")
    return save(s, "s3b_block")


# ---------------------------------------------------------------- 4  THE SONG ONCE THROUGH (#47)
def s4a_once():
    s = kit("song_playing")
    c = song_colours(s, 1)
    s.box(100, 2, 211, 20, fill=s.c["bg"])
    s.text((209, 17), "SONG", "lab", "mid", anchor="rs")
    s.text((209 - s.tw("SONG", "lab") - 6, 17), "SONG ONCE", "lab", c["band"], anchor="rs")
    song_cards(s, "C", "06", "2", "ONCE", 3)
    return save(s, "s4a_once")


# ---------------------------------------------------------------- 5  THE POOL'S ACTIONS (#46)
def pool_pick(s, i, text):
    """the pick moved from 23 to pattern i (a used one: TEXT, INK on it)"""
    surf = at(s, pv_xy(0)[0] + 1, pv_xy(0)[1] + 1)
    pv_tile(s, 22, surf, "23", s.c["mid"], outline=s.c["dim"])
    pv_tile(s, i, s.c["text"], text, s.c["ink"])


def s5a_empty():
    s = kit("pool")
    pool_pick(s, 20, "21")
    cards(s, [("PATTERN", "21", "", 21 / 64), ("LENGTH", "16", "", None), ("USED", "2", "x", None),
              ("ACTION", "EMPTY", "", None)], hot=3)
    foot(s, "OCT+: EMPTY 21")
    return save(s, "s5a_empty")


def s5a_unused():
    s = kit("pool")
    cards(s, [("PATTERN", "23", "", 23 / 64), ("LENGTH", "16", "", None), ("USED", "0", "x", None),
              ("ACTION", "UNUSED", "", None)], hot=3)
    for i in (16, 32):                                              # 17 and 33: the other unused ones, lit with it
        pv_tile(s, i, s.c["surf"], f"{i + 1:02d}", s.c["text"], outline=s.c["text"])
    foot(s, "OCT+: CLEAR 3 UNUSED")
    return save(s, "s5a_unused")


def s5b_range():
    s = kit("pool")
    surf = at(s, pv_xy(0)[0] + 1, pv_xy(0)[1] + 1)
    for i in (20, 21, 23):                                          # 21 22 24 in the range, 23 the cursor's end
        pv_tile(s, i, surf, f"{i + 1:02d}", s.c["text"], outline=s.c["text"])
    cards(s, [("PATTERN", "21-24", "", None), ("LENGTH", "16", "", None), ("USED", "5", "x", None),
              ("ACTION", "CLEAR", "", None)], hot=3)
    foot(s, "OCT+: CLEAR 23 (3 USED)")
    return save(s, "s5b_range")


# ---------------------------------------------------------------- the sheet
def sheet_songs():
    sh = Sheet("2.0  PATTERNS AND SONGS",
               "The asks on SEQ > SONG and SAVE > PATTERNS (GitHub #32 #33 #34 #44 #46 #47, most of them awchx's, and "
               "the part the comments ask about most: how patterns, entries and the song fit together). Each area has a "
               "recommended letter; the screens are the firmware's own where nothing changes. #45 (longer patterns) is "
               "a question at the end. Answer with a letter for each of 1 to 6, or NO for an area to leave as it is.")
    sh.label("TODAY")
    sh.row([(kit("song").img, "SONG: KNOB 2 a pattern, REST, COPY, CLEAR"),
            (kit("song_copy").img, "COPY: the entry gets its own copy"),
            (kit("pool").img, "SAVE > PATTERNS: KNOB 4 CLEAR")])
    sh.head("1  NEW AND COPY (#32, #33)")
    sh.opt("A", "NEW AND COPY+ ON KNOB 2", rec=True)
    sh.row([(s1a_new(), "F empty: NEW, a new empty pattern"), (s1a_copyplus(), "C: COPY+, the copy at F")])
    sh.para("KNOB 2 runs REST, the kept patterns, then NEW, COPY, COPY+, CLEAR. NEW: a new empty pattern, the first free "
            "number, in the entry (on an empty entry the first choice after --; recorded into or edited, it is kept as "
            "patterns are). COPY+ (#33): a copy of the entry's pattern into the first free entry after its block (C D E "
            "here: F), the entry as it is; not offered when no free entry follows. COPY stays as today (the entry gets "
            "its own copy, in place). About +0.4 KB.")
    sh.opt("B", "NEW, AND COPY ALWAYS AFTER")
    sh.row([(s1b_copy(), "C: COPY puts the copy at F")])
    sh.para("As A, but one COPY, always after the block (#33's way); today's in-place copy goes, so giving an entry its "
            "own copy where it is takes COPY, then CLEAR on the old entry. KNOB 2 one value shorter. About +0.3 KB.")
    sh.head("2  A COPY'S NUMBER (#34)")
    sh.opt("A", "NEXT TO ITS ORIGINAL", rec=True)
    sh.row([(s2a_song(), "COPY 06 TO 08: the first free after 06"), (s2a_pool(), "the pool: 08 beside 06 and 07")])
    sh.para("A copy takes the first free number after its original, not the first free of all: 06's copy is 08 when 07 "
            "is used, so copies sit beside their originals in the pool. Nothing else is renumbered, no other project is "
            "touched. About +40 B.")
    sh.opt("B", "RENUMBER, AS #34 ASKS")
    sh.row([(s2b_confirm(), "COPY 06 AS 07: the rest move up")])
    sh.para("The copy takes the very next number and every pattern after it moves up one: in every list, in each of "
            "the four saved projects and the autosave (the pool is shared by all of them), stopped only, after a "
            "dialog. Moving patterns rewrites most of the pool's 16 flash chunks (about a second), and a power cut "
            "midway must leave every project consistent: the most work and risk here. MENU > CONTROL > COPY NUMBER: "
            "END (as A) or NEXT. About +1.5 KB.")
    sh.head("3  CHAINS (#44)")
    sh.label("TODAY: each entry plays its REPS, then the next")
    sh.row([(kit("song").img, "entry C: REPS 2x (a tick under it)")])
    sh.opt("A", "CHAIN ON KNOB 3, BELOW 1x", rec=True)
    sh.row([(s3a_chain(), "A and B on CHAIN, C 2x: A B C A B C")])
    sh.para("REPS gets CHAIN below 1x: an entry on CHAIN plays once and goes on into the next; the block's last entry's "
            "REPS repeat the whole block (A B C A B C here). The rows draw a block as one bar over its tiles. A "
            "track's list may hold several blocks. CHAIN is a value of the entry's REPS byte that 1.4 does not know "
            "(2.0's projects do not open on 1.4 anyway). About +0.3 KB.")
    sh.opt("B", "A CHAIN WITH ITS OWN REPEATS")
    sh.row([(s3b_block(), "A-C a block, 3x; C its own 2x inside")])
    sh.para("Each entry keeps its REPS, and a block (marked from its first entry to its last, SHIFT + KNOB 3) repeats "
            "as a whole its own number of times: A B C C, three times. More to set and to read (the count shows only "
            "while turned), a byte more per entry (2.0's project format). About +0.8 KB.")
    sh.head("4  THE SONG ONCE THROUGH (#47)")
    sh.label("TODAY: SONG ON, each track loops its list; GLO + PLAY starts every track from its first entry")
    sh.row([(kit("song_playing").img, "SONG ON, playing")])
    sh.opt("A", "SONG ONCE ON KNOB 4", rec=True)
    sh.row([(s4a_once(), "SONG ONCE, playing")])
    sh.para("KNOB 4 SONG: OFF, ON, ONCE. ONCE: PLAY starts every track from its first entry (or REST), each plays its "
            "list once with its REPS and chains and stops after its last entry, and the transport stops when the last "
            "track ends. The header says SONG ONCE. (#47's loop of the whole song, every track restarting together, "
            "would be a fourth value later.) About +0.3 KB.")
    sh.opt("B", "GLO + PLAY PLAYS IT ONCE")
    sh.para("No new value: with SONG ON, GLO + PLAY (already: every track from its first entry) plays the song once and "
            "stops; PLAY alone loops as today. Nothing on the screen says which way it plays. About +0.1 KB.")
    sh.head("5  THE POOL'S ACTIONS (#46)")
    sh.opt("A", "KNOB 4 ACTION: CLEAR, EMPTY, CLEAR UNUSED", rec=True)
    sh.row([(s5a_empty(), "EMPTY 21: its steps go, its entries stay"), (s5a_unused(), "CLEAR UNUSED: 17 23 33")])
    sh.para("KNOB 4 (was CLEAR) picks the action, OCT+ does it, stopped, after a dialog. CLEAR: an unused pattern, as "
            "today. EMPTY (#46's PURGE): the pattern's steps and automation go, its number and every entry naming it "
            "stay (a used one too: the dialog says how many entries go quiet). CLEAR UNUSED (#46's CLR ALL, kept to "
            "what nothing uses): every unused pattern at once, the dialog says how many. #46's NEW is SONG's NEW (1): "
            "a pattern lives on an entry. About +0.5 KB.")
    sh.opt("B", "A RANGE, AS NOTES' SELECTION")
    sh.row([(s5b_range(), "21-24 picked: CLEAR the unused one")])
    sh.para("As A, and SHIFT + PRESETS widens the pick from KNOB 1's pattern into a range (as NOTES' selection); CLEAR "
            "and EMPTY work on the range (CLEAR only on its unused patterns). About +0.9 KB.")
    sh.head("6  LONGER PATTERNS (#45)")
    sh.opt("A", "64 STEPS STAY", rec=True)
    sh.para("Four bars a pattern; a longer phrase is a chain (3) of patterns, which the 16 entries hold. Nothing changes.")
    sh.opt("B", "128 STEPS (8 BARS)")
    sh.para("LEN up to 128, NOTES and the DRUM grid 8 pages. The six tracks' steps take 5 KB more RAM, each undo copy "
            "half again as big (the ring would hold 5, not 8), a pattern record twice the size (the pool's flash halves "
            "to 32 patterns, or 2.0's format grows it), copies and the waiting patterns too. Possible, but the RAM it "
            "takes is most of what 2.0's other asks need.")
    sh.opt("C", "256 STEPS (16 BARS)")
    sh.para("Does not fit the RAM: the steps alone would take 15 KB more.")
    sh.save("v2_songs.png")


# ---------------------------------------------------------------- 7  TWO PATTERNS AS ONE (the user's idea for #45)
def j_song_chain():
    """track 2: C (06) on CHAIN into D (07): one pattern of 128 steps wherever it plays"""
    s = kit("song")
    c = song_colours(s, 1)
    sg_bracket(s, 1, 2, 3, c["ink"])
    song_cards(s, "C", "06", "CHAIN", "ON", 2)
    foot(s, "OCT+: PLAY")
    return save(s, "s7a_song")


def j_notes():
    """NOTES on T1's A-B (01 + 02), its step 69: the second pattern's step 5"""
    s = kit("roll_acid")
    s.box(100, 2, 211, 20, fill=s.c["bg"])
    s.text((209, 17), "NOTES 69", "lab", "mid", anchor="rs")
    s.text((209 - s.tw("NOTES 69", "lab") - 6, 17), "A-B · 02", "lab", s.trk[0], anchor="rs")
    cards(s, [("PITCH", "A2", "", None), ("START", "69", "/128", None), ("LENGTH", "1", "STEP", None),
              ("VELOCITY", "96", "", 96 / 127)], hot=None, k=0)
    return save(s, "s7a_notes")


def j_song_join():
    s = kit("song")
    c = song_colours(s, 1)
    song_cards(s, "C", "JOIN", "2", "ON", 1)
    foot(s, "OCT+: JOIN 06 + 07 (D)")
    return save(s, "s7b_join")


def j_pool():
    """the pool: 06 and 07 joined, one tile over both places"""
    s = kit("pool")
    x0, y0 = pv_xy(5)
    x1, _ = pv_xy(6)
    s.g.rectangle([x0, y0, x1 + 27, y0 + 14], fill=s.c["surf"])
    s.text(((x0 + x1 + 27) / 2, y0 + 12), "06 + 07", "lab", s.c["text"], anchor="ms")
    return save(s, "s7b_pool")


def j_song_split():
    s = kit("song")
    c = song_colours(s, 1)
    x0, x1, ty = sg_x(2), sg_x(3) + 12, sg_y(1) + 2                  # C: 06 + 07 as one, its tile over C and D
    s.g.rectangle([x0, ty, x1, ty + 15], fill=c["band"])
    s.g.rectangle([x0, ty, x1, ty + 14], fill=c["text"])           # (one tile: KNOB 1's, the two halves' numbers)
    for p, t in ((2, "06"), (3, "07")):
        s.text((sg_x(p) + (13 - s.tw(t, "lab")) // 2, ty + 12), t, "lab", c["ink"], anchor="ls")
    s.g.rectangle([x0 + 1, ty + 16, x0 + 2, ty + 16], fill=c["ink"])   # (its REPS 2x)
    song_cards(s, "C", "SPLIT", "2", "ON", 1)
    foot(s, "OCT+: SPLIT 07 TO D")
    return save(s, "s7b_split")


def sheet_join():
    sh = Sheet("2.0  TWO PATTERNS AS ONE",
               "Instead of longer patterns (#45, 6): two patterns combined into one longer one, played and edited as "
               "one until split. A track keeps one pattern in RAM as today, so this costs no RAM: at the first one's "
               "end the second comes in without a gap, as SONG and PERFORM's launches already switch patterns. "
               "Answer with a letter.")
    sh.opt("A", "A CHAIN IS ONE PATTERN", rec=True)
    sh.row([(j_song_chain(), "SONG: C on CHAIN into D (3A)"), (j_notes(), "NOTES on A-B: step 69, the 2nd's 5th")])
    sh.para("3A's CHAIN made into the whole idea: entries on CHAIN into the next are one pattern wherever the track plays "
            "them, not only with SONG on: the track loops the whole chain (SONG OFF), a PERFORM key on any of its "
            "entries launches it from its first, SONG plays it as one block. NOTES, FEEL and the DRUM grid page through "
            "it as one (16 steps a page: 1-4 the first pattern, 5-8 the second), the steps counted on (NOTES 69); "
            "stopped, the cursor moves into the next pattern as the track switches to it; playing, the pages follow the "
            "one playing. COPY+ on a chain copies all of it. SPLIT: REPS back to a number. Two patterns or more (16 "
            "entries: up to 1,024 steps), each keeping its LEN, DIV, SWING. Stored in the lists (3A's CHAIN): no new "
            "pattern format. About +1 KB with 3A.")
    sh.opt("B", "JOINED PATTERNS IN THE POOL")
    sh.row([(j_song_join(), "SONG: JOIN C's 06 and D's 07"), (j_pool(), "the pool: 06 + 07, one pattern"),
            (j_song_split(), "C plays 06 + 07; SPLIT gives D 07")])
    sh.para("The join is the patterns' own: 06 + 07 is one pattern in the pool, wherever it is named (every entry, every "
            "project), played and edited as one (pages 5-8 its second half). KNOB 2's JOIN joins the entry's pattern "
            "with the next entry's (one tile over both entries); SPLIT gives the next entry its own back. COPY copies "
            "both, CLEAR clears both. Two patterns at most; a pattern in one pair only. A pattern record's new field "
            "(2.0's format, Tier B). About +1.5 KB.")
    sh.save("v2_join.png")


if __name__ == "__main__":
    check_tiles()
    sheet_songs()
    sheet_join()
