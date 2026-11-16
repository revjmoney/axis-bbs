// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Net state: board cards, Rev's signed notices, seen-post dedupe and per-board ingress limits.
#include "net/state.h"

#include <algorithm>
#include <cstdlib>

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

// value = "yes=<1..20>,no=<1..20>"
bool parseVotes(const std::string& v, uint32_t& yes, uint32_t& no) {
    static const std::string a = "yes=", b = ",no=";
    if (v.compare(0, a.size(), a) != 0) return false;
    size_t mid = v.find(b);
    if (mid == std::string::npos || mid < a.size()) return false;
    if (!parseU32(v.substr(a.size(), mid - a.size()), yes) || !parseU32(v.substr(mid + b.size()), no)) return false;
    return yes >= 1 && yes <= kMaxVotesNeeded && no >= 1 && no <= kMaxVotesNeeded;
}

enum class Group { None, Post, Board, Member, Limits, Votes, Rotate };

const char* groupName(Group g) {
    switch (g) {
        case Group::Post: return "post";
        case Group::Board: return "board";
        case Group::Member: return "member";
        case Group::Limits: return "limits";
        case Group::Votes: return "votes";
        case Group::Rotate: return "rotate";
        default: return "";
    }
}

// The group is decided by the kind and the shape of the target. None means rejected.
Group groupOf(const NetNotice& n) {
    if (n.kind == "limits") return n.target == "net" ? Group::Limits : Group::None;
    if (n.kind == "votes") return n.target == "net" ? Group::Votes : Group::None;
    if (n.kind == "rotate") return (n.target == "0" || n.target == "1") ? Group::Rotate : Group::None;
    if (n.target.find('-') != std::string::npos) {
        if (n.kind == "hold" || n.kind == "kill" || n.kind == "lift") return Group::Post;
        return Group::None;
    }
    if (isLowerHex(n.target, 16)) {
        if (n.kind == "ban" || n.kind == "lift") return Group::Board;
        if (n.kind == "admit" || n.kind == "deny" || n.kind == "veto") return Group::Member;
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
        if (c.seq <= it->second.c.seq) return false;
        it->second.c = c;
        it->second.touched = ++counter_;
        return true;
    }
    if (cards_.size() >= kNetMaxCards) {
        // Full: the unpinned card added or refreshed longest ago makes room, so a flood cannot lock
        // others out. Cards carrying a current Rev key, and those the owner pins (this board, its
        // link peers), never go; if every slot is pinned the new card is refused.
        auto oldest = cards_.end();
        for (auto i = cards_.begin(); i != cards_.end(); ++i) {
            const NetCard& k = i->second.c;
            if (k.pub == revKey(0) || k.pub == revKey(1) || (pinned_ && pinned_(k.board))) continue;
            if (oldest == cards_.end() || i->second.touched < oldest->second.touched) oldest = i;
        }
        if (oldest == cards_.end()) return false;
        cards_.erase(oldest);
    }
    CardRec r;
    r.c = c;
    r.touched = ++counter_;
    cards_[c.board] = r;
    return true;
}

// The two Rev keys in force for a notice with this seq: the built-ins with every rotate
// whose seq is below it applied, in order.
bool NetState::keysAt(uint32_t seq, std::string out[2]) const {
    out[0] = builtinRevKey(0);
    out[1] = builtinRevKey(1);
    for (const Entry& r : rotates_)
        if (r.n.seq < seq) out[1 - r.n.key] = r.n.value;
    return true;
}

bool NetState::verifyAt(const NetNotice& n) const {
    if (n.key != 0 && n.key != 1) return false;
    if (encode(n).empty() || !isLowerHex(n.sig, 128)) return false;
    std::string keys[2];
    keysAt(n.seq, keys);
    const std::string& key = keys[n.key];
    if (!isLowerHex(key, 64) || allZero(key)) return false;
    return verifyHex(key, signedPart(n), n.sig);
}

