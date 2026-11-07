// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/loginseq.h"

#include <cstdlib>

namespace axis::bbs {

std::vector<LoginStep> parseLoginSeq(const std::string& text) {
    std::vector<LoginStep> v;
    for (size_t from = 0; from < text.size();) {
        size_t      nl = text.find('\n', from);
        std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        from = nl == std::string::npos ? text.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.compare(0, 5, "step=") != 0) continue;
        std::vector<std::string> f;
        for (size_t a = 5;;) {
            size_t bar = line.find('|', a);
            f.push_back(line.substr(a, bar == std::string::npos ? std::string::npos : bar - a));
            if (bar == std::string::npos || f.size() == 4) {
                if (bar != std::string::npos) f.push_back(line.substr(bar + 1));
                break;
            }
            a = bar + 1;
        }
        LoginStep s;
        s.cmd = f[0];
        if (f.size() > 1) s.clear = f[1] == "1";
        if (f.size() > 2) s.pause = f[2] == "1";
        if (f.size() > 3) s.level = std::atoi(f[3].c_str());
        if (f.size() > 4) s.data = f[4];
        if (!s.cmd.empty()) v.push_back(s);
    }
    if (v.empty())  // ViSiON/3's configs/login.json template, plus the Saylor poster before the fast-login choice
        v = {{"SYSOPNOTICES"},
             {"NMAILSCAN", "", true, true},
             {"DISPLAYFILE", "SAYLOR", true, true},
             {"FASTLOGIN"},
             {"PRINTNEWS", "", true, true},
             {"LASTCALLS"},
             {"ONELINERS"},
             {"USERSTATS"},
             {"INFOFORMREQUIRED"},
             {"NEWUSERVAL", "", false, false, 255},
             {"CHECKNUV"}};
    return v;
}

}  // namespace axis::bbs
