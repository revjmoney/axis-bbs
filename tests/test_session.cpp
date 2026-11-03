// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <chrono>
#include <filesystem>
#include <fstream>
#include <vector>

#include "doctest.h"
#include "door/bytestream.h"
#include "door/mz.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

// Wrap raw code bytes in a minimal MZ (no relocations), with `minAllocParas` of memory after it.
static void writeExe(const fs::path& p, const std::vector<uint8_t>& code, uint16_t minAllocParas = 0x10) {
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
    w(14, 0);       // SS = load segment
    w(16, 0xFFFE);  // SP
    w(24, 28);
    f.insert(f.end(), code.begin(), code.end());
    std::ofstream(p, std::ios::binary).write((const char*)f.data(), f.size());
}

static fs::path tmpDir(const char* name) {
    fs::path d = fs::temp_directory_path() / (std::string("axis_sess_") + name);
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

TEST_CASE("session program exits with code 3") {
    fs::path d = tmpDir("exit");
    writeExe(d / "T.EXE", {0xB8, 0x03, 0x4C, 0xCD, 0x21});  // mov ax,4C03h ; int 21h
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::Exited);
    CHECK(s.exitCode() == 3);
}

TEST_CASE("session busy loop still times out") {
    fs::path d = tmpDir("busy");
    writeExe(d / "T.EXE", {0xEB, 0xFE});  // jmp $
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.timeLimitSec = 1;
    DoorSession s(cfg, fs, locks, caller, 1);
    auto t0 = std::chrono::steady_clock::now();
    CHECK(s.run() == DoorResult::TimeLimit);
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
}

TEST_CASE("session tiny budget ends cleanly with OutOfMemory") {
    fs::path d = tmpDir("oom");
    // rep stosw 32 KB of zeros... into fresh pages: mov ax,cs ; add ax,1000h ; mov es,ax ;
    // xor di,di ; mov cx,8000h ; mov ax,1 ; rep stosw ; mov ax,4C00h ; int 21h
    writeExe(d / "T.EXE", {0x8C, 0xC8, 0x05, 0x00, 0x10, 0x8E, 0xC0, 0x31, 0xFF, 0xB9, 0x00, 0x80, 0xB8, 0x01,
                           0x00, 0xF3, 0xAB, 0xB8, 0x00, 0x4C, 0xCD, 0x21},
             0x2000);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.ramBudget = 16 * 1024;  // low memory + text page + a little
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::OutOfMemory);
}

TEST_CASE("session missing program is a load error") {
    fs::path            d = tmpDir("noexe");
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "NOPE.EXE";
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::LoadError);
}

TEST_CASE("session local mode: DOS output shows on the screen and keys reach the door") {
    fs::path d = tmpDir("local");
    // mov ah,9 ; mov dx,msg ; int 21h ; mov ah,8 ; int 21h ; mov dl,al ; mov ah,2 ; int 21h ; mov ax,4C00h ; int 21h ; msg db 'KEY?$'
    std::vector<uint8_t> code = {0xB4, 0x09, 0xBA, 0x16, 0x00, 0x0E, 0x1F, 0xCD, 0x21, 0xB4, 0x08, 0xCD, 0x21,
                                 0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21, 0xB8, 0x00, 0x4C, 0xCD, 0x21};
    code[3] = (uint8_t)code.size();  // msg offset follows the code (push cs ; pop ds makes DS = CS)
    for (char c : std::string("KEY?$")) code.push_back((uint8_t)c);
    writeExe(d / "T.EXE", code);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    DoorSession s(cfg, fs, locks, caller, 1);
    for (int i = 0; i < 20 && s.step(20); i++) {
    }
    CHECK(s.screen()[0].substr(0, 4) == "KEY?");
    caller.push("z");
    while (s.step(20)) {
    }
    CHECK(s.result() == DoorResult::Exited);
    CHECK(s.screen()[0].substr(0, 5) == "KEY?z");
}

TEST_CASE("session exit stops the CPU at once") {
    fs::path d = tmpDir("exitstop");
    // mov ax,4C00h ; int 21h ; then (must never run) create BAD.DAT: mov ah,3Ch ; xor cx,cx ; mov dx,name ; push cs ; pop ds ; int 21h ; jmp $
    std::vector<uint8_t> code = {0xB8, 0x00, 0x4C, 0xCD, 0x21, 0xB4, 0x3C, 0x31, 0xC9, 0xBA, 0x00, 0x00, 0x0E, 0x1F, 0xCD, 0x21, 0xEB, 0xFE};
    code[10] = (uint8_t)code.size();
    for (char c : std::string("BAD.DAT")) code.push_back((uint8_t)c);
    code.push_back(0);
    writeExe(d / "T.EXE", code);
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::Exited);
    CHECK_FALSE(fs::exists(d / "BAD.DAT"));
}

