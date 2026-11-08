// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/cluster.h"

#include "bbs/password.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace axis::cluster {

namespace {
// 5 seconds was shorter than a slow door slice, so a caller on the main made followers
// drop it and renumber themselves into node 1. A real dead main still fails over.
constexpr uint32_t kListenMs = 3000, kBeatMs = 1000, kLostMs = 12000, kAnnounceMs = 2000, kAskMs = 1000;
constexpr uint32_t kCardMs = 10000;      // a follower with a card tells the main about it this often (it is alive)
constexpr uint32_t kTidyIdleMs = 10000;  // every node idle this long: the main renumbers itself to node 1
constexpr uint32_t kAuthMs = 5000;       // a link that hasn't proved the key by now is dropped
constexpr uint32_t kProbeAgainMs = 10000;  // a main that failed a check on another main's address waits this long to check it again
constexpr uint32_t kTurnedAwayMs = 60000;  // "turned away" goes in the log at most this often
std::string num(uint32_t v) { return std::to_string(v); }
std::string num64(uint64_t v) { return std::to_string((unsigned long long)v); }
uint32_t    toU(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 10); }
uint64_t    toU64(const std::string& s) { return (uint64_t)std::strtoull(s.c_str(), nullptr, 10); }

std::string hex(const std::string& b) {
    static const char* d = "0123456789abcdef";
    std::string        s;
    for (unsigned char c : b) s += d[c >> 4], s += d[c & 15];
    return s;
}
std::string unhex(const std::string& h) {
    auto        v = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    std::string b;
    if (h.size() % 2) return b;
    for (size_t i = 0; i < h.size(); i += 2) {
        int x = v(h[i]), y = v(h[i + 1]);
        if (x < 0 || y < 0) return std::string();
        b += (char)(x << 4 | y);
    }
    return b;
}
bool sameBytes(const std::string& a, const std::string& b) {  // in constant time: no early out on the first wrong byte
    if (a.size() != b.size() || a.empty()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); i++) d |= (unsigned char)(a[i] ^ b[i]);
    return d == 0;
}
std::string proof(const std::string& key, const char* what, const std::string& mainNonce, const std::string& followerNonce) {
    return bbs::hmacSha256Raw(key, std::string(what) + mainNonce + followerNonce);
}
std::string frameTag(const std::string& linkKey, char from, uint32_t seq, Frame t, const std::string& body) {
    std::string m(1, from);
    for (int i = 0; i < 4; i++) m += (char)(seq >> (8 * i));
    m += (char)t;
    m += body;
    return bbs::hmacSha256Raw(linkKey, m).substr(0, kTagBytes);
}
}  // namespace

std::string Cluster::nonce() {
    std::string n;
    for (int i = 0; i < 4; i++) {
        uint32_t r = rng_();
        for (int k = 0; k < 4; k++) n += (char)(r >> (8 * k));
    }
    return n;
}

std::string Cluster::sign(const std::string& d) { return d + " ~" + hex(bbs::hmacSha256Raw(key_, "datagram " + d).substr(0, kTagBytes)); }

bool Cluster::unsign(std::string& d) {
    size_t at = d.rfind(" ~");
    if (at == std::string::npos) return false;
    std::string body = d.substr(0, at);
    if (!sameBytes(unhex(d.substr(at + 2)), bbs::hmacSha256Raw(key_, "datagram " + body).substr(0, kTagBytes))) return false;
    d = body;
    return true;
}

bool Cluster::put(Link& l, Frame t, const std::vector<std::string>& f) {
    if (l.auth != 2 || l.dead) return false;
    std::string out = frame(t, f);
    out += frameTag(l.linkKey, asMain(l) ? 'M' : 'F', l.sent++, t, out.substr(5));
    uint32_t n = (uint32_t)(out.size() - 4);
    for (int i = 0; i < 4; i++) out[(size_t)i] = (char)(n >> (8 * i));
    return net_.send(l.id, out);
}

