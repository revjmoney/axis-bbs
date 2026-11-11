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

// A short header that still looks like a screen: a half-block lip around a faded title, then a
// drip line. Section .ANS files replace this when the theme folder has them (see drip / artFor).
std::string fade(const std::string& text);
std::string banner(const std::string& title) {
    std::string up;
    for (unsigned char c : title) {
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);
        if (c >= 0x20 && c < 0x7f && up.size() < 28) up += (char)c;
    }
    if (up.empty()) up = "AXIS";
    std::string core = "[ " + up + " ]";
    int         gap = 68 - (int)core.size();
    if (gap < 4) gap = 4;
    int left = gap / 2, right = gap - left;
    std::string s = "  ";
    s += HOT;
    s += "\xdc";
    s += ORANGE;
    s += "\xdf";
    s += std::string((size_t)left, '\xdf');
    s += DGREY;
    s += "[ ";
    s += fade(up);
    s += DGREY;
    s += " ]";
    s += ORANGE;
    s += std::string((size_t)right, '\xdf');
    s += HOT;
    s += "\xdc\r\n  ";
    s += ORANGE;
    s += "\xdf\xdf ";
    s += DGREY;
    s += std::string(6, '\xb0');
    s += ORANGE;
    s += " axis underground cnc ";
    s += DGREY;
    s += "\xfa ";
    s += HOT;
    s += "rotatocam.com";
    s += DGREY;
    s += " \xfa ";
    s += HOT;
    s += "jmscnc.com";
    return s + TEXT + "\r\n";
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
static bool lite_ = false;
void        setLite(bool on) { lite_ = on; }
void setMci(std::function<std::string(const std::string&)> fill) { mci_ = std::move(fill); }
void        setArt(std::function<std::string(const std::string&)> art) { art_ = std::move(art); }
std::string artFor(const std::string& title) {
    if (!art_) return "";
    std::string name;  // "BBS LIST" -> BBSLIST.ANS (letters only, 8 at most)
    for (char c : title)
        if (std::isalnum((unsigned char)c) && name.size() < 8) name += (char)std::toupper((unsigned char)c);
    if (name.empty()) return "";
    std::string a = lite_ ? art_(name + ".LITE.ANS") : std::string();
    if (a.empty()) a = art_(name + ".ANS");
    if (!a.empty() && mci_) a = mci_(a);
    if (!a.empty() && a.back() != '\n') a += "\r\n";
    return a;
}

Look look() { return look_; }

struct LookRow {
    const char* id;
    const char* label;
    const char* blurb;
    Look        chrome;
    const char* legacy;  // older folder name on the card, or null
};

// The footer, BBS setup, and the saved setting all walk this one list.
const LookRow kLooks[] = {
    {"axis", "Axis", "drippy orange", Look::Axis, nullptr},
    {"cyan", "Cyan", "grey and cyan", Look::Oblivion, "oblivion"},
    {"green", "Green", "green and grey", Look::Iniquity, "iniquity"},
    {"crt", "Crt", "hardcore crt", Look::Axis, nullptr},
    {"sea", "Sea", "deep sea", Look::Axis, nullptr},
    {"haunted", "Haunted", "haunted crt", Look::Axis, nullptr},
    {"cathedral", "Cathedral", "occult cathedral", Look::Axis, nullptr},
    {"skulls", "Skulls", "skulls", Look::Axis, nullptr},
    {"underground", "Underground", "underground", Look::Axis, nullptr},
    {"wasteland", "Wasteland", "wasteland", Look::Axis, nullptr},
};

const LookRow* findLook(const std::string& name) {
    std::string n;
    for (unsigned char c : name) n += (char)(c | 0x20);
    if (n == "oblivion") n = "cyan";
    if (n == "iniquity") n = "green";
    for (const LookRow& row : kLooks)
        if (n == row.id) return &row;
    return nullptr;
}

const char* knownLook(const std::string& name) {
    const LookRow* row = findLook(name);
    return row ? row->id : nullptr;
}

Look lookFrom(const std::string& name) {
    const LookRow* row = findLook(name);
    return row ? row->chrome : Look::Axis;
}

const char* lookName(Look l) { return l == Look::Oblivion ? "cyan" : l == Look::Iniquity ? "green" : "axis"; }

const char* legacyThemeFolder(const std::string& id) {
    const LookRow* row = findLook(id);
    return row ? row->legacy : nullptr;
}

