// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net areas, NetCore (taking, making, storing and logging net records) and the posting store.
#include "net/areas.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>

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
    // "YYYY-MM-DD HH:MM", read as sscanf("%d-%d-%d %d:%d") would, without sscanf (about 24 KB of
    // flash on the board for this one call).
    long        v[5];
    const char* p = text.c_str();
    for (int i = 0; i < 5; i++) {
        char* e = nullptr;
        v[i] = std::strtol(p, &e, 10);
        if (e == p) return 0;
        p = e;
        if (i == 0 || i == 1 || i == 3) {
            if (*p != (i == 3 ? ':' : '-')) return 0;
            p++;
        }
    }
    int y = (int)v[0], mo = (int)v[1], d = (int)v[2], h = (int)v[3], mi = (int)v[4];
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
    // This board's own card and its link peers' cards are never evicted for room.
    state_.setPinned([this](const std::string& b) { return b == myBoard_ || links_.count(b) > 0; });
    syncLimits();
}

bool NetCore::isRevBoard(const std::string& board) const {
    const NetCard* c = state_.card(board);
    if (!c || c->pub.empty()) return false;
    return (!revKey(0).empty() && c->pub == revKey(0)) || (!revKey(1).empty() && c->pub == revKey(1));
}

void NetCore::log(const std::string& entry) {
    if (log_.append(entry) == kOutLogFail) appendFailures_++;
    if (log_.next() - log_.first() > kOutLogKeep + kOutLogSlack) log_.trimTo(kOutLogKeep);
}

// Moves the body and the line into the stored message, so the post is held once. The line comes
// back after the store (for the net record); the body does not.
int NetCore::storePost(NetPost& p, std::string& line) {
    if (areaIndex(p.area) < 0) return 0;
    Message m;
    m.from = p.from;
    m.to = p.to;
    m.subject = p.subject;
    uint32_t date = p.date;
    if (clockSet() && date > now_()) date = now_();  // a post from the future ages from today
    m.date = netDateText(date);
    m.body = std::move(p.body);
    m.net = std::move(line);
    p.body.clear();
    line.clear();
    int num = store_.post(p.area, m);
    line = std::move(m.net);
    return num;
}

bool netNakTransient(const std::string& word) { return word == kNakNoCard || word == kNakStore; }

NetCore::Took NetCore::take(NetPost p, const std::string& fromPeer, bool history, const char** nak) {
    syncLimits();  // the host may have loaded the state directly
    // Every result goes to the net record; a refusal also gives its NAK word.
    auto res = [&](Took t, const char* why, const char* word) {
        if (nak) *nak = word;
        if (onEvent_) onEvent_(fromPeer, std::string("post ") + why + " " + p.id, std::string());
        return t;
    };
    if (nak) *nak = nullptr;
    // 1. decode limits: the record must be representable and signed, and (with a clock) in date
    if (p.sig.empty() || p.body.size() > kNetMaxBody || encode(p).empty()) return res(Took::Rejected, "rejected bad", kNakBad);
    if (clockSet()) {
        uint64_t now = now_(), date = p.date;
        if (date + (uint64_t)keepDays_ * 86400 < now || date > now + 86400) return res(Took::Rejected, "rejected date", kNakDate);
    }
    // 2. banned board
    if (state_.banned(p.board)) return res(Took::Banned, "banned", kNakBanned);
    if (blockedHere(p.board)) return res(Took::Rejected, "rejected blocked here", kNakRejected);
    // 3. a known card, and the post signed by its key
    const NetCard* c = state_.card(p.board);
    // No card is transient (the peer sends it, or it comes later): the sender keeps the post for another session.
    if (!c) return res(Took::Rejected, "rejected signature", kNakNoCard);
    if (!verifyPost(p, c->pub)) return res(Took::Rejected, "rejected signature", kNakSig);
    // 4. a Sysop Net area, and Rev's board only for the revOnly ones
    const NetArea* area = sysopNetArea(p.area);
    if (!area) return res(Took::Rejected, "rejected area", kNakBad);
    if (area->revOnly && !isRevBoard(p.board)) return res(Took::Rejected, "rejected area", kNakRevOnly);
    // 5. killed
    if (state_.killed(p.id)) return res(Took::Rejected, "rejected killed", kNakKilled);
    // 6. dedupe (it is marked seen only once stored, below). A history post below the seen window
    // (more than 64 seqs under its origin's highest) is not known from the window alone: it is a
    // duplicate only if its area already holds it. Only history gets this look, so a peer cannot make
    // every E line cost an area scan.
    if (seen_.has(p.id) && !(history && seen_.belowWindow(p.id) && !stored(p.area, p.id)))
        return res(Took::Duplicate, "duplicate", nullptr);
    // 7. ingress, counting the body size (history pulls are capped by the session instead)
    uint32_t today = day();
    if (!history && !ingress_.allow(p.board, p.body.size(), today)) return res(Took::Limited, "limited", kNakLimited);

    std::string cameWith = p.path;
    if (!fromPeer.empty()) p.path = appendHop(p.path, fromPeer);
    std::string line = encode(p);
    if (line.empty()) {  // the hop is not a valid path entry: keep the path as it came
        p.path = cameWith;
        line = encode(p);
    }
    cameWith.clear();
    if (line.empty()) return res(Took::Rejected, "rejected path", kNakBad);
    int num = storePost(p, line);  // the body is moved out here
    if (!num) return res(Took::Rejected, "rejected store", kNakStore);
    seen_.add(p.id, today);
    if (onEvent_) onEvent_(fromPeer, "post stored " + p.id + " " + p.area, line);
    log("P " + p.area + " " + std::to_string(num) + " " + p.id + " " + excludeList({fromPeer, p.path, p.board}));
    return Took::Stored;
}

