// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "doctest.h"
#include "door/dosmem.h"
#include "door/guestmem.h"
#include "door/mz.h"

using namespace axis::door;

// A tiny EXE: 32-byte header, one relocation at image offset 1 (the segment in "mov ax,0000h"),
// code: mov ax,0000h (relocated) ; ret ; plus 5 bytes. SS:SP = 0001:0100, CS:IP = 0000:0000.
static std::vector<uint8_t> tinyExe(uint16_t minAlloc = 0x10) {
    std::vector<uint8_t> f(32, 0);
    std::vector<uint8_t> code = {0xB8, 0x00, 0x00, 0xC3, 1, 2, 3, 4, 5, 6};
    uint32_t             total = 32 + code.size();
    auto w = [&](int at, uint16_t v) {
        f[at] = (uint8_t)v;
        f[at + 1] = v >> 8;
    };
    f[0] = 'M';
    f[1] = 'Z';
    w(2, total % 512);           // bytes in the last page
    w(4, (total + 511) / 512);   // pages
    w(6, 1);                     // relocations
    w(8, 2);                     // header paragraphs
    w(10, minAlloc);             // min alloc
    w(12, 0xFFFF);               // max alloc
    w(14, 0x0001);               // SS
    w(16, 0x0100);               // SP
    w(20, 0x0000);               // IP
    w(22, 0x0000);               // CS
    w(24, 28);                   // relocation table offset
    w(28, 0x0001);               // reloc: offset 1
    w(30, 0x0000);               //        segment 0
    f.insert(f.end(), code.begin(), code.end());
    return f;
}

TEST_CASE("parses a tiny hand-built MZ") {
    auto   f = tinyExe();
    MzInfo mz;
    REQUIRE(parseMz(f.data(), f.size(), mz));
    CHECK(mz.imageSize == 10);
    CHECK(mz.headerSize == 32);
    CHECK(mz.relocCount == 1);
    CHECK(mz.minAlloc == 0x10 * 16);
    CHECK(mz.ss == 1);
    CHECK(mz.sp == 0x100);
}

TEST_CASE("rejects files that are not MZ") {
    std::vector<uint8_t> junk(64, 0x41);
    MzInfo               mz;
    CHECK_FALSE(parseMz(junk.data(), junk.size(), mz));
}

TEST_CASE("applies relocations at the load segment") {
    auto   f = tinyExe();
    MzInfo mz;
    parseMz(f.data(), f.size(), mz);
    auto img = relocate(f.data(), f.size(), mz, kLoadSeg);
    CHECK(img.size() % kPageSize == 0);
    CHECK((img[1] | img[2] << 8) == kLoadSeg);
    CHECK(img[3] == 0xC3);
}

TEST_CASE("mcb alloc resize free") {
    PagePool    pool(256 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0, 0xA0000);
    DosMemory dm(mem, kPspSeg - 1, kTopSeg);
    uint16_t  seg = 0, largest = 0;
    REQUIRE(dm.alloc(0x100, 0x1234, seg, largest));
    CHECK(seg == kPspSeg);
    uint16_t seg2 = 0;
    REQUIRE(dm.alloc(0x50, 0x1234, seg2, largest));
    CHECK(seg2 == kPspSeg + 0x101);
    CHECK(dm.resize(seg, 0x80, largest));          // shrink
    CHECK_FALSE(dm.resize(seg, 0x200, largest));   // the second block is in the way
    CHECK(largest == 0x100);                       // 0x80 + the freed 0x7F + its MCB
    CHECK(dm.free(seg2));
    uint16_t big = 0;
    CHECK_FALSE(dm.alloc(0xFFFF, 1, big, largest));
    CHECK(largest == kTopSeg - kPspSeg - 0x80 - 1);
    CHECK_FALSE(dm.free(0x5000));                  // not a block
}

