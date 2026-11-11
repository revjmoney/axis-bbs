// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Encoding, decoding, signing and checking of net records (posts, cards, notices).
#include "net/record.h"

#include <vector>

#include "net/revkeys.h"

namespace axis::net {

namespace {

const char kSep = '\x1F';

bool isLowerHex(const std::string& s, size_t n) {
    if (s.size() != n) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

bool allZero(const std::string& s) {
    for (char c : s)
        if (c != '0') return false;
    return true;
}

// Single-line field: no separator, no CR/LF.
bool cleanField(const std::string& s) {
    for (char c : s)
        if (c == kSep || c == '\n' || c == '\r') return false;
    return true;
}

bool noSep(const std::string& s) { return s.find(kSep) == std::string::npos; }

bool cleanField(const std::string& s, size_t maxLen) { return s.size() <= maxLen && cleanField(s); }

bool validArea(const std::string& a) {
    if (a.empty() || a.size() > 16) return false;
    for (char c : a)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

// Parse an unsigned decimal of 1..10 digits that fits in uint32_t.
bool parseU32(const std::string& s, uint32_t& out) {
    if (s.empty() || s.size() > 10) return false;
    if (s.size() > 1 && s[0] == '0') return false;  // no leading zeros: one spelling per number
    uint64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    out = (uint32_t)v;
    return true;
}

std::string escapeBody(const std::string& b) {
    std::string o;
    o.reserve(b.size() + 8);
    for (char c : b) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";  // also escaped so a record never holds a raw CR
        else o += c;
    }
    return o;
}

bool unescapeBody(const std::string& e, std::string& out) {
    out.clear();
    for (size_t i = 0; i < e.size(); i++) {
        char c = e[i];
        if (c == '\n' || c == '\r') return false;
        if (c == '\\') {
            if (++i >= e.size()) return false;
            if (e[i] == '\\') out += '\\';
            else if (e[i] == 'n') out += '\n';
            else if (e[i] == 'r') out += '\r';
            else return false;
        } else {
            out += c;
        }
    }
    return true;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> f;
    size_t start = 0;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == kSep) {
            f.push_back(line.substr(start, i - start));
            start = i + 1;
        }
    }
    return f;
}

bool validNoticeKind(const std::string& k) {
    return k == "hold" || k == "kill" || k == "ban" || k == "lift" || k == "limits" || k == "rotate";
}

bool validPath(const std::string& p) {
    for (char c : p)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == ',')) return false;
    return true;
}

// id must be <board>-<decimal seq> with the board part equal to the board field.
bool validId(const std::string& id, const std::string& board) {
    if (!isLowerHex(board, 16)) return false;
    if (id.size() < 18 || id.size() > 27 || id.size() > 40) return false;
    if (id.compare(0, 16, board) != 0 || id[16] != '-') return false;
    uint32_t seq;
    return parseU32(id.substr(17), seq);
}

// Field checks shared by encode (refuses) and decode (rejects). sig is checked separately.
bool validFields(const NetPost& p) {
    return validArea(p.area) && validId(p.id, p.board) && cleanField(p.from, 36) && cleanField(p.to, 36) &&
           cleanField(p.subject, kNetMaxSubject) && p.body.size() <= kNetMaxBody && noSep(p.body) &&
           p.path.size() <= 512 && validPath(p.path);
}

bool validFields(const NetCard& c) {
    return isLowerHex(c.board, 16) && isLowerHex(c.pub, 64) && cleanField(c.name, 40) && cleanField(c.host, 64) &&
           cleanField(c.mac, 17) && cleanField(c.sysop, 40) && cleanField(c.location, 40) && cleanField(c.links, 16) &&
           c.port >= 0 && c.port <= 65535;
}

bool validFields(const NetNotice& n) {
    return validNoticeKind(n.kind) && cleanField(n.target, 40) && cleanField(n.value, 64) && (n.key == 0 || n.key == 1);
}

bool sigOk(const std::string& s) { return isLowerHex(s, 128); }

std::string u(uint32_t v) { return std::to_string(v); }

}  // namespace

// ---- signed parts ----------------------------------------------------------------------

std::string signedPart(const NetPost& p) {
    std::string s(1, (char)NetKind::Post);
    for (const std::string* f : {&p.id, &p.area, &p.board, &p.from, &p.to, &p.subject}) {
        s += kSep;
        s += *f;
    }
    s += kSep;
    s += escapeBody(p.body);
    s += kSep;
    s += u(p.date);
    return s;
}

std::string signedPart(const NetCard& c) {
    std::string s(1, (char)NetKind::Card);
    for (const std::string* f : {&c.board, &c.pub, &c.name, &c.host, &c.mac, &c.sysop, &c.location, &c.links}) {
        s += kSep;
        s += *f;
    }
    s += kSep;
    s += std::to_string(c.port);
    s += kSep;
    s += u(c.seq);
    return s;
}

