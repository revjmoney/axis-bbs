// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "doctest.h"
#include "bbs/eventlog.h"
#include "bbs/sysop_menu.h"

using namespace axis::bbs;

namespace {
// the centre of a button, wherever the layout puts it
void tapButton(SysopMenu& m, const char* label, SysopMenu::Action* got = nullptr) {
    for (auto& b : m.buttons())
        if (b.label == label) {
            auto a = m.tap(b.x + b.w / 2, b.y + b.h / 2);
            if (got) *got = a;
            return;
        }
    FAIL("no button " << label);
}
SysopData data() {
    SysopData d;
    d.cfg.sysopHandle = "Rev";
    d.cfg.location = "The Workshop";
    d.info.storage = "SD card 15193 MB";
    d.info.freeHeap = 35000;
    d.log.add("10:01 call from 192.168.1.20");
    d.log.add("10:02 Rev opened LORD");
    d.areaFiles = std::vector<int>(d.cfg.areas.size(), 0);
    d.areaFiles[0] = 3;
    return d;
}
}  // namespace

TEST_CASE("event log keeps the newest lines") {
    EventLog log(3);
    for (int i = 1; i <= 5; i++) log.add("line " + std::to_string(i));
    REQUIRE(log.lines().size() == 3);
    CHECK(log.lines().front() == "line 3");
    CHECK(log.lines().back() == "line 5");
}

TEST_CASE("sysop menu opens on the main screen with the six sections") {
    SysopMenu m;
    m.open();
    CHECK(m.screen() == SysopMenu::Screen::Main);
    int found = 0;
    for (const char* l : {"BBS SETUP", "USERS", "FILES", "MESSAGES", "LOGS", "SYSTEM"})
        for (auto& b : m.buttons()) found += b.label == l;
    CHECK(found == 6);
}

TEST_CASE("tapping a section opens it and BACK returns") {
    SysopMenu m;
    m.open();
    tapButton(m, "LOGS");
    CHECK(m.screen() == SysopMenu::Screen::Logs);
    tapButton(m, "BACK");
    CHECK(m.screen() == SysopMenu::Screen::Main);
}

TEST_CASE("EXIT on the main screen closes the menu") {
    SysopMenu          m;
    SysopMenu::Action  a = SysopMenu::Action::None;
    m.open();
    tapButton(m, "EXIT", &a);
    CHECK(a == SysopMenu::Action::Close);
    CHECK_FALSE(m.isOpen());
}

TEST_CASE("formatting the SD card needs a second tap to confirm") {
    SysopMenu         m;
    SysopMenu::Action a = SysopMenu::Action::None;
    m.open();
    tapButton(m, "SYSTEM");
    tapButton(m, "FORMAT SD", &a);
    CHECK(a == SysopMenu::Action::None);
    CHECK(m.screen() == SysopMenu::Screen::ConfirmFormat);
    tapButton(m, "YES, ERASE IT", &a);
    CHECK(a == SysopMenu::Action::FormatSd);
}

TEST_CASE("cancelling the format goes back to System") {
    SysopMenu m;
    m.open();
    tapButton(m, "SYSTEM");
    tapButton(m, "FORMAT SD");
    tapButton(m, "CANCEL");
    CHECK(m.screen() == SysopMenu::Screen::System);
}

TEST_CASE("taps outside every button do nothing") {
    SysopMenu m;
    m.open();
    CHECK(m.tap(1, 1) == SysopMenu::Action::None);
    CHECK(m.screen() == SysopMenu::Screen::Main);
}

TEST_CASE("setup screen shows the settings") {
    SysopMenu m;
    m.open();
    tapButton(m, "BBS SETUP");
    std::string all;
    for (auto& l : m.lines(data())) all += l + "\n";
    CHECK(all.find("Rev") != std::string::npos);
    CHECK(all.find("The Workshop") != std::string::npos);
}

TEST_CASE("files screen lists the areas with their file counts") {
    SysopMenu m;
    m.open();
    tapButton(m, "FILES");
    auto lines = m.lines(data());
    REQUIRE(!lines.empty());
    std::string all;
    for (auto& l : lines) all += l + "\n";
    CHECK(all.find("RotatoCAM: Pro") != std::string::npos);
    tapButton(m, "MORE");  // 17 areas: the music areas are on page two
    all.clear();
    for (auto& l : m.lines(data())) all += l + "\n";
    CHECK(all.find("Music: Rev J Money") != std::string::npos);
}

TEST_CASE("files screen pages through the areas") {
    SysopMenu m;
    m.open();
    tapButton(m, "FILES");
    auto first = m.lines(data());
    tapButton(m, "MORE");
    auto second = m.lines(data());
    CHECK(first != second);
}

