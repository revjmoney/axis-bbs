// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sysop Net and User Net areas, NetCore (taking, making, storing and logging net records) and the
// posting store.
#include "net/areas.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <initializer_list>

#include "monocypher.h"
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

const std::vector<NetArea>& userNetAreas() {
    static const std::vector<NetArea> areas = {
        {"un_chat", "User Net: General Chat", false},
        {"un_tech", "User Net: Tech Help", false},
        {"un_games", "User Net: Games", false},
        {"un_files", "User Net: Files", false},
    };
    return areas;
}

bool isUserNetArea(const std::string& id) {
    for (const NetArea& a : userNetAreas())
        if (a.id == id) return true;
    return false;
}

const NetArea* netArea(const std::string& id) {
    if (const NetArea* a = sysopNetArea(id)) return a;
    for (const NetArea& a : userNetAreas())
        if (a.id == id) return &a;
    return nullptr;
}

bool isNetAreaId(const std::string& area) { return area.compare(0, 3, "sn_") == 0 || area.compare(0, 3, "un_") == 0; }

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

uint32_t netSeqStart(uint32_t saved, bool found, uint32_t nowUnix) {
    uint32_t floor = !found && nowUnix > kNetClockSet ? nowUnix : 1;
    return saved > floor ? saved : floor;
}

// ---- NetCore ----

NetCore::NetCore(MessageStore& store, const KeyPair& me, std::function<uint32_t()> nowUnix, OutLog& log)
    : store_(store), me_(me), now_(std::move(nowUnix)), log_(log),
      members_(state_, [this](const std::string& b) { return isRevBoard(b); }) {
    myBoard_ = boardId(toHex(me_.pub, 32));
    // This board's own card and its link peers' cards are never evicted for room.
    state_.setPinned([this](const std::string& b) { return b == myBoard_ || links_.count(b) > 0; });
    syncLimits();
}

NetCore::~NetCore() { wipe(me_); }

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
    if (!netArea(p.area)) return 0;
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

bool netNakTransient(std::string_view word) { return word == kNakNoCard || word == kNakStore; }

NetCore::Took NetCore::take(NetPost p, const std::string& fromPeer, bool history, const char** nak) {
    syncLimits();  // the host may have loaded the state directly
    // Every result goes to the net record; a refusal also gives its NAK word.
    auto res = [&](Took t, const char* why, const char* word) {
        if (nak) *nak = word;
        if (onEvent_) onEvent_(fromPeer, std::string("post ") + why + " " + p.id, std::string());
        return t;
    };
    if (nak) *nak = nullptr;
    // The post is copied as little as it can be: the checks below build no line; the line is encoded
    // once, after them, and the body and that line are moved into the message base (storePost).
    // 1. decode limits: the fields valid and signed, and (with a clock) in date
    if (p.body.size() > kNetMaxBody || !validPost(p)) return res(Took::Rejected, "rejected bad", kNakBad);
    if (clockSet()) {
        uint64_t now = now_(), date = p.date;
        if (date + (uint64_t)keepDays_ * 86400 < now || date > now + 86400) return res(Took::Rejected, "rejected date", kNakDate);
    }
    // 2. banned board, 3. a known card, and the post signed by its key
    Refusal r;
    if (refusedOrigin(p.board, [&](const std::string& pub) { return verifyPost(p, pub); }, r)) return res(r.t, r.why, r.word);
    // 4. a net area, and Rev's board only for the revOnly ones. A User Net area only while this board
    // carries it and is a member, from a board that is a member by this board's tally.
    const NetArea* area = netArea(p.area);
    if (!area) return res(Took::Rejected, "rejected area", kNakBad);
    if (area->revOnly && !isRevBoard(p.board)) return res(Took::Rejected, "rejected area", kNakRevOnly);
    if (isUserNetArea(p.area) && refusedUser(p.area, p.board, r)) return res(r.t, r.why, r.word);
    // 5. killed
    if (state_.killed(p.id)) return res(Took::Rejected, "rejected killed", kNakKilled);
    // 6. dedupe (it is marked seen only once stored, below). A history post below the seen window
    // (more than 64 seqs under its origin's highest) is not known from the window alone: it is a
    // duplicate only if its area already holds it. Only history gets this look, so a peer cannot make
    // every E line cost an area scan.
    if (seen_.has(p.id) && !(history && seen_.belowWindow(p.id) && !stored(p.area, p.id)))
        return res(Took::Duplicate, "duplicate", nullptr);
    // 7. the line as it is stored and passed on, with this hop on its path: it must fit a frame
    std::string cameWith = p.path;
    if (!fromPeer.empty()) p.path = appendHop(p.path, fromPeer);
    std::string line = encode(p);
    if (line.empty() && p.path != cameWith) {  // the hop is not a valid path entry: keep the path as it came
        p.path = cameWith;
        line = encode(p);
    }
    std::string().swap(cameWith);
    if (line.empty()) return res(Took::Rejected, "rejected bad", kNakBad);
    // 8. ingress, counting the body size (history pulls are capped by the session instead; Rev's
    // boards are never limited)
    uint32_t today = day();
    if (!history && !isRevBoard(p.board) && !ingress_.allow(p.board, p.body.size(), today)) return res(Took::Limited, "limited", kNakLimited);
    int num = storePost(p, line);  // the body is moved out here
    if (!num) return res(Took::Rejected, "rejected store", kNakStore);
    seen_.add(p.id, today);
    if (onEvent_) onEvent_(fromPeer, "post stored " + p.id + " " + p.area, line);
    log("P " + p.area + " " + std::to_string(num) + " " + p.id + " " + excludeList({fromPeer, p.path, p.board}));
    return Took::Stored;
}

