// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/filelib.h"

#include <cctype>
#include <cstdio>

namespace axis::bbs {

namespace {
std::string upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
}  // namespace

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

std::string fileSizeText(uint32_t bytes) {
    char b[16];
    if (bytes < 1024) std::snprintf(b, sizeof b, "%u", (unsigned)bytes);
    else if (bytes < 1024u * 1024u) std::snprintf(b, sizeof b, "%uK", (unsigned)(bytes / 1024));
    else std::snprintf(b, sizeof b, "%.1fM", bytes / (1024.0 * 1024.0));
    return b;
}

bool isTextFile(const std::string& name) {
    size_t dot = name.rfind('.');
    if (dot == std::string::npos) return false;
    std::string ext = upper(name.substr(dot + 1));
    for (const char* t : {"TXT", "NFO", "DIZ", "ANS", "ASC", "BBS", "DOC", "MD", "1ST"})
        if (ext == t) return true;
    return false;
}

}  // namespace axis::bbs