bool NetCore::takeCard(const NetCard& c, const std::string& fromPeer) {
    if (!state_.addCard(c)) return false;
    log("C " + c.board + " " + excludeList({fromPeer, c.board}));
    if (onEvent_) onEvent_(fromPeer, "card " + c.board + " " + std::to_string(c.seq), encode(c));
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
    if (onEvent_) onEvent_(fromPeer, "notice " + n.kind + " " + n.target + " seq " + std::to_string(n.seq), encode(n));
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
    if (onEvent_) onEvent_(std::string(), "post made " + p.id + " " + area, line);
    return p.id;
}

size_t NetCore::poolSize(bool outbound) const {
    size_t n = 0;
    for (const auto& kv : links_)
        if (kv.second.outbound == outbound) n++;
    return n;
}

// Makes room for one more in a pool: the one in that pool synced longest ago goes, never the
// first peer and never `keep`. False if nothing could go.
bool NetCore::makeRoom(bool outbound, const std::string& keep) {
    if (poolSize(outbound) < (outbound ? kMaxOutboundPeers : kMaxInboundPeers)) return true;
    auto oldest = links_.end();
    for (auto i = links_.begin(); i != links_.end(); ++i)
        if (i->second.outbound == outbound && i->first != firstPeer_ && i->first != keep &&
            (oldest == links_.end() || i->second.lastSync < oldest->second.lastSync))
            oldest = i;
    if (oldest == links_.end()) return false;
    links_.erase(oldest);
    return true;
}

void NetCore::addLinkPeer(const std::string& board, bool outbound) {
    if (!isHex16(board) || board == myBoard_) return;
    auto it = links_.find(board);
    if (it != links_.end()) {
        if (outbound && !it->second.outbound && makeRoom(true, board)) it->second.outbound = true;
        it->second.lastSync = now_();
        return;
    }
    if (!makeRoom(outbound, "")) return;
    Link l;
    // Starts at the log's end. If the log is not open yet, the top value means the same thing:
    // pendingFor brings a cursor past next() back to next().
    l.cursor = log_.ok() ? log_.next() : kOutLogFail;
    l.lastSync = now_();
    l.outbound = outbound;
    links_[board] = l;
}

void NetCore::setFirstPeer(const std::string& board) {
    if (!isHex16(board) || board == myBoard_) return;
    firstPeer_ = board;
    addLinkPeer(board, true);
}

// What to send for a log entry. Skip only when it is excluded for this peer or confirmed gone;
// Retry when the post could not be read (the caller stops there and tries again later).
NetCore::Res NetCore::resolve(const std::string& entry, const std::string& peer, std::string& line) {
    line.clear();
    std::vector<std::string> t = tokens(entry);
    if (t.empty() || inList(t.back(), peer)) return Res::Skip;
    if (t[0] == "P" && t.size() == 5) {
        const std::string& id = t[3];
        if (id.size() < 17 || state_.killed(id) || state_.banned(id.substr(0, 16)) || !sysopNetArea(t[1]))
            return Res::Skip;
        Message m;
        if (!store_.read(t[1], std::atoi(t[2].c_str()), m)) return Res::Retry;
        if (netPostId(m.net) != id) return Res::Skip;  // that number now holds another post
        line = std::move(m.net);
        return Res::Ready;  // m and its body are released here
    }
    if (t[0] == "C" && t.size() == 3) {
        const NetCard* c = state_.card(t[1]);
        if (c && !state_.banned(t[1])) line = encode(*c);
    } else if (t[0] == "N" && t.size() == 4) {
        uint32_t seq;
        if (t[1] != "R") line = state_.noticeLine(t[1], t[2]);
        else if (parseSeq(t[2], seq)) line = state_.rotateLine(seq);
    }
    return line.empty() ? Res::Skip : Res::Ready;  // not held any more (or not an entry): gone
}

