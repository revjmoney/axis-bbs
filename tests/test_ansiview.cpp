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

using namespace axis::bbs;
using axis::door::ScriptStream;

namespace {
struct PackLibrary : FileLibrary {
    std::map<std::string, std::string> files;  // name -> bytes, all in one area
    std::vector<FileEntry> list(const std::string&) override {
        std::vector<FileEntry> v;
        for (auto& f : files) v.push_back({f.first, (uint32_t)f.second.size(), f.first == "AXISTEST.ZIP" ? "Test pack" : ""});
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
    CHECK(out.find("line 40") != std::string::npos);
    CHECK(out.find("SAUCE00") == std::string::npos);
}

TEST_CASE("Enter after a piece shows the next one") {
    Rig r;
    r.v.begin();
    r.run("1\r");
    r.run("1\r");
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
