# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Call the BBS over telnet, play a scripted session, and save what the caller sees as PNGs.

A small ANSI/CP437 terminal (80x25, IBM VGA font) runs inside this script, so every screenshot
shows the screen the way a real BBS terminal would after that step.

usage: python bbs_shots.py HOST[:PORT] OUTDIR [SCRIPT.py] [--no-busy]
       SCRIPT.py defines SCRIPT = [(name, keys bytes), ...]; default: login, info, credits, goodbye
"""
import os
import re
import socket
import sys
import time

from PIL import Image, ImageDraw, ImageFont

COLS, ROWS = 80, 25
PAL = [(0, 0, 0), (170, 0, 0), (0, 170, 0), (170, 85, 0), (0, 0, 170), (170, 0, 170), (0, 170, 170), (170, 170, 170),
       (85, 85, 85), (255, 85, 85), (85, 255, 85), (255, 255, 85), (85, 85, 255), (255, 85, 255), (85, 255, 255),
       (255, 255, 255)]


class Term:
    """80x25 ANSI terminal: SGR colours (bold = bright), cursor moves (H f A B C D), save and
    restore (s u), erase (J K), CP437 glyphs drawn with the IBM VGA 8x16 font."""

    def __init__(self):
        self.fg, self.bg, self.bold = 7, 0, False
        self.esc = None
        self.saved = (0, 0)
        self.reset_screen()

    def text(self):
        return '\n'.join(''.join(c[0] for c in row) for row in self.cells)

    def blank(self):
        return (' ', 7, self.bg)

    def reset_screen(self):
        self.cells = [[(' ', 7, 0) for _ in range(COLS)] for _ in range(ROWS)]
        self.x = self.y = 0

    def scroll(self):
        self.cells.pop(0)
        self.cells.append([(' ', 7, 0) for _ in range(COLS)])
        self.y = ROWS - 1

    def sgr(self, params):
        for p in params or [0]:
            if p == 0:
                self.fg, self.bg, self.bold = 7, 0, False
            elif p == 1:
                self.bold = True
            elif p == 22:
                self.bold = False
            elif 30 <= p <= 37:
                self.fg = p - 30
            elif p == 39:
                self.fg = 7
            elif 40 <= p <= 47:
                self.bg = p - 40
            elif p == 49:
                self.bg = 0

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
        elif cmd == 's':
            self.saved = (self.x, self.y)
        elif cmd == 'u':
            self.x, self.y = self.saved
        elif cmd == 'J':
            mode = nums[0] if nums else 0
            if mode == 2:
                self.reset_screen()
            elif mode == 0:
                for i in range(self.x, COLS):
                    self.cells[self.y][i] = self.blank()
                for r in range(self.y + 1, ROWS):
                    self.cells[r] = [self.blank() for _ in range(COLS)]
            elif mode == 1:
                for r in range(self.y):
                    self.cells[r] = [self.blank() for _ in range(COLS)]
                for i in range(self.x + 1):
                    self.cells[self.y][i] = self.blank()
        elif cmd == 'K':
            mode = nums[0] if nums else 0
            rng = range(self.x, COLS) if mode == 0 else range(self.x + 1) if mode == 1 else range(COLS)
            for i in rng:
                self.cells[self.y][i] = self.blank()

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
                    self.scroll()
            elif ch == '\b':
                self.x = max(0, self.x - 1)
            elif ch == '\x0c':
                self.reset_screen()
            elif ch not in '\x00\x07':
                if self.x >= COLS:
                    self.x = 0
                    self.y += 1
                    if self.y >= ROWS:
                        self.scroll()
                fg = self.fg + (8 if self.bold else 0)
                self.cells[self.y][self.x] = (ch, fg, self.bg)
                self.x += 1

    def png(self, path, title):
        font = vga_font()
        top = 24
        img = Image.new('RGB', (COLS * 8, ROWS * 16 + top), (20, 20, 20))
        px = img.load()
        for r, row in enumerate(self.cells):
            for c, (g, fg, bg) in enumerate(row):
                code = g.encode('cp437', 'replace')[0]
                x0, y0 = c * 8, top + r * 16
                for y in range(16):
                    bits = font[code * 16 + y]
                    for x in range(8):
                        px[x0 + x, y0 + y] = PAL[fg] if bits & (0x80 >> x) else PAL[bg]
        d = ImageDraw.Draw(img)
        d.text((6, 4), title, font=ImageFont.truetype('consola.ttf', 14), fill=(200, 200, 200))
        if 0 <= self.y < ROWS and self.x < COLS:  # cursor
            d.rectangle([self.x * 8, top + self.y * 16 + 13, self.x * 8 + 7, top + self.y * 16 + 15], fill=(200, 200, 200))
        img.save(path)


_FONT = []


def vga_font():  # the IBM VGA 8x16 font (third_party/vga_font), as b800png.py uses
    if not _FONT:
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'third_party', 'vga_font', 'font8x16.h')
        _FONT.extend(int(n) for n in re.findall(r'\b\d+\b', open(path).read().split('{', 1)[1])[:4096])
    return _FONT


def strip_telnet(data):
    return re.sub(rb'\xff[\xfb-\xfe].|\xff\xfa.*?\xff\xf0', b'', data, flags=re.S).replace(b'\xff\xff', b'\xff')


def read(sock, secs):
    sock.settimeout(0.2)
    buf, end = b'', time.time() + secs
    while time.time() < end:
        try:
            d = sock.recv(4096)
            if not d:
                break
            buf += d
        except socket.timeout:
            pass
    return buf


SCRIPT = [('01-welcome', b''), ('02-logged-in', b'Rev\r\n'), ('03-info', b'i'), ('04-credits', b'c'),
          ('05-goodbye', b'g')]


def main():
    host, out = sys.argv[1], sys.argv[2]
    port = 23
    if ':' in host:
        host, port = host.rsplit(':', 1)
        port = int(port)
    script = SCRIPT
    if len(sys.argv) > 3 and sys.argv[3].endswith('.py'):
        ns = {}
        exec(open(sys.argv[3]).read(), ns)
        script = ns['SCRIPT']
    os.makedirs(out, exist_ok=True)
    term = Term()
    s = socket.create_connection((host, port), 5)
    # a step is (name, keys), (name, keys, seconds) or (name, keys, seconds, regex): with a regex,
    # wait up to that long for it to show on screen, then let the screen settle for a second
    for step in script:
        name, keys = step[0], step[1]
        secs = step[2] if len(step) > 2 else 1.5
        if keys:
            s.sendall(keys)
        if len(step) > 3:
            end, seen = time.time() + secs, False
            while time.time() < end and not seen:
                term.feed(strip_telnet(read(s, 0.5)).decode('cp437'))
                seen = re.search(step[3], term.text(), re.I) is not None
            term.feed(strip_telnet(read(s, 1.0)).decode('cp437'))
            if not seen:
                print('MISS', name, step[3])
        else:
            term.feed(strip_telnet(read(s, secs)).decode('cp437'))
        path = os.path.join(out, name + '.png')
        term.png(path, f'AXIS UNDERGROUND CNC  telnet {host}  step: {name}')
        print(path)
    s.close()
    if '--no-busy' in sys.argv:
        return
    time.sleep(0.5)
    first = socket.create_connection((host, port), 5)  # holds the node
    read(first, 1.0)
    second = socket.create_connection((host, port), 5)
    busy = Term()
    busy.feed(strip_telnet(read(second, 1.5)).decode('cp437'))
    path = os.path.join(out, '06-second-caller-busy.png')
    busy.png(path, f'AXIS UNDERGROUND CNC  telnet {host}  step: second caller while node 1 is busy')
    print(path)
    second.close()
    first.close()


if __name__ == '__main__':
    main()
