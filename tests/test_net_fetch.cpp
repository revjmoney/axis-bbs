// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net Files fetch calls over a memory pipe: FetchServer and FetchClient end to end, their refusals, the stall and
// keepalive timers, the client's ring and crc check, re-requests from a new offset, and ZMODEM and XMODEM senders reading
// through a client with the ReadyAt hook.
#include "bbs/features.h"
#if AXIS_NETS && AXIS_TRANSFERS
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "net/crc.h"
#include "net/fetch.h"
#include "net/xmodem.h"
#include "net/zmodem.h"
#include "net_testutil.h"

using namespace nettest;
using axis::door::ByteStream;
using St = FetchClient::St;

namespace {

std::vector<uint8_t> bytes(size_t n, uint32_t seed = 7) {  // every byte value, controls and 0xFF included
    std::vector<uint8_t> v(n);
    uint32_t             x = seed;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245 + 12345;
        v[i] = (uint8_t)(x >> 16);
    }
    return v;
}

const KeyPair& homeKey() {
    static KeyPair k = keyOf(0x71);
    return k;
}
const KeyPair& myKey() {
    static KeyPair k = keyOf(0x72);
    return k;
}

NetListing listingFor(const std::vector<uint8_t>& file, const std::string& name = "TEST.BIN") {
    NetListing l;
    l.board = boardOf(homeKey());
    l.area = "files";
    l.name = name;
    l.desc = "A test file";
    l.uploader = "Sysop";
    l.reach = "i";
    l.seq = 5;
    l.size = (uint32_t)file.size();
    l.crc = crc32(file.data(), file.size());
    l.date = kNow;
    REQUIRE(signListing(l, homeKey()));
    return l;
}

// One fetch with everything a host does around it: the home board's check (the caller's signature first, as main's check
// does, then the listing), a TCP-like buffer of at most 4 KB between the server's output and the client, and the
// re-request from a new offset that main's ReadyAt makes when the sender asks outside the window (the running crc goes on
// with it; at most kFetchRetries).
struct Call {
    std::vector<uint8_t>         file;
    NetListing                   l;
    KeyPair                      serverKey = homeKey();
    std::unique_ptr<FetchServer> srv;
    std::unique_ptr<FetchClient> cli;
    std::string                  wire;     // what the server sent and the client has not taken (TCP's buffer)
    std::string                  toServer;  // everything the client sent this call
    bool                         carryUp = true;  // the client's bytes reach the server
    long                         flipAt = -1;     // the home board's file has this byte changed
    int                          flipCalls = 0;   // ... only in calls up to this one (0: in every call)
    int                          flipCall = 0;    // ... only in this call (0: any)
    const char*                  refuse = nullptr;  // the check's NO word
    long                         serveSize = -1;    // the check's file size, if not the file's
    uint32_t                     now = 0;
    int                          calls = 0, retries = 0;
    bool                         forward = false;  // a re-request skipped bytes: the end cannot be checked
    uint32_t                     lastAt = 0;       // where the last re-request started (a crc checkpoint)

    explicit Call(size_t n, uint32_t seed = 7) : file(bytes(n, seed)), l(listingFor(file)) {}

