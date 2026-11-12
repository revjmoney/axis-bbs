// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net keys: signing, checking, the expanded secret key and board ids.
#include <cstring>

#include "net_testutil.h"

using namespace nettest;

TEST_CASE("keys sign and verify") {
    KeyPair k = keyOf(0x01);
    std::string pub = pubOf(k);
    std::string sig = signHex(k, "hello");
    CHECK(sig.size() == 128);
    CHECK(verifyHex(pub, "hello", sig));
    CHECK_FALSE(verifyHex(pub, "hellp", sig));  // one changed byte of the message
    std::string bad = sig;
    bad[10] = bad[10] == '0' ? '1' : '0';  // one changed signature hex character
    CHECK_FALSE(verifyHex(pub, "hello", bad));
    CHECK_FALSE(verifyHex(pubOf(keyOf(0x02)), "hello", sig));  // another key
    std::string id = boardId(pub);
    CHECK(id.size() == 16);
    CHECK(id.find_first_not_of("0123456789abcdef") == std::string::npos);
    CHECK(boardId("xyz").empty());
}

TEST_CASE("the key pair holds the 64 byte expanded secret: seed then public key") {
    uint8_t seed[32];
    for (int i = 0; i < 32; i++) seed[i] = (uint8_t)(i * 7 + 3);
    KeyPair k = keyFromSeed(seed);
    CHECK(std::memcmp(k.secret, seed, 32) == 0);
    CHECK(std::memcmp(k.secret + 32, k.pub, 32) == 0);
    // keyFromSeed leaves its argument alone (Monocypher wipes the seed it is given)
    CHECK(seed[0] == 3);
    // the same seed always gives the same key
    KeyPair k2 = keyFromSeed(seed);
    CHECK(std::memcmp(k.pub, k2.pub, 32) == 0);
}

TEST_CASE("hex helpers") {
    uint8_t b[3] = {0x00, 0xab, 0xff};
    CHECK(toHex(b, 3) == "00abff");
    uint8_t out[3];
    CHECK(fromHex("00abff", out, 3));
    CHECK(std::memcmp(out, b, 3) == 0);
    CHECK_FALSE(fromHex("00abf", out, 3));  // wrong length
    CHECK_FALSE(fromHex("00abfg", out, 3));
}
