// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Preferences as <prefs>/<namespace>.ini, and the cluster key as <data>/cluster.key (see arduino/Preferences.h).
#include <Preferences.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "platform/linux/clusterhost.h"
#include "platform/linux/paths.h"

namespace {

const char kKeyName[] = "key";  // namespace "clu": the cluster key, kept in its own file (never in the .ini, never logged)

// the cluster key file over whatever an .ini held under that name (the file is the only place it lives)
void overlayKey(std::map<std::string, std::string>& keys) {
    keys.erase(kKeyName);
    std::string k = clusterKeyRead();
    if (!k.empty()) keys[kKeyName] = k;
}

std::string escape(const std::string& s, bool key) {
    std::string out;
    for (unsigned char c : s) {
        if (c < 0x20 || c >= 0x7f || c == '\\' || (key && c == '=')) {
            char hex[5];
            std::snprintf(hex, sizeof hex, "\\x%02x", c);
            out += hex;
        } else {
            out += (char)c;
        }
    }
    return out;
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        int hi, lo;
        if (s[i] == '\\' && i + 3 < s.size() &&s[i + 1] == 'x' && (hi = hexDigit(s[i + 2])) >= 0 &&
            (lo = hexDigit(s[i + 3])) >= 0) {
            out += (char)(hi * 16 + lo);
            i += 3;
        } else {
            out += s[i];
        }
    }
    return out;
}

// reads a namespace file into `keys`: Loaded, Missing (never written: no keys), or Failed (it could not be read, for
// example out of file handles or no permission; `keys` is left as it was so nothing is saved over the real file)
enum class Load { Loaded, Missing, Failed };
Load load(const std::string& file, std::map<std::string, std::string>& keys) {
    FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        if (errno != ENOENT) return Load::Failed;
        keys.clear();
        return Load::Missing;
    }
    std::map<std::string, std::string> got;
    std::string line;
    for (int c;;) {
        c = std::fgetc(f);
        if (c != EOF && c != '\n') {
            line += (char)c;
            continue;
        }
        size_t eq = line.find('=');
        if (eq != std::string::npos) got[unescape(line.substr(0, eq))] = unescape(line.substr(eq + 1));
        line.clear();
        if (c == EOF) break;
    }
    bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (bad) return Load::Failed;
    keys.swap(got);
    return Load::Loaded;
}

bool okName(const char* name) {  // one file in prefs/, never a path out of it
    if (!name || !*name || name[0] == '.' || std::strlen(name) > 15) return false;  // (NVS: 15 characters at most)
    for (const char* p = name; *p; p++)
        if (*p == '/' || *p == '\\' || (unsigned char)*p < 0x20) return false;
    return true;
}

}  // namespace

bool Preferences::begin(const char* name, bool readOnly, const char* partition) {
    (void)partition;
    end();
    std::string dir = axisPrefsDir();
    if (dir.empty() || !okName(name)) return false;
    file_ = dir + "/" + name + ".ini";
    clu_ = std::strcmp(name, "clu") == 0;
    keys_.clear();
    Load got = load(file_, keys_);
    if (got == Load::Failed) {  // never open on an empty copy: a put would then save over the real settings
        std::printf("axis: prefs: cannot read %s: %s\n", file_.c_str(), std::strerror(errno));
        return false;
    }
    if (clu_) overlayKey(keys_);
    // NVS cannot open a namespace read-only before it was ever written ("clu" was, if its key file is there)
    if (readOnly && got == Load::Missing && keys_.empty()) return false;
    open_ = true;
    readOnly_ = readOnly;
    return true;
}

void Preferences::end() {
    open_ = false;
    keys_.clear();
}