bool NetCore::refusedOrigin(const std::string& board, const std::function<bool(const std::string&)>& signedBy,
                            Refusal& r) const {
    const NetCard* c = state_.card(board);
    if (state_.banned(board)) r = {Took::Banned, "banned", kNakBanned};
    else if (blockedHere(board)) r = {Took::Rejected, "rejected blocked here", kNakRejected};
    // No card is transient (the peer sends it, or it comes later): the sender keeps the record for another session.
    else if (!c) r = {Took::Rejected, "rejected signature", kNakNoCard};
    else if (!signedBy(c->pub)) r = {Took::Rejected, "rejected signature", kNakSig};
    else return false;
    return true;
}

// Both refusals are permanent: a transient one would hold the sender's whole cursor, Sysop Net posts included.
bool NetCore::refusedUser(const std::string& area, const std::string& board, Refusal& r) const {
    if (!carries(area) || !selfMember()) r = {Took::Rejected, "rejected area", kNakArea};
    else if (members_.status(board) != NetMembers::Status::Member) r = {Took::Rejected, "rejected member", kNakMember};
    else return false;
    return true;
}

// ---- Net Files listings ----

void NetCore::eachListing(const std::function<bool(const Message&, const NetListing&)>& each) {
    store_.scanHeaders(kNetFilesArea, [&](const Message& m) {
        NetListing l;
        return !decode(m.net, l) || each(m, l);  // junk (a sysop's move into the area) is passed over
    });
}

bool NetCore::shown(const NetListing& l) const {
    return !l.gone && !state_.banned(l.board) && !state_.held(listingId(l)) && !state_.killed(listingId(l));
}

// ---- the listing index (/net/files.idx) ----

namespace {
constexpr uint32_t kIdxMagic = 0x3358464Eu;  // "NFX3"
constexpr uint32_t kIdxHead = sizeof(uint32_t) * 11, kIdxRec = sizeof(uint32_t) * 6;
constexpr size_t kIdxChunk = 16;   // records read at once (384 bytes)
constexpr size_t kIdxConfirm = 4;  // records a take reads back at most; past that it is refused "store"
}  // namespace

uint32_t NetCore::idxHash(const std::string& s) const {
    uint32_t h = 0;
    crypto_blake2b_keyed((uint8_t*)&h, sizeof h, (const uint8_t*)hd_.key, sizeof hd_.key, (const uint8_t*)s.data(), s.size());
    return h;
}

NetCore::IdxRec NetCore::idxRec(const NetListing& l, const Message& m) const {
    return IdxRec{idxHash(l.board + "|" + l.area + "|" + l.name), idxHash(l.board), l.seq, (uint32_t)m.num,
                  l.gone ? 1u : 0u, m.at};
}

// The head as held in RAM, clean (1) or not (0: a change to the area is under way, or failed).
bool NetCore::idxHead(bool clean) {
    hd_.magic = kIdxMagic;
    hd_.clean = clean ? 1u : 0u;
    if (idxWrite_ && idxWrite_(0, &hd_, sizeof hd_)) return true;
    idx_ = Idx::Unknown;
    return false;
}

bool NetCore::idxReady() {
    if (idx_ == Idx::Unknown) listingIndexStep();  // a cheap look at the head; a rebuild goes on from the host's tick
    return idx_ == Idx::Ready;
}

bool NetCore::idxEach(const std::function<bool(const IdxRec&)>& each) {
    static_assert(sizeof(IdxRec) == kIdxRec && sizeof(IdxHead) == kIdxHead, "index records are words");
    IdxRec buf[kIdxChunk];
    for (uint32_t i = 0; i < hd_.count;) {
        uint32_t n = std::min<uint32_t>(kIdxChunk, hd_.count - i);
        if (!idxRead_(kIdxHead + i * kIdxRec, buf, n * kIdxRec)) {
            idx_ = Idx::Unknown;
            return false;
        }
        for (uint32_t k = 0; k < n; k++)
            if (!each(buf[k])) return true;
        i += n;
    }
    return true;
}

// The area from the last message the head knows: exactly that one, ending where the head says.
bool NetCore::idxTailOk() {
    uint32_t pos = hd_.hasLast ? hd_.lastAt : 0, seen = 0;
    bool ok = true;
    store_.scanHeadersFrom(kNetFilesArea, pos, [&](const Message& m) {
        ok = hd_.hasLast && !seen++ && (uint32_t)m.num == hd_.lastNum && m.at == hd_.lastAt;
        return ok;
    });
    return ok && seen == (hd_.hasLast ? 1u : 0u) && pos == hd_.end;
}

