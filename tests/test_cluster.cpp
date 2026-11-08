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

uint32_t testRandom() {  // any numbers will do for the tests' nonces
    static uint32_t x = 2463534242u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}
const char* kTestKey = "test-cluster-key";

struct Board {
    FakeNet                  net;
    std::unique_ptr<Cluster> c;
    std::vector<std::string> heard;
    std::vector<std::string> events;
    Board(FakeLan& lan, uint32_t ip, const char* id, const char* key = kTestKey) {
        net.lan = &lan;
        net.ip = ip;
        lan.nets.push_back(&net);
        make(id, key);
    }
    void make(const char* id, const char* key = kTestKey) {  // a fresh Cluster (the board reboots)
        c.reset(new Cluster(net, id));
        c->setKey(key);
        c->setRandom(testRandom);
        c->onChat = [this](const ChatLine& l) { heard.push_back(std::string(l.page ? "page " : "chat ") + l.handle + ": " + l.text); };
        c->onEvent = [this](const std::string& e) { events.push_back(e); };
    }
    bool saw(const std::string& part) const {
        for (auto& e : events)
            if (e.find(part) != std::string::npos) return true;
        return false;
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

TEST_CASE("the SD board takes node 1 back once nobody is on a call, and the others renumber after it") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    int      savedA = 0, savedB = 0;
    a.c->onNumber = [&](int n) { savedA = n; };
    b.c->onNumber = [&](int n) { savedB = n; };
    b.c->setHasFiles(true);
    a.c->start(now);
    run({&a}, now, 4000);
    a.c->setStatus("Rev", "Menu");  // a caller on node 1: numbers can't move under them
    b.c->start(now);
    run({&a, &b}, now, 30000);
    REQUIRE(b.c->role() == Role::Main);
    CHECK(b.c->nodeNumber() == 2);
    CHECK(a.c->nodeNumber() == 1);
    a.c->setStatus("", "");  // they hung up
    run({&a, &b}, now, 15000);
    CHECK(b.c->nodeNumber() == 1);
    CHECK(a.c->nodeNumber() == 2);
    CHECK(savedB == 1);  // remembered for the next boot
    CHECK(savedA == 2);
    REQUIRE(a.c->nodes().size() == 2);
    for (const NodeInfo& n : a.c->nodes()) CHECK(n.number == (n.id == "board-b" ? 1 : 2));  // every board agrees
    a.c->setStatus("Kid", "Menu");  // and the new numbers carry the callers
    run({&a, &b}, now, 2000);
    for (const NodeInfo& n : b.c->nodes())
        if (n.number == 2) CHECK(n.handle == "Kid");
}

TEST_CASE("node numbers with a gap close up when the board is idle") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b");
    uint32_t now = 0;
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now, 4);  // it was node 4 last time and asks for it back
    run({&a, &b}, now, 3000);
    CHECK(b.c->nodeNumber() == 4);
    run({&a, &b}, now, 15000);
    CHECK(a.c->nodeNumber() == 1);
    CHECK(b.c->nodeNumber() == 2);
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
    t.b.make("board-b");  // board B restarts
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
    t.b.make("board-b");  // new firmware: two sessions
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

TEST_CASE("node to node lines keep their kind and target across boards") {
    TwoBoards                t;
    std::vector<ChatLine>    got;
    t.a.c->onChat = [&](const ChatLine& l) { got.push_back(l); };
    t.b.c->send("Spindle Kid", 2, 1, "meet me in LORD");  // a note (kind 2) for node 1, from node 2
    t.b.c->send("Spindle Kid", 8, 1, "h");                // a split chat keystroke
    run({&t.a, &t.b}, t.now, 500);
    REQUIRE(got.size() == 2);
    CHECK(got[0].kind == 2);
    CHECK(got[0].to == 1);
    CHECK(got[0].node == 2);
    CHECK(got[0].text == "meet me in LORD");
    CHECK(got[1].kind == 8);
    CHECK_FALSE(got[0].page);
}

