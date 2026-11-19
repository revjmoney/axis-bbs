// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's Preferences (NVS namespaces), kept as <prefs>/<namespace>.ini (paths.h: axisPrefsDir(), the
// profile's prefs folder): one key=value line each,
// values escaped (\xHH for "\", "=" in a key, and any byte outside printable ASCII). Every put saves the whole
// namespace at once (a temp file, then a rename), as NVS commits each write. One key is kept apart: namespace "clu",
// key "key" (the cluster key) is the file <data>/cluster.key in every profile (clusterhost.h). preferences.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

#include "WString.h"

class Preferences {
public:
    // false, as on the ESP32, when a namespace opened read-only has never been written
    bool   begin(const char* name, bool readOnly = false, const char* partition = nullptr);
    void   end();
    bool   clear();
    bool   remove(const char* key);
    bool   isKey(const char* key);
    size_t freeEntries();

    size_t putChar(const char* key, int8_t v) { return putNum(key, v, 1); }
    size_t putUChar(const char* key, uint8_t v) { return putNum(key, v, 1); }
    size_t putShort(const char* key, int16_t v) { return putNum(key, v, 2); }
    size_t putUShort(const char* key, uint16_t v) { return putNum(key, v, 2); }
    size_t putInt(const char* key, int32_t v) { return putNum(key, v, 4); }
    size_t putUInt(const char* key, uint32_t v) { return putNum(key, v, 4); }
    size_t putLong(const char* key, int32_t v) { return putNum(key, v, 4); }
    size_t putULong(const char* key, uint32_t v) { return putNum(key, v, 4); }
    size_t putLong64(const char* key, int64_t v) { return putNum(key, v, 8); }
    size_t putULong64(const char* key, uint64_t v);
    size_t putFloat(const char* key, float v);
    size_t putDouble(const char* key, double v);
    size_t putBool(const char* key, bool v) { return putNum(key, v ? 1 : 0, 1); }
    size_t putString(const char* key, const char* v);
    size_t putString(const char* key, const String& v) { return putString(key, v.c_str()); }
    size_t putBytes(const char* key, const void* v, size_t n);

    int8_t   getChar(const char* key, int8_t def = 0) { return (int8_t)getNum(key, def); }
    uint8_t  getUChar(const char* key, uint8_t def = 0) { return (uint8_t)getNum(key, def); }
    int16_t  getShort(const char* key, int16_t def = 0) { return (int16_t)getNum(key, def); }
    uint16_t getUShort(const char* key, uint16_t def = 0) { return (uint16_t)getNum(key, def); }
    int32_t  getInt(const char* key, int32_t def = 0) { return (int32_t)getNum(key, def); }
    uint32_t getUInt(const char* key, uint32_t def = 0) { return (uint32_t)getNum(key, def); }
    int32_t  getLong(const char* key, int32_t def = 0) { return (int32_t)getNum(key, def); }
    uint32_t getULong(const char* key, uint32_t def = 0) { return (uint32_t)getNum(key, def); }
    int64_t  getLong64(const char* key, int64_t def = 0) { return getNum(key, def); }
    uint64_t getULong64(const char* key, uint64_t def = 0);
    float    getFloat(const char* key, float def = 0);
    double   getDouble(const char* key, double def = 0);
    bool     getBool(const char* key, bool def = false) { return getNum(key, def ? 1 : 0) != 0; }
    String   getString(const char* key, const String& def = String());
    size_t   getBytesLength(const char* key);
    size_t   getBytes(const char* key, void* buf, size_t max);

private:
    size_t       putNum(const char* key, int64_t v, size_t bytes);
    int64_t      getNum(const char* key, int64_t def);
    size_t       put(const char* key, const std::string& v, size_t bytes);
    const std::string* find(const char* key) const;
    bool         save();

    bool                               open_ = false;
    bool                               readOnly_ = false;
    bool                               clu_ = false;  // namespace "clu": its "key" is <data>/cluster.key
    std::string                        file_;  // <prefs>/<namespace>.ini
    std::map<std::string, std::string> keys_;
};