    ServeFile check(const NetFetch& f) {
        ServeFile s;
        if (!verifyFetch(f, pubOf(myKey()))) {
            s.no = "sig";
            return s;
        }
        if (refuse) {
            s.no = refuse;
            return s;
        }
        if (f.listing != listingId(l)) {
            s.no = "gone";
            return s;
        }
        s.ok = true;
        s.size = serveSize >= 0 ? (uint32_t)serveSize : (uint32_t)file.size();
        s.read = [this](uint32_t off, uint8_t* buf, size_t k) {
            if ((size_t)off + k > file.size()) return false;
            for (size_t i = 0; i < k; i++) buf[i] = (long)(off + i) == flipAt && (!flipCalls || calls <= flipCalls) && (!flipCall || calls == flipCall) ? (uint8_t)(file[off + i] ^ 0x5A) : file[off + i];
            return true;
        };
        return s;
    }
    void start(uint32_t off, uint32_t cs = 0xFFFFFFFFu, uint32_t cp = 0) {
        calls++;
        if (calls == 1 && off > 0) forward = true;  // a caller resuming its own partial file
        srv.reset(new FetchServer(l.board, serverKey, nonce((uint8_t)(0x30 + calls)), [this](const NetFetch& f) { return check(f); }));
        srv->feed("AXISFILE 1 ", 11);  // the host's first-line dispatch consumed it and feeds it back
        cli.reset(new FetchClient(l, pubOf(homeKey()), "Caller", myKey(), boardOf(myKey()), nonce((uint8_t)(0x50 + calls)), off, cs, cp));
        wire.clear();
    }
    // One loop turn: the client's lines to the server, the server's output into the buffer, as much of it as the client
    // has room for into the client, then both ticks (10 ms a turn).
    void turn() {
        std::string c = cli->takeOut();
        toServer += c;
        if (carryUp && !c.empty() && !srv->finished()) {
            // "AXISFILE 1 " was fed already: the rest of the first line
            if (c.compare(0, 11, "AXISFILE 1 ") == 0) c.erase(0, 11);
            srv->feed(c.data(), c.size());
        }
        if (wire.size() < 4096 && !srv->finished()) wire += srv->takeOut(4096 - wire.size());
        size_t k = cli->feed(wire.data(), std::min(wire.size(), cli->room()));
        wire.erase(0, k);
        srv->tick(now);
        cli->tick(now);
        now += 10;
    }
    void turns(int n) {
        for (int i = 0; i < n; i++) turn();
    }
    bool settled() const { return cli->state() == St::Streaming || cli->state() == St::Done || cli->state() == St::Failed; }
    void connect() {
        for (int i = 0; i < 50 && !settled(); i++) turn();
    }
    // The sender's ReadyAt, as main's: a fetch that can never deliver says true (the read then fails); outside the window
    // a new call from there carrying the crc.
    bool readyAt(uint32_t off, size_t n) {
        turn();
        if (cli->state() == St::Failed) return true;
        if (off < l.size && cli->behind(off)) {
            if (retries >= kFetchRetries) return true;
            retries++;
            uint32_t              at = off, cs = 0xFFFFFFFFu;
            FetchClient::CrcMarks marks;
            if (cli->crcPos() != kFetchNoCrc && cli->checkpoint(off, at, cs)) marks = cli->takeMarks();
            else forward = true;
            lastAt = at;
            start(at, cs, forward ? kFetchNoCrc : at);
            cli->giveMarks(std::move(marks));
            cli->aim(off);
            connect();
            return false;
        }
        return cli->ready(off, n);
    }
    bool readAt(uint32_t off, uint8_t* buf, size_t n) { return cli->state() != St::Failed && cli->read(off, buf, n); }
    // Reads the whole file in order, k bytes at a time, as a sender would. Returns what it got.
    std::vector<uint8_t> drain(uint32_t from = 0, size_t k = 1024, int maxTurns = 20000) {
        std::vector<uint8_t> got;
        uint32_t             off = from;
        for (int i = 0; i < maxTurns && off < l.size && cli->state() != St::Failed; i++) {
            size_t want = std::min(k, (size_t)(l.size - off));
            if (!readyAt(off, want)) continue;
            std::vector<uint8_t> b(want);
            if (!readAt(off, b.data(), want)) break;
            got.insert(got.end(), b.begin(), b.end());
            off += (uint32_t)want;
        }
        for (int i = 0; i < 20 && cli->state() == St::Streaming; i++) turn();
        return got;
    }
};

std::vector<uint8_t> slice(const std::vector<uint8_t>& v, size_t from, size_t to) {
    return std::vector<uint8_t>(v.begin() + (long)from, v.begin() + (long)to);
}

// A line end the sender writes to and the caller's program reads from (as in test_transfer.cpp).
struct End : ByteStream {
    std::deque<uint8_t>* in;
    std::deque<uint8_t>* out;
    long                 sent = 0, corruptAt = -1;
    int  available() override { return (int)in->size(); }
    int  peek() override { return in->empty() ? -1 : in->front(); }
    int  read() override {
        if (in->empty()) return -1;
        int c = in->front();
        in->pop_front();
        return c;
    }
    void write(const uint8_t* p, size_t n) override {
        for (size_t i = 0; i < n; i++, sent++) out->push_back(sent == corruptAt ? (uint8_t)(p[i] ^ 0x5A) : p[i]);
    }
    bool carrier() override { return true; }
};
struct Line {
    std::deque<uint8_t> ab, ba;
    End                 a, b;  // a = the board, b = the caller's program
    Line() {
        a.in = &ba;
        a.out = &ab;
        b.in = &ab;
        b.out = &ba;
    }
};

// A ZRPOS hex header as the caller's program sends it.
std::string zrpos(uint32_t pos) {
    uint8_t  b[5] = {9, (uint8_t)pos, (uint8_t)(pos >> 8), (uint8_t)(pos >> 16), (uint8_t)(pos >> 24)};
    uint16_t crc = crc16Xmodem(0, b, 5);
    char     s[40];
    int      n = std::snprintf(s, sizeof s, "**\x18" "B%02x%02x%02x%02x%02x%02x%02x\r\n", b[0], b[1], b[2], b[3], b[4], crc >> 8,
                               crc & 0xFF);
    return std::string(s, (size_t)n);
}

void runPair(Transfer& x, Transfer& y, int steps = 40000) {
    uint32_t now = 0;
    bool     a = true, b = true;
    for (int i = 0; i < steps && (a || b); i++, now += 10) {
        if (a) a = x.step(now);
        if (b) b = y.step(now);
    }
}

}  // namespace

TEST_CASE("fetch: a full call sends a 5,000 byte file and the crc checks") {
    Call c(5000);
    c.start(0);
    c.connect();
    REQUIRE(c.cli->state() == St::Streaming);
    std::vector<uint8_t> got = c.drain();
    CHECK(got == c.file);
    CHECK(crc32(got.data(), got.size()) == c.l.crc);
    CHECK(c.cli->state() == St::Done);
    CHECK(c.cli->verified());
    CHECK(c.srv->finished());
    CHECK_FALSE(c.srv->failed());
    CHECK(std::string(c.srv->why()).empty());
    CHECK(c.calls == 1);
}

TEST_CASE("fetch: a HI signed by the wrong key fails the client sig, and no GET goes out") {
    Call c(5000);
    c.serverKey = keyOf(0x73);  // claims the home board's id with another key
    c.start(0);
    c.connect();
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "sig");
    CHECK(c.toServer.find("GET ") == std::string::npos);
}

