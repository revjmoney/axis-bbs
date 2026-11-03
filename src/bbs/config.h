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
struct FileArea {
    std::string id, name;
};

std::vector<FileArea> defaultFileAreas();  // Rev's list (design spec, file areas table)

struct BbsConfig {
    std::string sysopHandle, sysopSalt, sysopHash, sysopRealName;
    std::string bbsName = "Axis Underground CNC", location, boardName = "axis", publicHost;
    int         telnetPort = 23;
    std::string timezone = "EASTERN";
    std::string ftnAddress = "99:1/0";
    int         newUserSecurity = 10, dailyMinutes = 60, idleMinutes = 5;
    bool        setupDone = false;
    // "doorN=id|name|exe|args|drop"; LORD unless the settings list doors
    std::vector<DoorEntry> doors{{"lord", "Legend of the Red Dragon", "LORD.EXE", "1 /DREW", "doorsys"}};
    std::vector<FileArea>  areas = defaultFileAreas();  // "areaN=id|name"; the defaults unless listed
    std::vector<std::pair<std::string, std::string>> unknown;  // keys from newer firmware, kept on save
};

BbsConfig   parseConfig(const std::string& ini);
std::string formatConfig(const BbsConfig& c);
const char* tzRule(const std::string& name);  // POSIX TZ rule, "" if unknown
const std::vector<std::string>& tzNames();

}  // namespace axis::bbs
