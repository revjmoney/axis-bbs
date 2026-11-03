// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// BBS settings in internal flash (LittleFS /axis.ini), so they survive SD card swaps.
#pragma once
#include "bbs/config.h"

bool                  configMount();  // mounts LittleFS, formatting a blank partition on first boot
axis::bbs::BbsConfig  configLoad();   // defaults when there is no file yet
bool                  configSave(const axis::bbs::BbsConfig& c);
bool                  configErase();  // back to first-boot setup
// any small text file in internal flash (the user base: /users.dat), saved the same safe way
std::string           flashRead(const char* path);
bool                  flashWrite(const char* path, const std::string& text);
