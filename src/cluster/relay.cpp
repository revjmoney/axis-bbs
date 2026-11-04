// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "cluster/relay.h"

#include <algorithm>

namespace axis::cluster {

bool Relay::move(Pipe& from, Pipe& to, std::string& held) {
    bool closed = false;
    if (held.size() < kMaxWaiting) {  // room: take what the sender has
        uint8_t buf[512];
        size_t  room = kMaxWaiting - held.size();
        int     n = from.read(buf, room < sizeof buf ? room : sizeof buf);
        if (n > 0) held.append((const char*)buf, (size_t)n);
        else if (n < 0) closed = true;
    }
    while (!held.empty()) {
        int w = to.write((const uint8_t*)held.data(), held.size());
        if (w < 0) return false;
        if (w == 0) break;
        held.erase(0, (size_t)w);
    }
    if (held.empty()) std::string().swap(held);  // give the memory back
    return !(closed && held.empty());  // a closed sender is done once its last bytes went out
}

bool Relay::pump() {
    bool a = move(caller_, node_, toNode_);
    bool b = move(node_, caller_, toCaller_);
    return a && b;
}

uint32_t pickFreeNode(const std::vector<NodeInfo>& nodes, int mainNumber, const std::vector<uint32_t>& reserved) {
    const NodeInfo* best = nullptr;
    for (const NodeInfo& n : nodes) {
        if (n.number == mainNumber || !n.ip || !n.handle.empty() || !n.activity.empty()) continue;
        if (std::find(reserved.begin(), reserved.end(), n.ip) != reserved.end()) continue;
        if (!best || n.number < best->number) best = &n;
    }
    return best ? best->ip : 0;
}

}  // namespace axis::cluster
