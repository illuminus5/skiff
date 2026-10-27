#!/usr/bin/env python3
"""Skiff 2.0, the plan's Tier D: the merges, direction B as built, each a sheet of options to answer with a letter:
  1  STEP PAGES: STEP ROLL CHANCE PLAY as two pages; KNOB 1 (the step, as PRESETS) a value
  2  MUTES AND THE WAY BACK: one mute (GLO, FX's cuts, PERFORM's staged ones, MIXER); one return (MOMENTARY, PERFORM's
     throws, FX's macros)
  3  ONE MOD PAGE, ONE SHAPER: LFO DEST and ENV DEST in the matrix; DIST through INSERT's shapers
Screens start from the firmware's renders (tests/ui_render.c, SKIFF) where they keep what they have; the cards, titles,
footers and what else changes are drawn in their geometry and palette (the renders' own colours, sampled).
Writes out/V2_<screen>.png (1x) and mockups/v2_steps.png, v2_mutes.png, v2_mod.png.
  P6_RENDERS=~/skiff-v2-quick/build/ui_new/ppm python3 mock_v2.py
"""
from PIL import Image, ImageDraw
import mock
import mock_p6 as p6
from mock import mix, hexc, HERE
from mock_c27 import font, option, lines, BG, INK, MID

OUT = HERE / "out"
SHEETS = HERE / "mockups"
PAL = dict(bg=(8, 8, 8), surf=(24, 24, 24), text=(255, 250, 255), mid=(180, 178, 180), dim=(115, 109, 115),
           line=(49, 48, 49), ink=(8, 8, 8), rec=p6.q565(hexc("#FF3B30")))
T1 = p6.q565(hexc(p6.TRK6[0]))


def kit(base):
    """a screen on the firmware's render SKIFF_<base>, in its palette"""
    s = p6.kit(f"SKIFF_{base}")
    s.c.update(PAL)
    s.trk = [p6.q565(hexc(c)) for c in p6.TRK6]
    return s


def save(s, name):
    OUT.mkdir(exist_ok=True)
    s.img.save(OUT / f"V2_{name}.png")
    return s.img


# ---------------------------------------------------------------- the firmware's furniture, redrawn
def ink_at(s, x, t, f, right=False):
    """the pen x that puts t's ink edge (left, or right) at x (ui_blocks.c sets a value by its ink, not its pen)"""
    b = s.g.textbbox((0, 0), t, font=s.f[f], anchor="ls")
    return x - (b[2] if right else b[0])


def cards(s, items, hot=None, k=0):
    """K1..K4 (ui_blocks.c: 57 x 45 from x 3 + 59c, y 28): label S MID, value V by its ink from BK_L (M when long;
    DIM: VR_DIM), unit S on its baseline, right; the gauge the track's. An item: (label, value, unit, gauge 0..1 or
    None[, dim]). Checked against SKIFF_step_tiles: the same ink boxes to a pixel"""
    for c, it in enumerate(items):
        lab, val, unit, g = it[:4]
        dim = len(it) > 4 and it[4]
        x0, y0 = 3 + 59 * c, 28
        on = c == hot
        s.box(x0, y0, x0 + 56, y0 + 44, fill=s.c["text"] if on else s.c["surf"])
        if not lab:
            continue
        s.text((x0 + 4, y0 + 14), lab, "lab", s.c["ink"] if on else s.c["mid"])
        f = "val" if s.tw(val, "val") + (s.tw(unit, "lab") + 4 if unit else 0) <= 52 else "m"
        s.text((ink_at(s, x0 + 4, val, f), y0 + 37), val, f, s.c["ink"] if on else s.c["dim"] if dim else s.c["text"])
        if unit:
            s.text((ink_at(s, x0 + 54, unit, "lab", True), y0 + 37), unit, "lab", s.c["ink"] if on else s.c["mid"])
        if g:
            s.g.rectangle([x0, y0 + 42, x0 + round(56 * g), y0 + 44], fill=s.c["ink"] if on else s.trk[k])


def retitle(s, t, batt=True):
    """the page's name in the header, right (before the battery: its right edge at 208, as the renders')"""
    s.box(104, 2, 211 if batt else 239, 20, fill=s.c["bg"])
    s.text((209 if batt else 235, 17), t, "lab", "mid", anchor="rs")


def counter(s, t):
    """the footer's page count, right (an icon before it gone)"""
    s.box(150, 219, 239, 239, fill=s.c["bg"])
    s.text((232, 234), t, "lab", "mid", anchor="rs")


def hints(s, *items, right=None):
    """the footer as hints (KEY: WHAT), left to right; right: the page count"""
    s.box(0, 218, 239, 239, fill=s.c["bg"])
    x = 8
    for h in items:
        s.text((x, 234), h, "lab", "mid")
        x += s.tw(h, "lab") + 14
    if right:
        s.text((232, 234), right, "lab", "mid", anchor="rs")


def header_way(s, t):
    """a message where the page's name was (ui_way: after the BPM, white)"""
    s.box(100, 2, 239, 20, fill=s.c["bg"])
    s.text((107, 17), t, "lab", "text")


def panel(s, y0=76, y1=213):
    s.box(0, y0, 239, y1, fill=s.c["surf"])


# the tiles (ui_graph.c graph_tiles: 27 x 33, 29 apart from x 4, the rows 58 apart)
def tile_top(s):
    px = s.img.load()
    return next(y for y in range(76, 214) if px[8, y] not in (PAL["bg"],))


