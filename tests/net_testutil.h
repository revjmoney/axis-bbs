// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Shared helpers for the Sysop Net tests: test keys for Rev, signed records, a whole net node in a
// temp folder (message store, outbound log, NetCore, clock), a store whose reads can be made to fail,
// sessions wired back to back, and a hand-driven peer that does the handshake itself.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <initializer_list>
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

// A signed User Net application from the board with key k.
inline NetApp makeApp(const KeyPair& k, uint32_t seq = 1, uint32_t date = kNow, const std::string& blurb = "A board for testing",
                      const std::string& mac = "02:00:00:00:00:01") {
    NetApp a;
    a.pub = pubOf(k);
    a.board = boardId(a.pub);
    a.name = "Test Board";
    a.sysop = "Tester";
    a.location = "Nowhere";
    a.mac = mac;
    a.blurb = blurb;
    a.seq = seq;
    a.date = date;
    REQUIRE(signApp(a, k));
    return a;
}

// A signed vote from the board with key k on the application app-a<appSeq>.
inline NetVote makeVote(const KeyPair& k, const std::string& app, uint32_t appSeq, uint32_t seq, bool yes,
                        uint32_t date = kNow, const std::string& comment = "") {
    NetVote v;
    v.pub = pubOf(k);
    v.voter = boardId(v.pub);
    v.app = app;
    v.appSeq = appSeq;
    v.seq = seq;
    v.date = date;
    v.yes = yes;
    v.comment = comment;
    REQUIRE(signVote(v, k));
    return v;
}

// An apps file (/net/apps.txt) in RAM, wired into a NetCore with setAppsFile.
struct AppsFile {
    std::string text;
    void wire(NetCore& core) {
        core.setAppsFile(
            [this](const std::string& line, uint32_t& off) {
                off = (uint32_t)text.size();
                text += line;
                text += '\n';
                return true;
            },
            [this](const NetCore::AppsLineFn& each) {
                for (const auto& l : splitText())
                    if (!each(l)) return;
            },
            [this](uint32_t off, std::string& line) {
                if (off >= text.size()) return false;
                size_t e = text.find('\n', off);
                line = text.substr(off, e == std::string::npos ? std::string::npos : e - off);
                return true;
            });
    }
    std::vector<std::string> splitText() const {
        std::vector<std::string> out;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t e = text.find('\n', pos);
            if (e == std::string::npos) e = text.size();
            if (e > pos) out.push_back(text.substr(pos, e - pos));
            pos = e + 1;
        }
        return out;
    }
};

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
    void scanHeaders(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& each) override {
        inner_.scanHeaders(a, each);
    }
    bool read(const std::string& a, int num, axis::bbs::Message& out) override {
        if (failReads) return false;
        return inner_.read(a, num, out);
    }
    int post(const std::string& a, const axis::bbs::Message& m) override { return inner_.post(a, m); }
    int drop(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        return inner_.drop(a, gone);
    }
    void scanHeadersFrom(const std::string& a, uint32_t& pos, const std::function<bool(const axis::bbs::Message&)>& each) override {
        inner_.scanHeadersFrom(a, pos, each);
    }
    bool readAt(const std::string& a, uint32_t at, int num, axis::bbs::Message& out) override {
        if (failReads) return false;
        return inner_.readAt(a, at, num, out);
    }

private:
    axis::bbs::MessageStore& inner_;
};

// A store for checking the net never builds a header list for its own walks: headers() counts each call
// (a test then checks the count stays 0); scanHeaders, read, post and drop pass through.
class NoHeadersStore : public axis::bbs::MessageStore {
public:
    explicit NoHeadersStore(axis::bbs::MessageStore& inner) : inner_(inner) {}
    int headerCalls = 0;
    std::vector<axis::bbs::Message> headers(const std::string& a) override {
        headerCalls++;
        return inner_.headers(a);
    }
    void scanHeaders(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& each) override {
        inner_.scanHeaders(a, each);
    }
    bool read(const std::string& a, int num, axis::bbs::Message& out) override { return inner_.read(a, num, out); }
    int post(const std::string& a, const axis::bbs::Message& m) override { return inner_.post(a, m); }
    int drop(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        return inner_.drop(a, gone);
    }
    void scanHeadersFrom(const std::string& a, uint32_t& pos, const std::function<bool(const axis::bbs::Message&)>& each) override {
        inner_.scanHeadersFrom(a, pos, each);
    }
    bool readAt(const std::string& a, uint32_t at, int num, axis::bbs::Message& out) override {
        return inner_.readAt(a, at, num, out);
    }

private:
    axis::bbs::MessageStore& inner_;
};

