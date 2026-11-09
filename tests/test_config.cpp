// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/config.h"

using namespace axis::bbs;

TEST_CASE("config round trip keeps every field") {
    BbsConfig c;
    c.sysopHandle = "Rev";
    c.sysopSalt = "00112233445566778899aabbccddeeff";
    c.sysopHash = "abcd";
    c.sysopRealName = "Rev J Money";
    c.bbsName = "Axis Underground CNC";
    c.location = "Raleigh, NC";
    c.boardName = "axis";
    c.publicHost = "bbs.example.com";
    c.telnetPort = 2323;
    c.timezone = "PACIFIC";
    c.ftnAddress = "99:1/0";
    c.newUserSecurity = 20;
    c.dailyMinutes = 90;
    c.idleMinutes = 7;
    c.setupDone = true;
    BbsConfig d = parseConfig(formatConfig(c));
    CHECK(d.sysopHandle == "Rev");
    CHECK(d.sysopSalt == c.sysopSalt);
    CHECK(d.sysopHash == "abcd");
    CHECK(d.sysopRealName == "Rev J Money");
    CHECK(d.location == "Raleigh, NC");
    CHECK(d.publicHost == "bbs.example.com");
    CHECK(d.telnetPort == 2323);
    CHECK(d.timezone == "PACIFIC");
    CHECK(d.newUserSecurity == 20);
    CHECK(d.dailyMinutes == 90);
    CHECK(d.idleMinutes == 7);
    CHECK(d.setupDone);
}

TEST_CASE("config missing keys get defaults") {
    BbsConfig d = parseConfig("bbs_name=Other Board\n");
    CHECK(d.bbsName == "Other Board");
    CHECK(d.boardName == "axis");
    CHECK(d.telnetPort == 23);
    CHECK(d.timezone == "EASTERN");
    CHECK(d.ftnAddress == "99:1/0");
    CHECK(d.newUserSecurity == 10);
    CHECK(d.dailyMinutes == 60);
    CHECK(d.idleMinutes == 5);
}

TEST_CASE("config setupDone only after save") {
    CHECK_FALSE(parseConfig("bbs_name=X\n").setupDone);
    CHECK(parseConfig("setup_done=1\n").setupDone);
}

TEST_CASE("config unknown keys survive save") {
    BbsConfig d = parseConfig("future_option=on\nbbs_name=X\n");
    std::string out = formatConfig(d);
    CHECK(out.find("future_option=on") != std::string::npos);
}

TEST_CASE("config values with equals signs and spaces survive, CRLF accepted") {
    BbsConfig d = parseConfig("location= Somewhere = odd \r\nbbs_name=A B\r\n");
    CHECK(d.location == " Somewhere = odd ");
    CHECK(d.bbsName == "A B");
}

TEST_CASE("timezone rules") {
    CHECK(std::string(tzRule("EASTERN")) == "EST5EDT,M3.2.0,M11.1.0");
    CHECK(std::string(tzRule("nowhere")).empty());
    CHECK(tzNames().size() == 10);
}

TEST_CASE("config has LORD as the default door") {
    auto c = axis::bbs::parseConfig("");
    REQUIRE(c.doors.size() == 4);
    CHECK(c.doors[0].id == "lord");
    CHECK(c.doors[1].id == "blackjack");
    CHECK(c.doors[2].exe == "*ANSIVIEW");  // the built-in ANSI Pack Viewer
    CHECK(c.doors[3].exe == "*SCRIPT");  // programs from /scripts on the card
    CHECK(c.doors[0].args == "%N NOEMS /DREW");  // %N: the node number; NOEMS: LORD must not look for EMS
}

