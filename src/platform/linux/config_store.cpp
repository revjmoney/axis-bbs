// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The BBS settings and the small flash files, on Linux. They live in the data folder (plan task 2): until then
// nothing mounts, the settings are the first-boot defaults and nothing is saved.
#include "platform/cyd/config_store.h"

bool                 configMount() { return false; }
axis::bbs::BbsConfig configLoad() { return {}; }
bool                 configSave(const axis::bbs::BbsConfig&) { return false; }
bool                 configErase() { return false; }
std::string          flashRead(const char*) { return ""; }
bool                 flashWrite(const char*, const std::string&) { return false; }
bool                 flashWriteStream(const char*, const std::function<bool(File&)>&) { return false; }
