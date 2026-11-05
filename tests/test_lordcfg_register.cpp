// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// LORDCFG's registration screen in our runtime (Rev saw a runtime error 101, "disk write error",
// when registering on the board). Needs AXIS_LORD_DIR (the unpacked LORD folder).
#include <cstdio>
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
std::string text(const DoorSession& s) {
    std::string t;
    for (auto& l : s.screen()) t += l + "\n";
    return t;
}
bool waitFor(DoorSession& s, const std::string& pattern, int maxMs = 15000) {
    std::regex re(pattern, std::regex::icase);
    for (int t = 0; t < maxMs; t += 50) {
        if (std::regex_search(text(s), re)) return true;
        if (!s.step(50)) return std::regex_search(text(s), re);
    }
    return false;
}
void settle(DoorSession& s) {
    std::string last;
    for (int t = 0, still = 0; t < 3000 && still < 3; t += 100) {
        if (!s.step(100)) break;
        std::string now = text(s);
        still = now == last ? still + 1 : 0;
        last = now;
    }
}
}  // namespace

TEST_CASE("LORDCFG: walk to registration and show each screen") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;
    fs::path d = fs::temp_directory_path() / "axis_lordcfg_reg";
    fs::remove_all(d);
    fs::copy(src, d, fs::copy_options::recursive);
    axis::pc::StdHostFs fsys(d);
    LockTable           locks;
    ScriptStream        keys;
    DoorConfig          cfg;
    cfg.dir = d.string();
    cfg.exe = "LORDCFG.EXE";
    if (std::getenv("AXIS_DOS_TRACE")) cfg.trace = stderr;
    DoorSession s(cfg, fsys, locks, keys, 1);
    settle(s);
    std::printf("=== START\n%s", text(s).c_str());
    const char* steps = std::getenv("AXIS_LORDCFG_KEYS");  // e.g. "R" then more, separated by |
    std::string ks = steps ? steps : "";
    for (size_t from = 0; from <= ks.size() && !ks.empty();) {
        size_t bar = ks.find('|', from);
        std::string k = ks.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
        for (auto& c : k) if (c == '~') c = '\r';
        keys.push(k);
        settle(s);
        std::printf("=== AFTER [%s]\n%s", k.c_str(), text(s).c_str());
        if (bar == std::string::npos) break;
        from = bar + 1;
    }
}
