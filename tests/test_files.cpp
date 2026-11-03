// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/filelib.h"
#include "bbs/node.h"

using namespace axis::bbs;

namespace {
// a pretend file store: area id -> files (name, contents); FILES.BBS is generated from desc
struct FakeLibrary : FileLibrary {
    std::map<std::string, std::vector<FileEntry>> areas;
    std::map<std::string, std::string>            text;  // "area/name" -> contents
    std::vector<FileEntry> list(const std::string& area) override { return areas[area]; }
    bool read(const std::string& area, const std::string& name, std::string& out, size_t max) override {
        auto it = text.find(area + "/" + name);
        if (it == text.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
};

SysInfo info() { return SysInfo{}; }

BbsConfig twoAreas() {
    BbsConfig c;
    c.areas = {{"axis", "Axis Micro BBS Firmware"}, {"text", "Text Files"}};
    return c;
}

FakeLibrary library() {
    FakeLibrary l;
    l.areas["axis"] = {{"AXISFLSH.ZIP", 797000, "Ready to flash"}, {"README.TXT", 1200, "What it is"}};
    for (int i = 0; i < 20; i++) l.areas["text"].push_back({"LIST" + std::to_string(i) + ".TXT", 100u + i, "BBS list " + std::to_string(i)});
    l.text["axis/README.TXT"] = "Axis Micro BBS Firmware\nby Rev. J. Money\n";
    std::string longText;
    for (int i = 1; i <= 50; i++) longText += "line " + std::to_string(i) + "\n";
    l.text["text/LIST0.TXT"] = longText;
    return l;
}

void type(Node& n, const std::string& s) { n.input((const uint8_t*)s.data(), s.size(), 1000); }

Node inFiles(FakeLibrary& lib) {
    Node n(1, info, twoAreas(), &lib);
    n.begin(0);
    type(n, "Rev\r");
    n.takeOutput();
    return n;
}
}  // namespace

TEST_CASE("FILES.BBS order and descriptions merge with the folder") {
    std::string bbs = "README.TXT   What it is\r\nGONE.ZIP     Not in the folder\r\nAXIS.ZIP     The firmware\r\n";
    auto list = mergeFilesBbs(bbs, {{"axis.zip", 900}, {"FILES.BBS", 80}, {"readme.txt", 12}, {"EXTRA.BIN", 5}});
    REQUIRE(list.size() == 3);
    CHECK(list[0].name == "readme.txt");  // the folder's spelling
    CHECK(list[0].desc == "What it is");
    CHECK(list[0].size == 12);
    CHECK(list[1].name == "axis.zip");
    CHECK(list[2].name == "EXTRA.BIN");  // not described: listed last, no description
    CHECK(list[2].desc.empty());
}

TEST_CASE("file sizes read like a 90s file list") {
    CHECK(fileSizeText(512) == "512");
    CHECK(fileSizeText(48 * 1024) == "48K");
    CHECK(fileSizeText(21972477) == "21.0M");
}

TEST_CASE("menu shows Files") {
    FakeLibrary lib = library();
    Node        n(1, info, twoAreas(), &lib);
    n.begin(0);
    type(n, "Rev\r");
    CHECK(n.takeOutput().find("[F]") != std::string::npos);
}

TEST_CASE("F lists the file areas with their file counts") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f");
    std::string out = n.takeOutput();
    CHECK(out.find("Axis Micro BBS Firmware") != std::string::npos);
    CHECK(out.find("Text Files") != std::string::npos);
    CHECK(out.find("20") != std::string::npos);
    CHECK(n.state() == Node::State::Files);
}

TEST_CASE("choosing an area lists its files with size and description") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    std::string out = n.takeOutput();
    CHECK(out.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(out.find("778K") != std::string::npos);
    CHECK(out.find("Ready to flash") != std::string::npos);
}

TEST_CASE("long areas page with N and P") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    std::string first = n.takeOutput();
    CHECK(first.find("LIST0.TXT") != std::string::npos);
    CHECK(first.find("LIST19.TXT") == std::string::npos);
    type(n, "n");
    std::string second = n.takeOutput();
    CHECK(second.find("LIST19.TXT") != std::string::npos);
    type(n, "p");
    CHECK(n.takeOutput().find("LIST0.TXT") != std::string::npos);
}

TEST_CASE("V shows a text file") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "vreadme.txt\r");
    std::string out = n.takeOutput();
    CHECK(out.find("by Rev. J. Money\r\n") != std::string::npos);  // bare LF becomes CR LF
}

TEST_CASE("a long text file pages, and Q stops it") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    n.takeOutput();
    type(n, "vLIST0.TXT\r");
    std::string out = n.takeOutput();
    CHECK(out.find("line 1\r\n") != std::string::npos);
    CHECK(out.find("line 50") == std::string::npos);
    CHECK(out.find("more") != std::string::npos);
    type(n, "q");
    CHECK(n.takeOutput().find("LIST0.TXT") != std::string::npos);  // back at the file list
}

TEST_CASE("viewing a binary file explains downloads") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "vAXISFLSH.ZIP\r");
    CHECK(n.takeOutput().find("download") != std::string::npos);
}

TEST_CASE("Q leaves the list and Enter leaves the areas") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    type(n, "q");
    CHECK(n.state() == Node::State::Files);
    type(n, "\r");
    CHECK(n.state() == Node::State::Menu);
}

TEST_CASE("files without a library say the areas are offline") {
    Node n(1, info, twoAreas());
    n.begin(0);
    type(n, "Rev\r");
    n.takeOutput();
    type(n, "f");
    CHECK(n.takeOutput().find("offline") != std::string::npos);
    CHECK(n.state() == Node::State::Menu);
}
