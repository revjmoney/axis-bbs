// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The card tree service and the change feed (cardtree.h).
#include "cluster/cardtree.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utime.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace axis::cluster {

namespace {

constexpr char kSep = '\x1f';

bool mkdir1(const std::string& path) {
#ifdef _WIN32
    return ::mkdir(path.c_str()) == 0 || errno == EEXIST;
#else
    return ::mkdir(path.c_str(), 0777) == 0 || errno == EEXIST;
#endif
}

char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

bool endsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (lower(s[s.size() - n + i]) != lower(suffix[i])) return false;
    return true;
}

bool equalNoCase(const std::string& s, const char* t) {
    size_t n = std::strlen(t);
    if (s.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (lower(s[i]) != lower(t[i])) return false;
    return true;
}

// the first n-1 fields split at 0x1F, the rest (the last field) taken whole; missing fields read empty
std::vector<std::string> splitN(const std::string& s, size_t n) {
    std::vector<std::string> out;
    size_t                   pos = 0;
    while (out.size() + 1 < n) {
        size_t e = s.find(kSep, pos);
        if (e == std::string::npos) break;
        out.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    out.push_back(pos <= s.size() ? s.substr(pos) : std::string());
    out.resize(n);
    return out;
}

bool parseNum(const std::string& s, uint64_t& v) {
    if (s.empty() || s.size() > 18) return false;
    v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (uint64_t)(c - '0');
    }
    return true;
}

// rel is the folder top (in any case) or anything under it
bool underNoCase(const std::string& rel, const char* top) {
    size_t n = std::strlen(top);
    return rel.size() >= n && equalNoCase(rel.substr(0, n), top) && (rel.size() == n || rel[n] == '/');
}

std::string no() { return "0"; }

std::string num(uint64_t v) { return std::to_string(v); }

// every folder above rel's last component, under root
void makeParents(const std::string& root, const std::string& rel) {
    for (size_t i = 0; i < rel.size(); i++)
        if (rel[i] == '/') mkdir1(root + "/" + rel.substr(0, i));
}

}  // namespace

bool cardPathOk(const std::string& rel) {
    if (rel.empty() || rel.size() > 200 || rel[0] == '/') return false;
    size_t start = 0;
    for (size_t i = 0; i <= rel.size(); i++) {
        if (i == rel.size() || rel[i] == '/') {
            size_t len = i - start;
            if (len == 0 || len > 64) return false;
            if (len == 1 && rel[start] == '.') return false;
            if (len == 2 && rel[start] == '.' && rel[start + 1] == '.') return false;
            start = i + 1;
        } else {
            unsigned char c = (unsigned char)rel[i];
            if (c < 0x20 || c == '\\' || c == ':') return false;
        }
    }
    return true;
}

bool cardPathMirrored(const std::string& rel) {
    if (!cardPathOk(rel)) return false;
    if (endsWithNoCase(rel, ".log") || endsWithNoCase(rel, ".ax_")) return false;
    // the ANSI viewer's view cache (bbs/viewcache.h): made again from the packs wherever the BBS runs
    if (underNoCase(rel, "files/viewpacks") || underNoCase(rel, "files/viewcache")) return false;
    size_t start = 0;
    for (size_t i = 0; i <= rel.size(); i++) {
        if (i == rel.size() || rel[i] == '/') {
            if (equalNoCase(rel.substr(start, i - start), "System Volume Information")) return false;
            start = i + 1;
        }
    }
    return true;
}

// ---- the change feed ----

void CardFeed::setOn(bool on, uint32_t gen) {
    std::lock_guard<std::mutex> lk(mu_);
    if (on && !on_.load()) {
        gen_ = gen;
        seq_ = lostSeq_ = 0;
        n_ = 0;
    }
    on_.store(on);
}

