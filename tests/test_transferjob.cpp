// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/transferjob.h"
#include "net/xmodem.h"
#include "net/zmodem.h"

using namespace axis::bbs;
using namespace axis::net;

namespace {
// an area store that can be written, like the SD card
struct MemLibrary : FileLibrary {
    std::map<std::string, std::string> files;  // "area/name" -> bytes
    std::vector<FileEntry> list(const std::string& area) override {
        std::vector<FileEntry> v;
        for (auto& f : files)
            if (f.first.rfind(area + "/", 0) == 0 && f.first.find("/FILES.BBS") == std::string::npos)
                v.push_back({f.first.substr(area.size() + 1), (uint32_t)f.second.size(), ""});
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
    bool describe(const std::string& area, const std::string& name, const std::string& desc) override {
        files[area + "/FILES.BBS"] += name + " " + desc + "\r\n";
        return true;
    }
};

struct End : axis::door::ByteStream {
    std::deque<uint8_t>* in;
    std::deque<uint8_t>* out;
    int  available() override { return (int)in->size(); }
    int  peek() override { return in->empty() ? -1 : in->front(); }
    int  read() override {
        if (in->empty()) return -1;
        int c = in->front();
        in->pop_front();
        return c;
    }
    void write(const uint8_t* p, size_t n) override { out->insert(out->end(), p, p + n); }
    bool carrier() override { return true; }
};
struct Line {
    std::deque<uint8_t> ab, ba;
    End                 board, caller;
    Line() {
        board.in = &ba;
        board.out = &ab;
        caller.in = &ab;
        caller.out = &ba;
    }
};

std::string sample(size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += (char)((i * 7 + i / 300) & 0xFF);
    return s;
}

void run(TransferJob& job, Transfer& other) {
    uint32_t now = 0;
    bool     a = true, b = true;
    for (int i = 0; i < 20000 && (a || b); i++, now += 10) {
        if (a) a = job.step(now);
        if (b) b = other.step(now);
    }
}
}  // namespace

TEST_CASE("a ZMODEM download sends the file from the area") {
    MemLibrary lib;
    lib.files["gcode/GT40.NC"] = sample(9000);
    Line            l;
    TransferRequest req{false, "gcode", "GT40.NC", "", 'Z'};
    TransferJob     job(lib, req, l.board);
    std::string     got;
    ZmodemReceiver  caller(
        l.caller, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { got.append((const char*)p, n); return true; }, [](bool) {});
    run(job, caller);
    CHECK(job.ok());
    CHECK(got == lib.files["gcode/GT40.NC"]);
    CHECK(job.bytes() == 9000);
}

TEST_CASE("a ZMODEM upload lands in the area with its description") {
    MemLibrary      lib;
    std::string     file = sample(12000);
    Line            l;
    TransferRequest req{true, "freeware", "", "A tool I wrote", 'Z'};
    TransferJob     job(lib, req, l.board);
    ZmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), "MYTOOL.ZIP");
    run(job, caller);
    CHECK(job.ok());
    CHECK(job.savedName() == "MYTOOL.ZIP");
    CHECK(lib.files["freeware/MYTOOL.ZIP"] == file);
    CHECK(lib.files["freeware/FILES.BBS"].find("MYTOOL.ZIP A tool I wrote") != std::string::npos);
}

TEST_CASE("an XMODEM upload uses the typed name and loses the 1Ah padding") {
    MemLibrary      lib;
    std::string     file = sample(1000);
    Line            l;
    TransferRequest req{true, "text", "NOTES.TXT", "Shop notes", 'X'};
    TransferJob     job(lib, req, l.board);
    XmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), false);
    run(job, caller);
    CHECK(job.ok());
    CHECK(lib.files["text/NOTES.TXT"] == file);
}

TEST_CASE("an upload never overwrites a file that is already there") {
    MemLibrary lib;
    lib.files["freeware/MYTOOL.ZIP"] = "old";
    std::string     file = sample(500);
    Line            l;
    TransferRequest req{true, "freeware", "", "newer", 'Z'};
    TransferJob     job(lib, req, l.board);
    ZmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), "mytool.zip");
    run(job, caller);
    CHECK(lib.files["freeware/MYTOOL.ZIP"] == "old");
    CHECK(job.savedName() != "MYTOOL.ZIP");
    CHECK(lib.files["freeware/" + job.savedName()] == file);
}

TEST_CASE("upload names are cleaned up") {
    CHECK(cleanUploadName("my tool (v2).zip") == "MY_TOOL_V2_.ZIP");
    CHECK(cleanUploadName("../../evil.exe") == "EVIL.EXE");
    CHECK(cleanUploadName("FILES.BBS") == "FILES_.BBS");  // the area's list is not a file
    CHECK(cleanUploadName("").empty());
}
