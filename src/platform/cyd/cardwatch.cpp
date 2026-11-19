// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Change detection for the card mirror: the board links with -Wl,--wrap on the calls that change a file, so
// one choke point notes every write under /sd/ in the change feed (cluster/cardtree.h), whoever made it. Each
// wrapper calls the real function first and notes only on success, and only while a card mirror is linked.
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>

#include "cluster/cardtree.h"

extern "C" {
FILE* __real_fopen(const char* path, const char* mode);
int   __real_open(const char* path, int flags, ...);
int   __real_rename(const char* from, const char* to);
int   __real_unlink(const char* path);
int   __real_remove(const char* path);
int   __real_mkdir(const char* path, mode_t mode);
int   __real_rmdir(const char* path);
int   __real_truncate(const char* path, off_t len);
}

namespace {
inline void note(const char* path) { axis::cluster::cardFeed().note(path); }
}  // namespace

extern "C" {

FILE* __wrap_fopen(const char* path, const char* mode) {
    FILE* f = __real_fopen(path, mode);
    if (f && axis::cluster::cardFeed().on() && mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+'))) note(path);
    return f;
}

int __wrap_open(const char* path, int flags, ...) {
    int mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, int);
        va_end(ap);
    }
    int fd = __real_open(path, flags, mode);
    if (fd >= 0 && axis::cluster::cardFeed().on() && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) note(path);
    return fd;
}

int __wrap_rename(const char* from, const char* to) {
    int r = __real_rename(from, to);
    if (r == 0 && axis::cluster::cardFeed().on()) {
        note(from);
        note(to);
    }
    return r;
}

int __wrap_unlink(const char* path) {
    int r = __real_unlink(path);
    if (r == 0 && axis::cluster::cardFeed().on()) note(path);
    return r;
}

int __wrap_remove(const char* path) {
    int r = __real_remove(path);
    if (r == 0 && axis::cluster::cardFeed().on()) note(path);
    return r;
}

int __wrap_mkdir(const char* path, mode_t mode) {
    int r = __real_mkdir(path, mode);
    if (r == 0 && axis::cluster::cardFeed().on()) note(path);
    return r;
}

int __wrap_rmdir(const char* path) {
    int r = __real_rmdir(path);
    if (r == 0 && axis::cluster::cardFeed().on()) note(path);
    return r;
}

int __wrap_truncate(const char* path, off_t len) {
    int r = __real_truncate(path, len);
    if (r == 0 && axis::cluster::cardFeed().on()) note(path);
    return r;
}

}  // extern "C"