TEST_CASE("fetch: a GET replayed from an earlier call is refused sig") {
    Call c(5000);
    c.start(0);
    c.connect();
    REQUIRE(c.cli->state() == St::Streaming);
    size_t g = c.toServer.find("GET ");
    REQUIRE(g != std::string::npos);
    std::string get = c.toServer.substr(g, c.toServer.find('\n', g) + 1 - g);
    std::string dial = c.toServer.substr(11, 16);
    // A new call to the home board (a new server nonce) with the same dial nonce and the old GET.
    bool        checked = false;
    FetchServer s(c.l.board, homeKey(), nonce(0x3F), [&](const NetFetch&) {
        checked = true;
        return ServeFile();
    });
    std::string hello = "AXISFILE 1 " + dial + "\r\n";
    s.feed(hello.data(), hello.size());
    s.feed(get.data(), get.size());
    std::string out = s.takeOut(4096);
    CHECK(out.find("HI ") == 0);
    CHECK(out.find("NO sig\r\n") != std::string::npos);
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "sig");
    CHECK_FALSE(checked);  // the nonces are checked before the host's check runs
}

TEST_CASE("fetch: the server refuses with the check's word and the client reports it") {
    for (const char* word : {"member", "gone", "size", "banned"}) {
        CAPTURE(word);
        Call c(5000);
        c.refuse = word;
        c.start(0);
        c.connect();
        c.turns(5);
        CHECK(c.cli->state() == St::Failed);
        CHECK(std::string(c.cli->why()) == word);
        CHECK(c.srv->failed());
        CHECK(std::string(c.srv->why()) == word);
    }
    {  // a GET whose signature does not match the calling board's key: the check's verifyFetch says sig
        Call        c(5000);
        FetchServer s(c.l.board, homeKey(), nonce(0x3C), [&](const NetFetch& f) { return c.check(f); });
        std::string dial = nonce(0x5C);
        std::string hello = "AXISFILE 1 " + dial + "\r\n";
        s.feed(hello.data(), hello.size());
        std::string hi = s.takeOut(4096);
        REQUIRE(hi.size() == 167);
        NetFetch f;
        f.board = boardOf(myKey());
        f.listing = listingId(c.l);
        f.handle = "Caller";
        f.dialNonce = dial;
        f.servNonce = hi.substr(20, 16);
        REQUIRE(signFetch(f, myKey()));
        f.handle = "Someone";  // changed after signing
        std::string get = "GET " + encode(f) + "\r\n";
        s.feed(get.data(), get.size());
        CHECK(s.takeOut(4096) == "NO sig\r\n");
        CHECK(std::string(s.why()) == "sig");
    }
    {  // a removed listing (another id at the home board): gone
        Call c(5000);
        NetListing other = c.l;
        other.seq = 6;
        REQUIRE(signListing(other, homeKey()));
        c.l = other;  // the client asks for seq 6; the home board holds seq 5 only
        c.start(0);
        c.l = listingFor(c.file);
        c.connect();
        c.turns(5);
        CHECK(std::string(c.cli->why()) == "gone");
    }
}

TEST_CASE("fetch: NO busy from the host is the client's busy; a word outside the list reads bad") {
    Call c(5000);
    c.start(0);
    c.cli->takeOut();
    c.cli->feed("NO busy\r\n", 9);
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "busy");
    Call d(5000);
    d.start(0);
    d.cli->takeOut();
    d.cli->feed("NO whatever\r\n", 13);
    CHECK(std::string(d.cli->why()) == "bad");
}

TEST_CASE("fetch: an OK whose count disagrees with the listing fails size") {
    Call c(5000);
    c.serveSize = 4000;  // the file changed size at home after it was listed
    c.start(0);
    c.connect();
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "size");
}

TEST_CASE("fetch: a home board that closes short leaves the client streaming, never Done (the host calls it closed)") {
    Call c(40000);
    c.start(0);
    c.connect();
    REQUIRE(c.cli->state() == St::Streaming);
    c.drain(0, 1000, 30);  // some way in, then the home board's socket closes: nothing more arrives
    c.srv.reset(new FetchServer(c.l.board, homeKey(), nonce(0x3B), [](const NetFetch&) { return ServeFile(); }));
    c.wire.clear();
    for (int i = 0; i < 100; i++) {
        c.cli->tick(c.now);
        c.now += 10;
    }
    CHECK(c.cli->state() == St::Streaming);
    CHECK_FALSE(c.cli->verified());
}

TEST_CASE("fetch: an offset past the listing's size fails size before any call") {
    Call c(5000);
    FetchClient f(c.l, pubOf(homeKey()), "Caller", myKey(), boardOf(myKey()), nonce(0x51), 5001);
    CHECK(f.state() == St::Failed);
    CHECK(std::string(f.why()) == "size");
    CHECK(f.takeOut().empty());
    CHECK(f.room() == 0);
}

TEST_CASE("fetch: a 1025 byte line fails both ends") {
    std::string longLine(kFetchMaxLine + 1, 'x');
    Call        c(5000);
    c.start(0);
    c.cli->takeOut();
    c.cli->feed(longLine.data(), longLine.size());
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "bad");
    FetchServer s(c.l.board, homeKey(), nonce(0x3E), [](const NetFetch&) { return ServeFile(); });
    s.feed(longLine.data(), longLine.size());
    CHECK(s.failed());
    CHECK(std::string(s.why()) == "bad");
    CHECK(s.takeOut(100) == "NO bad\r\n");
}

