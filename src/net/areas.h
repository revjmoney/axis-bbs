// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Sysop Net message areas and NetCore, each board's share of the net: it takes posts, cards
// and Rev's notices from peers, makes this board's own posts, stores them in the message base and
// logs what to pass on for its link peers. NetPostingStore wraps the board's message store so
// posting into a Sysop Net area goes out on the net and held or killed posts stay hidden.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "bbs/msgbase.h"
#include "net/record.h"
#include "net/outlog.h"
#include "net/sign.h"
#include "net/state.h"

namespace axis::net {

struct NetArea {
    std::string id, name;
    bool revOnly;
};

// The four Sysop Net areas, in menu order.
const std::vector<NetArea>& sysopNetAreas();
const NetArea* sysopNetArea(const std::string& id);  // nullptr when id is not one of them
bool isNetAreaId(const std::string& area);           // starts with "sn_"

// The post id held in a Message's net field (the whole line or the headers-only start). "" if none.
std::string netPostId(const std::string& net);
// The board's usual date text ("YYYY-MM-DD HH:MM", local time) for a unix time, and back (0 if not
// parsable).
std::string netDateText(uint32_t unixTime);
uint32_t netDateUnix(const std::string& text);

constexpr size_t kMaxLinkPeers = 8;        // boards we sync with; the longest unsynced goes first
constexpr uint32_t kOutLogKeep = 5000;     // outbound log entries kept
constexpr uint32_t kOutLogSlack = 500;     // the log is trimmed back to kOutLogKeep past this many more
constexpr uint32_t kNetKeepPosts = 500;
constexpr uint32_t kNetKeepDays = 90;

// `store` must be the board's real message store, never the NetPostingStore wrapping it (that
// would loop through originate and hide held posts from trim and sending).
//
// Everything to pass on goes into the outbound log as one entry, "<kind> <key> <exclude>":
//   P <area> <msgnum> <postId> <exclude>   a post, read back from the message base
//   C <board> <exclude>                    the board's current card
//   N <group> <target> <exclude>           the notice stored now for that target
//   N R <seq> <exclude>                    a rotate notice from the chain
// exclude is a comma list of boards not to send it to, or "-". Each link peer reads the log from
// its own cursor; RAM stays small whatever the number of posts or boards.
class NetCore {
public:
    enum class Took { Stored, Duplicate, Rejected, Banned, Limited };

    NetCore(bbs::MessageStore& store, KeyPair me, std::function<uint32_t()> nowUnix, OutLog& log);

    NetState& state() { return state_; }
    const NetState& state() const { return state_; }
    NetSeen& seen() { return seen_; }
    const NetSeen& seen() const { return seen_; }
    const std::string& board() const { return myBoard_; }
    bool isRevBoard(const std::string& board) const;  // its card's pub is one of Rev's keys

    // A post from a peer (fromPeer may be empty). Stores it and logs it to pass on.
    Took take(const NetPost& p, const std::string& fromPeer);
    // A card or notice from a peer. True if it changed the state (then it is logged to pass on).
    bool takeCard(const NetCard& c, const std::string& fromPeer);
    bool takeNotice(const NetNotice& n, const std::string& fromPeer);
    // This board's own card and Rev's notices made here; both already signed by the caller.
    bool setOwnCard(const NetCard& c);
    bool originateNotice(const NetNotice& n);
    // Builds, signs, stores and logs a post from this board. Returns its id, or "" on failure.
    // num, if given, gets the stored message number.
    std::string originate(const std::string& area, const bbs::Message& m, int* num = nullptr);

    // A board we sync with (the first peer, then each board that completes a session with us).
    // A new one starts at the log's end; it gets the current state from the snapshot exchange.
    // Calling it again marks a sync now.
    void addLinkPeer(const std::string& board);
    bool isLinkPeer(const std::string& board) const { return links_.count(board) > 0; }
    // Up to max (log index, encoded line) pairs for a link peer, oldest first. Entries excluded for
    // it or no longer resolvable (a killed post, a gone card) are skipped.
    std::vector<std::pair<uint32_t, std::string>> pendingFor(const std::string& peer, size_t max);
    // The peer has the entry at index: its cursor moves past it (acks come in order).
    void acked(const std::string& peer, uint32_t index);
    std::string saveCursors() const;  // "<peer> <index> <last sync unix>" per line
    void loadCursors(const std::string& text);

    uint32_t postSeq() const { return seq_; }
    void setPostSeq(uint32_t s) { seq_ = s ? s : 1; }

    void setRetention(uint32_t keepPosts, uint32_t keepDays) {
        keepPosts_ = keepPosts;
        keepDays_ = keepDays;
    }
    // Once a day at most: retention for every Sysop Net area, pruning of the state and the seen
    // set, and trimming the outbound log.
    void trim(uint32_t nowUnix);

private:
    struct Link {
        uint32_t cursor = 0;
        uint32_t lastSync = 0;
    };

    uint32_t day() const { return now_() / 86400; }
    void syncLimits() { ingress_.setLimits(state_.postsPerDay(), state_.bytesPerDay()); }
    void log(const std::string& entry);
    int storePost(const NetPost& p, const std::string& line);
    std::string resolve(const std::string& entry, const std::string& peer);
    void dropPost(const std::string& id);
    void trimArea(const std::string& area, uint32_t nowDay);

    bbs::MessageStore& store_;
    KeyPair me_;
    std::function<uint32_t()> now_;
    OutLog& log_;
    std::string myBoard_;
    NetState state_;
    NetSeen seen_;
    Ingress ingress_;
    uint32_t seq_ = 1;
    uint32_t keepPosts_ = kNetKeepPosts, keepDays_ = kNetKeepDays;
    uint32_t lastTrimDay_ = 0xFFFFFFFFu;
    std::map<std::string, Link> links_;
};

// The board's message store with the net on top: a post into an sn_ area goes through
// NetCore::originate, and held or killed posts in sn_ areas are hidden. Everything else passes through.
class NetPostingStore : public bbs::MessageStore {
public:
    NetPostingStore(bbs::MessageStore& inner, NetCore& core) : inner_(inner), core_(core) {}
    std::vector<bbs::Message> headers(const std::string& area) override;
    bool read(const std::string& area, int num, bbs::Message& out) override;
    int post(const std::string& area, const bbs::Message& m) override;
    int drop(const std::string& area, const std::function<bool(const bbs::Message&)>& gone) override;

private:
    bool hidden(const bbs::Message& m) const;
    bbs::MessageStore& inner_;
    NetCore& core_;
};

}  // namespace axis::net
