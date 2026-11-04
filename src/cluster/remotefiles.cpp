// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "cluster/remotefiles.h"

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

std::string FileServer::handle(const std::string& req) {
    if (req.empty()) return no();
    char op = req[0];
    if (op != 'S' && op != 'A') open_.reset();  // a download is over: an open file holds SD buffers
    auto f = splitN(req.size() > 1 ? req.substr(2) : std::string(), op == 'W' ? 2 : 8);
    auto arg = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
    switch (op) {
        case 'L': {  // list: name, size, description per line
            std::string out;
            for (auto& e : lib_.list(arg(0))) out += e.name + kSep + std::to_string(e.size) + kSep + e.desc + kSep + std::to_string(e.date) + "\n";
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
            uploads_[id] = std::move(w);
            return ok(std::to_string(id));
        }
        case 'W': {  // upload data
            auto it = uploads_.find((int)toU(arg(0)));
            return it != uploads_.end() && it->second->write((const uint8_t*)arg(1).data(), arg(1).size()) ? ok() : no();
        }
        case 'F': {  // finish an upload: keep or throw away
            auto it = uploads_.find((int)toU(arg(0)));
            if (it == uploads_.end()) return no();
            bool done = it->second->finish(arg(1) == "1");
            uploads_.erase(it);
            open_.reset();  // the area changed
            return done ? ok() : no();
        }
        case 'D': return lib_.describe(arg(0), arg(1), arg(2)) ? ok() : no();
        case 'X':
            open_.reset();
            return lib_.remove(arg(0), arg(1)) ? ok() : no();
        case 'N':
            open_.reset();
            return lib_.rename(arg(0), arg(1), arg(2)) ? ok() : no();
        default: return no();
    }
}

bool RemoteFileLibrary::call(std::initializer_list<std::string> fields, std::string& data) {
    std::string req;
    for (auto& f : fields) req += (req.empty() ? "" : std::string(1, kSep)) + f;
    std::string reply;
    if (!ask_ || !ask_(req, reply) || reply.empty() || reply[0] != '1') return false;
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
    return call({"D", a, n, d}, ignored);
}
bool RemoteFileLibrary::remove(const std::string& a, const std::string& n) {
    std::string ignored;
    return call({"X", a, n}, ignored);
}
bool RemoteFileLibrary::rename(const std::string& a, const std::string& from, const std::string& to) {
    std::string ignored;
    return call({"N", a, from, to}, ignored);
}

}  // namespace axis::cluster
