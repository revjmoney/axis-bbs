# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Check every menu screen in the starter menu set and in every theme.

usage: python tools/art/check_menus.py [THEMES_DIR] [MENUS_DIR] [--show]
       THEMES_DIR defaults to local/sdstage/themes (the card's /themes); MENUS_DIR to art/menus (/menus).
       Without the themes folder (CI) it checks the menu set and its scripts only.

For each screen it checks the format (fits 80x25, never scrolls, under the board's size cap, pipe
codes the board knows) and, for a screen a .MNU draws, that every lightbar item lands on the art's
own text for that item: the bar's key and words must be what the art shows at that row and column.
--show prints the rows each bar lands on. Exit status 1 when anything fails.
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'screenshots'))
from bbs_shots import Term, ROWS, COLS  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), '..', '..'))
# menu art -> theme file (src/bbs/theme.cpp menuScreenFile)
SCREEN = {'MAIN.ANS': 'MAIN.ANS', 'LOGIN.ANS': 'LOGIN.ANS', 'FASTLOGN.ANS': 'FASTLOGN.ANS', 'NEWUSER.ANS': 'NEWUSER.ANS',
          'PDMATRIX.ANS': 'MATRIX.ANS', 'FILEM.ANS': 'FILES.ANS', 'DOORSM.ANS': 'DOORS.ANS', 'MSGMENU.ANS': 'MSGS.ANS',
          'PRELOGON1.ANS': 'PRELOGON.ANS', 'PRELOGON2.ANS': 'SHOP.ANS', 'PRELOGON3.ANS': 'MILL.ANS'}
THEME_CAP, MENU_CAP = 12288, 8192
# placeholder codes the board fills in menu art (Node::menuValue) and the controls (pipecodes.cpp)
VALUES = {'UH', 'ALIAS', 'HANDLE', 'NAME', 'LEVEL', 'GL', 'CALLS', 'TL', 'NODE', 'MN', 'TIME', 'DATE', 'NEWUSERS', 'BN'}
CONTROLS = {'CL', 'DE', 'CR', 'PP', 'XY'}
PERCENT = {'BN', 'DT', 'NN', 'SN', 'TM', 'UN', 'VN'}  # AXIS's own %XX codes (Node::mci)
WINDOW = (4, 9, 44, 21)  # the menu window every look keeps clear: x0, y0, x1, y1 (tools/menus/make_starter.py)
LOGIN_ROOM = 20  # cells a handle or password needs after its |{P} / |{O} mark


def expand(art):
    """The art as the board would send it, with sample values of a typical width."""
    out, i = [], 0
    while i < len(art):
        c = art[i]
        if c == '|' and i + 2 < len(art) + 1:
            m = re.match(r'\|(\d\d)', art[i:])
            if m and int(m.group(1)) <= 15:
                n = int(m.group(1))
                out.append(f'\x1b[0;{"1;" if n >= 8 else ""}{30 + [0, 4, 2, 6, 1, 5, 3, 7][n & 7]}m')
                i += 3
                continue
            m = re.match(r'\|B(\d\d?)', art[i:])
            if m:
                out.append(f'\x1b[{40 + [0, 4, 2, 6, 1, 5, 3, 7][int(m.group(1)) & 7]}m')
                i += len(m.group(0))
                continue
            m = re.match(r'\|XY(\d\d),(\d\d)', art[i:])
            if m:
                out.append(f'\x1b[{int(m.group(2))};{int(m.group(1))}H')
                i += len(m.group(0))
                continue
            m = re.match(r'\|([A-Z]{2,})', art[i:])
            if m:
                name = m.group(1)
                if name[:2] == 'CL':
                    out.append('\x1b[2J\x1b[H'); i += 3; continue
                if name[:2] == 'DE':
                    out.append('\x1b[K'); i += 3; continue
                if name[:2] == 'CR':
                    out.append('\r\n'); i += 3; continue
                if name[:2] == 'PP':
                    out.append('\x1b[u'); i += 3; continue
                key = name if name in VALUES else name[:2]
                out.append('V' * 6)  # a sample value
                i += 1 + len(key)
                continue
        if c == '%' and re.match(r'%[A-Z]{2}', art[i:i + 3]) and art[i + 1:i + 3] in PERCENT:
            out.append('V' * 6)
            i += 3
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def codes(art):
    found = set()
    for m in re.finditer(r'\|([A-Z]{2,})', art):
        name = m.group(1)
        found.add(name if name in VALUES else name[:2])
    for m in re.finditer(r'@([A-Z]{2})#*@', art):
        found.add(m.group(1))
    for m in re.finditer(r'%([A-Z]{2})', art):
        found.add('%' + m.group(1))
    return found


