// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstring>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/archive.h"

using namespace axis::bbs;

namespace {
struct Bytes : RandomFile {
    std::string d;
    explicit Bytes(std::string s) : d(std::move(s)) {}
    uint32_t size() const override { return (uint32_t)d.size(); }
    bool     readAt(uint32_t off, uint8_t* buf, size_t n) override {
        if ((uint64_t)off + n > d.size()) return false;
        std::memcpy(buf, d.data() + off, n);
        return true;
    }
};
void u16(std::string& s, uint32_t v) { s += (char)(v & 255), s += (char)(v >> 8 & 255); }
void u32(std::string& s, uint32_t v) { u16(s, v & 0xFFFF), u16(s, v >> 16); }
void vint(std::string& s, uint64_t v) {
    do {
        uint8_t b = v & 0x7F;
        v >>= 7;
        s += (char)(b | (v ? 0x80 : 0));
    } while (v);
}

std::string tarFile() {
    std::string t;
    auto add = [&](const std::string& name, const std::string& body) {
        std::string h(512, '\0');
        std::memcpy(&h[0], name.data(), name.size());
        std::memcpy(&h[100], "0000644", 7);
        char sz[12];
        std::snprintf(sz, sizeof sz, "%011o", (unsigned)body.size());
        std::memcpy(&h[124], sz, 11);
        std::memcpy(&h[136], "00000000000", 11);
        h[156] = '0';
        std::memcpy(&h[257], "ustar", 5);
        std::memset(&h[148], ' ', 8);
        unsigned sum = 0;
        for (unsigned char c : h) sum += c;
        char ck[8];
        std::snprintf(ck, sizeof ck, "%06o", sum);
        std::memcpy(&h[148], ck, 7);
        t += h + body + std::string((512 - body.size() % 512) % 512, '\0');
    };
    add("README.TXT", "read me first\n");
    add("art/LOGO.ANS", std::string(1300, 'x'));
    return t + std::string(1024, '\0');
}

std::string gzFile() {
    const uint8_t g[] = {0x1f, 0x8b, 0x08, 0x08, 0x00, 0x00, 0x00, 0x00, 0x02, 0xff, 0x48, 0x45, 0x4c, 0x4c, 0x4f, 0x2e, 0x54, 0x58, 0x54, 0x00,
                         0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0x48, 0x2b, 0xca, 0xcf, 0x55, 0x28, 0xc9, 0x48, 0x55, 0x48, 0xac, 0xc8, 0x2c, 0x56,
                         0x28, 0xcd, 0x4b, 0x49, 0x2d, 0x4a, 0x2f, 0xca, 0x07, 0xd2, 0x5c, 0x00, 0xbb, 0xbc, 0x2d, 0x2c, 0x20, 0x00, 0x00, 0x00};
    return std::string((const char*)g, sizeof g);
}

std::string rar4File() {  // marker, archive header, one file block with its data, the end block
    std::string r = std::string("Rar!\x1a\x07\x00", 7);
    u16(r, 0x90cf), r += '\x73', u16(r, 0), u16(r, 13), u16(r, 0), u32(r, 0);
    std::string name = "DOORS\\LORD.ZIP", data(400, 'z');
    u16(r, 0), r += '\x74', u16(r, 0x8000), u16(r, (uint32_t)(32 + name.size()));
    u32(r, (uint32_t)data.size()), u32(r, 1000), r += '\x02', u32(r, 0xdeadbeef), u32(r, 0x55f12345), r += '\x1d', r += '\x33';
    u16(r, (uint32_t)name.size()), u32(r, 0x20);
    r += name + data;
    u16(r, 0x3dc4), r += '\x7b', u16(r, 0x4000), u16(r, 7);
    return r;
}

std::string rar5File() {
    std::string r = std::string("Rar!\x1a\x07\x01\x00", 8);
    auto block = [&](const std::string& body, uint64_t dataSize, const std::string& data) {
        std::string hs;
        vint(hs, body.size());
        r += std::string(4, '\0') + hs + body + data;
        (void)dataSize;
    };
    std::string mainHead;
    vint(mainHead, 1), vint(mainHead, 0), vint(mainHead, 0);  // type 1 (main), flags, archive flags
    block(mainHead, 0, "");
    std::string fh, name = "ANSI/ACID.ANS", data(250, 'q');
    vint(fh, 2), vint(fh, 2), vint(fh, data.size());  // type 2 (file), flags: has data, data size
    vint(fh, 0x04), vint(fh, 9000), vint(fh, 0x20);   // file flags: CRC present; unpacked size; attributes
    u32(fh, 0x12345678);                              // data CRC
    vint(fh, 0x03), vint(fh, 0), vint(fh, name.size());
    fh += name;
    block(fh, data.size(), data);
    return r;
}

std::string arjFile() {
    auto header = [](const std::string& name, uint8_t type, uint32_t packed, uint32_t orig, uint8_t method) {
        std::string b;
        b += (char)30, b += (char)11, b += (char)1, b += (char)0, b += (char)0, b += (char)method, b += (char)type, b += (char)0;
        u32(b, 0x55f12345), u32(b, packed), u32(b, orig), u32(b, 0xcafef00d), u16(b, 0), u16(b, 0x20), u16(b, 0);
        b += name + '\0' + '\0';  // name, empty comment
        std::string h = "\x60\xea";
        u16(h, (uint32_t)b.size());
        h += b;
        u32(h, 0);  // header CRC (not checked by a lister)
        u16(h, 0);  // no extended header
        return h;
    };
    std::string a = header("TEST.ARJ", 2, 0, 0, 0);
    a += header("RMRS-42.NFO", 0, 120, 640, 1) + std::string(120, 'n');
    a += header("ART\\RMRS.ANS", 0, 300, 5000, 3) + std::string(300, 'a');
    return a + "\x60\xea\x00\x00";
}
}  // namespace

