// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "door/bios.h"
#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/guestmem.h"

using namespace axis::door;

namespace {
struct Rig {
    PagePool     pool{256 * 1024};
    GuestMemory  mem{pool};
    Cpu          cpu{mem};
    Bios         bios{cpu, mem};
    ScriptStream kb;
    Rig() {
        mem.mapRam(0, 0x2000);
        mem.mapZero(0x2000, 0x9E000);
        bios.install();
        bios.setKeyboard(&kb);
        cpu.reset(0x0200, 0x0010, 0x0200, 0xFFFE, 0x0200, 0x0200);
    }
    void int10(uint16_t ax) {
        cpu.setAx(ax);
        bios.dispatch(0x10);
    }
};
}  // namespace

TEST_CASE("bios teletype writes into B800 and screenText shows it") {
    Rig r;
    r.int10(0x0003);  // mode 3 clears the screen
    for (char c : std::string("HELLO\r\nWORLD")) {
        r.cpu.setBx(0x0007);
        r.int10(0x0E00 | (uint8_t)c);
    }
    auto s = r.bios.screenText();
    CHECK(s[0].substr(0, 5) == "HELLO");
    CHECK(s[1].substr(0, 5) == "WORLD");
    CHECK(r.mem.read8(0xB8000 + 1) == 0x07);
}

TEST_CASE("bios set cursor and write char with attribute") {
    Rig r;
    r.int10(0x0003);
    r.cpu.setDx(0x0510);  // row 5, column 16
    r.cpu.setBx(0);
    r.int10(0x0200);
    r.cpu.setBx(0x001E);
    r.cpu.setCx(3);
    r.int10(0x0900 | 'X');
    auto s = r.bios.screenText();
    CHECK(s[5].substr(16, 3) == "XXX");
    CHECK(r.mem.read8(0xB8000 + (5 * 80 + 16) * 2 + 1) == 0x1E);
    r.int10(0x0300);
    CHECK(r.cpu.dx() == 0x0510);  // 09h does not move the cursor
}

TEST_CASE("bios scroll moves lines up and blanks the bottom") {
    Rig r;
    r.int10(0x0003);
    for (int row = 0; row < 3; row++) {
        r.cpu.setDx((uint16_t)(row << 8));
        r.cpu.setBx(0);
        r.int10(0x0200);
        r.cpu.setBx(0x0007);
        r.cpu.setCx(1);
        r.int10(0x0900 | ('A' + row));
    }
    r.cpu.setBx(0x0700);
    r.cpu.setCx(0x0000);
    r.cpu.setDx(0x184F);
    r.int10(0x0601);  // scroll whole screen up by one
    auto s = r.bios.screenText();
    CHECK(s[0][0] == 'B');
    CHECK(s[1][0] == 'C');
    CHECK(s[24][0] == ' ');
}

TEST_CASE("bios teletype at the bottom right scrolls") {
    Rig r;
    r.int10(0x0003);
    r.cpu.setDx(0x1800);
    r.cpu.setBx(0);
    r.int10(0x0200);
    r.cpu.setBx(0x0007);
    r.int10(0x0E00 | 'Z');
    r.int10(0x0E00 | '\n');
    auto s = r.bios.screenText();
    CHECK(s[23][0] == 'Z');
}

TEST_CASE("bios int16 01 with no key sets ZF and the idle hint") {
    Rig r;
    r.cpu.setAx(0x0100);
    r.bios.dispatch(0x16);
    CHECK(r.cpu.zf());
    CHECK(r.bios.consumeIdleHint());
    CHECK_FALSE(r.bios.consumeIdleHint());  // consumed once
}

TEST_CASE("bios int16 00 returns a pushed key and maps Enter and Escape") {
    Rig r;
    r.kb.push("a\r\x1b");
    r.cpu.setAx(0x0000);
    r.bios.dispatch(0x16);
    CHECK(r.cpu.al() == 'a');
    r.cpu.setAx(0x0000);
    r.bios.dispatch(0x16);
    CHECK(r.cpu.ax() == 0x1C0D);
    r.cpu.setAx(0x1000);
    r.bios.dispatch(0x16);
    CHECK(r.cpu.ax() == 0x011B);
}

TEST_CASE("bios int16 00 with no key waits by rewinding the INT") {
    Rig r;
    uint16_t ip = r.cpu.ip();
    r.cpu.setAx(0x0000);
    r.bios.dispatch(0x16);
    CHECK(r.bios.waitingForKey());
    CHECK(r.cpu.ip() == (uint16_t)(ip - 2));
}