std::string signedPart(const NetNotice& n) {
    std::string s(1, (char)NetKind::Notice);
    s += kSep;
    s += n.kind;
    s += kSep;
    s += n.target;
    s += kSep;
    s += n.value;
    s += kSep;
    s += u(n.seq);
    s += kSep;
    s += std::to_string(n.key);
    return s;
}

// ---- encode ----------------------------------------------------------------------------

std::string encode(const NetPost& p) {
    if (!validFields(p)) return "";
    if (!p.sig.empty() && !sigOk(p.sig)) return "";
    std::string line = signedPart(p) + kSep + p.path + kSep + p.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

std::string encode(const NetCard& c) {
    if (!validFields(c)) return "";
    if (!c.sig.empty() && !sigOk(c.sig)) return "";
    std::string line = signedPart(c) + kSep + c.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

std::string encode(const NetNotice& n) {
    if (!validFields(n)) return "";
    if (!n.sig.empty() && !sigOk(n.sig)) return "";
    std::string line = signedPart(n) + kSep + n.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

// ---- decode ----------------------------------------------------------------------------

bool decode(const std::string& line, NetPost& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::vector<std::string> f = split(line);
    if (f.size() != 11 || f[0] != "P") return false;
    NetPost p;
    p.id = f[1];
    p.area = f[2];
    p.board = f[3];
    p.from = f[4];
    p.to = f[5];
    p.subject = f[6];
    if (f[7].size() > kNetMaxBody * 2) return false;
    if (!unescapeBody(f[7], p.body)) return false;
    if (!parseU32(f[8], p.date)) return false;
    p.path = f[9];
    p.sig = f[10];
    if (!validFields(p) || !sigOk(p.sig)) return false;
    out = p;
    return true;
}

bool decode(const std::string& line, NetCard& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::vector<std::string> f = split(line);
    if (f.size() != 12 || f[0] != "C") return false;
    NetCard c;
    c.board = f[1];
    c.pub = f[2];
    c.name = f[3];
    c.host = f[4];
    c.mac = f[5];
    c.sysop = f[6];
    c.location = f[7];
    c.links = f[8];
    uint32_t port;
    if (!parseU32(f[9], port) || port > 65535) return false;
    c.port = (int)port;
    if (!parseU32(f[10], c.seq)) return false;
    c.sig = f[11];
    if (!validFields(c) || !sigOk(c.sig)) return false;
    out = c;
    return true;
}

bool decode(const std::string& line, NetNotice& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::vector<std::string> f = split(line);
    if (f.size() != 7 || f[0] != "N") return false;
    NetNotice n;
    n.kind = f[1];
    n.target = f[2];
    n.value = f[3];
    if (!parseU32(f[4], n.seq)) return false;
    uint32_t key;
    if (!parseU32(f[5], key) || key > 1) return false;
    n.key = (int)key;
    n.sig = f[6];
    if (!validFields(n) || !sigOk(n.sig)) return false;
    out = n;
    return true;
}

// ---- sign / verify ---------------------------------------------------------------------

bool signPost(NetPost& p, const KeyPair& k) {
    if (!validFields(p)) return false;
    if (boardId(toHex(k.pub, 32)) != p.board) return false;
    p.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(p).empty()) {
        p.sig.clear();
        return false;
    }
    p.sig = signHex(k, signedPart(p));
    return !p.sig.empty();
}

bool verifyPost(const NetPost& p, const std::string& pubHex) {
    if (!isLowerHex(pubHex, 64) || !sigOk(p.sig) || !validFields(p)) return false;
    if (boardId(pubHex) != p.board) return false;
    return verifyHex(pubHex, signedPart(p), p.sig);
}

bool signCard(NetCard& c, const KeyPair& k) {
    if (toHex(k.pub, 32) != c.pub || boardId(c.pub) != c.board || !validFields(c)) return false;
    c.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(c).empty()) {
        c.sig.clear();
        return false;
    }
    c.sig = signHex(k, signedPart(c));
    return !c.sig.empty();
}

bool verifyCard(const NetCard& c) {
    if (!validFields(c) || !sigOk(c.sig)) return false;
    if (boardId(c.pub) != c.board) return false;
    return verifyHex(c.pub, signedPart(c), c.sig);
}

bool verifyCard(const NetCard& c, const std::string& pubHex) {
    return pubHex == c.pub && verifyCard(c);
}

bool signNotice(NetNotice& n, const KeyPair& k) {
    if (!validFields(n)) return false;
    n.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(n).empty()) {
        n.sig.clear();
        return false;
    }
    n.sig = signHex(k, signedPart(n));
    return !n.sig.empty();
}

bool verifyNotice(const NetNotice& n) {
    if (!validFields(n) || !sigOk(n.sig)) return false;
    const std::string& key = revKey(n.key);
    if (!isLowerHex(key, 64) || allZero(key)) return false;
    return verifyHex(key, signedPart(n), n.sig);
}

}  // namespace axis::net
