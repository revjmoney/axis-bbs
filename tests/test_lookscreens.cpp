// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The account screen, who's online, the doors lightbar and the sysop panel: a look's template and art when it has
// them, and the built-in screen when it does not. The user list was already on this path (USERS.LST).
#include <filesystem>
#include <map>
#include <regex>
#include <string>

#include "doctest.h"
#include "app/themeprovider.h"
#include "bbs/config.h"
#include "bbs/features.h"
#include "bbs/listscreen.h"
#include "bbs/node.h"
#include "bbs/password.h"
#include "bbs/sysopshell.h"
#include "bbs/theme.h"
#include "bbs/users.h"

using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {

std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*[A-Za-z]"), ""); }

struct ThemeGuard {
    std::map<std::string, std::string> text;
    std::vector<std::string>           bad;
    explicit ThemeGuard(std::map<std::string, std::string> files = {}) : text(std::move(files)) { install(); }
    void install() {
        theme::setListTemplate([this](const std::string& n) { auto it = text.find(n); return it == text.end() ? std::string() : it->second; },
                               [this](const std::string& n) { bad.push_back(n); });
    }
    ~ThemeGuard() {
        theme::setListTemplate({});
        theme::setArt({});
        theme::setArtTop({});
        theme::setLite(false);
    }
};

struct TempDir {
    fs::path p;
    TempDir() {
        static int n = 0;
        p = fs::temp_directory_path() / ("axis_look_" + std::to_string(n++));
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);
    }
};

struct TestHost : axis::app::Host {
    std::string sd;
    bool        withSd = true;
    uint32_t    nowMs() override { return 1000; }
    uint32_t    unixNow() override { return 1780000000u; }
    uint32_t    random32() override { return 1; }
    void        event(const std::string&) override {}
    std::string sdRoot() override { return sd; }
    bool        hasSd() override { return withSd; }
    uint64_t    sdFreeBytes() override { return 0; }
    std::string flashRoot() override { return sd; }
};

void writeFile(const fs::path& path, const std::string& body) {
    fs::create_directories(path.parent_path());
    FILE* f = std::fopen(path.string().c_str(), "wb");
    REQUIRE(f);
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
}

SysInfo info() { return SysInfo{}; }

void type(Node& n, const std::string& s) { n.input((const uint8_t*)s.data(), s.size(), 1000); }

Node loggedIn() {
    Node n(1, info);
    n.begin(0);
    type(n, "Rev\r");
    n.takeOutput();
    return n;
}

}  // namespace

TEST_CASE("the new screens are names the theme resolver knows") {
    CHECK(std::string(theme::menuScreenFile("ACCOUNT.LST")) == "ACCOUNT.LST");
    CHECK(std::string(theme::menuScreenFile("WHO.LST")) == "WHO.LST");
    CHECK(std::string(theme::menuScreenFile("DOORLB.LST")) == "DOORLB.LST");
    CHECK(std::string(theme::menuScreenFile("SYSOP.LST")) == "SYSOP.LST");
    CHECK(std::string(theme::menuScreenFile("ACCOUNT.ANS")) == "ACCOUNT.ANS");
    CHECK(std::string(theme::menuScreenFile("WHO.LITE.ANS")) == "WHO.LITE.ANS");
    CHECK(std::string(theme::menuScreenFile("DOORLB.LITE.ANS")) == "DOORLB.LITE.ANS");
    CHECK(std::string(theme::menuScreenFile("MENUS.ANS")) == "MENUS.ANS");
    CHECK(std::string(theme::menuScreenFile("MENUS.LITE.ANS")) == "MENUS.LITE.ANS");
    CHECK(theme::menuSetArt("WHO.ANS"));
    CHECK(theme::menuSetArt("ACCOUNT.LITE.ANS"));
    CHECK(theme::menuSetArt("SYSOP.ANS"));
    CHECK(theme::menuSetArt("MENUS.LITE.ANS"));
    CHECK_FALSE(theme::menuSetArt("MAIN.ANS"));     // a menu screen keeps its own lookup
    CHECK_FALSE(theme::menuSetArt("WHO.LST"));      // a list template is not header art
    CHECK_FALSE(theme::menuSetArt("DOORS.ANS"));    // the doors menu art, not the door list
    CHECK(theme::menuScreenFile("USERS.LST") != nullptr);  // the user list was already themed
}

