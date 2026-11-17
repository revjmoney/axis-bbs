// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ESP32 core's File and FS, over the data folder (fs.cpp). An FS has the board's folder name ("/sd" or
// "/littlefs"); FS.open("/x") opens hostPath("<that>/x"), so LittleFS.open("/x") is <data>/flash/x and
// SD.open("/x") is <data>/sd/x. A File is a FILE* (or a DIR* for a folder) shared by its copies, as on the ESP32:
// closing one copy closes them all.
#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <memory>

#include "Print.h"
#include "WString.h"

namespace fs {

enum SeekMode { SeekSet = 0, SeekCur = 1, SeekEnd = 2 };

struct FileImpl;  // fs.cpp

class File : public Stream {
public:
    File() = default;
    explicit File(std::shared_ptr<FileImpl> impl) : impl_(std::move(impl)) {}

    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* buf, size_t n) override;
    using Print::write;
    int         available() override;
    int         read() override;
    int         peek() override;
    void        flush() override;
    size_t      read(uint8_t* buf, size_t n);
    bool        seek(uint32_t pos, SeekMode mode = SeekSet);
    size_t      position() const;
    size_t      size() const;
    void        close();
    explicit    operator bool() const;
    time_t      getLastWrite();
    const char* path() const;  // as it was opened ("/users.dat")
    const char* name() const;  // the last part of path (the ESP32 core 2.x way)
    bool        isDirectory();
    File        openNextFile(const char* mode = "r");
    void        rewindDirectory();

private:
    std::shared_ptr<FileImpl> impl_;
};

class FS {
public:
    explicit constexpr FS(const char* base) : base_(base) {}

    File open(const char* path, const char* mode = "r", bool create = false);
    File open(const String& path, const char* mode = "r", bool create = false) { return open(path.c_str(), mode, create); }
    bool exists(const char* path);
    bool exists(const String& path) { return exists(path.c_str()); }
    bool remove(const char* path);
    bool remove(const String& path) { return remove(path.c_str()); }
    bool rename(const char* from, const char* to);
    bool rename(const String& a, const String& b) { return rename(a.c_str(), b.c_str()); }
    bool mkdir(const char* path);
    bool mkdir(const String& path) { return mkdir(path.c_str()); }
    bool rmdir(const char* path);
    bool rmdir(const String& path) { return rmdir(path.c_str()); }

protected:
    const char* base_;  // the board's folder: "/sd" or "/littlefs"
    uint64_t    total();  // the filesystem holding the data folder (statvfs), in bytes
    uint64_t    used();
};

}  // namespace fs

using fs::File;
using fs::FS;
using fs::SeekMode;
using fs::SeekSet;
using fs::SeekCur;
using fs::SeekEnd;
