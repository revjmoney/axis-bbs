// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <vector>

#include "doctest.h"
#include "door/guestmem.h"

using namespace axis::door;

TEST_CASE("zero page reads zero and allocates on first write") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0x10000, 0x10000);
    CHECK(mem.read8(0x12345) == 0);
    CHECK(mem.stats().ramPages == 0);
    mem.write8(0x12345, 0xAB);
    CHECK(mem.read8(0x12345) == 0xAB);
    CHECK(mem.stats().ramPages == 1);
}

TEST_CASE("read-only page copies on write and leaves the source alone") {
    std::vector<uint8_t> flash(kPageSize * 2, 0x11);
    PagePool             pool(64 * 1024);
    GuestMemory          mem(pool);
    mem.mapReadOnly(0x20000, flash.data(), kPageSize * 2);
    CHECK(mem.read8(0x20010) == 0x11);
    CHECK(mem.stats().roPages == 2);
    mem.write8(0x20010, 0x99);
    CHECK(mem.read8(0x20010) == 0x99);
    CHECK(mem.read8(0x20011) == 0x11);  // the rest of the page was copied
    CHECK(flash[0x10] == 0x11);
    CHECK(mem.stats().roPages == 1);
    CHECK(mem.stats().ramPages == 1);
}

TEST_CASE("word access across a page boundary") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0x10000, 0x2000);
    mem.write16(0x10FFF, 0xBEEF);
    CHECK(mem.read8(0x10FFF) == 0xEF);
    CHECK(mem.read8(0x11000) == 0xBE);
    CHECK(mem.read16(0x10FFF) == 0xBEEF);
    CHECK(mem.stats().ramPages == 2);
}

TEST_CASE("budget exhaustion sets outOfMemory and drops the write") {
    PagePool    pool(2 * kPageSize);
    GuestMemory mem(pool);
    mem.mapZero(0, 0x10000);
    mem.write8(0x0000, 1);
    mem.write8(0x1000, 2);
    CHECK_FALSE(mem.outOfMemory());
    mem.write8(0x2000, 3);
    CHECK(mem.outOfMemory());
    CHECK(mem.read8(0x2000) == 0);
    CHECK(mem.read8(0x1000) == 2);
}

TEST_CASE("peak tracks the most pages ever in use") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0, 0x10000);
    for (int p = 0; p < 5; p++) mem.write8(p * kPageSize, 1);
    CHECK(mem.stats().peakRamPages == 5);
    CHECK(pool.peakPages() == 5);
}

TEST_CASE("mapRam allocates now and unmapped memory reads FF and ignores writes") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapRam(0xB8000, kPageSize);
    CHECK(mem.stats().ramPages == 1);
    CHECK(mem.read8(0x90000) == 0xFF);
    mem.write8(0x90000, 5);
    CHECK(mem.read8(0x90000) == 0xFF);
    CHECK_FALSE(mem.outOfMemory());
}

TEST_CASE("block copies cross pages") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0, 0x10000);
    std::vector<uint8_t> src(6000);
    for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)i;
    mem.writeBlock(0x0F00, src.data(), src.size());
    std::vector<uint8_t> back(6000);
    mem.readBlock(0x0F00, back.data(), back.size());
    CHECK(back == src);
}

TEST_CASE("page pool hands out a lent arena first and never frees it") {
    std::vector<uint8_t> arena(3 * kPageSize + 100);  // room for 3 whole pages
    PagePool             pool(8 * kPageSize);
    pool.addArena(arena.data(), arena.size());
    uint8_t*             a = pool.alloc();
    uint8_t*             b = pool.alloc();
    uint8_t*             c = pool.alloc();
    uint8_t*             d = pool.alloc();  // from the heap
    CHECK(a == arena.data());
    CHECK(b == arena.data() + kPageSize);
    CHECK(c == arena.data() + 2 * kPageSize);
    CHECK((d < arena.data() || d >= arena.data() + arena.size()));
    pool.releaseAll();  // frees d only (the arena belongs to the screen)
    CHECK(pool.alloc() == arena.data());
}

TEST_CASE("arena pages count against the budget") {
    std::vector<uint8_t> arena(2 * kPageSize);
    PagePool             pool(2 * kPageSize);
    pool.addArena(arena.data(), arena.size());
    CHECK(pool.alloc() != nullptr);
    CHECK(pool.alloc() != nullptr);
    CHECK(pool.alloc() == nullptr);
}

TEST_CASE("page pool uses every lent arena before the heap") {
    std::vector<uint8_t> a(kPageSize), b(2 * kPageSize);
    PagePool             pool(8 * kPageSize);
    pool.addArena(a.data(), a.size());
    pool.addArena(b.data(), b.size());
    std::vector<uint8_t*> got;
    for (int i = 0; i < 4; i++) got.push_back(pool.alloc());
    int lent = 0;
    for (uint8_t* p : got) lent += (p == a.data() || p == b.data() || p == b.data() + kPageSize);
    CHECK(lent == 3);
}

TEST_CASE("guest bytes and words read back through word-only pages") {
    PagePool    pool(64 * 1024);
    GuestMemory mem(pool);
    mem.mapZero(0x20000, 0x2000);
    mem.write8(0x20001, 0xAB);
    mem.write16(0x20FFF, 0x1234);  // across a page boundary
    CHECK(mem.read8(0x20000) == 0);
    CHECK(mem.read8(0x20001) == 0xAB);
    CHECK(mem.read8(0x20002) == 0);
    CHECK(mem.read16(0x20FFF) == 0x1234);
    CHECK(mem.read8(0x21000) == 0x12);
}