TEST_CASE("stock LORD arguments gain NOEMS; custom tails and other doors stay") {
    DoorEntry stock{"lord", "Legend of the Red Dragon", "LORD.EXE", "%N /DREW", "doorsys"};
    CHECK(migrateLordArgs(stock));
    CHECK(stock.args == "%N NOEMS /DREW");
    DoorEntry old{"lord", "Legend of the Red Dragon", "lord.exe", "1 /DREW", "doorsys"};
    CHECK(migrateLordArgs(old));
    CHECK(old.args == "%N NOEMS /DREW");
    DoorEntry lower{"lord", "x", "LORD.EXE", "%n /DREW", "doorsys"};
    CHECK(migrateLordArgs(lower));
    CHECK(lower.args == "%N NOEMS /DREW");
    DoorEntry already{"lord", "x", "LORD.EXE", "%N NOEMS /DREW", "doorsys"};
    CHECK_FALSE(migrateLordArgs(already));
    CHECK(already.args == "%N NOEMS /DREW");
    DoorEntry local{"lord", "x", "LORD.EXE", "0 NOEMS /DREW", "doorsys"};
    CHECK_FALSE(migrateLordArgs(local));
    DoorEntry custom{"lord", "x", "LORD.EXE", "%N /DREW /FAST", "doorsys"};
    CHECK_FALSE(migrateLordArgs(custom));
    CHECK(custom.args == "%N /DREW /FAST");
    DoorEntry jack{"blackjack", "BBS Black Jack", "BLKJK.EXE", "BJ.CFG", "doorsys"};
    CHECK_FALSE(migrateLordArgs(jack));
    CHECK(jack.args == "BJ.CFG");
}

TEST_CASE("config doors round trip") {
    auto c = axis::bbs::parseConfig("door1=bre|Barren Realms Elite|BRE.EXE|/N1|dorinfo\n");
    REQUIRE(c.doors.size() == 3);  // plus the script door and native LORD
    CHECK(c.doors[0].name == "Barren Realms Elite");
    CHECK(c.doors[0].drop == "dorinfo");
    bool script = false, native = false, dos = false;
    for (auto& d0 : c.doors) {
        script = script || d0.exe == "*SCRIPT";
        native = native || d0.exe == "*LORD";
        dos = dos || d0.exe == "*DOSSHELL";
    }
    CHECK(script);
    CHECK(native);
    CHECK_FALSE(dos);
    auto d = axis::bbs::parseConfig(axis::bbs::formatConfig(c));
    REQUIRE(d.doors.size() == 3);
    CHECK(d.doors[0].exe == "BRE.EXE");
}

TEST_CASE("config has the default file areas, music and CAD/CAM included") {
    auto c = parseConfig("");
    auto has = [&](const std::string& name) {
        for (auto& a : c.areas)
            if (a.name == name) return true;
        return false;
    };
    CHECK(c.areas.size() == 19);
    CHECK(has("ANSI Packs"));
    CHECK(c.areas[0].name == "Axis Micro BBS Firmware");  // our own area comes first
    CHECK(has("RotatoCAM: Pro"));
    CHECK(has("Abandonware"));
    CHECK(has("CAD/CAM Software"));
    CHECK(has("Post Processors"));
    CHECK(has("BBS Software"));
    CHECK(has("Music: Rev J Money"));
    CHECK(has("Music: Other"));
}

TEST_CASE("config file areas round trip and replace the defaults") {
    auto c = parseConfig("area1=gcode|G-code and Toolpaths\narea2=music-rev|Music: Rev J Money\n");
    REQUIRE(c.areas.size() == 2);
    CHECK(c.areas[1].id == "music-rev");
    auto d = parseConfig(formatConfig(c));
    REQUIRE(d.areas.size() == 2);
    CHECK(d.areas[0].name == "G-code and Toolpaths");
}

TEST_CASE("file areas sort by number and a repeated number replaces the earlier one") {
    auto c = parseConfig("area2=gcode|G-code\narea1=text|Text\narea2=models|3D Models\narea0=axis|Axis Micro BBS Firmware\n");
    REQUIRE(c.areas.size() == 3);
    CHECK(c.areas[0].id == "axis");
    CHECK(c.areas[1].id == "text");
    CHECK(c.areas[2].id == "models");
}

TEST_CASE("the DOS prompt password hash survives a save, and the script door is always listed") {
    BbsConfig c;
    c.dosSalt = "00112233445566778899aabbccddeeff";
    c.dosHash = "abc123";
    c.doors = {{"lord", "Legend of the Red Dragon", "LORD.EXE", "1 /DREW", "doorsys"},
               {"dos", "DOS Prompt (password)", "*DOSSHELL", "dosbox", "none"}};
    BbsConfig back = parseConfig(formatConfig(c));
    CHECK(back.dosSalt == c.dosSalt);
    CHECK(back.dosHash == "abc123");
    bool script = false, native = false, dos = false;
    for (auto& d : back.doors) {
        script = script || d.exe == "*SCRIPT";
        native = native || d.exe == "*LORD";
        dos = dos || d.exe == "*DOSSHELL";
    }
    CHECK(script);
    CHECK(native);
    CHECK_FALSE(dos);
    CHECK(back.doors.size() == 3);
    CHECK(parseConfig(formatConfig(back)).doors.size() == 3);  // listed once, not again on every save
}

