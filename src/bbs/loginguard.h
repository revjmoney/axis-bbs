// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Brute-force brake: an address (IPv4) that fails three calls within ten minutes is turned away for
// five minutes, then ten, twenty ... up to an hour for a repeat offender. A good login clears it.
// A fixed small table: the oldest entry makes room, so a flood of addresses can't eat the heap.
#pragma once
#include <cstddef>
#include <cstdint>

namespace axis::bbs {

class LoginGuard {
public:
    static constexpr size_t   kMaxTracked = 16;
    static constexpr int      kStrikes = 3;
    static constexpr uint32_t kWindowMs = 10 * 60000u, kFirstLockS = 300, kMaxLockS = 3600;

    void     failed(uint32_t ip, uint32_t nowMs);
    void     succeeded(uint32_t ip);
    bool     allowed(uint32_t ip, uint32_t nowMs) const { return secondsLeft(ip, nowMs) == 0; }
    uint32_t secondsLeft(uint32_t ip, uint32_t nowMs) const;
    size_t   tracked() const;

private:
    struct Entry {
        uint32_t ip = 0, first = 0, lockedAt = 0, lockS = 0, seen = 0;
        uint8_t  strikes = 0, locks = 0;
        bool     used = false;
    };
    Entry*       find(uint32_t ip);
    const Entry* find(uint32_t ip) const { return const_cast<LoginGuard*>(this)->find(ip); }
    Entry        table_[kMaxTracked];
};

}  // namespace axis::bbs