bool Cluster::take(Link& l, Frame& t, std::string& body) {
    while (!l.dead && l.in.next(t, body)) {
        if (l.auth == 2) {  // trusted: every frame must carry the right tag, in order
            if (body.size() < kTagBytes) {
                l.dead = true;
                break;
            }
            std::string tag = body.substr(body.size() - kTagBytes);
            body.resize(body.size() - kTagBytes);
            if (!sameBytes(tag, frameTag(l.linkKey, asMain(l) ? 'F' : 'M', l.got, t, body))) {
                event("a frame with a bad cluster tag: dropping the link");
                l.dead = true;
                break;
            }
            l.got++;
            return true;
        }
        std::vector<std::string> f = fields(body);
        bool                     asMainHere = asMain(l);
        if (asMainHere && l.auth == 0 && t == Frame::Auth && f.size() >= 2) {  // the follower's proof
            std::string fn = unhex(f[0]);
            if (fn.size() == 16 && sameBytes(unhex(f[1]), proof(key_, "follower", l.mainNonce, fn))) {
                l.followerNonce = fn;
                l.linkKey = proof(key_, "link", l.mainNonce, fn);
                l.auth = 2;
                net_.send(l.id, frame(Frame::Auth, {hex(proof(key_, "main", l.mainNonce, fn))}));
                continue;
            }
        } else if (!asMainHere && l.auth == 0 && t == Frame::Challenge && !f.empty()) {
            std::string mn = unhex(f[0]);
            if (mn.size() == 16) {
                l.mainNonce = mn;
                l.followerNonce = nonce();
                l.linkKey = proof(key_, "link", mn, l.followerNonce);
                l.auth = 1;
                net_.send(l.id, frame(Frame::Auth, {hex(l.followerNonce), hex(proof(key_, "follower", mn, l.followerNonce))}));
                continue;
            }
        } else if (!asMainHere && l.auth == 1 && t == Frame::Auth && !f.empty() &&
                   sameBytes(unhex(f[0]), proof(key_, "main", l.mainNonce, l.followerNonce))) {
            l.auth = 2;  // the main holds the key too: now say hello
            if (l.probe) {  // (a main checking another main: it is real, so step down at the end of the tick)
                stepDown_ = true;
                return false;
            }
            sendHello(l);
            continue;
        }
        // (a main's link that failed is reported when tick() drops it, at most once a minute)
        // (a failed probe is reported by serveProbe(), at most once a minute)
        if (!l.probe && !asMainHere) event("the main didn't prove the cluster key: not joining it");
        l.dead = true;
    }
    return false;
}

void Cluster::sendHello(Link& l) {
    put(l, Frame::Hello, {id_, num((uint32_t)(wantNumber_ ? wantNumber_ : number_)), "1", hasFiles_ ? "1" : "0", num(net_.myIp()),
                          num((uint32_t)slots_), num64(freeBytes_)});
}

NodeInfo* Cluster::node(int number) {
    for (NodeInfo& n : nodes_)
        if (n.number == number) return &n;
    return nullptr;
}

void Cluster::start(uint32_t now, int previousNumber) {
    if (inTick_) {  // called from inside tick (a command a follower asked for): the links can't change under its loop
        restart_ = true;
        restartNumber_ = previousNumber;
        return;
    }
    closeProbe(now);
    wantNumber_ = previousNumber;
    alone_ = key_.empty() || !rng_;
    if (alone_) {  // no shared key: never answer or join another board
        becomeMain(now, epoch_ + 1, previousNumber ? previousNumber : 1);
        event("no cluster key: this board runs alone (set the same @CLUSTERKEY on every board to cluster them)");
        return;
    }
    discover(now);
}

void Cluster::discover(uint32_t now) {
    closeProbe(now);
    for (Link& l : links_) net_.close(l.id);
    links_.clear();
    cards_.clear();
    role_ = Role::Discovering;
    since_ = now;
    lastAnnounce_ = now;
    shout("AXIS? " + id_);
}

std::vector<int> Cluster::allocate(const std::vector<int>& want) {
    std::vector<int> got;
    auto taken = [&](int k) { return node(k) || std::find(got.begin(), got.end(), k) != got.end(); };
    for (int w : want)
        if (w >= 1 && !taken(w) && (int)got.size() < slots_) got.push_back(w);
    for (int k = 1; (int)got.size() < (int)(want.empty() ? 1 : want.size()); k++)
        if (!taken(k)) got.push_back(k);
    return got;
}

void Cluster::becomeMain(uint32_t now, uint32_t epoch, int number) {
    closeProbe(now);
    for (Link& l : links_) net_.close(l.id);
    links_.clear();
    cards_.clear();
    role_ = Role::Main;
    epoch_ = epoch;
    mainIp_ = net_.myIp();
    std::vector<int> want = {number};  // this board keeps its numbers where it can
    for (int n : numbers_)
        if (n != number) want.push_back(n);
    want.resize((size_t)slots_, 0);
    nodes_.clear();
    numbers_ = allocate(want);
    number_ = numbers_[0];
    status_.resize(numbers_.size());
    for (size_t i = 0; i < numbers_.size(); i++)
        nodes_.push_back({numbers_[i], id_, status_[i].first, status_[i].second, net_.myIp()});
    lastBeat_ = lastAnnounce_ = now;
    announce();
    if (onNumber) onNumber(number_);
}