TEST_CASE("a board takes two callers at once unless the settings say otherwise, and never more than four") {
    CHECK(parseConfig("").sessions == 2);
    CHECK(parseConfig("sessions=3\n").sessions == 3);
    CHECK(parseConfig("sessions=9\n").sessions == 4);
    CHECK(parseConfig("sessions=0\n").sessions == 1);
    BbsConfig c;
    c.sessions = 3;
    CHECK(parseConfig(formatConfig(c)).sessions == 3);
}

TEST_CASE("a follower takes the main's settings but keeps its own name, session count and port") {
    BbsConfig mine;
    mine.boardName = "axis2";
    mine.sessions = 3;
    mine.telnetPort = 2323;
    mine.location = "old place";
    BbsConfig main;
    main.boardName = "axis";
    main.location = "Myrtle Beach, SC, USA";
    main.sysopHandle = "Rev";
    main.setupDone = true;
    BbsConfig got = mergeSettings(mine, formatConfig(main));
    CHECK(got.location == "Myrtle Beach, SC, USA");
    CHECK(got.sysopHandle == "Rev");
    CHECK(got.boardName == "axis2");
    CHECK(got.sessions == 3);
    CHECK(got.telnetPort == 2323);
    CHECK(got.setupDone);
}

TEST_CASE("the login splash is on unless the settings turn it off") {
    CHECK(parseConfig("").loginSplash);
    CHECK_FALSE(parseConfig("login_splash=0\n").loginSplash);
    BbsConfig c = parseConfig("login_splash=0\n");
    CHECK_FALSE(parseConfig(formatConfig(c)).loginSplash);
}

TEST_CASE("the look is axis, cyan, or green, and an old name still loads") {
    CHECK(parseConfig("").theme == "axis");
    BbsConfig c = parseConfig("theme=oblivion\n");
    CHECK(c.theme == "cyan");
    CHECK(wfcFolder(c) == "cyan");
    CHECK(parseConfig(formatConfig(c)).theme == "cyan");
    CHECK(mergeSettings(parseConfig(""), formatConfig(c)).theme == "cyan");  // one look for the whole cluster
    CHECK(parseConfig("theme=INIQUITY\n").theme == "green");
    CHECK(wfcFolder(parseConfig("theme=green\n")) == "green");
    CHECK(parseConfig("theme=nope\n").theme == "axis");
    CHECK(parseConfig("theme=skulls\n").theme == "skulls");
    CHECK(wfcFolder(parseConfig("theme=skulls\n")) == "skulls");
    CHECK(parseConfig("theme=CRT\n").theme == "crt");
    CHECK(parseConfig("wfc=wasteland\n").theme == "axis");  // wfc no longer sets the theme
    CHECK(wfcFolder(parseConfig("wfc=wasteland\n")) == "wasteland");
}

TEST_CASE("wfc=classic is the built-in waiting screen, whatever the theme") {
    BbsConfig c = parseConfig("theme=crt\nwfc=classic\n");
    CHECK(c.wfc == "classic");
    CHECK(wfcFolder(c).empty());  // no folder: the built-in art
    CHECK(parseConfig(formatConfig(c)).wfc == "classic");
    CHECK(parseConfig("theme=crt\nwfc=CLASSIC\n").wfc == "classic");
}

TEST_CASE("the waiting screen skin (wfc) can differ from the theme") {
    BbsConfig c = parseConfig("theme=sea\nwfc=skulls\n");
    CHECK(c.theme == "sea");
    CHECK(c.wfc == "skulls");
    CHECK(wfcFolder(c) == "skulls");
    CHECK(parseConfig(formatConfig(c)).wfc == "skulls");
    CHECK(mergeSettings(parseConfig(""), formatConfig(c)).wfc == "skulls");
}

