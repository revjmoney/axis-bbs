// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's paths, made real at link time. The axis link passes -Wl,--wrap=<name> for each call below
// (CMakeLists.pc.txt), so every call to it from main.cpp, the core (axiscore_native) and these platform files reaches
// __wrap_<name> here, which hands the C library hostPath(path) instead: "/sd/doors/lord" becomes
// "<data>/sd/doors/lord", "/littlefs/events.log" becomes "<data>/flash/events.log", any other path is unchanged.
//
// Why at link time and not with a header force-included into main.cpp: main.cpp hands board paths to the core,
// and the core opens them itself (RotatingLog gets "/littlefs/events.log", PosixHostFs gets "/sd/doors/<id>",
// the message bases get storageRoot() + "/msgs"). A header in main.cpp cannot reach those calls, and the core is
// not edited or built differently for this. The wrap touches only the axis executable; axiscore_native and the
// tests are compiled the same as ever.
//
// Every file opened through here is close-on-exec (O_CLOEXEC, fopen's "e"), so a restart (hostRestart, which
// re-execs the program) hands none of them to the new program. That covers the host's own files as well: the
// platform files are linked into the same executable, so their open() and fopen() come here too.
//
// Not covered (none is used by main.cpp or the core today): C++ streams and std::filesystem, which call the
// library from inside libstdc++, and the *at() calls. The list is checked against `nm -u` of the objects.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utime.h>

#include <cstdarg>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "platform/linux/clusterhost.h"
#include "platform/linux/paths.h"

// The takeover change log: in MirrorMain, a successful write-type call on a card path ("/sd/...") is noted
// (clusterhost.h takeoverNote), the same calls the board's cardwatch.cpp notes. errno is kept as the call left it.
namespace {
inline bool cardPath(const char* p) { return p && p[0] == '/' && p[1] == 's' && p[2] == 'd' && p[3] == '/'; }
inline void noteCard(const char* p) {
    if (!cardPath(p) || axisProfile() != Profile::MirrorMain) return;
    int e = errno;
    takeoverNote(p);
    errno = e;
}
inline bool writeMode(const char* mode) {
    return mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+'));
}
inline bool writeFlags(int flags) { return (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC)) != 0; }
}  // namespace