def known(code):
    return code in VALUES or code in CONTROLS or (code[0] == '%' and code[1:] in PERCENT)


def check_login(t):
    """|{P} and |{O}: both there, and room after each for what the caller types."""
    rows, probs = t.text().split('\n'), []
    for mark in ('|{P}', '|{O}'):
        at = [(r, row.find(mark)) for r, row in enumerate(rows) if mark in row]
        if not at:
            probs.append(mark + ' missing')
            continue
        r, c = at[0]
        room = rows[r][c + 4:c + LOGIN_ROOM]
        if room.strip() or c + LOGIN_ROOM > 80:
            probs.append(f'{mark} at {c + 1},{r + 1} has art where the typing goes: [{room}]')
    return probs


def parse_menu(mnu):
    m = {'bars': [], 'keys': set(), 'prompt': '0', 'prompt1': ''}
    for line in open(mnu, encoding='cp437').read().splitlines():
        if line.startswith('bar='):
            x, y, hi, reg, key, text = line[4:].split(',', 5)
            m['bars'].append((int(x), int(y), key, text))
        elif line.startswith('cmd='):
            m['keys'].update(k.upper() for k in line[4:].split('|')[0].split())
        elif '=' in line and not line.startswith(';'):
            k, v = line.split('=', 1)
            m[k] = v
    return m


def check_script(m):
    """The .MNU itself: every bar inside the window with a command behind its key, and the prompt placed
    inside the window (a prompt with no |XY lands under the last bar, over the art's left frame)."""
    x0, y0, x1, y1 = WINDOW
    probs = []
    for x, y, key, text in m['bars']:
        if key.upper() not in m['keys']:
            probs.append(f'bar {key!r} has no cmd= line (picking it does nothing)')
        if not (x0 <= x and x + len(text) - 1 <= x1 and y0 <= y <= y1):
            probs.append(f'bar {key!r} at {x},{y} ({len(text)} wide) leaves the menu window')
    if m['prompt'] == '1' and m['prompt1']:
        xy = re.match(r'\|XY(\d\d),(\d\d)', m['prompt1'])
        if not xy:
            probs.append('prompt1 has no |XY, so it prints under the last bar at column 1')
        elif not (x0 <= int(xy.group(1)) <= x1 and y0 <= int(xy.group(2)) <= y1):
            probs.append(f'prompt1 at {xy.group(1)},{xy.group(2)} is outside the menu window')
        bad = sorted(c for c in codes(m['prompt1']) if not known(c))
        if bad:
            probs.append('prompt1 codes the board leaves blank: ' + ' '.join(bad))
    return probs


class CountingTerm(Term):
    def __init__(self):
        super().__init__()
        self.scrolled = 0

    def scroll(self):
        self.scrolled += 1
        super().scroll()


def render(raw):
    t = CountingTerm()
    t.feed(expand(raw))
    return t


def read_art(path):
    b = open(path, 'rb').read()
    size = len(b)
    if b.find(b'\x1a') >= 0:
        b = b[:b.find(b'\x1a')]
    return b.decode('cp437'), size


def parse_bars(mnu):
    bars = []
    for line in open(mnu, encoding='cp437').read().splitlines():
        if line.startswith('bar='):
            x, y, hi, reg, key, text = line[4:].split(',', 5)
            bars.append((int(x), int(y), key, text))
    return bars


def norm(s):
    return re.sub(r'[^a-z0-9]', '', s.lower())