void CardFeed::note(const char* boardPath) {
    if (!on() || !boardPath || std::strncmp(boardPath, "/sd/", 4) != 0) return;
    std::string rel(boardPath + 4);
    if (!cardPathMirrored(rel)) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (!on_.load()) return;
    if (rel.size() >= kPathMax) {  // too long to hold: the asker compares the whole tree
        lostSeq_ = ++seq_;
        return;
    }
    for (size_t i = 0; i < n_; i++) {  // already in the ring: it moves to the newest number
        if (rel == e_[i].path) {
            for (size_t j = i + 1; j < n_; j++) e_[j - 1] = e_[j];
            n_--;
            break;
        }
    }
    if (n_ == kMax) {  // the oldest falls out: an asker behind it missed that change
        if (e_[0].seq > lostSeq_) lostSeq_ = e_[0].seq;
        for (size_t j = 1; j < n_; j++) e_[j - 1] = e_[j];
        n_--;
    }
    e_[n_].seq = ++seq_;
    std::memcpy(e_[n_].path, rel.c_str(), rel.size() + 1);
    n_++;
}

std::string CardFeed::answer(uint32_t gen, uint32_t seq) const {
    std::lock_guard<std::mutex> lk(mu_);
    bool        lost = gen != gen_ || seq < lostSeq_ || seq > seq_;
    std::string out = num(gen_) + kSep + num(seq_) + kSep + (lost ? "1" : "0") + "\n";
    if (!lost)
        for (size_t i = 0; i < n_; i++)
            if (e_[i].seq > seq) out += std::string(e_[i].path) + "\n";
    return out;
}

CardFeed& cardFeed() {
    static CardFeed feed;
    return feed;
}

// ---- the tree service ----

void CardTreeServer::closeRead() {
    if (rd_) std::fclose(rd_);
    rd_ = nullptr;
    rdPath_.clear();
}

void CardTreeServer::closeWrite(bool removeTemp) {
    if (wr_) std::fclose(wr_);
    wr_ = nullptr;
    if (removeTemp && !wrPath_.empty()) std::remove(full(wrPath_ + ".ax_").c_str());
    wrPath_.clear();
    written_ = 0;
}

void CardTreeServer::drop() {
    closeRead();
    closeWrite(true);
}

// 'I S': "size" 1F "mtime"; '0' when missing. A folder answers size -1 (a mirror makes the folder).
std::string CardTreeServer::stat1(const std::string& rel) const {
    if (!cardPathMirrored(rel)) return no();
    struct stat st;
    if (::stat(full(rel).c_str(), &st) != 0) return no();
    std::string size = S_ISDIR(st.st_mode) ? std::string("-1") : num((uint64_t)st.st_size);
    return "1" + size + kSep + num((uint64_t)st.st_mtime);
}

std::string CardTreeServer::handleRead(const std::string& req) {
    try {
        return readOne(req);
    } catch (const std::bad_alloc&) {  // a board low on memory: the asker tries again later
        closeRead();
        return no();
    }
}

std::string CardTreeServer::handleWrite(const std::string& req) {
    try {
        return writeOne(req);
    } catch (const std::bad_alloc&) {
        closeWrite(true);
        return no();
    }
}

