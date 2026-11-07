// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// LORD Fast reads a LORD 4.07 PLAYER.DAT and rewrites only that warrior.
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"

#include "door/bytestream.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {

constexpr int kRec = 236;
constexpr int kMon = 239;

std::string plain(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if ((unsigned char)s[i] == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i++];
                if (c >= 0x40 && c <= 0x7e) break;
            }
            continue;
        }
        o.push_back(s[i++]);
    }
    return o;
}

std::string tail(const std::string& s) {
    return s.size() > 1600 ? s.substr(s.size() - 1600) : s;
}

void pstr(uint8_t* rec, int off, int cap, const char* s) {
    size_t n = std::strlen(s);
    if (n > (size_t)cap) n = (size_t)cap;
    rec[off] = (uint8_t)n;
    std::memcpy(rec + off + 1, s, n);
}

void w16(uint8_t* rec, int off, int v) {
    rec[off] = (uint8_t)(v & 0xFF);
    rec[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

void w32(uint8_t* rec, int off, int v) {
    for (int i = 0; i < 4; i++) rec[off + i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

int r16(const uint8_t* rec, int off) {
    return (int)(int16_t)(rec[off] | (rec[off + 1] << 8));
}

int r32(const uint8_t* rec, int off) {
    return (int)(int32_t)(rec[off] | (rec[off + 1] << 8) | (rec[off + 2] << 16) | (rec[off + 3] << 24));
}

std::vector<uint8_t> warriors() {
    std::vector<uint8_t> f(kRec * 2, 0);
    pstr(f.data(), 0, 20, "Rex");
    pstr(f.data(), 21, 50, "Redking ");
    w16(f.data(), 72, 10);
    w16(f.data(), 78, 20);
    pstr(f.data(), 82, 20, "Stick");
    w16(f.data(), 105, 2);
    w16(f.data(), 107, 3);
    w32(f.data(), 109, 100);
    w32(f.data(), 113, 5);
    w16(f.data(), 117, 3);
    w16(f.data(), 119, 12);
    w16(f.data(), 121, 2);
    w16(f.data(), 127, 1);
    pstr(f.data(), 131, 20, "Coat");
    w32(f.data(), 158, 10);
    f[179] = 1;
    std::memset(f.data() + kRec, 0x5A, kRec);
    pstr(f.data() + kRec, 0, 20, "Keepme");
    return f;
}

std::vector<uint8_t> wolves() {
    std::vector<uint8_t> f(kMon * 11, 0);
    for (int i = 0; i < 11; i++) {
        uint8_t* rec = f.data() + i * kMon;
        pstr(rec, 0, 60, "Wolf");
        w32(rec, 61, 2);
        w32(rec, 65, 10);
        pstr(rec, 69, 60, "Bite");
        w32(rec, 130, 3);
        w32(rec, 134, 4);
        pstr(rec, 138, 100, "Down");
    }
    return f;
}

void save(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write((const char*)bytes.data(), (std::streamsize)bytes.size());
}

std::vector<uint8_t> load(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

fs::path stage(const char* name) {
    fs::path exe = fs::path(AXIS_TEST_DATA).parent_path().parent_path() / "doors" / "lordfast" / "FAST.EXE";
    fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file(exe, root / "FAST.EXE");
    save(root / "PLAYER.DAT", warriors());
    save(root / "LENEMY.DAT", wolves());
    return root;
}

DoorConfig cfgFor(const fs::path& root, const char* alias) {
    DoorConfig c;
    c.dir = root.string();
    c.exe = "FAST.EXE";
    c.args = "1";
    c.localMode = false;
    c.dropFile = DropFile::DoorSys;
    c.timeLimitSec = 180;
    c.caller.alias = alias;
    c.caller.realName = alias;
    c.caller.node = 1;
    c.sleepMs = [](uint32_t) {};
    return c;
}

bool waitFor(DoorSession& me, ScriptStream& mine, const char* needle, int budgetMs) {
    auto t0 = std::chrono::steady_clock::now();
    while (plain(mine.output()).find(needle) == std::string::npos) {
        if (me.result() != DoorResult::Running) return false;
        me.step(20);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms > budgetMs) return false;
    }
    return true;
}

void expect(DoorSession& me, ScriptStream& mine, const char* needle) {
    bool ok = waitFor(me, mine, needle, 3000);
    if (!ok)
        INFO("missing: " << needle << " result " << (int)me.result() << " exit " << me.exitCode() << "\n"
                         << me.debugState() << "\n" << tail(plain(mine.output())));
    CHECK(ok);
}

void pushKey(DoorSession& me, ScriptStream& mine, char key, const char* needle) {
    mine.clearOutput();
    mine.push(std::string(1, key));
    expect(me, mine, needle);
}

void untilExit(DoorSession& me) {
    auto t0 = std::chrono::steady_clock::now();
    while (me.result() == DoorResult::Running) {
        me.step(20);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms > 3000) break;
    }
}

}  // namespace

TEST_CASE("lord fast heals then wins and leaves the other warrior") {
    fs::path root = stage("axis_lordfast_play");
    std::vector<uint8_t> before = load(root / "PLAYER.DAT");
    axis::pc::StdHostFs host(root);
    LockTable            locks;
    ScriptStream         line;
    DoorConfig           cfg = cfgFor(root, "Redking");
    DoorSession          door(cfg, host, locks, line, 1);

    expect(door, line, "Rex");
    std::string screen = plain(line.output());
    CHECK(screen.find("Death Knight") != std::string::npos);
    CHECK(screen.find("10/20") != std::string::npos);
    CHECK(screen.find("forest 2") != std::string::npos);
    CHECK(screen.find("gold 100") != std::string::npos);
    CHECK(screen.find("Stick") != std::string::npos);
    CHECK(screen.find("Coat") != std::string::npos);

    pushKey(door, line, 'I', "You rest");
    screen = plain(line.output());
    CHECK(screen.find("20/20") != std::string::npos);
    CHECK(screen.find("gold 70") != std::string::npos);

    pushKey(door, line, 'F', "You win.");
    screen = plain(line.output());
    CHECK(screen.find("Faced Wolf") != std::string::npos);
    CHECK(screen.find("forest 1") != std::string::npos);
    CHECK(screen.find("gold 80") != std::string::npos);
    CHECK(screen.find("xp 13") != std::string::npos);

    line.clearOutput();
    line.push("Q");
    untilExit(door);
    CHECK(door.result() == DoorResult::Exited);
    CHECK(door.exitCode() == 0);

    std::vector<uint8_t> after = load(root / "PLAYER.DAT");
    REQUIRE(after.size() == before.size());
    CHECK(r16(after.data(), 72) == 20);
    CHECK(r16(after.data(), 105) == 1);
    CHECK(r32(after.data(), 109) == 80);
    CHECK(r32(after.data(), 113) == 5);
    CHECK(r16(after.data(), 119) == 12);
    CHECK(r32(after.data(), 158) == 13);
    CHECK(std::memcmp(after.data() + kRec, before.data() + kRec, kRec) == 0);
}

TEST_CASE("lord fast does not create a warrior") {
    fs::path root = stage("axis_lordfast_none");
    std::vector<uint8_t> before = load(root / "PLAYER.DAT");
    axis::pc::StdHostFs host(root);
    LockTable            locks;
    ScriptStream         line;
    DoorConfig           cfg = cfgFor(root, "Ghost");
    DoorSession          door(cfg, host, locks, line, 1);

    expect(door, line, "No warrior for this name.");
    line.clearOutput();
    line.push("Q");
    untilExit(door);
    CHECK(door.result() == DoorResult::Exited);
    CHECK(door.exitCode() == 0);
    CHECK(load(root / "PLAYER.DAT") == before);
}
