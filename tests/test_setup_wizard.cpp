// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/password.h"
#include "bbs/setup_wizard.h"

using namespace axis::bbs;

static uint32_t rs = 7;
static uint32_t rng() { return rs = rs * 1664525u + 1013904223u; }

static void type(SetupWizard& w, const std::string& s) { w.input((const uint8_t*)s.data(), s.size()); }

// handle and password, then Enter through every other field
static SetupWizard defaultsRun() {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\r");
    type(w, "secret1\r");
    type(w, "secret1\r");
    for (int i = 0; i < 15; i++) type(w, "\r");  // (11 settings + 3 optional No-IP questions + the optional NUP)
    return w;
}

TEST_CASE("wizard opens with a setup title and asks for the sysop handle") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    std::string out = w.takeOutput();
    CHECK(out.find("setup") != std::string::npos);
    CHECK(out.find("Sysop handle") != std::string::npos);
}

TEST_CASE("wizard accepting all defaults after handle and password finishes with defaults") {
    SetupWizard w = defaultsRun();
    std::string out = w.takeOutput();
    CHECK(out.find("Save these settings") != std::string::npos);
    CHECK_FALSE(w.finished());
    type(w, "y");
    CHECK(w.finished());
    const BbsConfig& c = w.result();
    CHECK(c.setupDone);
    CHECK(c.sysopHandle == "Rev");
    CHECK(c.bbsName == "Axis Underground CNC");
    CHECK(c.boardName == "axis");
    CHECK(c.telnetPort == 23);
    CHECK(c.timezone == "EASTERN");
    CHECK(c.ftnAddress == "99:1/0");
    CHECK(c.idleMinutes == 5);
}

TEST_CASE("wizard stored password verifies with checkPassword") {
    SetupWizard w = defaultsRun();
    type(w, "Y");
    const BbsConfig& c = w.result();
    CHECK(c.sysopSalt.size() == 32);
    CHECK(checkPassword(c.sysopSalt, c.sysopHash, "secret1"));
    CHECK_FALSE(checkPassword(c.sysopSalt, c.sysopHash, "secret2"));
}

TEST_CASE("wizard password echo is stars and the summary hides it") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\r");
    w.takeOutput();
    type(w, "abcd");
    CHECK(w.takeOutput() == "****");
    type(w, "\rabcd\r");
    for (int i = 0; i < 15; i++) type(w, "\r");  // (11 settings + 3 optional No-IP questions + the optional NUP)
    CHECK(w.takeOutput().find("abcd") == std::string::npos);
}

TEST_CASE("wizard empty handle is rejected") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    w.takeOutput();
    type(w, "\r");
    std::string out = w.takeOutput();
    CHECK(out.find("Sysop handle") != std::string::npos);
    CHECK(w.validate("sysop_handle", "").size() > 0);
}

TEST_CASE("wizard password confirm mismatch restarts the password") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\rsecret1\rsecret2\r");
    std::string out = w.takeOutput();
    CHECK(out.find("did not match") != std::string::npos);
    type(w, "good1\rgood1\r");
    for (int i = 0; i < 15; i++) type(w, "\r");  // (11 settings + 3 optional No-IP questions + the optional NUP)
    type(w, "y");
    CHECK(checkPassword(w.result().sysopSalt, w.result().sysopHash, "good1"));
}

TEST_CASE("wizard port 70000 is rejected with a reason and asked again") {
    CHECK(SetupWizard(BbsConfig{}, rng).validate("telnet_port", "70000").size() > 0);
    CHECK(SetupWizard(BbsConfig{}, rng).validate("telnet_port", "0").size() > 0);
    CHECK(SetupWizard(BbsConfig{}, rng).validate("telnet_port", "2323").empty());
}

TEST_CASE("wizard ftn abc is rejected") {
    SetupWizard w(BbsConfig{}, rng);
    CHECK(w.validate("ftn_address", "abc").size() > 0);
    CHECK(w.validate("ftn_address", "99:1/0").empty());
    CHECK(w.validate("ftn_address", "21:4/158.1").empty());
}

TEST_CASE("wizard board name must be a valid local name") {
    SetupWizard w(BbsConfig{}, rng);
    CHECK(w.validate("board_name", "axis-2").empty());
    CHECK(w.validate("board_name", "Axis Board").size() > 0);
}

