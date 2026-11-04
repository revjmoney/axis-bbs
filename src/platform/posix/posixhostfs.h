// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// HostFs over a folder, using only POSIX file descriptors and dirent, so the same code runs on the PC
// (MinGW) and on the CYD (ESP-IDF VFS: the SD card at /sd or LittleFS at /littlefs). Drive C: is
// the folder; names match in any case through the shared resolver.
#pragma once
#include <map>
#include <string>
#include <vector>

#include "door/hostfs.h"

namespace axis {

class PosixHostFs : public door::HostFs {
public:
    explicit PosixHostFs(std::string root);  // no trailing slash ("/sd/doors/lord")
    ~PosixHostFs() override;
    int     open(const std::string& dosPath, int access, bool create, bool truncate, bool exclusive = false) override;
    int     read(int h, void* buf, int n) override;
    int     write(int h, const void* buf, int n) override;
    int64_t seek(int h, int64_t off, int whence) override;
    int     close(int h) override;
    bool    stat(const std::string& dosPath, door::FileInfo& out) override;
    int     remove(const std::string& dosPath) override;
    int     rename(const std::string& from, const std::string& to) override;
    int     mkdir(const std::string& dosPath) override;
    int     rmdir(const std::string& dosPath) override;
    bool    isDir(const std::string& dosPath) override;
    bool    find(const std::string& dosPattern, uint8_t attr, door::FindState& st) override;
    int     fileKey(int h) override;
    bool    fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) override;
    void    setCwd(const std::string& dosDir) override { cwd_ = dosDir; }
    std::string cwd() const override { return cwd_; }

    std::string resolve(const std::string& dosPath) const;  // host path, matching case where it exists
    const std::string& root() const { return root_; }

private:
    struct Open {
        int         fd;
        std::string path;
    };
    std::string              root_, cwd_;
    std::map<int, Open>      open_;
    int                      next_ = 1;
};

}  // namespace axis