void Cluster::setStatus(int slot, const std::string& handle, const std::string& activity) {
    if (slot < 0 || slot >= 4) return;
    if ((int)status_.size() <= slot) status_.resize((size_t)slot + 1);
    status_[(size_t)slot] = {handle, activity};
    if (role_ == Role::Main) {
        if (slot < (int)numbers_.size())
            if (NodeInfo* me = node(numbers_[(size_t)slot])) {
                me->handle = handle;
                me->activity = activity;
            }
    } else if (role_ == Role::Follower && !links_.empty()) {
        put(links_[0], Frame::Status, {handle, activity, num((uint32_t)slot)});
    }
}

void Cluster::setFreeBytes(uint64_t bytes) {
    if (bytes == freeBytes_) return;
    freeBytes_ = bytes;
    if (role_ == Role::Follower && !links_.empty() && hasFiles_)
        put(links_[0], Frame::CardInfo, {"1", num64(freeBytes_)});
}

std::vector<CardBoard> Cluster::cardBoards() const {
    std::vector<CardBoard> v;
    if (role_ != Role::Main) return v;
    for (auto& c : cards_) v.push_back({c.first, c.second.ip, c.second.freeBytes, c.second.alive});
    return v;
}

void Cluster::say(const std::string& handle, const std::string& text, bool page, int slot, int kind, int to) {
    int      me = slot >= 0 && slot < (int)numbers_.size() ? numbers_[(size_t)slot] : number_;
    ChatLine line{me, handle, text, page, kind, to};
    if (role_ == Role::Main) deliver(line);
    else if (role_ == Role::Follower && !links_.empty())
        put(links_[0], Frame::Chat, {num((uint32_t)me), handle, text, page ? "1" : "0", num((uint32_t)kind), num((uint32_t)to)});
    else if (onChat) onChat(line);  // on our own: just this board
}

void Cluster::deliver(const ChatLine& line) {
    if (onChat) onChat(line);
    std::vector<std::string> f = {num(line.node), line.handle, line.text, line.page ? "1" : "0", num((uint32_t)line.kind), num((uint32_t)line.to)};
    for (Link& l : links_) put(l, Frame::Chat, f);
}

void Cluster::heartbeat() {
    std::vector<std::string> f = {num(epoch_)};
    for (const NodeInfo& n : nodes_) {
        f.push_back(num(n.number));
        f.push_back(n.id);
        f.push_back(num(n.ip));
        f.push_back(n.handle);
        f.push_back(n.activity);
    }
    for (Link& l : links_)
        if (l.auth == 2 && !put(l, Frame::Heartbeat, f)) event("heartbeat to node " + num((uint32_t)l.node) + " failed");
}

void Cluster::announce() {  // "AXIS! ip epoch node sd": where the main is, and whether it holds the SD card
    if (alone_) return;
    shout("AXIS! " + num(net_.myIp()) + " " + num(epoch_) + " " + num(number_) + (hasFiles_ ? " 1" : " 0"));
}

void Cluster::tidyNumbers(uint32_t now) {
    bool busy = busyHere && busyHere();
    for (const NodeInfo& n : nodes_) busy = busy || !n.handle.empty() || !n.activity.empty();
    if (busy || links_.size() > 8) {
        idleSince_ = 0;
        return;
    }
    if (!idleSince_) idleSince_ = now ? now : 1;
    if (now - idleSince_ < kTidyIdleMs) return;
    // this board first, then each other board in the order of its lowest number now
    std::vector<std::pair<int, Link*>> order;
    for (Link& l : links_) {
        if (l.board.empty()) continue;
        int low = 0;
        for (const NodeInfo& n : nodes_)
            if (n.id == l.board && (!low || n.number < low)) low = n.number;
        if (low) order.push_back({low, &l});
    }
    std::sort(order.begin(), order.end(), [](const std::pair<int, Link*>& x, const std::pair<int, Link*>& y) { return x.first < y.first; });
    std::map<int, int> to;  // old number -> new
    int                next = 1;
    for (int n : numbers_) to[n] = next++;
    for (auto& o : order) {
        std::vector<int> theirs;
        for (const NodeInfo& n : nodes_)
            if (n.id == o.second->board) theirs.push_back(n.number);
        std::sort(theirs.begin(), theirs.end());
        for (int n : theirs) to[n] = next++;
    }
    bool same = true;
    for (auto& m : to) same = same && m.first == m.second;
    if (same) return;
    for (NodeInfo& n : nodes_)
        if (to.count(n.number)) n.number = to[n.number];
    std::sort(nodes_.begin(), nodes_.end(), [](const NodeInfo& x, const NodeInfo& y) { return x.number < y.number; });
    for (int& n : numbers_) n = to[n];
    number_ = numbers_[0];
    if (onNumber) onNumber(number_);
    for (auto& o : order) {
        std::vector<std::string> nums;
        for (const NodeInfo& n : nodes_)
            if (n.id == o.second->board) nums.push_back(num((uint32_t)n.number));
        o.second->node = (int)toU(nums[0]);
        put(*o.second, Frame::Renumber, nums);
    }
    event("nobody on: renumbered, this board is node " + num((uint32_t)number_));
    heartbeat();
}

