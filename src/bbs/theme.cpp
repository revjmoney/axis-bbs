// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/theme.h"

#include <cctype>

namespace axis::bbs::theme {

const char* TEXT = "\x1b[0;37m";
const char* LGREY = "\x1b[0;37m";
const char* WHITE = "\x1b[1;37m";
const char* DGREY = "\x1b[1;30m";
const char* ORANGE = "\x1b[0;33m";
const char* HOT = "\x1b[1;33m";
const char* KEY = "\x1b[0;33m";

namespace {
Look look_ = Look::Axis;

std::string fit(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    return s + std::string(n - s.size(), ' ');
}

std::string spacedOut(const std::string& t) {  // "MENU" -> "M E N U"
    std::string s;
    for (size_t i = 0; i < t.size(); i++) s += (i ? " " : "") + std::string(1, t[i]);
    return s;
}

}  // namespace

// Oblivion: a title bar, a grey-to-cyan rule fading in from both sides around the spaced title
std::string titleBar(const std::string& text) {
    std::string up = text;
    for (char& c : up) c = (char)toupper((unsigned char)c);
    std::string t = spacedOut(up), s = "\r\n  ";
    int         side = (76 - (int)t.size() - 6) / 2;
    if (side < 2) side = 2;
    auto rule = [&](int n, bool in) {
        for (int i = 0; i < n; i++) {
            int k = in ? i : n - 1 - i;  // distance from the outer end
            s += k < n / 3 ? DGREY : k < n * 2 / 3 ? ORANGE : HOT;
            s += '\xc4';
        }
    };
    rule(side, true);
    s += std::string(DGREY) + "[ " + WHITE + t + DGREY + " ]";
    rule(side, false);
    return s + "\r\n" + TEXT;
}

namespace {
std::string o2Window(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, int width) {
    const int   inner = width - 2;
    std::string s, t = " " + spacedOut(title) + " ";
    s.reserve((rows.size() + 4) * (size_t)(width + 48));
    s += std::string("  ") + ORANGE + "\xc9\xcd" + DGREY + "[" + HOT + t + DGREY + "]" + ORANGE;
    s += std::string((size_t)(inner - 3 - (int)t.size() > 0 ? inner - 3 - (int)t.size() : 0), '\xcd') + "\xbb\r\n";
    for (auto& r : rows) {
        std::string label = fit(r.first, 10), value = fit(r.second, (size_t)inner - 13);
        s += std::string("  ") + ORANGE + "\xba  " + HOT + label + WHITE + value;
        int len = 2 + 10 + (int)value.size();
        if (len < inner) s += std::string((size_t)(inner - len), ' ');
        s += std::string(ORANGE) + "\xba\r\n";
    }
    s += std::string("  ") + ORANGE + "\xc8" + std::string((size_t)inner, '\xcd') + "\xbc\r\n";
    return s + TEXT;
}
}  // namespace

void use(Look l) {
    look_ = l;
    bool o2 = l == Look::Oblivion, inq = l == Look::Iniquity;
    ORANGE = inq ? "\x1b[0;32m" : o2 ? "\x1b[0;36m" : "\x1b[0;33m";
    HOT = inq ? "\x1b[1;32m" : o2 ? "\x1b[1;36m" : "\x1b[1;33m";
    KEY = inq ? "\x1b[1;32m" : o2 ? "\x1b[1;36m" : "\x1b[0;33m";
}

static std::function<std::string(const std::string&)> art_, mci_;
void setMci(std::function<std::string(const std::string&)> fill) { mci_ = std::move(fill); }
void        setArt(std::function<std::string(const std::string&)> art) { art_ = std::move(art); }
std::string artFor(const std::string& title) {
    if (!art_) return "";
    std::string name;  // "BBS LIST" -> BBSLIST.ANS (letters only, 8 at most)
    for (char c : title)
        if (std::isalnum((unsigned char)c) && name.size() < 8) name += (char)std::toupper((unsigned char)c);
    if (name.empty()) return "";
    std::string a = art_(name + ".ANS");
    if (!a.empty() && mci_) a = mci_(a);
    if (!a.empty() && a.back() != '\n') a += "\r\n";
    return a;
}

Look look() { return look_; }

Look lookFrom(const std::string& name) {
    std::string n;
    for (char c : name) n += (char)(c | 0x20);
    return n == "oblivion" ? Look::Oblivion : n == "iniquity" ? Look::Iniquity : Look::Axis;
}

const char* lookName(Look l) { return l == Look::Oblivion ? "oblivion" : l == Look::Iniquity ? "iniquity" : "axis"; }

std::string pipes(const std::string& text) {
    static const char ansi[8] = {'0', '4', '2', '6', '1', '5', '3', '7'};  // DOS colour order to ANSI
    std::string s;
    s.reserve(text.size() + 32);
    for (size_t i = 0; i < text.size(); i++) {
        if (text[i] == '|' && i + 2 < text.size() && isdigit((unsigned char)text[i + 1]) && isdigit((unsigned char)text[i + 2])) {
            int c = (text[i + 1] - '0') * 10 + (text[i + 2] - '0');
            if (c < 16) {
                s += c < 8 ? "\x1b[0;3" : "\x1b[0;1;3";
                s += ansi[c & 7];
                s += 'm';
                i += 2;
                continue;
            }
            if (c < 24) {
                s += "\x1b[4";
                s += ansi[c - 16];
                s += 'm';
                i += 2;
                continue;
            }
        }
        s += text[i];
    }
    return s;
}

// Each word fades on its own toward its centre: dark at both ends, brighter, brightest in the
// middle (the Iniquity look in greens).
std::string fade(const std::string& text) {
    std::string s;
    size_t      i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            s += ' ';
            i++;
            continue;
        }
        size_t end = text.find(' ', i);
        if (end == std::string::npos) end = text.size();
        size_t      len = end - i;
        const char* last = nullptr;
        for (size_t k = 0; k < len; k++) {
            // edge: 0 at either end of the word, 1 in its middle
            double      edge = (double)(k < len - 1 - k ? k : len - 1 - k) / ((len - 1) / 2.0 > 0 ? (len - 1) / 2.0 : 1);
            bool        inq = look_ == Look::Iniquity;
            const char* c = len < 3 ? (inq ? "\x1b[1;32m" : WHITE)
                            : edge < 0.34 ? (inq ? "\x1b[0;32m" : DGREY)
                            : edge < 0.75 ? (inq ? "\x1b[1;32m" : LGREY)
                                          : WHITE;
            if (c != last) s += c;
            last = c;
            s += text[k + i];
        }
        i = end;
    }
    return s + TEXT;
}

