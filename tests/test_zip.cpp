// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/zipreader.h"

using axis::bbs::ZipReader;

namespace {
std::vector<uint8_t> load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}
std::vector<uint8_t> testZip() { return load(std::string(AXIS_TEST_DATA) + "/pack.zip"); }

ZipReader readerOver(const std::vector<uint8_t>& z) {
    return ZipReader(
        [&z](uint32_t off, uint8_t* buf, size_t n) {
            if (off + n > z.size()) return false;
            std::copy(z.begin() + off, z.begin() + off + n, buf);
            return true;
        },
        (uint32_t)z.size());
}

std::string extract(ZipReader& r, const std::string& name, ZipReader::Result* res = nullptr) {
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    std::string          out;
    for (auto& e : r.entries())
        if (e.name == name) {
            auto got = r.extract(e, ws.data(), ws.size(), [&](const uint8_t* p, size_t n) {
                out.append((const char*)p, n);
                return true;
            });
            if (res) *res = got;
        }
    return out;
}
}  // namespace

TEST_CASE("zip reader lists entries behind a long comment") {
    auto z = testZip();
    auto r = readerOver(z);
    REQUIRE(r.open());
    REQUIRE(r.entries().size() == 3);
    CHECK(r.entries()[0].name == "REV-AXIS.ANS");
    CHECK(r.entries()[0].method == 8);
    CHECK(r.entries()[1].name == "FILE_ID.DIZ");
}

TEST_CASE("zip reader inflates a deflated entry") {
    auto z = testZip();
    auto r = readerOver(z);
    REQUIRE(r.open());
    ZipReader::Result res = ZipReader::Result::Corrupt;
    std::string       art = extract(r, "REV-AXIS.ANS", &res);
    CHECK(res == ZipReader::Result::Ok);
    CHECK(art.size() == r.entries()[0].usize);
    CHECK(art.find("line 40") != std::string::npos);
    CHECK(art.find("SAUCE00") != std::string::npos);
}

TEST_CASE("zip reader copies a stored entry") {
    auto z = testZip();
    auto r = readerOver(z);
    REQUIRE(r.open());
    CHECK(extract(r, "FILE_ID.DIZ") == "AXIS test pack\r\n");
}

TEST_CASE("a sink can stop extraction early") {
    auto z = testZip();
    auto r = readerOver(z);
    REQUIRE(r.open());
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    size_t               got = 0;
    auto res = r.extract(r.entries()[0], ws.data(), ws.size(), [&](const uint8_t*, size_t n) {
        got += n;
        return false;
    });
    CHECK(res == ZipReader::Result::Stopped);
    CHECK(got > 0);
}

TEST_CASE("zip reader refuses a file that isn't a zip") {
    std::vector<uint8_t> junk(300, 'x');
    auto                 r = readerOver(junk);
    CHECK_FALSE(r.open());
}

TEST_CASE("zip reader reads every piece of a real artpack") {
    const char* path = std::getenv("AXIS_ANSI_PACK");  // e.g. F:/cyd-demoscene/ansi_src/impure53.zip
    if (!path) return;
    auto z = load(path);
    auto r = readerOver(z);
    REQUIRE(r.open());
    std::vector<uint8_t> ws(ZipReader::kWorkspace);
    for (auto& e : r.entries()) {
        size_t total = 0;
        auto   res = r.extract(e, ws.data(), ws.size(), [&](const uint8_t*, size_t n) {
            total += n;
            return true;
        });
        CHECK(res == ZipReader::Result::Ok);
        CHECK(total == e.usize);
    }
}
