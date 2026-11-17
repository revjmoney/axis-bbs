// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/remotefiles.h"
#include "bbs/features.h"
#if AXIS_FILES

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace axis::cluster {

namespace {
constexpr char kSep = '\x1f';

// the first n-1 fields split at 0x1F, the rest (an upload's data) taken whole
std::vector<std::string> splitN(const std::string& s, size_t n) {
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
uint32_t    toU(const std::string& s) { return (uint32_t)std::strtoul(s.c_str(), nullptr, 10); }
std::string ok(const std::string& data = "") { return "1" + data; }
std::string no() { return "0"; }
}  // namespace

#if AXIS_NETS
std::string (*FileServer::netOp)(const void*, char, const std::vector<std::string>&) = nullptr;
void (*FileServer::uploaded)(const std::string&, const std::string&, const std::string&, const std::string&) = nullptr;
#endif

std::string FileServer::handle(const std::string& req) {
    if (req.empty()) return no();
    char op = req[0];
    auto f = splitN(req.size() > 1 ? req.substr(2) : std::string(), op == 'W' ? 2 : 8);
    f.resize(8);  // (missing fields read empty)
    auto arg = [&](size_t i) -> const std::string& { return f[i]; };
#if AXIS_NETS
    if (op == 'G' || op == 'b' || op == 't' || op == 'e') return netOp ? netOp(this, op, f) : no();  // (a download's open file stays)
    std::string at;  // an upload's "area<1F>name" (not for W: its second field is the data)
    if (op == 'C' || op == 'D' || op == 'X' || op == 'N') at = f[0] + kSep + f[1];
#endif
    if (op != 'S' && op != 'A') open_.reset();  // a download is over: an open file holds SD buffers
    // the area and file names a follower sends must stay inside the file areas, whatever the store does
    size_t names = op == 'L' ? 1 : op == 'N' ? 3 : (op == 'R' || op == 'S' || op == 'A' || op == 'C' || op == 'D' || op == 'X') ? 2 : 0;
    for (size_t i = 0; i < names; i++)
        if (!bbs::safeFileName(arg(i))) return no();
    switch (op) {
        case 'L': {  // list: name, size, description per line
            std::string out;
            for (auto& e : lib_.list(arg(0))) {
                std::string line = e.name + kSep + std::to_string(e.size) + kSep + e.desc + kSep + std::to_string(e.date) + "\n";
                if (out.size() + line.size() > kListMax) break;  // too many for one frame: the rest are left off
                out += line;
            }
            return ok(out);
        }
        case 'R': {  // read a small file whole
            std::string out;
            size_t      max = toU(arg(2));
            if (max > kChunk) max = kChunk;
            return lib_.read(arg(0), arg(1), out, max) ? ok(out) : no();
        }
        case 'S':    // open: its size
        case 'A': {  // read a piece: offset, length
            if (!open_ || openArea_ != arg(0) || openName_ != arg(1)) {
                open_ = lib_.open(arg(0), arg(1));
                openArea_ = arg(0);
                openName_ = arg(1);
            }
            if (!open_) return no();
            if (op == 'S') return ok(std::to_string(open_->size()));
            uint32_t off = toU(arg(2)), len = toU(arg(3));
            if (len > kChunk || (uint64_t)off + len > open_->size()) return no();
            std::string out(len, '\0');
            return open_->readAt(off, (uint8_t*)&out[0], len) ? ok(out) : no();
        }
        case 'C': {  // start an upload
            auto w = lib_.create(arg(0), arg(1));
            if (!w) return no();
            int id = nextUpload_++;
            Upload& u = uploads_[id];
            u.w = std::move(w);
#if AXIS_NETS
            u.at = at;
#endif
            return ok(std::to_string(id));
        }
        case 'W': {  // upload data
            auto it = uploads_.find((int)toU(arg(0)));
            return it != uploads_.end() && it->second.w->write((const uint8_t*)arg(1).data(), arg(1).size()) ? ok() : no();
        }
        case 'F': {  // finish an upload: keep or throw away
            auto it = uploads_.find((int)toU(arg(0)));
            if (it == uploads_.end()) return no();
            bool done = it->second.w->finish(arg(1) == "1");
#if AXIS_NETS
            if (done && arg(1) == "1") {
                if (kept_.size() >= 4) kept_.erase(kept_.begin());  // (a few at once: one per caller on the follower)
                kept_.push_back(std::move(it->second.at));
            }
#endif
            uploads_.erase(it);
            open_.reset();  // the area changed
            return done ? ok() : no();
        }
        case 'D': {  // describe: area, name, text (and, for an upload, the uploader's handle)
            if (!lib_.describe(arg(0), arg(1), arg(2))) return no();
#if AXIS_NETS
            // an upload's description is its last step (TransferJob::finish, after any ZIP rework): now it can be listed
            auto k = std::find(kept_.begin(), kept_.end(), at);
            if (k != kept_.end()) {
                kept_.erase(k);
                if (uploaded) uploaded(arg(0), arg(1), arg(2), arg(3));
            }
#endif
            return ok();
        }
        case 'X':
        case 'N': {
            open_.reset();
            if (!(op == 'X' ? lib_.remove(arg(0), arg(1)) : lib_.rename(arg(0), arg(1), arg(2)))) return no();
#if AXIS_NETS
            // a kept upload goes with its file, or follows its new name (a ZIP: the rezipped temp replaces the upload)
            auto k = std::find(kept_.begin(), kept_.end(), at);
            if (k != kept_.end()) {
                if (op == 'N') *k = f[0] + kSep + f[2];
                else kept_.erase(k);
            }
#endif
            return ok();
        }
        default: return no();
    }
}

bool RemoteFileLibrary::raw(std::initializer_list<std::string> fields, std::string& reply) {
    std::string req;
    for (auto& f : fields) req += (req.empty() ? "" : std::string(1, kSep)) + f;
    return raw(req, reply);
}
bool RemoteFileLibrary::raw(const std::string& req, std::string& reply) { return ask_ && ask_(req, reply) && !reply.empty(); }

bool RemoteFileLibrary::call(std::initializer_list<std::string> fields, std::string& data) {
    std::string reply;
    if (!raw(fields, reply) || reply[0] != '1') return false;
    data = reply.substr(1);
    return true;
}

std::vector<bbs::FileEntry> RemoteFileLibrary::list(const std::string& areaId) {
    std::vector<bbs::FileEntry> v;
    std::string                 data;
    if (!call({"L", areaId}, data)) return v;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) e = data.size();
        auto parts = splitN(data.substr(pos, e - pos), 4);
        pos = e + 1;
        if (parts.size() < 3 || parts[0].empty()) continue;
        v.push_back({parts[0], toU(parts[1]), parts[2], parts.size() > 3 ? toU(parts[3]) : 0});
    }
    return v;
}

