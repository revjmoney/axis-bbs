// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/linux/updatecheck.h"

#include "doctest.h"

using namespace axis::pcupdate;

namespace {

const char* kList = R"([
 {"tag_name":"v0.99","name":"board","draft":false,"prerelease":false,"body":"ESP32 notes","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/v0.99"},
 {"tag_name":"pc-v0.52","name":"old","draft":false,"prerelease":false,"body":"old pc","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.52"},
 {"tag_name":"pc-v0.54","name":"ten","draft":false,"prerelease":false,"body":"Faster doors\nNew waiting screen","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.54"},
 {"tag_name":"pc-v0.90","name":"draft","draft":true,"prerelease":false,"body":"not yet","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.90"},
 {"tag_name":"pc-v0.80","name":"pre","draft":false,"prerelease":true,"body":"pre","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.80"},
 {"tag_name":"pc-v0.70-rc1","name":"rc","draft":false,"prerelease":false,"body":"rc","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.70-rc1"}
])";

}  // namespace

TEST_CASE("version order is numeric, and a missing part is zero") {
    CHECK(cmpVersion(parseVersion("0.53"), parseVersion("0.54")) < 0);
    CHECK(cmpVersion(parseVersion("0.54"), parseVersion("0.53")) > 0);
    CHECK(cmpVersion(parseVersion("0.53"), parseVersion("0.53")) == 0);
    CHECK(cmpVersion(parseVersion("0.53"), parseVersion("0.53.0")) == 0);
    CHECK(cmpVersion(parseVersion("0.9"), parseVersion("0.10")) < 0);
    CHECK(cmpVersion(parseVersion("1.0"), parseVersion("0.99")) > 0);
    CHECK(cmpVersion(parseVersion("1.2.3"), parseVersion("1.2")) > 0);
    CHECK_FALSE(parseVersion("").ok);
    CHECK_FALSE(parseVersion("0.53-rc1").ok);
    CHECK_FALSE(parseVersion("pc-v0.53").ok);
    CHECK(std::string(kBlink).size() <= 5);
}

TEST_CASE("pc-v tags are the Linux releases and v tags are the board") {
    CHECK(parseTag("pc-v0.53").kind == TagKind::Pc);
    CHECK(cmpVersion(parseTag("pc-v0.53").version, parseVersion("0.53")) == 0);
    CHECK(parseTag("v0.61").kind == TagKind::Board);
    CHECK(parseTag("pc-v0.70-rc1").kind == TagKind::Other);
    CHECK(parseTag("PC-V0.53").kind == TagKind::Other);
    CHECK(parseTag("pc-v").kind == TagKind::Other);
    CHECK(parseTag("not-a-release").kind == TagKind::Other);
}

TEST_CASE("the newest real pc-v release wins over board tags, drafts and prereleases") {
    auto list = parseReleaseList(kList);
    REQUIRE(list.ok);
    CHECK(list.releases.size() == 6);
    auto pick = pickPcRelease(list, "0.53", kDefaultRepo);
    CHECK(pick.found);
    CHECK(pick.newer);
    CHECK(pick.version == "0.54");
    CHECK(pick.tag == "pc-v0.54");
    CHECK(pick.summary == "Faster doors New waiting screen");
    CHECK(pick.notes.find("New waiting screen") != std::string::npos);
    CHECK(pick.url == "https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.54");

    auto order = parseReleaseList(
        R"([{"tag_name":"pc-v0.9","draft":false,"prerelease":false,"body":"nine","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.9"},{"tag_name":"pc-v0.10","draft":false,"prerelease":false,"body":"ten","html_url":"https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.10"}])");
    REQUIRE(order.ok);
    auto ten = pickPcRelease(order, "0.9", kDefaultRepo);
    CHECK(ten.version == "0.10");  // 0.10 is newer than 0.9; a text sort would say the opposite
    CHECK(ten.newer);

    auto same = pickPcRelease(list, "0.54", kDefaultRepo);
    CHECK(same.found);
    CHECK_FALSE(same.newer);
    CHECK(same.version == "0.54");

    auto ahead = pickPcRelease(list, "1.0", kDefaultRepo);
    CHECK(ahead.found);
    CHECK_FALSE(ahead.newer);
}

