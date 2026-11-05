// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "doctest.h"
#include "door/bytestream.h"
#include "door/cpu.h"
#include "door/dos.h"
#include "door/dosmem.h"
#include "door/guestmem.h"
#include "door/mz.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {
// A guest with low memory mapped and a DOS over a fresh temp folder.
struct Rig {
    fs::path                   dir;
    PagePool                   pool{512 * 1024};
    GuestMemory                mem{pool};
    std::unique_ptr<DosMemory> dm;
    Cpu                        cpu{mem};
    axis::pc::StdHostFs        hfs;
    LockTable                  locks;
    ScriptStream               con;
    std::unique_ptr<Dos>       dos;

    explicit Rig(const char* name)
        : dir(fs::temp_directory_path() / (std::string("axis_dos_") + name)), hfs((fs::remove_all(dir), fs::create_directories(dir), dir)) {
        mem.mapRam(0, 0x2000);
        mem.mapZero(0x2000, 0xA0000 - 0x2000);
        dm.reset(new DosMemory(mem, kPspSeg - 1, kTopSeg));
        uint16_t seg, largest;
        dm->alloc(0x1000, kPspSeg, seg, largest);  // the program's own block
        cpu.reset(kLoadSeg, 0, kLoadSeg, 0xFFFE, kPspSeg, kPspSeg);
        dos.reset(new Dos(cpu, mem, *dm, hfs, locks, 1, kPspSeg));
        dos->setConsole(&con);
    }
    ~Rig() {
        dos.reset();
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    // put an ASCIIZ string at DS:offset
    void str(uint16_t off, const std::string& s) {
        mem.writeBlock(cpu.ds() * 16u + off, s.c_str(), s.size() + 1);
    }
    void file(const std::string& name, const std::string& body) { std::ofstream(dir / name, std::ios::binary) << body; }
    std::string slurp(const std::string& name) {
        std::ifstream in(dir / name, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    }
    void call(uint16_t ax) {
        cpu.setAx(ax);
        dos->int21();
    }
};
}  // namespace

TEST_CASE("dos open with AL=44h reads like LORD does") {
    Rig r("open44");
    r.file("LORDTXT.DAT", "hello");
    r.str(0x200, "LORDTXT.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D44);
    REQUIRE_FALSE(r.cpu.cf());
    uint16_t h = r.cpu.ax();
    CHECK(h >= 5);
    r.cpu.setBx(h);
    r.cpu.setCx(5);
    r.cpu.setDx(0x300);
    r.call(0x3F00);
    CHECK_FALSE(r.cpu.cf());
    CHECK(r.cpu.ax() == 5);
    CHECK(r.mem.read8(r.cpu.ds() * 16u + 0x300) == 'h');
}

TEST_CASE("dos case-insensitive open with a drive and backslash path") {
    Rig r("case");
    r.file("lord.dat", "x");
    r.str(0x200, "C:\\LORD.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D00);
    CHECK_FALSE(r.cpu.cf());
}

TEST_CASE("dos missing file gives error 2") {
    Rig r("missing");
    r.str(0x200, "NOPE.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D00);
    CHECK(r.cpu.cf());
    CHECK(r.cpu.ax() == 2);
}

TEST_CASE("dos create write seek read close") {
    Rig r("cwsr");
    r.str(0x200, "PLAYER.DAT");
    r.cpu.setDx(0x200);
    r.cpu.setCx(0);
    r.call(0x3C00);
    REQUIRE_FALSE(r.cpu.cf());
    uint16_t h = r.cpu.ax();
    r.str(0x300, "ABCDEFGH");
    r.cpu.setBx(h);
    r.cpu.setCx(8);
    r.cpu.setDx(0x300);
    r.call(0x4000);
    CHECK(r.cpu.ax() == 8);
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.cpu.setDx(2);
    r.call(0x4200);  // seek to 2 from start
    CHECK(r.cpu.ax() == 2);
    CHECK(r.cpu.dx() == 0);
    r.cpu.setBx(h);
    r.cpu.setCx(3);
    r.cpu.setDx(0x400);
    r.call(0x3F00);
    CHECK(r.cpu.ax() == 3);
    CHECK(r.mem.read8(r.cpu.ds() * 16u + 0x400) == 'C');
    r.cpu.setBx(h);
    r.call(0x3E00);
    CHECK_FALSE(r.cpu.cf());
    CHECK(r.slurp("PLAYER.DAT") == "ABCDEFGH");
}

TEST_CASE("dos write of zero bytes truncates") {
    Rig r("trunc");
    r.file("T.DAT", "0123456789");
    r.str(0x200, "T.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D02);
    uint16_t h = r.cpu.ax();
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.cpu.setDx(4);
    r.call(0x4200);
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.call(0x4000);
    r.cpu.setBx(h);
    r.call(0x3E00);
    CHECK(r.slurp("T.DAT") == "0123");
}

TEST_CASE("dos find first and next with a wildcard fill the DTA") {
    Rig r("find");
    r.file("A1.DAT", "1");
    r.file("A2.DAT", "22");
    r.file("B.TXT", "333");
    r.str(0x200, "*.DAT");
    r.cpu.setDx(0x200);
    r.cpu.setCx(0);
    r.call(0x4E00);
    REQUIRE_FALSE(r.cpu.cf());
    uint32_t    dta = kPspSeg * 16u + 0x80;
    std::string n1;
    for (int i = 0; i < 13 && r.mem.read8(dta + 30 + i); i++) n1 += (char)r.mem.read8(dta + 30 + i);
    r.call(0x4F00);
    REQUIRE_FALSE(r.cpu.cf());
    std::string n2;
    for (int i = 0; i < 13 && r.mem.read8(dta + 30 + i); i++) n2 += (char)r.mem.read8(dta + 30 + i);
    CHECK(((n1 == "A1.DAT" && n2 == "A2.DAT") || (n1 == "A2.DAT" && n2 == "A1.DAT")));
    r.call(0x4F00);
    CHECK(r.cpu.cf());
    CHECK(r.cpu.ax() == 0x12);
}

TEST_CASE("dos alloc and resize through 48h and 4Ah") {
    Rig r("mem");
    r.cpu.setBx(0x100);
    r.call(0x4800);
    REQUIRE_FALSE(r.cpu.cf());
    uint16_t seg = r.cpu.ax();
    r.cpu.setEs(seg);
    r.cpu.setBx(0x80);
    r.call(0x4A00);
    CHECK_FALSE(r.cpu.cf());
    r.cpu.setBx(0xFFFF);
    r.call(0x4800);
    CHECK(r.cpu.cf());
    CHECK(r.cpu.ax() == 8);
    CHECK(r.cpu.bx() > 0);
}

TEST_CASE("dos 5Ch lock conflict between two owners") {
    Rig r("lock");
    r.file("SHARED.DAT", std::string(100, 'x'));
    r.str(0x200, "SHARED.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D42);
    uint16_t h = r.cpu.ax();
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.cpu.setDx(10);
    r.cpu.setSi(0);
    r.cpu.setDi(20);
    r.call(0x5C00);
    CHECK_FALSE(r.cpu.cf());
    // another node's DOS on the same lock table
    int key = r.hfs.fileKey(r.dos->hostHandle(h));
    CHECK_FALSE(r.locks.lock(key, 15, 5, 2));
    CHECK(r.locks.lock(key, 40, 5, 2));
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.cpu.setDx(10);
    r.cpu.setSi(0);
    r.cpu.setDi(20);
    r.call(0x5C01);
    CHECK_FALSE(r.cpu.cf());
    CHECK(r.locks.lock(key, 15, 5, 2));
}

TEST_CASE("dos exit 4Ch sets the exit code") {
    Rig r("exit");
    r.call(0x4C03);
    CHECK(r.dos->exited());
    CHECK(r.dos->exitCode() == 3);
}

TEST_CASE("dos version, PSP, drive, time and vectors") {
    Rig r("misc");
    r.call(0x3000);
    CHECK(r.cpu.al() >= 3);
    r.call(0x6200);
    CHECK(r.cpu.bx() == kPspSeg);
    r.call(0x1900);
    CHECK(r.cpu.al() == 2);
    r.call(0x2C00);
    CHECK(r.cpu.ch() < 24);
    r.cpu.setDs(0x1234);
    r.cpu.setDx(0x5678);
    r.call(0x2560);
    r.call(0x3560);
    CHECK(r.cpu.es() == 0x1234);
    CHECK(r.cpu.bx() == 0x5678);
}

TEST_CASE("dos string output goes to the console") {
    Rig r("out");
    r.str(0x200, "hi there$");
    r.cpu.setDx(0x200);
    r.call(0x0900);
    r.cpu.setDx('!');
    r.call(0x0200);
    CHECK(r.con.output() == "hi there!");
}

TEST_CASE("dos read char with no key waits by rewinding the INT") {
    Rig r("wait");
    uint16_t ip = r.cpu.ip();
    r.call(0x0800);
    CHECK(r.dos->waitingForKey());
    CHECK(r.cpu.ip() == (uint16_t)(ip - 2));
    r.con.push("k");
    r.cpu.setIp(ip);
    r.call(0x0800);
    CHECK_FALSE(r.dos->waitingForKey());
    CHECK(r.cpu.al() == 'k');
}

TEST_CASE("dos unknown function sets carry and is traced") {
    Rig r("unknown");
    std::FILE* t = std::tmpfile();
    r.dos->setTrace(t);
    r.call(0xEE00);
    CHECK(r.cpu.cf());
    CHECK(r.cpu.ax() == 1);
    std::rewind(t);
    char buf[200] = {};
    size_t n = std::fread(buf, 1, sizeof buf - 1, t);
    std::fclose(t);
    CHECK(std::string(buf, n).find("AH=EE") != std::string::npos);
}

TEST_CASE("dos one read larger than 512 bytes gets the whole file") {
    Rig r("bigread");
    std::string body;
    for (int i = 0; i < 1361; i++) body += (char)('A' + i % 26);
    r.file("PLAYEXT.DAT", body);
    r.str(0x200, "PLAYEXT.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D44);
    uint16_t h = r.cpu.ax();
    r.cpu.setBx(h);
    r.cpu.setCx(1361);
    r.cpu.setDx(0x1000);
    r.call(0x3F00);
    CHECK(r.cpu.ax() == 1361);
    CHECK(r.mem.read8(r.cpu.ds() * 16u + 0x1000 + 1360) == (uint8_t)('A' + 1360 % 26));
}

TEST_CASE("dos write then read back on the same handle") {
    Rig r("rw");
    r.file("RW.DAT", std::string(2000, '.'));
    r.str(0x200, "RW.DAT");
    r.cpu.setDx(0x200);
    r.call(0x3D02);
    uint16_t h = r.cpu.ax();
    r.str(0x300, "XYZ");
    r.cpu.setBx(h);
    r.cpu.setCx(3);
    r.cpu.setDx(0x300);
    r.call(0x4000);
    r.cpu.setBx(h);
    r.cpu.setCx(1000);
    r.cpu.setDx(0x400);
    r.call(0x3F00);
    CHECK(r.cpu.ax() == 1000);
    r.cpu.setBx(h);
    r.cpu.setCx(0);
    r.cpu.setDx(0);
    r.call(0x4200);
    r.cpu.setBx(h);
    r.cpu.setCx(3);
    r.cpu.setDx(0x500);
    r.call(0x3F00);
    CHECK(r.mem.read8(r.cpu.ds() * 16u + 0x500) == 'X');
}

TEST_CASE("dos closing a duplicate handle keeps the original open") {
    Rig r("dup");
    r.str(0x200, "LOG.DAT");
    r.cpu.setDx(0x200);
    r.cpu.setCx(0);
    r.call(0x3C00);
    uint16_t h = r.cpu.ax();
    r.cpu.setBx(h);
    r.call(0x4500);  // dup
    uint16_t dup = r.cpu.ax();
    r.cpu.setBx(dup);
    r.call(0x3E00);  // close the duplicate ("commit by dup and close")
    r.str(0x300, "KEPT");
    r.cpu.setBx(h);
    r.cpu.setCx(4);
    r.cpu.setDx(0x300);
    r.call(0x4000);
    CHECK_FALSE(r.cpu.cf());
    CHECK(r.cpu.ax() == 4);
    r.cpu.setBx(h);
    r.call(0x3E00);
    CHECK(r.slurp("LOG.DAT") == "KEPT");
}

TEST_CASE("dos open with AL=44h can also write (LORDCFG registers by patching LORD.EXE that way)") {
    Rig r("open44w");
    r.file("LORD.EXE", "MZ......");
    r.str(0x200, "LORD.EXE");
    r.cpu.setDx(0x200);
    r.call(0x3D44);
    REQUIRE_FALSE(r.cpu.cf());
    uint16_t h = r.cpu.ax();
    r.cpu.setBx(h), r.cpu.setCx(0), r.cpu.setDx(2);
    r.call(0x4200);  // to offset 2
    r.str(0x300, "R");
    r.cpu.setBx(h), r.cpu.setCx(1), r.cpu.setDx(0x300);
    r.call(0x4000);
    CHECK_FALSE(r.cpu.cf());
    CHECK(r.cpu.ax() == 1);  // a short write is Pascal's runtime error 101
    r.cpu.setBx(h);
    r.call(0x3E00);
    CHECK(r.slurp("LORD.EXE") == "MZR.....");
}
