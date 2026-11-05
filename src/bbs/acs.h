// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Access strings as ViSiON/3's menus write them: "*" everyone, "s20" level 20 and up, "fM" has flag M,
// "!" not, "&" and, "|" or ("&" binds tighter). AXIS's own users map onto them: the security level,
// and flags F files, M messages, D doors, $ free downloads, T twit. Anything unreadable denies.
#pragma once
#include <string>

namespace axis::bbs {

struct User;

struct AcsUser {
    int         level = 0;
    std::string flags;
};

bool    acsAllows(const std::string& acs, const AcsUser& u);
AcsUser acsFor(const User& u, bool sysop);

}  // namespace axis::bbs
