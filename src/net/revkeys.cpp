// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Storage for Rev's two notice-signing public keys. Placeholders (all zero) until the real
// keys are made; verifyNotice rejects every notice while the selected key is all zero.
#include "net/revkeys.h"

namespace axis::net {

namespace {
std::string g_keys[2] = {std::string(64, '0'), std::string(64, '0')};
const std::string g_empty;
}  // namespace

const std::string& revKey(int i) {
    if (i < 0 || i > 1) return g_empty;
    return g_keys[i];
}

void setRevKey(int i, const std::string& hex) {
    if (i < 0 || i > 1) return;
    g_keys[i] = hex;
}

void setRevKeysForTest(const std::string& a, const std::string& b) {
    g_keys[0] = a;
    g_keys[1] = b;
}

}  // namespace axis::net