// A new hash key from the host's random source: nobody else can know it ahead. No random source: no index.
void NetCore::idxStart() {
    hd_ = IdxHead{};
    idxPos_ = 0;
    if (!idxRandom_) {
        idx_ = Idx::Unknown;
        return;
    }
    idxRandom_(hd_.key, sizeof hd_.key);
    idx_ = idxHead(false) ? Idx::Rebuild : Idx::Unknown;
}

// After a rewrite of the area every offset moved: the index is built again now, in one go.
void NetCore::idxRebuildNow() {
    idxStart();
    while (idx_ == Idx::Rebuild && listingIndexStep()) {
    }
}

bool NetCore::listingIndexStep() {
    if (!idxRead_ || !idxWrite_) return false;
    if (idx_ == Idx::Unknown) {
        if (idxRead_(0, &hd_, sizeof hd_) && hd_.magic == kIdxMagic && hd_.clean == 1 && idxTailOk()) {
            idx_ = Idx::Ready;
            return false;
        }
        // Missing, from a change cut short, the area changed, or unreadable: built again, a piece a call.
        idxStart();
        return idx_ == Idx::Rebuild;
    }
    if (idx_ != Idx::Rebuild) return false;
    IdxRec buf[kNetIndexStep];
    uint32_t n = 0, pos = idxPos_;
    bool end = true;
    IdxHead h = hd_;
    store_.scanHeadersFrom(kNetFilesArea, pos, [&](const Message& m) {
        NetListing l;
        bool listing = decode(m.net, l) && m.num > 0;
        if (listing && n == kNetIndexStep) return end = false;
        if (listing) buf[n++] = idxRec(l, m);
        h.hasLast = 1;  // the last message, junk too
        h.lastNum = (uint32_t)m.num;
        h.lastAt = m.at;
        return true;
    });
    if (n && !idxWrite_(kIdxHead + hd_.count * kIdxRec, buf, n * kIdxRec)) {
        idx_ = Idx::Unknown;  // started again later
        return false;
    }
    hd_.count += n;
    hd_.hasLast = h.hasLast;
    hd_.lastNum = h.lastNum;
    hd_.lastAt = h.lastAt;
    idxPos_ = pos;
    if (!end) return true;
    hd_.end = pos;
    if (idxHead(true)) idx_ = Idx::Ready;
    return false;
}

// ---- taking and making listings ----

// The daily count is keyed on day() as ingress is, with the clock set; without one, day() does not move and trim (run
// once per day of uptime) resets it.
bool NetCore::listingDropAllowed() {
    if (clockSet() && day() != listingDropsDay_) {
        listingDropsDay_ = day();
        listingDropsToday_ = 0;
    }
    return listingDrops_ < kNetListingDropsPerSession && listingDropsToday_ < kNetListingDropsPerDay;
}

