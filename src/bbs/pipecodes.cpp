// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/pipecodes.h"

#include <cctype>
#include <cstdlib>
#include <exception>

namespace axis::bbs {

namespace {
const char* const kAnsi = kDosToAnsi;  // DOS colour order to ANSI's (one copy, artstyle.cpp)

// ViSiON/3's placeholder names longer than two letters, longest first (two-letter ones are any |XX)
const char* const kLongNames[] = {"NEWUSERS", "HANDLE", "ALIAS", "LEVEL", "LCALL", "CALLS", "UPLDS", "DNLDS", "POSTS",
                                  "NAME",     "CFAN",   "DATE",  "TIME",  "NODE",  "CAN",   "CCN",   "CFA",   "FCN"};

bool digits(const std::string& s, size_t at, size_t n) {
    if (at + n > s.size()) return false;
    for (size_t i = 0; i < n; i++)
        if (!std::isdigit((unsigned char)s[at + i])) return false;
    return true;
}
bool upper2(const std::string& s, size_t at) {
    return at + 2 <= s.size() && std::isupper((unsigned char)s[at]) && std::isupper((unsigned char)s[at + 1]);
}
// the placeholder name at s[at] (after a '|'), or "" when there isn't one
std::string nameAt(const std::string& s, size_t at) {
    for (const char* n : kLongNames)
        if (s.compare(at, std::char_traits<char>::length(n), n) == 0) return n;
    return upper2(s, at) ? s.substr(at, 2) : "";
}
bool isControl(const std::string& n, const ArtStyle& style) {
    return n == "CL" || n == "DE" || n == "CR" || n == "PP" || n == "XY" || (style.dialect == Dialect::Renegade && n == "CS");
}

// how many characters a stretch of ANSI text takes on screen (escape sequences take none)
size_t visible(const std::string& s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '\x1b') {
            while (i < s.size() && !std::isalpha((unsigned char)s[i])) i++;
            continue;
        }
        if (s[i] != '\r' && s[i] != '\n') n++;
    }
    return n;
}

// an optional group keeps its text unless it holds placeholders and every one of them is empty
bool keepGroup(const std::string& inner, const ArtValue& value, const ArtStyle& style) {
    bool any = false;
    for (size_t i = 0; i + 1 < inner.size(); i++) {
        if (style.dialect != Dialect::Axis && inner[i] == '|' && inner[i + 1] == '|') {  // a literal bar, not a code
            i++;
            continue;
        }
        std::string n = inner[i] == '|' ? nameAt(inner, i + 1) : "";
        if (n.empty() || isControl(n, style)) continue;
        any = true;
        std::string v = value(n);
        for (char c : v)
            if (c != ' ') return true;
        i += n.size();
    }
    return !any;
}

class Out {
public:
    explicit Out(const ArtEmit& emit) : emit_(emit) { buf_.reserve(520); }
    ~Out() {
        if (std::uncaught_exceptions() == unwinding_) flush();  // (not while a failed write unwinds: nothing more goes out)
    }
    void put(char c) {
        buf_ += c;
        if (buf_.size() >= 480) flush();
    }
    void put(const std::string& s) {
        for (char c : s) put(c);
    }
    void flush() {
        if (buf_.empty()) return;
        struct Clear {  // emptied even when the write throws (bad_alloc): the piece is never handed over twice
            std::string& b;
            ~Clear() { b.clear(); }
        } clear{buf_};
        emit_(buf_.data(), buf_.size());
    }

private:
    const int      unwinding_ = std::uncaught_exceptions();
    const ArtEmit& emit_;
    std::string    buf_;
};