void Cluster::handleDatagram(const std::string& signedD, uint32_t from, uint32_t now) {
    std::string d = signedD;
    if (!unsign(d)) return;  // not signed with our key: not one of this BBS's boards
    if (d.rfind("AXIS? ", 0) == 0) {
        if (role_ == Role::Main) announce();
        return;
    }
    if (d.rfind("AXIS! ", 0) != 0) return;
    std::vector<std::string> p;
    for (size_t pos = 6; pos <= d.size();) {
        size_t sp = d.find(' ', pos);
        p.push_back(d.substr(pos, sp == std::string::npos ? std::string::npos : sp - pos));
        if (sp == std::string::npos) break;
        pos = sp + 1;
    }
    if (p.size() < 3) return;
    uint32_t ip = toU(p[0]), epoch = toU(p[1]);
    int      number = (int)toU(p[2]);
    if (ip == net_.myIp()) return;
    bool theirSd = p.size() >= 4 && p[3] == "1";
    if (role_ == Role::Main) {  // two mains: the SD card's board wins; then the higher epoch, the lower node, the lower address
        bool theyWin = theirSd != hasFiles_ ? theirSd
                     : epoch != epoch_      ? epoch > epoch_
                     : number != number_    ? number < number_
                                            : ip < net_.myIp();
        // never on the announcement alone (it may be a recording): check that main holds the key first
        if (theyWin) probeMain(ip, epoch, now);
        return;
    }
    if (role_ != Role::Discovering || (epoch < minEpoch_ && !theirSd)) return;
    int link = net_.connect(ip);
    if (link < 0) return;
    links_.push_back({link, FrameReader(), 0});
    role_ = Role::Joining;
    mainIp_ = ip;
    since_ = now;  // the main's Challenge comes first; take() answers it, then says Hello
    (void)from;
}

void Cluster::probeMain(uint32_t ip, uint32_t epoch, uint32_t now) {
    // one at a time, one per tick, and at most one per kProbeAgainMs whatever the address (a replayer must not
    // be able to make this board sit in connect() for every datagram); measured from the end of the last attempt
    if (probing_ || probedThisTick_ || (probeTried_ && now - probeEnd_ < kProbeAgainMs)) return;
    probedThisTick_ = true;
    probeTried_ = true;
    int link = net_.connect(ip);
    probeEnd_ = now;  // (a failed connect counts as an attempt too)
    if (link < 0) return;
    probe_ = Link{link, FrameReader()};
    probe_.probe = true;
    probe_.since = now;
    probing_ = true;
    probeIp_ = ip;
    probeEpoch_ = epoch;
}

void Cluster::closeProbe(uint32_t now) {
    if (!probing_) return;
    net_.close(probe_.id);
    probe_ = Link{-1, FrameReader()};
    probing_ = stepDown_ = false;
    probeEnd_ = now;
}

void Cluster::serveProbe(uint32_t now) {
    if (role_ != Role::Main) {
        closeProbe(now);
        return;
    }
    std::string bytes;
    int         n = net_.recv(probe_.id, bytes);
    if (n > 0) probe_.in.add(bytes);
    Frame       t;
    std::string body;
    while (take(probe_, t, body)) {}  // only the handshake comes before our Hello, which a probe never sends
    if (stepDown_) {  // it proved the key: it is a real main that wins, so join it
        uint32_t epoch = probeEpoch_;
        closeProbe(now);
        event("another main with epoch " + num(epoch) + " proved the cluster key: joining it");
        wantNumber_ = number_;
        minEpoch_ = epoch;
        discover(now);
    } else if (n < 0 || probe_.in.bad() || probe_.dead || now - probe_.since > kAuthMs) {
        if ((probe_.in.bad() || probe_.dead) && (!probeFailOnce_ || now - probeFailAt_ >= kTurnedAwayMs)) {  // at most once a minute
            probeFailOnce_ = true;
            probeFailAt_ = now;
            event("another main's announcement, but it didn't prove the cluster key: staying main");
        }
        closeProbe(now);  // nothing there, or no key: stay main
    }
}

