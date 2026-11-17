// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/linux/paths.h"

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

std::string dataDir;  // set once by axisDataSetup, before setup(); read only after that

// true when `p` is `dir` itself or a path under it ("/sd", "/sd/x"; not "/sdcard")
bool under(const std::string& p, const char* dir) {
    size_t n = std::strlen(dir);
    return p.compare(0, n, dir) == 0 && (p.size() == n || p[n] == '/');
}
bool boardPath(const std::string& p) { return under(p, "/sd") || under(p, "/littlefs"); }

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// mkdir -p, each folder it makes with `mode`
bool makeDirs(const std::string& path, mode_t mode) {
    for (size_t i = 1; i <= path.size(); i++) {
        if (i < path.size() && path[i] != '/') continue;
        std::string part = path.substr(0, i);
        if (::mkdir(part.c_str(), mode) != 0 && (errno != EEXIST || !isDir(part))) return false;
    }
    return true;
}

std::string absolute(const std::string& p) {
    char* r = ::realpath(p.c_str(), nullptr);
    if (!r) return "";
    std::string s = r;
    std::free(r);
    return s;
}

std::string defaultDir() {
    const passwd* pw = ::getpwuid(::geteuid());
    if (pw && std::strcmp(pw->pw_name, "axis") == 0) return "/var/lib/axis";  // the service's own user (plan task 7)
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = pw ? pw->pw_dir : nullptr;
    if (!home || !*home) return "";
    return std::string(home) + "/.axis";
}

bool emptyDir(const std::string& p) {
    DIR* d = ::opendir(p.c_str());
    if (!d) return false;
    bool empty = true;
    while (dirent* e = ::readdir(d))
        if (std::strcmp(e->d_name, ".") != 0 && std::strcmp(e->d_name, "..") != 0) {
            empty = false;
            break;
        }
    ::closedir(d);
    return empty;
}

bool copyFile(const std::string& from, const std::string& to) {
    int in = ::open(from.c_str(), O_RDONLY);
    if (in < 0) return false;
    int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (out < 0) {
        ::close(in);
        return false;
    }
    char buf[65536];
    bool ok = true;
    for (;;) {
        ssize_t n = ::read(in, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = n == 0;
            break;
        }
        for (ssize_t done = 0; done < n;) {
            ssize_t w = ::write(out, buf + done, (size_t)(n - done));
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) {
                ok = false;
                break;
            }
            done += w;
        }
        if (!ok) break;
    }
    ::close(in);
    if (::close(out) != 0) ok = false;
    return ok;
}

// copies the folders and plain files under `from` into `to` (links and devices are skipped: a card has none)
bool copyTree(const std::string& from, const std::string& to, int& files, std::string& why) {
    DIR* d = ::opendir(from.c_str());
    if (!d) {
        why = "cannot read " + from + ": " + std::strerror(errno);
        return false;
    }
    bool ok = true;
    while (dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        std::string a = from + "/" + e->d_name, b = to + "/" + e->d_name;
        struct stat st;
        if (::lstat(a.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (::mkdir(b.c_str(), 0777) != 0 && errno != EEXIST) {
                why = "cannot make " + b + ": " + std::strerror(errno);
                ok = false;
            } else {
                ok = copyTree(a, b, files, why);
            }
        } else if (S_ISREG(st.st_mode)) {
            if (copyFile(a, b)) {
                files++;
            } else {
                why = "cannot copy " + a + ": " + std::strerror(errno);
                ok = false;
            }
        }
        if (!ok) break;
    }
    ::closedir(d);
    return ok;
}

}  // namespace

std::string axisDataDir() { return dataDir; }

std::string hostPath(const char* p) {
    if (!p) return "";
    if (p[0] != '/' || p[1] == 0) return p;  // the fast way out: nothing the board names starts otherwise
    std::string s = p;
    if (under(s, "/sd")) return dataDir.empty() ? "" : dataDir + "/sd" + s.substr(3);
    if (under(s, "/littlefs")) return dataDir.empty() ? "" : dataDir + "/flash" + s.substr(9);
    return s;
}

bool axisDataSetup(const std::string& requested, const std::string& seed, std::string& why) {
    std::string want = requested.empty() ? defaultDir() : requested;
    if (want.empty()) {
        why = "no home folder to put ~/.axis in: give one with --data DIR";
        return false;
    }
    if (boardPath(want)) {  // (checked again below once it is absolute)
        why = "the data folder cannot be under /sd or /littlefs (those are the board's names for its folders)";
        return false;
    }
    if (!makeDirs(want, 0700)) {
        why = "cannot make the data folder " + want + ": " + std::strerror(errno);
        return false;
    }
    std::string dir = absolute(want);
    if (dir.empty() || dir == "/") {
        why = "the data folder cannot be " + (dir.empty() ? want : dir);
        return false;
    }
    if (boardPath(dir)) {  // its files would be mapped a second time: /sd/x would land in <data>/sd/sd/x
        why = "the data folder cannot be under /sd or /littlefs (those are the board's names for its folders)";
        return false;
    }
    for (const char* sub : {"/sd", "/flash", "/prefs", "/log"})
        if (::mkdir((dir + sub).c_str(), 0700) != 0 && errno != EEXIST) {
            why = "cannot make " + dir + sub + ": " + std::strerror(errno);
            return false;
        }
    if (!seed.empty()) {
        std::string from = absolute(seed);
        if (from.empty() || !isDir(from)) {
            why = "the seed folder " + seed + " is not a folder";
            return false;
        }
        if (boardPath(from) || under(from, dir.c_str()) || under(dir, from.c_str())) {
            why = "the seed folder cannot be under /sd or /littlefs, inside the data folder, or hold it";
            return false;
        }
        if (emptyDir(dir + "/sd")) {
            int files = 0;
            if (!copyTree(from, dir + "/sd", files, why)) return false;
            std::printf("axis: data: copied %d files from %s into %s/sd (first run)\n", files, from.c_str(), dir.c_str());
        } else {
            std::printf("axis: data: %s/sd already has files, so --seed is not used\n", dir.c_str());
        }
    }
    dataDir = dir;
    return true;
}
