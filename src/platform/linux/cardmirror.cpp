// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card mirror's manifest, listing difference, noon test and pace (cardmirror.h).
#include "platform/linux/cardmirror.h"

#include <cstdlib>
#include <set>

namespace {

constexpr char kSep = '\x1f';

bool parseU64(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 19) return false;
    v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    return true;
}

bool parseI64(const std::string& s, int64_t& v) {
    bool     neg = !s.empty() && s[0] == '-';
    uint64_t u;
    if (!parseU64(neg ? s.substr(1) : s, u) || u > (uint64_t)INT64_MAX) return false;
    v = neg ? -(int64_t)u : (int64_t)u;
    return true;
}

}  // namespace

std::string formatManifest(const Manifest& m) {
    std::string out = "#gen=" + std::to_string(m.gen) + "\n#seq=" + std::to_string(m.seq) +
                      "\n#complete=" + (m.complete ? "1" : "0") + "\n#noon=" + m.noon + "\n";
    for (const auto& kv : m.files) {
        const CardEntry& e = kv.second;
        out += e.path;
        out += kSep;
        out += std::to_string(e.size);
        out += kSep;
        out += std::to_string(e.mtime);
        out += '\n';
    }
    return out;
}

bool parseManifest(const std::string& text, Manifest& out) {
    Manifest m;
    size_t   pos = 0;
    while (pos < text.size()) {
        size_t      nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '#' && line.find(kSep) == std::string::npos) {  // (a card file may start with '#': it has fields)
            size_t eq = line.find('=');
            if (eq == std::string::npos) return false;
            std::string k = line.substr(1, eq - 1), v = line.substr(eq + 1);
            uint64_t    n = 0;
            if (k == "gen" || k == "seq") {
                if (!parseU64(v, n) || n > UINT32_MAX) return false;
                (k == "gen" ? m.gen : m.seq) = (uint32_t)n;
            } else if (k == "complete") {
                if (v != "0" && v != "1") return false;
                m.complete = v == "1";
            } else if (k == "noon") {
                m.noon = v;
            }  // (an unknown header is left alone: a later version may add one)
            continue;
        }
        size_t a = line.find(kSep);
        size_t b = a == std::string::npos ? std::string::npos : line.find(kSep, a + 1);
        if (a == std::string::npos || b == std::string::npos || a == 0) return false;
        CardEntry e;
        e.path = line.substr(0, a);
        if (!parseU64(line.substr(a + 1, b - a - 1), e.size) || !parseI64(line.substr(b + 1), e.mtime)) return false;
        m.files[e.path] = e;
    }
    out = std::move(m);
    return true;
}

MirrorDiff diffListing(const Manifest& m, const std::vector<CardEntry>& card) {
    MirrorDiff            d;
    std::set<std::string> onCard;
    for (const CardEntry& e : card) {
        onCard.insert(e.path);
        auto it = m.files.find(e.path);
        if (it == m.files.end() || it->second.size != e.size || it->second.mtime != e.mtime) d.copy.push_back(e);
    }
    for (const auto& kv : m.files)
        if (!onCard.count(kv.first)) d.remove.push_back(kv.first);
    return d;
}

bool noonDue(const std::string& lastNoon, const std::string& today, int nowMinutes, int noonMinutes) {
    return today > lastNoon && nowMinutes >= noonMinutes;
}

Pace::Pace(uint32_t bytesPerSec) : rate_(bytesPerSec ? bytesPerSec : 1) {}

void Pace::refill(uint32_t nowMs) {
    uint32_t rate = busy_ ? (rate_ / 4 ? rate_ / 4 : 1) : rate_;
    if (!started_) {
        started_ = true;
        last_ = nowMs;
        budget_ = rate;
        return;
    }
    uint32_t elapsed = nowMs - last_;
    last_ = nowMs;
    uint64_t milli = (uint64_t)rate * elapsed + carry_;
    budget_ += (int64_t)(milli / 1000);
    carry_ = (uint32_t)(milli % 1000);
    if (budget_ > (int64_t)rate) budget_ = rate, carry_ = 0;  // at most one second's worth saved up
}

bool Pace::take(uint32_t bytes, uint32_t nowMs) {
    refill(nowMs);
    if (budget_ < 0) return false;
    budget_ -= (int64_t)bytes;
    return true;
}
