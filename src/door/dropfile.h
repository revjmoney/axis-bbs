// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Drop files: what the BBS tells a door about the caller before it starts.
// DOOR.SYS (52-line GAP format, Synchronet's line list) and DORINFO1.DEF (13 lines).
#pragma once
#include <string>

namespace axis {
namespace door {

struct CallerInfo {
    std::string realName = "Guest", alias = "Guest", location = "Underground", password = "*";
    int         security = 10;
    int         node = 1;
    int         minutesLeft = 60;
    int         comPort = 1;  // 0 = local (sysop at the board)
    long        baud = 38400;
    std::string sysopName = "Rev J Money", bbsName = "Axis Underground CNC";  // passed exactly (door keys depend on them)
    bool        ansi = true;
    int         userRecord = 1;
    int         timesOn = 1;
};

std::string makeDoorSys(const CallerInfo& c);  // 52 CRLF lines
std::string makeDorinfo(const CallerInfo& c);  // 13 CRLF lines

}  // namespace door
}  // namespace axis
