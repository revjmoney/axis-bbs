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

#include <cstdarg>
#include <cstdio>
#include <string>

#include "platform/linux/paths.h"

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

// fopen's mode with glibc's "e" (close-on-exec) added
static std::string cloexec(const char* mode) {
    std::string m = mode ? mode : "";
    if (!m.empty() && m.find('e') == std::string::npos) m += 'e';
    return m;
}
FILE* __wrap_fopen(const char* path, const char* mode) {
    return __real_fopen(hostPath(path).c_str(), mode ? cloexec(mode).c_str() : mode);
}
FILE* __wrap_fopen64(const char* path, const char* mode) {
    return __real_fopen64(hostPath(path).c_str(), mode ? cloexec(mode).c_str() : mode);
}
FILE* __wrap_freopen(const char* path, const char* mode, FILE* f) {
    return __real_freopen(path ? hostPath(path).c_str() : nullptr, mode, f);  // (no path: reopen f in a new mode)
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
    return __real_open(hostPath(path).c_str(), flags | O_CLOEXEC, mode);
}
int __wrap_open64(const char* path, int flags, ...) {
    va_list ap;
    va_start(ap, flags);
    mode_t mode = openMode(flags, ap);
    va_end(ap);
    return __real_open64(hostPath(path).c_str(), flags | O_CLOEXEC, mode);
}
// _FORTIFY_SOURCE turns an open() with no mode and flags not known at compile time into these
int __wrap___open_2(const char* path, int flags) { return __real___open_2(hostPath(path).c_str(), flags | O_CLOEXEC); }
int __wrap___open64_2(const char* path, int flags) {
    return __real___open64_2(hostPath(path).c_str(), flags | O_CLOEXEC);
}

int __wrap_stat(const char* path, struct stat* st) { return __real_stat(hostPath(path).c_str(), st); }
int __wrap_stat64(const char* path, struct stat64* st) { return __real_stat64(hostPath(path).c_str(), st); }
int __wrap_lstat(const char* path, struct stat* st) { return __real_lstat(hostPath(path).c_str(), st); }
int __wrap_lstat64(const char* path, struct stat64* st) { return __real_lstat64(hostPath(path).c_str(), st); }
int __wrap_mkdir(const char* path, mode_t mode) { return __real_mkdir(hostPath(path).c_str(), mode); }
int __wrap_rename(const char* from, const char* to) { return __real_rename(hostPath(from).c_str(), hostPath(to).c_str()); }
int __wrap_remove(const char* path) { return __real_remove(hostPath(path).c_str()); }
DIR* __wrap_opendir(const char* path) { return __real_opendir(hostPath(path).c_str()); }
int  __wrap_unlink(const char* path) { return __real_unlink(hostPath(path).c_str()); }
int  __wrap_rmdir(const char* path) { return __real_rmdir(hostPath(path).c_str()); }
int  __wrap_access(const char* path, int how) { return __real_access(hostPath(path).c_str(), how); }
int  __wrap_truncate(const char* path, off_t len) { return __real_truncate(hostPath(path).c_str(), len); }
int  __wrap_statvfs(const char* path, struct statvfs* st) { return __real_statvfs(hostPath(path).c_str(), st); }
}
