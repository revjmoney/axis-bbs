// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Encoding, decoding, signing and checking of net records (posts, cards, notices, applications, votes, listings, fetches).
#include "net/record.h"

#include <vector>

#include "bbs/filelib.h"
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
bool parseU32(std::string_view s, uint32_t& out) {
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

// Appends the body, escaped, to o (no copy of its own).
void escapeBody(const std::string& b, std::string& o) {
    for (char c : b) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";  // also escaped so a record never holds a raw CR
        else o += c;
    }
}

bool unescapeBody(std::string_view e, std::string& out) {
    out.clear();
    out.reserve(e.size());
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

// The fields of a line as views into it, at most max of them. Returns how many fields the line has
// (more than max means it has too many).
size_t split(std::string_view line, std::string_view* f, size_t max) {
    size_t n = 0, start = 0;
    for (size_t i = 0; i <= line.size(); i++) {
        if (i == line.size() || line[i] == kSep) {
            if (n < max) f[n] = line.substr(start, i - start);
            n++;
            start = i + 1;
        }
    }
    return n;
}

bool validNoticeKind(const std::string& k) {
    return k == "hold" || k == "kill" || k == "ban" || k == "lift" || k == "limits" || k == "rotate" ||
           k == "admit" || k == "deny" || k == "veto" || k == "votes";
}

// "xx:xx:xx:xx:xx:xx" with hex digits in either case, or empty.
bool validMac(const std::string& m) {
    if (m.empty()) return true;
    if (m.size() != 17) return false;
    for (size_t i = 0; i < 17; i++) {
        char c = m[i];
        if (i % 3 == 2) {
            if (c != ':') return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
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

bool validFields(const NetApp& a) {
    return isLowerHex(a.board, 16) && isLowerHex(a.pub, 64) && cleanField(a.name, 36) && cleanField(a.sysop, 36) &&
           cleanField(a.location, 36) && validMac(a.mac) && a.blurb.size() <= kNetMaxBlurb && noSep(a.blurb) &&
           a.seq >= 1;
}

bool validFields(const NetVote& v) {
    return isLowerHex(v.voter, 16) && isLowerHex(v.pub, 64) && isLowerHex(v.app, 16) && v.appSeq >= 1 &&
           v.seq >= 1 && v.comment.size() <= kNetMaxComment && noSep(v.comment);
}

bool noCtl(const std::string& s, size_t maxLen) {
    if (s.size() > maxLen) return false;
    for (unsigned char ch : s)
        if (ch < 0x20 || ch == 0x7f) return false;
    return true;
}

// "<board>-<decimal seq>" for any board (a listing id; validId() ties the board to a field).
bool validListingId(const std::string& id) {
    uint32_t seq;
    return id.size() >= 18 && id.size() <= 27 && isLowerHex(id.substr(0, 16), 16) && id[16] == '-' &&
           parseU32(id.substr(17), seq);
}

bool validFields(const NetListing& l) {
    return isLowerHex(l.board, 16) && validArea(l.area) && bbs::safeFileName(l.name) && cleanField(l.desc, kNetListDesc) &&
           noCtl(l.uploader, 30) && (l.reach == "i" || l.reach == "r" || l.reach == "ir") && l.seq >= 1 &&
           (!l.gone || (l.size == 0 && l.crc == 0 && l.desc.empty()));
}

bool validFields(const NetFetch& f) {
    return isLowerHex(f.board, 16) && validListingId(f.listing) && noCtl(f.handle, 30) && isLowerHex(f.dialNonce, 16) &&
           isLowerHex(f.servNonce, 16);
}

bool sigOk(const std::string& s) { return isLowerHex(s, 128); }

std::string u(uint32_t v) { return std::to_string(v); }

}  // namespace

// ---- signed parts ----------------------------------------------------------------------

std::string signedPart(const NetPost& p) {
    std::string s(1, (char)NetKind::Post);
    // Room for the whole line (the path and sig come after in encode), so it is built in one buffer.
    s.reserve(p.id.size() + p.area.size() + p.board.size() + p.from.size() + p.to.size() + p.subject.size() +
              p.body.size() + p.body.size() / 16 + p.path.size() + 160);
    for (const std::string* f : {&p.id, &p.area, &p.board, &p.from, &p.to, &p.subject}) {
        s += kSep;
        s += *f;
    }
    s += kSep;
    escapeBody(p.body, s);
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

std::string appId(const NetApp& a) { return a.board + "-a" + u(a.seq); }

std::string signedPart(const NetApp& a) {
    std::string s(1, (char)NetKind::App);
    for (const std::string* f : {&a.board, &a.pub, &a.name, &a.sysop, &a.location, &a.mac}) {
        s += kSep;
        s += *f;
    }
    s += kSep;
    escapeBody(a.blurb, s);
    s += kSep;
    s += u(a.seq);
    s += kSep;
    s += u(a.date);
    return s;
}

std::string signedPart(const NetVote& v) {
    std::string s(1, (char)NetKind::Vote);
    for (const std::string* f : {&v.voter, &v.pub, &v.app}) {
        s += kSep;
        s += *f;
    }
    s += kSep;
    s += u(v.appSeq);
    s += kSep;
    s += u(v.seq);
    s += kSep;
    s += u(v.date);
    s += kSep;
    s += v.yes ? 'y' : 'n';
    s += kSep;
    escapeBody(v.comment, s);
    return s;
}

std::string listingId(const NetListing& l) { return l.board + "-" + u(l.seq); }

std::string signedPart(const NetListing& l) {
    std::string s(1, (char)NetKind::Listing);
    for (const std::string* f : {&l.board, &l.area, &l.name, &l.desc, &l.uploader, &l.reach}) {
        s += kSep;
        s += *f;
    }
    for (uint32_t n : {l.seq, l.size, l.crc, l.date}) {
        s += kSep;
        s += u(n);
    }
    s += kSep;
    s += l.gone ? '1' : '0';
    return s;
}

std::string signedPart(const NetFetch& f) {
    std::string s(1, (char)NetKind::Fetch);
    for (const std::string* p : {&f.board, &f.listing, &f.handle, &f.dialNonce, &f.servNonce}) {
        s += kSep;
        s += *p;
    }
    s += kSep;
    s += u(f.offset);
    return s;
}

// ---- encode ----------------------------------------------------------------------------

std::string encode(const NetPost& p) {
    if (!validFields(p)) return "";
    if (!p.sig.empty() && !sigOk(p.sig)) return "";
    std::string line = signedPart(p);  // appended to in place: one copy of the body's worth
    line += kSep;
    line += p.path;
    line += kSep;
    line += p.sig;
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

std::string encode(const NetApp& a) {
    if (!validFields(a)) return "";
    if (!a.sig.empty() && !sigOk(a.sig)) return "";
    std::string line = signedPart(a) + kSep + a.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

std::string encode(const NetVote& v) {
    if (!validFields(v)) return "";
    if (!v.sig.empty() && !sigOk(v.sig)) return "";
    std::string line = signedPart(v) + kSep + v.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

std::string encode(const NetListing& l) {
    if (!validFields(l)) return "";
    if (!l.sig.empty() && !sigOk(l.sig)) return "";
    std::string line = signedPart(l) + kSep + l.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

std::string encode(const NetFetch& f) {
    if (!validFields(f)) return "";
    if (!f.sig.empty() && !sigOk(f.sig)) return "";
    std::string line = signedPart(f) + kSep + f.sig;
    return line.size() > kNetMaxFrame ? std::string() : line;
}

// ---- decode ----------------------------------------------------------------------------

bool decode(std::string_view line, NetPost& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[11];
    if (split(line, f, 11) != 11 || f[0] != "P") return false;
    if (f[7].size() > kNetMaxBody * 2) return false;
    NetPost p;
    p.id = f[1];
    p.area = f[2];
    p.board = f[3];
    p.from = f[4];
    p.to = f[5];
    p.subject = f[6];
    if (!unescapeBody(f[7], p.body)) return false;
    if (!parseU32(f[8], p.date)) return false;
    p.path = f[9];
    p.sig = f[10];
    if (!validFields(p) || !sigOk(p.sig)) return false;
    out = std::move(p);
    return true;
}

bool decode(std::string_view line, NetCard& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[12];
    if (split(line, f, 12) != 12 || f[0] != "C") return false;
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
    out = std::move(c);
    return true;
}

bool decode(std::string_view line, NetNotice& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[7];
    if (split(line, f, 7) != 7 || f[0] != "N") return false;
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
    out = std::move(n);
    return true;
}

bool decode(std::string_view line, NetApp& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[11];
    if (split(line, f, 11) != 11 || f[0] != "A") return false;
    if (f[7].size() > kNetMaxBlurb * 2) return false;
    NetApp a;
    a.board = f[1];
    a.pub = f[2];
    a.name = f[3];
    a.sysop = f[4];
    a.location = f[5];
    a.mac = f[6];
    if (!unescapeBody(f[7], a.blurb)) return false;
    if (!parseU32(f[8], a.seq) || !parseU32(f[9], a.date)) return false;
    a.sig = f[10];
    if (!validFields(a) || !sigOk(a.sig)) return false;
    out = std::move(a);
    return true;
}

bool decode(std::string_view line, NetVote& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[10];
    if (split(line, f, 10) != 10 || f[0] != "V") return false;
    if (f[8].size() > kNetMaxComment * 2) return false;
    NetVote v;
    v.voter = f[1];
    v.pub = f[2];
    v.app = f[3];
    if (!parseU32(f[4], v.appSeq) || !parseU32(f[5], v.seq) || !parseU32(f[6], v.date)) return false;
    if (f[7] == "y") v.yes = true;
    else if (f[7] == "n") v.yes = false;
    else return false;
    if (!unescapeBody(f[8], v.comment)) return false;
    v.sig = f[9];
    if (!validFields(v) || !sigOk(v.sig)) return false;
    out = std::move(v);
    return true;
}

bool decode(std::string_view line, NetListing& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[13];
    if (split(line, f, 13) != 13 || f[0] != "L") return false;
    NetListing l;
    l.board = f[1];
    l.area = f[2];
    l.name = f[3];
    l.desc = f[4];
    l.uploader = f[5];
    l.reach = f[6];
    if (!parseU32(f[7], l.seq) || !parseU32(f[8], l.size) || !parseU32(f[9], l.crc) || !parseU32(f[10], l.date)) return false;
    if (f[11] == "1") l.gone = true;
    else if (f[11] != "0") return false;
    l.sig = f[12];
    if (!validFields(l) || !sigOk(l.sig)) return false;
    out = std::move(l);
    return true;
}

bool decode(std::string_view line, NetFetch& out) {
    if (line.size() > kNetMaxFrame) return false;
    std::string_view f[8];
    if (split(line, f, 8) != 8 || f[0] != "Q") return false;
    NetFetch q;
    q.board = f[1];
    q.listing = f[2];
    q.handle = f[3];
    q.dialNonce = f[4];
    q.servNonce = f[5];
    if (!parseU32(f[6], q.offset)) return false;
    q.sig = f[7];
    if (!validFields(q) || !sigOk(q.sig)) return false;
    out = std::move(q);
    return true;
}

// ---- sign / verify ---------------------------------------------------------------------

bool validPost(const NetPost& p) { return validFields(p) && sigOk(p.sig); }

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

bool signApp(NetApp& a, const KeyPair& k) {
    if (toHex(k.pub, 32) != a.pub || boardId(a.pub) != a.board || !validFields(a)) return false;
    a.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(a).empty()) {
        a.sig.clear();
        return false;
    }
    a.sig = signHex(k, signedPart(a));
    return !a.sig.empty();
}

bool verifyApp(const NetApp& a) {
    if (!validFields(a) || !sigOk(a.sig)) return false;
    if (boardId(a.pub) != a.board) return false;
    return verifyHex(a.pub, signedPart(a), a.sig);
}

bool signVote(NetVote& v, const KeyPair& k) {
    if (toHex(k.pub, 32) != v.pub || boardId(v.pub) != v.voter || !validFields(v)) return false;
    v.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(v).empty()) {
        v.sig.clear();
        return false;
    }
    v.sig = signHex(k, signedPart(v));
    return !v.sig.empty();
}

bool verifyVote(const NetVote& v) {
    if (!validFields(v) || !sigOk(v.sig)) return false;
    if (boardId(v.pub) != v.voter) return false;
    return verifyHex(v.pub, signedPart(v), v.sig);
}

bool signListing(NetListing& l, const KeyPair& k) {
    if (boardId(toHex(k.pub, 32)) != l.board || !validFields(l)) return false;
    l.sig.assign(128, '0');  // final length, to check the frame cap before signing
    if (encode(l).empty()) {
        l.sig.clear();
        return false;
    }
    l.sig = signHex(k, signedPart(l));
    return !l.sig.empty();
}

bool verifyListing(const NetListing& l, const std::string& pubHex) {
    if (!isLowerHex(pubHex, 64) || !validFields(l) || !sigOk(l.sig) || boardId(pubHex) != l.board) return false;
    return verifyHex(pubHex, signedPart(l), l.sig);
}

bool signFetch(NetFetch& f, const KeyPair& k) {
    if (boardId(toHex(k.pub, 32)) != f.board || !validFields(f)) return false;
    f.sig.assign(128, '0');
    if (encode(f).empty()) {
        f.sig.clear();
        return false;
    }
    f.sig = signHex(k, signedPart(f));
    return !f.sig.empty();
}

bool verifyFetch(const NetFetch& f, const std::string& pubHex) {
    if (!isLowerHex(pubHex, 64) || !validFields(f) || !sigOk(f.sig) || boardId(pubHex) != f.board) return false;
    return verifyHex(pubHex, signedPart(f), f.sig);
}

}  // namespace axis::net