TEST_CASE("two mains that meet (both node 1, same epoch): the one with the SD card stays main") {
    FakeLan  lanA, lanB;
    Board    a(lanA, 101, "board-a"), b(lanB, 102, "board-b");
    uint32_t now = 0;
    a.c->setHasFiles(true);
    a.c->start(now);
    b.c->start(now);
    run({&a, &b}, now, 4000);  // two separate LANs: each becomes main, node 1, epoch 1
    REQUIRE(a.c->role() == Role::Main);
    REQUIRE(b.c->role() == Role::Main);
    REQUIRE(a.c->epoch() == b.c->epoch());
    lanB.nets.clear();  // now they share one LAN
    b.net.lan = &lanA;
    lanA.nets.push_back(&b.net);
    run({&a, &b}, now, 15000);
    CHECK(a.c->role() == Role::Main);
    CHECK(b.c->role() == Role::Follower);
    CHECK(a.c->nodes().size() == 2);
}

TEST_CASE("two mains without SD cards that tie on everything: the lower address stays main") {
    FakeLan  lanA, lanB;
    Board    a(lanA, 101, "board-a"), b(lanB, 102, "board-b");
    uint32_t now = 0;
    a.c->start(now);
    b.c->start(now);
    run({&a, &b}, now, 4000);
    lanB.nets.clear();
    b.net.lan = &lanA;
    lanA.nets.push_back(&b.net);
    run({&a, &b}, now, 15000);
    CHECK(a.c->role() == Role::Main);
    CHECK(b.c->role() == Role::Follower);
}

TEST_CASE("a chat line that arrives while a request waits is held until after it, so nothing nests") {
    TwoBoards t;
    bool once = true;
    t.a.c->onRequest = [&](int, const std::string&) {
        if (once) t.a.c->chat("Rev", "this arrives mid-request");  // reaches b before the reply does
        once = false;
        return std::string("1answer");
    };
    t.b.c->clockMs = [&] { return t.now; };
    t.b.c->sleepMs = [&](uint32_t ms) {
        t.now += ms;
        t.a.c->tick(t.now);
    };
    int  heard = 0;
    bool nested = true;
    t.b.c->onChat = [&](const ChatLine&) {
        heard++;
        std::string r;
        nested = t.b.c->request("again", r, 200);  // the BBS asking again from inside: must not nest
    };
    std::string reply;
    CHECK(t.b.c->request("q", reply, 2000));
    CHECK(reply == "1answer");
    CHECK(heard == 0);  // held during the request
    run({&t.a, &t.b}, t.now, 300);
    CHECK(heard == 1);  // delivered after it
    CHECK(nested);      // and by then a fresh request is fine
}

// SD card clustering: the main knows which followers hold a card and how much room it has, and can
// ask one of them a question (requestTo), the way a follower asks the main.
namespace {
struct CardBoards {  // a: main with a card, b: follower with a card, c: follower without one
    FakeLan  lan;
    Board    a{lan, 101, "board-a"}, b{lan, 102, "board-b"}, c{lan, 103, "board-c"};
    uint32_t now = 0;
    CardBoards() {
        a.c->setHasFiles(true);
        b.c->setHasFiles(true);
        b.c->setFreeBytes(5000000000ull);  // more than 32 bits
        a.c->start(now);
        run({&a}, now, 4000);
        b.c->start(now);
        run({&a, &b}, now, 3000);
        c.c->start(now);
        run({&a, &b, &c}, now, 3000);
    }
};
}  // namespace

TEST_CASE("a follower with an SD card tells the main its free space; one without a card is not listed") {
    CardBoards t;
    REQUIRE(t.a.c->role() == Role::Main);
    auto cards = t.a.c->cardBoards();
    REQUIRE(cards.size() == 1);
    CHECK(cards[0].id == "board-b");
    CHECK(cards[0].ip == 102);
    CHECK(cards[0].freeBytes == 5000000000ull);
    CHECK(cards[0].alive);
    t.b.c->setFreeBytes(4000);  // an upload landed: it tells the main again
    run({&t.a, &t.b, &t.c}, t.now, 200);
    CHECK(t.a.c->cardBoards()[0].freeBytes == 4000);
}

