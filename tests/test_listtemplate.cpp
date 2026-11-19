// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/listtemplate.h"

#include "doctest.h"

using namespace axis::bbs;

namespace {
ArtValue fields() {
    return [](const std::string& k) -> std::string {
        if (k == "FN") return "verylongname";
        if (k == "SZ") return "123";
        return "";
    };
}
}  // namespace

TEST_CASE("a field token clips and pads to its width") {
    ListTemplate t;
    REQUIRE(parseListTemplate("row=|15@FN8@|07 @SZ5@|07\n", t));
    AcsUser who;
    std::string plain = renderRow(t, who, fields());
    // strip the colour escapes to see the text
    std::string vis;
    for (size_t i = 0; i < plain.size(); i++) {
        if (plain[i] == '\x1b') {
            while (i < plain.size() && plain[i] != 'm') i++;
        } else vis += plain[i];
    }
    CHECK(vis == "verylong 123  ");
    ListTemplate r;
    REQUIRE(parseListTemplate("row=[@SZ>5@]\n", r));
    CHECK(renderRow(r, who, fields()) == "[  123]");
    CHECK(renderRow(r, who, [](const std::string&) { return std::string("1234567"); }) == "[1234567]");  // (a right-aligned value widens, never cut)
}

TEST_CASE("a row is clipped by visible width, not by escape bytes, and never breaks the line") {
    ListTemplate t;
    REQUIRE(parseListTemplate("geom=3,6,5,14\nrow=|15@FN20@\n", t));
    AcsUser     who;
    std::string r = renderRow(t, who, [](const std::string&) { return std::string("abcdefgh\r\nij"); });
    std::string vis;
    for (size_t i = 0; i < r.size(); i++) {
        if (r[i] == '\x1b') {
            while (i < r.size() && r[i] != 'm') i++;
        } else vis += r[i];
    }
    CHECK(vis == "abcde");
    CHECK(r.find('\n') == std::string::npos);
    CHECK(r.find('\r') == std::string::npos);
}

TEST_CASE("a malformed template is rejected") {
    ListTemplate t;
    CHECK_FALSE(parseListTemplate("geom=3,6,74,14\ncaption=Name\n", t));
    CHECK_FALSE(parseListTemplate("geom=3,6\nrow=@FN8@\n", t));
}

TEST_CASE("geometry is clamped to the screen") {
    ListTemplate t;
    REQUIRE(parseListTemplate("geom=70,6,40,14\nrow=x\n", t));
    CHECK(t.x + t.width <= 80);
    CHECK(t.x == 70);
    CHECK(t.width == 10);
    REQUIRE(parseListTemplate("geom=3,20,74,14\nrow=x\n", t));
    CHECK(t.y + t.rows <= 22);
    CHECK(t.rows == 2);
    REQUIRE(parseListTemplate("geom=0,1,0,0\nrow=x\n", t));
    CHECK(t.x >= 1);
    CHECK(t.y >= 4);
    CHECK(t.width >= 1);
    CHECK(t.rows >= 1);
}

TEST_CASE("a round trip keeps the fields") {
    const char* f = "; the file list\r\ngeom=4,7,60,10\r\ncolours=112,14\r\nhead=FILES\r\ncaption=|07Name     Size\r\nrule=0\r\n"
                    "row=|15@FN8@ @SZ>5@\r\nstatus=|08Page |15@PG@\r\nkeys=|15[N]ext\r\nfuture=ignored\r\n";
    ListTemplate t;
    REQUIRE(parseListTemplate(f, t));
    CHECK(t.x == 4);
    CHECK(t.y == 7);
    CHECK(t.width == 60);
    CHECK(t.rows == 10);
    CHECK(t.hi == 112);
    CHECK(t.reg == 14);
    CHECK(t.head == "FILES");
    CHECK(t.caption == "|07Name     Size");
    CHECK_FALSE(t.rule);
    CHECK(t.row == "|15@FN8@ @SZ>5@");
    CHECK(t.status == "|08Page |15@PG@");
    CHECK(t.keys == "|15[N]ext");
}

namespace {
std::string stripEsc(const std::string& r) {
    std::string vis;
    for (size_t i = 0; i < r.size(); i++) {
        if (r[i] == '\x1b') {
            while (i < r.size() && r[i] != 'm') i++;
        } else vis += r[i];
    }
    return vis;
}
}  // namespace

TEST_CASE("control bytes in a field value are neutralised, sized or not") {
    AcsUser  who;
    ArtValue evil = [](const std::string&) { return std::string("a\x1b[2Jb\tc\x07"); };
    ListTemplate sized, unsized;
    REQUIRE(parseListTemplate("row=@FN8@\n", sized));
    std::string a = renderRow(sized, who, evil);
    CHECK(a.find('\x1b') == std::string::npos);
    CHECK(a.find('\t') == std::string::npos);
    CHECK(a.find('\x07') == std::string::npos);
    CHECK(a == "a?[2Jb?c");
    REQUIRE(parseListTemplate("row=[|FN]\n", unsized));
    std::string b = renderRow(unsized, who, evil);
    CHECK(b.find('\x1b') == std::string::npos);
    CHECK(b.find('\t') == std::string::npos);
    CHECK(b.find('\x07') == std::string::npos);
    CHECK(b == "[a?[2Jb?c?]");
}

TEST_CASE("a colour reset after the clip point is kept whole") {
    AcsUser      who;
    ListTemplate t;
    REQUIRE(parseListTemplate("geom=3,6,5,14\nrow=@FN20@|07\n", t));
    std::string r = renderRow(t, who, [](const std::string&) { return std::string("abcdefgh"); });
    CHECK(stripEsc(r) == "abcde");
    CHECK(r.back() == 'm');
    CHECK(r.rfind('\x1b') > r.find('e'));
}

TEST_CASE("a zero width field is empty and an unclosed token stays literal") {
    AcsUser      who;
    ListTemplate t;
    REQUIRE(parseListTemplate("row=[@FN0@]\n", t));
    CHECK(renderRow(t, who, fields()) == "[]");
    REQUIRE(parseListTemplate("row=x@FN8\n", t));
    CHECK(renderRow(t, who, fields()) == "x@FN8");
}

TEST_CASE("a right-aligned number wider than its field keeps every digit") {
    ListTemplate t;
    REQUIRE(parseListTemplate("geom=3,6,20,14\nrow=#@NM>4@|@SU4@|\n", t));
    AcsUser who;
    auto    f = [](const std::string& k) { return std::string(k == "NM" ? "12345" : "abcdefg"); };
    CHECK(renderRow(t, who, f) == "#12345|abcd|");
    // a hidden {{ACS}} group blanks to the width the field really has
    ListTemplate h;
    REQUIRE(parseListTemplate("geom=3,6,20,14\nrow={{S255}}@NM>4@{{/}}!\n", h));
    CHECK(renderRow(h, who, f).find("12345") == std::string::npos);
    CHECK(renderRow(h, who, f) == "     !");
    // the visible clip still takes the whole line
    ListTemplate c;
    REQUIRE(parseListTemplate("geom=3,6,3,14\nrow=@NM>4@\n", c));
    CHECK(renderRow(c, who, f) == "123");
}

TEST_CASE("an empty row is a malformed template") {
    ListTemplate t;
    CHECK_FALSE(parseListTemplate("geom=3,6,74,14\nrow=\n", t));
    CHECK_FALSE(parseListTemplate("row=\r\n", t));
}
