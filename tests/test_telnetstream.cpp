// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <string>

#include "doctest.h"
#include "net/telnetstream.h"

using axis::net::TelnetStream;

namespace {
// a pretend socket: bytes queued from the caller, bytes the caller received, a clock we move
struct FakeLine : TelnetStream {
    std::string fromCaller, toCaller;
    bool        open = true, closed = false;
    int         acceptPerWrite = 1 << 30;  // bytes the socket takes per write (0 = stalled)
    uint32_t    now = 0;

    int rawRead(uint8_t* buf, size_t n) override {
        size_t k = fromCaller.size() < n ? fromCaller.size() : n;
        fromCaller.copy((char*)buf, k);
        fromCaller.erase(0, k);
        return (int)k;
    }
    int rawWrite(const uint8_t* p, size_t n) override {
        if (!open) return -1;
        size_t k = n < (size_t)acceptPerWrite ? n : (size_t)acceptPerWrite;
        toCaller.append((const char*)p, k);
        return (int)k;
    }
    bool     rawConnected() override { return open; }
    void     rawClose() override { closed = true, open = false; }
    uint32_t nowMs() override { return now; }
    void     idle() override { now += 100; }
};
}  // namespace

TEST_CASE("telnet stream sends the negotiation on start") {
    FakeLine l;
    l.start();
    CHECK(l.toCaller == axis::net::TelnetFilter().initialNegotiation());
}

TEST_CASE("telnet stream reads clean keys and answers options") {
    FakeLine l;
    l.start();
    l.toCaller.clear();
    l.fromCaller = std::string("\xFF\xFD\x18", 3) + "Hi";  // DO TERMINAL-TYPE, then keys
    CHECK(l.available() == 2);
    CHECK(l.peek() == 'H');
    CHECK(l.read() == 'H');
    CHECK(l.read() == 'i');
    CHECK(l.read() == -1);
    CHECK(l.toCaller == std::string("\xFF\xFC\x18", 3));  // WONT TERMINAL-TYPE
}

TEST_CASE("telnet stream doubles 0xFF on the way out") {
    FakeLine l;
    uint8_t  b[] = {'A', 0xFF, 'B'};
    l.write(b, 3);
    l.flush();
    CHECK(l.toCaller == std::string("A\xFF\xFF" "B", 4));
}

TEST_CASE("telnet stream finishes a write the socket takes in pieces") {
    FakeLine l;
    l.acceptPerWrite = 2;
    l.write((const uint8_t*)"hello", 5);
    l.flush();
    CHECK(l.toCaller == "hello");
    CHECK(l.carrier());
}

TEST_CASE("telnet stream sends an 8 KB burst through a 17-byte socket window") {
    FakeLine l;
    l.acceptPerWrite = 17;
    std::string body(8192, '\x7e');
    body[100] = '\xff';
    body[4000] = '\x0d';
    l.write((const uint8_t*)body.data(), body.size());
    l.flush();
    CHECK(l.carrier());
    CHECK_FALSE(l.closed);
    // 0xFF is doubled for telnet; CR stays one byte (ZMODEM escapes CR before it gets here)
    CHECK(l.toCaller.size() == body.size() + 1);
    CHECK(l.toCaller.find('\xff') != std::string::npos);
}

TEST_CASE("telnet stream drops carrier after 10 s with no progress") {
    FakeLine l;
    l.acceptPerWrite = 0;
    l.write((const uint8_t*)"stuck", 5);
    l.flush();
    CHECK_FALSE(l.carrier());
    CHECK(l.closed);
    CHECK(l.now >= 10000);
    CHECK(l.now < 11000);
}

TEST_CASE("telnet stream reports a hang-up") {
    FakeLine l;
    CHECK(l.carrier());
    l.open = false;
    CHECK_FALSE(l.carrier());
}

TEST_CASE("telnet stream hands the BBS all waiting keys at once") {
    FakeLine l;
    l.fromCaller = "abc\r\n";
    CHECK(l.takeInput() == "abc\r");
    CHECK(l.takeInput().empty());
}

namespace {
struct CountingLine : FakeLine {
    int writes = 0;
    int rawWrite(const uint8_t* p, size_t n) override {
        writes++;
        return FakeLine::rawWrite(p, n);
    }
};
}  // namespace

TEST_CASE("telnet stream batches a door's one-byte writes until flush") {
    CountingLine l;
    for (char c : std::string("Legend of the Red Dragon")) l.write((const uint8_t*)&c, 1);
    CHECK(l.writes == 0);
    l.flush();
    CHECK(l.writes == 1);
    CHECK(l.toCaller == "Legend of the Red Dragon");
}

TEST_CASE("telnet stream does not send on a key poll") {
    CountingLine l;
    l.write((const uint8_t*)"Your choice? ", 13);
    CHECK(l.available() == 0);
    CHECK(l.peek() == -1);
    CHECK(l.read() == -1);
    CHECK(l.writes == 0);
    CHECK(l.toCaller.empty());
    l.flush();
    CHECK(l.writes == 1);
    CHECK(l.toCaller == "Your choice? ");
}

TEST_CASE("telnet stream flushes a full buffer by itself") {
    CountingLine l;
    std::string  big(3000, 'x');
    for (char c : big) l.write((const uint8_t*)&c, 1);
    CHECK(l.writes >= 2);
    l.flush();
    CHECK(l.toCaller == big);
}