TEST_CASE("logs screen shows the newest events") {
    SysopMenu m;
    m.open();
    tapButton(m, "LOGS");
    auto lines = m.lines(data());
    REQUIRE(!lines.empty());
    CHECK(lines.back().find("opened LORD") != std::string::npos);
}

namespace {
SysopData withUsers() {
    SysopData d = data();
    User      a, b;
    a.handle = "Validated Guy";
    a.status = User::Status::Validated;
    b.handle = "Waiting Kid";
    b.location = "Conway, SC";
    b.answers = {{"Do you own a CNC machine", "no"}, {"Why should we let you in", "I want to learn"}};
    d.users.add(a);
    d.users.add(b);
    return d;
}
}  // namespace

TEST_CASE("users screen lists pending callers first") {
    SysopMenu m;
    m.open();
    tapButton(m, "USERS");
    auto lines = m.lines(withUsers());
    REQUIRE(lines.size() >= 2);
    CHECK(lines[0].find("Waiting Kid") != std::string::npos);
    CHECK(lines[0].find("pending") != std::string::npos);
}

TEST_CASE("tapping a caller shows their application and validate returns their id") {
    SysopMenu m;
    SysopData d = withUsers();
    m.open();
    tapButton(m, "USERS");
    m.lines(d);                  // the screen is drawn before it is tapped
    m.tap(100, 32 + 8);          // the first row
    CHECK(m.screen() == SysopMenu::Screen::UserDetail);
    std::string all;
    for (auto& l : m.lines(d)) all += l + "\n";
    CHECK(all.find("Conway, SC") != std::string::npos);
    CHECK(all.find("I want to learn") != std::string::npos);
    SysopMenu::Action a = SysopMenu::Action::None;
    tapButton(m, "VALIDATE", &a);
    CHECK(a == SysopMenu::Action::ValidateUser);
    CHECK(m.selectedUser() == 2);
}

TEST_CASE("deny also returns the caller's id") {
    SysopMenu m;
    SysopData d = withUsers();
    m.open();
    tapButton(m, "USERS");
    m.lines(d);
    m.tap(100, 32 + 8);
    m.lines(d);
    SysopMenu::Action a = SysopMenu::Action::None;
    tapButton(m, "DENY", &a);
    CHECK(a == SysopMenu::Action::DenyUser);
    CHECK(m.selectedUser() == 2);
}

TEST_CASE("a caller's ACCESS screen shows their permissions and toggles each one") {
    SysopMenu m;
    SysopData d = withUsers();
    m.open();
    tapButton(m, "USERS");
    m.lines(d);
    m.tap(100, 32 + 8);
    tapButton(m, "ACCESS");
    CHECK(m.screen() == SysopMenu::Screen::Access);
    std::string all;
    for (auto& l : m.lines(d)) all += l + "\n";
    CHECK(all.find("files") != std::string::npos);
    CHECK(all.find("doors") != std::string::npos);
    CHECK(all.find("downloads") != std::string::npos);
    SysopMenu::Action a = SysopMenu::Action::None;
    tapButton(m, "FILES", &a);
    CHECK(a == SysopMenu::Action::ToggleFiles);
    tapButton(m, "MSGS", &a);
    CHECK(a == SysopMenu::Action::ToggleMessages);
    tapButton(m, "DOORS", &a);
    CHECK(a == SysopMenu::Action::ToggleDoors);
    tapButton(m, "FREE DL", &a);
    CHECK(a == SysopMenu::Action::ToggleFreeDownloads);
    tapButton(m, "SYSOP", &a);
    CHECK(a == SysopMenu::Action::ToggleSysop);
    CHECK(all.find("sysop") != std::string::npos);
    CHECK(m.selectedUser() == 2);
    tapButton(m, "BACK");
    CHECK(m.screen() == SysopMenu::Screen::UserDetail);
}

TEST_CASE("BBS SETUP shows the theme and its THEME button asks to switch it") {
    SysopMenu m;
    m.open();
    tapButton(m, "BBS SETUP");
    std::string all;
    for (auto& l : m.lines(data())) all += l + "\n";
    CHECK(all.find("theme") != std::string::npos);
    CHECK(all.find("axis") != std::string::npos);
    tapButton(m, "THEME");
    CHECK(m.screen() == SysopMenu::Screen::Theme);
    std::string looks;
    for (auto& l : m.lines(data())) looks += l + "\n";
    CHECK(looks.find("Axis") != std::string::npos);
    CHECK(looks.find("Oblivion") != std::string::npos);
    CHECK(looks.find("Iniquity") != std::string::npos);
    SysopMenu::Action a = SysopMenu::Action::None;
    tapButton(m, "OBLIVION", &a);
    CHECK(a == SysopMenu::Action::SetTheme);
    CHECK(m.chosenTheme() == "oblivion");
    CHECK(m.screen() == SysopMenu::Screen::Setup);
}
