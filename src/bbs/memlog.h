// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The low-memory web notes ("ddns: waiting for memory", Discord lines dropped) go to the event log at most once an hour.
// Portable so the PC tests check the same timing the board uses.
#pragma once
#include <cstdint>

namespace axis::bbs {

constexpr uint32_t kMemLogEveryMs = 3600000u;

// Whether to look for news to log now: before the first note always, then once an hour has passed since the last note
// (lastMs is stamped by the caller only when it really logged one, so a quiet hour does not push the next note back).
inline bool gateLogDue(uint32_t nowMs, uint32_t lastMs, bool logged) { return !logged || nowMs - lastMs > kMemLogEveryMs; }

}  // namespace axis::bbs
