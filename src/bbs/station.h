// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Sensors, relays, and a small speaker on a bare board's terminal strip.
// Nothing is wired yet. Readings are stubs so a script can be written now.
// A later build can fill these in from the pins without changing the script words.
#pragma once
#include <string>

namespace axis::bbs::station {

constexpr int kRelays = 4;

// "temp", "water", "light", "humid" (and a few aliases). Empty when the name is unknown.
// The text is the number a script stores.
std::string read(const std::string& name);

// The same reading, for a menu line, marked stub.
std::string line(const std::string& name);

// n is 1..kRelays. on is 0 or 1. False when n is out of range.
bool relay(int n, int on);
int  relayState(int n);  // 0 or 1, or -1 when n is out of range

// Remembered only. No pin is driven.
void beep(int hz, int ms);
int  lastBeepHz();
int  lastBeepMs();

}  // namespace axis::bbs::station
