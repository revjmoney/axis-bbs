# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""The starter AXIS menu set: ViSiON/3's menus and keys (via v3import), lightbars laid over a placeholder AXIS
header for MAIN, MSGMENU, FILEM and DOORSM, and the doors menu pointed at AXIS's own doors. The real art pass
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

# menu -> (header word, lightbar items in columns of (key, label))
BARS = {
    'MAIN': ('AXIS', [[('M', 'Messages Menu'), ('T', 'Transfer Menu'), ('D', 'Doors Menu'), ('E', 'Private Email'), ('C', 'Chat with SysOp')],
                      [('O', 'One-Liners'), ('R', 'Rumors'), ('B', 'BBS Listings'), ('V', 'Voting Booth'), ('L', 'User List')],
                      [('W', 'Last Callers'), ('S', 'System Stats'), ('Y', 'Your Stats'), ('K', 'Your Konfig'), ('G', 'Logoff')]]),
    'MSGMENU': ('MSGS', [[('R', 'Read Messages'), ('P', 'Post a Message'), ('N', 'New Scan')],
                         [('*', 'List Areas'), ('E', 'Private Email'), ('Q', 'Main Menu')]]),
    'FILEM': ('FILES', [[('F', 'List Files'), ('D', 'Download'), ('U', 'Upload')],
                        [('S', 'Search Files'), ('N', 'New Files'), ('Q', 'Main Menu')]]),
    'DOORSM': ('DOORS', [[('L', 'Red Dragon (LORD)'), ('B', 'BBS Black Jack')],
                         [('A', 'ANSI Pack Viewer'), ('Q', 'Main Menu')]]),
}
DOORS = ['cmd=L|DOOR:lord|fD|0|Playing LORD', 'cmd=B|DOOR:blackjack|fD|0|Playing Black Jack',
         'cmd=A|DOOR:ansiview|*|0|Viewing ANSI Packs', 'cmd=S|DOOR:dos|s255|1|At the DOS prompt', 'cmd=Q|GOTO:MAIN|*|0|',
         'cmd=G|LOGOFF|*|0|']


def bars(cols):
    out = []
    for c, items in enumerate(cols):
        x = 4 + c * 26
        for r, (k, label) in enumerate(items):
            out.append('bar=%d,%d,%d,%d,%s, %s %s' % (x, 10 + r, HI, REG, k, ('(' + k + ')').ljust(4), label.ljust(19)))
    return out


def art(word, seed):
    a = axis_packs.header(word, word, 'axis', seed)
    a += '\x1b[22;4H\x1b[0;30;1m' + 'arrows move, Enter picks, or press the key'
    return a


def main():
    v3, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    for i, name in enumerate(['MAIN', 'MSGMENU', 'FILEM', 'DOORSM', 'BBSLISTM', 'RUMORM', 'EMAILM']):
        lines = v3import.one(v3, name).rstrip('\n').split('\n')
        if name == 'DOORSM':
            lines = [l for l in lines if not l.startswith('cmd=')] + DOORS
        if name in BARS:
            word, cols = BARS[name]
            lines += bars(cols)
            with open(os.path.join(out, name + '.ANS'), 'wb') as f:
                f.write(art(word, 40 + i).encode('latin-1'))
        with open(os.path.join(out, name + '.MNU'), 'w', encoding='latin-1', newline='\n') as f:
            f.write('\n'.join(lines) + '\n')
        print(name)


if __name__ == '__main__':
    main()
