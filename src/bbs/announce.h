// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Which board events go out to the Discord channel, and how they read there. Only scene news:
// new users, uploads, oneliners, door games, sysop pages. Never addresses, calls or system detail.
#pragma once
#include <string>

namespace axis::bbs {

// a line for Discord (Discord markdown), or "" to keep the event on the board
std::string announceLine(const std::string& event);

}  // namespace axis::bbs
