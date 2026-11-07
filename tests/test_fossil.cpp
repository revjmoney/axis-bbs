// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "door/bios.h"
#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/fossil.h"
#include "door/guestmem.h"

using namespace axis::door;

namespace {
struct Rig {
    PagePool     pool{256 * 1024};
    GuestMemory  mem{pool};
    Cpu          cpu{mem};
    Bios         bios{cpu, mem};
    ScriptStream line;
    Fossil       fossil{cpu, mem, line};
    Rig() {
        mem.mapRam(0, 0x2000);
        mem.mapZero(0x2000, 0x9E000);
        bios.install();
        cpu.reset(0x0200, 0x0010, 0x0200, 0xFFFE, 0x0200, 0x0200);
    }
    void call(uint16_t ax, uint16_t dx = 0) {
        cpu.setAx(ax);
        cpu.setDx(dx);
        fossil.int14();
    }
};
}  // namespace

TEST_CASE("fossil init returns 1954h, highest function 1Bh, revision 5") {
    Rig r;
    r.call(0x0400, 0);
    CHECK(r.cpu.ax() == 0x1954);
    CHECK(r.cpu.bl() == 0x1B);
    CHECK(r.cpu.bh() == 5);
    r.call(0x1C00, 0);  // X00's extended init answers the same
    CHECK(r.cpu.ax() == 0x1954);
}

TEST_CASE("fossil transmit with wait writes to the line and returns status") {
    Rig r;
    r.call(0x0141);
    r.call(0x0142);
    CHECK(r.line.output() == "AB");
    CHECK((r.cpu.ah() & 0x20) != 0);  // room in the output buffer
}

TEST_CASE("fossil status shows data ready, output room and carrier") {
    Rig r;
    r.call(0x0300);
    CHECK((r.cpu.ah() & 0x01) == 0);
    CHECK((r.cpu.ah() & 0x60) == 0x60);
    CHECK((r.cpu.al() & 0x80) != 0);
    r.line.push("x");
    r.call(0x0300);
    CHECK((r.cpu.ah() & 0x01) != 0);
}

TEST_CASE("fossil status drops carrier after the caller hangs up") {
    Rig r;
    r.line.hangUp();
    r.call(0x0300);
    CHECK((r.cpu.al() & 0x80) == 0);
}

TEST_CASE("fossil status poll does not flush, and a waiting receive does") {
    struct Line : ScriptStream {
        int flushes = 0;
        void flush() override { flushes++; }
    } line;
    PagePool    pool{256 * 1024};
    GuestMemory mem{pool};
    Cpu         cpu{mem};
    Bios        bios{cpu, mem};
    Fossil      fossil{cpu, mem, line};
    mem.mapRam(0, 0x2000);
    mem.mapZero(0x2000, 0x9E000);
    bios.install();
    cpu.reset(0x0200, 0x0010, 0x0200, 0xFFFE, 0x0200, 0x0200);
    cpu.setAx(0x0141);
    fossil.int14();
    cpu.setAx(0x0300);
    fossil.int14();
    CHECK(line.flushes == 0);
    uint16_t ip = cpu.ip();
    cpu.setAx(0x0200);
    fossil.int14();
    CHECK(fossil.waitingForInput());
    CHECK(cpu.ip() == (uint16_t)(ip - 2));
    CHECK(line.flushes == 1);
}

TEST_CASE("fossil receive with wait returns a pushed byte, or waits by rewinding") {
    Rig r;
    uint16_t ip = r.cpu.ip();
    r.call(0x0200);
    CHECK(r.fossil.waitingForInput());
    CHECK(r.cpu.ip() == (uint16_t)(ip - 2));
    r.line.push("q");
    r.cpu.setIp(ip);
    r.call(0x0200);
    CHECK(r.cpu.ax() == 'q');
}

TEST_CASE("fossil peek does not take the byte") {
    Rig r;
    r.call(0x0C00);
    CHECK(r.cpu.ax() == 0xFFFF);
    r.line.push("z");
    r.call(0x0C00);
    CHECK(r.cpu.ax() == 'z');
    r.call(0x0200);
    CHECK(r.cpu.al() == 'z');
}

TEST_CASE("fossil block read and write") {
    Rig r;
    r.line.push("hello");
    r.cpu.setEs(0x0200);
    r.cpu.setDi(0x0100);
    r.cpu.setCx(10);
    r.call(0x1800);
    CHECK(r.cpu.ax() == 5);
    CHECK(r.mem.read8(0x2100) == 'h');
    r.mem.writeBlock(0x2200, "WORLD", 5);
    r.cpu.setDi(0x0200);
    r.cpu.setCx(5);
    r.call(0x1900);
    CHECK(r.cpu.ax() == 5);
    CHECK(r.line.output() == "WORLD");
}

TEST_CASE("fossil driver info structure") {
    Rig r;
    r.cpu.setEs(0x0200);
    r.cpu.setDi(0x0300);
    r.cpu.setCx(19);
    r.call(0x1B00);
    CHECK(r.cpu.ax() == 19);
    uint32_t a = 0x2300;
    CHECK(r.mem.read16(a + 0x00) == 19);
    CHECK(r.mem.read8(a + 0x02) == 5);
    uint32_t id = r.mem.read16(a + 0x06) * 16u + r.mem.read16(a + 0x04);
    CHECK(r.mem.read8(id) == 'A');
    CHECK(r.mem.read8(a + 0x10) == 80);
    CHECK(r.mem.read8(a + 0x11) == 25);
}

TEST_CASE("fossil handler signature 1954h sits at the INT 14h vector + 6") {
    Rig r;
    uint32_t h = r.mem.read16(0x14 * 4 + 2) * 16u + r.mem.read16(0x14 * 4);
    CHECK(r.mem.read16(h + 6) == 0x1954);
    CHECK(r.mem.read8(h + 8) == 0x1B);
    CHECK(r.bios.isStub(0x14));
}