TEST_CASE("PSP has the command tail, the environment and the program's registers") {
    PagePool    pool(256 * 1024);
    GuestMemory mem(pool);
    auto        f = tinyExe();
    MzInfo      mz;
    parseMz(f.data(), f.size(), mz);
    auto            img = relocate(f.data(), f.size(), mz, kLoadSeg);
    DosMemory*      dm = nullptr;
    LoadedProgram   lp;
    REQUIRE(loadProgram(mem, dm, img, mz, " 0 NOEMS /DREW", "C:\\LORD.EXE", lp));
    uint32_t psp = kPspSeg * 16;
    CHECK(mem.read8(psp) == 0xCD);
    CHECK(mem.read8(psp + 1) == 0x20);
    CHECK(mem.read8(psp + 0x80) == 14);
    CHECK(mem.read8(psp + 0x81) == ' ');
    CHECK(mem.read8(psp + 0x81 + 14) == '\r');
    uint16_t env = mem.read16(psp + 0x2C);
    std::string e;
    for (uint32_t a = env * 16; e.size() < 200; a++) e += (char)mem.read8(a);
    CHECK(e.find("PATH=C:\\") == 0);
    CHECK(e.find("C:\\LORD.EXE") != std::string::npos);
    CHECK(lp.cs == kLoadSeg);
    CHECK(lp.ip == 0);
    CHECK(lp.ss == kLoadSeg + 1);
    CHECK(lp.sp == 0x100);
    CHECK(lp.psp == kPspSeg);
    CHECK(mem.read16(psp + 2) > kLoadSeg);  // top of the program's memory block
    CHECK((mem.read8(kLoadSeg * 16 + 1) | mem.read8(kLoadSeg * 16 + 2) << 8) == kLoadSeg);
    CHECK(mem.stats().roPages >= 1);         // the code is served read-only
    delete dm;
}

TEST_CASE("LORD header values") {
    const char* dir = std::getenv("AXIS_LORD_DIR");
    if (!dir) return;  // skipped without the LORD folder
    std::ifstream        in(std::string(dir) + "/LORD.EXE", std::ios::binary);
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), {});
    MzInfo               mz;
    REQUIRE(parseMz(f.data(), f.size(), mz));
    CHECK(mz.imageSize == 348368);
    CHECK(mz.minAlloc == 129344);
    CHECK(mz.relocCount == 14988);
}

// relocateChunk builds the same image as relocate(), one chunk at a time
static std::vector<uint8_t> chunked(const std::vector<uint8_t>& f, const MzInfo& mz, uint16_t seg, size_t chunk) {
    size_t                len = (mz.imageSize + kPageSize - 1) / kPageSize * kPageSize;
    std::vector<uint32_t> relocs = relocTargets(f.data() + mz.relocTable, mz.relocCount, len);
    std::vector<uint8_t>  out;
    auto byteAt = [&](uint32_t at) -> uint8_t { return at < mz.imageSize ? f[mz.headerSize + at] : 0; };
    const uint32_t window = 16384;  // as the board's flash writer: only relocations touching this window
    for (uint32_t base = 0; base < len; base += chunk) {
        uint32_t              wbase = base / window * window;
        std::vector<uint32_t> near;
        for (uint32_t at : relocs)
            if (at + 1 >= wbase && at < wbase + window) near.push_back(at);
        std::vector<uint8_t> c(chunk, 0);
        for (uint32_t i = 0; i < chunk && base + i < mz.imageSize; i++) c[i] = f[mz.headerSize + base + i];
        relocateChunk(c.data(), base, chunk, near, seg, byteAt);
        out.insert(out.end(), c.begin(), c.end());
    }
    return out;
}

TEST_CASE("chunked relocation matches relocate on a tiny EXE") {
    auto   f = tinyExe();
    MzInfo mz;
    REQUIRE(parseMz(f.data(), f.size(), mz));
    CHECK(chunked(f, mz, kLoadSeg, kPageSize) == relocate(f.data(), f.size(), mz, kLoadSeg));
}

TEST_CASE("chunked relocation handles a word split across chunks") {
    auto f = tinyExe();
    f[32 + 2] = 0x12;  // the relocated word's high byte, in the second 2-byte chunk
    MzInfo mz;
    REQUIRE(parseMz(f.data(), f.size(), mz));
    auto whole = relocate(f.data(), f.size(), mz, kLoadSeg);
    auto parts = chunked(f, mz, kLoadSeg, 2);
    whole.resize(parts.size());
    CHECK(parts == whole);
}

TEST_CASE("chunked relocation matches relocate on LORD.EXE") {
    const char* dir = std::getenv("AXIS_LORD_DIR");
    if (!dir) return;
    std::ifstream        in(std::string(dir) + "/LORD.EXE", std::ios::binary);
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), {});
    MzInfo               mz;
    REQUIRE(parseMz(f.data(), f.size(), mz));
    CHECK(chunked(f, mz, kLoadSeg, 4096) == relocate(f.data(), f.size(), mz, kLoadSeg));
}
