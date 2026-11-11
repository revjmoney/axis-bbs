// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net areas, NetCore (taking, making, storing and logging net records) and the posting store.
#include "net/areas.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>
#include <set>

#include "net/revkeys.h"

namespace axis::net {

using bbs::Message;
using bbs::MessageStore;

namespace {

const char kSep = '\x1F';

bool isHex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool parseSeq(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10 || (s.size() > 1 && s[0] == '0')) return false;
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = (uint32_t)v;
    return true;
}

// One line of at most max characters, with no separator or line break.
std::string cleanLine(const std::string& s, size_t max) {
    std::string o = s.substr(0, max);
    for (char& c : o)
        if (c == '\n' || c == '\r' || c == kSep) c = ' ';
    return o;
}

// Adds a hop to a path; if it would pass 512 characters, the oldest hops go first.
std::string appendHop(const std::string& path, const std::string& hop) {
    std::string s = path.empty() ? hop : path + "," + hop;
    while (s.size() > 512) {
        size_t c = s.find(',');
        if (c == std::string::npos) return path;
        s.erase(0, c + 1);
    }
    return s;
}

// True if peer is one of the comma separated entries of list.
bool inList(const std::string& list, const std::string& peer) {
    size_t pos = 0;
    while (pos <= list.size()) {
        size_t e = list.find(',', pos);
        if (e == std::string::npos) e = list.size();
        if (e - pos == peer.size() && list.compare(pos, e - pos, peer) == 0) return true;
        pos = e + 1;
    }
    return false;
}

// A token safe inside a log entry: no spaces, commas or line breaks.
bool plainToken(const std::string& s) {
    return !s.empty() && s.size() <= 40 && s.find_first_of(" ,\t\r\n") == std::string::npos;
}

// The exclude field: the given boards (each may itself be a comma list, a path), or "-".
std::string excludeList(std::initializer_list<std::string> boards) {
    std::string out;
    for (const std::string& b : boards) {
        size_t pos = 0;
        while (pos < b.size()) {
            size_t e = b.find(',', pos);
            if (e == std::string::npos) e = b.size();
            std::string one = b.substr(pos, e - pos);
            pos = e + 1;
            if (plainToken(one) && !inList(out, one)) out += (out.empty() ? "" : ",") + one;
        }
    }
    return out.empty() ? "-" : out;
}

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> t;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find(' ', pos);
        if (e == std::string::npos) e = s.size();
        if (e > pos) t.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    return t;
}

int areaIndex(const std::string& id) {
    const auto& a = sysopNetAreas();
    for (size_t i = 0; i < a.size(); i++)
        if (a[i].id == id) return (int)i;
    return -1;
}

}  // namespace

// ---- areas and helpers ----

const std::vector<NetArea>& sysopNetAreas() {
    static const std::vector<NetArea> areas = {
        {"sn_support", "Sysop Net: Support", false},
        {"sn_announce", "Sysop Net: Announcements", true},
        {"sn_updates", "Sysop Net: Update Notices", true},
        {"sn_bugs", "Sysop Net: Bug Reports", false},
    };
    return areas;
}

const NetArea* sysopNetArea(const std::string& id) {
    int i = areaIndex(id);
    return i < 0 ? nullptr : &sysopNetAreas()[(size_t)i];
}

bool isNetAreaId(const std::string& area) { return area.compare(0, 3, "sn_") == 0; }

std::string netPostId(const std::string& net) {
    if (net.size() < 3 || net[0] != (char)NetKind::Post || net[1] != kSep) return "";
    size_t e = net.find(kSep, 2);
    return net.substr(2, e == std::string::npos ? std::string::npos : e - 2);
}

std::string netDateText(uint32_t unixTime) {
    std::time_t t = (std::time_t)unixTime;
    std::tm* tm = std::localtime(&t);
    char b[24];
    if (!tm || !std::strftime(b, sizeof b, "%Y-%m-%d %H:%M", tm)) return std::to_string(unixTime);
    return b;
}

