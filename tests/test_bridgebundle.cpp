// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "bbs/msgbase.h"
#include "bridgebundle.h"
#include "doctest.h"
#include "net/record.h"

using namespace axis::bbs;
namespace fs = std::filesystem;

namespace {
struct BridgeDir {
    fs::path p = fs::temp_directory_path() / "axis_bridgebundle";
    BridgeDir() {
        fs::remove_all(p);
        fs::create_directories(p);
    }
    ~BridgeDir() { fs::remove_all(p); }
};
Message local(const char* from, const char* subj, const std::string& body) {
    Message m;
    m.from = from;
    m.to = "All";
    m.subject = subj;
    m.date = "2026-11-28 12:00";
    m.body = body;
    return m;
}
std::vector<std::string> exportLines(MessageStore& store, const std::vector<bridge::ExportArea>& areas,
                                     std::vector<int> skip = {}) {
    std::vector<std::string> lines;
    bridge::exportAreas(store, "0a1b2c3d4e5f6789", areas, [&](const std::string&) { return skip; },
                        [&](const std::string& l) { lines.push_back(l); });
    return lines;
}
}  // namespace

TEST_CASE("bridge: CP437, pipe colours and ANSI become plain UTF-8, and back") {
    CHECK(bridge::cp437ToUtf8("caf\x82 \xb0\xb1\xb2") == "caf\xc3\xa9 \xe2\x96\x91\xe2\x96\x92\xe2\x96\x93");
    CHECK(bridge::utf8ToCp437("caf\xc3\xa9 \xe2\x80\x94 \xf0\x9f\x98\x80") == "caf\x82 - ?");
    CHECK(bridge::plainText("|14Hi|07 \x1b[1;31mthere\x1b[0m |99x") == "Hi there |99x");
    CHECK(bridge::originOk("axis:0a1b2c3d4e5f6789:general.3"));
    CHECK_FALSE(bridge::originOk("AXIS:x:y"));
    CHECK_FALSE(bridge::originOk("axis:a b:c"));
}

TEST_CASE("bridge: flat JSON reads strings, nulls and numbers, and refuses the rest") {
    std::map<std::string, std::string> f;
    std::map<std::string, bool>        q;
    REQUIRE(bridge::jsonFlat(R"({"a":"x\ny é 😀","b":null,"c":12})", f, q));
    CHECK(f["a"] == "x\ny \xc3\xa9 \xf0\x9f\x98\x80");
    CHECK(f["b"] == "null");
    CHECK_FALSE(q["b"]);
    CHECK(f["c"] == "12");
    CHECK_FALSE(bridge::jsonFlat(R"({"a":{"b":1}})", f, q));
    CHECK_FALSE(bridge::jsonFlat(R"({"a":"x","a":"y"})", f, q));
    CHECK_FALSE(bridge::jsonFlat(R"({"a":"x"} junk)", f, q));
    CHECK(bridge::jsonQuote("a\"b\\\n\x01") == "\"a\\\"b\\\\\\n\\u0001\"");
}

TEST_CASE("bridge: export gives local and net posts portable origins") {
    BridgeDir        d;
    FileMessageStore store(d.p.string());
    store.post("general", local("Rev", "|11Hello", "line one\nline two \xb2"));
    axis::net::NetPost p;
    p.id = "0123456789abcdef-7";
    p.area = "sn_support";
    p.board = "0123456789abcdef";
    p.from = "Sysop";
    p.to = "All";
    p.subject = "Net hello";
    p.body = "from the net";
    p.date = 1764000000;
    Message nm = local("Sysop", "Net hello", "from the net");
    p.sig = std::string(128, 'a');  // decode checks the form only; the board checks signatures, not the bridge
    nm.net = axis::net::encode(p);
    REQUIRE_FALSE(nm.net.empty());
    store.post("sn_support", nm);
    auto lines = exportLines(store, {{"general", "General Chatter"}, {"sn_support", ""}});
    REQUIRE(lines.size() == 5);  // header, 2 boards, 2 posts
    CHECK(lines[0].find("\"format\":\"radnet-bridge\"") != std::string::npos);
    CHECK(lines[0].find("\"records\":4") != std::string::npos);
    CHECK(lines[1].find("\"origin\":\"axis:0a1b2c3d4e5f6789:general\"") != std::string::npos);
    CHECK(lines[1].find("General Chatter") != std::string::npos);
    CHECK(lines[2].find("\"origin\":\"axis:0a1b2c3d4e5f6789:general.1\"") != std::string::npos);
    CHECK(lines[2].find("\"title\":\"Hello\"") != std::string::npos);
    CHECK(lines[2].find("line two \xe2\x96\x93") != std::string::npos);
    bool net = false;
    for (auto& l : lines)
        if (l.find("\"origin\":\"axis:0123456789abcdef:0123456789abcdef-7\"") != std::string::npos &&
            l.find("\"board\":\"axis:net:sn_support\"") != std::string::npos)
            net = true;
    CHECK(net);
}

TEST_CASE("bridge: import posts into one area, once, and export never sends them back") {
    BridgeDir        d;
    FileMessageStore store(d.p.string());
    std::string      bundle =
        "{\"format\":\"radnet-bridge\",\"network\":\"radnet\",\"records\":5,\"station\":\"s\",\"version\":1}\n"
        "{\"type\":\"board\",\"origin\":\"radnet:s:b1\",\"title\":\"Field reports\",\"author\":\"Longwood\"}\n"
        "{\"type\":\"post\",\"origin\":\"radnet:s:p1\",\"board\":\"radnet:s:b1\",\"parent\":null,\"author\":\"Longwood\","
        "\"title\":\"LP-008\",\"body\":\"ten probes \\u2014 caf\\u00e9\"}\n"
        "{\"type\":\"post\",\"origin\":\"axis:0a1b2c3d4e5f6789:general.1\",\"board\":\"axis:0a1b2c3d4e5f6789:general\","
        "\"title\":\"echo\",\"body\":\"mine\"}\n"
        "{\"type\":\"post\",\"origin\":\"bad origin\",\"board\":\"radnet:s:b1\",\"title\":\"x\",\"body\":\"y\"}\n"
        "{\"type\":\"poll\",\"origin\":\"radnet:s:p9\"}\n";
    std::map<std::string, int> ledger;
    auto r = bridge::importBundle(bundle, store, "radnet", ledger, "2026-11-28 12:00",
                                  [&](int num, const std::string& o) { ledger[o] = num; });
    CHECK(r.fatal.empty());
    CHECK(r.posts == 1);
    CHECK(r.fromAxis == 1);
    CHECK(r.rejected == 2);
    Message m;
    REQUIRE(store.read("radnet", 1, m));
    CHECK(m.subject == "[Field reports] LP-008");
    CHECK(m.from == "Longwood");
    CHECK(m.body.find("ten probes - caf\x82") == 0);
    CHECK(m.body.find("[via radnet, unverified]") != std::string::npos);
    auto again = bridge::importBundle(bundle, store, "radnet", ledger, "x", nullptr);
    CHECK(again.posts == 0);
    CHECK(again.duplicates == 1);
    auto lines = exportLines(store, {{"radnet", "RadNet"}}, {1});
    CHECK(lines.size() == 1);  // header only: the imported post is not sent back
    CHECK(bridge::parseLedger("1 radnet:s:p1\n2 not an origin\nx\n") == std::map<std::string, int>{{"radnet:s:p1", 1}});
    CHECK(bridge::importBundle("{\"format\":\"other\",\"version\":1,\"network\":\"x\"}\n", store, "radnet", {}, "", nullptr)
              .fatal.size() > 0);
}
