// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net state: board cards, Rev's signed notices (ban, hold, kill, lift, limits, rotate) applied
// by sequence number, the seen-post dedupe set, and per-board daily ingress limits.
// Days are day numbers (unix time / 86400) supplied by the caller; nothing here reads a clock.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "net/record.h"

namespace axis::net {

constexpr size_t kNetMaxCards = 512;
constexpr size_t kNetMaxSeen = 20000;
constexpr size_t kNetMaxIngressBoards = 512;
constexpr uint32_t kSeenDays = 120;
constexpr uint32_t kDefaultPostsPerDay = 50;
constexpr uint32_t kDefaultBytesPerDay = 64 * 1024;

class NetState {
public:
    bool addCard(const NetCard& c);
    bool applyNotice(const NetNotice& n);

    bool banned(const std::string& board) const;
    bool held(const std::string& postId) const;
    bool killed(const std::string& postId) const;
    uint32_t highSeq() const { return highSeq_; }
    uint32_t postsPerDay() const { return posts_; }
    uint32_t bytesPerDay() const { return bytes_; }
    size_t cardCount() const { return cards_.size(); }
    const NetCard* card(const std::string& board) const;

    // Every current card, then the latest notice per (group, target), as encoded lines.
    std::vector<std::string> snapshot() const;
    // Text for the SD card: K0/K1 lines when a Rev key was rotated, then cards, then notices.
    std::string save() const;
    // Replaces the state. Junk lines are skipped and every record is verified again.
    void load(const std::string& text);

private:
    bool applyNoticeInner(const NetNotice& n);
    std::map<std::string, NetCard> cards_;
    std::map<std::pair<std::string, std::string>, NetNotice> notices_;  // (group, target)
    std::unordered_set<std::string> banned_, held_, killed_;
    uint32_t highSeq_ = 0;
    uint32_t posts_ = kDefaultPostsPerDay;
    uint32_t bytes_ = kDefaultBytesPerDay;
};

class NetSeen {
public:
    bool add(const std::string& id, uint32_t nowDay);  // false if already seen
    void prune(uint32_t nowDay);                        // drops ids older than kSeenDays
    size_t size() const { return order_.size(); }
    std::string save() const;  // "<day> <id>" per line
    void load(const std::string& text);

private:
    std::deque<std::pair<uint32_t, std::string>> order_;
    std::unordered_set<std::string> ids_;
};

class Ingress {
public:
    void setLimits(uint32_t posts, uint32_t bytes) {
        posts_ = posts;
        bytes_ = bytes;
    }
    bool allow(const std::string& board, size_t bytes, uint32_t nowDay);
    bool warnOnce(const std::string& board, uint32_t nowDay);  // true the first time per board per day

private:
    struct Rec {
        uint32_t day = 0, posts = 0, bytes = 0;
        bool warned = false;
    };
    Rec* rec(const std::string& board, uint32_t nowDay);
    std::map<std::string, Rec> recs_;
    uint32_t posts_ = kDefaultPostsPerDay;
    uint32_t bytes_ = kDefaultBytesPerDay;
};

}  // namespace axis::net
