# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Call the BBS over telnet, play a scripted session, and save what the caller sees as PNGs.

A small ANSI/CP437 terminal (80x25) runs inside this script, so every screenshot shows the
screen exactly as a real terminal would after that step.

usage: python bbs_shots.py HOST OUTDIR            (default script: login, info, credits, goodbye)
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
    def __init__(self):
        self.reset_screen()
        self.fg, self.bg, self.bold = 7, 0, False
        self.esc = None

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
            elif 30 <= p <= 37:
                self.fg = p - 30
            elif 40 <= p <= 47:
                self.bg = p - 40

    def feed(self, text):
        for ch in text:
            if self.esc is not None:
                self.esc += ch
                if self.esc == '[' or (len(self.esc) > 1 and not ch.isalpha()):
                    continue
                if not self.esc.startswith('['):
                    self.esc = None
                    continue
                cmd, args = self.esc[-1], self.esc[1:-1]
                nums = [int(a) if a.isdigit() else 0 for a in args.split(';')] if args else []
                self.esc = None
                if cmd == 'm':
                    self.sgr(nums)
                elif cmd == 'J' and (nums or [0])[0] == 2:
                    self.reset_screen()
                elif cmd in 'Hf':
                    self.y = max(0, min(ROWS - 1, (nums[0] if nums else 1) - 1))
                    self.x = max(0, min(COLS - 1, (nums[1] if len(nums) > 1 else 1) - 1))
                elif cmd == 'K':
                    for i in range(self.x, COLS):
                        self.cells[self.y][i] = (' ', 7, self.bg)
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
            elif ch >= ' ':
                if self.x >= COLS:
                    self.x = 0
                    self.y += 1
                    if self.y >= ROWS:
                        self.scroll()
                fg = self.fg + (8 if self.bold else 0)
                self.cells[self.y][self.x] = (ch, fg, self.bg)
                self.x += 1

    def png(self, path, title):
        font = ImageFont.truetype('consola.ttf', 16)
        cw, ch = 9, 18
        img = Image.new('RGB', (COLS * cw + 16, ROWS * ch + 40), (20, 20, 20))
        d = ImageDraw.Draw(img)
        d.text((8, 6), title, font=ImageFont.truetype('consola.ttf', 14), fill=(200, 200, 200))
        for r, row in enumerate(self.cells):
            for c, (g, fg, bg) in enumerate(row):
                x0, y0 = 8 + c * cw, 30 + r * ch
                if bg:
                    d.rectangle([x0, y0, x0 + cw - 1, y0 + ch - 1], fill=PAL[bg])
                if g != ' ':
                    d.text((x0, y0), g, font=font, fill=PAL[fg])
        if 0 <= self.y < ROWS and self.x < COLS:  # cursor
            d.rectangle([8 + self.x * cw, 30 + self.y * ch + ch - 3, 8 + self.x * cw + cw - 1, 30 + self.y * ch + ch - 1],
                        fill=(200, 200, 200))
        img.save(path)


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
    os.makedirs(out, exist_ok=True)
    term = Term()
    s = socket.create_connection((host, 23), 5)
    for name, keys in SCRIPT:
        if keys:
            s.sendall(keys)
        term.feed(strip_telnet(read(s, 1.5)).decode('cp437'))
        path = os.path.join(out, name + '.png')
        term.png(path, f'AXIS UNDERGROUND CNC  telnet {host}  step: {name}')
        print(path)
    s.close()
    time.sleep(0.5)
    first = socket.create_connection((host, 23), 5)  # holds the node
    read(first, 1.0)
    second = socket.create_connection((host, 23), 5)
    busy = Term()
    busy.feed(strip_telnet(read(second, 1.5)).decode('cp437'))
    path = os.path.join(out, '06-second-caller-busy.png')
    busy.png(path, f'AXIS UNDERGROUND CNC  telnet {host}  step: second caller while node 1 is busy')
    print(path)
    second.close()
    first.close()


if __name__ == '__main__':
    main()