void Cluster::turnedAway(uint32_t now) {
    if (turnedOnce_ && now - turnedAt_ < kTurnedAwayMs) return;
    turnedOnce_ = true;
    turnedAt_ = now;
    event("a board without the cluster key was turned away");
}

void Cluster::handleFrame(Link& l, Frame t, const std::string& body, uint32_t now) {
    // the BBS is mid-request: deliver this after it. The main's own questions are answered at once
    // (only this board's card is touched), or a main waiting on us and us on it would both time out.
    if (inRequest_ && t != Frame::FileResp && t != Frame::MainReq && role_ == Role::Follower) {
        held_.push_back({l.id, t, body});
        return;
    }
    std::vector<std::string> f = fields(body);
    if (role_ == Role::Main) {
        if (!l.board.empty()) {  // heard from: a card marked down by a request that timed out is back
            auto c = cards_.find(l.board);
            if (c != cards_.end()) c->second.alive = true;
        }
        if (t == Frame::Hello && f.size() >= 2) {
            int              want = (int)toU(f[1]), sessions = f.size() >= 6 ? (int)toU(f[5]) : 1;
            uint32_t         ip = f.size() >= 5 ? toU(f[4]) : 0;  // where a busy main can pass it a caller
            std::vector<int> mine;  // it came back: the same numbers
            for (NodeInfo& n : nodes_)
                if (n.id == f[0]) {
                    mine.push_back(n.number);
                    if (ip) n.ip = ip;
                }
            std::sort(mine.begin(), mine.end());
            size_t wanted = (size_t)(sessions < 1 ? 1 : sessions > 4 ? 4 : sessions);
            while (mine.size() > wanted) {  // it came back with fewer sessions: drop the extra numbers
                int drop = mine.back();
                mine.pop_back();
                for (size_t k = 0; k < nodes_.size(); k++)
                    if (nodes_[k].number == drop) nodes_.erase(nodes_.begin() + (long)k);
            }
            if (!mine.empty() && mine.size() < wanted) {  // more sessions than before: the lowest free numbers
                for (int k = 1; mine.size() < wanted; k++)
                    if (!node(k)) {
                        mine.push_back(k);
                        nodes_.push_back({k, f[0], "", "", ip});
                    }
            }
            if (mine.empty()) {  // its old first number if free (even 1, after a failover), then the lowest free
                std::vector<int> ask((size_t)(sessions < 1 ? 1 : sessions > 4 ? 4 : sessions), 0);
                ask[0] = want;
                int keep = slots_;
                slots_ = (int)ask.size();
                mine = allocate(ask);
                slots_ = keep;
                for (int k : mine) nodes_.push_back({k, f[0], "", "", ip});
            }
            std::sort(mine.begin(), mine.end());
            int give = mine[0];
            for (Link& other : links_)  // the board rebooted: its old link is dead, whatever TCP says yet
                if (&other != &l && other.board == f[0]) other.board.clear(), other.node = 0;
            l.node = give;
            l.board = f[0];
            std::vector<std::string> welcome = {num((uint32_t)give), num(epoch_)};
            for (size_t i = 1; i < mine.size(); i++) welcome.push_back(num((uint32_t)mine[i]));
            put(l, Frame::Welcome, welcome);
            event("node " + num((uint32_t)give) + " joined");
            heartbeat();
            if (settings) put(l, Frame::Settings, {settings()});
            if (allUsers)  // its replica of the user base
                for (const std::string& rec : allUsers()) put(l, Frame::UserChange, {rec});
            // a card for the pool; only from a board that sends its free space (it answers MainReq: older ones never would)
            if (f.size() >= 7 && f[3] == "1") cards_[f[0]] = {ip, toU64(f[6]), true};
            else cards_.erase(f[0]);
            if (!hasFiles_ && f.size() >= 4 && f[3] == "1") {  // the file areas live on its card: it should be main
                event("node " + num((uint32_t)give) + " has the SD card: handing main to it");
                put(l, Frame::Yield, {});
            }
        } else if (t == Frame::Status && f.size() >= 2 && !l.board.empty()) {
            std::vector<int> mine;  // that board's numbers, in order: a session's slot picks one
            for (const NodeInfo& n : nodes_)
                if (n.id == l.board) mine.push_back(n.number);
            std::sort(mine.begin(), mine.end());
            size_t slot = f.size() >= 3 ? toU(f[2]) : 0;
            if (slot < mine.size())
                if (NodeInfo* n = node(mine[slot])) {
                    n->handle = f[0];
                    n->activity = f[1];
                }
        } else if (t == Frame::Chat && f.size() >= 4) {
            deliver({toU(f[0]) ? (int)toU(f[0]) : l.node, f[1], f[2], f[3] == "1", f.size() >= 6 ? (int)toU(f[4]) : 0, f.size() >= 6 ? (int)toU(f[5]) : 0});
        } else if (t == Frame::UserChange) {
            if (onUser) onUser(body);
        } else if (t == Frame::CardInfo && f.size() >= 2 && !l.board.empty()) {
            if (f[0] == "1") {
                Card& c = cards_[l.board];
                c.freeBytes = toU64(f[1]);
                c.alive = true;
            } else {
                cards_.erase(l.board);
            }
        } else if (t == Frame::FileReq) {
            size_t      sep = body.find(kFieldSep);
            std::string id = body.substr(0, sep), req = sep == std::string::npos ? "" : body.substr(sep + 1);
            put(l, Frame::FileResp, {id, onRequest ? onRequest(l.node, req) : std::string("0")});
        }
        return;
    }
    if (t == Frame::Welcome && f.size() >= 2 && role_ == Role::Joining) {
        role_ = Role::Follower;
        number_ = (int)toU(f[0]);
        epoch_ = toU(f[1]);
        numbers_ = {number_};
        for (size_t i = 2; i < f.size(); i++) numbers_.push_back((int)toU(f[i]));
        status_.resize(numbers_.size());
        lastBeat_ = now;
        if (onNumber) onNumber(number_);
        for (size_t i = 0; i < status_.size(); i++)
            put(l, Frame::Status, {status_[i].first, status_[i].second, num((uint32_t)i)});
        std::string all;
        for (size_t i = 0; i < numbers_.size(); i++) all += (i ? "+" : "") + num((uint32_t)numbers_[i]);
        event("joined as node " + all);
    } else if (t == Frame::Heartbeat && !f.empty() && role_ == Role::Follower) {
        lastBeat_ = now;
        epoch_ = toU(f[0]);
        nodes_.clear();
        for (size_t i = 1; i + 4 < f.size(); i += 5) {  // number, id, ip, handle, activity per node
            NodeInfo n;
            n.number = (int)toU(f[i]);
            n.id = f[i + 1];
            n.ip = toU(f[i + 2]);
            n.handle = f[i + 3];
            n.activity = f[i + 4];
            nodes_.push_back(n);
        }
        for (const NodeInfo& n : nodes_)
            if (n.ip == mainIp_) lastMain_ = n.number;
    } else if (t == Frame::Chat && f.size() >= 4) {
        if (onChat) onChat({(int)toU(f[0]), f[1], f[2], f[3] == "1", f.size() >= 6 ? (int)toU(f[4]) : 0, f.size() >= 6 ? (int)toU(f[5]) : 0});
    } else if (t == Frame::UserChange) {
        if (onUser) onUser(body);
    } else if (t == Frame::Settings) {
        if (onSettings) onSettings(body);
    } else if (t == Frame::Renumber && !f.empty() && role_ == Role::Follower) {
        numbers_.clear();
        for (const std::string& n : f) numbers_.push_back((int)toU(n));
        number_ = wantNumber_ = numbers_[0];
        status_.resize(numbers_.size());
        if (onNumber) onNumber(number_);
        std::string all;
        for (size_t i = 0; i < numbers_.size(); i++) all += (i ? "+" : "") + num((uint32_t)numbers_[i]);
        event("renumbered by the main: now node " + all);
    } else if (t == Frame::MainReq) {  // the main asks about this board's card
        size_t      sep = body.find(kFieldSep);
        std::string id = body.substr(0, sep), req = sep == std::string::npos ? "" : body.substr(sep + 1);
        put(l, Frame::MainResp, {id, onMainRequest ? onMainRequest(req) : std::string("0")});
    } else if (t == Frame::Yield && role_ == Role::Follower) {
        yield_ = true;  // not here: becoming main closes the link we are reading
    } else if (t == Frame::FileResp) {
        size_t sep = body.find(kFieldSep);
        if (sep != std::string::npos && wantReq_ && toU(body.substr(0, sep)) == wantReq_ && replyTo_) {
            *replyTo_ = body.substr(sep + 1);
            replied_ = true;
        }
    }
}