TEST_CASE("an old ini's wfc=<the theme> follows the theme after the upgrade; a different skin stays") {
    BbsConfig c = parseConfig("wfc=green\ntheme=green\n");  // what older firmware wrote, in either order
    CHECK(c.wfc.empty());
    CHECK(parseConfig("theme=green\nwfc=green\n").wfc.empty());
    CHECK(parseConfig("theme=iniquity\nwfc=green\n").wfc.empty());  // an old theme name is the same look
    c.theme = "skulls";  // the sysop changes the theme later: the waiting screen follows it
    CHECK(wfcFolder(parseConfig(formatConfig(c))) == "skulls");
    BbsConfig rev = parseConfig("theme=green\nwfc=cyan\n");  // board 1: cyan on purpose, theme green
    CHECK(rev.wfc == "cyan");
    CHECK(wfcFolder(rev) == "cyan");
    CHECK(parseConfig(formatConfig(rev)).wfc == "cyan");
}

TEST_CASE("a blank or unknown wfc follows the theme") {
    BbsConfig b = parseConfig("theme=sea\nwfc=\n");
    CHECK(b.wfc.empty());
    CHECK(wfcFolder(b) == "sea");
    CHECK(formatConfig(b).find("wfc=\n") != std::string::npos);
    CHECK(parseConfig(formatConfig(b)).wfc.empty());
    BbsConfig n = parseConfig("theme=sea\nwfc=nosuch\n");
    CHECK(wfcFolder(n) == "sea");
    CHECK(parseConfig("").wfc.empty());
    CHECK(wfcFolder(parseConfig("")) == "axis");
    CHECK(parseConfig("wfc=Skulls\n").wfc == "skulls");
    CHECK(parseConfig("wfc=oblivion\n").wfc == "cyan");  // an old name canonicalises
    CHECK(wfcFolder(parseConfig("theme=sea\nwfc=skulls\nwfc=\n")) == "sea");  // a later blank resets it
    CHECK(wfcFolder(parseConfig("theme=sea\nwfc=skulls\ntheme=crt\n")) == "skulls");
}

TEST_CASE("an area can need a security level: id|name|level, kept when saved") {
    BbsConfig c = parseConfig("area1=axis|Axis\narea2=zero|Zero Day|150\nmsgarea1=general|General\nmsgarea2=elite|Elite Talk|100\n");
    REQUIRE(c.areas.size() == 2);
    CHECK(c.areas[0].minSecurity == 0);
    CHECK(c.areas[1].minSecurity == 150);
    CHECK(c.msgAreas[1].minSecurity == 100);
    BbsConfig back = parseConfig(formatConfig(c));
    CHECK(back.areas[1].minSecurity == 150);
    CHECK(back.areas[1].name == "Zero Day");
    CHECK(back.msgAreas[1].minSecurity == 100);
}

TEST_CASE("an area can be mirrored onto two SD cards: id|name|level|m, kept when saved") {
    BbsConfig c = parseConfig("area1=axis|Axis||m\narea2=zero|Zero Day|150|m\narea3=gcode|G-code|20\narea4=text|Text\n");
    REQUIRE(c.areas.size() == 4);
    CHECK(c.areas[0].mirror);
    CHECK(c.areas[0].minSecurity == 0);
    CHECK(c.areas[1].mirror);
    CHECK(c.areas[1].minSecurity == 150);
    CHECK(c.areas[1].name == "Zero Day");
    CHECK_FALSE(c.areas[2].mirror);
    CHECK(c.areas[2].minSecurity == 20);
    CHECK_FALSE(c.areas[3].mirror);
    CHECK(areaText(c.areas[0]) == "axis|Axis|0|m");
    CHECK(areaText(c.areas[1]) == "zero|Zero Day|150|m");
    CHECK(areaText(c.areas[2]) == "gcode|G-code|20");  // unmirrored areas save as before
    CHECK(areaText(c.areas[3]) == "text|Text");
    BbsConfig back = parseConfig(formatConfig(c));
    CHECK(back.areas[0].mirror);
    CHECK(back.areas[1].mirror);
    CHECK(back.areas[1].minSecurity == 150);
    CHECK_FALSE(back.areas[2].mirror);
}

