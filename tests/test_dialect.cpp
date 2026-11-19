// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Art dialects (axis, vision3, renegade), a look's THEME.CFG, and the local terminal's baseline (CHA, iCE colour, ESC[1J).
// Expected bytes are written out by hand from the code table (.superpowers/sdd/2026-11-19-axis-dialect-codes/code-table.md,
// docs/MENU-ART.md "Dialects"), never computed by calling the code under test.
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "app/host.h"
#include "app/themeprovider.h"
#include "bbs/acs.h"
#include "bbs/ansiterm.h"
#include "bbs/artstyle.h"
#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/pipecodes.h"
#include "bbs/theme.h"
#include "bbs/users.h"

using namespace axis;
using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {

const AcsUser kWho{10, ""};

std::string expand(const std::string& in, const ArtStyle& style = ArtStyle{}) {
    auto v = [](const std::string& k) { return k == "%UN" ? std::string("Rev") : k == "CS" ? std::string("[cs]") : std::string(); };
    return expandArt(in, kWho, v, style);
}

ArtStyle styleOf(Dialect d) {
    ArtStyle s;
    s.dialect = d;
    return s;
}

bool sameStyle(const ArtStyle& a, const ArtStyle& b) {
    if (a.dialect != b.dialect) return false;
    for (int i = 0; i < 7; i++)
        if (a.c[i] != b.c[i]) return false;
    for (int i = 0; i < 10; i++)
        if (a.r[i] != b.r[i]) return false;
    return true;
}

// A look's THEME.CFG reader for the length of a test, cleared (as the session teardown does) when it ends.
struct CfgGuard {
    std::string text;
    bool        answers = true;
    int         asked = 0, bad = 0;
    std::string lastWhy;
    CfgGuard() { install(); }
    explicit CfgGuard(const std::string& t) : text(t) { install(); }
    void install() {
        theme::setThemeCfg(
            [this](std::string& out) {
                asked++;
                if (!answers) return false;
                out = text;
                return true;
            },
            [this](const std::string& why) {
                bad++;
                lastWhy = why;
            });
    }
    ~CfgGuard() { theme::setThemeCfg({}); }
};

struct TempDir {
    fs::path p;
    TempDir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("axis_dialect_" + std::to_string((unsigned long long)::time(nullptr)) + "_" + std::to_string(n++));
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

struct TestHost : app::Host {
    std::string sd;
    bool        withSd = false;
    uint32_t    ms = 1000;
    uint32_t    nowMs() override { return ms; }
    uint32_t    unixNow() override { return 1780000000u; }
    uint32_t    random32() override { return 1; }
    void        event(const std::string&) override {}
    std::string sdRoot() override { return sd; }
    bool        hasSd() override { return withSd; }
    uint64_t    sdFreeBytes() override { return 0; }
    std::string flashRoot() override { return sd; }
};

std::string row(const AnsiTerm& t, int r) {
    std::string s;
    for (int c = 0; c < t.cols(); c++) s += (char)t.cell(r, c).ch;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

std::string readFile(const std::string& path) {
    std::string s;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char b[1024];
        for (size_t n; (n = std::fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

}  // namespace

TEST_CASE("the axis dialect is byte-identical to today") {
    // Every expected string below was worked out by hand from src/bbs/pipecodes.cpp at e99ef6d (before any dialect existed).
    // |NN is ESC[0;(1;)3Xm with X the ANSI digit of the DOS colour; |23 is a reset; |Bn is ESC[4Xm.
    const std::string in = "|07|23|16|31|B1|C1||^1|CS%UN";
    const std::string want = std::string("\x1b[0;37m") + "\x1b[0m" + "\x1b[0;30m" + "\x1b[0;1;37m" + "\x1b[44m" + "|C1" + "||" + "^1" + "[cs]" + "Rev";
    CHECK(expand(in) == want);
    CHECK(expand(in, styleOf(Dialect::Axis)) == want);

    // a second string with the control codes and the colours that wrap around
    const std::string in2 = "|CL|XY05,03|DE|CR|PP|P|B12|08|15|B7|24|20";
    const std::string want2 = std::string("\x1b[2J\x1b[H") + "\x1b[3;5H" + "\x1b[K" + "\r\n" + "\x1b[u" + "\x1b[s" + "\x1b[41m" +
                              "\x1b[0;1;30m" + "\x1b[0;1;37m" + "\x1b[47m" + "\x1b[0;1;30m" + "\x1b[0;31m";
    CHECK(expand(in2) == want2);

    // the axis table does not know the vision3 or renegade extras, even when the look's colours say otherwise
    ArtStyle odd;
    odd.c[0] = 1;
    odd.r[3] = 2;
    CHECK(expand("|C1^3|CS||", odd) == "|C1^3[cs]||");
}

TEST_CASE("the same |23 is a background in renegade and a reset in axis") {
    const ArtStyle ren = styleOf(Dialect::Renegade);
    CHECK(expand("|23") == "\x1b[0m");
    CHECK(expand("|23", styleOf(Dialect::Vision3)) == "\x1b[0m");
    CHECK(expand("|23", ren) == "\x1b[47m");
    CHECK(expand("|17", ren) == "\x1b[44m");
    CHECK(expand("|25", ren) == "\x1b[5;44m");
    CHECK(expand("|16", ren) == "\x1b[40m");
    CHECK(expand("|22", ren) == "\x1b[43m");  // DOS 6 is brown, ANSI 3
    CHECK(expand("|24", ren) == "\x1b[5;40m");
    CHECK(expand("|31", ren) == "\x1b[5;47m");
    CHECK(expand("|17", styleOf(Dialect::Axis)) == "\x1b[0;34m");  // today's bytes: a foreground, DOS 1 = blue
}

TEST_CASE("vision3 theme colours come from the look") {
    const ArtStyle v3 = styleOf(Dialect::Vision3);
    CHECK(expand("|C7", v3) == attrSgr(31));
    CHECK(expand("|C7", v3) == "\x1b[0;1;37;44m");  // white on blue, written out
    CHECK(expand("|C1", v3) == "\x1b[0;37;40m");     // the default slot 1 is 7
    CHECK(expand("|C2", v3) == "\x1b[0;1;37;40m");   // slot 2 is 15

    ArtStyle set;
    REQUIRE(parseThemeCfg("dialect=vision3\nc7=78\n", set));
    CHECK(set.dialect == Dialect::Vision3);
    CHECK(set.c[6] == 78);
    CHECK(expand("|C7", set) == attrSgr(78));
    CHECK(expand("|C7", set) == "\x1b[0;1;33;41m");  // 78 = 0x4E: bright brown on red
    CHECK(expand("||", v3) == "|");
    CHECK(expand("a||b|15c", v3) == "a|b\x1b[0;1;37mc");
}

TEST_CASE("renegade slots and clear screen") {
    const ArtStyle ren = styleOf(Dialect::Renegade);
    CHECK(expand("^3", ren) == attrSgr(ren.r[3]));
    CHECK(expand("^3", ren) == "\x1b[0;1;37;40m");  // slot 3 defaults to 15
    CHECK(expand("^0", ren) == "\x1b[0;37;40m");
    CHECK(expand("|CS", ren) == "\x1b[2J\x1b[H");
    CHECK(expand("|CS", ren) == expand("|CL", ren));
    CHECK(expand("||", ren) == "|");
    CHECK(expand("^a ^ ^", ren) == "^a ^ ^");  // not a slot

    ArtStyle set;
    REQUIRE(parseThemeCfg("dialect=renegade\nr3=78\nr9=0\n", set));
    CHECK(expand("^3", set) == attrSgr(78));
    CHECK(expand("^9", set) == attrSgr(0));
}

TEST_CASE("art stops at ctrl-z across pieces") {
    // the picture, then Ctrl-Z, then a SAUCE record that starts in the next piece
    const std::string piece1 = "pic line\r\nlast row\x1a";
    const std::string piece2 = "SAUCE00title of the work\x1a more";
    for (Dialect d : {Dialect::Axis, Dialect::Vision3, Dialect::Renegade}) {
        std::string out, carry;
        bool        ended = false;
        auto        v = [](const std::string&) { return std::string(); };
        auto        emit = [&](const char* p, size_t n) { out.append(p, n); };
        artPiece(carry, piece1.data(), piece1.size(), false, kWho, v, emit, styleOf(d), &ended);
        CHECK(ended);
        artPiece(carry, piece2.data(), piece2.size(), true, kWho, v, emit, styleOf(d), &ended);
        CHECK(out == "pic line\r\nlast row");
        CHECK(carry.empty());
    }
    // the Ctrl-Z is the last byte of a piece that ends on a line break
    {
        std::string out, carry;
        bool        ended = false;
        auto        v = [](const std::string&) { return std::string(); };
        auto        emit = [&](const char* p, size_t n) { out.append(p, n); };
        const std::string a = "one\r\n", b = "\x1a", c = "SAUCE00";
        artPiece(carry, a.data(), a.size(), false, kWho, v, emit, ArtStyle{}, &ended);
        artPiece(carry, b.data(), b.size(), false, kWho, v, emit, ArtStyle{}, &ended);
        artPiece(carry, c.data(), c.size(), true, kWho, v, emit, ArtStyle{}, &ended);
        CHECK(out == "one\r\n");
    }
    // without the flag only the rest of that piece goes (the old callers)
    {
        std::string out, carry;
        auto        v = [](const std::string&) { return std::string(); };
        auto        emit = [&](const char* p, size_t n) { out.append(p, n); };
        std::string a = "ab\x1a" "cd";
        artPiece(carry, a.data(), a.size(), true, kWho, v, emit);
        CHECK(out == "ab");
    }
}

TEST_CASE("a bad theme cfg falls back to axis") {
    ArtStyle out;
    std::string why;
    CHECK_FALSE(parseThemeCfg("dialect=mystic\n", out, &why));
    CHECK(sameStyle(out, ArtStyle{}));
    CHECK_FALSE(why.empty());
    CHECK_FALSE(parseThemeCfg("dialect=vision3\nc3=300\n", out));
    CHECK(sameStyle(out, ArtStyle{}));  // the good first line does not survive a bad second one
    CHECK_FALSE(parseThemeCfg("garbage\n", out));
    CHECK(sameStyle(out, ArtStyle{}));
    CHECK_FALSE(parseThemeCfg("r2=abc\n", out));
    CHECK_FALSE(parseThemeCfg("c1=\n", out));

    // good files, for contrast: a comment, a BOM, an unknown key, mixed case
    CHECK(parseThemeCfg("\xEF\xBB\xBF; a look\r\nDialect = Renegade\r\nname=whatever\r\nr1=12 ; comment\r\n", out));
    CHECK(out.dialect == Dialect::Renegade);
    CHECK(out.r[1] == 12);
    CHECK(parseThemeCfg("", out));
    CHECK(sameStyle(out, ArtStyle{}));

    for (const char* bad : {"dialect=mystic", "c3=300", "garbage"}) {
        CfgGuard g(bad);
        CHECK(sameStyle(theme::artStyle(), ArtStyle{}));
        CHECK(sameStyle(theme::artStyle(), ArtStyle{}));  // cached: not parsed again
        theme::forgetArtStyle();
        CHECK(sameStyle(theme::artStyle(), ArtStyle{}));
        CHECK(g.bad == 1);  // told once, however often the style is asked for
        CHECK_FALSE(g.lastWhy.empty());
    }

    CfgGuard good("dialect=vision3\nc7=78\n");
    ArtStyle s = theme::artStyle();
    CHECK(s.dialect == Dialect::Vision3);
    CHECK(s.c[6] == 78);
    CHECK(good.bad == 0);
    CHECK(good.asked == 1);  // read once, cached
    theme::artStyle();
    CHECK(good.asked == 1);
    theme::forgetArtStyle();
    theme::artStyle();
    CHECK(good.asked == 2);
}

TEST_CASE("an unanswered ask for the look's style is not cached") {
    CfgGuard g("dialect=renegade\n");
    g.answers = false;
    CHECK(sameStyle(theme::artStyle(), ArtStyle{}));  // axis for now
    CHECK(sameStyle(theme::artStyle(), ArtStyle{}));
    CHECK(g.asked == 2);  // asked again each time
    CHECK(g.bad == 0);
    g.answers = true;
    CHECK(theme::artStyle().dialect == Dialect::Renegade);
    CHECK(g.asked == 3);
    theme::artStyle();
    CHECK(g.asked == 3);  // an answer is kept
}

TEST_CASE("a follower reads the look's style from the main") {
    TestHost         host;
    BbsConfig        cfg;
    app::ThemeProvider tp(host);
    tp.config = [&]() -> const BbsConfig& { return cfg; };
    tp.follower = [] { return true; };

    std::vector<std::string> asked;
    bool                     answers = false;
    std::string              reply = "1dialect=vision3\nc7=78\n";
    tp.remote = [&](const std::string& req, std::string& r) {
        asked.push_back(req);
        if (!answers) return false;
        r = reply;
        return true;
    };

    // no answer: false, nothing in out, and the ask goes out again once the back-off (10 s) is over
    std::string text = "stale";
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(text.empty());
    host.ms += 10000;
    CHECK_FALSE(tp.themeCfgText(text));
    REQUIRE(asked.size() == 2);
    CHECK(asked[0] == std::string("T\x1f") + "THEME.CFG" + "\x1f" + "0" + "\x1f" + "4096");

    // the main answers: its THEME.CFG text comes back, and is kept (no more asks)
    answers = true;
    host.ms += 10000;
    CHECK(tp.themeCfgText(text));
    CHECK(text == "dialect=vision3\nc7=78\n");
    CHECK(asked.size() == 3);
    answers = false;
    text.clear();
    CHECK(tp.themeCfgText(text));
    CHECK(text == "dialect=vision3\nc7=78\n");
    CHECK(asked.size() == 3);

    // the settings arriving (the look may have changed) forget it
    tp.forgetRemote();
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(asked.size() == 4);

    // the main has no THEME.CFG ("0"): a real answer, empty, kept
    answers = true;
    host.ms += 10000;
    reply = "0";
    CHECK(tp.themeCfgText(text));
    CHECK(text.empty());
    CHECK(asked.size() == 5);
    CHECK(tp.themeCfgText(text));
    CHECK(asked.size() == 5);

    // a THEME.CFG over 1 KB counts as none
    tp.forgetRemote();
    reply = "1" + std::string(1100, ';');
    CHECK(tp.themeCfgText(text));
    CHECK(text.empty());

    // through the theme: artStyle() sees the main's file, and a failed ask leaves axis without caching it
    tp.forgetRemote();
    reply = "1dialect=renegade\nr3=78\n";
    answers = false;
    theme::setThemeCfg([&tp](std::string& t) { return tp.themeCfgText(t); });
    struct Clear {
        ~Clear() { theme::setThemeCfg({}); }
    } clear;
    CHECK(sameStyle(theme::artStyle(), ArtStyle{}));
    answers = true;
    host.ms += 10000;
    ArtStyle got = theme::artStyle();
    CHECK(got.dialect == Dialect::Renegade);
    CHECK(got.r[3] == 78);
}

TEST_CASE("the main reads THEME.CFG from the look's folder, 1 KB at most") {
    TempDir  dir;
    TestHost host;
    host.sd = dir.p.string();
    host.withSd = true;
    BbsConfig cfg;
    cfg.theme = "axis";
    app::ThemeProvider tp(host);
    tp.config = [&]() -> const BbsConfig& { return cfg; };

    std::string text = "x";
    CHECK(tp.themeCfgText(text));  // no file: "" and a real answer
    CHECK(text.empty());

    fs::create_directories(dir.p / "themes" / "axis");
    {
        FILE* f = std::fopen((dir.p / "themes" / "axis" / "THEME.CFG").string().c_str(), "wb");
        REQUIRE(f);
        std::fputs("dialect=vision3\n", f);
        std::fclose(f);
    }
    CHECK(tp.themeCfgText(text));
    CHECK(text == "dialect=vision3\n");

    {
        FILE* f = std::fopen((dir.p / "themes" / "axis" / "THEME.CFG").string().c_str(), "wb");
        REQUIRE(f);
        std::string big(1025, ';');
        std::fwrite(big.data(), 1, big.size(), f);
        std::fclose(f);
    }
    CHECK(tp.themeCfgText(text));
    CHECK(text.empty());  // over the cap: none
}

TEST_CASE("the local terminal: CHA, iCE and 1J") {
    {
        AnsiTerm t;
        t.feed("\x1b[10Gx");
        CHECK(t.cell(0, 9).ch == 'x');
        CHECK(t.cell(0, 8).ch == ' ');
        t.feed("\r\n\x1b[Gy\x1b[3Gz\x1b[999Gw");
        CHECK(t.cell(1, 0).ch == 'y');
        CHECK(t.cell(1, 2).ch == 'z');
        CHECK(t.cell(1, 79).ch == 'w');  // clamped to the last column
    }
    {
        AnsiTerm t;
        t.feed("\x1b[5;44mx");
        CHECK((t.cell(0, 0).attr >> 4) == 9);  // blue, with the bright bit from blink (iCE colour)
        t.feed("\x1b[25my");
        CHECK((t.cell(0, 1).attr >> 4) == 1);  // blink off: plain blue
        t.feed("\x1b[5;47mz\x1b[0mq");
        CHECK((t.cell(0, 2).attr >> 4) == 15);
        CHECK((t.cell(0, 3).attr >> 4) == 0);
        CHECK((t.cell(0, 3).attr & 15) == 7);
    }
    {
        AnsiTerm t;
        for (int r = 0; r < 6; r++) t.feed("\x1b[" + std::to_string(r + 1) + ";1Hrow" + std::to_string(r) + "-abcdefgh");
        t.feed("\x1b[3;6H\x1b[1J");  // row 3 (index 2), column 6 (index 5)
        CHECK(row(t, 0).empty());
        CHECK(row(t, 1).empty());
        CHECK(t.cell(2, 0).ch == ' ');
        CHECK(t.cell(2, 5).ch == ' ');  // the cursor's own cell goes too
        CHECK(t.cell(2, 6).ch == 'b');  // "row2-abcdefgh": index 6 is 'b'
        CHECK(row(t, 3) == "row3-abcdefgh");
        CHECK(row(t, 5) == "row5-abcdefgh");
        CHECK(t.row() == 2);  // the cursor stays where it was
        CHECK(t.col() == 5);
    }
}

TEST_CASE("the vision3 pack screens render clean") {
    const char* const files[] = {"YOURSTAT.ANS", "KONFIG.ANS", "MSGAREA.TOP.ANS", "NSCANHDR.ANS"};
    const std::string dir = std::string(AXIS_TEST_DATA) + "/packs/vision3/";
    ArtStyle          style = styleOf(Dialect::Vision3);
    int               withSauce = 0;

    // |XX: two digits, |Bn, |C1..|C7, the control names, two capitals (a placeholder), a group
    auto knownCode = [](const std::string& s, size_t i) {  // s[i] == '|'
        auto d = [&](size_t k) { return k < s.size() && s[k] >= '0' && s[k] <= '9'; };
        auto u = [&](size_t k) { return k < s.size() && s[k] >= 'A' && s[k] <= 'Z'; };
        if (d(i + 1) && d(i + 2)) return true;
        if (i + 1 < s.size() && s[i + 1] == 'B' && d(i + 2)) return true;
        if (i + 1 < s.size() && s[i + 1] == 'C' && i + 2 < s.size() && s[i + 2] >= '1' && s[i + 2] <= '7') return true;
        if (u(i + 1) && u(i + 2)) return true;
        if (i + 1 < s.size() && (s[i + 1] == '{' || s[i + 1] == '}' || s[i + 1] == '|')) return true;
        return false;
    };

    for (const char* name : files) {
        INFO(name);
        const std::string raw = readFile(dir + name);
        REQUIRE_FALSE(raw.empty());
        // the screen's height: from its SAUCE record (TInfo2, 98 bytes in) when it has one, else its line count
        size_t sauce = raw.find("SAUCE00");
        size_t height = (size_t)std::count(raw.begin(), raw.end(), '\n');
        if (sauce != std::string::npos) {
            withSauce++;
            REQUIRE(raw.size() >= sauce + 100);
            height = (uint8_t)raw[sauce + 98] | ((size_t)(uint8_t)raw[sauce + 99] << 8);
        }
        const bool tall = height > 25;  // (YOURSTAT.ANS is 34 rows: a scrolling stat sheet, not one 25-row screen)

        // the whole file, and again fed in 100-byte pieces (a code and the SAUCE record can fall across a cut)
        for (size_t step : {raw.size(), (size_t)100}) {
            std::string bytes, carry;
            bool        ended = false;
            auto        v = [](const std::string& k) { return k.size() == 2 ? std::string("ab") : std::string("ABC"); };
            auto        emit = [&](const char* p, size_t n) { bytes.append(p, n); };
            for (size_t at = 0; at < raw.size(); at += step) {
                size_t n = std::min(step, raw.size() - at);
                artPiece(carry, raw.data() + at, n, at + n >= raw.size(), kWho, v, emit, style, &ended);
            }
            CHECK(bytes.find('\x1a') == std::string::npos);
            CHECK(bytes.find("SAUCE") == std::string::npos);
            for (size_t i = 0; i < bytes.size(); i++)
                if (bytes[i] == '|') CHECK_FALSE(knownCode(bytes, i));

            AnsiTerm t(80, 30);  // 30 rows of room, so that a screen running past row 25 shows up instead of scrolling
            t.feed(bytes);
            int furthest = -1;
            for (int r = 0; r < t.rows(); r++)
                for (int c = 0; c < t.cols(); c++)
                    if (t.cell(r, c).ch != ' ' || (t.cell(r, c).attr >> 4) != 0) furthest = r;
            if (!tall) {
                CHECK(furthest < 25);
                CHECK(t.row() <= 25);
            }
            for (int r = 0; r < t.rows(); r++) {
                std::string line = row(t, r);
                CHECK(line.find("SAUCE") == std::string::npos);
                for (size_t i = 0; i < line.size(); i++)
                    if (line[i] == '|') CHECK_FALSE(knownCode(line, i));
            }
        }
    }
    CHECK(withSauce >= 1);  // the test means something: some of these files do carry a SAUCE record
}

TEST_CASE("renegade |00..|15 keep the background; axis and vision3 keep today's bytes") {
    const ArtStyle ren = styleOf(Dialect::Renegade);
    // |17 is a blue background, |15 a bright white foreground that leaves it alone (no 0; reset in front)
    CHECK(expand("|17|15Hello", ren) == std::string("\x1b[44m") + "\x1b[1;25;37m" + "Hello");
    // |23 is a light grey background, |00 a black foreground on it
    CHECK(expand("|23|00x", ren) == std::string("\x1b[47m") + "\x1b[22;25;30m" + "x");
    CHECK(expand("|07|08|09|12", ren) == std::string("\x1b[22;25;37m") + "\x1b[1;25;30m" + "\x1b[1;25;34m" + "\x1b[1;25;31m");
    // the other dialects are as they were: a reset in front of every foreground
    for (Dialect d : {Dialect::Axis, Dialect::Vision3}) {
        CHECK(expand("|17|15Hello", styleOf(d)) == std::string("\x1b[0;34m") + "\x1b[0;1;37m" + "Hello");
        CHECK(expand("|07|08", styleOf(d)) == std::string("\x1b[0;37m") + "\x1b[0;1;30m");
    }
}

TEST_CASE("a follower whose main is quiet does not ask for THEME.CFG again for 10 seconds") {
    TestHost           host;
    BbsConfig          cfg;
    app::ThemeProvider tp(host);
    tp.config = [&]() -> const BbsConfig& { return cfg; };
    tp.follower = [] { return true; };
    int         asked = 0;
    bool        answers = false;
    std::string reply = "1dialect=renegade\n";
    tp.remote = [&](const std::string&, std::string& r) {
        asked++;
        if (!answers) return false;
        r = reply;
        return true;
    };
    std::string text;
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(asked == 1);
    // however often it is wanted (per art piece, list row, prompt), a quiet main is not asked again inside the 10 s
    for (int i = 0; i < 20; i++) CHECK_FALSE(tp.themeCfgText(text));
    host.ms += 9999;
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(asked == 1);
    CHECK(text.empty());
    // artStyle() through it: axis for now, and no ask either
    theme::setThemeCfg([&tp](std::string& t) { return tp.themeCfgText(t); });
    struct Clear {
        ~Clear() { theme::setThemeCfg({}); }
    } clear;
    for (int i = 0; i < 5; i++) CHECK(theme::artStyle().dialect == Dialect::Axis);
    CHECK(asked == 1);
    // at 10 s it asks again (and a second silence backs off from there)
    host.ms += 1;
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(asked == 2);
    CHECK_FALSE(tp.themeCfgText(text));
    CHECK(asked == 2);
    host.ms += 10000;
    answers = true;
    theme::forgetArtStyle();
    CHECK(theme::artStyle().dialect == Dialect::Renegade);
    CHECK(asked == 3);
    theme::artStyle();
    CHECK(asked == 3);  // answered and kept

    // THEME.CFG is fetched with a 1 KB cap: a long file stops the pulling after its first piece
    tp.forgetRemote();
    asked = 0;
    reply = "1" + std::string(4096, ';');
    CHECK(tp.themeCfgText(text));
    CHECK(text.empty());  // over 1 KB: none
    CHECK(asked == 1);
}

namespace {
uint32_t rngFixed() { return 0x5A5A5A5Au; }
SysInfo  rigInfo() {
    SysInfo s;
    s.timeText = "2026-11-19 12:00";
    return s;
}
std::string esc(const std::string& s) {  // printable, for a failure message
    std::string o;
    for (unsigned char c : s) {
        char b[8];
        if (c == 0x1b) o += "<ESC>";
        else if (c < 0x20 || c > 0x7e) std::snprintf(b, sizeof b, "<%02x>", c), o += b;
        else o += (char)c;
    }
    return o;
}
// A caller's board whose theme art is `artFiles` (by file name), under the look's THEME.CFG. login() starts the call and logs in as Spindle Kid.
struct DialectRig {
    CfgGuard                           cfgText;
    UserStore                          users;
    std::map<std::string, std::string> artFiles;
    Node                               n;
    std::string                        all;  // everything sent to the caller since the call began
    DialectRig(const std::string& cfg, std::map<std::string, std::string> art) : cfgText(cfg), artFiles(std::move(art)), n(1, rigInfo, BbsConfig()) {
        auto read = [this](const std::string& name) {
            auto it = artFiles.find(name);
            return it == artFiles.end() ? std::string() : it->second;
        };
        theme::setArt(read);
        n.setArt(read);
        User u;
        u.handle = "Spindle Kid";
        u.salt = makeSalt(rngFixed);
        u.hash = hashPassword(u.salt, "secret99");
        u.status = User::Status::Validated;
        u.calls = 41;
        u.uploads = 2 * 1024 * 1024;
        u.downloads = 48 * 1024;
        u.lastCall = 1750000000;  // 2025-06-15
        u.location = "Myrtle Beach";
        users.add(u);
        n.setAccounts(&users, [] {}, rngFixed, [] { return 1800000000u; });
    }
    ~DialectRig() { theme::setArt({}); }
    std::string send(const std::string& s) {
        n.input((const uint8_t*)s.data(), s.size(), 1000);
        std::string out = n.takeOutput();
        all += out;
        return out;
    }
    void login() {
        n.begin(0);
        all += n.takeOutput();
        send("Spindle Kid\rsecret99\r");
    }
};
}  // namespace

TEST_CASE("a vision3 look's header screens (YOURSTAT.ANS) read |C7 and |UH instead of printing them") {
    {
        DialectRig r("dialect=vision3\n", {{"YOURSTAT.ANS", "|C7[|UH]|C1|15|LEVEL"}});
        r.login();
        std::string out = r.send("z");  // Your Stats
        INFO(esc(out));
        // |C7 is the default slot 31 (bright white on blue), |UH the handle, |C1 slot 7 (grey), |15 bright white, |LEVEL 10
        CHECK(out.find(std::string("\x1b[0;1;37;44m") + "[Spindle Kid]" + "\x1b[0;37;40m" + "\x1b[0;1;37m" + "10") != std::string::npos);
        CHECK(out.find("|C7") == std::string::npos);
        CHECK(out.find("|UH") == std::string::npos);
        CHECK(out.find("|LEVEL") == std::string::npos);
    }
    {
        // an axis look (no THEME.CFG) keeps the old fill: plain pipe colours, the v3 codes left as they are
        DialectRig r("", {{"YOURSTAT.ANS", "|C7[|UH]|15x"}});
        r.login();
        std::string out = r.send("z");
        CHECK(out.find(std::string("|C7[|UH]") + "\x1b[0;1;37m" + "x") != std::string::npos);
    }
    {
        // the same for the header art of the other screens (STATS here)
        DialectRig r("dialect=vision3\n", {{"STATS.ANS", "<|UH>"}});
        r.login();
        CHECK(r.send("i").find("<Spindle Kid>") != std::string::npos);
    }
}

TEST_CASE("BULLETIN.ANS and MAIN.ANS stop at Ctrl-Z, in every dialect") {
    for (const char* cfg : {"", "dialect=vision3\n", "dialect=renegade\n"}) {
        INFO(cfg);
        DialectRig r(cfg, {{"BULLETIN.ANS", std::string("NEWS|UH\x1a") + "SAUCE00junk"}, {"MAIN.ANS", std::string("MAINART\x1a") + "SAUCE00more"}});
        r.login();
        CHECK(r.all.find("NEWS") != std::string::npos);
        r.send(" ");  // past the bulletin
        CHECK(r.all.find("MAINART") != std::string::npos);
        CHECK(r.all.find("SAUCE") == std::string::npos);
        CHECK(r.all.find("junk") == std::string::npos);
        CHECK(r.all.find("more") == std::string::npos);
        CHECK(r.all.find('\x1a') == std::string::npos);
    }
}

TEST_CASE("menu codes: %TL is blank in vision3 and filled in renegade; UPLDS and DNLDS blank; LCALL formatted") {
    const std::string art = "T[%TL]U[|UPLDS]D[|DNLDS]L[|LCALL]E";
    {
        DialectRig r("dialect=vision3\n", {{"BULLETIN.ANS", art}});
        r.login();
        // AXIS keeps bytes, not counts: the two blank. 1750000000 is 2025-06-15.
        CHECK(r.all.find("T[]U[]D[]L[06/15/25]E") != std::string::npos);
    }
    {
        DialectRig r("dialect=renegade\n", {{"BULLETIN.ANS", art}});
        r.login();
        INFO(esc(r.all));
        CHECK(r.all.find("T[60]U[]D[]L[06/15/25]E") != std::string::npos);  // 60 minutes left (the default limit)
    }
}