TEST_CASE("a tar's files are listed with their sizes") {
    Bytes       f(tarFile());
    ArchiveInfo a = listArchive("PACK.TAR", f);
    CHECK(a.kind == "TAR");
    REQUIRE(a.entries.size() == 2);
    CHECK(a.entries[1].name == "art/LOGO.ANS");
    CHECK(a.entries[1].size == 1300);
}

TEST_CASE("a gzip shows the file inside it, its name and size") {
    Bytes       f(gzFile());
    ArchiveInfo a = listArchive("HELLO.GZ", f);
    CHECK(a.kind == "GZIP");
    REQUIRE(a.entries.size() == 1);
    CHECK(a.entries[0].name == "HELLO.TXT");
    CHECK(a.entries[0].size == 32);
    CHECK(a.entries[0].method == "deflate");
}

TEST_CASE("a RAR (1.5 to 4) lists its files without unpacking them") {
    Bytes       f(rar4File());
    ArchiveInfo a = listArchive("DOORS.RAR", f);
    CHECK(a.kind == "RAR");
    REQUIRE(a.entries.size() == 1);
    CHECK(a.entries[0].name == "DOORS\\LORD.ZIP");
    CHECK(a.entries[0].size == 1000);
    CHECK(a.entries[0].packed == 400);
    CHECK(a.entries[0].crc == 0xdeadbeef);
}

TEST_CASE("a RAR 5 lists its files too") {
    Bytes       f(rar5File());
    ArchiveInfo a = listArchive("ART.RAR", f);
    CHECK(a.kind == "RAR5");
    REQUIRE(a.entries.size() == 1);
    CHECK(a.entries[0].name == "ANSI/ACID.ANS");
    CHECK(a.entries[0].size == 9000);
    CHECK(a.entries[0].packed == 250);
}

TEST_CASE("an ARJ lists its files with their methods") {
    Bytes       f(arjFile());
    ArchiveInfo a = listArchive("RMRS-42.ARJ", f);
    CHECK(a.kind == "ARJ");
    REQUIRE(a.entries.size() == 2);
    CHECK(a.entries[0].name == "RMRS-42.NFO");
    CHECK(a.entries[0].size == 640);
    CHECK(a.entries[1].packed == 300);
    CHECK(a.entries[1].method == "arj 3");
}

TEST_CASE("something that isn't an archive says so instead of listing junk") {
    Bytes       f(std::string(2000, 'x'));
    ArchiveInfo a = listArchive("README.TXT", f);
    CHECK(a.kind.empty());
    CHECK(a.entries.empty());
}
