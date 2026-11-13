// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/features.h"

// Built with no switches (AXIS_OFF empty) every selectable feature (nets included) is on and
// the reserved ones are off. A build with AXIS_OFF set skips this check.
#ifndef AXIS_TEST_SWITCHED
TEST_CASE("features: defaults") {
    CHECK(AXIS_LORD == 1);
    CHECK(AXIS_SCRIPTS == 1);
    CHECK(AXIS_DOSPROMPT == 1);
    CHECK(AXIS_FILES == 1);
    CHECK(AXIS_ANSIVIEW == 1);
    CHECK(AXIS_TRANSFERS == 1);
    CHECK(AXIS_CLUSTER == 1);
    CHECK(AXIS_MODEMLINK == 1);
    CHECK(AXIS_WEB == 1);
    CHECK(AXIS_NETS == 1);
    CHECK(AXIS_GPIO == 0);
}
#endif

// ---- doors (Task 3): a door left out of the build is never offered, and its saved line is kept as written.
#include <map>
#include <string>
#include <vector>

#include "bbs/config.h"
#include "bbs/node.h"

namespace {
const char* kSavedDoors = "door1=lord|Legend of the Red Dragon|*LORD||none|1\n"
                          "door2=ansiview|ANSI Pack Viewer|*ANSIVIEW|ansi|none|1\n"
                          "door3=script|Scripts|*SCRIPT||none|1\n";

std::vector<std::string> doorLines(const std::string& ini) {
    std::vector<std::string> v;
    for (size_t at = 0; at < ini.size();) {
        size_t nl = ini.find('\n', at);
        std::string line = ini.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        if (line.rfind("door", 0) == 0 && line.size() > 4 && line[4] >= '0' && line[4] <= '9') v.push_back(line);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return v;
}

bool offers(const axis::bbs::Node& n, const char* exe) {
    for (const auto& d : n.doorChoices())
        if (d.exe == exe) return true;
    return false;
}
}  // namespace

TEST_CASE("features: the doors offered are the ones built in") {
    axis::bbs::BbsConfig c = axis::bbs::parseConfig(kSavedDoors);
    axis::bbs::Node      n(1, [] { return axis::bbs::SysInfo{}; }, c);
    CHECK(offers(n, "*LORD") == (AXIS_LORD == 1));
    CHECK(offers(n, "*SCRIPT") == (AXIS_SCRIPTS == 1));
    CHECK(offers(n, "*ANSIVIEW") == (AXIS_ANSIVIEW == 1));
    CHECK(axis::bbs::doorBuiltIn("*LORD") == (AXIS_LORD == 1));
    CHECK(axis::bbs::doorBuiltIn("*lord") == (AXIS_LORD == 1));
    CHECK(axis::bbs::doorBuiltIn("*SCRIPT") == (AXIS_SCRIPTS == 1));
    CHECK(axis::bbs::doorBuiltIn("*ANSIVIEW") == (AXIS_ANSIVIEW == 1));
    CHECK_FALSE(axis::bbs::doorBuiltIn("LORD.EXE"));
}

TEST_CASE("features: saved door lines round trip unchanged, built in or not") {
    std::vector<std::string> before = doorLines(kSavedDoors);
    std::vector<std::string> after = doorLines(axis::bbs::formatConfig(axis::bbs::parseConfig(kSavedDoors)));
    CHECK(after == before);
}

TEST_CASE("features: a saved DOS prompt row") {
    const char* ini = "door1=lord|Legend of the Red Dragon|*LORD||none|1\n"
                      "door2=dos|DOS Prompt|*DOSSHELL|dosbox|none|1\n"
                      "door3=script|Scripts|*SCRIPT||none|1\n";
    axis::bbs::BbsConfig c = axis::bbs::parseConfig(ini);
    std::vector<std::string> after = doorLines(axis::bbs::formatConfig(c));
#if AXIS_DOSPROMPT
    CHECK(after.size() == 2);  // the full build drops it, as it always has (the door is not in the firmware)
#else
    CHECK(after == doorLines(ini));  // a build without the DOS prompt keeps the row exactly as saved
#endif
    axis::bbs::Node n(1, [] { return axis::bbs::SysInfo{}; }, c);
    CHECK_FALSE(offers(n, "*DOSSHELL"));
}

TEST_CASE("features: a menu file line for a door or script this build leaves out is not shown") {
    axis::bbs::BbsConfig c = axis::bbs::parseConfig(kSavedDoors);
    c.loginSplash = false;
    std::map<std::string, std::string> fs = {
        {"MAIN.MNU", "cmd=L|DOOR:lord|*|0|\ncmd=S|SCRIPT:hello|*|0|\ncmd=A|DOOR:ansiview|*|0|\n"
                     "bar=5,10,96,7,L, (L) Red Dragon\nbar=5,11,96,7,S, (S) Hello Script\nbar=5,12,96,7,A, (A) ANSI Pack Viewer\n"}};
    axis::bbs::Node n(1, [] { return axis::bbs::SysInfo{}; }, c);
    n.setMenus([&fs](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    n.begin(0);
    n.input((const uint8_t*)"Rev\r", 4, 1000);
    if (n.state() == axis::bbs::Node::State::SeqPause) n.input((const uint8_t*)" ", 1, 1000);
    std::string out = n.takeOutput();
    CHECK((out.find("Red Dragon") != std::string::npos) == (AXIS_LORD == 1));
    CHECK((out.find("Hello Script") != std::string::npos) == (AXIS_SCRIPTS == 1));
    CHECK((out.find("ANSI Pack Viewer") != std::string::npos) == (AXIS_ANSIVIEW == 1));
}

// ---- file areas, transfers and the ANSI viewer (Task 4): left out of the build, they are never offered,
// and the saved settings lines (files_on, the viewer's door row) are kept exactly as written.
#include <regex>

#include "bbs/filebrowser.h"
#include "cluster/remotefiles.h"

namespace {
std::string noColor(const std::string& s) { return std::regex_replace(s, std::regex("\x1b\[[0-9;]*[A-Za-z]"), ""); }

void typeIn(axis::bbs::Node& n, const std::string& s) { n.input((const uint8_t*)s.data(), s.size(), 1000); }

struct TwoFiles : axis::bbs::FileLibrary {
    std::vector<axis::bbs::FileEntry> list(const std::string&) override {
        return {{"README.TXT", 12, "What it is"}, {"AXIS.ZIP", 900, "The firmware"}};
    }
    bool read(const std::string&, const std::string& name, std::string& out, size_t) override {
        if (name != "README.TXT") return false;
        out = "Axis Micro BBS\n";
        return true;
    }
};
}  // namespace

TEST_CASE("features: the main menu offers the file areas only when they are built in") {
    TwoFiles             lib;
    axis::bbs::BbsConfig c;
    c.areas = {{"axis", "Axis Micro BBS Firmware"}};
    axis::bbs::Node n(1, [] { return axis::bbs::SysInfo{}; }, c, &lib);
    n.begin(0);
    typeIn(n, "Rev\r");
    std::string menu = noColor(n.takeOutput());
    CHECK((menu.find("[F]") != std::string::npos) == (AXIS_FILES == 1));
    CHECK((menu.find("file areas") != std::string::npos) == (AXIS_FILES == 1));
    typeIn(n, "f");
    std::string out = noColor(n.takeOutput());
#if AXIS_FILES
    CHECK(n.state() == axis::bbs::Node::State::Files);
#else
    CHECK(n.state() == axis::bbs::Node::State::Menu);
    CHECK(out.find("not in this firmware") != std::string::npos);
#endif
    CHECK(axis::bbs::filesOffered(c) == (AXIS_FILES == 1));
}

TEST_CASE("features: a menu file's file commands are not shown when the file areas are left out") {
    axis::bbs::BbsConfig c;
    c.loginSplash = false;
    std::map<std::string, std::string> fs = {
        {"MAIN.MNU", "cmd=F|LISTFILES|*|0|\ncmd=O|ONELINER|*|0|\nbar=5,10,96,7,F, (F) File Listing\nbar=5,11,96,7,O, (O) Oneliners\n"}};
    axis::bbs::Node n(1, [] { return axis::bbs::SysInfo{}; }, c);
    n.setMenus([&fs](const std::string& f) { return fs.count(f) ? fs[f] : std::string(); });
    n.begin(0);
    typeIn(n, "Rev\r");
    if (n.state() == axis::bbs::Node::State::SeqPause) typeIn(n, " ");
    std::string out = n.takeOutput();
    CHECK((out.find("File Listing") != std::string::npos) == (AXIS_FILES == 1));
    CHECK(out.find("Oneliners") != std::string::npos);
}

TEST_CASE("features: the saved files_on line and the viewer's door row round trip, built in or not") {
    const char* ini = "files_on=1\n"
                      "door1=ansiview|ANSI Pack Viewer|*ANSIVIEW|ansi|none|1\n";
    std::string out = axis::bbs::formatConfig(axis::bbs::parseConfig(ini));
    CHECK(out.find("files_on=1\n") != std::string::npos);
    CHECK(out.find("door1=ansiview|ANSI Pack Viewer|*ANSIVIEW|ansi|none|1\n") != std::string::npos);
}

TEST_CASE("features: the ANSI viewer door is offered only when it is built in") {
    axis::bbs::BbsConfig c = axis::bbs::parseConfig(kSavedDoors);
    axis::bbs::Node      n(1, [] { return axis::bbs::SysInfo{}; }, c);
    bool viewerIn = false;
    for (const auto& d : n.doorChoices())
        if (d.exe == "*ANSIVIEW") viewerIn = true;
    CHECK(viewerIn == (AXIS_ANSIVIEW == 1));
    CHECK(axis::bbs::doorBuiltIn("*ANSIVIEW") == (AXIS_ANSIVIEW == 1));
    CHECK(axis::bbs::doorBuiltIn("*ansiview") == (AXIS_ANSIVIEW == 1));
    CHECK(axis::bbs::doorBuiltIn("*PACKS") == (AXIS_ANSIVIEW == 1));  // any other '*' door is the viewer
    CHECK(axis::bbs::doorLeftOut("*ANSIVIEW") == (AXIS_ANSIVIEW == 0));
}

#if AXIS_FILES
TEST_CASE("features: the file list offers downloads and uploads only when transfers are built in") {
    TwoFiles                              lib;
    std::vector<axis::bbs::FileArea>      areas = {{"axis", "Axis Micro BBS Firmware"}};
    std::string                           out;
    axis::bbs::FileBrowser                b(areas, lib, out);
    b.setLightbars(false);
    b.begin();
    b.key('1');
    b.key('\r');
    std::string list = noColor(out);
    CHECK((list.find("ownload") != std::string::npos) == (AXIS_TRANSFERS == 1));
    CHECK((list.find("pload") != std::string::npos) == (AXIS_TRANSFERS == 1));
    out.clear();
    b.key('d');
    CHECK((noColor(out).find("File name") != std::string::npos) == (AXIS_TRANSFERS == 1));
    if (AXIS_TRANSFERS) b.key('\r');  // (an empty name: back to the list)
    out.clear();
    b.key('u');
    CHECK((noColor(out).find("Describe it") != std::string::npos) == (AXIS_TRANSFERS == 1));
    CHECK_FALSE(b.wantsTransfer());
}
#endif

#if AXIS_FILES
TEST_CASE("features: a follower whose main has no file areas gets nothing, not a crash") {
    // a main without the file areas in its firmware answers every file request "0"
    axis::cluster::RemoteFileLibrary remote([](const std::string&, std::string& reply) {
        reply = "0";
        return true;
    });
    CHECK(remote.list("axis").empty());
    std::string body;
    CHECK_FALSE(remote.read("axis", "README.TXT", body, 1024));
    CHECK(remote.open("axis", "README.TXT") == nullptr);
    CHECK(remote.create("axis", "NEW.ZIP") == nullptr);
    CHECK_FALSE(remote.describe("axis", "NEW.ZIP", "about"));
    CHECK_FALSE(remote.remove("axis", "NEW.ZIP"));
    CHECK_FALSE(remote.rename("axis", "A.ZIP", "B.ZIP"));
}
#endif

// ---- the cluster, the modem link and the web jobs (Task 5)
#include "bbs/sdnotice.h"
#include "bbs/setup_wizard.h"
#include "bbs/wfc.h"
#include "cluster/cluster.h"
#include "net/telnet.h"

#if !AXIS_CLUSTER
TEST_CASE("features: built without the cluster the board runs alone") {
    axis::cluster::Cluster c;  // the stand-in main.cpp compiles against (its pointer is always null)
    CHECK(c.role() == axis::cluster::Role::Off);
    CHECK(c.nodes().empty());
    CHECK(c.numbers().empty());
    CHECK(c.cardBoards().empty());
    CHECK_FALSE(c.mainHasCard());
    std::string reply;
    CHECK_FALSE(c.request("V\x1f@WHO", reply));
    CHECK_FALSE(c.requestTo("board", "F", reply));
    CHECK(reply.empty());
}
#endif

TEST_CASE("features: the modem's WILL 90 is taken only with the modem link built in") {
    const std::string will90("\xFF\xFB\x5A", 3), do90("\xFF\xFD\x5A", 3), dont90("\xFF\xFE\x5A", 3);
    axis::net::TelnetFilter t;
    t.acceptLink(true);  // modem_link=1 in the settings
    std::string app, reply;
    t.fromNet((const uint8_t*)will90.data(), will90.size(), app, reply);
    CHECK(reply == (AXIS_MODEMLINK ? do90 : dont90));  // off: refused like any option we do not use
    CHECK(t.linkAgreed() == (AXIS_MODEMLINK == 1));
    const std::string report = std::string("\xFF\xFA\x5A", 3) + "v=1 modem=LFD3K-SM kind=radio tier=SF7 bps=5469" + std::string("\xFF\xF0", 2);
    app.clear();
    reply.clear();
    t.fromNet((const uint8_t*)report.data(), report.size(), app, reply);
    CHECK(t.linkReports() == (AXIS_MODEMLINK ? 1u : 0u));
    CHECK(app.empty());  // a plain telnet caller either way: no byte of it reaches the BBS
}

TEST_CASE("features: the WFC radio codes show the link only with the modem link built in") {
    axis::bbs::WfcSnapshot s;
    s.radio.label = "RADIO SF7";
    s.radio.tier = "SF7";
    auto v = axis::bbs::wfcValues(s);
    CHECK(v("RL") == (AXIS_MODEMLINK ? "RADIO SF7" : ""));
    CHECK(v("RT") == (AXIS_MODEMLINK ? "SF7" : ""));
}

TEST_CASE("features: the setup wizard asks for No-IP only with the web jobs built in") {
    axis::bbs::SetupWizard w(axis::bbs::BbsConfig{}, [] { return 7u; });
    bool asked = false;
    for (const auto& f : w.fields()) asked = asked || f.key == std::string("noip_host");
    CHECK(asked == (AXIS_WEB == 1));
}

TEST_CASE("features: the SD card notice") {
    using axis::bbs::sdNoticeShown;
    // a feature that needs the card built in, no card, running alone (or a follower of a main with no card): the notice
    CHECK(sdNoticeShown(false, false, true));
    // the same board as a follower of a main that has a card: none, the main serves it
    CHECK_FALSE(sdNoticeShown(false, true, true));
    // a card here: none
    CHECK_FALSE(sdNoticeShown(true, false, true));
    // built with no feature that needs the card (the no-sd preset): none, card or not
    CHECK_FALSE(sdNoticeShown(false, false, false));
    // this build's own answer follows its switches (the "needs_sd" features of installer/features.json)
    const bool needsSd = AXIS_LORD || AXIS_SCRIPTS || AXIS_FILES || AXIS_ANSIVIEW || AXIS_TRANSFERS || AXIS_NETS;
    CHECK(sdNoticeShown(false, false) == needsSd);
    CHECK(std::string(axis::bbs::kSdNotice) == "YOU NEED AN SD CARD FOR FULL FUNCTIONS");
}

TEST_CASE("features: the telnet WFC draws the SD card notice when the snapshot has it") {
    for (bool on : {false, true}) {
        axis::bbs::BbsConfig c;
        c.sysopHandle = "Rev";
        axis::bbs::Node n(1, [] { return axis::bbs::SysInfo{}; }, c);
        n.setWfcSnapshot([on] {
            axis::bbs::WfcSnapshot s;
            s.bbsName = "AXIS";
            if (on) s.notice = axis::bbs::kSdNotice;
            return s;
        });
        n.beginLocal(0);
        n.openWfc(0);
        CHECK((n.takeOutput().find("YOU NEED AN SD CARD FOR FULL FUNCTIONS") != std::string::npos) == on);
    }
}