TEST_CASE("wizard timezone by number") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\rsecret1\rsecret1\r");
    // real name, bbs name, location, board name, public host, 3 No-IP questions, port
    for (int i = 0; i < 9; i++) type(w, "\r");
    CHECK(w.takeOutput().find("PACIFIC") != std::string::npos);  // the list is shown
    type(w, "5\r");
    for (int i = 0; i < 5; i++) type(w, "\r");
    type(w, "y");
    CHECK(w.result().timezone == "PACIFIC");
}

TEST_CASE("wizard N at the summary cancels and nothing is applied") {
    SetupWizard w = defaultsRun();
    CHECK_FALSE(w.result().setupDone);
    type(w, "n");
    CHECK(w.cancelled());
    CHECK_FALSE(w.finished());
    CHECK_FALSE(w.result().setupDone);
}

TEST_CASE("wizard set and fields support the touch path") {
    SetupWizard w(BbsConfig{}, rng);
    CHECK(w.fields().size() == 18);
    CHECK(w.defaultFor("bbs_name") == "Axis Underground CNC");
    w.set("bbs_name", "Shop BBS");
    CHECK(w.defaultFor("bbs_name") == "Shop BBS");
}

TEST_CASE("the wizard asks for optional No-IP dynamic DNS and keeps it out of the shared settings") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\rsecret1\rsecret1\r");
    for (int i = 0; i < 4; i++) type(w, "\r");  // real name, BBS name, location, board name
    type(w, "\r");                                // public hostname: empty, so the No-IP one becomes it
    std::string out = w.takeOutput();
    CHECK(out.find("No-IP") != std::string::npos);
    type(w, "axisbbs.ddns.net\r");
    type(w, "revjmoney\r");
    type(w, "noippass1\r");
    for (int i = 0; i < 7; i++) type(w, "\r");
    out = w.takeOutput();
    CHECK(out.find("axisbbs.ddns.net") != std::string::npos);  // in the summary
    CHECK(out.find("noippass1") == std::string::npos);         // never echoed
    type(w, "y");
    REQUIRE(w.finished());
    CHECK(w.noip().host == "axisbbs.ddns.net");
    CHECK(w.noip().user == "revjmoney");
    CHECK(w.noip().pass == "noippass1");
    CHECK(w.result().publicHost == "axisbbs.ddns.net");
    CHECK(formatConfig(w.result()).find("noippass1") == std::string::npos);  // the password never goes in axis.ini
}

TEST_CASE("a No-IP DDNS key updater name is never used as the public hostname") {
    SetupWizard w(BbsConfig{}, rng);
    w.set("public_host", "myboard.hopto.org");
    w.set("noip_host", "all.ddnskey.com");
    CHECK(w.result().publicHost == "myboard.hopto.org");
    SetupWizard w2(BbsConfig{}, rng);
    w2.set("noip_host", "all.ddnskey.com");
    CHECK(w2.result().publicHost.empty());
    CHECK(w2.noip().host == "all.ddnskey.com");
}

TEST_CASE("skipping the No-IP questions leaves dynamic DNS off") {
    SetupWizard w = defaultsRun();
    type(w, "y");
    CHECK(w.noip().host.empty());
}

TEST_CASE("the wizard's last question sets a new user password, kept only as a hash") {
    SetupWizard w(BbsConfig{}, rng);
    w.begin();
    type(w, "Rev\rsecret1\rsecret1\r");
    for (int i = 0; i < 14; i++) type(w, "\r");  // everything up to the NUP
    CHECK(w.takeOutput().find("New user password") != std::string::npos);
    type(w, "elite\r");
    std::string out = w.takeOutput();
    CHECK(out.find("elite") == std::string::npos);  // never echoed, not in the summary
    type(w, "y");
    REQUIRE(w.finished());
    CHECK(checkPassword(w.result().nupSalt, w.result().nupHash, "elite"));
    CHECK(formatConfig(w.result()).find("elite") == std::string::npos);
}

TEST_CASE("pressing Enter at the NUP question keeps the one already set") {
    BbsConfig c;
    c.nupSalt = "abc";
    c.nupHash = "def";
    SetupWizard w(c, rng);
    w.begin();
    type(w, "Rev\rsecret1\rsecret1\r");
    for (int i = 0; i < 15; i++) type(w, "\r");
    type(w, "y");
    CHECK(w.result().nupHash == "def");
}
