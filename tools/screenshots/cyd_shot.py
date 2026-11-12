# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Rev. J. Money
"""Save the CYD's own screen as a PNG (the board sends its off-screen copy over serial).

usage: python cyd_shot.py COMxx out.png ["@TAP x y" ...]   (optional serial commands first)
"""
import sys
import time

import serial
from PIL import Image


def rgb332(v):
    r, g, b = (v >> 5) & 7, (v >> 2) & 7, v & 3
    return (r * 255 // 7, g * 255 // 7, b * 255 // 3)


def main():
    port, out, cmds = sys.argv[1], sys.argv[2], sys.argv[3:]
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = s.rts = False  # opening must not reset the board
    s.open()
    for c in cmds:
        s.write((c + '\n').encode())
        time.sleep(1.0)
    s.reset_input_buffer()
    s.write(b'@SHOT\n')
    w = h = 0
    rows = []
    end = time.time() + 40
    while time.time() < end:
        line = s.readline().decode('latin1').strip()
        if line.startswith('@SHOT END'):
            break
        if line.startswith('@SHOT NONE'):  # drawn straight to the panel (no room for the copy), or a headless board
            s.close()
            sys.exit('no screenshot: the board has no copy of this screen')
        if line.startswith('@SHOT '):
            w, h = map(int, line.split()[1:3])
            rows = []
        elif w and len(line) == w * 2:
            rows.append(line)
    s.close()
    if not w or len(rows) != h:
        sys.exit(f'incomplete screenshot: {len(rows)} of {h} rows')
    img = Image.new('RGB', (w, h))
    img.putdata([rgb332(int(r[i:i + 2], 16)) for r in rows for i in range(0, w * 2, 2)])
    img = img.resize((w * 2, h * 2), Image.NEAREST)
    img.save(out)
    print(out)


if __name__ == '__main__':
    main()
