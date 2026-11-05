# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Turn ViSiON/3's menus into AXIS menu files (src/bbs/menufile.h): the MNU fields, the CFG commands and the
BAR lightbar items of each menu, one /menus/NAME.MNU per menu. No art is copied (AXIS draws its own).

usage: python -I tools/menus/v3import.py V3_MENU_DIR OUT_DIR [NAME ...]
       e.g. python -I tools/menus/v3import.py F:/vision-3-bbs/menus/v3 art/menus MAIN MSGMENU FILEM DOORSM
"""
import json
import os
import sys


def one(v3, name):
    with open(os.path.join(v3, 'mnu', name + '.MNU'), encoding='utf-8') as f:
        m = json.load(f)
    cmds = []
    cfg = os.path.join(v3, 'cfg', name + '.CFG')
    if os.path.exists(cfg):
        with open(cfg, encoding='utf-8') as f:
            cmds = json.load(f)
    bars = []
    bar = os.path.join(v3, 'bar', name + '.BAR')
    if os.path.exists(bar):
        with open(bar, encoding='latin-1') as f:
            for line in f:
                line = line.rstrip('\r\n')
                if line and not line.startswith(';'):
                    p = line.split(',', 6)  # X,Y,Hi,Reg,HotKey,ReturnValue,Text
                    if len(p) == 7:
                        bars.append('bar=%s,%s,%s,%s,%s,%s' % (p[0], p[1], p[2], p[3], p[4], p[6].strip()))
    flag = lambda b: '1' if b else '0'
    out = ['; from ViSiON/3 menus/v3 %s (MIT); AXIS art is its own' % name,
           'title=' + m.get('TITLE', ''), 'clr=' + flag(m.get('CLR', True)), 'prompt=' + flag(m.get('USEPROMPT', True)),
           'prompt1=' + m.get('PROMPT1', ''), 'prompt2=' + m.get('PROMPT2', ''), 'fallback=' + m.get('FALLBACK', ''),
           'acs=' + (m.get('ACS') or '*'), 'pass=' + m.get('PASS', ''), 'help=' + m.get('HELPMENU', ''),
           'hotkey=' + flag(m.get('FORCEHOTKEY', False))]
    for c in cmds:
        out.append('cmd=%s|%s|%s|%s|%s' % (c.get('KEYS', ''), c.get('CMD', ''), c.get('ACS', '*'), flag(c.get('HIDDEN', False)),
                                          c.get('NODE_ACTIVITY', '')))
    return '\n'.join(out + bars) + '\n'


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    v3, outdir = sys.argv[1], sys.argv[2]
    names = sys.argv[3:] or sorted(n[:-4] for n in os.listdir(os.path.join(v3, 'mnu')) if n.upper().endswith('.MNU'))
    os.makedirs(outdir, exist_ok=True)
    for n in names:
        with open(os.path.join(outdir, n + '.MNU'), 'w', encoding='latin-1', newline='\n') as f:
            f.write(one(v3, n))
        print(n)


if __name__ == '__main__':
    main()
