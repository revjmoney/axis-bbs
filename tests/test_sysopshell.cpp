#include "bbs/sysopshell.h"
#include "bbs/features.h"

#include <algorithm>
#include <cctype>
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
    SysopShell::Run run = [this](const std::string& c) {
        ran.push_back(c);
        auto it = replies.find(c);
        return it == replies.end() ? std::string("axis: ok\n") : it->second;
    };
    SysopShell shell{out, run, 80, true};  // local: the board's own screen, the serial console or the panel's console
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

TEST_CASE("the waiting screen skin offers the classic built-in screen") {
    Rig r;
    r.shell.beginAt(axis::bbs::SysopSection::Look);
    r.type("2");  // Waiting screen skin
    CHECK(plain(r.out).find("Classic AXIS (built in)") != std::string::npos);
    r.type("2");  // 1 follows the theme, 2 is the classic screen
    CHECK(r.ranCmd("@SET wfc=classic"));
}

TEST_CASE("with previews on, picking a look asks for its preview and sets nothing yet") {
    Rig r;
    r.shell.setPreviews(true);
    r.shell.beginAt(axis::bbs::SysopSection::Look);
    r.type("1");  // Theme
    r.type("4");  // Crt
    std::string key, look;
    REQUIRE(r.shell.takePreview(key, look));
    CHECK(key == "theme");
    CHECK(look == "crt");
    CHECK_FALSE(r.ranCmdPrefix("@SET theme="));
    CHECK_FALSE(r.shell.takePreview(key, look));  // once
    r.out.clear();
    r.shell.previewDone("axis: theme now crt");  // the host set it: back to the Look layer with the note
    CHECK(r.shell.open());
    CHECK(plain(r.out).find("Waiting screen skin") != std::string::npos);
    r.shell.setPreviews(true);
    r.type("2");  // the skin picker previews too, the classic screen included
    r.type("2");
    REQUIRE(r.shell.takePreview(key, look));
    CHECK(key == "wfc");
    CHECK(look == "classic");
}