// A store that counts what is asked of one area (Net Files' nf_list by default) and passes everything through: whole
// scans, scans from an offset, reads by number and reads at an offset.
class CountingStore : public axis::bbs::MessageStore {
public:
    explicit CountingStore(axis::bbs::MessageStore& inner, std::string area = kNetFilesArea)
        : inner_(inner), area_(std::move(area)) {}
    int headerCalls = 0, scans = 0, scansFrom = 0, reads = 0, readsAt = 0;
    std::vector<axis::bbs::Message> headers(const std::string& a) override {
        headerCalls += a == area_;
        return inner_.headers(a);
    }
    void scanHeaders(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& each) override {
        scans += a == area_;
        inner_.scanHeaders(a, each);
    }
    void scanHeadersFrom(const std::string& a, uint32_t& pos, const std::function<bool(const axis::bbs::Message&)>& each) override {
        scansFrom += a == area_;
        inner_.scanHeadersFrom(a, pos, each);
    }
    bool read(const std::string& a, int num, axis::bbs::Message& out) override {
        reads += a == area_;
        return inner_.read(a, num, out);
    }
    bool readAt(const std::string& a, uint32_t at, int num, axis::bbs::Message& out) override {
        readsAt += a == area_;
        return inner_.readAt(a, at, num, out);
    }
    int post(const std::string& a, const axis::bbs::Message& m) override { return inner_.post(a, m); }
    int drop(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& gone) override {
        return inner_.drop(a, gone);
    }
    void reset() { headerCalls = scans = scansFrom = reads = readsAt = 0; }

private:
    axis::bbs::MessageStore& inner_;
    std::string area_;
};

