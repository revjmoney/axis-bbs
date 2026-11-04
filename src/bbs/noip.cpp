// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "noip.h"

namespace axis::bbs {

NoIpReply noipReply(const std::string& body) {
    NoIpReply r;
    bool      any = false, allGood = true;
    size_t    from = 0;
    while (from < body.size()) {
        size_t      nl = body.find('\n', from);
        std::string line = body.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        from = nl == std::string::npos ? body.size() : nl + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (!r.text.empty()) r.text += ", ";
        r.text += line;
        any = true;
        if (line.rfind("good", 0) == 0 || line.rfind("nochg", 0) == 0) continue;
        allGood = false;
        if (line == "nohost" || line == "badauth" || line == "badagent" || line == "!donator" || line == "abuse")
            r.stop = true;
    }
    r.ok = any && allGood;
    return r;
}

std::string noipUrl(const std::string& host, const std::string& ip) {
    return "https://dynupdate.no-ip.com/nic/update?hostname=" + host + "&myip=" + ip;
}

}  // namespace axis::bbs
