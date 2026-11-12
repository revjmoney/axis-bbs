// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net records: encoding, decoding, field and frame caps, signing and checking.
#include "net_testutil.h"

using namespace nettest;

namespace {
const char kSep = '\x1F';

// The wire line of a post as encode would build it, but without encode's checks, so a test can
// hand decode a line that encode would refuse.
std::string rawLine(const NetPost& p, const std::string& sig) { return signedPart(p) + kSep + p.path + kSep + sig; }

std::string sig128() { return std::string(128, 'a'); }

std::vector<std::string> fields(const std::string& line) {
    std::vector<std::string> f;
    size_t s = 0;
    for (size_t i = 0; i <= line.size(); i++)
        if (i == line.size() || line[i] == kSep) {
            f.push_back(line.substr(s, i - s));
            s = i + 1;
        }
    return f;
}

std::string join(const std::vector<std::string>& f) {
    std::string o;
    for (size_t i = 0; i < f.size(); i++) o += (i ? std::string(1, kSep) : std::string()) + f[i];
    return o;
}
}  // namespace

TEST_CASE("post round trip") {
    KeyPair k = keyOf(0x11);
    NetPost p = makePost(k, 7, kNow, "sn_support", "Subject here", "line one\nback\\slash\r\nend");
    p.path = "0123456789abcdef";
    std::string line = encode(p);
    REQUIRE(!line.empty());
    CHECK(line.find('\n') == std::string::npos);
    CHECK(line.find('\r') == std::string::npos);
    NetPost q;
    REQUIRE(decode(line, q));
    CHECK(q.id == p.id);
    CHECK(q.area == p.area);
    CHECK(q.board == p.board);
    CHECK(q.from == p.from);
    CHECK(q.to == p.to);
    CHECK(q.subject == p.subject);
    CHECK(q.body == p.body);
    CHECK(q.date == p.date);
    CHECK(q.path == p.path);
    CHECK(q.sig == p.sig);
    CHECK(verifyPost(q, pubOf(k)));
}

TEST_CASE("card and notice round trip") {
    KeyPair k = keyOf(0x12);
    NetCard c = makeCard(k, "Name", 5);
    NetCard c2;
    REQUIRE(decode(encode(c), c2));
    CHECK(c2.board == c.board);
    CHECK(c2.pub == c.pub);
    CHECK(c2.name == "Name");
    CHECK(c2.port == 23);
    CHECK(c2.seq == 5);
    CHECK(verifyCard(c2));
    CHECK(verifyCard(c2, c.pub));
    CHECK_FALSE(verifyCard(c2, pubOf(keyOf(0x13))));

    NetNotice n = makeNotice(k, 1, "ban", "0123456789abcdef", 9);
    NetNotice n2;
    REQUIRE(decode(encode(n), n2));
    CHECK(n2.kind == "ban");
    CHECK(n2.target == "0123456789abcdef");
    CHECK(n2.seq == 9);
    CHECK(n2.key == 1);
    CHECK(n2.sig == n.sig);
}

TEST_CASE("post rejects") {
    KeyPair k = keyOf(0x11);
    NetPost good = makePost(k, 1, kNow);
    REQUIRE(decode(encode(good), good));

    NetPost p = good;
    p.body = std::string(9 * 1024, 'x');  // a 9 KB body
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    CHECK_FALSE(signPost(p, k));

    p = good;
    p.subject = std::string(73, 's');  // a 73 byte subject
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    CHECK(encode(p).empty());

    p = good;
    p.area = "Bad-Area";
    CHECK_FALSE(decode(rawLine(p, sig128()), p));

    CHECK_FALSE(decode(rawLine(good, std::string(127, 'a')), p));  // a 127 hex character sig

    std::vector<std::string> f = fields(encode(good));
    f.push_back("extra");  // a wrong field count
    CHECK_FALSE(decode(join(f), p));
    f.pop_back();
    f.pop_back();
    CHECK_FALSE(decode(join(f), p));

    p = good;
    p.from = std::string(37, 'f');  // from caps at 36
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    p = good;
    p.path = std::string(513, 'a');  // path caps at 512
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    p = good;
    p.path = "ABCDEF0123456789";  // hex must be lowercase
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    p = good;
    p.id = good.board + "-x";
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
    p = good;
    p.id = boardOf(keyOf(0x99)) + "-1";  // id's board part must be the board field
    CHECK_FALSE(decode(rawLine(p, sig128()), p));
}

