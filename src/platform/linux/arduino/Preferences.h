// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's Preferences (NVS namespaces). Saving comes with the data folder (plan task 2, prefs/): until
// then no namespace opens, every read gives its default and every write saves nothing.
#pragma once
#include <cstddef>
#include <cstdint>

#include "WString.h"

class Preferences {
public:
    bool begin(const char* name, bool readOnly = false, const char* partition = nullptr) {
        (void)name, (void)readOnly, (void)partition;
        return false;
    }
    void   end() {}
    bool   clear() { return false; }
    bool   remove(const char*) { return false; }
    bool   isKey(const char*) { return false; }
    size_t freeEntries() { return 0; }

    size_t putChar(const char*, int8_t) { return 0; }
    size_t putUChar(const char*, uint8_t) { return 0; }
    size_t putShort(const char*, int16_t) { return 0; }
    size_t putUShort(const char*, uint16_t) { return 0; }
    size_t putInt(const char*, int32_t) { return 0; }
    size_t putUInt(const char*, uint32_t) { return 0; }
    size_t putLong(const char*, int32_t) { return 0; }
    size_t putULong(const char*, uint32_t) { return 0; }
    size_t putLong64(const char*, int64_t) { return 0; }
    size_t putULong64(const char*, uint64_t) { return 0; }
    size_t putFloat(const char*, float) { return 0; }
    size_t putDouble(const char*, double) { return 0; }
    size_t putBool(const char*, bool) { return 0; }
    size_t putString(const char*, const char*) { return 0; }
    size_t putString(const char*, const String&) { return 0; }
    size_t putBytes(const char*, const void*, size_t) { return 0; }

    int8_t   getChar(const char*, int8_t def = 0) { return def; }
    uint8_t  getUChar(const char*, uint8_t def = 0) { return def; }
    int16_t  getShort(const char*, int16_t def = 0) { return def; }
    uint16_t getUShort(const char*, uint16_t def = 0) { return def; }
    int32_t  getInt(const char*, int32_t def = 0) { return def; }
    uint32_t getUInt(const char*, uint32_t def = 0) { return def; }
    int32_t  getLong(const char*, int32_t def = 0) { return def; }
    uint32_t getULong(const char*, uint32_t def = 0) { return def; }
    int64_t  getLong64(const char*, int64_t def = 0) { return def; }
    uint64_t getULong64(const char*, uint64_t def = 0) { return def; }
    float    getFloat(const char*, float def = 0) { return def; }
    double   getDouble(const char*, double def = 0) { return def; }
    bool     getBool(const char*, bool def = false) { return def; }
    String   getString(const char*, const String& def = String()) { return def; }
    size_t   getBytesLength(const char*) { return 0; }
    size_t   getBytes(const char*, void*, size_t) { return 0; }
};
