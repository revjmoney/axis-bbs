// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The Sysop Net message areas and NetCore, each board's share of the net: it takes posts, cards
// and Rev's notices from peers, makes this board's own posts, stores them in the message base and
// keeps a send queue per peer. NetPostingStore wraps the board's message store so posting into a
// Sysop Net area goes out on the net and held or killed posts stay hidden.
#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "bbs/msgbase.h"
#include "net/record.h"
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

constexpr size_t kNetQueueCap = 2000;      // keys per peer; the oldest go first
constexpr size_t kNetNoticeLines = 2048;   // queued notice lines kept by seq
constexpr size_t kNetPostLocs = 2048;      // post id -> (area, number) cache for sending
constexpr size_t kNetExtraPeers = 64;      // peers named in calls that have no card
constexpr uint32_t kNetKeepPosts = 500;
constexpr uint32_t kNetKeepDays = 90;

// `store` must be the board's real message store, never the NetPostingStore wrapping it (that
// would loop through originate and hide held posts from trim and sending).
class NetCore {
public:
    enum class Took { Stored, Duplicate, Rejected, Banned, Limited };

    NetCore(bbs::MessageStore& store, KeyPair me, std::function<uint32_t()> nowUnix);

    NetState& state() { return state_; }
    const NetState& state() const { return state_; }
    NetSeen& seen() { return seen_; }
    const NetSeen& seen() const { return seen_; }
    const std::string& board() const { return myBoard_; }
    bool isRevBoard(const std::string& board) const;  // its card's pub is one of Rev's keys

    // A post from a peer (fromPeer may be empty). Stores it and queues it onward.
    Took take(const NetPost& p, const std::string& fromPeer);
    // A card or notice from a peer. True if it changed the state (then it is queued onward).
    bool takeCard(const NetCard& c, const std::string& fromPeer);
    bool takeNotice(const NetNotice& n, const std::string& fromPeer);
    // This board's own card and Rev's notices made here; both already signed by the caller.
    bool setOwnCard(const NetCard& c);
    bool originateNotice(const NetNotice& n);
    // Builds, signs, stores and queues a post from this board. Returns its id, or "" on failure.
    // num, if given, gets the stored message number.
    std::string originate(const std::string& area, const bbs::Message& m, int* num = nullptr);

    // Encoded lines waiting for a peer, oldest first. Keys that no longer resolve are dropped.
    std::vector<std::string> pendingFor(const std::string& peer, size_t max);
    // keyOrId is a queue key ("P:<id>", "C:<board>", "N:<seq>") or a bare post id.
    void acked(const std::string& peer, const std::string& keyOrId);
    std::string saveQueues() const;  // "<peer> <key>" per line
    void loadQueues(const std::string& text);

    uint32_t postSeq() const { return seq_; }
    void setPostSeq(uint32_t s) { seq_ = s ? s : 1; }

    void setRetention(uint32_t keepPosts, uint32_t keepDays) {
        keepPosts_ = keepPosts;
        keepDays_ = keepDays;
    }
    // Retention for every Sysop Net area, and pruning of the state and the seen set.
    void trim(uint32_t nowUnix);

private:
    struct QKey {  // a queue entry, kept small: kind, then seq and board as numbers
        char kind = 0;
        uint32_t seq = 0;
        uint64_t board = 0;
        bool operator==(const QKey& o) const { return kind == o.kind && seq == o.seq && board == o.board; }
    };
    struct Loc {
        uint8_t area = 0;
        int num = 0;
    };
    using PostKey = std::pair<uint64_t, uint32_t>;

    uint32_t day() const { return now_() / 86400; }
    void syncLimits() { ingress_.setLimits(state_.postsPerDay(), state_.bytesPerDay()); }
    void notePeer(const std::string& peer);
    void forEachPeer(const std::function<void(const std::string&)>& f) const;
    void push(const std::string& peer, const QKey& k, bool dedupe);
    void queuePost(const NetPost& p, const std::string& fromPeer);
    int storePost(const NetPost& p, const std::string& line);
    void rememberLoc(const std::string& id, uint8_t area, int num);
    void warmLocs();
    std::string resolve(const QKey& k, bool& warmed);
    void dropPost(const std::string& id);
    void trimArea(const std::string& area, uint32_t nowDay);
    static bool parseKey(const std::string& text, QKey& k);
    static std::string keyText(const QKey& k);

    bbs::MessageStore& store_;
    KeyPair me_;
    std::function<uint32_t()> now_;
    std::string myBoard_;
    NetState state_;
    NetSeen seen_;
    Ingress ingress_;
    uint32_t seq_ = 1;
    uint32_t keepPosts_ = kNetKeepPosts, keepDays_ = kNetKeepDays;
    std::map<std::string, std::deque<QKey>> queues_;
    std::map<uint32_t, std::string> noticeLines_;
    std::map<PostKey, Loc> locs_;
    std::deque<PostKey> locOrder_;
    std::set<std::string> extraPeers_;
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
