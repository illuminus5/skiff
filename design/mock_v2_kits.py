#!/usr/bin/env python3
"""Skiff 2.0, #35 ("is it possible to replace the drum kits with my own drum kits?"; promised on YouTube for 2.0: own
samples on the DRUM grid): three directions, each to answer with a letter. Screens start from the firmware's renders
(tests/ui_render.c, SKIFF: drum, lanes, lane_mix, samples; drumkit is DRUM's EDIT 1, KIT, rendered for this sheet by
a copy of ui_render.c whose lanes scene has go_title("EDIT 1"), saved as SKIFF_drumkit.ppm beside the others);
what changes is drawn in their geometry and their own colours, sampled. The editor's kit builder is drawn in the web
editor's dark look.
Writes out/V2_k<letter>_<screen>.png (1x) and mockups/v2_kits.png.
  P6_RENDERS=~/skiff-v2-quick/build/ui_new/ppm python3 mock_v2_kits.py
"""
from PIL import Image, ImageDraw
from mock_c27 import font
from mock_v2 import kit, save, cards, retitle, Sheet
from mock_p6 import footer_track
from mock_v2_songs import at

LANES = ["KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "TOM", "RIM", "BELL"]
ABBR = ["BD", "SD", "CP", "CH", "OH", "TM", "RS", "CB"]


def sound_name(s, k, eng, name):
    footer_track(s, k, eng, name)


# ---------------------------------------------------------------- A  YOUR KIT IN DRUM'S KIT LIST
def ka_kit():
    s = kit("drumkit")
    cards(s, [("KIT", "USR1", "", None), ("PITCH", "0.0", "st", 0.5), ("TONE", "55", "%", 0.55), ("DECAY", "50", "%", 0.5)],
          hot=0, k=3)
    sound_name(s, 3, "DRUM", "MY KIT")
    return save(s, "ka_kit")


def ka_lanes():
    """LANES on the user kit: as today; a lane the kit has no sample for (BELL) dimmed, silent"""
    s = kit("lanes")
    sound_name(s, 3, "DRUM", "MY KIT")
    surf = at(s, 6, 90)
    px = s.img.load()
    top = next(y for y in range(80, 200) if px[16, y] != surf)   # (lane 0's fader: its cap's top)
    bot = next(y for y in range(top, 213) if px[16, y] == surf) - 1   # (.. its track's foot)
    x6 = 3 + 29 * 6                                  # (RS's name: the names' baseline)
    base = max(y for y in range(bot + 3, 213) for i in range(x6 + 4, x6 + 24) if px[i, y] != surf)
    x = 3 + 29 * 7                                   # (lane 7's column: its fader and its name, graph_lanes)
    s.box(x + 2, top, x + 26, 213, fill=surf)
    s.g.rounded_rectangle([x + 12, top + 4, x + 15, bot], 2, fill=s.c["line"])   # (the track, empty: no level, no cap)
    s.text((x + 14, base), "CB", "lab", s.c["dim"], anchor="ms")
    return save(s, "ka_lanes")


def editor_kit(lanes=8):
    """the web editor's SAMPLES tab, DRUM KIT: a WAV a lane, the kit's levels kept, onto a user slot (480 x 480)"""
    W = 480
    bg, tile, raise_, fg, dim, line, acc = (12, 12, 12), (30, 30, 30), (44, 44, 44), (255, 255, 255), (140, 140, 140), \
        (110, 110, 110), (149, 128, 255)
    im = Image.new("RGB", (W, W), bg)
    g = ImageDraw.Draw(im)
    fb, fm, fs = font(True, 26), font(False, 18), font(False, 15)
    g.rounded_rectangle([10, 10, W - 10, W - 10], 10, fill=tile)
    g.text((26, 46), "SAMPLES  ›  DRUM KIT", font=fb, fill=fg, anchor="ls")
    g.text((26, 70), "A WAV a lane (drop it on its row). The kit's levels as they are.", font=fs, fill=dim, anchor="ls")
    files = ["kick_808.wav", "snare_tight.wav", "clap_room.wav", "hat_closed.wav", "hat_open.wav", "tom_low.wav",
             "rim_short.wav", ""]
    lv = [0.92, 0.8, 0.7, 0.55, 0.6, 0.75, 0.5, 0]
    for i in range(lanes):
        y = 84 + 35 * i                              # (a row: its box y + 4 .. y + 28)
        g.text((26, y + 22), LANES[i], font=fm, fill=fg, anchor="ls")
        has = bool(files[i])
        g.rounded_rectangle([110, y + 4, 330, y + 28], 5, fill=raise_ if has else bg, outline=None if has else line)
        g.text((120, y + 21), files[i] if has else "drop a WAV here (silent)", font=fs, fill=fg if has else dim,
               anchor="ls")
        g.rounded_rectangle([344, y + 13, 454, y + 19], 3, fill=raise_)
        if has:
            g.rounded_rectangle([344, y + 13, 344 + int(110 * lv[i]), y + 19], 3, fill=acc)
            g.ellipse([344 + int(110 * lv[i]) - 6, y + 10, 344 + int(110 * lv[i]) + 6, y + 22], fill=fg)
    y = 84 + 35 * lanes + 8
    g.text((26, y + 16), "4.1 s of 7.4 s", font=fs, fill=dim, anchor="ls")
    g.rounded_rectangle([140, y + 8, 454, y + 14], 3, fill=raise_)
    g.rounded_rectangle([140, y + 8, 140 + int(314 * 4.1 / 7.4), y + 14], 3, fill=acc)
    g.rounded_rectangle([26, y + 30, 240, y + 62], 6, fill=fg)
    g.text((40, y + 52), "Upload to USR1  MY KIT", font=fm, fill=bg, anchor="ls")
    g.text((254, y + 52), "(the SMK-37: USR1 only)", font=fs, fill=dim, anchor="ls")
    return im


# ---------------------------------------------------------------- B  THE GRID ON A SAMPLE TRACK
def kb_grid():
    s = kit("drum")
    retitle(s, "NOTES 5")
    return save(s, "kb_grid")


def kb_samples():
    s = kit("samples")
    cards(s, [("SAMPLE", "SD", "", None), ("LEVEL", "50", "%", 0.5), ("REVERB", "35", "%", 0.35), ("DELAY", "16", "%", 0.16)],
          hot=2, k=0)
    sound_name(s, 0, "SAMPLE", "MY KIT")
    return save(s, "kb_samples")


# ---------------------------------------------------------------- C  EACH LANE ITS OWN SOURCE
def kc_mix():
    """LANE MIX's free KNOB 4: SOURCE, the kit's own or a user sample; a lane playing a sample a small wave mark"""
    s = kit("lane_mix")
    cards(s, [("LANE", "SNARE", "", None), ("LEVEL", "100", "%", 1.0), ("PAN", "-31", "%", 0.35), ("SOURCE", "USR1 2", "", None)],
          hot=3, k=3)
    for i in (0, 1):                                 # BD and SD from USR1: a three-bar mark over their faders
        x = 3 + 29 * i + 9                           # (the faders' tops at y 101)
        col = s.c["text"] if i == 1 else s.trk[3]
        for j, h in enumerate((3, 6, 4)):
            s.box(x + 3 * j, 96 - h, x + 3 * j + 1, 96, fill=col)
    return save(s, "kc_mix")


def sheet_kits():
    sh = Sheet("2.0  YOUR OWN DRUM KIT (#35)",
               "#35 asks whether the drum kits can be replaced by your own, and 2.0 was promised own samples on the "
               "drum grid. A user sample slot (USR1..3, 80 KB each: about 7 s at 22 kHz) can already hold 16 one-shots, "
               "and the grid's hits already go out as the lanes' notes (KICK 36, SNARE 38 ..). What is missing is a way "
               "to build a kit (today's upload tools place one sample per key only by file name, and level each file "
               "on its own) and a place on the unit that plays it as a kit. All three directions add the editor's DRUM "
               "KIT builder. Answer with a letter.")
    sh.label("TODAY")
    sh.row([(kit("drumkit").img, "DRUM: KIT  PITCH  TONE  DECAY"), (kit("drum").img, "the grid (NOTES on DRUM)"),
            (kit("samples").img, "SAMPLES (#21): a USR sample's mix")])
    sh.opt("A", "YOUR KIT IN DRUM'S KIT LIST", rec=True)
    sh.row([(ka_kit(), "KIT: STD .. PERC, then USR1 USR2 USR3"), (ka_lanes(), "LANES on it: BELL has no sample"),
            (editor_kit(), "the editor: SAMPLES › DRUM KIT")])
    sh.para("DRUM's KIT runs on past PERC to USR1, USR2 and USR3 (a slot built as a kit); the footer names it. Each lane "
            "plays the slot's sample for its key, and everything a DRUM kit has keeps working: the grid and its keys, "
            "LANES' LEVEL TUNE DECAY, LANE MIX's pans, the closed hat choking the open one, ducking from the kick, the "
            "accents. A lane with no sample is silent and dimmed. Recommended: it is what #35 asks (replace the kit), "
            "with nothing new to learn. About 1.5 KB: a sample voice inside DRUM's lanes (the audio budget to check).")
    sh.opt("B", "THE DRUM GRID ON A SAMPLE TRACK")
    sh.row([(kb_grid(), "NOTES on SAMPLE (USR1): the grid"), (kb_samples(), "SAMPLES: each lane's LEVEL REVERB DELAY")])
    sh.para("A SAMPLE track on a kit slot gets the drum grid, its lanes the slot's samples. Each lane's LEVEL, REVERB and "
            "DELAY are #21's (SAMPLES page); PITCH and the envelope are the whole track's. No per-lane TUNE, DECAY or "
            "PAN, no hat choke, every hit ducks (not the kick only), unless each is added again for SAMPLE. About 1 KB, "
            "spread over the 25 places that ask whether a track is a drum track.")
    sh.opt("C", "EACH LANE ITS OWN SOURCE")
    sh.row([(kc_mix(), "LANE MIX: KNOB 4 SOURCE, KIT or a user sample")])
    sh.para("A, and then any lane can take one sample of any slot (LANE MIX's free fourth knob): your own kick and snare "
            "over the kit's synthesized hats. The faders mark the lanes playing samples. A's work plus eight sound "
            "parameters (the sources, kept with the sound and its user presets): about 2 KB.")
    sh.save("v2_kits.png")


if __name__ == "__main__":
    sheet_kits()
