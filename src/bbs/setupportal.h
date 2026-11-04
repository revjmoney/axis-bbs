// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// A headless node (a plain ESP32, no screen) with no saved WiFi opens a hotspot, AXIS-SETUP; this is
// its one web page, at http://192.168.4.1/: the networks it can see and a form for the network
// name and password (typed by the sysop, sent once to /join, never shown back or logged).
#pragma once
#include <string>
#include <vector>

namespace axis::bbs {

std::string htmlEscape(const std::string& s);
std::string portalPage(const std::vector<std::string>& ssids, const std::string& message);

}  // namespace axis::bbs
