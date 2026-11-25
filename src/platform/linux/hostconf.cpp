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
std::string                        confPath;  // <data>/axis.conf, even when the file is not there yet

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool keyOk(const char* key) {
    if (!key || !*key) return false;
    for (const char* p = key; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}
}  // namespace

void hostConfLoad(const std::string& dataDir) {
    conf.clear();
    confPath = dataDir.empty() ? std::string() : dataDir + "/axis.conf";
    std::string path = confPath;
    FILE*       f = std::fopen(path.c_str(), "rb");  // binary: a Windows text read would turn \n into the file's \r\n and back
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

bool hostConfRewrite(const std::string& text, const char* key, const std::string& value, std::string& out) {
    if (!keyOk(key) || value.find('\n') != std::string::npos || value.find('\r') != std::string::npos ||
        value.find('#') != std::string::npos)
        return false;
    std::string next;
    bool        found = false;
    size_t      i = 0;
    while (i < text.size()) {
        size_t eol = text.find('\n', i);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(i, eol - i);
        bool        cr = !line.empty() && line.back() == '\r';
        if (cr) line.pop_back();
        std::string comment;
        std::string code = line;
        size_t      hash = line.find('#');
        if (hash != std::string::npos) {
            comment = line.substr(hash);
            code = line.substr(0, hash);
        }
        std::string body = trim(code);
        size_t      eq = body.find('=');
        std::string got = eq == std::string::npos ? std::string() : trim(body.substr(0, eq));
        if (!got.empty() && got == key) {
            found = true;
            next += key;
            next += '=';
            next += value;
            if (!comment.empty()) {
                next += ' ';
                next += comment;
            }
        } else {
            next += line;
        }
        if (cr) next += '\r';
        if (eol < text.size()) next += '\n';
        i = eol < text.size() ? eol + 1 : text.size();
    }
    if (!found) {
        if (!next.empty() && next.back() != '\n') next += '\n';
        next += key;
        next += '=';
        next += value;
        next += '\n';
    }
    out = std::move(next);
    return true;
}

bool hostConfSet(const char* key, const std::string& value) {
    std::string next;
    std::string cur;
    if (confPath.empty()) return false;
    FILE* f = std::fopen(confPath.c_str(), "rb");
    if (f) {
        char buf[1024];
        while (std::fgets(buf, sizeof buf, f)) cur += buf;
        std::fclose(f);
    }
    if (!hostConfRewrite(cur, key, value, next)) return false;
    std::string tmp = confPath + ".tmp";
    FILE*       w = std::fopen(tmp.c_str(), "wb");
    if (!w) return false;
    size_t n = std::fwrite(next.data(), 1, next.size(), w);
    if (n != next.size() || std::fflush(w) != 0) {
        std::fclose(w);
        std::remove(tmp.c_str());
        return false;
    }
    if (std::fclose(w) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    if (std::rename(tmp.c_str(), confPath.c_str()) != 0) {
#ifdef _WIN32
        // Windows will not rename onto a file that is already there. Linux rename replaces it, so this is not that path.
        if (std::remove(confPath.c_str()) != 0 || std::rename(tmp.c_str(), confPath.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
#else
        std::remove(tmp.c_str());
        return false;
#endif
    }
    conf[key] = value;
    return true;
}
