// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/doorfs.h"

#include <cstdlib>
#include <cstring>
#include <vector>

namespace axis::cluster {

namespace {
constexpr char   kSep = '\x1f', kSub = '\x1e';
constexpr size_t kPiece = 2048;  // the most a read or write moves per request

std::vector<std::string> splitN(const std::string& s, size_t n) {  // the last field taken whole
    std::vector<std::string> v;
    size_t                   pos = 0;
    while (v.size() + 1 < n) {
        size_t e = s.find(kSep, pos);
        if (e == std::string::npos) break;
        v.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    v.push_back(s.substr(pos));
    return v;
}
std::vector<std::string> parts(const std::string& s, char sep) {
    std::vector<std::string> v;
    size_t                   pos = 0;
    for (;;) {
        size_t e = s.find(sep, pos);
        v.push_back(s.substr(pos, e == std::string::npos ? std::string::npos : e - pos));
        if (e == std::string::npos) return v;
        pos = e + 1;
    }
}
long long   toL(const std::string& s) { return std::strtoll(s.c_str(), nullptr, 10); }
std::string ok(const std::string& d = "") { return "1" + d; }
std::string okN(long long n) { return "1" + std::to_string(n); }
std::string no() { return "0"; }
std::string infoText(const door::FileInfo& f) {
    return std::to_string(f.size) + kSub + std::to_string(f.dosDate) + kSub + std::to_string(f.dosTime) + kSub + std::to_string(f.attr) + kSub +
           f.name83;
}
bool infoFrom(const std::string& s, door::FileInfo& f) {
    auto p = parts(s, kSub);
    if (p.size() < 5) return false;
    f.size = (uint32_t)toL(p[0]);
    f.dosDate = (uint16_t)toL(p[1]);
    f.dosTime = (uint16_t)toL(p[2]);
    f.attr = (uint8_t)toL(p[3]);
    f.name83 = p[4];
    return true;
}
}  // namespace

std::string DoorFsServer::handle(const std::string& request) {
    std::string req = !request.empty() && request[0] == 'H' ? request.substr(1) : request;
    if (req.empty()) return no();
    char op = req[0];
    auto f = splitN(req.size() > 1 ? req.substr(2) : std::string(), op == 'w' ? 2 : 8);
    auto a = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    if (op == 'B') {  // begin a door: its folder
        if (!fs_ || door_ != a(0)) {
            fs_ = open_(a(0));
            door_ = a(0);
        }
        return fs_ ? ok() : no();
    }
    if (op == 'L') {
        int owner = (int)toL(a(3));
        bool seen = false;
        for (int o : owners_) seen = seen || o == owner;
        if (!seen) owners_.push_back(owner);
    }
    if (op == 'L') return locks_.lock((int)toL(a(0)), (uint32_t)toL(a(1)), (uint32_t)toL(a(2)), (int)toL(a(3))) ? ok() : no();
    if (op == 'U') return locks_.unlock((int)toL(a(0)), (uint32_t)toL(a(1)), (uint32_t)toL(a(2)), (int)toL(a(3))) ? ok() : no();
    if (op == 'R') {
        locks_.releaseOwner((int)toL(a(0)));
        return ok();
    }
    if (!fs_) return no();
    door::HostFs& fs = *fs_;
    switch (op) {  // path operations carry the door's current folder first
        case 'o':
            fs.setCwd(a(0));
            return okN(fs.open(a(1), (int)toL(a(2)), a(3) == "1", a(4) == "1", a(5) == "1"));
        case 'r': {
            size_t n = (size_t)toL(a(1));
            if (n > kPiece) n = kPiece;
            std::string buf(n, '\0');
            int got = fs.read((int)toL(a(0)), n ? &buf[0] : nullptr, (int)n);
            if (got < 0) return okN(got);
            return ok(":" + buf.substr(0, (size_t)got));  // ':' then the bytes (a number means an error)
        }
        case 'w': return okN(fs.write((int)toL(a(0)), a(1).data(), (int)a(1).size()));
        case 'p': {  // read at an offset: handle, offset, length
            int h = (int)toL(a(0));
            if (fs.seek(h, toL(a(1)), 0) < 0) return okN(-25);
            size_t n = (size_t)toL(a(2));
            if (n > kPiece) n = kPiece;
            std::string buf(n, '\0');
            int got = fs.read(h, n ? &buf[0] : nullptr, (int)n);
            if (got < 0) return okN(got);
            return ok(":" + buf.substr(0, (size_t)got));
        }
        case 'q': {  // write at an offset: handle, offset, data (empty = truncate there)
            auto g = splitN(req.size() > 1 ? req.substr(2) : std::string(), 3);
            int  h = (int)toL(g[0]);
            if (fs.seek(h, toL(g.size() > 1 ? g[1] : "0"), 0) < 0) return okN(-25);
            const std::string& d = g.size() > 2 ? g[2] : std::string();
            return okN(fs.write(h, d.data(), (int)d.size()));
        }
        case 'z': {  // a file's size by handle
            int     h = (int)toL(a(0));
            int64_t here = fs.seek(h, 0, 1), end = fs.seek(h, 0, 2);
            fs.seek(h, here, 0);
            return okN(end);
        }
        case 's': return okN(fs.seek((int)toL(a(0)), toL(a(1)), (int)toL(a(2))));
        case 'c': return okN(fs.close((int)toL(a(0))));
        case 't': {
            fs.setCwd(a(0));
            door::FileInfo fi;
            return fs.stat(a(1), fi) ? ok(infoText(fi)) : no();
        }
        case 'm': fs.setCwd(a(0)); return okN(fs.remove(a(1)));
        case 'n': fs.setCwd(a(0)); return okN(fs.rename(a(1), a(2)));
        case 'k': fs.setCwd(a(0)); return okN(fs.mkdir(a(1)));
        case 'K': fs.setCwd(a(0)); return okN(fs.rmdir(a(1)));
        case 'd': fs.setCwd(a(0)); return fs.isDir(a(1)) ? ok() : no();
        case 'f': {
            fs.setCwd(a(0));
            door::FindState st;
            if (!fs.find(a(1), (uint8_t)toL(a(2)), st)) return no();
            std::string out;
            for (const door::FileInfo& fi : st.matches) out += infoText(fi) + "\n";
            return ok(out);
        }
        case 'y': return okN(fs.fileKey((int)toL(a(0))));
        case 'T': {
            uint16_t d = 0, t = 0;
            return fs.fileTime((int)toL(a(0)), d, t) ? ok(std::to_string(d) + kSub + std::to_string(t)) : no();
        }
        default: return no();
    }
}

void DoorFsServer::drop() {
    fs_.reset();
    door_.clear();
    for (int o : owners_) locks_.releaseOwner(o);
    owners_.clear();
}

bool RemoteHostFs::call(std::initializer_list<std::string> fields, std::string& data) {
    if (!begun_) {  // the main opens this door's folder first
        std::string r;
        if (!ask_ || !ask_(std::string("HB") + kSep + door_, r) || r.empty() || r[0] != '1') return false;
        begun_ = true;
    }
    std::string req = "H";
    bool        first = true;
    for (const std::string& f : fields) {
        req += first ? f : std::string(1, kSep) + f;
        first = false;
    }
    std::string reply;
    if (!ask_(req, reply) || reply.empty() || reply[0] != '1') return false;
    data = reply.substr(1);
    return true;
}

long long RemoteHostFs::number(std::initializer_list<std::string> fields, long long fail) {
    std::string d;
    return call(fields, d) ? toL(d) : fail;
}

int RemoteHostFs::open(const std::string& p, int access, bool create, bool truncate, bool exclusive) {
    int h = (int)number({"o", cwd_, p, std::to_string(access), create ? "1" : "0", truncate ? "1" : "0", exclusive ? "1" : "0"}, -5);
    if (h >= 0) handles_[h] = Handle();
    return h;
}

void RemoteHostFs::dropCaches() {
    for (auto& h : handles_) h.second.cacheAt = -1, h.second.cache.clear();
}

int RemoteHostFs::read(int h, void* buf, int n) {
    Handle& hd = handles_[h];
    int     total = 0;
    while (total < n) {
        int64_t at = hd.pos;
        if (hd.cacheAt < 0 || at < hd.cacheAt || at >= hd.cacheAt + (int64_t)hd.cache.size()) {  // fetch the next piece
            std::string d;
            if (!call({"p", std::to_string(h), std::to_string((long long)at), std::to_string(kPiece)}, d)) return total ? total : -5;
            if (d.empty() || d[0] != ':') return total ? total : (int)toL(d);
            hd.cache = d.substr(1);
            hd.cacheAt = at;
            if (hd.cache.empty()) break;  // the end of the file
        }
        size_t from = (size_t)(at - hd.cacheAt), k = hd.cache.size() - from;
        if (k > (size_t)(n - total)) k = (size_t)(n - total);
        std::memcpy((char*)buf + total, hd.cache.data() + from, k);
        total += (int)k;
        hd.pos += (int64_t)k;
        if (hd.cache.size() < kPiece && hd.pos >= hd.cacheAt + (int64_t)hd.cache.size()) break;  // short piece: the end
    }
    return total;
}

int RemoteHostFs::write(int h, const void* buf, int n) {
    Handle& hd = handles_[h];
    hd.cacheAt = -1;  // what we read may be stale now
    hd.cache.clear();
    if (n == 0) return (int)number({"q", std::to_string(h), std::to_string((long long)hd.pos), std::string()}, -5);  // truncate here
    int total = 0;
    while (total < n) {
        int piece = n - total < (int)kPiece ? n - total : (int)kPiece;
        int w = (int)number({"q", std::to_string(h), std::to_string((long long)hd.pos), std::string((const char*)buf + total, (size_t)piece)}, -5);
        if (w < 0) return total ? total : w;
        total += w;
        hd.pos += w;
        if (w < piece) break;
    }
    return total;
}

int64_t RemoteHostFs::seek(int h, int64_t off, int whence) {
    Handle& hd = handles_[h];
    int64_t base = whence == 0 ? 0 : whence == 1 ? hd.pos : number({"z", std::to_string(h)}, -1);
    if (base < 0 || base + off < 0) return -1;
    hd.pos = base + off;
    return hd.pos;
}
int RemoteHostFs::close(int h) {
    handles_.erase(h);
    return (int)number({"c", std::to_string(h)}, -6);
}
bool RemoteHostFs::stat(const std::string& p, door::FileInfo& out) {
    std::string d;
    return call({"t", cwd_, p}, d) && infoFrom(d, out);
}
int  RemoteHostFs::remove(const std::string& p) { return (int)number({"m", cwd_, p}, -5); }
int  RemoteHostFs::rename(const std::string& a, const std::string& b) { return (int)number({"n", cwd_, a, b}, -5); }
int  RemoteHostFs::mkdir(const std::string& p) { return (int)number({"k", cwd_, p}, -5); }
int  RemoteHostFs::rmdir(const std::string& p) { return (int)number({"K", cwd_, p}, -5); }
bool RemoteHostFs::isDir(const std::string& p) {
    std::string d;
    return call({"d", cwd_, p}, d);
}
bool RemoteHostFs::find(const std::string& pattern, uint8_t attr, door::FindState& st) {
    std::string d;
    st = door::FindState();
    if (!call({"f", cwd_, pattern, std::to_string(attr)}, d)) return false;
    for (const std::string& line : parts(d, '\n')) {
        door::FileInfo fi;
        if (!line.empty() && infoFrom(line, fi)) st.matches.push_back(fi);
    }
    return !st.matches.empty();
}
int  RemoteHostFs::fileKey(int h) { return (int)number({"y", std::to_string(h)}, -1); }
bool RemoteHostFs::fileTime(int h, uint16_t& dosDate, uint16_t& dosTime) {
    std::string d;
    if (!call({"T", std::to_string(h)}, d)) return false;
    auto p = parts(d, kSub);
    if (p.size() < 2) return false;
    dosDate = (uint16_t)toL(p[0]);
    dosTime = (uint16_t)toL(p[1]);
    return true;
}

namespace {
bool lockCall(DoorFsServer::Ask& ask, char op, std::initializer_list<long long> v) {
    std::string req = std::string("H") + op, reply;
    for (long long x : v) req += std::string(1, kSep) + std::to_string(x);
    return ask && ask(req, reply) && !reply.empty() && reply[0] == '1';
}
}  // namespace

bool RemoteLockTable::lock(int key, uint32_t off, uint32_t len, int owner) {
    if (fs_) fs_->dropCaches();  // the record may have changed since it was read
    return lockCall(ask_, 'L', {key, off, len, owner});
}
bool RemoteLockTable::unlock(int key, uint32_t off, uint32_t len, int owner) { return lockCall(ask_, 'U', {key, off, len, owner}); }
void RemoteLockTable::releaseOwner(int owner) { lockCall(ask_, 'R', {owner}); }

std::string lordNodeFile(const std::string& node1, int node) {
    std::string n = std::to_string(node), out;
    bool        header = false;
    for (const std::string& raw : parts(node1, '\n')) {
        std::string line = raw;
        bool        cr = !line.empty() && line.back() == '\r';
        if (cr) line.pop_back();
        size_t at = line.find("for node ");
        if (at != std::string::npos) {
            line = line.substr(0, at) + "for node " + n;
            header = true;
        }
        if (line.rfind("BBSDROP", 0) == 0) line = "BBSDROP C:\\NODE" + n + "\\";
        out += line + (cr || !line.empty() ? "\r\n" : "");
    }
    while (out.size() >= 4 && out.compare(out.size() - 4, 4, "\r\n\r\n") == 0) out.erase(out.size() - 2);
    if (!header) out = ";AXIS: LORD node data for node " + n + "\r\n" + out;
    return out;
}

std::string doorArgs(const std::string& args, int node) {
    std::string out;
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i] == '%' && i + 1 < args.size() && (args[i + 1] == 'N' || args[i + 1] == 'n')) {
            out += std::to_string(node);
            i++;
        } else {
            out += args[i];
        }
    }
    return out;
}

}  // namespace axis::cluster