TEST_CASE("header art is the theme folder, then the menu set, then nothing") {
    TempDir  dir;
    TestHost host;
    host.sd = dir.p.string();
    BbsConfig cfg;
    cfg.theme = "axis";
    axis::app::ThemeProvider tp(host);
    tp.config = [&]() -> const BbsConfig& { return cfg; };

    writeFile(dir.p / "menus" / "WHO.ANS", "MENUWHO\r\nsecond\r\nthird\r\nfourth\r\n");
    writeFile(dir.p / "menus" / "WHO.LITE.ANS", "LITEWHO\r\n");
    writeFile(dir.p / "menus" / "WHO.LST", "geom=4,8,74,12\nrow=@ND @HN\n");
    writeFile(dir.p / "menus" / "MAIN.ANS", "MENUSMAIN\r\n");
    CHECK(tp.themeArt("WHO.ANS") == "MENUWHO\r\nsecond\r\nthird\r\nfourth\r\n");
    CHECK(tp.themeArtTop("WHO.ANS", 2).find("fourth") == std::string::npos);
    CHECK(tp.themeArtTop("WHO.ANS", 2).find("MENUWHO") != std::string::npos);
    CHECK(tp.themeArt("WHO.LITE.ANS") == "LITEWHO\r\n");
    CHECK(tp.listTemplateText("WHO.LST").find("row=@ND @HN") != std::string::npos);
    CHECK(tp.themeArt("MAIN.ANS").empty());  // not one of the menu-set header screens

    writeFile(dir.p / "themes" / "axis" / "WHO.ANS", "THEMEWHO\r\n");
    writeFile(dir.p / "themes" / "axis" / "WHO.LST", "geom=4,8,74,12\nrow=@HN\n");
    CHECK(tp.themeArt("WHO.ANS") == "THEMEWHO\r\n");
    CHECK(tp.themeArtTop("WHO.ANS", 3) == "THEMEWHO\r\n");
    CHECK(tp.listTemplateText("WHO.LST").find("row=@HN") != std::string::npos);

    fs::remove(dir.p / "themes" / "axis" / "WHO.ANS");
    fs::remove(dir.p / "menus" / "WHO.ANS");
    CHECK(tp.themeArt("WHO.ANS").empty());
    CHECK(tp.themeArt("crt:WHO.ANS").empty());  // a look preview does not borrow the menu set
}

TEST_CASE("who's online keeps the built-in list until the look has WHO.LST") {
    Node n = loggedIn();
    n.setChat({}, [] { return std::vector<Node::WhoLine>{{1, "Rev", "Menu", ""}, {2, "Spindle Kid", "Files", ""}, {3, "", "", ""}}; });
    type(n, "w");
    std::string built = plain(n.takeOutput());
    CHECK(built.find("WHO'S ONLINE") != std::string::npos);
    CHECK(built.find("Spindle Kid") != std::string::npos);
    CHECK(built.find("waiting") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);

    ThemeGuard g({{"WHO.LST", "geom=4,8,74,8\nhead=WHO\ncaption=|08ON THE NODES\nrule=1\nrow=@ND>2@ @HN16@ @AC20@\n"}});
    theme::setArt([](const std::string& name) { return name == "WHO.ANS" ? std::string("WHOART\r\n") : std::string(); });
    type(n, "w");
    std::string out = n.takeOutput();
    CHECK(out.find("WHOART") != std::string::npos);
    CHECK(plain(out).find("ON THE NODES") != std::string::npos);
    CHECK(plain(out).find("Spindle Kid") != std::string::npos);
    CHECK(plain(out).find("Files") != std::string::npos);

    g.text["WHO.LST"] = "geom=4,8\ncaption=BROKEN\n";  // no row=: not a template
    g.install();
    type(n, "w");
    std::string fell = plain(n.takeOutput());
    CHECK(fell.find("WHO'S ONLINE") != std::string::npos);
    CHECK(fell.find("BROKEN") == std::string::npos);
    CHECK(g.bad.size() == 1);

    g.text["WHO.LST"] = "geom=4,8,74,8\ncaption=|08RADIO LIST\nrule=1\nrow=@HN16@\n";
    g.install();
    axis::net::LinkInfo slow;
    slow.any = true;
    slow.kind = "radio";
    slow.bps = 1000;
    n.setLink(slow);  // a slow radio caller: the template is skipped, the lite header is used
    theme::setArt([](const std::string& name) { return name == "WHO.LITE.ANS" ? std::string("SLOWWHO\r\n") : std::string(); });
    type(n, "w");
    std::string lite = n.takeOutput();
    CHECK(lite.find("SLOWWHO") != std::string::npos);          // the lite header
    CHECK(plain(lite).find("WHO'S ONLINE") != std::string::npos);  // the built-in list, not the template
    CHECK(plain(lite).find("RADIO LIST") == std::string::npos);
}