const char* menuScreenFile(const std::string& name) {
    static const char* const kMap[][2] = {
        {"MAIN.ANS", "MAIN.ANS"},       {"LOGIN.ANS", "LOGIN.ANS"},       {"FASTLOGN.ANS", "FASTLOGN.ANS"},
        {"NEWUSER.ANS", "NEWUSER.ANS"}, {"PDMATRIX.ANS", "MATRIX.ANS"},   {"FILEM.ANS", "FILES.ANS"},
        {"DOORSM.ANS", "DOORS.ANS"},    {"MSGMENU.ANS", "MSGS.ANS"},      {"PRELOGON1.ANS", "PRELOGON.ANS"},
        {"PRELOGON2.ANS", "SHOP.ANS"},  {"PRELOGON3.ANS", "MILL.ANS"},
    };
    for (const auto& m : kMap)
        if (name == m[0]) return m[1];
    static const char* const kLite[][2] = {  // a lite screen maps as its full one (docs/MENU-ART.md)
        {"MAIN.LITE.ANS", "MAIN.LITE.ANS"},       {"LOGIN.LITE.ANS", "LOGIN.LITE.ANS"},     {"FASTLOGN.LITE.ANS", "FASTLOGN.LITE.ANS"},
        {"NEWUSER.LITE.ANS", "NEWUSER.LITE.ANS"}, {"PDMATRIX.LITE.ANS", "MATRIX.LITE.ANS"}, {"FILEM.LITE.ANS", "FILES.LITE.ANS"},
        {"DOORSM.LITE.ANS", "DOORS.LITE.ANS"},    {"MSGMENU.LITE.ANS", "MSGS.LITE.ANS"},    {"PRELOGON1.LITE.ANS", "PRELOGON.LITE.ANS"},
        {"PRELOGON2.LITE.ANS", "SHOP.LITE.ANS"},  {"PRELOGON3.LITE.ANS", "MILL.LITE.ANS"},
    };
    for (const auto& m : kLite)
        if (name == m[0]) return m[1];
    return nullptr;
}

bool isClassicWfc(const std::string& v) {
    if (v.size() != 7) return false;
    for (size_t i = 0; i < 7; i++)
        if (std::tolower((unsigned char)v[i]) != "classic"[i]) return false;
    return true;
}

bool splitLookFile(const std::string& name, std::string& look, std::string& file) {
    size_t colon = name.find(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= name.size()) return false;
    std::string f = name.substr(colon + 1);
    if (f.find_first_of(":/\\") != std::string::npos || f.find("..") != std::string::npos) return false;
    const char* id = knownLook(name.substr(0, colon));
    if (!id) return false;
    look = id;
    file = f;
    return true;
}

int lookCount() { return (int)(sizeof kLooks / sizeof kLooks[0]); }

const char* lookId(int index) { return index >= 0 && index < lookCount() ? kLooks[index].id : nullptr; }

const char* lookLabel(int index) { return index >= 0 && index < lookCount() ? kLooks[index].label : ""; }

const char* lookBlurb(const char* id) {
    const LookRow* row = id ? findLook(id) : nullptr;
    return row ? row->blurb : "";
}

const char* nextLook(const std::string& name) {
    const LookRow* row = findLook(name);
    int            i = row ? (int)(row - kLooks) : -1;
    return kLooks[(i + 1) % lookCount()].id;
}

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
                bool endsHere = d == drip3 || (d == drip2 ? (i >= (int)sizeof drip3 || drip3[i] != '#') : (i >= (int)sizeof drip2 || drip2[i] != '#'));
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

WfcColours wfcColours(Look look) {
    if (look == Look::Axis)  // black, grey, orange, white: a gold diode over orange frames
        return {12, 14, 7, 15, 8, 8, 6, 6, 8, 13, 15, {14, 14, 12, 12, 6, 6, 6}};
    if (look == Look::Oblivion)  // grey and cyan, the same accent the caller screens use
        return {3, 11, 7, 15, 8, 8, 1, 3, 8, 13, 15, {11, 11, 3, 3, 9, 9, 1}};
    // green frames and a cyan-to-blue diode
    return {2, 10, 7, 15, 8, 8, 2, 2, 8, 13, 15, {11, 11, 3, 3, 9, 9, 1}};
}

WfcColours wfcColoursFor(const std::string& name) {
    const char* id = knownLook(name);
    if (!id) id = "axis";
    if (std::string(id) == "cyan") return wfcColours(Look::Oblivion);
    if (std::string(id) == "green" || std::string(id) == "crt" || std::string(id) == "haunted") return wfcColours(Look::Iniquity);
    if (std::string(id) == "sea") return {1, 9, 7, 15, 8, 8, 1, 3, 8, 13, 15, {9, 9, 1, 1, 3, 3, 1}};
    if (std::string(id) == "cathedral") return {5, 13, 7, 15, 8, 8, 5, 5, 8, 13, 15, {13, 13, 5, 5, 4, 4, 5}};
    if (std::string(id) == "skulls") return {4, 12, 7, 15, 8, 8, 4, 4, 8, 13, 15, {12, 12, 4, 4, 4, 8, 8}};
    if (std::string(id) == "wasteland") return {6, 14, 7, 15, 8, 8, 6, 6, 8, 13, 14, {14, 6, 6, 6, 8, 8, 6}};
    return wfcColours(Look::Axis);  // axis and underground
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