TEST_CASE("session remote mode: FOSSIL talks to the caller and DOOR.SYS is written") {
    fs::path d = tmpDir("remote");
    // mov ah,4 ; xor dx,dx ; int 14h ; mov ax,0158h ; int 14h ; mov ah,2 ; int 14h ; mov ah,1 ; int 14h ; mov ax,4C00h ; int 21h
    // (init, send 'X', wait for a byte, echo it back, exit)
    writeExe(d / "T.EXE", {0xB4, 0x04, 0x31, 0xD2, 0xCD, 0x14, 0xB8, 0x58, 0x01, 0xCD, 0x14, 0xB4, 0x02, 0xCD,
                           0x14, 0xB4, 0x01, 0xCD, 0x14, 0xB8, 0x00, 0x4C, 0xCD, 0x21});
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.localMode = false;
    cfg.dropFile = DropFile::DoorSys;
    cfg.caller.alias = "Revman";
    DoorSession s(cfg, fs, locks, caller, 1);
    for (int i = 0; i < 10 && s.step(20); i++) {
    }
    CHECK(caller.output() == "X");
    caller.push("k");
    while (s.step(20)) {
    }
    CHECK(s.result() == DoorResult::Exited);
    CHECK(caller.output() == "Xk");
    std::ifstream in(d / "DOOR.SYS");
    std::string   all((std::istreambuf_iterator<char>(in)), {});
    CHECK(all.find("Revman") != std::string::npos);
}

TEST_CASE("session remote mode: the caller hanging up ends the door") {
    fs::path d = tmpDir("hangup");
    writeExe(d / "T.EXE", {0xB4, 0x02, 0xCD, 0x14, 0xEB, 0xFA});  // loop: wait for a byte forever
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.localMode = false;
    cfg.carrierGraceMs = 300;
    DoorSession s(cfg, fs, locks, caller, 1);
    for (int i = 0; i < 5; i++) s.step(20);
    caller.hangUp();
    while (s.step(20)) {
    }
    CHECK(s.result() == DoorResult::CarrierLost);
}

TEST_CASE("session HLT waits for the timer instead of freezing") {
    fs::path d = tmpDir("hlt");
    writeExe(d / "T.EXE", {0xFB, 0xF4, 0xB8, 0x05, 0x4C, 0xCD, 0x21});  // sti ; hlt ; mov ax,4C05h ; int 21h
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.timeLimitSec = 5;
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::Exited);
    CHECK(s.exitCode() == 5);
}

TEST_CASE("session a door waiting for a key does not spin the CPU") {
    fs::path d = tmpDir("idlewait");
    writeExe(d / "T.EXE", {0xB4, 0x00, 0xCD, 0x16, 0xB8, 0x00, 0x4C, 0xCD, 0x21});  // mov ah,0 ; int 16h ; exit
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    DoorSession s(cfg, fs, locks, caller, 1);
    s.step(200);
    CHECK(s.instructions() < 2000);
    caller.push("x");
    while (s.step(20)) {
    }
    CHECK(s.result() == DoorResult::Exited);
}

TEST_CASE("session after a hang-up the door gets a grace period to save and exit") {
    fs::path d = tmpDir("grace");
    // wait for the carrier to drop, then "save" for 5 timer ticks (about 275 ms), then exit 7:
    // loop: mov ah,3 ; int 14h ; test al,80h ; jnz loop
    //       mov ax,40h ; mov es,ax ; mov bx,es:[6Ch] ; add bx,5
    // wait: cmp es:[6Ch],bx ; jb wait ; mov ax,4C07h ; int 21h
    writeExe(d / "T.EXE", {0xB4, 0x03, 0xCD, 0x14, 0xA8, 0x80, 0x75, 0xF8, 0xB8, 0x40, 0x00, 0x8E, 0xC0, 0x26, 0x8B,
                           0x1E, 0x6C, 0x00, 0x83, 0xC3, 0x05, 0x26, 0x39, 0x1E, 0x6C, 0x00, 0x72, 0xF9, 0xB8, 0x07,
                           0x4C, 0xCD, 0x21});
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.localMode = false;
    DoorSession s(cfg, fs, locks, caller, 1);
    for (int i = 0; i < 5; i++) s.step(20);
    caller.hangUp();
    while (s.step(20)) {
    }
    CHECK(s.result() == DoorResult::Exited);
    CHECK(s.exitCode() == 7);
}

TEST_CASE("session terminate with AH=00h ends the program") {
    fs::path d = tmpDir("ah00");
    writeExe(d / "T.EXE", {0xB4, 0x00, 0xCD, 0x21, 0xEB, 0xFE});  // mov ah,0 ; int 21h ; jmp $
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "T.EXE";
    cfg.timeLimitSec = 2;
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::Exited);
}

namespace {
// hands the session a ready-made image, as the board's flash cache will
struct TestImageSource : ImageSource {
    std::vector<uint8_t> img;
    int                  calls = 0;
    const uint8_t* image(const std::vector<uint8_t>& exe, const MzInfo& mz, uint16_t loadSeg, size_t& len) override {
        calls++;
        img = relocate(exe.data(), exe.size(), mz, loadSeg);
        len = img.size();
        return img.data();
    }
};
}  // namespace

TEST_CASE("session runs a program from an injected image source") {
    fs::path d = tmpDir("imgsrc");
    writeExe(d / "T.EXE", {0xB8, 0x03, 0x4C, 0xCD, 0x21});
    axis::pc::StdHostFs fs(d);
    LockTable           locks;
    ScriptStream        caller;
    TestImageSource     src;
    DoorConfig          cfg;
    cfg.exe = "T.EXE";
    cfg.imageSource = &src;
    DoorSession s(cfg, fs, locks, caller, 1);
    CHECK(s.run() == DoorResult::Exited);
    CHECK(s.exitCode() == 3);
    CHECK(src.calls == 1);
}
