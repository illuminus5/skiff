#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""(Skiff) THEME CRYPT's pixel faces (run by tools/build.py generate()): one 5 x 7 glyph set of our own (as
design/mock_crypt.py drew it), as the firmware's antialiased faces are laid out (src/gfx.c aafont_t; every pixel
either off or full, so no blend shows), so the text code draws them as it draws Barlow:

  S  the glyphs 1x              labels, small text (32 .. 96: lower case folds to capitals; the extras)
  M  1x bold (each stroke 2 px wide)   names, values set smaller
  V  2x                         the cards' values (the sparse set of Barlow's V: a value with another
                                character is set in M, ui_blocks.c)
  L  3x                         MENU > LARGE's values and titles (Barlow's L's set)

Each face keeps its Barlow face's line height (h), so the rows and boxes stay where they are, and centres its
capitals on Barlow's capitals' band (a pixel left over up), so text placed against Barlow's capitals' rows sits as
it did; its baseline (asc) is under them. The rows are AF_x_PX_CAP_Y / _CAP_H, for the centring (gfx.c CAP_IN). Digits are all 5 wide: the figures of a value don't move as it changes.
(Skiff 2.0) Only S keeps pixels: M V L are S's drawn at their size (aafont_t sc: the scale, PX_BOLD; gfx.c
cv_alpha_px), their glyphs' boxes as before; each glyph is checked here to come out as its own pixels did.

  gen_pixel_font.py OUT.h
"""
import sys
from pathlib import Path

# the glyphs: 7 rows each, '#' a pixel; the width is the rows' (equal); spacing 1 px after each
G = {
    ' ': ["   "] * 7,
    '!': ["#", "#", "#", "#", "#", " ", "#"],
    '"': ["# #", "# #", "   ", "   ", "   ", "   ", "   "],
    '#': [" # # ", "#####", " # # ", " # # ", " # # ", "#####", " # # "],
    '$': ["  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "],
    '%': ["##  #", "##  #", "   # ", "  #  ", " #   ", "#  ##", "#  ##"],
    '&': [" ##  ", "#  # ", " ##  ", " ## #", "#  # ", "#  # ", " ## #"],
    "'": ["#", "#", " ", " ", " ", " ", " "],
    '(': ["  #", " # ", "#  ", "#  ", "#  ", " # ", "  #"],
    ')': ["#  ", " # ", "  #", "  #", "  #", " # ", "#  "],
    '*': ["     ", "# # #", " ### ", "#####", " ### ", "# # #", "     "],
    '+': ["     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "],
    ',': [" ", " ", " ", " ", " ", "#", "#"],
    '-': ["   ", "   ", "   ", "###", "   ", "   ", "   "],
    '.': [" ", " ", " ", " ", " ", " ", "#"],
    '/': ["    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "],
    '0': [" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "],
    '1': ["  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "],
    '2': [" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"],
    '3': ["#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "],
    '4': ["   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "],
    '5': ["#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "],
    '6': ["  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "],
    '7': ["#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "],
    '8': [" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "],
    '9': [" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "],
    ':': [" ", " ", "#", " ", " ", "#", " "],
    ';': [" ", " ", "#", " ", " ", "#", "#"],
    '<': ["   #", "  # ", " #  ", "#   ", " #  ", "  # ", "   #"],
    '=': ["     ", "     ", "#####", "     ", "#####", "     ", "     "],
    '>': ["#   ", " #  ", "  # ", "   #", "  # ", " #  ", "#   "],
    '?': [" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "],
    '@': [" ### ", "#   #", "# ###", "# # #", "# ###", "#    ", " ####"],
    'A': [" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'B': ["#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "],
    'C': [" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "],
    'D': ["#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "],
    'E': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"],
    'F': ["#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "],
    'G': [" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"],
    'H': ["#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"],
    'I': ["###", " # ", " # ", " # ", " # ", " # ", "###"],
    'J': ["  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "],
    'K': ["#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"],
    'L': ["#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"],
    'M': ["#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"],
    'N': ["#   #", "##  #", "# # #", "#  ##", "#   #", "#   #", "#   #"],
    'O': [" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'P': ["#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "],
    'Q': [" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"],
    'R': ["#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"],
    'S': [" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "],
    'T': ["#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "],
    'U': ["#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "],
    'V': ["#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "],
    'W': ["#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "],
    'X': ["#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"],
    'Y': ["#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  ", "  #  "],
    'Z': ["#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"],
    '[': ["###", "#  ", "#  ", "#  ", "#  ", "#  ", "###"],
    '\\': ["#    ", "#    ", " #   ", "  #  ", "   # ", "    #", "    #"],
    ']': ["###", "  #", "  #", "  #", "  #", "  #", "###"],
    '^': ["  #  ", " # # ", "#   #", "     ", "     ", "     ", "     "],
    '_': ["     ", "     ", "     ", "     ", "     ", "     ", "#####"],
    '`': ["# ", " #", "  ", "  ", "  ", "  ", "  "],
    # the extras: code -> glyph (Latin-1, the ellipsis as 0x85 as in gen_aa_font.py), and | ~ past the range
    0x7C: ["#"] * 7,
    0x7E: ["     ", "     ", " #  #", "# ## ", "     ", "     ", "     "],
    0x85: ["     ", "     ", "     ", "     ", "     ", "     ", "# # #"],
    0xB7: [" ", " ", " ", "#", " ", " ", " "],
    0xFC: ["#   #", "     ", "#   #", "#   #", "#   #", "#   #", " ### "],   # (no lower case: u: as its capital)
}
# (Skiff 2.0) the extras a text holds, as gen_aa_font.py's (c) deg A: O: U: a: o: went
EXTRAS = [0x7C, 0x7E, 0x85, 0xB7, 0xFC]
L_CHARS = " #+-./0123456789ABCDEFGHIJKLMNOPQRSTUVWXY"            # gen_aa_font.py's L and V sets
V_CHARS = " !#+-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ"
# name, scale, bold, (h, Barlow's capitals' first row, their rows) of the Barlow face it stands for, range, extras
FACES = [("S", 1, False, (16, 3, 10), (32, 96), EXTRAS),
         ("M", 1, True, (20, 4, 12), (32, 96), EXTRAS),
         ("V", 2, False, (29, 7, 17), (32, 32), [ord(c) for c in V_CHARS[1:]]),
         ("L", 3, False, (34, 8, 20), (32, 32), [ord(c) for c in L_CHARS[1:]])]


def rows_of(code):
    return G[chr(code)] if chr(code) in G else G[code]


def bitmap(rows, scale, bold):
    """the glyph as rows of 0 / 1 at its size (bold: each pixel doubled to the right)"""
    w = len(rows[0])
    bits = [[1 if c == '#' else 0 for c in r] for r in rows]
    if bold:
        bits = [[1 if r[x] or (x and r[x - 1]) else 0 for x in range(w)] + [r[w - 1]] for r in bits]
    return [[b for b in r for _ in range(scale)] for r in bits for _ in range(scale)]


PX_BOLD = 0x80                                         # gfx.c: aafont_t sc's bold bit


def drawn(data, off, bw, bh, scale, bold):
    """what gfx.c cv_alpha_px draws for a glyph box bw x bh from S's pixels at byte off"""
    sw = (bw - bold) // scale

    def nib(k):
        return data[off + (k >> 1)] >> (0 if k & 1 else 4) & 15
    out = []
    for gy in range(bh):
        k = gy // scale * sw
        for gx in range(bw):
            i = gx // scale
            out.append((nib(k + i) if i < sw else 0) | (nib(k + i - 1) if bold and gx else 0))
    return out


