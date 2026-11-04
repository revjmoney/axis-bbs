// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Files as the door sees them. The PC uses std::filesystem; the CYD will use SD or LittleFS;
// a cluster follower will ask the main board. Paths are DOS style ("C:\LORD\X.DAT", any case).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace axis {
namespace door {

struct FileInfo {
    uint32_t    size = 0;
    uint16_t    dosDate = 0, dosTime = 0;
    uint8_t     attr = 0;  // 0x10 = directory
    std::string name83;    // upper case 8.3 name
};

struct FindState {
    std::vector<FileInfo> matches;
    size_t                next = 0;
};

class HostFs {
public:
    // access: 0 read, 1 write, 2 read/write. >= 0 handle, < 0 = -(DOS error code)
    virtual int     open(const std::string& dosPath, int access, bool create, bool truncate, bool exclusive = false) = 0;
    virtual int     read(int h, void* buf, int n) = 0;
    virtual int     write(int h, const void* buf, int n) = 0;  // n == 0 truncates at the current position
    virtual int64_t seek(int h, int64_t off, int whence) = 0;  // new position, < 0 on error
    virtual int     close(int h) = 0;
    virtual bool    stat(const std::string& dosPath, FileInfo& out) = 0;
    virtual int     remove(const std::string& dosPath) = 0;  // 0 or -(DOS error)
    virtual int     rename(const std::string& from, const std::string& to) = 0;
    virtual int     mkdir(const std::string& dosPath) = 0;
    virtual int     rmdir(const std::string& dosPath) = 0;
    virtual bool    isDir(const std::string& dosPath) = 0;
    virtual bool    find(const std::string& dosPattern, uint8_t attr, FindState& st) = 0;
    virtual int     fileKey(int h) = 0;   // same number for every handle on the same file (for locks)
    virtual bool    fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) = 0;
    virtual void    setCwd(const std::string& dosDir) = 0;  // "" = root; relative paths resolve here
    virtual std::string cwd() const = 0;
    virtual ~HostFs() = default;
};

// Byte-range locks (INT 21h 5Ch), shared by every node on a board; a cluster follower's door asks
// the main's table instead (cluster/doorfs.h), so a door played on two boards keeps its files whole.
class LockTable {
public:
    virtual bool lock(int key, uint32_t off, uint32_t len, int owner);    // false if any locked range overlaps
    virtual bool unlock(int key, uint32_t off, uint32_t len, int owner);  // false unless that exact lock exists
    virtual void releaseOwner(int owner);
    virtual ~LockTable() = default;

private:
    struct Lock {
        int      key;
        uint32_t off, len;
        int      owner;
    };
    std::vector<Lock> locks_;
};

}  // namespace door
}  // namespace axis