TEST_CASE("fetch: no bytes for kFetchStallMs ends the call; a full ring does not") {
    {
        Call c(40000);
        c.start(0);
        c.connect();
        REQUIRE(c.cli->state() == St::Streaming);
        // The home board goes quiet: nothing more reaches the client, whose ring has room.
        c.wire.clear();
        c.srv.reset(new FetchServer(c.l.board, homeKey(), nonce(0x3D), [](const NetFetch&) { return ServeFile(); }));
        std::vector<uint8_t> b(1024);
        REQUIRE(c.cli->ready(0, 1024));
        REQUIRE(c.cli->read(0, b.data(), 1024));
        REQUIRE(c.cli->room() > 0);
        uint32_t t0 = c.now;
        while (c.cli->state() == St::Streaming && c.now - t0 < 2 * kFetchStallMs) {
            c.cli->tick(c.now);
            c.now += 100;
        }
        CHECK(c.cli->state() == St::Failed);
        CHECK(std::string(c.cli->why()) == "stall");
        CHECK(c.now - t0 > kFetchStallMs);
        CHECK(c.now - t0 <= kFetchStallMs + 200);
    }
    {  // the caller's sender reads nothing for 60 s: the ring stays full (room 0) and the call lives
        Call c(40000);
        c.start(0);
        c.connect();
        c.turns(20);
        REQUIRE(c.cli->room() == 0);
        for (int i = 0; i < 6000; i++) c.turn();  // 60 s
        CHECK(c.cli->state() == St::Streaming);
        CHECK_FALSE(c.srv->failed());  // the client's keepalives reached it
        CHECK(c.toServer.find('\n', c.toServer.find("GET ")) != std::string::npos);
        std::vector<uint8_t> got = c.drain();
        CHECK(got == c.file);
        CHECK(c.cli->verified());
    }
}

TEST_CASE("fetch: a slow caller keeps the call alive; without its keepalives the server stalls") {
    Call c(40000);
    c.start(0);
    c.connect();
    c.turns(20);
    REQUIRE(c.cli->room() == 0);
    for (int i = 0; i < 6000; i++) c.turn();
    CHECK_FALSE(c.srv->failed());
    size_t keepalives = 0;
    for (size_t i = c.toServer.find("GET "); i < c.toServer.size(); i++)
        if (c.toServer[i] == '\n') keepalives++;
    CHECK(keepalives >= 5);  // one each kFetchStallMs / 2 for 60 s, after the GET's own line end

    Call d(40000);
    d.start(0);
    d.connect();
    d.turns(20);
    d.carryUp = false;  // the dialer's bytes never arrive: the server hears nothing and can send nothing
    for (int i = 0; i < 3000 && !d.srv->failed(); i++) d.turn();
    CHECK(d.srv->failed());
    CHECK(std::string(d.srv->why()) == "stall");
}

TEST_CASE("fetch: the ring") {
    Call c(40000);
    c.start(0);
    c.connect();
    c.turns(20);
    // Nobody has read: the client took one window from offset 0 and stops.
    CHECK(c.cli->room() == 0);
    std::vector<uint8_t> b(1024);
    CHECK(c.cli->ready(0, 1024));
    CHECK(c.cli->read(0, b.data(), 1024));
    CHECK(b == slice(c.file, 0, 1024));
    CHECK(c.cli->ready(7168, 1024));  // the window's last block
    CHECK_FALSE(c.cli->ready(7168, 1025));
    CHECK_FALSE(c.cli->behind(0));
    CHECK(c.cli->ready(0, 1));  // the sender is back at the start (asking ahead moved the window's reach)
    // Read on to 20,000: the window moved past the start.
    std::vector<uint8_t> got;
    uint32_t             off = 0;
    for (int i = 0; i < 2000 && off < 20000; i++) {
        c.turn();
        if (c.cli->ready(off, 1000) && c.cli->read(off, b.data(), 1000)) {
            got.insert(got.end(), b.begin(), b.begin() + 1000);
            off += 1000;
        }
    }
    REQUIRE(off == 20000);
    CHECK(got == slice(c.file, 0, 20000));
    c.turns(5);
    CHECK_FALSE(c.cli->ready(4000, 100));
    CHECK_FALSE(c.cli->read(4000, b.data(), 100));
    CHECK(c.cli->behind(4000));  // below the window
    CHECK(c.cli->ready(19000, 1000));  // still inside it
    CHECK(c.cli->read(19000, b.data(), 1000));
    CHECK(slice(b, 0, 1000) == slice(c.file, 19000, 20000));
    CHECK(c.cli->behind(39999 + kFetchWindow) == false);  // at or past the end: never behind
    CHECK(c.cli->behind(39000));                          // past what arrived plus the window
    CHECK_FALSE(c.cli->ready(39000, 100));
    // A forward ask past what arrived (inside the reach): room is still at most the window.
    CHECK(c.cli->room() <= kFetchWindow);
    c.cli->ready(26000, 1024);
    CHECK(c.cli->room() <= kFetchWindow);
    CHECK(c.cli->room() > 0);
}