std::vector<std::pair<uint32_t, std::string>> NetCore::pendingFor(const std::string& peer, size_t max) {
    std::vector<std::pair<uint32_t, std::string>> out;
    auto it = links_.find(peer);
    if (it == links_.end() || !log_.ok()) return out;  // a log not open yet leaves cursors alone
    Link& link = it->second;
    if (link.cursor < log_.first()) link.cursor = log_.first();  // fell behind: what was trimmed is lost
    if (link.cursor > log_.next()) link.cursor = log_.next();
    std::string entry, line;
    size_t bytes = 0;
    for (uint32_t i = link.cursor; i < log_.next() && out.size() < max; i++) {
        if (!log_.read(i, entry)) break;  // try again next time
        Res r = resolve(entry, peer, line);
        if (r == Res::Retry) {
            // A post that cannot be read. Usually a short failure; but a post trimmed away from its
            // area reads the same, so after kNetReadStrikes calls stuck on it, it counts as gone.
            if (link.stuckAt != i) {
                link.stuckAt = i;
                link.strikes = 0;
            }
            if (++link.strikes < kNetReadStrikes) break;
            r = Res::Skip;
        } else if (link.stuckAt == i) {
            link.stuckAt = 0xFFFFFFFFu;  // it read fine: only failures in a row count
            link.strikes = 0;
        }
        if (r == Res::Skip) {
            if (out.empty()) link.cursor = i + 1;  // nothing here for this peer: never look at it again
            continue;
        }
        if (!out.empty() && bytes + line.size() > kNetPendingBytes) break;
        bytes += line.size();
        out.emplace_back(i, std::move(line));
    }
    return out;
}

void NetCore::acked(const std::string& peer, uint32_t index) {
    auto it = links_.find(peer);
    if (it == links_.end() || !log_.ok() || index >= log_.next()) return;
    if (index + 1 > it->second.cursor) it->second.cursor = index + 1;
    if (it->second.nakAt <= index) {  // past the entry that held it: those strikes are over
        it->second.nakAt = 0xFFFFFFFFu;
        it->second.nakStrikes = 0;
    }
}

bool NetCore::nakedTransient(const std::string& peer, uint32_t index) {
    auto it = links_.find(peer);
    if (it == links_.end()) return false;
    Link& l = it->second;
    if (l.nakAt != index) {
        l.nakAt = index;
        l.nakStrikes = 0;
    }
    if (++l.nakStrikes < kNetNakStrikes) return false;
    acked(peer, index);  // held it three sessions in a row: it cannot go to this peer
    return true;
}

std::string NetCore::saveCursors() const {
    std::string out;
    for (const auto& kv : links_)
        out += kv.first + " " + std::to_string(kv.second.cursor) + " " + std::to_string(kv.second.lastSync) +
               (kv.second.outbound ? " out\n" : " in\n");
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
        if (links_.count(t[0])) continue;
        if (t.size() > 3) {
            if (t[3] != "out" && t[3] != "in") continue;
            l.outbound = t[3] == "out";
        } else {
            l.outbound = poolSize(true) < kMaxOutboundPeers;  // an older file without pools
        }
        if (poolSize(l.outbound) >= (l.outbound ? kMaxOutboundPeers : kMaxInboundPeers)) continue;
        links_[t[0]] = l;
    }
}

void NetCore::dropPost(const std::string& id) {
    for (const NetArea& a : sysopNetAreas()) {  // each area's headers, one at a time, up to the first hit
        int num = 0;
        store_.scanHeaders(a.id, [&](const Message& m) {
            if (netPostId(m.net) != id) return true;
            num = m.num;
            return false;
        });
        if (num) {
            store_.drop(a.id, [&](const Message& m) { return m.num == num && netPostId(m.net) == id; });
            return;
        }
    }
    // Not found (a store whose scan sees only the newest headers): the next trim drops it.
}

void NetCore::trimArea(const std::string& area, uint32_t nowDay, bool clock) {
    auto gone = [&](const Message& m) {
        std::string id = netPostId(m.net);
        if (!id.empty() && state_.killed(id)) return true;
        if (!clock) return false;  // no clock: ages mean nothing, only the count trims
        uint32_t t = netDateUnix(m.date);
        if (t == 0) return false;
        uint32_t d = t / 86400;
        // Posts stored with a clock are dated no later than the day they came in (storePost), so
        // one dated past tomorrow came in without a clock and a bad date: it goes now.
        if (d > nowDay + 1) return true;
        return nowDay > d && nowDay - d > keepDays_;
    };
    // A count pass over the headers, one at a time, then one rewrite only if something goes: the killed and
    // old posts, and the oldest of the rest past keepPosts. (gone reads only the header fields, so both passes agree.)
    size_t total = 0, going = 0;
    store_.scanHeaders(area, [&](const Message& m) {
        total++;
        if (gone(m)) going++;
        return true;
    });
    size_t kept = total - going, excess = kept > keepPosts_ ? kept - keepPosts_ : 0, n = 0;
    if (!going && !excess) return;
    store_.drop(area, [&](const Message& m) { return gone(m) || n++ < excess; });
}

