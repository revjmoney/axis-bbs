// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// LFD3K-LINK (telnet option 90) from the LORA FLOWTRON DATAMASTER 3000 SERIAL MODEM - LFD3K-SM.
// Not testable from the host: src/main.cpp's wiring (pulling the report from the stream into the Node, the sysop
// System > Modem page, @MODEM). Tested here instead: the filter, the stream, the parser, Node::lite/setLink, the WFC codes.
#include <regex>
#include <string>

#include "bbs/config.h"
#include "bbs/node.h"
#include "bbs/wfc.h"
#include "doctest.h"
#include "net/telnet.h"
#include "net/telnetstream.h"

using axis::bbs::BbsConfig;
using axis::bbs::Node;
using axis::bbs::SysInfo;
using axis::net::LinkInfo;
using axis::net::TelnetFilter;

namespace {
const std::string IAC_WILL90("\xFF\xFB\x5A", 3), IAC_DO90("\xFF\xFD\x5A", 3), IAC_DONT90("\xFF\xFE\x5A", 3);
const std::string IAC_WONT90("\xFF\xFC\x5A", 3);

std::string sb90(const std::string& payload) { return std::string("\xFF\xFA\x5A", 3) + payload + std::string("\xFF\xF0", 2); }
const std::string kFull = "v=1 modem=LFD3K-SM kind=radio caller=Rev tier=SF7 bps=5469 snr=7.2 rssi=-81 retrains=3 lz=1.8";

struct Out {
    std::string app, reply;
};
Out feed(TelnetFilter& t, const std::string& in) {
    Out o;
    t.fromNet((const uint8_t*)in.data(), in.size(), o.app, o.reply);
    return o;
}
TelnetFilter agreed() {
    TelnetFilter t;
    t.acceptLink(true);
    feed(t, IAC_WILL90);
    return t;
}

struct FakeLine : axis::net::TelnetStream {
    std::string fromCaller, toCaller;
    uint32_t    now = 0;
    int         rawRead(uint8_t* buf, size_t n) override {
        size_t k = fromCaller.size() < n ? fromCaller.size() : n;
        fromCaller.copy((char*)buf, k);
        fromCaller.erase(0, k);
        return (int)k;
    }
    int      rawWrite(const uint8_t* p, size_t n) override { toCaller.append((const char*)p, n); return (int)n; }
    bool     rawConnected() override { return true; }
    void     rawClose() override {}
    uint32_t nowMs() override { return now; }
};

SysInfo info() { return SysInfo{}; }
std::string plain(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\\[[0-9;?]*[A-Za-z]"), ""); }
LinkInfo report(const std::string& payload) {
    LinkInfo l;
    REQUIRE(axis::net::parseLinkInfo(payload, l));
    return l;
}
}  // namespace

// plan: link_option_negotiation
TEST_CASE("the modem's WILL 90 is answered DO 90 only when modem_link is on, else refused") {
    TelnetFilter on;
    on.acceptLink(true);
    Out a = feed(on, IAC_WILL90);
    CHECK(a.reply == IAC_DO90);
    CHECK(on.linkAgreed());
    CHECK(feed(on, IAC_WILL90).reply.empty());  // a repeat WILL gets no second answer
    CHECK(feed(on, IAC_WONT90).reply == IAC_DONT90);
    CHECK_FALSE(on.linkAgreed());
    CHECK(feed(on, IAC_WONT90).reply.empty());

    TelnetFilter off;  // default and explicit off
    CHECK(feed(off, IAC_WILL90).reply == IAC_DONT90);
    CHECK_FALSE(off.linkAgreed());
    off.acceptLink(false);
    CHECK(feed(off, IAC_WILL90).reply == IAC_DONT90);
    // with the option off, a report that arrives anyway is ignored
    feed(off, sb90(kFull));
    CHECK(off.linkReports() == 0);
    CHECK_FALSE(off.link().any);

    // through the stream: modem_link on answers DO 90, off answers DONT 90
    FakeLine l1;
    l1.acceptLink(true);
    l1.start();
    l1.toCaller.clear();
    l1.fromCaller = IAC_WILL90;
    CHECK(l1.available() == 0);
    CHECK(l1.toCaller == IAC_DO90);
    FakeLine l2;
    l2.acceptLink(false);
    l2.start();
    l2.toCaller.clear();
    l2.fromCaller = IAC_WILL90;
    CHECK(l2.available() == 0);
    CHECK(l2.toCaller == IAC_DONT90);
}

// plan: link_sb_parse_all_fields
TEST_CASE("a link report IAC SB 90 ... IAC SE is read into every field") {
    TelnetFilter t = agreed();
    feed(t, sb90(kFull));
    REQUIRE(t.linkReports() == 1);
    const LinkInfo& l = t.link();
    CHECK(l.any);
    CHECK(l.version == 1);
    CHECK(l.modem == "LFD3K-SM");
    CHECK(l.kind == "radio");
    CHECK(l.caller == "Rev");
    CHECK(l.tier == "SF7");
    CHECK(l.sf() == 7);
    CHECK(l.bps == 5469);
    CHECK(l.hasSnr);
    CHECK(l.snrTenths == 72);
    CHECK(l.hasRssi);
    CHECK(l.rssi == -81);
    CHECK(l.retrains == 3);
    CHECK(l.lzTenths == 18);
    CHECK(l.radio());
    CHECK(axis::net::linkLabel(l) == "RADIO SF7 -81");
    CHECK(axis::net::linkLabel(l, false) == "RADIO SF7");
    // a second report replaces the first
    feed(t, sb90("v=1 kind=radio tier=SF9 bps=1758 rssi=-99"));
    CHECK(t.linkReports() == 2);
    CHECK(t.link().tier == "SF9");
    CHECK(t.link().bps == 1758);
    // wifi, and a "-" caller is no name
    LinkInfo w = report("v=1 kind=wifi caller=- tier=WIFI bps=1000000");
    CHECK(w.caller.empty());
    CHECK(axis::net::linkLabel(w) == "WIFI");
    CHECK_FALSE(w.radio());
}

// plan: link_sb_garbage_ignored
TEST_CASE("garbage, unknown keys and bad numbers in a link report are ignored or clamped") {
    LinkInfo keep;
    keep.bps = 42;
    // no v= : not a report, and the output is left alone
    CHECK_FALSE(axis::net::parseLinkInfo("modem=X tier=SF7", keep));
    CHECK(keep.bps == 42);
    CHECK_FALSE(axis::net::parseLinkInfo("", keep));
    CHECK_FALSE(axis::net::parseLinkInfo("v=0", keep));
    CHECK_FALSE(axis::net::parseLinkInfo("v=abc", keep));
    CHECK_FALSE(axis::net::parseLinkInfo(std::string("v=1 modem=a\x01") + "b", keep));  // control byte
    CHECK_FALSE(axis::net::parseLinkInfo("v=1 modem=caf\xC3\xA9", keep));              // non-ASCII
    CHECK(keep.bps == 42);
    // junk tokens and unknown keys are skipped, the rest still read
    LinkInfo l = report("v=1 ===  =x novalue future=thing  tier=SF8 bps=1000");
    CHECK(l.tier == "SF8");
    CHECK(l.bps == 1000);
    // bad names and values leave a field unknown
    l = report("v=1 modem=bad/name caller=bad/name kind=laser tier=SF13 bps=fast snr=x rssi=1e3 retrains=-4 lz=-1");
    CHECK(l.modem.empty());
    CHECK(l.caller.empty());
    CHECK(l.kind.empty());
    CHECK(l.tier.empty());
    CHECK(l.bps == -1);
    CHECK_FALSE(l.hasSnr);
    CHECK_FALSE(l.hasRssi);
    CHECK(l.retrains == -1);
    CHECK(l.lzTenths == -1);
    CHECK(report("v=1 modem=" + std::string(25, 'a')).modem.empty());  // 25 > 24
    CHECK(report("v=1 modem=" + std::string(24, 'a')).modem.size() == 24);
    CHECK(report("v=1 tier=SF4").tier.empty());
    CHECK(report("v=1 tier=SF12").tier == "SF12");
    // numbers are clamped
    l = report("v=1 bps=99999999999 snr=-5000 rssi=-9000 retrains=99999999 lz=99999");
    CHECK(l.bps == -1);  // 11 digits: refused, not clamped
    CHECK(l.snrTenths == -999);
    CHECK(l.rssi == -200);
    CHECK(l.retrains == 999999);
    CHECK(l.lzTenths == 999);
    l = report("v=1 bps=999999999 rssi=500 snr=5000");
    CHECK(l.bps == 100000000);
    CHECK(l.rssi == 50);
    CHECK(l.snrTenths == 999);
    CHECK(report("v=1 snr=-0.55").snrTenths == -5);  // extra decimals cut
    CHECK(report("v=1 lz=0").lzTenths == 0);
    // a garbage report through the filter does not count and does not crash
    TelnetFilter t = agreed();
    feed(t, sb90("complete nonsense"));
    feed(t, sb90(""));
    feed(t, sb90("v=1 x=\x07"));
    CHECK(t.linkReports() == 0);
    CHECK_FALSE(t.link().any);
}

// plan: link_sb_oversize_dropped
TEST_CASE("a link report longer than 160 bytes is dropped whole, one of 160 is kept") {
    std::string ok = "v=1 tier=SF7 bps=1000 " + std::string(160 - 22, 'x');  // 160 bytes: real fields then a junk token
    REQUIRE(ok.size() == 160);
    TelnetFilter t = agreed();
    feed(t, sb90(ok));
    CHECK(t.linkReports() == 1);
    CHECK(t.link().tier == "SF7");

    // 161 bytes: dropped whole, even though its fields are valid; the previous report stands
    std::string big = "v=1 tier=SF9 bps=1758 " + std::string(161 - 22, 'x');
    REQUIRE(big.size() == 161);
    Out o = feed(t, sb90(big) + "after");
    CHECK(t.linkReports() == 1);
    CHECK(t.link().tier == "SF7");
    CHECK(o.app == "after");  // and the filter is back to data
    // far longer
    feed(t, sb90("v=1 tier=SF9 " + std::string(5000, 'y')));
    CHECK(t.linkReports() == 1);
    CHECK(t.link().tier == "SF7");
    // the parser itself refuses it too
    LinkInfo l;
    CHECK_FALSE(axis::net::parseLinkInfo(big, l));
    // a good one after the oversize ones still works
    feed(t, sb90("v=1 tier=SF10 bps=976"));
    CHECK(t.linkReports() == 2);
    CHECK(t.link().tier == "SF10");
}

// plan: link_sb_split_across_reads
TEST_CASE("a link report split across several reads is still read") {
    std::string whole = std::string("A") + sb90(kFull) + "B";
    for (size_t cut = 1; cut < whole.size(); cut++) {  // every split point
        TelnetFilter t = agreed();
        Out a = feed(t, whole.substr(0, cut));
        Out b = feed(t, whole.substr(cut));
        CHECK(a.app + b.app == "AB");
        CHECK(t.linkReports() == 1);
        CHECK(t.link().bps == 5469);
    }
    TelnetFilter t = agreed();  // one byte at a time
    std::string  app;
    for (char c : whole) app += feed(t, std::string(1, c)).app;
    CHECK(app == "AB");
    CHECK(t.linkReports() == 1);
    CHECK(t.link().caller == "Rev");
    // and through the stream, a few bytes per read
    FakeLine l;
    l.acceptLink(true);
    l.start();
    l.fromCaller = IAC_WILL90;
    l.available();
    std::string rest = whole, keys;
    while (!rest.empty()) {
        l.fromCaller = rest.substr(0, 7);
        rest.erase(0, 7);
        keys += l.takeInput();  // (the stream reads the socket again once its keys are used up)
    }
    CHECK(keys == "AB");
    LinkInfo got;
    l.now = 5000;
    CHECK(l.takeLink(got));
    CHECK(got.tier == "SF7");
    CHECK_FALSE(l.takeLink(got));  // taken once
}

// plan: link_never_reaches_caller_input
TEST_CASE("no byte of the link option or its report reaches the caller's input") {
    TelnetFilter t;  // modem_link on
    t.acceptLink(true);
    Out o = feed(t, "x" + IAC_WILL90 + "y" + sb90(kFull) + "z" + sb90(kFull) + IAC_WONT90 + "w");
    CHECK(o.app == "xyzw");
    TelnetFilter off;  // modem_link off: the WILL and any stray report are also invisible
    o = feed(off, "x" + IAC_WILL90 + "y" + sb90(kFull) + "z");
    CHECK(o.app == "xyz");
    // an oversize report too, and one with an escaped 0xFF inside
    TelnetFilter t2 = agreed();
    o = feed(t2, "a" + sb90(std::string(500, 'q')) + "b" + sb90("v=1 modem=a\xFF\xFF" "b") + "c");
    CHECK(o.app == "abc");
    // through the stream: takeInput and read see only the keys
    FakeLine l;
    l.acceptLink(true);
    l.start();
    l.fromCaller = "h" + IAC_WILL90 + "i" + sb90(kFull) + "!";
    CHECK(l.takeInput() == "hi!");
    CHECK(l.available() == 0);
}

// plan: lite_below_threshold
TEST_CASE("a radio caller at or below lite_below_bps gets the lite screens") {
    BbsConfig cfg;
    CHECK(cfg.liteBelowBps == 7031);
    Node n(1, info, cfg);
    CHECK_FALSE(n.lite());  // no report yet
    n.setLink(report("v=1 kind=radio tier=SF7 bps=5469"));
    CHECK(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF7 bps=7031"));  // the threshold is inclusive
    CHECK(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF7 bps=7032"));
    CHECK_FALSE(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF6 bps=9375"));
    CHECK_FALSE(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF9 bps=1758"));
    CHECK(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF9"));  // radio but no bps: unknown speed is not lite
    CHECK_FALSE(n.lite());
    n.setLink(report("v=1 kind=wifi tier=WIFI bps=100"));  // a wire is never lite
    CHECK_FALSE(n.lite());
    // the name mapping used for lite art
    CHECK(Node::liteName("MAIN.ANS") == "MAIN.LITE.ANS");
    CHECK(Node::liteName("MAIN.LITE.ANS").empty());
    // a custom threshold
    BbsConfig c2;
    c2.liteBelowBps = 1000;
    Node m(1, info, c2);
    m.setLink(report("v=1 kind=radio tier=SF10 bps=1000"));
    CHECK(m.lite());
    m.setLink(report("v=1 kind=radio tier=SF9 bps=1001"));
    CHECK_FALSE(m.lite());
}

// plan: lite_off_when_zero
TEST_CASE("lite_below_bps of 0 turns the lite screens off") {
    BbsConfig cfg;
    cfg.liteBelowBps = 0;
    Node n(1, info, cfg);
    n.setLink(report("v=1 kind=radio tier=SF12 bps=0"));
    CHECK_FALSE(n.lite());
    n.setLink(report("v=1 kind=radio tier=SF12 bps=293"));
    CHECK_FALSE(n.lite());
    CHECK(axis::bbs::parseConfig("lite_below_bps=0\n").liteBelowBps == 0);
    CHECK(axis::bbs::parseConfig("lite_below_bps=-5\n").liteBelowBps == 0);
}

// plan: wfc_shows_radio_label
TEST_CASE("the WFC shows a radio caller's RADIO SF label after the activity") {
    axis::bbs::WfcSnapshot s;
    LinkInfo               l = report(kFull);
    axis::bbs::WfcNode     n{2, "Rev", "doors", axis::net::linkLabel(l)};
    s.nodes.push_back({1, "", "", ""});
    s.nodes.push_back(n);
    CHECK(axis::bbs::expandArt("|NB", {}, axis::bbs::wfcValues(s)) == "2 Rev  doors  RADIO SF7 -81");
    CHECK(axis::bbs::expandArt("|NA", {}, axis::bbs::wfcValues(s)) == "1 waiting");  // nobody: no label
    s.nodes[1].link.clear();
    CHECK(axis::bbs::expandArt("|NB", {}, axis::bbs::wfcValues(s)) == "2 Rev  doors");
    CHECK(axis::net::linkLabel(report("v=1 kind=wifi tier=WIFI")) == "WIFI");  // a wire caller is not RADIO
    // a label change is a different node line, so the screen redraws
    axis::bbs::WfcNode a = n, b = n;
    b.link = "RADIO SF9";
    CHECK_FALSE(a == b);
}

// plan: wfc_codes_link
TEST_CASE("the WFC pipe codes RL RW RO RT RB RS RR RN RZ show the newest link") {
    axis::bbs::WfcSnapshot s;
    CHECK(axis::bbs::expandArt("[|RL|RW|RO|RT|RB|RS|RR|RN|RZ]", {}, axis::bbs::wfcValues(s)) == "[]");  // before any report
    s.radio = axis::bbs::wfcRadio(report(kFull), "");
    auto code = [&](const char* c) { return axis::bbs::expandArt(c, {}, axis::bbs::wfcValues(s)); };
    CHECK(code("|RL") == "RADIO SF7 -81");
    CHECK(code("|RW") == "Rev");  // the modem's own caller name when no handle
    CHECK(code("|RO") == "LFD3K-SM");
    CHECK(code("|RT") == "SF7");
    CHECK(code("|RB") == "5469");
    CHECK(code("|RS") == "7.2");
    CHECK(code("|RR") == "-81");
    CHECK(code("|RN") == "3");
    CHECK(code("|RZ") == "1.8");
    s.radio = axis::bbs::wfcRadio(report(kFull), "Spindle");  // a logged-in handle wins over the modem's caller name
    CHECK(code("|RW") == "Spindle");
    // negative SNR, lz off, unknown fields blank
    s.radio = axis::bbs::wfcRadio(report("v=1 kind=radio tier=SF9 snr=-0.5 lz=0"), "");
    CHECK(code("|RS") == "-0.5");
    CHECK(code("|RZ") == "off");
    CHECK(code("|RB").empty());
    CHECK(code("|RR").empty());
    CHECK(code("|RN").empty());
    CHECK(axis::bbs::wfcRadio(LinkInfo{}, "x").label.empty());
    // the numbers moving does not redraw the WFC; a different tier does
    axis::bbs::WfcSnapshot a, b;
    a.radio = axis::bbs::wfcRadio(report("v=1 modem=M caller=Rev tier=SF7 bps=5469 rssi=-80"), "");
    b.radio = axis::bbs::wfcRadio(report("v=1 modem=M caller=Rev tier=SF7 bps=5400 rssi=-90"), "");
    CHECK(a == b);
    b.radio = axis::bbs::wfcRadio(report("v=1 modem=M caller=Rev tier=SF9 bps=1758"), "");
    CHECK_FALSE(a == b);
}

// plan: config_modem_keys_roundtrip
TEST_CASE("the modem_link and lite_below_bps settings round trip and default sensibly") {
    BbsConfig d;
    CHECK(d.modemLink);
    CHECK(d.liteBelowBps == 7031);
    CHECK(axis::bbs::parseConfig("bbs_name=X\n").modemLink);
    CHECK(axis::bbs::parseConfig("bbs_name=X\n").liteBelowBps == 7031);
    BbsConfig c;
    c.modemLink = false;
    c.liteBelowBps = 1758;
    BbsConfig r = axis::bbs::parseConfig(axis::bbs::formatConfig(c));
    CHECK_FALSE(r.modemLink);
    CHECK(r.liteBelowBps == 1758);
    c.modemLink = true;
    c.liteBelowBps = 0;
    r = axis::bbs::parseConfig(axis::bbs::formatConfig(c));
    CHECK(r.modemLink);
    CHECK(r.liteBelowBps == 0);
    std::string ini = axis::bbs::formatConfig(c);
    CHECK(ini.find("modem_link=1") != std::string::npos);
    CHECK(ini.find("lite_below_bps=0") != std::string::npos);
    r = axis::bbs::parseConfig("modemLink=0\nliteBelowBps=500\n");  // the plan's camelCase spellings are read too
    CHECK_FALSE(r.modemLink);
    CHECK(r.liteBelowBps == 500);
}

namespace {
// A caller's connect as main.cpp does it: begin() at connect, the modem's report some time later, poll() every 20 ms.
std::string runCall(Node& n, uint32_t reportAtMs, const std::string& payload, uint32_t endMs, std::string* firstSplashByte = nullptr) {
    std::string all;
    n.begin(0);
    all += n.takeOutput();
    bool sent = payload.empty();
    for (uint32_t t = 20; t <= endMs; t += 20) {
        if (!sent && t >= reportAtMs) {
            n.setLink(report(payload));
            sent = true;
        }
        n.poll(t);
        std::string o = n.takeOutput();
        if (firstSplashByte && firstSplashByte->empty() && !o.empty()) *firstSplashByte = std::to_string(t);
        all += o;
    }
    return all;
}
}  // namespace

TEST_CASE("a radio caller at 3906 bps whose report lands 300 ms after connect never gets the splash, only the welcome") {
    BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    std::string out = runCall(n, 300, "v=1 kind=radio tier=SF10 bps=3906", 3000);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(out).find("Handle:") != std::string::npos);
    // no splash frame: the splash clears the screen and types 24 rows, the lite welcome is far smaller
    Node plain1(1, info, c);
    std::string splashCall = runCall(plain1, 0, "", 3000);
    CHECK(out.size() < splashCall.size());
    CHECK(out.find("ROTATO") == std::string::npos);
}

TEST_CASE("a caller with no link report gets nothing for the hold, then the splash") {
    BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    std::string first;
    std::string out = runCall(n, 0, "", 3000, &first);
    CHECK(n.state() == Node::State::Splash);  // still playing at 3 s
    CHECK(out.find("Handle:") == std::string::npos);
    CHECK(first == "1000");  // silent until the hold is over
}

TEST_CASE("a key during the splash hold skips to the welcome") {
    BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    n.begin(0);
    CHECK(n.takeOutput().empty());
    n.input((const uint8_t*)" ", 1, 500);
    n.poll(520);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
}

TEST_CASE("a link report that makes the caller lite in the middle of the splash ends it and shows the welcome") {
    BbsConfig c;
    c.loginSplash = true;
    Node n(1, info, c);
    std::string out = runCall(n, 2500, "v=1 kind=radio tier=SF10 bps=3906", 2400);
    CHECK(n.state() == Node::State::Splash);  // playing, caller not yet known to be slow
    n.takeOutput();
    n.setLink(report("v=1 kind=radio tier=SF10 bps=3906"));
    n.poll(2520);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
    n.poll(2540);  // nothing of the splash comes after
    CHECK(n.takeOutput().empty());
}

TEST_CASE("with the login splash off, the welcome comes at connect whatever the link") {
    BbsConfig c;
    c.loginSplash = false;
    Node n(1, info, c);
    n.begin(0);
    CHECK(n.state() == Node::State::Login);
    CHECK(plain(n.takeOutput()).find("Handle:") != std::string::npos);
}
