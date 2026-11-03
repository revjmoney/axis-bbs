// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstring>
#include <vector>

#include "doctest.h"
#include "door/cpu.h"

using axis::door::Cpu;
using axis::door::MemoryBus;

namespace {
struct FlatBus : MemoryBus {
    std::vector<uint8_t> m = std::vector<uint8_t>(0x100000, 0);
    uint8_t  read8(uint32_t a) override { return m[a & 0xFFFFF]; }
    uint16_t read16(uint32_t a) override { return m[a & 0xFFFFF] | m[(a + 1) & 0xFFFFF] << 8; }
    void     write8(uint32_t a, uint8_t v) override { m[a & 0xFFFFF] = v; }
    void     write16(uint32_t a, uint16_t v) override {
        m[a & 0xFFFFF] = (uint8_t)v;
        m[(a + 1) & 0xFFFFF] = v >> 8;
    }
    void load(uint32_t at, std::initializer_list<uint8_t> bytes) {
        for (uint8_t b : bytes) m[at++] = b;
    }
};
}  // namespace

TEST_CASE("cpu runs mov/add/hlt") {
    FlatBus bus;
    bus.load(0x1000, {0xB8, 0x34, 0x12, 0x05, 0x01, 0x00, 0xF4});  // mov ax,1234h; add ax,1; hlt
    Cpu cpu(bus);
    cpu.reset(0x0100, 0x0000, 0x0200, 0xFFFE, 0x0100, 0x0100);
    cpu.run(100);
    CHECK(cpu.ax() == 0x1235);
    CHECK(cpu.halted());
}

TEST_CASE("int 21h reaches the C++ handler") {
    FlatBus bus;
    bus.load(0x1000, {0xB4, 0x30, 0xCD, 0x21, 0xF4});  // mov ah,30h; int 21h; hlt
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0xFFFE, 0x0100, 0x0100);
    int seen = -1;
    cpu.setIntHandler([&](int n) {
        seen = n;
        cpu.setAx(0x0005);
        return true;
    });
    cpu.run(100);
    CHECK(seen == 0x21);
    CHECK(cpu.ax() == 0x0005);
    CHECK(cpu.halted());
}

TEST_CASE("unhandled interrupt jumps through the IVT and IRET returns") {
    FlatBus bus;
    bus.load(0x60 * 4, {0x00, 0x00, 0x00, 0x20});            // IVT[60h] -> 2000:0000
    bus.load(0x20000, {0xBB, 0x07, 0x00, 0xCF});             // mov bx,7; iret
    bus.load(0x1000, {0xCD, 0x60, 0xB9, 0x09, 0x00, 0xF4});  // int 60h; mov cx,9; hlt
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0xFFFE, 0x0100, 0x0100);
    cpu.setIntHandler([](int) { return false; });
    cpu.run(100);
    CHECK(cpu.bx() == 7);
    CHECK(cpu.cx() == 9);
    CHECK(cpu.sp() == 0xFFFE);
}

TEST_CASE("memory writes and stack go through the bus") {
    FlatBus bus;
    // mov word [0010h],0BEEFh ; push word [0010h] ; pop dx ; hlt   (DS=0100h)
    bus.load(0x1000, {0xC7, 0x06, 0x10, 0x00, 0xEF, 0xBE, 0xFF, 0x36, 0x10, 0x00, 0x5A, 0xF4});
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0xFFFE, 0x0100, 0x0100);
    cpu.run(100);
    CHECK(bus.m[0x1010] == 0xEF);
    CHECK(bus.m[0x1011] == 0xBE);
    CHECK(cpu.dx() == 0xBEEF);
}

TEST_CASE("a call and ret near a segment edge wrap the offset") {
    FlatBus bus;
    bus.load(0x1000, {0xE8, 0x02, 0x00, 0xF4, 0x90, 0xB0, 0x2A, 0xC3});  // call +2; hlt; nop; mov al,42; ret
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0x0000, 0x0100, 0x0100);  // SP=0 wraps to FFFE on push
    cpu.run(100);
    CHECK(cpu.al() == 42);
    CHECK(cpu.halted());
    CHECK(bus.m[0x2000 + 0xFFFE] == 0x03);  // return address 0003 pushed at 0200:FFFE
}

TEST_CASE("carry and zero flags can be read and set") {
    FlatBus bus;
    bus.load(0x1000, {0xF9, 0xF4});  // stc; hlt
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0xFFFE, 0x0100, 0x0100);
    cpu.run(10);
    CHECK(cpu.cf());
    cpu.setCf(false);
    CHECK_FALSE(cpu.cf());
}

TEST_CASE("a program starts with interrupts enabled, as under DOS") {
    FlatBus bus;
    bus.load(0x1000, {0x9C, 0x58, 0xF4});  // pushf ; pop ax ; hlt
    Cpu cpu(bus);
    cpu.reset(0x0100, 0, 0x0200, 0xFFFE, 0x0100, 0x0100);
    cpu.run(10);
    CHECK((cpu.ax() & 0x0200) != 0);
}