void NetState::capGroup(const std::string& group, size_t cap) {
    for (;;) {
        size_t count = 0;
        auto victim = notices_.end();
        for (auto i = notices_.begin(); i != notices_.end(); ++i) {
            if (i->first.first != group) continue;
            ++count;
            if (victim == notices_.end()) {
                victim = i;
                continue;
            }
            // Board group: lifts go before bans. Otherwise the oldest applied goes first.
            bool iLift = i->second.n.kind == "lift", vLift = victim->second.n.kind == "lift";
            if (group == "board" && iLift != vLift) {
                if (iLift) victim = i;
            } else if (i->second.order < victim->second.order) {
                victim = i;
            }
        }
        if (count <= cap || victim == notices_.end()) return;
        notices_.erase(victim);
    }
}

// After a rotate lands, anything stored that the new chain no longer vouches for is dropped,
// so live state matches what a reload would accept. Nothing earlier is restored.
void NetState::reverify() {
    bool dropped = false;
    for (size_t i = 0; i < rotates_.size();) {
        if (verifyAt(rotates_[i].n)) {
            ++i;
        } else {
            rotates_.erase(rotates_.begin() + static_cast<long>(i));
            dropped = true;
        }
    }
    for (auto it = notices_.begin(); it != notices_.end();) {
        if (verifyAt(it->second.n)) {
            ++it;
        } else {
            it = notices_.erase(it);
            dropped = true;
        }
    }
    if (notices_.find(std::make_pair(std::string("limits"), std::string("net"))) == notices_.end()) {
        posts_ = kDefaultPostsPerDay;
        bytes_ = kDefaultBytesPerDay;
    }
    if (notices_.find(std::make_pair(std::string("votes"), std::string("net"))) == notices_.end()) {
        votesYes_ = kDefaultVotesYes;
        votesNo_ = kDefaultVotesNo;
    }
    if (dropped) {
        highSeq_ = 0;
        for (const Entry& r : rotates_) highSeq_ = std::max(highSeq_, r.n.seq);
        for (const auto& kv : notices_) highSeq_ = std::max(highSeq_, kv.second.n.seq);
    }
    std::string keys[2];
    keysAt(0xFFFFFFFFu, keys);
    setRevKey(0, keys[0]);
    setRevKey(1, keys[1]);
}

// Accepted risk: nothing here bounds seq. A signing key that leaks before Rev rotates it is a
// full compromise; the recovery is a firmware update carrying new built-in keys.
bool NetState::applyNotice(const NetNotice& n, uint32_t nowDay) {
    Group g = groupOf(n);
    if (g == Group::None) return false;
    if (!verifyAt(n)) return false;

    if (g == Group::Rotate) {
        if (rotates_.size() >= kMaxRotates) return false;
        if (n.target != (n.key == 0 ? "1" : "0")) return false;
        if (!isLowerHex(n.value, 64) || allZero(n.value)) return false;
        for (const Entry& r : rotates_)
            if (r.n.seq == n.seq) return false;
        std::string keys[2];
        keysAt(n.seq, keys);
        if (n.value == keys[n.key]) return false;  // would give both slots the same key
        Entry e;
        e.n = n;
        e.day = nowDay;
        e.order = ++counter_;
        auto pos = rotates_.begin();
        while (pos != rotates_.end() && pos->n.seq < n.seq) ++pos;
        rotates_.insert(pos, e);
        if (n.seq > highSeq_) highSeq_ = n.seq;
        if (n.seq > usedSeq_) usedSeq_ = n.seq;
        reverify();
        return true;
    }

    const auto key = std::make_pair(std::string(groupName(g)), n.target);
    auto it = notices_.find(key);
    if (it != notices_.end()) {
        if (n.seq <= it->second.n.seq) return false;  // replay or stale
        if (g == Group::Post && it->second.n.kind == "kill" && n.kind != "kill")
            return false;  // a kill is final
    }
    uint32_t posts = 0, bytes = 0;
    if (g == Group::Limits && !parseLimits(n.value, posts, bytes)) return false;
    uint32_t yes = 0, no = 0;
    if (g == Group::Votes && !parseVotes(n.value, yes, no)) return false;
    if (g == Group::Member) {
        // admit and deny name the application seq; a veto names none.
        uint32_t appSeq = 0;
        if (n.kind == "veto" ? !n.value.empty() : (!parseU32(n.value, appSeq) || appSeq == 0)) return false;
        if (it == notices_.end()) {
            // Full: a new target is refused. Never evicted, the tally depends on every seal.
            size_t count = 0;
            for (auto i = notices_.lower_bound(std::make_pair(std::string("member"), std::string()));
                 i != notices_.end() && i->first.first == "member"; ++i)
                ++count;
            if (count >= kMaxMemberNotices) return false;
        }
    }

    Entry e;
    e.n = n;
    e.day = nowDay;
    e.order = ++counter_;
    notices_[key] = e;
    if (g == Group::Limits) {
        posts_ = posts;
        bytes_ = bytes;
    } else if (g == Group::Votes) {
        votesYes_ = yes;
        votesNo_ = no;
    } else if (g == Group::Board && n.kind == "ban") {
        cards_.erase(n.target);  // the ban notice alone records the ban
    }
    if (g == Group::Post) capGroup("post", kMaxPostNotices);
    if (g == Group::Board) capGroup("board", kMaxBoardNotices);
    if (n.seq > highSeq_) highSeq_ = n.seq;
    if (n.seq > usedSeq_) usedSeq_ = n.seq;
    return true;
}

