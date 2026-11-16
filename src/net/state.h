// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net state: board cards, Rev's signed notices (ban, hold, kill, lift, limits, rotate, and for User
// Net admit, deny, veto and votes) applied by sequence number, the seen-post dedupe set, and per-board daily ingress limits.
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
constexpr size_t kNetMaxIngressBoards = 48;  // boards counted per day; past it the least recently used goes
constexpr uint32_t kSeenDays = 120;
constexpr uint32_t kPostNoticeDays = 150;
constexpr size_t kMaxRotates = 16;
constexpr size_t kMaxPostNotices = 64;   // hold/kill/lift of posts: the oldest applied goes first
constexpr size_t kMaxBoardNotices = 64;  // ban/lift of boards: the oldest lift goes first, then the oldest ban
// admit/deny/veto of User Net boards: the hard User Net size. A new target past it is refused, never
// evicted, since the tally depends on every seal.
constexpr size_t kMaxMemberNotices = 64;
constexpr uint32_t kDefaultVotesYes = 3;  // yes votes that admit an applicant, until a votes notice
constexpr uint32_t kDefaultVotesNo = 3;   // no votes that deny one
constexpr uint32_t kMaxVotesNeeded = 20;  // a votes notice asks for 1..20 of each
constexpr uint32_t kDefaultPostsPerDay = 50;
constexpr uint32_t kDefaultBytesPerDay = 64 * 1024;
// Below this day number the board's clock was not set (1700000000 / 86400, as kNetClockSet in areas.h).
// A day stamp below it is re-stamped to the current day once the clock is set, so nothing stamped
// before NTP expires the moment the clock arrives.
constexpr uint32_t kNetClockDay = 1700000000u / 86400u;

class NetState {
public:
    bool addCard(const NetCard& c);
    // Verified against the keys in force at the notice's seq. nowDay is the local day it is applied.
    bool applyNotice(const NetNotice& n, uint32_t nowDay);
    void prune(uint32_t nowDay);  // expires post-group notices 150 days after they were applied (re-stamps first)
    // With a set clock (nowDay at or past kNetClockDay): notices applied before it was set get nowDay.
    void restamp(uint32_t nowDay);

    bool banned(const std::string& board) const;
    bool held(const std::string& postId) const;
    bool killed(const std::string& postId) const;
    uint32_t highSeq() const { return highSeq_; }
    // The highest notice seq ever applied here, kept across pruning and restarts (save writes it as "H <seq>");
    // a new notice from Rev's board takes the one after max(this, highSeq), so no seq is ever used twice.
    uint32_t usedSeq() const { return usedSeq_ > highSeq_ ? usedSeq_ : highSeq_; }
    uint32_t postsPerDay() const { return posts_; }
    uint32_t bytesPerDay() const { return bytes_; }
    uint32_t votesYes() const { return votesYes_; }
    uint32_t votesNo() const { return votesNo_; }
    // Rev's current admit, deny or veto for this board, or nullptr if none.
    const NetNotice* memberNotice(const std::string& board) const;
    // Visits every current admit, deny and veto (no copies), in board id order.
    void forEachMemberNotice(const std::function<void(const std::string& board, const NetNotice&)>& f) const;
    size_t cardCount() const { return cards_.size(); }
    const NetCard* card(const std::string& board) const;
    // Visits every current card (no copies), for callers that need the list of known boards.
    void forEachCard(const std::function<void(const NetCard&)>& f) const;
    // Boards whose cards are never evicted to make room (besides cards carrying a current Rev key).
    void setPinned(std::function<bool(const std::string& board)> pinned) { pinned_ = std::move(pinned); }
    // The encoded line of the notice stored now for (group, target), or "" if none. Groups are
    // "post", "board", "member", "limits" and "votes" (both target "net"); see noticeGroup.
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
    std::function<bool(const std::string&)> pinned_;
    uint64_t counter_ = 0;
    uint32_t highSeq_ = 0;
    uint32_t usedSeq_ = 0;  // never lowered
    uint32_t posts_ = kDefaultPostsPerDay;
    uint32_t bytes_ = kDefaultBytesPerDay;
    uint32_t votesYes_ = kDefaultVotesYes;
    uint32_t votesNo_ = kDefaultVotesNo;
};

// The group a notice is stored under: "post", "board", "member", "limits", "votes" or "rotate"; "" if
// it is invalid.
std::string noticeGroup(const NetNotice& n);

// Seen posts, per origin board: post ids are "<board>-<seq>", and each board keeps its highest seq
// seen plus a 64 bit window of the 64 seqs below it (high - 1 down to high - 64). A seq below
// high - 64 counts as seen. At most kNetMaxSeenBoards boards; the least recently used one goes first.
class NetSeen {
public:
    bool has(const std::string& id) const;
    // The id is below its board's window (more than 64 under the highest), so has() says seen
    // without knowing.
    bool belowWindow(const std::string& id) const;
    bool add(const std::string& id, uint32_t nowDay);  // false if already seen or not a post id
    void prune(uint32_t nowDay);  // drops boards not touched for kSeenDays (re-stamps first)
    // With a set clock (nowDay at or past kNetClockDay): boards last touched before it was set get nowDay.
    void restamp(uint32_t nowDay);
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
        uint64_t used = 0;  // for least recently used eviction
    };
    Rec* rec(const std::string& board, uint32_t nowDay);
    std::map<std::string, Rec> recs_;
    uint64_t counter_ = 0;
    uint32_t posts_ = kDefaultPostsPerDay;
    uint32_t bytes_ = kDefaultBytesPerDay;
};

}  // namespace axis::net