std::string CardTreeServer::readOne(const std::string& req) {
    if (req.size() < 3 || req[0] != 'I' || req[1] != kSep) return no();
    char op = req[2];
    auto f = splitN(req.size() > 4 ? req.substr(4) : std::string(), 3);
    if (op != 'R') closeRead();  // an open file holds SD buffers
    switch (op) {
        case 'V':
            return "12";
        case 'D': {  // in the folder's own order, never every name at once (a folder may hold thousands)
            const std::string& dir = f[0];
            uint64_t           skip;
            if (!parseNum(f[1], skip)) return no();
            if (!dir.empty() && !cardPathMirrored(dir)) return no();
            DIR* d = ::opendir(full(dir).c_str());
            if (!d) return no();
            struct Close {  // (every way out, a bad_alloc too)
                DIR* d;
                ~Close() { ::closedir(d); }
            } closer{d};
            std::string out = "1";
            uint64_t    at = 0;  // the mirrored entries passed so far
            bool        more = false;
            while (dirent* e = ::readdir(d)) {
                std::string n = e->d_name;
                if (n == "." || n == "..") continue;
                std::string rel = dir.empty() ? n : dir + "/" + n;
                if (!cardPathMirrored(rel)) continue;
                if (at < skip) {
                    at++;
                    continue;
                }
                struct stat st;
                std::string line;
                if (::stat(full(rel).c_str(), &st) == 0) {
                    if (S_ISDIR(st.st_mode))
                        line = std::string("d") + kSep + n + "\n";
                    else
                        line = std::string("f") + kSep + n + kSep + num((uint64_t)st.st_size) + kSep + num((uint64_t)st.st_mtime) + "\n";
                }
                if (out.size() + line.size() + 24 > kTreeListMax) {
                    more = true;
                    break;
                }
                out += line;
                at++;
            }
            if (more) out += std::string("+") + kSep + num(at) + "\n";
            return out;
        }
        case 'S':
            return stat1(f[0]);
        case 'R': {
            uint64_t off, len;
            if (!cardPathMirrored(f[0]) || !parseNum(f[1], off) || !parseNum(f[2], len)) return no();
            if (len > kTreePiece) len = kTreePiece;
            if (!rd_ || rdPath_ != f[0] || off == 0) {  // (a file's first piece opens it again: it may have been rewritten)
                closeRead();
                struct stat st;
                if (::stat(full(f[0]).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return no();
                rd_ = std::fopen(full(f[0]).c_str(), "rb");
                if (!rd_) return no();
                rdPath_ = f[0];
            }
            if (std::fseek(rd_, (long)off, SEEK_SET) != 0) return no();
            std::string out = "1";
            out.resize(1 + (size_t)len);
            size_t got = std::fread(&out[1], 1, (size_t)len, rd_);
            out.resize(1 + got);
            return out;
        }
        case 'C': {
            uint64_t gen, seq;
            if (!parseNum(f[0], gen) || !parseNum(f[1], seq)) return no();
            return "1" + cardFeed().answer((uint32_t)gen, (uint32_t)seq);
        }
        default:
            return no();
    }
}

std::string CardTreeServer::writeOne(const std::string& req) {
    if (req.size() < 3 || req[0] != 'I' || req[1] != kSep) return no();
    char op = req[2];
    auto f = splitN(req.size() > 4 ? req.substr(4) : std::string(), 2);
    if (op != 'P' && op != 'F' && op != 'S') closeWrite(true);  // anything but the next step of a copy drops the half-written temp
    switch (op) {
        case 'S':
            return stat1(f[0]);
        case 'O': {
            if (!cardPathMirrored(f[0])) return no();
            struct stat st;
            if (::stat(full(f[0]).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) return no();
            makeParents(root_, f[0]);
            wr_ = std::fopen(full(f[0] + ".ax_").c_str(), "wb");
            if (!wr_) return no();
            wrPath_ = f[0];
            written_ = 0;
            return "1";
        }
        case 'P': {
            uint64_t off;
            if (!wr_ || !parseNum(f[0], off) || off != written_) return no();
            if (!f[1].empty() && std::fwrite(f[1].data(), 1, f[1].size(), wr_) != f[1].size()) {
                closeWrite(true);
                return no();
            }
            written_ += f[1].size();
            return "1";
        }
        case 'F': {
            uint64_t size, mtime;
            if (!wr_ || !parseNum(f[0], size) || !parseNum(f[1], mtime)) {
                closeWrite(true);
                return no();
            }
            std::string rel = wrPath_;
            size_t      got = written_;
            bool        flushed = std::fflush(wr_) == 0;
            closeWrite(false);
            std::string tmp = full(rel + ".ax_"), dst = full(rel);
            if (!flushed || got != size) {
                std::remove(tmp.c_str());
                return no();
            }
            if (std::rename(tmp.c_str(), dst.c_str()) != 0) {  // (a FAT card will not rename over a file)
                std::remove(dst.c_str());
                if (std::rename(tmp.c_str(), dst.c_str()) != 0) return no();  // path.ax_ is kept: the only copy now
            }
            struct utimbuf ut;
            ut.actime = ut.modtime = (time_t)mtime;
            ::utime(dst.c_str(), &ut);
            return stat1(rel);
        }
        case 'X': {
            if (!cardPathMirrored(f[0])) return no();
            struct stat st;
            if (::stat(full(f[0]).c_str(), &st) != 0) return "1";  // already gone
            return std::remove(full(f[0]).c_str()) == 0 ? "1" : no();
        }
        case 'E':
            finished_ = true;
            return "1";
        default:
            return no();
    }
}

}  // namespace axis::cluster
