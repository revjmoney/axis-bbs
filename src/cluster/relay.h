// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Callers dial one address. When the main's own node is busy, it passes a new caller through to a
// free node's telnet port: bytes go both ways untouched (the node does its own telnet talk with
// the caller). A side that can't keep up holds at most kMaxWaiting bytes; then the relay stops
// reading the other side until it catches up, so a relay never needs much of the main's heap.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "cluster/cluster.h"

namespace axis::cluster {

class Pipe {  // one TCP connection, without blocking
public:
    virtual int read(uint8_t* buf, size_t n) = 0;         // bytes read, 0 none yet, < 0 closed
    virtual int write(const uint8_t* p, size_t n) = 0;    // bytes taken (maybe fewer), < 0 closed
    virtual ~Pipe() = default;
};

class Relay {
public:
    static constexpr size_t kMaxWaiting = 2048;

    Relay(Pipe& caller, Pipe& node) : caller_(caller), node_(node) {}
    bool   pump();  // move what's ready both ways; false once either side has closed
    size_t waiting() const { return toNode_.size() + toCaller_.size(); }

private:
    bool move(Pipe& from, Pipe& to, std::string& held);  // false once a side closed
    Pipe&       caller_;
    Pipe&       node_;
    std::string toNode_, toCaller_;
};

// The node a busy main should pass a caller to: the lowest-numbered one other than the main with
// nobody on it, skipping ones just given a caller (their status hasn't caught up). 0 = none.
uint32_t pickFreeNode(const std::vector<NodeInfo>& nodes, int mainNumber, const std::vector<uint32_t>& reserved);

}  // namespace axis::cluster
