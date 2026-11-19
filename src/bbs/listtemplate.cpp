// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/listtemplate.h"

#include <cstdlib>

namespace axis::bbs {

namespace {
// "a,b,c,d" -> exactly four integers (optional sign, digits only, spaces allowed around)
bool fourInts(const std::string& v, int out[4]) {
    size_t from = 0;
    for (int i = 0; i < 4; i++) {
        size_t at = v.find(',', from);
        if ((i < 3) == (at == std::string::npos)) return false;
        std::string f = v.substr(from, at == std::string::npos ? std::string::npos : at - from);
        from = at == std::string::npos ? v.size() : at + 1;
        size_t a = f.find_first_not_of(' '), b = f.find_last_not_of(' ');
        if (a == std::string::npos) return false;
        f = f.substr(a, b - a + 1);
        size_t d = (f[0] == '-' || f[0] == '+') ? 1 : 0;
        if (d >= f.size() || f.size() - d > 6) return false;
        for (size_t k = d; k < f.size(); k++)
            if (f[k] < '0' || f[k] > '9') return false;
        out[i] = std::atoi(f.c_str());
    }
    return true;
}

int clampInt(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// the visible text of a field: art codes in it must not be read back as codes by expandArt, and no control byte
// (ESC, BEL, TAB, BS, CR, LF ...) gets through: each would shift columns or move the terminal
std::string plain(const std::string& s) {
    std::string r;
    for (char c : s) r += (c == '|' || c == '@' || c == '%' || c == '{' || (unsigned char)c < 0x20 || c == 0x7f) ? '?' : c;
    return r;
}

// @XXnn@ / @XX>nn@ -> the field, cut and padded to nn columns; everything else is left for expandArt
std::string sizeFields(const std::string& in, const ArtValue& field) {
    std::string out;
    for (size_t i = 0; i < in.size();) {
        if (in[i] == '@' && i + 4 < in.size() && in[i + 1] >= 'A' && in[i + 1] <= 'Z' && in[i + 2] >= 'A' && in[i + 2] <= 'Z') {
            size_t j = i + 3;
            bool   right = in[j] == '>';
            if (right) j++;
            size_t d = j;
            int    n = 0;
            while (j < in.size() && in[j] >= '0' && in[j] <= '9' && j - d < 3) n = n * 10 + (in[j++] - '0');
            if (j > d && j < in.size() && in[j] == '@') {
                std::string v = field(in.substr(i + 1, 2));  // (already filtered by renderRow)
                if ((int)v.size() > n) v.resize(n);
                std::string pad(n - v.size(), ' ');
                out += right ? pad + v : v + pad;
                i = j + 1;
                continue;
            }
        }
        out += in[i++];
    }
    return out;
}
}  // namespace

bool parseListTemplate(const std::string& text, ListTemplate& out) {
    out = ListTemplate();
    bool haveRow = false;
    for (size_t from = 0; from < text.size();) {
        size_t      nl = text.find('\n', from);
        std::string line = text.substr(from, nl == std::string::npos ? std::string::npos : nl - from);
        from = nl == std::string::npos ? text.size() : nl + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();  // (trailing spaces stay: a row may end in one)
        size_t eq = line.find('=');
        if (line.empty() || line[0] == ';' || eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "geom") {
            int g[4];
            if (!fourInts(v, g)) return false;
            out.x = g[0], out.y = g[1], out.width = g[2], out.rows = g[3];
        } else if (k == "colours") {
            size_t at = v.find(',');
            if (at == std::string::npos) continue;
            out.hi = std::atoi(v.c_str()), out.reg = std::atoi(v.c_str() + at + 1);
        } else if (k == "head") out.head = v;
        else if (k == "caption") out.caption = v;
        else if (k == "rule") out.rule = v != "0";
        else if (k == "row") out.row = v, haveRow = true;
        else if (k == "status") out.status = v;
        else if (k == "keys") out.keys = v;
    }
    if (!haveRow) return false;
    out.x = clampInt(out.x, 1, 79);
    out.width = clampInt(out.width, 1, 80 - out.x);
    out.y = clampInt(out.y, 4, 21);
    out.rows = clampInt(out.rows, 1, 22 - out.y);
    return true;
}

std::string renderRow(const ListTemplate& t, const AcsUser& who, const ArtValue& field) {
    ArtValue    safe = [&field](const std::string& k) { return plain(field(k)); };  // every value, sized or not
    std::string s = expandArt(sizeFields(t.row, safe), who, safe);
    std::string out;
    int         vis = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = s[i];
        if (c == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {  // an escape sequence: kept, never counted
            size_t j = i + 2;
            while (j < s.size() && !((unsigned char)s[j] >= 0x40 && (unsigned char)s[j] <= 0x7e)) j++;
            if (j < s.size()) j++;
            out.append(s, i, j - i);
            i = j;
        } else if (c == '\r' || c == '\n') {
            i++;
        } else {
            if (vis < t.width) out += (char)c, vis++;
            i++;
        }
    }
    return out;
}

}  // namespace axis::bbs