NetCore::Took NetCore::takeListing(const NetListing& l, const std::string& fromPeer, bool history, const char** nak) {
    syncLimits();
    std::string id = listingId(l);
    auto res = [&](Took t, const char* why, const char* word) {
        if (nak) *nak = word;
        if (onEvent_) onEvent_(fromPeer, std::string("listing ") + why + " " + id, std::string());
        return t;
    };
    if (nak) *nak = nullptr;
    // 1. valid (the fields, and a signature of the right form), as it is stored and passed on
    std::string line = encode(l);
    if (line.empty() || l.sig.empty()) return res(Took::Rejected, "rejected bad", kNakBad);
    // 2. banned, blocked, 3. the home board's card and its signature, 4. un_files carried and both members
    Refusal r;
    if (refusedOrigin(l.board, [&](const std::string& pub) { return verifyListing(l, pub); }, r) ||
        refusedUser(kNetFilesUserArea, l.board, r))
        return res(r.t, r.why, r.word);
    // 5. killed
    if (state_.killed(id)) return res(Took::Rejected, "rejected killed", kNakKilled);
    // 6. dedupe, as for posts: below the seen window (a history listing, or any tombstone, which must never be lost)
    // it is a duplicate only if the index holds this id.
    if (seen_.has(id) && !((history || l.gone) && seen_.belowWindow(id))) return res(Took::Duplicate, "duplicate", nullptr);
    // 7. what is held of this id and of this name, by the index (no index yet: the sender tries again later). Each hit
    // is read back at its offset and checked, so a collision costs one short read, never a wrong answer; a name holds
    // one record, so more than kIdxConfirm hits is not a state this board makes: refused, transient.
    if (!idxReady()) return res(Took::Rejected, "rejected store", kNakStore);
    Message probe;
    IdxRec me = idxRec(l, probe);
    std::vector<IdxRec> hits;
    bool many = false;
    if (!idxEach([&](const IdxRec& x) {
            if (x.board != me.board || (x.key != me.key && x.seq != l.seq)) return true;
            if (hits.size() == kIdxConfirm) return !(many = true);
            hits.push_back(x);
            return true;
        }) ||
        many)
        return res(Took::Rejected, "rejected store", kNakStore);
    bool held = false, later = false, removed = false;
    std::vector<uint32_t> lower;  // the records of this name below it (they go once it is stored)
    for (const IdxRec& x : hits) {
        Message m;
        NetListing s;
        if (!store_.readAt(kNetFilesArea, x.at, (int)x.num, m) || !decode(m.net, s) || s.board != l.board) continue;
        if (s.seq == l.seq) held = true;
        if (s.area != l.area || s.name != l.name) continue;
        if (s.seq > l.seq) {
            later = true;
            removed = removed || s.gone;
        } else if (s.seq < l.seq) {
            lower.push_back(x.num);
        }
    }
    if (held) return res(Took::Duplicate, "duplicate", nullptr);
    // A record of this name with a higher seq is held: this one is older, whatever the order they came in. Seen, so a
    // repeat costs nothing.
    if (later) {
        seen_.add(id, day());
        return res(Took::Duplicate, removed ? "duplicate removed" : "duplicate replaced", nullptr);
    }
    // A peer's listing that replaces or removes one held costs a rewrite of the area: a few a session and a few more
    // a day, then the transient "store" (before ingress is charged and before anything is stored, so a name never
    // holds two records). This board's own are not counted.
    if (!lower.empty() && !fromPeer.empty() && !listingDropAllowed()) return res(Took::Rejected, "rejected store", kNakStore);
    // 8. ingress, counting the line (history pulls are capped by the session; Rev's boards and this board are not)
    uint32_t today = day();
    if (!history && l.board != myBoard_ && !isRevBoard(l.board) && !ingress_.allow(l.board, line.size(), today))
        return res(Took::Limited, "limited", kNakLimited);
    // 9. store, with the index marked unclean around the change (a reset in between rebuilds it)
    Message m;
    m.from = l.uploader;
    m.to = l.board;
    m.subject = l.name;
    m.date = netDateText(clockSet() && l.date > now_() ? now_() : l.date);  // as storePost
    m.net = line;
    if (!idxHead(false)) return res(Took::Rejected, "rejected store", kNakStore);
    int num = store_.post(kNetFilesArea, m);
    if (!num) {
        idx_ = Idx::Unknown;
        return res(Took::Rejected, "rejected store", kNakStore);
    }
    seen_.add(id, today);
    if (!lower.empty()) {
        if (!fromPeer.empty()) {
            listingDrops_++;
            listingDropsToday_++;
        }
        // It replaces the records of its name below it: one rewrite of the area, then the index built again (every
        // offset after the first one dropped moved). A count that is not what was asked leaves the index unclean.
        if (store_.drop(kNetFilesArea, [&](const Message& x) {
                return std::find(lower.begin(), lower.end(), (uint32_t)x.num) != lower.end();
            }) == (int)lower.size())
            idxRebuildNow();
        else
            idx_ = Idx::Unknown;
    } else {
        // Appended where the area ended: its record, and the new end, from a scan of just that message.
        uint32_t pos = hd_.end;
        IdxRec rec{};
        bool ok = false;
        store_.scanHeadersFrom(kNetFilesArea, pos, [&](const Message& x) {
            NetListing s;
            ok = !ok && x.num == num && decode(x.net, s);
            if (ok) rec = idxRec(s, x);
            return ok;
        });
        if (ok && idxWrite_(kIdxHead + hd_.count * kIdxRec, &rec, kIdxRec)) {
            hd_.count++;
            hd_.hasLast = 1;
            hd_.lastNum = (uint32_t)num;
            hd_.lastAt = rec.at;
            hd_.end = pos;
            idxHead(true);
        } else {
            idx_ = Idx::Unknown;  // the head stays unclean: the next look rebuilds
        }
    }
    if (onEvent_) onEvent_(fromPeer, (fromPeer.empty() ? "listing made " : "listing stored ") + id, line);
    log("L " + std::to_string(num) + " " + id + " " + excludeList({fromPeer, l.board}));
    return Took::Stored;
}

std::string NetCore::originateListing(NetListing l) {
    const NetCard* own = state_.card(myBoard_);
    if (!own || !carries(kNetFilesUserArea) || !selfMember()) return "";
    l.board = myBoard_;
    l.seq = seq_;
    l.date = now_();
    if (l.gone) {
        l.size = l.crc = 0;
        l.desc.clear();
    }
    for (std::string* f : {&l.desc, &l.uploader})
        for (char& ch : *f)
            if ((unsigned char)ch < 0x20 || ch == 0x7f) ch = '?';
    if (l.desc.size() > kNetListDesc) l.desc.resize(kNetListDesc);
    if (l.uploader.size() > 30) l.uploader.resize(30);
    if (!signListing(l, me_) || !verifyListing(l, own->pub)) return "";
    // The seq is used up from here, stored or not, as for originate.
    uint32_t next = seq_ == 0xFFFFFFFFu ? 1 : seq_ + 1;
    if (reserve_ && !reserve_(next)) return "";
    seq_ = next;
    std::string id = listingId(l);
    if (seen_.has(id)) return "";  // the counter was set back
    return takeListing(l, "") == Took::Stored ? id : "";
}