TEST_CASE("a release with no body uses its name, and a javascript url is dropped") {
    auto list = parseReleaseList(
        R"json([{"tag_name":"pc-v0.54","author":{"login":"rev"},"draft":false,"prerelease":false,"body":null,"name":"pc 0.54","html_url":"javascript:alert(1)"}])json");
    REQUIRE(list.ok);
    auto pick = pickPcRelease(list, "0.53", kDefaultRepo);
    CHECK(pick.summary == "pc 0.54");
    CHECK(pick.url == "https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.54");
    CHECK(pick.newer);
}

TEST_CASE("malformed release lists are not a version") {
    for (const char* bad : {"", "not json", "{", "[{", R"json([{"tag_name":])json", "[]x", "[1]", R"({"tag_name":"pc-v9.9"})", "null"}) {
        auto list = parseReleaseList(bad);
        CHECK_FALSE(list.ok);
        CHECK(list.releases.empty());
        auto pick = pickPcRelease(list, "0.53", kDefaultRepo);
        CHECK_FALSE(pick.found);
        CHECK_FALSE(pick.newer);
    }
    auto empty = parseReleaseList("  [ ]  ");
    CHECK(empty.ok);
    CHECK_FALSE(pickPcRelease(empty, "0.53", kDefaultRepo).found);
}

TEST_CASE("the cache round-trips and a broken file does not look like a release") {
    Cache c;
    c.checked = 1700000000;
    c.latest = "0.54";
    c.newer = true;
    c.summary = "Faster doors";
    c.notes = "line one\nline two";
    c.url = "https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.54";
    Cache back = readCache(writeCache(c));
    CHECK(back.checked == c.checked);
    CHECK(back.latest == c.latest);
    CHECK(back.newer);
    CHECK(back.summary == c.summary);
    CHECK(back.notes == c.notes);
    CHECK(back.url == c.url);

    CHECK(readCache("!!!!").checked == 0);
    CHECK(readCache("checked=abc\nlatest=0.99\nnewer=1\n").latest.empty());
    CHECK_FALSE(readCache("checked=10\nlatest=../../etc/passwd\nnewer=1\n").newer);
    CHECK(readCache("checked=10\nlatest=0.54\nnewer=1\nurl=http://evil.example/\n").url.empty());
    auto hostile = readCache(std::string(20000, 'x'));
    CHECK(hostile.checked == 0);
    CHECK_FALSE(hostile.newer);
}

TEST_CASE("a check is due once a day, and not again just because the program restarted") {
    int64_t t = 1700000000;
    CHECK(due(0, t));
    CHECK_FALSE(due(t, t + kCheckInterval - 1));
    CHECK(due(t, t + kCheckInterval));
    CHECK(due(t, t - 10));  // the clock went backwards: look again rather than wait forever

    Cache prev;
    prev.checked = t - 10;
    prev.latest = "0.50";
    prev.summary = "kept";
    prev.newer = true;
    prev.url = "https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.50";
    auto bad = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::Ok, 200, "{not json");
    CHECK(bad.remember);
    CHECK_FALSE(bad.parsed);
    CHECK(bad.cache.checked == t);
    CHECK(bad.cache.latest == "0.50");
    CHECK(bad.cache.summary == "kept");
    CHECK(bad.cache.newer);
    CHECK_FALSE(due(bad.cache.checked, t + 10));

    auto limited = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::RateLimit, 429, "rate limit");
    CHECK(limited.cache.latest == "0.50");
    CHECK(limited.cache.checked == t);

    auto offline = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::NoAnswer, 0, "");
    CHECK(offline.cache.latest == prev.latest);
    CHECK(offline.cache.checked == t);

    auto noTls = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::NoTls, 0, "");
    CHECK_FALSE(noTls.remember);
    CHECK(noTls.cache.checked == prev.checked);
    CHECK(noTls.cache.latest == prev.latest);

    auto got = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::Ok, 200, kList);
    REQUIRE(got.parsed);
    CHECK(got.cache.newer);
    CHECK(got.cache.latest == "0.54");
    CHECK(got.cache.checked == t);
    CHECK_FALSE(due(got.cache.checked, t + 60));
    CHECK(due(got.cache.checked, t + kCheckInterval));

    auto none = applyFetch(prev, t, "0.53", kDefaultRepo, FetchKind::Ok, 200, "[]");
    REQUIRE(none.parsed);
    CHECK_FALSE(none.cache.newer);
    CHECK(none.cache.latest.empty());
    CHECK(none.cache.checked == t);
}

