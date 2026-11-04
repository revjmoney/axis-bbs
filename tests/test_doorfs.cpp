// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <thread>

#include "doctest.h"
#include "cluster/doorfs.h"
#include "door/session.h"
#include "platform/pc/stdhostfs.h"
#include "platform/pc/tcpstream.h"

using namespace axis::cluster;
using namespace axis::door;
namespace fs = std::filesystem;

namespace {
// the main's SD card: door folders under one root; one server per follower
struct Main {
    fs::path      root = fs::temp_directory_path() / "axis_doorfs";
    LockTable     locks;  // the main's own door locks, shared with every follower's door
    DoorFsServer  server{[this](const std::string& id) { return std::unique_ptr<HostFs>(new axis::pc::StdHostFs(root / id)); },
                        locks};
    int           asks = 0;
    DoorFsServer::Ask ask() {
        return [this](const std::string& req, std::string& reply) {
            asks++;
            reply = server.handle(req);
            return true;
        };
    }
    Main() {
        fs::remove_all(root);
        fs::create_directories(root / "lord");
        std::ofstream(root / "lord" / "README.TXT") << "the realm";
    }
    ~Main() { fs::remove_all(root); }
};
}  // namespace

TEST_CASE("a follower's door reads, writes, seeks and lists the main's door folder") {
    Main         m;
    RemoteHostFs rfs(m.ask(), "lord");
    int          h = rfs.open("README.TXT", 0, false, false);
    REQUIRE(h >= 0);
    char buf[16] = {};
    CHECK(rfs.read(h, buf, sizeof buf) == 9);
    CHECK(std::string(buf) == "the realm");
    rfs.close(h);

    h = rfs.open("C:\\SAVE.DAT", 2, true, true);
    REQUIRE(h >= 0);
    std::string rec(5000, 'r');
    rec[1] = '\x1f';
    CHECK(rfs.write(h, rec.data(), (int)rec.size()) == 5000);
    CHECK(rfs.seek(h, 1, 0) == 1);
    char one = 0;
    CHECK(rfs.read(h, &one, 1) == 1);
    CHECK(one == '\x1f');
    uint16_t d = 0, t = 0;
    CHECK(rfs.fileTime(h, d, t));
    rfs.close(h);
    CHECK(fs::file_size(m.root / "lord" / "SAVE.DAT") == 5000);

    FileInfo fi;
    REQUIRE(rfs.stat("save.dat", fi));
    CHECK(fi.size == 5000);
    FindState st;
    REQUIRE(rfs.find("*.*", 0x10, st));
    CHECK(st.matches.size() == 2);
    CHECK(rfs.mkdir("NODE3") == 0);
    CHECK(rfs.isDir("NODE3"));
    rfs.setCwd("NODE3");
    h = rfs.open("DOOR.SYS", 1, true, true);
    REQUIRE(h >= 0);
    rfs.close(h);
    CHECK(fs::exists(m.root / "lord" / "NODE3" / "DOOR.SYS"));
    CHECK(rfs.rename("DOOR.SYS", "OLD.SYS") == 0);
    CHECK(rfs.remove("OLD.SYS") == 0);
    rfs.setCwd("");
    CHECK(rfs.rmdir("NODE3") == 0);
    CHECK(rfs.open("NOPE.DAT", 0, false, false) < 0);
}

TEST_CASE("a door on a follower and a door on the main lock the same records") {
    Main            m;
    RemoteHostFs    rfs(m.ask(), "lord");
    RemoteLockTable remoteLocks(m.ask());
    axis::pc::StdHostFs local(m.root / "lord");
    int hl = local.open("README.TXT", 2, false, false), hr = rfs.open("readme.txt", 2, false, false);
    REQUIRE(hl >= 0);
    REQUIRE(hr >= 0);
    CHECK(local.fileKey(hl) == rfs.fileKey(hr));  // one file, one key, on both boards
    CHECK(m.locks.lock(local.fileKey(hl), 0, 4, 1));           // node 1 on the main
    CHECK_FALSE(remoteLocks.lock(rfs.fileKey(hr), 2, 4, 3));  // node 3 on a follower: taken
    CHECK(remoteLocks.lock(rfs.fileKey(hr), 4, 4, 3));        // the next record is free
    remoteLocks.releaseOwner(3);
    CHECK(m.locks.lock(local.fileKey(hl), 4, 4, 1));
    local.close(hl);
    rfs.close(hr);
}

TEST_CASE("LORD's node file for another node drops its DOOR.SYS in that node's folder") {
    std::string node1 = "BBSNAME Axis\r\nBBSTYPE DOORSYS\r\nFOSSIL\r\nBBSDROP C:\\\r\nNOOPEN\r\n";
    std::string n3 = lordNodeFile(node1, 3);
    CHECK(n3.find("BBSDROP C:\\NODE3\\\r\n") != std::string::npos);
    CHECK(n3.find("node 3") != std::string::npos);
    CHECK(n3.find("FOSSIL") != std::string::npos);
    CHECK(doorArgs("%N /DREW", 3) == "3 /DREW");
    CHECK(doorArgs("1 /DREW", 3) == "1 /DREW");
}