void NetCore::trim(uint32_t nowUnix) {
    uint32_t nowDay = nowUnix / 86400;
    if (nowDay == lastTrimDay_) return;  // once a day: the area rewrites below touch the SD card
    lastTrimDay_ = nowDay;
    state_.prune(nowDay);
    seen_.prune(nowDay);
    log_.trimTo(kOutLogKeep);
    for (const NetArea& a : sysopNetAreas()) trimArea(a.id, nowDay, nowUnix > kNetClockSet);
}

bool NetCore::historyNext(HistoryCursor& c, uint32_t sinceUnix, std::string& line) {
    line.clear();
    const std::vector<NetArea>& areas = sysopNetAreas();
    if (!c.loaded) {
        // Each area's newest kNetHistoryPerArea net posts by a header scan, as (date, seq, area, number):
        // 16 bytes each, at most 800 (about 13 KB) for a moment, sorted, then cut to 4 bytes each.
        struct Pick {
            uint32_t date, seq, area;
            int num;
        };
        std::vector<Pick> all;
        for (size_t a = 0; a < areas.size(); a++) {
            size_t from = all.size();
            store_.scanHeaders(areas[a].id, [&](const Message& m) {
                std::string id = netPostId(m.net);
                uint32_t seq = 0;
                if (id.size() < 18 || !parseSeq(id.substr(17), seq) || m.num <= 0 || m.num >= (1 << 28)) return true;
                // The stored date is minute text and never later than the post's own date, so a
                // minute of slack keeps posts from the edge of the window.
                uint32_t date = netDateUnix(m.date);
                if (sinceUnix && date + 60 < sinceUnix) return true;
                if (all.size() - from == kNetHistoryPerArea) all.erase(all.begin() + (long)from);  // keep the newest
                all.push_back(Pick{date, seq, (uint32_t)a, m.num});
                return true;
            });
        }
        // Oldest first, ties by seq then area and number. An insertion sort: each area's run is already
        // about in order, and it costs no flash for a sort template.
        auto before = [](const Pick& x, const Pick& y) {
            if (x.date != y.date) return x.date < y.date;
            if (x.seq != y.seq) return x.seq < y.seq;
            if (x.area != y.area) return x.area < y.area;
            return x.num < y.num;
        };
        for (size_t i = 1; i < all.size(); i++)
            for (size_t j = i; j > 0 && before(all[j], all[j - 1]); j--) std::swap(all[j], all[j - 1]);
        c.picks.clear();
        c.picks.reserve(all.size());
        for (const Pick& p : all) c.picks.push_back(p.area << 28 | (uint32_t)p.num);
        std::vector<Pick>().swap(all);
        c.pos = 0;
        c.loaded = true;
    }
    while (c.pos < c.picks.size()) {
        uint32_t pick = c.picks[c.pos++];
        size_t a = pick >> 28;
        Message m;
        if (a >= areas.size() || !store_.read(areas[a].id, (int)(pick & 0x0FFFFFFFu), m)) continue;
        std::string id = netPostId(m.net);
        if (id.size() < 17 || state_.held(id) || state_.killed(id) || state_.banned(id.substr(0, 16))) continue;
        line = std::move(m.net);
        return true;
    }
    std::vector<uint32_t>().swap(c.picks);
    return false;
}

bool NetCore::stored(const std::string& area, const std::string& id) {
    bool found = false;
    store_.scanHeaders(area, [&](const Message& m) { return !(found = netPostId(m.net) == id); });
    return found;
}

bool NetCore::hasNetPosts() const {
    bool found = false;
    for (const NetArea& a : sysopNetAreas()) {
        store_.scanHeaders(a.id, [&](const Message& m) { return !(found = !netPostId(m.net).empty()); });
        if (found) return true;
    }
    return false;
}

// ---- NetPostingStore ----

bool NetPostingStore::hidden(const Message& m) const {
    std::string id = netPostId(m.net);
    return !id.empty() && (core_.state().held(id) || core_.state().killed(id) || core_.hiddenHere(id));
}

std::vector<Message> NetPostingStore::headers(const std::string& area) {
    std::vector<Message> v = inner_.headers(area);
    if (!isNetAreaId(area)) return v;
    v.erase(std::remove_if(v.begin(), v.end(), [&](const Message& m) { return hidden(m); }), v.end());
    return v;
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
