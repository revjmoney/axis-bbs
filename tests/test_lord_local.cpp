// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Stage 1 exit test: the real LORD 4.07 runs in our runtime in local mode, through a new
// character, a forest fight and a clean quit. Needs AXIS_LORD_DIR (the unpacked LORD folder).
#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>

#include "doctest.h"
#include "door/bytestream.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"

using namespace axis::door;
namespace fs = std::filesystem;

namespace {
std::string screenText(const DoorSession& s) {
    std::string t;
    for (auto& l : s.screen()) t += l + "\n";
    return t;
}

// run the door until the screen matches `pattern` (or the door ends / times out)
bool waitFor(DoorSession& s, const std::string& pattern, int maxMs = 15000) {
    std::regex re(pattern, std::regex::icase);
    for (int t = 0; t < maxMs; t += 50) {
        if (std::regex_search(screenText(s), re)) return true;
        if (!s.step(50)) return std::regex_search(screenText(s), re);
    }
    return false;
}

// type like a person: wait until the screen has stopped changing (LORD clears its keyboard buffer
// whenever it redraws a prompt), then send the keys
void send(DoorSession& s, ScriptStream& keys, const std::string& k) {
    std::string last;
    int         still = 0;
    for (int t = 0; t < 3000 && still < 3; t += 100) {
        if (!s.step(100)) break;
        std::string now = screenText(s);
        still = now == last ? still + 1 : 0;
        last = now;
    }
    keys.push(k);
}

// a fresh copy of the LORD folder; runs LORDCFG once (through our runtime) to create lord.dat
fs::path freshLord(const char* src) {
    fs::path d = fs::temp_directory_path() / "axis_lord_local";
    fs::remove_all(d);
    fs::copy(src, d, fs::copy_options::recursive);
    for (auto& e : fs::directory_iterator(d)) {
        std::string n = e.path().filename().string();
        for (char& c : n) c = (char)std::tolower((unsigned char)c);
        if (n == "player.dat" || n == "lord.dat" || n == "playext.dax") fs::remove(e.path());
    }
    axis::pc::StdHostFs fsys(d);
    LockTable           locks;
    ScriptStream        keys;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "LORDCFG.EXE";
    DoorSession cfgRun(cfg, fsys, locks, keys, 1);
    if (waitFor(cfgRun, "Create a NEW LORD.DAT")) keys.push("A");
    if (waitFor(cfgRun, "Exit the program")) keys.push("x");
    cfgRun.run();
    return d;
}
}  // namespace

TEST_CASE("LORD 4.07 plays locally in our runtime") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;  // skipped without the LORD folder
    fs::path d = freshLord(src);
    REQUIRE(fs::exists(d / "lord.dat"));

    axis::pc::StdHostFs fsys(d);
    LockTable           locks;
    ScriptStream        keys;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "LORD.EXE";
    cfg.args = "0 NOEMS /DREW";
    DoorSession s(cfg, fsys, locks, keys, 1);

    REQUIRE(waitFor(s, "Name or handle"));
    send(s, keys, "\r");
    // past the title art and the main menu
    for (int i = 0; i < 4 && !waitFor(s, "choice, warrior", 3000); i++) send(s, keys, "\r");
    REQUIRE(waitFor(s, "choice, warrior"));
    send(s, keys, "E");
    REQUIRE(waitFor(s, "join this place"));
    send(s, keys, "Y");
    REQUIRE(waitFor(s, "Name:"));
    send(s, keys, "Revman\r");
    REQUIRE(waitFor(s, "Revman\\? \\[Y\\]"));
    send(s, keys, "Y");
    REQUIRE(waitFor(s, "gender"));
    send(s, keys, "M");
    REQUIRE(waitFor(s, "Pick one"));
    send(s, keys, "K");
    // news and story screens until the town prompt
    for (int i = 0; i < 8 && !waitFor(s, "Your command", 2500); i++) send(s, keys, "\r");
    REQUIRE(waitFor(s, "Your command"));
    // on first arriving in town LORD may flush the keyboard once more: press F again if needed
    bool forest = false;
    for (int i = 0; i < 4 && !forest; i++) {
        send(s, keys, "F");
        forest = waitFor(s, "The Forest +\\(L,H,R,Q\\)", 3000);
    }
    REQUIRE(forest);
    // looking around sometimes finds an event instead of a monster: keep looking
    bool fight = false;
    for (int i = 0; i < 10 && !fight; i++) {
        // some events send the character back to town: head out to the forest again
        auto lines = s.screen();
        for (int r = 24; r >= 0; r--) {
            if (lines[r].find("Your command") == std::string::npos) continue;
            if (r > 0 && lines[r - 1].find("(F,S,K") != std::string::npos) send(s, keys, "F");
            break;
        }
        send(s, keys, "L");
        fight = waitFor(s, "Attack", 3000);
        if (!fight) send(s, keys, "N\r");  // a forest event: decline its question, dismiss it
    }
    if (!fight) MESSAGE("screen after looking:\n" << screenText(s));
    REQUIRE(fight);
    // fight until it ends (we win, lose, or it runs)
    for (int i = 0; i < 20 && waitFor(s, "\\(A\\)ttack|Attack \\(", 2000); i++) send(s, keys, "A");
    // back to town and quit
    for (int i = 0; i < 6 && !waitFor(s, "The Forest|Town Square", 2000); i++) send(s, keys, "\r");
    send(s, keys, "R");
    waitFor(s, "Your command", 4000);
    send(s, keys, "Q");
    waitFor(s, "sure|quit|leave", 4000);
    send(s, keys, "Y");
    for (int i = 0; i < 10 && s.step(200); i++) send(s, keys, "\r");
    s.run();
    MESSAGE("LORD finished: result " << (int)s.result() << ", peak RAM pages " << s.memStats().peakRamPages << " ("
                                     << s.memStats().peakRamPages * 4 << " KB), " << s.instructions() << " instructions");
    CHECK(s.result() == DoorResult::Exited);
    CHECK(s.memStats().peakRamPages * 4 <= 128);
}
