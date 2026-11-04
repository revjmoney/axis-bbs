// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/posix/posixhostfs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <ctime>

#include "door/pathresolve.h"

#ifndef O_BINARY
#define O_BINARY 0  // only Windows has text-mode files
#endif

namespace axis {

namespace {
std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
// DOS wildcard match on an 8.3 name ("*.DAT", "LORD?.*"), case-insensitive
bool wild(const std::string& pat, const std::string& name) {
    auto split = [](const std::string& s, std::string& b, std::string& e) {
        size_t dot = s.find('.');
        b = dot == std::string::npos ? s : s.substr(0, dot);
        e = dot == std::string::npos ? "" : s.substr(dot + 1);
    };
    auto part = [](const std::string& p, const std::string& n) {
        size_t i = 0;
        for (; i < p.size(); i++) {
            if (p[i] == '*') return true;
            if (i >= n.size()) return p[i] == '?' && std::all_of(p.begin() + i, p.end(), [](char c) { return c == '?' || c == '*'; });
            if (p[i] != '?' && std::toupper((unsigned char)p[i]) != std::toupper((unsigned char)n[i])) return false;
        }
        return i == n.size();
    };
    std::string pb, pe, nb, ne;
    split(pat, pb, pe);
    split(name, nb, ne);
    if (pat == "*.*" || pat == "*") return true;
    return part(pb, nb) && part(pe, ne);
}
void dosStamp(time_t t, uint16_t& date, uint16_t& time) {
    std::tm lt = *std::localtime(&t);
    if (lt.tm_year < 80) {  // DOS dates start in 1980 (a board with no clock yet is in 1970)
        lt = std::tm();
        lt.tm_mday = 1;
        lt.tm_year = 80;
    }
    date = (uint16_t)(((lt.tm_year - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
    time = (uint16_t)((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
}
bool statPath(const std::string& p, struct stat& st) { return ::stat(p.c_str(), &st) == 0; }
bool exists(const std::string& p) {
    struct stat st;
    return statPath(p, st);
}
bool dirExists(const std::string& p) {
    struct stat st;
    return statPath(p, st) && S_ISDIR(st.st_mode);
}
std::string parentOf(const std::string& p) {
    size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "." : p.substr(0, slash);
}
std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n != "." && n != "..") names.push_back(n);
        }
        closedir(d);
    }
    return names;
}
}  // namespace

PosixHostFs::PosixHostFs(std::string root) : root_(std::move(root)) {}

PosixHostFs::~PosixHostFs() {
    for (auto& o : open_) ::close(o.second.fd);
}

// "C:\LORD\X.DAT" or "X.DAT" (relative to cwd) -> host path inside the door folder, matching
// existing names in any case (the shared resolver)
std::string PosixHostFs::resolve(const std::string& dosPath) const {
    std::string rel = door::resolveDosPath(cwd_, dosPath, [this](const std::string& dir) {
        return listDir(dir.empty() ? root_ : root_ + "/" + dir);
    });
    return rel.empty() ? root_ : root_ + "/" + rel;
}

int PosixHostFs::open(const std::string& dosPath, int access, bool create, bool truncate, bool exclusive) {
    std::string p = resolve(dosPath);
    if (p == root_ || dirExists(p)) return -5;
    bool here = exists(p);
    if (exclusive && here) return -80;  // file exists
    if (!here && !create) return dirExists(parentOf(p)) ? -2 : -3;
    if (create && !dirExists(parentOf(p))) return -3;
    // plain descriptors, no stdio: reads and writes go straight to the file system (newlib's
    // stdio buffer is 128 bytes, and unbuffered it reads a byte at a time), and other nodes
    // see every write at once
    int flags = O_BINARY | (access == 0 && !create ? O_RDONLY : O_RDWR);
    if (create && (truncate || !here)) flags |= O_CREAT | O_TRUNC;
    int fd = ::open(p.c_str(), flags, 0666);
    if (fd < 0) return access == 0 ? -2 : -5;
    int h = next_++;
    open_[h] = {fd, p};
    return h;
}

int PosixHostFs::read(int h, void* buf, int n) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    int got = 0;
    while (got < n) {
        int r = (int)::read(it->second.fd, (char*)buf + got, (unsigned)(n - got));
        if (r <= 0) break;
        got += r;
    }
    return got;
}

int PosixHostFs::write(int h, const void* buf, int n) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    int fd = it->second.fd;
    if (n == 0)  // DOS: a zero-length write sets the file size to the current position
        return ftruncate(fd, ::lseek(fd, 0, SEEK_CUR)) == 0 ? 0 : -5;
    int r = (int)::write(fd, buf, (unsigned)n);
    return r < 0 ? -5 : r;
}

