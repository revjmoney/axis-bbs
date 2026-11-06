// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/node.h"
#include "bbs/transferjob.h"
#include "bbs/zipreader.h"
#include <fstream>
#include <iterator>
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
    bool remove(const std::string& area, const std::string& name) override { return files.erase(area + "/" + name) > 0; }
    bool rename(const std::string& area, const std::string& from, const std::string& to) override {
        auto it = files.find(area + "/" + from);
        if (it == files.end() || files.count(area + "/" + to)) return false;
        files[area + "/" + to] = it->second;
        files.erase(it);
        return true;
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
    bool up = true;
    bool carrier() override { return up; }
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
    // Sized here, not as a file-scope vector. ZipReader::kWorkspace is set in another file, so a
    // file-scope vector would be built while that constant is still 0 and every ZIP check would
    // be rejected for lack of memory.
    std::vector<uint8_t> workspace(ZipReader::kWorkspace);
    uint32_t now = 0;
    bool     a = true, b = true;
    for (int i = 0; i < 20000 && (a || b); i++, now += 10) {
        if (a && job.needsWorkspace()) job.beginProcessing(workspace.data(), workspace.size());  // as the host does
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
        (uint32_t)file.size(), "MYTOOL.BIN");
    run(job, caller);
    CHECK(job.ok());
    CHECK(job.savedName() == "MYTOOL.BIN");
    CHECK(lib.files["freeware/MYTOOL.BIN"] == file);
    CHECK(lib.files["freeware/FILES.BBS"].find("MYTOOL.BIN A tool I wrote") != std::string::npos);
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
    lib.files["freeware/MYTOOL.BIN"] = "old";
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
        (uint32_t)file.size(), "mytool.bin");
    run(job, caller);
    CHECK(lib.files["freeware/MYTOOL.BIN"] == "old");
    CHECK(job.savedName() != "MYTOOL.BIN");
    CHECK(lib.files["freeware/" + job.savedName()] == file);
}

TEST_CASE("upload names are cleaned up") {
    CHECK(cleanUploadName("my tool (v2).zip") == "MY_TOOL_V2_.ZIP");
    CHECK(cleanUploadName("../../evil.exe") == "EVIL.EXE");
    CHECK(cleanUploadName("FILES.BBS") == "FILES_.BBS");  // the area's list is not a file
    CHECK(cleanUploadName("").empty());
}

namespace {
std::string packZip() {
    std::ifstream in(std::string(AXIS_TEST_DATA) + "/pack.zip", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}
}  // namespace

TEST_CASE("an uploaded zip is checked and gets AXISBBS.NFO before it is listed") {
    MemLibrary      lib;
    std::string     file = packZip();
    Line            l;
    TransferRequest req{true, "ansi", "", "Test pack", 'Z'};
    TransferJob     job(lib, req, l.board);
    ZmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), "PACK.ZIP");
    run(job, caller);
    CHECK(job.ok());
    CHECK(lib.files["ansi/PACK.ZIP"].find("AXISBBS.NFO") != std::string::npos);
    CHECK(lib.files["ansi/FILES.BBS"].find("PACK.ZIP Test pack") != std::string::npos);
    CHECK(job.note().find("AXISBBS.NFO") != std::string::npos);
    std::string seen(l.ab.begin(), l.ab.end());  // whatever is left for the caller to read
    (void)seen;
}

TEST_CASE("a 256 KB ZMODEM download from an area matches byte for byte") {
    MemLibrary lib;
    lib.files["gcode/LONG.NC"] = sample(256 * 1024);
    Line            l;
    TransferRequest req{false, "gcode", "LONG.NC", "", 'Z'};
    TransferJob     job(lib, req, l.board);
    std::string     got;
    ZmodemReceiver  caller(
        l.caller, [](const std::string&, uint32_t n) { return n == 256 * 1024; },
        [&](const uint8_t* p, size_t n) { got.append((const char*)p, n); return true; }, [](bool) {});
    run(job, caller);
    CHECK(job.ok());
    CHECK(got == lib.files["gcode/LONG.NC"]);
    CHECK(job.bytes() == 256 * 1024);
}

TEST_CASE("two area downloads stepped on alternate turns stay intact") {
    MemLibrary lib;
    lib.files["gcode/A.NC"] = sample(40 * 1024);
    lib.files["gcode/B.NC"] = sample(50 * 1024);
    lib.files["gcode/B.NC"][0] = 'B';
    Line            la, lb;
    TransferRequest ra{false, "gcode", "A.NC", "", 'Z'};
    TransferRequest rb{false, "gcode", "B.NC", "", 'Z'};
    TransferJob     ja(lib, ra, la.board), jb(lib, rb, lb.board);
    std::string     ga, gb;
    ZmodemReceiver  ca(
        la.caller, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { ga.append((const char*)p, n); return true; }, [](bool) {});
    ZmodemReceiver cb(
        lb.caller, [](const std::string&, uint32_t) { return true; },
        [&](const uint8_t* p, size_t n) { gb.append((const char*)p, n); return true; }, [](bool) {});
    uint32_t now = 0;
    bool     go = true;
    for (int i = 0; i < 20000 && go; i++, now += 10) {
        bool a = ja.step(now), b = jb.step(now), c = ca.step(now), d = cb.step(now);
        go = a || b || c || d;
    }
    CHECK(ja.ok());
    CHECK(jb.ok());
    CHECK(ga == lib.files["gcode/A.NC"]);
    CHECK(gb == lib.files["gcode/B.NC"]);
}

TEST_CASE("a ZMODEM upload that loses carrier is not kept in the area") {
    MemLibrary      lib;
    std::string     file = sample(80 * 1024);
    Line            l;
    TransferRequest req{true, "freeware", "", "partial", 'Z'};
    TransferJob     job(lib, req, l.board);
    ZmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            if (off >= file.size()) return false;
            if (off + n > file.size()) n = file.size() - off;
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), "PARTIAL.BIN");
    uint32_t now = 0;
    bool     dropped = false;
    for (int i = 0; i < 200; i++, now += 10) {
        bool more = job.step(now);
        caller.step(now);
        if (!dropped && job.bytes() > 1024 && job.bytes() < file.size()) {
            l.board.up = false;
            l.caller.up = false;
            dropped = true;
        }
        if (dropped && !more) break;
    }
    CHECK(dropped);
    CHECK_FALSE(job.ok());
    for (auto& f : lib.files) CHECK(f.first.find("PARTIAL") == std::string::npos);
}

TEST_CASE("a damaged zip upload is rejected and not listed") {
    MemLibrary      lib;
    std::string     file = packZip();
    file[60] ^= 0x55;
    Line            l;
    TransferRequest req{true, "ansi", "", "Broken", 'Z'};
    TransferJob     job(lib, req, l.board);
    ZmodemSender    caller(
        l.caller,
        [&](uint32_t off, uint8_t* buf, size_t n) {
            file.copy((char*)buf, n, off);
            return true;
        },
        (uint32_t)file.size(), "BROKEN.ZIP");
    run(job, caller);
    CHECK_FALSE(job.ok());
    CHECK(lib.files.count("ansi/BROKEN.ZIP") == 0);
    CHECK(lib.files["ansi/FILES.BBS"].find("BROKEN") == std::string::npos);
    CHECK(job.note().find("damaged") != std::string::npos);
}
