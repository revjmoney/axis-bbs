// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The web task (Discord lines, dynamic DNS) only exists while it has work. These two decisions are shared by the
// main loop (start the task?) and the task itself (end now?), so they can never disagree. Portable for the PC tests.
#pragma once
#include <cstdint>

namespace axis::bbs {

constexpr uint32_t kWebFirstCheckMs = 60u * 1000u;       // after boot: time to clear bad settings before any network call
constexpr uint32_t kWebCheckEveryMs = 10u * 60u * 1000u;  // the DNS check cadence

// A DNS check is due: past the first minute, then the first time, on @DDNS now, or once the cadence has passed.
inline bool webCheckDue(uint32_t nowMs, bool first, bool soon, uint32_t lastMs) {
    return nowMs > kWebFirstCheckMs && (first || soon || nowMs - lastMs > kWebCheckEveryMs);
}

// Work for the task: a handed-over DNS check or a queued Discord line, and no door holding the heap.
inline bool webHasWork(bool door, bool checkPending, uint32_t queuedLines) { return !door && (checkPending || queuedLines > 0); }

}  // namespace axis::bbs
