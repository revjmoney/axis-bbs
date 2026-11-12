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
