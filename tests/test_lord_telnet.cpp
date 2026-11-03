// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Stage 2 exit test: the real LORD 4.07 played over a telnet connection (FOSSIL + DOOR.SYS),
// and hanging up mid-game ends the door without damaging its files. Needs AXIS_LORD_DIR.
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>

#include "doctest.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"
#include "platform/pc/tcpstream.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {
const char* kNode1 =
    "**Seth Able's Node Data System, for node 1\r\n"
    "BBSNAME Axis Underground CNC\r\n"
    "BBSTYPE DOORSYS\r\n"
    ";COMPORT 0\r\n"
    "FOSSIL\r\n"
    "LOCKBAUD 0\r\n"
    "STATUS ON\r\n"
    "STATFORE 15\r\n"
    "STATBACK 1\r\n"
    ";NODE1\r\n"
    "NODIRECT\r\n"
    ";UNUSED\r\n"
    "BBSDROP C:\\\r\n"
    "NOOPEN\r\n"
    "NORESET\r\n"
    ";UNUSED\r\n"
    "TASKER 4\r\n"
    "XTRATASK 0\r\n";

// a LORD copy configured for node 1 over FOSSIL with DOOR.SYS; lord.dat comes from LORDCFG
fs::path lordForTelnet(const char* src, const char* name) {
    fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::copy(src, d, fs::copy_options::recursive);
    for (auto& e : fs::directory_iterator(d)) {
        std::string n = e.path().filename().string();
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n == "player.dat" || n == "lord.dat" || n == "playext.dax") fs::remove(e.path());
    }
    std::ofstream(d / "NODE1.DAT", std::ios::binary) << kNode1;
    axis::pc::StdHostFs fsys(d);
    LockTable           locks;
    ScriptStream        keys;
    DoorConfig          cfg;
    cfg.dir = d;
    cfg.exe = "LORDCFG.EXE";
    DoorSession cfgRun(cfg, fsys, locks, keys, 1);
    std::string t;
    for (int i = 0; i < 300 && cfgRun.step(50); i++) {
        t.clear();
        for (auto& l : cfgRun.screen()) t += l;
        if (t.find("Create a NEW LORD.DAT") != std::string::npos) {
            keys.push("A");
            break;
        }
    }
    for (int i = 0; i < 300 && cfgRun.step(50); i++) {
        t.clear();
        for (auto& l : cfgRun.screen()) t += l;
        if (t.find("Exit the program") != std::string::npos) {
            keys.push("x");
            break;
        }
    }
    cfgRun.run();
    return d;
}

// the caller's side: a plain socket, text with ANSI and telnet commands stripped
struct Caller {
    axis::pc::TcpClient sock;
    std::string         seen;
    explicit Caller(uint16_t port) : sock("127.0.0.1", port) {}
    void pump() {
        std::string raw = sock.receive();
        raw = std::regex_replace(raw, std::regex("\xff[\xfb-\xfe]."), "");
        raw = std::regex_replace(raw, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), " ");
        seen += raw;
        if (seen.size() > 20000) seen.erase(0, seen.size() - 20000);
    }
    bool waitFor(const std::string& pattern, int ms = 15000) {
        std::regex re(pattern, std::regex::icase);
        for (int t = 0; t < ms; t += 20) {
            pump();
            if (std::regex_search(seen, re)) {
                seen.clear();
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return false;
    }
    void send(const std::string& s) { sock.send(s); }
};
}  // namespace

TEST_CASE("LORD 4.07 plays over telnet with FOSSIL and DOOR.SYS") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;
    fs::path d = lordForTelnet(src, "axis_lord_telnet");
    REQUIRE(fs::exists(d / "lord.dat"));

    axis::pc::TcpListener listener(0);
    uint16_t              port = listener.port();
    std::atomic<int>      result{-1};
    std::thread door([&] {
        auto                line = listener.accept(10000);
        REQUIRE(line);
        axis::pc::StdHostFs fsys(d);
        LockTable           locks;
        DoorConfig          cfg;
        cfg.dir = d;
        cfg.exe = "LORD.EXE";
        cfg.args = "1 /DREW";
        cfg.localMode = false;
        cfg.dropFile = DropFile::DoorSys;
        cfg.caller.alias = "Revman";
        cfg.caller.realName = "Rev Test";
        DoorSession s(cfg, fsys, locks, *line, 1);
        result = (int)s.run();
    });

    Caller c(port);
    // LORD picks one of several intro screens at random
    bool ok = c.waitFor("more|key|choice, warrior|egend|Seth|Able|Dragon", 30000);
    CHECK(ok);
    for (int i = 0; i < 4 && !c.waitFor("choice, warrior", 3000); i++) c.send("\r");
    c.send("E");
    REQUIRE(c.waitFor("join this place"));
    c.send("Y");
    REQUIRE(c.waitFor("Name:"));
    c.send("Revman\r\n");
    REQUIRE(c.waitFor("\\[ *Y *\\]"));  // colours inside the brackets become spaces once stripped
    c.send("Y");
    REQUIRE(c.waitFor("gender"));
    c.send("M");
    REQUIRE(c.waitFor("Pick one"));
    c.send("K");
    for (int i = 0; i < 8 && !c.waitFor("Your command", 2500); i++) c.send("\r");
    c.send("V");  // view stats: proves the game state over the wire
    CHECK(c.waitFor("Revman"));
    c.send("\r");
    c.waitFor("Your command", 5000);
    c.send("Q");
    c.waitFor("sure|quit|leave", 5000);
    c.send("Y");
    for (int i = 0; i < 50 && result.load() < 0; i++) {
        c.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    c.sock.close();
    door.join();
    CHECK(result.load() == (int)DoorResult::Exited);
}

TEST_CASE("hanging up in the middle of LORD ends the door and keeps its files") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;
    fs::path d = lordForTelnet(src, "axis_lord_hangup");
    axis::pc::TcpListener listener(0);
    uint16_t              port = listener.port();
    std::atomic<int>      result{-1};
    std::thread door([&] {
        auto                line = listener.accept(10000);
        REQUIRE(line);
        axis::pc::StdHostFs fsys(d);
        LockTable           locks;
        DoorConfig          cfg;
        cfg.dir = d;
        cfg.exe = "LORD.EXE";
        cfg.args = "1 /DREW";
        cfg.localMode = false;
        cfg.dropFile = DropFile::DoorSys;
        cfg.caller.alias = "Hangman";
        DoorSession s(cfg, fsys, locks, *line, 1);
        result = (int)s.run();
    });
    Caller c(port);
    c.waitFor("more|Press a key|choice, warrior", 20000);
    c.send("\r");
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    c.sock.close();  // the caller hangs up
    auto t0 = std::chrono::steady_clock::now();
    door.join();
    CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(30));
    int r = result.load();
    CHECK((r == (int)DoorResult::CarrierLost || r == (int)DoorResult::Exited));
    CHECK(fs::exists(d / "lord.dat"));
    CHECK(fs::file_size(d / "lord.dat") > 0);
}
