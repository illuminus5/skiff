#!/usr/bin/env python3
"""Skiff 2.0: a chain shown more clearly on SEQ > SONG (the user on the unit, 2026-10-24: "very subtle effect to show the
linkage"). Today graph_song draws a chain as a 2 px bar in the band's top rows: INK on the selected track's band, the
track's colour on the others. Two directions on the firmware's renders (tests/ui_render.c song_chain3: T2's A B on
CHAIN, C 2x), each also on a row not selected (T4's C D E drawn as a chain), in each theme's own colours, sampled.
Writes out/V2_cm_<letter>_<theme>.png (1x) and mockups/v2_chainmark.png.
  P6_RENDERS=~/skiff-v2-quick/build/ui_new/ppm python3 mock_v2_chainmark.py
"""
from collections import Counter
from PIL import Image
import mock_p6 as p6
from mock import mix
from mock_v2 import save, Sheet
from mock_v2_songs import at, sg_x, sg_y

CHAINS = [(0, 0, 1), (0, 2, 3), (1, 0, 2), (3, 2, 4)]   # (row, first entry, last): T1 A-B and C-D side by side (drawn),
SEL = 1                                                 # T2 A-C (the render's), T4 C-E (drawn); T2 the band
DIM = 0.35                                              # A's box: the track's colour on the ground


def theme(name):
    """a SONG render of THEME name, the ground and each row's track colour sampled"""
    s = p6.kit(f"{name}_song_chain3")
    s.look = name
    s.c.update(text=(255, 250, 255), ink=(8, 8, 8))
    s.bg = at(s, 236, sg_y(0) + 19)             # (T1's row past its last tile)
    s.trk = {}
    for r in range(4):
        y = sg_y(r)
        if r == SEL:
            s.trk[r] = at(s, 1, y + 10)         # (the band, left of its number)
        else:                                   # (the number's ink: its commonest colour)
            px = Counter(at(s, x, yy) for x in range(3, 17) for yy in range(y, y + 20))
            s.trk[r] = next(c for c, _ in px.most_common() if c != s.bg)
    return s


def today(s):
    """T1's and T4's chains as graph_song draws one today (T2's is the render's own)"""
    for r, p0, p1 in CHAINS:
        if r != SEL:
            s.g.rectangle([sg_x(p0) + 1, sg_y(r), sg_x(p1) + 11, sg_y(r) + 1], fill=s.trk[r])


def a_box(s):
    """A: the chain's tiles on a box of their own, the band's height and 2 px round them, the track's colour dimmed on
    every row (the selected track's band too); where two chains meet, 2 px inside their tiles (a notch between them).
    The tiles as they are (the one playing, the one picked) and over it (a tile next to it too: graph_song draws the boxes
    first); the repeats' ticks (the last part's: those on CHAIN play once) and a staged frame on it the track's colour;
    NEON's lit tube glows on the box"""
    ends = {(r, p) for r, p0, p1 in CHAINS for p in (p0, p1)}
    for r, p0, p1 in CHAINS:
        col, y, ty = s.trk[r], sg_y(r), sg_y(r) + 2
        box = mix(s.bg, col, DIM)
        old = s.img.copy()
        a = 2 if (r, p0 - 1) in ends else -2
        z = 10 if (r, p1 + 1) in ends else 14
        s.g.rectangle([sg_x(p0) + a, y, sg_x(p1) + z, y + 19], fill=box)
        for q in (p0 - 1, p1 + 1):              # (a tile beside it, not an empty entry's dot or dash: over the box)
            x = sg_x(q)
            if 0 <= q < 16 and old.getpixel((x + 1, ty)) != old.getpixel((x + 1, y + 19)):
                s.img.paste(old.crop((x, ty, x + 13, ty + 15)), (x, ty))
        for p in range(p0, p1 + 1):
            x = sg_x(p)
            if s.look == "NEON" and r != SEL and old.getpixel((x + 1, ty + 4)) == col:
                s.g.rectangle([x - 1, ty - 1, x + 13, ty + 15], fill=mix(col, box, 0.62))
            s.img.paste(old.crop((x, ty, x + 13, ty + 15)), (x, ty))
            for k in range(3 if p == p1 else 0):   # (the repeats' ticks, y + 18)
                tx = x + 1 + 3 * k
                if old.getpixel((tx, ty + 16)) != old.getpixel((tx, y + 19)):
                    s.g.rectangle([tx, ty + 16, tx + 1, ty + 16], fill=col)


def b_bar(s):
    """B: the bar 3 px, one of them in the gap above the band, TEXT on the selected track's band (the track's colour on
    the others)"""
    for r, p0, p1 in CHAINS:
        col = s.c["text"] if r == SEL else s.trk[r]
        s.g.rectangle([sg_x(p0) + 1, sg_y(r) - 1, sg_x(p1) + 11, sg_y(r) + 1], fill=col)


def shot(key, look, draw):
    s = theme(look)
    draw(s)
    return save(s, f"cm_{key}_{look.lower()}")


def zoom(im, n=3):
    """the rows T1 .. T4, n times"""
    c = im.crop((0, 76, 240, 170))
    return c.resize((c.width * n, c.height * n), Image.NEAREST)


def sheet():
    sh = Sheet("2.0  A CHAIN, SHOWN CLEARLY",
               "SEQ > SONG draws a chain (entries on CHAIN, played as one pattern) as a 2 px bar over its tiles: dark on "
               "the selected track's band, the track's colour on the others. On the unit it hardly shows. Here T2's "
               "A B C are a chain (the selected track: its band), T1's A B and C D two chains side by side, and T4's "
               "C D E. Answer with a letter.")
    sh.label("TODAY")
    sh.row([(shot("t", "SKIFF", today), "SKIFF: the 2 px bar"), (shot("t", "NEON", today), "NEON")])
    sh.opt("A", "THE CHAIN ON A BOX OF ITS OWN", rec=True)
    sh.row([(shot("a", lk, a_box), lk) for lk in ("SKIFF", "NEON", "CRYPT", "QUARK")])
    sh.para("Behind a chain's tiles a box the band's height, 2 px wider than they are, in the track's colour dimmed: "
            "on the selected track's band darker than the band, on the other tracks lighter than the ground. The tiles "
            "show what they show today (the entry playing, the one picked); a staged frame and the repeats' ticks on "
            "it are drawn in the track's colour. Two chains side by side keep a notch between them. The chain reads "
            "as one block, as it plays. The web editor's Song tab draws its chains the same way. Recommended: it shows "
            "from across the room, and nothing else on SONG is a box.")
    sh.opt("B", "A BRIGHTER, THICKER BAR")
    sh.row([(shot("b", lk, b_bar), lk) for lk in ("SKIFF", "NEON")])
    sh.para("Today's bar 3 px, one of them in the gap above the band; white on the selected track's band, the track's "
            "colour on the others. The least change: the same mark, easier to see.")
    for k, f, lk in (("TODAY", today, "SKIFF"), ("A", a_box, "SKIFF"), ("A", a_box, "NEON"), ("B", b_bar, "SKIFF")):
        s = theme(lk)
        f(s)
        sh.label(f"{k}, {lk}, THE ROWS 3x")
        sh.im.paste(zoom(s.img), (sh.PAD, sh.y))
        sh.y += 94 * 3 + 30
    sh.save("v2_chainmark.png")


if __name__ == "__main__":
    sheet()