int NetCore::listingsPage(int skip, int max, const std::function<void(const NetListing&)>& each) {
    if (skip < 0 || max <= 0) return -1;
    // Newest first: one scan counts the live ones, the next takes the page (positions oldest first).
    int n = 0, i = 0;
    eachListing([&](const Message&, const NetListing& l) {
        n += shown(l);
        return true;
    });
    int lo = n - skip - max, hi = n - skip;
    if (hi <= 0) return -1;
    std::vector<NetListing> page;
    eachListing([&](const Message&, const NetListing& l) {
        if (!shown(l)) return true;
        if (i >= lo) page.push_back(l);
        return ++i < hi;
    });
    for (size_t k = page.size(); k > 0; k--) each(page[k - 1]);
    return lo > 0 ? hi - lo + skip : -1;
}

// By the index: the record of that board and seq, read back at its offset and checked.
bool NetCore::findListing(const std::string& id, NetListing& out) {
    uint32_t seq = 0;
    if (id.size() < 18 || id[16] != '-' || !parseSeq(id.substr(17), seq) || !idxReady()) return false;
    uint32_t bk = idxHash(id.substr(0, 16));
    std::vector<IdxRec> hits;
    idxEach([&](const IdxRec& x) {
        if (x.board == bk && x.seq == seq && !(x.flags & 1)) hits.push_back(x);
        return hits.size() < kIdxConfirm;
    });
    for (const IdxRec& x : hits) {
        Message m;
        NetListing l;
        if (store_.readAt(kNetFilesArea, x.at, (int)x.num, m) && decode(m.net, l) && listingId(l) == id && shown(l)) {
            out = std::move(l);
            return true;
        }
    }
    return false;
}

bool NetCore::listingLines(uint32_t& pos, bool tombs, std::vector<std::string>& out) {
    if (!carries(kNetFilesUserArea) || !selfMember()) return true;
    uint32_t now = now_();
    bool clock = clockSet(), end = true;
    size_t bytes = 0;
    store_.scanHeadersFrom(kNetFilesArea, pos, [&](const Message& m) {
        NetListing l;
        if (!decode(m.net, l)) return true;
        if (tombs ? !l.gone || state_.banned(l.board) || state_.killed(listingId(l)) : !shown(l)) return true;
        if (tombs && clock) {
            uint32_t t = netDateUnix(m.date);
            if (t && t + (uint64_t)kNetGoneKeepDays * 86400 < now) return true;
        }
        if (bytes && bytes + m.net.size() > kNetPendingBytes) return end = false;  // a full batch: goes on from here
        bytes += m.net.size();
        out.push_back(m.net);
        return true;
    });
    return end;
}

// Drops killed listings, live ones of banned boards and tombstones past kNetGoneKeepDays (by count without a clock:
// the newest kNetGoneKeepCount stay), then holds live listings to the caps. A board's oldest listing is its lowest seq,
// so arrival order does not change what goes. Header scans count (a further one per board past a cap finds its
// seqs), then one rewrite of the area and of the index. Not while the index is being built.
void NetCore::trimListings(uint32_t nowDay, bool clock) {
    if (!idxReady()) return;
    auto old = [&](const Message& m) {
        uint32_t t = netDateUnix(m.date);
        return clock && t && nowDay > t / 86400 && nowDay - t / 86400 > kNetGoneKeepDays;
    };
    auto dead = [&](const NetListing& l) { return state_.killed(listingId(l)) || (!l.gone && state_.banned(l.board)); };
    std::map<std::string, uint32_t> live, cut;  // live per home board; then, per board past a cap, how many go
    uint32_t total = 0, tombs = 0;
    bool drops = false;
    eachListing([&](const Message& m, const NetListing& l) {
        if (dead(l)) drops = true;
        else if (!l.gone) live[l.board]++, total++;
        else if (old(m)) drops = true;
        else tombs++;
        return true;
    });
    uint32_t tombCut = !clock && tombs > kNetGoneKeepCount ? tombs - kNetGoneKeepCount : 0;
    for (auto& kv : live)
        if (kv.first != myBoard_ && kv.second > listingsPerBoard_) {
            cut[kv.first] = kv.second - listingsPerBoard_;
            total -= kv.second - listingsPerBoard_;
            kv.second = listingsPerBoard_;
        }
    while (total > listingsMax_) {  // one at a time from the board holding the most (ties: the lowest id)
        auto most = live.end();
        for (auto it = live.begin(); it != live.end(); ++it)
            if (it->first != myBoard_ && it->second && (most == live.end() || it->second > most->second)) most = it;
        if (most == live.end()) break;  // only this board's own are left
        most->second--;
        cut[most->first]++;
        total--;
    }
    std::map<std::string, uint32_t>().swap(live);
    // Each cut board's seq at the cut: its lowest seqs up to there go.
    for (auto& kv : cut) {
        std::vector<uint32_t> seqs;
        eachListing([&](const Message&, const NetListing& l) {
            if (l.board == kv.first && !l.gone && !dead(l)) {
                size_t j = seqs.size();
                seqs.push_back(l.seq);
                for (; j > 0 && seqs[j - 1] > l.seq; j--) std::swap(seqs[j], seqs[j - 1]);  // insertion, ascending
            }
            return true;
        });
        kv.second = kv.second <= seqs.size() ? seqs[kv.second - 1] : 0xFFFFFFFFu;
    }
    if (!drops && !tombCut && cut.empty()) return;
    uint32_t n = 0;
    std::vector<uint32_t> gone;  // the numbers dropped, ascending (store order), for the index
    if (!idxHead(false)) return;
    int dropped = store_.drop(kNetFilesArea, [&](const Message& m) {
        NetListing l;
        if (!decode(m.net, l)) return false;
        auto c = cut.find(l.board);
        bool go = dead(l) || (l.gone ? old(m) || (!clock && n++ < tombCut) : c != cut.end() && l.seq <= c->second);
        if (go) gone.push_back((uint32_t)m.num);
        return go;
    });
    if (!dropped && gone.empty()) idxHead(true);           // nothing went after all: the index stands
    else if (dropped == (int)gone.size()) idxRebuildNow();  // every offset after the first drop moved
    else idx_ = Idx::Unknown;                          // the head stays unclean: the next look rebuilds
}