bool Cluster::requestTo(const std::string& boardId, const std::string& req, std::string& reply, uint32_t timeoutMs) {
    if (role_ != Role::Main || !clockMs || !sleepMs || inRequest_ || boardId.empty()) return false;  // (never nested)
    int   target = -1;
    Link* to = nullptr;
    for (Link& l : links_)
        if (l.board == boardId) target = l.id, to = &l;
    auto card = cards_.find(boardId);
    if (target < 0 || (card != cards_.end() && !card->second.alive)) return false;
    uint32_t id = nextReq_++, start = clockMs();
    if (!put(*to, Frame::MainReq, {num(id), req})) return false;
    bool done = false;
    inRequest_ = true;
    // never accept or drop a link in here: tick() may be in the middle of its loop over links_
    while (!done && clockMs() - start < timeoutMs) {
        bool any = false, lost = false;
        for (Link& l : links_) {
            std::string bytes;
            int         n = net_.recv(l.id, bytes);
            if (n > 0) l.in.add(bytes), any = true;
            if (l.id != target) continue;  // the others' frames stay in their readers for tick()
            if (n < 0) lost = true;
            Frame       t;
            std::string body;
            while (!done && take(l, t, body)) {
                size_t sep = body.find(kFieldSep);
                if (t == Frame::MainResp && sep != std::string::npos && toU(body.substr(0, sep)) == id) {
                    reply = body.substr(sep + 1);
                    done = true;
                } else if (t != Frame::MainResp) {
                    held_.push_back({l.id, t, body});
                }
            }
            if (l.dead) lost = true;
        }
        if (done || lost) break;
        uint32_t now = clockMs();
        if (now - lastBeat_ >= kBeatMs) {  // a long wait must not look like a lost main
            lastBeat_ = now;
            heartbeat();
        }
        if (!any) sleepMs(1);
    }
    inRequest_ = false;
    if (!done && card != cards_.end()) card->second.alive = false;  // silent (lost power, no FIN yet): skip it until it is heard from
    return done;
}

