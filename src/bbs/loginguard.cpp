// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/loginguard.h"

namespace axis::bbs {

LoginGuard::Entry* LoginGuard::find(uint32_t ip) {
    for (Entry& e : table_)
        if (e.used && e.ip == ip) return &e;
    return nullptr;
}

void LoginGuard::failed(uint32_t ip, uint32_t now) {
    Entry* e = find(ip);
    if (!e) {  // a free slot, or the one seen longest ago
        e = &table_[0];
        for (Entry& c : table_) {
            if (!c.used) {
                e = &c;
                break;
            }
            if (now - c.seen > now - e->seen) e = &c;
        }
        *e = Entry();
        e->used = true, e->ip = ip;
    }
    e->seen = now;
    if (!e->strikes || now - e->first > kWindowMs) e->strikes = 0, e->first = now;  // a fresh window
    if (++e->strikes >= kStrikes) {
        uint32_t s = kFirstLockS << (e->locks < 8 ? e->locks : 8);
        e->lockS = s < kMaxLockS ? s : kMaxLockS;
        e->lockedAt = now;
        e->locks++;
        e->strikes = 0;
    }
}

void LoginGuard::succeeded(uint32_t ip) {
    if (Entry* e = find(ip)) *e = Entry();
}

uint32_t LoginGuard::secondsLeft(uint32_t ip, uint32_t now) const {
    const Entry* e = find(ip);
    if (!e || !e->lockS) return 0;
    uint32_t gone = (now - e->lockedAt) / 1000;
    return gone >= e->lockS ? 0 : e->lockS - gone;
}

size_t LoginGuard::tracked() const {
    size_t n = 0;
    for (const Entry& e : table_) n += e.used ? 1 : 0;
    return n;
}

}  // namespace axis::bbs
