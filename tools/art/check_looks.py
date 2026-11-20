# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Check GOODBYE screens, list templates and slow-radio lite art for every AXIS look.

The house look lives in art/menus. The other seven live in art/packs (the axis/ folder inside each zip).
A lite screen is NAME.LITE.ANS beside the full screen. List templates are FILES.LST, MSGS.LST and USERS.LST.
"""
import os
import re
import sys
import zipfile

COLS, ROWS = 80, 25
LITE_CAP = 2048
THEME_CAP = 12288
LST_CAP = 2048
CFG_CAP = 1024
# rows 9 to 21, columns 4 to 44: the lightbar window (docs/MENU-ART.md), 0-based below
WIN = (3, 8, 43, 20)
LOGIN_ROOM = 20
SHADES = set('░▒▓')
MENU_LITE = {
    'MAIN.LITE.ANS', 'FASTLOGN.LITE.ANS', 'PDMATRIX.LITE.ANS', 'MATRIX.LITE.ANS',
    'FILEM.LITE.ANS', 'FILES.LITE.ANS', 'DOORSM.LITE.ANS', 'DOORS.LITE.ANS',
    'MSGMENU.LITE.ANS', 'MSGS.LITE.ANS', 'BBSLISTM.LITE.ANS', 'EMAILM.LITE.ANS', 'RUMORM.LITE.ANS',
}
PACK_LITE = [
    'MAIN.LITE.ANS', 'LOGIN.LITE.ANS', 'FASTLOGN.LITE.ANS', 'NEWUSER.LITE.ANS', 'MATRIX.LITE.ANS',
    'FILES.LITE.ANS', 'DOORS.LITE.ANS', 'MSGS.LITE.ANS', 'PRELOGON.LITE.ANS', 'SHOP.LITE.ANS',
    'MILL.LITE.ANS', 'BULLETIN.LITE.ANS', 'GOODBYE.LITE.ANS',
]
PACKS = [
    ('crt', 'axis-crt-hardcore-ansi-2026-11-07-v2.zip', False),
    ('sea', 'axis-deep-sea-ansi-2026-11-07.zip', False),
    ('haunted', 'axis-haunted-crt-ansi-2026-11-07.zip', False),
    ('cathedral', 'axis-occult-cathedral-ansi-2026-11-07.zip', False),
    ('skulls', 'axis-skulls-ansi-2026-11-07.zip', False),
    ('underground', 'axis-underground-ansi-2026-11-07.zip', True),
    ('wasteland', 'axis-wasteland-ansi-2026-11-07.zip', False),
]
FILES_ROW = '@FN12@ @SZ>7@  |AB'
MSGS_ROW = '@NW1@@NM>4@ @FR12@ @TO12@ @SU24@'
FILES_GEOM = '3,6,74,14'
MSGS_GEOM = '3,6,74,14'
USERS_GEOM = '4,6,76,16'


class Term:
    """The same 80x25 wrap as tools/screenshots/bbs_shots.py, without the PNG half."""

    def __init__(self):
        self.fg, self.bg, self.bold = 7, 0, False
        self.esc = None
        self.reset()
        self.scrolled = 0

    def reset(self):
        self.cells = [[(' ', 7, 0) for _ in range(COLS)] for _ in range(ROWS)]
        self.x = self.y = 0

    def sgr(self, params):
        for p in params or [0]:
            if p == 0:
                self.fg, self.bg, self.bold = 7, 0, False
            elif p == 1:
                self.bold = True
            elif 30 <= p <= 37:
                self.fg = p - 30
            elif 40 <= p <= 47:
                self.bg = p - 40

    def csi(self, cmd, nums):
        n = nums[0] if nums and nums[0] else 1
        if cmd == 'm':
            self.sgr(nums)
        elif cmd in 'Hf':
            self.y = max(0, min(ROWS - 1, (nums[0] if nums and nums[0] else 1) - 1))
            self.x = max(0, min(COLS - 1, (nums[1] if len(nums) > 1 and nums[1] else 1) - 1))
        elif cmd == 'A':
            self.y = max(0, self.y - n)
        elif cmd == 'B':
            self.y = min(ROWS - 1, self.y + n)
        elif cmd == 'C':
            self.x = min(COLS - 1, self.x + n)
        elif cmd == 'D':
            self.x = max(0, self.x - n)
        elif cmd == 'J' and (not nums or nums[0] in (0, 2)):
            if not nums or nums[0] == 2:
                self.reset()

    def feed(self, text):
        for ch in text:
            if self.esc is not None:
                self.esc += ch
                if self.esc == '[' or (len(self.esc) > 1 and not ch.isalpha()):
                    continue
                if not self.esc.startswith('['):
                    self.esc = None
                    continue
                cmd, args = self.esc[-1], self.esc[1:-1].lstrip('?')
                nums = [int(a) if a.isdigit() else 0 for a in args.split(';')] if args else []
                self.esc = None
                self.csi(cmd, nums)
                continue
            if ch == '\x1b':
                self.esc = ''
            elif ch == '\r':
                self.x = 0
            elif ch == '\n':
                self.y += 1
                if self.y >= ROWS:
                    self.scrolled += 1
                    self.y = ROWS - 1
            elif self.y < ROWS and self.x < COLS and ord(ch) >= 32:
                fg = self.fg + (8 if self.bold else 0)
                self.cells[self.y][self.x] = (ch, fg, self.bg)
                self.x += 1
                if self.x >= COLS:
                    self.x = 0
                    self.y += 1
                    if self.y >= ROWS:
                        self.scrolled += 1
                        self.y = ROWS - 1

    def text(self):
        return '\n'.join(''.join(c[0] for c in row) for row in self.cells)


def decode(data):
    probs = []
    if isinstance(data, str):
        data = data.encode('cp437')
    try:
        text = data.decode('cp437')
    except UnicodeError:
        return '', ['not cp437']
    if text.encode('cp437') != data:
        probs.append('cp437 does not round trip')
    for i, b in enumerate(data):
        if b < 0x20 and b not in (0x1b, 0x0d, 0x0a):
            probs.append('control byte at %d' % i)
            break
    if '\x1a' in text:
        probs.append('a Ctrl-Z would hide the rest of the screen')
    if any(ch in SHADES for ch in text):
        probs.append('block shading (a slow screen wants plain colour, not a shade flood)')
    return text, probs


def lint_screen(name, data, kind):
    """kind: menu, login, note, bye, goodbye. goodbye is the full logoff screen."""
    text, probs = decode(data)
    cap = LITE_CAP if kind != 'goodbye' else THEME_CAP
    if len(data) > cap:
        probs.append('%d bytes, over the %d byte cap' % (len(data), cap))
    if '%BN' not in text or '%NN' not in text:
        probs.append('row 6 wants %BN and %NN')
    if kind == 'goodbye' and 'Call back soon' not in text:
        probs.append('the logoff line is missing')
    if kind == 'bye' and 'Call back soon' not in text:
        probs.append('the logoff line is missing')
    t = Term()
    t.feed(text)
    if t.scrolled:
        probs.append('scrolls %d line(s)' % t.scrolled)
    if kind == 'menu':
        x0, y0, x1, y1 = WIN
        for r in range(y0, y1 + 1):
            for c in range(x0, x1 + 1):
                ch, _fg, bg = t.cells[r][c]
                if ch != ' ' or bg:
                    probs.append('menu window painted at %d,%d' % (c + 1, r + 1))
                    return probs
        row = t.text().split('\n')[21]
        if 'arrows move' not in row:
            probs.append('the lightbar hint is not on row 22')
    # DOS 8 is bright black. On a black cell the footer disappears. A bright colour (not 8) reads.
    for r, row in enumerate(t.cells):
        chars = ''.join(c[0] for c in row)
        at = chars.find('AXIS MICRO BBS')
        if at < 0:
            continue
        for c in range(at, at + len('AXIS MICRO BBS')):
            ch, fg, bg = row[c]
            if ch == ' ':
                continue
            if fg < 9 or bg:
                probs.append('footer hard to read at %d,%d (colour %d on %d)' % (c + 1, r + 1, fg, bg))
                return probs
    if kind == 'login':
        rows = t.text().split('\n')
        for mark in ('|{P}', '|{O}'):
            at = [(r, row.find(mark)) for r, row in enumerate(rows) if mark in row]
            if not at:
                probs.append(mark + ' missing')
                continue
            r, c = at[0]
            room = rows[r][c + 4:c + LOGIN_ROOM]
            if room.strip() or c + LOGIN_ROOM > 80:
                probs.append('%s has art where the typing goes' % mark)
    return probs


def _kv(text):
    out = {}
    for line in text.splitlines():
        if not line or line[0] == ';' or '=' not in line:
            continue
        k, v = line.split('=', 1)
        out[k] = v
    return out


def lint_lst(name, data):
    text, probs = decode(data)
    if len(data) > LST_CAP:
        probs.append('%d bytes, over %d' % (len(data), LST_CAP))
    kv = _kv(text)
    if 'row' not in kv or not kv['row']:
        probs.append('no row=')
    if kv.get('rule') == '0':
        probs.append('the divider is off')
    geom = kv.get('geom', '')
    if name == 'FILES.LST':
        if geom != FILES_GEOM:
            probs.append('geom %s' % geom)
        if kv.get('row') != FILES_ROW:
            probs.append('row tokens changed')
        if re.search(r'\|\d', kv.get('row', '')):
            probs.append('a colour code in the file row cuts the lightbar')
        if not kv.get('caption', '').startswith('|') or '|PG' not in kv.get('caption', ''):
            probs.append('caption lost |PG')
    elif name == 'MSGS.LST':
        if geom != MSGS_GEOM:
            probs.append('geom %s' % geom)
        if kv.get('row') != MSGS_ROW:
            probs.append('row tokens changed')
        if re.search(r'\|\d', kv.get('row', '')):
            probs.append('a colour code in the message row cuts the lightbar')
        if '|CT' not in kv.get('caption', ''):
            probs.append('caption lost |CT')
    elif name == 'USERS.LST':
        if geom != USERS_GEOM:
            probs.append('geom %s' % geom)
        row = kv.get('row', '')
        for tok in ('@HN20@', '@LO19@', '@CA>5@', '@TG22@', '|TW'):
            if tok not in row:
                probs.append('missing ' + tok)
    else:
        probs.append('unexpected list ' + name)
    colours = kv.get('colours', '')
    parts = colours.split(',')
    if len(parts) != 2 or not all(p.isdigit() and 0 <= int(p) <= 255 for p in parts):
        probs.append('colours ' + colours)
    return probs


def lint_cfg(data):
    text, probs = decode(data)
    if len(data) > CFG_CAP:
        probs.append('%d bytes, over %d' % (len(data), CFG_CAP))
    if 'dialect=axis' not in text.split():
        # a value may sit on its own line; accept a line that is exactly dialect=axis
        if not any(line.strip().lower() == 'dialect=axis' for line in text.splitlines()):
            probs.append('dialect is not axis')
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(';'):
            continue
        if '=' not in line:
            probs.append('bad line')
            break
        k, v = line.split('=', 1)
        k = k.strip().lower()
        if k == 'dialect' and v.strip().lower() != 'axis':
            probs.append('dialect ' + v.strip())
        elif k != 'dialect' and not re.fullmatch(r'c[1-7]|r[0-9]', k):
            pass  # unknown keys are skipped by the board
        elif k != 'dialect' and not (v.strip().isdigit() and 0 <= int(v.strip()) <= 255):
            probs.append(k + ' out of range')
    return probs


def _report(label, probs):
    print('  %-24s %s' % (label, 'ok' if not probs else 'FAIL'))
    for p in probs:
        print('      ' + p)
    return 1 if probs else 0


def check_blob(label, name, data, kind):
    if kind == 'lst':
        probs = lint_lst(name, data)
    elif kind == 'cfg':
        probs = lint_cfg(data)
    else:
        probs = lint_screen(name, data, kind)
    return _report('%s %d B' % (label, len(data)), probs)


def kind_of(name):
    if name == 'GOODBYE.ANS':
        return 'goodbye'
    if name == 'GOODBYE.LITE.ANS':
        return 'bye'
    if name == 'LOGIN.LITE.ANS':
        return 'login'
    if name in MENU_LITE:
        return 'menu'
    return 'note'


def check_menus(menus):
    bad = 0
    print('== axis menus  %s' % menus)
    need = ['GOODBYE.ANS', 'GOODBYE.LITE.ANS', 'FILES.LST', 'MSGS.LST', 'USERS.LST', 'THEME.CFG']
    for name in sorted(os.listdir(menus)):
        if name.upper().endswith('.ANS') and not name.upper().endswith('.LITE.ANS'):
            lite = name[:-4] + '.LITE.ANS'
            if lite not in need:
                need.append(lite)
    for name in need:
        path = os.path.join(menus, name)
        if not os.path.isfile(path):
            bad += _report(name, ['missing'])
            continue
        with open(path, 'rb') as fh:
            data = fh.read()
        if name.endswith('.LST'):
            bad += check_blob(name, name, data, 'lst')
        elif name == 'THEME.CFG':
            bad += check_blob(name, name, data, 'cfg')
        else:
            bad += check_blob(name, name, data, kind_of(name))
    return bad


def axis_members(z):
    out = {}
    for n in z.namelist():
        norm = n.replace('\\', '/')
        if norm.startswith('axis/') and not norm.endswith('/'):
            out[os.path.basename(norm)] = z.read(n)
    return out


def check_pack(path, saylor):
    bad = 0
    print('== %s' % os.path.basename(path))
    if not os.path.isfile(path):
        return _report(os.path.basename(path), ['missing'])
    with zipfile.ZipFile(path) as z:
        files = axis_members(z)
    need = ['GOODBYE.ANS', 'THEME.CFG', 'FILES.LST', 'MSGS.LST', 'USERS.LST'] + PACK_LITE
    if saylor:
        need.append('SAYLOR.LITE.ANS')
    for name in need:
        data = files.get(name)
        if data is None:
            bad += _report(name, ['missing from axis/'])
            continue
        if name.endswith('.LST'):
            bad += check_blob(name, name, data, 'lst')
        elif name == 'THEME.CFG':
            bad += check_blob(name, name, data, 'cfg')
        else:
            bad += check_blob(name, name, data, kind_of(name))
    return bad


def check_repo(root):
    bad = check_menus(os.path.join(root, 'art', 'menus'))
    packs = os.path.join(root, 'art', 'packs')
    for _look, fn, saylor in PACKS:
        bad += check_pack(os.path.join(packs, fn), saylor)
    print('looks: %s' % ('ok' if not bad else '%d failed' % bad))
    return bad


def main():
    root = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
    return check_repo(root)


if __name__ == '__main__':
    sys.exit(main())
