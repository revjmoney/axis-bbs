// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/viewcache.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "bbs/ansiviewer.h"
#include "bbs/inflatelease.h"
#include "net/crc.h"

namespace axis::bbs::viewcache {

namespace {
// A piece the copy carries: one the viewer shows, packed in a way this reader can open.
bool viewable(const ZipEntry& e) { return isAnsiPiece(e.name) && (e.method == 0 || e.method == 8); }

bool isZip(const std::string& n) {
    if (n.size() < 4) return false;
    std::string x = n.substr(n.size() - 4);
    for (char& c : x) c = (char)std::toupper((unsigned char)c);
    return x == ".ZIP";
}

void le16(std::string& s, uint32_t v) {
    s += (char)(v & 0xFF);
    s += (char)((v >> 8) & 0xFF);
}
void le32(std::string& s, uint32_t v) {
    le16(s, v & 0xFFFF);
    le16(s, v >> 16);
}
}  // namespace

std::string cacheName(const std::vector<ZipEntry>& es) {
    uint32_t crc = 0xFFFFFFFFu;
    unsigned n = 0;
    for (const ZipEntry& e : es) {
        if (!viewable(e)) continue;
        n++;
        std::string k = e.name;
        le32(k, e.crc);
        le32(k, e.usize);
        crc = net::crc32Update(crc, (const uint8_t*)k.data(), k.size());
    }
    char b[32];
    std::snprintf(b, sizeof b, "%08X-%04X.ZIP", (unsigned)~crc, n & 0xFFFF);
    return b;
}

bool matches(const std::vector<ZipEntry>& pack, const std::vector<ZipEntry>& copy) {
    size_t j = 0;
    for (const ZipEntry& e : pack) {
        if (!viewable(e)) continue;
        if (j >= copy.size()) return false;
        const ZipEntry& c = copy[j++];
        if (c.method != 0 || c.name != e.name || c.crc != e.crc || c.usize != e.usize) return false;
    }
    return j == copy.size();
}

Filler::Filler(FileLibrary& lib, std::vector<std::string> areas, Room room, Space space, size_t stepBytes)
    : lib_(lib), areas_(std::move(areas)), room_(std::move(room)), space_(std::move(space)), stepBytes_(stepBytes) {}

Filler::~Filler() { dropCopy(); }

void Filler::dropCopy() {
    stream_.reset();  // first: it points into the window
    if (leased_) inflate::give(this);
    leased_ = false;
    if (out_) out_->finish(false);
    out_.reset();
    recs_.clear();
}

bool Filler::write(const void* p, size_t n) {
    if (!ok_ || !out_->write((const uint8_t*)p, n)) return ok_ = false;
    outBytes_ += (uint32_t)n;
    return true;
}

bool Filler::nextPack() {
    while (area_ < areas_.size()) {
        if (!listed_) {
            packs_.clear();
            for (auto& f : lib_.list(areas_[area_]))
                if (isZip(f.name)) packs_.push_back(f.name);
            pack_ = 0;
            listed_ = true;
            return true;  // listing an area is a step of its own
        }
        if (pack_ < packs_.size()) {
            file_ = lib_.open(areas_[area_], packs_[pack_++]);
            if (file_) {
                RandomFile* f = file_.get();
                zip_.reset(new ZipReader([f](uint32_t off, uint8_t* buf, size_t n) { return f->readAt(off, buf, n); }, f->size()));
                if (zip_->open()) return startPack();
            }
            zip_.reset();
            file_.reset();
            return true;
        }
        area_++;
        listed_ = false;
        std::vector<std::string>().swap(packs_);
    }
    return false;
}

bool Filler::startPack() {
    const std::vector<ZipEntry>& es = zip_->entries();
    uint64_t need = 22, pieces = 0;
    for (const ZipEntry& e : es)
        if (viewable(e)) {
            pieces++;
            need += 30 + 46 + 2 * (uint64_t)e.name.size() + e.usize;
        }
    std::string name = cacheName(es);
    auto        it = std::find(have_.begin(), have_.end(), name);
    if (it != have_.end()) keep_[(size_t)(it - have_.begin())] = true;  // already copied
    if (it != have_.end() || pieces == 0 || (space_ && !space_(need + 1048576u))) {
        zip_.reset();
        file_.reset();
        return true;
    }
    out_ = lib_.create(kArea, name);
    if (!out_) {
        zip_.reset();
        file_.reset();
        return true;
    }
    outName_ = name;
    outBytes_ = 0;
    ok_ = true;
    recs_.clear();
    entry_ = 0;
    return true;
}

bool Filler::finishCopy() {
    std::string cd;
    for (const Rec& r : recs_) {
        const ZipEntry& e = zip_->entries()[r.entry];
        le32(cd, 0x02014b50u);
        le16(cd, 20);
        le16(cd, 10);
        le16(cd, 0);
        le16(cd, 0);  // stored
        le16(cd, e.time);
        le16(cd, e.date);
        le32(cd, e.crc);
        le32(cd, e.usize);
        le32(cd, e.usize);
        le16(cd, (uint32_t)e.name.size());
        le16(cd, 0);
        le16(cd, 0);
        le16(cd, 0);
        le16(cd, 0);
        le32(cd, 0);
        le32(cd, r.offset);
        cd += e.name;
    }
    uint32_t dirSize = (uint32_t)cd.size(), dirAt = outBytes_;
    le32(cd, 0x06054b50u);  // the end record
    le16(cd, 0);
    le16(cd, 0);
    le16(cd, (uint32_t)recs_.size());
    le16(cd, (uint32_t)recs_.size());
    le32(cd, dirSize);
    le32(cd, dirAt);
    le16(cd, 0);
    write(cd.data(), cd.size());
    bool kept = ok_ && out_->finish(true);
    out_.reset();
    if (kept) copied_++;  // (a finish that failed throws its temp file away; a copy already there by that name is the same pack)
    have_.push_back(outName_);  // so a second pack with the same pieces in this pass is not copied twice
    keep_.push_back(true);
    recs_.clear();
    zip_.reset();
    file_.reset();
    return true;
}

bool Filler::copyStep() {
    const std::vector<ZipEntry>& es = zip_->entries();
    if (!stream_) {  // the next piece: its local header, then its bytes from the next step
        while (entry_ < es.size() && !viewable(es[entry_])) entry_++;
        if (entry_ >= es.size()) return finishCopy();
        const ZipEntry& e = es[entry_];
        uint8_t *w = nullptr, *s = nullptr;
        if (e.method == 8) {
            if (!inflate::take(this, w, s)) return true;  // a caller's piece or an upload check has it, or the heap is short: later
            leased_ = true;
            if (room_ && !room_(kFillFree, kFillLargest)) {  // the board is too low with it taken: later
                inflate::give(this);
                leased_ = false;
                return true;
            }
        }
        recs_.push_back({entry_, outBytes_});
        std::string h;
        le32(h, 0x04034b50u);
        le16(h, 10);
        le16(h, 0);
        le16(h, 0);  // stored
        le16(h, e.time);
        le16(h, e.date);
        le32(h, e.crc);
        le32(h, e.usize);
        le32(h, e.usize);
        le16(h, (uint32_t)e.name.size());
        le16(h, 0);
        h += e.name;
        write(h.data(), h.size());
        stream_.reset(new ZipReader::Stream(*zip_, e, w, w ? inflate::kWindow : 0, s));
        crc_ = 0xFFFFFFFFu;
        if (!ok_) {
            dropCopy();
            zip_.reset();
            file_.reset();
        }
        return true;
    }
    uint8_t buf[512];
    size_t  done = 0;
    while (done < stepBytes_) {
        size_t n = stream_->read(buf, sizeof buf);
        if (n == 0) break;
        crc_ = net::crc32Update(crc_, buf, n);
        if (!write(buf, n)) break;
        done += n;
    }
    if (ok_ && done >= stepBytes_) return true;  // more next step
    const ZipEntry& e = es[entry_];
    bool good = ok_ && stream_->result() == ZipReader::Result::Ok && ~crc_ == e.crc;
    stream_.reset();
    if (leased_) inflate::give(this);
    leased_ = false;
    if (!good) {  // a damaged piece or a failed write: no copy of this pack (the viewer plays it from its ZIP)
        dropCopy();
        zip_.reset();
        file_.reset();
        return true;
    }
    entry_++;
    return true;
}

bool Filler::step() {
    if (!listedCache_) {  // the copies on the card now: listed once a pass, never looked up one by one
        for (auto& f : lib_.list(kArea))
            if (isZip(f.name)) have_.push_back(f.name);
        keep_.assign(have_.size(), false);
        listedCache_ = true;
        return true;
    }
    if (out_) return copyStep();
    if (nextPack()) return true;
    while (removeAt_ < have_.size()) {  // every pack seen: the copies none of them matched go
        size_t i = removeAt_++;
        if (keep_[i]) continue;
        if (lib_.remove(kArea, have_[i])) removed_++;
        return true;
    }
    return false;
}

}  // namespace axis::bbs::viewcache
