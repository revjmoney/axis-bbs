// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// AXIS boards on one LAN acting as one BBS (spec 5.11). The first board up is main, node 1; a board
// that finds a main joins it as node 2, 3 ... (asking for its old number back). The main sends a
// heartbeat every second with every node's status, so who's online is the same on every board.
// Chat and sysop pages go through the main to every board. If the heartbeat stops for 5 s, the
// lowest-numbered surviving node becomes main with a higher epoch and the rest join it; a main that
// hears a higher epoch (or an equal one from a lower node) steps down. Portable: the board supplies
// the network calls.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "cluster/protocol.h"

namespace axis::cluster {

class Net {
public:
    virtual void     broadcast(const std::string& datagram) = 0;  // UDP to every board on the LAN
    virtual bool     recvDatagram(std::string& datagram, uint32_t& fromIp) = 0;
    virtual int      connect(uint32_t ip) = 0;                    // a link to the main, or -1
    virtual int      accept() = 0;                                // main: a new link, or -1
    virtual bool     send(int link, const std::string& bytes) = 0;
    virtual int      recv(int link, std::string& out) = 0;        // bytes appended; -1 = closed
    virtual void     close(int link) = 0;
    virtual uint32_t myIp() = 0;
    virtual ~Net() = default;
};

enum class Role { Off, Discovering, Joining, Main, Follower };

struct NodeInfo {
    int         number = 0;
    std::string id, handle, activity;  // id: the board's MAC; handle "" = waiting for a caller
    uint32_t    ip = 0;
};

// A follower holding an SD card, as the main knows it (the pooled file library adds its files).
struct CardBoard {
    std::string id;  // the board's id
    uint32_t    ip = 0;
    uint64_t    freeBytes = 0;
    bool        alive = false;  // false: its link went; it stays listed so @CARDS can say so
};

struct ChatLine {
    int         node = 0;
    std::string handle, text;
    bool        page = false;  // a page for the sysop
    int         kind = 0, to = 0;  // node to node (bbs/node.h kNodeNote ...): the kind, and the node it is for (0 = all)
};

class Cluster {
public:
    Cluster(Net& net, std::string boardId) : net_(net), id_(std::move(boardId)) {}
    void start(uint32_t nowMs, int previousNumber = 0);
    void tick(uint32_t nowMs);

    Role                         role() const { return role_; }
    int                          nodeNumber() const { return number_; }  // this board's first node
    const std::vector<int>&      numbers() const { return numbers_; }   // one per session (setSlots)
    uint32_t                     epoch() const { return epoch_; }
    uint32_t                     mainIp() const { return mainIp_; }
    const std::vector<NodeInfo>& nodes() const { return nodes_; }

    // The shared cluster key and a source of random numbers (the board's hardware RNG). Without
    // both, the board runs alone: it never answers or joins another board (set them before start()).
    void setKey(const std::string& key) { key_ = key; }
    void setRandom(uint32_t (*rng)()) { rng_ = rng; }
    bool alone() const { return alone_; }
    // A new key (@CLUSTERKEY): drop every link and find the others again, at the top of the next
    // tick, never in the middle of one (a command can run inside tick, from a follower's request).
    void restartSoon(const std::string& key) {
        newKey_ = key;
        keyPending_ = true;
        restart_ = true;
        restartNumber_ = number_;
    }

    void setSlots(int sessions) { slots_ = sessions < 1 ? 1 : sessions > 4 ? 4 : sessions; }  // callers this board takes at once
    void setStatus(const std::string& handle, const std::string& activity) { setStatus(0, handle, activity); }
    void setStatus(int slot, const std::string& handle, const std::string& activity);
    void setHasFiles(bool sd) { hasFiles_ = sd; }  // this board holds the file areas (an SD card)
    // this board's card's free space; a follower tells the main when it changes
    void     setFreeBytes(uint64_t bytes);
    uint64_t freeBytes() const { return freeBytes_; }
    // Main: every follower that has told it about an SD card, in id order.
    std::vector<CardBoard> cardBoards() const;
    void chat(const std::string& handle, const std::string& text, int slot = 0) { say(handle, text, false, slot); }
    void page(const std::string& handle, const std::string& text, int slot = 0) { say(handle, text, true, slot); }
    // a node to node line (a note, a broadcast, a split chat keystroke ...): every board gets it, each
    // hands it to the session with node number "to" (or to all for 0)
    void send(const std::string& handle, int kind, int to, const std::string& text, int slot = 0) { say(handle, text, kind == 1, slot, kind, to); }