void NetState::restamp(uint32_t nowDay) {
    if (nowDay < kNetClockDay) return;
    for (Entry& r : rotates_)
        if (r.day < kNetClockDay) r.day = nowDay;
    for (auto& kv : notices_)
        if (kv.second.day < kNetClockDay) kv.second.day = nowDay;
}

void NetState::prune(uint32_t nowDay) {
    restamp(nowDay);
    for (auto i = notices_.begin(); i != notices_.end();) {
        if (i->first.first == "post" && nowDay > i->second.day &&
            nowDay - i->second.day > kPostNoticeDays)
            i = notices_.erase(i);
        else
            ++i;
    }
}

bool NetState::banned(const std::string& board) const {
    auto it = notices_.find(std::make_pair(std::string("board"), board));
    return it != notices_.end() && it->second.n.kind == "ban";
}
const NetNotice* NetState::memberNotice(const std::string& board) const {
    auto it = notices_.find(std::make_pair(std::string("member"), board));
    return it == notices_.end() ? nullptr : &it->second.n;
}
uint32_t NetState::memberNoticeDay(const std::string& board) const {
    auto it = notices_.find(std::make_pair(std::string("member"), board));
    return it == notices_.end() ? 0 : it->second.day;
}
void NetState::forEachMemberNotice(const std::function<void(const std::string& board, const NetNotice&)>& f) const {
    for (auto i = notices_.lower_bound(std::make_pair(std::string("member"), std::string()));
         i != notices_.end() && i->first.first == "member"; ++i)
        f(i->first.second, i->second.n);
}
bool NetState::held(const std::string& postId) const {
    auto it = notices_.find(std::make_pair(std::string("post"), postId));
    return it != notices_.end() && it->second.n.kind == "hold";
}
bool NetState::killed(const std::string& postId) const {
    auto it = notices_.find(std::make_pair(std::string("post"), postId));
    return it != notices_.end() && it->second.n.kind == "kill";
}

const NetCard* NetState::card(const std::string& board) const {
    auto it = cards_.find(board);
    return it == cards_.end() ? nullptr : &it->second.c;
}

void NetState::forEachCard(const std::function<void(const NetCard&)>& f) const {
    for (const auto& kv : cards_) f(kv.second.c);
}

std::string NetState::noticeLine(const std::string& group, const std::string& target) const {
    auto it = notices_.find(std::make_pair(group, target));
    return it == notices_.end() ? std::string() : encode(it->second.n);
}

std::string NetState::rotateLine(uint32_t seq) const {
    for (const Entry& r : rotates_)
        if (r.n.seq == seq) return encode(r.n);
    return "";
}

std::string noticeGroup(const NetNotice& n) { return groupName(groupOf(n)); }

std::vector<std::string> NetState::snapshot() const {
    std::vector<std::string> out;
    for (const Entry& r : rotates_) {
        std::string l = encode(r.n);
        if (!l.empty()) out.push_back(l);
    }
    for (const auto& kv : cards_) {
        std::string l = encode(kv.second.c);
        if (!l.empty()) out.push_back(l);
    }
    for (const auto& kv : notices_) {
        std::string l = encode(kv.second.n);
        if (!l.empty()) out.push_back(l);
    }
    return out;
}

