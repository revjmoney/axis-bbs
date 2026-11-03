# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw the AXIS waiting-for-caller screen as an 80x30 CP437 ANSI file (art/wfc.ans).

Big drippy block letters, original art (no TheDraw font). Rows 17-28 are left for the
live status the firmware writes in. Edit art/wfc.ans in PabloDraw or Moebius afterwards,
then run tools/art/ans2header.py to rebuild the firmware copy.

usage: python -I tools/art/make_wfc.py art/wfc.ans
"""
import random
import sys

W, H = 80, 30
FULL, DARK, MED, LIGHT, TOP, BOT = 0xDB, 0xB2, 0xB1, 0xB0, 0xDF, 0xDC

LETTERS = {
    'A': ["  █████  ", " ███ ███ ", "███   ███", "█████████", "███   ███", "███   ███", "███   ███"],
    'X': ["███   ███", " ███ ███ ", "  █████  ", "   ███   ", "  █████  ", " ███ ███ ", "███   ███"],
    'I': ["█████", " ███ ", " ███ ", " ███ ", " ███ ", " ███ ", "█████"],
    'S': [" ████████", "███      ", "███      ", " ███████ ", "      ███", "      ███", "████████ "],
}


def main(out):
    rnd = random.Random(1337)
    grid = [[(0x20, 0x07) for _ in range(W)] for _ in range(H)]

    def put(r, c, ch, attr):
        if 0 <= r < H and 0 <= c < W:
            grid[r][c] = (ch, attr)

    def text(r, c, s, attr):
        for i, ch in enumerate(s):
            put(r, c + i, ord(ch) if isinstance(ch, str) else ch, attr)

    # logo mask: letters doubled sideways, 2 columns of gap between letters
    mask = [[False] * W for _ in range(H)]
    top, col = 0, 2
    for letter in "AXIS":
        rows = LETTERS[letter]
        for r, line in enumerate(rows):
            for c, px in enumerate(line):
                if px == '█':
                    mask[top + r][col + 2 * c] = mask[top + r][col + 2 * c + 1] = True
        col += 2 * len(rows[0]) + 4

    # letter body: bright crown, a shaded band, deep red body
    for r in range(H):
        for c in range(W):
            if not mask[r][c]:
                continue
            band = r - top
            if band == 0:
                put(r, c, FULL, 0x0C)
            elif band == 1:
                put(r, c, DARK, 0x4C)
            else:
                put(r, c, FULL, 0x04)

    # paint drips off the bottom of each letter: long runs, short runs, falling drops
    base = top + 6
    for c in range(W):
        if not mask[base][c] or rnd.random() > 0.55:
            continue
        length = rnd.choice([1, 1, 2, 2, 3, 3, 4])
        rr = base + 1
        for i in range(length):
            put(rr, c, FULL if i < length - 1 else TOP, 0x04)
            rr += 1
        if rnd.random() < 0.35 and rr + 1 < 11:
            put(rr + 1, c, BOT, 0x04)  # a drop falling away

    # name and tagline
    name = " ".join("UNDERGROUND") + "   " + " ".join("CNC")
    text(11, (W - len(name)) // 2, name, 0x0E)
    tag = "rotary axes \xfa toolpaths \xfa G-code \xfa the underground workshop"

    # status panel frame (the firmware writes the live values inside)
    text(16, 0, [0xC9] + [0xCD] * 78 + [0xBB], 0x08)
    for r in range(17, 28):
        put(r, 0, 0xBA, 0x08)
        put(r, 79, 0xBA, 0x08)
    text(28, 0, [0xC8] + [0xCD] * 78 + [0xBC], 0x08)
    text(16, 3, [0xB5] + [ord(ch) for ch in " WAITING FOR CALLER "] + [0xC6], 0x0B)
    text(16, 60, [0xB5] + [ord(ch) for ch in " node 1 "] + [0xC6], 0x08)
    text(29, 2, "Axis Micro BBS Firmware by Rev. J. Money  \xfa  rotatocam.com  \xfa  jmscnc.com".encode('latin1'), 0x08)

    # write ANSI: clear, then each row with SGR changes only where the colour changes
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
        data += b"\x1b[0m"
        if r < H - 1:
            data += b"\r\n"
    open(out, "wb").write(bytes(data))
    print(out, len(data), "bytes")


if __name__ == "__main__":
    main(sys.argv[1])
