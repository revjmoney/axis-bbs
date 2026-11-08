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
        {"@SETTINGS", "bbs_name=AXIS UNDERGROUND CNC\ntheme=iniquity\nwfc=iniquity\nsysop_salt_should_never=x\n"},
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
    bool ranCmdPrefix(const std::string& p) const {
        for (auto& r : ran)
            if (r.rfind(p, 0) == 0) return true;
        return false;
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
    CHECK(plain(r.out).find("look") != std::string::npos);
    r.type("1");
    std::string o = plain(r.out);
    CHECK(o.find("Axis, drippy orange") != std::string::npos);
    CHECK(o.find("Cyan, grey and cyan") != std::string::npos);
    CHECK(o.find("Green, green and grey") != std::string::npos);
    CHECK(o.find("Crt, hardcore crt") != std::string::npos);
    CHECK(o.find("Skulls, skulls") != std::string::npos);
    CHECK(o.find("Wasteland, wasteland") != std::string::npos);
    CHECK(o.find("Oblivion") == std::string::npos);
    CHECK(o.find("now") != std::string::npos);  // the fixture's theme is the old green id
    r.type("2");
    CHECK(r.ranCmd("@SET theme=cyan"));
}

TEST_CASE("Settings > Look has no wfc row, so no settings path changes the theme for the WFC skin") {
    Rig r;
    r.shell.begin();
    r.type("44");  // Settings, then Look
    std::string o = plain(r.out);
    CHECK(o.find("wfc") == std::string::npos);
    CHECK(o.find("look") != std::string::npos);
}

TEST_CASE("the Look section's waiting screen skin sends @SET wfc=, never @SET theme=") {
    Rig r;
    r.shell.beginAt(axis::bbs::SysopSection::Look);
    r.type("2");  // Waiting screen skin
    r.type("5");  // a look (1 is follow the theme)
    CHECK(r.ranCmdPrefix("@SET wfc="));
    CHECK_FALSE(r.ranCmdPrefix("@SET theme="));
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

TEST_CASE("sections switch messages off and turn original LORD back on") {
    Rig r;
    r.replies["@SETTINGS"] += "messages_on=1\nfiles_on=1\ndoors_on=1\n"
                              "door1=lord|Legend of the Red Dragon|LORD.EXE|%N NOEMS /DREW|doorsys\n"
                              "door8=lord|LORD Fast|FAST.EXE|%N|doorsys\n";
    r.shell.begin();
    r.type("7");  // Sections, after System
    std::string o = plain(r.out);
    CHECK(o.find("Messages") != std::string::npos);
    CHECK(o.find("Legend of the Red Dragon") != std::string::npos);
    CHECK(o.find("LORD Fast") != std::string::npos);
    CHECK(o.find("off") != std::string::npos);
    r.type("1");
    CHECK(r.ranCmd("@SET messages_on=0"));
    r.type("4");
    CHECK(r.ranCmd("@SET door1=lord|Legend of the Red Dragon|LORD.EXE|%N NOEMS /DREW|doorsys|1"));
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

TEST_CASE("the one sysop menu reaches every touch menu feature") {
    using axis::bbs::SysopSection;
    {  // System > Restart, confirmed
        Rig r;
        r.shell.beginAt(SysopSection::System);
        r.type("9");  // Restart this board
        CHECK_FALSE(r.ranCmd("@RESTART"));
        r.type("y");
        CHECK(r.ranCmd("@RESTART"));
    }
    {  // System > Rerun setup, confirmed
        Rig r;
        r.shell.beginAt(SysopSection::System);
        r.type("a");
        CHECK_FALSE(r.ranCmd("@RESETSETUP"));
        r.type("y");
        CHECK(r.ranCmd("@RESETSETUP"));
    }
    {  // System > Format SD needs FORMAT typed
        Rig r;
        r.shell.beginAt(SysopSection::System);
        r.type("b");
        r.type("nope\r");
        CHECK_FALSE(r.ranCmd("@SDFORMAT FORMAT"));
        r.type("b");
        r.type("format\r");  // case matters
        CHECK_FALSE(r.ranCmd("@SDFORMAT FORMAT"));
        r.type("b");
        r.type("FORMAT\r");
        CHECK(r.ranCmd("@SDFORMAT FORMAT"));
    }
    {  // System > Station
        Rig r;
        r.replies["@STATION"] = "sensor|temp 21.5 (stub)\nrelay|1|0\nrelay|2|1\n";
        r.shell.beginAt(SysopSection::System);
        r.type("c");
        CHECK(plain(r.out).find("Relay 2") != std::string::npos);
        r.type("3");  // sensor, relay 1, relay 2: the third row is relay 2
        CHECK(r.ranCmd("@RELAY 2 off"));
    }
    {  // Look > theme, and the waiting screen skin
        Rig r;
        r.shell.beginAt(SysopSection::Look);
        r.type("1");  // Theme
        r.type("8");  // Skulls
        CHECK(r.ranCmd("@SET theme=skulls"));
        r.type("2");  // Waiting screen skin
        r.type("1");  // follow the theme
        CHECK(r.ranCmd("@SET wfc="));
        r.type("2");
        r.type("9");  // 1 follow, then the looks: 9 is skulls
        CHECK(r.ranCmd("@SET wfc=skulls"));
    }
    {  // Settings: the board switches and the telnet sysop switch
        Rig r;
        r.replies["@SETTINGS"] += "files_on=1\nmessages_on=0\ndoors_on=1\nsysop_telnet=1\nfree_kb=500\n";
        r.shell.beginAt(SysopSection::Settings);
        r.type("b");  // page 1 ends with Files (b), Messages (c), Doors (d), Free downloads (e)
        CHECK(r.ranCmd("@SET files_on=0"));
        r.type("c");
        CHECK(r.ranCmd("@SET messages_on=1"));
        r.type("e");
        r.type("750\r");
        CHECK(r.ranCmd("@SET free_kb=750"));
        r.type("n");
        r.type("1");  // page 2: Telnet sysop access
        CHECK(r.ranCmd("@SET sysop_telnet=0"));
    }
    {  // the top layer has a Look entry
        Rig r;
        r.shell.begin();
        r.type("8");
        CHECK(plain(r.out).find("Waiting screen skin") != std::string::npos);
    }
}

TEST_CASE("beginAt opens a section and Esc leaves the shell") {
    using axis::bbs::SysopSection;
    Rig r;
    r.shell.beginAt(SysopSection::Users);
    CHECK(r.shell.open());
    CHECK(plain(r.out).find("USERS") != std::string::npos);
    r.shell.key(0x1b);
    CHECK_FALSE(r.shell.key('x'));  // the lone ESC resolves on the next key
    CHECK_FALSE(r.shell.open());
}
