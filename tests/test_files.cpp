// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <regex>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/filebrowser.h"
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

std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;]*m"), ""); }

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
    CHECK(plain(n.takeOutput()).find("[F]") != std::string::npos);
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
    CHECK(plain(n.takeOutput()).find("ownload") != std::string::npos);
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

TEST_CASE("D asks for a file and a protocol, then shows the download window and requests it") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "dAXISFLSH.ZIP\r");
    CHECK(plain(n.takeOutput()).find("[Z]MODEM") != std::string::npos);  // the protocol menu
    type(n, "z");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("D O W N L O A D") != std::string::npos);
    CHECK(n.state() == Node::State::Transfer);
    CHECK_FALSE(n.transfer().upload);
    CHECK(n.transfer().name == "AXISFLSH.ZIP");
    CHECK(n.transfer().area == "axis");
    CHECK(n.transfer().proto == 'Z');
}

TEST_CASE("U asks for a description and a protocol, then requests the upload") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    n.takeOutput();
    type(n, "uA list of boards\rz");
    CHECK(n.state() == Node::State::Transfer);
    CHECK(n.transfer().upload);
    CHECK(n.transfer().desc == "A list of boards");
    CHECK(n.transfer().area == "text");
}

TEST_CASE("an XMODEM upload also asks for the file name") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f2\r");
    type(n, "unotes\rknotes.txt\r");
    CHECK(n.state() == Node::State::Transfer);
    CHECK(n.transfer().proto == 'K');
    CHECK(n.transfer().name == "notes.txt");
}

TEST_CASE("after a transfer the caller sees how it went and is back at the list") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\rdREADME.TXT\rz");
    n.takeOutput();
    n.transferDone(true, 1200, 2000, "", 0);
    std::string out = plain(n.takeOutput());
    CHECK(out.find("C O M P L E T E") != std::string::npos);
    CHECK(n.state() == Node::State::Files);
    CHECK(out.find("ownload") != std::string::npos);  // the list prompt is back
}

TEST_CASE("a download of a missing file is refused before any protocol starts") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    type(n, "f1\rdNOPE.ZIP\r");
    CHECK(n.state() == Node::State::Files);
    CHECK(n.takeOutput().find("No file by that name") != std::string::npos);
}

namespace {
FakeLibrary withZip() {
    FakeLibrary   l = library();
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    std::string   zip((std::istreambuf_iterator<char>(in)), {});
    l.areas["axis"].push_back({"PACK.ZIP", (uint32_t)zip.size(), "Test art pack"});
    l.text["axis/PACK.ZIP"] = zip;
    return l;
}
}  // namespace

TEST_CASE("[I] shows a ZIP's full archive listing while browsing") {
    FakeLibrary lib = withZip();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    std::string list = plain(n.takeOutput());
    CHECK(list.find("[I]") != std::string::npos);
    type(n, "ipack.zip\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("PACK.ZIP") != std::string::npos);
    CHECK(out.find("Test art pack") != std::string::npos);
    CHECK(out.find("REV-AXIS.ANS") != std::string::npos);
    CHECK(out.find("FILE_ID.DIZ") != std::string::npos);
    CHECK(out.find("PROGRAM.EXE") != std::string::npos);
    CHECK(out.find("3 files") != std::string::npos);
    CHECK(out.find("3703") != std::string::npos);  // REV-AXIS.ANS unpacked size
}

TEST_CASE("[I] lists a TAR's files too, and says the board only lists that kind") {
    FakeLibrary lib = library();
    std::string h(512, '\0'), body = "greetz to the underground\n";
    std::memcpy(&h[0], "GREETZ.TXT", 10);
    std::memcpy(&h[100], "0000644", 7);
    std::snprintf(&h[124], 12, "%011o", (unsigned)body.size());
    h[156] = '0';
    std::memset(&h[148], ' ', 8);
    unsigned sum = 0;
    for (unsigned char c : h) sum += c;
    std::snprintf(&h[148], 8, "%06o", sum);
    std::string tar = h + body + std::string(512 - body.size(), '\0') + std::string(1024, '\0');
    lib.areas["axis"].push_back({"GREETZ.TAR", (uint32_t)tar.size(), "Greetz"});
    lib.text["axis/GREETZ.TAR"] = tar;
    Node n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "igreetz.tar\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("TAR, 1 files") != std::string::npos);
    CHECK(out.find("GREETZ.TXT") != std::string::npos);
    CHECK(out.find("1 files, 26 bytes") != std::string::npos);
}

TEST_CASE("[I] on a file that isn't an archive shows what is known about it") {
    FakeLibrary lib = withZip();
    Node        n = inFiles(lib);
    type(n, "f1\r");
    n.takeOutput();
    type(n, "iREADME.TXT\r");
    std::string out = plain(n.takeOutput());
    CHECK(out.find("README.TXT") != std::string::npos);
    CHECK(out.find("What it is") != std::string::npos);
    type(n, "inope.zip\r");
    CHECK(plain(n.takeOutput()).find("No file by that name") != std::string::npos);
}

TEST_CASE("a node says where its caller is, for the sysop's activity screen") {
    FakeLibrary lib = library();
    Node        n = inFiles(lib);
    CHECK(n.activityText() == "main menu");
    type(n, "f");
    CHECK(n.activityText() == "files: area list");
    type(n, "2\r");
    CHECK(n.activityText() == "files: Text Files");
    type(n, "d");
    type(n, "LIST1.TXT\r");
    type(n, "z");
    CHECK(n.activityText() == "downloading LIST1.TXT");
}

TEST_CASE("[N] at the area list shows every area's files newer than the caller's last call") {
    FakeLibrary lib = library();
    lib.areas["axis"][0].date = 2000;  // AXISFLSH.ZIP: new since the last call
    lib.areas["axis"][1].date = 500;   // README.TXT: old
    lib.areas["text"][3].date = 3000;  // LIST3.TXT: new
    std::string out;
    BbsConfig   c = twoAreas();
    FileBrowser b(c.areas, lib, out);
    b.setNewSince(1000);
    b.begin();
    out.clear();
    b.key('n');
    std::string p = plain(out);
    CHECK(p.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(p.find("LIST3.TXT") != std::string::npos);
    CHECK(p.find("README.TXT") == std::string::npos);
    CHECK(p.find("2 new files") != std::string::npos);
}

TEST_CASE("/ at the area list searches every area's names and descriptions") {
    FakeLibrary lib = library();
    std::string out;
    BbsConfig   c = twoAreas();
    FileBrowser b(c.areas, lib, out);
    b.begin();
    out.clear();
    b.key('/');
    for (char ch : std::string("flash\r")) b.key(ch);  // "Ready to flash" is AXISFLSH.ZIP's description
    std::string p = plain(out);
    CHECK(p.find("AXISFLSH.ZIP") != std::string::npos);
    CHECK(p.find("Axis Micro BBS Firmware") != std::string::npos);  // which area it's in
    CHECK(p.find("1 found") != std::string::npos);
    out.clear();
    b.key('/');
    for (char ch : std::string("list1\r")) b.key(ch);  // names too: LIST1.TXT, LIST10 ... LIST19
    CHECK(plain(out).find("LIST15.TXT") != std::string::npos);
}
