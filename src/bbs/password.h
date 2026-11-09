// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Salted password hashes (PBKDF2-HMAC-SHA256; plain salted SHA-256 from older boards still checks). Passwords never leave this file in clear text.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

// PBKDF2-HMAC-SHA256 rounds for new hashes. Each round is two SHA-256 blocks, and the board's whole
// loop (every node, the cluster link) waits while a login checks one, so this stays well under a
// second on an ESP32. A stored hash names its own count ("pbkdf2-sha256$<rounds>$<hex>"), so hashes
// made with another count still check, and a good login rewrites them with this one.
constexpr uint32_t kPasswordRounds = 10000;  // about 0.5 s on an ESP32 (52 us a round)

std::string sha256Hex(const std::string& data);
std::string pbkdf2Sha256Hex(const std::string& password, const std::string& salt, uint32_t rounds);  // 32-byte key, hex
uint32_t    passwordRounds(const std::string& hash);  // a PBKDF2 hash's round count, 0 for a legacy SHA-256 hash
bool        needsRehash(const std::string& hash);     // legacy, or another round count than kPasswordRounds
std::string hmacSha256Raw(const std::string& key, const std::string& data);  // 32 raw bytes
std::string makeSalt(uint32_t (*rng)());  // 32 hex characters (16 random bytes)
std::string hashPassword(const std::string& salt, const std::string& password);
bool        checkPassword(const std::string& salt, const std::string& hash, const std::string& password);

}  // namespace axis::bbs