void expand(const std::string& in, const AcsUser& who, const ArtValue& value, Out& out, const ArtStyle& style) {
    for (size_t i = 0; i < in.size(); i++) {
        char c = in[i];
        if (c == '{' && in.compare(i, 2, "{{") == 0) {  // {{ACS}}text{{/}}
            size_t close = in.find("}}", i + 2), end = close == std::string::npos ? close : in.find("{{/}}", close + 2);
            if (close != std::string::npos && end != std::string::npos) {
                std::string acs = in.substr(i + 2, close - i - 2), inner = in.substr(close + 2, end - close - 2);
                if (acsAllows(acs, who)) {
                    expand(inner, who, value, out, style);
                } else {  // the same width in spaces, so the art around it stays put
                    for (size_t k = visible(expandArt(inner, who, value, style)); k; k--) out.put(' ');
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
        if (c == '%' && upper2(in, i + 1)) {  // AXIS's own %XX codes, as the existing theme art writes them
            out.put(value(in.substr(i, 3)));
            i += 2;
            continue;
        }
        if (c == '^' && style.dialect == Dialect::Renegade && digits(in, i + 1, 1)) {  // ^0..^9: the look's colour slot
            out.put(attrSgr(style.r[in[i + 1] - '0']));
            i += 1;
            continue;
        }
        if (c != '|' || i + 1 >= in.size()) {
            out.put(c);
            continue;
        }
        if (in[i + 1] == '|' && style.dialect != Dialect::Axis) {  // || is one literal bar
            out.put('|');
            i += 1;
            continue;
        }
        if (in[i + 1] == '{') {  // an optional group |{ ... |}; |{P} and |{O} are login markers: left as they are
            if (in.compare(i, 4, "|{P}") == 0 || in.compare(i, 4, "|{O}") == 0) {
                out.put(in.substr(i, 4));
                i += 3;
                continue;
            }
            size_t end = in.find("|}", i + 2);
            if (end != std::string::npos) {
                std::string inner = in.substr(i + 2, end - i - 2);
                if (keepGroup(inner, value, style)) expand(inner, who, value, out, style);
                i = end + 1;
                continue;
            }
        }
        if (digits(in, i + 1, 2)) {
            int n = std::atoi(in.substr(i + 1, 2).c_str());
            if (style.dialect == Dialect::Renegade && n >= 16 && n <= 31) {  // |16..|23 background, |24..|31 the same blinking (iCE bright)
                out.put(std::string("\x1b[") + (n >= 24 ? "5;" : "") + "4" + kAnsi[(n - 16) & 7] + "m");
            } else if (n == 23) out.put("\x1b[0m");  // reset
            else out.put(std::string("\x1b[0;") + (n & 8 ? "1;" : "") + "3" + kAnsi[n & 7] + "m");
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
        if (in.compare(i + 1, 2, "XY") == 0 && digits(in, i + 3, 2) && i + 5 < in.size() && in[i + 5] == ',' && digits(in, i + 6, 2)) {
            out.put("\x1b[" + std::to_string(std::atoi(in.substr(i + 6, 2).c_str())) + ";" + std::to_string(std::atoi(in.substr(i + 3, 2).c_str())) + "H");
            i += 7;
            continue;
        }
        if (style.dialect == Dialect::Vision3 && in[i + 1] == 'C' && i + 2 < in.size() && in[i + 2] >= '1' && in[i + 2] <= '7') {
            out.put(attrSgr(style.c[in[i + 2] - '1']));  // |C1..|C7: the look's colour slot
            i += 2;
            continue;
        }
        if (style.dialect == Dialect::Renegade && in.compare(i + 1, 2, "CS") == 0) {  // |CS clears the screen like |CL
            out.put("\x1b[2J\x1b[H");
            i += 2;
            continue;
        }
        static const struct { const char* code; const char* ansi; } kControls[] = {
            {"CL", "\x1b[2J\x1b[H"}, {"DE", "\x1b[K"}, {"CR", "\r\n"}, {"PP", "\x1b[u"}};
        bool done = false;
        for (const auto& k : kControls)
            if (in.compare(i + 1, 2, k.code) == 0) {
                out.put(k.ansi);
                i += 2;
                done = true;
                break;
            }
        if (done) continue;
        std::string n = nameAt(in, i + 1);
        if (!n.empty()) {
            out.put(value(n));
            i += n.size();
            continue;
        }
        if (in[i + 1] == 'P') {  // |P save the cursor (|PP, restore, is above)
            out.put("\x1b[s");
            i += 1;
            continue;
        }
        out.put(c);
    }
}
}  // namespace

void expandArt(const std::string& in, const AcsUser& who, const ArtValue& value, const ArtEmit& emit, const ArtStyle& style) {
    Out out(emit);
    expand(in, who, value, out, style);
}

void artPiece(std::string& carry, const char* raw, size_t n, bool last, const AcsUser& who, const ArtValue& value, const ArtEmit& emit,
              const ArtStyle& style, bool* ended) {
    if (ended && *ended) {  // Ctrl-Z came in an earlier piece: nothing more is drawn
        if (last) std::string().swap(carry);
        return;
    }
    carry.append(raw, n);
    if (size_t z = carry.find('\x1a'); z != std::string::npos) {  // Ctrl-Z: it and all after it go (SAUCE is never drawn)
        carry.resize(z);
        last = true;
        if (ended) *ended = true;
    }
    size_t cut = carry.size();
    if (!last) {
        cut = carry.rfind('\n');
        cut = cut != std::string::npos ? cut + 1 : carry.size() > 1024 ? carry.size() - 32 : 0;
    }
    if (cut) {
        expandArt(carry.substr(0, cut), who, value, emit, style);
        carry.erase(0, cut);
    }
    if (last) std::string().swap(carry);
}

std::string expandArt(const std::string& in, const AcsUser& who, const ArtValue& value, const ArtStyle& style) {
    std::string s;
    expandArt(in, who, value, [&s](const char* d, size_t n) { s.append(d, n); }, style);
    return s;
}

}  // namespace axis::bbs
