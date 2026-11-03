// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "door/pathresolve.h"

#include <cctype>

namespace axis {
namespace door {

namespace {
bool sameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); i++)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

// split on '\' and '/', applying '.' and '..' (clamped at the root); false on a part with ':'
bool addParts(const std::string& s, std::vector<std::string>& parts) {
    size_t start = 0;
    while (start <= s.size()) {
        size_t end = s.find_first_of("\\/", start);
        std::string part = s.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.find(':') != std::string::npos) return false;
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}
}  // namespace

std::string resolveDosPath(const std::string& cwd, const std::string& dosPath,
                           const std::function<std::vector<std::string>(const std::string&)>& list) {
    std::string p = dosPath;
    if (p.size() >= 2 && p[1] == ':') p = p.substr(2);  // the drive letter
    std::vector<std::string> parts;
    bool absolute = !p.empty() && (p[0] == '\\' || p[0] == '/');
    if (!absolute && !addParts(cwd, parts)) return "";
    if (!addParts(p, parts) || parts.empty()) return "";
    std::string host;
    for (const std::string& part : parts) {
        std::string name = part;  // a new file or folder keeps the case the door asked for
        for (const std::string& n : list(host))
            if (sameName(n, part)) {
                name = n;
                break;
            }
        host += host.empty() ? name : "/" + name;
    }
    return host;
}

}  // namespace door
}  // namespace axis
