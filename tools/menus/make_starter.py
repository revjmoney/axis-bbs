# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""The starter AXIS menu set: ViSiON/3's menus and keys (via v3import), lightbars laid over a placeholder AXIS
header for MAIN, MSGMENU, FILEM and DOORSM, and the doors menu pointed at AXIS's own doors. The front door
(PDMATRIX, FASTLOGN) keeps ViSiON/3's lightbar coordinates and recolours them to AXIS orange. Placeholder
art covers the matrix, fast login, three pre-logon screens, the login screen (|{P} and |{O} mark the
handle and password) and the new-user screen. LOGIN.SEQ is the same template the firmware uses
when the file is missing, including the Saylor poster before the fast-login choice. The real art pass
replaces the .ANS files later; the menus and keys stay.

usage: python -I tools/menus/make_starter.py V3_MENU_DIR OUT_DIR
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'art'))

import axis_packs  # noqa: E402
import v3import  # noqa: E402

HI, REG = 96, 7  # lit: black on orange (DOS brown); plain: grey

# Every look (the eight theme packs and this starter art) keeps one menu window clear: rows 9-21, columns
# 4-44, with the "arrows move" hint on row 22. Lightbars and the prompt go inside it, so no theme's art is
# painted over. tools/art/check_menus.py checks this against every theme.
WIN_X0, WIN_Y0, WIN_X1, WIN_Y1 = 4, 9, 44, 21
BAR_Y = 10      # first lightbar row
PROMPT_Y = 19   # the command prompt row (|XY in prompt1)

# menu -> (header word, lightbar items in columns of (key, label), prompt label)
BARS = {
    'MAIN': ('AXIS', [[('M', 'Messages Menu'), ('T', 'Transfer Menu'), ('D', 'Doors Menu'), ('E', 'Private Email'),
                       ('C', 'SysOp Chat'), ('O', 'One-Liners'), ('R', 'Rumors'), ('B', 'BBS Listings')],
                      [('V', 'Voting Booth'), ('L', 'User List'), ('W', 'Last Callers'), ('S', 'System Stats'),
                       ('Y', 'Your Stats'), ('K', 'Your Konfig'), ('G', 'Logoff')]], None),
    'MSGMENU': ('MSGS', [[('R', 'Read Messages'), ('P', 'Post a Message'), ('N', 'New Scan'), ('*', 'List Areas'),
                          ('E', 'Private Email'), ('Q', 'Main Menu')]], 'Messages'),
    'FILEM': ('FILES', [[('F', 'List Files'), ('D', 'Download'), ('U', 'Upload'), ('S', 'Search Files'), ('N', 'New Files'),
                         ('Q', 'Main Menu')]], 'Transfer'),
    'DOORSM': ('DOORS', [[('L', 'Red Dragon (LORD)'), ('A', 'ANSI Pack Viewer'), ('Q', 'Main Menu')]],  # no DOS doors: no runtime
               'Doors'),
    'BBSLISTM': ('BBS', [[('L', 'List the BBSes'), ('A', 'Add a BBS'), ('Q', 'Main Menu')]], 'BBS List'),
    'RUMORM': ('RUMOR', [[('L', 'List Rumors'), ('A', 'Add a Rumor'), ('Q', 'Main Menu')]], 'Rumors'),
    'EMAILM': ('EMAIL', [[('R', 'Read Email'), ('S', 'Send Email'), ('Q', 'Main Menu')]], 'Email'),
}
# Only commands that run on AXIS get a bar (the rest still answer "coming soon" when typed). A bar key with no
# cmd= line would do nothing, so main() checks every bar has one. MSGMENU's E had none in ViSiON/3's set.
EXTRA = {'MSGMENU': ['cmd=E|GOTO:EMAILM|*|0|Email Menu']}
# the front door keeps ViSiON/3's words and keys, moved into the window
FRONT_X, FRONT_Y, FRONT_W = 12, 12, 24
MAIN_PROMPT = '|08[|07|TIME|08] |10|UH |08-|15> |07'

DOORS = ['cmd=L|DOOR:lord|fD|0|Playing LORD', 'cmd=A|DOOR:ansiview|*|0|Viewing ANSI Packs', 'cmd=Q|GOTO:MAIN|*|0|',
         'cmd=G|LOGOFF|*|0|']  # (Black Jack and the DOS prompt need the DOS runtime, which is out of the firmware)

# ViSiON/3's login.json template, the same steps loginseq.cpp uses when LOGIN.SEQ is empty.
# SAYLOR is the poster in the theme folder. Fast login still skips everything after FASTLOGIN.
SEQ = """; AXIS login sequence. step=COMMAND|CLEAR|PAUSE|LEVEL|DATA
step=SYSOPNOTICES|0|0|0|
step=NMAILSCAN|1|1|0|
step=DISPLAYFILE|1|1|0|SAYLOR
step=FASTLOGIN|0|0|0|
step=PRINTNEWS|1|1|0|
step=LASTCALLS|0|0|0|
step=ONELINERS|0|0|0|
step=USERSTATS|0|0|0|
step=INFOFORMREQUIRED|0|0|0|
step=NEWUSERVAL|0|0|255|
step=CHECKNUV|0|0|0|
"""


