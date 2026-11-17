// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's MDNS responder. Advertising comes with the network layer (plan task 3): until then begin fails.
#pragma once
#include <cstdint>

#include "WString.h"
#include "mdns.h"

class MDNSResponder {
public:
    bool begin(const String&) { return false; }
    void end() {}
    bool addService(const char*, const char*, uint16_t) { return false; }
    bool addService(const String&, const String&, uint16_t) { return false; }
};

inline MDNSResponder MDNS;
