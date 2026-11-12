// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Storage for Rev's two notice-signing public keys. Slot 0 is Rev's daily key (board id
// 57d0e0fb6643ef68). Slot 1 is Rev's backup key (board id 271575a62d472b4c), whose private half
// is kept offline. verifyNotice rejects every notice while the selected key is all zero.
#include "net/revkeys.h"

namespace axis::net {

namespace {
// Rev's two PUBLIC keys (safe to commit), slot 0 = daily key, slot 1 = backup key, each 64
// lowercase hex characters (the "pub" line printed by axis_netkey). Never put a seed or
// private key in this file, and never invent a key.
const std::string g_real[2] = {"fc6d9e865ceb2af25243fd89fd820476ba4da49e39a8e196f03e95c3d79a0935",
                               "3fca4659518b7b3e8624334dbf7c7b61365235f227663c93c113166527f33b20"};
std::string g_builtin[2] = {g_real[0], g_real[1]};
std::string g_keys[2] = {g_builtin[0], g_builtin[1]};
const std::string g_empty;
}  // namespace

const std::string& revKey(int i) {
    if (i < 0 || i > 1) return g_empty;
    return g_keys[i];
}

const std::string& builtinRevKey(int i) {
    if (i < 0 || i > 1) return g_empty;
    return g_builtin[i];
}

void setRevKey(int i, const std::string& hex) {
    if (i < 0 || i > 1) return;
    g_keys[i] = hex;
}

void setRevKeysForTest(const std::string& a, const std::string& b) {
    g_builtin[0] = g_keys[0] = a;
    g_builtin[1] = g_keys[1] = b;
}

void resetRevKeysForTest() {
    g_builtin[0] = g_keys[0] = g_real[0];
    g_builtin[1] = g_keys[1] = g_real[1];
}

}  // namespace axis::net