TEST_CASE("bios tick raises irq0 about 18 times a second and counts in the BIOS data area") {
    Rig r;
    int raised = 0;
    for (uint64_t ms = 0; ms <= 1000; ms += 5) raised += r.bios.tick(ms);
    CHECK(raised >= 18);
    CHECK(raised <= 19);
}

TEST_CASE("bios int15 1000h sets the idle hint") {
    Rig r;
    r.cpu.setAx(0x1000);
    r.bios.dispatch(0x15);
    CHECK(r.bios.consumeIdleHint());
}

TEST_CASE("bios reports SHARE installed and no XMS") {
    Rig r;
    r.cpu.setAx(0x1000);
    r.bios.dispatch(0x2F);
    CHECK(r.cpu.al() == 0xFF);
    r.cpu.setAx(0x4300);
    r.bios.dispatch(0x2F);
    CHECK(r.cpu.al() == 0x00);
}

TEST_CASE("bios vectors point at stubs until a program replaces them") {
    Rig r;
    CHECK(r.bios.isStub(0x1C));
    r.mem.write16(0x1C * 4, 0x1234);
    r.mem.write16(0x1C * 4 + 2, 0x0300);
    CHECK_FALSE(r.bios.isStub(0x1C));
}

TEST_CASE("bios timer stub calls a program's INT 1Ch hook and returns") {
    Rig r;
    // the program hooks 1Ch: inc byte [0100h] ; iret   (at 0300:0000, DS = 0200h)
    r.mem.write16(0x1C * 4, 0x0000);
    r.mem.write16(0x1C * 4 + 2, 0x0300);
    const uint8_t hook[] = {0x2E, 0xFE, 0x06, 0x00, 0x01, 0xCF};  // inc byte cs:[0100h] ; iret
    r.mem.writeBlock(0x3000, hook, sizeof hook);
    const uint8_t prog[] = {0xFB, 0x90, 0x90, 0x90, 0xEB, 0xFC};  // sti ; nop x3 ; jmp back
    r.mem.writeBlock(0x2010, prog, sizeof prog);
    r.cpu.setIntHandler([&](int n) { return r.bios.route(n); });
    r.cpu.raiseIrq(8);
    r.cpu.run(50);
    CHECK(r.mem.read8(0x3100) == 1);                 // the hook ran once
    CHECK(r.mem.read16(0x46C) == 1);                 // and the BIOS counted the tick
    CHECK(r.cpu.cs() == 0x0200);                     // back in the program
}

TEST_CASE("bios video status port toggles its retrace bits") {
    Rig r;
    uint8_t a = r.bios.portIn(0x3DA), b = r.bios.portIn(0x3DA);
    CHECK((a & 1) != (b & 1));
    CHECK((a & 8) != (b & 8));
}

TEST_CASE("bios font info reports 16-point characters on a 25-row screen") {
    Rig r;
    r.cpu.setBx(0x0000);
    r.int10(0x1130);
    CHECK(r.cpu.cx() == 16);
    CHECK(r.cpu.dl() == 24);
}

TEST_CASE("bios int16 01 reports the waiting key without taking it") {
    Rig r;
    r.kb.push("L");
    r.cpu.setAx(0x0100);
    r.bios.dispatch(0x16);
    CHECK_FALSE(r.cpu.zf());
    CHECK(r.cpu.al() == 'L');
    r.cpu.setAx(0x0000);
    r.bios.dispatch(0x16);
    CHECK(r.cpu.al() == 'L');
}

TEST_CASE("bios int16 turns terminal arrow and paging keys into PC scan codes; a lone Escape stays Escape") {
    Rig r;
    r.kb.push("\x1b[A\x1b[B\x1b[C\x1b[D\x1b[H\x1b[F\x1b[5~\x1b[6~\x1b[3~\x1bOA\x1b" "x");
    const uint16_t want[] = {0x4800, 0x5000, 0x4D00, 0x4B00, 0x4700, 0x4F00, 0x4900, 0x5100, 0x5300, 0x4800, 0x011B, 'x'};
    for (uint16_t w : want) {
        r.cpu.setAx(0x0100);  // peek first, as programs do
        r.bios.dispatch(0x16);
        CHECK(r.cpu.ax() == w);
        r.cpu.setAx(0x0000);
        r.bios.dispatch(0x16);
        CHECK(r.cpu.ax() == w);
    }
}
