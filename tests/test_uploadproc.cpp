// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/uploadproc.h"
#include "bbs/zipreader.h"

using namespace axis::bbs;

namespace {
struct MemLibrary : FileLibrary {
    std::map<std::string, std::string> files;  // "area/name" -> bytes
    std::vector<FileEntry> list(const std::string& area) override {
        std::vector<FileEntry> v;
        for (auto& f : files)
            if (f.first.rfind(area + "/", 0) == 0) v.push_back({f.first.substr(area.size() + 1), (uint32_t)f.second.size(), ""});
        return v;
    }
    bool read(const std::string& area, const std::string& name, std::string& out, size_t max) override {
        auto it = files.find(area + "/" + name);
        if (it == files.end()) return false;
        out = it->second.substr(0, max);
        return true;
    }
    struct W : WriteFile {
        MemLibrary* lib;
        std::string key, data;
        bool write(const uint8_t* p, size_t n) override {
            data.append((const char*)p, n);
            return true;
        }
        bool finish(bool keep) override {
            if (keep) lib->files[key] = data;
            return true;
        }
    };
    std::unique_ptr<WriteFile> create(const std::string& area, const std::string& name) override {
        std::unique_ptr<W> w(new W);
        w->lib = this;
        w->key = area + "/" + name;
        return std::unique_ptr<WriteFile>(w.release());
    }
    bool remove(const std::string& area, const std::string& name) override { return files.erase(area + "/" + name) > 0; }
    bool rename(const std::string& area, const std::string& from, const std::string& to) override {
        auto it = files.find(area + "/" + from);
        if (it == files.end() || files.count(area + "/" + to)) return false;
        files[area + "/" + to] = it->second;
        files.erase(it);
        return true;
    }
};

std::string testZip() {
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

std::vector<std::string> names(const std::string& zip) {  // the entries of a zip, through our reader
    ZipReader r(
        [&](uint32_t off, uint8_t* buf, size_t n) {
            if (off + n > zip.size()) return false;
            zip.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)zip.size());
    std::vector<std::string> v;
    if (!r.open()) return v;
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    for (auto& e : r.entries()) {
        bool ok = r.extract(e, ws.data(), ws.size(), [](const uint8_t*, size_t) { return true; }) == ZipReader::Result::Ok;
        v.push_back(e.name + (ok ? "" : " (BAD)"));
    }
    return v;
}

UploadInfo info() {
    UploadInfo i;
    i.bbsName = "AXIS UNDERGROUND CNC";
    i.address = "axis.local";
    i.location = "Myrtle Beach, SC, USA";
    i.software = "Axis Micro BBS Firmware 0.3";
    return i;
}

void runAll(UploadProcessor& p) {
    for (int i = 0; i < 1000 && p.step(); i++) {
    }
}
}  // namespace

TEST_CASE("a good zip is checked, gets AXISBBS.NFO and is rezipped in place") {
    MemLibrary lib;
    lib.files["ansi/PACK.ZIP"] = testZip();
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    UploadProcessor      p(lib, "ansi", "PACK.ZIP", info(), ws.data(), ws.size());
    runAll(p);
    CHECK(p.result() == UploadProcessor::Result::Rezipped);
    auto n = names(lib.files["ansi/PACK.ZIP"]);
    REQUIRE(n.size() == 4);
    CHECK(n[0] == "REV-AXIS.ANS");
    CHECK(n[3] == "AXISBBS.NFO");
    std::string nfo;
    for (auto& f : lib.files)
        if (f.first == "ansi/PACK.ZIP") nfo = f.second;
    CHECK(nfo.find("Myrtle Beach, SC, USA") != std::string::npos);  // the info file is stored, readable
    CHECK(lib.files.size() == 1);                                    // no temp file left behind
}

TEST_CASE("a damaged zip is rejected and removed") {
    MemLibrary  lib;
    std::string z = testZip();
    z[60] ^= 0x55;  // inside the deflated art
    lib.files["ansi/BAD.ZIP"] = z;
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    UploadProcessor      p(lib, "ansi", "BAD.ZIP", info(), ws.data(), ws.size());
    runAll(p);
    CHECK(p.result() == UploadProcessor::Result::Rejected);
    CHECK(lib.files.count("ansi/BAD.ZIP") == 0);
    CHECK(p.note().find("damaged") != std::string::npos);
}

TEST_CASE("a file that isn't a zip is kept as uploaded") {
    MemLibrary lib;
    lib.files["text/NOTES.TXT"] = "just some notes\r\n";
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    UploadProcessor      p(lib, "text", "NOTES.TXT", info(), ws.data(), ws.size());
    runAll(p);
    CHECK(p.result() == UploadProcessor::Result::Kept);
    CHECK(lib.files["text/NOTES.TXT"] == "just some notes\r\n");
}

TEST_CASE("processing twice replaces our info file instead of adding another") {
    MemLibrary lib;
    lib.files["ansi/PACK.ZIP"] = testZip();
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    for (int round = 0; round < 2; round++) {
        UploadProcessor p(lib, "ansi", "PACK.ZIP", info(), ws.data(), ws.size());
        runAll(p);
        CHECK(p.result() == UploadProcessor::Result::Rezipped);
    }
    CHECK(names(lib.files["ansi/PACK.ZIP"]).size() == 4);
}

TEST_CASE("progress moves forward through the steps") {
    MemLibrary lib;
    lib.files["ansi/PACK.ZIP"] = testZip();
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    UploadProcessor      p(lib, "ansi", "PACK.ZIP", info(), ws.data(), ws.size());
    double               last = -1;
    bool                 forward = true;
    for (int i = 0; i < 1000 && p.step(); i++) {
        forward = forward && p.progress() >= last;
        last = p.progress();
    }
    CHECK(forward);
    CHECK(p.progress() == doctest::Approx(1.0));
}
