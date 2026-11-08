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
    // The art is handed to expandArt in pieces of about 400 bytes (cut only where no {{ACS}} or |{ group is open), so the screen is
    // never held twice.
    ArtValue base = wfcValues(s);
    ArtValue value = [&](const std::string& k) -> std::string {
        std::string v = base(k);
        for (char& c : v)  // free text (handles, page text) must never look like a marker
            if (idForMarker(c) >= 0) c = ' ';
        if (k.size() == 2 && k[0] == 'K')
            for (int i = 0; i < kKeyCount; i++)
                if (k == kKeys[i].code) return std::string(1, markerFor(i)) + v;
        if (nodeCode(k)) {
            size_t i = k[1] - 'A';
            return std::string(1, markerFor(i < s.nodes.size() ? kKeyCount + (int)i : kNone)) + v;
        }
        return v;
    };

    std::vector<WfcHotspot> spots;
    ArtEmit emit = [&](const char* d, size_t n) {
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
            else if (id < kNone && (size_t)(id - kKeyCount) < s.nodes.size())
                spots.push_back({term.row(), term.col(), 'N', s.nodes[id - kKeyCount].number});
        }
        term.feed(run);
    };

    std::string chunk;
    chunk.reserve(460);
    int depth = 0;  // open {{ACS}} blocks and |{ groups
    for (size_t i = 0; i < art.size(); i++) {
        char c = art[i];
        if (c == '{' && art.compare(i, 5, "{{/}}") == 0) depth = depth > 0 ? depth - 1 : 0;
        else if (c == '{' && art.compare(i, 2, "{{") == 0) depth++;
        else if (c == '|' && art.compare(i, 2, "|}") == 0) depth = depth > 0 ? depth - 1 : 0;
        else if (c == '|' && art.compare(i, 2, "|{") == 0 && art.compare(i, 4, "|{P}") != 0 && art.compare(i, 4, "|{O}") != 0) depth++;
        if (idForMarker(c) >= 0) c = ' ';  // a stray control byte in the art must not read as a hotspot
        chunk += c;
        if (c == '@') {
            size_t j = i + 1;
            while (j < art.size() && std::isupper((unsigned char)art[j])) j++;
            size_t k = j;
            while (k < art.size() && art[k] == '#') k++;
            if (j > i + 1 && k > j && k < art.size() && art[k] == '@') {
                chunk.append(art, i + 1, k - i - 1);
                if (j - i - 1 == 2 && (art[i + 1] == 'K' || art[i + 1] == 'N')) chunk += '#';
                chunk += '@';
                i = k;
            }
        }
        if (chunk.size() >= 400 && depth == 0 && (c == '\n' || c == ' ')) {
            expandArt(chunk, AcsUser{}, value, emit);
            chunk.clear();
        }
    }
    if (!chunk.empty()) expandArt(chunk, AcsUser{}, value, emit);
    return spots;
}

// whatever a theme file holds, made safe to send: cut at the cap and at a SAUCE/EOF mark, no escape left open at the end (it would
// swallow what comes next), and a bare LF made a CR LF
std::string cleanWfcArt(std::string a) {
    if (a.size() > kWfcArtCap) a.resize(kWfcArtCap);
    size_t eof = a.find('\x1a');
    if (eof != std::string::npos) a.resize(eof);
    size_t esc = a.rfind('\x1b');
    if (esc != std::string::npos) {
        size_t i = esc + 1;
        bool   open = true;
        if (i < a.size() && a[i] == '[') {
            for (i++; i < a.size(); i++) {
                unsigned char ch = (unsigned char)a[i];
                if (ch >= 0x40 && ch <= 0x7e) {
                    open = false;
                    break;
                }
                if (ch < 0x20 || ch > 0x3f) {  // not part of a sequence: the escape is already broken, not open
                    open = false;
                    break;
                }
            }
        } else if (i < a.size()) {
            open = false;  // ESC and one more byte
        }
        if (open) a.resize(esc);
    }
    std::string out;
    out.reserve(a.size() + 64);
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] == '\n' && (i == 0 || a[i - 1] != '\r')) out += '\r';
        out += a[i];
    }
    return out;
}

char wfcKeyAt(const std::vector<WfcHotspot>& spots, int row, int col, int* node) {
    if (node) *node = 0;
    for (const auto& h : spots)  // the key's own cell first
        if (h.node == 0 && h.row == row && h.col == col) return h.key;
    for (const auto& h : spots)  // then a cell either side
        if (h.node == 0 && h.row == row && (col == h.col - 1 || col == h.col + 1)) return h.key;
    for (const auto& h : spots)  // then a node line anywhere on its row
        if (h.node > 0 && h.row == row) {
            if (node) *node = h.node;
            return h.key;
        }
    return 0;
}

}  // namespace axis::bbs
