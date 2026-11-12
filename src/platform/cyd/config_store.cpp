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

std::string flashRead(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) return "";
    std::string s;
    s.resize(f.size());
    size_t n = f.read((uint8_t*)&s[0], s.size());
    s.resize(n);
    f.close();
    return s;
}

bool flashWrite(const char* path, const std::string& text) {
    File f = LittleFS.open("/save.tmp", "w");
    if (!f) return false;
    bool ok = f.write((const uint8_t*)text.data(), text.size()) == text.size();
    f.close();
    if (!ok) return false;
    LittleFS.remove(path);
    return LittleFS.rename("/save.tmp", path);
}

bool flashWriteStream(const char* path, const std::function<bool(File&)>& fill) {
    File f = LittleFS.open("/save.tmp", "w");
    if (!f) return false;
    bool ok = fill(f);
    f.close();
    if (!ok) {
        LittleFS.remove("/save.tmp");  // the old file stays as it was
        return false;
    }
    LittleFS.remove(path);
    return LittleFS.rename("/save.tmp", path);
}
