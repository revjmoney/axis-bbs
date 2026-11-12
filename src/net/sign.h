// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Board signing for the nets: an Ed25519 key per board, signing and checking of text,
// and the short board id derived from the public key. Keys and signatures travel as hex.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace axis::net {

// secret is the 64 byte expanded Ed25519 secret key (seed then public key), which is what
// signing needs. pub is the 32 byte public key.
struct KeyPair {
    uint8_t secret[64];
    uint8_t pub[32];
};

KeyPair keyFromSeed(const uint8_t seed[32]);
void wipe(KeyPair& k);  // zeroes the secret and the public key (crypto_wipe, never optimised away)
std::string signHex(const KeyPair& k, const std::string& msg);  // 128 hex characters
bool verifyHex(const std::string& pubHex, const std::string& msg, const std::string& sigHex);
std::string toHex(const uint8_t* p, size_t n);
bool fromHex(const std::string& hex, uint8_t* out, size_t n);
// First 16 hex characters of SHA-256 over the 32 public key bytes.
std::string boardId(const std::string& pubHex);

}  // namespace axis::net