std::string key(const std::string& letter, const std::string& rest) {
    return std::string(DGREY) + "[" + KEY + letter + DGREY + "]" + TEXT + rest;
}

std::string marquee(const std::string& text, size_t width, uint32_t ms) {
    if (text.size() <= width) return text;
    const uint32_t step = 300, hold = 5;  // steps paused at each end
    uint32_t       span = (uint32_t)(text.size() - width), cycle = span + 2 * hold;
    uint32_t       at = (ms / step) % cycle;
    uint32_t       off = at < hold ? 0 : at < hold + span ? at - hold : span;
    return text.substr(off, width);
}

std::string window(const std::string& title, const std::vector<std::pair<std::string, std::string>>& rows, int width) {
    if (look_ != Look::Axis) return o2Window(title, rows, width);
    const int   inner = width - 2;  // between the side walls
    std::string s;
    s.reserve((rows.size() + 4) * (size_t)(width + 64));  // one block, not doubling: a busy board has little heap
    // top: an orange lip of half blocks
    s += "  ";
    s += ORANGE;
    s += std::string((size_t)width, '\xdc');
    s += "\r\n";
    auto row = [&](const std::string& body, size_t bodyLen) {  // body already coloured, bodyLen visible chars
        s += "  ";
        s += ORANGE;
        s += "\xdb";
        s += body;
        if ((int)bodyLen < inner) s += std::string((size_t)(inner - bodyLen), ' ');
        s += ORANGE;
        s += "\xdb\r\n";
    };
    std::string spaced;
    for (char c : title) {
        spaced += c;
        spaced += ' ';
    }
    row("  " + fade(spaced), 2 + spaced.size());
    s += "  ";
    s += ORANGE;
    s += "\xdb";
    s += DGREY;
    s += std::string((size_t)inner, '\xc4');
    s += ORANGE;
    s += "\xdb\r\n";
    for (auto& r : rows) {
        std::string label = fit(r.first, 10), value = fit(r.second, (size_t)inner - 13);
        row(std::string("  ") + LGREY + label + WHITE + value, 2 + 10 + value.size());
    }
    // bottom wall, then paint running down off it: fixed pattern so screens don't flicker between redraws
    s += "  ";
    s += ORANGE;
    s += std::string((size_t)width, '\xdb');  // a solid wall, so the drips hang straight off it
    s += "\r\n";
    static const char drip1[] = "## ### # ## ### #### # ### ## #  ### # #### ## # ### ## ### # ## ##### #";
    static const char drip2[] = " #  #    #  #    # #   #   #    #     # #   #    #  #    #   #  # #  ";
    static const char drip3[] = " #       .  #      #       .    #       .   #       #    .      #  .  ";
    for (const char* d : {drip1, drip2, drip3}) {
        s += "  ";
        for (int i = 0; i < width && d[i]; i++) {
            if (d[i] == '#') {  // runs: full blocks, ending in a top half; the odd grey one for depth
                bool endsHere = d == drip3 || (d == drip2 ? drip3[i] != '#' : drip2[i] != '#');
                s += i % 7 == 3 ? DGREY : ORANGE;
                s += endsHere ? "\xdf" : "\xdb";
            } else if (d[i] == '.') {
                s += ORANGE;
                s += "\xdc";
            } else {
                s += ' ';
            }
        }
        s += "\r\n";
    }
    return s + TEXT;
}

std::string bar(double done, int width) {
    if (done < 0) done = 0;
    if (done > 1) done = 1;
    int         full = (int)(done * width + 0.5);
    std::string s = ORANGE;
    s += std::string((size_t)full, '\xdb');
    s += DGREY;
    s += std::string((size_t)(width - full), '\xb0');
    return s + TEXT;
}

}  // namespace axis::bbs::theme
