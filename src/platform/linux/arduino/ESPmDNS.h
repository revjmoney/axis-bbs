// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's MDNS responder. Not used on Linux (plan 1 is solo): begin
// logs once and fails, so main.cpp advertises nothing. Body in hostnet.cpp.
#pragma once
#include <cstdint>

#include "WString.h"
#include "mdns.h"

class MDNSResponder {
public:
    bool begin(const String& name);
    void end() {}
    bool addService(const char*, const char*, uint16_t) { return false; }
    bool addService(const String&, const String&, uint16_t) { return false; }
};

inline MDNSResponder MDNS;
