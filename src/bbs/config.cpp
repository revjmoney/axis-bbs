// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/config.h"

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

BbsConfig parseConfig(const std::string& ini) {
    BbsConfig              c;
    std::vector<DoorEntry> listed;
    std::map<int, FileArea> areas;  // by number: sorted, and a repeated number replaces
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
                listed.push_back(d);
            }
        } else if (k.rfind("area", 0) == 0 && k.size() > 4 && std::isdigit((unsigned char)k[4])) {
            size_t bar = v.find('|');
            if (bar != std::string::npos && bar > 0) areas[std::atoi(k.c_str() + 4)] = {v.substr(0, bar), v.substr(bar + 1)};
        } else c.unknown.emplace_back(k, v);
    }
    if (listed.empty()) listed = c.doors;  // none in the file: keep the default list
    c.doors = listed;
    if (!areas.empty()) {
        c.areas.clear();
        for (auto& a : areas) c.areas.push_back(a.second);
    }
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
    kv("location", c.location);
    kv("board_name", c.boardName);
    kv("public_host", c.publicHost);
    kv("telnet_port", std::to_string(c.telnetPort));
    kv("timezone", c.timezone);
    kv("ftn_address", c.ftnAddress);
    kv("new_user_security", std::to_string(c.newUserSecurity));
    kv("daily_minutes", std::to_string(c.dailyMinutes));
    kv("idle_minutes", std::to_string(c.idleMinutes));
    for (size_t i = 0; i < c.doors.size(); i++) {
        const DoorEntry& d = c.doors[i];
        kv(("door" + std::to_string(i + 1)).c_str(), d.id + "|" + d.name + "|" + d.exe + "|" + d.args + "|" + d.drop);
    }
    for (size_t i = 0; i < c.areas.size(); i++)
        kv(("area" + std::to_string(i + 1)).c_str(), c.areas[i].id + "|" + c.areas[i].name);
    for (auto& u : c.unknown) kv(u.first.c_str(), u.second);
    kv("setup_done", c.setupDone ? "1" : "0");
    return s;
}

}  // namespace axis::bbs
