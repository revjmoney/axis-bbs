// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The list screens drawn from a look's list template (FILES.LST, MSGS.LST, USERS.LST): the built-in frame is still what a
// look without a template gets, a template restyles it, and the rules around the rows (paging, twits, lite callers) hold.
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/features.h"
#include "bbs/filebrowser.h"
#include "bbs/filelib.h"
#include "bbs/listscreen.h"
#include "bbs/listtemplate.h"
#include "bbs/msgbase.h"
#include "bbs/msgreader.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/theme.h"
#include "bbs/users.h"

using namespace axis::bbs;

namespace {
// the shipped default templates, read from the repository art folder
std::string shipped(const char* name) {
    std::string s;
    if (FILE* f = std::fopen((std::string(AXIS_TEST_DATA) + "/../../art/menus/" + name).c_str(), "rb")) {
        char b[1024];
        for (size_t n; (n = std::fread(b, 1, sizeof b, f)) > 0;) s.append(b, n);
        std::fclose(f);
    }
    return s;
}

// A look's list templates for the length of a test, cleared (like the session teardown does) when it ends.
struct Templates {
    std::map<std::string, std::string> text;
    std::vector<std::string>           bad;
    Templates() { install(); }
    Templates(const char* name, const std::string& body) {
        text[name] = body;
        install();
    }
    void install() {
        theme::setListTemplate([this](const std::string& n) { auto it = text.find(n); return it == text.end() ? std::string() : it->second; },
                               [this](const std::string& n) { bad.push_back(n); });
    }
    ~Templates() {
        theme::setListTemplate({});
        theme::setLite(false);
    }
};

// what the caller sees: escape sequences gone, each cursor move starting a new line
std::string visible(const std::string& s) {
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '[') {
            size_t j = i + 2;
            while (j < s.size() && !(s[j] >= '@' && s[j] <= '~')) j++;
            if (j < s.size() && (s[j] == 'H' || s[j] == 'f' || s[j] == 'J')) r += '\n';
            i = j;
        } else r += s[i];
    }
    return r;
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> v;
    std::string              cur;
    for (char c : visible(s) + "\n") {
        if (c == '\n' || c == '\r') {
            size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
            if (a != std::string::npos) v.push_back(cur.substr(a, b - a + 1));
            cur.clear();
        } else cur += c;
    }
    return v;
}

std::vector<std::string> linesWith(const std::string& s, const std::string& what) {
    std::vector<std::string> v;
    for (const std::string& l : lines(s))
        if (l.find(what) != std::string::npos) v.push_back(l);
    return v;
}

bool has(const std::string& s, const std::string& what) { return s.find(what) != std::string::npos; }
}  // namespace

TEST_CASE("a value shows as typed in a row and in the status line") {
    AcsUser      who;
    ListTemplate t;
    REQUIRE(parseListTemplate("row=@FN12@ |AB\nstatus=|FN  |AB\n", t));
    for (const char* v : {"50% off", "a@b", "x|07y", "a@FN@b"}) {
        ArtValue f = [v](const std::string& k) { return k == "FN" ? std::string(v) : std::string(v); };
        std::string row = renderRow(t, who, f);
        std::string st = renderLine(t.status, t.width, who, f);
        CHECK(has(row, v));
        CHECK(has(st, v));
        CHECK_FALSE(has(row, "?"));  // not a control byte, not a colour code
        CHECK_FALSE(has(st, "?"));
        CHECK(row.find('\x1b') == std::string::npos);  // x|07y never became a colour
        CHECK(st.find('\x1b') == std::string::npos);
    }
}

TEST_CASE("a sized field inside a hidden ACS group blanks to its full width") {
    ListTemplate t;
    REQUIRE(parseListTemplate("row=[{{S255}}@FN8@{{/}}]\n", t));
    ArtValue f = [](const std::string&) { return std::string("secret"); };
    CHECK(renderRow(t, AcsUser{255, ""}, f) == "[secret  ]");
    CHECK(renderRow(t, AcsUser{10, ""}, f) == "[        ]");  // hidden: the full 8 columns, so the next column stays put
}

