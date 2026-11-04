# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Build the website's VGA webfont from the board's own 8x16 bitmap (third_party/vga_font/font8x16.h).

usage: python tools/art/make_vga_webfont.py site/fonts

Every lit pixel becomes a square in the outline (64 units a pixel, 1024 units to the em), so the font
is crisp at 16 px and 32 px. CP437 glyphs are mapped to their Unicode code points, box drawing and the
shade blocks included. The glyphs are the IBM VGA 8x16 set as packaged by VileR (int10h.org), CC BY-SA 4.0,
so the webfont it writes is CC BY-SA 4.0 too. Needs fontTools and brotli (pip install fonttools brotli).
"""
import os
import re
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "third_party", "vga_font", "font8x16.h")
PX = 64  # units per pixel
ASC_ROWS = 12  # rows above the baseline

LOW = "\u0000☺☻♥♦♣♠•◘○◙♂♀♪♫☼" \
      "►◄↕‼¶§▬↨↑↓→←∟↔▲▼"


def cp437(i):
    if i < 32:
        return LOW[i]
    if i == 127:
        return "⌂"
    return bytes([i]).decode("cp437")


def main(outdir):
    text = open(SRC, encoding="utf-8").read()
    nums = [int(n) for n in re.findall(r"\b\d+\b", text[text.index("{"):])]
    assert len(nums) >= 4096, len(nums)
    rows = [nums[i * 16:(i + 1) * 16] for i in range(256)]

    names, glyphs, cmap, metrics = [".notdef"], {}, {}, {}
    pen = TTGlyphPen(None)
    glyphs[".notdef"] = pen.glyph()
    metrics[".notdef"] = (8 * PX, 0)
    for code in range(1, 256):
        ch = cp437(code)
        name = "uni%04X" % ord(ch)
        if name in glyphs:
            continue
        pen = TTGlyphPen(None)
        for r, bits in enumerate(rows[code]):
            x = 0
            while x < 8:
                if bits & (0x80 >> x):
                    x0 = x
                    while x < 8 and bits & (0x80 >> x):
                        x += 1
                    y0, y1 = (ASC_ROWS - r - 1) * PX, (ASC_ROWS - r) * PX
                    pen.moveTo((x0 * PX, y0))
                    pen.lineTo((x0 * PX, y1))
                    pen.lineTo((x * PX, y1))
                    pen.lineTo((x * PX, y0))
                    pen.closePath()
                else:
                    x += 1
        glyphs[name] = pen.glyph()
        names.append(name)
        cmap[ord(ch)] = name
        metrics[name] = (8 * PX, 0)
    cmap[0xA0] = cmap[0xFF] if 0xFF in cmap else "uni00A0"  # (CP437 0xFF is the no-break space)

    fb = FontBuilder(16 * PX, isTTF=True)
    fb.setupGlyphOrder(names)
    fb.setupCharacterMap(cmap)
    fb.setupGlyf(glyphs)
    fb.setupHorizontalMetrics({n: (metrics[n][0], 0) for n in names})
    fb.setupHorizontalHeader(ascent=ASC_ROWS * PX, descent=-(16 - ASC_ROWS) * PX)
    fb.setupNameTable({"familyName": "AxisVGA", "styleName": "Regular",
                       "copyright": "IBM VGA 8x16 glyphs as packaged by VileR (int10h.org), CC BY-SA 4.0. Webfont build by Rev. J. Money.",
                       "licenseDescription": "Creative Commons Attribution-ShareAlike 4.0"})
    fb.setupOS2(sTypoAscender=ASC_ROWS * PX, sTypoDescender=-(16 - ASC_ROWS) * PX, sTypoLineGap=0,
                usWinAscent=ASC_ROWS * PX, usWinDescent=(16 - ASC_ROWS) * PX, xAvgCharWidth=8 * PX)
    fb.setupPost(isFixedPitch=1)
    os.makedirs(outdir, exist_ok=True)
    ttf = os.path.join(outdir, "AxisVGA.ttf")
    fb.save(ttf)
    fb.font.flavor = "woff2"
    fb.save(os.path.join(outdir, "AxisVGA.woff2"))
    os.remove(ttf)
    print("glyphs", len(names), "->", os.path.join(outdir, "AxisVGA.woff2"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "site/fonts")
