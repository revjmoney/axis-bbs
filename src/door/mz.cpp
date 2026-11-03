// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/mz.h"

#include <cstring>

namespace axis {
namespace door {

namespace {
uint16_t w16(const uint8_t* p, size_t at) { return p[at] | p[at + 1] << 8; }
}  // namespace

bool parseMz(const uint8_t* f, size_t len, MzInfo& out) {
    if (len < 28 || !((f[0] == 'M' && f[1] == 'Z') || (f[0] == 'Z' && f[1] == 'M'))) return false;
    uint16_t last = w16(f, 2), pages = w16(f, 4);
    out.headerSize = w16(f, 8) * 16u;
    uint32_t fileImage = (pages ? (pages - 1) * 512u : 0) + (last ? last : 512);
    if (pages == 0 || out.headerSize > fileImage || fileImage > len + 511) return false;
    out.imageSize = fileImage - out.headerSize;
    if (out.headerSize + out.imageSize > len) out.imageSize = (uint32_t)(len - out.headerSize);
    out.relocCount = w16(f, 6);
    out.minAlloc = w16(f, 10) * 16u;
    out.maxAlloc = w16(f, 12) * 16u;
    out.ss = w16(f, 14);
    out.sp = w16(f, 16);
    out.ip = w16(f, 20);
    out.cs = w16(f, 22);
    out.relocTable = w16(f, 24);
    return out.relocTable + out.relocCount * 4u <= len;
}

std::vector<uint8_t> relocate(const uint8_t* f, size_t, const MzInfo& mz, uint16_t loadSeg) {
    std::vector<uint8_t> img((mz.imageSize + kPageSize - 1) / kPageSize * kPageSize, 0);
    std::memcpy(img.data(), f + mz.headerSize, mz.imageSize);
    for (uint16_t i = 0; i < mz.relocCount; i++) {
        uint16_t off = w16(f, mz.relocTable + i * 4u), seg = w16(f, mz.relocTable + i * 4u + 2);
        uint32_t at = seg * 16u + off;
        if (at + 1 >= img.size()) continue;  // a bad entry: leave the image alone
        uint16_t v = (uint16_t)(img[at] | img[at + 1] << 8) + loadSeg;
        img[at] = (uint8_t)v;
        img[at + 1] = v >> 8;
    }
    return img;
}

bool loadProgram(GuestMemory& mem, DosMemory*& dm, const uint8_t* image, size_t imageLen, const MzInfo& mz,
                 const std::string& cmdTail, const std::string& progPath, LoadedProgram& out) {
    uint32_t loadAt = kLoadSeg * 16u;
    // low memory (IVT, BIOS data, DOS data, environment, MCB, PSP) is plain RAM
    mem.mapRam(0, loadAt);
    mem.mapReadOnly(loadAt, image, (uint32_t)imageLen);
    mem.mapZero(loadAt + (uint32_t)imageLen, kTopSeg * 16u - loadAt - (uint32_t)imageLen);

    // the program's block takes everything from the PSP up, as DOS gives it the largest block
    delete dm;
    dm = new DosMemory(mem, kPspSeg - 1, kTopSeg);
    uint16_t need = (uint16_t)((0x100 + mz.imageSize + mz.minAlloc + 15) / 16);
    uint16_t want = (uint32_t)need + mz.maxAlloc / 16 > 0xFFFF ? 0xFFFF : need + mz.maxAlloc / 16;
    uint16_t seg = 0, largest = 0;
    if (!dm->alloc(want, kPspSeg, seg, largest)) {
        if (largest < need || !dm->alloc(largest, kPspSeg, seg, largest)) return false;
    }
    uint16_t blockParas = mem.read16((seg - 1) * 16u + 3);

    // environment: PATH and COMSPEC, then the program's full path
    std::string env = std::string("PATH=C:\\") + '\0' + "COMSPEC=C:\\COMMAND.COM" + '\0' + '\0';
    env += '\x01';
    env += '\0';
    env += progPath;
    env += '\0';
    mem.writeBlock(kEnvSeg * 16u, env.data(), env.size());

    // PSP
    uint32_t psp = kPspSeg * 16u;
    uint8_t  zero[256] = {};
    mem.writeBlock(psp, zero, sizeof zero);
    mem.write8(psp, 0xCD);  // INT 20h
    mem.write8(psp + 1, 0x20);
    mem.write16(psp + 2, (uint16_t)(seg + blockParas));  // first segment past the program's memory
    mem.write16(psp + 0x16, kPspSeg);                    // parent: itself
    const uint8_t jft[20] = {0, 1, 2, 3, 4, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    mem.writeBlock(psp + 0x18, jft, sizeof jft);
    mem.write16(psp + 0x2C, kEnvSeg);
    mem.write16(psp + 0x32, 20);
    mem.write16(psp + 0x34, 0x18);
    mem.write16(psp + 0x36, kPspSeg);
    mem.write8(psp + 0x50, 0xCD);  // INT 21h; RETF
    mem.write8(psp + 0x51, 0x21);
    mem.write8(psp + 0x52, 0xCB);
    for (int i = 0; i < 11; i++) {  // blank FCBs
        mem.write8(psp + 0x5D + i, ' ');
        mem.write8(psp + 0x6D + i, ' ');
    }
    std::string tail = cmdTail.substr(0, 126);
    mem.write8(psp + 0x80, (uint8_t)tail.size());
    mem.writeBlock(psp + 0x81, tail.data(), tail.size());
    mem.write8(psp + 0x81 + (uint32_t)tail.size(), '\r');

    out.psp = kPspSeg;
    out.cs = kLoadSeg + mz.cs;
    out.ip = mz.ip;
    out.ss = kLoadSeg + mz.ss;
    out.sp = mz.sp;
    return !mem.outOfMemory();
}

}  // namespace door
}  // namespace axis
