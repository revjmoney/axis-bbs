// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/ansiviewer.h"
#include "bbs/zipreader.h"
#include "door/bytestream.h"
#include "net/crc.h"

using namespace axis::bbs;
using axis::door::ScriptStream;

namespace {
struct PackLibrary : FileLibrary {
    std::map<std::string, std::string> files;  // name -> bytes, all in one area
    std::map<std::string, std::string> about;
    std::vector<FileEntry> list(const std::string&) override {
        std::vector<FileEntry> v;
        for (auto& f : files) {
            std::string desc = f.first == "AXISTEST.ZIP" ? "Test pack" : "";
            auto it = about.find(f.first);
            if (it != about.end()) desc = it->second;
            v.push_back({f.first, (uint32_t)f.second.size(), desc});
        }
        return v;
    }
    bool read(const std::string&, const std::string& name, std::string& out, size_t max) override {
        auto it = files.find(name);
        if (it == files.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
};

PackLibrary library() {
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    PackLibrary   l;
    l.files["AXISTEST.ZIP"] = std::string((std::istreambuf_iterator<char>(in)), {});
    l.files["FILES.BBS"] = "AXISTEST.ZIP Test pack\r\n";
    return l;
}

struct Rig {
    PackLibrary          lib = library();
    std::vector<uint8_t> ws = std::vector<uint8_t>(ZipReader::kWorkspace);
    ScriptStream         line;
    AnsiViewer           v{lib, "ansi", "ANSI Packs", ws.data(), ws.size(), line};
    std::string          run(const std::string& keys) {  // type, let the viewer work, return what it sent
        line.push(keys);
        for (int i = 0; i < 20 && running; i++) running = v.step();
        std::string out = line.output();
        line.clearOutput();
        return out;
    }
    bool running = true;
};
}  // namespace

TEST_CASE("a pack described as movies is listed first, with that word on the row") {
    Rig r;
    r.lib.files.clear();
    r.lib.files["AXIS-01.ZIP"] = "a";
    r.lib.files["BT-1980.ZIP"] = "b";
    r.lib.files["BT-AA23.ZIP"] = "c";
    r.lib.about["AXIS-01.ZIP"] = "AXIS artpack";
    r.lib.about["BT-1980.ZIP"] = "Blocktronics: 1980 (2012) [16colo.rs]";
    r.lib.about["BT-AA23.ZIP"] = "Blocktronics: Detention Block AA-23, Star Wars (2017): ANSI movies inside [16colo.rs]";
    r.v.begin();
    std::string out = r.run("");
    size_t movie = out.find("BT-AA23.ZIP");
    size_t still = out.find("AXIS-01.ZIP");
    CHECK(movie != std::string::npos);
    CHECK(still != std::string::npos);
    CHECK(movie < still);
    CHECK(out.find("ANSI movies") != std::string::npos);
    CHECK(out.find("Detention Block AA-23") != std::string::npos);
}

TEST_CASE("viewer opens on the list of packs in the area") {
    Rig         r;
    r.v.begin();
    std::string out = r.run("");
    CHECK(out.find("AXISTEST.ZIP") != std::string::npos);
    CHECK(out.find("Test pack") != std::string::npos);
    CHECK(out.find("FILES.BBS") == std::string::npos);
}

TEST_CASE("choosing a pack lists the pieces you can view") {
    Rig r;
    r.v.begin();
    r.run("");
    std::string out = r.run("1\r");
    CHECK(out.find("REV-AXIS.ANS") != std::string::npos);
    CHECK(out.find("FILE_ID.DIZ") != std::string::npos);
    CHECK(out.find("PROGRAM.EXE") == std::string::npos);
}

TEST_CASE("choosing a piece shows the art without its SAUCE record") {
    Rig r;
    r.v.begin();
    r.run("1\r");
    std::string out = r.run("1\r");
    out += r.run("n");  // n: the rest without stopping (two statements: + has no order)
    CHECK(out.find("line 40") != std::string::npos);
    CHECK(out.find("SAUCE00") == std::string::npos);
}

TEST_CASE("a long piece stops after each screen until the caller asks for more") {
    Rig         r;
    r.v.begin();
    r.run("1\r");
    std::string first = r.run("1\r");
    CHECK(first.find("more") != std::string::npos);
    CHECK(first.find("line 40") == std::string::npos);
    std::string rest = r.run("\r");
    rest += r.run("\r");
    CHECK(rest.find("line 40") != std::string::npos);
}

TEST_CASE("Enter after a piece shows the next one") {
    Rig r;
    r.v.begin();
    r.run("1\r");
    r.run("1\r");
    r.run("n");
    CHECK(r.run("\r").find("AXIS test pack") != std::string::npos);
}

TEST_CASE("Q goes back a level and leaves the viewer from the pack list") {
    Rig r;
    r.v.begin();
    r.run("1\r");
    CHECK(r.run("q").find("AXISTEST.ZIP") != std::string::npos);
    CHECK(r.running);
    r.run("q");
    CHECK_FALSE(r.running);
}

TEST_CASE("a hang-up ends the viewer") {
    Rig r;
    r.v.begin();
    r.line.hangUp();
    CHECK_FALSE(r.v.step());
}

namespace {
// a stored (uncompressed) ZIP of the given files, built in memory
std::string storedZip(const std::vector<std::pair<std::string, std::string>>& files) {
    std::string z, cd;
    auto        u16 = [](std::string& s, uint32_t v) { s += (char)(v & 255), s += (char)(v >> 8 & 255); };
    auto        u32 = [&](std::string& s, uint32_t v) { u16(s, v & 0xFFFF), u16(s, v >> 16); };
    for (auto& f : files) {
        uint32_t crc = axis::net::crc32((const uint8_t*)f.second.data(), f.second.size()), off = (uint32_t)z.size();
        u32(z, 0x04034b50), u16(z, 10), u16(z, 0), u16(z, 0), u16(z, 0), u16(z, 0x21);
        u32(z, crc), u32(z, (uint32_t)f.second.size()), u32(z, (uint32_t)f.second.size());
        u16(z, (uint32_t)f.first.size()), u16(z, 0);
        z += f.first + f.second;
        u32(cd, 0x02014b50), u16(cd, 20), u16(cd, 10), u16(cd, 0), u16(cd, 0), u16(cd, 0), u16(cd, 0x21);
        u32(cd, crc), u32(cd, (uint32_t)f.second.size()), u32(cd, (uint32_t)f.second.size());
        u16(cd, (uint32_t)f.first.size()), u16(cd, 0), u16(cd, 0), u16(cd, 0), u16(cd, 0), u32(cd, 0), u32(cd, off);
        cd += f.first;
    }
    uint32_t at = (uint32_t)z.size();
    z += cd;
    u32(z, 0x06054b50), u16(z, 0), u16(z, 0), u16(z, (uint32_t)files.size()), u16(z, (uint32_t)files.size());
    u32(z, (uint32_t)cd.size()), u32(z, at), u16(z, 0);
    return z;
}
std::string movie() {  // an ANSI movie: the same spot redrawn, frame after frame
    std::string m = "\x1b[2J";
    for (int i = 0; i < 60; i++) m += "\x1b[10;30H\x1b[1;33mframe " + std::to_string(i) + "   \x1b[12;30H*";
    return m;
}
}  // namespace

TEST_CASE("an ANSI movie plays at modem speed, not all at once") {
    Rig r;
    r.lib.files.clear();
    r.lib.files["MOVIES.ZIP"] = storedZip({{"TIE.ANS", movie()}});
    r.lib.files["FILES.BBS"] = "MOVIES.ZIP ANSI movies\r\n";
    uint32_t now = 0;
    r.v.setClock([&now] { return now; });
    r.v.begin();
    r.run("1\r");
    std::string start = r.run("1\r");
    CHECK(start.find("frame 59") == std::string::npos);  // not yet
    std::string all = start;
    for (int k = 0; k < 40 && all.find("frame 59") == std::string::npos; k++) {
        now += 500;
        all += r.run("");
    }
    CHECK(all.find("frame 59") != std::string::npos);  // it gets there
    CHECK(now >= 2000);                                // at 9600 baud the movie takes a few seconds
    CHECK(all.find("more") == std::string::npos);      // a movie never stops for "more"
}

TEST_CASE("a movie can be stopped, and the speed changed while it plays") {
    Rig r;
    r.lib.files.clear();
    r.lib.files["MOVIES.ZIP"] = storedZip({{"TIE.ANS", movie()}});
    r.lib.files["FILES.BBS"] = "MOVIES.ZIP ANSI movies\r\n";
    uint32_t now = 0;
    r.v.setClock([&now] { return now; });
    r.v.begin();
    r.run("1\r");
    r.run("1\r");
    now += 300;
    r.run("1");  // 2400 baud
    now += 300;
    size_t slow = r.run("").size();
    r.run("4");  // 57600
    now += 300;
    size_t fast = r.run("").size();
    CHECK(fast > slow * 3);
    std::string out = r.run("q");
    CHECK(out.find("TIE.ANS") != std::string::npos);  // back at the list of pieces
}

TEST_CASE("pieces that all share a long name prefix are listed without it, in wider columns") {
    Rig r;
    r.lib.files.clear();
    r.lib.files["SW.ZIP"] = storedZip({{"ANSI_Star_Wars_KM-FALC_.ANS", "falcon"},
                                       {"ANSI_Star_Wars_Luciano-From_Anakin_to_Vader_.ans", "vader"},
                                       {"ANSI_Star_Wars_goo-episode_01.ans", "goo"},
                                       {"__BLOCKTRONICS_Detention_Block_NFO.ans", "the pack's info"}});  // one doesn't share it
    r.lib.files["FILES.BBS"] = "SW.ZIP Star Wars\r\n";
    r.v.begin();
    std::string out = r.run("1\r");
    CHECK(out.find("KM-FALC_.ANS") != std::string::npos);
    CHECK(out.find("Luciano-From_Anakin_to_Vad") != std::string::npos);  // long names get room
    CHECK(out.find("ANSI_Star_Wars_K") == std::string::npos);
    CHECK(out.find("__BLOCKTRONICS") != std::string::npos);  // the odd one out keeps its name
}

TEST_CASE("the pack and piece lists are lightbars: Enter on the bar opens it, Esc goes back a level") {
    Rig r;
    r.v.begin();
    r.run("");
    std::string pieces = r.run("\r");  // the bar starts on the first (only) pack
    CHECK(pieces.find("REV-AXIS.ANS") != std::string::npos);
    r.line.push("\x1b");
    r.running = r.v.step();
    r.v.idle();  // the caller paused: a lone Esc
    CHECK(r.run("").find("AXISTEST.ZIP") != std::string::npos);  // back on the pack list
    CHECK(r.running);
}