    // Follower: one request to the main (remotefiles.h) and its reply, waiting up to timeoutMs while
    // the link keeps being served; false with no main or no answer.
    bool request(const std::string& req, std::string& reply, uint32_t timeoutMs = 4000);
    // Main: one request to the follower board with this id and its reply (onMainRequest answers it
    // there), the same way: the other links keep being read but their frames wait for the next
    // tick, and heartbeats go on. False at once for a board with no link, or after timeoutMs.
    bool requestTo(const std::string& boardId, const std::string& req, std::string& reply, uint32_t timeoutMs = 2500);
    // One user record (users.h formatUser): a follower sends it to the main, the main to every follower.
    void sendUser(const std::string& record);
    // Main: the BBS settings changed (settings()), send them to every board now, not just at join.
    void shareSettings();

    std::function<void(const ChatLine&)>    onChat;
    std::function<void(int)>                onNumber;  // a number to remember for next boot
    std::function<void(const std::string&)> onEvent;   // "node 2 joined", "MAIN NODE LOST ..."
    std::function<std::string(int node, const std::string& req)> onRequest;  // main: answer a follower
    std::function<void(int node)>                 onNodeGone;  // main: a follower's link closed
    std::function<std::string(const std::string& req)> onMainRequest;  // follower: answer the main (requestTo)
    std::function<void()>                         onMainGone;  // follower: the link to the main closed
    std::function<void(const std::string& record)> onUser;     // a user record came over the link
    std::function<std::vector<std::string>()>     allUsers;    // main: every user, for a board that joins
    std::function<std::string()>                  settings;    // main: the BBS settings, for a board that joins
    std::function<void(const std::string&)>       onSettings;  // follower: the main's settings arrived
    std::function<uint32_t()>                     clockMs;     // for request(): the time
    std::function<void(uint32_t)>                 sleepMs;     // and a short wait
    std::function<bool()>                         busyHere;    // main: someone on this board the status doesn't show (the local console)

private:
    struct Link {
        int         id;
        FrameReader in;
        int         node = 0;
        std::string board;  // the board's id (its numbers are every node with this id)
        // the key handshake: 0 waiting for the other side's proof, 1 (follower) sent ours, 2 trusted
        int         auth = 0;
        bool        dead = false;  // failed the handshake or a frame's tag: tick() drops it
        std::string mainNonce, followerNonce, linkKey;
        uint32_t    sent = 0, got = 0;  // frames sealed / opened on this link
        uint32_t    since = 0;          // when it opened: a link that hasn't proved the key in 5 s goes
        bool        probe = false;      // a main checking another main holds the key (it speaks as a follower)
    };
    bool asMain(const Link& l) const { return mainSide() && !l.probe; }  // which side of the handshake this link is
    void probeMain(uint32_t ip, uint32_t epoch, uint32_t now);  // main: does that other main really hold the key?
    void closeProbe(uint32_t now);
    void serveProbe(uint32_t now);
    void turnedAway(uint32_t now);  // the "turned away" event, at most once a minute
    // one frame on a link, sealed with its tag once the link is trusted
    bool put(Link& l, Frame t, const std::vector<std::string>& f);
    // the next frame from a link that is for the BBS; the handshake is handled in here
    bool take(Link& l, Frame& t, std::string& body);
    void sendHello(Link& l);
    std::string sign(const std::string& datagram);
    bool        unsign(std::string& datagram);
    void        shout(const std::string& datagram) { net_.broadcast(sign(datagram)); }
    std::string nonce();
    bool        mainSide() const { return role_ == Role::Main; }
    void becomeMain(uint32_t now, uint32_t epoch, int number);
    void discover(uint32_t now);
    void say(const std::string& handle, const std::string& text, bool page, int slot, int kind = 0, int to = 0);
    std::vector<int> allocate(const std::vector<int>& want);  // main: free numbers, the wanted ones first
    void deliver(const ChatLine& line);  // main: to this board and every follower
    void handleDatagram(const std::string& d, uint32_t from, uint32_t now);
    void handleFrame(Link& l, Frame t, const std::string& body, uint32_t now);
    void heartbeat();
    void announce();
    void tidyNumbers(uint32_t now);  // main: once every node has sat idle a while, it is 1, 2 ... and the others follow
    void event(const std::string& e) {
        if (onEvent) onEvent(e);
    }
    NodeInfo* node(int number);

