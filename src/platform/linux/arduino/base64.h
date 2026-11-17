// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's base64 helper.
#pragma once
#include <cstddef>
#include <cstdint>

#include "WString.h"

class base64 {
public:
    static String encode(const uint8_t* data, size_t length);
    static String encode(const String& text) { return encode((const uint8_t*)text.c_str(), text.length()); }
};
