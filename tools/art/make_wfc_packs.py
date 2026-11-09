# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw the waiting-for-caller (WFC) screens of all eight looks: WFC.ANS (80x25, telnet, panel console, serial) and
WFCCYD.ANS (45x24, the CYD's own screen in 7x10 cells), CP437 ANSI with the pipe codes of docs/WFC-CODES.md.

Each look has one style function (palette, frames, rule pattern, logo ramp, a small motif drawn in half blocks), matched
to its ANSI pack (the README and previews of Rev's axis-*-ansi-2026-11-07 zips):
  axis         the house look: yellow to brown letters with drips, an end mill (as art/wfc80.ans and art/wfccyd.ans)
  crt          crt-hardcore: green and silver glitch, shard shades, a green skull
  haunted      haunted-crt: green scanlines, a grey CRT bezel with a phantom face
  sea          deep-sea: navy, teal and cyan, wave rules, an anglerfish with its lure
  cathedral    occult-cathedral: purple and magenta, checker rules, a stained-glass lancet
  skulls       skulls: red, grey and bone, red checker rules, a bone skull
  underground  underground: blue, cyan and magenta colour bars, a gear
  wasteland    wasteland: amber and brown, hazard stripes, a salvage crawler

Every screen is checked here before it is written: rendered through a Python copy of the firmware's AnsiTerm (deferred wrap)
and of a BBS terminal that wraps right after the last column (bbs_shots.Term), it must fill exactly 80x25 or 45x24 with no
scroll; a refresh (home, overwrite, as Node::wfcDraw does) over long values must leave the same screen as a fresh draw; every
code must be there, the file under 7 KB, and on the CYD every key's tap area must be its own label and no node field.

usage: python -I tools/art/make_wfc_packs.py OUTDIR [PREVIEWDIR]
       OUTDIR/<look>/WFC.ANS and WFCCYD.ANS; PREVIEWDIR gets a PNG per screen (sample values) and one contact sheet per size
"""
import os
import re
import sys

ESC = '\x1b['
CAP = 7 * 1024
LOOK_ORDER = ['axis', 'crt', 'haunted', 'sea', 'cathedral', 'skulls', 'underground', 'wasteland']
DOS2ANSI = [0, 4, 2, 6, 1, 5, 3, 7]
KEYS = {'KS': 'S', 'KU': 'U', 'KM': '#', 'KB': 'B', 'KF': 'F', 'KL': 'L', 'KE': 'E', 'KA': 'A', 'KV': '!', 'KO': ' '}
# the keys in the order they are listed: code, the text right after it (the tap label), and the label's lead-in
KEY_LIST = [('KO', '[space] logon'), ('KS', 'settings'), ('KU', 'users'), ('KM', 'menus'), ('KB', 'boards'),
            ('KF', 'files'), ('KL', 'logs'), ('KE', 'events'), ('KA', 'archive'), ('KV', 'validate')]
COMMON = ['BN', 'SN', 'MB', 'VN', 'CT', 'PT', 'UT', 'LC', 'SD', 'RM', 'IP', 'UP', 'TM', 'DT', 'PG'] + list(KEYS)
REQUIRED = {80: COMMON + ['N' + c for c in 'ABCDEFGH'] + ['EA', 'EB', 'EC'],
            45: COMMON + ['N' + c for c in 'ABCD'] + ['EA', 'EB', 'EC']}

# ---------------------------------------------------------------------------------------------------------------- canvas


class Grid:
    """cols x rows cells of (char, fg 0-15, bg 0-7) in DOS colour order, plus code tokens (a field or a |K? key)."""

    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.cells = [[[' ', 7, 0] for _ in range(cols)] for _ in range(rows)]
        self.tokens = {}  # (r, c) -> (text in the file, width on screen)
        self.owned = set()  # cells under a token

    def put(self, r, c, ch, fg=7, bg=0):
        assert 0 <= r < self.rows and 0 <= c < self.cols, (r, c, ch)
        assert (r, c) not in self.owned, ('a token is here', r, c)
        assert bg < 8
        self.cells[r][c] = [ch, fg, bg]

    def text(self, r, c, s, fg=7, bg=0):
        for i, ch in enumerate(s):
            self.put(r, c + i, ch, fg, bg)
        return c + len(s)

    def field(self, r, c, code, w, fg, bg=0):
        assert w >= len(code) + 3, (code, w)
        tok = '@' + code + '#' * (w - len(code) - 2) + '@'
        assert len(tok) == w
        return self._token(r, c, tok, w, fg, bg)

    def key(self, r, c, code, fg, bg=0):
        return self._token(r, c, '|' + code, 1, fg, bg)

    def _token(self, r, c, tok, w, fg, bg):
        assert c + w <= self.cols, (tok, r, c)
        for i in range(w):
            assert (r, c + i) not in self.owned, (tok, r, c)
            self.cells[r][c + i] = ['\0', fg, bg]
            self.owned.add((r, c + i))
        self.tokens[(r, c)] = (tok, w)
        return c + w

    def pixels(self, r, c, rows, pal):
        """Half-block pixel art: rows of letters (two pixel rows to a text row), pal maps a letter to a colour ('.' = none)."""
        for y in range(0, len(rows), 2):
            top, bot = rows[y], rows[y + 1] if y + 1 < len(rows) else '.' * len(rows[y])
            for x in range(len(top)):
                t, b = pal.get(top[x]), pal.get(bot[x])
                if t is None and b is None:
                    continue
                self.put(r + y // 2, c + x, *halfblock(t or 0, b or 0))

    def blank(self, r, c):
        ch, fg, bg = self.cells[r][c]
        return ch == ' ' and bg == 0 and (r, c) not in self.owned


def halfblock(t, b):
    if t == b:
        return (' ', 7, 0) if t == 0 else ('█', t, 0)
    if t == 0:
        return ('▄', b, 0)
    if b == 0:
        return ('▀', t, 0)
    if b < 8:
        return ('▀', t, b)
    if t < 8:
        return ('▄', b, t)
    return ('▀', t, b - 8)  # both bright: the bottom one dims (no iCE colours)


def sgr(cur, fg, bg):
    """The shortest SGR from the attribute in force (None: unknown) to fg/bg."""
    if cur is None or (cur[0] >= 8 and fg < 8):
        p = ['0'] + (['1'] if fg >= 8 else []) + ([str(30 + DOS2ANSI[fg & 7])] if fg & 7 != 7 else []) + \
            ([str(40 + DOS2ANSI[bg])] if bg else [])
    else:
        p = (['1'] if fg >= 8 and cur[0] < 8 else []) + ([str(30 + DOS2ANSI[fg & 7])] if fg & 7 != cur[0] & 7 else []) + \
            ([str(40 + DOS2ANSI[bg])] if bg != cur[1] else [])
    return ESC + ';'.join(p) + 'm' if p else ''


def serialize(g):
    """The grid as ANSI: a full-width row wraps by itself (no CR LF after it); a shorter row ends with ESC[0m CR LF; the last
    row never reaches the last column. Colours change only where they must (a space only needs its background)."""
    out, cur = [], None
    for r in range(g.rows):
        last = max([c for c in range(g.cols) if not g.blank(r, c)] + [-1])
        if r == g.rows - 1:
            assert last < g.cols - 1, 'the last row must leave its last column empty'
        c = 0
        while c <= last:
            ch, fg, bg = g.cells[r][c]
            if (r, c) in g.tokens:
                tok, w = g.tokens[(r, c)]
                s = sgr(cur, fg, bg)
                out.append(s + tok)
                cur = (fg, bg)
                c += w
                continue
            if ch == ' ' and cur is not None and cur[1] == bg:
                out.append(' ')
            elif ch == '█' and cur is not None and cur[0] == fg:
                out.append(ch)
            else:
                out.append(sgr(cur, fg, bg) + ch)
                cur = (fg, bg)
            c += 1
        if last < g.cols - 1 and r < g.rows - 1:
            out.append(ESC + '0m\r\n')
            cur = (7, 0)
    out.append(ESC + '0m')
    return ''.join(out).encode('cp437')


# ---------------------------------------------------------------------------------------------------------------- models

LONG_NAMES = ["NEWUSERS", "HANDLE", "ALIAS", "LEVEL", "LCALL", "CALLS", "UPLDS", "DNLDS", "POSTS", "NAME", "CFAN", "DATE",
              "TIME", "NODE", "CAN", "CCN", "CFA", "FCN"]


def lint(data):
    """Only what the firmware's expandArt reads as ours: @XX###@ fields and |K? keys; nothing else it would expand."""
    s = data.decode('cp437')
    assert data.decode('cp437').encode('cp437') == data
    for i, ch in enumerate(data):
        assert ch >= 0x20 or ch in (0x1b, 0x0d, 0x0a), ('control byte', i, ch)
    rest = re.sub(r'@[A-Z]{2}#+@', '', s)
    assert '@' not in rest, 'a stray @'
    for m in re.finditer(r'\|', rest):
        code = rest[m.start() + 1:m.start() + 3]
        assert code in KEYS, ('a | that is not a key code', rest[m.start():m.start() + 6])
        after = rest[m.start() + 1:m.start() + 9]
        assert not any(after.startswith(n) for n in LONG_NAMES), after
    assert not re.search(r'%[A-Z]{2}', rest), 'a %XX code'
    assert '{{' not in s and '\x1a' not in s
    return s


def expand(data, values):
    """expandArt for the codes this art uses: a field becomes its value cut or padded to the field's width; a key code its
    key, led by a marker (\x01 + the code) so the terminal can note where it landed."""
    s = data.decode('cp437')

    def fld(m):
        v = values.get(m.group(1), '')
        return (v + ' ' * len(m.group(0)))[:len(m.group(0))]
    s = re.sub(r'@([A-Z]{2})#+@', fld, s)
    return re.sub(r'\|(K[A-Z])', lambda m: '\x01' + m.group(1) + KEYS[m.group(1)], s)


class FwTerm:
    """src/bbs/ansiterm.cpp in Python: the wrap waits for the next character; a scroll is an error here."""

    def __init__(self, cols, rows):
        self.cols, self.rows = cols, rows
        self.cells = [[(' ', 7) for _ in range(cols)] for _ in range(rows)]
        self.x = self.y = 0
        self.fg, self.bg, self.bold = 7, 0, False
        self.spots = []  # (row, col, code)

    def newline(self):
        self.y += 1
        if self.y >= self.rows:
            raise AssertionError('the screen scrolled')

    def put(self, ch):
        if self.x >= self.cols:
            self.x = 0
            self.newline()
        self.cells[self.y][self.x] = (ch, (self.fg | 8 if self.bold else self.fg) | self.bg << 4)
        self.x += 1

    def feed(self, s):
        i = 0
        while i < len(s):
            ch = s[i]
            if ch == '\x01':
                self.spots.append((self.y, self.x, s[i + 1:i + 3]))
                i += 3
                continue
            if ch == '\x1b':
                m = re.match(r'\x1b\[([0-9;?]*)([^0-9;?])', s[i:])
                self.csi(m.group(2), [int(p) if p else -1 for p in m.group(1).replace('?', '').split(';')])
                i += len(m.group(0))
                continue
            if ch == '\r':
                self.x = 0
            elif ch == '\n':
                self.newline()
            elif ord(ch) >= 32:
                self.put(ch)
            i += 1

    def csi(self, f, p):
        arg = lambda i, d: p[i] if i < len(p) and p[i] >= 0 else d
        if f == 'm':
            for v in p:
                v = max(v, 0)
                if v == 0:
                    self.fg, self.bg, self.bold = 7, 0, False
                elif v == 1:
                    self.bold = True
                elif v == 22:
                    self.bold = False
                elif 30 <= v <= 37:
                    self.fg = DOS2ANSI.index(v - 30)
                elif 40 <= v <= 47:
                    self.bg = DOS2ANSI.index(v - 40)
        elif f in 'Hf':
            self.y, self.x = arg(0, 1) - 1, arg(1, 1) - 1
        elif f == 'K':
            for c in range(max(self.x, 0), self.cols):
                self.cells[self.y][c] = (' ', 7)
        elif f == 'J':
            for c in range(max(self.x, 0), self.cols):
                self.cells[self.y][c] = (' ', 7)
            for r in range(self.y + 1, self.rows):
                self.cells[r] = [(' ', 7) for _ in range(self.cols)]
        else:
            raise AssertionError('unexpected CSI ' + f)
        self.x = max(0, min(self.x, self.cols - 1)) if f != 'm' else self.x
        self.y = max(0, min(self.y, self.rows - 1))


def bbs_term(cols, rows):
    """bbs_shots.Term (a BBS terminal that wraps right after the last column) at this size; a scroll is an error."""
    import bbs_shots
    bbs_shots.COLS, bbs_shots.ROWS = cols, rows

    class Strict(bbs_shots.Term):
        def scroll(self):
            raise AssertionError('the screen scrolled')
    return Strict()


def expected(g, values):
    """What the grid should show with these values: (char, attr) per cell, as FwTerm keeps them."""
    out = []
    for r in range(g.rows):
        row = []
        c = 0
        while c < g.cols:
            if (r, c) in g.tokens:
                tok, w = g.tokens[(r, c)]
                _, fg, bg = g.cells[r][c]
                v = KEYS[tok[1:]] if tok.startswith('|') else (values.get(tok[1:3], '') + ' ' * w)[:w]
                row += [(ch, fg | bg << 4) for ch in v]
                c += w
                continue
            ch, fg, bg = g.cells[r][c]
            row.append((ch, fg | bg << 4))
            c += 1
        out.append(row)
    return out


def dos(ansi):
    return DOS2ANSI.index(ansi & 7) | (ansi & 8)


def norm(cells):
    """A blank only shows its background, a full block only its foreground."""
    return [[(ch, a & 0xF0 if ch == ' ' else a & 0x0F if ch == '█' else a) for ch, a in row] for row in cells]


def tap_spans(t):
    """drawWfc's tap areas: a key's label runs from its letter to two spaces, a box line or non-ASCII, the next key, or 14
    cells."""
    spans = []
    keys = [(r, c) for r, c, code in t.spots]
    for r, c, code in t.spots:
        end = c
        for x in range(c + 1, min(t.cols, c + 15)):
            ch = t.cells[r][x][0].encode('cp437')[0]
            if (r, x) in keys or ch < 0x20 or ch >= 0x7f:
                break
            if ch == 0x20 and (x + 1 >= t.cols or t.cells[r][x + 1][0] == ' '):
                break
            end = x
        spans.append((r, c, code, end - c + 1))
    return spans


def check(g, data, size, nodes):
    """Every check this script makes on one screen. Returns each key's tap label (CYD rules) for the log."""
    cols, rows = g.cols, g.rows
    assert len(data) < CAP, ('too big', len(data))
    s = lint(data)
    have = set(re.findall(r'@([A-Z]{2})#+@', s)) | set(re.findall(r'\|(K[A-Z])', s))
    missing = [c for c in REQUIRED[size] if c not in have]
    assert not missing, ('missing codes', missing)
    for code in have:
        assert s.count('|' + code) + len(re.findall('@' + code + '#+@', s)) == 1, ('a code used twice', code)
    long_vals = {code: 'X' * 80 for code in have}
    for vals in (SAMPLE, {}, long_vals):
        want = expected(g, vals)
        fw = FwTerm(cols, rows)
        fw.feed(expand(data, vals))
        assert norm(fw.cells) == norm(want), 'firmware terminal: the screen is not the grid'
        bt = bbs_term(cols, rows)
        bt.feed(re.sub('\x01K[A-Z]', '', expand(data, vals)))
        bt_cells = [[(ch, dos(fg) | dos(bg) << 4) for ch, fg, bg in row] for row in bt.cells]  # (its colours: ANSI order)
        assert norm(bt_cells) == norm(want), 'BBS terminal: the screen is not the grid'
        assert bt.y <= rows - 1
    # a refresh over the longest values (home, the art with each line's tail cleared, then the rest) = a fresh draw
    art = expand(data, SAMPLE).replace('\r\n', ESC + 'K\r\n')
    fw = FwTerm(cols, rows)
    fw.feed(expand(data, long_vals))
    fw.feed(ESC + '0m' + ESC + 'H' + re.sub('\x01K[A-Z]', '', art) + ESC + '0m' + ESC + 'J')
    assert norm(fw.cells) == norm(expected(g, SAMPLE)), 'a refresh left old values behind'
    # the CYD's taps: every key's area is its label, and no node field is under a key's area
    fw = FwTerm(cols, rows)
    fw.feed(expand(data, SAMPLE))
    assert sorted(code for _, _, code in fw.spots) == sorted(KEYS)
    labels = {}
    node_cells = set()
    for (r, c), (tok, w) in g.tokens.items():
        if tok[1] == 'N':
            node_cells |= {(r, c + i) for i in range(w)}
    for r, c, code, span in tap_spans(fw):
        label = ''.join(fw.cells[r][x][0] for x in range(c, c + span))
        labels[code] = label
        want = (' ' if code == 'KO' else KEYS[code] + '] ') + dict(KEY_LIST)[code]
        assert label == want, (code, label, want)
        for x in range(c - 1, c + max(span, 2)):
            assert (r, x) not in node_cells, ('a key area over a node field', code)
        assert 0 < c < cols - 1, ('a key at the edge', code)
    assert nodes == sum(1 for (r, c), (tok, w) in g.tokens.items() if tok[1] == 'N')
    return labels


SAMPLE = {'BN': 'AXIS UNDERGROUND CNC', 'SN': 'Rev', 'MB': 'main board', 'VN': '0.51', 'CT': '12', 'PT': '4', 'UT': '2', 'LC': 'Saylor',
          'SD': '14.2G', 'RM': '143', 'IP': '192.168.1.164', 'UP': '3d 04:12', 'TM': '21:04', 'DT': '2026-11-08',
          'NA': '1 Rev  reading messages', 'NB': '2 Saylor  in a door: LORD', 'NC': '3 waiting', 'ND': '4 waiting',
          'EA': '21:03 Saylor logged on to node 2', 'EB': '20:58 upload: AXISFLSH.ZIP (1.9 MB)',
          'EC': '20:41 Rev posted in General', 'PG': 'Saylor is paging you: got a minute?'}

# ---------------------------------------------------------------------------------------------------------------- art parts

LETTERS = [[" #### ", "##  ##", "##  ##", "######", "##  ##", "##  ##", "##  ##"],
           ["##  ##", "##  ##", " #### ", "  ##  ", " #### ", "##  ##", "##  ##"],
           ["######", "  ##  ", "  ##  ", "  ##  ", "  ##  ", "  ##  ", "######"],
           [" #####", "##    ", "##    ", " #### ", "    ##", "    ##", "##### "]]


def logo(g, r, c, st):
    """The AXIS letters, 29x4 cells: each pixel row in its ramp colour, a cast shadow down and right, drips for some."""
    W, H = 29, 8
    px = [[0] * W for _ in range(H)]
    on = lambda x, y: 0 <= y < 7 and any(0 <= x - 7 * i < 6 and LETTERS[i][y][x - 7 * i] == '#' for i in range(4))
    for y in range(H):
        for x in range(W):
            if on(x, y):
                px[y][x] = st['ramp'][y]
            elif on(x - 1, y - 1):
                px[y][x] = st['shadow']
    for x, y in st.get('drips', []):
        px[y][x] = st['ramp'][6]
    for x, y, col in st.get('glints', []):
        if px[y][x] and px[y][x] != st['shadow']:
            px[y][x] = col
    rows = []
    for y in range(H):
        rows.append(''.join('.' if not v else chr(65 + v) for v in px[y]))
    g.pixels(r, c, rows, {chr(65 + v): v for v in range(1, 16)})


def frame_top(g, r, c, w, label, st):
    b = st['box']
    g.put(r, c, b[0], st['frame'])
    x = g.text(r, c + 1, b[1], st['frame'])
    x = g.text(r, x, ': ', st['dim'])
    x = g.text(r, x, label, st['hot'])
    x = g.text(r, x, ' :', st['dim'])
    g.text(r, x, b[1] * (c + w - 1 - x), st['frame'])
    g.put(r, c + w - 1, b[2], st['frame'])


def frame_sides(g, r, c, w, st):
    g.put(r, c, st['box'][3], st['side'])
    g.put(r, c + w - 1, st['box'][3], st['side'])


def frame_end(g, r, c, w, st):
    b = st['box']
    g.text(r, c, b[4] + b[1] * (w - 2) + b[5], st['frame'])


def rule(g, r, c, w, st, phase=0):
    pat = st['rule']
    for i in range(w):
        ch, fg, bg = pat[(i + phase) % len(pat)]
        g.put(r, c + i, ch, fg, bg)


def keycap(g, r, c, code, label, st):
    """[S]ettings style: '[S] settings' with the letter tappable; space is ' [space] logon' (the key is the blank before)."""
    if code == 'KO':
        g.key(r, c, code, st['hot'])
        g.put(r, c + 1, '[', st['dim'])
        x = g.text(r, c + 2, 'space', st['hot'])
        g.put(r, x, ']', st['dim'])
        return g.text(r, x + 1, label[7:], st['body'])
    g.put(r, c + 1, '[', st['dim'])
    g.key(r, c + 2, code, st['hot'])
    g.put(r, c + 3, ']', st['dim'])
    return g.text(r, c + 4, ' ' + label, st['body'])


# ---------------------------------------------------------------------------------------------------------------- layouts


def wfc80(st):
    g = Grid(80, 25)
    rule(g, 0, 0, 80, st)
    logo(g, 1, 1, st)
    g.pixels(1, 31, st['motif'], st['mpal'])
    g.text(1, 46, st['title80'][:34].center(34), st['hot'])
    g.field(2, 46, 'BN', 34, st['value'])
    x = g.text(3, 46, 'sysop ', st['body'])
    x = g.field(3, x, 'SN', 20, st['value'])
    x = g.text(3, x, ' v', st['body'])
    g.field(3, x, 'VN', 6, st['value'])
    x = g.field(4, 46, 'MB', 15, st['body'])
    x = g.field(4, x + 1, 'TM', 7, st['value'])
    g.field(4, x + 1, 'DT', 10, st['value'])
    rule(g, 5, 0, 80, st, 3)
    frame_top(g, 6, 0, 45, 'nodes', st)
    frame_top(g, 6, 45, 35, 'today', st)
    stats = [('calls', 'CT'), ('posts', 'PT'), ('uploads', 'UT'), ('last caller', 'LC'), ('sd free', 'SD'),
             ('ram free kb', 'RM'), ('ip address', 'IP'), ('uptime', 'UP')]
    for i in range(8):
        r = 7 + i
        frame_sides(g, r, 0, 45, st)
        g.field(r, 2, 'N' + 'ABCDEFGH'[i], 41, st['value'])
        frame_sides(g, r, 45, 35, st)
        g.text(r, 47, stats[i][0].ljust(12), st['body'])
        g.field(r, 59, stats[i][1], 19, st['value'])
    frame_end(g, 15, 0, 45, st)
    frame_end(g, 15, 45, 35, st)
    frame_top(g, 16, 0, 80, 'events', st)
    for i, c in enumerate('ABC'):
        g.put(17 + i, 2, st['bullet'], st['accent'])
        g.field(17 + i, 4, 'E' + c, 75, st['body'])
    x = g.text(20, 1, 'page ', st['hot'])
    g.field(20, x, 'PG', 72, st['value'])
    frame_top(g, 21, 0, 80, 'keys', st)
    for i, (code, label) in enumerate(KEY_LIST):
        keycap(g, 22 + i // 5, 1 + (i % 5) * 16, code, label, st)
    x = g.text(24, 1, 'A X I S   M I C R O   B B S', st['foot'])
    x = g.text(24, x + 3, st['name'], st['hot'])
    rule(g, 24, x + 2, 78 - x - 2 - 12, st, 1)
    g.text(24, 66, '  u.g. cnc  ', st['foot'])
    return g


def wfccyd(st):
    g = Grid(45, 24)
    L, R = 18, 27
    g.put(0, 0, '[', st['frame'])
    x = g.text(0, 1, st['name'], st['hot'])
    x = g.text(0, x, ' ; ', st['dim'])
    g.field(0, x, 'BN', 44 - x, st['value'])
    g.put(0, 44, ']', st['frame'])
    logo(g, 1, 1, st)
    g.pixels(1, 31, st['motif'], st['mpal'])
    g.put(5, 0, '[', st['frame'])
    x = g.field(5, 1, 'TM', 7, st['value'])
    x = g.field(5, x + 1, 'DT', 12, st['value'])
    x = g.text(5, x, ' ip ', st['hot'])
    g.field(5, x, 'IP', 44 - x, st['value'])
    g.put(5, 44, ']', st['frame'])
    frame_top(g, 6, 0, L, 'keys', st)
    for i, (code, label) in enumerate(KEY_LIST):
        frame_sides(g, 7 + i, 0, L, st)
        keycap(g, 7 + i, 1, code, label, st)
    frame_end(g, 17, 0, L, st)
    frame_top(g, 6, L, R, 'nodes', st)
    for i in range(4):
        frame_sides(g, 7 + i, L, R, st)
        g.field(7 + i, L + 2, 'N' + 'ABCD'[i], 23, st['value'])
    frame_end(g, 11, L, R, st)
    frame_top(g, 12, L, R, 'today', st)
    for r in range(13, 17):
        frame_sides(g, r, L, R, st)
    x = g.text(13, L + 2, 'calls ', st['body'])
    x = g.field(13, x, 'CT', 5, st['value'])
    x = g.text(13, x, ' posts ', st['body'])
    g.field(13, x, 'PT', 5, st['value'])
    x = g.text(14, L + 2, 'ups   ', st['body'])
    x = g.field(14, x, 'UT', 5, st['value'])
    x = g.text(14, x, ' kb ram ', st['body'])
    g.field(14, x, 'RM', 5, st['value'])
    x = g.text(15, L + 2, 'last  ', st['body'])
    g.field(15, x, 'LC', 17, st['value'])
    x = g.text(16, L + 2, 'sd ', st['body'])
    x = g.field(16, x, 'SD', 6, st['value'])
    x = g.text(16, x, ' up ', st['body'])
    g.field(16, x, 'UP', 10, st['value'])
    frame_end(g, 17, L, R, st)
    x = g.text(18, 1, 'page ', st['hot'])
    g.field(18, x, 'PG', 44 - x, st['value'])
    frame_top(g, 19, 0, 45, 'events', st)
    for i, c in enumerate('ABC'):
        g.put(20 + i, 1, st['bullet'], st['accent'])
        g.field(20 + i, 3, 'E' + c, 42, st['body'])
    x = g.text(23, 1, 'sysop ', st['foot'])
    x = g.field(23, x, 'SN', 14, st['value'])
    x = g.field(23, x + 1, 'MB', 14, st['body'])
    x = g.field(23, x + 1, 'VN', 5, st['value'])
    rule(g, 23, x + 1, 44 - x - 1, st, 2)
    return g


# ---------------------------------------------------------------------------------------------------------------- the looks
# colours are DOS ones: 0 black 1 blue 2 green 3 cyan 4 red 5 magenta 6 brown 7 grey, +8 bright (8 charcoal, 14 yellow ...)
SINGLE = '┌─┐│└┘'
DOUBLE = '╔═╗║╚╝'
MIXED = '╒═╕│╘╛'
UPPER, LOWER, FULL, L1, L2, L3, SQ, DOT = '▀', '▄', '█', '░', '▒', '▓', '■', '·'


def axis():
    """The house look (Rev's default): yellow to brown letters with drips and an end mill, as the built-in WFC screens."""
    return dict(name='axis', title80='waiting for caller', box=MIXED, frame=12,
                side=8, hot=14, body=7, value=15, dim=8, accent=6, foot=6, bullet='»',
                ramp=[15, 14, 14, 6, 6, 6, 6], shadow=8, drips=[(2, 7), (16, 7), (26, 7)],
                rule=[(UPPER, 6, 0), (UPPER, 6, 0), (FULL, 6, 0), (UPPER, 6, 0), (LOWER, 14, 0), (UPPER, 6, 0), (' ', 7, 0),
                      (UPPER, 6, 0)],
                motif=["....YYYYYY....",
                       "....WYYYYO....",
                       "....OOOOOO....",
                       ".....GGGG.....",
                       ".....YOYO.....",
                       ".....OYOY.....",
                       "......YO......",
                       ".......O......"],
                mpal={'Y': 14, 'O': 6, 'W': 15, 'G': 7})


def crt():
    """crt-hardcore: green and silver glitch (VGA 0/2/10/8/7/15), shard shades, a green skull."""
    return dict(name='crt', title80='>> waiting for caller_', box=SINGLE, frame=2, side=8, hot=10, body=7, value=15,
                dim=8, accent=10, foot=2, bullet='>',
                ramp=[15, 10, 10, 7, 2, 2, 8], shadow=8, glints=[(1, 3, 15), (15, 1, 7), (22, 4, 15)],
                rule=[(L1, 2, 0), (L2, 2, 0), (L3, 10, 0), (FULL, 2, 0), (L3, 8, 0), (' ', 7, 0), (L1, 8, 0),
                      (LOWER, 10, 0), (L2, 7, 0), (' ', 7, 0), (' ', 7, 0), (UPPER, 2, 0)],
                motif=["...WWWWWWWW...",
                       "..WLLLLLLLLWG.",
                       ".GLkkLLLLkkLG.",
                       ".GLkkLLLLkkLG.",
                       ".GLLLLkkLLLLG.",
                       "..GLLLLLLLLG..",
                       "...LkLkLkLkG..",
                       "....GGGGGGG..."],
                mpal={'W': 15, 'L': 10, 'G': 8, 'k': 0, 'D': 2})


def haunted():
    """haunted-crt: green phosphor scanlines in a grey bezel, a phantom face with black voids (VGA 0/2/10/8/7/15)."""
    return dict(name='haunted', title80='~ waiting for caller ~', box=DOUBLE, frame=7, side=8, hot=10, body=7, value=15,
                dim=8, accent=2, foot=8, bullet=SQ,
                ramp=[15, 10, 2, 10, 2, 10, 2], shadow=8,
                rule=[(UPPER, 10, 2), (UPPER, 2, 0), (UPPER, 10, 2), (UPPER, 2, 0), (UPPER, 2, 0), (UPPER, 10, 2)],
                motif=["GWWWWWWWWWWWG.",
                       "GLLLLLLLLLLLG.",
                       "GDkkDDDDDkkDG.",
                       "GLkkLLLLLkkLG.",
                       "GDDDDDkDDDDDG.",
                       "GLLkkkkkkkLLG.",
                       "GGGGGGGGGGGGGR",
                       "....GGGGG....."],
                mpal={'G': 7, 'W': 15, 'L': 10, 'D': 2, 'k': 0, 'R': 10})


def sea():
    """deep-sea: navy haze, teal and cyan light, silver and bone (VGA 0/1/9/3/11/8/7/15), an anglerfish with its lure."""
    return dict(name='sea', title80='. o  waiting for caller  o .', box=SINGLE, frame=1, side=1, hot=11, body=7,
                value=15, dim=8, accent=3, foot=9, bullet='°',
                ramp=[15, 11, 7, 9, 7, 1, 1], shadow=8,
                rule=[('≈', 3, 0), ('≈', 9, 0), (' ', 7, 0), ('≈', 1, 0), ('o', 3, 0), (' ', 7, 0),
                      ('≈', 11, 0), ('≈', 3, 0), (DOT, 9, 0), (' ', 7, 0)],
                motif=["........WC....",
                       ".......W.CCC..",
                       "..SSSSSW..C...",
                       ".SSSCSSSS.....",
                       "SSSSSSSSSSS.T.",
                       "kWkWkWSSSSSSS.",
                       ".SSSSSSSSS..T.",
                       "..T...T.......",],
                mpal={'S': 7, 'W': 15, 'C': 11, 'T': 3, 'k': 0})


def cathedral():
    """occult-cathedral: purple, magenta, charcoal, grey and bone (VGA 0/5/13/8/7/15), a stained-glass lancet."""
    return dict(name='cathedral', title80='+ waiting for caller +', box=DOUBLE, frame=5, side=8, hot=13, body=7,
                value=15, dim=8, accent=5, foot=8, bullet='┼',
                ramp=[15, 13, 7, 13, 5, 8, 5], shadow=8,
                rule=[(UPPER, 5, 0), (LOWER, 8, 0), (UPPER, 13, 0), (LOWER, 5, 0), (UPPER, 8, 0), (LOWER, 5, 0)],
                motif=["......GG......",
                       ".....GMMG.....",
                       "....GMPWPG....",
                       "...GMPMPMPG...",
                       "...GPMWMPMG...",
                       "...GMPMPMPG...",
                       ".W.GPMPMPMG.W.",
                       ".P.GGGGGGGG.P."],
                mpal={'G': 7, 'M': 13, 'P': 5, 'W': 15})


def skulls():
    """skulls: black, dark red, red, grey, charcoal and bone (VGA 0/4/12/7/8/15), a bone skull with a red rim light."""
    return dict(name='skulls', title80='x waiting for caller x', box=SINGLE, frame=4, side=8, hot=12, body=7, value=15,
                dim=8, accent=4, foot=8, bullet='x',
                ramp=[15, 15, 7, 12, 7, 4, 8], shadow=4, glints=[(8, 3, 12), (23, 2, 12)],
                rule=[(UPPER, 4, 0), (LOWER, 8, 0), (UPPER, 8, 0), (LOWER, 4, 0), (SQ, 12, 0), (LOWER, 8, 0)],
                motif=["...WWWWWWWW...",
                       "..WWWWWWWWWWG.",
                       ".RWkkWWWWkkWG.",
                       ".RWkRWWWWkRWG.",
                       ".RWWWWkkWWWWG.",
                       "..RWWWWWWWWG..",
                       "...WkWkWkWkG..",
                       "....GGGGGGG..."],
                mpal={'W': 15, 'G': 7, 'R': 4, 'k': 0})


def underground():
    """underground: blue, cyan, white, magenta and yellow bars on black, pipes and a gear (the underground pack)."""
    return dict(name='underground', title80='= waiting for caller =', box=MIXED, frame=9, side=5, hot=11, body=7,
                value=15, dim=8, accent=13, foot=5, bullet='»',
                ramp=[11, 15, 13, 5, 9, 1, 5], shadow=8,
                rule=[(FULL, 1, 0), (FULL, 9, 0), (FULL, 11, 0), (FULL, 15, 0), (FULL, 14, 0), (FULL, 15, 0),
                      (FULL, 11, 0), (FULL, 9, 0), (FULL, 1, 0), (FULL, 5, 0), (FULL, 13, 0), (FULL, 5, 0)],
                motif=["M..G..GG..G...",
                       "M.GGGGGGGGGG..",
                       "MGGGGkkkkGGGG.",
                       "M.GGkkCCkkGG..",
                       "M.GGkkCCkkGG..",
                       "MGGGGkkkkGGGG.",
                       "M.GGGGGGGGGG..",
                       "M..G..GG..G..."],
                mpal={'M': 13, 'G': 7, 'k': 0, 'C': 11})


def wasteland():
    """wasteland: black, brown, amber, charcoal, grey and bone (VGA 0/6/14/8/7/15), hazard stripes, a salvage crawler."""
    return dict(name='wasteland', title80='/// waiting for caller ///', box=MIXED, frame=6, side=8, hot=14, body=7,
                value=15, dim=8, accent=6, foot=6, bullet='/',
                ramp=[15, 14, 7, 6, 14, 6, 8], shadow=8,
                rule=[(UPPER, 14, 6), (UPPER, 14, 6), (UPPER, 14, 6), (UPPER, 0, 6), (UPPER, 0, 6)],
                motif=["....GGG.....B.",
                       "...GkYG....B..",
                       "GGGGGGGGGGBB..",
                       "GOOOOOOOOOOG..",
                       "YkYkYkYkYkYk..",
                       "kGGGGGGGGGGk..",
                       "GkGkGkGkGkGk..",
                       ".GGGGGGGGGG..."],
                mpal={'G': 7, 'k': 0, 'Y': 14, 'O': 6, 'B': 8})


LOOKS = {'axis': axis, 'crt': crt, 'haunted': haunted, 'sea': sea, 'cathedral': cathedral, 'skulls': skulls,
         'underground': underground, 'wasteland': wasteland}


# ---------------------------------------------------------------------------------------------------------------- previews


def preview(data, cols, rows, title):
    t = bbs_term(cols, rows)
    t.feed(re.sub('\x01K[A-Z]', '', expand(data, SAMPLE)))
    t.x = cols  # no cursor in the picture
    return t.image(title)


def sheet(images, path, label):
    from PIL import Image, ImageDraw, ImageFont
    w, h = images[0].size
    pad = 8
    out = Image.new('RGB', (4 * w + 5 * pad, 2 * h + 3 * pad + 24), (12, 12, 12))
    ImageDraw.Draw(out).text((pad, 6), label, font=ImageFont.truetype('consola.ttf', 16), fill=(220, 220, 220))
    for i, im in enumerate(images):
        out.paste(im, (pad + (i % 4) * (w + pad), 24 + pad + (i // 4) * (h + pad)))
    out.save(path)


def main(outdir, previewdir=None):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'screenshots'))
    shots = {80: [], 45: []}
    for look in LOOK_ORDER:
        st = LOOKS[look]()
        os.makedirs(os.path.join(outdir, look), exist_ok=True)
        for size, build, name, nodes in ((80, wfc80, 'WFC.ANS', 8), (45, wfccyd, 'WFCCYD.ANS', 4)):
            g = build(st)
            data = serialize(g)
            labels = check(g, data, size, nodes)
            path = os.path.join(outdir, look, name)
            open(path, 'wb').write(data)
            print(f'{look:12} {name:11} {g.cols}x{g.rows} {len(data):5} bytes  ok  keys: ' +
                  ' '.join(f'{k}="{v.strip()}"' for k, v in sorted(labels.items())) if size == 45 else
                  f'{look:12} {name:11} {g.cols}x{g.rows} {len(data):5} bytes  ok')
            if previewdir:
                im = preview(data, g.cols, g.rows, f'{look}  {name}  {g.cols}x{g.rows}  {len(data)} bytes')
                im.save(os.path.join(previewdir, f'{look}-{name.split(".")[0].lower()}.png'))
                shots[size].append(im)
    if previewdir:
        sheet(shots[80], os.path.join(previewdir, 'contact-sheet-wfc80.png'), 'WFC.ANS, 80x25, all eight looks (sample values)')
        sheet(shots[45], os.path.join(previewdir, 'contact-sheet-wfccyd.png'), 'WFCCYD.ANS, 45x24, all eight looks (sample values)')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    if len(sys.argv) > 2:
        os.makedirs(sys.argv[2], exist_ok=True)
    main(*sys.argv[1:])
