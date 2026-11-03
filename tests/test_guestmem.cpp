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
