// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/theme.h"

namespace axis::bbs::theme {

namespace {
std::string fit(std::string s, size_t n) {
    if (s.size() > n) s.resize(n);
    return s + std::string(n - s.size(), ' ');
}
}  // namespace

// Each word fades on its own, centred: white at both ends, light grey, grey in the middle.
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
            const char* c = len < 3 || edge < 0.34 ? WHITE : edge < 0.75 ? LGREY : DGREY;
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