bool Cluster::request(const std::string& req, std::string& reply, uint32_t timeoutMs) {
    if (role_ != Role::Follower || links_.empty() || !clockMs || !sleepMs || inRequest_) return false;  // (never nested)
    uint32_t id = nextReq_++, start = clockMs();
    if (!put(links_[0], Frame::FileReq, {num(id), req})) return false;
    wantReq_ = id;
    replyTo_ = &reply;
    replied_ = false;
    inRequest_ = true;
    while (!replied_ && clockMs() - start < timeoutMs) {
        std::string bytes;
        int         n = net_.recv(links_[0].id, bytes);
        if (n < 0) break;  // the link went: tick() sees it next
        if (n > 0) links_[0].in.add(bytes);
        Frame       t;
        std::string body;
        while (!links_.empty() && take(links_[0], t, body)) handleFrame(links_[0], t, body, clockMs());
        if (links_.empty() || links_[0].dead) break;
        if (!replied_ && n == 0) sleepMs(1);
    }
    wantReq_ = 0;
    replyTo_ = nullptr;
    inRequest_ = false;
    return replied_;
}

void Cluster::shareSettings() {
    if (role_ != Role::Main || !settings) return;
    std::string s = settings();
    for (Link& l : links_) put(l, Frame::Settings, {s});
}

void Cluster::sendUser(const std::string& record) {
    if (role_ == Role::Main)
        for (Link& l : links_) put(l, Frame::UserChange, {record});
    else if (role_ == Role::Follower && !links_.empty())
        put(links_[0], Frame::UserChange, {record});
}

