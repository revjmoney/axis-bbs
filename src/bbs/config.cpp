// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/config.h"
#include "bbs/theme.h"

#include <cctype>
#include <cstdlib>
#include <map>

namespace axis::bbs {

namespace {
// Same zones and rules as the CYD Demoscene clock.
const std::vector<std::pair<std::string, std::string>>& zones() {
    static const std::vector<std::pair<std::string, std::string>> z = {
        {"EASTERN", "EST5EDT,M3.2.0,M11.1.0"}, {"CENTRAL", "CST6CDT,M3.2.0,M11.1.0"},
        {"MOUNTAIN", "MST7MDT,M3.2.0,M11.1.0"}, {"ARIZONA", "MST7"},
        {"PACIFIC", "PST8PDT,M3.2.0,M11.1.0"}, {"ALASKA", "AKST9AKDT,M3.2.0,M11.1.0"},
        {"HAWAII", "HST10"}, {"UTC", "UTC0"},
        {"UK", "GMT0BST,M3.5.0/1,M10.5.0"}, {"CENTRAL EUROPE", "CET-1CEST,M3.5.0,M10.5.0/3"}};
    return z;
}
}  // namespace

const char* tzRule(const std::string& name) {
    for (auto& z : zones())
        if (z.first == name) return z.second.c_str();
    return "";
}

const std::vector<std::string>& tzNames() {
    static std::vector<std::string> v;
    if (v.empty())
        for (auto& z : zones()) v.push_back(z.first);
    return v;
}

std::vector<FileArea> defaultFileAreas() {
    return {{"axis", "Axis Micro BBS Firmware"},  // this firmware, the BBS, our door code
            {"rotatocam-pro", "RotatoCAM: Pro"},
            {"rotatocam-free", "RotatoCAM: Free"},
            {"cnc-free", "CNC Software: Freeware"},
            {"cnc-share", "CNC Software: Shareware/Trials"},
            {"cnc-demos", "CNC Software: Pro Demos"},
            {"gcode", "G-code and Toolpaths"},
            {"models", "3D Models"},
            {"text", "Text Files"},
            {"ansi", "ANSI Packs"},
            {"freeware", "Freeware"},
            {"shareware", "Shareware and Free Trials"},
            {"abandonware", "Abandonware"},
            {"doors", "BBS Doors"},
            {"cadcam", "CAD/CAM Software"},
            {"posts", "Post Processors"},
            {"bbs", "BBS Software"},
            {"music-rev", "Music: Rev J Money"},
            {"music-other", "Music: Other"}};
}

namespace {
bool areaFrom(const std::string& v, FileArea& a);
}  // namespace

BbsConfig parseConfig(const std::string& ini) {
    BbsConfig              c;
    c.loginSplash = true;  // on unless the settings say login_splash=0
    c.messagesOn = c.filesOn = c.doorsOn = true;  // on unless messages_on / files_on / doors_on is 0
    std::map<int, DoorEntry> doorByNum;  // by number: a repeated doorN replaces, so @SET doorN= edits that row
    std::map<int, FileArea> areas;  // by number: sorted, and a repeated number replaces
    std::map<int, MenuItem> menu;  // "menu.main.N=..."
    std::map<int, FileArea> msgAreas;  // "msgareaN=id|name"
    size_t pos = 0;
    while (pos < ini.size()) {
        size_t end = ini.find('\n', pos);
        if (end == std::string::npos) end = ini.size();
        std::string line = ini.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        int n = std::atoi(v.c_str());
        if (k == "sysop_handle") c.sysopHandle = v;
        else if (k == "sysop_salt") c.sysopSalt = v;
        else if (k == "sysop_hash") c.sysopHash = v;
        else if (k == "sysop_real_name") c.sysopRealName = v;
        else if (k == "dospass_salt") c.dosSalt = v;
        else if (k == "dospass_hash") c.dosHash = v;
        else if (k == "nup_salt") c.nupSalt = v;
        else if (k == "nup_hash") c.nupHash = v;
        else if (k == "bbs_name") c.bbsName = v;
        else if (k == "location") c.location = v;
        else if (k == "board_name") c.boardName = v;
        else if (k == "public_host") c.publicHost = v;
        else if (k == "telnet_port") c.telnetPort = n;
        else if (k == "timezone") c.timezone = v;
        else if (k == "ftn_address") c.ftnAddress = v;
        else if (k == "new_user_security") c.newUserSecurity = n;
        else if (k == "daily_minutes") c.dailyMinutes = n;
        else if (k == "idle_minutes") c.idleMinutes = n;
        else if (k == "login_splash") c.loginSplash = n != 0;
        else if (k == "messages_on") c.messagesOn = n != 0;
        else if (k == "files_on") c.filesOn = n != 0;
        else if (k == "doors_on") c.doorsOn = n != 0;
        else if (k == "sysop_telnet") c.sysopTelnet = n != 0;
        else if (k == "ratio") c.ratio = (int)n;
        else if (k == "free_kb") c.freeKb = (int)n;
        else if (k == "theme") {
            if (const char* id = theme::knownLook(v)) c.theme = id;
        } else if (k == "wfc") {  // a look id, or blank to follow the theme. Anything else is ignored.
            if (v.empty()) c.wfc.clear();
            else if (const char* id = theme::knownLook(v)) c.wfc = id;
        }
        else if (k == "sessions") c.sessions = n < 1 ? 1 : n > 4 ? 4 : n;
        else if (k == "setup_done") c.setupDone = n == 1;
        else if (k.rfind("door", 0) == 0 && k.size() > 4 && std::isdigit((unsigned char)k[4])) {
            std::vector<std::string> f;
            for (size_t a = 0;;) {
                size_t bar = v.find('|', a);
                f.push_back(v.substr(a, bar == std::string::npos ? std::string::npos : bar - a));
                if (bar == std::string::npos) break;
                a = bar + 1;
            }
            if (f.size() >= 3 && !f[0].empty()) {
                DoorEntry d{f[0], f[1], f[2], f.size() > 3 ? f[3] : "", f.size() > 4 && !f[4].empty() ? f[4] : "doorsys"};
                if (f.size() > 5 && !f[5].empty()) d.enabled = f[5] != "0";
                else if (lordGameExe(d.exe)) d.enabled = false;  // original LORD stays off until the sysop turns that row on
                doorByNum[std::atoi(k.c_str() + 4)] = std::move(d);
            }
        } else if (k.rfind("msgarea", 0) == 0 && k.size() > 7 && std::isdigit((unsigned char)k[7])) {
            if (FileArea a; areaFrom(v, a)) msgAreas[std::atoi(k.c_str() + 7)] = a;
        } else if (k.rfind("area", 0) == 0 && k.size() > 4 && std::isdigit((unsigned char)k[4])) {
            if (FileArea a; areaFrom(v, a)) areas[std::atoi(k.c_str() + 4)] = a;
        } else if (k.rfind("menu.main.", 0) == 0 && k.size() > 10 && std::isdigit((unsigned char)k[10])) {
            size_t b1 = v.find('|'), b2 = b1 == std::string::npos ? b1 : v.find('|', b1 + 1);
            if (b2 != std::string::npos && b1 > 0) menu[std::atoi(k.c_str() + 10)] = {v.substr(0, b1), v.substr(b1 + 1, b2 - b1 - 1), v.substr(b2 + 1)};
        } else c.unknown.emplace_back(k, v);
    }
    for (auto& m : menu) c.mainMenu.push_back(m.second);
    std::vector<DoorEntry> listed;
    if (doorByNum.empty()) listed = c.doors;  // none in the file: keep the default list
    else
        for (auto& e : doorByNum) listed.push_back(e.second);
    bool script = false, native = false;
    std::vector<DoorEntry> kept;
    for (auto& d : listed) {
        if (d.exe == "*DOSSHELL") continue;  // the DOS prompt is out of the firmware
        script = script || d.exe == "*SCRIPT";
        native = native || d.exe == "*LORD";
        kept.push_back(std::move(d));
    }
    // A saved door list gets the native game even when the old LORD.EXE row is still there.
    if (!doorByNum.empty() && !native) kept.push_back({"lord", "LORD", "*LORD", "", "none"});
    if (!script) kept.push_back({"script", "Scripts", "*SCRIPT", "", "none"});
    c.doors = std::move(kept);
    if (!msgAreas.empty()) {
        c.msgAreas.clear();
        for (auto& a : msgAreas) c.msgAreas.push_back(a.second);
    }
    if (!areas.empty()) {
        c.areas.clear();
        for (auto& a : areas) c.areas.push_back(a.second);
    }
    if (const char* id = theme::knownLook(c.theme)) c.theme = id;
    else c.theme = "axis";
    return c;
}

std::string wfcFolder(const BbsConfig& c) {
    const char* id = theme::knownLook(c.wfc);
    return id ? id : c.theme;
}

namespace {
bool areaFrom(const std::string& v, FileArea& a) {  // "id|name", "id|name|level" or "id|name|level|m"
    size_t bar = v.find('|');
    if (bar == std::string::npos || bar == 0) return false;
    size_t bar2 = v.find('|', bar + 1);
    size_t bar3 = bar2 == std::string::npos ? std::string::npos : v.find('|', bar2 + 1);
    a.id = v.substr(0, bar);
    a.name = v.substr(bar + 1, bar2 == std::string::npos ? std::string::npos : bar2 - bar - 1);
    a.minSecurity = bar2 == std::string::npos ? 0 : std::atoi(v.c_str() + bar2 + 1);
    a.mirror = bar3 != std::string::npos && (v[bar3 + 1] == 'm' || v[bar3 + 1] == 'M');
    if (bar2 != std::string::npos && bar3 == std::string::npos && (v.substr(bar2 + 1) == "m" || v.substr(bar2 + 1) == "M"))
        a.mirror = true;  // "id|name|m": mirrored, no level
    return true;
}
}  // namespace

std::string areaText(const FileArea& a) {
    if (a.mirror) return a.id + "|" + a.name + "|" + std::to_string(a.minSecurity) + "|m";
    return a.id + "|" + a.name + (a.minSecurity ? "|" + std::to_string(a.minSecurity) : "");
}

BbsConfig mergeSettings(const BbsConfig& mine, const std::string& mainIni) {
    BbsConfig c = parseConfig(mainIni);
    c.boardName = mine.boardName;
    c.sessions = mine.sessions;
    c.telnetPort = mine.telnetPort;
    c.setupDone = true;
    return c;
}

std::string formatConfig(const BbsConfig& c) {
    std::string s = "# Axis Micro BBS Firmware settings (written by the board; safe to edit)\n";
    auto kv = [&](const char* k, const std::string& v) { s += std::string(k) + "=" + v + "\n"; };
    kv("bbs_name", c.bbsName);
    kv("sysop_handle", c.sysopHandle);
    kv("sysop_salt", c.sysopSalt);
    kv("sysop_hash", c.sysopHash);
    kv("sysop_real_name", c.sysopRealName);
    kv("dospass_salt", c.dosSalt);
    kv("dospass_hash", c.dosHash);
    kv("nup_salt", c.nupSalt);
    kv("nup_hash", c.nupHash);
    kv("location", c.location);
    kv("board_name", c.boardName);
    kv("public_host", c.publicHost);
    kv("telnet_port", std::to_string(c.telnetPort));
    kv("timezone", c.timezone);
    kv("ftn_address", c.ftnAddress);
    kv("new_user_security", std::to_string(c.newUserSecurity));
    kv("daily_minutes", std::to_string(c.dailyMinutes));
    kv("idle_minutes", std::to_string(c.idleMinutes));
    kv("sessions", std::to_string(c.sessions));
    kv("login_splash", c.loginSplash ? "1" : "0");
    kv("messages_on", c.messagesOn ? "1" : "0");
    kv("files_on", c.filesOn ? "1" : "0");
    kv("doors_on", c.doorsOn ? "1" : "0");
    kv("sysop_telnet", c.sysopTelnet ? "1" : "0");
    kv("theme", c.theme);
    kv("wfc", c.wfc);
    kv("ratio", std::to_string(c.ratio));
    kv("free_kb", std::to_string(c.freeKb));
    for (size_t i = 0; i < c.doors.size(); i++) {
        const DoorEntry& d = c.doors[i];
        kv(("door" + std::to_string(i + 1)).c_str(),
           d.id + "|" + d.name + "|" + d.exe + "|" + d.args + "|" + d.drop + "|" + (d.enabled ? "1" : "0"));
    }
    for (size_t i = 0; i < c.areas.size(); i++)
        kv(("area" + std::to_string(i + 1)).c_str(), areaText(c.areas[i]));
    for (size_t i = 0; i < c.msgAreas.size(); i++)
        kv(("msgarea" + std::to_string(i + 1)).c_str(), areaText(c.msgAreas[i]));
    for (size_t i = 0; i < c.mainMenu.size(); i++)
        kv(("menu.main." + std::to_string(i + 1)).c_str(), c.mainMenu[i].key + "|" + c.mainMenu[i].name + "|" + c.mainMenu[i].cmd);
    for (auto& u : c.unknown) kv(u.first.c_str(), u.second);
    kv("setup_done", c.setupDone ? "1" : "0");
    return s;
}

const std::vector<std::pair<std::string, char>>& menuCommands() {
    static const std::vector<std::pair<std::string, char>> v = {
        {"messages", 'M'}, {"files", 'F'},    {"doors", 'D'},    {"teleconference", 'T'}, {"who", 'W'},     {"page", 'P'},
        {"account", 'U'},  {"oneliners", 'O'}, {"multinode", 'N'}, {"lastcallers", 'L'},   {"users", 'H'},   {"email", 'E'},
        {"scan", 'S'},
        {"rumors", 'R'},   {"bbslist", 'B'},  {"axisnet", 'J'}, {"topten", 'K'},   {"voting", 'V'},         {"info", 'I'},    {"credits", 'C'},
        {"goodbye", 'G'},  {"sysop", '*'}};
    return v;
}

bool migrateLordArgs(DoorEntry& d) {
    std::string exe = d.exe;
    for (char& c : exe) c = (char)std::toupper((unsigned char)c);
    if (exe != "LORD.EXE") return false;
    if (d.args != "1 /DREW" && d.args != "%N /DREW" && d.args != "%n /DREW") return false;
    d.args = "%N NOEMS /DREW";
    return true;
}

char menuCommandKey(const std::string& cmd) {
    for (auto& c : menuCommands())
        if (c.first == cmd) return c.second;
    return 0;
}

}  // namespace axis::bbs
