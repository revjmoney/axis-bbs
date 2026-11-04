// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "cluster/cluster.h"

using namespace axis::cluster;

namespace {
// A LAN in memory: datagrams go to every board that is powered; a link is two byte queues.
struct Wire {
    std::string toFollower, toMain;
    bool        up = true;
};
struct FakeLan;
struct FakeNet : Net {
    FakeLan*                                   lan = nullptr;
    uint32_t                                   ip = 0;
    bool                                       powered = true;
    std::deque<std::pair<std::string, uint32_t>> datagrams;
    std::map<int, std::pair<std::shared_ptr<Wire>, bool>> wires;  // link -> wire, true if I dialed
    std::deque<int>                            incoming;
    int                                        next = 1;

    void broadcast(const std::string& d) override;
    bool recvDatagram(std::string& d, uint32_t& from) override {
        if (datagrams.empty()) return false;
        d = datagrams.front().first;
        from = datagrams.front().second;
        datagrams.pop_front();
        return true;
    }
    int  connect(uint32_t to) override;
    int  accept() override {
        if (incoming.empty()) return -1;
        int id = incoming.front();
        incoming.pop_front();
        return id;
    }
    bool send(int link, const std::string& b) override {
        auto it = wires.find(link);
        if (it == wires.end() || !it->second.first->up) return false;
        (it->second.second ? it->second.first->toMain : it->second.first->toFollower) += b;
        return true;
    }
    int recv(int link, std::string& out) override {
        auto it = wires.find(link);
        if (it == wires.end()) return -1;
        std::string& in = it->second.second ? it->second.first->toFollower : it->second.first->toMain;
        int          n = (int)in.size();
        out += in;
        in.clear();
        return (!it->second.first->up && n == 0) ? -1 : n;
    }
    void close(int link) override {
        auto it = wires.find(link);
        if (it != wires.end()) it->second.first->up = false;
        wires.erase(link);
    }
    uint32_t myIp() override { return ip; }
    void     powerOff() {  // the board is unplugged: its links drop, it hears nothing
        powered = false;
        for (auto& w : wires) w.second.first->up = false;
        wires.clear();
        datagrams.clear();
    }
};
struct FakeLan {
    std::vector<FakeNet*> nets;
};
void FakeNet::broadcast(const std::string& d) {
    if (!powered) return;
    for (FakeNet* n : lan->nets)
        if (n != this && n->powered) n->datagrams.emplace_back(d, ip);
}
int FakeNet::connect(uint32_t to) {
    for (FakeNet* n : lan->nets)
        if (n->ip == to && n->powered) {
            auto w = std::make_shared<Wire>();
            int  mine = next++, theirs = n->next++;
            wires[mine] = {w, true};
            n->wires[theirs] = {w, false};
            n->incoming.push_back(theirs);
            return mine;
        }
    return -1;
}

struct Board {
    FakeNet                  net;
    std::unique_ptr<Cluster> c;
    std::vector<std::string> heard;
    Board(FakeLan& lan, uint32_t ip, const char* id) {
        net.lan = &lan;
        net.ip = ip;
        lan.nets.push_back(&net);
        c.reset(new Cluster(net, id));
        c->onChat = [this](const ChatLine& l) { heard.push_back(std::string(l.page ? "page " : "chat ") + l.handle + ": " + l.text); };
    }
};

void run(std::vector<Board*> boards, uint32_t& now, uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 50, now += 50)
        for (Board* b : boards)
            if (b->net.powered) b->c->tick(now);
}

struct TwoBoards {
    FakeLan  lan;
    Board    a{lan, 101, "board-a"}, b{lan, 102, "board-b"};
    uint32_t now = 0;
    TwoBoards() {
        a.c->start(now);
        run({&a}, now, 4000);
        b.c->start(now);
        run({&a, &b}, now, 3000);
    }
};
}  // namespace

TEST_CASE("frames keep their fields, spaces included, and arrive whole when split") {
    std::string  f = frame(Frame::Chat, {"2", "Spindle Kid", "anyone cutting aluminum?", "0"});
    FrameReader  r;
    Frame        t;
    std::string  body;
    r.add(f.substr(0, 7));
    CHECK_FALSE(r.next(t, body));
    r.add(f.substr(7));
    REQUIRE(r.next(t, body));
    CHECK(t == Frame::Chat);
    auto v = fields(body);
    REQUIRE(v.size() == 4);
    CHECK(v[1] == "Spindle Kid");
    CHECK(v[2] == "anyone cutting aluminum?");
}

TEST_CASE("the first board up becomes main, node 1") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a");
    uint32_t now = 0;
    a.c->start(now);
    run({&a}, now, 4000);
    CHECK(a.c->role() == Role::Main);
    CHECK(a.c->nodeNumber() == 1);
}

