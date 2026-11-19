// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/artstyle.h"

#include <cctype>

namespace axis::bbs {

const char kDosToAnsi[8] = {'0', '4', '2', '6', '1', '5', '3', '7'};

const char* dialectName(Dialect d) { return d == Dialect::Vision3 ? "vision3" : d == Dialect::Renegade ? "renegade" : "axis"; }

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

bool fail(ArtStyle& out, std::string* why, const char* what) {
    out = ArtStyle{};
    if (why) *why = what;
    return false;
}
}  // namespace

bool dialectFrom(const std::string& name, Dialect& out) {
    std::string n = lower(name);
    for (Dialect d : {Dialect::Axis, Dialect::Vision3, Dialect::Renegade})
        if (n == dialectName(d)) {
            out = d;
            return true;
        }
    return false;
}

std::string attrSgr(uint8_t attr) {
    return std::string("\x1b[0;") + (attr & 8 ? "1;" : "") + (attr & 0x80 ? "5;" : "") + "3" + kDosToAnsi[attr & 7] + ";4" +
           kDosToAnsi[(attr >> 4) & 7] + "m";
}

bool parseThemeCfg(const std::string& text, ArtStyle& out, std::string* why) {
    ArtStyle s;
    size_t   at = text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? 3 : 0;  // (a UTF-8 BOM from a Windows editor)
    while (at < text.size()) {
        size_t eol = text.find('\n', at);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(at, eol - at);
        at = eol + 1;
        if (size_t semi = line.find(';'); semi != std::string::npos) line.resize(semi);
        line = trim(line);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return fail(out, why, "a line without an =");
        std::string key = lower(trim(line.substr(0, eq))), val = trim(line.substr(eq + 1));
        if (key == "dialect") {
            if (!dialectFrom(val, s.dialect)) return fail(out, why, "an unknown dialect");
            continue;
        }
        bool isC = key.size() == 2 && key[0] == 'c' && key[1] >= '1' && key[1] <= '7';
        bool isR = key.size() == 2 && key[0] == 'r' && key[1] >= '0' && key[1] <= '9';
        if (!isC && !isR) continue;  // (the look's other keys: not this parser's)
        if (val.empty() || val.size() > 3) return fail(out, why, "a colour that is not 0 to 255");
        int n = 0;
        for (char ch : val) {
            if (ch < '0' || ch > '9') return fail(out, why, "a colour that is not 0 to 255");
            n = n * 10 + (ch - '0');
        }
        if (n > 255) return fail(out, why, "a colour that is not 0 to 255");
        (isC ? s.c[key[1] - '1'] : s.r[key[1] - '0']) = (uint8_t)n;
    }
    out = s;
    return true;
}

}  // namespace axis::bbs