TEST_CASE("the account screen draws from ACCOUNT.LST and falls back without one") {
    UserStore users;
    User      u;
    u.handle = "Kid Chips";
    u.salt = makeSalt([] { return 1u; });
    u.hash = hashPassword(u.salt, "oldpass1");
    u.status = User::Status::Validated;
    u.realName = "Jesse";
    u.location = "Conway, SC";
    users.add(u);
    Node n(1, info);
    n.setAccounts(&users, [] {}, [] { return 1u; }, [] { return 1800000000u; });
    n.begin(0);
    type(n, "Kid Chips\roldpass1\r");
    n.takeOutput();
    type(n, "u");
    std::string built = plain(n.takeOutput());
    CHECK(built.find("Jesse") != std::string::npos);
    CHECK(built.find("[T]") != std::string::npos);
    CHECK(built.find("[P]") != std::string::npos);

    ThemeGuard g({{"ACCOUNT.LST", "geom=4,8,70,10\nhead=ACCOUNT\ncaption=|08CARD FILE\nrule=1\nrow=|15@LB10@ @VL30@\n"}});
    theme::setArt([](const std::string& name) { return name == "ACCOUNT.ANS" ? std::string("ACCTART\r\n") : std::string(); });
    type(n, "q");
    n.takeOutput();
    type(n, "u");
    std::string out = n.takeOutput();
    CHECK(out.find("ACCTART") != std::string::npos);
    CHECK(plain(out).find("CARD FILE") != std::string::npos);
    CHECK(plain(out).find("Jesse") != std::string::npos);
    CHECK(plain(out).find("Conway, SC") != std::string::npos);
    CHECK(plain(out).find("[T]") != std::string::npos);  // the built-in keys, the template gave none

    g.text.clear();
    g.install();
    axis::net::LinkInfo slow;
    slow.any = true;
    slow.kind = "radio";
    slow.bps = 1000;
    n.setLink(slow);
    theme::setArt([](const std::string& name) { return name == "ACCOUNT.LITE.ANS" ? std::string("SLOWACCT\r\n") : std::string(); });
    type(n, "q");
    n.takeOutput();
    type(n, "u");
    std::string lite = n.takeOutput();
    CHECK(lite.find("SLOWACCT") != std::string::npos);
    CHECK(plain(lite).find("Jesse") != std::string::npos);
    CHECK(plain(lite).find("CARD FILE") == std::string::npos);
}

