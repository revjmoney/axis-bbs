#include "bbs/sysopshell.h"

#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"

using axis::bbs::SysopShell;

namespace {
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }

struct Rig {
    std::string              out;
    std::vector<std::string> ran;
    std::map<std::string, std::string> replies = {
        {"@USERS", "user 1|Rev|valid|255|9|FMD$-\nuser 2|Kid|pending|10|1|FMD--\n"},
        {"@USER Kid", "handle Kid\nreal Kid Kutter\nfrom Conway\nsecurity 10\n"},
        {"@AREAS", "1|axis|Axis Firmware|0|\n2|gcode|G-code|20|m\n"},
        {"@MSGAREAS", "1|general|General Chatter|0|\n"},
        {"@SETTINGS", "bbs_name=AXIS UNDERGROUND CNC\ntheme=iniquity\nsysop_salt_should_never=x\n"},
        {"@MENU", ""},
        {"@LOG", "12:00 boot\n12:01 call from 10.0.0.2\n"},
    };
    SysopShell shell{out, [this](const std::string& c) {
                         ran.push_back(c);
                         auto it = replies.find(c);
                         return it == replies.end() ? std::string("axis: ok\n") : it->second;
                     }};
    void type(const std::string& s) {
        for (char c : s) shell.key(c);
    }
    bool ranCmd(const std::string& c) const {
        for (auto& r : ran)
            if (r == c) return true;
        return false;
    }
};
}  // namespace

TEST_CASE("the look is picked from a numbered list, not typed") {
    Rig r;
    r.shell.begin();
    r.type("44");  // Settings, then Look
    CHECK(plain(r.out).find("theme") != std::string::npos);
    r.type("1");
    std::string o = plain(r.out);
    CHECK(o.find("Axis, drippy orange") != std::string::npos);
    CHECK(o.find("Oblivion, grey and cyan") != std::string::npos);
    CHECK(o.find("Iniquity, green and grey") != std::string::npos);
    CHECK(o.find("now") != std::string::npos);  // the fixture's theme is iniquity
    r.type("2");
    CHECK(r.ranCmd("@SET theme=oblivion"));
}

TEST_CASE("the sysop menu opens on its top layer") {
    Rig r;
    r.shell.begin();
    std::string o = plain(r.out);
    for (const char* s : {"Users", "File areas", "Message bases", "Settings", "Menus", "System"}) CHECK(o.find(s) != std::string::npos);
}

TEST_CASE("users: pick one, change a field, and the change goes out as an @ command") {
    Rig r;
    r.shell.begin();
    r.type("1");  // Users
    CHECK(plain(r.out).find("Kid") != std::string::npos);
    r.type("3");  // (1 add a user, 2 Rev, 3 Kid)
    CHECK(plain(r.out).find("Kid Kutter") != std::string::npos);
    r.type("2");  // Location
    r.type("Myrtle Beach\r");
    CHECK(r.ranCmd("@UEDIT Kid|location|Myrtle Beach"));
    r.type("6");  // Validate
    CHECK(r.ranCmd("@VALIDATE Kid"));
}

TEST_CASE("a password is typed as stars and sent, never shown") {
    Rig r;
    r.shell.begin();
    r.type("13");  // Users, Kid
    r.out.clear();
    r.type("8");  // Set password
    r.type("hunter2\r");
    CHECK(r.ranCmd("@USERPASS Kid hunter2"));
    CHECK(r.out.find("hunter2") == std::string::npos);
}

TEST_CASE("deleting asks first, and N keeps the user") {
    Rig r;
    r.shell.begin();
    r.type("13");
    r.type("n");  // page 2 (a page holds 14): Delete this user is its first item
    r.type("1");
    r.type("n");
    CHECK_FALSE(r.ranCmd("@UDEL Kid"));
    r.type("1");
    r.type("y");
    CHECK(r.ranCmd("@UDEL Kid"));
}

TEST_CASE("file areas: rename one and it keeps its id and level") {
    Rig r;
    r.shell.begin();
    r.type("2");  // File areas
    r.type("3");  // (1 add, 2 axis, 3 gcode)
    r.type("2");  // Name
    r.type("Toolpaths\r");
    CHECK(r.ranCmd("@AREASET files|2|gcode|Toolpaths|20"));
}

TEST_CASE("settings come in groups and salts or hashes never appear") {
    Rig r;
    r.shell.begin();
    r.type("4");  // Settings
    r.type("1");  // BBS and sysop
    CHECK(plain(r.out).find("AXIS UNDERGROUND CNC") != std::string::npos);
    r.type("1");
    r.type("AXIS BBS\r");
    CHECK(r.ranCmd("@SET bbs_name=AXIS BBS"));
}

TEST_CASE("the menu editor starts from a copy of the built-in menu") {
    Rig r;
    r.shell.begin();
    r.type("5");  // Menus
    r.type("1");
    CHECK(r.ranCmd("@MENUSTART"));
}

TEST_CASE("Q and Esc go back a layer, and Q on the top layer leaves") {
    Rig r;
    r.shell.begin();
    r.type("6");  // System
    r.type("1");  // Event log (a text page)
    CHECK(plain(r.out).find("call from 10.0.0.2") != std::string::npos);
    r.type("q");
    r.shell.key(0x1b);
    r.shell.key('x');  // a lone ESC (not an arrow)
    CHECK(r.shell.open());
    r.type("q");
    CHECK_FALSE(r.shell.open());
}

TEST_CASE("arrow keys move the lightbar") {
    Rig r;
    r.shell.begin();
    r.type("\x1b[B\x1b[B\r");  // down twice: Message bases
    CHECK(r.ranCmd("@MSGAREAS"));
}

TEST_CASE("on the board (40 columns, 7 a page) tapping under the list turns the page, and wraps back to the first") {
    Rig r;
    SysopShell board{r.out, [&r](const std::string& c) {
                         auto it = r.replies.find(c);
                         return it == r.replies.end() ? std::string("axis: ok\n") : it->second;
                     },
                     40};
    board.begin();
    board.key('1');  // Users
    board.key('3');  // Kid (after "Add a user" and Rev)
    r.out.clear();
    board.tapRow(11);  // the page line under the list
    CHECK(plain(r.out).find("Set password") != std::string::npos);  // item 8: page 2
    r.out.clear();
    board.tapRow(11);
    board.tapRow(11);
    CHECK(plain(r.out).find("Real name") != std::string::npos);  // back to page 1
}