std::string NetState::save() const {
    std::string out = "H " + std::to_string(usedSeq()) + "\n";  // the highest seq ever applied (load skips it as a record)
    for (const Entry& r : rotates_) {
        std::string l = encode(r.n);
        if (!l.empty()) out += std::to_string(r.day) + " " + l + "\n";
    }
    for (const auto& kv : cards_) {
        std::string l = encode(kv.second.c);
        if (!l.empty()) out += l + "\n";
    }
    for (const auto& kv : notices_) {
        std::string l = encode(kv.second.n);
        if (!l.empty()) out += std::to_string(kv.second.day) + " " + l + "\n";
    }
    return out;
}

void NetState::load(const std::string& text, uint32_t nowDay) {
    cards_.clear();
    notices_.clear();
    rotates_.clear();
    highSeq_ = 0;
    posts_ = kDefaultPostsPerDay;
    bytes_ = kDefaultBytesPerDay;
    votesYes_ = kDefaultVotesYes;
    votesNo_ = kDefaultVotesNo;
    setRevKey(0, builtinRevKey(0));
    setRevKey(1, builtinRevKey(1));

    std::vector<std::pair<NetNotice, uint32_t>> pending;
    for (std::string line : splitLines(text)) {
        uint32_t day = nowDay;
        if (line[0] >= '0' && line[0] <= '9') {
            size_t sp = line.find(' ');
            uint32_t d = 0;
            if (sp == std::string::npos || !parseU32(line.substr(0, sp), d)) continue;
            day = d;
            line = line.substr(sp + 1);
            if (line.empty()) continue;
        }
        if (line.size() > kNetMaxFrame) continue;
        uint32_t used = 0;
        if (line.compare(0, 2, "H ") == 0 && parseU32(line.substr(2), used)) {
            if (used > usedSeq_) usedSeq_ = used;
            continue;
        }
        if (line[0] == static_cast<char>(NetKind::Card)) {
            NetCard c;
            if (decode(line, c)) addCard(c);
        } else if (line[0] == static_cast<char>(NetKind::Notice)) {
            NetNotice n;
            if (decode(line, n)) pending.emplace_back(n, day);
        }
    }
    // In seq order, ties kept in file order. An insertion sort: a few hundred notices at most, and
    // std::stable_sort's merge code costs about 10 KB of the board's flash.
    for (size_t i = 1; i < pending.size(); i++)
        for (size_t j = i; j > 0 && pending[j].first.seq < pending[j - 1].first.seq; j--)
            std::swap(pending[j], pending[j - 1]);
    for (const auto& p : pending) applyNotice(p.first, p.second);
    prune(nowDay);
}

// ---- NetSeen ----

namespace {

// "<16 hex board>-<seq>" as numbers.
bool seenId(const std::string& id, uint64_t& board, uint32_t& seq) {
    if (id.size() < 18 || id[16] != '-' || !isLowerHex(id.substr(0, 16), 16)) return false;
    board = std::strtoull(id.substr(0, 16).c_str(), nullptr, 16);
    return parseU32(id.substr(17), seq);
}

std::string hex16(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; i--, v >>= 4) s[static_cast<size_t>(i)] = d[v & 15];
    return s;
}

}  // namespace

bool NetSeen::has(const std::string& id) const {
    uint64_t board;
    uint32_t seq;
    if (!seenId(id, board, seq)) return false;
    auto it = boards_.find(board);
    if (it == boards_.end()) return false;
    const Rec& r = it->second;
    if (seq > r.high) return false;
    if (seq == r.high) return true;
    uint32_t d = r.high - seq;
    if (d > kSeenWindow) return true;  // below the window: counted as seen
    return (r.window >> (d - 1)) & 1u;
}

bool NetSeen::belowWindow(const std::string& id) const {
    uint64_t board;
    uint32_t seq;
    if (!seenId(id, board, seq)) return false;
    auto it = boards_.find(board);
    return it != boards_.end() && seq < it->second.high && it->second.high - seq > kSeenWindow;
}

