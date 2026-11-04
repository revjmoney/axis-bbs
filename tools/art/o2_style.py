# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw AXIS screens in the 90s Oblivion/2 artpack style (grey label bar, big blocky letters that
fade white to cyan to green, a dark shadow, acid speckle, small spaced captions), as .ANS files.

usage: python tools/art/o2_style.py OUTDIR
Writes MATRIX.ANS and MAIN.ANS for /sd/themes/oblivion/ (the matrix carries O/2 pulldown marks
|01%01 ... |04%04 where the board puts its lightbar), plus ROTATO.ANS and AXISNFO.ANS for an
artpack. All original art; no artpack pieces are copied.
"""
import os
import random
import sys

ESC = '\x1b['
FULL, UP, DOWN, LIGHT, MED, DARK, SQ = '\xdb', '\xdf', '\xdc', '\xb0', '\xb1', '\xb2', '\xfe'

# 7-row pixel glyphs, '#' = ink
GLYPHS = {  # chunky: two-pixel strokes so they read at a glance
    'A': [' #### ', '##  ##', '##  ##', '######', '##  ##', '##  ##', '##  ##'],
    'X': ['##  ##', '##  ##', ' #### ', '  ##  ', ' #### ', '##  ##', '##  ##'],
    'I': ['######', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '######'],
    'S': [' #####', '##    ', '##    ', ' #### ', '    ##', '    ##', '##### '],
    'R': ['##### ', '##  ##', '##  ##', '##### ', '## ## ', '##  ##', '##  ##'],
    'O': [' #### ', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', ' #### '],
    'T': ['######', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  '],
    'C': [' #####', '##    ', '##    ', '##    ', '##    ', '##    ', ' #####'],
    'M': ['##   ##', '### ###', '## # ##', '##   ##', '##   ##', '##   ##', '##   ##'],
    'U': ['##  ##', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', ' #### '],
    'N': ['##  ##', '### ##', '######', '## ###', '##  ##', '##  ##', '##  ##'],
    'D': ['##### ', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', '##### '],
    'E': ['######', '##    ', '##    ', '##### ', '##    ', '##    ', '######'],
    'G': [' #####', '##    ', '##    ', '## ###', '##  ##', '##  ##', ' #### '],
    '.': ['  ', '  ', '  ', '  ', '  ', '##', '##'],
    ' ': ['   ', '   ', '   ', '   ', '   ', '   ', '   '],
}

# CGA colour numbers: 0 black 1 blue 2 green 3 cyan 7 grey 8 dark grey 10 bright green 11 bright cyan 15 white
def sgr(fg, bg=0):
    bright = fg >= 8
    return f'{ESC}0;{"1;" if bright else ""}{30 + [0, 4, 2, 6, 1, 5, 3, 7][fg & 7]};{40 + [0, 4, 2, 6, 1, 5, 3, 7][bg & 7]}m'


class Canvas:
    def __init__(self, rows=25, cols=80):
        self.rows, self.cols = rows, cols
        self.cells = [[(' ', 7, 0) for _ in range(cols)] for _ in range(rows)]
        self.raw = {}  # (row, col) -> literal text drawn there as is (pulldown marks)

    def put(self, r, c, ch, fg=7, bg=0):
        if 0 <= r < self.rows and 0 <= c < self.cols:
            self.cells[r][c] = (ch, fg, bg)

    def text(self, r, c, s, fg=7, bg=0):
        for i, ch in enumerate(s):
            self.put(r, c + i, ch, fg, bg)

    def empty(self, r, c):
        return 0 <= r < self.rows and 0 <= c < self.cols and self.cells[r][c][0] == ' '

    def ansi(self, last_row=None):
        out = [ESC + '0m' + ESC + '2J' + ESC + 'H']
        last = None
        rows = last_row + 1 if last_row is not None else self.rows
        for r in range(rows):
            line = self.cells[r]
            end = max([i + 1 for i, cell in enumerate(line) if cell[0] != ' ' or cell[2]] + [0])
            c = 0
            while c < end:
                if (r, c) in self.raw:
                    out.append(ESC + '0m' + self.raw[(r, c)])
                    last = None
                    c += len(self.raw[(r, c)])
                    continue
                ch, fg, bg = line[c]
                if (fg, bg) != last and not (ch == ' ' and bg == (last or (0, 0))[1]):
                    out.append(sgr(fg, bg))
                    last = (fg, bg)
                out.append(ch)
                c += 1
            if r < rows - 1:
                out.append(ESC + '0m\r\n')
                last = None
        out.append(ESC + '0m')
        return ''.join(out)


def logo(cv, r0, c0, word, sx=2, sy=2, gap=1, seed=1):
    """Big letters: each glyph pixel is sx columns by sy half-rows, so 7 pixel rows = 7 * sy / 2 text rows.
    Rows fade white, grey, bright cyan, cyan, green; a dark shadow drops down and right."""
    rnd = random.Random(seed)
    px = []  # (pixel row, pixel col) inked
    col = 0
    for ch in word:
        g = GLYPHS[ch]
        for y, line in enumerate(g):
            for x, p in enumerate(line):
                if p == '#':
                    for dy in range(sy):
                        for dx in range(sx):
                            px.append((y * sy + dy, col + x * sx + dx))
        col += len(g[0]) * sx + gap * sx
    ink = set(px)
    height = 7 * sy  # half-rows
    ramp = [15, 15, 7, 11, 11, 3, 3, 10, 2, 2]
    for hy in range(0, height, 2):
        for x in range(col):
            top, bot = (hy, x) in ink, (hy + 1, x) in ink
            fg = ramp[min(len(ramp) - 1, hy * len(ramp) // height)]
            r, c = r0 + hy // 2, c0 + x
            if top and bot:
                cv.put(r, c, FULL, fg)
            elif top:
                cv.put(r, c, UP, fg)
            elif bot:
                cv.put(r, c, DOWN, fg)
    # shadow: one row down, one column right, light dark-grey shade where nothing is drawn
    rows = height // 2
    for r in range(r0, r0 + rows + 1):
        for c in range(c0, c0 + col + 2):
            if cv.empty(r, c) and not cv.empty(r - 1, c - 1) and cv.cells[r - 1][c - 1][1] != 8:
                cv.put(r, c, LIGHT, 8)
    # acid speckle: little blocks scattered around the letters, cyan and green
    for _ in range(col // 5):
        r = r0 + rnd.randrange(-1, rows + 2)
        c = c0 + rnd.randrange(-2, col + 3)
        if cv.empty(r, c):
            cv.put(r, c, rnd.choice([SQ, DOWN, UP, LIGHT]), rnd.choice([3, 11, 2, 10, 8]))
    return col


def label_bar(cv, r, text, width=38):
    """The grey 'matrix menu...' bar across the top left, Lord Jazz style but our own drawing."""
    for c in range(1, width):
        cv.put(r, c, UP if c % 7 else DOWN, 7 if c < width - 8 else 8)
    cv.text(r + 1, 1, ' ' * width, 0, 7)
    cv.text(r + 1, 2, text, 0, 7)
    for c in range(width - 6, width):
        cv.put(r + 1, c, DARK if c % 2 else MED, 8, 7)


def spaced(cv, r, c, text, fg=15, dim=7):
    s = ' '.join(text)
    for i, ch in enumerate(s):
        cv.put(r, c + i, ch, fg if i % 4 == 0 else dim)


def matrix(out):
    cv = Canvas()
    label_bar(cv, 1, 'matrix menu...')
    w = logo(cv, 4, 6, 'AXIS', sx=2, sy=2, seed=7)
    cv.text(4, 6 + w + 2, 'underground', 11)
    cv.text(5, 6 + w + 2, 'cnc', 15)
    cv.text(7, 6 + w + 2, 'myrtle beach', 3)
    cv.text(8, 6 + w + 2, 'axis.local', 8)
    spaced(cv, 13, 10, 'axis underground cnc', 15, 7)
    cv.text(13, 52, 'rev^axis', 3)
    # the board puts its lightbar on these marks (and blanks them)
    for i, c in enumerate([12, 26, 40, 54]):
        cv.text(17, c - 2, '\xfa', 8)
        cv.raw[(17, c)] = f'|0{i + 1}%0{i + 1}'
        cv.text(17, c + 7, '\xfa', 8)
    cv.text(20, 10, 'rotatocam.com', 11)
    cv.text(20, 25, '\xfa', 8)
    cv.text(20, 27, 'jmscnc.com', 3)
    cv.text(20, 39, '\xfa', 8)
    cv.text(20, 41, 'axis micro bbs firmware', 7)
    open(out, 'wb').write(cv.ansi(21).encode('latin-1'))  # the strings hold CP437 byte values


def main_menu(out):
    cv = Canvas()
    label_bar(cv, 1, 'main menu...')
    w = logo(cv, 4, 6, 'AXIS', sx=2, sy=2, seed=11)
    cv.text(5, 6 + w + 2, 'underground', 11)
    cv.text(6, 6 + w + 2, 'cnc', 15)
    # stock Oblivion/2 letters (the board maps them onto its own commands when this art is up)
    cols = [
        [('a', 'account/tagline'), ('c', 'chat'), ('e', 'email/msgs'), ('f', 'feedback'), ('g', 'goodbye')],
        [('l', 'list users'), ('m', 'messages'), ('o', 'open doors'), ('r', 'rumors'), ('s', 'system stats')],
        [('t', 'transfers'), ('x', 'file vaults'), ('y', 'user stats'), ('=', 'multinode'), ('.', 'credits')],
    ]
    for ci, col in enumerate(cols):
        for ri, (k, name) in enumerate(col):
            cv.text(13 + ri, 8 + ci * 24, k, 11)
            cv.text(13 + ri, 10 + ci * 24, name, 3 if ri % 2 else 7)
    cv.text(19, 60, '-rev^axis', 3)
    open(out, 'wb').write(cv.ansi(19).encode('latin-1'))  # the strings hold CP437 byte values


def rotato(out):
    cv = Canvas()
    label_bar(cv, 1, 'rotatocam...')
    w = logo(cv, 5, 4, 'ROTATOCAM', sx=1, sy=2, seed=3)
    cv.text(11, 4 + w, '.com', 15)
    spaced(cv, 15, 6, 'rotary toolpaths for the rest of us', 11, 3)
    cv.text(18, 6, '4th axis cam', 7)
    cv.text(22, 20, '\xfa', 8)
    cv.text(18, 22, 'free and pro', 7)
    cv.text(22, 36, '\xfa', 8)
    cv.text(18, 38, 'rotatocam.com', 11)
    cv.text(18, 60, 'rev^axis', 3)
    open(out, 'wb').write(cv.ansi(18).encode('latin-1'))  # the strings hold CP437 byte values


def nfo(out):
    cv = Canvas()
    label_bar(cv, 1, 'axis artpack #1...')
    logo(cv, 4, 6, 'AXIS', sx=2, sy=2, seed=19)
    lines = [
        ('AXIS UNDERGROUND CNC', 15), ('', 7),
        ('original screens for the board, in the old', 7),
        ('oblivion/2 artpack style.', 7), ('', 7),
        ('MATRIX.ANS  the matrix (pulldown marks for the lightbar)', 3),
        ('MAIN.ANS    the main menu (stock o/2 letters)', 3),
        ('ROTATO.ANS  rotatocam.com', 3), ('', 7),
        ('telnet axis.local  \xfa  rotatocam.com  \xfa  jmscnc.com', 11),
    ]
    for i, (s, fg) in enumerate(lines):
        cv.text(13 + i, 8, s, fg)
    open(out, 'wb').write(cv.ansi(23).encode('latin-1'))  # the strings hold CP437 byte values


if __name__ == '__main__':
    d = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(d, exist_ok=True)
    matrix(os.path.join(d, 'MATRIX.ANS'))
    main_menu(os.path.join(d, 'MAIN.ANS'))
    rotato(os.path.join(d, 'ROTATO.ANS'))
    nfo(os.path.join(d, 'AXISNFO.ANS'))
    print('wrote 4 screens to', d)
