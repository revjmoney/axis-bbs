// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Repeat door start, exit, a missing program, a hang-up and an out-of-memory stop on one
// file table. The numbers here are guest pages inside the 8086 runtime. They are not the
// ESP32 heap, and a passing run does not mean the CYD's fragmented byte heap can hold LORD.
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "door/session.h"
#include "net/zmodem.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {
void writeExe(const fs::path& p, const std::vector<uint8_t>& code, uint16_t minAllocParas = 0x10) {
    std::vector<uint8_t> f(32, 0);
    uint32_t             total = 32 + code.size();
    auto w = [&](int at, uint16_t v) {
        f[at] = (uint8_t)v;
        f[at + 1] = v >> 8;
    };
    f[0] = 'M';
    f[1] = 'Z';
    w(2, total % 512);
    w(4, (total + 511) / 512);
    w(8, 2);
    w(10, minAllocParas);
    w(12, 0xFFFF);
    w(14, 0);
    w(16, 0xFFFE);
    w(24, 28);
    f.insert(f.end(), code.begin(), code.end());
    std::ofstream(p, std::ios::binary).write((const char*)f.data(), f.size());
}

fs::path tmpDir(const char* name) {
    fs::path d = fs::temp_directory_path() / (std::string("axis_life_") + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

const std::vector<uint8_t> kExit = {0xB8, 0x03, 0x4C, 0xCD, 0x21};  // mov ax,4C03h ; int 21h
// touch a fresh 32 KB and then exit; a 16 KB budget stops it first
const std::vector<uint8_t> kTouch = {0x8C, 0xC8, 0x05, 0x00, 0x10, 0x8E, 0xC0, 0x31, 0xFF, 0xB9, 0x00, 0x80, 0xB8, 0x01,
                                     0x00, 0xF3, 0xAB, 0xB8, 0x00, 0x4C, 0xCD, 0x21};
const std::vector<uint8_t> kWait = {0xB4, 0x02, 0xCD, 0x14, 0xEB, 0xFA};  // FOSSIL read, forever
const std::vector<uint8_t> kSpin = {0xEB, 0xFE};                            // jmp $

struct Pipe : ByteStream {
    std::deque<uint8_t> in, out;
    bool                up = true;
    int  available() override { return (int)in.size(); }
    int  peek() override { return in.empty() ? -1 : in.front(); }
    int  read() override {
        if (in.empty()) return -1;
        int c = in.front();
        in.pop_front();
        return c;
    }
    void write(const uint8_t* p, size_t n) override { out.insert(out.end(), p, p + n); }
    bool carrier() override { return up; }
};

std::vector<uint8_t> bytes(size_t n, uint32_t seed) {
    std::vector<uint8_t> v(n);
    uint32_t             x = seed;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        v[i] = (uint8_t)(x >> 16);
    }
    return v;
}
}  // namespace

TEST_CASE("the guest page cap stays 128 KB") {
    DoorConfig cfg;
    CHECK(cfg.ramBudget == 128 * 1024);
}

TEST_CASE("a door that locks a file drops the lock when the session is destroyed") {
    fs::path d = tmpDir("lock");
    // create L.DAT, lock one byte at offset 0, exit. The name is at offset 0x20.
    std::vector<uint8_t> code = {0xB4, 0x3C, 0x31, 0xC9, 0xBA, 0x20, 0x00, 0x0E, 0x1F, 0xCD, 0x21, 0x8B, 0xD8, 0xB8, 0x00, 0x5C,
                                 0x31, 0xC9, 0x31, 0xD2, 0x31, 0xF6, 0xBF, 0x01, 0x00, 0xCD, 0x21, 0xB8, 0x00, 0x4C, 0xCD, 0x21};
    REQUIRE(code.size() == 0x20);
    for (char c : std::string("L.DAT")) code.push_back((uint8_t)c);
    code.push_back(0);
    writeExe(d / "T.EXE", code);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    {
        DoorSession s(cfg, fs, locks, caller, 7);
        CHECK(s.run() == DoorResult::Exited);
        CHECK(s.exitCode() == 0);
        CHECK(locks.count() >= 1);  // still held until the session object goes away
    }
    CHECK(locks.count() == 0);
    CHECK(fs::exists(d / "L.DAT"));
}