    Net&                  net_;
    std::string           id_;
    int                   slots_ = 1;
    std::vector<int>      numbers_;                                   // this board's nodes
    std::vector<std::pair<std::string, std::string>> status_{{"", ""}};  // per session: handle, activity
    Role                  role_ = Role::Off;
    int                   number_ = 0, wantNumber_ = 0, lastMain_ = 0;
    uint32_t              epoch_ = 0, mainIp_ = 0, since_ = 0, lastBeat_ = 0, lastAnnounce_ = 0, minEpoch_ = 0, idleSince_ = 0;
    uint32_t              listenMs_ = 3000;  // how long discovery waits for a main before becoming one
    std::string           key_;
    uint32_t              (*rng_)() = nullptr;
    bool                  alone_ = false;  // no key: no cluster
    bool                  hasFiles_ = false, yield_ = false;  // yield_: the main handed over, act at the end of tick()
    uint32_t              nextReq_ = 1;
    uint32_t              wantReq_ = 0;      // request() is waiting for this reply
    std::string*          replyTo_ = nullptr;
    bool                  replied_ = false;
    bool                  inRequest_ = false;  // request() is waiting: other frames are held, never handled inside it
    struct Held {
        int         link;
        Frame       type;
        std::string body;
    };
    std::vector<Held>     held_;  // what arrived meanwhile, for the next tick (by link id)
    uint64_t              freeBytes_ = 0;
    uint32_t              lastCard_ = 0;  // follower: the last CardInfo sent
    uint32_t              lastYield_ = 0;  // main without the card: the last time it asked the card's board to take main
    static constexpr uint32_t kYieldAgainMs = 30000;
    struct Card {
        uint32_t ip = 0;
        uint64_t freeBytes = 0;
        bool     alive = false;
    };
    std::map<std::string, Card> cards_;  // main: the followers with SD cards, by board id
    std::vector<NodeInfo> nodes_;
    std::vector<Link>     links_;  // main: one per follower; follower: one, to the main
    // a main that hears a winning announcement checks first: it dials that main as a follower would and
    // only steps down once that main proves the key (a replayed announcement alone changes nothing)
    Link                  probe_{-1, FrameReader()};
    bool                  probing_ = false, stepDown_ = false;
    uint32_t              probeIp_ = 0, probeEpoch_ = 0, probeEnd_ = 0, probeFailAt_ = 0;
    bool                  probeTried_ = false, probedThisTick_ = false, probeFailOnce_ = false;
    bool                  joinedThisTick_ = false, joinFailed_ = false;  // a Discovering board's join dials
    uint32_t              joinFailEnd_ = 0;  // probeEnd_: end of the last attempt, any address
    uint32_t              turnedAt_ = 0;  // the last "turned away" event
    bool                  turnedOnce_ = false;
    bool                  inTick_ = false, restart_ = false, keyPending_ = false;  // restart_: start() again at the top of the next tick
    int                   restartNumber_ = 0;
    std::string           newKey_;
};

}  // namespace axis::cluster
