#!/usr/bin/env python3
"""Skiff 2.0, Tier B step 6 (design/TIER_B.md 11): where a DRUM lane's PAN is set (#36) and how a song entry gets its
sound (#30), each a choice to answer with a letter. #20 (the user slot stored, so HOME names the right sound after a
load) changes no screen. Screens start from the firmware's renders (tests/ui_render.c, SKIFF: lanes, song); what
changes is drawn in their geometry (ui_graph.c graph_lanes, graph_song) and their own colours, sampled.
Writes out/V2_f<area><letter>_<screen>.png (1x) and mockups/v2_feats.png.
  P6_RENDERS=~/skiff-v2-fast/build/ui_new/ppm python3 mock_v2_feats.py
"""
from mock_v2 import kit, save, cards, retitle, counter, Sheet
from mock_v2_songs import at, foot, sg_x, sg_y, song_colours, sg_tile, song_cards

LANES = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"]
PANS = [0, -20, 12, -36, -48, 40, 24, 0]           # a kit spread out: kick and bell C, hats left, toms right


def pan_txt(v):
    return "C" if v == 0 else ("L" if v < 0 else "R") + str(abs(v) * 100 // 64)


# ---------------------------------------------------------------- 1  A LANE'S PAN (#36)
# graph_lanes (ui_graph.c): lane i a column at x 3 + 29 i: its fader's track x + 12 .. + 15 over 76 px, the level a
# fill and a 12 x 6 cap; the name on a 24 x 16 tile under it (the picked one's white). Here the fader is 20 px shorter
# and a pan row sits under it: a 20 px line, the pan a 4 x 6 mark on it (C its middle)
def canvas_y(s):
    """the panel canvas's top: the faders' tracks start 6 below it (x 16: lane 0's track; x 6 the panel's surface)"""
    px = s.img.load()
    return next(y for y in range(80, 200) if px[16, y] != px[6, 90]) - 6


def lanes_graph(s, picked, k=3):
    y0 = canvas_y(s)
    surf, raise_ = at(s, 6, 90), s.c["line"]
    s.box(1, y0, 238, 213, fill=surf)
    for i in range(8):
        x, sel = 3 + 29 * i, i == picked
        col = s.c["text"] if sel else s.trk[k]
        h = 56 * 127 // 127
        s.g.rounded_rectangle([x + 12, y0 + 6, x + 15, y0 + 61], 2, fill=raise_)
        s.g.rounded_rectangle([x + 12, y0 + 61 - h + 4, x + 15, y0 + 61], 2, fill=col)
        s.g.rounded_rectangle([x + 8, y0 + 58 - h + 4, x + 19, y0 + 63 - h + 4], 3, fill=col)
        cy = y0 + 72                                 # the pan row
        s.g.line([(x + 4, cy), (x + 23, cy)], fill=raise_, width=2)
        s.g.line([(x + 13, cy - 3), (x + 13, cy + 3)], fill=raise_)
        px = x + 13 + PANS[i] * 9 // 64
        s.g.rectangle([px - 2, cy - 3, px + 2, cy + 3], fill=col)
        s.text((x + 14, cy + 17), pan_txt(PANS[i]), "lab", s.c["text"] if sel else s.c["mid"], anchor="ms")
        if sel:
            s.box(x + 2, y0 + 92, x + 25, y0 + 107, fill=s.c["text"])
        s.text((x + 14, y0 + 104), LANES[i], "lab", s.c["ink"] if sel else s.c["mid"], anchor="ms")


def f1a_lanemix():
    s = kit("lanes")
    retitle(s, "LANE MIX")
    cards(s, [("LANE", "SD", "", 2 / 8), ("LEVEL", "100", "%", 1.0), ("PAN", "L31", "", 0.35), ("", "", "", None)], hot=2,
          k=3)
    lanes_graph(s, 1)
    counter(s, "4/4")
    return save(s, "f1a_lanemix")


def f1b_lanes():
    s = kit("lanes")
    retitle(s, "LANES SD")
    cards(s, [("LEVEL", "100", "%", 1.0), ("PAN", "L31", "", 0.35), ("TUNE", "0.0", "st", 0.5), ("DECAY", "0", "%", 0.5)],
          hot=1, k=3)
    lanes_graph(s, 1)
    foot(s, "PRESETS: LANE")
    return save(s, "f1b_lanes")


# ---------------------------------------------------------------- 2  AN ENTRY'S SOUND (#30)
# graph_song: row r a band at y 80 + 22 r, entry p a 13 x 15 tile at x sg_x(p). An entry with a sound: its tile's top
# right corner cut, a 4 px step (the ground's colour)
def sound_mark(s, r, p, ground):
    x, ty = sg_x(p), sg_y(r) + 2
    for d in range(4):
        s.g.line([(x + 9 + d, ty), (x + 12, ty + 3 - d)], fill=ground)


MARKS = [(0, 0), (1, 0), (1, 3), (3, 4), (5, 2)]  # (row, entry): T1 A, T2 A and D, T4 E, T6 C


def marks(s):
    for r, p in MARKS:
        sound_mark(s, r, p, song_colours(s, r)["band"])


def f2a_pick():
    s = kit("song")
    marks(s)
    song_cards(s, "C", "SOUND", "2", "ON", 1)
    foot(s, "OCT+: SET SOUND STRINGS")
    return save(s, "f2a_pick")


def f2a_none():
    s = kit("song")
    marks(s)
    sound_mark(s, 1, 2, song_colours(s, 1)["band"])
    foot(s, "OCT+: PLAY    SOUND STRINGS")
    return save(s, "f2a_none")


def f2b_sounds():
    s = kit("song")
    retitle(s, "SOUNDS")
    marks(s)
    cards(s, [("ENTRY", "C", "", 3 / 16), ("SOUND", "STRINGS", "", None), ("", "", "", None), ("", "", "", None)], hot=1,
          k=1)
    foot(s, "OCT+: SET SOUND")
    counter(s, "6/6")
    return save(s, "f2b_sounds")


def sheet_feats():
    sh = Sheet("2.0  LANE PANS AND AN ENTRY'S SOUND",
               "Step 6 of Tier B (design/TIER_B.md 11). #36: each DRUM or DRUM-X lane gets its own PAN, as the track's "
               "PAN works, the kit through DRIVE, the INSERT and the sends as now. #30: a song entry may carry a sound, "
               "loaded as the entry comes in, at the same step as its pattern (a program change). #20 needs no screen: a "
               "project keeps which user sound a track came from, so HOME names it after a load. Answer with a letter "
               "for each of 1 and 2.")
    sh.label("TODAY")
    sh.row([(kit("lanes").img, "LANES 3/3 (DRUM): LANE LEVEL TUNE DECAY"),
            (kit("dx_sound_3").img, "SOUND (DRUM-X): a lane's sheet"), (kit("song").img, "SONG 5/5: KNOB 2 picks")])
    sh.head("1  WHERE A LANE'S PAN IS SET (#36)")
    sh.opt("A", "A PAGE, LANE MIX, FOR BOTH DRUM ENGINES", rec=True)
    sh.row([(f1a_lanemix(), "LANE MIX 4/4: LANE  LEVEL  PAN")])
    sh.para("After LANES on DRUM and after SOUND on DRUM-X: KNOB 1 the lane (struck keys follow, as on the grid), 2 its "
            "LEVEL (the same as LANES' and the sheet's), 3 its PAN. The eight lanes as faders with their pans under "
            "them. LANES and the SOUND sheet stay as they are. About 0.4 KB of the 1.2.")
    sh.opt("B", "PAN ON THE ENGINES' OWN PAGES")
    sh.row([(f1b_lanes(), "LANES SD 3/3: LEVEL  PAN  TUNE  DECAY")])
    sh.para("LANES picks its lane with PRESETS (as DRUM-X's sheet does), which frees KNOB 1 for PAN: LEVEL PAN TUNE "
            "DECAY. DRUM-X's sheet: the LEVEL row becomes LEVEL PAN, its knobs COLR A  COLR B  LEVL  PAN, DRIVE joins "
            "the MODE row's knobs (DRV WAVE FILT SNAP) and P SHAPE gets a window of its own. No new page; the grid's "
            "KNOB 1 stays the lane. About 0.3 KB of the 1.2.")
    sh.head("2  HOW AN ENTRY GETS ITS SOUND (#30)")
    sh.opt("A", "TWO MORE PICKS ON SONG'S KNOB 2", rec=True)
    sh.row([(f2a_pick(), "SONG: KNOB 2 at SOUND"), (f2a_none(), "SONG, no pick: C's sound named")])
    sh.para("KNOB 2 runs on past CLEAR to SOUND (OCT+ puts the track's sound now on the entry: SET SOUND STRINGS) and NO "
            "SOUND. You find a sound as always, with PRESETS, and hear it; then you put it on the entry. An entry with a "
            "sound has its tile's corner cut, and with no pick the footer names the sound of KNOB 1's entry.")
    sh.opt("B", "A PAGE, SOUNDS, AFTER SONG")
    sh.row([(f2b_sounds(), "SOUNDS 6/6: ENTRY  SOUND")])
    sh.para("KNOB 1 the entry, KNOB 2 its sound browsed as PRESETS lists them (none, every engine's presets, the user "
            "sounds), OCT+ puts it; the rows as SONG's, the corners cut the same. A sound is chosen by its name, not "
            "heard first (PRESETS on the track still auditions). About 0.5 KB more than A.")
    sh.save("v2_feats.png")


if __name__ == "__main__":
    sheet_feats()