TEST_CASE("fetch: a byte changed on the way fails crc and the last block never reads") {
    Call c(40000);
    c.flipAt = 39000;
    c.start(0);
    c.connect();
    std::vector<uint8_t> got = c.drain();
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "crc");
    CHECK(got.size() < c.file.size());
    CHECK(got.size() <= 39000);  // the block holding the changed byte, and the end, never came out
    CHECK_FALSE(c.cli->ready(39000, 1000));
    std::vector<uint8_t> b(1000);
    CHECK_FALSE(c.cli->read(39000, b.data(), 1000));
}

TEST_CASE("fetch: a rewind re-request carries the crc and verifies; with a changed byte it fails crc") {
    for (bool flip : {false, true}) {
        CAPTURE(flip);
        Call c(40000);
        c.start(0);
        c.connect();
        uint32_t             off = 0;
        std::vector<uint8_t> b(1000);
        for (int i = 0; i < 4000 && off < 25000; i++)
            if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
        REQUIRE(off == 25000);
        // The home board's copy changes under the second call, past what the first call brought (the crc covers it).
        if (flip) c.flipAt = 39000;
        // The receiver seeks back to 4,000: outside the window, a new call from there.
        CHECK_FALSE(c.readyAt(4000, 1000));
        CHECK(c.calls == 2);
        CHECK_FALSE(c.forward);
        std::vector<uint8_t> rest = c.drain(4000, 1000);
        if (!flip) {
            CHECK(rest == slice(c.file, 4000, 40000));
            CHECK(c.cli->state() == St::Done);
            CHECK(c.cli->verified());
        } else {
            CHECK(c.cli->state() == St::Failed);
            CHECK(std::string(c.cli->why()) == "crc");
        }
    }
}

// The rewind asks again from the crc checkpoint below 4,000 (offset 0), so the bytes it sends again are checked.
TEST_CASE("fetch: a byte changed in the part a rewind re-request sends again fails crc") {
    Call c(40000);
    c.start(0);
    c.connect();
    uint32_t             off = 0;
    std::vector<uint8_t> b(1000);
    for (int i = 0; i < 4000 && off < 25000; i++)
        if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
    REQUIRE(off == 25000);
    uint32_t covered = c.cli->crcPos();  // the first call brought this much
    REQUIRE(covered > 10000);
    c.flipAt = 10000;  // the home board's copy changes inside what the second call sends again
    CHECK_FALSE(c.readyAt(4000, 1000));
    REQUIRE(c.calls == 2);
    std::vector<uint8_t> rest = c.drain(4000, 1000);
    bool same = rest == slice(c.file, 4000, 4000 + rest.size());
    INFO("delivered bytes match the listing: " << same << ", state " << (int)c.cli->state());
    CHECK((c.cli->state() == St::Failed && std::string(c.cli->why()) == "crc"));
    CHECK_FALSE(c.cli->verified());
}

TEST_CASE("fetch: a rewind asks again from the crc checkpoint below it; an unchanged home copy still verifies") {
    Call c(40000);
    c.start(0);
    c.connect();
    uint32_t             off = 0;
    std::vector<uint8_t> b(1000);
    for (int i = 0; i < 4000 && off < 30000; i++)
        if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
    REQUIRE(off == 30000);
    uint32_t at = 0, state = 0;
    REQUIRE(c.cli->checkpoint(13000, at, state));
    CHECK(at == 8192);  // every kFetchWindow from 0
    CHECK_FALSE(c.readyAt(13000, 1000));
    CHECK(c.calls == 2);
    CHECK(c.lastAt == 8192);
    CHECK_FALSE(c.forward);
    std::vector<uint8_t> rest = c.drain(13000, 1000);
    CHECK(rest == slice(c.file, 13000, 40000));
    CHECK(c.cli->verified());
}

TEST_CASE("fetch: the crc checkpoints halve when full and stay within 1 KB; a rewind across the coarser spacing verifies") {
    const size_t big = 3 * 1024 * 1024;  // 384 windows: past kFetchCrcMarks
    Call         c(big, 11);
    c.start(0);
    c.connect();
    uint32_t             off = 0;
    std::vector<uint8_t> b(4096);
    for (int i = 0; i < 200000 && off < 2600000; i++)
        if (c.readyAt(off, 4096) && c.readAt(off, b.data(), 4096)) off += 4096;
    REQUIRE(off >= 2600000);
    uint32_t at = 0, state = 0;
    REQUIRE(c.cli->checkpoint(2 * 1024 * 1024 + 20000, at, state));
    CHECK(at == 2 * 1024 * 1024 + 16384);  // the spacing doubled to 16 KB
    // A rewind 12,000 past a checkpoint: with 16 KB spacing that is more than a window back.
    uint32_t target = 61 * 16384 + 12000;  // 12,000 past a 16 KB checkpoint
    REQUIRE(c.cli->checkpoint(target, at, state));
    CHECK(at % 16384 == 0);
    CHECK(target - at > kFetchWindow);
    c.flipAt = -1;
    CHECK_FALSE(c.readyAt(target, 4096));
    CHECK(c.lastAt == at);
    std::vector<uint8_t> rest = c.drain(target, 4096, 400000);
    CHECK(rest == slice(c.file, target, big));
    CHECK(c.cli->state() == St::Done);
    CHECK(c.cli->verified());
    FetchClient::CrcMarks m = c.cli->takeMarks();
    CHECK(m.count <= kFetchCrcMarks);
    CHECK(kFetchCrcMarks * sizeof(uint32_t) <= 1024);  // the whole table, taken once
    CHECK(m.step == 16384);
}