TEST_CASE("a malformed template falls back to the built-in frame") {
    Templates tp("FILES.LST", "geom=3,6,74,14\ncaption=Name\n");  // no row=
    ListTemplate out;
    CHECK_FALSE(theme::listTemplate("FILES.LST", out));
    CHECK_FALSE(listScreenTemplate("FILES.LST", out));
    CHECK(tp.bad.size() == 1);  // told once
    CHECK_FALSE(theme::listTemplate("FILES.LST", out));
    CHECK(tp.bad.size() == 1);
    CHECK_FALSE(theme::listTemplate("MSGS.LST", out));  // a look without the file
}

#if AXIS_FILES
namespace {
struct FakeLibrary : FileLibrary {
    std::map<std::string, std::vector<FileEntry>> areas;
    std::vector<FileEntry> list(const std::string& area) override { return areas[area]; }
    bool read(const std::string&, const std::string&, std::string&, size_t) override { return false; }
};

FakeLibrary library(int bigCount = 40) {
    FakeLibrary l;
    l.areas["axis"] = {{"AXISFLSH.ZIP", 797000, "Ready to flash"}, {"README.TXT", 1200, "What it is"}};
    for (int i = 0; i < bigCount; i++)
        l.areas["text"].push_back({"LIST" + std::to_string(i) + ".TXT", 100u + 37u * i, "BBS list number " + std::to_string(i)});
    return l;
}

const std::vector<FileArea> kAreas = {{"axis", "Axis Micro BBS Firmware"}, {"text", "Text Files"}};

// open area `which` (0 or 1) and press `keys` there; the bytes the last of them drew
std::string fileScreen(FakeLibrary& lib, int which, const std::string& keys = "") {
    std::string out;
    FileBrowser b(kAreas, lib, out);
    b.begin();
    for (int i = 0; i < which; i++) {
        b.key('\x1b');
        b.key('[');
        b.key('B');
    }
    out.clear();
    b.key('\r');
    for (char c : keys) {
        out.clear();
        b.key(c);
    }
    return out;
}

std::string keyLine() {
    std::string k = theme::key("N", "ext  ") + theme::key("P", "rev  ") + theme::key("V", "iew  ") + theme::key("I", "nfo  ");
#if AXIS_TRANSFERS
    k += theme::key("D", "ownload  ") + theme::key("U", "pload  ");
#endif
    return k + theme::key("Q", "uit");
}
}  // namespace

TEST_CASE("a null template renders the same rows as the hardcoded frame") {
    Templates   none;  // no look has a FILES.LST
    FakeLibrary lib = library();
    std::string got = fileScreen(lib, 0);
    // the frame as it was before templates: the 12 / 7 / 40 column rows on the 14 row bar, the page caption, the status and keys
    std::vector<std::string> rows;
    for (const FileEntry& f : lib.areas["axis"]) {
        char b[160];
        std::snprintf(b, sizeof b, "%-12.12s %7s  %s", f.name.c_str(), fileSizeText(f.size).c_str(), fileAbout(f.desc, 40).c_str());
        rows.emplace_back(b);
    }
    ScrollList bar{3, kListRow, 74, kListRows, 111, 15};
    bar.setItems(rows, 0);
    char head[160];
    std::snprintf(head, sizeof head, "%-51s%s", "Name          Size   About", "page 1/1");
    const FileEntry& f0 = lib.areas["axis"][0];
    std::string      want = listScreenTop("Axis Micro BBS Firmware", head) + bar.draw() +
                       listScreenStatus(f0.name + "  " + fileSizeText(f0.size) + "  " + f0.desc) + listScreenKeys(keyLine() + "   Enter selects");
    // (Enter on the area list first repaints that list's status line; the list screen itself is what follows)
    REQUIRE(got.size() >= want.size());
    CHECK(got.substr(got.size() - want.size()) == want);
    CHECK(has(got, "AXISFLSH.ZIP    778K  Ready to flash"));  // the row text itself, as typed on a 90s list

    // and the shipped FILES.LST draws the same rows (its caption and status are its own to keep or change)
    std::string tplText = shipped("FILES.LST");
    REQUIRE_FALSE(tplText.empty());
    Templates   shippedLook("FILES.LST", tplText);
    std::string viaTpl = fileScreen(lib, 0);
    CHECK(linesWith(viaTpl, "AXISFLSH.ZIP") == linesWith(got, "AXISFLSH.ZIP"));
    CHECK(linesWith(viaTpl, "README.TXT") == linesWith(got, "README.TXT"));
    CHECK(has(visible(viaTpl), "page 1/1"));
}

