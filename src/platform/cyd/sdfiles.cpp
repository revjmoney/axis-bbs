// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/sdfiles.h"

#include <dirent.h>
#include <sys/stat.h>

#include <fcntl.h>
#include <unistd.h>

#include "platform/cyd/storage.h"

using axis::bbs::FileEntry;

namespace {
std::string folderOf(const std::string& areaId) { return "/sd/files/" + areaId; }

bool safeName(const std::string& n) {  // one name inside the area, never a path out of it
    return !n.empty() && n.find('/') == std::string::npos && n.find('\\') == std::string::npos && n != "." && n != "..";
}
}  // namespace

std::vector<FileEntry> SdFileLibrary::list(const std::string& areaId) {
    std::vector<std::pair<std::string, uint32_t>> folder;
    if (!storageHasSd() || !safeName(areaId)) return {};
    std::string dir = folderOf(areaId);
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            struct stat st;
            if (n[0] == '.' || ::stat((dir + "/" + n).c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
            folder.emplace_back(n, (uint32_t)st.st_size);
        }
        closedir(d);
    }
    std::string bbs;
    read(areaId, "FILES.BBS", bbs, 16 * 1024);
    return axis::bbs::mergeFilesBbs(bbs, folder);
}

bool SdFileLibrary::read(const std::string& areaId, const std::string& name, std::string& out, size_t max) {
    out.clear();
    if (!storageHasSd() || !safeName(areaId) || !safeName(name)) return false;
    int fd = ::open((folderOf(areaId) + "/" + name).c_str(), O_RDONLY);  // no stdio: its 128-byte buffer is slow
    if (fd < 0) return false;
    char buf[1024];
    int  n;
    while (out.size() < max && (n = ::read(fd, buf, sizeof buf < max - out.size() ? sizeof buf : max - out.size())) > 0)
        out.append(buf, (size_t)n);
    ::close(fd);
    return true;
}
