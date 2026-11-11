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
    CHECK(hash.rfind("pbkdf2-sha256$", 0) == 0);
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

TEST_CASE("PBKDF2-HMAC-SHA256 matches the published vectors") {
    CHECK(pbkdf2Sha256Hex("password", "salt", 1) == "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(pbkdf2Sha256Hex("password", "salt", 2) == "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
    CHECK(pbkdf2Sha256Hex("password", "salt", 4096) == "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    CHECK(pbkdf2Sha256Hex(std::string(100, 'k'), "salt", 3) == "219c68bb88f1010f4a763b7c8fd58639c2280baf6820814c6d404891fcd85c3d");  // a key over 64 bytes
}

TEST_CASE("a hash keeps its own round count: the old 200000-round hashes still check") {
    const std::string salt = "00112233445566778899aabbccddeeff";
    const std::string old = "pbkdf2-sha256$200000$8a9c52286c39cb1e25c31ef1abf35c3ffe17f4a5a21e19b8808801ccc95f6ef1";
    CHECK(checkPassword(salt, old, "tool path 42"));
    CHECK_FALSE(checkPassword(salt, old, "tool path 43"));
    CHECK(needsRehash(old));
    CHECK(checkPassword(salt, "pbkdf2-sha256$2$" + pbkdf2Sha256Hex("pw", salt, 2), "pw"));
    CHECK_FALSE(checkPassword(salt, "pbkdf2-sha256$0$" + pbkdf2Sha256Hex("pw", salt, 1), "pw"));  // nonsense counts never pass
    CHECK_FALSE(checkPassword(salt, "pbkdf2-sha256$x$" + pbkdf2Sha256Hex("pw", salt, 1), "pw"));
}

TEST_CASE("new hashes use the board's round count and do not need a rehash") {
    std::string salt = makeSalt(fakeRng), hash = hashPassword(salt, "chips fly");
    CHECK(hash.rfind("pbkdf2-sha256$" + std::to_string(kPasswordRounds) + "$", 0) == 0);
    CHECK_FALSE(needsRehash(hash));
    CHECK(needsRehash(sha256Hex(salt + "chips fly")));  // a legacy hash
    CHECK(passwordRounds(hash) == kPasswordRounds);
    CHECK(passwordRounds(sha256Hex("x")) == 0);
}

TEST_CASE("the two-part HMAC equals the HMAC of the joined bytes (cluster frame tags stay the same)") {
    std::string key = "link-key-0123456789", head = std::string("M\x05\x00\x00\x00\x09", 6), body(7000, 'q');
    body[17] = '\0';
    CHECK(axis::bbs::hmacSha256Raw(key, head, body.data(), body.size()) == axis::bbs::hmacSha256Raw(key, head + body));
    CHECK(axis::bbs::hmacSha256Raw(key, "", body.data(), 0) == axis::bbs::hmacSha256Raw(key, ""));
}
