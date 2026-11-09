# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Draw the built-in terminal waiting-for-caller screen as CP437 ANSI (art/wfc80.ans), 80 columns by 25 rows.

A Renegade-style WFC with the pipe codes of docs/WFC-CODES.md in it. Every framed row is exactly 80 columns once the codes are
filled in (the @XX###@ fields keep their width), and the key rows under the frame are unframed because a |K? code shrinks.
Build the firmware copy with:  python -I tools/art/ans2header.py art/wfc80.ans src/bbs/wfc80_art.h kWfc80Art --rows=25

usage: python -I tools/art/make_wfc80.py art/wfc80.ans
"""
import re
import sys

BORDER, LABEL, HEAD, VALUE, KEY, TITLE = '1;30', '0;37', '0;33', '1;37', '1;33', '1;33'
TL, TR, BL, BR, H, V, ML, MR = 0xC9, 0xBB, 0xC8, 0xBC, 0xCD, 0xBA, 0xCC, 0xB9


def width(text):
    text = re.sub(r'@[A-Z]+#*@', lambda m: ' ' * len(m.group(0)), text)
    return len(re.sub(r'\|K[A-Z]', ' ', text))


def f(code, w):  # a field exactly w columns wide, @CODE####@
    return '@' + code + '#' * (w - len(code) - 2) + '@'


def sgr(a):
    return b'\x1b[0;' + a.encode() + b'm' if ';' not in a else b'\x1b[' + a.encode() + b'm'


def row(parts, inner=78):
    used = sum(width(t) for _, t in parts)
    assert used <= inner, (used, parts)
    out = sgr(BORDER) + bytes([V])
    for a, t in parts + [(LABEL, ' ' * (inner - used))]:
        out += sgr(a) + t.encode('cp437')
    return out + sgr(BORDER) + bytes([V])


def rule(l, r):
    return sgr(BORDER) + bytes([l]) + bytes([H]) * 78 + bytes([r])


def main(path):
    rows = [rule(TL, TR)]
    rows.append(row([(VALUE, ' ' + f('BN', 40)), (LABEL, ' ' * 12 + 'v'), (VALUE, f('VN', 9)), (LABEL, f('MB', 14)), (LABEL, ' ')]))
    rows.append(row([(TITLE, ' ' * 21 + 'W A I T I N G   F O R   C A L L E R')]))
    rows.append(rule(ML, MR))
    rows.append(row([(HEAD, ' NODES'), (LABEL, ' ' * 38), (HEAD, 'TODAY AND SYSTEM')]))
    right = [('Calls today', 'CT'), ('Posts today', 'PT'), ('Uploads', 'UT'), ('Last caller', 'LC'),
             ('SD free', 'SD'), ('RAM free KB', 'RM'), ('IP address', 'IP'), ('Uptime', 'UP')]
    for i, (lab, code) in enumerate(right):
        rows.append(row([(VALUE, ' ' + f('N' + 'ABCDEFGH'[i], 42)), (LABEL, ' '), (LABEL, lab.ljust(12)), (VALUE, f(code, 20))]))
    rows.append(rule(ML, MR))
    rows.append(row([(HEAD, ' EVENTS')]))
    for c in 'ABC':
        rows.append(row([(LABEL, ' ' + f('E' + c, 76))]))
    rows.append(row([(HEAD, ' PAGE '), (VALUE, f('PG', 70))]))
    rows.append(rule(ML, MR))
    rows.append(row([(VALUE, ' ' + f('TM', 10)), (LABEL, ' '), (VALUE, f('DT', 20)), (LABEL, ' ' * 17), (HEAD, 'SYSOP '), (VALUE, f('SN', 20))]))
    rows.append(rule(BL, BR))

    def keys(items):
        out = b''
        for label in items:
            out += sgr(LABEL) + b'[' + sgr(KEY) + label[0].encode() + sgr(LABEL) + b']' + label[1].encode() + b'  '
        return out
    rows.append(keys([('|KS', 'ettings'), ('|KU', 'sers'), ('|KM', ' Menus'), ('|KB', 'oards'), ('|KF', 'iles'), ('|KL', 'ogs')]))
    rows.append(keys([('|KE', 'vents'), ('|KA', 'rchive'), ('|KV', ' Validate'), ('Space', ' Log on here'), ('Q', 'uit')]))
    rows.append(b'')
    assert len(rows) == 25
    # a full 80-column row wraps by itself (BBS style): a CR LF after it would leave a blank line
    open(path, 'wb').write(b''.join(rows[:-3]) + rows[-3] + b'\r\n' + rows[-2] + b'\x1b[0m')


if __name__ == '__main__':
    main(sys.argv[1])
