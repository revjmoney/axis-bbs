// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// File, FS, LittleFS and SD over the data folder (see arduino/FS.h).
#include <FS.h>
#include <LittleFS.h>
#include <SD.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "platform/linux/paths.h"

namespace fs {

struct FileImpl {
    FILE*       f = nullptr;
    DIR*        d = nullptr;
    std::string path;  // as the board named it, inside its FS ("/users.dat")
    std::string real;  // where it is (<data>/flash/users.dat)
    std::string base;  // the FS's board folder, for openNextFile
    ~FileImpl() { shut(); }
    void shut() {
        if (f) std::fclose(f);
        if (d) ::closedir(d);
        f = nullptr;
        d = nullptr;
    }
};

namespace {
std::string inside(const char* path) {  // LittleFS and SD paths start with "/"
    std::string p = path ? path : "";
    return p.empty() || p[0] != '/' ? "/" + p : p;
}

std::shared_ptr<FileImpl> openAt(const std::string& base, const std::string& path, const char* mode) {
    auto impl = std::make_shared<FileImpl>();
    impl->path = path;
    impl->base = base;
    impl->real = hostPath((base + path).c_str());
    if (impl->real.empty()) return nullptr;  // no data folder yet
    struct stat st;
    bool        reading = !mode || std::strcmp(mode, "r") == 0;
    if (reading && ::stat(impl->real.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        impl->d = ::opendir(impl->real.c_str());
        return impl->d ? impl : nullptr;
    }
    impl->f = std::fopen(impl->real.c_str(), mode ? mode : "r");
    return impl->f ? impl : nullptr;
}
}  // namespace

// ------------------------------------------------------------------ File
size_t File::write(const uint8_t* buf, size_t n) {
    if (!impl_ || !impl_->f) return 0;
    return std::fwrite(buf, 1, n, impl_->f);
}
int File::available() {
    if (!impl_ || !impl_->f) return 0;
    size_t s = size(), p = position();
    return p < s ? (int)(s - p > 0x7fffffff ? 0x7fffffff : s - p) : 0;
}
int File::read() {
    if (!impl_ || !impl_->f) return -1;
    int c = std::fgetc(impl_->f);
    return c == EOF ? -1 : c;
}
int File::peek() {
    if (!impl_ || !impl_->f) return -1;
    int c = std::fgetc(impl_->f);
    if (c == EOF) return -1;
    std::ungetc(c, impl_->f);
    return c;
}
void File::flush() {
    if (impl_ && impl_->f) std::fflush(impl_->f);
}
size_t File::read(uint8_t* buf, size_t n) {
    if (!impl_ || !impl_->f) return 0;
    return std::fread(buf, 1, n, impl_->f);
}
bool File::seek(uint32_t pos, SeekMode mode) {
    if (!impl_ || !impl_->f) return false;
    int whence = mode == SeekCur ? SEEK_CUR : mode == SeekEnd ? SEEK_END : SEEK_SET;
    return std::fseek(impl_->f, (long)pos, whence) == 0;
}
size_t File::position() const {
    if (!impl_ || !impl_->f) return 0;
    long p = std::ftell(impl_->f);
    return p < 0 ? 0 : (size_t)p;
}
size_t File::size() const {
    if (!impl_ || !impl_->f) return 0;
    std::fflush(impl_->f);  // what was written so far counts
    struct stat st;
    return ::fstat(fileno(impl_->f), &st) == 0 ? (size_t)st.st_size : 0;
}
void File::close() {
    if (impl_) impl_->shut();
    impl_.reset();
}
File::operator bool() const { return impl_ && (impl_->f || impl_->d); }
time_t File::getLastWrite() {
    if (!impl_) return 0;
    struct stat st;
    return ::stat(impl_->real.c_str(), &st) == 0 ? st.st_mtime : 0;
}
const char* File::path() const { return impl_ ? impl_->path.c_str() : ""; }
const char* File::name() const {
    if (!impl_) return "";
    size_t slash = impl_->path.rfind('/');
    return impl_->path.c_str() + (slash == std::string::npos ? 0 : slash + 1);
}
bool File::isDirectory() { return impl_ && impl_->d; }
File File::openNextFile(const char* mode) {
    if (!impl_ || !impl_->d) return File();
    while (dirent* e = ::readdir(impl_->d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        std::string child = (impl_->path == "/" ? "" : impl_->path) + "/" + e->d_name;
        if (auto f = openAt(impl_->base, child, mode)) return File(f);
    }
    return File();
}
void File::rewindDirectory() {
    if (impl_ && impl_->d) ::rewinddir(impl_->d);
}

// ------------------------------------------------------------------ FS
File FS::open(const char* path, const char* mode, bool create) {
    (void)create;  // (the ESP32 makes missing parent folders with it; nothing here asks for that)
    auto f = openAt(base_, inside(path), mode);
    return f ? File(f) : File();
}
bool FS::exists(const char* path) {
    struct stat st;
    return ::stat(hostPath((base_ + inside(path)).c_str()).c_str(), &st) == 0;
}
bool FS::remove(const char* path) { return ::unlink(hostPath((base_ + inside(path)).c_str()).c_str()) == 0; }
bool FS::rename(const char* from, const char* to) {
    return std::rename(hostPath((base_ + inside(from)).c_str()).c_str(), hostPath((base_ + inside(to)).c_str()).c_str()) == 0;
}
bool FS::mkdir(const char* path) { return ::mkdir(hostPath((base_ + inside(path)).c_str()).c_str(), 0777) == 0; }
bool FS::rmdir(const char* path) { return ::rmdir(hostPath((base_ + inside(path)).c_str()).c_str()) == 0; }

uint64_t FS::total() {
    struct statvfs v;
    if (::statvfs(hostPath(base_).c_str(), &v) != 0) return 0;
    return (uint64_t)v.f_blocks * v.f_frsize;
}
uint64_t FS::used() {
    struct statvfs v;
    if (::statvfs(hostPath(base_).c_str(), &v) != 0) return 0;
    return (uint64_t)(v.f_blocks - v.f_bavail) * v.f_frsize;
}

bool LittleFSFS::begin(bool formatOnFail, const char* basePath, uint8_t maxOpen, const char* label) {
    (void)formatOnFail, (void)basePath, (void)maxOpen, (void)label;
    struct stat st;
    return ::stat(hostPath(base_).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

}  // namespace fs
