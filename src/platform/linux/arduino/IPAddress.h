// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's IPAddress (IPv4): the four bytes in network order, so the uint32_t is an s_addr.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "WString.h"

class IPAddress {
public:
    IPAddress() = default;
    IPAddress(uint8_t a, uint8_t b, uint8_t c, uint8_t d) : bytes_{a, b, c, d} {}
    IPAddress(uint32_t addr) { std::memcpy(bytes_, &addr, 4); }

    operator uint32_t() const {
        uint32_t v;
        std::memcpy(&v, bytes_, 4);
        return v;
    }
    bool     operator==(const IPAddress& o) const { return std::memcmp(bytes_, o.bytes_, 4) == 0; }
    bool     operator!=(const IPAddress& o) const { return !(*this == o); }
    uint8_t  operator[](int i) const { return bytes_[i]; }
    uint8_t& operator[](int i) { return bytes_[i]; }

    String toString() const {
        char s[16];
        std::snprintf(s, sizeof s, "%u.%u.%u.%u", bytes_[0], bytes_[1], bytes_[2], bytes_[3]);
        return String(s);
    }
    bool fromString(const char* s);  // dotted quad only, as the core's
    bool fromString(const String& s) { return fromString(s.c_str()); }

private:
    uint8_t bytes_[4] = {0, 0, 0, 0};
};