TEST_CASE("a long file area pages the same with a template and without") {
    FakeLibrary lib = library(40);
    std::string without, with;
    {
        Templates none;
        without = fileScreen(lib, 1, "n");  // page 2 of 3
    }
    {
        std::string tplText = shipped("FILES.LST");
        REQUIRE_FALSE(tplText.empty());
        Templates look("FILES.LST", tplText);
        with = fileScreen(lib, 1, "n");
    }
    CHECK(has(visible(without), "page 2/3"));
    CHECK(has(visible(with), "page 2/3"));
    std::vector<std::string> a = linesWith(without, "LIST"), b = linesWith(with, "LIST");
    REQUIRE(a.size() >= 15);  // the 15 rows of page 2 and the status line
    CHECK(a == b);
    CHECK(has(a.front(), "LIST15.TXT"));  // page 2 starts at the 16th file
    CHECK_FALSE(has(visible(with), "LIST14.TXT"));
    CHECK_FALSE(has(visible(with), "LIST30.TXT"));
    std::string last = fileScreen(lib, 1, "nn");  // page 3 holds the last ten (and no template here)
    CHECK(has(visible(last), "page 3/3"));
}

TEST_CASE("a look's template restyles the columns") {
    Templates look("FILES.LST", "geom=3,6,74,14\ncaption=|14Size  File\nrule=1\nrow=@SZ>6@  @FN12@\nstatus=|FN is |SZ\n");
    FakeLibrary lib = library();
    std::string out = fileScreen(lib, 0);
    std::vector<std::string> l = lines(out);
    auto at = [&](const std::string& s) { return std::find(l.begin(), l.end(), s) - l.begin(); };
    std::string r0 = std::string(6 - fileSizeText(797000).size(), ' ') + fileSizeText(797000) + "  AXISFLSH.ZIP";
    std::string r1 = std::string(6 - fileSizeText(1200).size(), ' ') + fileSizeText(1200) + "  README.TXT";
    size_t cap = (size_t)at("Size  File");
    REQUIRE(cap < l.size());
    // the size first, then the name, in this order, under the new caption
    CHECK(has(visible(out), r0.substr(r0.find_first_not_of(' '))));
    CHECK(linesWith(out, "778K  AXISFLSH.ZIP").size() >= 1);
    CHECK(linesWith(out, "1K  README.TXT").size() >= 1);
    CHECK(at("778K  AXISFLSH.ZIP") > (long)cap);
    CHECK(at("1K  README.TXT") > at("778K  AXISFLSH.ZIP"));
    CHECK(linesWith(out, "AXISFLSH.ZIP   778K").empty());  // not the built-in order
    CHECK(has(visible(out), "AXISFLSH.ZIP is 778K"));      // the template's status line
    CHECK_FALSE(has(visible(out), "Name          Size"));  // nor the built-in caption
}

TEST_CASE("a malformed FILES.LST draws the built-in frame, and a lite caller keeps it too") {
    FakeLibrary lib = library();
    std::string builtIn;
    {
        Templates none;
        builtIn = fileScreen(lib, 0);
    }
    {
        Templates bad("FILES.LST", "geom=3,6,74,14\ncaption=Name\n");
        CHECK(fileScreen(lib, 0) == builtIn);
    }
    {
        Templates look("FILES.LST", "geom=2,4,60,8\ncaption=|14Size  File\nrow=@SZ>6@  @FN12@\n");
        CHECK(fileScreen(lib, 0) != builtIn);  // the look's template is used
        theme::setLite(true);
        ListTemplate t;
        CHECK_FALSE(listScreenTemplate("FILES.LST", t));  // a slow radio caller is never given the template
        CHECK(fileScreen(lib, 0) == builtIn);
        theme::setLite(false);
        CHECK(listScreenTemplate("FILES.LST", t));
    }
}
#endif

