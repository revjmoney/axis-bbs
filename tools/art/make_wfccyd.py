# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw the built-in CYD waiting-for-caller screen as CP437 ANSI (art/wfccyd.ans), 45 columns by 24 rows.

The board's own screen shows a terminal of 45x24 cells of 7x10 pixels. This is the screen it shows when the theme has no
WFCCYD.ANS: the same look as the old pixel-drawn WFC (a title bar, the chunky AXIS logo, a status line, a key box on the left,
the nodes and today boxes on the right, the newest events, a footer), drawn with the pipe codes of docs/WFC-CODES.md so the
values are live and the keys tappable. Every row is exactly 45 columns once the codes are filled in, so it wraps by itself (no
CR LF); the last row stops at 44 so the screen never scrolls.

usage: python -I tools/art/make_wfccyd.py art/wfccyd.ans src/platform/cyd/wfccyd_art.h [preview.png]
"""
import os
import re
import sys

COLS, ROWS = 45, 24
# the axis look: orange frames, yellow keys and accents, grey text, white values, dark grey rules
FRAME, HOT, BODY, VALUE, DIM, BROWN = '1;31', '1;33', '0;37', '1;37', '1;30', '0;33'
RAMP = ['1;33', '1;31', '0;33', '0;33']  # the logo, top to bottom


def width(text):
    text = re.sub(r'@[A-Z]+#*@', lambda m: ' ' * len(m.group(0)), text)
    return len(re.sub(r'\|K[A-Z]', ' ', text))


def f(code, w):  # a field exactly w columns wide, @CODE####@
    assert w >= len(code) + 3
    return '@' + code + '#' * (w - len(code) - 2) + '@'


def sgr(a):
    return b'\x1b[0;' + a.encode() + b'm' if a.count(';') == 1 and not a.startswith('0;') else b'\x1b[' + a.encode() + b'm'


_last = [None]  # the colour in force: a run in the same colour needs no new SGR (the screen is drawn top to bottom)


def row(parts, cols=COLS):
    used = sum(width(t) for _, t in parts)
    assert used <= cols, (used, parts)
    out = b''
    for a, t in parts + [(BODY, ' ' * (cols - used))]:
        if t:
            out += (sgr(a) if a != _last[0] else b'') + t.encode('cp437')
            _last[0] = a
    return out


# the diode AXIS of the old screen: 6x7 letters, two pixel rows to a text row with half blocks
LETTERS = [[" #### ", "##  ##", "##  ##", "######", "##  ##", "##  ##", "##  ##"],
           ["##  ##", "##  ##", " #### ", "  ##  ", " #### ", "##  ##", "##  ##"],
           ["######", "  ##  ", "  ##  ", "  ##  ", "  ##  ", "  ##  ", "######"],
           [" #####", "##    ", "##    ", " #### ", "    ##", "    ##", "##### "]]


def logo_row(r):  # text row r (0..3) of the logo, 28 columns
    parts = []
    for l in LETTERS:
        s = ''
        for c in range(6):
            top = l[2 * r][c] == '#'
            bottom = 2 * r + 1 < 7 and l[2 * r + 1][c] == '#'
            s += '█' if top and bottom else '▀' if top else '▄' if bottom else ' '
        parts.append((RAMP[r], s + ' '))
    return parts


def box_top(label, w):  # "/--: label :--\" in w columns
    rest = w - 2 - len(label) - 6
    return [(FRAME, '╒═'), (DIM, ': '), (HOT, label), (DIM, ' :'), (FRAME, '═' * rest + '╕')]


def box_mid(parts, w):
    used = sum(width(t) for _, t in parts)
    assert used <= w - 2, (used, parts)
    return [(DIM, '│')] + parts + [(BODY, ' ' * (w - 2 - used)), (DIM, '│')]


def box_end(w):
    return [(FRAME, '╘' + '═' * (w - 2) + '╛')]


def key(code, rest):
    return [(DIM, '['), (HOT, code), (DIM, ']'), (BODY, rest)]


def main(path, header, preview=None):
    L, R = 18, 27  # the key box (columns 0..17) and the right column (18..44)
    rows = []
    rows.append(row([(FRAME, '['), (HOT, 'axis'), (BODY, ' micro bbs '), (DIM, '; '), (VALUE, f('BN', 26)), (FRAME, ']')]))
    right = [[(BODY, 'underground')], [(HOT, 'c   n   c'), (BODY, ' '), (VALUE, f('VN', 5))], [(BODY, f('MB', 14))], [(VALUE, f('SN', 14))]]
    for r in range(4):
        rows.append(row([(BODY, ' ')] + logo_row(r) + [(BODY, ' ')] + right[r]))
    rows.append(row([(FRAME, '['), (VALUE, f('TM', 7)), (BODY, ' '), (VALUE, f('DT', 12)), (HOT, '  ip '), (VALUE, f('IP', 15)),
                     (FRAME, ']')]))
    keys = [key('|KO', ' local logon'), key('|KS', ' settings'), key('|KU', ' users'), key('|KM', ' menus'),
            key('|KB', ' boards'), key('|KF', ' files'), key('|KL', ' logs'), key('|KE', ' events'),
            key('|KA', ' archive'), key('|KV', ' validate')]
    left = [box_top('axis', L)] + [box_mid([(BODY, ' ')] + k, L) for k in keys] + [box_end(L)]
    nodes = [box_top('nodes', R)] + [box_mid([(BODY, ' '), (VALUE, f('N' + c, 24))], R) for c in 'ABCD'] + [box_end(R)]
    today = [box_top('today', R),
             box_mid([(BODY, ' calls '), (VALUE, f('CT', 5)), (BODY, ' posts '), (VALUE, f('PT', 5))], R),
             box_mid([(BODY, ' ups   '), (VALUE, f('UT', 5)), (BODY, ' ram '), (VALUE, f('RM', 6)), (BODY, 'k')], R),
             box_mid([(BODY, ' last  '), (VALUE, f('LC', 18))], R),
             box_mid([(BODY, ' sd '), (VALUE, f('SD', 7)), (BODY, ' up '), (VALUE, f('UP', 10))], R),
             box_end(R)]
    right_col = nodes + today
    for i in range(max(len(left), len(right_col))):
        l = left[i] if i < len(left) else []
        lw = sum(width(t) for _, t in l)
        rows.append(row(l + [(BODY, ' ' * (L - lw))] + (right_col[i] if i < len(right_col) else [])))
    rows.append(row([(HOT, ' page '), (VALUE, f('PG', 39))]))
    rows.append(row([(FRAME, '═' * 2), (DIM, ': '), (HOT, 'events'), (DIM, ' :'), (FRAME, '═' * 33)]))
    for c in 'ABC':
        rows.append(row([(BODY, ' '), (BODY, f('E' + c, 44))]))
    rows.append(row([(BROWN, ' (release 11/08/26)'), (BODY, ' ' * 9), (BROWN, '(c) jmscnc.com')], COLS - 1))
    assert len(rows) == ROWS, len(rows)
    data = b''.join(rows) + b'\x1b[0m'
    open(path, 'wb').write(data)
    print(path, len(data), 'bytes')
    # the firmware copy: the bytes as one C string (octal escapes, so a following digit is never swallowed)
    lit = ''.join(chr(b) if 0x20 <= b < 0x7f and chr(b) not in '"\\?' else '\\%03o' % b for b in data)
    lines = [lit[i:i + 120] for i in range(0, len(lit), 120)]
    for i in range(len(lines) - 1):  # never split an escape
        while '\\' in lines[i][-3:]:
            cut = lines[i].rindex('\\')
            lines[i + 1] = lines[i][cut:] + lines[i + 1]
            lines[i] = lines[i][:cut]
    with open(header, 'w', newline='\r\n') as h:
        h.write('// Generated by tools/art/make_wfccyd.py (art/wfccyd.ans). Do not edit; edit the generator.\n')
        h.write('// The CYD waiting-for-caller screen when the theme has no WFCCYD.ANS: 45x24 ANSI with WFC codes.\n')
        h.write('#pragma once\n')
        h.write('static const char kWfcCydAns[] =\n')
        for l in lines:
            h.write('    "' + l + '"\n')
        h.write('    ;\n')
    print(header)
    if preview:
        sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'screenshots'))
        import bbs_shots
        bbs_shots.COLS, bbs_shots.ROWS = COLS, ROWS
        t = bbs_shots.Term()
        sample = re.sub(rb'@([A-Z]+)#*@', lambda m: m.group(1) + b'.' * (len(m.group(0)) - len(m.group(1))), data)
        t.feed(re.sub(rb'\|K([A-Z])', lambda m: m.group(1), sample).decode('cp437'))
        t.png(preview, 'wfccyd.ans')
        print(preview)


if __name__ == '__main__':
    main(*sys.argv[1:])