bool RemoteFileLibrary::read(const std::string& areaId, const std::string& name, std::string& out, size_t max) {
    if (max <= FileServer::kChunk) return call({"R", areaId, name, std::to_string(max)}, out);
    auto f = open(areaId, name);  // bigger: piece by piece
    if (!f) return false;
    size_t n = f->size() < max ? f->size() : max;
    out.assign(n, '\0');
    return n == 0 || f->readAt(0, (uint8_t*)&out[0], n);
}

namespace {
class RemoteRead : public bbs::RandomFile {
public:
    RemoteRead(RemoteFileLibrary& lib, std::string area, std::string name, uint32_t size)
        : lib_(lib), area_(std::move(area)), name_(std::move(name)), size_(size) {}
    uint32_t size() const override { return size_; }
    bool     readAt(uint32_t off, uint8_t* buf, size_t n) override {
        if ((uint64_t)off + n > size_) return false;
        // Small reads (a stored ANSI piece goes 512 bytes at a time) come out of the last 2 KB piece fetched: one round
        // trip per kChunk instead of one per read.
        if (n < FileServer::kChunk) {
            if (!(off >= aheadAt_ && (uint64_t)off + n <= (uint64_t)aheadAt_ + ahead_.size())) {
                uint32_t k = size_ - off < FileServer::kChunk ? size_ - off : (uint32_t)FileServer::kChunk;
                std::string data;
                if (!lib_.call({"A", area_, name_, std::to_string(off), std::to_string(k)}, data) || data.size() != k) return false;
                ahead_ = std::move(data);
                aheadAt_ = off;
            }
            std::memcpy(buf, ahead_.data() + (off - aheadAt_), n);
            return true;
        }
        while (n) {
            size_t      k = n < FileServer::kChunk ? n : FileServer::kChunk;
            std::string data;
            if (!lib_.call({"A", area_, name_, std::to_string(off), std::to_string(k)}, data) || data.size() != k) return false;
            std::memcpy(buf, data.data(), k);
            buf += k;
            off += (uint32_t)k;
            n -= k;
        }
        return true;
    }

private:
    RemoteFileLibrary& lib_;
    std::string        area_, name_;
    uint32_t           size_;
    std::string        ahead_;  // the last piece fetched, at aheadAt_
    uint32_t           aheadAt_ = 0;
};

class RemoteWrite : public bbs::WriteFile {
public:
    RemoteWrite(RemoteFileLibrary& lib, std::string id) : lib_(lib), id_(std::move(id)) {}
    ~RemoteWrite() override {
        if (!done_) finish(false);  // abandoned (hang-up): the main throws it away
    }
    bool write(const uint8_t* p, size_t n) override {
        while (n) {
            size_t k = n < FileServer::kChunk ? n : FileServer::kChunk;
            std::string ignored;
            if (!lib_.call({"W", id_, std::string((const char*)p, k)}, ignored)) return false;
            p += k;
            n -= k;
        }
        return true;
    }
    bool finish(bool keep) override {
        done_ = true;
        std::string ignored;
        return lib_.call({"F", id_, keep ? "1" : "0"}, ignored);
    }

private:
    RemoteFileLibrary& lib_;
    std::string        id_;
    bool               done_ = false;
};
}  // namespace

