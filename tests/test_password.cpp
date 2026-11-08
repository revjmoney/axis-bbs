// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/password.h"

using namespace axis::bbs;

static uint32_t seq = 1;
static uint32_t fakeRng() { return seq = seq * 1103515245u + 12345u; }

TEST_CASE("sha256 of abc") {
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("sha256 of empty") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("check accepts the right password and rejects others") {
    std::string salt = makeSalt(fakeRng);
    std::string hash = hashPassword(salt, "tool path 42");
    CHECK(checkPassword(salt, hash, "tool path 42"));
    CHECK_FALSE(checkPassword(salt, hash, "tool path 43"));
    CHECK_FALSE(checkPassword(salt, hash, ""));
    CHECK(hash.find("tool") == std::string::npos);
    CHECK(hash.rfind("pbkdf2-sha256$200000$", 0) == 0);
}

TEST_CASE("legacy salted SHA-256 hashes remain verifiable") {
    const std::string salt = "00112233445566778899aabbccddeeff";
    const std::string oldHash = sha256Hex(salt + "old password");
    CHECK(checkPassword(salt, oldHash, "old password"));
    CHECK_FALSE(checkPassword(salt, oldHash, "wrong password"));
}

TEST_CASE("salt is 32 hex chars and differs per call") {
    std::string a = makeSalt(fakeRng), b = makeSalt(fakeRng);
    CHECK(a.size() == 32);
    CHECK(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(a != b);
}
