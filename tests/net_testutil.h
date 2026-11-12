// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Shared helpers for the Sysop Net tests: test keys for Rev, signed records, a whole net node in a
// temp folder (message store, outbound log, NetCore, clock), a store whose reads can be made to fail,
// sessions wired back to back, and a hand-driven peer that does the handshake itself.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/msgbase.h"
#include "net/areas.h"
#include "net/outlog.h"
#include "net/record.h"
#include "net/revkeys.h"
#include "net/session.h"
#include "net/sign.h"
#include "net/state.h"

namespace nettest {

using namespace axis::net;
namespace fs = std::filesystem;

constexpr uint32_t kNow = 1780000000;  // a set clock (2026-05-28)
constexpr uint32_t kDay = 86400;

inline KeyPair keyOf(uint8_t b) {
    uint8_t s[32];
    for (auto& x : s) x = b;
    return keyFromSeed(s);
}
inline std::string pubOf(const KeyPair& k) { return toHex(k.pub, 32); }
inline std::string boardOf(const KeyPair& k) { return boardId(pubOf(k)); }

// Rev's two keys in the tests (never the real ones).
inline const KeyPair& rev0() {
    static KeyPair k = keyOf(0xA0);
    return k;
}
inline const KeyPair& rev1() {
    static KeyPair k = keyOf(0xA1);
    return k;
}

// Puts the test keys in for one test and the real built-in keys back afterwards, so no other test
// sees them (the state writes the current keys globally on a rotate or a load).
struct RevKeys {
    RevKeys() { setRevKeysForTest(pubOf(rev0()), pubOf(rev1())); }
    ~RevKeys() { resetRevKeysForTest(); }
};

inline NetCard makeCard(const KeyPair& k, const std::string& name, uint32_t seq = 1) {
    NetCard c;
    c.pub = pubOf(k);
    c.board = boardId(c.pub);
    c.name = name;
    c.host = "10.0.0.1";
    c.port = 23;
    c.mac = "aa:bb:cc:dd:ee:ff";
    c.sysop = "Sysop";
    c.location = "Here";
    c.links = "internet";
    c.seq = seq;
    REQUIRE(signCard(c, k));
    return c;
}

inline NetPost makePost(const KeyPair& k, uint32_t seq, uint32_t date, const std::string& area = "sn_support",
                        const std::string& subject = "Hello", const std::string& body = "Body text") {
    NetPost p;
    p.board = boardOf(k);
    p.id = p.board + "-" + std::to_string(seq);
    p.area = area;
    p.from = "Sysop";
    p.to = "All";
    p.subject = subject;
    p.body = body;
    p.date = date;
    REQUIRE(signPost(p, k));
    return p;
}

inline NetNotice makeNotice(const KeyPair& k, int keyIdx, const std::string& kind, const std::string& target,
                            uint32_t seq, const std::string& value = "") {
    NetNotice n;
    n.kind = kind;
    n.target = target;
    n.value = value;
    n.seq = seq;
    n.key = keyIdx;
    REQUIRE(signNotice(n, k));
    return n;
}

// A temp folder of its own per object, removed afterwards.
struct TempDir {
    fs::path p;
    TempDir() {
        static std::atomic<unsigned> n{0};
        auto t = (unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count();
        p = fs::temp_directory_path() / ("axis_net_" + std::to_string(t) + "_" + std::to_string(n++));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// The real file store with a switch that makes every read fail (an SD hiccup).
class FlakyStore : public axis::bbs::MessageStore {
public:
    explicit FlakyStore(axis::bbs::MessageStore& inner) : inner_(inner) {}
    bool failReads = false;
    std::vector<axis::bbs::Message> headers(const std::string& a) override { return inner_.headers(a); }
    bool read(const std::string& a, int num, axis::bbs::Message& out) override {
        if (failReads) return false;
        return inner_.read(a, num, out);
    }
    int post(const std::string& a, const axis::bbs::Message& m) override { return inner_.post(a, m); }
    int drop(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        return inner_.drop(a, gone);
    }

private:
    axis::bbs::MessageStore& inner_;
};

// A message store in RAM with the file store's rules (numbers per area never reused, headers give
// at most the newest 200 without bodies and only the start of the net line), for tests that post
// hundreds of times.
class MemStore : public axis::bbs::MessageStore {
public:
    std::vector<axis::bbs::Message> headers(const std::string& a) override {
        std::vector<axis::bbs::Message> out;
        auto& v = areas_[a];
        size_t from = v.size() > kMaxHeaders ? v.size() - kMaxHeaders : 0;
        for (size_t i = from; i < v.size(); i++) {
            axis::bbs::Message m = v[i];
            m.body.clear();
            std::string id = netPostId(m.net);
            m.net = id.empty() ? std::string() : std::string("P\x1F") + id;
            out.push_back(m);
        }
        return out;
    }
    bool read(const std::string& a, int num, axis::bbs::Message& out) override {
        for (const auto& m : areas_[a])
            if (m.num == num) {
                out = m;
                return true;
            }
        return false;
    }
    int post(const std::string& a, const axis::bbs::Message& m) override {
        axis::bbs::Message c = m;
        c.num = ++next_[a];
        areas_[a].push_back(c);
        return c.num;
    }
    int drop(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        auto& v = areas_[a];
        size_t before = v.size();
        std::vector<axis::bbs::Message> keep;
        for (const auto& m : v)
            if (!gone(m)) keep.push_back(m);
        v.swap(keep);
        return (int)(before - v.size());
    }
    size_t count(const std::string& a) { return areas_[a].size(); }
    const std::vector<axis::bbs::Message>& all(const std::string& a) { return areas_[a]; }

private:
    std::map<std::string, std::vector<axis::bbs::Message>> areas_;
    std::map<std::string, int> next_;
};

// One net node: its own folder, message store, outbound log, key, clock, NetCore and signed card.
struct Node {
    TempDir dir;
    axis::bbs::FileMessageStore files;
    FlakyStore store;
    MemOutLog log;
    KeyPair key;
    uint32_t now;
    NetCore core;
    NetCard card;
    std::string board;

    explicit Node(uint8_t seedByte, uint32_t clock = kNow, const std::string& name = "")
        : files(dir.p.string()), store(files), key(keyOf(seedByte)), now(clock),
          core(store, key, [this] { return now; }, log) {
        card = makeCard(key, name.empty() ? "Board " + std::to_string(seedByte) : name);
        board = card.board;
        REQUIRE(core.setOwnCard(card));
    }
    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    void knows(const Node& o) { REQUIRE(core.takeCard(o.card, "")); }

    // Makes a post here and returns its encoded line ("" on failure).
    std::string post(const std::string& area = "sn_support", const std::string& subject = "Hi",
                     const std::string& body = "Body") {
        axis::bbs::Message m;
        m.from = "Sysop";
        m.to = "All";
        m.subject = subject;
        m.body = body;
        int num = 0;
        std::string id = core.originate(area, m, &num);
        if (id.empty()) return "";
        axis::bbs::Message back;
        if (!files.read(area, num, back)) return "";
        return back.net;
    }

    // The ids of the posts in an area, read one by one (headers show only the newest 200).
    std::vector<std::string> ids(const std::string& area = "sn_support", int upTo = 1000) {
        std::vector<std::string> out;
        for (int i = 1; i <= upTo; i++) {
            axis::bbs::Message m;
            if (files.read(area, i, m)) out.push_back(netPostId(m.net));
        }
        return out;
    }

    // Post lines (P records) among what pendingFor gives a peer now.
    std::vector<std::string> pendingPosts(const std::string& peer, size_t max = 64) {
        std::vector<std::string> out;
        for (auto& e : core.pendingFor(peer, max))
            if (!e.second.empty() && e.second[0] == 'P') out.push_back(e.second);
        return out;
    }
};

inline NetPost decodePost(const std::string& line) {
    NetPost p;
    REQUIRE(decode(line, p));
    return p;
}

inline std::string nonce(uint8_t b) {
    uint8_t n[8];
    for (auto& x : n) x = b;
    return toHex(n, 8);
}

inline bool settled(const NetSession& s) { return s.done() || s.failed(); }

// Two sessions back to back in memory: each one's output is the other's input. Runs until both are
// done or failed (plus one more round so the last answers land), or rounds run out.
inline void run(NetSession& a, NetSession& b, uint32_t& ms, int rounds = 5000, bool heapOk = true) {
    for (int i = 0; i < rounds; i++) {
        std::string x = a.takeOut();
        if (!x.empty()) b.feed(x.data(), x.size());
        std::string y = b.takeOut();
        if (!y.empty()) a.feed(y.data(), y.size());
        a.tick(ms, heapOk);
        b.tick(ms, heapOk);
        ms += 10;
        if (settled(a) && settled(b)) {
            x = a.takeOut();
            if (!x.empty()) b.feed(x.data(), x.size());
            y = b.takeOut();
            if (!y.empty()) a.feed(y.data(), y.size());
            return;
        }
    }
}

inline std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('\n', pos);
        if (e == std::string::npos) e = s.size();
        out.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    return out;
}

inline size_t countPrefix(const std::string& out, const std::string& prefix) {
    size_t n = 0;
    for (const auto& l : lines(out))
        if (l.compare(0, prefix.size(), prefix) == 0) n++;
    return n;
}

inline std::string transcriptOf(char role, const std::string& dNonce, const std::string& aNonce,
                                const std::string& dBoard, const std::string& aBoard) {
    return std::string("AXISNET2|") + role + "|" + dNonce + "|" + aNonce + "|" + dBoard + "|" + aBoard;
}

// A hand-driven dialer against a NetSession answerer: it does the hello and the PROOF itself and
// then writes whatever lines a test wants.
struct RawDialer {
    KeyPair key;
    NetCard card;
    std::string myNonce, theirNonce, theirBoard;
    explicit RawDialer(uint8_t seedByte, const std::string& n = "") : key(keyOf(seedByte)) {
        card = makeCard(key, "Raw " + std::to_string(seedByte));
        myNonce = n.empty() ? nonce(seedByte) : n;
    }
    std::string hello() const { return "AXISNET 2 " + encode(card) + " " + myNonce + "\n"; }
    // Feeds the hello, reads the answerer's hello, proves. Returns what the answerer said after the proof.
    std::string handshake(NetSession& s) {
        std::string h = hello();
        s.feed(h.data(), h.size());
        std::string reply = s.takeOut();
        auto ls = lines(reply);
        REQUIRE(!ls.empty());
        const std::string& l = ls[0];
        REQUIRE(l.compare(0, 10, "AXISNET 2 ") == 0);
        theirNonce = l.substr(l.size() - 16);
        NetCard c;
        REQUIRE(decode(l.substr(10, l.size() - 17 - 10), c));
        theirBoard = c.board;
        std::string p = "PROOF " + signHex(key, transcriptOf('D', myNonce, theirNonce, card.board, theirBoard)) + "\n";
        s.feed(p.data(), p.size());
        return s.takeOut();
    }
    static void say(NetSession& s, const std::string& line) {
        std::string l = line + "\n";
        s.feed(l.data(), l.size());
    }
};

}  // namespace nettest