// Each change re-reads the file first, so two Preferences open on the same namespace do not undo each other's writes.
bool Preferences::save() {
    std::string tmp = file_ + ".tmp";
    int         fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    std::string text;
    for (auto& kv : keys_)
        if (!clu_ || kv.first != kKeyName) text += escape(kv.first, true) + "=" + escape(kv.second, false) + "\n";
    bool ok = true;
    for (size_t done = 0; done < text.size();) {
        ssize_t w = ::write(fd, text.data() + done, text.size() - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) {
            ok = false;
            break;
        }
        done += (size_t)w;
    }
    if (ok && ::fsync(fd) != 0) ok = false;
    if (::close(fd) != 0) ok = false;
    if (!ok || std::rename(tmp.c_str(), file_.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

bool Preferences::clear() {
    if (!open_ || readOnly_) return false;
    std::map<std::string, std::string> now;
    if (load(file_, now) == Load::Failed) return false;  // (the file stays as it is)
    if (clu_) clusterKeyRemove();
    keys_.clear();
    return save();
}

bool Preferences::remove(const char* key) {
    if (!open_ || readOnly_ || !key) return false;
    if (clu_ && std::strcmp(key, kKeyName) == 0) {
        keys_.erase(kKeyName);
        return clusterKeyRemove();
    }
    if (load(file_, keys_) == Load::Failed) return false;  // re-read first; a file it cannot read is left alone
    if (clu_) overlayKey(keys_);
    if (!keys_.erase(key)) return false;
    return save();
}

bool Preferences::isKey(const char* key) { return find(key) != nullptr; }

size_t Preferences::freeEntries() { return open_ ? 500 : 0; }  // a folder on the PC: no NVS page to fill

const std::string* Preferences::find(const char* key) const {
    if (!open_ || !key) return nullptr;
    auto it = keys_.find(key);
    return it == keys_.end() ? nullptr : &it->second;
}

size_t Preferences::put(const char* key, const std::string& v, size_t bytes) {
    if (!open_ || readOnly_ || !key || !*key) return 0;
    if (clu_ && std::strcmp(key, kKeyName) == 0) {
        if (!clusterKeyWrite(v)) return 0;
        keys_[kKeyName] = v;
        return bytes;
    }
    if (load(file_, keys_) == Load::Failed) return 0;  // re-read first; a file it cannot read is left alone
    if (clu_) overlayKey(keys_);
    keys_[key] = v;
    return save() ? bytes : 0;
}

size_t Preferences::putNum(const char* key, int64_t v, size_t bytes) { return put(key, std::to_string(v), bytes); }
size_t Preferences::putULong64(const char* key, uint64_t v) { return put(key, std::to_string(v), 8); }
size_t Preferences::putFloat(const char* key, float v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", (double)v);
    return put(key, buf, 4);
}
size_t Preferences::putDouble(const char* key, double v) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return put(key, buf, 8);
}
size_t Preferences::putString(const char* key, const char* v) {
    std::string s = v ? v : "";
    return put(key, s, s.size());
}
size_t Preferences::putBytes(const char* key, const void* v, size_t n) {
    if (!v && n) return 0;
    return put(key, std::string((const char*)v, n), n);
}

int64_t Preferences::getNum(const char* key, int64_t def) {
    const std::string* s = find(key);
    if (!s || s->empty()) return def;
    char*   end = nullptr;
    int64_t v = std::strtoll(s->c_str(), &end, 10);
    return end && *end == 0 ? v : def;
}
uint64_t Preferences::getULong64(const char* key, uint64_t def) {
    const std::string* s = find(key);
    if (!s || s->empty()) return def;
    char*    end = nullptr;
    uint64_t v = std::strtoull(s->c_str(), &end, 10);
    return end && *end == 0 ? v : def;
}
float Preferences::getFloat(const char* key, float def) { return (float)getDouble(key, def); }
double Preferences::getDouble(const char* key, double def) {
    const std::string* s = find(key);
    if (!s || s->empty()) return def;
    char*  end = nullptr;
    double v = std::strtod(s->c_str(), &end);
    return end && *end == 0 ? v : def;
}
String Preferences::getString(const char* key, const String& def) {
    const std::string* s = find(key);
    return s ? String(s->c_str()) : def;
}
size_t Preferences::getBytesLength(const char* key) {
    const std::string* s = find(key);
    return s ? s->size() : 0;
}
size_t Preferences::getBytes(const char* key, void* buf, size_t max) {
    const std::string* s = find(key);
    if (!s || !buf || s->size() > max) return 0;  // NVS gives nothing when the buffer is too small
    std::memcpy(buf, s->data(), s->size());
    return s->size();
}