TEST_CASE("a card board that goes away stays listed as down, and comes back alive when it rejoins") {
    CardBoards t;
    t.b.net.powerOff();
    run({&t.a, &t.c}, t.now, 500);
    auto cards = t.a.c->cardBoards();
    REQUIRE(cards.size() == 1);
    CHECK_FALSE(cards[0].alive);
    t.b.net.powered = true;
    t.b.c->start(t.now, 2);
    run({&t.a, &t.b, &t.c}, t.now, 4000);
    REQUIRE(t.a.c->cardBoards().size() == 1);
    CHECK(t.a.c->cardBoards()[0].alive);
}

TEST_CASE("the main asks a card board a question and gets its answer, binary bytes intact") {
    CardBoards t;
    t.b.c->onMainRequest = [](const std::string& req) { return "1b saw:" + req; };
    t.a.c->clockMs = [&t] { return t.now; };
    t.a.c->sleepMs = [&t](uint32_t) {  // while the main waits, the follower keeps running
        t.now += 5;
        t.b.c->tick(t.now);
    };
    std::string reply, req("A\x1f" "ansi\x1f\x00\xff", 9);
    REQUIRE(t.a.c->requestTo("board-b", req, reply));
    CHECK(reply == "1b saw:" + req);
}

TEST_CASE("the main's request to an unknown or gone board fails at once, and one that never answers times out") {
    CardBoards t;
    t.a.c->clockMs = [&t] { return t.now; };
    t.a.c->sleepMs = [&t](uint32_t ms) { t.now += ms ? ms : 1; };  // nobody else runs
    std::string reply;
    uint32_t    start = t.now;
    CHECK_FALSE(t.a.c->requestTo("board-z", "L\x1f" "ansi", reply, 1000));
    CHECK(t.now == start);
    CHECK_FALSE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 1000));  // b is frozen
    CHECK(t.now - start >= 1000);
    t.b.net.powerOff();
    start = t.now;
    CHECK_FALSE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 1000));
    CHECK(t.now - start < 100);  // the link is down: no waiting out the timeout
}

TEST_CASE("a follower without an answer for the main says no") {
    CardBoards t;
    t.a.c->clockMs = [&t] { return t.now; };
    t.a.c->sleepMs = [&t](uint32_t) {
        t.now += 5;
        t.b.c->tick(t.now);
    };
    std::string reply;
    REQUIRE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 1000));
    CHECK(reply == "0");
}

TEST_CASE("a cardless follower's request that makes the main ask a card board gets the combined answer") {
    CardBoards t;
    t.b.c->onMainRequest = [](const std::string& req) { return "1from b:" + req; };
    t.a.c->clockMs = [&t] { return t.now; };
    t.a.c->sleepMs = [&t](uint32_t) {
        t.now += 5;
        t.b.c->tick(t.now);
    };
    t.a.c->onRequest = [&t](int, const std::string& req) {
        std::string r;
        return t.a.c->requestTo("board-b", req, r) ? "1main+" + r : std::string("0");
    };
    t.c.c->clockMs = [&t] { return t.now; };
    t.c.c->sleepMs = [&t](uint32_t) {
        t.now += 5;
        t.a.c->tick(t.now);
    };
    std::string reply;
    REQUIRE(t.c.c->request("L\x1f" "ansi", reply, 3000));
    CHECK(reply == "1main+1from b:L\x1f" "ansi");
}

TEST_CASE("a follower answers the main even while it waits on its own request, so the two never deadlock") {
    CardBoards t;
    t.b.c->onMainRequest = [](const std::string&) { return std::string("1here"); };
    // the main is busy asking b something when b's own request goes out: the main's question is
    // already on the wire to b, and the main won't answer b until its own question is answered
    t.a.c->clockMs = [&t] { return t.now; };
    t.b.c->clockMs = [&t] { return t.now; };
    t.b.c->sleepMs = [&t](uint32_t ms) { t.now += ms ? ms : 1; };
    bool        asked = false, bGot = true;
    std::string bReply;
    t.a.c->sleepMs = [&](uint32_t) {
        t.now += 1;
        if (!asked) asked = true, bGot = t.b.c->request("q", bReply, 300);  // b asks while the main waits on it
    };
    std::string reply;
    CHECK(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 2000));
    CHECK(asked);
    CHECK(reply == "1here");  // b answered the main from inside its own wait
    CHECK_FALSE(bGot);        // and the main held b's request for later instead of nesting
}