uint32_t netDateUnix(const std::string& text) {
    int y, mo, d, h, mi;
    if (std::sscanf(text.c_str(), "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) != 5) return 0;
    std::tm tm = {};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    return t <= 0 ? 0 : (uint32_t)t;
}

// ---- NetCore ----

NetCore::NetCore(MessageStore& store, KeyPair me, std::function<uint32_t()> nowUnix, OutLog& log)
    : store_(store), me_(me), now_(std::move(nowUnix)), log_(log) {
    myBoard_ = boardId(toHex(me_.pub, 32));
    syncLimits();
}

bool NetCore::isRevBoard(const std::string& board) const {
    const NetCard* c = state_.card(board);
    if (!c || c->pub.empty()) return false;
    return (!revKey(0).empty() && c->pub == revKey(0)) || (!revKey(1).empty() && c->pub == revKey(1));
}

void NetCore::log(const std::string& entry) {
    log_.append(entry);
    if (log_.next() - log_.first() > kOutLogKeep + kOutLogSlack) log_.trimTo(kOutLogKeep);
}

int NetCore::storePost(const NetPost& p, const std::string& line) {
    if (areaIndex(p.area) < 0) return 0;
    Message m;
    m.from = p.from;
    m.to = p.to;
    m.subject = p.subject;
    m.date = netDateText(p.date);
    m.body = p.body;
    m.net = line;
    return store_.post(p.area, m);
}

NetCore::Took NetCore::take(const NetPost& in, const std::string& fromPeer) {
    syncLimits();  // the host may have loaded the state directly
    // 1. decode limits: the record must be representable and signed
    if (in.sig.empty() || in.body.size() > kNetMaxBody || encode(in).empty()) return Took::Rejected;
    // 2. banned board
    if (state_.banned(in.board)) return Took::Banned;
    // 3. a known card, and the post signed by its key
    const NetCard* c = state_.card(in.board);
    if (!c || !verifyPost(in, c->pub)) return Took::Rejected;
    // 4. a Sysop Net area, and Rev's board only for the revOnly ones
    const NetArea* area = sysopNetArea(in.area);
    if (!area || (area->revOnly && !isRevBoard(in.board))) return Took::Rejected;
    // 5. killed
    if (state_.killed(in.id)) return Took::Rejected;
    // 6. dedupe (it is marked seen only once stored, below)
    if (seen_.has(in.id)) return Took::Duplicate;
    // 7. ingress, counting the body size
    uint32_t today = day();
    if (!ingress_.allow(in.board, in.body.size(), today)) return Took::Limited;

    NetPost p = in;
    if (!fromPeer.empty()) p.path = appendHop(p.path, fromPeer);
    std::string line = encode(p);
    if (line.empty()) {  // the hop is not a valid path entry: keep the path as it came
        p.path = in.path;
        line = encode(p);
    }
    if (line.empty()) return Took::Rejected;
    int num = storePost(p, line);
    if (!num) return Took::Rejected;
    seen_.add(p.id, today);
    log("P " + p.area + " " + std::to_string(num) + " " + p.id + " " + excludeList({fromPeer, p.path, p.board}));
    return Took::Stored;
}

bool NetCore::takeCard(const NetCard& c, const std::string& fromPeer) {
    if (!state_.addCard(c)) return false;
    log("C " + c.board + " " + excludeList({fromPeer, c.board}));
    return true;
}

bool NetCore::takeNotice(const NetNotice& n, const std::string& fromPeer) {
    bool changed = state_.applyNotice(n, day());
    syncLimits();
    if (!changed) return false;
    std::string group = noticeGroup(n);
    if (group == "rotate") log("N R " + std::to_string(n.seq) + " " + excludeList({fromPeer}));
    else if (plainToken(n.target)) log("N " + group + " " + n.target + " " + excludeList({fromPeer}));
    if (n.kind == "kill") dropPost(n.target);
    return true;
}

bool NetCore::setOwnCard(const NetCard& c) {
    if (c.board != myBoard_) return false;
    return takeCard(c, "");
}

bool NetCore::originateNotice(const NetNotice& n) { return takeNotice(n, ""); }

std::string NetCore::originate(const std::string& area, const Message& m, int* num) {
    if (num) *num = 0;
    const NetArea* a = sysopNetArea(area);
    if (!a) return "";
    const NetCard* own = state_.card(myBoard_);
    if (!own) return "";
    if (a->revOnly && !isRevBoard(myBoard_)) return "";
    if (m.body.size() > kNetMaxBody) return "";

    NetPost p;
    p.id = myBoard_ + "-" + std::to_string(seq_);
    p.area = area;
    p.board = myBoard_;
    p.from = cleanLine(m.from, 36);
    p.to = cleanLine(m.to, 36);
    p.subject = cleanLine(m.subject, kNetMaxSubject);
    p.body = m.body;
    for (char& ch : p.body)
        if (ch == kSep) ch = ' ';
    p.date = now_();
    if (!signPost(p, me_) || !verifyPost(p, own->pub)) return "";
    std::string line = encode(p);
    if (line.empty()) return "";

    // The id is used up from here, stored or not, so a retry never reuses it.
    seq_ = seq_ == 0xFFFFFFFFu ? 1 : seq_ + 1;
    if (seen_.has(p.id)) return "";  // the counter was set back
    int n = storePost(p, line);
    if (!n) return "";
    seen_.add(p.id, day());
    if (num) *num = n;
    log("P " + area + " " + std::to_string(n) + " " + p.id + " " + excludeList({myBoard_}));
    return p.id;
}

void NetCore::addLinkPeer(const std::string& board) {
    if (!isHex16(board) || board == myBoard_) return;
    auto it = links_.find(board);
    if (it != links_.end()) {
        it->second.lastSync = now_();
        return;
    }
    if (links_.size() >= kMaxLinkPeers) {
        auto oldest = links_.begin();
        for (auto i = links_.begin(); i != links_.end(); ++i)
            if (i->second.lastSync < oldest->second.lastSync) oldest = i;
        links_.erase(oldest);
    }
    Link l;
    l.cursor = log_.next();
    l.lastSync = now_();
    links_[board] = l;
}

// The line to send for a log entry, or "" if it is excluded for this peer or no longer resolves.
std::string NetCore::resolve(const std::string& entry, const std::string& peer) {
    std::vector<std::string> t = tokens(entry);
    if (t.empty() || inList(t.back(), peer)) return "";
    if (t[0] == "P" && t.size() == 5) {
        if (state_.killed(t[3]) || !sysopNetArea(t[1])) return "";
        Message m;
        if (!store_.read(t[1], std::atoi(t[2].c_str()), m) || netPostId(m.net) != t[3]) return "";
        return m.net;
    }
    if (t[0] == "C" && t.size() == 3) {
        const NetCard* c = state_.card(t[1]);
        return c && !state_.banned(t[1]) ? encode(*c) : std::string();
    }
    if (t[0] == "N" && t.size() == 4) {
        if (t[1] == "R") {
            uint32_t seq;
            return parseSeq(t[2], seq) ? state_.rotateLine(seq) : std::string();
        }
        return state_.noticeLine(t[1], t[2]);
    }
    return "";
}

std::vector<std::pair<uint32_t, std::string>> NetCore::pendingFor(const std::string& peer, size_t max) {
    std::vector<std::pair<uint32_t, std::string>> out;
    auto it = links_.find(peer);
    if (it == links_.end()) return out;
    uint32_t& cur = it->second.cursor;
    if (cur < log_.first()) cur = log_.first();  // fell behind the log: what was trimmed is lost
    if (cur > log_.next()) cur = log_.next();
    std::string entry;
    for (uint32_t i = cur; i < log_.next() && out.size() < max; i++) {
        std::string line = log_.read(i, entry) ? resolve(entry, peer) : std::string();
        if (line.empty()) {
            if (out.empty()) cur = i + 1;  // nothing here for this peer: never look at it again
            continue;
        }
        out.emplace_back(i, std::move(line));
    }
    return out;
}

void NetCore::acked(const std::string& peer, uint32_t index) {
    auto it = links_.find(peer);
    if (it == links_.end() || index >= log_.next()) return;
    if (index + 1 > it->second.cursor) it->second.cursor = index + 1;
}

std::string NetCore::saveCursors() const {
    std::string out;
    for (const auto& kv : links_)
        out += kv.first + " " + std::to_string(kv.second.cursor) + " " + std::to_string(kv.second.lastSync) + "\n";
    return out;
}

void NetCore::loadCursors(const std::string& text) {
    links_.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> t = tokens(line);
        Link l;
        if (t.size() < 2 || !isHex16(t[0]) || t[0] == myBoard_ || !parseSeq(t[1], l.cursor)) continue;
        if (t.size() > 2 && !parseSeq(t[2], l.lastSync)) continue;
        if (links_.size() >= kMaxLinkPeers && !links_.count(t[0])) continue;
        links_[t[0]] = l;
    }
}

void NetCore::dropPost(const std::string& id) {
    for (const NetArea& a : sysopNetAreas()) {  // look in each area's newest headers
        std::vector<Message> h = store_.headers(a.id);
        int num = 0;
        for (const Message& m : h)
            if (netPostId(m.net) == id) num = m.num;
        h.clear();
        h.shrink_to_fit();
        if (num) {
            store_.drop(a.id, [&](const Message& m) { return m.num == num && netPostId(m.net) == id; });
            return;
        }
    }
    // Older than the newest headers: the next trim drops it.
}

void NetCore::trimArea(const std::string& area, uint32_t nowDay) {
    auto gone = [&](const Message& m) {
        std::string id = netPostId(m.net);
        if (!id.empty() && state_.killed(id)) return true;
        uint32_t t = netDateUnix(m.date);
        uint32_t d = t / 86400;
        return t != 0 && nowDay > d && nowDay - d > keepDays_;
    };
    std::vector<Message> h = store_.headers(area);
    if (h.size() < MessageStore::kMaxHeaders) {  // the whole area is here: decide from the headers
        std::set<int> drop;
        size_t excess = h.size() > keepPosts_ ? h.size() - keepPosts_ : 0;
        for (size_t i = 0; i < h.size(); i++)
            if (i < excess || gone(h[i])) drop.insert(h[i].num);
        h.clear();
        h.shrink_to_fit();
        if (!drop.empty()) store_.drop(area, [&](const Message& m) { return drop.count(m.num) > 0; });
        return;
    }
    h.clear();
    h.shrink_to_fit();
    // Possibly more than the headers show: stream it. Killed and old first, counting what stays.
    // FileMessageStore::drop writes a temp copy of the area even when nothing goes, so a full area
    // costs one SD rewrite per trim; trim runs at most once a day.
    size_t kept = 0;
    store_.drop(area, [&](const Message& m) {
        if (gone(m)) return true;
        kept++;
        return false;
    });
    if (kept > keepPosts_) {
        size_t excess = kept - keepPosts_, n = 0;
        store_.drop(area, [&](const Message&) { return n++ < excess; });
    }
}

void NetCore::trim(uint32_t nowUnix) {
    uint32_t nowDay = nowUnix / 86400;
    if (nowDay == lastTrimDay_) return;  // once a day: the area rewrites below touch the SD card
    lastTrimDay_ = nowDay;
    state_.prune(nowDay);
    seen_.prune(nowDay);
    log_.trimTo(kOutLogKeep);
    for (const NetArea& a : sysopNetAreas()) trimArea(a.id, nowDay);
}

// ---- NetPostingStore ----

bool NetPostingStore::hidden(const Message& m) const {
    std::string id = netPostId(m.net);
    return !id.empty() && (core_.state().held(id) || core_.state().killed(id));
}

std::vector<Message> NetPostingStore::headers(const std::string& area) {
    std::vector<Message> v = inner_.headers(area);
    if (!isNetAreaId(area)) return v;
    std::vector<Message> out;
    out.reserve(v.size());
    for (Message& m : v)
        if (!hidden(m)) out.push_back(std::move(m));
    return out;
}

bool NetPostingStore::read(const std::string& area, int num, Message& out) {
    if (!inner_.read(area, num, out)) return false;
    if (isNetAreaId(area) && hidden(out)) {
        out = Message();
        return false;
    }
    return true;
}

int NetPostingStore::post(const std::string& area, const Message& m) {
    if (!isNetAreaId(area)) return inner_.post(area, m);
    int num = 0;
    return core_.originate(area, m, &num).empty() ? 0 : num;
}

int NetPostingStore::drop(const std::string& area, const std::function<bool(const Message&)>& gone) {
    return inner_.drop(area, gone);
}

}  // namespace axis::net
