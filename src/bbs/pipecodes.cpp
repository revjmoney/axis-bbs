// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "bbs/pipecodes.h"

#include <cctype>
#include <cstdlib>

namespace axis::bbs {

namespace {
const char kAnsi[8] = {'0', '4', '2', '6', '1', '5', '3', '7'};  // DOS colour order to ANSI's

bool digits(const std::string& s, size_t at, size_t n) {
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; i++)
        if (!std::isdigit((unsigned char)s[at + i])) return false;
    return true;
}
bool upper2(const std::string& s, size_t at) {
    return at + 2 <= s.size() && std::isupper((unsigned char)s[at]) && std::isupper((unsigned char)s[at + 1]);
}

// how many characters a stretch of ANSI text takes on screen (escape sequences take none)
size_t visible(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\x1b') {
            while (i < s.size() && !std::isalpha((unsigned char)s[i]) ) i++;
            continue;
        }
        if (s[i] != '\r' && s[i] != '\n') n++;
    }
    return n;
}

class Out {
public:
    explicit Out(const ArtEmit& emit) : emit_(emit) { buf_.reserve(520); }
    ~Out() { flush(); }
    void put(char c) {
        buf_ += c;
        if (buf_.size() >= 480) flush();
    }
    void put(const std::string& s) {
        for (char c : s) put(c);
    }
    void flush() {
        if (!buf_.empty()) emit_(buf_.data(), buf_.size());
        buf_.clear();
    }

private:
    const ArtEmit& emit_;
    std::string    buf_;
};

void expand(const std::string& in, const AcsUser& who, const ArtValue& value, Out& out) {
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '{' && in.compare(i, 2, "{{") == 0) {  // {{ACS}}text{{/}}
            size_t close = in.find("}}", i + 2), end = close == std::string::npos ? close : in.find("{{/}}", close + 2);
            if (close != std::string::npos && end != std::string::npos) {
                std::string acs = in.substr(i + 2, close - i - 2), inner = in.substr(close + 2, end - close - 2);
                if (acsAllows(acs, who)) {
                    expand(inner, who, value, out);
                } else {  // the same width in spaces, so the art around it stays put
                    for (size_t k = visible(expandArt(inner, who, value)); k; k--) out.put(' ');
                }
                i = end + 4;
                continue;
            }
        }
        if (c == '@') {  // @XX####@
            size_t j = i + 1;
            while (j < in.size() && std::isupper((unsigned char)in[j])) j++;
            size_t k = j;
            while (k < in.size() && in[k] == '#') k++;
            if (j > i + 1 && k > j && k < in.size() && in[k] == '@') {
                std::string v = value(in.substr(i + 1, j - i - 1));
                size_t      w = k - i + 1;
                v.resize(w, ' ');
                out.put(v);
                i = k;
                continue;
            }
        }
        if (c == '|' && i + 1 < in.size()) {
            if (digits(in, i + 1, 2)) {
                int col = std::atoi(in.substr(i + 1, 2).c_str()) & 15;
                out.put(std::string("\x1b[0;") + (col & 8 ? "1;" : "") + "3" + kAnsi[col & 7] + "m");
                i += 2;
                continue;
            }
            if (in[i + 1] == 'B' && digits(in, i + 2, 1)) {
                size_t n = digits(in, i + 2, 2) ? 2 : 1;
                int    col = std::atoi(in.substr(i + 2, n).c_str()) & 7;
                out.put(std::string("\x1b[4") + kAnsi[col] + "m");
                i += 1 + n;
                continue;
            }
            if (in.compare(i + 1, 2, "CL") == 0) {
                out.put("\x1b[2J\x1b[H");
                i += 2;
                continue;
            }
            if (in.compare(i + 1, 2, "XY") == 0 && digits(in, i + 3, 2) && i + 5 < in.size() && in[i + 5] == ',' && digits(in, i + 6, 2)) {
                out.put("\x1b[" + std::to_string(std::atoi(in.substr(i + 6, 2).c_str())) + ";" + std::to_string(std::atoi(in.substr(i + 3, 2).c_str())) + "H");
                i += 7;
                continue;
            }
            if (upper2(in, i + 1)) {
                out.put(value(in.substr(i + 1, 2)));
                i += 2;
                continue;
            }
        }
        out.put(c);
    }
}
}  // namespace

void expandArt(const std::string& in, const AcsUser& who, const ArtValue& value, const ArtEmit& emit) {
    Out out(emit);
    expand(in, who, value, out);
}

std::string expandArt(const std::string& in, const AcsUser& who, const ArtValue& value) {
    std::string s;
    expandArt(in, who, value, [&s](const char* d, size_t n) { s.append(d, n); });
    return s;
}

}  // namespace axis::bbs
