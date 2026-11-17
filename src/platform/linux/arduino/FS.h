// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's File and FS. Files come with the data folder (plan task 2: flash/ and sd/): until then
// nothing mounts, no file opens, and a File is always closed.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>

#include "Print.h"
#include "WString.h"

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

class File : public Stream {
public:
    File() = default;

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override {
        (void)buf, (void)n;
        return 0;
    }
    using Print::write;
    int    available() override { return 0; }
    int    read() override { return -1; }
    int    peek() override { return -1; }
    void   flush() override {}
    size_t read(uint8_t* buf, size_t n) {
        (void)buf, (void)n;
        return 0;
    }
    bool        seek(uint32_t pos, SeekMode mode = SeekSet) { return (void)pos, (void)mode, false; }
    size_t      position() const { return 0; }
    size_t      size() const { return 0; }
    void        close() {}
    explicit    operator bool() const { return false; }
    time_t      getLastWrite() { return 0; }
    const char* path() const { return ""; }
    const char* name() const { return ""; }
    bool        isDirectory() { return false; }
    File        openNextFile(const char* mode = "r") { return (void)mode, File(); }
    void        rewindDirectory() {}
};

class FS {
public:
    File open(const char* path, const char* mode = "r", bool create = false) { return (void)path, (void)mode, (void)create, File(); }
    File open(const String& path, const char* mode = "r", bool create = false) { return open(path.c_str(), mode, create); }
    bool exists(const char*) { return false; }
    bool exists(const String& path) { return exists(path.c_str()); }
    bool remove(const char*) { return false; }
    bool remove(const String& path) { return remove(path.c_str()); }
    bool rename(const char*, const char*) { return false; }
    bool rename(const String& a, const String& b) { return rename(a.c_str(), b.c_str()); }
    bool mkdir(const char*) { return false; }
    bool mkdir(const String& path) { return mkdir(path.c_str()); }
    bool rmdir(const char*) { return false; }
    bool rmdir(const String& path) { return rmdir(path.c_str()); }
};

}  // namespace fs

using fs::File;
using fs::FS;
using fs::SeekMode;
using fs::SeekSet;
using fs::SeekCur;
using fs::SeekEnd;