namespace {
struct MemStore : MessageStore {
    std::map<std::string, std::vector<Message>> m;
    int add(const std::string& area, const char* from, const char* to, const char* subj) {
        Message x;
        x.num = (int)m[area].size() + 1;
        x.from = from;
        x.to = to;
        x.subject = subj;
        x.body = "body";
        m[area].push_back(x);
        return x.num;
    }
    std::vector<Message> headers(const std::string& a) override { return m[a]; }
    bool read(const std::string& a, int n, Message& out) override {
        for (const Message& x : m[a])
            if (x.num == n) return out = x, true;
        return false;
    }
    int post(const std::string& a, const Message& x) override { return add(a, x.from.c_str(), x.to.c_str(), x.subject.c_str()); }
};

std::string msgScreen(MemStore& st) {
    std::vector<FileArea>      areas{{"general", "General Chatter"}};
    std::string                out;
    std::map<std::string, int> last{{"general", 3}};
    MessageReader              r(areas, st, out, "Rev", last, [] { return std::string("now"); });
    r.begin();
    out.clear();
    r.key('\r');
    return out;
}
}  // namespace

TEST_CASE("the shipped MSGS.LST draws the message rows the built-in list does") {
    MemStore st;
    for (int i = 1; i <= 20; i++) st.add("general", "Spindle Kid", "All", ("note number " + std::to_string(i)).c_str());
    std::string without, with;
    {
        Templates none;
        without = msgScreen(st);
    }
    {
        std::string tplText = shipped("MSGS.LST");
        REQUIRE_FALSE(tplText.empty());
        Templates look("MSGS.LST", tplText);
        with = msgScreen(st);
    }
    std::vector<std::string> a = linesWith(without, "note number"), b = linesWith(with, "note number");
    REQUIRE(a.size() >= 14);
    CHECK(a == b);
    CHECK(has(a.front(), "*"));  // the new-to-this-caller marker (messages above 3)
}

#if AXIS_ACCOUNTS || !defined(AXIS_ACCOUNTS)
namespace {
uint32_t rng() { return 0x5A5A5A5Au; }
SysInfo  noInfo() { return SysInfo{}; }

User person(const char* handle, int security, bool twit) {
    User u;
    u.handle = handle;
    u.location = "Myrtle Beach";
    u.tagline = "rotary chips";
    u.calls = 14;
    u.security = security;
    u.salt = makeSalt(rng);
    u.hash = hashPassword(u.salt, "pass1234");
    u.status = User::Status::Validated;
    u.twit = twit;
    return u;
}

// the user list (H on the main menu) as `who` sees it
std::string userList(const char* who) {
    UserStore users;
    users.add(person("Spindle Kid", 10, false));
    users.add(person("Top Dog", 10, false));
    users.add(person("TwitCaller", 10, true));
    users.add(person("Boss", kSysopSecurity, false));
    Node n(1, noInfo, BbsConfig{});
    n.setAccounts(&users, [] {}, rng, [] { return 1800000000u; });
    n.begin(0);
    std::string login = std::string(who) + "\rpass1234\r";
    n.input((const uint8_t*)login.data(), login.size(), 1000);
    n.takeOutput();
    n.input((const uint8_t*)"h", 1, 1000);
    return n.takeOutput();
}
}  // namespace

TEST_CASE("the user list template hides a twit from a non-sysop") {
    Templates look("USERS.LST", "geom=3,8,74,10\ncaption=|08Handle            Calls\nrule=1\nrow=@HN16@ @CA>5@|12|TW\n");
    std::string caller = userList("Spindle Kid");
    CHECK(has(visible(caller), "Handle            Calls"));  // the template is in use
    CHECK(has(caller, "Top Dog"));
    CHECK(has(caller, "Spindle Kid"));
    CHECK_FALSE(has(caller, "TwitCaller"));
    CHECK_FALSE(has(caller, "twit"));
    std::string sysop = userList("Boss");
    CHECK(has(sysop, "TwitCaller"));
    std::vector<std::string> mine = linesWith(sysop, "TwitCaller");
    REQUIRE(mine.size() == 1);
    CHECK(has(mine[0], "twit"));  // the twit marker, the sysop's view only
}

TEST_CASE("without a USERS.LST the user list is the built-in one") {
    Templates none;
    std::string caller = userList("Spindle Kid");
    CHECK(has(visible(caller), "handle                  from                  calls  tagline"));
    CHECK_FALSE(has(caller, "TwitCaller"));
    CHECK(has(userList("Boss"), "TwitCaller"));
}
#endif