TEST_CASE("the doors list is a lightbar when the look has DOORLB.LST, and the numbered window otherwise") {
    ThemeGuard none;
    Node       n = loggedIn();
    type(n, "d");
    std::string built = plain(n.takeOutput());
    CHECK(built.find("DOORBAR") == std::string::npos);
    CHECK(n.state() == Node::State::Doors);
    const bool hasDoor = built.find("[1]") != std::string::npos;
    type(n, "q");
    CHECK(n.state() == Node::State::Menu);

    ThemeGuard g({{"DOORLB.LST", "geom=4,8,44,10\ncolours=112,7\nhead=DOORLB\ncaption=|08DOORBAR\nrule=1\nrow=[@KY1@] @NM30@\n"}});
    theme::setArt([](const std::string& name) { return name == "DOORLB.ANS" ? std::string("DOORART\r\n") : std::string(); });
    type(n, "d");
    std::string out = n.takeOutput();
    CHECK(out.find("DOORART") != std::string::npos);
    CHECK(plain(out).find("DOORBAR") != std::string::npos);
    CHECK(plain(out).find("Door number") != std::string::npos);
    CHECK(n.state() == Node::State::Doors);
    type(n, "\x1b[B");
    CHECK(n.state() == Node::State::Doors);  // an arrow moves the bar, it does not leave
    n.takeOutput();
    if (hasDoor) {
        type(n, "1");
        CHECK(n.state() == Node::State::InDoor);  // the old number key still opens the door
        n.takeOutput();
        n.resumeAfterDoor(Node::DoorEnd::Normal, 1000);
        n.takeOutput();
    } else {
        type(n, "q");
        CHECK(n.state() == Node::State::Menu);
    }

    g.text["DOORLB.LST"] = "caption=NOPE\n";  // malformed: no row=
    g.install();
    if (n.state() != Node::State::Menu) {
        type(n, "q");
        n.takeOutput();
    }
    type(n, "d");
    std::string fell = plain(n.takeOutput());
    CHECK(fell.find("DOORBAR") == std::string::npos);
    CHECK(fell.find("NOPE") == std::string::npos);
    CHECK_FALSE(g.bad.empty());
    type(n, "q");

    g.text["DOORLB.LST"] = "geom=4,8,44,10\ncaption=|08SLOWDOORS\nrule=1\nrow=[@KY1@] @NM30@\n";
    g.install();
    axis::net::LinkInfo slow;
    slow.any = true;
    slow.kind = "radio";
    slow.bps = 1000;
    n.setLink(slow);
    type(n, "d");
    std::string lite = plain(n.takeOutput());
    CHECK(lite.find("SLOWDOORS") == std::string::npos);  // a slow caller keeps the numbered window
    CHECK(n.state() == Node::State::Doors);
}

TEST_CASE("the sysop panel rows come from SYSOP.LST on an 80 column screen") {
    std::string out;
    auto        run = [](const std::string& c) { return c == "@MENU" ? std::string() : std::string("axis: ok\n"); };
    {
        ThemeGuard none;
        SysopShell shell(out, run, 80, true);
        shell.begin();
        CHECK(plain(out).find("Users") != std::string::npos);
        CHECK(plain(out).find("PANEL") == std::string::npos);
        CHECK(out.find("\x1b[0;30;47m") != std::string::npos);  // the built-in bar
    }
    out.clear();
    ThemeGuard g({{"SYSOP.LST", "geom=3,6,74,14\ncolours=112,7\nhead=SYSOP\ncaption=|15PANEL @CR40@\nrule=1\nrow=@KY1@ @LB24@ @VL20@\n"}});
    theme::setArt([](const std::string& name) { return name == "SYSOP.ANS" ? std::string("SYSART\r\n") : name == "MENUS.ANS" ? std::string("MENUART\r\n") : std::string(); });
    SysopShell shell(out, run, 80, true);
    shell.begin();
    CHECK(out.find("SYSART") != std::string::npos);
    CHECK(plain(out).find("PANEL") != std::string::npos);
    CHECK(plain(out).find("SYSOP") != std::string::npos);  // the breadcrumb, from @CR
    CHECK(plain(out).find("Users") != std::string::npos);
    shell.key('5');  // Menus, the page a sysop uses to copy the built-in menu in
    CHECK(out.find("MENUART") != std::string::npos);
    CHECK(plain(out).find("Start editing") != std::string::npos);

    out.clear();
    SysopShell board(out, run, 40, true);  // the board's own screen stays on the built-in rows
    board.begin();
    CHECK(plain(out).find("PANEL") == std::string::npos);
    CHECK(plain(out).find("Users") != std::string::npos);

    g.text["SYSOP.LST"] = "caption=BROKEN\n";
    g.install();
    out.clear();
    SysopShell again(out, run, 80, true);
    again.begin();
    CHECK(plain(out).find("BROKEN") == std::string::npos);
    CHECK(plain(out).find("Users") != std::string::npos);
    CHECK_FALSE(g.bad.empty());
}
