// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/config_store.h"

#include <LittleFS.h>

static const char* kPath = "/axis.ini";

bool configMount() { return LittleFS.begin(true); }

axis::bbs::BbsConfig configLoad() {
    File f = LittleFS.open(kPath, "r");
    if (!f) return {};
    std::string s;
    while (f.available()) s += (char)f.read();
    f.close();
    return axis::bbs::parseConfig(s);
}

// write to a temp file first so a power cut never leaves a half-written config
bool configSave(const axis::bbs::BbsConfig& c) {
    std::string s = axis::bbs::formatConfig(c);
    File f = LittleFS.open("/axis.tmp", "w");
    if (!f) return false;
    bool ok = f.write((const uint8_t*)s.data(), s.size()) == s.size();
    f.close();
    if (!ok) return false;
    LittleFS.remove(kPath);
    return LittleFS.rename("/axis.tmp", kPath);
}

bool configErase() { return LittleFS.remove(kPath); }
