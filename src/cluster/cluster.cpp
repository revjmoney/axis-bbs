// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/cluster.h"

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
std::string num(uint32_t v) { return std::to_string(v); }
std::string num64(uint64_t v) { return std::to_string((unsigned long long)v); }
uint32_t    toU(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 10); }
uint64_t    toU64(const std::string& s) { return (uint64_t)std::strtoull(s.c_str(), nullptr, 10); }
}  // namespace

NodeInfo* Cluster::node(int number) {
    for (NodeInfo& n : nodes_)
        if (n.number == number) return &n;
    return nullptr;
}

void Cluster::start(uint32_t now, int previousNumber) {
    wantNumber_ = previousNumber;
    discover(now);
}

void Cluster::discover(uint32_t now) {
    for (Link& l : links_) net_.close(l.id);
    links_.clear();
    cards_.clear();
    role_ = Role::Discovering;
    since_ = now;
    lastAnnounce_ = now;
    net_.broadcast("AXIS? " + id_);
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
        net_.send(links_[0].id, frame(Frame::Status, {handle, activity, num((uint32_t)slot)}));
    }
}

void Cluster::setFreeBytes(uint64_t bytes) {
    if (bytes == freeBytes_) return;
    freeBytes_ = bytes;
    if (role_ == Role::Follower && !links_.empty() && hasFiles_)
        net_.send(links_[0].id, frame(Frame::CardInfo, {"1", num64(freeBytes_)}));
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
        net_.send(links_[0].id, frame(Frame::Chat, {num((uint32_t)me), handle, text, page ? "1" : "0", num((uint32_t)kind), num((uint32_t)to)}));
    else if (onChat) onChat(line);  // on our own: just this board
}

void Cluster::deliver(const ChatLine& line) {
    if (onChat) onChat(line);
    std::string f = frame(Frame::Chat, {num(line.node), line.handle, line.text, line.page ? "1" : "0", num((uint32_t)line.kind), num((uint32_t)line.to)});
    for (Link& l : links_) net_.send(l.id, f);
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
    std::string b = frame(Frame::Heartbeat, f);
    for (Link& l : links_)
        if (!net_.send(l.id, b)) event("heartbeat to node " + num((uint32_t)l.node) + " failed");
}

void Cluster::announce() {  // "AXIS! ip epoch node sd": where the main is, and whether it holds the SD card
    net_.broadcast("AXIS! " + num(net_.myIp()) + " " + num(epoch_) + " " + num(number_) + (hasFiles_ ? " 1" : " 0"));
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
        net_.send(o.second->id, frame(Frame::Renumber, nums));
    }
    event("nobody on: renumbered, this board is node " + num((uint32_t)number_));
    heartbeat();
}

void Cluster::handleDatagram(const std::string& d, uint32_t from, uint32_t now) {
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
        if (theyWin) {
            event("another main with epoch " + num(epoch) + ": joining it");
            wantNumber_ = number_;
            minEpoch_ = epoch;
            discover(now);
        } else {
            return;
        }
    }
    if (role_ != Role::Discovering || (epoch < minEpoch_ && !theirSd)) return;
    int link = net_.connect(ip);
    if (link < 0) return;
    links_.push_back({link, FrameReader(), 0});
    role_ = Role::Joining;
    mainIp_ = ip;
    since_ = now;
    net_.send(link, frame(Frame::Hello, {id_, num((uint32_t)(wantNumber_ ? wantNumber_ : number_)), "1", hasFiles_ ? "1" : "0",
                                         num(net_.myIp()), num((uint32_t)slots_), num64(freeBytes_)}));
    (void)from;
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
            net_.send(l.id, frame(Frame::Welcome, welcome));
            event("node " + num((uint32_t)give) + " joined");
            heartbeat();
            if (settings) net_.send(l.id, frame(Frame::Settings, {settings()}));
            if (allUsers)  // its replica of the user base
                for (const std::string& rec : allUsers()) net_.send(l.id, frame(Frame::UserChange, {rec}));
            // a card for the pool; only from a board that sends its free space (it answers MainReq: older ones never would)
            if (f.size() >= 7 && f[3] == "1") cards_[f[0]] = {ip, toU64(f[6]), true};
            else cards_.erase(f[0]);
            if (!hasFiles_ && f.size() >= 4 && f[3] == "1") {  // the file areas live on its card: it should be main
                event("node " + num((uint32_t)give) + " has the SD card: handing main to it");
                net_.send(l.id, frame(Frame::Yield, {}));
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
            net_.send(l.id, frame(Frame::FileResp, {id, onRequest ? onRequest(l.node, req) : std::string("0")}));
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
            net_.send(l.id, frame(Frame::Status, {status_[i].first, status_[i].second, num((uint32_t)i)}));
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
        net_.send(l.id, frame(Frame::MainResp, {id, onMainRequest ? onMainRequest(req) : std::string("0")}));
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
    int target = -1;
    for (const Link& l : links_)
        if (l.board == boardId) target = l.id;
    auto card = cards_.find(boardId);
    if (target < 0 || (card != cards_.end() && !card->second.alive)) return false;
    uint32_t id = nextReq_++, start = clockMs();
    if (!net_.send(target, frame(Frame::MainReq, {num(id), req}))) return false;
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
            while (!done && l.in.next(t, body)) {
                size_t sep = body.find(kFieldSep);
                if (t == Frame::MainResp && sep != std::string::npos && toU(body.substr(0, sep)) == id) {
                    reply = body.substr(sep + 1);
                    done = true;
                } else if (t != Frame::MainResp) {
                    held_.push_back({l.id, t, body});
                }
            }
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
    if (!net_.send(links_[0].id, frame(Frame::FileReq, {num(id), req}))) return false;
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
        while (!links_.empty() && links_[0].in.next(t, body)) handleFrame(links_[0], t, body, clockMs());
        if (!replied_ && n == 0) sleepMs(1);
    }
    wantReq_ = 0;
    replyTo_ = nullptr;
    inRequest_ = false;
    return replied_;
}

void Cluster::shareSettings() {
    if (role_ != Role::Main || !settings) return;
    std::string f = frame(Frame::Settings, {settings()});
    for (Link& l : links_) net_.send(l.id, f);
}

void Cluster::sendUser(const std::string& record) {
    std::string f = frame(Frame::UserChange, {record});
    if (role_ == Role::Main)
        for (Link& l : links_) net_.send(l.id, f);
    else if (role_ == Role::Follower && !links_.empty())
        net_.send(links_[0].id, f);
}

void Cluster::tick(uint32_t now) {
    if (role_ == Role::Off) return;
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
    while (net_.recvDatagram(d, from)) handleDatagram(d, from, now);

    if (role_ == Role::Main) {
        for (int id; (id = net_.accept()) >= 0;) links_.push_back({id, FrameReader(), 0});
        if (NodeInfo* me = node(number_)) me->ip = net_.myIp();
    }
    for (size_t i = 0; i < links_.size();) {
        std::string bytes;
        int         n = net_.recv(links_[i].id, bytes);
        if (n > 0) links_[i].in.add(bytes);
        Frame       t;
        std::string body;
        while (links_[i].in.next(t, body)) handleFrame(links_[i], t, body, now);
        if (n < 0 || links_[i].in.bad()) {  // the other board went away
            if (role_ != Role::Main) {
                event(links_[i].in.bad() ? "link to the main: garbled frame" : "link to the main closed");
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
                net_.broadcast("AXIS? " + id_);
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
                net_.send(links_[0].id, frame(Frame::CardInfo, {"1", num64(freeBytes_)}));
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
