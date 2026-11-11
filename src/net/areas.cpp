// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net areas, NetCore (taking, making, storing and queueing net records) and the posting store.
#include "net/areas.h"

#include <cstdio>
#include <ctime>

#include "net/revkeys.h"

namespace axis::net {

using bbs::Message;
using bbs::MessageStore;

namespace {

const char kSep = '\x1F';

bool hexVal(char c, uint64_t& v) {
    if (c >= '0' && c <= '9') v = (uint64_t)(c - '0');
    else if (c >= 'a' && c <= 'f') v = (uint64_t)(c - 'a' + 10);
    else return false;
    return true;
}

bool parseHex16(const std::string& s, uint64_t& out) {
    if (s.size() != 16) return false;
    out = 0;
    for (char c : s) {
        uint64_t v;
        if (!hexVal(c, v)) return false;
        out = (out << 4) | v;
    }
    return true;
}

std::string hex16(uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; i--, v >>= 4) s[(size_t)i] = d[v & 15];
    return s;
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

// "<16 hex board>-<seq>"
bool parsePostId(const std::string& id, uint64_t& board, uint32_t& seq) {
    return id.size() > 17 && id[16] == '-' && parseHex16(id.substr(0, 16), board) && parseSeq(id.substr(17), seq);
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

bool onPath(const std::string& path, const std::string& peer) {
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t e = path.find(',', pos);
        if (e == std::string::npos) e = path.size();
        if (path.compare(pos, e - pos, peer) == 0 && e - pos == peer.size()) return true;
        pos = e + 1;
    }
    return false;
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

NetCore::NetCore(MessageStore& store, KeyPair me, std::function<uint32_t()> nowUnix)
    : store_(store), me_(me), now_(std::move(nowUnix)) {
    myBoard_ = boardId(toHex(me_.pub, 32));
    syncLimits();
}

bool NetCore::isRevBoard(const std::string& board) const {
    const NetCard* c = state_.card(board);
    if (!c || c->pub.empty()) return false;
    return (!revKey(0).empty() && c->pub == revKey(0)) || (!revKey(1).empty() && c->pub == revKey(1));
}

void NetCore::notePeer(const std::string& peer) {
    if (peer.empty() || peer == myBoard_ || peer.size() > 40 || peer.find_first_of(" \t\r\n") != std::string::npos)
        return;
    if (state_.card(peer) || extraPeers_.count(peer) || extraPeers_.size() >= kNetExtraPeers) return;
    extraPeers_.insert(peer);
}

void NetCore::forEachPeer(const std::function<void(const std::string&)>& f) const {
    state_.forEachCard([&](const NetCard& c) {
        if (c.board != myBoard_) f(c.board);
    });
    for (const std::string& p : extraPeers_)
        if (!state_.card(p)) f(p);
}

void NetCore::push(const std::string& peer, const QKey& k, bool dedupe) {
    std::deque<QKey>& q = queues_[peer];
    if (dedupe)
        for (const QKey& e : q)
            if (e == k) return;
    q.push_back(k);
    while (q.size() > kNetQueueCap) q.pop_front();
}

void NetCore::queuePost(const NetPost& p, const std::string& fromPeer) {
    QKey k;
    k.kind = 'P';
    if (!parsePostId(p.id, k.board, k.seq)) return;
    forEachPeer([&](const std::string& peer) {
        if (peer == fromPeer || peer == p.board || onPath(p.path, peer)) return;
        push(peer, k, false);  // post ids are unique (the seen set lets each in once)
    });
}

void NetCore::rememberLoc(const std::string& id, uint8_t area, int num) {
    PostKey pk;
    if (!parsePostId(id, pk.first, pk.second)) return;
    auto it = locs_.find(pk);
    if (it != locs_.end()) {
        it->second.area = area;
        it->second.num = num;
        return;
    }
    Loc l;
    l.area = area;
    l.num = num;
    locs_[pk] = l;
    locOrder_.push_back(pk);
    while (locOrder_.size() > kNetPostLocs) {
        locs_.erase(locOrder_.front());
        locOrder_.pop_front();
    }
}

void NetCore::warmLocs() {
    const auto& areas = sysopNetAreas();
    for (size_t i = 0; i < areas.size(); i++) {
        std::vector<Message> h = store_.headers(areas[i].id);
        for (const Message& m : h) {
            std::string id = netPostId(m.net);
            if (!id.empty()) rememberLoc(id, (uint8_t)i, m.num);
        }
    }
}

int NetCore::storePost(const NetPost& p, const std::string& line) {
    int a = areaIndex(p.area);
    if (a < 0) return 0;
    Message m;
    m.from = p.from;
    m.to = p.to;
    m.subject = p.subject;
    m.date = netDateText(p.date);
    m.body = p.body;
    m.net = line;
    int num = store_.post(p.area, m);
    if (num) rememberLoc(p.id, (uint8_t)a, num);
    return num;
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
    // 6. dedupe (marked seen from here on, even if ingress or the store refuses it below)
    uint32_t today = day();
    if (!seen_.add(in.id, today)) return Took::Duplicate;
    // 7. ingress, counting the body size
    if (!ingress_.allow(in.board, in.body.size(), today)) return Took::Limited;

    notePeer(fromPeer);
    NetPost p = in;
    if (!fromPeer.empty()) p.path = appendHop(p.path, fromPeer);
    std::string line = encode(p);
    if (line.empty()) {  // the hop is not a valid path entry: keep the path as it came
        p.path = in.path;
        line = encode(p);
    }
    if (line.empty() || !storePost(p, line)) return Took::Rejected;
    queuePost(p, fromPeer);
    return Took::Stored;
}

bool NetCore::takeCard(const NetCard& c, const std::string& fromPeer) {
    if (!state_.addCard(c)) return false;
    notePeer(fromPeer);
    QKey k;
    k.kind = 'C';
    if (!parseHex16(c.board, k.board)) return true;
    forEachPeer([&](const std::string& peer) {
        if (peer == fromPeer || peer == c.board) return;
        push(peer, k, true);  // a newer card replaces the queued one in place
    });
    return true;
}

bool NetCore::takeNotice(const NetNotice& n, const std::string& fromPeer) {
    bool changed = state_.applyNotice(n, day());
    syncLimits();
    if (!changed) return false;
    notePeer(fromPeer);
    std::string line = encode(n);
    if (!line.empty()) {
        noticeLines_[n.seq] = line;
        while (noticeLines_.size() > kNetNoticeLines) noticeLines_.erase(noticeLines_.begin());
        QKey k;
        k.kind = 'N';
        k.seq = n.seq;
        forEachPeer([&](const std::string& peer) {
            if (peer != fromPeer) push(peer, k, false);
        });
    }
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
    bool fresh = seen_.add(p.id, day());
    seq_ = seq_ == 0xFFFFFFFFu ? 1 : seq_ + 1;
    if (!fresh) return "";
    int n = storePost(p, line);
    if (!n) return "";
    if (num) *num = n;
    queuePost(p, "");
    return p.id;
}

std::string NetCore::resolve(const QKey& k, bool& warmed) {
    if (k.kind == 'C') {
        const NetCard* c = state_.card(hex16(k.board));
        return c ? encode(*c) : std::string();
    }
    if (k.kind == 'N') {
        auto it = noticeLines_.find(k.seq);
        return it == noticeLines_.end() ? std::string() : it->second;
    }
    if (k.kind != 'P') return "";
    std::string id = hex16(k.board) + "-" + std::to_string(k.seq);
    if (state_.killed(id)) return "";
    PostKey pk(k.board, k.seq);
    auto it = locs_.find(pk);
    if (it == locs_.end() && !warmed) {
        warmed = true;
        warmLocs();
        it = locs_.find(pk);
    }
    if (it == locs_.end()) return "";
    const auto& areas = sysopNetAreas();
    if (it->second.area >= areas.size()) return "";
    Message m;
    if (!store_.read(areas[it->second.area].id, it->second.num, m) || netPostId(m.net) != id) return "";
    return m.net;
}

std::vector<std::string> NetCore::pendingFor(const std::string& peer, size_t max) {
    std::vector<std::string> out;
    notePeer(peer);
    auto it = queues_.find(peer);
    if (it == queues_.end()) return out;
    bool warmed = false;
    std::deque<QKey>& q = it->second;
    for (auto qi = q.begin(); qi != q.end() && out.size() < max;) {
        std::string line = resolve(*qi, warmed);
        if (line.empty()) {
            qi = q.erase(qi);
            continue;
        }
        out.push_back(std::move(line));
        ++qi;
    }
    if (q.empty()) queues_.erase(it);
    return out;
}

void NetCore::acked(const std::string& peer, const std::string& keyOrId) {
    QKey k;
    if (!parseKey(keyOrId, k) && !parseKey("P:" + keyOrId, k)) return;
    auto it = queues_.find(peer);
    if (it == queues_.end()) return;
    std::deque<QKey>& q = it->second;
    for (auto qi = q.begin(); qi != q.end(); ++qi)
        if (*qi == k) {
            q.erase(qi);
            break;
        }
    if (q.empty()) queues_.erase(it);
}

bool NetCore::parseKey(const std::string& text, QKey& k) {
    if (text.size() < 3 || text[1] != ':') return false;
    std::string v = text.substr(2);
    k = QKey();
    k.kind = text[0];
    if (k.kind == 'P') return parsePostId(v, k.board, k.seq);
    if (k.kind == 'C') return parseHex16(v, k.board);
    if (k.kind == 'N') return parseSeq(v, k.seq);
    return false;
}

std::string NetCore::keyText(const QKey& k) {
    if (k.kind == 'P') return "P:" + hex16(k.board) + "-" + std::to_string(k.seq);
    if (k.kind == 'C') return "C:" + hex16(k.board);
    if (k.kind == 'N') return "N:" + std::to_string(k.seq);
    return "";
}

std::string NetCore::saveQueues() const {
    std::string out;
    for (const auto& kv : queues_)
        for (const QKey& k : kv.second) {
            std::string t = keyText(k);
            if (!t.empty()) out += kv.first + " " + t + "\n";
        }
    return out;
}

void NetCore::loadQueues(const std::string& text) {
    queues_.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        if (sp == std::string::npos || sp == 0 || sp > 40) continue;
        std::string peer = line.substr(0, sp);
        if (peer == myBoard_) continue;
        QKey k;
        if (!parseKey(line.substr(sp + 1), k)) continue;
        push(peer, k, false);  // capped per peer: the oldest go first
    }
}

void NetCore::dropPost(const std::string& id) {
    PostKey pk;
    if (!parsePostId(id, pk.first, pk.second)) return;
    const auto& areas = sysopNetAreas();
    auto it = locs_.find(pk);
    if (it != locs_.end() && it->second.area < areas.size()) {
        int num = it->second.num;
        if (store_.drop(areas[it->second.area].id,
                        [&](const Message& m) { return m.num == num && netPostId(m.net) == id; }))
            return;
    }
    for (const NetArea& a : areas) {  // not cached: look in each area's newest headers
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
    // Older than the newest headers: trim drops it.
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
    size_t kept = 0;
    store_.drop(area, [&](const Message& m) {
        if (gone(m)) return true;
        kept++;
        return false;
    });
    if (kept > keepPosts_) {
        size_t excess = kept - keepPosts_, seen = 0;
        store_.drop(area, [&](const Message&) { return seen++ < excess; });
    }
}

void NetCore::trim(uint32_t nowUnix) {
    uint32_t nowDay = nowUnix / 86400;
    state_.prune(nowDay);
    seen_.prune(nowDay);
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
