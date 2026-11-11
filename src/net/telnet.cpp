// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "net/telnet.h"

#include <cctype>
#include <cstdlib>

namespace axis::net {

namespace {
constexpr uint8_t IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240;
constexpr uint8_t BINARY = 0, ECHO = 1, SGA = 3, NAWS = 31;
constexpr size_t  kMaxSubneg = 64;                    // NAWS needs 5; nothing else we use needs more ...
constexpr size_t  kMaxLinkSubneg = 1 + kLinkPayloadMax;  // ... but an LFD3K-LINK report: its option byte and payload

std::string cmd(uint8_t verb, uint8_t opt) { return {(char)IAC, (char)verb, (char)opt}; }
size_t      subnegCap(const std::string& sb) { return !sb.empty() && (uint8_t)sb[0] == kLinkOption ? kMaxLinkSubneg : kMaxSubneg; }

// a name field: letters, digits and - _ . only, 24 at most; anything else leaves it unknown
bool nameOk(const std::string& v) {
    if (v.empty() || v.size() > 24) return false;
    for (char c : v)
        if (!std::isalnum((unsigned char)c) && c != '-' && c != '_' && c != '.') return false;
    return true;
}
// "-81", "7.25", "1.8": a whole number, or tenths when `tenths` (more decimals are cut). False for anything else.
bool number(const std::string& v, bool tenths, long& out) {
    size_t i = 0;
    bool   neg = false;
    if (i < v.size() && (v[i] == '-' || v[i] == '+')) neg = v[i++] == '-';
    long   whole = 0;
    size_t digits = 0;
    for (; i < v.size() && std::isdigit((unsigned char)v[i]); i++, digits++)
        if (digits < 9) whole = whole * 10 + (v[i] - '0');
    long frac = 0;
    if (i < v.size() && v[i] == '.') {
        i++;
        if (i < v.size() && std::isdigit((unsigned char)v[i])) frac = v[i] - '0';
        while (i < v.size() && std::isdigit((unsigned char)v[i])) i++;
    }
    if (!digits || i != v.size() || digits > 9) return false;
    out = tenths ? whole * 10 + frac : whole;
    if (neg) out = -out;
    return true;
}
long clampL(long v, long lo, long hi) { return v < lo ? lo : v > hi ? hi : v; }
}  // namespace

int LinkInfo::sf() const {
    if (tier.size() < 3 || tier[0] != 'S' || tier[1] != 'F') return 0;
    int n = std::atoi(tier.c_str() + 2);
    return n >= 5 && n <= 12 ? n : 0;
}

bool parseLinkInfo(const std::string& payload, LinkInfo& out) {
    if (payload.size() > kLinkPayloadMax) return false;
    for (char c : payload)
        if ((unsigned char)c < 0x20 || (unsigned char)c > 0x7e) return false;
    LinkInfo l;
    l.any = true;
    bool   versioned = false;
    size_t pos = 0;
    while (pos < payload.size()) {
        size_t end = payload.find(' ', pos);
        if (end == std::string::npos) end = payload.size();
        std::string tok = payload.substr(pos, end - pos);
        pos = end + 1;
        size_t eq = tok.find('=');
        if (eq == std::string::npos || eq == 0) continue;
        std::string k = tok.substr(0, eq), v = tok.substr(eq + 1);
        long        n = 0;
        if (k == "v") {
            if (!number(v, false, n) || n < 1) return false;
            l.version = (int)clampL(n, 1, 999);
            versioned = true;
        } else if (k == "modem") {
            if (nameOk(v)) l.modem = v;
        } else if (k == "caller") {
            if (v != "-" && nameOk(v)) l.caller = v;
        } else if (k == "kind") {
            if (v == "radio" || v == "wifi") l.kind = v;
        } else if (k == "tier") {
            if (v == "WIFI") l.tier = v;
            else if (v.size() >= 3 && v.size() <= 4 && v[0] == 'S' && v[1] == 'F' && number(v.substr(2), false, n) && n >= 5 && n <= 12)
                l.tier = "SF" + std::to_string(n);
        } else if (k == "bps") {
            if (number(v, false, n) && n >= 0) l.bps = (int32_t)clampL(n, 0, 100000000);
        } else if (k == "snr") {
            if (number(v, true, n)) l.snrTenths = (int)clampL(n, -999, 999), l.hasSnr = true;
        } else if (k == "rssi") {
            if (number(v, false, n)) l.rssi = (int)clampL(n, -200, 50), l.hasRssi = true;
        } else if (k == "retrains") {
            if (number(v, false, n) && n >= 0) l.retrains = (int32_t)clampL(n, 0, 999999);
        } else if (k == "lz") {
            if (number(v, true, n) && n >= 0) l.lzTenths = (int)clampL(n, 0, 999);
        }  // a key from a newer modem: ignored
    }
    if (!versioned) return false;
    out = l;
    return true;
}

std::string linkLabel(const LinkInfo& l, bool withRssi) {
    if (!l.any) return "";
    if (l.kind == "wifi" || (l.kind.empty() && l.tier == "WIFI")) return "WIFI";
    std::string s = "RADIO";
    if (l.sf()) s += " " + l.tier;
    if (withRssi && l.hasRssi) s += " " + std::to_string(l.rssi);
    return s;
}

// The server echoes and runs character at a time in 8-bit binary, so CP437 art passes untouched.
std::string TelnetFilter::initialNegotiation() const {
    return cmd(WILL, ECHO) + cmd(WILL, SGA) + cmd(DO, SGA) + cmd(WILL, BINARY) + cmd(DO, BINARY) + cmd(DO, NAWS);
}

void TelnetFilter::fromNet(const uint8_t* p, size_t n, std::string& toApp, std::string& replyToNet) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = p[i];
        switch (st_) {
            case St::Data:
                if (c == IAC) {
                    st_ = St::Iac;
                } else if (lastCr_ && !callerBinary_ && (c == 0 || c == '\n')) {
                    lastCr_ = false;  // second half of CR NUL / CR LF
                } else {
                    lastCr_ = c == '\r';
                    toApp += (char)c;
                }
                break;
            case St::Iac:
                if (c == IAC) {
                    toApp += (char)IAC;
                    lastCr_ = false;
                    st_ = St::Data;
                } else if (c >= WILL && c <= DONT) {
                    cmd_ = c;
                    st_ = St::Opt;
                } else if (c == SB) {
                    sb_.clear();
                    sbOver_ = false;
                    st_ = St::Sb;
                } else {
                    st_ = St::Data;  // NOP, GA, AYT and friends: ignore
                }
                break;
            case St::Opt: {
                if (c == kLinkOption && (cmd_ == WILL || cmd_ == WONT)) {  // LFD3K-LINK: the modem offers its stats
                    if (cmd_ == WILL && !linkOn_) replyToNet += cmd(DONT, c);
                    else if (cmd_ == WILL && !linkAgreed_) replyToNet += cmd(DO, c), linkAgreed_ = true;
                    else if (cmd_ == WONT && linkAgreed_) replyToNet += cmd(DONT, c), linkAgreed_ = false;
                    st_ = St::Data;  // (a WILL we already took, or a WONT for one we never asked, needs no answer)
                    break;
                }
                // Options we offered or asked for need no answer; refuse everything else.
                if (c == BINARY && cmd_ == WILL) callerBinary_ = true;  // our DO BINARY was accepted
                if (c == BINARY && cmd_ == WONT) callerBinary_ = false;
                bool ours = c == ECHO || c == SGA || c == BINARY || c == NAWS;
                if (!ours) {
                    if (cmd_ == DO) replyToNet += cmd(WONT, c);
                    else if (cmd_ == WILL) replyToNet += cmd(DONT, c);
                }
                st_ = St::Data;
                break;
            }
            case St::Sb:
                if (c == IAC) st_ = St::SbIac;
                else if (sb_.size() < subnegCap(sb_)) sb_ += (char)c;  // a caller can't grow it without end
                else sbOver_ = true;
                break;
            case St::SbIac:
                if (c == SE) {
                    if (!sbOver_) subnegotiation();  // one past the cap is dropped whole, never cut and used
                    sbOver_ = false;
                    st_ = St::Data;
                } else {
                    if (sb_.size() < subnegCap(sb_)) sb_ += (char)c;  // IAC IAC inside a subnegotiation
                    else sbOver_ = true;
                    st_ = St::Sb;
                }
                break;
        }
    }
}

void TelnetFilter::subnegotiation() {
    if (sb_.size() >= 5 && (uint8_t)sb_[0] == NAWS) {
        auto b = [&](int k) { return (int)(uint8_t)sb_[k]; };
        int w = b(1) << 8 | b(2), h = b(3) << 8 | b(4);
        if (w > 0) width_ = w;
        if (h > 0) height_ = h;
    } else if (!sb_.empty() && (uint8_t)sb_[0] == kLinkOption && linkAgreed_) {  // a report we asked for (DO 90)
        if (parseLinkInfo(sb_.substr(1), link_)) linkSeq_++;
    }
}

std::string TelnetFilter::toNet(const uint8_t* p, size_t n) const {
    std::string s;
    s.reserve(n);
    for (size_t i = 0; i < n; i++) {
        s += (char)p[i];
        if (p[i] == IAC) s += (char)IAC;
    }
    return s;
}

}  // namespace axis::net