extern "C" {
FILE* __real_fopen(const char* path, const char* mode);
FILE* __real_fopen64(const char* path, const char* mode);
FILE* __real_freopen(const char* path, const char* mode, FILE* f);
int   __real_open(const char* path, int flags, ...);
int   __real_open64(const char* path, int flags, ...);
int   __real___open_2(const char* path, int flags);
int   __real___open64_2(const char* path, int flags);
int   __real_stat(const char* path, struct stat* st);
int   __real_stat64(const char* path, struct stat64* st);
int   __real_lstat(const char* path, struct stat* st);
int   __real_lstat64(const char* path, struct stat64* st);
int   __real_mkdir(const char* path, mode_t mode);
int   __real_rename(const char* from, const char* to);
int   __real_remove(const char* path);
DIR*  __real_opendir(const char* path);
int   __real_unlink(const char* path);
int   __real_rmdir(const char* path);
int   __real_access(const char* path, int how);
int   __real_truncate(const char* path, off_t len);
int   __real_statvfs(const char* path, struct statvfs* st);
int   __real_utime(const char* path, const struct utimbuf* t);

// fopen's mode with glibc's "e" (close-on-exec) added
static std::string cloexec(const char* mode) {
    std::string m = mode ? mode : "";
    if (!m.empty() && m.find('e') == std::string::npos) m += 'e';
    return m;
}
FILE* __wrap_fopen(const char* path, const char* mode) {
    FILE* f = __real_fopen(hostPath(path).c_str(), mode ? cloexec(mode).c_str() : mode);
    if (f && writeMode(mode)) noteCard(path);
    return f;
}
FILE* __wrap_fopen64(const char* path, const char* mode) {
    FILE* f = __real_fopen64(hostPath(path).c_str(), mode ? cloexec(mode).c_str() : mode);
    if (f && writeMode(mode)) noteCard(path);
    return f;
}
FILE* __wrap_freopen(const char* path, const char* mode, FILE* f) {
    FILE* r = __real_freopen(path ? hostPath(path).c_str() : nullptr, mode, f);  // (no path: reopen f in a new mode)
    if (r && path && writeMode(mode)) noteCard(path);
    return r;
}

// open's mode argument is there only when the flags create a file
static mode_t openMode(int flags, va_list ap) {
    return (flags & O_CREAT) || (flags & O_TMPFILE) == O_TMPFILE ? (mode_t)va_arg(ap, int) : 0;
}
int __wrap_open(const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = openMode(flags, ap);
    va_end(ap);
    int fd = __real_open(hostPath(path).c_str(), flags | O_CLOEXEC, mode);
    if (fd >= 0 && writeFlags(flags)) noteCard(path);
    return fd;
}
int __wrap_open64(const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = openMode(flags, ap);
    va_end(ap);
    int fd = __real_open64(hostPath(path).c_str(), flags | O_CLOEXEC, mode);
    if (fd >= 0 && writeFlags(flags)) noteCard(path);
    return fd;
}
// _FORTIFY_SOURCE turns an open() with no mode and flags not known at compile time into these
int __wrap___open_2(const char* path, int flags) {
    int fd = __real___open_2(hostPath(path).c_str(), flags | O_CLOEXEC);
    if (fd >= 0 && writeFlags(flags)) noteCard(path);
    return fd;
}
int __wrap___open64_2(const char* path, int flags) {
    int fd = __real___open64_2(hostPath(path).c_str(), flags | O_CLOEXEC);
    if (fd >= 0 && writeFlags(flags)) noteCard(path);
    return fd;
}

int __wrap_stat(const char* path, struct stat* st) { return __real_stat(hostPath(path).c_str(), st); }
int __wrap_stat64(const char* path, struct stat64* st) { return __real_stat64(hostPath(path).c_str(), st); }
int __wrap_lstat(const char* path, struct stat* st) { return __real_lstat(hostPath(path).c_str(), st); }
int __wrap_lstat64(const char* path, struct stat64* st) { return __real_lstat64(hostPath(path).c_str(), st); }
int __wrap_mkdir(const char* path, mode_t mode) {
    int r = __real_mkdir(hostPath(path).c_str(), mode);
    if (r == 0) noteCard(path);
    return r;
}
int __wrap_rename(const char* from, const char* to) {
    int r = __real_rename(hostPath(from).c_str(), hostPath(to).c_str());
    if (r == 0) {
        noteCard(from);
        noteCard(to);
    }
    return r;
}
int __wrap_remove(const char* path) {
    int r = __real_remove(hostPath(path).c_str());
    if (r == 0) noteCard(path);
    return r;
}
DIR* __wrap_opendir(const char* path) { return __real_opendir(hostPath(path).c_str()); }
int __wrap_unlink(const char* path) {
    int r = __real_unlink(hostPath(path).c_str());
    if (r == 0) noteCard(path);
    return r;
}
int __wrap_rmdir(const char* path) {
    int r = __real_rmdir(hostPath(path).c_str());
    if (r == 0) noteCard(path);
    return r;
}
int  __wrap_access(const char* path, int how) { return __real_access(hostPath(path).c_str(), how); }
int __wrap_truncate(const char* path, off_t len) {
    int r = __real_truncate(hostPath(path).c_str(), len);
    if (r == 0) noteCard(path);
    return r;
}
int  __wrap_statvfs(const char* path, struct statvfs* st) { return __real_statvfs(hostPath(path).c_str(), st); }
int  __wrap_utime(const char* path, const struct utimbuf* t) { return __real_utime(hostPath(path).c_str(), t); }  // (cardtree.cpp)
}
