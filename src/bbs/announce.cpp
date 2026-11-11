// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/announce.h"

namespace axis::bbs {

namespace {
std::string tame(std::string s) {  // no mass pings, no stray markdown from callers
    for (size_t at; (at = s.find('@')) != std::string::npos;) s.replace(at, 1, "(at)");
    for (char& c : s)
        if (c == '*' || c == '`' || c == '_' || c == '~' || c == '|') c = ' ';
    return s.size() > 300 ? s.substr(0, 300) : s;
}
bool starts(const std::string& s, const char* p, std::string& rest) {
    std::string q = p;
    if (s.compare(0, q.size(), q) != 0) return false;
    rest = s.substr(q.size());
    return true;
}
}  // namespace

std::string announceLine(const std::string& e) {
    std::string r;
    if (starts(e, "sysop: validated ", r)) return "**" + tame(r) + "** joined the underground.";
    if (starts(e, "sysop: added ", r)) return "**" + tame(r.substr(0, r.find(" ("))) + "** has an account now.";
    if (starts(e, "oneliner: ", r)) return "oneliner wall: " + tame(r);
    if (starts(e, "rumor: ", r)) return "a rumor is going round: " + tame(r);
    if (starts(e, "new user application waiting", r)) return "A new user applied and waits for the sysop.";
    if (starts(e, "PAGE from ", r)) return "The sysop was paged by " + tame(r);
    size_t up = e.find(" uploaded ");
    if (up != std::string::npos) {
        std::string file = e.substr(up + 10);
        size_t sp = file.find(' ');
        if (sp != std::string::npos) file.resize(sp);
        return "**" + tame(e.substr(0, up)) + "** uploaded " + tame(file) + ". Fresh files in the vaults.";
    }
    size_t op = e.find(" opened ");
    if (op != std::string::npos && e.find(':') == std::string::npos) return "**" + tame(e.substr(0, op)) + "** is playing " + tame(e.substr(op + 8)) + ".";
    return "";
}

}  // namespace axis::bbs
