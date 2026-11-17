// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// WiFi on Linux: the PC's network is already up (wired or wireless, the operating system's business), so the
// board's join always succeeds and there is nothing to scan, join or remember.
#include "platform/cyd/wifi.h"

#include <time.h>

const char* wifiSsid() { return "host network"; }
bool        wifiHasTime() { return time(nullptr) > 1700000000; }  // the PC keeps its own clock
bool        wifiJoinSaved() { return true; }

std::vector<axis::bbs::WifiPicker::Network> wifiScan() { return {}; }
bool wifiJoin(const String&, const String&) { return false; }
void wifiRemember(const String&, const String&) {}
