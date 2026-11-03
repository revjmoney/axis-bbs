# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw the AXIS waiting-for-caller logo as CP437 ANSI (art/wfc.ans), 80 columns by 8 rows.

A compact drippy logo for the top 64 pixels of the CYD's waiting-for-caller screen (4x8 cells);
the firmware draws the live board info under it. Rev's scheme: black, grey, orange, white.
Original art (no TheDraw font). The file is 30 rows tall for the firmware's AnsiScreen; rows 8
and down are left blank. Edit art/wfc.ans in PabloDraw or Moebius, then rebuild the firmware copy
with tools/art/ans2header.py.

usage: python -I tools/art/make_wfc.py art/wfc.ans
"""
import random
import sys

W, H, USED = 80, 30, 8
TOPROW = 1  # a row of headroom so the letter tops aren't flush with the screen edge
FULL, MED, LIGHT, TOP, BOT, DOT = 0xDB, 0xB1, 0xB0, 0xDF, 0xDC, 0xFA

LETTERS = {  # 5 rows; each pixel becomes 3 columns
    'A': [" ### ", "#   #", "#####", "#   #", "#   #"],
    'X': ["#   #", " # # ", "  #  ", " # # ", "#   #"],
    'I': ["###", " # ", " # ", " # ", "###"],
    'S': [" ####", "#    ", " ### ", "    #", "#### "],
}
# CGA attributes: 0x0F white, 0x07 light grey, 0x08 dark grey, 0x06 brown (VGA orange)
BAND = [(FULL, 0x0F), (TOP, 0x7F), (TOP, 0x67), (FULL, 0x06), (FULL, 0x06)]


def main(out):
    rnd = random.Random(80)
    grid = [[(0x20, 0x07) for _ in range(W)] for _ in range(H)]

    def put(r, c, ch, attr):
        if 0 <= r < USED and 0 <= c < W:
            grid[r][c] = (ch, attr)

    mask = [[False] * W for _ in range(H)]
    col = 2
    for letter in "AXIS":
        rows = LETTERS[letter]
        for r, line in enumerate(rows):
            for c, px in enumerate(line):
                if px == '#':
                    for k in range(3):
                        mask[r + TOPROW][col + 3 * c + k] = True
        col += 3 * len(rows[0]) + 3

    for _ in range(26):  # spray specks
        put(rnd.randrange(USED - 1), rnd.randrange(W), rnd.choice([LIGHT, DOT, DOT]), rnd.choice([0x08, 0x08, 0x06]))
    T = TOPROW
    for r in range(T, T + 5):  # shadow, then the letters
        for c in range(W - 1):
            if mask[r][c] and not mask[r + 1][c + 1]:
                put(r + 1, c + 1, LIGHT, 0x08)
    for r in range(T, T + 5):
        for c in range(W):
            if mask[r][c]:
                put(r, c, *BAND[r - T])
    for c in range(W):  # drips off the bottom row (the banner, drawn last, covers any that reach it)
        if not mask[T + 4][c] or rnd.random() > 0.55:
            continue
        colour = rnd.choice([0x06, 0x06, 0x06, 0x08])
        if rnd.random() < 0.5:
            put(T + 5, c, FULL, colour)
            put(T + 6, c, TOP, colour)
        else:
            put(T + 5, c, TOP, colour)
    # the name, black on orange, at the right of the logo
    name = " U N D E R G R O U N D   C N C "
    c0 = W - len(name) - 1
    put(7, c0 - 1, 0xDE, 0x06)
    for i, ch in enumerate(name):
        put(7, c0 + i, ord(ch), 0x60)
    put(7, c0 + len(name), 0xDD, 0x06)
    tag = "Axis Micro BBS Firmware"
    for i, ch in enumerate(tag):
        put(7, 2 + i, ord(ch), 0x08)

    cga = [0, 4, 2, 6, 1, 5, 3, 7]  # CGA index -> ANSI colour number
    data = bytearray(b"\x1b[0m\x1b[2J\x1b[H")
    for r in range(H):
        last = None
        for c in range(W):
            ch, attr = grid[r][c]
            if attr != last:
                fg, bg = attr & 15, attr >> 4
                data += f"\x1b[0;{'1;' if fg > 7 else ''}{30 + cga[fg & 7]};{40 + cga[bg & 7]}m".encode()
                last = attr
            data.append(ch)
        data += b"\x1b[0m"  # full 80-column rows: no CR LF (BBS terminals wrap right after column 80)
    open(out, "wb").write(bytes(data))
    print(out, len(data), "bytes")


if __name__ == "__main__":
    main(sys.argv[1])
