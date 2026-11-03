// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "door/pathresolve.h"

using axis::door::resolveDosPath;

namespace {
// a pretend case-sensitive store: folder -> names
std::map<std::string, std::vector<std::string>> tree = {
    {"", {"lord.dat", "LORD.EXE", "Data"}},
    {"Data", {"Player.Dat"}},
};
std::vector<std::string> list(const std::string& dir) {
    auto it = tree.find(dir);
    return it == tree.end() ? std::vector<std::string>() : it->second;
}
}  // namespace

TEST_CASE("resolver matches names in any case") {
    CHECK(resolveDosPath("", "C:\\LORD.DAT", list) == "lord.dat");
    CHECK(resolveDosPath("", "lord.exe", list) == "LORD.EXE");
    CHECK(resolveDosPath("", "DATA\\PLAYER.DAT", list) == "Data/Player.Dat");
}

TEST_CASE("resolver keeps the asked case for a new file") {
    CHECK(resolveDosPath("", "NEW.DAT", list) == "NEW.DAT");
    CHECK(resolveDosPath("", "data\\New.dat", list) == "Data/New.dat");
}

TEST_CASE("resolver uses the current directory for relative paths") {
    CHECK(resolveDosPath("DATA", "player.dat", list) == "Data/Player.Dat");
    CHECK(resolveDosPath("DATA", "\\LORD.DAT", list) == "lord.dat");
}

TEST_CASE("resolver clamps .. at the root") {
    CHECK(resolveDosPath("", "..\\..\\LORD.DAT", list) == "lord.dat");
    CHECK(resolveDosPath("DATA", "..\\LORD.DAT", list) == "lord.dat");
}

TEST_CASE("resolver rejects a part with a colon") {
    CHECK(resolveDosPath("", "C:\\D:\\X.DAT", list).empty());
    CHECK(resolveDosPath("", "C:\\", list).empty());  // the root itself is not a file
}