TEST_CASE("fetch: a plain rewind fails crc when the byte differed only in the first call; unchanged it verifies") {
    for (int flipCalls : {1, 0}) {
        CAPTURE(flipCalls);
        Call c(40000);
        if (flipCalls) {
            c.flipAt = 10000;
            c.flipCalls = 1;
        }
        c.start(0);
        c.connect();
        uint32_t             off = 0;
        std::vector<uint8_t> b(1000);
        for (int i = 0; i < 4000 && off < 25000; i++)
            if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
        REQUIRE(off == 25000);
        CHECK_FALSE(c.readyAt(4000, 1000));  // a new call from the checkpoint at 0, with the right bytes this time
        std::vector<uint8_t> rest = c.drain(4000, 1000);
        if (flipCalls) {
            CHECK(c.cli->state() == St::Failed);
            CHECK(std::string(c.cli->why()) == "crc");  // the second call's state at the next checkpoint is not the first's
        } else {
            CHECK(rest == slice(c.file, 4000, 40000));
            CHECK(c.cli->verified());
        }
    }
}

TEST_CASE("fetch: a byte changed between the last checkpoint and where the first call stopped fails crc on the rewind") {
    Call c(40000);
    c.flipAt = 30000;  // past the last checkpoint the first call reached (24,576), before where it stopped
    c.flipCalls = 1;
    c.start(0);
    c.connect();
    uint32_t             off = 0;
    std::vector<uint8_t> b(1000);
    for (int i = 0; i < 4000 && off < 25000; i++)
        if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
    REQUIRE(off == 25000);
    REQUIRE(c.cli->crcPos() > 30000);
    uint32_t at = 0, state = 0;
    REQUIRE(c.cli->checkpoint(c.cli->crcPos(), at, state));
    REQUIRE(at < 30000);
    CHECK_FALSE(c.readyAt(4000, 1000));
    c.drain(4000, 1000);
    CHECK(c.cli->state() == St::Failed);
    CHECK(std::string(c.cli->why()) == "crc");  // the second call's state where the first stopped is not the first's
}

TEST_CASE("fetch: with 16 KB checkpoints, a byte that differed only in the second call fails crc after a second rewind") {
    for (bool flip : {true, false}) {
        CAPTURE(flip);
        const size_t big = 3 * 1024 * 1024;
        Call         c(big, 13);
        const uint32_t S = 16384, j = 61, r2 = j * S + 1076;  // a 16 KB checkpoint, and a place just past it
        if (flip) {
            c.flipAt = r2 - 300;  // the caller gets this byte from the second call only
            c.flipCall = 2;
        }
        c.start(0);
        c.connect();
        uint32_t             off = 0;
        std::vector<uint8_t> b(4096);
        for (int i = 0; i < 200000 && off < 2600000; i++)  // the first call: well past 2 MB, so the spacing is 16 KB
            if (c.readyAt(off, 4096) && c.readAt(off, b.data(), 4096)) off += 4096;
        REQUIRE(off >= 2600000);
        // First rewind, a little before r2: the second call starts at the checkpoint j * S.
        uint32_t t1 = j * S + 500;
        CHECK_FALSE(c.readyAt(t1, 1000));
        REQUIRE(c.calls == 2);
        REQUIRE(c.lastAt == j * S);
        // The caller reads on to about 7 KB, the second call stopping short of the next checkpoint.
        off = t1;
        std::vector<uint8_t> k(1000);
        for (int i = 0; i < 4000 && off < j * S + 7000; i++)
            if (c.readyAt(off, 1000) && c.readAt(off, k.data(), 1000)) off += 1000;
        REQUIRE(c.calls == 2);
        uint32_t p2 = c.cli->crcPos();
        CHECK(p2 < (j + 1) * S);  // the second call never reached a checkpoint the first one recorded
        CHECK(r2 + kFetchWindow < p2);
        // Second rewind, to r2: the third call starts at j * S again and must match where the second stopped.
        CHECK_FALSE(c.readyAt(r2, 1000));
        REQUIRE(c.calls == 3);
        CHECK(c.lastAt == j * S);
        std::vector<uint8_t> rest = c.drain(r2, 4096, 400000);
        if (flip) {
            CHECK(c.cli->state() == St::Failed);
            CHECK(std::string(c.cli->why()) == "crc");
        } else {
            CHECK(rest == slice(c.file, r2, big));
            CHECK(c.cli->verified());
        }
    }
}

TEST_CASE("fetch: a resumed fetch stays unverified across a re-request") {
    Call c(40000);
    c.start(10000);
    c.connect();
    CHECK(c.forward);
    uint32_t             off = 10000;
    std::vector<uint8_t> b(1000);
    for (int i = 0; i < 4000 && off < 30000; i++)
        if (c.readyAt(off, 1000) && c.readAt(off, b.data(), 1000)) off += 1000;
    REQUIRE(off == 30000);
    CHECK_FALSE(c.readyAt(12000, 1000));  // a rewind: no checkpoints, so a call from 12,000, still unchecked
    CHECK(c.calls == 2);
    CHECK(c.lastAt == 12000);
    std::vector<uint8_t> rest = c.drain(12000, 1000);
    CHECK(rest == slice(c.file, 12000, 40000));
    CHECK(c.cli->state() == St::Done);
    CHECK_FALSE(c.cli->verified());
    CHECK(c.cli->crcPos() == kFetchNoCrc);
}

