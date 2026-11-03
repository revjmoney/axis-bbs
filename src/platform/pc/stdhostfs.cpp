// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/pc/stdhostfs.h"

#include "door/pathresolve.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>

namespace fs = std::filesystem;

namespace axis {
namespace pc {

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
void dosStamp(fs::file_time_type t, uint16_t& date, uint16_t& time) {
    auto        sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sys);
    std::tm     lt = *std::localtime(&tt);
    date = (uint16_t)(((lt.tm_year - 80) << 9) | ((lt.tm_mon + 1) << 5) | lt.tm_mday);
    time = (uint16_t)((lt.tm_hour << 11) | (lt.tm_min << 5) | (lt.tm_sec / 2));
}
}  // namespace

StdHostFs::StdHostFs(fs::path root) : root_(std::move(root)) {}

StdHostFs::~StdHostFs() {
    for (auto& o : open_) std::fclose(o.second.f);
}

// "C:\LORD\X.DAT" or "X.DAT" (relative to cwd) -> host path inside the door folder, matching
// existing names in any case (the shared resolver, the same one the board uses)
fs::path StdHostFs::resolve(const std::string& dosPath) const {
    std::string rel = door::resolveDosPath(cwd_, dosPath, [this](const std::string& dir) {
        std::vector<std::string> names;
        std::error_code          ec;
        for (auto& e : fs::directory_iterator(dir.empty() ? root_ : root_ / dir, ec)) names.push_back(e.path().filename().string());
        return names;
    });
    return rel.empty() ? root_ : root_ / rel;
}

int StdHostFs::open(const std::string& dosPath, int access, bool create, bool truncate, bool exclusive) {
    fs::path        p = resolve(dosPath);
    std::error_code ec;
    bool            exists = fs::exists(p, ec);
    if (exists && fs::is_directory(p, ec)) return -5;
    if (exclusive && exists) return -80;  // file exists
    if (!exists && !create) return fs::exists(p.parent_path(), ec) ? -2 : -3;
    if (create && !fs::exists(p.parent_path(), ec)) return -3;
    const char* mode = (create && (truncate || !exists)) ? "w+b" : access == 0 ? "rb" : "r+b";
    std::FILE*  f = std::fopen(p.string().c_str(), mode);
    if (!f) return access == 0 ? -2 : -5;
    int h = next_++;
    open_[h] = {f, p};
    return h;
}

int StdHostFs::read(int h, void* buf, int n) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    if (it->second.writing) {  // switching from writing to reading needs a seek (never fflush a read stream)
        std::fseek(it->second.f, 0, SEEK_CUR);
        it->second.writing = false;
    }
    return (int)std::fread(buf, 1, n, it->second.f);
}

int StdHostFs::write(int h, const void* buf, int n) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    std::FILE* f = it->second.f;
    if (n == 0) {  // DOS: a zero-length write sets the file size to the current position
        long pos = std::ftell(f);
        std::fflush(f);
        std::error_code ec;
        fs::resize_file(it->second.path, (uintmax_t)pos, ec);
        std::fseek(f, pos, SEEK_SET);
        return ec ? -5 : 0;
    }
    if (!it->second.writing) {
        std::fseek(f, 0, SEEK_CUR);  // switching from reading to writing needs a seek
        it->second.writing = true;
    }
    int w = (int)std::fwrite(buf, 1, n, f);
    std::fflush(f);  // other nodes read the same files: keep the disk current
    return w;
}

int64_t StdHostFs::seek(int h, int64_t off, int whence) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    std::FILE* f = it->second.f;
    it->second.writing = false;
    if (std::fseek(f, (long)off, whence == 0 ? SEEK_SET : whence == 1 ? SEEK_CUR : SEEK_END) != 0) return -25;
    return std::ftell(f);
}

int StdHostFs::close(int h) {
    auto it = open_.find(h);
    if (it == open_.end()) return -6;
    std::fclose(it->second.f);
    open_.erase(it);
    return 0;
}

bool StdHostFs::stat(const std::string& dosPath, door::FileInfo& out) {
    fs::path        p = resolve(dosPath);
    std::error_code ec;
    if (!fs::exists(p, ec)) return false;
    out.attr = fs::is_directory(p, ec) ? 0x10 : 0x20;
    out.size = out.attr & 0x10 ? 0 : (uint32_t)fs::file_size(p, ec);
    dosStamp(fs::last_write_time(p, ec), out.dosDate, out.dosTime);
    out.name83 = upper(p.filename().string());
    return true;
}

int StdHostFs::remove(const std::string& dosPath) {
    std::error_code ec;
    fs::path        p = resolve(dosPath);
    if (!fs::exists(p, ec)) return -2;
    return fs::remove(p, ec) ? 0 : -5;
}

int StdHostFs::rename(const std::string& from, const std::string& to) {
    std::error_code ec;
    fs::path        a = resolve(from), b = resolve(to);
    if (!fs::exists(a, ec)) return -2;
    if (fs::exists(b, ec)) return -5;
    fs::rename(a, b, ec);
    return ec ? -5 : 0;
}

int StdHostFs::mkdir(const std::string& dosPath) {
    std::error_code ec;
    return fs::create_directory(resolve(dosPath), ec) ? 0 : -5;
}

int StdHostFs::rmdir(const std::string& dosPath) {
    std::error_code ec;
    fs::path        p = resolve(dosPath);
    if (!fs::is_directory(p, ec)) return -3;
    return fs::remove(p, ec) ? 0 : -5;
}

bool StdHostFs::isDir(const std::string& dosPath) {
    std::error_code ec;
    return fs::is_directory(resolve(dosPath), ec);
}

bool StdHostFs::find(const std::string& dosPattern, uint8_t attr, door::FindState& st) {
    st = {};
    std::string p = dosPattern;
    std::replace(p.begin(), p.end(), '/', '\\');
    size_t      slash = p.find_last_of("\\:");
    std::string dir = slash == std::string::npos ? "" : p.substr(0, slash + 1);
    std::string pat = slash == std::string::npos ? p : p.substr(slash + 1);
    if (dir.size() == 2 && dir[1] == ':') dir = "";
    fs::path        d = dir.empty() ? resolve("") : resolve(dir);
    std::error_code ec;
    for (auto& e : fs::directory_iterator(d, ec)) {
        std::string name = e.path().filename().string();
        bool        isdir = e.is_directory(ec);
        if (isdir && !(attr & 0x10)) continue;
        if (!wild(pat, name)) continue;
        door::FileInfo fi;
        fi.attr = isdir ? 0x10 : 0x20;
        fi.size = isdir ? 0 : (uint32_t)e.file_size(ec);
        dosStamp(e.last_write_time(ec), fi.dosDate, fi.dosTime);
        fi.name83 = upper(name);
        st.matches.push_back(fi);
    }
    return !st.matches.empty();
}

int StdHostFs::fileKey(int h) {
    auto it = open_.find(h);
    if (it == open_.end()) return -1;
    std::error_code ec;
    std::string     k = lower(fs::weakly_canonical(it->second.path, ec).string());
    for (size_t i = 0; i < keys_.size(); i++)
        if (keys_[i] == k) return (int)i;
    keys_.push_back(k);
    return (int)keys_.size() - 1;
}

bool StdHostFs::fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) {
    auto it = open_.find(h);
    if (it == open_.end()) return false;
    std::error_code ec;
    dosStamp(fs::last_write_time(it->second.path, ec), dosDate, dosTime);
    return !ec;
}

}  // namespace pc
}  // namespace axis