bool NetCore::takeCard(const NetCard& c, const std::string& fromPeer) {
    if (!state_.addCard(c)) return false;
    log("C " + c.board + " " + excludeList({fromPeer, c.board}));
    if (onEvent_) onEvent_(fromPeer, "card " + c.board + " " + std::to_string(c.seq), encode(c));
    return true;
}

bool NetCore::takeNotice(const NetNotice& n, const std::string& fromPeer) {
    std::string group = noticeGroup(n);
    // Notices that can change who is a User Net member: the roll before, to note what changed.
    bool roster = onEvent_ && (group == "member" || group == "votes" || group == "board");
    Roll before = roster ? roll() : Roll();
    bool changed = state_.applyNotice(n, day());
    syncLimits();
    if (!changed) return false;
    if (group == "rotate") log("N R " + std::to_string(n.seq) + " " + excludeList({fromPeer}));
    else if (plainToken(n.target)) log("N " + group + " " + n.target + " " + excludeList({fromPeer}));
    if (n.kind == "kill") dropPost(n.target);
    if (onEvent_) onEvent_(fromPeer, "notice " + n.kind + " " + n.target + " seq " + std::to_string(n.seq), encode(n));
    if (roster) noteRoll(fromPeer, before);
    return true;
}

NetCore::Roll NetCore::roll() const {
    Roll r;
    if (onEvent_) {
        r.in = members_.members();
        r.denied = members_.denied();
    }
    return r;
}

// Every board that entered or left the members, and every applicant newly denied (both lists sorted the
// same way before and after, so a merge finds the differences).
void NetCore::noteRoll(const std::string& who, const Roll& before) {
    if (!onEvent_) return;
    Roll after = roll();
    auto note = [&](const std::vector<std::string>& now, const std::vector<std::string>& was, const char* what) {
        for (const std::string& b : now)
            if (std::find(was.begin(), was.end(), b) == was.end()) onEvent_(who, "member " + b + " " + what, std::string());
    };
    note(after.in, before.in, "admitted");
    note(before.in, after.in, "removed");
    note(after.denied, before.denied, "denied");
}

// The line goes to the apps file and its offset to members(); false if it could not (the record is held,
// but never passed on: resolve reads it back from the file).
bool NetCore::appended(const std::string& line) {
    uint32_t off = kNoOffset;
    if (!appsAppend_ || !appsAppend_(line, off)) {
        appsFailures_++;
        return false;
    }
    members_.setOffset(line, off);
    return true;
}

// A banned board's records are still held and passed on: the tally makes them count for nothing, and a
// lift brings them back the same way on every board, whenever the ban reached it.
bool NetCore::takeApp(const NetApp& a, const std::string& fromPeer) {
    Roll before = roll();
    std::string line;
    if (!members_.takeApp(a, line)) return false;
    appended(line);
    log("A " + a.board + " " + excludeList({fromPeer, a.board}));
    if (onEvent_) {
        onEvent_(fromPeer, "app " + a.board + " " + std::to_string(a.seq), line);
        noteRoll(fromPeer, before);
    }
    return true;
}

bool NetCore::takeVote(const NetVote& v, const std::string& fromPeer) {
    Roll before = roll();
    std::string line;
    if (!members_.takeVote(v, line)) return false;
    appended(line);
    log("V " + v.app + " " + v.voter + " " + excludeList({fromPeer, v.voter}));
    if (onEvent_) {
        onEvent_(fromPeer, "vote " + v.voter + " on " + v.app + (v.yes ? " yes" : " no"), line);
        noteRoll(fromPeer, before);
    }
    return true;
}

void NetCore::setUserAreas(std::vector<std::string> carried) {
    userAreas_.clear();
    for (std::string& id : carried)
        if (isUserNetArea(id) && !carries(id)) userAreas_.push_back(std::move(id));
}

