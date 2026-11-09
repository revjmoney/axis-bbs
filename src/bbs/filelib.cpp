// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/filelib.h"

#include <cctype>
#include <cstdio>

namespace axis::bbs {

namespace {
struct MemoryFile : RandomFile {
    std::string data;
    uint32_t    size() const override { return (uint32_t)data.size(); }
    bool        readAt(uint32_t off, uint8_t* buf, size_t n) override {
        if ((size_t)off + n > data.size()) return false;
        data.copy((char*)buf, n, off);
        return true;
    }
};

std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
}  // namespace

std::unique_ptr<RandomFile> FileLibrary::open(const std::string& areaId, const std::string& name) {
    std::unique_ptr<MemoryFile> f(new MemoryFile);
    if (!read(areaId, name, f->data, 64u * 1024 * 1024)) return nullptr;
    return std::unique_ptr<RandomFile>(f.release());
}

std::vector<FileEntry> mergeFilesBbs(const std::string& filesBbs,
                                     const std::vector<std::pair<std::string, uint32_t>>& folder) {
    std::vector<FileEntry> out;
    std::vector<bool>      used(folder.size(), false);
    size_t                 pos = 0;
    while (pos < filesBbs.size()) {
        size_t end = filesBbs.find('\n', pos);
        if (end == std::string::npos) end = filesBbs.size();
        std::string line = filesBbs.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t sp = line.find(' ');
        std::string name = upper(line.substr(0, sp));
        if (name.empty()) continue;
        size_t      d = sp == std::string::npos ? std::string::npos : line.find_first_not_of(' ', sp);
        std::string desc = d == std::string::npos ? "" : line.substr(d);
        for (size_t i = 0; i < folder.size(); i++)
            if (!used[i] && upper(folder[i].first) == name) {
                used[i] = true;
                out.push_back({folder[i].first, folder[i].second, desc});
                break;
            }
    }
    for (size_t i = 0; i < folder.size(); i++)
        if (!used[i] && upper(folder[i].first) != "FILES.BBS") out.push_back({folder[i].first, folder[i].second, ""});
    return out;
}

bool safeFileName(const std::string& n) {
    if (n.empty() || n.size() > 64 || n[0] == '.') return false;
    for (unsigned char c : n)
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':') return false;
    return true;
}

std::string fileSizeText(uint32_t bytes) {
    char b[16];
    if (bytes < 1024) std::snprintf(b, sizeof b, "%u", (unsigned)bytes);
    else if (bytes < 1024u * 1024u) std::snprintf(b, sizeof b, "%uK", (unsigned)(bytes / 1024));
    else std::snprintf(b, sizeof b, "%.1fM", bytes / (1024.0 * 1024.0));
    return b;
}

std::string fileAbout(const std::string& desc, size_t width) {
    if (width == 0 || desc.empty()) return "";
    std::string low(desc.size(), '\0');
    for (size_t i = 0; i < desc.size(); i++) low[i] = (char)std::tolower((unsigned char)desc[i]);
    size_t movie = low.find("movie");
    if (desc.size() <= width && (movie == std::string::npos || movie + 5 <= desc.size())) return desc;
    if (movie == std::string::npos || movie + 5 <= width) return desc.substr(0, width);
    size_t colon = low.rfind(':', movie);
    std::string title = colon == std::string::npos ? desc : desc.substr(0, colon);
    while (!title.empty() && title.back() == ' ') title.pop_back();
    size_t group = title.find(':');
    if (group != std::string::npos) {
        size_t t = group + 1;
        while (t < title.size() && title[t] == ' ') t++;
        if (title.size() - t >= 8) title = title.substr(t);
    }
    std::string s = "ANSI movies: " + title;
    if (s.size() > width) s.resize(width);
    return s;
}

bool isTextFile(const std::string& name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = upper(name.substr(dot + 1));
    for (const char* t : {"TXT", "NFO", "DIZ", "ANS", "ASC", "BBS", "DOC", "MD", "1ST", "NC", "GC", "NGC", "GCODE", "TAP", "CNC"})  // G-code is text too
        if (ext == t) return true;
    return false;
}

}  // namespace axis::bbs
