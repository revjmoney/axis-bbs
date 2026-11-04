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

ZipReader::Result ZipReader::extract(const ZipEntry& e, uint8_t* ws, size_t wsSize, const Sink& sink, uint8_t* state) {
    if (e.method != 0 && e.method != 8) return Result::Unsupported;
    uint8_t lh[30];
    if (!read_(e.local, lh, 30)) return Result::ReadError;
    if (u32(lh) != 0x04034b50) return Result::Corrupt;
    uint32_t data = e.local + 30 + u16(lh + 26) + u16(lh + 28);
    if (e.method == 0) {
        uint8_t buf[512];
        for (uint32_t done = 0; done < e.usize;) {
            uint32_t n = e.usize - done < sizeof buf ? e.usize - done : (uint32_t)sizeof buf;
            if (!read_(data + done, buf, n)) return Result::ReadError;
            if (!sink(buf, n)) return Result::Stopped;
            done += n;
        }
        return Result::Ok;
    }
    tinfl_decompressor* inf;
    uint8_t *           dict, *in;  // dict: the 32 KB ring, the output window
    if (state && wsSize >= TINFL_LZ_DICT_SIZE) {
        inf = (tinfl_decompressor*)state;
        in = state + align8(sizeof(tinfl_decompressor));
        dict = ws;
    } else if (wsSize >= kWorkspace) {
        inf = (tinfl_decompressor*)ws;
        dict = ws + align8(sizeof(tinfl_decompressor));
        in = dict + TINFL_LZ_DICT_SIZE;
    } else {
        return Result::NoMemory;
    }
    tinfl_init(inf);
    uint32_t inPos = 0;            // compressed bytes read so far
    size_t   inAvail = 0, inOfs = 0, dictOfs = 0, produced = 0;
    for (;;) {
        if (inAvail == 0 && inPos < e.csize) {
            size_t n = e.csize - inPos < kInput ? e.csize - inPos : kInput;
            if (!read_(data + inPos, in, n)) return Result::ReadError;
            inPos += (uint32_t)n;
            inAvail = n;
            inOfs = 0;
        }
        size_t inBytes = inAvail, outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
        tinfl_status st = tinfl_decompress(inf, in + inOfs, &inBytes, dict, dict + dictOfs, &outBytes,
                                           inPos < e.csize ? TINFL_FLAG_HAS_MORE_INPUT : 0);
        inOfs += inBytes;
        inAvail -= inBytes;
        if (outBytes && !sink(dict + dictOfs, outBytes)) return Result::Stopped;
        produced += outBytes;
        dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
        if (st == TINFL_STATUS_DONE) return produced == e.usize ? Result::Ok : Result::Corrupt;
        if (st < 0) return Result::Corrupt;
        if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inAvail == 0 && inPos >= e.csize) return Result::Corrupt;
    }
}

}  // namespace axis::bbs
