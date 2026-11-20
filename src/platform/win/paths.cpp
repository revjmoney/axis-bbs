// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The data folder on Windows. The layout matches the Linux program (paths.h): sd/, flash/, prefs/, log/ and the
// rest. The default is %LOCALAPPDATA%\axis, or %USERPROFILE%\.axis when that is unset. Unix modes are not
// applied: Windows has ACLs, and a directory made here keeps the user's default ACL.
#include "platform/linux/paths.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <direct.h>
#include <io.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef O_BINARY
#define O_BINARY 0
#endif

namespace {

std::string dataDir;
Profile     profile = Profile::Solo;

bool under(const std::string& p, const char* dir) {
    size_t n = std::strlen(dir);
    return p.compare(0, n, dir) == 0 && (p.size() == n || p[n] == '/');
}
bool boardPath(const std::string& p) { return under(p, "/sd") || under(p, "/littlefs"); }

std::string slash(std::string p) {
    for (char& c : p)
        if (c == '\\') c = '/';
    return p;
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR);
}

bool makeDirs(const std::string& path) {
    std::string norm = slash(path);
    size_t      i = 0;
    if (norm.size() >= 2 && ((norm[0] >= 'A' && norm[0] <= 'Z') || (norm[0] >= 'a' && norm[0] <= 'z')) && norm[1] == ':')
        i = 2;  // "C:" is not a directory to make
    if (norm.size() >= 2 && norm[0] == '/' && norm[1] == '/') {
        int slashes = 0;
        i = 2;
        for (; i < norm.size() && slashes < 2; i++)
            if (norm[i] == '/') slashes++;
    }
    for (size_t j = (i == 0 ? 1 : i + 1); j <= norm.size(); j++) {
        if (j < norm.size() && norm[j] != '/') continue;
        std::string part = norm.substr(0, j);
        if (part.empty() || part == "/" || (part.size() == 2 && part[1] == ':')) continue;
        if (_mkdir(part.c_str()) != 0 && (errno != EEXIST || !isDir(part))) return false;
    }
    return isDir(norm);
}

std::string absolute(const std::string& p) {
    char buf[4096];
    if (!_fullpath(buf, p.c_str(), sizeof buf)) return "";
    std::string s = slash(buf);
    while (s.size() > 3 && (s.back() == '/')) s.pop_back();
    return s;
}

std::string defaultDir() {
    const char* local = std::getenv("LOCALAPPDATA");
    if (local && *local) return slash(local) + "/axis";
    const char* home = std::getenv("USERPROFILE");
    if (!home || !*home) home = std::getenv("HOME");
    if (!home || !*home) return "";
    return slash(home) + "/.axis";
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
    int in = ::open(from.c_str(), O_RDONLY | O_BINARY);
    if (in < 0) return false;
    int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0666);
    if (out < 0) {
        ::close(in);
        return false;
    }
    char buf[65536];
    bool ok = true;
    for (;;) {
        int n = (int)::read(in, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = n == 0;
            break;
        }
        for (int done = 0; done < n;) {
            int w = (int)::write(out, buf + done, (unsigned)(n - done));
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
        if (::stat(a.c_str(), &st) != 0) continue;
        if (st.st_mode & _S_IFDIR) {
            if (_mkdir(b.c_str()) != 0 && errno != EEXIST) {
                why = "cannot make " + b + ": " + std::strerror(errno);
                ok = false;
            } else {
                ok = copyTree(a, b, files, why);
            }
        } else if (st.st_mode & _S_IFREG) {
            if (copyFile(a, b)) files++;
            else {
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

void    axisSetProfile(Profile p) { profile = p; }
Profile axisProfile() { return profile; }
const char* axisProfileName(Profile p) {
    return p == Profile::Follower ? "follower" : p == Profile::MirrorMain ? "mirror main" : "solo";
}

std::string axisPrefsDir() {
    if (dataDir.empty()) return "";
    return dataDir + (profile == Profile::Solo ? "/prefs" : "/cluster/prefs");
}

std::string hostPath(const char* p) {
    if (!p) return "";
    if (p[0] != '/' || p[1] == 0) return p;
    std::string s = p;
    if (under(s, "/sd")) {
        if (dataDir.empty() || profile == Profile::Follower) return "";
        return dataDir + (profile == Profile::MirrorMain ? "/mirror" : "/sd") + s.substr(3);
    }
    if (under(s, "/littlefs")) {
        if (dataDir.empty()) return "";
        return dataDir + (profile == Profile::Solo ? "/flash" : "/cluster/flash") + s.substr(9);
    }
    return s;
}

bool axisDataSetup(const std::string& requested, const std::string& seed, std::string& why) {
    std::string want = requested.empty() ? defaultDir() : slash(requested);
    if (want.empty()) {
        why = "no home folder to put the data folder in (LOCALAPPDATA or USERPROFILE): give one with --data DIR";
        return false;
    }
    if (boardPath(want)) {
        why = "the data folder cannot be under /sd or /littlefs (those are the board's names for its folders)";
        return false;
    }
    if (!makeDirs(want)) {
        why = "cannot make the data folder " + want + ": " + std::strerror(errno);
        return false;
    }
    std::string dir = absolute(want);
    if (dir.empty() || dir == "/") {
        why = "the data folder cannot be " + (dir.empty() ? want : dir);
        return false;
    }
    if (boardPath(dir)) {
        why = "the data folder cannot be under /sd or /littlefs (those are the board's names for its folders)";
        return false;
    }
    for (const char* sub : {"/sd", "/flash", "/prefs", "/log", "/cluster", "/cluster/flash", "/cluster/prefs", "/mirror"})
        if (_mkdir((dir + sub).c_str()) != 0 && errno != EEXIST) {
            why = "cannot make " + dir + sub + ": " + std::strerror(errno);
            return false;
        }
    std::string seeding = dir + "/.seeding";
    if (::_access(seeding.c_str(), 0) == 0)
        std::printf("axis: data: WARNING: an earlier --seed copy into %s/sd did not finish, so sd/ may be missing files "
                    "(empty sd/ and start again with --seed to copy it afresh, or delete %s to keep it as it is)\n",
                    dir.c_str(), seeding.c_str());
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
            int mark = ::open(seeding.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
            if (mark >= 0) ::close(mark);
            if (!copyTree(from, dir + "/sd", files, why)) {
                why += " (the copy stopped partway: " + dir + "/sd is incomplete)";
                return false;
            }
            ::_unlink(seeding.c_str());
            std::printf("axis: data: copied %d files from %s into %s/sd (first run)\n", files, from.c_str(), dir.c_str());
        } else {
            std::printf("axis: data: %s/sd already has files, so --seed is not used\n", dir.c_str());
        }
    }
    dataDir = dir;
    return true;
}