TEST_CASE("the real LORD plays as node 3 on a follower, its files over the link") {
    const char* src = std::getenv("AXIS_LORD_DIR");
    if (!src) return;
    Main m;
    fs::remove_all(m.root / "lord");
    fs::copy(src, m.root / "lord", fs::copy_options::recursive);
    std::string node1;
    {
        std::ifstream in(m.root / "lord" / "NODE1.DAT", std::ios::binary);
        node1.assign((std::istreambuf_iterator<char>(in)), {});
    }
    if (node1.empty()) node1 = "BBSTYPE DOORSYS\r\nFOSSIL\r\nBBSDROP C:\\\r\n";
    std::ofstream(m.root / "lord" / "NODE3.DAT", std::ios::binary) << lordNodeFile(node1, 3);

    axis::pc::TcpListener listener(0);
    uint16_t              port = listener.port();
    std::atomic<int>      result{-1};
    std::atomic<bool>     dropped{false};
    std::thread           door([&] {
        auto line = listener.accept(10000);
        REQUIRE(line);
        RemoteHostFs    rfs(m.ask(), "lord");
        RemoteLockTable locks(m.ask());
        DoorConfig      cfg;
        cfg.exe = "LORD.EXE";
        cfg.args = doorArgs("%N /DREW", 3);
        cfg.localMode = false;
        cfg.dropFile = DropFile::DoorSys;
        cfg.dropDir = "NODE3";
        cfg.caller.alias = "Lathe Lord";
        cfg.caller.node = 3;
        DoorSession s(cfg, rfs, locks, *line, 3);
        result = (int)s.run();
    });
    axis::pc::TcpClient c("127.0.0.1", port);
    std::string         seen;
    bool                ok = false;
    for (int t = 0; t < 60000 && !ok; t += 20) {
        seen += c.receive();
        ok = std::regex_search(seen, std::regex("LOADING|REALM|egend|Seth|Dragon|warrior", std::regex::icase));
        if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CHECK(ok);
    dropped = fs::exists(m.root / "lord" / "NODE3" / "DOOR.SYS");
    CHECK(dropped);
    CHECK_FALSE(fs::exists(m.root / "lord" / "DOOR.SYS"));  // node 1's spot stays free
    CHECK(m.asks > 100);                                     // it really went over the link
    c.close();
    door.join();
    CHECK(result != (int)DoorResult::LoadError);
}

TEST_CASE("when a follower goes away mid-door, the records it locked come free") {
    Main            m;
    RemoteLockTable remoteLocks(m.ask());
    CHECK(remoteLocks.lock(77, 0, 10, 3));
    m.server.drop();
    CHECK(m.locks.lock(77, 0, 10, 1));
}

TEST_CASE("reading a file a byte at a time costs a few round trips, not one per byte") {
    Main m;
    std::ofstream(m.root / "lord" / "TEXT.DAT", std::ios::binary) << std::string(3000, 'q');
    RemoteHostFs rfs(m.ask(), "lord");
    int          h = rfs.open("TEXT.DAT", 0, false, false);
    REQUIRE(h >= 0);
    int before = m.asks, got = 0;
    char c;
    while (rfs.read(h, &c, 1) == 1) got++;
    CHECK(got == 3000);
    CHECK(m.asks - before <= 4);
    CHECK(rfs.seek(h, 10, 0) == 10);
    CHECK(rfs.seek(h, 0, 2) == 3000);
    CHECK(rfs.seek(h, -1, 1) == 2999);
    rfs.close(h);
}

TEST_CASE("after a write or a record lock the next read sees fresh data") {
    Main            m;
    std::ofstream(m.root / "lord" / "REC.DAT", std::ios::binary) << "aaaa";
    RemoteHostFs    rfs(m.ask(), "lord");
    RemoteLockTable locks(m.ask(), &rfs);
    int             h = rfs.open("REC.DAT", 2, false, false);
    char            b[4];
    REQUIRE(rfs.read(h, b, 4) == 4);
    {  // another node writes the record on the main
        std::ofstream out(m.root / "lord" / "REC.DAT", std::ios::binary);
        out << "bbbb";
    }
    locks.lock(rfs.fileKey(h), 0, 4, 3);  // a door locks before it reads a shared record
    rfs.seek(h, 0, 0);
    REQUIRE(rfs.read(h, b, 4) == 4);
    CHECK(std::string(b, 4) == "bbbb");
    rfs.seek(h, 0, 0);
    CHECK(rfs.write(h, "cc", 2) == 2);
    rfs.seek(h, 0, 0);
    REQUIRE(rfs.read(h, b, 4) == 4);
    CHECK(std::string(b, 4) == "ccbb");
    rfs.close(h);
}