TEST_CASE("while the main waits on a card board, other followers' frames wait for after it and heartbeats keep going") {
    CardBoards t;
    t.a.c->clockMs = [&t] { return t.now; };
    int heardOnA = 0;
    t.a.c->onChat = [&](const ChatLine&) { heardOnA++; };
    bool chatted = false;
    t.a.c->sleepMs = [&](uint32_t) {  // b is frozen; c keeps running and chats once
        t.now += 5;
        if (!chatted) t.c.c->chat("Chip", "hello from c"), chatted = true;
        t.c.c->tick(t.now);
    };
    std::string reply;
    CHECK_FALSE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 6000));  // a long wait; heartbeats still arrive
    CHECK(heardOnA == 0);  // held while the main waited
    CHECK(t.c.c->role() == Role::Follower);  // the heartbeat kept coming: no failover
    run({&t.a, &t.c}, t.now, 100);
    CHECK(heardOnA == 1);
}

TEST_CASE("a board on firmware from before the cluster key, or any device that just says Hello, is turned away") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a");
    uint32_t now = 0;
    a.c->setHasFiles(true);
    a.c->start(now);
    run({&a}, now, 4000);
    FakeNet old;  // speaks the old protocol: Hello straight away, no proof of the key
    old.lan = &lan;
    old.ip = 104;
    lan.nets.push_back(&old);
    int link = old.connect(101);
    REQUIRE(link >= 0);
    old.send(link, frame(Frame::Hello, {"board-old", "0", "1", "1", "104", "1", "999999"}));
    run({&a}, now, 200);
    CHECK(a.c->nodes().size() == 1);  // it never joined
    CHECK(a.c->cardBoards().empty());
    CHECK(a.saw("turned away"));
    std::string bytes;
    run({&a}, now, 200);
    old.recv(link, bytes);             // the main's Challenge, which it never answered
    CHECK(old.recv(link, bytes) < 0);  // and the main hung up on it
}

TEST_CASE("a card board that stops answering is marked down at once, and up again when it is heard from") {
    CardBoards t;
    t.a.c->clockMs = [&t] { return t.now; };
    t.a.c->sleepMs = [&t](uint32_t ms) { t.now += ms ? ms : 1; };  // b is frozen (lost power, no FIN yet)
    std::string reply;
    CHECK_FALSE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 1000));
    REQUIRE(t.a.c->cardBoards().size() == 1);
    CHECK_FALSE(t.a.c->cardBoards()[0].alive);
    uint32_t start = t.now;
    CHECK_FALSE(t.a.c->requestTo("board-b", "L\x1f" "ansi", reply, 1000));  // no second wait
    CHECK(t.now == start);
    run({&t.a, &t.b, &t.c}, t.now, 11000);  // b wakes up: it tells the main about its card now and then
    CHECK(t.a.c->cardBoards()[0].alive);
}

// ---------------------------------------------------------------- the cluster key

TEST_CASE("boards with the same cluster key cluster, and every frame between them is signed") {
    TwoBoards t;
    CHECK(t.a.c->role() == Role::Main);
    CHECK(t.b.c->role() == Role::Follower);
    CHECK(t.a.c->nodes().size() == 2);
    t.b.c->chat("Spindle Kid", "anyone cutting aluminum?");
    run({&t.a, &t.b}, t.now, 200);
    REQUIRE(t.a.heard.size() == 1);
    CHECK(t.a.heard[0] == "chat Spindle Kid: anyone cutting aluminum?");
}

