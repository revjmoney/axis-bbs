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

// Caps sized for the ESP32 (about 90 KB free heap). Cards are what an outsider can inflate, so their
// cap is the tight one (about 22 KB at worst). Notices are made only with Rev's keys, so their
// count is under Rev's control, not an attacker's.
constexpr size_t kNetMaxCards = 32;          // the card touched longest ago makes room
constexpr size_t kNetMaxSeenBoards = 48;     // origin boards in the seen set, least recently used goes
constexpr uint32_t kSeenWindow = 64;         // seqs tracked below each board's highest
constexpr size_t kNetMaxIngressBoards = 48;  // boards counted per day
constexpr uint32_t kSeenDays = 120;
constexpr uint32_t kPostNoticeDays = 150;
constexpr size_t kMaxRotates = 16;
constexpr size_t kMaxPostNotices = 64;   // hold/kill/lift of posts: the oldest applied goes first
constexpr size_t kMaxBoardNotices = 64;  // ban/lift of boards: the oldest lift goes first, then the oldest ban
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

// Seen posts, per origin board: post ids are "<board>-<seq>", and each board keeps its highest seq
// seen plus a 64 bit window of the 64 seqs below it. A seq at or below high - 64 counts as seen.
// At most kNetMaxSeenBoards boards; the least recently used one goes first.
class NetSeen {
public:
    bool has(const std::string& id) const;
    bool add(const std::string& id, uint32_t nowDay);  // false if already seen or not a post id
    void prune(uint32_t nowDay);  // drops boards not touched for kSeenDays
    size_t size() const { return boards_.size(); }
    std::string save() const;  // "<board> <high> <window as 16 hex> <last day>" per line
    void load(const std::string& text);

private:
    struct Rec {
        uint32_t high = 0;
        uint64_t window = 0;  // bit i: seq high - 1 - i seen
        uint32_t day = 0;     // last touched
        uint64_t used = 0;    // for least recently used eviction
    };
    void put(uint64_t board, const Rec& r);
    std::map<uint64_t, Rec> boards_;
    uint64_t counter_ = 0;
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