def face(name, scale, bold, band, rng, extras, src=None):
    """src: S's {code: its pixels' offset} and pixels; given, the face keeps none of its own (S's, drawn scaled)"""
    h, by, bh = band
    asc = by + (bh - 7 * scale) // 2 + 7 * scale       # (the capitals centred on Barlow's band, a pixel over up)
    codes = list(range(rng[0], rng[1] + 1)) + extras
    table, data, at = [], bytearray(), {}
    for code in codes:
        rows = rows_of(code)
        assert len(rows) == 7 and len({len(r) for r in rows}) == 1, (name, code)
        bm = bitmap(rows, scale, bold)
        w, gh = len(bm[0]), len(bm)
        ink = [y for y, r in enumerate(bm) if any(r)]
        adv = (w + scale) * 16                         # the glyph and one (scaled) pixel after it
        if not ink:                                    # a blank (the space)
            table.append((0 if src else len(data), adv, 0, 0, 0, 0))
            continue
        y0, y1 = ink[0], ink[-1] + 1
        px = [15 if bm[y][x] else 0 for y in range(y0, y1) for x in range(w)]
        if src:
            off = src[0][code]
            assert drawn(src[1], off, w, y1 - y0, scale, 1 if bold else 0) == px, (name, code)
        else:
            off = at[code] = len(data)
            for i in range(0, len(px), 2):
                data.append(px[i] << 4 | (px[i + 1] if i + 1 < len(px) else 0))
        table.append((off, adv, 0, asc - gh + y0, w, y1 - y0))
    return codes, table, data, asc - 7 * scale, 7 * scale, at


def main():
    out = ["/* generated by tools/gen_pixel_font.py: THEME CRYPT's pixel faces (src/gfx.c aafont_t) */", "#pragma once", ""]
    src = None
    for name, scale, bold, band, rng, extras in FACES:
        codes, table, data, cy, ch, at = face(name, scale, bold, band, rng, extras, src)
        n = f"AF_{name}_PX"
        out.append(f"static const aag_t {n}_G[{len(table)}] = {{" +
                   ", ".join(f"{{{o}, {a}, {bx}, {by}, {bw}, {bh}}}" for o, a, bx, by, bw, bh in table) + "};")
        if not src:
            out.append(f"static const uint8_t {n}_DATA[{max(1, len(data))}] = {{" + ", ".join(str(b) for b in data) + "};")
        out.append(f"static const uint8_t {n}_EX[{max(1, len(extras))}] = {{" + (", ".join(str(c) for c in extras) or "0") + "};")
        out.append(f"static const aafont_t {n} = {{{band[0]}, {cy + ch}, {rng[0]}, {rng[1]}, {len(extras)}, 0, {n}_G, "
                   f"AF_S_PX_DATA, 0, 0, {n}_EX, 0, 0, {scale | (PX_BOLD if bold else 0)}}};")
        out.append(f"#define {n}_CAP_Y {cy}   /* the capitals: rows {cy} .. {cy + ch - 1} of the line */")
        out.append(f"#define {n}_CAP_H {ch}")
        out.append("")
        print(f"pixel face {name}: {len(codes)} glyphs, " +
              (f"S's pixels at {scale}x{' bold' if bold else ''}" if src else f"{len(data)} bytes of pixels"))
        if not src:
            src = (at, data)
    Path(sys.argv[1]).write_text("\n".join(out))


if __name__ == "__main__":
    main()