bool NetCore::carries(const std::string& area) const {
    return std::find(userAreas_.begin(), userAreas_.end(), area) != userAreas_.end();
}

bool NetCore::setOwnCard(const NetCard& c) {
    if (c.board != myBoard_) return false;
    return takeCard(c, "");
}

bool NetCore::originateNotice(const NetNotice& n) { return takeNotice(n, ""); }

std::string NetCore::originate(const std::string& area, const Message& m, int* num) {
    if (num) *num = 0;
    const NetArea* a = netArea(area);
    if (!a) return "";
    if (isUserNetArea(area) && (!carries(area) || !selfMember())) return "";
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

    // The id is used up from here, stored or not, so a retry never reuses it; the next seq is saved first.
    uint32_t next = seq_ == 0xFFFFFFFFu ? 1 : seq_ + 1;
    if (reserve_ && !reserve_(next)) return "";
    seq_ = next;
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
NetCore::Res NetCore::resolve(const std::string& entry, const std::string& peer, std::string& line, bool peerUserNet,
                              bool peerNet3) {
    line.clear();
    std::vector<std::string> t = tokens(entry);
    if (t.empty() || inList(t.back(), peer)) return Res::Skip;
    // Applications, votes, User Net posts and listings never go to a peer that would only refuse them: no read.
    if (!peerUserNet && (t[0] == "A" || t[0] == "V" || (t[0] == "P" && t.size() > 1 && isUserNetArea(t[1]))))
        return Res::Skip;
    if (t[0] == "L") {
        if (!peerNet3 || t.size() != 4 || t[2].size() < 17 || state_.killed(t[2]) || state_.banned(t[2].substr(0, 16)))
            return Res::Skip;
        Message m;
        if (!store_.read(kNetFilesArea, std::atoi(t[1].c_str()), m)) return Res::Retry;
        NetListing l;
        if (!decode(m.net, l) || listingId(l) != t[2]) return Res::Skip;  // that number now holds another record
        line = std::move(m.net);
        return Res::Ready;
    }
    if (t[0] == "P" && t.size() == 5) {
        const std::string& id = t[3];
        if (id.size() < 17 || state_.killed(id) || state_.banned(id.substr(0, 16)) || !netArea(t[1]))
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
    } else if (t[0] == "A" && t.size() == 3) {
        // held first: off is filled by heldApp, and argument order is unspecified
        uint32_t off = kNoOffset;
        bool held = members_.heldApp(t[1], off);
        return readRecord(held, off, line);
    } else if (t[0] == "V" && t.size() == 4) {
        uint32_t off = kNoOffset;
        bool held = members_.heldVote(t[2], t[1], off);
        return readRecord(held, off, line);
    }
    return line.empty() ? Res::Skip : Res::Ready;  // not held any more (or not an entry): gone
}

// An application or vote entry: RAM says first whether it is held (no read when not); then the one line
// at its offset, which must still be the record held (else it is gone). A failed read is tried again.
NetCore::Res NetCore::readRecord(bool held, uint32_t off, std::string& line) {
    if (!held || off == kNoOffset || !appsReadAt_) return Res::Skip;
    if (!appsReadAt_(off, line)) {
        line.clear();
        return Res::Retry;
    }
    if (members_.holds(line)) return Res::Ready;
    line.clear();
    return Res::Skip;
}

bool NetCore::userRecord(uint32_t off, std::string& line) {
    line.clear();
    if (readRecord(true, off, line) == Res::Ready) return true;
    line.clear();
    return false;
}

std::vector<std::pair<uint32_t, std::string>> NetCore::pendingFor(const std::string& peer, size_t max, bool peerUserNet,
                                                                 bool peerNet3) {
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
        Res r = resolve(entry, peer, line, peerUserNet, peerNet3);
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

bool NetCore::trustedPeer(const std::string& board) const {
    auto it = links_.find(board);
    return it != links_.end() && (it->second.outbound || it->second.sessions >= kNetTrustSessions);
}

void NetCore::sessionDone(const std::string& board) {
    auto it = links_.find(board);
    if (it != links_.end() && it->second.sessions < 255) it->second.sessions++;
}

std::string NetCore::saveCursors() const {
    std::string out;
    for (const auto& kv : links_)
        out += kv.first + " " + std::to_string(kv.second.cursor) + " " + std::to_string(kv.second.lastSync) +
               (kv.second.outbound ? " out " : " in ") + std::to_string(kv.second.sessions) + "\n";
    if (history_ >= 0) out += history_ ? "history 1\n" : "history 0\n";
    out += userHistory_ ? "uhistory 1\n" : "uhistory 0\n";
    out += filesHistory_ ? "fhistory 1\n" : "fhistory 0\n";
    if (!filesHistory_ && filesHeld_) out += "fheld " + std::to_string(filesHeld_) + "\n";
    return out;
}

void NetCore::loadCursors(const std::string& text) {
    links_.clear();
    history_ = -1;
    userHistory_ = filesHistory_ = false;
    filesHeld_ = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> t = tokens(line);
        if (t.size() == 2 && t[0] == "history" && (t[1] == "0" || t[1] == "1")) {
            history_ = t[1] == "1" ? 1 : 0;
            continue;
        }
        if (t.size() == 2 && (t[0] == "uhistory" || t[0] == "fhistory") && (t[1] == "0" || t[1] == "1")) {
            (t[0][0] == 'u' ? userHistory_ : filesHistory_) = t[1] == "1";
            continue;
        }
        if (t.size() == 2 && t[0] == "fheld" && t[1].size() == 1 && t[1][0] >= '0' && t[1][0] <= '9') {
            filesHeld_ = (uint8_t)std::min<uint32_t>((uint32_t)(t[1][0] - '0'), kNetFilesHeldPulls);
            continue;
        }
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
        uint32_t n = kNetTrustSessions;  // an older file without the count: it was a link peer already
        if (t.size() > 4 && !parseSeq(t[4], n)) continue;
        l.sessions = (uint8_t)std::min<uint32_t>(n, 255);
        if (poolSize(l.outbound) >= (l.outbound ? kMaxOutboundPeers : kMaxInboundPeers)) continue;
        links_[t[0]] = l;
    }
}

void NetCore::dropPost(const std::string& id) {
    for (const std::vector<NetArea>* family : {&sysopNetAreas(), &userNetAreas()})
        for (const NetArea& a : *family) {  // each area's headers, one at a time, up to the first hit
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
    bool clock = nowUnix > kNetClockSet;
    if (clock && nowDay == lastTrimDay_) return;  // once a day: the area rewrites below touch the SD card
    lastTrimDay_ = nowDay;
    if (!clock) listingDropsToday_ = 0;  // a day of uptime: the daily listing drop budget is full again
    state_.prune(nowDay);
    seen_.prune(nowDay);
    log_.trimTo(kOutLogKeep);
    for (const NetArea& a : sysopNetAreas()) trimArea(a.id, nowDay, clock);
    for (const NetArea& a : userNetAreas()) trimArea(a.id, nowDay, clock);
    trimListings(nowDay, clock);
}

bool NetCore::historyNext(HistoryCursor& c, uint32_t sinceUnix, std::string& line) {
    std::vector<const NetArea*> areas;
    for (const NetArea& a : sysopNetAreas()) areas.push_back(&a);
    return walk(c, sinceUnix, areas, kNetHistoryPerArea, line);
}

// The areas list keeps every User Net area at its own index (nullptr when not walked), so a pick's index
// means the same area on every call even if what this board carries changes during the walk.
bool NetCore::userHistoryNext(HistoryCursor& c, uint32_t sinceUnix, uint8_t areaMask, std::string& line) {
    std::vector<const NetArea*> areas;
    const std::vector<NetArea>& all = userNetAreas();
    for (size_t i = 0; i < all.size(); i++) areas.push_back(((areaMask >> i) & 1) && carries(all[i].id) ? &all[i] : nullptr);
    return walk(c, sinceUnix, areas, kNetUserHistoryPerArea, line);
}

// One history walk over areas (nullptr entries are skipped), at most perArea posts per area.
bool NetCore::walk(HistoryCursor& c, uint32_t sinceUnix, const std::vector<const NetArea*>& areas, size_t perArea,
                   std::string& line) {
    line.clear();
    if (!c.loaded) {
        // Each area's newest perArea net posts by a header scan, as (date, seq, area, number): 16 bytes
        // each, at most 800 (about 13 KB) for a moment, sorted, then cut to 4 bytes each.
        struct Pick {
            uint32_t date, seq, area;
            int num;
        };
        std::vector<Pick> all;
        for (size_t a = 0; a < areas.size(); a++) {
            if (!areas[a]) continue;
            size_t from = all.size();
            store_.scanHeaders(areas[a]->id, [&](const Message& m) {
                std::string id = netPostId(m.net);
                uint32_t seq = 0;
                if (id.size() < 18 || !parseSeq(id.substr(17), seq) || m.num <= 0 || m.num >= (1 << 28)) return true;
                // The stored date is minute text and never later than the post's own date, so a
                // minute of slack keeps posts from the edge of the window.
                uint32_t date = netDateUnix(m.date);
                if (sinceUnix && date + 60 < sinceUnix) return true;
                if (all.size() - from == perArea) all.erase(all.begin() + (long)from);  // keep the newest
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
        if (a >= areas.size() || !areas[a] || !store_.read(areas[a]->id, (int)(pick & 0x0FFFFFFFu), m)) continue;
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

bool NetCore::needHistory() {
    if (history_ < 0) history_ = hasNetPosts() ? 1 : 0;  // decided once, then saved with the cursors
    return history_ == 0;
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
    if (area == kNetFilesArea) return 0;  // only NetCore writes listings, signed
    if (!isNetAreaId(area)) return inner_.post(area, m);
    int num = 0;
    return core_.originate(area, m, &num).empty() ? 0 : num;
}

int NetPostingStore::drop(const std::string& area, const std::function<bool(const Message&)>& gone) {
    return inner_.drop(area, gone);
}

}  // namespace axis::net
