// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/wfc.h"

#include <cctype>
#include <cstdio>

namespace axis::bbs {

namespace {
const int kNodeSlots = 8;  // |NA..|NH
struct KeyCode {
    const char* code;
    char        key;
};
const KeyCode kKeys[] = {{"KS", 'S'}, {"KU", 'U'}, {"KM", '#'}, {"KB", 'B'}, {"KF", 'F'},
                         {"KL", 'L'}, {"KE", 'E'}, {"KA", 'A'}, {"KV", '!'}, {"KO", ' '}};
const int kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
// hotspot ids: 0..kKeyCount-1 the keys, then the node slots, then "none" (an empty node slot)
const int kNone = kKeyCount + kNodeSlots;

// the in-band marker for a hotspot: a control byte that art never holds, leading the value text
char markerFor(int id) {
    static const char kBytes[] = {1, 2, 3, 4, 5, 6, 7, 11, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 28};
    return kBytes[id];
}
int idForMarker(char c) {
    for (int i = 0; i <= kNone; i++)
        if (markerFor(i) == c) return i;
    return -1;
}

std::string two(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02u", v);
    return b;
}
std::string sdFree(uint32_t kb) {
    char b[24];
    if (kb >= 1024u * 1024u) std::snprintf(b, sizeof b, "%.1fG", kb / (1024.0 * 1024.0));
    else if (kb >= 1024u) std::snprintf(b, sizeof b, "%uM", (unsigned)(kb / 1024u));
    else std::snprintf(b, sizeof b, "%uK", (unsigned)kb);
    return b;
}
std::string uptime(uint32_t s) { return std::to_string(s / 86400) + "d " + two(s / 3600 % 24) + ":" + two(s / 60 % 60); }
std::string nodeLine(const WfcNode& n) {
    std::string l = std::to_string(n.number) + " ";
    if (n.handle.empty()) return l + "waiting";
    l += n.handle;
    if (!n.activity.empty()) l += "  " + n.activity;
    return l;
}
bool nodeCode(const std::string& k) { return k.size() == 2 && k[0] == 'N' && k[1] >= 'A' && k[1] < 'A' + kNodeSlots; }
}  // namespace

ArtValue wfcValues(const WfcSnapshot& s) {
    return [s](const std::string& k) -> std::string {
        if (k == "BN") return s.bbsName;
        if (k == "SN") return s.sysop;
        if (k == "CT") return std::to_string(s.callsToday);
        if (k == "PT") return std::to_string(s.postsToday);
        if (k == "UT") return std::to_string(s.uploadsToday);
        if (k == "LC") return s.lastCaller;
        if (k == "SD") return sdFree(s.sdFreeKb);
        if (k == "RM") return std::to_string(s.ramFreeKb);
        if (k == "IP") return s.ip;
        if (k == "UP") return uptime(s.uptimeS);
        if (k == "TM") return s.time;
        if (k == "DT") return s.date;
        if (k == "PG") return s.page;
        if (k == "MB") return s.main ? "main board" : "follower board";
        if (nodeCode(k)) {
            size_t i = k[1] - 'A';
            return i < s.nodes.size() ? nodeLine(s.nodes[i]) : "";
        }
        if (k.size() == 2 && k[0] == 'E' && k[1] >= 'A' && k[1] <= 'E') {
            size_t i = k[1] - 'A';
            return i < s.events.size() ? s.events[i] : "";
        }
        for (const auto& kc : kKeys)
            if (k == kc.code) return std::string(1, kc.key);
        return "";
    };
}

std::vector<WfcHotspot> drawWfc(const std::string& art, const WfcSnapshot& s, AnsiTerm& term) {
    // expandArt buffers its output, so the terminal has not seen the text before a code when its value is asked for. The value of a
    // K? or N? code therefore leads with a one-byte marker; the emit callback feeds the text up to each marker to the terminal and
    // records where the cursor stands. The marker takes one column inside an @XX###@ field, so those fields are made one wider.
    std::string wide;
    wide.reserve(art.size() + 16);
    for (size_t i = 0; i < art.size(); i++) {
        wide += art[i];
        if (art[i] != '@') continue;
        size_t j = i + 1;
        while (j < art.size() && std::isupper((unsigned char)art[j])) j++;
        size_t k = j;
        while (k < art.size() && art[k] == '#') k++;
        if (j > i + 1 && k > j && k < art.size() && art[k] == '@') {
            std::string name = art.substr(i + 1, j - i - 1);
            wide.append(art, i + 1, k - i - 1);
            if (name.size() == 2 && (name[0] == 'K' || name[0] == 'N')) wide += '#';
            wide += '@';
            i = k;
        }
    }

    ArtValue base = wfcValues(s);
    ArtValue value = [&](const std::string& k) -> std::string {
        if (k.size() == 2 && k[0] == 'K')
            for (int i = 0; i < kKeyCount; i++)
                if (k == kKeys[i].code) return std::string(1, markerFor(i)) + base(k);
        if (nodeCode(k)) {
            size_t i = k[1] - 'A';
            return std::string(1, markerFor(i < s.nodes.size() ? kKeyCount + (int)i : kNone)) + base(k);
        }
        return base(k);
    };

    std::vector<WfcHotspot> spots;
    expandArt(wide, AcsUser{}, value, [&](const char* d, size_t n) {
        std::string run;
        for (size_t i = 0; i < n; i++) {
            int id = idForMarker(d[i]);
            if (id < 0) {
                run += d[i];
                continue;
            }
            term.feed(run);
            run.clear();
            if (id < kKeyCount) spots.push_back({term.row(), term.col(), kKeys[id].key, 0});
            else if (id < kNone) spots.push_back({term.row(), term.col(), 'N', s.nodes[id - kKeyCount].number});
        }
        term.feed(run);
    });
    return spots;
}

char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node) {
    if (node) *node = 0;
    for (const auto& h : spots) {
        if (h.row != row) continue;
        if (h.node > 0) {
            if (node) *node = h.node;
            return h.key;
        }
        if (col >= h.col - 1 && col <= h.col + 1) return h.key;
    }
    return 0;
}

}  // namespace axis::bbs
