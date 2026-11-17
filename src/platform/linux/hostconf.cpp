// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// <data>/axis.conf (hostconf.h).
#include "platform/linux/hostconf.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace {
std::map<std::string, std::string> conf;

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
}  // namespace

void hostConfLoad(const std::string& dataDir) {
    conf.clear();
    std::string path = dataDir + "/axis.conf";
    FILE*       f = std::fopen(path.c_str(), "r");
    if (!f) {
        if (errno == ENOENT) std::printf("axis: conf: no %s, using the defaults\n", path.c_str());
        else std::printf("axis: conf: cannot read %s (%s), using the defaults\n", path.c_str(), std::strerror(errno));
        return;
    }
    char line[512];
    int  n = 0;
    while (std::fgets(line, sizeof line, f)) {
        n++;
        std::string s = line;
        size_t      hash = s.find('#');
        if (hash != std::string::npos) s.erase(hash);
        s = trim(s);
        if (s.empty()) continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) {
            std::printf("axis: conf: %s line %d has no '=', skipped\n", path.c_str(), n);
            continue;
        }
        std::string k = trim(s.substr(0, eq)), v = trim(s.substr(eq + 1));
        if (!k.empty()) conf[k] = v;
    }
    std::fclose(f);
    std::printf("axis: conf: read %s (%zu settings)\n", path.c_str(), conf.size());
}

std::string hostConfStr(const char* key, const std::string& def) {
    auto it = conf.find(key);
    return it == conf.end() ? def : it->second;
}

long hostConfInt(const char* key, long def, long lo, long hi) {
    auto it = conf.find(key);
    if (it == conf.end()) return def;
    const char* s = it->second.c_str();
    char*       end = nullptr;
    errno = 0;
    long v = std::strtol(s, &end, 10);
    if (errno || end == s || *end || v < lo || v > hi) {
        std::printf("axis: conf: %s=%s is not a number from %ld to %ld, using %ld\n", key, s, lo, hi, def);
        return def;
    }
    return v;
}
