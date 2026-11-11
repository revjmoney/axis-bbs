// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Rev's two notice-signing public keys (hex). Two so one can be rotated without a flag day.
#pragma once
#include <string>

namespace axis::net {

// i is 0 or 1. Anything else returns an empty string, which never verifies.
const std::string& revKey(int i);
// Replace a key at runtime (key rotation). Ignored for i outside 0..1.
void setRevKey(int i, const std::string& hex);
// The keys compiled in (what the runtime keys start as), so a saved override can be told apart.
const std::string& builtinRevKey(int i);
// Test hook: replace both keys (and the built-ins they start from).
void setRevKeysForTest(const std::string& a, const std::string& b);

}  // namespace axis::net
