// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Storage for Rev's two notice-signing public keys. Placeholders (all zero) until the real
// keys are made; verifyNotice rejects every notice while the selected key is all zero.
#include "net/revkeys.h"

namespace axis::net {

namespace {
// WHERE REV'S TWO PUBLIC KEYS GO: the two all-zero strings in g_real below, slot 0 then slot 1,
// each 64 lowercase hex characters (the "pub" line printed by axis_netkey). A key lands here
// only after Rev pastes it himself. Never put a seed in this file, and never invent a key.
const std::string g_real[2] = {std::string(64, '0'), std::string(64, '0')};
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