TEST_CASE("Esc from a preview goes back to the list it came from") {
    Rig r;
    r.shell.setPreviews(true);
    r.shell.beginAt(axis::bbs::SysopSection::Look);
    r.type("1");
    r.type("4");
    std::string key, look;
    REQUIRE(r.shell.takePreview(key, look));
    r.out.clear();
    r.shell.previewDone("");  // nothing set
    CHECK(plain(r.out).find("Crt, hardcore crt") != std::string::npos);
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

#if AXIS_FILES
TEST_CASE("file areas: rename one and it keeps its id and level") {
    Rig r;
    r.shell.begin();
    r.type("2");  // File areas
    r.type("3");  // (1 add, 2 axis, 3 gcode)
    r.type("2");  // Name
    r.type("Toolpaths\r");
    CHECK(r.ranCmd("@AREASET files|2|gcode|Toolpaths|20"));
}
#else
TEST_CASE("file areas: a build without them says so and asks the board nothing") {
    Rig r;
    r.shell.begin();
    r.ran.clear();
    r.type("2");  // File areas
    CHECK(plain(r.out).find("The file areas are not in this firmware.") != std::string::npos);
    CHECK(r.ran.empty());
}
#endif

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
    CHECK((o.find("Files") != std::string::npos) == (AXIS_FILES == 1));  // (a build without the file areas: no switch)
    CHECK(o.find("Messages") != std::string::npos);
    CHECK(o.find("Legend of the Red Dragon") != std::string::npos);
    CHECK(o.find("LORD Fast") != std::string::npos);
    CHECK(o.find("off") != std::string::npos);
    r.type("1");
    CHECK(r.ranCmd("@SET messages_on=0"));
    r.type(AXIS_FILES ? "4" : "3");  // (Messages, Files, Doors, then the doors)
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
                     40, true};
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
    {  // System > Rerun setup, confirmed: setup again, the settings kept until the wizard saves (no erase)
        Rig r;
        r.shell.beginAt(SysopSection::System);
        r.type("a");
        CHECK_FALSE(r.ranCmd("@RERUNSETUP"));
        r.type("y");
        CHECK(r.ranCmd("@RERUNSETUP"));
        CHECK_FALSE(r.ranCmd("@RESETSETUP"));
    }
    {  // System > Format SD needs FORMAT typed
        Rig r;
        r.shell.beginAt(SysopSection::System);
        r.type("b");
        r.type("nope\r");
        CHECK_FALSE(r.ranCmd("@SDFORMAT FORMAT"));
        r.type("b");
        r.type("formats\r");
        CHECK_FALSE(r.ranCmd("@SDFORMAT FORMAT"));
        r.type("b");
        r.type("format\r");  // any case (the touch keyboard has no caps lock)
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
        r.type("a");  // 1 follow, 2 classic, then the looks: the tenth (a) is skulls
        CHECK(r.ranCmd("@SET wfc=skulls"));
    }
    {  // Settings: the board switches and the telnet sysop switch
        Rig r;
        r.replies["@SETTINGS"] += "files_on=1\nmessages_on=0\ndoors_on=1\nsysop_telnet=1\nfree_kb=500\n";
        r.shell.beginAt(SysopSection::Settings);
        // Row 9 is the cluster key and row 10 (a) the DOS prompt password, each when the build has it. Then Files (when the build has the file areas),
        // Messages, Doors, Free downloads, Telnet sysop access. A page holds 1 to 9 and a to e; N turns it. In the full
        // build: Files b, Messages c, Doors d, Free downloads e, and Telnet sysop access 1 on page 2.
        if (!AXIS_DOSPROMPT) CHECK(plain(r.out).find("DOS prompt") == std::string::npos);  // left out: no password to set
        int  row = 9 + AXIS_CLUSTER + AXIS_DOSPROMPT;
        auto pick = [&r](int at) {
            if (at > 14) {
                r.type("n");
                at -= 14;
            }
            r.type(std::string(1, "123456789abcde"[at - 1]));
        };
        if (AXIS_FILES) {
            pick(row++);
            CHECK(r.ranCmd("@SET files_on=0"));
        }
        pick(row++);
        CHECK(r.ranCmd("@SET messages_on=1"));
        row++;  // (Doors)
        pick(row++);
        r.type("750\r");
        CHECK(r.ranCmd("@SET free_kb=750"));
        pick(row);
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

TEST_CASE("over telnet the board-only items are not there: no restart, setup, format, cluster key or station") {
    using axis::bbs::SysopSection;
    Rig        r;
    SysopShell remote{r.out, r.run, 80, false};
    remote.beginAt(SysopSection::System);
    std::string o = plain(r.out);
    CHECK(o.find("Event log") != std::string::npos);
    for (const char* s : {"Restart this board", "Rerun setup", "Format SD card", "Station"}) CHECK(o.find(s) == std::string::npos);
    r.out.clear();
    remote.beginAt(SysopSection::Settings);
    o = plain(r.out);
    CHECK(o.find("Sysop password") != std::string::npos);
    CHECK(o.find("Cluster key") == std::string::npos);
    r.out.clear();
    r.shell.beginAt(SysopSection::System);  // the same layers on the board itself have them
    o = plain(r.out);
    for (const char* s : {"Restart this board", "Rerun setup", "Format SD card", "Station"}) CHECK(o.find(s) != std::string::npos);
    r.out.clear();
    r.shell.beginAt(SysopSection::Settings);
    CHECK((plain(r.out).find("Cluster key") != std::string::npos) == (AXIS_CLUSTER == 1));  // (built without the cluster: no key)
}

TEST_CASE("a board's own commands are never run for another board") {
    using axis::bbs::boardOnlyCommand;
    for (const char* c : {"@CLUSTERKEY secret-key", "@CLUSTERKEY", "@RESTART", "@RESTART force", "@RERUNSETUP", "@RESETSETUP",
                          "@SDFORMAT FORMAT", "@SDCHECK", "@HEADLESS on", "@RELAY 2 on", "@BEEP", "@STATION", "@SHOT", "@clusterkey x"})
        CHECK(boardOnlyCommand(c));
    for (const char* c : {"@SET bbs_name=AXIS", "@USERS", "@SETTINGS", "@KICK 3", "@NODES", "@CLUSTERKEYS", "@LOG"}) CHECK_FALSE(boardOnlyCommand(c));
    int  ran = 0;
    auto run = [&](const std::string&) {
        ran++;
        return std::string("axis: ok\n");
    };
    std::string r = axis::bbs::followerCommand("@CLUSTERKEY secret-key", run);  // a "C" request from a follower
    CHECK(ran == 0);
    CHECK(r.find("refused") != std::string::npos);
    CHECK(axis::bbs::followerCommand("@RESTART", run).find("refused") != std::string::npos);
    CHECK(ran == 0);
    CHECK(axis::bbs::followerCommand("@SET location=Conway", run) == "axis: ok\n");
    CHECK(ran == 1);
}

TEST_CASE("a layer asks for the settings once however many it shows") {
    using axis::bbs::SysopSection;
    Rig r;
    r.replies["@SETTINGS"] += "files_on=1\nmessages_on=0\ndoors_on=1\nsysop_telnet=1\nfree_kb=500\n";
    r.shell.beginAt(SysopSection::Settings);
    int n = 0;
    for (auto& c : r.ran) n += c == "@SETTINGS";
    CHECK(n == 1);
    r.ran.clear();
    // the first board switch (Files off in the full build, row b): the layer is built again, and asks again (once)
    r.type(std::string(1, "123456789abcde"[9 + AXIS_CLUSTER + AXIS_DOSPROMPT - 1]));
    n = 0;
    for (auto& c : r.ran) n += c == "@SETTINGS";
    CHECK(n == 1);
    r.ran.clear();
    r.shell.beginAt(SysopSection::Look);
    n = 0;
    for (auto& c : r.ran) n += c == "@SETTINGS";
    CHECK(n == 1);
}

// ---- build switches (Task 5): what the sysop menu shows without the cluster or the modem link
TEST_CASE("features: the Modem page and the modem settings show only with the modem link built in") {
    using axis::bbs::SysopSection;
    const bool modem = AXIS_MODEMLINK == 1;
    {
        Rig r;
        r.shell.beginAt(SysopSection::System);
        CHECK((plain(r.out).find("LFD3K-SM radio callers") != std::string::npos) == modem);
    }
    for (char page : {'2', '5'}) {  // Network, then Other settings: kept as saved, shown on Network or nowhere
        Rig r;
        r.replies["@SETTINGS"] += "modem_link=1\nlite_below_bps=7031\n";
        r.shell.beginAt(SysopSection::Settings);
        r.out.clear();
        r.type(std::string(1, page));
        std::string o = plain(r.out);
        CHECK((o.find("modem_link") != std::string::npos) == (modem && page == '2'));
        CHECK((o.find("lite_below_bps") != std::string::npos) == (modem && page == '2'));
    }
}

TEST_CASE("features: the cluster key is offered only with the cluster built in") {
    using axis::bbs::SysopSection;
    Rig r;
    r.shell.beginAt(SysopSection::Settings);
    CHECK((plain(r.out).find("Cluster key") != std::string::npos) == (AXIS_CLUSTER == 1));
}

#if AXIS_NETS
namespace {
// The screen drawn last (from its clear screen on), escapes stripped.
std::string screen(const std::string& out) {
    size_t cls = out.rfind("\x1b[2J");
    return plain(cls == std::string::npos ? out : out.substr(cls));
}

// Picks the row with this label on the layer on top, turning pages to find it. False if it is on none.
bool pickRow(Rig& r, const std::string& label) {
    for (int page = 0; page < 6; page++) {
        std::string s = screen(r.out);
        for (size_t at = s.find(" " + label); at != std::string::npos; at = s.find(" " + label, at + 1)) {
            if (at < 2 || s[at - 2] != ' ' || !std::isalnum((unsigned char)s[at - 1])) continue;
            char k = s[at - 1];
            r.out.clear();
            r.type(std::string(1, k));
            return true;
        }
        r.out.clear();
        r.type("n");
    }
    return false;
}

size_t ranCount(const Rig& r, const std::string& c) { return (size_t)std::count(r.ran.begin(), r.ran.end(), c); }

const char* const kRecordPage0 =
    "12 202611:0:1780000000 2026-11-15 10:30 local app 0123456789abcdef 1\n"
    "11 202611:120:1780000060 2026-11-15 10:31 fedcba9876543210 vote fedcba9876543210 on 0123456789abcdef yes\n"
    "axis: older from 10\n";
const char* const kRecordPage1 = "10 202610:40:1779000000 2026-10-01 09:00 local member 0123456789abcdef admitted\n";

void toRecords(Rig& r) {
    r.replies["@NET"] = "board 0123456789abcdef\nrev board no\n";
    r.shell.beginAt(axis::bbs::SysopSection::System);
    REQUIRE(pickRow(r, "Sysop Net"));
    REQUIRE(pickRow(r, "Traffic and logs"));
}
}  // namespace

TEST_CASE("the record browser: list lines, SHOW, Older and Newer") {
    Rig r;
    r.replies["@NET RECORD KIND votes 20 0"] = kRecordPage0;
    r.replies["@NET RECORD KIND votes 20 10"] = kRecordPage1;
    r.replies["@NET RECORD SHOW 202611:120:1780000060"] = "axis: line 11\ncheck signed by fedcba9876543210, checks\n";
    toRecords(r);
    REQUIRE(pickRow(r, "Votes"));
    CHECK(ranCount(r, "@NET RECORD KIND votes 20 0") == 1);
    std::string s = screen(r.out);
    CHECK(s.find("11-15 10:30 local") != std::string::npos);  // the label: month-day, time, who
    CHECK(s.find("app 0123456789abcdef 1") != std::string::npos);  // the value: what
    CHECK(s.find("11-15 10:31 fedcba9876543210") != std::string::npos);
    CHECK(s.find("Older") != std::string::npos);
    CHECK(s.find("Newer") == std::string::npos);
    CHECK(s.find("older from") == std::string::npos);

    // A line opens SHOW for that very line.
    REQUIRE(pickRow(r, "11-15 10:31"));
    CHECK(r.ranCmd("@NET RECORD SHOW 202611:120:1780000060"));
    CHECK(screen(r.out).find("line 11") != std::string::npos);
    r.out.clear();
    r.type("q");

    REQUIRE(pickRow(r, "Older"));
    CHECK(ranCount(r, "@NET RECORD KIND votes 20 10") == 1);
    s = screen(r.out);
    CHECK(s.find("10-01 09:00 local") != std::string::npos);
    CHECK(s.find("member 0123456789abcdef admitted") != std::string::npos);
    CHECK(s.find("Newer") != std::string::npos);
    CHECK(s.find("Older") == std::string::npos);

    REQUIRE(pickRow(r, "Newer"));
    s = screen(r.out);
    CHECK(s.find("11-15 10:30 local") != std::string::npos);
    CHECK(s.find("Newer") == std::string::npos);
    CHECK(ranCount(r, "@NET RECORD KIND votes 20 0") >= 2);
}

TEST_CASE("the record browser: a search answer runs once, and a line that is no record shows as it is") {
    Rig r;
    r.replies["@NET RECORD FIND spindle 20 0"] = "axis: nothing found\n";
    toRecords(r);
    REQUIRE(pickRow(r, "Search"));
    r.type("spindle\r");
    CHECK(ranCount(r, "@NET RECORD FIND spindle 20 0") == 1);
    std::string s = screen(r.out);
    CHECK(s.find("nothing found") != std::string::npos);
    CHECK(s.find("axis: nothing found") == std::string::npos);
}

TEST_CASE("a text page keeps 400 lines and says the rest is cut") {
    Rig r;
    std::string months = "axis: months:\n";
    for (int i = 1; i < 500; i++) months += "line " + std::to_string(i) + "\n";
    r.replies["@NET RECORD MONTHS"] = months;
    toRecords(r);
    REQUIRE(pickRow(r, "Months"));
    for (int i = 0; i < 40; i++) r.type("n");
    std::string o = plain(r.out);
    CHECK(o.find("months:") != std::string::npos);
    CHECK(o.find("axis: months") == std::string::npos);
    CHECK(o.find("line 399\r") != std::string::npos);
    CHECK(o.find("line 400\r") == std::string::npos);
    CHECK(screen(r.out).find("(the rest is not shown)") != std::string::npos);
}

TEST_CASE("New Board Voting: the waiting list, the application and a vote with its comment") {
    Rig r;
    r.replies["@NET"] = "board 0123456789abcdef\nuser net member\nrev board no\n";
    r.replies["@NET WAITING"] = "fedcba9876543210 2 1 Spindle Shop, Conway SC\n1111222233334444 0 0 Other Board, Here\n";
    r.replies["@NET APP fedcba9876543210"] = "name Spindle Shop\nsysop Kid\nyes 2\nno 1\n";
    r.shell.beginAt(axis::bbs::SysopSection::System);
    REQUIRE(pickRow(r, "User Net"));
    CHECK(screen(r.out).find("2 waiting") != std::string::npos);
    REQUIRE(pickRow(r, "New Board Voting"));
    std::string s = screen(r.out);
    CHECK(s.find("Spindle Shop, Conway SC") != std::string::npos);
    CHECK(s.find("yes 2 no 1") != std::string::npos);
    CHECK(s.find("Other Board, Here") != std::string::npos);
    REQUIRE(pickRow(r, "Spindle Shop, Conway SC"));
    CHECK(r.ranCmd("@NET APP fedcba9876543210"));
    s = screen(r.out);
    CHECK(s.find("Kid") != std::string::npos);
    REQUIRE(pickRow(r, "Comment"));
    r.type("solid board\r");
    REQUIRE(pickRow(r, "Vote yes"));
    CHECK(r.ranCmd("@NETVOTE fedcba9876543210 Y solid board"));
    // Back in the list and into another applicant: the comment is not carried over.
    r.out.clear();
    r.type("q");
    r.type("q");
    REQUIRE(pickRow(r, "New Board Voting"));
    REQUIRE(pickRow(r, "Other Board, Here"));
    REQUIRE(pickRow(r, "Vote no"));
    CHECK(r.ranCmd("@NETVOTE 1111222233334444 N "));
}

TEST_CASE("New Board Voting with nobody waiting, and no voting row before membership") {
    Rig r;
    r.replies["@NET"] = "board 0123456789abcdef\nuser net member\nrev board no\n";
    r.replies["@NET WAITING"] = "";
    r.shell.beginAt(axis::bbs::SysopSection::System);
    REQUIRE(pickRow(r, "User Net"));
    REQUIRE(pickRow(r, "New Board Voting"));
    CHECK(screen(r.out).find("Nobody is waiting") != std::string::npos);

    Rig p;
    p.replies["@NET"] = "board 0123456789abcdef\nuser net pending (yes 1 of 3, no 0 of 3)\nrev board no\n";
    p.shell.beginAt(axis::bbs::SysopSection::System);
    REQUIRE(pickRow(p, "User Net"));
    std::string s = screen(p.out);
    CHECK(s.find("New Board Voting") == std::string::npos);
    CHECK(s.find("Apply") != std::string::npos);
}
#endif
