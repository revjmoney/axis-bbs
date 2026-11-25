// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The accept gate (netgate.h). All times are uint32_t milliseconds compared by subtraction, so the 49-day wrap of
// the clock does no harm.
#include "platform/linux/netgate.h"

#include <cstdio>
#include <cstring>

bool NetGate::isLan(uint32_t ip) {
    uint8_t b[4];
    std::memcpy(b, &ip, 4);  // network order: b[0] is the first number of the dotted quad
    if (b[0] == 10 || b[0] == 127) return true;
    if (b[0] == 172 && (b[1] & 0xF0) == 16) return true;
    if (b[0] == 192 && b[1] == 168) return true;
    if (b[0] == 169 && b[1] == 254) return true;
    return false;
}

std::string NetGate::ipText(uint32_t ip) {
    uint8_t b[4];
    std::memcpy(b, &ip, 4);  // network order, same bytes inet_ntop would print
    char s[16];
    std::snprintf(s, sizeof s, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    return s;
}

uint32_t NetGate::openFrom(uint32_t ip) const {
    auto it = table_.find(ip);
    return it == table_.end() ? 0 : it->second.open;
}

bool NetGate::banned(uint32_t ip, uint32_t nowMs) const {
    auto it = table_.find(ip);
    return it != table_.end() && it->second.isBanned && nowMs - it->second.banAt < kBanMs;
}

bool NetGate::rateOver(const Entry& e, uint32_t now) const {
    return e.ring.size() >= lim_.rate_per_ip && !e.ring.empty() && now - e.ring[e.next] < kWindowMs;
}

void NetGate::note(Entry& e, uint32_t now) {
    size_t cap = lim_.rate_per_ip ? lim_.rate_per_ip : 1;
    if (e.ring.size() > cap) {  // the limit was lowered: start the count afresh
        e.ring.clear();
        e.next = 0;
    }
    if (e.ring.size() < cap) {
        e.ring.push_back(now);
        e.next = 0;  // the oldest is ring[0] until the ring is full
    } else {
        e.ring[e.next] = now;
        e.next = (e.next + 1) % cap;
    }
    e.lastSeen = now;
}

void NetGate::prune(uint32_t now) {
    for (auto it = table_.begin(); it != table_.end();) {
        const Entry& e = it->second;
        bool         banLive = e.isBanned && now - e.banAt < kBanMs;
        if (e.open == 0 && !banLive && now - e.lastSeen >= kWindowMs) it = table_.erase(it);
        else ++it;
    }
    lastPrune_ = now;
}

bool NetGate::admit(uint32_t ip, bool lan, uint32_t now) {
    // Forgets idle addresses every minute (every 5 s while very many call), so no old time outlives the clock's wrap.
    if (now - lastPrune_ >= (table_.size() >= 1024 ? 5000u : kWindowMs)) prune(now);
    auto it = table_.find(ip);
    if (it == table_.end()) {
        if (table_.size() >= kMaxTracked) {
            prune(now);
            if (table_.size() >= kMaxTracked) {  // a flood spread over very many addresses: nobody new for now
                if (now - lastFullLog_ >= 10000 || !fullLogged_) {
                    say("gate: refuse " + ipText(ip) + " (too many addresses calling at once)");
                    lastFullLog_ = now;
                    fullLogged_ = true;
                }
                return false;
            }
        }
        it = table_.emplace(ip, Entry()).first;
    }
    Entry& e = it->second;

    if (e.isBanned) {
        if (now - e.banAt < kBanMs) {
            e.lastSeen = now;
            return false;
        }
        e.isBanned = false;  // the ban is over: a clean slate
        e.over = false;
        e.ring.clear();
        e.next = 0;
    }

    bool        tooFast = rateOver(e, now);
    uint32_t    perIp = lan ? lim_.max_per_ip_lan : lim_.max_per_ip;
    bool        tooMany = e.open >= perIp;
    note(e, now);  // every attempt counts toward the rate, refused or not
    if (tooFast || tooMany) {
        const char* why = tooFast ? "too many new connections a minute" : "too many open from there";
        if (!e.over || now - e.lastRefuse >= kStreakGapMs) {  // a new run of refusals
            e.over = true;
            e.overSince = now;
            say("gate: refuse " + ipText(ip) + " (" + why + ")");
        } else if (now - e.overSince >= kWindowMs) {
            e.isBanned = true;
            e.banAt = now;
            e.over = false;
            say("gate: ban " + ipText(ip) + " 10 min");
        }
        e.lastRefuse = now;
        return false;
    }
    e.over = false;

    if (total_ >= lim_.max_callers) {  // the board is full: not this address's fault, so no ban run
        if (!fullLogged_ || now - lastFullLog_ >= 10000) {
            say("gate: refuse " + ipText(ip) + " (" + std::to_string(total_) + " callers open, the most allowed)");
            lastFullLog_ = now;
            fullLogged_ = true;
        }
        return false;
    }
    e.open++;
    total_++;
    return true;
}

void NetGate::closed(uint32_t ip) {
    auto it = table_.find(ip);
    if (it != table_.end() && it->second.open > 0) {
        it->second.open--;
        if (total_ > 0) total_--;
    }
}
