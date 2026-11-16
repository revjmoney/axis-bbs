// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Theme art on its way to a caller's screen: the read of one piece of a file, a follower's assembly of a file from the
// main's pieces, and a follower's small cache of what it assembled. Portable (stdio only) so the PC tests drive the same
// code the board runs.
#pragma once
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace axis::bbs {

// Append at most min(fileSize - offset, cap) bytes of f (from offset) to out, cut at the first 0x1a. The file's size is asked
// first so out grows once (a string read in 512-byte pieces doubles its block and costs ~16 KB per screen). False: the
// seek to offset failed (nothing appended).
inline bool readFileAppend(FILE* f, size_t cap, size_t offset, std::string& out) {
    const size_t old = out.size();
    long         end = -1;
    if (fseek(f, 0, SEEK_END) == 0) end = ftell(f);
    if (end >= 0) {
        if (fseek(f, (long)offset, SEEK_SET) != 0) return false;
        size_t n = (size_t)end > offset ? (size_t)end - offset : 0;
        if (n > cap) n = cap;
        out.resize(old + n);
        n = n ? fread(&out[old], 1, n, f) : 0;
        out.resize(old + n);
    } else {  // (no size: the old piece-by-piece read)
        if (fseek(f, (long)offset, SEEK_SET) != 0) return false;
        char b[512];
        for (size_t n; out.size() - old < cap && (n = fread(b, 1, sizeof b, f)) > 0;) out.append(b, n);
        if (out.size() - old > cap) out.resize(old + cap);
    }
    if (size_t sauce = out.find('\x1a', old); sauce != std::string::npos) out.resize(sauce);
    return true;
}

// Where an ANSI art's screen rows end, as an 80-column terminal draws it: a byte at a time, it follows line ends, the wrap
// after column 80 and the cursor codes (CUP, up/down/forward/back). The theme packs' header art has no line ends at all (every
// MSGS.ANS is one 80-column wrapped run), so counting '\n' alone never found its rows.
struct ArtRows {
    int  row = 0, col = 0;
    int  esc = 0;  // 0 text, 1 after ESC, 2 inside ESC [
    int  p[2] = {0, 0};
    int  np = 0;
    bool feed(unsigned char c, int rows) {  // true once the art has reached row `rows` (the byte just fed ends the top)
        if (esc == 1) {
            esc = c == '[' ? 2 : 0;
            p[0] = p[1] = np = 0;
        } else if (esc == 2) {
            if (c >= '0' && c <= '9') {
                if (np < 2 && p[np] < 1000) p[np] = p[np] * 10 + (c - '0');
            } else if (c == ';') {
                np++;
            } else if (c >= 0x40 && c <= 0x7e) {
                esc = 0;
                const int n = p[0] ? p[0] : 1;
                if (c == 'H' || c == 'f') row = (p[0] ? p[0] : 1) - 1, col = (p[1] ? p[1] : 1) - 1;
                else if (c == 'A') row = row > n ? row - n : 0;
                else if (c == 'B') row += n;
                else if (c == 'C') col = col + n < 79 ? col + n : 79;
                else if (c == 'D') col = col > n ? col - n : 0;
            }
        } else if (c == 0x1b) {
            esc = 1;
        } else if (c == '\r') {
            col = 0;
        } else if (c == '\n') {
            row++;
        } else if (c >= 0x20 || c == 0x0e || c == 0x0f) {  // (CP437 art: every byte from space up draws a cell)
            if (++col >= 80) row++, col = 0;
        }
        return rows > 0 && row >= rows;
    }
};

// Append the top `rows` screen rows of f (from where f is now) to out, cut at the first 0x1a, at most cap bytes. For a screen
// that keeps only a header's top rows (board 1, 2026-11-16: the theme's 7.7 KB MSGS.ANS was read whole, copied by the code fill
// and sent, for 3 rows that the clear under them kept): the rows are found through a 128-byte buffer on the stack, then that
// many bytes are read in one exact block, so the rest of the file is never held and nothing grows by doubling.
// rows <= 0, or art that never reaches `rows`: the whole file up to cap, still one exact block.
inline void readTopRowsAppend(FILE* f, int rows, size_t cap, std::string& out) {
    const long start = std::ftell(f);
    if (start < 0) return;
    ArtRows       at;
    size_t        len = 0;
    unsigned char b[128];
    for (bool done = false; !done && len < cap;) {
        const size_t n = std::fread(b, 1, std::min(sizeof b, cap - len), f);
        if (!n) break;
        for (size_t i = 0; i < n && !done; i++) {
            if (b[i] == 0x1a) done = true;  // (the SAUCE record: the art has ended)
            else {
                len++;
                done = at.feed(b[i], rows);
            }
        }
    }
    if (!len || std::fseek(f, start, SEEK_SET) != 0) return;
    const size_t old = out.size();
    out.resize(old + len);
    out.resize(old + std::fread(&out[old], 1, len, f));
}

// A follower pulls a file from the main in pieces of `want` bytes, up to cap. ask(offset, reply) sends one "T" request and
// fills reply ('1' then the piece, or '0' for none); false when it went unanswered. *answered: every request was answered
// (an empty file is a real "none"). reserve: the whole cap is reserved up front (one block: the pieces do not double it).
template <class Ask>
std::string assembleArt(size_t cap, size_t want, bool reserve, Ask&& ask, bool* answered) {
    std::string all;
    if (reserve) all.reserve(cap);
    *answered = false;
    for (size_t off = 0; all.size() < cap;) {
        std::string reply;
        if (!ask(off, reply) || reply.empty()) {
            *answered = false;  // (a piece went missing: not a whole answer)
            return all;
        }
        *answered = true;
        if (reply[0] != '1') break;
        reply.erase(0, 1);  // (in place: the piece is the reply)
        const std::string& chunk = reply;
        if (chunk.empty()) break;
        all += chunk;
        if (chunk.size() < want) break;  // (a short piece is the end; an older main that ignores want sends 7000, never short before the end)
        off += chunk.size();
    }
    if (all.size() > cap) all.resize(cap);
    return all;
}

// The follower's theme art, at most cap bytes in all: a hit moves the entry to the back, the front (least recently used) goes first.
struct ArtCache {
    explicit ArtCache(size_t capBytes) : cap(capBytes) {}
    const size_t                                     cap;
    std::vector<std::pair<std::string, std::string>> v;
    size_t                                           bytes = 0;
    void clear() { v.clear(), v.shrink_to_fit(), bytes = 0; }
    const std::string* find(const std::string& name) {
        for (size_t i = 0; i < v.size(); i++)
            if (v[i].first == name) {
                if (i + 1 < v.size()) std::rotate(v.begin() + i, v.begin() + i + 1, v.end());
                return &v.back().second;
            }
        return nullptr;
    }
    void put(const std::string& name, const std::string& art) {
        for (size_t i = 0; i < v.size(); i++)  // the old entry goes first: a bigger version that is too big to keep must not leave the stale one
            if (v[i].first == name) {
                bytes -= v[i].second.size();
                v.erase(v.begin() + i);
                break;
            }
        if (art.size() > cap) return;  // (too big to keep: asked again each time)
        while (!v.empty() && bytes + art.size() > cap) {
            bytes -= v.front().second.size();
            v.erase(v.begin());
        }
        v.emplace_back(name, art);
        bytes += art.size();
    }
};

}  // namespace axis::bbs