def bars(cols):
    """Columns of 19 cells side by side (MAIN); a single column gets 30."""
    width = 19 if len(cols) > 1 else 30
    out = []
    for c, items in enumerate(cols):
        x = WIN_X0 + 1 + c * (width + 1)
        for r, (k, label) in enumerate(items):
            text = (' (%s) %s' % (k, label)).ljust(width)
            out.append('bar=%d,%d,%d,%d,%s,%s' % (x, BAR_Y + r, HI, REG, k, text))
    return out


def in_window(lines, name):
    for l in lines:
        if l.startswith('bar='):
            x, y, _, _, _, text = l[4:].split(',', 5)
            x, y = int(x), int(y)
            if not (WIN_X0 <= x and x + len(text) - 1 <= WIN_X1 and WIN_Y0 <= y < PROMPT_Y - 1):
                sys.exit('%s: bar %r leaves the menu window' % (name, l))


def set_field(lines, key, value):
    return [key + '=' + value if l.startswith(key + '=') else l for l in lines]


def art(word, seed):
    a = axis_packs.header(word, word, 'axis', seed)
    a += '\x1b[22;4H\x1b[0;30;1m' + 'arrows move, Enter picks, or press the key'
    return a


def recolor(lines):
    """ViSiON's front door bars, moved into the menu window and recoloured AXIS orange (lit 96, plain 7).
    Their keys and words stay. Text may contain commas."""
    out, row = [], 0
    for line in lines:
        if line.startswith('bar='):
            p = line[4:].split(',', 5)
            if len(p) == 6:
                p[0], p[1], p[2], p[3] = str(FRONT_X), str(FRONT_Y + row), str(HI), str(REG)
                p[5] = (' ' + p[5].strip()).ljust(FRONT_W)
                row += 1
                line = 'bar=' + ','.join(p)
        out.append(line)
    return out


def write_text(path, text):
    with open(path, 'w', encoding='latin-1', newline='\n') as f:
        f.write(text)


def write_ans(path, text):
    with open(path, 'wb') as f:
        f.write(text.encode('latin-1'))


def line_at(row, col, text):
    return '\x1b[%d;%dH\x1b[0;37m%s' % (row, col, text)


def main():
    v3, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for i, name in enumerate(['MAIN', 'MSGMENU', 'FILEM', 'DOORSM', 'BBSLISTM', 'RUMORM', 'EMAILM']):
        lines = v3import.one(v3, name).rstrip('\n').split('\n')
        if name == 'DOORSM':
            lines = [l for l in lines if not l.startswith('cmd=')] + DOORS
        word, cols, label = BARS[name]
        lines = [l for l in lines if not l.startswith('bar=')] + EXTRA.get(name, []) + bars(cols)
        keys = {k.upper() for l in lines if l.startswith('cmd=') for k in l[4:].split('|')[0].split()}
        for k in [k for col in cols for k, _ in col]:
            if k.upper() not in keys:
                sys.exit('%s: bar %s has no cmd= line' % (name, k))
        prompt = MAIN_PROMPT if label is None else '|08[|15%s|08] |07Command |08-|15> |07' % label
        lines = set_field(set_field(lines, 'prompt', '1'), 'prompt1', '|XY%02d,%02d' % (WIN_X0 + 1, PROMPT_Y) + prompt)
        in_window(lines, name)
        write_ans(os.path.join(out, name + '.ANS'), art(word, 40 + i))
        with open(os.path.join(out, name + '.MNU'), 'w', encoding='latin-1', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print(name)

    # Front door: ViSiON's bars, moved into the menu window.
    for i, (name, word) in enumerate([('PDMATRIX', 'MATRIX'), ('FASTLOGN', 'FAST')]):
        lines = recolor(v3import.one(v3, name).rstrip('\n').split('\n'))
        write_text(os.path.join(out, name + '.MNU'), '\n'.join(lines) + '\n')
        write_ans(os.path.join(out, name + '.ANS'), art(word, 60 + i))
        in_window(lines, name)
        bars_out = [l for l in lines if l.startswith('bar=')]
        if not bars_out or any(',%d,%d,' % (HI, REG) not in l for l in bars_out):
            sys.exit('front door %s lost its orange lightbars' % name)
        print(name, len(bars_out), 'bars')

    # |{P} and |{O} are login markers. The cursor is the '|' cell. Both marks share a column.
    login = axis_packs.header('LOGIN', 'LOGIN', 'axis', 65)
    login += line_at(12, 4, 'Handle:   |{P}')
    login += line_at(14, 4, 'Password: |{O}')
    write_ans(os.path.join(out, 'LOGIN.ANS'), login)

    write_ans(os.path.join(out, 'NEWUSER.ANS'), axis_packs.header('APPLY', 'APPLY', 'axis', 66) + line_at(12, 4, 'A new account waits on the sysop.'))
    for i, (n, word, blurb) in enumerate([
            (1, 'AXIS', 'The shop is open.'),
            (2, 'SHOP', 'The spindle is warm.'),
            (3, 'MILL', 'Mind the chips.')]):
        write_ans(os.path.join(out, 'PRELOGON%d.ANS' % n), axis_packs.header(word, word, 'axis', 70 + i) + line_at(12, 4, blurb))
    write_text(os.path.join(out, 'LOGIN.SEQ'), SEQ)
    print('LOGIN NEWUSER PRELOGON1-3 LOGIN.SEQ')


if __name__ == '__main__':
    main()
