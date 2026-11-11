// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net state: board cards, Rev's signed notices (ban, hold, kill, lift, limits, rotate) applied
// by sequence number, the seen-post dedupe set, and per-board daily ingress limits.
// Days are day numbers (unix time / 86400) supplied by the caller; nothing here reads a clock.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
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
constexpr uint32_t kPostNoticeDays = 150;
constexpr size_t kMaxRotates = 16;
constexpr size_t kMaxPostNotices = 4096;
constexpr size_t kMaxBoardNotices = 1024;
constexpr size_t kMaxSeenIdLen = 40;
constexpr uint32_t kDefaultPostsPerDay = 50;
constexpr uint32_t kDefaultBytesPerDay = 64 * 1024;

class NetState {
public:
    bool addCard(const NetCard& c);
    // Verified against the keys in force at the notice's seq. nowDay is the local day it is applied.
    bool applyNotice(const NetNotice& n, uint32_t nowDay);
    void prune(uint32_t nowDay);  // expires post-group notices 150 days after they were applied

    bool banned(const std::string& board) const;
    bool held(const std::string& postId) const;
    bool killed(const std::string& postId) const;
    uint32_t highSeq() const { return highSeq_; }
    uint32_t postsPerDay() const { return posts_; }
    uint32_t bytesPerDay() const { return bytes_; }
    size_t cardCount() const { return cards_.size(); }
    const NetCard* card(const std::string& board) const;
    // Visits every current card (no copies), for callers that need the list of known boards.
    void forEachCard(const std::function<void(const NetCard&)>& f) const;
    // The encoded line of the notice stored now for (group, target), or "" if none. Groups are
    // "post", "board" and "limits" (target "net"); see noticeGroup.
    std::string noticeLine(const std::string& group, const std::string& target) const;
    // The encoded rotate notice with this seq from the rotate chain, or "" if none.
    std::string rotateLine(uint32_t seq) const;

    // The rotate chain in seq order, then every current card, then the other latest notices.
    std::vector<std::string> snapshot() const;
    // Text for the SD card: the same lines as snapshot, notices prefixed with "<day> ".
    std::string save() const;
    // Replaces the state. Junk lines are skipped and every record is verified again.
    void load(const std::string& text, uint32_t nowDay);

private:
    struct Entry {
        NetNotice n;
        uint32_t day = 0;
        uint64_t order = 0;
    };
    struct CardRec {
        NetCard c;
        uint64_t touched = 0;
    };
    bool keysAt(uint32_t seq, std::string out[2]) const;
    bool verifyAt(const NetNotice& n) const;
    void reverify();
    void capGroup(const std::string& group, size_t cap);
    std::map<std::string, CardRec> cards_;
    std::map<std::pair<std::string, std::string>, Entry> notices_;  // (group, target), not rotates
    std::vector<Entry> rotates_;                                     // seq order, at most kMaxRotates
    uint64_t counter_ = 0;
    uint32_t highSeq_ = 0;
    uint32_t posts_ = kDefaultPostsPerDay;
    uint32_t bytes_ = kDefaultBytesPerDay;
};

// The group a notice is stored under: "post", "board", "limits" or "rotate"; "" if it is invalid.
std::string noticeGroup(const NetNotice& n);

class NetSeen {
public:
    bool has(const std::string& id) const { return ids_.count(id) > 0; }
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