std::unique_ptr<bbs::RandomFile> RemoteFileLibrary::open(const std::string& areaId, const std::string& name) {
    std::string data;
    if (!call({"S", areaId, name}, data)) return nullptr;
    return std::make_unique<RemoteRead>(*this, areaId, name, toU(data));
}

std::unique_ptr<bbs::WriteFile> RemoteFileLibrary::create(const std::string& areaId, const std::string& name) {
    std::string id;
    if (!call({"C", areaId, name}, id)) return nullptr;
    return std::make_unique<RemoteWrite>(*this, id);
}

bool RemoteFileLibrary::describe(const std::string& a, const std::string& n, const std::string& d) {
    std::string ignored;
#if AXIS_NETS
    return call({"D", a, n, d, who ? who() : std::string()}, ignored);  // (an older main reads three fields)
#else
    return call({"D", a, n, d}, ignored);
#endif
}
bool RemoteFileLibrary::remove(const std::string& a, const std::string& n) {
    std::string ignored;
    return call({"X", a, n}, ignored);
}
bool RemoteFileLibrary::rename(const std::string& a, const std::string& from, const std::string& to) {
    std::string ignored;
    return call({"N", a, from, to}, ignored);
}


#if AXIS_NETS
int RemoteFileLibrary::netPage(int skip, int max, std::vector<bbs::FileEntry>& out, std::vector<std::string>& ids,
                               std::vector<std::string>& homes) {
    std::string data;
    if (!call({"G", std::to_string(skip), std::to_string(max)}, data)) return -1;  // (a main without Net Files says "0")
    size_t pos = data.find('\n');
    int    next = (int)std::strtol(data.c_str(), nullptr, 10);
    while (pos != std::string::npos && ++pos < data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) e = data.size();
        auto p = splitN(data.substr(pos, e - pos), 6);  // id name size desc home reach (shown already in desc)
        pos = e;
        if (p.size() < 5 || p[0].empty()) continue;
        out.push_back({p[1], toU(p[2]), p[3], 0});
        ids.push_back(p[0]);
        homes.push_back(p[4]);
    }
    return next;
}

char RemoteFileLibrary::netFetch(const std::string& id, const std::string& handle, int level, std::string& rest) {
    std::string reply;
    if (!raw({"b", id, handle, "0", std::to_string(level)}, reply)) return 0;
    rest.assign(reply, 1, std::string::npos);
    return reply[0];
}

bool RemoteFetchFile::ready(uint32_t off, size_t n) {
    if (!word_.empty() || has(off, n)) return true;
    if (waiting_ && clock_ && clock_() - waitMs_ < kWaitMs) return false;
    uint32_t    k = size_ - off < FileServer::kChunk ? size_ - off : (uint32_t)FileServer::kChunk;  // (n is a block: at most 1 KB)
    char        req[32];
    std::snprintf(req, sizeof req, "t%c%u%c%u", kSep, (unsigned)off, kSep, (unsigned)k);
    std::string reply;
    if (off >= size_ || !lib_.raw(req, reply)) reply = "0closed";
    waiting_ = reply[0] == '2';
    if (waiting_) {
        waitMs_ = clock_ ? clock_() : 0;
        return false;
    }
    if (reply[0] == '1' && reply.size() == 1 + (size_t)k) {
        ahead_.assign(reply, 1, std::string::npos);
        aheadAt_ = off;
    } else
        word_ = reply[0] == '0' && reply.size() > 1 ? reply.substr(1) : std::string("closed");
    return true;
}

bool RemoteFetchFile::readAt(uint32_t off, uint8_t* buf, size_t n) {
    if (!has(off, n) && (!ready(off, n) || !has(off, n))) return false;
    std::memcpy(buf, ahead_.data() + (off - aheadAt_), n);
    return true;
}
#endif

}  // namespace axis::cluster

#endif  // AXIS_FILES