TEST_CASE("a second board joins as node 2 and both list both nodes") {
    TwoBoards t;
    CHECK(t.b.c->role() == Role::Follower);
    CHECK(t.b.c->nodeNumber() == 2);
    CHECK(t.a.c->nodes().size() == 2);
    CHECK(t.b.c->nodes().size() == 2);
}

TEST_CASE("each node's caller shows on every board") {
    TwoBoards t;
    t.b.c->setStatus("Spindle Kid", "Files");
    run({&t.a, &t.b}, t.now, 2500);
    bool onA = false;
    for (auto& n : t.a.c->nodes()) onA = onA || (n.number == 2 && n.handle == "Spindle Kid" && n.activity == "Files");
    CHECK(onA);
}

TEST_CASE("chat and pages reach every board, the sender's too") {
    TwoBoards t;
    t.b.c->chat("Spindle Kid", "who has a rotary axis?");
    t.a.c->page("Rev", "hello from node 1");
    run({&t.a, &t.b}, t.now, 500);
    REQUIRE(t.a.heard.size() == 2);
    REQUIRE(t.b.heard.size() == 2);  // in the order the main got them
    bool chat = false, page = false;
    for (auto& h : t.b.heard) {
        chat = chat || h == "chat Spindle Kid: who has a rotary axis?";
        page = page || h == "page Rev: hello from node 1";
    }
    CHECK(chat);
    CHECK(page);
}

TEST_CASE("when the main is unplugged node 2 takes over with a higher epoch") {
    TwoBoards t;
    uint32_t  before = t.b.c->epoch();
    t.a.net.powerOff();
    run({&t.a, &t.b}, t.now, 7000);
    CHECK(t.b.c->role() == Role::Main);
    CHECK(t.b.c->nodeNumber() == 2);
    CHECK(t.b.c->epoch() > before);
}

TEST_CASE("the old main comes back as a follower of the new one") {
    TwoBoards t;
    t.a.net.powerOff();
    run({&t.a, &t.b}, t.now, 7000);
    t.a.net.powered = true;
    t.a.c->start(t.now, 1);
    run({&t.a, &t.b}, t.now, 4000);
    CHECK(t.b.c->role() == Role::Main);
    CHECK(t.a.c->role() == Role::Follower);
    CHECK(t.a.c->nodeNumber() == 1);  // it keeps its number
}

TEST_CASE("a follower asks the main a question and gets its answer, binary bytes intact") {
    TwoBoards t;
    t.a.c->onRequest = [](int node, const std::string& req) { return "1node " + std::to_string(node) + ":" + req; };
    t.b.c->clockMs = [&t] { return t.now; };
    t.b.c->sleepMs = [&t](uint32_t) {  // while node 2 waits, the main keeps running
        t.now += 5;
        t.a.c->tick(t.now);
    };
    std::string reply;
    REQUIRE(t.b.c->request(std::string("A\x1f" "ansi\x1f\x00\xff", 9), reply));
    CHECK(reply == "1node 2:" + std::string("A\x1f" "ansi\x1f\x00\xff", 9));
}

TEST_CASE("a request with no main to ask fails at once") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a");
    uint32_t now = 0;
    a.c->start(now);
    run({&a}, now, 4000);
    std::string reply;
    CHECK_FALSE(a.c->request("L\x1f" "ansi", reply));
}

TEST_CASE("a request times out if the main never answers") {
    TwoBoards t;
    t.b.c->clockMs = [&t] { return t.now; };
    t.b.c->sleepMs = [&t](uint32_t ms) { t.now += ms ? ms : 1; };  // the main is frozen
    std::string reply;
    CHECK_FALSE(t.b.c->request("L\x1f" "ansi", reply, 1000));
}

TEST_CASE("a joining board gets every user, and user changes travel both ways") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    std::vector<std::string> onA, onB;
    a.c->allUsers = [] { return std::vector<std::string>{"[user]\nhandle=Rev\n", "[user]\nhandle=Chip Breaker\n"}; };
    a.c->onUser = [&](const std::string& r) { onA.push_back(r); };
    b.c->onUser = [&](const std::string& r) { onB.push_back(r); };
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now);
    run({&a, &b}, now, 3000);
    REQUIRE(onB.size() == 2);
    CHECK(onB[1] == "[user]\nhandle=Chip Breaker\n");
    b.c->sendUser("[user]\nhandle=Lathe Lord\nstatus=pending\n");  // a new application on node 2
    run({&a, &b}, now, 200);
    REQUIRE(onA.size() == 1);
    CHECK(onA[0] == "[user]\nhandle=Lathe Lord\nstatus=pending\n");
    a.c->sendUser("[user]\nhandle=Lathe Lord\nstatus=validated\n");  // the sysop validates on node 1
    run({&a, &b}, now, 200);
    REQUIRE(onB.size() == 3);
    CHECK(onB[2] == "[user]\nhandle=Lathe Lord\nstatus=validated\n");
}

