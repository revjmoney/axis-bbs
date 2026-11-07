// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Two callers, one TABLE.DAT. AXIS FOUR is a real DOS EXE on the door runtime.
#include <chrono>
#include <filesystem>
#include <string>

#include "doctest.h"

#include "door/bytestream.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {

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

DoorConfig seatCfg(const fs::path& root, const char* args, const char* drop, const char* alias, int node) {
    DoorConfig c;
    c.dir = root.string();
    c.exe = "FOUR.EXE";
    c.args = args;
    if (drop) c.dropDir = drop;
    c.localMode = false;
    c.dropFile = DropFile::DoorSys;
    c.timeLimitSec = 180;
    c.caller.alias = alias;
    c.caller.realName = alias;
    c.caller.node = node;
    c.sleepMs = [](uint32_t) {};
    return c;
}

fs::path stage(const char* name) {
    fs::path exe = fs::path(AXIS_TEST_DATA).parent_path().parent_path() / "doors" / "axisfour" / "FOUR.EXE";
    fs::path root = fs::temp_directory_path() / name;
    fs::remove_all(root);
    fs::create_directories(root);
    fs::copy_file(exe, root / "FOUR.EXE");
    return root;
}

// Step `me` until its screen contains needle. A short step of `other` lets a lock go.
bool waitFor(DoorSession& me, ScriptStream& mine, const char* needle, DoorSession* other, int budgetMs) {
    auto t0 = std::chrono::steady_clock::now();
    while (plain(mine.output()).find(needle) == std::string::npos) {
        if (me.result() != DoorResult::Running) return false;
        me.step(20);
        if (other && other->result() == DoorResult::Running) other->step(5);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms > budgetMs) return false;
    }
    return true;
}

void expect(DoorSession& me, ScriptStream& mine, const char* needle, DoorSession* other) {
    bool ok = waitFor(me, mine, needle, other, 3000);
    if (!ok)
        INFO("missing: " << needle << " result " << (int)me.result() << " exit " << me.exitCode() << "\n"
                         << me.debugState() << "\n" << tail(plain(mine.output())));
    CHECK(ok);
}

}  // namespace

TEST_CASE("axis four two nodes play one table") {
    fs::path root = stage("axis_four_win");
    axis::pc::StdHostFs host(root);
    LockTable            locks;
    ScriptStream         redLine, goldLine;
    DoorConfig           redCfg = seatCfg(root, "1", nullptr, "Redking", 1);
    DoorConfig           goldCfg = seatCfg(root, "2", "NODE2", "Goldbug", 2);
    DoorSession          red(redCfg, host, locks, redLine, 1);
    DoorSession          gold(goldCfg, host, locks, goldLine, 2);

    expect(red, redLine, "Waiting for the other node", &gold);
    expect(gold, goldLine, "Redking", &red);
    CHECK(plain(goldLine.output()).find("Goldbug") != std::string::npos);

    // Column 1 is Redking, column 2 is Goldbug. Four reds in a column wins.
    // A player blocks on the drop, and stepping the opponent lets that prompt
    // land, so look before sending and do not wipe a prompt that is already up.
    auto drop = [&](DoorSession& me, ScriptStream& mine, DoorSession& other, char key, const char* after) {
        if (plain(mine.output()).find("Your drop") == std::string::npos)
            expect(me, mine, "Your drop", &other);
        mine.clearOutput();
        mine.push(std::string(1, key));
        expect(me, mine, after, &other);
    };
    for (int i = 0; i < 3; i++) {
        drop(red, redLine, gold, '1', "Waiting for Goldbug");
        drop(gold, goldLine, red, '2', "Waiting for Redking");
    }
    drop(red, redLine, gold, '1', "You win.");
    std::string board = plain(redLine.output());
    CHECK(board.find("  O . . . . . . ") != std::string::npos);
    CHECK(board.find("  O O . . . . . ") != std::string::npos);
    CHECK(board.find("node 1 ") != std::string::npos);
    CHECK(board.find("node 2\r") != std::string::npos);

    if (plain(goldLine.output()).find("You lose.") == std::string::npos)
        expect(gold, goldLine, "You lose.", &red);
    CHECK(red.result() == DoorResult::Running);
    CHECK(gold.result() == DoorResult::Running);
}

TEST_CASE("axis four standing up frees the seat") {
    fs::path root = stage("axis_four_leave");
    axis::pc::StdHostFs host(root);
    LockTable            locks;
    ScriptStream         redLine, goldLine;
    DoorConfig           redCfg = seatCfg(root, "1", nullptr, "Redking", 1);
    DoorConfig           goldCfg = seatCfg(root, "2", "NODE2", "Goldbug", 2);
    DoorSession          red(redCfg, host, locks, redLine, 1);
    DoorSession          gold(goldCfg, host, locks, goldLine, 2);

    expect(red, redLine, "Waiting for the other node", &gold);
    expect(gold, goldLine, "Goldbug", &red);
    if (plain(redLine.output()).find("Your drop") == std::string::npos)
        expect(red, redLine, "Your drop", &gold);
    redLine.clearOutput();
    redLine.push("Q");

    auto t0 = std::chrono::steady_clock::now();
    while (red.result() == DoorResult::Running) {
        red.step(20);
        if (gold.result() == DoorResult::Running) gold.step(5);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms > 8000) break;
    }
    CHECK(red.result() == DoorResult::Exited);
    CHECK(red.exitCode() == 0);

    goldLine.clearOutput();
    expect(gold, goldLine, "Waiting for the other node", nullptr);
    CHECK(plain(goldLine.output()).find("(empty seat)") != std::string::npos);
    CHECK(gold.result() == DoorResult::Running);
}
