// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")
#include "bbs/station.h"

#include <cctype>

namespace axis::bbs::station {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string canon(const std::string& name) {
    std::string k = lower(name);
    if (k == "temp" || k == "temperature") return "temp";
    if (k == "water" || k == "level") return "water";
    if (k == "light") return "light";
    if (k == "humid" || k == "humidity") return "humid";
    return {};
}
int relays[kRelays] = {};
int beepHz = 0, beepMs = 0;
}  // namespace

std::string read(const std::string& name) {
    std::string k = canon(name);
    if (k == "temp") return "72";
    if (k == "water") return "0";
    if (k == "light") return "10";
    if (k == "humid") return "40";
    return {};
}

std::string line(const std::string& name) {
    std::string k = canon(name);
    if (k == "temp") return "temp    72 F   stub";
    if (k == "water") return "water   0 %    stub";
    if (k == "light") return "light   10     stub";
    if (k == "humid") return "humid   40 %   stub";
    return {};
}

bool relay(int n, int on) {
    if (n < 1 || n > kRelays) return false;
    relays[n - 1] = on ? 1 : 0;
    return true;
}

int relayState(int n) {
    if (n < 1 || n > kRelays) return -1;
    return relays[n - 1];
}

void beep(int hz, int ms) {
    if (hz < 40) hz = 40;
    if (hz > 4000) hz = 4000;
    if (ms < 20) ms = 20;
    if (ms > 2000) ms = 2000;
    beepHz = hz;
    beepMs = ms;
}

int lastBeepHz() { return beepHz; }
int lastBeepMs() { return beepMs; }

}  // namespace axis::bbs::station