TEST_CASE("a board with the wrong cluster key can't join, and the main's node list doesn't change") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a"), b(lan, 102, "board-b", "some-other-key");
    uint32_t now = 0;
    a.c->start(now);
    run({&a}, now, 4000);
    b.c->start(now);
    run({&a, &b}, now, 8000);
    CHECK(a.c->role() == Role::Main);
    CHECK(a.c->nodes().size() == 1);  // only itself
    CHECK(b.c->role() != Role::Follower);  // it never heard a main it could trust: it is on its own
}

TEST_CASE("a forged announcement doesn't make the real main step down") {
    TwoBoards t;
    FakeNet   rogue;  // a laptop on the Wi-Fi: no key
    rogue.lan = &t.lan;
    rogue.ip = 9;
    t.lan.nets.push_back(&rogue);
    for (int i = 0; i < 5; i++) {
        rogue.broadcast("AXIS! 9 4000000000 1 1");             // "I'm main, newer, with the card"
        rogue.broadcast("AXIS! 9 4000000000 1 1 ~0123456789abcdef0123456789abcdef");  // with a made-up tag
        run({&t.a, &t.b}, t.now, 100);
    }
    CHECK(t.a.c->role() == Role::Main);
    CHECK(t.b.c->role() == Role::Follower);
    CHECK(t.a.c->nodes().size() == 2);
    CHECK(rogue.incoming.empty());  // nobody dialed it
}

TEST_CASE("a replayed, validly signed announcement from where no main answers changes nothing") {
    TwoBoards t;
    // capture a real announcement with a higher epoch: a board with the same key on another LAN, restarted once
    FakeLan  other;
    Board    old(other, 103, "board-old");
    FakeNet  sniffer;
    sniffer.lan = &other;
    sniffer.ip = 200;
    other.nets.push_back(&sniffer);
    uint32_t then = 0;
    old.c->start(then);
    run({&old}, then, 4000);
    old.c->start(then, 1);
    run({&old}, then, 4000);
    REQUIRE(old.c->role() == Role::Main);
    REQUIRE(old.c->epoch() > t.a.c->epoch());
    std::string captured;
    for (auto& d : sniffer.datagrams)
        if (d.first.rfind("AXIS! ", 0) == 0) captured = d.first;
    REQUIRE(!captured.empty());
    // replayed on the real LAN, first where nothing is at 103, then where a silent device sits there
    for (int i = 0; i < 20; i++) {
        t.a.net.datagrams.emplace_back(captured, 103);
        run({&t.a, &t.b}, t.now, 100);
    }
    CHECK(t.a.c->role() == Role::Main);
    CHECK(t.a.c->nodes().size() == 2);
    CHECK(t.b.c->role() == Role::Follower);
    FakeNet silent;  // answers the dial, never says a word
    silent.lan = &t.lan;
    silent.ip = 103;
    t.lan.nets.push_back(&silent);
    for (int i = 0; i < 100; i++) {  // 10 s of replays, one every 100 ms
        t.a.net.datagrams.emplace_back(captured, 103);
        run({&t.a, &t.b}, t.now, 100);
    }
    CHECK(t.a.c->role() == Role::Main);
    CHECK(t.a.c->nodes().size() == 2);
    CHECK(t.b.c->role() == Role::Follower);
    CHECK(silent.incoming.size() <= 2);  // one check at a time, and not again at once after it failed
}

