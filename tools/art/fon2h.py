"""Pull the 8x8 CP437 bitmap out of a Windows .FON (FNT resource) and write a C header."""
import struct
import sys

data = open(sys.argv[1], 'rb').read()
# find an FNT header: version 0x0200 or 0x0300, then dfSize, and a pixel height of 8
for off in range(len(data) - 118):
    ver = struct.unpack_from('<H', data, off)[0]
    if ver not in (0x200, 0x300):
        continue
    size = struct.unpack_from('<I', data, off + 2)[0]
    if not 200 < size <= len(data) - off:
        continue
    pixw, pixh = struct.unpack_from('<HH', data, off + 86)
    first, last = data[off + 95], data[off + 96]
    if pixh == 8 and pixw in (0, 8) and first == 0 and last == 255 and struct.unpack_from('<H', data, off + 118)[0] == 8:
        break
else:
    sys.exit('no 8x8 FNT found')
fnt = data[off:off + size]
table = 148 if ver == 0x300 else 118
glyphs = []
for ch in range(256):
    if ver == 0x300:
        w, o = struct.unpack_from('<HI', fnt, table + ch * 6)
    else:
        w, o = struct.unpack_from('<HH', fnt, table + ch * 4)
    assert w == 8, (ch, w)
    glyphs.append(list(fnt[o:o + 8]))
with open(sys.argv[2], 'w', newline='\n') as f:
    f.write('// IBM PC BIOS 8x8 font (CP437), from the Ultimate Oldschool PC Font Pack v2.2 by VileR\n')
    f.write('// (int10h.org), CC BY-SA 4.0. 2 KB: 256 glyphs, 8 rows each, bit 7 is the leftmost pixel.\n')
    f.write('#pragma once\n#include <stdint.h>\nstatic const uint8_t ibm_font8x8[2048] = {\n')
    for ch, g in enumerate(glyphs):
        f.write('  ' + ','.join(str(b) for b in g) + ',\n')
    f.write('};\n')
print('ok', hex(off), 'v%x' % ver)
