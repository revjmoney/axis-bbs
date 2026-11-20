// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card tree service and its change feed (cluster/cardtree.h), over a temp folder standing in for the SD card.
#include <sys/stat.h>
#include <utime.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "doctest.h"
#include "cluster/cardtree.h"

using namespace axis::cluster;
namespace fs = std::filesystem;

namespace {
const std::string kSepS(1, '\x1f');

struct Card {
    fs::path root;
    explicit Card(const char* name) : root(fs::temp_directory_path() / (std::string("axis_cardtree_") + name)) {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Card() { fs::remove_all(root); }
    void put(const std::string& rel, const std::string& bytes, time_t mtime = 1700000000) {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << bytes;
        struct utimbuf ut;
        ut.actime = ut.modtime = mtime;
        ::utime((root / rel).string().c_str(), &ut);
    }
    bool has(const std::string& rel) const { return fs::exists(root / rel); }
};

std::string rd(CardTreeServer& s, const std::string& op, const std::string& args = "") {
    return s.handleRead("I" + kSepS + op + (args.empty() ? "" : kSepS + args));
}
std::string wr(CardTreeServer& s, const std::string& op, const std::string& args = "") {
    return s.handleWrite("I" + kSepS + op + (args.empty() ? "" : kSepS + args));
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    size_t                   pos = 0;
    while (pos < s.size()) {
        size_t nl = s.find('\n', pos);
        out.push_back(s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos));
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return out;
}
}  // namespace

TEST_CASE("a directory listing comes in pages under 7 KB that cover every entry once, with size and time") {
    Card card("list");
    for (int i = 0; i < 400; i++) {
        char name[40];
        snprintf(name, sizeof name, "file-with-a-longer-name-%04d.dat", 399 - i);  // made in reverse order
        card.put(std::string("up/") + name, std::string((size_t)i % 7, 'x'), 1700000000 + i);
    }
    fs::create_directories(card.root / "up" / "zdir");
    CardTreeServer s(card.root.string());
    std::vector<std::string> names;
    uint64_t                 skip = 0;
    int                      pages = 0;
    for (;;) {
        std::string r = rd(s, "D", "up" + kSepS + std::to_string(skip));
        REQUIRE(r.size() > 1);
        REQUIRE(r[0] == '1');
        CHECK(r.size() <= kTreeListMax);
        pages++;
        bool more = false;
        for (const std::string& l : lines(r.substr(1))) {
            if (l.empty()) continue;
            if (l[0] == '+') {
                skip = std::stoull(l.substr(2));
                more = true;
            } else {
                names.push_back(l);
            }
        }
        if (!more) break;
        REQUIRE(pages < 20);
    }
    CHECK(pages >= 2);
    REQUIRE(names.size() == 401);  // 400 files and the folder, each once (the folder's own order, not sorted)
    std::set<std::string> once(names.begin(), names.end());
    CHECK(once.size() == 401);
    CHECK(once.count(std::string("f") + kSepS + "file-with-a-longer-name-0000.dat" + kSepS + "0" + kSepS + "1700000399"));  // i = 399: size 399 % 7 = 0
    CHECK(once.count(std::string("f") + kSepS + "file-with-a-longer-name-0001.dat" + kSepS + "6" + kSepS + "1700000398"));  // i = 398: 398 % 7 = 6
    CHECK(once.count(std::string("d") + kSepS + "zdir"));
}

TEST_CASE("paths that leave the card are refused") {
    Card card("paths");
    card.put("ok.txt", "hello");
    CardTreeServer s(card.root.string());
    CHECK(rd(s, "S", "ok.txt") == "1" + std::string("5") + kSepS + "1700000000");
    const std::vector<std::string> bad = {"../x", "/etc/passwd", "a/../../b", "a\\b", "", std::string("a\x01" "b")};
    for (const std::string& p : bad) {
        INFO("path: " << doctest::toString(p.c_str()));
        CHECK(rd(s, "S", p) == "0");
        CHECK(rd(s, "R", p + kSepS + "0" + kSepS + "10") == "0");
        CHECK(wr(s, "O", p) == "0");
        CHECK(wr(s, "X", p) == "0");
        CHECK_FALSE(cardPathOk(p));
        CHECK_FALSE(cardPathMirrored(p));
    }
    CHECK(rd(s, "D", "../x" + kSepS + "0") == "0");
    CHECK(rd(s, "D", "a/../.." + kSepS + "0") == "0");
    CHECK(cardPathOk("a/b.txt"));
    CHECK_FALSE(cardPathOk("a//b"));
    CHECK_FALSE(cardPathOk("./a"));
    CHECK_FALSE(cardPathOk("c:x"));
    CHECK_FALSE(cardPathOk(std::string(201, 'a')));
    CHECK_FALSE(cardPathOk(std::string(65, 'a')));
}

TEST_CASE("logs, temp files, System Volume Information and the view cache are never listed or served") {
    Card card("hidden");
    card.put("ok.txt", "fine");
    card.put("files/viewpacks/1A2B3C4D-0042.ZIP", "x");
    card.put("files/ViewCache/piece.ans", "x");
    card.put("files/viewpacks2/kept.zip", "x");  // (only those two folders)
    card.put("a.log", "log");
    card.put("B.LOG", "log");
    card.put("half.ax_", "temp");
    card.put("Half.AX_", "temp");
    card.put("System Volume Information/WPSettings.dat", "x");
    card.put("sub/ok2.txt", "fine");
    card.put("sub/system volume information/y.txt", "x");
    CardTreeServer s(card.root.string());
    std::string root = rd(s, "D", kSepS + "0");  // (in the folder's own order)
    std::set<std::string> top;
    for (const std::string& l : lines(root.substr(1)))
        if (!l.empty()) top.insert(l);
    CHECK(root[0] == '1');
    CHECK(top == std::set<std::string>{"f" + kSepS + "ok.txt" + kSepS + "4" + kSepS + "1700000000", "d" + kSepS + "sub",
                                       "d" + kSepS + "files"});
    CHECK(rd(s, "D", "sub" + kSepS + "0") == "1f" + kSepS + "ok2.txt" + kSepS + "4" + kSepS + "1700000000\n");
    CHECK(rd(s, "D", "files" + kSepS + "0") == "1d" + kSepS + "viewpacks2\n");
    CHECK(cardPathMirrored("files/viewpacks2/kept.zip"));
    CHECK_FALSE(cardPathMirrored("files/viewpacks"));
    for (const char* p : {"a.log", "B.LOG", "half.ax_", "Half.AX_", "System Volume Information/WPSettings.dat", "sub/system volume information/y.txt",
                          "files/viewpacks/1A2B3C4D-0042.ZIP", "files/ViewCache/piece.ans", "FILES/VIEWPACKS/x"}) {
        INFO("path: " << p);
        CHECK(rd(s, "S", p) == "0");
        CHECK(rd(s, "R", std::string(p) + kSepS + "0" + kSepS + "10") == "0");
        CHECK(wr(s, "O", p) == "0");
    }
    CHECK(rd(s, "D", "System Volume Information" + kSepS + "0") == "0");
}

TEST_CASE("a read piece is exactly the bytes asked for and never more than 4096") {
    Card        card("read");
    std::string data(10000, '\0');
    for (size_t i = 0; i < data.size(); i++) data[i] = (char)(i * 7 + i / 13);
    card.put("big.bin", data);
    CardTreeServer s(card.root.string());
    auto           piece = [&](size_t off, size_t len) { return rd(s, "R", "big.bin" + kSepS + std::to_string(off) + kSepS + std::to_string(len)); };
    CHECK(piece(0, 100) == "1" + data.substr(0, 100));
    CHECK(piece(100, 4096) == "1" + data.substr(100, 4096));
    CHECK(piece(0, 100000) == "1" + data.substr(0, 4096));  // asked for more than a piece: a piece
    CHECK(piece(9990, 4096) == "1" + data.substr(9990));    // the end of the file: only what is there
    CHECK(piece(10000, 10) == "1");                         // at the end: nothing, but not an error
    CHECK(piece(5000, 4096) == "1" + data.substr(5000, 4096));
    CHECK(rd(s, "R", "missing.bin" + kSepS + "0" + kSepS + "10") == "0");
    card.put("dir/x", "y");
    CHECK(rd(s, "R", "dir" + kSepS + "0" + kSepS + "10") == "0");  // a folder is not a file
    CHECK(rd(s, "R", "big.bin" + kSepS + "x" + kSepS + "10") == "0");
}

TEST_CASE("a write shows the file only once finished, at the size given") {
    Card           card("write");
    CardTreeServer s(card.root.string());
    CHECK(wr(s, "O", "d/e/x") == "1");  // the folders are made
    CHECK(wr(s, "P", "0" + kSepS + "hel") == "1");
    CHECK(wr(s, "P", "3" + kSepS + "lo") == "1");
    CHECK(card.has("d/e/x.ax_"));
    CHECK_FALSE(card.has("d/e/x"));  // not there until it is finished
    CHECK(wr(s, "F", "5" + kSepS + "1600000000") == "1" + std::string("5") + kSepS + "1600000000");
    CHECK(card.has("d/e/x"));
    CHECK_FALSE(card.has("d/e/x.ax_"));
    std::ifstream in(card.root / "d/e/x", std::ios::binary);
    std::string   got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();  // Windows will not replace a file another handle still has open
    CHECK(got == "hello");
    CHECK(rd(s, "S", "d/e/x") == "1" + std::string("5") + kSepS + "1600000000");

    // a wrong size leaves neither the file nor its temp
    CHECK(wr(s, "O", "y") == "1");
    CHECK(wr(s, "P", "0" + kSepS + "abc") == "1");
    CHECK(wr(s, "F", "5" + kSepS + "1600000000") == "0");
    CHECK_FALSE(card.has("y"));
    CHECK_FALSE(card.has("y.ax_"));

    // a piece at the wrong offset is refused, and the write of another file replaces an older file at the name
    CHECK(wr(s, "O", "d/e/x") == "1");
    CHECK(wr(s, "P", "9" + kSepS + "zz") == "0");
    CHECK(wr(s, "O", "d/e/x") == "1");
    CHECK(wr(s, "P", "0" + kSepS + "new") == "1");
    CHECK(wr(s, "F", "3" + kSepS + "1600000100") == "1" + std::string("3") + kSepS + "1600000100");
    CHECK(rd(s, "S", "d/e/x") == "1" + std::string("3") + kSepS + "1600000100");

    // a link that goes mid-copy drops the temp
    CHECK(wr(s, "O", "z") == "1");
    CHECK(wr(s, "P", "0" + kSepS + "half") == "1");
    CHECK(card.has("z.ax_"));
    s.drop();
    CHECK_FALSE(card.has("z.ax_"));
    CHECK_FALSE(card.has("z"));

    // remove, and the end of a hand-back
    CHECK(wr(s, "X", "d/e/x") == "1");
    CHECK_FALSE(card.has("d/e/x"));
    CHECK(wr(s, "X", "d/e/x") == "1");  // missing counts as done
    CHECK_FALSE(s.finished());
    CHECK(wr(s, "E") == "1");
    CHECK(s.finished());
}

TEST_CASE("the change feed lists each changed path once after a sequence number") {
    CardFeed f;
    f.setOn(true, 7);
    f.note("/sd/a");
    f.note("/sd/b");
    f.note("/sd/a");  // changed again: it moves to the newest number, listed once
    CHECK(f.answer(7, 0) == "7" + kSepS + "3" + kSepS + "0\nb\na\n");
    CHECK(f.answer(7, 2) == "7" + kSepS + "3" + kSepS + "0\na\n");
    CHECK(f.answer(7, 3) == "7" + kSepS + "3" + kSepS + "0\n");
    f.note("/sd/dir/c.txt");
    CHECK(f.answer(7, 3) == "7" + kSepS + "4" + kSepS + "0\ndir/c.txt\n");
    // through the tree service, which answers from the program's one feed
    Card           card("feed");
    CardTreeServer s(card.root.string());
    cardFeed().setOn(true, 11);
    cardFeed().note("/sd/q.txt");
    CHECK(rd(s, "C", "11" + kSepS + "0") == "1" + std::string("11") + kSepS + "1" + kSepS + "0\nq.txt\n");
    cardFeed().setOn(false, 11);
}

TEST_CASE("the change feed says lost after 33 paths, for another generation, and for a path over 96 bytes") {
    CardFeed f;
    f.setOn(true, 7);
    for (int i = 1; i <= 33; i++) f.note(("/sd/p" + std::to_string(i)).c_str());  // the ring holds 32: p1 fell out
    std::string r = f.answer(7, 0);
    CHECK(r == "7" + kSepS + "33" + kSepS + "1\n");  // lost, and no paths
    r = f.answer(7, 1);  // an asker that has seen p1 misses nothing
    CHECK(r.substr(0, 7) == "7" + kSepS + "33" + kSepS + "0\n");
    CHECK(lines(r).size() == 33);  // the header and p2 ... p33
    CHECK(lines(r)[1] == "p2");
    CHECK(f.answer(8, 0).substr(0, 2) == "7" + kSepS);  // another generation
    CHECK(f.answer(8, 0).find(kSepS + "1\n") != std::string::npos);
    CHECK(f.answer(7, 99).find(kSepS + "1\n") != std::string::npos);  // ahead of the feed: lost too

    CardFeed g;  // a path too long for the ring (96 bytes or more, as kPathMax is the buffer with its end)
    g.setOn(true, 1);
    g.note("/sd/short");
    std::string ok95 = std::string(60, 'a') + "/" + std::string(34, 'b');
    std::string long96 = std::string(60, 'a') + "/" + std::string(35, 'b');
    std::string long101 = std::string(60, 'a') + "/" + std::string(40, 'b');
    REQUIRE(ok95.size() == 95);
    g.note(("/sd/" + ok95).c_str());
    CHECK(g.answer(1, 0) == "1" + kSepS + "2" + kSepS + "0\nshort\n" + ok95 + "\n");
    g.note(("/sd/" + long96).c_str());  // kept nowhere: it is a change the ring cannot hold
    CHECK(g.answer(1, 3) == "1" + kSepS + "3" + kSepS + "0\n");  // an asker that is up to date is fine
    CHECK(g.answer(1, 2) == "1" + kSepS + "3" + kSepS + "1\n");  // one that was behind it must compare the tree
    g.note(("/sd/" + long101).c_str());
    CHECK(g.answer(1, 4) == "1" + kSepS + "4" + kSepS + "0\n");
    CHECK(g.answer(1, 3) == "1" + kSepS + "4" + kSepS + "1\n");
}

TEST_CASE("the change feed notes nothing while off, and nothing outside /sd/") {
    CardFeed f;
    CHECK_FALSE(f.on());
    f.note("/sd/a");  // off: nothing is kept
    f.setOn(true, 1);
    CHECK(f.answer(1, 0) == "1" + kSepS + "0" + kSepS + "0\n");
    f.note("/flash/a");
    f.note("sd/a");
    f.note("/sdx/a");
    f.note("/sd/../x");
    f.note("/sd/a.log");
    f.note("/sd/System Volume Information/x");
    f.note("/sd/t.ax_");
    f.note(nullptr);
    CHECK(f.answer(1, 0) == "1" + kSepS + "0" + kSepS + "0\n");
    f.note("/sd/good");
    CHECK(f.answer(1, 0) == "1" + kSepS + "1" + kSepS + "0\ngood\n");
    f.setOn(false, 1);
    f.note("/sd/later");
    CHECK(f.answer(1, 0) == "1" + kSepS + "1" + kSepS + "0\ngood\n");  // off again: nothing more is noted
}