// A message store in RAM with the file store's rules (numbers per area never reused, headers give
// at most the newest 200 without bodies and only the start of the net line; a whole-area scan gives a
// Net Files listing line whole, as the file store does), for tests that post hundreds of times. A
// message's at is its place in the area, which readAt checks.
class MemStore : public axis::bbs::MessageStore {
public:
    // The net line as headers() give it: "<kind>" 0x1F "<first field>" (a post's id, a listing's home board).
    static std::string cut(const std::string& net) {
        if (net.size() < 2 || net[1] != '\x1F') return std::string();
        size_t e = net.find('\x1F', 2);
        return net.substr(0, e);
    }
    std::vector<axis::bbs::Message> headers(const std::string& a) override {
        std::vector<axis::bbs::Message> out;
        auto& v = areas_[a];
        size_t from = v.size() > kMaxHeaders ? v.size() - kMaxHeaders : 0;
        for (size_t i = from; i < v.size(); i++) {
            axis::bbs::Message m = v[i];
            m.body.clear();
            m.net = cut(m.net);
            m.at = (uint32_t)i;
            out.push_back(m);
        }
        return out;
    }
    // Like the file store: every message of the area, one at a time, in header form, a listing (L) line whole.
    void scanHeaders(const std::string& a, const std::function<bool(const axis::bbs::Message&)>& each) override {
        auto& v = areas_[a];
        for (size_t i = 0; i < v.size(); i++) {
            axis::bbs::Message m = v[i];
            m.body.clear();
            if (m.net.compare(0, 2, "L\x1F") != 0) m.net = cut(m.net);
            m.at = (uint32_t)i;
            if (!each(m)) return;
        }
    }
    bool readAt(const std::string& a, uint32_t at, int num, axis::bbs::Message& out) override {
        auto& v = areas_[a];
        if (at >= v.size() || v[at].num != num) return false;
        out = v[at];
        out.at = at;
        return true;
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

// The node says net2 on its card (a Phase 2 board: peers send it UWANT and User Net records).
inline void sayNet2(Node& n) {
    NetCard c = n.card;
    c.links = "internet,net2";
    c.seq = n.card.seq + 1;
    REQUIRE(signCard(c, n.key));
    REQUIRE(n.core.setOwnCard(c));
    n.card = c;
}

// A Phase 2 node with User Net on: net2 on its card, an apps file in RAM, and (carry) every User Net area carried.
struct UNode : Node {
    AppsFile apps;
    explicit UNode(uint8_t seedByte, bool carry = true) : Node(seedByte) {
        apps.wire(core);
        sayNet2(*this);
        if (carry) {
            std::vector<std::string> all;
            for (const auto& a : userNetAreas()) all.push_back(a.id);
            core.setUserAreas(all);
        }
    }
    // Rev's admit, deny or veto of a board, applied here.
    bool seal(const std::string& kind, const std::string& board, uint32_t seq) {
        return core.takeNotice(makeNotice(rev0(), 0, kind, board, seq, kind == "veto" ? "" : "1"), "");
    }
};

// The node's card says what a Phase 3 board's says (kNetCardLinks: net2 and net3): peers send it listings.
inline void sayNet3(Node& n) {
    NetCard c = n.card;
    c.links = kNetCardLinks;
    c.seq = n.card.seq + 1;
    REQUIRE(signCard(c, n.key));
    REQUIRE(n.core.setOwnCard(c));
    n.card = c;
}

// /net/files.idx in RAM, wired into a NetCore with setListingIndex. Each build draws its key from a counter, so two
// builds draw different keys. Reads past the end fail, as a missing file's do.
struct IdxFile {
    std::string bytes;
    uint8_t     draw = 0x40;
    int         draws = 0;
    void wire(NetCore& core) {
        core.setListingIndex(
            [this](uint32_t off, void* p, size_t n) {
                if ((size_t)off + n > bytes.size()) return false;
                std::memcpy(p, bytes.data() + off, n);
                return true;
            },
            [this](uint32_t off, const void* p, size_t n) {
                if (bytes.size() < (size_t)off + n) bytes.resize((size_t)off + n);
                std::memcpy(&bytes[off], p, n);
                return true;
            },
            [this](void* p, size_t n) {
                draws++;
                for (size_t i = 0; i < n; i++) ((uint8_t*)p)[i] = (uint8_t)(draw + i * 7);
                draw = (uint8_t)(draw + 0x11);
            });
    }
    // The head's words: magic, clean, count, key[4], hasLast, lastNum, lastAt, end.
    uint32_t word(size_t i) const {
        uint32_t w = 0;
        if (bytes.size() >= (i + 1) * 4) std::memcpy(&w, bytes.data() + i * 4, 4);
        return w;
    }
    std::string key() const { return bytes.size() >= 28 ? bytes.substr(12, 16) : std::string(); }
    void setWord(size_t i, uint32_t w) { std::memcpy(&bytes[i * 4], &w, 4); }
};

// Steps the listing index until it is ready (or n calls).
inline int indexReady(NetCore& core, int n = 100000) {
    int steps = 0;
    while (steps < n && core.listingIndexStep()) steps++;
    return steps;
}

// A Phase 3 node: User Net carried, net3 on its card, the listing index in RAM and built.
struct FNode : UNode {
    IdxFile idx;
    explicit FNode(uint8_t seedByte, bool carry = true) : UNode(seedByte, carry) {
        sayNet3(*this);
        idx.wire(core);
        indexReady(core);
        REQUIRE(core.listingIndexReady());
    }
    // Rev admits each board here (seqs from 1, in order).
    void admit(std::initializer_list<const std::string*> boards) {
        uint32_t seq = admitted_;
        for (const std::string* b : boards) REQUIRE(seal("admit", *b, ++seq));
        admitted_ = seq;
    }
    // Every record in nf_list, oldest first (a header scan of the file store).
    std::vector<NetListing> records() {
        std::vector<NetListing> out;
        files.scanHeaders(kNetFilesArea, [&](const axis::bbs::Message& m) {
            NetListing l;
            if (decode(m.net, l)) out.push_back(l);
            return true;
        });
        return out;
    }
    // The seqs of the live (not gone) records of one name.
    std::vector<uint32_t> liveSeqs(const std::string& name, const std::string& home = "") {
        std::vector<uint32_t> out;
        for (const auto& l : records())
            if (l.name == name && !l.gone && (home.empty() || l.board == home)) out.push_back(l.seq);
        return out;
    }
    // The ids listingsPage shows, newest first.
    std::vector<std::string> page(int skip = 0, int max = 1000, int* next = nullptr) {
        std::vector<std::string> out;
        int n = core.listingsPage(skip, max, [&](const NetListing& l) { out.push_back(listingId(l)); });
        if (next) *next = n;
        return out;
    }
    uint32_t admitted_ = 0;
};

// A signed listing (or removal) from the board with key k.
inline NetListing makeListing(const KeyPair& k, uint32_t seq, const std::string& name = "FILE.ZIP", bool gone = false,
                              uint32_t date = kNow, const std::string& area = "files", uint32_t size = 1234) {
    NetListing l;
    l.board = boardOf(k);
    l.area = area;
    l.name = name;
    l.desc = gone ? "" : "A file";
    l.uploader = "Sysop";
    l.reach = "i";
    l.seq = seq;
    l.size = gone ? 0 : size;
    l.crc = gone ? 0 : 0x1234u + seq;
    l.date = date;
    l.gone = gone;
    REQUIRE(signListing(l, k));
    return l;
}

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

// Like run, keeping what each side said: fromA is what a sent to b, fromB what b sent to a.
inline void runBoth(NetSession& a, NetSession& b, std::string& fromA, std::string& fromB, int rounds = 5000) {
    uint32_t ms = 0;
    for (int i = 0; i < rounds; i++) {
        std::string x = a.takeOut();
        fromA += x;
        if (!x.empty()) b.feed(x.data(), x.size());
        std::string y = b.takeOut();
        fromB += y;
        if (!y.empty()) a.feed(y.data(), y.size());
        a.tick(ms, true);
        b.tick(ms, true);
        ms += 10;
        if (settled(a) && settled(b)) break;
    }
    std::string x = a.takeOut();
    fromA += x;
    if (!x.empty()) b.feed(x.data(), x.size());
    std::string y = b.takeOut();
    fromB += y;
    if (!y.empty()) a.feed(y.data(), y.size());
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
