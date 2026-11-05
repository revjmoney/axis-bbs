// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/acs.h"

#include <cctype>
#include <cstdlib>

#include "bbs/users.h"

namespace axis::bbs {

namespace {
// one term: "*", "s<n>", "f<c>", each with an optional "!"; -1 when it can't be read
int term(const std::string& t, const AcsUser& u) {
    if (t.empty()) return -1;
    if (t[0] == '!') {
        int r = term(t.substr(1), u);
        return r < 0 ? -1 : !r;
    }
    if (t == "*") return 1;
    char k = (char)std::tolower((unsigned char)t[0]);
    if (k == 's' && t.size() > 1) {
        for (size_t i = 1; i < t.size(); i++)
            if (!std::isdigit((unsigned char)t[i])) return -1;
        return u.level >= std::atoi(t.c_str() + 1);
    }
    if (k == 'f' && t.size() == 2) return u.flags.find((char)std::toupper((unsigned char)t[1])) != std::string::npos;
    return -1;
}
}  // namespace

bool acsAllows(const std::string& acs, const AcsUser& u) {
    if (acs.empty()) return true;
    bool any = false;  // "a|b|c": any group whose "&" terms all hold
    for (size_t from = 0; from <= acs.size();) {
        size_t      bar = acs.find('|', from);
        std::string group = acs.substr(from, bar == std::string::npos ? std::string::npos : bar - from);
        bool        all = true;
        for (size_t f = 0; f <= group.size();) {
            size_t amp = group.find('&', f);
            int    r = term(group.substr(f, amp == std::string::npos ? std::string::npos : amp - f), u);
            if (r < 0) return false;  // unreadable: deny
            all = all && r;
            if (amp == std::string::npos) break;
            f = amp + 1;
        }
        any = any || all;
        if (bar == std::string::npos) break;
        from = bar + 1;
    }
    return any;
}

AcsUser acsFor(const User& u, bool sysop) {
    AcsUser a;
    a.level = sysop ? kSysopSecurity : u.security;
    if (u.permFiles) a.flags += 'F';
    if (u.permMessages) a.flags += 'M';
    if (u.permDoors) a.flags += 'D';
    if (u.freeDownloads) a.flags += '$';
    if (u.twit) a.flags += 'T';
    return a;
}

}  // namespace axis::bbs
