// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// BBS settings from the first-boot setup, stored as key=value lines (axis.ini).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

// A door on the Doors menu. Its files live in /doors/<id>/ on the board's storage.
struct DoorEntry {
    std::string id, name, exe, args, drop = "doorsys";  // drop: "doorsys" (DOOR.SYS) or "dorinfo"
};

// A file area. Its files live in /files/<id>/ on the SD card.
struct FileArea;
std::string areaText(const FileArea& a);  // "id|name", "id|name|level" or "id|name|level|m", as the settings keep it

struct FileArea {
    std::string id, name;
    int         minSecurity = 0;  // callers below this level don't see the area ("areaN=id|name|level")
    bool        mirror = false;   // "areaN=id|name|level|m": every file kept on two SD cards (cluster/mirrorjob.h)
};

std::vector<FileArea> defaultFileAreas();  // Rev's list (design spec, file areas table)

struct BbsConfig {
    std::string sysopHandle, sysopSalt, sysopHash, sysopRealName;
    std::string dosSalt, dosHash;  // the DOS Prompt door password (set at the serial console: @DOSPASS)
    std::string bbsName = "Axis Underground CNC", location, boardName = "axis", publicHost;
    int         telnetPort = 23;
    std::string timezone = "EASTERN";
    std::string ftnAddress = "99:1/0";
    int         newUserSecurity = 10, dailyMinutes = 60, idleMinutes = 5;
    int         sessions = 2;
    int         ratio = 0, freeKb = 0;  // downloads up to ratio x uploads + freeKb ("ratio", "free_kb"; 0 = no ratio)
    std::string theme = "axis";  // the look: "axis" (drippy orange) or "oblivion" (stock Oblivion/2 style) ("theme")
    bool        loginSplash = false;  // Rev's RotatoCAM ANSI splash before the login screen ("login_splash", on unless 0)  // callers this board takes at once (1 to 4); doors and transfers one at a time
    bool        setupDone = false;
    // "doorN=id|name|exe|args|drop"; LORD unless the settings list doors
    // an exe starting with '*' is a door built into the firmware ("*ANSIVIEW": args = the area it shows)
    std::vector<DoorEntry> doors{{"lord", "Legend of the Red Dragon", "LORD.EXE", "%N /DREW", "doorsys"},
                                 {"blackjack", "BBS Black Jack", "BLKJK.EXE", "BJ.CFG", "doorsys"},
                                 {"ansiview", "ANSI Pack Viewer", "*ANSIVIEW", "ansi", "none"},
                                 {"dos", "DOS Prompt (password)", "*DOSSHELL", "dosbox", "none"}};
    // "*DOSSHELL": args = its sandbox folder on the SD card; always listed (added if a saved list lacks it)
    std::vector<FileArea>  areas = defaultFileAreas();
    std::vector<FileArea>  msgAreas{{"general", "General Chatter"}, {"cnc", "CNC Shop Talk"}, {"rotatocam", "RotatoCAM"},
                                   {"scene", "BBS Scene & ANSI"}, {"sysop", "Ask the Sysop"}};  // "msgareaN=id|name"  // "areaN=id|name"; the defaults unless listed
    std::vector<std::pair<std::string, std::string>> unknown;  // keys from newer firmware, kept on save
};

BbsConfig   parseConfig(const std::string& ini);
std::string formatConfig(const BbsConfig& c);
// A cluster follower's settings: the main's (one BBS), keeping this board's own name, session count
// and telnet port; set up, since the main was.
BbsConfig   mergeSettings(const BbsConfig& mine, const std::string& mainIni);
const char* tzRule(const std::string& name);  // POSIX TZ rule, "" if unknown
const std::vector<std::string>& tzNames();

}  // namespace axis::bbs