TEST_CASE("the main hears when a follower's link goes, to drop its unfinished uploads") {
    TwoBoards t;
    int       gone = 0;
    t.a.c->onNodeGone = [&](int n) { gone = n; };
    t.b.net.powerOff();
    run({&t.a, &t.b}, t.now, 500);
    CHECK(gone == 2);
}

TEST_CASE("a main without an SD card hands over to a board that has one") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    a.c->setHasFiles(false);  // no SD card
    b.c->setHasFiles(true);
    a.c->start(now);
    run({&a}, now, 4000);
    REQUIRE(a.c->role() == Role::Main);
    b.c->start(now);
    run({&a, &b}, now, 8000);
    CHECK(b.c->role() == Role::Main);
    CHECK(a.c->role() == Role::Follower);
    CHECK(b.c->nodeNumber() == 2);  // numbers stay with the boards
    CHECK(a.c->nodeNumber() == 1);
    run({&a, &b}, now, 10000);  // and it stays that way
    CHECK(b.c->role() == Role::Main);
}

TEST_CASE("two boards that both lack an SD card don't hand over back and forth") {
    TwoBoards t;
    run({&t.a, &t.b}, t.now, 8000);
    CHECK(t.a.c->role() == Role::Main);
    CHECK(t.b.c->role() == Role::Follower);
}

TEST_CASE("the main knows each node's address, so it can pass a caller to it") {
    TwoBoards t;
    uint32_t  ipB = 0;
    for (auto& n : t.a.c->nodes())
        if (n.number == 2) ipB = n.ip;
    CHECK(ipB == 102);
}

TEST_CASE("a node that reboots and rejoins before its old link dies stays listed") {
    TwoBoards t;
    auto      old = t.b.net.wires;  // its old link, not yet noticed as dead by the main
    t.b.net.wires.clear();
    t.b.c.reset(new Cluster(t.b.net, "board-b"));  // board B restarts
    t.b.c->start(t.now, 2);
    run({&t.a, &t.b}, t.now, 4000);
    REQUIRE(t.b.c->role() == Role::Follower);
    for (auto& w : old) w.second.first->up = false;  // now the old link is found dead
    run({&t.a, &t.b}, t.now, 2000);
    bool listed = false;
    for (auto& n : t.a.c->nodes()) listed = listed || n.number == 2;
    CHECK(listed);
    CHECK(t.a.c->nodes().size() == 2);
}

TEST_CASE("a board with two sessions gets two node numbers, and each session's caller shows") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    a.c->setSlots(2);
    b.c->setSlots(2);
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now);
    run({&a, &b}, now, 3000);
    CHECK(a.c->numbers() == std::vector<int>{1, 2});
    CHECK(b.c->numbers() == std::vector<int>{3, 4});
    CHECK(a.c->nodes().size() == 4);
    CHECK(b.c->nodes().size() == 4);
    b.c->setStatus(1, "Lathe Lord", "Doors");  // the board's second session
    run({&a, &b}, now, 2500);
    bool seen = false;
    for (auto& n : a.c->nodes()) seen = seen || (n.number == 4 && n.handle == "Lathe Lord" && n.activity == "Doors");
    CHECK(seen);
}

TEST_CASE("when a two-session main is lost, the next board takes over with its own numbers") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    a.c->setSlots(2);
    b.c->setSlots(2);
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now);
    run({&a, &b}, now, 3000);
    a.net.powerOff();
    run({&a, &b}, now, 7000);
    CHECK(b.c->role() == Role::Main);
    CHECK(b.c->numbers() == std::vector<int>{3, 4});
}

TEST_CASE("a board that rejoins with more sessions than before gets the extra numbers") {
    TwoBoards t;  // board B joined with one session (node 2)
    t.a.c->setSlots(1);
    auto old = t.b.net.wires;
    t.b.net.wires.clear();
    t.b.c.reset(new Cluster(t.b.net, "board-b"));  // new firmware: two sessions
    t.b.c->setSlots(2);
    t.b.c->start(t.now, 2);
    run({&t.a, &t.b}, t.now, 4000);
    CHECK(t.b.c->numbers() == std::vector<int>{2, 3});
    (void)old;
}

TEST_CASE("a joining board gets the main's BBS settings") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    std::string got;
    a.c->settings = [] { return std::string("bbs_name=Axis Underground CNC\nlocation=Myrtle Beach, SC, USA\n"); };
    b.c->onSettings = [&](const std::string& s) { got = s; };
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now);
    run({&a, &b}, now, 3000);
    CHECK(got == "bbs_name=Axis Underground CNC\nlocation=Myrtle Beach, SC, USA\n");
}