TEST_CASE("repeated door exit, load failure, hangup and out of memory leave the next door runnable") {
    fs::path d = tmpDir("cycle");
    writeExe(d / "T.EXE", kExit);
    writeExe(d / "BIG.EXE", kTouch, 0x2000);
    writeExe(d / "WAIT.EXE", kWait);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    std::vector<int>    saw;
    for (int pass = 0; pass < 3; pass++) {
        {
            ScriptStream caller;
            DoorConfig   cfg;
            cfg.dir = d.string();
            cfg.exe = "T.EXE";
            DoorSession s(cfg, fs, locks, caller, 1);
            CHECK(s.run() == DoorResult::Exited);
            CHECK(s.exitCode() == 3);
            saw.push_back((int)s.memStats().peakRamPages);
        }
        CHECK(locks.count() == 0);
        {
            ScriptStream caller;
            DoorConfig   cfg;
            cfg.dir = d.string();
            cfg.exe = "MISSING.EXE";
            DoorSession s(cfg, fs, locks, caller, 1);
            CHECK(s.run() == DoorResult::LoadError);
            CHECK(s.memStats().peakRamPages == 0);
        }
        CHECK(locks.count() == 0);
        {
            ScriptStream caller;
            DoorConfig   cfg;
            cfg.dir = d.string();
            cfg.exe = "WAIT.EXE";
            cfg.localMode = false;
            cfg.carrierGraceMs = 200;
            DoorSession s(cfg, fs, locks, caller, 1);
            for (int i = 0; i < 4; i++) s.step(20);
            caller.hangUp();
            while (s.step(20)) {
            }
            CHECK(s.result() == DoorResult::CarrierLost);
        }
        CHECK(locks.count() == 0);
        {
            int          oom = 0;
            ScriptStream caller;
            DoorConfig   cfg;
            cfg.dir = d.string();
            cfg.exe = "BIG.EXE";
            cfg.ramBudget = 16 * 1024;
            cfg.onOutOfMemory = [&] { oom++; };
            std::vector<uint8_t> arena(8 * 1024, 0x5A);
            cfg.arenas.emplace_back(arena.data(), arena.size());
            DoorSession s(cfg, fs, locks, caller, 1);
            CHECK(s.run() == DoorResult::OutOfMemory);
            CHECK(oom >= 1);
            CHECK(s.memStats().peakRamPages > 0);
            CHECK(arena[0] == 0);  // zero-on-write used the lent page; the buffer is still ours
            arena[0] = 0x11;
            CHECK(arena[0] == 0x11);
            saw.push_back((int)s.memStats().peakRamPages);
        }
        CHECK(locks.count() == 0);
    }
    ScriptStream caller;
    DoorConfig   cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    DoorSession again(cfg, fs, locks, caller, 1);
    CHECK(again.run() == DoorResult::Exited);
    CHECK(again.exitCode() == 3);
    INFO("guest page peaks (not ESP32 heap): " << saw.size());
    CHECK(saw.size() == 6);
}

TEST_CASE("a spinning door and a ZMODEM transfer on another line both finish") {
    fs::path d = tmpDir("both");
    writeExe(d / "T.EXE", kSpin);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.timeLimitSec = 1;
    DoorSession door(cfg, fs, locks, caller, 1);

    Pipe                 board, remote;  // board is node 1's transfer peer; remote is node 2
    auto                 file = bytes(48 * 1024, 99);
    std::vector<uint8_t> got;
    axis::net::ZmodemSender sender(
        board, [&](uint32_t off, uint8_t* buf, size_t n) {
            if (off + n > file.size()) return false;
            std::copy(file.begin() + off, file.begin() + off + n, buf);
            return true;
        },
        (uint32_t)file.size(), "NODE2.BIN");
    axis::net::ZmodemReceiver receiver(
        remote, [](const std::string& name, uint32_t sz) { return name == "NODE2.BIN" && sz == 48 * 1024; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [](bool) {});

    uint32_t now = 0;
    bool     xfer = true;
    for (int i = 0; i < 500 && (door.step(10) || xfer); i++, now += 10) {
        remote.in.insert(remote.in.end(), board.out.begin(), board.out.end());
        board.out.clear();
        board.in.insert(board.in.end(), remote.out.begin(), remote.out.end());
        remote.out.clear();
        bool send = sender.step(now);
        bool recv = receiver.step(now);
        xfer = send || recv;
    }
    CHECK(door.result() == DoorResult::TimeLimit);
    CHECK(sender.result() == axis::net::Transfer::Result::Ok);
    CHECK(receiver.result() == axis::net::Transfer::Result::Ok);
    CHECK(got == file);
    CHECK(locks.count() == 0);  // the spinner never locked anything, and it is still the live session
}
