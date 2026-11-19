// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma once

namespace axis::bbs {

// The software's name and version (the board's own name, e.g. AXIS UNDERGROUND CNC, is a setting).
constexpr const char* kAxisSoftware = "Axis Micro BBS Firmware";  // the ESP32 build's name, and the name in logs
constexpr const char* kAxisProduct = "AXIS BBS";                   // the whole family, as the About screen names it
constexpr const char* kAxisVersion = "0.53";  // shown on the login screen and in Info

}  // namespace axis::bbs
