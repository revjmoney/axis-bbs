// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/statsshare.h"

namespace axis::bbs {

namespace {
constexpr char kSep = '\x1f';

std::string cleanName(const std::string& h) {  // printable, one line, no field separators, short
    std::string out;
    for (char c : h) {
        if (out.size() >= kMaxCallerName) break;
        if (c >= 0x20 && c < 0x7f) out += c;
    }
    return out;
}

bool count(const std::string& text, uint32_t& out) { return parseStatTotal(text, out) && out <= kMaxBatch; }
}  // namespace

std::string encodeStatsSnapshot(const DayStats& s, const std::string& lastCaller) { return s.format() + cleanName(lastCaller); }

bool decodeStatsSnapshot(const std::string& text, DayStats& s, std::string& lastCaller) {
    size_t nl = text.find('\n');
    if (nl == std::string::npos || text.size() > 200) return false;
    DayStats got;
    if (!got.parse(text.substr(0, nl))) return false;
    s = got;
    lastCaller = cleanName(text.substr(nl + 1));
    return true;
}

std::string encodeStatsBatch(uint32_t id, const StatsCounts& c) {
    return std::string("B") + kSep + std::to_string(id) + kSep + std::to_string(c.calls) + kSep + std::to_string(c.posts) + kSep +
           std::to_string(c.uploads) + kSep + cleanName(c.who);
}

bool decodeStatsBatch(const std::string& req, uint32_t& id, StatsCounts& c) {
    if (req.size() < 2 || req[0] != 'B' || req[1] != kSep || req.size() > 120) return false;
    std::string f[5];
    size_t      pos = 2;
    for (int i = 0; i < 4; i++) {
        size_t e = req.find(kSep, pos);
        if (e == std::string::npos) return false;
        f[i] = req.substr(pos, e - pos);
        pos = e + 1;
    }
    f[4] = req.substr(pos);
    StatsCounts got;
    uint32_t    n = 0;
    if (!parseStatTotal(f[0], n) || !n) return false;
    if (!count(f[1], got.calls) || !count(f[2], got.posts) || !count(f[3], got.uploads)) return false;
    if (f[4] != cleanName(f[4])) return false;
    got.who = f[4];
    id = n;
    c = got;
    return true;
}

void StatsShare::caller(const std::string& handle) {
    std::string h = cleanName(handle);
    if (!h.empty()) pend_.who = h;
}

std::string StatsShare::request(uint32_t freshId) {
    if (!batch_) {
        batch_ = freshId ? freshId : 1;
        out_ = pend_;
        pend_ = StatsCounts();
    }
    return encodeStatsBatch(batch_, out_);
}

bool StatsShare::answer(const std::string& reply) {
    if (!batch_ || reply.empty() || reply[0] != '1') return false;
    if (!heard(reply.substr(1))) return false;
    batch_ = 0;
    out_ = StatsCounts();
    return true;
}

bool StatsShare::heard(const std::string& field) {
    DayStats    s;
    std::string last;
    if (!decodeStatsSnapshot(field, s, last)) return false;
    main_ = s;
    mainLast_ = last;
    known_ = true;
    return true;
}

DayStats StatsShare::shown(const DayStats& base) const {
    DayStats s = base;
    for (const StatsCounts* c : {&out_, &pend_}) {
        s.calls += c->calls;
        s.total += c->calls;
        s.posts += c->posts;
        s.uploads += c->uploads;
    }
    return s;
}

std::string StatsShare::lastCaller() const {
    if (!pend_.who.empty()) return pend_.who;
    if (batch_ && !out_.who.empty()) return out_.who;
    return known_ ? mainLast_ : std::string();
}

StatsInbox::Result StatsInbox::apply(const std::string& req, DayStats& s, std::string& lastCaller) {
    uint32_t    id = 0;
    StatsCounts c;
    if (!decodeStatsBatch(req, id, c)) return Result::Bad;
    for (uint32_t seen : seen_)
        if (seen == id) return Result::Again;
    seen_[next_++ % 8] = id;
    s.calls += c.calls;
    s.total += c.calls;
    s.posts += c.posts;
    s.uploads += c.uploads;
    if (!c.who.empty()) lastCaller = c.who;
    return Result::Applied;
}

}  // namespace axis::bbs