TEST_CASE("fetch: a forward resume reports unverified") {
    Call c(40000);
    c.start(10000);  // a receiver resuming its own partial file: the bytes before 10,000 are never seen here
    c.connect();
    std::vector<uint8_t> got = c.drain(10000);
    CHECK(got == slice(c.file, 10000, 40000));
    CHECK(c.cli->state() == St::Done);
    CHECK(c.cli->crcPos() == kFetchNoCrc);
    CHECK_FALSE(c.cli->verified());
}

TEST_CASE("fetch: bytes after Done are dropped, and the server ignores input once serving") {
    Call c(3000);
    c.start(0);
    c.connect();
    REQUIRE(c.cli->state() == St::Streaming);
    c.srv->feed("GET junk\r\nAXISFILE 1 x\r\n", 24);  // a dialer's later bytes count as life only
    CHECK_FALSE(c.srv->failed());
    std::vector<uint8_t> got = c.drain();
    CHECK(got == c.file);
    REQUIRE(c.cli->state() == St::Done);
    CHECK(c.cli->feed("extra", 5) == 5);
    CHECK(c.cli->state() == St::Done);
    CHECK(c.cli->verified());
    std::vector<uint8_t> b(100);
    CHECK(c.cli->read(2900, b.data(), 100));
    CHECK(b == slice(c.file, 2900, 3000));
}

TEST_CASE("fetch: a ZMODEM sender over a client resumes through a re-request after a ZRPOS back past the window") {
    Line l;
    Call c(60000);
    c.start(0);
    c.connect();
    REQUIRE(c.cli->state() == St::Streaming);
    std::vector<uint8_t> got;
    bool                 ended = false;
    ZmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return c.readAt(off, buf, n); }, c.l.size, "TEST.BIN");
    s.setReady([&](uint32_t off, size_t n) { return c.readyAt(off, n); });
    ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    uint32_t now = 0;
    bool     a = true, b = true, forced = false;
    for (int i = 0; i < 40000 && (a || b); i++, now += 10) {
        if (a) a = s.step(now);
        if (!forced && s.bytes() >= 30000) {  // a ZRPOS back to 5,000, far behind the window
            forced = true;
            std::string z = zrpos(5000);
            l.ba.insert(l.ba.end(), z.begin(), z.end());
        }
        if (b) b = r.step(now);
    }
    INFO("sender: " << s.error() << " / receiver: " << r.error());
    CHECK(forced);
    CHECK(s.result() == Transfer::Result::Ok);
    CHECK(r.result() == Transfer::Result::Ok);
    CHECK(ended);
    CHECK(got == c.file);
    // The rewind went past the window (a new call from the checkpoint below 5,000); the receiver then asked for its own
    // place again, past the new window (one more call, from the last checkpoint the second call reached). Each call
    // carried the crc state, the checkpoints and where each earlier call stopped, and matched them as it went.
    CHECK(c.calls == 3);
    CHECK_FALSE(c.forward);
    CHECK(c.cli->verified());  // checked end to end across the calls
}

TEST_CASE("fetch: a byte that differed only in the first call fails crc after a rewind and a seek back ahead") {
    Line l;
    Call c(60000);
    c.flipAt = 20000;  // the caller's program got this byte changed from the first call only
    c.flipCalls = 1;
    c.start(0);
    c.connect();
    std::vector<uint8_t> got;
    ZmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return c.readAt(off, buf, n); }, c.l.size, "TEST.BIN");
    s.setReady([&](uint32_t off, size_t n) { return c.readyAt(off, n); });
    ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [](bool) {});
    uint32_t now = 0;
    bool     a = true, b = true, forced = false;
    for (int i = 0; i < 40000 && (a || b); i++, now += 10) {
        if (a) a = s.step(now);
        if (!forced && s.bytes() >= 30000) {
            forced = true;
            std::string z = zrpos(5000);
            l.ba.insert(l.ba.end(), z.begin(), z.end());
        }
        if (b) b = r.step(now);
    }
    CHECK(forced);
    CHECK(c.calls >= 2);
    CHECK(std::string(c.cli->why()) == "crc");
    CHECK_FALSE(c.cli->verified());
    CHECK(r.result() != Transfer::Result::Ok);
}

TEST_CASE("fetch: a changed byte gives the caller's ZMODEM program a cancel, not ZEOF") {
    Line l;
    Call c(30000);
    c.flipAt = 29500;
    c.start(0);
    c.connect();
    std::vector<uint8_t> got;
    bool                 ended = false;
    ZmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return c.readAt(off, buf, n); }, c.l.size, "TEST.BIN");
    s.setReady([&](uint32_t off, size_t n) { return c.readyAt(off, n); });
    ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    runPair(s, r);
    CHECK(std::string(c.cli->why()) == "crc");
    CHECK(s.result() == Transfer::Result::Failed);
    CHECK(s.error() == "read error");
    CHECK(r.result() != Transfer::Result::Ok);
    CHECK_FALSE(ended);
    CHECK(got.size() < c.file.size());
}