TEST_CASE("http responses: chunked json, a rate limit, and a cut-off body") {
    auto ok = parseHttp("HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\n[]");
    CHECK(ok.headersOk);
    CHECK(ok.bodyOk);
    CHECK(ok.code == 200);
    CHECK(ok.body == "[]");
    CHECK(classifyHttp(ok) == FetchKind::Ok);

    auto chunked = parseHttp("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    CHECK(chunked.bodyOk);
    CHECK(chunked.body == "hello");

    auto cut = parseHttp("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhel");
    CHECK(cut.headersOk);
    CHECK_FALSE(cut.bodyOk);
    CHECK(classifyHttp(cut) == FetchKind::BadHttp);

    CHECK(classifyHttp(parseHttp("")) == FetchKind::BadBody);
    CHECK(classifyHttp(parseHttp("not http")) == FetchKind::BadBody);
    auto rate = parseHttp("HTTP/1.1 403 Forbidden\r\nContent-Length: 10\r\n\r\nrate limit");
    CHECK(classifyHttp(rate) == FetchKind::RateLimit);
    auto forbid = parseHttp("HTTP/1.1 403 Forbidden\r\nContent-Length: 2\r\n\r\nno");
    CHECK(classifyHttp(forbid) == FetchKind::BadHttp);
    auto tooMany = parseHttp("HTTP/1.1 429 Too Many Requests\r\nContent-Length: 0\r\n\r\n");
    CHECK(classifyHttp(tooMany) == FetchKind::RateLimit);
}

TEST_CASE("the request names the pc releases and does not take a dirty version") {
    auto q = buildRequest("revjmoney/axis-bbs", "0.53");
    CHECK(q.find("GET /repos/revjmoney/axis-bbs/releases?per_page=100 HTTP/1.0\r\n") == 0);
    CHECK(q.find("Host: api.github.com\r\n") != std::string::npos);
    CHECK(q.find("User-Agent: axispc/0.53\r\n") != std::string::npos);
    CHECK(q.find("Accept: application/vnd.github+json\r\n") != std::string::npos);
    auto dirty = buildRequest("revjmoney/axis-bbs", "0.53\r\nX-Evil: 1");
    CHECK(dirty.find("X-Evil") == std::string::npos);
    CHECK(dirty.find("User-Agent: axispc\r\n") != std::string::npos);
}

TEST_CASE("the sysop report names the release, the url and how to turn the check off") {
    Cache c;
    c.checked = 1700000000;
    c.latest = "0.54";
    c.newer = true;
    c.notes = "Faster doors";
    c.url = "https://github.com/revjmoney/axis-bbs/releases/tag/pc-v0.54";
    auto on = formatReport(c, "0.53", true);
    CHECK(on.find("pc-v0.54") != std::string::npos);
    CHECK(on.find("Faster doors") != std::string::npos);
    CHECK(on.find(c.url) != std::string::npos);
    CHECK(on.find("update_check=0") != std::string::npos);
    CHECK(on.find("blinks") != std::string::npos);
    auto off = formatReport(c, "0.53", false);
    CHECK(off.find("off") != std::string::npos);
    CHECK(off.find("0.54") == std::string::npos);
}
