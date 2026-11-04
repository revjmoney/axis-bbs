// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/rotatelog.h"

#include <sys/stat.h>

#include <cstdio>
#include <deque>

namespace axis::bbs {

namespace {
size_t fileSize(const std::string& f) {
    struct stat st;
    return stat(f.c_str(), &st) == 0 ? (size_t)st.st_size : 0;
}
void readLines(const std::string& f, std::deque<std::string>& into, size_t keep) {
    std::FILE* in = std::fopen(f.c_str(), "rb");
    if (!in) return;
    std::string line;
    for (int c; (c = std::fgetc(in)) != EOF;) {
        if (c == '\n') {
            into.push_back(line);
            if (into.size() > keep) into.pop_front();
            line.clear();
        } else if (c != '\r') {
            line += (char)c;
        }
    }
    std::fclose(in);
}
}  // namespace

void RotatingLog::append(const std::string& line) {
    if (fileSize(file_) + line.size() + 1 > limit_) {
        std::remove(old_.c_str());
        std::rename(file_.c_str(), old_.c_str());
    }
    std::FILE* out = std::fopen(file_.c_str(), "ab");
    if (!out) return;
    std::string l = line.size() + 1 > limit_ ? line.substr(0, limit_ - 1) : line;
    l += '\n';
    std::fwrite(l.data(), 1, l.size(), out);
    std::fclose(out);
}

std::vector<std::string> RotatingLog::tail(size_t n) const {
    std::deque<std::string> lines;
    readLines(old_, lines, n);
    readLines(file_, lines, n);
    return std::vector<std::string>(lines.begin(), lines.end());
}

}  // namespace axis::bbs
