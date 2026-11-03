// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Only made-up networks here: tests never touch Rev's real ones.
#include <string>
#include <vector>

#include "doctest.h"
#include "bbs/touchkeyboard.h"
#include "bbs/wifipicker.h"

using namespace axis::bbs;

namespace {
void tapKey(TouchKeyboard& k, const std::string& label) {
    for (auto& key : k.keys())
        if (key.label == label) {
            k.tap(key.x + key.w / 2, key.y + key.h / 2);
            return;
        }
    FAIL("no key " << label);
}
void typeOn(TouchKeyboard& k, const std::string& s) {
    for (char c : s) {
        bool upper = c >= 'A' && c <= 'Z';
        if (upper != k.shifted()) tapKey(k, "SHIFT");
        std::string label(1, upper ? (char)(c - 'A' + 'a') : c);
        if (upper) label = std::string(1, c);
        tapKey(k, label);
    }
}
void tapButton(WifiPicker& p, const std::string& label) {
    for (auto& b : p.buttons())
        if (b.label == label) {
            p.tap(b.x + b.w / 2, b.y + b.h / 2);
            return;
        }
    FAIL("no button " << label);
}
// on the board every tap goes through the picker, keyboard screens included
void pickerKey(WifiPicker& p, const std::string& label) {
    for (auto& key : p.keyboard().keys())
        if (key.label == label) {
            p.tap(key.x + key.w / 2, key.y + key.h / 2);
            return;
        }
    FAIL("no key " << label);
}
void pickerType(WifiPicker& p, const std::string& s) {
    for (char c : s) {
        bool upper = c >= 'A' && c <= 'Z';
        if (upper != p.keyboard().shifted()) pickerKey(p, "SHIFT");
        pickerKey(p, std::string(1, c));
    }
}
std::vector<WifiPicker::Network> fakeScan() {
    return {{"TestNet Weak", -80, true}, {"TestNet Open", -60, false}, {"TestNet Strong", -40, true}};
}
}  // namespace

TEST_CASE("keyboard types letters, shifts, deletes and finishes") {
    TouchKeyboard k("Password", true);
    typeOn(k, "Ab1");
    tapKey(k, "DEL");
    typeOn(k, "c");
    CHECK(k.text() == "Abc");
    tapKey(k, "OK");
    CHECK(k.done());
}

TEST_CASE("keyboard masks a password until SHOW is tapped") {
    TouchKeyboard k("Password", true);
    typeOn(k, "abc");
    CHECK(k.shown() == "***");
    tapKey(k, "SHOW");
    CHECK(k.shown() == "abc");
}

TEST_CASE("keyboard has a symbols page") {
    TouchKeyboard k("Password", false);
    tapKey(k, "#+=");
    tapKey(k, "!");
    CHECK(k.text() == "!");
}

TEST_CASE("picker lists networks strongest first, open ones marked") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    auto rows = p.rows();
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].find("TestNet Strong") != std::string::npos);
    CHECK(rows[1].find("open") != std::string::npos);
}

TEST_CASE("tapping a secured network opens the keyboard, OK asks to join") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    p.tap(100, WifiPicker::kRowTop + 4);  // the first row: TestNet Strong
    REQUIRE(p.screen() == WifiPicker::Screen::Password);
    pickerType(p, "hunter22");
    pickerKey(p, "OK");
    CHECK(p.wantsJoin());
    CHECK(p.ssid() == "TestNet Strong");
    CHECK(p.password() == "hunter22");
}

TEST_CASE("an open network joins without a password") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    p.tap(100, WifiPicker::kRowTop + WifiPicker::kRowH + 4);  // the second row: TestNet Open
    CHECK(p.wantsJoin());
    CHECK(p.password().empty());
}

TEST_CASE("a hidden network asks for its name, then its password") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    tapButton(p, "HIDDEN");
    REQUIRE(p.screen() == WifiPicker::Screen::Name);
    pickerType(p, "HIDDENTESTNET");
    pickerKey(p, "OK");
    REQUIRE(p.screen() == WifiPicker::Screen::Password);
    pickerType(p, "notreal1");
    pickerKey(p, "OK");
    CHECK(p.wantsJoin());
    CHECK(p.ssid() == "HIDDENTESTNET");
}

TEST_CASE("a failed join says so and goes back to the list") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    p.tap(100, WifiPicker::kRowTop + WifiPicker::kRowH + 4);
    p.joinFailed();
    CHECK(p.screen() == WifiPicker::Screen::List);
    CHECK(p.message().find("couldn't join") != std::string::npos);
    CHECK_FALSE(p.wantsJoin());
}

TEST_CASE("a board that is already online can leave the picker") {
    WifiPicker p;
    p.setNetworks(fakeScan());
    bool hasBack = false;
    for (auto& b : p.buttons()) hasBack = hasBack || b.label == "BACK";
    CHECK_FALSE(hasBack);  // a board with no network has nowhere to go back to
    p.setCanLeave(true);
    tapButton(p, "BACK");
    CHECK(p.wantsLeave());
}