void Cluster::tick(uint32_t now) {
    if (restart_) {  // a new key or a start() asked for during the last tick: here nothing is looping over the links
        restart_ = false;
        if (keyPending_) key_ = newKey_, keyPending_ = false;
        start(now, restartNumber_);
    }
    if (role_ == Role::Off) return;
    struct Busy {
        bool& b;
        explicit Busy(bool& x) : b(x) { b = true; }
        ~Busy() { b = false; }
    } busy(inTick_);
    if (alone_) {  // no key: hear nothing, let nobody in
        std::string d;
        uint32_t    from;
        while (net_.recvDatagram(d, from)) {}
        for (int id; (id = net_.accept()) >= 0;) net_.close(id);
        return;
    }
    if (!held_.empty()) {  // frames that came in while a request waited, on links still here
        std::vector<Held> h;
        h.swap(held_);
        for (Held& f : h)
            for (size_t i = 0; i < links_.size(); i++)
                if (links_[i].id == f.link) {
                    handleFrame(links_[i], f.type, f.body, now);
                    break;
                }
    }
    std::string d;
    uint32_t    from;
    probedThisTick_ = false;
    while (net_.recvDatagram(d, from)) handleDatagram(d, from, now);

    if (role_ == Role::Main) {
        for (int id; (id = net_.accept()) >= 0;) {  // a new link proves it holds the key before anything else
            links_.push_back({id, FrameReader(), 0});
            links_.back().since = now;
            links_.back().mainNonce = nonce();
            net_.send(id, frame(Frame::Challenge, {hex(links_.back().mainNonce)}));
        }
        if (NodeInfo* me = node(number_)) me->ip = net_.myIp();
    }
    for (size_t i = 0; i < links_.size();) {
        std::string bytes;
        int         n = net_.recv(links_[i].id, bytes);
        if (n > 0) links_[i].in.add(bytes);
        Frame       t;
        std::string body;
        while (take(links_[i], t, body)) handleFrame(links_[i], t, body, now);
        bool unproved = asMain(links_[i]) && links_[i].auth != 2;
        if (n < 0 || links_[i].in.bad() || links_[i].dead || (unproved && now - links_[i].since > kAuthMs)) {  // the other board went away
            if (unproved) turnedAway(now);  // (it never proved the key: a stranger, or a board with another key)
            if (role_ != Role::Main) {
                event(links_[i].dead ? "link to the main: failed the cluster key" : links_[i].in.bad() ? "link to the main: garbled frame" : "link to the main closed");
                if (onMainGone) onMainGone();
            }
            net_.close(links_[i].id);
            int         gone = links_[i].node;
            std::string board = links_[i].board;
            links_.erase(links_.begin() + (long)i);
            bool stillLinked = false;  // the same board on a newer link (it rejoined first)
            for (const Link& other : links_) stillLinked = stillLinked || (!board.empty() && other.board == board);
            if (role_ == Role::Main && !stillLinked && cards_.count(board)) cards_[board].alive = false;
            if (role_ == Role::Main && gone && !board.empty() && !stillLinked) {
                for (size_t k = 0; k < nodes_.size();)  // every node (session) of that board
                    if (nodes_[k].id == board) nodes_.erase(nodes_.begin() + (long)k);
                    else k++;
                event("node " + num((uint32_t)gone) + " left");
                if (onNodeGone) onNodeGone(gone);
                heartbeat();
            }
            continue;
        }
        i++;
    }

    if (probing_) serveProbe(now);
    if (yield_) {
        yield_ = false;
        if (role_ == Role::Follower) {
            becomeMain(now, epoch_ + 1, number_);
            event("node " + num((uint32_t)number_) + " is main (it has the SD card)");
        }
    }
    switch (role_) {
        case Role::Discovering:
            if (now - since_ > listenMs_) {
                // nobody answered: a fresh board is node 1; a cluster survivor keeps its number
                listenMs_ = kListenMs;
                becomeMain(now, epoch_ + 1, number_ ? number_ : (wantNumber_ ? wantNumber_ : 1));
                event("node " + num((uint32_t)number_) + " is main");
            } else if (now - lastAnnounce_ > kAskMs) {
                lastAnnounce_ = now;
                shout("AXIS? " + id_);
            }
            break;
        case Role::Joining:
            if (now - since_ > kListenMs) discover(now);
            break;
        case Role::Main:
            if (now - lastBeat_ >= kBeatMs) {
                lastBeat_ = now;
                if (NodeInfo* me = node(number_)) me->ip = net_.myIp();
                heartbeat();
                tidyNumbers(now);
            }
            if (now - lastAnnounce_ >= kAnnounceMs) {
                lastAnnounce_ = now;
                announce();
            }
            break;
        case Role::Follower:
            if (hasFiles_ && !links_.empty() && now - lastCard_ >= kCardMs) {
                lastCard_ = now;
                put(links_[0], Frame::CardInfo, {"1", num64(freeBytes_)});
            }
            if (now - lastBeat_ > kLostMs || links_.empty()) {  // the main is gone
                int lowest = 0;  // the lowest node on a surviving board (the lost main's sessions don't count)
                for (const NodeInfo& n : nodes_)
                    if (n.ip != mainIp_ && n.number != lastMain_ && (!lowest || n.number < lowest)) lowest = n.number;
                event("MAIN NODE LOST (" + std::string(links_.empty() ? "link closed" : "no heartbeat " + num((now - lastBeat_) / 1000) + "s") +
                      "), NODE " + num((uint32_t)lowest) + " IS NOW MAIN");
                if (lowest && std::find(numbers_.begin(), numbers_.end(), lowest) != numbers_.end()) {
                    becomeMain(now, epoch_ + 1, number_);
                } else {
                    minEpoch_ = epoch_ + 1;
                    wantNumber_ = number_;
                    discover(now);
                    listenMs_ = kListenMs + kLostMs;  // give the new main time to take over before we would
                }
            }
            break;
        default:
            break;
    }
}

}  // namespace axis::cluster