TEST_CASE("fetch: XMODEM-1K over a client delivers the file") {
    Line l;
    Call c(20000);
    c.start(0);
    c.connect();
    std::vector<uint8_t> got;
    XmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) { return c.readAt(off, buf, n); }, c.l.size, true);
    s.setReady([&](uint32_t off, size_t n) { return c.readyAt(off, n); });
    XmodemReceiver r(l.b, [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; });
    runPair(s, r);
    CHECK(s.result() == Transfer::Result::Ok);
    REQUIRE(got.size() >= c.file.size());
    CHECK(slice(got, 0, c.file.size()) == c.file);  // XMODEM pads the last block with 1Ah
    CHECK(c.cli->verified());
}

TEST_CASE("ReadyAt false holds a ZMODEM sender in Streaming without its timers running") {
    Line                 l;
    auto                 file = bytes(5000);
    bool                 open = false;
    std::vector<uint8_t> got;
    ZmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) {
        std::copy(file.begin() + off, file.begin() + off + (long)n, buf);
        return true;
    }, (uint32_t)file.size(), "HOLD.BIN");
    s.setReady([&](uint32_t, size_t) { return open; });
    ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [](bool) {});
    uint32_t now = 0;
    for (int i = 0; i < 200; i++, now += 10) {  // the handshake: the sender reaches Streaming and waits
        s.step(now);
        r.step(now);
    }
    REQUIRE(s.result() == Transfer::Result::Running);
    size_t before = l.ab.size();
    for (; now < 300000; now += 100) CHECK_FALSE(!s.step(now));  // 5 minutes with no data: no timeout, nothing sent
    CHECK(s.result() == Transfer::Result::Running);
    CHECK(l.ab.size() == before);
    CHECK(got.empty());
    open = true;
    bool a = true, b = true;
    for (int i = 0; i < 20000 && (a || b); i++, now += 10) {
        if (a) a = s.step(now);
        if (b) b = r.step(now);
    }
    CHECK(s.result() == Transfer::Result::Ok);
    CHECK(got == file);
}

TEST_CASE("ReadyAt false holds an XMODEM sender's block: NAKs while it waits count no tries and no timer runs") {
    Line                 l;
    auto                 file = bytes(3000);
    bool                 open = false;
    int                  asks = 0;
    XmodemSender         s(l.a, [&](uint32_t off, uint8_t* buf, size_t n) {
        size_t have = std::min(n, file.size() - off);
        std::copy(file.begin() + off, file.begin() + off + (long)have, buf);
        return true;
    }, (uint32_t)file.size(), true);
    s.setReady([&](uint32_t, size_t) {
        asks++;
        return open;
    });
    uint32_t now = 0;
    l.ba.push_back('C');  // the caller's program starts (CRC)
    s.step(now);
    CHECK(l.ab.empty());  // the first block waits on its data
    for (int i = 0; i < 30; i++) {  // 300 s, a NAK (and a second, duplicate one) every 10 s: past kTries and every timer
        now += 10000;
        l.ba.push_back(0x15);
        l.ba.push_back(0x15);
        CHECK(s.step(now));
    }
    CHECK(s.result() == Transfer::Result::Running);
    CHECK(l.ab.empty());
    CHECK(asks > 0);
    open = true;
    now += 10;
    CHECK(s.step(now));
    REQUIRE(!l.ab.empty());
    CHECK(l.ab.front() == 0x02);  // STX: one 1K block went out
    CHECK(l.ab.size() == 3 + 1024 + 2);  // only one, though two NAKs came in the same step
    // Then the receiver finishes it.
    l.ab.clear();
    std::vector<uint8_t> got;
    XmodemReceiver       r(l.b, [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; });
    l.ba.clear();
    l.ba.push_back(0x15);  // ask for that block again (the receiver never saw it)
    bool a = true, b = true;
    for (int i = 0; i < 40000 && (a || b); i++, now += 10) {
        if (a) a = s.step(now);
        if (b) b = r.step(now);
    }
    CHECK(s.result() == Transfer::Result::Ok);
    REQUIRE(got.size() >= file.size());
    CHECK(slice(got, 0, file.size()) == file);
}

TEST_CASE("fetchLetGo: a follower's slot with a lost e is let go once Done or Failed; a caller's job never is") {
    int caller = 0, follower = 0, job = 0;
    const uint32_t quiet = kFetchIdleMs + 1;
    // begun, no transfer yet: let go after the idle time, ended or not
    CHECK(fetchLetGo(nullptr, &caller, false, quiet));
    CHECK(fetchLetGo(nullptr, &caller, true, quiet));
    CHECK_FALSE(fetchLetGo(nullptr, &caller, false, kFetchIdleMs));
    // a TransferJob here holds it (job is not the owner): never by this rule, even ended and quiet
    CHECK_FALSE(fetchLetGo(&job, &caller, true, quiet));
    CHECK_FALSE(fetchLetGo(&job, &caller, false, quiet));
    // a follower's transfer holds it (job is the owner, its token): kept while the fetch runs, however quiet
    CHECK_FALSE(fetchLetGo(&follower, &follower, false, quiet));
    // Done or Failed and its "e" lost: let go once nobody asked (t and b) for 3 minutes (past the senders' ~110 s give-up)
    CHECK(fetchLetGo(&follower, &follower, true, 3 * kFetchIdleMs + 1));
    CHECK_FALSE(fetchLetGo(&follower, &follower, true, 3 * kFetchIdleMs));
    CHECK_FALSE(fetchLetGo(&follower, &follower, true, quiet));
}

#endif  // AXIS_NETS && AXIS_TRANSFERS
