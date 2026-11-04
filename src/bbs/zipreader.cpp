// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/zipreader.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include "esp32/rom/miniz.h"  // the ROM's tinfl: same API as miniz's, no flash spent on it
#else
#include "miniz_tinfl.h"
#endif

namespace axis::bbs {

namespace {
constexpr size_t kInput = 1024;
uint16_t         u16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t         u32(const uint8_t* p) { return u16(p) | (uint32_t)u16(p + 2) << 16; }
size_t           align8(size_t n) { return (n + 7) & ~(size_t)7; }
}  // namespace

const size_t ZipReader::kWorkspace = align8(sizeof(tinfl_decompressor)) + TINFL_LZ_DICT_SIZE + kInput;
const size_t ZipReader::kStateBytes = align8(sizeof(tinfl_decompressor)) + kInput;

bool ZipReader::open() {
    entries_.clear();
    if (size_ < 22) return false;
    // the end record is in the last 22 bytes plus up to 64 KB of comment: search back 1 KB at a time
    uint8_t  buf[1024 + 22];
    uint32_t eocd = 0;
    bool     found = false;
    for (uint32_t back = 0; back < 65536 + 22 && !found && back < size_; back += 1024) {
        uint32_t end = size_ - back, start = end > sizeof buf ? end - (uint32_t)sizeof buf : 0;
        if (!read_(start, buf, end - start)) return false;
        for (int i = (int)(end - start) - 22; i >= 0; i--)
            if (u32(buf + i) == 0x06054b50) {
                eocd = start + (uint32_t)i;
                found = true;
                break;
            }
        if (start == 0) break;
    }
    if (!found || !read_(eocd, buf, 22)) return false;
    uint16_t count = u16(buf + 10);
    uint32_t cdOffset = u32(buf + 16);
    uint32_t at = cdOffset;
    for (uint16_t i = 0; i < count && entries_.size() < kMaxEntries; i++) {
        uint8_t h[46];
        if (!read_(at, h, 46) || u32(h) != 0x02014b50) return false;
        uint16_t nameLen = u16(h + 28), extra = u16(h + 30), comment = u16(h + 32);
        ZipEntry e;
        e.flags = u16(h + 8);
        e.method = u16(h + 10);
        e.time = u16(h + 12);
        e.date = u16(h + 14);
        e.crc = u32(h + 16);
        e.csize = u32(h + 20);
        e.usize = u32(h + 24);
        e.local = u32(h + 42);
        e.name.resize(nameLen);
        if (nameLen && !read_(at + 46, (uint8_t*)&e.name[0], nameLen)) return false;
        at += 46u + nameLen + extra + comment;
        if (!e.name.empty() && e.name.back() != '/') entries_.push_back(std::move(e));  // skip folders
    }
    return true;
}

ZipReader::Stream::Stream(ZipReader& zip, const ZipEntry& e, uint8_t* ws, size_t wsSize, uint8_t* state) : zip_(zip), e_(e) {
    if (e.method != 0 && e.method != 8) {
        result_ = Result::Unsupported;
        finished_ = true;
        return;
    }
    uint8_t lh[30];
    if (!zip.read_(e.local, lh, 30)) {
        result_ = Result::ReadError;
        finished_ = true;
        return;
    }
    if (u32(lh) != 0x04034b50) {
        result_ = Result::Corrupt;
        finished_ = true;
        return;
    }
    data_ = e.local + 30 + u16(lh + 26) + u16(lh + 28);
    if (e.method == 0) return;
    if (state && wsSize >= TINFL_LZ_DICT_SIZE) {
        inf_ = state;
        in_ = state + align8(sizeof(tinfl_decompressor));
        dict_ = ws;
    } else if (wsSize >= kWorkspace) {
        inf_ = ws;
        dict_ = ws + align8(sizeof(tinfl_decompressor));
        in_ = dict_ + TINFL_LZ_DICT_SIZE;
    } else {
        result_ = Result::NoMemory;
        finished_ = true;
        return;
    }
    tinfl_init((tinfl_decompressor*)inf_);
}

size_t ZipReader::Stream::read(uint8_t* out, size_t max) {
    size_t got = 0;
    if (e_.method == 0) {  // stored: straight from the file
        while (got < max && stored_ < e_.usize && !finished_) {
            uint32_t n = e_.usize - stored_ < (uint32_t)(max - got) ? e_.usize - stored_ : (uint32_t)(max - got);
            if (!zip_.read_(data_ + stored_, out + got, n)) {
                result_ = Result::ReadError;
                finished_ = true;
                break;
            }
            stored_ += n;
            got += n;
        }
        return got;
    }
    while (got < max) {
        if (pendLen_) {  // what the last inflate step made, still to hand out
            size_t k = pendLen_ < max - got ? pendLen_ : max - got;
            std::memcpy(out + got, dict_ + pendOfs_, k);
            got += k;
            pendOfs_ += k;
            pendLen_ -= k;
            continue;
        }
        if (finished_) break;
        if (inAvail_ == 0 && inPos_ < e_.csize) {
            size_t n = e_.csize - inPos_ < kInput ? e_.csize - inPos_ : kInput;
            if (!zip_.read_(data_ + inPos_, in_, n)) {
                result_ = Result::ReadError;
                finished_ = true;
                break;
            }
            inPos_ += (uint32_t)n;
            inAvail_ = n;
            inOfs_ = 0;
        }
        size_t       inBytes = inAvail_, outBytes = TINFL_LZ_DICT_SIZE - dictOfs_;
        tinfl_status st = tinfl_decompress((tinfl_decompressor*)inf_, in_ + inOfs_, &inBytes, dict_, dict_ + dictOfs_, &outBytes,
                                           inPos_ < e_.csize ? TINFL_FLAG_HAS_MORE_INPUT : 0);
        inOfs_ += inBytes;
        inAvail_ -= inBytes;
        pendOfs_ = dictOfs_;
        pendLen_ = outBytes;
        produced_ += outBytes;
        dictOfs_ = (dictOfs_ + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) {
            finished_ = true;
            if (produced_ != e_.usize) result_ = Result::Corrupt;
        } else if (st < 0 || (st == TINFL_STATUS_NEEDS_MORE_INPUT && inAvail_ == 0 && inPos_ >= e_.csize)) {
            finished_ = true;
            result_ = Result::Corrupt;
        }
    }
    return got;
}

ZipReader::Result ZipReader::extract(const ZipEntry& e, uint8_t* ws, size_t wsSize, const Sink& sink, uint8_t* state) {
    Stream  s(*this, e, ws, wsSize, state);
    uint8_t buf[512];
    for (size_t n; (n = s.read(buf, sizeof buf)) > 0;)
        if (!sink(buf, n)) return Result::Stopped;
    return s.result();
}

}  // namespace axis::bbs