def check_bars(term, bars, show):
    probs, lines = [], []
    rows = term.text().split('\n')
    for x, y, key, text in bars:
        if not (1 <= y <= ROWS) or x < 1 or x - 1 + len(text) > COLS:
            probs.append(f'bar {key!r} at {x},{y} runs off the screen')
            continue
        under = rows[y - 1][x - 1:x - 1 + len(text)]
        lines.append(f'      {key!r:5} {x:2},{y:2}  bar [{text}]  art [{under}]')
        if not under.strip():
            continue  # a blank slot: the bar draws the label
        words = [w for w in re.findall(r'[A-Za-z]{3,}', text) if w.lower() not in ('the', 'and', 'your')]
        art_n = norm(under)
        missing = [w for w in words if norm(w) not in art_n]
        if missing:
            probs.append(f'bar {key!r} at {x},{y} [{text.strip()}] lands on [{under.strip()}]')
    # the art's own menu items ("[M] Messages", "(M) Messages", "M) Messages") that no bar covers
    covered = {(y - 1, c) for x, y, key, text in bars for c in range(x - 1, x - 1 + len(text))}
    for r, row in enumerate(rows):
        for m in re.finditer(r'[\[(<]([A-Z0-9*!#?+])[\])>]\s*[A-Za-z]', row):
            if (r, m.start(1)) not in covered:
                probs.append(f'art item {m.group(0)[:3]!r} at {m.start() + 1},{r + 1} has no lightbar: [{row[m.start():m.start() + 24].rstrip()}]')
    return probs, lines if show else []


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    show = '--show' in sys.argv
    themes = args[0] if args else os.path.join(ROOT, 'local', 'sdstage', 'themes')
    menus = args[1] if len(args) > 1 else os.path.join(ROOT, 'art', 'menus')
    mnus = {f[:-4] + '.ANS': os.path.join(menus, f) for f in os.listdir(menus) if f.upper().endswith('.MNU')}
    looks = sorted(os.listdir(themes)) if os.path.isdir(themes) else []  # (no theme folder, as on CI: the menu set only)
    sets = [('menus', menus, None)] + [(t, os.path.join(themes, t), t) for t in looks]
    bad = 0
    for label, folder, theme in sets:
        print(f'== {label}')
        names = sorted(SCREEN) if theme else sorted(set(SCREEN) | set(mnus))  # (a theme draws only SCREEN's)
        for art_name in names:
            fname = SCREEN[art_name] if theme else art_name
            path = os.path.join(folder, fname)
            if not os.path.exists(path):
                print(f'  {fname:13} missing')
                continue
            raw, size = read_art(path)
            t = render(raw)
            probs = []
            cap = THEME_CAP if theme else MENU_CAP
            if size > cap:
                probs.append(f'{size} bytes, over the {cap} byte cap')
            if t.scrolled:
                probs.append(f'scrolls {t.scrolled} line(s)')
            unknown = sorted(c for c in codes(raw) if not known(c))
            if unknown:
                probs.append('unknown codes ' + ' '.join(unknown))
            if '\x1b[2J' not in raw and '|CL' not in raw and not raw.startswith('\x1b[0m\x1b[2J'):
                pass  # clr=1 in the .MNU clears first, so a clear is not required
            lines = []
            if art_name == 'LOGIN.ANS':
                probs += check_login(t)
            if art_name in mnus:
                bars = parse_bars(mnus[art_name])
                if not bars:
                    probs.append('its .MNU has no lightbar')
                bp, lines = check_bars(t, bars, show)
                probs += bp
            status = 'ok' if not probs else 'FAIL'
            bad += bool(probs)
            print(f'  {fname:13} {size:6} B  codes {",".join(sorted(codes(raw))) or "-":20} {status}')
            for p in probs:
                print('      ' + p)
            for ln in lines:
                print(ln)
    print('== menu scripts')
    for a, p in sorted(mnus.items()):
        m = parse_menu(p)
        probs = check_script(m) + ([] if m['bars'] else ['no lightbar'])
        bad += bool(probs)
        print(f'  {os.path.basename(p):13} {len(m["bars"]):2} bars  {"ok" if not probs else "FAIL"}')
        for q in probs:
            print('      ' + q)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
