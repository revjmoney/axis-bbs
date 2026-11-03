// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <deque>
#include <string>
#include <vector>

#include "doctest.h"
#include "door/bytestream.h"
#include "net/crc.h"
#include "net/xmodem.h"
#include "net/zmodem.h"

using namespace axis::net;
using axis::door::ByteStream;

namespace {
// one end of a simulated line; `corruptAt` flips one byte going out (to test error recovery)
struct End : ByteStream {
    std::deque<uint8_t>* in;
    std::deque<uint8_t>* out;
    long                 sent = 0, corruptAt = -1;
    bool                 up = true;
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
    bool carrier() override { return up; }
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

std::vector<uint8_t> sample(size_t n) {  // every byte value, controls and 0xFF included
    std::vector<uint8_t> v(n);
    uint32_t             x = 12345;
    for (size_t i = 0; i < n; i++) {
        x = x * 1103515245 + 12345;
        v[i] = (uint8_t)(i % 7 == 0 ? (i / 7) & 0xFF : x >> 16);
    }
    return v;
}

Transfer::ReadAt reader(const std::vector<uint8_t>& f) {
    return [&f](uint32_t off, uint8_t* buf, size_t n) {
        if (off + n > f.size()) return false;
        std::copy(f.begin() + off, f.begin() + off + n, buf);
        return true;
    };
}

void run(Transfer& x, Transfer& y, int steps = 20000) {  // both sides, 10 ms of pretend time a step
    uint32_t now = 0;
    bool     a = true, b = true;
    for (int i = 0; i < steps && (a || b); i++, now += 10) {
        if (a) a = x.step(now);
        if (b) b = y.step(now);
    }
}
}  // namespace

TEST_CASE("crc values match the published check values") {
    const uint8_t s[] = "123456789";
    CHECK(crc16Xmodem(0, s, 9) == 0x31C3);
    CHECK(crc32(s, 9) == 0xCBF43926);
}

TEST_CASE("zmodem sends a file our receiver gets byte for byte") {
    Line                 l;
    auto                 file = sample(70000);
    std::vector<uint8_t> got;
    std::string          name;
    bool                 ended = false;
    ZmodemSender         s(l.a, reader(file), (uint32_t)file.size(), "TEST.BIN");
    ZmodemReceiver       r(
        l.b, [&](const std::string& n, uint32_t size) { name = n; return size == 70000; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    run(s, r);
    CHECK(s.result() == Transfer::Result::Ok);
    CHECK(r.result() == Transfer::Result::Ok);
    CHECK(name == "TEST.BIN");
    CHECK(ended);
    CHECK(got == file);
}

TEST_CASE("zmodem output never carries a raw control character or 0xFF") {
    Line                 l;
    auto                 file = sample(5000);
    ZmodemSender         s(l.a, reader(file), (uint32_t)file.size(), "X.BIN");
    std::vector<uint8_t> wire;
    ZmodemReceiver       r(
        l.b, [](const std::string&, uint32_t) { return true; }, [](const uint8_t*, size_t) { return true; }, [](bool) {});
    uint32_t now = 0;
    for (int i = 0; i < 2000; i++, now += 10) {
        s.step(now);
        for (uint8_t c : l.ab) wire.push_back(c);
        r.step(now);
    }
    int bad = 0;
    for (size_t i = 0; i < wire.size(); i++) {
        uint8_t c = wire[i];
        bool    allowed = c == 0x18 || c == 0x11 || c == '\r' || c == 0x8A;  // ZDLE, XON, header line ends
        if ((c < 0x20 || c == 0x7F || c == 0xFF || (c >= 0x80 && c < 0xA0)) && !allowed) bad++;
    }
    CHECK(bad == 0);
}

TEST_CASE("zmodem recovers from a corrupted byte by repositioning") {
    Line l;
    l.a.corruptAt = 30000;  // somewhere in the data
    auto                 file = sample(64000);
    std::vector<uint8_t> got;
    ZmodemSender         s(l.a, reader(file), (uint32_t)file.size(), "C.BIN");
    ZmodemReceiver       r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [](bool) {});
    run(s, r);
    CHECK(r.result() == Transfer::Result::Ok);
    CHECK(got == file);
}

TEST_CASE("zmodem receiver can skip a file it doesn't want") {
    Line           l;
    auto           file = sample(1000);
    ZmodemSender   s(l.a, reader(file), (uint32_t)file.size(), "NO.BIN");
    ZmodemReceiver r(
        l.b, [](const std::string&, uint32_t) { return false; }, [](const uint8_t*, size_t) { return true; }, [](bool) {});
    run(s, r, 3000);
    CHECK(s.result() == Transfer::Result::Failed);
}

TEST_CASE("zmodem sends an empty file") {
    Line                 l;
    std::vector<uint8_t> file, got;
    bool                 ended = false;
    ZmodemSender         s(l.a, reader(file), 0, "EMPTY.TXT");
    ZmodemReceiver       r(
        l.b, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; }, [&](bool ok) { ended = ok; });
    run(s, r);
    INFO("sender: " << s.error() << " / receiver: " << r.error());
    CHECK(s.result() == Transfer::Result::Ok);
    CHECK(ended);
    CHECK(got.empty());
}

TEST_CASE("five CANs cancel a zmodem transfer") {
    Line           l;
    auto           file = sample(200000);
    ZmodemSender   s(l.a, reader(file), (uint32_t)file.size(), "BIG.BIN");
    uint32_t       now = 0;
    for (int i = 0; i < 5; i++, now += 10) s.step(now);
    uint8_t can[] = {0x18, 0x18, 0x18, 0x18, 0x18, 0x18};
    l.b.write(can, sizeof can);
    for (int i = 0; i < 10; i++, now += 10) s.step(now);
    CHECK(s.result() == Transfer::Result::Cancelled);
}

TEST_CASE("xmodem-1k sends a file our receiver gets, padded with 1Ah") {
    Line                 l;
    auto                 file = sample(5000);
    std::vector<uint8_t> got;
    XmodemSender         s(l.a, reader(file), (uint32_t)file.size(), true);
    XmodemReceiver       r(l.b, [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; });
    run(s, r);
    CHECK(s.result() == Transfer::Result::Ok);
    CHECK(r.result() == Transfer::Result::Ok);
    REQUIRE(got.size() >= file.size());
    CHECK(std::vector<uint8_t>(got.begin(), got.begin() + (long)file.size()) == file);
    for (size_t i = file.size(); i < got.size(); i++) CHECK(got[i] == 0x1A);
}

TEST_CASE("xmodem 128-byte blocks recover from a corrupted block") {
    Line l;
    l.a.corruptAt = 700;
    auto                 file = sample(3000);
    std::vector<uint8_t> got;
    XmodemSender         s(l.a, reader(file), (uint32_t)file.size(), false);
    XmodemReceiver       r(l.b, [&](const uint8_t* p, size_t n) { got.insert(got.end(), p, p + n); return true; });
    run(s, r);
    CHECK(r.result() == Transfer::Result::Ok);
    REQUIRE(got.size() >= file.size());
    CHECK(std::vector<uint8_t>(got.begin(), got.begin() + (long)file.size()) == file);
}

TEST_CASE("xmodem falls back to checksum when the sender ignores C") {
    Line           l;
    XmodemReceiver r(l.b, [](const uint8_t*, size_t) { return true; });
    uint32_t       now = 0;
    for (int i = 0; i < 2000; i++, now += 10) r.step(now);  // 20 s with no sender
    std::string seen(l.ba.begin(), l.ba.end());
    CHECK(seen.find('C') != std::string::npos);
    CHECK(seen.find('\x15') != std::string::npos);  // NAK: checksum mode
}