int64_t PosixHostFs::seek(int h, int64_t off, int whence) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    long r = ::lseek(it->second.fd, (long)off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END);
    return r < 0 ? -25 : r;
}

int PosixHostFs::close(int h) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    ::close(it->second.fd);
    open_.erase(it);
    return 0;
}

bool PosixHostFs::stat(const std::string& dosPath, door::FileInfo& out) {
    std::string p = resolve(dosPath);
    struct stat st;
    if (!statPath(p, st)) return false;
    out.attr = S_ISDIR(st.st_mode) ? 0x10 : 0x20;
    out.size = out.attr & 0x10 ? 0 : (uint32_t)st.st_size;
    dosStamp(st.st_mtime, out.dosDate, out.dosTime);
    out.name83 = upper(p.substr(p.find_last_of('/') + 1));
    return true;
}

int PosixHostFs::remove(const std::string& dosPath) {
    std::string p = resolve(dosPath);
    if (!exists(p)) return -2;
    return std::remove(p.c_str()) == 0 ? 0 : -5;
}

int PosixHostFs::rename(const std::string& from, const std::string& to) {
    std::string a = resolve(from), b = resolve(to);
    if (!exists(a)) return -2;
    if (exists(b)) return -5;
    return std::rename(a.c_str(), b.c_str()) == 0 ? 0 : -5;
}

int PosixHostFs::mkdir(const std::string& dosPath) {
    std::string p = resolve(dosPath);
#ifdef _WIN32
    return ::mkdir(p.c_str()) == 0 ? 0 : -5;
#else
    return ::mkdir(p.c_str(), 0777) == 0 ? 0 : -5;
#endif
}

int PosixHostFs::rmdir(const std::string& dosPath) {
    std::string p = resolve(dosPath);
    if (p == root_ || !dirExists(p)) return -3;
    return ::rmdir(p.c_str()) == 0 ? 0 : -5;
}

bool PosixHostFs::isDir(const std::string& dosPath) {
    std::string p = resolve(dosPath);
    return p == root_ || dirExists(p);  // some VFS mounts can't stat their own root
}

bool PosixHostFs::find(const std::string& dosPattern, uint8_t attr, door::FindState& st) {
    st = {};
    std::string p = dosPattern;
    std::replace(p.begin(), p.end(), '/', '\\');
    size_t      slash = p.find_last_of("\\:");
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    std::string pat = slash == std::string::npos ? p : p.substr(slash + 1);
    if (dir.size() == 2 && dir[1] == ':') dir = "";
    std::string d = resolve(dir);
    for (const std::string& name : listDir(d)) {
        struct stat s;
        if (!statPath(d + "/" + name, s)) continue;
        bool isdir = S_ISDIR(s.st_mode);
        if (isdir && !(attr & 0x10)) continue;
        if (!wild(pat, name)) continue;
        door::FileInfo fi;
        fi.attr = isdir ? 0x10 : 0x20;
        fi.size = isdir ? 0 : (uint32_t)s.st_size;
        dosStamp(s.st_mtime, fi.dosDate, fi.dosTime);
        fi.name83 = upper(name);
        st.matches.push_back(fi);
    }
    return !st.matches.empty();
}

int PosixHostFs::fileKey(int h) {
    auto it = open_.find(h);
    if (it == open_.end()) return -1;
    // the file's full path, hashed: the same number for the same file in every PosixHostFs (the main's
    // own door and a follower's door served over the link lock the same records)
    std::string k = lower(root_ + "/" + it->second.path);
    uint32_t    hash = 2166136261u;  // FNV-1a
    for (char c : k) hash = (hash ^ (uint8_t)c) * 16777619u;
    return (int)(hash & 0x7FFFFFFF);
}

bool PosixHostFs::fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) {
    auto it = open_.find(h);
    if (it == open_.end()) return false;
    struct stat st;
    if (fstat(it->second.fd, &st) != 0) return false;
    dosStamp(st.st_mtime, dosDate, dosTime);
    return true;
}

}  // namespace axis