TEST_CASE("a connection that never proves the cluster key is dropped after 5 s, and the event is said once a minute") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a");
    uint32_t now = 0;
    a.c->start(now);
    run({&a}, now, 4000);
    FakeNet quiet;
    quiet.lan = &lan;
    quiet.ip = 104;
    lan.nets.push_back(&quiet);
    int link = quiet.connect(101);
    REQUIRE(link >= 0);
    std::string bytes;
    run({&a}, now, 4000);
    CHECK(quiet.recv(link, bytes) >= 0);  // still open at 4 s (the Challenge came)
    run({&a}, now, 1500);
    bytes.clear();
    CHECK(quiet.recv(link, bytes) < 0);  // dropped after 5 s
    int said = 0;
    for (auto& e : a.events) said += e.find("turned away") != std::string::npos;
    CHECK(said == 1);
    for (int i = 0; i < 5; i++) {  // more bad peers inside the minute: nothing more in the log
        int l = quiet.connect(101);
        quiet.send(l, frame(Frame::Hello, {"board-old", "0", "1", "1", "104", "1", "999999"}));
        run({&a}, now, 500);
    }
    said = 0;
    for (auto& e : a.events) said += e.find("turned away") != std::string::npos;
    CHECK(said == 1);
    run({&a}, now, 60000);
    int l = quiet.connect(101);
    quiet.send(l, frame(Frame::Hello, {"board-old", "0", "1", "1", "104", "1", "999999"}));
    run({&a}, now, 500);
    said = 0;
    for (auto& e : a.events) said += e.find("turned away") != std::string::npos;
    CHECK(said == 2);  // a minute later it is said again
}

TEST_CASE("a cluster restart asked for in the middle of a tick waits until the tick is done") {
    TwoBoards t;
    bool      intact = false;
    t.a.c->onRequest = [&](int, const std::string&) {
        t.a.c->restartSoon("another-cluster-key");  // @CLUSTERKEY on the main
        t.a.c->start(t.now, 1);                     // and a plain start() is held the same way
        intact = t.a.c->role() == Role::Main && t.a.c->nodes().size() == 2;
        return std::string("1ok");
    };
    t.b.c->clockMs = [&t] { return t.now; };
    t.b.c->sleepMs = [&t](uint32_t) {
        t.now += 5;
        t.a.c->tick(t.now);
    };
    std::string reply;
    REQUIRE(t.b.c->request("C\x1f" "@CLUSTERKEY another-cluster-key", reply));
    CHECK(intact);  // nothing changed under the tick's loop
    run({&t.a}, t.now, 100);
    CHECK(t.a.c->role() == Role::Discovering);  // then it started over
    run({&t.a, &t.b}, t.now, 15000);
    CHECK(t.a.c->nodes().size() == 1);  // with the new key: b (old key) can't join it
}

TEST_CASE("a frame changed on the wire drops the link") {
    TwoBoards t;
    Wire*     w = nullptr;
    for (auto& link : t.b.net.wires) w = link.second.first.get();
    REQUIRE(w);
    t.b.c->chat("Spindle Kid", "hello");  // b's frame is now on the wire to the main
    REQUIRE(w->toMain.size() > 5 + kTagBytes);
    w->toMain[6] ^= 0x01;  // one bit flipped in the body
    run({&t.a, &t.b}, t.now, 200);
    CHECK(t.a.heard.empty());
    CHECK(t.a.saw("bad cluster tag"));
}

TEST_CASE("a frame replayed on the wire drops the link") {
    TwoBoards t;
    Wire*     w = nullptr;
    for (auto& link : t.b.net.wires) w = link.second.first.get();
    REQUIRE(w);
    t.b.c->chat("Spindle Kid", "hello");
    std::string copy = w->toMain;
    run({&t.a, &t.b}, t.now, 100);
    REQUIRE(t.a.heard.size() == 1);
    w->toMain += copy;  // the same, signed frame again
    run({&t.a, &t.b}, t.now, 100);
    CHECK(t.a.heard.size() == 1);
    CHECK(t.a.saw("bad cluster tag"));
}

TEST_CASE("with no cluster key a board runs alone: it hears nobody and lets nobody in") {
    FakeLan  lan;
    Board    a(lan, 101, "board-a", ""), b(lan, 102, "board-b");
    uint32_t now = 0;
    a.c->start(now);
    CHECK(a.c->alone());
    CHECK(a.c->role() == Role::Main);
    CHECK(a.c->nodeNumber() == 1);
    CHECK(a.saw("no cluster key"));
    b.c->start(now);
    run({&a, &b}, now, 8000);
    CHECK(a.c->nodes().size() == 1);
    CHECK(b.c->role() == Role::Main);  // b found no main it could join
    CHECK(b.net.datagrams.empty());    // and a never announced itself
}
