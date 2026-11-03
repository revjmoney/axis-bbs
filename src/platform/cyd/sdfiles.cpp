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

struct SdFile : axis::bbs::RandomFile {
    int      fd = -1;
    uint32_t bytes = 0;
    ~SdFile() override {
        if (fd >= 0) ::close(fd);
    }
    uint32_t size() const override { return bytes; }
    bool     readAt(uint32_t off, uint8_t* buf, size_t n) override {
        if (::lseek(fd, off, SEEK_SET) != (off_t)off) return false;
        size_t got = 0;
        while (got < n) {
            int r = ::read(fd, buf + got, n - got);
            if (r <= 0) return false;
            got += (size_t)r;
        }
        return true;
    }
};

// an upload: written to a hidden temp file, renamed into the area only when it is complete
struct SdWrite : axis::bbs::WriteFile {
    int         fd = -1;
    std::string temp, final;
    ~SdWrite() override { finish(false); }
    bool write(const uint8_t* p, size_t n) override {
        size_t done = 0;
        while (done < n) {
            int w = ::write(fd, p + done, n - done);
            if (w <= 0) return false;
            done += (size_t)w;
        }
        return true;
    }
    bool finish(bool keep) override {
        if (fd < 0) return false;
        ::close(fd);
        fd = -1;
        struct stat st;
        if (keep && ::stat(final.c_str(), &st) != 0 && ::rename(temp.c_str(), final.c_str()) == 0) return true;
        ::unlink(temp.c_str());
        return false;
    }
};

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

std::unique_ptr<axis::bbs::RandomFile> SdFileLibrary::open(const std::string& areaId, const std::string& name) {
    if (!storageHasSd() || !safeName(areaId) || !safeName(name)) return nullptr;
    std::unique_ptr<SdFile> f(new SdFile);
    std::string             path = folderOf(areaId) + "/" + name;
    struct stat             st;
    if (::stat(path.c_str(), &st) != 0) return nullptr;
    f->fd = ::open(path.c_str(), O_RDONLY);
    if (f->fd < 0) return nullptr;
    f->bytes = (uint32_t)st.st_size;
    return std::unique_ptr<axis::bbs::RandomFile>(f.release());
}

std::unique_ptr<axis::bbs::WriteFile> SdFileLibrary::create(const std::string& areaId, const std::string& name) {
    if (!storageHasSd() || !safeName(areaId) || !safeName(name)) return nullptr;
    std::unique_ptr<SdWrite> w(new SdWrite);
    w->final = folderOf(areaId) + "/" + name;
    w->temp = folderOf(areaId) + "/.upload.tmp";
    ::unlink(w->temp.c_str());  // a leftover from a dropped call
    w->fd = ::open(w->temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (w->fd < 0) return nullptr;
    return std::unique_ptr<axis::bbs::WriteFile>(w.release());
}

bool SdFileLibrary::describe(const std::string& areaId, const std::string& name, const std::string& desc) {
    if (!storageHasSd() || !safeName(areaId) || !safeName(name)) return false;
    int fd = ::open((folderOf(areaId) + "/FILES.BBS").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0) return false;
    std::string line = name;
    line += std::string(name.size() < 13 ? 13 - name.size() : 1, ' ') + desc + "\r\n";
    bool ok = ::write(fd, line.data(), line.size()) == (int)line.size();
    ::close(fd);
    return ok;
}

bool SdFileLibrary::remove(const std::string& areaId, const std::string& name) {
    if (!storageHasSd() || !safeName(areaId) || !safeName(name)) return false;
    return ::unlink((folderOf(areaId) + "/" + name).c_str()) == 0;
}

bool SdFileLibrary::rename(const std::string& areaId, const std::string& from, const std::string& to) {
    if (!storageHasSd() || !safeName(areaId) || !safeName(from) || !safeName(to)) return false;
    struct stat st;
    std::string a = folderOf(areaId) + "/" + from, b = folderOf(areaId) + "/" + to;
    if (::stat(b.c_str(), &st) == 0) return false;  // never over an existing file
    return ::rename(a.c_str(), b.c_str()) == 0;
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