TEST_CASE("telnet stream sends one big write in small pieces, so it never needs a big buffer") {
    struct Biggest : FakeLine {
        size_t most = 0;
        int    rawWrite(const uint8_t* p, size_t n) override {
            if (n > most) most = n;
            return FakeLine::rawWrite(p, n);
        }
    } l;
    std::string screen(20000, 'A');  // a whole login screen in one call
    l.write((const uint8_t*)screen.data(), screen.size());
    l.flush();
    CHECK(l.toCaller == screen);
    CHECK(l.most <= 2048);
}

// Hold mode: a slow caller (a LoRa radio call) must never stop the board's loop.
TEST_CASE("telnet stream in hold mode never waits for a full socket and keeps every byte") {
    CountingLine l;
    l.setHold(true);
    l.acceptPerWrite = 0;  // the caller's radio is busy
    std::string screen(5000, 'S');
    screen[10] = '\xff';
    l.write((const uint8_t*)screen.data(), screen.size());
    l.flush();
    CHECK(l.now == 0);  // no waiting (FakeLine::idle moves the clock)
    CHECK(l.carrier());
    CHECK(l.toCaller.empty());
    CHECK(l.backlog() == screen.size() + 1);  // the 0xFF doubled
    l.acceptPerWrite = 700;
    while (l.backlog()) CHECK(l.drain());
    CHECK(l.now == 0);
    std::string want = screen;
    want.insert(10, 1, '\xff');
    CHECK(l.toCaller == want);
}

TEST_CASE("telnet stream in hold mode hangs up after 10 s with no progress, counted across drains") {
    FakeLine l;
    l.setHold(true);
    l.acceptPerWrite = 0;
    l.write((const uint8_t*)"stuck", 5);
    l.flush();
    l.now = 5000;
    CHECK(l.drain());
    l.now = 9999;
    CHECK(l.drain());
    l.now = 10000;
    CHECK_FALSE(l.drain());
    CHECK_FALSE(l.carrier());
    CHECK(l.closed);
}

TEST_CASE("telnet stream in hold mode starts the 10 s again whenever the caller takes something") {
    struct Trickle : FakeLine {
        size_t budget = 0;  // bytes the socket takes before it is full again
        int    rawWrite(const uint8_t* p, size_t n) override {
            size_t k = n < budget ? n : budget;
            budget -= k;
            toCaller.append((const char*)p, k);
            return (int)k;
        }
    } l;
    l.setHold(true);
    std::string screen(3000, 'x');
    l.write((const uint8_t*)screen.data(), screen.size());
    l.flush();
    for (int i = 0; i < 5; i++) {  // a byte every 8 s: slow, but not gone
        l.now += 8000;
        l.budget = 1;
        CHECK(l.drain());
    }
    CHECK(l.carrier());
    CHECK(l.toCaller == "xxxxx");
}

TEST_CASE("telnet stream in hold mode waits a burst bigger than the hold down to the hold") {
    FakeLine l;
    l.setHold(true);
    l.acceptPerWrite = 0;
    std::string a(TelnetStream::kHoldMax, 'a');
    l.write((const uint8_t*)a.data(), a.size());
    l.flush();
    CHECK(l.backlog() == TelnetStream::kHoldMax);
    CHECK(l.now == 0);
    l.acceptPerWrite = 100;
    l.write((const uint8_t*)"0123456789", 10);
    l.flush();
    CHECK(l.carrier());
    CHECK(l.backlog() <= TelnetStream::kHoldMax);
    CHECK(l.backlog() + l.toCaller.size() == a.size() + 10);
    while (l.backlog()) l.drain();
    CHECK(l.toCaller == a + "0123456789");
}

TEST_CASE("telnet stream in hold mode does not try the socket on every byte while it holds a screen") {
    CountingLine l;
    l.setHold(true);
    l.acceptPerWrite = 0;
    std::string screen(2000, 'z');
    l.write((const uint8_t*)screen.data(), screen.size());
    l.flush();
    int before = l.writes;
    for (char c : std::string("one key at a time")) l.write((const uint8_t*)&c, 1);
    CHECK(l.writes == before);  // batched as before hold mode: a door's bytes wait for the flush
    l.acceptPerWrite = 1 << 30;
    l.flush();
    CHECK(l.backlog() == 0);
    CHECK(l.toCaller == screen + "one key at a time");
}

TEST_CASE("telnet stream in hold mode drops what it held when the caller hangs up") {
    FakeLine l;
    l.setHold(true);
    l.acceptPerWrite = 0;
    l.write((const uint8_t*)"goodbye", 7);
    l.flush();
    l.open = false;
    l.acceptPerWrite = 1 << 30;
    CHECK_FALSE(l.drain());
    CHECK(l.backlog() == 0);
    CHECK(l.toCaller.empty());
}

TEST_CASE("telnet stream with hold off still waits until everything is out") {
    FakeLine l;
    l.acceptPerWrite = 3;
    l.write((const uint8_t*)"transfer block", 14);
    l.flush();
    CHECK(l.backlog() == 0);
    CHECK(l.toCaller == "transfer block");
}
