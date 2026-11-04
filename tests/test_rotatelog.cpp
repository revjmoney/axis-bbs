// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "doctest.h"
#include "bbs/eventlog.h"
#include "bbs/rotatelog.h"

using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {
struct Dir {
    fs::path p = fs::temp_directory_path() / "axis_rotatelog_test";
    Dir() {
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~Dir() { fs::remove_all(p); }
    std::string file(const char* n) const { return (p / n).string(); }
};
uintmax_t sizeOf(const std::string& f) { return fs::exists(f) ? fs::file_size(f) : 0; }
}  // namespace

TEST_CASE("the event log drops its oldest lines past its byte limit, not just its line limit") {
    EventLog log(40, 100);
    for (int i = 0; i < 10; i++) log.add(std::string(30, 'a' + i));
    size_t bytes = 0;
    for (auto& l : log.lines()) bytes += l.size();
    CHECK(bytes <= 100);
    CHECK(log.lines().back() == std::string(30, 'j'));
}

TEST_CASE("a log file rolls over to .old at its size limit, so the pair never passes twice the limit") {
    Dir        d;
    RotatingLog log(d.file("events.log"), d.file("events.old"), 200);
    for (int i = 0; i < 100; i++) log.append("12:00 line number " + std::to_string(i));
    CHECK(sizeOf(d.file("events.log")) <= 200);
    CHECK(sizeOf(d.file("events.old")) <= 200);
    CHECK(sizeOf(d.file("events.old")) > 0);
}

TEST_CASE("the newest lines come back after a reboot, oldest first, from both files") {
    Dir d;
    {
        RotatingLog log(d.file("events.log"), d.file("events.old"), 200);
        for (int i = 0; i < 30; i++) log.append("line " + std::to_string(i));
    }
    RotatingLog again(d.file("events.log"), d.file("events.old"), 200);
    auto        last = again.tail(5);
    REQUIRE(last.size() == 5);
    CHECK(last.front() == "line 25");
    CHECK(last.back() == "line 29");
}

TEST_CASE("a missing log is just empty") {
    Dir         d;
    RotatingLog log(d.file("events.log"), d.file("events.old"), 200);
    CHECK(log.tail(10).empty());
}
