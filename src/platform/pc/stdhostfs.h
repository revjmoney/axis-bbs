// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// HostFs over a folder on the PC (std::filesystem). Drive C: is the folder; names match in any case.
#pragma once
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "door/hostfs.h"

namespace axis {
namespace pc {

class StdHostFs : public door::HostFs {
public:
    explicit StdHostFs(std::filesystem::path root);
    ~StdHostFs() override;
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

    std::filesystem::path resolve(const std::string& dosPath) const;  // host path, matching case where it exists

private:
    struct Open {
        std::FILE*            f;
        std::filesystem::path path;
        bool                  writing = false;  // last operation was a write (C needs a seek to switch)
    };
    std::filesystem::path       root_;
    std::string                 cwd_;
    std::map<int, Open>         open_;
    std::vector<std::string>    keys_;
    int                         next_ = 1;
};

}  // namespace pc
}  // namespace axis
