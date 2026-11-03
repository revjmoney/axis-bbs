// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/config.h"

#include <cstdlib>

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

BbsConfig parseConfig(const std::string& ini) {
    BbsConfig c;
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
        else c.unknown.emplace_back(k, v);
    }
    return c;
}

std::string formatConfig(const BbsConfig& c) {
    std::string s = "# AXIS UNDERGROUND CNC settings (written by the board; safe to edit)\n";
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
    for (auto& u : c.unknown) kv(u.first.c_str(), u.second);
    kv("setup_done", c.setupDone ? "1" : "0");
    return s;
}

}  // namespace axis::bbs
