# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Rebuild GOODBYE screens, list templates, THEME.CFG and slow-radio lite art for all eight looks.

    python3 tools/art/make_look_extras.py

Writes the house look into art/menus and the axis/ folder of each look zip in art/packs.
Palettes and motifs match tools/art/make_wfc_packs.py. Lite screens stay under 2 KB: one
colour per run, a short rule, no shade blocks. Menu screens leave rows 9 to 21, columns 4
to 44 blank, with %BN and %NN on row 6.
"""
import os
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_looks  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
DOS = [0, 4, 2, 6, 1, 5, 3, 7]

# 5 pixel rows, the same block shapes as the house drip titles.
FONT = {
    'G': [' ###', '#   ', '# ##', '#  #', ' ###'],
    'O': [' ## ', '#  #', '#  #', '#  #', ' ## '],
    'D': ['### ', '#  #', '#  #', '#  #', '### '],
    'B': ['### ', '#  #', '### ', '#  #', '### '],
    'Y': ['# #', '# #', ' # ', ' # ', ' # '],
    'E': ['####', '#   ', '### ', '#   ', '####'],
}

# Shared with the waiting-screen motifs (tools/art/make_wfc_packs.py).
MOTIF = {
    'axis': (["....YYYYYY....", "....WYYYYO....", "....OOOOOO....", ".....GGGG.....",
              ".....YOYO.....", ".....OYOY.....", "......YO......", ".......O......"],
             {'Y': 14, 'O': 6, 'W': 15, 'G': 7}),
    'crt': (["...WWWWWWWW...", "..WLLLLLLLLWG.", ".GLkkLLLLkkLG.", ".GLkkLLLLkkLG.",
             ".GLLLLkkLLLLG.", "..GLLLLLLLLG..", "...LkLkLkLkG..", "....GGGGGGG..."],
            {'W': 15, 'L': 10, 'G': 8, 'k': 0}),
    'haunted': (["GWWWWWWWWWWWG.", "GLLLLLLLLLLLG.", "GDkkDDDDDkkDG.", "GLkkLLLLLkkLG.",
                 "GDDDDDkDDDDDG.", "GLLkkkkkkkLLG.", "GGGGGGGGGGGGGR", "....GGGGG....."],
                {'G': 7, 'W': 15, 'L': 10, 'D': 2, 'k': 0, 'R': 10}),
    'sea': (["........WC....", ".......W.CCC..", "..SSSSSW..C...", ".SSSCSSSS.....",
             "SSSSSSSSSSS.T.", "kWkWkWSSSSSSS.", ".SSSSSSSSS..T.", "..T...T......."],
            {'S': 7, 'W': 15, 'C': 11, 'T': 3, 'k': 0}),
    'cathedral': (["......GG......", ".....GMMG.....", "....GMPWPG....", "...GMPMPMPG...",
                   "...GPMWMPMG...", "...GMPMPMPG...", ".W.GPMPMPMG.W.", ".P.GGGGGGGG.P."],
                  {'G': 7, 'M': 13, 'P': 5, 'W': 15}),
    'skulls': (["...WWWWWWWW...", "..WWWWWWWWWWG.", ".RWkkWWWWkkWG.", ".RWkRWWWWkRWG.",
                ".RWWWWkkWWWWG.", "..RWWWWWWWWG..", "...WkWkWkWkG..", "....GGGGGGG..."],
               {'W': 15, 'G': 7, 'R': 4, 'k': 0}),
    'underground': (["M..G..GG..G...", "M.GGGGGGGGGG..", "MGGGGkkkkGGGG.", "M.GGkkCCkkGG..",
                     "M.GGkkCCkkGG..", "MGGGGkkkkGGGG.", "M.GGGGGGGGGG..", "M..G..GG..G..."],
                    {'M': 13, 'G': 7, 'k': 0, 'C': 11}),
    'wasteland': (["....GGG.....B.", "...GkYG....B..", "GGGGGGGGGGGBB..", "GOOOOOOOOOOG..",
                   "YkYkYkYkYkYk..", "kGGGGGGGGGGk..", "GkGkGkGkGkGk..", ".GGGGGGGGGG..."],
                  {'G': 7, 'k': 0, 'Y': 14, 'O': 6, 'B': 8}),
}

# cap: caption colour. hi, reg: the lightbar (DOS attributes). axis keeps 111,15 so the
# shipped file list rows stay the built-in colours.
LOOKS = {
    'axis': dict(mark='AXIS', hot=14, accent=6, body=7, dim=8, frame=6, value=15,
                 ramp=(15, 14, 6), cap=6, hi=111, reg=15,
                 farewell='Keep your tools sharp.'),
    'crt': dict(mark='// CRT HARDCORE', hot=10, accent=10, body=7, dim=8, frame=2, value=15,
                ramp=(15, 10, 2), cap=10, hi=47, reg=10,
                farewell='The phosphor holds the line.'),
    'haunted': dict(mark='~ HAUNTED CRT', hot=10, accent=2, body=7, dim=8, frame=2, value=15,
                    ramp=(15, 10, 2), cap=10, hi=47, reg=10,
                    farewell='The glass keeps your seat.'),
    'sea': dict(mark='.o DEEP SEA', hot=11, accent=3, body=7, dim=8, frame=1, value=15,
                ramp=(15, 11, 1), cap=11, hi=31, reg=11,
                farewell='The lure stays lit.'),
    'cathedral': dict(mark='+ OCCULT CATHEDRAL', hot=13, accent=5, body=7, dim=8, frame=5, value=15,
                      ramp=(15, 13, 5), cap=13, hi=95, reg=13,
                      farewell='The nave keeps your name.'),
    'skulls': dict(mark='x SKULLS', hot=12, accent=4, body=7, dim=8, frame=4, value=15,
                   ramp=(15, 12, 4), cap=12, hi=79, reg=12,
                   farewell='The bone door stays open.'),
    'underground': dict(mark='= UNDERGROUND', hot=11, accent=13, body=7, dim=8, frame=9, value=15,
                        ramp=(11, 13, 9), cap=11, hi=31, reg=11,
                        farewell='Clock out. The gear keeps turning.'),
    'wasteland': dict(mark='/// WASTELAND', hot=14, accent=6, body=7, dim=8, frame=6, value=15,
                      ramp=(15, 14, 6), cap=14, hi=111, reg=14,
                      farewell='Salvage what you carried.'),
}

# Theme-folder lite name, art/menus lite name (None when the menu set has no such screen), title, kind.
SCREENS = [
    ('MAIN.LITE.ANS', 'MAIN.LITE.ANS', 'main menu', 'menu'),
    ('LOGIN.LITE.ANS', 'LOGIN.LITE.ANS', 'login', 'login'),
    ('FASTLOGN.LITE.ANS', 'FASTLOGN.LITE.ANS', 'fast login', 'menu'),
    ('NEWUSER.LITE.ANS', 'NEWUSER.LITE.ANS', 'new user', 'note'),
    ('MATRIX.LITE.ANS', 'PDMATRIX.LITE.ANS', 'matrix', 'menu'),
    ('FILES.LITE.ANS', 'FILEM.LITE.ANS', 'files', 'menu'),
    ('DOORS.LITE.ANS', 'DOORSM.LITE.ANS', 'doors', 'menu'),
    ('MSGS.LITE.ANS', 'MSGMENU.LITE.ANS', 'messages', 'menu'),
    ('PRELOGON.LITE.ANS', 'PRELOGON1.LITE.ANS', 'welcome', 'note'),
    ('SHOP.LITE.ANS', 'PRELOGON2.LITE.ANS', 'shop', 'note'),
    ('MILL.LITE.ANS', 'PRELOGON3.LITE.ANS', 'mill', 'note'),
    ('BULLETIN.LITE.ANS', None, 'news', 'note'),
    ('GOODBYE.LITE.ANS', 'GOODBYE.LITE.ANS', 'logoff', 'bye'),
]
MENU_ONLY = [
    ('BBSLISTM.LITE.ANS', 'bbs list', 'menu'),
    ('EMAILM.LITE.ANS', 'email', 'menu'),
    ('RUMORM.LITE.ANS', 'rumors', 'menu'),
]
NOTES = {
    'new user': 'A new account waits on the sysop.',
    'welcome': 'Carrier up. The board is open.',
    'shop': 'The shop is open.',
    'mill': 'The mill is running.',
    'news': 'Board news.',
    'saylor': 'Saylor is clocked in.',
}


class Grid:
    def __init__(self):
        self.cells = [[None] * 80 for _ in range(25)]

    def put(self, r, c, ch, fg, bg=0):
        if 0 <= r < 25 and 0 <= c < 79 and ch != ' ':
            self.cells[r][c] = (ch, fg, bg)

    def text(self, r, c, s, fg, bg=0):
        for i, ch in enumerate(s):
            self.put(r, c + i, ch, fg, bg)
        return c + len(s)


def sgr(fg, bg):
    parts = ['0']
    if fg >= 8:
        parts.append('1')
    parts.append(str(30 + DOS[fg & 7]))
    if bg:
        parts.append(str(40 + DOS[bg & 7]))
    return '\x1b[' + ';'.join(parts) + 'm'


def emit(g):
    out = ['\x1b[0m\x1b[2J\x1b[H']
    for r in range(25):
        row = g.cells[r]
        last = max([i for i, cell in enumerate(row) if cell] + [-1])
        if last < 0:
            continue
        c = 0
        while c <= last and not row[c]:
            c += 1
        out.append('\x1b[%d;%dH' % (r + 1, c + 1))
        cur = None
        while c <= last:
            if not row[c]:
                n = c
                while n <= last and not row[n]:
                    n += 1
                gap = n - c
                out.append('\x1b[%dC' % gap if gap >= 2 else ' ' * gap)
                c = n
                continue
            ch, fg, bg = row[c]
            if (fg, bg) != cur:
                out.append(sgr(fg, bg))
                cur = (fg, bg)
            out.append(ch)
            c += 1
    out.append('\x1b[0m')
    return ''.join(out).encode('cp437')


def rule(g, r, st):
    for x in range(1, 74):
        g.put(r, x, '─', st['frame'])


def motif(g, r, c, look):
    rows, pal = MOTIF[look]
    for y in range(0, len(rows), 2):
        top = rows[y]
        bot = rows[y + 1] if y + 1 < len(rows) else '.' * len(top)
        w = max(len(top), len(bot))
        top, bot = top.ljust(w), bot.ljust(w)
        for x in range(w):
            t, b = pal.get(top[x]) or 0, pal.get(bot[x]) or 0
            if not t and not b:
                continue
            if t and b and t != b:
                ch, fg = '▀', t
            elif t == b:
                ch, fg = '█', t
            elif t:
                ch, fg = '▀', t
            else:
                ch, fg = '▄', b
            g.put(r + y // 2, c + x, ch, fg)


def block_word(g, r, c, text, ramp):
    rows = ['' for _ in range(5)]
    for ch in text:
        glyph = FONT[ch]
        for y in range(5):
            rows[y] += glyph[y] + ' '
    width = len(rows[0])
    if width and all(row.endswith(' ') for row in rows):
        rows = [row[:-1] for row in rows]
        width -= 1
    for y0, colour in ((0, ramp[0]), (2, ramp[1])):
        for x in range(width):
            top = rows[y0][x] == '#'
            bot = rows[y0 + 1][x] == '#'
            if top and bot:
                g.put(r + y0 // 2, c + x, '█', colour)
            elif top:
                g.put(r + y0 // 2, c + x, '▀', colour)
            elif bot:
                g.put(r + y0 // 2, c + x, '▄', colour)
    for x in range(width):
        if rows[4][x] == '#':
            g.put(r + 2, c + x, '▀', ramp[2])
            if x % 6 == 2:
                g.put(r + 3, c + x, '│', ramp[2])
    return width


def chrome(g, look, title, motif_row):
    st = LOOKS[look]
    g.text(0, 1, st['mark'], st['hot'])
    g.text(1, 1, title, st['accent'])
    g.text(5, 1, title, st['hot'])
    g.text(5, 46, '%BN  node %NN', st['body'])
    rule(g, 6, st)
    motif(g, motif_row, 62, look)
    g.text(23, 1, 'AXIS MICRO BBS', st['dim'])
    g.text(23, 20, st['mark'], st['hot'])
    return st


def draw_lite(look, title, kind):
    g = Grid()
    # Login fields sit on rows 12 and 14, so the motif goes below them. Menus keep it in the right margin.
    st = chrome(g, look, title, 16 if kind == 'login' else 9)
    if kind == 'login':
        g.text(11, 3, 'Handle:   |{P}' + ' ' * 16, st['body'])
        g.text(13, 3, 'Password: |{O}' + ' ' * 16, st['body'])
    elif kind == 'menu':
        g.text(21, 3, 'arrows move, Enter picks, or press the key', st['dim'])
    elif kind == 'bye':
        g.text(11, 3, 'Thanks for calling.', st['body'])
        g.text(12, 3, st['farewell'], st['hot'])
        g.text(14, 3, 'Call back soon.', st['accent'])
    else:
        g.text(11, 3, NOTES[title], st['body'])
    return emit(g)


def draw_goodbye(look):
    g = Grid()
    st = LOOKS[look]
    block_word(g, 1, 2, 'GOODBYE', st['ramp'])
    g.text(5, 1, st['mark'], st['hot'])
    g.text(5, 46, '%BN  node %NN', st['body'])
    rule(g, 7, st)
    motif(g, 9, 58, look)
    g.text(15, 3, 'Thanks for calling.', st['body'])
    g.text(16, 3, st['farewell'], st['hot'])
    g.text(18, 3, 'Call back soon.', st['accent'])
    g.text(23, 1, 'AXIS MICRO BBS', st['dim'])
    g.text(23, 20, st['mark'], st['hot'])
    return emit(g)


def files_lst(st):
    return (
        '; File list. Same columns as the built-in list. docs/MENU-ART.md\n'
        'geom=3,6,74,14\n'
        'colours=%d,%d\n'
        'caption=|%02dName          Size   About                         |PG\n'
        'rule=1\n'
        'row=@FN12@ @SZ>7@  |AB\n' % (st['hi'], st['reg'], st['cap'])
    ).encode('ascii')


def msgs_lst(st):
    return (
        '; Message list. Same columns as the built-in list. docs/MENU-ART.md\n'
        'geom=3,6,74,14\n'
        'colours=%d,%d\n'
        'caption=|%02d  #  From          To            Subject          |CT\n'
        'rule=1\n'
        'row=@NW1@@NM>4@ @FR12@ @TO12@ @SU24@\n' % (st['hi'], st['reg'], st['cap'])
    ).encode('ascii')


def users_lst(st):
    # @HN20@ space @LO19@ space @CA>5@ two spaces @TG22@, so the words sit on the fields.
    text = 'handle'.ljust(20) + ' ' + 'from'.ljust(19) + ' ' + 'calls' + '  ' + 'tagline'
    return (
        '; User list. Tokens and widths: docs/MENU-ART.md\n'
        'geom=4,6,76,16\n'
        'colours=%d,%d\n'
        'caption=|%02d%s\n'
        'rule=1\n'
        'row=|%02d@HN20@ |%02d@LO19@ |%02d@CA>5@  |%02d@TG22@|TW\n'
        % (st['hi'], st['reg'], st['dim'], text, st['value'], st['body'], st['cap'], st['dim'])
    ).encode('ascii')


def theme_cfg(look):
    return (
        '; %s draws with the AXIS pipe colours.\n'
        'dialect=axis\n' % LOOKS[look]['mark']
    ).encode('ascii')


def bundle(look, menus):
    """Firmware filenames for one look. menus: also the art/menus names."""
    st = LOOKS[look]
    files = {
        'GOODBYE.ANS': draw_goodbye(look),
        'FILES.LST': files_lst(st),
        'MSGS.LST': msgs_lst(st),
        'USERS.LST': users_lst(st),
        'THEME.CFG': theme_cfg(look),
    }
    menu_files = {}
    for theme_name, menu_name, title, kind in SCREENS:
        data = draw_lite(look, title, kind)
        files[theme_name] = data
        if menus and menu_name:
            menu_files[menu_name] = data
    if look == 'underground':
        files['SAYLOR.LITE.ANS'] = draw_lite(look, 'saylor', 'note')
    if menus:
        menu_files['GOODBYE.ANS'] = files['GOODBYE.ANS']
        menu_files['FILES.LST'] = files['FILES.LST']
        menu_files['MSGS.LST'] = files['MSGS.LST']
        menu_files['USERS.LST'] = files['USERS.LST']
        menu_files['THEME.CFG'] = files['THEME.CFG']
        for name, title, kind in MENU_ONLY:
            menu_files[name] = draw_lite(look, title, kind)
    return files, menu_files


def check_pair(label, name, data):
    if name.endswith('.LST'):
        kind = 'lst'
    elif name == 'THEME.CFG':
        kind = 'cfg'
    else:
        kind = check_looks.kind_of(name)
    probs = check_looks.lint_lst(name, data) if kind == 'lst' else \
        check_looks.lint_cfg(data) if kind == 'cfg' else \
        check_looks.lint_screen(name, data, kind)
    if probs:
        raise SystemExit('%s %s: %s' % (label, name, '; '.join(probs)))


def write_zip(path, files):
    tmp = path + '.part'
    with zipfile.ZipFile(path, 'r') as zin, zipfile.ZipFile(tmp, 'w') as zout:
        for item in zin.infolist():
            norm = item.filename.replace('\\', '/')
            if norm.startswith('axis/'):
                continue
            zout.writestr(item, zin.read(item.filename))
        for name in sorted(files):
            info = zipfile.ZipInfo('axis/' + name, (2026, 11, 20, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            zout.writestr(info, files[name])
    os.replace(tmp, path)


def main():
    menus = os.path.join(ROOT, 'art', 'menus')
    for look in LOOKS:
        files, menu_files = bundle(look, look == 'axis')
        for name, data in files.items():
            check_pair(look, name, data)
        if look == 'axis':
            for name, data in menu_files.items():
                check_pair('menus', name, data)
                with open(os.path.join(menus, name), 'wb') as fh:
                    fh.write(data)
            print('axis menus', len(menu_files), 'files, largest lite',
                  max(len(v) for k, v in menu_files.items() if k.endswith('.LITE.ANS')))
        else:
            fn = dict((a, b) for a, b, _s in check_looks.PACKS)[look]
            write_zip(os.path.join(ROOT, 'art', 'packs', fn), files)
            print('%-12s %d files, goodbye %d B, largest lite %d B' % (
                look, len(files), len(files['GOODBYE.ANS']),
                max(len(v) for k, v in files.items() if k.endswith('.LITE.ANS'))))
    if check_looks.check_repo(ROOT):
        raise SystemExit('look check failed')


if __name__ == '__main__':
    main()