TEST_CASE("hex must be lowercase and numbers have one spelling") {
    KeyPair k = keyOf(0x21);
    NetPost good = makePost(k, 3, kNow);
    std::string upper = good.sig;
    for (char& c : upper)
        if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    NetPost p;
    CHECK_FALSE(decode(rawLine(good, upper), p));

    std::vector<std::string> f = fields(encode(good));
    REQUIRE(f.size() == 11);
    f[8] = "0" + f[8];  // date with a leading zero
    CHECK_FALSE(decode(join(f), p));
    f = fields(encode(good));
    f[1] = good.board + "-03";  // seq with a leading zero
    CHECK_FALSE(decode(join(f), p));

    NetCard c = makeCard(k, "C");
    std::vector<std::string> cf = fields(encode(c));
    REQUIRE(cf.size() == 12);
    cf[9] = "023";  // port with a leading zero
    NetCard c2;
    CHECK_FALSE(decode(join(cf), c2));
    cf = fields(encode(c));
    cf[2][0] = 'A';
    CHECK_FALSE(decode(join(cf), c2));

    NetNotice n = makeNotice(k, 0, "hold", good.id, 4);
    std::vector<std::string> nf = fields(encode(n));
    REQUIRE(nf.size() == 7);
    nf[4] = "04";
    NetNotice n2;
    CHECK_FALSE(decode(join(nf), n2));
    nf = fields(encode(n));
    nf[5] = "2";  // only keys 0 and 1
    CHECK_FALSE(decode(join(nf), n2));
    nf = fields(encode(n));
    nf[1] = "nuke";
    CHECK_FALSE(decode(join(nf), n2));
}

TEST_CASE("records cap every field and the whole frame at kNetMaxFrame") {
    KeyPair k = keyOf(0x22);
    // A body at the 8 KB cap that escaping doubles: the frame would pass 9 KB, so it is refused.
    NetPost p = makePost(k, 1, kNow);
    p.body = std::string(kNetMaxBody, '\n');
    p.sig.clear();
    CHECK(encode(p).empty());
    CHECK_FALSE(signPost(p, k));
    CHECK(p.sig.empty());

    // A full 8 KB plain body fits.
    p.body = std::string(kNetMaxBody, 'b');
    CHECK(signPost(p, k));
    std::string line = encode(p);
    CHECK(!line.empty());
    CHECK(line.size() <= kNetMaxFrame);

    // decode refuses any line over the frame cap, whatever it holds.
    NetPost q;
    CHECK_FALSE(decode(line + std::string(kNetMaxFrame, 'x'), q));

    NetCard c = makeCard(k, "ok");
    c.name = std::string(41, 'n');
    CHECK_FALSE(signCard(c, k));
    c = makeCard(k, "ok");
    c.host = std::string(65, 'h');
    CHECK_FALSE(signCard(c, k));
    c = makeCard(k, "ok");
    c.links = std::string(17, 'l');
    CHECK_FALSE(signCard(c, k));
    c = makeCard(k, "ok");
    c.port = 70000;
    CHECK_FALSE(signCard(c, k));
    c = makeCard(k, "ok");
    c.pub = pubOf(keyOf(0x23));  // a card must carry the signing key's pub
    CHECK_FALSE(signCard(c, k));

    NetNotice n;
    n.kind = "hold";
    n.target = std::string(41, 't');
    CHECK_FALSE(signNotice(n, k));
    n.target = "abc-1";
    n.value = std::string(65, 'v');
    CHECK_FALSE(signNotice(n, k));
    n.value.clear();
    n.kind = "explode";
    CHECK_FALSE(signNotice(n, k));
    n.kind = "hold";
    CHECK(signNotice(n, k));
}

TEST_CASE("a post is signed only by its own board's key") {
    NetPost p = makePost(keyOf(0x31), 1, kNow);
    p.sig.clear();
    CHECK_FALSE(signPost(p, keyOf(0x32)));
}

TEST_CASE("tampered post fails verify") {
    KeyPair k = keyOf(0x33);
    NetPost p = makePost(k, 2, kNow);
    CHECK(verifyPost(p, pubOf(k)));
    NetPost t = p;
    t.subject = "Changed";
    CHECK_FALSE(verifyPost(t, pubOf(k)));
    t = p;
    t.body += "!";
    CHECK_FALSE(verifyPost(t, pubOf(k)));
    t = p;
    t.date += 1;
    CHECK_FALSE(verifyPost(t, pubOf(k)));
    t = p;
    t.path = "0123456789abcdef,fedcba9876543210";  // the path is not signed
    CHECK(verifyPost(t, pubOf(k)));
    CHECK_FALSE(verifyPost(p, pubOf(keyOf(0x34))));
}
