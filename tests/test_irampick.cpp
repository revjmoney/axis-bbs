// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstdint>
#include <vector>

#include "doctest.h"
#include "door/irampick.h"

using axis::door::ExecTake;
using axis::door::pickInstructionRam;

namespace {
struct Script {
    std::vector<ExecTake> takes;
    std::vector<void*>    freed;
    size_t                next = 0;
    int                   calls = 0;
    size_t                largest = 64 * 1024;
    ExecTake take(size_t bytes) {
        calls++;
        if (next >= takes.size()) return {};
        ExecTake t = takes[next++];
        if (t.ptr && !t.bytes) t.bytes = bytes;
        return t;
    }
};
}  // namespace

TEST_CASE("instruction RAM search skips a failed size, returns shared blocks, and keeps pure IRAM") {
    Script s;
    int    shared = 0x11, pure = 0x22;
    s.takes = {{nullptr, 0, false}, {&shared, 60 * 1024, true}, {&pure, 36 * 1024, false}};
    auto picked = pickInstructionRam([&] { return s.largest; }, [&](size_t n) { return s.take(n); },
                                    [&](void* p) { s.freed.push_back(p); });
    CHECK(picked.ptr == &pure);
    CHECK(picked.bytes == 36 * 1024);
    CHECK(picked.sharedHeld == 1);
    REQUIRE(s.freed.size() == 1);
    CHECK(s.freed[0] == &shared);
}

TEST_CASE("the first pure instruction block is kept and nothing else is taken") {
    Script s;
    int    pure = 1;
    s.largest = 60 * 1024;
    s.takes = {{&pure, 60 * 1024, false}};
    auto picked = pickInstructionRam([&] { return s.largest; }, [&](size_t n) { return s.take(n); },
                                    [&](void* p) { s.freed.push_back(p); });
    CHECK(picked.ptr == &pure);
    CHECK(picked.bytes == 60 * 1024);
    CHECK(s.freed.empty());
    CHECK(s.next == 1);
}

TEST_CASE("only shared blocks means none is kept and every one is freed") {
    Script s;
    int    a = 1, b = 2;
    s.takes = {{&a, 40 * 1024, true}, {&b, 20 * 1024, true}, {nullptr, 0, false}};
    s.largest = 40 * 1024;
    auto picked = pickInstructionRam([&] { return s.largest; }, [&](size_t n) { return s.take(n); },
                                    [&](void* p) { s.freed.push_back(p); });
    CHECK(picked.ptr == nullptr);
    CHECK(picked.bytes == 0);
    REQUIRE(s.freed.size() == 2);
    CHECK(s.freed[0] == &b);  // set-aside blocks are returned last-in first-out
    CHECK(s.freed[1] == &a);
}

TEST_CASE("three failed takes stop the search with nothing held") {
    Script s;
    s.largest = 12 * 1024;
    auto picked = pickInstructionRam([&] { return s.largest; }, [&](size_t) { return s.take(0); },
                                    [&](void* p) { s.freed.push_back(p); });
    CHECK(picked.ptr == nullptr);
    CHECK(picked.misses == 1);
    CHECK(s.freed.empty());
    CHECK(s.calls == 3);  // the asked size, then two pages smaller
}
