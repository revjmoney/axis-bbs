// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/dropfile.h"

#include <ctime>
#include <vector>

namespace axis {
namespace door {

namespace {
std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (auto& l : v) s += l + "\r\n";
    return s;
}
std::string today() {
    std::time_t t = std::time(nullptr);
    char        b[16];
    std::strftime(b, sizeof b, "%m/%d/%y", std::localtime(&t));
    return b;
}
std::string nowHm() {
    std::time_t t = std::time(nullptr);
    char        b[8];
    std::strftime(b, sizeof b, "%H:%M", std::localtime(&t));
    return b;
}
}  // namespace

std::string makeDoorSys(const CallerInfo& c) {
    auto num = [](long v) { return std::to_string(v); };
    std::vector<std::string> l = {
        "COM" + num(c.comPort) + ":",   // 1  COM port (COM0: = local)
        num(c.baud),                    // 2  baud (DTE rate)
        "8",                            // 3  data bits
        num(c.node),                    // 4  node
        num(c.baud),                    // 5  DTE rate
        "Y",                            // 6  screen display
        "N",                            // 7  printer
        "Y",                            // 8  page bell
        "Y",                            // 9  caller alarm
        c.realName,                     // 10 full name
        c.location,                     // 11 calling from
        "000-000-0000",                 // 12 home phone
        "000-000-0000",                 // 13 work phone
        c.password,                     // 14 password
        num(c.security),                // 15 security level
        num(c.timesOn),                 // 16 times on
        today(),                        // 17 last date called
        num(c.minutesLeft * 60L),       // 18 seconds remaining
        num(c.minutesLeft),             // 19 minutes remaining
        c.ansi ? "GR" : "NG",           // 20 graphics
        "24",                           // 21 page length
        "N",                            // 22 expert
        "1",                            // 23 conferences
        "0",                            // 24 conference exited from
        "12/31/99",                     // 25 expiration date
        num(c.userRecord),              // 26 user record number
        "Z",                            // 27 default protocol
        "0",                            // 28 uploads
        "0",                            // 29 downloads
        "0",                            // 30 daily download K
        "9999",                         // 31 daily download max K
        "01/01/70",                     // 32 birthdate
        "C:\\",                         // 33 main directory
        "C:\\",                         // 34 general directory
        c.sysopName,                    // 35 sysop name
        c.alias,                        // 36 alias
        "00:00",                        // 37 event time
        "Y",                            // 38 error correcting
        c.ansi ? "Y" : "N",             // 39 ANSI in NG mode
        "Y",                            // 40 use record locking
        "7",                            // 41 default colour
        "0",                            // 42 time credits
        today(),                        // 43 last new-file scan
        nowHm(),                        // 44 time of this call
        nowHm(),                        // 45 time of last call
        "9999",                         // 46 max daily files
        "0",                            // 47 files downloaded today
        "0",                            // 48 total K uploaded
        "0",                            // 49 total K downloaded
        "",                             // 50 comment
        "0",                            // 51 doors opened
        "0",                            // 52 messages left
    };
    return join(l);
}

std::string makeDorinfo(const CallerInfo& c) {
    auto first = [](const std::string& s) {
        size_t sp = s.find(' ');
        return sp == std::string::npos ? s : s.substr(0, sp);
    };
    auto rest = [](const std::string& s) {
        size_t sp = s.find(' ');
        return sp == std::string::npos ? std::string() : s.substr(sp + 1);
    };
    std::vector<std::string> l = {
        c.bbsName,
        first(c.sysopName),
        rest(c.sysopName),
        "COM" + std::to_string(c.comPort),
        std::to_string(c.baud) + " BAUD,N,8,1",
        "0",
        first(c.alias),
        rest(c.alias),
        c.location,
        c.ansi ? "1" : "0",
        std::to_string(c.security),
        std::to_string(c.minutesLeft),
        "-1",  // FOSSIL
    };
    return join(l);
}

}  // namespace door
}  // namespace axis
