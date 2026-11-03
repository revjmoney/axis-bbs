# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Render an 80x25 text page (char,attribute pairs, as doorhost --shots saves them) to a PNG
with the real IBM VGA 8x16 font and the CGA palette, the way the door looks on a PC.

usage: python -I tools/screenshots/b800png.py page.b800 out.png ["caption"]
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

CGA = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170), (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255), (255, 85, 85), (255, 85, 255), (255, 255, 85),
       (255, 255, 255)]


def load_font():
    path = os.path.join(os.path.dirname(__file__), '..', '..', 'third_party', 'vga_font', 'font8x16.h')
    nums = [int(n) for n in re.findall(r'\b\d+\b', open(path).read().split('{', 1)[1])]
    return nums[:4096]


def main(src, out, caption=None):
    font = load_font()
    page = open(src, 'rb').read()
    top = 24 if caption else 0
    img = Image.new('RGB', (640, 400 + top), (20, 20, 20))
    px = img.load()
    for i in range(2000):
        ch, attr = page[i * 2], page[i * 2 + 1]
        fg, bg = CGA[attr & 15], CGA[(attr >> 4) & 7]
        x0, y0 = (i % 80) * 8, (i // 80) * 16 + top
        for y in range(16):
            bits = font[ch * 16 + y]
            for x in range(8):
                px[x0 + x, y0 + y] = fg if bits & (0x80 >> x) else bg
    if caption:
        ImageDraw.Draw(img).text((6, 4), caption, fill=(200, 200, 200), font=ImageFont.truetype('consola.ttf', 14))
    img.save(out)
    print(out)


if __name__ == '__main__':
    main(*sys.argv[1:])
