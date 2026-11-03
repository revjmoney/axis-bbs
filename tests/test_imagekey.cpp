// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <vector>

#include "doctest.h"
#include "door/imagekey.h"

using axis::door::imageKey;

TEST_CASE("cache key is stable") {
    std::vector<uint8_t> exe(1000, 0x42);
    CHECK(imageKey(exe.data(), exe.size(), 0x0200) == imageKey(exe.data(), exe.size(), 0x0200));
    // FNV-1a: offset basis, then each byte xored in and multiplied by the prime (two segment bytes)
    uint32_t h = 0x811C9DC5u;
    for (uint8_t byte : {0x00, 0x02}) h = (h ^ byte) * 16777619u;
    CHECK(imageKey(nullptr, 0, 0x0200) == h);
}

TEST_CASE("cache key changes when the exe changes") {
    std::vector<uint8_t> a(1000, 0x42), b = a;
    b[500] ^= 1;
    CHECK(imageKey(a.data(), a.size(), 0x0200) != imageKey(b.data(), b.size(), 0x0200));
    CHECK(imageKey(a.data(), a.size(), 0x0200) != imageKey(a.data(), 999, 0x0200));
}

TEST_CASE("cache key changes with the load segment") {
    std::vector<uint8_t> a(1000, 0x42);
    CHECK(imageKey(a.data(), a.size(), 0x0200) != imageKey(a.data(), a.size(), 0x0300));
}

TEST_CASE("cache key fed in pieces equals the one-shot key") {
    std::vector<uint8_t> a(5000);
    for (size_t i = 0; i < a.size(); i++) a[i] = (uint8_t)(i * 7);
    uint32_t h = axis::door::imageKeyStart();
    h = axis::door::imageKeyAdd(h, a.data(), 1234);
    h = axis::door::imageKeyAdd(h, a.data() + 1234, a.size() - 1234);
    CHECK(axis::door::imageKeyFinish(h, 0x0200) == imageKey(a.data(), a.size(), 0x0200));
}
