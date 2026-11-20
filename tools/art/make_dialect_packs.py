# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Two original look packs that draw in the dialects the theme engine actually reads.

    python3 tools/art/make_dialect_packs.py

Vision Gate is dialect=vision3 (ViSiON/3 |C1 to |C7, ||, |B, |XY, |CL).
Outpost is dialect=renegade (Renegade ^0 to ^9, |16 to |31, |CS, ||, %TL, %ND).

The art is drawn here. Nothing is copied from a real board. Each zip in art/packs
holds numbered ans/ screens (the same installer map as the other looks) and an
axis/ folder of firmware names, list templates, THEME.CFG and .LITE.ANS screens.
A lite screen stays at or under 700 bytes.
"""
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
LITE_CAP = 700
THEME_CAP = 12288
HINT = 'arrows move, Enter picks, or press the key'
WIN = (3, 8, 43, 20)  # columns 4 to 44, rows 9 to 21 (docs/MENU-ART.md)

# Visible width of a placeholder when the screen is laid out. The source keeps the code.
PLACE = {
    'NODE': 2, 'UH': 8, 'TIME': 5, 'DATE': 8, 'CALLS': 2, 'LEVEL': 2, 'BN': 12,
    'TL': 2, 'CAN': 0, 'CFAN': 0, 'LCALL': 8, 'MN': 4, 'NAME': 8,
}
PCT = {
    'BN': 12, 'NN': 2, 'ND': 2, 'TL': 2, 'SN': 6, 'UN': 8, 'MA': 0, 'FA': 0,
    'DT': 8, 'TM': 5, 'VN': 4,
}

# 5 pixel rows. Half-block pairs make the goodbye title.
FONT = {
    'G': [' ###', '#   ', '# ##', '#  #', ' ###'],
    'O': [' ## ', '#  #', '#  #', '#  #', ' ## '],
    'D': ['### ', '#  #', '#  #', '#  #', '### '],
    'B': ['### ', '#  #', '### ', '#  #', '### '],
    'Y': ['# #', '# #', ' # ', ' # ', ' # '],
    'E': ['####', '#   ', '### ', '#   ', '####'],
    'V': ['#  #', '#  #', '#  #', ' ## ', ' ## '],
    'I': ['###', ' # ', ' # ', ' # ', '###'],
    'S': [' ###', '#   ', ' ## ', '   #', '### '],
    'N': ['#  #', '## #', '# ##', '#  #', '#  #'],
    'U': ['#  #', '#  #', '#  #', '#  #', ' ## '],
    'T': ['###', ' # ', ' # ', ' # ', ' # '],
    'P': ['### ', '#  #', '### ', '#   ', '#   '],
    'R': ['### ', '#  #', '### ', '# # ', '#  #'],
    'A': [' ## ', '#  #', '####', '#  #', '#  #'],
    'M': ['#  #', '####', '#  #', '#  #', '#  #'],
    'L': ['#  ', '#  ', '#  ', '#  ', '###'],
    'C': [' ##', '#  ', '#  ', '#  ', ' ##'],
}


def walk(text):
    """Visible characters of a pipe-code run. Codes the dialect eats take no column."""
    i = 0
    n = len(text)
    while i < n:
        if text.startswith('||', i):
            yield '|'
            i += 2
            continue
        if text.startswith('|{P}', i) or text.startswith('|{O}', i):
            for ch in text[i:i + 4]:
                yield ch
            i += 4
            continue
        if text[i] == '|' and i + 1 < n:
            if text[i + 1] == 'C' and i + 2 < n and text[i + 2] in '1234567':
                i += 3
                continue
            if text[i + 1] == 'B' and i + 2 < n and text[i + 2].isdigit():
                i += 3
                if i < n and text[i].isdigit():
                    i += 1
                continue
            if i + 2 < n and text[i + 1].isdigit() and text[i + 2].isdigit():
                i += 3
                continue
            if text.startswith(('|DE', '|CL', '|CS', '|CR', '|PP'), i):
                i += 3
                continue
            if text.startswith('|XY', i) and i + 8 <= n:
                i += 8
                continue
            j = i + 1
            while j < n and text[j].isupper():
                j += 1
            if j >= i + 2:
                width = PLACE.get(text[i + 1:j], 4)
                for _ in range(width):
                    yield 'X'
                i = j
                continue
        if text[i] == '%' and i + 2 < n and text[i + 1].isupper() and text[i + 2].isupper():
            width = PCT.get(text[i + 1:i + 3], 4)
            for _ in range(width):
                yield 'X'
            i += 3
            continue
        if text[i] == '^' and i + 1 < n and text[i + 1].isdigit():
            i += 2
            continue
        yield text[i]
        i += 1


def vis(text):
    return sum(1 for _ in walk(text))


def pad(left, right, width):
    gap = width - vis(left) - vis(right)
    if gap < 1:
        raise SystemExit('bar does not fit: %r %r' % (left, right))
    return left + (' ' * gap) + right


def paint(runs):
    """Occupy an 80x25 grid. A second glyph on one cell is a layout error."""
    grid = [[None] * 80 for _ in range(25)]
    for r, c, _color, text in runs:
        if not 0 <= r < 25 or not 0 <= c < 80:
            raise SystemExit('off the screen at %d,%d' % (c + 1, r + 1))
        col = c
        for ch in walk(text):
            if col >= 80:
                raise SystemExit('row %d runs past column 80' % (r + 1))
            if grid[r][col] not in (None, ' ') and ch != ' ':
                raise SystemExit('overlap at %d,%d' % (col + 1, r + 1))
            if grid[r][col] is None:
                grid[r][col] = ch
            col += 1
    return grid


def emit(clear, runs):
    parts = [clear]
    for r, c, color, text in sorted(runs, key=lambda item: (item[0], item[1])):
        parts.append('|XY%02d,%02d' % (c + 1, r + 1))
        if color:
            parts.append(color)
        parts.append(text)
    return ''.join(parts).encode('cp437')


def window_clear(grid):
    x0, y0, x1, y1 = WIN
    for r in range(y0, y1 + 1):
        for c in range(x0, x1 + 1):
            if grid[r][c] not in (None, ' '):
                return 'menu window painted at %d,%d (%r)' % (c + 1, r + 1, grid[r][c])
    return ''


def row_text(grid, r):
    return ''.join(ch or ' ' for ch in grid[r])


def block_runs(word, r0, c0, colors):
    """Half-block letters. colors: top pair, lower pair, the baseline."""
    rows = ['' for _ in range(5)]
    for ch in word:
        glyph = FONT[ch]
        wide = max(len(line) for line in glyph)
        for y in range(5):
            rows[y] += glyph[y].ljust(wide) + ' '
    if rows[0].endswith(' '):
        rows = [row[:-1] for row in rows]
    width = len(rows[0])
    runs = []
    for y0, color in ((0, colors[0]), (2, colors[1])):
        line = []
        for x in range(width):
            top = rows[y0][x] == '#'
            bot = rows[y0 + 1][x] == '#'
            if top and bot:
                line.append('█')
            elif top:
                line.append('▀')
            elif bot:
                line.append('▄')
            else:
                line.append(' ')
        runs.append((r0 + y0 // 2, c0, color, ''.join(line).rstrip(' ')))
    base = []
    drips = []
    for x in range(width):
        if rows[4][x] == '#':
            base.append('▀')
            drips.append('│' if x % 7 == 3 else ' ')
        else:
            base.append(' ')
            drips.append(' ')
    runs.append((r0 + 2, c0, colors[2], ''.join(base).rstrip(' ')))
    drip = ''.join(drips).rstrip(' ')
    if drip.strip():
        runs.append((r0 + 3, c0, colors[2], drip))
    return runs


class Look:
    def __init__(self, id_, zipname, ans, clear, mark, farewell, slots):
        self.id = id_
        self.zipname = zipname
        self.ans = ans
        self.clear = clear
        self.mark = mark
        self.farewell = farewell
        self.slots = slots  # THEME.CFG body, dialect already chosen


VISION = Look(
    'vision', 'axis-vision-gate-ansi-2026-11-20.zip', 'vision', '|CL', 'VISION GATE',
    'The gate stays open.',
    'dialect=vision3\nc1=11\nc2=15\nc3=3\nc4=14\nc5=8\nc6=7\nc7=31\n',
)
OUTPOST = Look(
    'outpost', 'axis-outpost-rampart-ansi-2026-11-20.zip', 'outpost', '|CS', 'OUTPOST',
    'The rampart keeps watch.',
    'dialect=renegade\nr0=7\nr1=11\nr2=14\nr3=15\nr4=9\nr5=12\nr6=4\nr7=10\nr8=8\nr9=3\n',
)

# num, firmware name or None, slug, kind, title, blurb
SCREENS = [
    ('01', 'PRELOGON.ANS', 'prelogon', 'note', 'WELCOME', 'Carrier up. The board is open.'),
    ('02', None, 'banner', 'banner', 'BANNER', ''),
    ('03', 'BULLETIN.ANS', 'bulletin', 'note', 'BULLETIN', 'Board news for this call.'),
    ('04', 'MAIN.ANS', 'main', 'menu', 'MAIN MENU', 'Pick a door in the gate.'),
    ('05', 'LOGIN.ANS', 'login', 'login', 'LOGIN', 'Name yourself to the gate.'),
    ('06', 'NEWUSER.ANS', 'newuser', 'note', 'NEW USER', 'A new account waits on the sysop.'),
    ('07', 'DOORS.ANS', 'doors', 'menu', 'DOORS', 'Programs that run outside.'),
    ('08', 'MSGS.ANS', 'messages', 'menu', 'MESSAGES', 'The message bases.'),
    ('09', 'FILES.ANS', 'files', 'menu', 'FILES', 'The file bases.'),
    ('10', 'MATRIX.ANS', 'matrix', 'menu', 'MATRIX', 'Login, apply, or leave.'),
    ('11', 'FASTLOGN.ANS', 'fast', 'menu', 'FAST LOGIN', 'A short way back in.'),
    ('12', 'SHOP.ANS', 'shop', 'note', 'THE SHOP', 'The shop is open.'),
    ('13', 'MILL.ANS', 'mill', 'note', 'THE MILL', 'The mill is running.'),
]

NOTES = {
    'prelogon': ('Press a key.', 'The line is yours.'),
    'bulletin': ('Tools stay sharp.', 'Leave the bench clean.'),
    'newuser': ('Tell the board who you are.', 'The sysop reads it next.'),
    'shop': ('Cutters, stock and fixtures.', 'Ask before you borrow one.'),
    'mill': ('Mind the chips.', 'The spindle is live.'),
}
WORDS = {
    'main': 'OPEN', 'doors': 'DOORS', 'messages': 'NOTES', 'files': 'FILES',
    'matrix': 'ENTER', 'fast': 'FAST', 'login': 'NAME', 'prelogon': 'HELLO',
    'bulletin': 'NEWS', 'newuser': 'APPLY', 'shop': 'SHOP', 'mill': 'MILL',
}


def frame(runs, color, kind):
    runs.append((7, 0, color, '╔' + '═' * 76 + '╗'))
    for r in range(8, 21):
        runs.append((r, 0, color, '║'))
        runs.append((r, 1, color, '║'))
        runs.append((r, 44, color, '│'))
        runs.append((r, 76, color, '║'))
        runs.append((r, 77, color, '║'))
    if kind == 'menu':
        runs.append((21, 3, color, HINT))
    runs.append((22, 0, color, '╚' + '═' * 76 + '╝'))


def side_panel(look, slug, r0=8):
    """Original gate or rampart, in the right margin, clear of the menu window."""
    word = WORDS.get(slug, 'AXIS')
    runs = []
    c0 = 47
    if look.id == 'vision':
        runs.append((r0, c0, '|C4', '▄' * 22))
        for i in range(5):
            rr = r0 + 1 + i
            runs.append((rr, c0, '|C7', '██'))
            runs.append((rr, c0 + 20, '|C7', '██'))
            if i == 2:
                runs.append((rr, c0 + 4, '|C2', word.center(14)))
        runs.append((r0 + 6, c0, '|C3', '▀' * 22))
        runs.append((r0 + 8, c0, '|C4', 'north || line'))
        runs.append((r0 + 10, c0, '|C5', '|TIME'))
        runs.append((r0 + 11, c0, '|C6', '|DATE'))
    else:
        runs.append((r0, c0, '^2', '▄▄  ▄▄  ▄▄  ▄▄  ▄▄  ▄▄'))
        runs.append((r0 + 1, c0, '^5', '█' * 22))
        for i in range(3):
            rr = r0 + 2 + i
            runs.append((rr, c0, '|17|15', '██'))
            runs.append((rr, c0 + 20, '|17|15', '██'))
            if i == 1:
                runs.append((rr, c0 + 3, '^3', word.center(16)))
        runs.append((r0 + 5, c0, '^6', '█' * 22))
        runs.append((r0 + 6, c0, '|22|14', '▀' * 22))
        runs.append((r0 + 8, c0, '^8', 'rampart || %TL min'))
        runs.append((r0 + 10, c0, '^8', '|TIME'))
        runs.append((r0 + 11, c0, '^0', '|DATE'))
    return runs


def chrome(look, title, blurb, kind):
    runs = []
    if look.id == 'vision':
        runs.append((0, 0, '|C7', pad(' VISION GATE', ' |NODE ', 78)))
        runs.append((1, 2, '|C1', 'handle |UH'))
        runs.append((1, 40, '|C4', '|TIME   |DATE'))
        runs.append((2, 2, '|C5', 'calls |CALLS    level |LEVEL'))
        runs.append((3, 2, '|C2', title))
        runs.append((4, 2, '|C6', blurb))
        runs.append((5, 1, '|C1', '%BN  node %NN|DE'))
        runs.append((6, 2, '|15|B1', '────────'))
        frame(runs, '|C3', kind)
        runs.append((23, 1, '|C2', 'AXIS MICRO BBS'))
        runs.append((23, 20, '|C4', 'north || line'))
    else:
        runs.append((0, 0, '|17|15', pad(' OUTPOST', ' %TL ', 78)))
        runs.append((1, 2, '^1', 'node %ND'))
        runs.append((1, 22, '^3', 'sysop %SN'))
        runs.append((1, 70, '|15|25', '*'))
        runs.append((2, 2, '^8', 'calls |CALLS    level |LEVEL'))
        runs.append((3, 2, '^2', title))
        runs.append((4, 2, '^0', blurb))
        runs.append((5, 1, '^1', '%BN  node %NN|DE'))
        runs.append((6, 2, '^4', '────────'))
        frame(runs, '^9', kind)
        runs.append((23, 1, '^3', 'AXIS MICRO BBS'))
        runs.append((23, 20, '^2', 'outpost || renegade'))
        runs.append((24, 0, '|23|00', pad(' rampart', ' %ND ', 78)))
    return runs


def login_fields(look):
    color = '|C1' if look.id == 'vision' else '^3'
    label = '|C2' if look.id == 'vision' else '^1'
    return [
        (11, 4, label, 'Handle'),
        (11, 16, color, '|{P}' + ' ' * 16),
        (13, 4, label, 'Password'),
        (13, 16, color, '|{O}' + ' ' * 16),
        (16, 4, color, 'last call |LCALL'),
    ]


def note_lines(look, slug):
    color = '|C1' if look.id == 'vision' else '^3'
    accent = '|C4' if look.id == 'vision' else '^2'
    lines = NOTES.get(slug, ())
    runs = []
    for i, line in enumerate(lines):
        runs.append((11 + i * 2, 6, accent if i == 0 else color, line))
    if look.id == 'outpost' and slug == 'messages':
        runs.append((16, 6, '^8', 'area %MA'))
    if look.id == 'outpost' and slug == 'files':
        runs.append((16, 6, '^8', 'area %FA'))
    if look.id == 'vision' and slug == 'messages':
        runs.append((16, 6, '|C5', 'base |CAN'))
    if look.id == 'vision' and slug == 'files':
        runs.append((16, 6, '|C5', 'base |CFAN'))
    return runs


def full_runs(look, slug, kind, title, blurb):
    if kind == 'banner':
        word = 'VISION' if look.id == 'vision' else 'OUTPOST'
        colors = ('|C2', '|C7', '|C4') if look.id == 'vision' else ('^3', '^1', '^2')
        runs = block_runs(word, 2, 4, colors)
        tag = '|C4' if look.id == 'vision' else '^2'
        line = 'north || line' if look.id == 'vision' else 'rampart || renegade'
        sub = '|C1' if look.id == 'vision' else '^8'
        runs.append((8, 4, tag, line))
        runs.append((10, 4, sub, '%BN  node %NN'))
        return runs
    if look.id == 'outpost':
        blurb = {
            'main': 'Pick a door on the rampart.',
            'login': 'Name yourself to the board.',
        }.get(slug, blurb)
    runs = chrome(look, title, blurb, kind)
    if kind == 'login':
        runs += login_fields(look)
    elif kind == 'note':
        runs += note_lines(look, slug)
    if kind in ('menu', 'login', 'note'):
        runs += side_panel(look, slug)
    return runs


def goodbye_runs(look):
    colors = ('|C2', '|C7', '|C4') if look.id == 'vision' else ('^3', '^1', '^2')
    runs = block_runs('GOODBYE', 1, 2, colors)
    if look.id == 'vision':
        runs.append((5, 1, '|C1', '%BN  node %NN|DE'))
        runs.append((8, 4, '|C3', 'The gate closes behind you.'))
        runs.append((10, 4, '|C2', 'Thanks for calling.'))
        runs.append((12, 4, '|C4', look.farewell))
        runs.append((14, 4, '|C1', 'Call back soon.'))
        runs.append((18, 4, '|C6', 'handle |UH    calls |CALLS'))
        runs.append((20, 2, '|C4', '▄' * 48))
        runs.append((21, 2, '|C2', '─' * 60))
        runs.append((22, 2, '|C1', '▀' * 48))
        runs.append((23, 1, '|C2', 'AXIS MICRO BBS'))
        runs.append((23, 20, '|C4', 'north || line'))
    else:
        runs.append((5, 1, '^1', '%BN  node %NN|DE'))
        runs.append((6, 40, '|15|25', '*'))
        runs.append((8, 4, '^9', 'The rampart keeps your name.'))
        runs.append((10, 4, '^3', 'Thanks for calling.'))
        runs.append((12, 4, '^2', look.farewell))
        runs.append((14, 4, '^1', 'Call back soon.'))
        runs.append((16, 4, '^1', 'time left %TL    node %ND'))
        runs.append((20, 2, '^2', '▄▀' * 24))
        runs.append((21, 2, '^5', '─' * 56))
        runs.append((22, 2, '^1', '▀▄' * 24))
        runs.append((23, 1, '^3', 'AXIS MICRO BBS'))
        runs.append((23, 20, '^2', 'outpost || renegade'))
        runs.append((24, 40, '|23|00', ' rampart'))
    return runs


def lite_runs(look, slug, kind, title):
    """A short screen for a slow radio caller. Same window and the same marks."""
    runs = []
    if look.id == 'vision':
        runs.append((0, 0, '|C7', 'VISION'))
        runs.append((0, 16, '|C2', '|NODE'))
        runs.append((1, 0, '|C1', look.mark))
        runs.append((3, 1, '|C2', title))
        runs.append((5, 1, '|C6', '%BN  node %NN'))
        runs.append((6, 1, '|15|B1', '------'))
        runs.append((8, 50, '|C4', '▄▄▄▄▄▄'))
        runs.append((9, 50, '|C7', '█    █'))
        runs.append((10, 50, '|C2', '█ || █'))
        runs.append((11, 50, '|C7', '█    █'))
        runs.append((12, 50, '|C3', '▀▀▀▀▀▀'))
        runs.append((23, 1, '|C2', 'AXIS MICRO BBS'))
    else:
        runs.append((0, 0, '|17|15', 'OUTPOST'))
        runs.append((0, 16, '^2', '%TL'))
        runs.append((1, 0, '^1', 'node %ND'))
        runs.append((1, 16, '|15|25', '*'))
        runs.append((3, 1, '^2', title))
        runs.append((5, 1, '^0', '%BN  node %NN'))
        runs.append((6, 1, '^4', '------'))
        runs.append((8, 50, '^2', '▄▄ ▄▄ ▄▄'))
        runs.append((9, 50, '|17|15', '████████'))
        runs.append((10, 50, '^3', '█ ||   █'))
        runs.append((11, 50, '^6', '▀▀▀▀▀▀▀▀'))
        runs.append((23, 1, '^3', 'AXIS MICRO BBS'))
        runs.append((24, 0, '|23|00', ' rampart'))
    if kind == 'menu':
        color = '|C5' if look.id == 'vision' else '^8'
        runs.append((21, 3, color, HINT))
    elif kind == 'login':
        color = '|C1' if look.id == 'vision' else '^3'
        label = '|C2' if look.id == 'vision' else '^1'
        runs += [
            (11, 4, label, 'Handle'),
            (11, 16, color, '|{P}' + ' ' * 16),
            (13, 4, label, 'Password'),
            (13, 16, color, '|{O}' + ' ' * 16),
        ]
    elif kind == 'bye':
        body = '|C1' if look.id == 'vision' else '^3'
        hot = '|C4' if look.id == 'vision' else '^2'
        runs += [
            (11, 3, body, 'Thanks for calling.'),
            (12, 3, hot, look.farewell),
            (14, 3, body, 'Call back soon.'),
        ]
    else:
        body = '|C1' if look.id == 'vision' else '^3'
        line = NOTES.get(slug, ('Board news.',))[0]
        runs.append((11, 3, body, line))
    return runs


def files_lst(color, hi, reg):
    return (
        '; File list. Same columns as the built-in list. docs/MENU-ART.md\n'
        'geom=3,6,74,14\n'
        'colours=%d,%d\n'
        'caption=%sName          Size   About                         |PG\n'
        'rule=1\n'
        'row=@FN12@ @SZ>7@  |AB\n' % (hi, reg, color)
    ).encode('ascii')


def msgs_lst(color, hi, reg):
    return (
        '; Message list. Same columns as the built-in list. docs/MENU-ART.md\n'
        'geom=3,6,74,14\n'
        'colours=%d,%d\n'
        'caption=%s  #  From          To            Subject          |CT\n'
        'rule=1\n'
        'row=@NW1@@NM>4@ @FR12@ @TO12@ @SU24@\n' % (hi, reg, color)
    ).encode('ascii')


def users_lst(handle, loc, calls, tag, hi, reg):
    text = 'handle'.ljust(20) + ' ' + 'from'.ljust(19) + ' ' + 'calls' + '  ' + 'tagline'
    return (
        '; User list. Tokens and widths: docs/MENU-ART.md\n'
        'geom=4,6,76,16\n'
        'colours=%d,%d\n'
        'caption=%s%s\n'
        'rule=1\n'
        'row=%s@HN20@ %s@LO19@ %s@CA>5@  %s@TG22@|TW\n'
        % (hi, reg, tag, text, handle, loc, calls, tag)
    ).encode('ascii')


def theme_cfg(look):
    if look.id == 'vision':
        head = '; Vision Gate draws with ViSiON/3 colour slots.\n'
    else:
        head = '; Outpost draws with Renegade prompt colours.\n'
    return (head + look.slots).encode('ascii')


def build_screen(look, slug, kind, title, blurb, lite):
    if slug == 'goodbye':
        runs = goodbye_runs(look) if not lite else lite_runs(look, slug, 'bye', 'LOGOFF')
    elif lite:
        runs = lite_runs(look, slug, kind, title)
    else:
        runs = full_runs(look, slug, kind, title, blurb)
    paint(runs)  # raises on overlap or a row past column 80
    return emit(look.clear, runs), runs


def check_screen(look, name, data, kind, lite):
    text = data.decode('cp437')
    cap = LITE_CAP if lite else THEME_CAP
    probs = []
    if len(data) > cap:
        probs.append('%d bytes, over %d' % (len(data), cap))
    if '%BN' not in text or '%NN' not in text:
        probs.append('wants %BN and %NN')
    if look.clear not in text:
        probs.append('missing ' + look.clear)
    if '||' not in text:
        probs.append('missing a literal bar')
    if kind in ('bye', 'goodbye') and 'Call back soon' not in text:
        probs.append('the logoff line is missing')
    if kind == 'menu' and HINT not in text:
        probs.append('the lightbar hint is missing')
    if kind == 'login' and ('|{P}' not in text or '|{O}' not in text):
        probs.append('login marks missing')
    if look.id == 'vision':
        for code in ('|C1', '|C7', '|XY'):
            if code not in text and kind != 'banner':
                probs.append('missing ' + code)
        if kind == 'banner' and ('|C' not in text or '|XY' not in text):
            probs.append('missing a vision colour')
    else:
        for code in ('^1', '|XY'):
            if code not in text:
                probs.append('missing ' + code)
        if kind in ('menu', 'login', 'note') and '|17' not in text:
            probs.append('missing |17')
        if not lite and kind != 'banner' and '|23' not in text:
            probs.append('missing |23')
    if probs:
        raise SystemExit('%s %s: %s' % (look.id, name, '; '.join(probs)))
    runs = goodbye_runs(look) if name == 'GOODBYE.ANS' else None
    if runs is None:
        return
    # The grid was already painted. Re-check the window and the login row from the source runs.
    return


def lists(look):
    if look.id == 'vision':
        return {
            'FILES.LST': files_lst('|C5', 31, 11),
            'MSGS.LST': msgs_lst('|C5', 31, 11),
            'USERS.LST': users_lst('|C2', '|C1', '|C4', '|C5', 31, 11),
        }
    return {
        'FILES.LST': files_lst('^8', 31, 7),
        'MSGS.LST': msgs_lst('^8', 31, 7),
        'USERS.LST': users_lst('^3', '^1', '^2', '^8', 31, 7),
    }


def bundle(look):
    files = {'THEME.CFG': theme_cfg(look)}
    files.update(lists(look))
    numbered = {}
    for num, firmware, slug, kind, title, blurb in SCREENS:
        data, runs = build_screen(look, slug, kind, title, blurb, False)
        grid = paint(runs)
        if kind == 'menu':
            bad = window_clear(grid)
            if bad:
                raise SystemExit('%s %s: %s' % (look.id, slug, bad))
            if HINT not in row_text(grid, 21):
                raise SystemExit('%s %s: hint is not on row 22' % (look.id, slug))
        if kind == 'login':
            for mark in ('|{P}', '|{O}'):
                found = False
                for r in range(25):
                    if mark in row_text(grid, r):
                        found = True
                if not found:
                    raise SystemExit('%s login missing %s' % (look.id, mark))
        check_screen(look, firmware or slug, data, 'goodbye' if False else kind, False)
        if firmware:
            files[firmware] = data
        numbered['ans/%s_%s_%s.ans' % (num, look.ans, slug)] = data
        if firmware:
            lite_name = firmware[:-4] + '.LITE.ANS'
            lite, lruns = build_screen(look, slug, kind, title, blurb, True)
            lgrid = paint(lruns)
            if kind == 'menu':
                bad = window_clear(lgrid)
                if bad:
                    raise SystemExit('%s %s: %s' % (look.id, lite_name, bad))
                if HINT not in row_text(lgrid, 21):
                    raise SystemExit('%s %s: hint is not on row 22' % (look.id, lite_name))
            check_screen(look, lite_name, lite, kind, True)
            files[lite_name] = lite
    bye, bruns = build_screen(look, 'goodbye', 'bye', 'LOGOFF', '', False)
    check_screen(look, 'GOODBYE.ANS', bye, 'goodbye', False)
    paint(bruns)
    files['GOODBYE.ANS'] = bye
    lite, lruns = build_screen(look, 'goodbye', 'bye', 'LOGOFF', '', True)
    check_screen(look, 'GOODBYE.LITE.ANS', lite, 'bye', True)
    paint(lruns)
    files['GOODBYE.LITE.ANS'] = lite
    return files, numbered


def readme(look, files, numbered):
    lines = [
        '# %s' % look.mark,
        '',
        'Original AXIS look pack. The art was drawn for this board.',
        'Dialect: %s' % ('vision3' if look.id == 'vision' else 'renegade'),
        '',
        'ans/ holds the numbered screens the installer renames.',
        'axis/ holds the firmware names, the lite screens, the list templates and THEME.CFG.',
        'A .LITE.ANS screen is for a slow radio caller and stays under 700 bytes.',
        '',
        'File sizes',
        '',
    ]
    for name in sorted(numbered):
        lines.append('- %s  %d B' % (name, len(numbered[name])))
    for name in sorted(files):
        lines.append('- axis/%s  %d B' % (name, len(files[name])))
    lines.append('')
    return '\n'.join(lines).encode('ascii')


def write_zip(path, blobs):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + '.part'
    with zipfile.ZipFile(tmp, 'w') as zout:
        for name in sorted(blobs):
            info = zipfile.ZipInfo(name, (2026, 11, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zout.writestr(info, blobs[name])
    os.replace(tmp, path)


def build_all(root):
    made = []
    for look in (VISION, OUTPOST):
        files, numbered = bundle(look)
        blobs = {}
        blobs.update(numbered)
        for name, data in files.items():
            blobs['axis/' + name] = data
        blobs['README.md'] = readme(look, files, numbered)
        path = os.path.join(root, 'art', 'packs', look.zipname)
        write_zip(path, blobs)
        lites = [len(v) for k, v in files.items() if k.endswith('.LITE.ANS')]
        print('%-10s %s  %d members, goodbye %d B, lite %d to %d B' % (
            look.id, look.zipname, len(blobs), len(files['GOODBYE.ANS']), min(lites), max(lites)))
        for name in sorted(files):
            print('  axis/%-20s %5d' % (name, len(files[name])))
        made.append((look, path, files))
    return made


def verify(root):
    """Reload the zips and check the screen set, the dialect and the lite cap."""
    bad = 0
    for look in (VISION, OUTPOST):
        path = os.path.join(root, 'art', 'packs', look.zipname)
        if not os.path.isfile(path):
            print('missing', path)
            bad += 1
            continue
        with zipfile.ZipFile(path) as z:
            names = set(z.namelist())
            need = ['axis/THEME.CFG', 'axis/GOODBYE.ANS', 'axis/FILES.LST', 'axis/MSGS.LST', 'axis/USERS.LST']
            for _n, firmware, _s, _k, _t, _b in SCREENS:
                if firmware:
                    need.append('axis/' + firmware)
                    need.append('axis/' + firmware[:-4] + '.LITE.ANS')
            need.append('axis/GOODBYE.LITE.ANS')
            for num, _fw, slug, _k, _t, _b in SCREENS:
                need.append('ans/%s_%s_%s.ans' % (num, look.ans, slug))
            for name in need:
                if name not in names:
                    print(look.id, 'missing', name)
                    bad += 1
            cfg = z.read('axis/THEME.CFG').decode('ascii')
            want = 'dialect=vision3' if look.id == 'vision' else 'dialect=renegade'
            if want not in cfg.split():
                if not any(line.strip() == want for line in cfg.splitlines()):
                    print(look.id, 'dialect line missing')
                    bad += 1
            for info in z.infolist():
                if info.filename.endswith('.LITE.ANS') and info.file_size > LITE_CAP:
                    print(look.id, info.filename, info.file_size, 'over', LITE_CAP)
                    bad += 1
            main = z.read('axis/MAIN.ANS').decode('cp437')
            if look.id == 'vision' and ('|C7' not in main or '||' not in main or '|CL' not in main):
                print('vision MAIN does not exercise the dialect')
                bad += 1
            if look.id == 'outpost' and ('|CS' not in main or '^1' not in main or '|17' not in main or '|23' not in main or '%ND' not in main or '%TL' not in main):
                print('outpost MAIN does not exercise the dialect')
                bad += 1
    if bad:
        print('dialect packs: %d failed' % bad)
    else:
        print('dialect packs: ok')
    return bad


def main():
    build_all(ROOT)
    if verify(ROOT):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
