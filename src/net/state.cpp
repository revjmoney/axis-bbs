// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net state: board cards, Rev's signed notices, seen-post dedupe and per-board ingress limits.
#include "net/state.h"

#include <algorithm>

#include "net/revkeys.h"
#include "net/sign.h"

namespace axis::net {

namespace {

bool isLowerHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool allZero(const std::string& s) {
    for (char c : s)
        if (c != '0') return false;
    return true;
}

// Strict decimal: digits only, no leading zeros except "0" itself, fits uint32.
bool parseU32(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<uint64_t>(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

// value = "posts=<n>,bytes=<n>"
bool parseLimits(const std::string& v, uint32_t& posts, uint32_t& bytes) {
    static const std::string a = "posts=", b = ",bytes=";
    if (v.compare(0, a.size(), a) != 0) return false;
    size_t mid = v.find(b);
    if (mid == std::string::npos || mid < a.size()) return false;
    return parseU32(v.substr(a.size(), mid - a.size()), posts) &&
           parseU32(v.substr(mid + b.size()), bytes);
}

enum class Group { None, Post, Board, Limits, Rotate };

const char* groupName(Group g) {
    switch (g) {
        case Group::Post: return "post";
        case Group::Board: return "board";
        case Group::Limits: return "limits";
        case Group::Rotate: return "rotate";
        default: return "";
    }
}

// The group is decided by the kind and the shape of the target. None means rejected.
Group groupOf(const NetNotice& n) {
    if (n.kind == "limits") return n.target == "net" ? Group::Limits : Group::None;
    if (n.kind == "rotate") return n.target == "net" ? Group::Rotate : Group::None;
    if (n.target.find('-') != std::string::npos) {
        if (n.kind == "hold" || n.kind == "kill" || n.kind == "lift") return Group::Post;
        return Group::None;
    }
    if (isLowerHex(n.target, 16)) {
        if (n.kind == "ban" || n.kind == "lift") return Group::Board;
        return Group::None;
    }
    return Group::None;
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i <= text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string line = text.substr(i, j - i);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) out.push_back(line);
        i = j + 1;
    }
    return out;
}

}  // namespace

// ---- NetState ----

bool NetState::addCard(const NetCard& c) {
    if (!verifyCard(c)) return false;
    if (c.board.empty() || boardId(c.pub) != c.board) return false;
    if (banned(c.board)) return false;
    auto it = cards_.find(c.board);
    if (it != cards_.end()) {
        if (c.seq <= it->second.seq) return false;
        it->second = c;
        return true;
    }
    if (cards_.size() >= kNetMaxCards) return false;
    cards_[c.board] = c;
    return true;
}

bool NetState::applyNotice(const NetNotice& n) {
    if (!verifyNotice(n)) return false;
    return applyNoticeInner(n);
}

bool NetState::applyNoticeInner(const NetNotice& n) {
    Group g = groupOf(n);
    if (g == Group::None) return false;
    const auto key = std::make_pair(std::string(groupName(g)), n.target);
    auto it = notices_.find(key);
    if (it != notices_.end() && n.seq <= it->second.seq) return false;  // replay or stale

    uint32_t posts = 0, bytes = 0;
    switch (g) {
        case Group::Post:
            if (killed_.count(n.target)) return false;  // a kill is final
            break;
        case Group::Limits:
            if (!parseLimits(n.value, posts, bytes)) return false;
            break;
        case Group::Rotate:
            if (n.key != 0 && n.key != 1) return false;
            if (!isLowerHex(n.value, 64) || allZero(n.value)) return false;
            break;
        default: break;
    }

    switch (g) {
        case Group::Post:
            if (n.kind == "hold") {
                held_.insert(n.target);
            } else if (n.kind == "kill") {
                killed_.insert(n.target);
                held_.erase(n.target);
            } else {
                held_.erase(n.target);  // lift
            }
            break;
        case Group::Board:
            if (n.kind == "ban") banned_.insert(n.target);
            else banned_.erase(n.target);
            break;
        case Group::Limits:
            posts_ = posts;
            bytes_ = bytes;
            break;
        case Group::Rotate:
            setRevKey(1 - n.key, n.value);
            break;
        default: return false;
    }
    notices_[key] = n;
    if (n.seq > highSeq_) highSeq_ = n.seq;
    return true;
}

bool NetState::banned(const std::string& board) const { return banned_.count(board) != 0; }
bool NetState::held(const std::string& postId) const { return held_.count(postId) != 0; }
bool NetState::killed(const std::string& postId) const { return killed_.count(postId) != 0; }

const NetCard* NetState::card(const std::string& board) const {
    auto it = cards_.find(board);
    return it == cards_.end() ? nullptr : &it->second;
}

std::vector<std::string> NetState::snapshot() const {
    std::vector<std::string> out;
    for (const auto& kv : cards_) {
        std::string l = encode(kv.second);
        if (!l.empty()) out.push_back(l);
    }
    for (const auto& kv : notices_) {
        std::string l = encode(kv.second);
        if (!l.empty()) out.push_back(l);
    }
    return out;
}

std::string NetState::save() const {
    std::string out;
    bool rotated = false;
    for (int i = 0; i < 2; ++i)
        if (revKey(i) != builtinRevKey(i)) rotated = true;
    if (rotated)
        for (int i = 0; i < 2; ++i) out += "K" + std::to_string(i) + " " + revKey(i) + "\n";
    for (const std::string& l : snapshot()) out += l + "\n";
    return out;
}

void NetState::load(const std::string& text) {
    cards_.clear();
    notices_.clear();
    banned_.clear();
    held_.clear();
    killed_.clear();
    highSeq_ = 0;
    posts_ = kDefaultPostsPerDay;
    bytes_ = kDefaultBytesPerDay;

    std::vector<NetNotice> pending;
    std::string keys[2];
    for (const std::string& line : splitLines(text)) {
        if (line.size() > kNetMaxFrame + 8) continue;
        if (line[0] == 'K' && line.size() == 3 + 64 && (line[1] == '0' || line[1] == '1') &&
            line[2] == ' ') {
            std::string hex = line.substr(3);
            if (isLowerHex(hex, 64) && !allZero(hex)) keys[line[1] - '0'] = hex;
            continue;
        }
        if (line[0] == static_cast<char>(NetKind::Card)) {
            NetCard c;
            if (decode(line, c)) addCard(c);  // before notices, so a banned board's card loads
        } else if (line[0] == static_cast<char>(NetKind::Notice)) {
            NetNotice n;
            if (decode(line, n)) pending.push_back(n);
        }
    }
    std::stable_sort(pending.begin(), pending.end(),
                     [](const NetNotice& a, const NetNotice& b) { return a.seq < b.seq; });

    // A notice signed by a key that was later rotated out only verifies under the old keys, and
    // one signed by a rotated-in key only under the saved keys, so try both key sets.
    std::vector<NetNotice> later;
    for (const NetNotice& n : pending)
        if (!applyNotice(n)) later.push_back(n);
    for (int i = 0; i < 2; ++i)
        if (!keys[i].empty()) setRevKey(i, keys[i]);
    for (const NetNotice& n : later) applyNotice(n);
}

// ---- NetSeen ----

bool NetSeen::add(const std::string& id, uint32_t nowDay) {
    if (id.empty()) return false;
    if (!ids_.insert(id).second) return false;
    order_.emplace_back(nowDay, id);
    while (order_.size() > kNetMaxSeen) {
        ids_.erase(order_.front().second);
        order_.pop_front();
    }
    return true;
}

void NetSeen::prune(uint32_t nowDay) {
    std::deque<std::pair<uint32_t, std::string>> keep;
    for (auto& e : order_) {
        if (nowDay > e.first && nowDay - e.first > kSeenDays) ids_.erase(e.second);
        else keep.push_back(std::move(e));
    }
    order_.swap(keep);
}

std::string NetSeen::save() const {
    std::string out;
    for (const auto& e : order_) out += std::to_string(e.first) + " " + e.second + "\n";
    return out;
}

void NetSeen::load(const std::string& text) {
    order_.clear();
    ids_.clear();
    for (const std::string& line : splitLines(text)) {
        size_t sp = line.find(' ');
        if (sp == std::string::npos || sp == 0) continue;
        uint32_t day = 0;
        if (!parseU32(line.substr(0, sp), day)) continue;
        std::string id = line.substr(sp + 1);
        if (id.empty() || id.size() > 64 || id.find(' ') != std::string::npos) continue;
        add(id, day);  // bounded: the oldest are dropped first if there are too many
    }
}

// ---- Ingress ----

Ingress::Rec* Ingress::rec(const std::string& board, uint32_t nowDay) {
    auto it = recs_.find(board);
    if (it == recs_.end()) {
        if (recs_.size() >= kNetMaxIngressBoards) {
            for (auto i = recs_.begin(); i != recs_.end();) {
                if (i->second.day != nowDay) i = recs_.erase(i);
                else ++i;
            }
            if (recs_.size() >= kNetMaxIngressBoards) return nullptr;
        }
        it = recs_.emplace(board, Rec()).first;
        it->second.day = nowDay;
    }
    if (it->second.day != nowDay) {
        it->second = Rec();
        it->second.day = nowDay;
    }
    return &it->second;
}

bool Ingress::allow(const std::string& board, size_t bytes, uint32_t nowDay) {
    Rec* r = rec(board, nowDay);
    if (!r) return false;
    if (r->posts >= posts_) return false;
    if (bytes > bytes_ || r->bytes > bytes_ - bytes) return false;
    r->posts += 1;
    r->bytes += static_cast<uint32_t>(bytes);
    return true;
}

bool Ingress::warnOnce(const std::string& board, uint32_t nowDay) {
    Rec* r = rec(board, nowDay);
    if (!r || r->warned) return false;
    r->warned = true;
    return true;
}

}  // namespace axis::net