void NetSeen::put(uint64_t board, const Rec& r) {
    if (boards_.find(board) == boards_.end() && boards_.size() >= kNetMaxSeenBoards) {
        auto lru = boards_.begin();
        for (auto i = boards_.begin(); i != boards_.end(); ++i)
            if (i->second.used < lru->second.used) lru = i;
        boards_.erase(lru);
    }
    boards_[board] = r;
}

bool NetSeen::add(const std::string& id, uint32_t nowDay) {
    uint64_t board;
    uint32_t seq;
    if (!seenId(id, board, seq) || has(id)) return false;
    Rec r;
    auto it = boards_.find(board);
    if (it == boards_.end()) {
        r.high = seq;
    } else {
        r = it->second;
        if (seq > r.high) {
            uint32_t shift = seq - r.high;  // the old high moves to bit shift - 1
            if (shift > kSeenWindow) r.window = 0;
            else if (shift == kSeenWindow) r.window = 1ull << (kSeenWindow - 1);
            else r.window = (r.window << shift) | (1ull << (shift - 1));
            r.high = seq;
        } else {
            r.window |= 1ull << (r.high - seq - 1);
        }
    }
    r.day = nowDay;
    r.used = ++counter_;
    put(board, r);
    return true;
}

void NetSeen::restamp(uint32_t nowDay) {
    if (nowDay < kNetClockDay) return;
    for (auto& kv : boards_)
        if (kv.second.day < kNetClockDay) kv.second.day = nowDay;
}

void NetSeen::prune(uint32_t nowDay) {
    restamp(nowDay);
    for (auto i = boards_.begin(); i != boards_.end();) {
        if (nowDay > i->second.day && nowDay - i->second.day > kSeenDays) i = boards_.erase(i);
        else ++i;
    }
}

std::string NetSeen::save() const {
    std::string out;
    for (const auto& kv : boards_)
        out += hex16(kv.first) + " " + std::to_string(kv.second.high) + " " + hex16(kv.second.window) + " " +
               std::to_string(kv.second.day) + "\n";
    return out;
}

void NetSeen::load(const std::string& text) {
    boards_.clear();
    counter_ = 0;
    for (const std::string& line : splitLines(text)) {
        // four fields: board, high, window, day
        size_t a = line.find(' '), b = a == std::string::npos ? a : line.find(' ', a + 1),
               c = b == std::string::npos ? b : line.find(' ', b + 1);
        if (c == std::string::npos || line.find(' ', c + 1) != std::string::npos) continue;
        std::string board = line.substr(0, a), window = line.substr(b + 1, c - b - 1);
        Rec r;
        if (!isLowerHex(board, 16) || !isLowerHex(window, 16)) continue;
        if (!parseU32(line.substr(a + 1, b - a - 1), r.high) || !parseU32(line.substr(c + 1), r.day)) continue;
        r.window = std::strtoull(window.c_str(), nullptr, 16);
        r.used = ++counter_;
        put(std::strtoull(board.c_str(), nullptr, 16), r);  // bounded: the earliest lines go first
    }
}

// ---- Ingress ----

// Full: other days' records go first, then the one used longest ago, so a flood of boards can never
// lock the rest out (it costs the evicted board at most a fresh count).
Ingress::Rec* Ingress::rec(const std::string& board, uint32_t nowDay) {
    auto it = recs_.find(board);
    if (it == recs_.end()) {
        if (recs_.size() >= kNetMaxIngressBoards) {
            for (auto i = recs_.begin(); i != recs_.end();) {
                if (i->second.day != nowDay) i = recs_.erase(i);
                else ++i;
            }
        }
        if (recs_.size() >= kNetMaxIngressBoards) {
            auto lru = recs_.begin();
            for (auto i = recs_.begin(); i != recs_.end(); ++i)
                if (i->second.used < lru->second.used) lru = i;
            recs_.erase(lru);
        }
        it = recs_.emplace(board, Rec()).first;
        it->second.day = nowDay;
    }
    it->second.used = ++counter_;
    if (it->second.day != nowDay) {
        uint64_t used = it->second.used;
        it->second = Rec();
        it->second.day = nowDay;
        it->second.used = used;
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