TEST_CASE("a mirrored area with no level: id|name|m") {
    BbsConfig c = parseConfig("area1=axis|Axis|m\n");
    REQUIRE(c.areas.size() == 1);
    CHECK(c.areas[0].mirror);
    CHECK(c.areas[0].minSecurity == 0);
    CHECK(c.areas[0].name == "Axis");
}

TEST_CASE("the main menu's items are data: hotkey, name and command, kept in order") {
    BbsConfig c = parseConfig("menu.main.2=F|File Vaults|files\nmenu.main.1=X|Message Bases|messages\n");
    REQUIRE(c.mainMenu.size() == 2);
    CHECK(c.mainMenu[0].key == "X");
    CHECK(c.mainMenu[0].name == "Message Bases");
    CHECK(c.mainMenu[0].cmd == "messages");
    CHECK(c.mainMenu[1].cmd == "files");
    std::string t = formatConfig(c);
    CHECK(t.find("menu.main.1=X|Message Bases|messages") != std::string::npos);
    CHECK(parseConfig(t).mainMenu.size() == 2);
    CHECK(parseConfig("").mainMenu.empty());  // none set: the built-in menu
}

TEST_CASE("every menu command has the board's own key for it") {
    CHECK(menuCommandKey("messages") == 'M');
    CHECK(menuCommandKey("files") == 'F');
    CHECK(menuCommandKey("goodbye") == 'G');
    CHECK(menuCommandKey("users") == 'H');
    CHECK(menuCommandKey("sysop") == '*');
    CHECK(menuCommandKey("nonsense") == 0);
    CHECK(menuCommands().size() >= 18);
}

TEST_CASE("section switches default on and round trip") {
    CHECK(parseConfig("").messagesOn);
    CHECK(parseConfig("").filesOn);
    CHECK(parseConfig("").doorsOn);
    BbsConfig off = parseConfig("messages_on=0\nfiles_on=0\ndoors_on=0\n");
    CHECK_FALSE(off.messagesOn);
    CHECK_FALSE(off.filesOn);
    CHECK_FALSE(off.doorsOn);
    BbsConfig back = parseConfig(formatConfig(off));
    CHECK_FALSE(back.messagesOn);
    CHECK_FALSE(back.filesOn);
    CHECK_FALSE(back.doorsOn);
    CHECK(parseConfig(formatConfig(BbsConfig{})).messagesOn);
}

TEST_CASE("a LORD.EXE row with no on/off flag is off, and a later door number replaces") {
    BbsConfig c = parseConfig("door1=lord|Legend of the Red Dragon|LORD.EXE|%N NOEMS /DREW|doorsys\n"
                              "door2=lord|LORD Fast|FAST.EXE|%N|doorsys\n"
                              "door3=four|AXIS FOUR|FOUR.EXE|%N|doorsys\n"
                              "door1=lord|Legend of the Red Dragon|lord.exe|%N NOEMS /DREW|doorsys\n");
    REQUIRE(c.doors.size() >= 4);
    CHECK(c.doors[0].exe == "lord.exe");
    CHECK_FALSE(c.doors[0].enabled);
    CHECK(c.doors[1].exe == "FAST.EXE");
    CHECK(c.doors[1].enabled);
    CHECK(c.doors[2].exe == "FOUR.EXE");
    CHECK(c.doors[2].enabled);
    BbsConfig saved = parseConfig(formatConfig(c));
    CHECK_FALSE(saved.doors[0].enabled);
    CHECK(saved.doors[1].enabled);
    BbsConfig on = parseConfig("door1=lord|Legend of the Red Dragon|LORD.EXE|%N NOEMS /DREW|doorsys|1\n");
    CHECK(on.doors[0].enabled);
    CHECK(on.doors[0].exe == "LORD.EXE");
}

TEST_CASE("sysop_telnet defaults on and round trips") {
    CHECK(parseConfig("").sysopTelnet);
    BbsConfig off = parseConfig("sysop_telnet=0\n");
    CHECK_FALSE(off.sysopTelnet);
    CHECK_FALSE(parseConfig(formatConfig(off)).sysopTelnet);
    CHECK(parseConfig(formatConfig(BbsConfig{})).sysopTelnet);
}
