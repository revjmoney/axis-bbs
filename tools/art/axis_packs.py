# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""AXIS theme screens for all three looks, as .ANS files.

usage: python tools/art/axis_packs.py OUTDIR

Writes OUTDIR/iniquity/, OUTDIR/oblivion/ and OUTDIR/axis/, one folder per theme, ready for
/sd/themes/<look>/ on the card:
  MAIN.ANS                 the main menu (stock Oblivion/2 letters for iniquity and oblivion,
                           the board's own letters for axis)
  MATRIX.ANS               the front door lightbar (iniquity and oblivion; |0N%0N marks)
  MSGS FILES DOORS CHAT WHO NODES ACCOUNT EMAIL ONELINER BBSLIST TOPTEN .ANS
                           section headers (the board shows one in place of a section's title)
  GOODBYE.ANS              a logoff screen for the artpack

Styles (our own drawing, inspired by 90s artpacks, nothing copied):
  iniquity  big grey block letters with bright green pixels set in them, a dark shadow, thin
            '/--: :--\\' frames and '[time: ..; date: ..]' status lines in greens
  oblivion  white to cyan to blue letters, acid speckle, double-line frames
  axis      yellow to orange letters with drips, drippy rules
Every title is 4 text rows (3/16 of an 80x25 screen at most); every caption fades from dark at
its ends to bright in its middle. Runs of blanks are sent as cursor-forward codes so each screen
stays well under the board's 7 KB limit for a theme screen.
"""
import os
import random
import sys

ESC = '\x1b['
FULL, UP, DOWN, LIGHT, MED, DARK, SQ, DOT = '\xdb', '\xdf', '\xdc', '\xb0', '\xb1', '\xb2', '\xfe', '\xfa'

# 7-pixel-tall glyphs, 6 wide, chunky two-pixel strokes
G = {
    'A': [' #### ', '##  ##', '##  ##', '######', '##  ##', '##  ##', '##  ##'],
    'B': ['##### ', '##  ##', '##  ##', '##### ', '##  ##', '##  ##', '##### '],
    'C': [' #####', '##    ', '##    ', '##    ', '##    ', '##    ', ' #####'],
    'D': ['##### ', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', '##### '],
    'E': ['######', '##    ', '##    ', '##### ', '##    ', '##    ', '######'],
    'F': ['######', '##    ', '##    ', '##### ', '##    ', '##    ', '##    '],
    'G': [' #####', '##    ', '##    ', '## ###', '##  ##', '##  ##', ' #### '],
    'H': ['##  ##', '##  ##', '##  ##', '######', '##  ##', '##  ##', '##  ##'],
    'I': ['######', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '######'],
    'J': ['  ####', '    ##', '    ##', '    ##', '##  ##', '##  ##', ' #### '],
    'K': ['##  ##', '## ## ', '####  ', '###   ', '####  ', '## ## ', '##  ##'],
    'L': ['##    ', '##    ', '##    ', '##    ', '##    ', '##    ', '######'],
    'M': ['##   ##', '### ###', '## # ##', '##   ##', '##   ##', '##   ##', '##   ##'],
    'N': ['##  ##', '### ##', '######', '## ###', '##  ##', '##  ##', '##  ##'],
    'O': [' #### ', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', ' #### '],
    'P': ['##### ', '##  ##', '##  ##', '##### ', '##    ', '##    ', '##    '],
    'Q': [' #### ', '##  ##', '##  ##', '##  ##', '## ###', '##  ##', ' ### #'],
    'R': ['##### ', '##  ##', '##  ##', '##### ', '## ## ', '##  ##', '##  ##'],
    'S': [' #####', '##    ', '##    ', ' #### ', '    ##', '    ##', '##### '],
    'T': ['######', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  ', '  ##  '],
    'U': ['##  ##', '##  ##', '##  ##', '##  ##', '##  ##', '##  ##', ' #### '],
    'V': ['##  ##', '##  ##', '##  ##', '##  ##', '##  ##', ' #### ', '  ##  '],
    'W': ['##   ##', '##   ##', '##   ##', '## # ##', '## # ##', '### ###', '##   ##'],
    'X': ['##  ##', '##  ##', ' #### ', '  ##  ', ' #### ', '##  ##', '##  ##'],
    'Y': ['##  ##', '##  ##', ' #### ', '  ##  ', '  ##  ', '  ##  ', '  ##  '],
    'Z': ['######', '    ##', '   ## ', '  ##  ', ' ##   ', '##    ', '######'],
    "'": ['##', '##', '  ', '  ', '  ', '  ', '  '],
    ' ': ['   ', '   ', '   ', '   ', '   ', '   ', '   '],
}

LOOKS = {
    #           title ramp (top to bottom)   shadow  frame  accent  bright  fade ramp (edge -> centre)
    'iniquity': dict(ramp=[15, 7, 7, 8], shadow=8, frame=2, accent=10, bright=15, fade=[2, 10, 15], dim=8),
    'oblivion': dict(ramp=[15, 11, 3, 1], shadow=8, frame=3, accent=11, bright=15, fade=[8, 3, 11, 15], dim=8),
    'axis':     dict(ramp=[15, 14, 6, 6], shadow=8, frame=6, accent=14, bright=15, fade=[8, 6, 14, 15], dim=8),
}


def sgr(fg, bg=0):
    return f'{ESC}0;{"1;" if fg >= 8 else ""}{30 + [0, 4, 2, 6, 1, 5, 3, 7][fg & 7]};{40 + [0, 4, 2, 6, 1, 5, 3, 7][bg & 7]}m'


class Canvas:
    def __init__(self, rows=25, cols=80):
        self.rows, self.cols = rows, cols
        self.cells = [[(' ', 7, 0) for _ in range(cols)] for _ in range(rows)]
        self.raw = {}

    def put(self, r, c, ch, fg=7, bg=0):
        if 0 <= r < self.rows and 0 <= c < self.cols:
            self.cells[r][c] = (ch, fg, bg)

    def text(self, r, c, s, fg=7):
        for i, ch in enumerate(s):
            self.put(r, c + i, ch, fg)

    def empty(self, r, c):
        return 0 <= r < self.rows and 0 <= c < self.cols and self.cells[r][c][0] == ' ' and not self.cells[r][c][2]

    def ansi(self, last_row, clear=True):
        out = [ESC + '0m' + (ESC + '2J' + ESC + 'H' if clear else '')]
        for r in range(last_row + 1):
            line = self.cells[r]
            end = max([i + 1 for i, (ch, fg, bg) in enumerate(line) if ch != ' ' or bg] + [0])
            last, c = None, 0
            while c < end:
                if (r, c) in self.raw:
                    out.append(ESC + '0m' + self.raw[(r, c)])
                    last = None
                    c += len(self.raw[(r, c)])
                    continue
                ch, fg, bg = line[c]
                if ch == ' ' and not bg:  # a run of blanks: one cursor-forward code
                    run = c
                    while run < end and line[run][0] == ' ' and not line[run][2] and (r, run) not in self.raw:
                        run += 1
                    if run - c > 3:
                        out.append(f'{ESC}{run - c}C')
                        c = run
                        continue
                if (fg, bg) != last and not (ch == ' ' and not bg and last and last[1] == 0):
                    out.append(sgr(fg, bg))
                    last = (fg, bg)
                out.append(ch)
                c += 1
            if r < last_row:
                out.append(ESC + '0m\r\n')
        out.append(ESC + '0m\r\n')
        return ''.join(out)


def fade(cv, r, c, text, look, keep=None):
    """Each word fades from dark at its ends to bright in its middle (keep: colour for short words)."""
    ramp = LOOKS[look]['fade']
    i = 0
    while i < len(text):
        if text[i] == ' ':
            i += 1
            continue
        j = text.find(' ', i)
        j = len(text) if j < 0 else j
        n = j - i
        if text[i] == '%' and n >= 3:  # an MCI code (%TM %UN ...): one colour, or the board can't find it
            for k in range(n):
                cv.put(r, c + i + k, text[i + k], ramp[-1])
            i = j
            continue
        for k in range(n):
            if n < 3:
                fg = keep if keep is not None else ramp[-1]
            else:
                edge = min(k, n - 1 - k) / max(1, (n - 1) / 2)
                fg = ramp[min(len(ramp) - 1, int(edge * len(ramp)))]
            cv.put(r, c + i + k, text[i + k], fg)
        i = j
    return len(text)


def title(cv, r0, c0, word, look, seed=1):
    """Big block letters, 7 pixels tall = 4 text rows (half-block pairs), with each look's tricks."""
    st = LOOKS[look]
    rnd = random.Random(seed)
    ink, col = set(), 0
    for ch in word.upper():
        g = G.get(ch, G[' '])
        for y, line in enumerate(g):
            for x, p in enumerate(line):
                if p == '#':
                    ink.add((y, col + x))
        col += len(g[0]) + 1
    width = col - 1
    for hy in range(0, 8, 2):
        for x in range(width):
            top, bot = (hy, x) in ink, (hy + 1, x) in ink
            fg = st['ramp'][hy // 2]
            r, c = r0 + hy // 2, c0 + x
            if look == 'iniquity' and top and bot and rnd.random() < 0.07:
                cv.put(r, c, FULL, st['accent'])  # a bright green pixel set into the grey
            elif top and bot:
                cv.put(r, c, FULL, fg)
            elif top:
                cv.put(r, c, UP, fg)
            elif bot:
                cv.put(r, c, DOWN, fg)
    for r in range(r0, r0 + 5):  # drop shadow: one right, one down, in light shade
        for c in range(c0, c0 + width + 2):
            if cv.empty(r, c) and not cv.empty(r - 1, c - 1) and cv.cells[r - 1][c - 1][1] != st['shadow']:
                cv.put(r, c, LIGHT, st['shadow'])
    if look == 'oblivion':  # acid speckle around the letters
        for _ in range(width // 4):
            r, c = r0 + rnd.randrange(-1, 5), c0 + rnd.randrange(-2, width + 3)
            if cv.empty(r, c):
                cv.put(r, c, rnd.choice([SQ, DOWN, UP, LIGHT]), rnd.choice([3, 11, 1, 9, 8]))
    if look == 'axis':  # paint drips under some letter bottoms
        for x in range(width):
            if (6, x) in ink and rnd.random() < 0.18 and cv.empty(r0 + 4, c0 + x):
                cv.put(r0 + 4, c0 + x, rnd.choice([UP, '\xb3', DOWN]), 6)
    return width


def rule(cv, r, look, c0=1, c1=79):
    """A shaded gradient rule: dark at the ends, solid in the middle."""
    st = LOOKS[look]
    span = c1 - c0
    shades = [LIGHT, MED, DARK, FULL, DARK, MED, LIGHT]
    for i in range(span):
        t = min(i, span - 1 - i) / max(1, span / 2)
        ch = shades[min(3, int(t * 5))] if t < 0.8 else FULL
        cv.put(r, c0 + i, UP if look == 'iniquity' else ch, st['frame'] if t < 0.5 else st['accent'])


def status(cv, r, section, look):
    if look == 'iniquity':
        cv.text(r, 1, '[', 2)
        fade(cv, r, 2, 'time: %TM;  date: %DT', look)
        cv.text(r, 24, ']', 2)
        cv.text(r, 26, '%% [ ==', 2)
        fade(cv, r, 34, section, look)
        cv.text(r, 35 + len(section), '== ]', 2)
        fade(cv, r, 60, 'node %NN  %UN', look)
    elif look == 'oblivion':
        cv.text(r, 1, '\xcd\xcd\xb5', 3)
        fade(cv, r, 5, section, look)
        cv.text(r, 6 + len(section), '\xc6' + '\xcd' * 8, 3)
        fade(cv, r, 50, '%BN  node %NN', look)
    else:
        cv.text(r, 1, '\xdc\xdf\xdc', 6)
        fade(cv, r, 5, section, look)
        cv.text(r, 6 + len(section), '\xdf\xdc\xdf\xdc', 6)
        fade(cv, r, 50, '%BN  node %NN', look)


def frame(cv, r0, c0, r1, c1, label, look):
    """Iniquity '/--: label :--\\' thin frames; double lines for oblivion; blocks for axis."""
    st = LOOKS[look]
    if look == 'iniquity':
        tl, tr, bl, br, h, v = '/', '\\', '\\', '/', '-', '~'
    elif look == 'oblivion':
        tl, tr, bl, br, h, v = '\xc9', '\xbb', '\xc8', '\xbc', '\xcd', '\xba'
    else:
        tl, tr, bl, br, h, v = '\xdc', '\xdc', '\xdf', '\xdf', '\xdc', '\xdb'
    cv.put(r0, c0, tl, st['frame']); cv.put(r0, c1, tr, st['frame'])
    cv.put(r1, c0, bl, st['frame']); cv.put(r1, c1, br, st['frame'])
    for c in range(c0 + 1, c1):
        cv.put(r0, c, h, st['frame'] if (c - c0) % 9 else st['accent'])
        cv.put(r1, c, h if look != 'axis' else '\xdf', st['frame'] if (c - c0) % 9 else st['accent'])
    for r in range(r0 + 1, r1):
        cv.put(r, c0, v, st['frame'] if look != 'iniquity' else 8)
        cv.put(r, c1, v, st['frame'] if look != 'iniquity' else 8)
    if label:
        lab = ': ' + label + ' :' if look == 'iniquity' else ' ' + label + ' '
        start = c0 + 3
        cv.text(r0, start, lab, st['frame'])
        fade(cv, r0, start + (2 if look == 'iniquity' else 1), label, look)


def header(name, section, look, seed):
    cv = Canvas(7)
    w = title(cv, 0, 2, section, look, seed=seed)
    tag = 'axis' if look != 'axis' else 'u.g. cnc'
    fade(cv, 3, min(76, 4 + w), tag, look)
    status(cv, 5, section.lower(), look)
    rule(cv, 6, look)
    return cv.ansi(6, clear=False)


MAIN_O2 = [[('a', 'account & tagline'), ('c', 'chat with sysop'), ('e', 'private email'), ('f', 'feedback'), ('!', 'scan new msgs'), ('g', 'goodbye')],
           [('m', 'message bases'), ('t', 'transfers'), ('x', 'file vaults'), ('o', 'open doors'), ('l', 'list users'), ('w', 'last callers')],
           [('r', 'rumors'), ('b', 'bbs list'), ('v', 'voting booth'), ('-', 'top ten'), ('=', 'multinode'), ('s', 'system stats')]]
MAIN_AXIS = [[('M', 'messages'), ('E', 'private email'), ('S', 'scan new msgs'), ('O', 'oneliners'), ('R', 'rumors'), ('G', 'goodbye')],
             [('F', 'file areas'), ('D', 'doors'), ('T', 'teleconference'), ('N', 'multinode'), ('W', "who's online"), ('L', 'last callers')],
             [('U', 'your account'), ('B', 'bbs list'), ('V', 'voting booth'), ('K', 'top ten'), ('P', 'page sysop'), ('I', 'system info')]]


def main_menu(look, seed):
    st = LOOKS[look]
    cv = Canvas(22)
    w = title(cv, 0, 3, 'AXIS', look, seed=seed)
    fade(cv, 1, 6 + w, 'u n d e r g r o u n d', look)
    fade(cv, 2, 6 + w, 'c  n  c', look)
    cv.text(3, 6 + w, 'rotatocam.com  ' + DOT + '  jmscnc.com', st['dim'])
    status(cv, 5, 'main menu', look)
    frame(cv, 7, 2, 15, 77, 'commands', look)
    cols = MAIN_O2 if look != 'axis' else MAIN_AXIS
    for ci, col in enumerate(cols):
        for ri, (k, name) in enumerate(col):
            r, c = 8 + ri + (1 if ri >= 3 else 0), 6 + ci * 24
            cv.text(r, c, '(' if look == 'iniquity' else '[', st['dim'])
            cv.put(r, c + 1, k, st['accent'])
            cv.text(r, c + 2, ')' if look == 'iniquity' else ']', st['dim'])
            fade(cv, r, c + 4, name, look)
    rule(cv, 16, look)
    fade(cv, 17, 3, 'pick a letter  ' + DOT + '  ? redraws this menu', look)
    fade(cv, 18, 50, '-rev ' + DOT + ' axis underground', look)
    return cv.ansi(18)


def matrix_screen(look, seed):
    st = LOOKS[look]
    cv = Canvas(22)
    w = title(cv, 1, 8, 'AXIS', look, seed=seed)
    fade(cv, 2, 11 + w, 'u n d e r g r o u n d   c n c', look)
    fade(cv, 3, 11 + w, 'myrtle beach  ' + DOT + '  sc', look)
    status(cv, 6, 'front door', look)
    frame(cv, 8, 6, 14, 73, 'matrix', look)
    for i, c in enumerate([12, 26, 40, 54]):  # the board draws its lightbar on these marks
        cv.put(11, c - 2, DOT, st['dim'])
        cv.raw[(11, c)] = f'|0{i + 1}%0{i + 1}'
        cv.put(11, c + 8, DOT, st['dim'])
    rule(cv, 16, look)
    fade(cv, 17, 8, 'axis micro bbs firmware', look)
    fade(cv, 17, 46, 'rotatocam.com  jmscnc.com', look)
    return cv.ansi(17)


def goodbye(look, seed):
    cv = Canvas(14)
    w = title(cv, 1, 4, 'LATER', look, seed=seed)
    fade(cv, 2, 8 + w, 'thanks for calling', look)
    fade(cv, 3, 8 + w, 'axis underground cnc', look)
    rule(cv, 6, look)
    fade(cv, 7, 6, 'chips or it didn\'t happen', look)
    fade(cv, 9, 6, 'call back soon  ' + DOT + '  %BN', look)
    return cv.ansi(10)


HEADERS = [('MSGS', 'MESSAGES'), ('FILES', 'FILES'), ('DOORS', 'DOORS'), ('CHAT', 'CHAT'), ('WHO', "WHO'S ON"),
           ('NODES', 'NODES'), ('ACCOUNT', 'ACCOUNT'), ('EMAIL', 'EMAIL'), ('ONELINER', 'ONELINERS'), ('BBSLIST', 'BBS LIST'),
           ('TOPTEN', 'TOP TEN')]


def main(outdir):
    for li, look in enumerate(LOOKS):
        d = os.path.join(outdir, look)
        os.makedirs(d, exist_ok=True)
        files = {'MAIN.ANS': main_menu(look, 11 + li), 'GOODBYE.ANS': goodbye(look, 31 + li)}
        if look != 'axis':
            files['MATRIX.ANS'] = matrix_screen(look, 7 + li)
        for i, (name, text) in enumerate(HEADERS):
            files[name + '.ANS'] = header(name, text, look, 100 + i + li * 20)
        for name, art in files.items():
            data = art.encode('latin-1')
            assert len(data) < 7168, (look, name, len(data))
            open(os.path.join(d, name), 'wb').write(data)
        print(look, len(files), 'screens, largest', max(len(a.encode('latin-1')) for a in files.values()), 'bytes')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else 'art_out')