def tile_box(s, i):
    y0 = tile_top(s)
    x, y = 4 + 29 * (i % 8), y0 + 58 * (i // 8)
    return x, y, x + 26, y + 32


def tile(s, i, fill, text=None, ink=None, mark=False):
    """tile i repainted: its fill, a text where the note's name goes; mark: the 4 px INK block of a chance under 100 %"""
    x0, y0, x1, y1 = tile_box(s, i)
    s.g.rectangle([x0, y0, x1, y1], fill=PAL["bg"])   # (the old tile's corners off first)
    s.g.rounded_rectangle([x0, y0, x1, y1], 3, fill=fill)
    if text:
        s.text(((x0 + x1) / 2 + 0.5, y0 + 26), text, "lab", ink or s.c["ink"], anchor="ms")
    if mark:
        s.g.rectangle([x0 + 2, y0 + 2, x0 + 5, y0 + 5], fill=s.c["ink"])


def arrow(s, x, y, col):
    """MOD's list arrow: a shaft and its head, 18 px"""
    s.g.line([(x, y), (x + 17, y)], fill=col)
    s.g.line([(x + 13, y - 4), (x + 17, y), (x + 13, y + 4)], fill=col)


# ---------------------------------------------------------------- 1  STEP PAGES
def steps_today():
    shots = []
    for base, cap in [("step_tiles", "STEP 1/7: KNOB 1 the step"), ("roll_acid", "ROLL 2/7"),
                      ("chance_flam", "CHANCE 3/7"), ("play_gate", "PLAY 4/7")]:
        shots.append((kit(base).img, cap))
    return shots


def a_step():
    s = kit("step_tiles")
    cards(s, [("NOTE", "D#2", "", None), ("VELOCITY", "96", "", 96 / 127), ("LENGTH", "2", "STEPS", 2 / 16),
              ("TYPE", "ACC", "", None)], hot=2)
    tie = p6.q565(mix(T1, PAL["surf"], 0.5))
    tile(s, 5, tie)                                   # step 6 the note's tie now (LENGTH 2 STEPS)
    hints(s, "PRESETS: STEP", "REC: WRITE", right="1/5")
    return save(s, "a_step")


def a_feel():
    s = kit("roll_acid")
    retitle(s, "FEEL")
    cards(s, [("CHANCE", "65", "%", 0.65), ("RATCHET", "X2", "", None), ("COND", "1:2", "", None),
              ("NUDGE", "+12", "/64", None)], hot=0)
    counter(s, "2/5")
    return save(s, "a_feel")


def a_drum():
    s = kit("drum")
    cards(s, [("LANE", "SNARE", "", None), ("HIT", "ON", "", None), ("ACC", "ON", "", None),
              ("VELOCITY", "96", "", 96 / 127)], hot=0, k=3)
    counter(s, "1/5")
    return save(s, "a_drum")


def a_pattern():
    s = kit("pattern")
    hints(s, "PRESETS: DIR", right="3/5")
    y = 178                                           # DIR under the bars: the playhead's way, PING both ends
    s.g.line([(16, y), (224, y)], fill=s.c["mid"], width=1)
    s.g.polygon([(12, y), (20, y - 4), (20, y + 4)], fill=s.c["mid"])
    s.g.polygon([(228, y), (220, y - 4), (220, y + 4)], fill=s.c["mid"])
    s.text((120, y - 9), "DIR  PING", "lab", "text", anchor="ms")
    return save(s, "a_pattern")


def b_step_roll():
    s = kit("roll_chords")
    retitle(s, "STEP")
    cards(s, [("NOTE", "F3", "+3", None), ("VELOCITY", "96", "", 96 / 127), ("LENGTH", "50", "%", 0.5),
              ("TYPE", "NOTE", "", None)], hot=0)
    hints(s, "PRESETS: STEP", "SEQ: TILES", right="1/5")
    return save(s, "b_step_roll")


def b_feel_tiles():
    s = kit("step_tiles")
    retitle(s, "FEEL")
    cards(s, [("CHANCE", "65", "%", 0.65), ("RATCHET", "X1", "", None), ("COND", "--", "", None, True),
              ("NUDGE", "0", "", None, True)], hot=0)
    chance = {0: "100", 2: "50", 5: "100", 8: "100", 10: "100", 13: "25", 15: "100"}
    for i, v in chance.items():                       # the steps' CHANCE where their notes were (3 and 14 had the
        tile(s, i, T1, v, mark=v != "100")            # corner mark: under 100 %)
    tile(s, 4, s.c["text"], "65", mark=True)          # (the step edited: white)
    hints(s, "PRESETS: STEP", "SEQ: ROLL", right="2/5")
    return save(s, "b_feel_tiles")


def c_step():
    s = kit("step_tiles")
    cards(s, [("TYPE", "ACC", "", None), ("NOTE", "D#2", "", None), ("VELOCITY", "96", "", 96 / 127),
              ("CHANCE", "100", "%", 1.0)], hot=1)
    hints(s, "PRESETS: STEP", "REC: WRITE", right="1/5")
    return save(s, "c_step")


def c_roll():
    s = kit("roll_ties")
    cards(s, [("LENGTH", "2", "STEPS", 2 / 16), ("RATCHET", "X1", "", None), ("COND", "--", "", None, True),
              ("NUDGE", "0", "", None, True)], hot=0)
    counter(s, "2/5")
    return save(s, "c_roll")


def sheet_steps():
    sh = Sheet("2.0  STEP PAGES: FOUR INTO TWO",
               "A melodic track's steps are edited on four pages today: STEP (the tiles), ROLL (the roll), CHANCE and "
               "PLAY. KNOB 1 is the step on all four, which PRESETS already moves; CHANCE is on two of them. Each option "
               "makes them two pages, PRESETS stays the step (said in the footer, as now) and KNOB 1 gets a value. The "
               "screens are the firmware's renders with the cards, titles and footers redrawn. The pattern data stays as "
               "it is (a step's locks and ties as today). A merge is for the interface: each saves under 1 KB (the room "
               "is Tier A's). Answer with a letter.")
    sh.label("TODAY")
    sh.row(steps_today())
    sh.opt("A", "STEP AND FEEL: WHAT PLAYS, THEN WHEN IT PLAYS", rec=True)
    sh.row([(a_step(), "STEP 1/5 (tiles): LENGTH 2 STEPS"), (a_feel(), "FEEL 2/5 (the roll)"),
            (a_drum(), "DRUM: STEP 1/5 (the grid)"), (a_pattern(), "PATTERN 3/5: PRESETS = DIR")])
    sh.para("STEP: NOTE  VELOCITY  LENGTH  TYPE, on the tiles.   FEEL: CHANCE  RATCHET  COND  NUDGE, on the roll (a "
            "DRUM track: STEP is the grid with LANE  HIT  ACC  VELOCITY, FEEL the grid too).")
    sh.para("LENGTH: how long the note sounds: 1 .. 100 % of its step (the GATE lock PLAY's KNOB 4 sets today), then "
            "2 .. 16 STEPS, the steps after it becoming its ties (left: step 6 is step 5's tie now). #31's long notes on "
            "one knob, no key held.")
    sh.para("TYPE: NOTE  ACC  SLIDE  A+S  REST (ROLL's FLAG and TIME's REST; a tie is LENGTH's, and the cursor on a "
            "tied step shows TIE / TIE+SLIDE there).   DIR (the track's, PLAY's KNOB 3) moves to PATTERN, on PRESETS: "
            "the footer says so and the panel draws the way the playhead goes.")
    sh.para("SEQ: STEP  FEEL  PATTERN  AUTOMATION  SONG, five pages (seven today; a DRUM track six).   About -0.3 to "
            "-0.5 KB (CHANCE's and PLAY's own cards and knob paths go; LENGTH's ties and TYPE are new).", fill=INK)
    sh.opt("B", "THE SAME TWO PAGES, EACH AS TILES OR AS THE ROLL")
    sh.row([(b_step_roll(), "STEP on the roll (SEQ: TILES)"), (b_feel_tiles(), "FEEL on the tiles (SEQ: ROLL)")])
    sh.para("A's pages and knobs. SEQ tapped on either flips its view (kept per track): STEP on the roll is ROLL's "
            "chords and contour, FEEL on the tiles shows every step's value of the knob turned last (here CHANCE). "
            "About -0.2 KB: the tiles' values are new drawing.")
    sh.opt("C", "TODAY'S STEP PAGE KEPT, THE REST ON ONE MORE")
    sh.row([(c_step(), "STEP 1/5: TYPE on KNOB 1"), (c_roll(), "ROLL 2/5")])
    sh.para("STEP keeps NOTE, VELOCITY and CHANCE on KNOB 2, 3 and 4, as today; KNOB 1 is TYPE. ROLL: LENGTH  RATCHET  "
            "COND  NUDGE. DIR on PATTERN's PRESETS as in A. About -0.5 KB.")
    sh.save("v2_steps.png")


# ---------------------------------------------------------------- 2  MUTES AND THE WAY BACK
def glo_row(dst):
    """GLO's black keys (the speaker cells, y 182 .. 205) onto another map: FX's row has the same place"""
    src = kit("six_glo").img
    dst.img.paste(src.crop((0, 180, 240, 207)), (0, 180))


def m1a_fx():
    s = kit("six_fx")
    glo_row(s)
    hints(s, "WHITE: EFFECTS", "BLACK: MUTE", "K1-4: MACROS")
    return save(s, "m1a_fx")


def m1a_glo():
    s = kit("six_glo")
    return save(s, "m1a_glo")


def m1a_perform():
    s = kit("six_perform")
    return save(s, "m1a_perform")


def m1b_glo():
    s = kit("six_glo")
    x0 = 4 + 39 * 2                                   # A#3: track 3's cell, its MUTE staged for the bar
    s.box(x0 - 1, 180, x0 + 36, 205, outline=s.c["rec"], r=0)
    s.box(104, 2, 239, 20, fill=s.c["bg"])
    s.text((235, 17), "GLO SET  BAR", "lab", "mid", anchor="rs")
    return save(s, "m1b_glo")


def flt_to(s, dx, label):
    """FLT's response moved dx px up the axis (the cutoff's peak at x 97, its label at 102 .. 131 x 87 .. 95): the curve
    from x 84 on shifted, the flat part before it widened, the right margin as it was"""
    im = s.img
    keep = im.crop((229, 78, 240, 199))
    part = im.crop((84, 78, 240 - dx, 199))
    flat = im.crop((60, 78, 61, 199))
    im.paste(part, (84 + dx, 78))
    for x in range(84, 84 + dx):
        im.paste(flat, (x, 78))
    im.paste(keep, (229, 78))
    s.g = ImageDraw.Draw(im)
    s.g.fontmode = "L"
    s.box(100 + dx, 85, 140 + dx, 97, fill=s.c["surf"])
    s.text((102 + dx, 96), label, "lab", "text")


def m2a_momentary():
    s = kit("edit_flt")
    flt_to(s, 56, "2.4 kHz")
    header_way(s, "MOMENTARY")
    cards(s, [("CUTOFF", "2.4", "kHz", 0.78), ("RES", "79", "%", 0.62), ("DRIVE", "20", "%", 0.16),
              ("KEYTRK", "50", "%", 0.5)], hot=0)
    hints(s, "LET GO: BACK OVER 1 BAR", "OCT+: KEEP")
    return save(s, "m2a_momentary")


def m2a_fx():
    s = kit("perform_held")
    hints(s, "LET GO: BACK OVER 1 BAR", "OCT+: KEEP")
    return save(s, "m2a_fx")


def m2a_menu():
    s = kit("menu_hold")
    im = s.img
    rows = [im.crop((0, 62 + 26 * r, 240, 62 + 26 * r + 26)) for r in range(5)]
    for r in range(1, 5):                             # scrolled one row: KNOB ACCEL .. STEP PREVIEW, then RETURN
        im.paste(rows[r], (0, 62 + 26 * (r - 1)))
    s.g = ImageDraw.Draw(im)
    s.g.fontmode = "L"
    y = 62 + 26 * 4                                   # (the row picked: white, x 8 .. 231, as HOLD's)
    s.box(8, y, 231, y + 23, fill=s.c["text"], r=0)
    s.text((16, y + 19), "RETURN", "lab", "ink")
    s.text((224, y + 19), "1 BAR", "m", "ink", anchor="rs")
    return save(s, "m2a_menu")


def sheet_mutes():
    sh = Sheet("2.0  MUTES, AND WHAT COMES BACK",
               "A track is muted four ways today: GLO's black keys (latched, saved), FX's black keys (only while held, "
               "never saved), PERFORM's black keys (latched at its LAUNCH point) and MIXER's MUTE card; GLO's white keys "
               "solo while held. A value comes back by itself three ways: MOMENTARY (LFO held + a knob: back when LFO is "
               "let go, OCT+ keeps), PERFORM's throws (back over a bar once the knob rests) and FX's macros (back when FX "
               "is let go). A merge is for the interface: each saves under 1 KB. Answer with a letter for each of 1 and 2.")
    sh.label("TODAY")
    sh.row([(kit("six_glo").img, "GLO: black keys latch"), (kit("six_fx").img, "FX: black keys cut while held"),
            (kit("six_perform").img, "PERFORM: black keys at LAUNCH"), (kit("six_mixer").img, "MIXER: KNOB 4 MUTE")])
    sh.head("1  ONE MUTE")
    sh.opt("A", "TAP MUTES, HOLD CUTS, IN EVERY LAYER", rec=True)
    sh.row([(m1a_glo(), "GLO (as today)"), (m1a_fx(), "FX: GLO's row, BLACK: MUTE"),
            (m1a_perform(), "PERFORM (as today)")])
    sh.para("The black keys 1 .. 6 in GLO, FX and PERFORM are the six tracks' MUTE, and do the same thing in each: a "
            "tap mutes or unmutes (latched, saved: MIXER's card, HOME's M and every map agree), a hold mutes only while "
            "held (FX's cut, now in GLO and PERFORM too). GLO's and FX's taps act at once, PERFORM's at its LAUNCH "
            "point, as today. GLO's and FX's maps draw the keys alike (PERFORM shows them on its rows, as today). About "
            "-0.1 KB (FX's own mute bits in the mix): the gain is one meaning.")
    sh.opt("B", "THE SAME, AND EVERY TAP LANDS AT LAUNCH")
    sh.row([(m1b_glo(), "GLO: track 3's MUTE staged for the bar")])
    sh.para("As A, but GLO's and FX's taps wait for PERFORM's LAUNCH point too (LOOP, BAR, BEAT or NOW, set on PERFORM "
            "with SELECT; the header says it, the key's cell is outlined until it lands). Holds still cut at once. "
            "LAUNCH starts at LOOP, so GLO's mutes would wait for the loop's end until it is set to NOW.")
    sh.head("2  ONE WAY BACK")
    sh.opt("A", "ONE RETURN FOR ALL THREE", rec=True)
    sh.row([(m2a_momentary(), "LFO held + KNOB 1: MOMENTARY"), (m2a_fx(), "FX held: the macros"),
            (m2a_menu(), "MENU > CONTROL > RETURN")])
    sh.para("MOMENTARY, PERFORM's four knobs and FX's macros all come back the same way, set once: MENU > CONTROL > "
            "RETURN  SNAP (MOMENTARY's today)  1 BEAT  1 BAR (the throws' today). OCT+ keeps the changes in all three "
            "(MOMENTARY's today). What starts it stays: LFO or FX let go, a PERFORM knob at rest. One engine (MOMENTARY's "
            "list, the glide in the main loop): the throws' offsets in the audio ISR go. About -0.2 KB, -0.4 KB RAM.")
    sh.opt("B", "THROWS ONLY: MOMENTARY GOES")
    sh.para("LFO + a knob no longer does anything (LFO's tap opens LFO as ever). PERFORM's throws and FX's macros keep "
            "coming back, with RETURN and OCT+ KEEP. About -0.5 KB. MOMENTARY came from Felucca 1.2 (Discussions #72, "
            "#73): its users lose it.")
    sh.opt("C", "MOMENTARY EVERYWHERE: THE THROWS GO")
    sh.para("PERFORM's four knobs become MOMENTARY's on CUTOFF REVERB DELAY DIST: they come back when PERFORM is let go "
            "(or closed, when locked), SNAP; OCT+ keeps. The throws' bar-long glide goes. About -0.5 KB, -0.4 KB RAM.")
    sh.save("v2_mutes.png")


# ---------------------------------------------------------------- 3  ONE MOD PAGE, ONE SHAPER
GRID = [("LFO", ["+9", "+46", "0", "20"]), ("ENV", ["-12", "+76", "0", "-"])]
SLOTS = [("LFO", "CUT", "+63 %"), ("MODW", "MIX", "-100 %"), ("OFF", "OFF", "0 %"), ("OFF", "OFF", "0 %")]


def mod_cards(s, n):
    cards(s, [("ROUTE", str(n), "/11", n / 11), ("SOURCE", "LFO", "", None, True), ("DEST", "CUT", "", None, True),
              ("AMOUNT", "+46", "%", 0.86)], hot=3)


def d1a_grid():
    s = kit("mod")
    mod_cards(s, 2)
    panel(s)
    cols = [84, 124, 166, 208]
    for c, lab in enumerate(["PITCH", "CUT", "SHAPE", "AMP"]):
        s.text((cols[c], 92), lab, "lab", "mid", anchor="ms")
    for r, (src, vals) in enumerate(GRID):            # the seven routings: a source a row, a place a column
        y = 96 + 21 * r
        s.text((12, y + 15), src, "lab", "text")
        for c, v in enumerate(vals):
            if r == 0 and c == 1:                     # (the cell edited)
                s.box(cols[c] - 19, y + 1, cols[c] + 19, y + 19, fill=s.c["text"])
                s.text((cols[c], y + 15), v, "m", "ink", anchor="ms")
            else:
                s.text((cols[c], y + 15), v, "m", s.trk[0] if v not in ("0", "-") else s.c["dim"], anchor="ms")
    s.g.line([(8, 140), (231, 140)], fill=s.c["line"])
    for i, (src, dst, amt) in enumerate(SLOTS):       # the four free slots (8 .. 11), as MOD's list
        y = 143 + 18 * i
        off = src == "OFF"
        s.text((12, y + 13), str(i + 8), "lab", "mid")
        s.text((34, y + 13), src, "lab", s.c["dim"] if off else s.trk[0])
        arrow(s, 78, y + 8, s.c["dim"] if off else s.c["text"])
        s.text((102, y + 13), dst, "lab", s.c["dim"] if off else s.trk[0])
        s.text((204, y + 13), amt, "lab", s.c["dim"] if off else s.trk[0], anchor="rs")
    counter(s, "3/3")
    return save(s, "d1a_grid")


def d1b_list():
    s = kit("mod")
    mod_cards(s, 2)
    panel(s)
    rows = [("LFO", "PITCH", "+9 %"), ("LFO", "CUT", "+46 %"), ("LFO", "SHAPE", "0 %"), ("LFO", "AMP", "20 %"),
            ("ENV", "PITCH", "-12 %")]
    for i, (src, dst, amt) in enumerate(rows):        # 11 rows as MOD's (24 apart, the picked one white): the
        y = 86 + 24 * i                               # routings first, SOURCE and DEST fixed (MID), then the slots
        if i == 1:
            s.box(6, y, 230, y + 16, fill=s.c["text"], r=0)
        ink = s.c["ink"] if i == 1 else None
        s.text((16, y + 13), str(i + 1), "lab", ink or s.c["mid"])
        s.text((34, y + 13), src, "lab", ink or s.c["mid"])
        arrow(s, 78, y + 8, ink or s.c["mid"])
        s.text((102, y + 13), dst, "lab", ink or s.c["mid"])
        s.text((204, y + 13), amt, "lab", ink or (s.c["dim"] if amt.startswith("0") else s.trk[0]), anchor="rs")
    s.g.rectangle([234, 86, 235, 205], fill=s.c["line"])     # the scroll bar: 5 of the 11 shown
    s.g.rectangle([234, 86, 235, 86 + 119 * 5 // 11], fill=s.c["mid"])
    counter(s, "3/3")
    return save(s, "d1b_list")


def d1_lfo2():
    s = kit("lfo_2")
    cards(s, [("SYNC", "1/4", "", None), ("TRIG", "NOTE", "", None), ("POLARITY", "UNI", "", None),
              ("ENV SYNC", "OFF", "", None)], hot=3)
    counter(s, "2/3")
    return save(s, "d1_lfo2")


def d2a_drive():
    s = kit("drive_soft")
    ins = kit("insert_fuzz").img
    s.img.paste(ins.crop((0, 76, 240, 214)), (0, 76))        # INSERT's FUZZ transfer, at DIST's drive
    s.g = ImageDraw.Draw(s.img)
    s.g.fontmode = "L"
    cards(s, [("DIST", "76", "%", 0.6), ("TYPE", "FUZZ", "", None), ("TONE", "-31", "%", 0.26), ("", "", "", None)],
          hot=1)
    return save(s, "d2a_drive")


def sheet_mod():
    sh = Sheet("2.0  ONE MOD PAGE, ONE SHAPER",
               "LFO DEST and ENV DEST are seven fixed routings (the LFO to PITCH CUTOFF SHAPE AMP, the ENV to CUTOFF "
               "PITCH SHAPE) on two pages beside MOD's four free slots, which reach the same places and more. DRIVE's "
               "DIST has four clippers of its own (SOFT HARD FOLD BITS) beside INSERT's shapers (SOFT HARD FOLD FUZZ "
               "CRUSH). Every routing keeps its own curve (the LFO's PITCH squared, AMP's gain): the sound stays. A merge "
               "is for the interface: each saves under 1 KB. Answer with a letter for each of 1 and 2.")
    sh.label("TODAY")
    sh.row([(kit("lfo_dest").img, "LFO DEST 3/4"), (kit("env_dest").img, "ENV DEST 2/3"), (kit("mod").img, "MOD 4/4"),
            (kit("lfo_2").img, "LFO 2 2/4: a card free")])
    sh.head("1  ONE MOD PAGE")
    sh.opt("A", "THE ROUTINGS AS A GRID OVER THE FOUR SLOTS", rec=True)
    sh.row([(d1a_grid(), "MOD 3/3: LFO to CUT edited"), (d1_lfo2(), "LFO 2 2/3: ENV DEST's SYNC")])
    sh.para("MOD shows the seven routings as a grid (a source a row, a place a column; ENV to AMP is the envelope "
            "itself) over the four free slots. KNOB 1 picks one of the eleven, AMOUNT turns it; a routing's SOURCE and "
            "DEST are fixed (DIM). LFO DEST and ENV DEST go: LFO is LFO  LFO 2  MOD, ENV is ENV  ENV 2. ENV DEST's SYNC "
            "(the ENV's times in note values) needs a card: LFO 2's free fourth is the only one near (ENV and ENV 2 are "
            "full). About -0.4 KB (the two pages' graphs and cards; the grid is new).")
    sh.opt("B", "ONE LIST OF ELEVEN ROWS")
    sh.row([(d1b_list(), "MOD 3/3: 5 of the 11 rows")])
    sh.para("The same eleven as MOD's list today, the routings first (their SOURCE and DEST in MID: fixed), scrolling "
            "five at a time. SYNC as in A. About -0.6 KB (MOD's list drawing does it all).")
    sh.label("TODAY")
    sh.row([(kit("fx").img, "FX 1/6"), (kit("drive_soft").img, "DRIVE 2/6: DIST's own SOFT"),
            (kit("insert_fuzz").img, "INSERT 3/6: FUZZ")])
    sh.head("2  ONE SHAPER")
    sh.opt("A", "DIST PLAYS INSERT'S SHAPERS", rec=True)
    sh.row([(d2a_drive(), "DRIVE 2/6: TYPE FUZZ, INSERT's curve")])
    sh.para("DRIVE keeps DIST  TYPE  TONE, and DIST before INSERT, but its TYPEs are INSERT's: SOFT  HARD  FOLD  CRUSH  "
            "FUZZ (BITS is CRUSH by name; FUZZ is new and last, so a project's or a preset's TYPE stays). One shaper's "
            "code for both. DIST sounds a little different, the factory presets with HARD or FOLD too (the audio golden "
            "moves; a WAV pair per type to judge). About -0.6 KB.")
    sh.opt("B", "DIST BECOMES THE INSERT")
    sh.para("DRIVE's page goes and FX's DIST card turns INSERT's DRIVE on a SOFT INSERT. A track then has one of a "
            "drive, a phaser, a flanger or a chorus, not a drive and one of them: a project that has both keeps the "
            "INSERT and loses its DIST. About -0.8 KB and a page.")
    sh.save("v2_mod.png")


# ---------------------------------------------------------------- 4  NOTES AS A UNIT (the user's own asks, 2026-10-22)
# The roll (ui_graph.c graph_roll), measured on SKIFF_roll_chords: step i (1..16) a column 12 wide from x 35 + 12 (i - 1);
# a semitone a row, r = 0 the top of 21; a note bar y 94 + 5r .. 96 + 5r from its column's x + 2 to its last's x + 10;
# its key on the strip x 24 .. 32, y 93 + 5r .. 96 + 5r; the cursor box x 83 .. 95, y 91 .. 199 (step 5). Row 10 is C4
# (the label), the scale A MINOR. Its chords (Am7 F C G, two steps and one again) are drawn back as they were
PC = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}
CHORDS = [(1, 2, "A3 C4 E4 G4"), (3, 1, "A3 C4 E4 G4"), (5, 2, "F3 A3 C4 E4"), (7, 1, "F3 A3 C4 E4"),
          (9, 2, "C4 E4 G4"), (11, 1, "C4 E4 G4"), (13, 2, "G3 B3 D4"), (15, 1, "G3 B3 D4")]


def rx(i):
    return 35 + 12 * (i - 1)


def row_of(name):
    return 10 - (12 * (int(name[-1]) + 1) + PC[name[:-1]] - 60)


def redraw(s):
    s.g = ImageDraw.Draw(s.img)
    s.g.fontmode = "L"


def roll_clear(s):
    """every note and the cursor box off the roll: each column as step 16's (empty: the lanes and the lines)"""
    px = s.img.load()
    for y in range(91, 200):
        for x in range(35, 227):
            px[x, y] = px[215 + (x - 35) % 12, y]
    redraw(s)


def roll_note(s, step, length, name, col):
    r = row_of(name)
    s.g.rectangle([rx(step) + 2, 94 + 5 * r, rx(step) + 12 * length - 2, 96 + 5 * r], fill=col)


def roll_box(s, i0, i1):
    """the cursor's box over steps i0 .. i1 (a range: SHIFT + PRESETS)"""
    s.g.rectangle([rx(i0), 91, rx(i1 + 1), 199], outline=s.c["text"])


def roll_key(s, name):
    """a key picking its note: its row on the keyboard strip lit (graph_roll's held keys)"""
    r = row_of(name)
    s.g.rectangle([24, 93 + 5 * r, 32, 96 + 5 * r], fill=s.c["text"])


def notes_page(sel=(5, 5), moved=None, picked=None, own=None, chosen=(5, 5)):
    """NOTES on the roll: the chords, the ones starting in the steps chosen (chosen: before a move) white (picked: only
    those names); sel: the box (where they are now); moved: {old step: (new step, names)}; own: {name: length} a
    picked note's own length (a tie bit a note, #25)"""
    s = kit("roll_chords")
    roll_clear(s)
    for step, ln, names in CHORDS:
        st, names = (moved or {}).get(step, (step, names.split()))
        for nm in names:
            on = chosen[0] <= step <= chosen[1] and (not picked or nm in picked)
            if own and nm in own and on:
                continue
            roll_note(s, st, ln, nm, s.c["text"] if on else T1)
    for nm, ln in (own or {}).items():               # (drawn last: its tie runs over the chord at step 7)
        roll_note(s, sel[0], ln, nm, s.c["text"])
    for nm in picked or []:
        roll_key(s, nm)
    roll_box(s, *sel)
    retitle(s, "NOTES" if sel[0] == sel[1] else f"NOTES {sel[0]}-{sel[1]}")
    hints(s, "SHIFT+PRESETS: RANGE", "KEYS: PICK", right="1/5")
    return s


def n_chord():
    s = notes_page()
    cards(s, [("PITCH", "F3", "+3", None), ("START", "5", "/16", 5 / 16), ("LENGTH", "2", "STEPS", 2 / 16),
              ("VELOCITY", "96", "", 96 / 127)], hot=2)
    return save(s, "n_chord")


def n_start():
    s = notes_page((6, 8), moved={5: (6, "F3 A3 C4 E4".split()), 7: (8, "F3 A3 C4 E4".split())}, chosen=(5, 8))
    cards(s, [("PITCH", "F3", "+7", None), ("START", "6", "/16", 6 / 16), ("LENGTH", "2", "STEPS", 2 / 16),
              ("VELOCITY", "96", "", 96 / 127)], hot=1)
    return save(s, "n_start")


def n_scale():
    up = "G3 B3 D4 F4".split()                         # F A C E one step up A minor
    s = notes_page((5, 8), moved={5: (5, up), 7: (7, up)}, chosen=(5, 8))
    cards(s, [("PITCH", "G3", "+7", None), ("START", "5", "/16", 5 / 16), ("LENGTH", "2", "STEPS", 2 / 16),
              ("VELOCITY", "96", "", 96 / 127)], hot=0)
    return save(s, "n_scale")


def n_pick():
    s = notes_page(picked=["C4"], own={"C4": 3})
    cards(s, [("PITCH", "C4", "", None), ("START", "5", "/16", 5 / 16), ("LENGTH", "3", "STEPS", 3 / 16),
              ("VELOCITY", "96", "", 96 / 127)], hot=2)
    return save(s, "n_pick")


def n_feel():
    s = kit("step_tiles")
    retitle(s, "FEEL 5-8")
    cards(s, [("CHANCE", "1:2", "", None), ("RATCHET", "X1", "", None), ("NUDGE", "0", "", None, True),
              ("TYPE", "ACC", "", None)], hot=0)
    tie = p6.q565(mix(T1, PAL["surf"], 0.5))
    for i in range(16):                               # the chords' steps: a note, its tie, nothing
        step = i + 1
        if step % 2:
            v = "1:2" if step in (5, 7) else "50" if step == 15 else "100"
            tile(s, i, T1, v, mark=v != "100")
        else:
            tile(s, i, tie if step % 4 == 2 else PAL["surf"])
    x0, y0, x1, y1 = tile_box(s, 4)
    s.g.rectangle([x0 - 2, y0 - 2, tile_box(s, 7)[2] + 2, y1 + 2], outline=s.c["text"])
    hints(s, "SHIFT+PRESETS: RANGE", right="2/5")
    return save(s, "n_feel")


def shift_cell(s, r, c, lab, val, white=False):
    """a cell of SHIFT's map (ui_layer.c shift_map; measured: 74 x 31 from x 4 + 78c, y 78 + 34r)"""
    x0, y0 = 4 + 78 * c, 78 + 34 * r
    s.box(x0, y0, x0 + 73, y0 + 30, fill=s.c["text"] if white else s.c["surf"], r=0)
    s.text((x0 + 5, y0 + 12), lab, "lab", s.c["ink"] if white else s.c["mid"])
    s.text((x0 + 5, y0 + 27), val, "m", s.c["ink"] if white else s.c["text"])


def n_shift():
    s = kit("shift_step")
    cards(s, [("PITCH", "F3", "+3", None), ("START", "5", "/16", 5 / 16), ("LENGTH", "2", "STEPS", 2 / 16),
              ("VELOCITY", "96", "", 96 / 127)])
    for r, row in enumerate([[("PRESETS", "STEP"), ("SELECT", "TEMPO"), ("ALGO", "TRACK")],
                             [("KEYS", "PICK"), ("KEYS + REC", "WRITE"), ("HOLD SAVE", "UNDO")],
                             [("+ PRESETS", "RANGE"), ("+ OCT-", "COPY"), ("+ OCT+", "PASTE")],
                             [("+ SAVE", "TAKE"), ("+ REC", "REC SET"), ("+ BUTTON", "LOCK")]]):
        for c, (lab, val) in enumerate(row):
            shift_cell(s, r, c, lab, val, white=r >= 2)
    hints(s, "LET GO: MENU", right="NOTES")
    return save(s, "n_shift")


def sheet_notes():
    sh = Sheet("2.0  NOTES AS A UNIT: YOUR STEP EDITING",
               "Your asks: a chord put in at a step; its start, length and the rest moved as a unit; notes moved up and "
               "down the scale; copy and paste; and at a step one note or several picked to do all that to. What the "
               "firmware does today, then the two step pages again with a selection (D). The screens are firmware "
               "renders; the roll's notes are drawn in its geometry.")
    sh.label("TODAY")
    sh.row([(kit("roll_chords").img, "ROLL: a chord is one step's notes"), (kit("shift_step").img, "SHIFT's map on STEP")])
    sh.para("A chord goes in with REC on and stopped: keys held together (up to 4 a step), the cursor moving on when "
            "they are let go; a chord held while recording keeps its length. A step's notes share one length (TIME: TIE "
            "on the steps after), one VELOCITY, TYPE, CHANCE, RATCHET and NUDGE. NOTE moves them in semitones; a scale "
            "bends them only with SCALE's QUANTIZE on SEQ. Nothing is selected but the cursor's step, nothing moves to "
            "another step (SEQ TOOLS' SHIFT moves the whole pattern), and steps can't be copied.")
    sh.opt("D", "NOTES AND FEEL, WITH A SELECTION")
    sh.row([(n_chord(), "a chord as a unit: LENGTH"), (n_start(), "START: steps 5-8 one step later"),
            (n_scale(), "PITCH: 5-8 up A minor (F A C E: G B D F)"), (n_pick(), "C4 picked by its key: its own LENGTH")])
    sh.row([(n_feel(), "FEEL on the tiles: 5-8 at 1:2"), (n_shift(), "SHIFT's map: RANGE, COPY, PASTE")])
    sh.para("The selection: the cursor's step (PRESETS), or a range: SHIFT (HOME held) + PRESETS stretches it from where "
            "it was (the header says 5-8). With REC off a key picks its note in the selection (again: unpicked; its row "
            "lit on the keyboard strip); none picked: all of them. With REC on the keys write, as today.")
    sh.para("NOTES, on the roll: PITCH  START  LENGTH  VELOCITY, everything selected at once. PITCH walks the track's "
            "SCALE (CHROMATIC: semitones), START moves by steps (NUDGE stays on FEEL for less), LENGTH is 1 .. 100 % of "
            "the step then 2 .. 16 STEPS (ties), VELOCITY moves all by the same amount.")
    sh.para("FEEL, on the tiles (each step's value of the knob turned last): CHANCE (100 .. 1 %, OFF for a rest, then "
            "FILL  !FILL  1:2 .. 4:4: COND joins it, as Elektron's trig conditions)  RATCHET  NUDGE  TYPE (-  ACC  SLD  "
            "A+S).")
    sh.para("COPY and PASTE: SHIFT + OCT- copies the selection (the picked notes, their places and lengths, their steps' "
            "values), SHIFT + OCT+ pastes it at the cursor into the chords there (4 notes a step: more is refused and "
            "said). One undo each (SAVE held). #31: with REC on, hold the chord and turn PRESETS: its LENGTH grows as "
            "you turn (KNOB 1 .. 4 still lock the sound, as today).")
    sh.para("What the data allows: 4 notes a step. A picked note's own LENGTH needs a tie bit a note (#25, in the 2.0 "
            "pattern record, Tier B); its own VELOCITY, CHANCE or TYPE would need more: a step's are its chord's, so a "
            "picked note takes its step's, or START moves it to a step of its own.")
    sh.para("About 3 KB of flash (the selection and its drawing, scale steps, moves with their ties, picking, copy and "
            "paste, #31) and a clipboard up to 1 KB of RAM (a pattern's 64 steps; 256 B for a bar).", fill=INK)
    sh.head("WHAT IT CHANGES IN A, B AND C")
    sh.para("A: the views swap (NOTES on the roll, where a chord, its length and a picked note show; FEEL on the tiles), "
            "START takes TYPE's knob, TYPE moves to FEEL and COND joins CHANCE.   B: D shows its pages the way B's flip "
            "does (NOTES on the roll, FEEL on the tiles); the flip can stay.   C: no room for START or a selection: "
            "dropped.   DIR on PATTERN's PRESETS: as before.")
    sh.head("TO DECIDE")
    sh.para("1  a key picks its note with a tap (kept until the cursor moves) or only while held.   2  CHANCE and COND on "
            "one knob, or COND apart (TYPE then needs another home).   3  COPY and PASTE on SHIFT + OCT- / OCT+, or SEQ "
            "TOOLS' two free keys (F5, G5).   4  a turn moves PITCH, START and VELOCITY by the same amount for all and "
            "sets LENGTH and the FEEL values to the one shown for all (as above), or every value moves by the same "
            "amount.")
    sh.save("v2_notes.png")


# ---------------------------------------------------------------- the sheets
class Sheet:
    W, PAD, Z, GAP = 2080, 40, 480, 30

    def __init__(self, title, intro):
        self.im = Image.new("RGB", (self.W, 9000), BG)
        self.g = ImageDraw.Draw(self.im)
        self.g.text((self.PAD, 30), title, font=font(True, 56), fill=INK)
        self.y = lines(self.g, self.PAD, 104, intro, w=self.W - 2 * self.PAD) + 24

    def label(self, t):
        self.g.text((self.PAD, self.y), t, font=font(True, 26), fill=MID)
        self.y += 38

    def head(self, t):
        self.y += 10
        self.g.text((self.PAD, self.y), t, font=font(True, 44), fill=INK)
        self.y += 64

    def opt(self, key, title, rec=False):
        self.y += 6
        option(self.g, self.PAD, self.y, key, title, rec)
        self.y += 52

    def row(self, shots):
        for i, (im, cap) in enumerate(shots):
            x = self.PAD + i * (self.Z + self.GAP)
            self.im.paste(im.resize((self.Z, self.Z), Image.NEAREST), (x, self.y))
            self.g.text((x + self.Z // 2, self.y + self.Z + 8), cap, font=font(True, 22), fill=MID, anchor="mt")
        self.y += self.Z + 46

    def para(self, t, fill=MID):
        self.y = lines(self.g, self.PAD, self.y, t, w=self.W - 2 * self.PAD, fill=fill) + 6

    def save(self, name):
        SHEETS.mkdir(exist_ok=True)
        out = self.im.crop((0, 0, self.W, self.y + self.PAD))
        out.save(SHEETS / name, optimize=True)
        print(f"mockups/{name} {out.width} x {out.height}")


if __name__ == "__main__":
    sheet_steps()
    sheet_mutes()
    sheet_mod()
    sheet_notes()
