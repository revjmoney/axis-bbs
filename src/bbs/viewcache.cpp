// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/viewcache.h"

#include <cctype>
#include <cstdio>

#include "bbs/ansiviewer.h"
#include "bbs/inflatelease.h"
#include "net/crc.h"

namespace axis::bbs::viewcache {

std::string name(const ZipEntry& e) {
    char b[32];
    std::snprintf(b, sizeof b, "%08X-%X.ANS", (unsigned)e.crc, (unsigned)e.usize);
    return b;
}

std::unique_ptr<RandomFile> open(FileLibrary& lib, const ZipEntry& e) {
    std::unique_ptr<RandomFile> f = lib.open(kArea, name(e));
    if (f && f->size() != e.usize) f.reset();  // a short copy (a card pulled mid-write) is as good as none
    return f;
}

bool unpack(FileLibrary& lib, ZipReader& zip, const ZipEntry& e, uint8_t* ws, size_t wsSize, uint8_t* state) {
    std::unique_ptr<WriteFile> w = lib.create(kArea, name(e));
    if (!w) return false;
    uint32_t          crc = 0xFFFFFFFFu;
    bool              wrote = true;
    ZipReader::Result res = zip.extract(
        e, ws, wsSize,
        [&](const uint8_t* p, size_t n) {
            crc = net::crc32Update(crc, p, n);
            if (!w->write(p, n)) wrote = false;
            return wrote;
        },
        state);
    bool ok = wrote && res == ZipReader::Result::Ok && ~crc == e.crc;
    return w->finish(ok) && ok;  // a bad copy is thrown away, never left for a viewer to play
}

static bool isZip(const std::string& n) {
    if (n.size() < 4) return false;
    std::string x = n.substr(n.size() - 4);
    for (char& c : x) c = (char)std::toupper((unsigned char)c);
    return x == ".ZIP";
}

bool Filler::nextPack() {
    while (area_ < areas_.size()) {
        if (!listed_) {
            packs_.clear();
            for (auto& f : lib_.list(areas_[area_]))
                if (isZip(f.name)) packs_.push_back(f.name);
            pack_ = 0;
            listed_ = true;
        }
        while (pack_ < packs_.size()) {
            file_ = lib_.open(areas_[area_], packs_[pack_++]);
            if (!file_) continue;
            RandomFile* f = file_.get();
            zip_.reset(new ZipReader([f](uint32_t off, uint8_t* buf, size_t n) { return f->readAt(off, buf, n); }, f->size()));
            if (zip_->open()) {
                entry_ = 0;
                return true;
            }
            zip_.reset();
            file_.reset();
        }
        area_++;
        listed_ = false;
        std::vector<std::string>().swap(packs_);
    }
    return false;
}

Filler::~Filler() { dropPiece(); }

void Filler::dropPiece() {
    stream_.reset();  // first: it points into the window
    if (out_) out_->finish(false);
    out_.reset();
    if (leased_) inflate::give(this);
    leased_ = false;
}

bool Filler::step() {
    if (stream_) {  // the piece part way: the next stepBytes of it
        uint8_t buf[512];
        size_t  done = 0;
        while (done < stepBytes_) {
            size_t n = stream_->read(buf, sizeof buf);
            if (n == 0) break;
            crc_ = net::crc32Update(crc_, buf, n);
            if (!out_->write(buf, n)) {
                wrote_ = false;
                break;
            }
            done += n;
        }
        if (wrote_ && done >= stepBytes_) return true;  // more next step
        bool ok = wrote_ && stream_->result() == ZipReader::Result::Ok && ~crc_ == zip_->entries()[entry_].crc;
        stream_.reset();
        bool kept = out_->finish(ok) && ok;  // a bad copy is thrown away, never left for a viewer to play
        out_.reset();
        inflate::give(this);
        leased_ = false;
        entry_++;
        if (kept) unpacked_++;
        return true;
    }
    if (!zip_) return nextPack();  // opening a pack is a step of its own
    const std::vector<ZipEntry>& es = zip_->entries();
    while (entry_ < es.size()) {
        const ZipEntry& e = es[entry_];
        if (e.method != 8 || !isAnsiPiece(e.name) || open(lib_, e)) {
            entry_++;
            continue;
        }
        uint8_t *w = nullptr, *s = nullptr;
        if (!inflate::take(this, w, s)) return true;  // a caller's viewer or an upload check has it, or the heap is short: later
        leased_ = true;
        out_ = lib_.create(kArea, name(e));
        if (!out_) {  // a read-only store or a full card: skip it
            inflate::give(this);
            leased_ = false;
            entry_++;
            return true;
        }
        stream_.reset(new ZipReader::Stream(*zip_, e, w, inflate::kWindow, s));
        crc_ = 0xFFFFFFFFu;
        wrote_ = true;
        return true;  // its bytes start next step
    }
    zip_.reset();  // this pack is done
    file_.reset();
    return true;
}

}  // namespace axis::bbs::viewcache
