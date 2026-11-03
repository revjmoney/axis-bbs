// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Salted SHA-256 password hashes. Passwords never leave this file in clear text.
#pragma once
#include <cstdint>
#include <string>

namespace axis::bbs {

std::string sha256Hex(const std::string& data);
std::string makeSalt(uint32_t (*rng)());  // 32 hex characters (16 random bytes)
std::string hashPassword(const std::string& salt, const std::string& password);
bool        checkPassword(const std::string& salt, const std::string& hash, const std::string& password);

}  // namespace axis::bbs
