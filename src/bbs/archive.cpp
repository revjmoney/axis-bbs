// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#pragma GCC optimize("Os")  // menus and screens: size over speed (the door CPU and transfers stay -O2)
#include "bbs/archive.h"

#include <cstring>
#include <memory>

#include "bbs/zipreader.h"

namespace axis::bbs {

namespace {
constexpr size_t kMaxEntries = 400;

uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// a RAR 5 variable-length number; false past the end
bool vint(const uint8_t* b, size_t n, size_t& at, uint64_t& v) {
    v = 0;
    for (int shift = 0; at < n && shift < 64; shift += 7) {
        uint8_t c = b[at++];
        v |= (uint64_t)(c & 0x7F) << shift;
        if (!(c & 0x80)) return true;
    }
    return false;
}

void listZip(RandomFile& f, ArchiveInfo& a) {
    ZipReader z([&f](uint32_t off, uint8_t* buf, size_t n) { return f.readAt(off, buf, n); }, f.size());
    if (!z.open()) return;
    a.kind = "ZIP";
    for (const ZipEntry& e : z.entries()) {
        ArchiveEntry x;
        x.name = e.name;
        x.size = e.usize;
        x.packed = e.csize;
        x.crc = e.crc;
        x.date = e.date;
        x.dir = !e.name.empty() && e.name.back() == '/';
        x.method = e.method == 0 ? "stored" : e.method == 8 ? "deflate" : e.method == 6 ? "implode" : e.method == 1 ? "shrink"
                 : "method " + std::to_string(e.method);
        a.entries.push_back(x);
    }
}

void listRar4(RandomFile& f, ArchiveInfo& a) {
    a.kind = "RAR";
    uint32_t pos = 7, size = f.size();
    uint8_t  h[48];
    while (pos + 7 <= size && a.entries.size() < kMaxEntries) {
        if (!f.readAt(pos, h, 7)) break;
        uint8_t  type = h[2];
        uint16_t flags = le16(h + 3), headSize = le16(h + 5);
        if (headSize < 7 || type == 0x7B) break;  // the end block (or junk)
        uint32_t add = 0;
        if (type == 0x74 && pos + 32 <= size && f.readAt(pos, h, 32)) {  // a file
            ArchiveEntry e;
            e.packed = le32(h + 7);
            e.size = le32(h + 11);
            e.crc = le32(h + 16);
            e.date = (uint16_t)(le32(h + 20) >> 16);
            uint8_t method = h[25];
            e.method = method == 0x30 ? "stored" : "rar m" + std::to_string(method - 0x30);
            uint16_t nameLen = le16(h + 26);
            uint32_t at = pos + 32 + ((flags & 0x100) ? 8 : 0);
            if (nameLen > 260) nameLen = 260;
            std::string name(nameLen, '\0');
            if (nameLen && !f.readAt(at, (uint8_t*)&name[0], nameLen)) break;
            size_t z = name.find('\0');  // (a Unicode name follows a zero)
            e.name = name.substr(0, z);
            e.dir = (flags & 0xE0) == 0xE0;
            add = e.packed;
            a.entries.push_back(e);
        } else if (flags & 0x8000) {
            if (!f.readAt(pos + 7, h, 4)) break;
            add = le32(h);
        }
        pos += headSize + add;
    }
}

void listRar5(RandomFile& f, ArchiveInfo& a) {
    a.kind = "RAR5";
    uint32_t pos = 8, size = f.size();
    std::unique_ptr<uint8_t[]> buf(new uint8_t[512]);  // heap, not stack: the loop task has 8 KB in all
    uint8_t*                   b = buf.get();
    while (pos + 6 <= size && a.entries.size() < kMaxEntries) {
        size_t n = size - pos < 512 ? size - pos : 512;
        if (!f.readAt(pos, b, n)) break;
        size_t   at = 4;  // past the header CRC
        uint64_t headSize, type, flags, extra = 0, data = 0;
        if (!vint(b, n, at, headSize) || headSize == 0) break;
        size_t start = at;
        if (!vint(b, n, at, type) || !vint(b, n, at, flags)) break;
        if ((flags & 1) && !vint(b, n, at, extra)) break;
        if ((flags & 2) && !vint(b, n, at, data)) break;
        if (type == 5) break;  // the end of the archive
        if (type == 2) {       // a file
            uint64_t fileFlags, unpacked, attr, comp, host, nameLen;
            if (!vint(b, n, at, fileFlags) || !vint(b, n, at, unpacked) || !vint(b, n, at, attr)) break;
            ArchiveEntry e;
            if (fileFlags & 2) at += 4;  // modification time
            if (fileFlags & 4) {
                if (at + 4 > n) break;
                e.crc = le32(b + at);
                at += 4;
            }
            if (!vint(b, n, at, comp) || !vint(b, n, at, host) || !vint(b, n, at, nameLen) || at + nameLen > n) break;
            e.name.assign((const char*)b + at, (size_t)nameLen);
            e.size = (uint32_t)unpacked;
            e.packed = (uint32_t)data;
            e.dir = fileFlags & 1;
            int method = (int)(comp >> 7 & 7);
            e.method = method == 0 ? "stored" : "rar5 m" + std::to_string(method);
            a.entries.push_back(e);
        }
        pos += (uint32_t)(start + headSize + data);
    }
}

void listArj(RandomFile& f, ArchiveInfo& a) {
    a.kind = "ARJ";
    uint32_t pos = 0, size = f.size();
    std::unique_ptr<uint8_t[]> buf(new uint8_t[2600]);
    uint8_t*                   b = buf.get();
    bool                       first = true;  // the first header describes the archive itself
    while (pos + 4 <= size && a.entries.size() < kMaxEntries) {
        if (!f.readAt(pos, b, 4) || b[0] != 0x60 || b[1] != 0xEA) break;
        uint16_t n = le16(b + 2);
        if (n == 0 || n > 2600 || pos + 4 + n > size) break;  // the end mark (or junk)
        if (!f.readAt(pos + 4, b, n)) break;
        uint32_t next = pos + 4 + n + 4;  // the header, its CRC
        uint8_t  ext[2];
        while (next + 2 <= size && f.readAt(next, ext, 2) && le16(ext)) next += 2 + le16(ext) + 4;  // extended headers
        next += 2;
        uint32_t packed = le32(b + 12);
        if (!first) {
            ArchiveEntry e;
            e.method = b[5] == 0 ? "stored" : "arj " + std::to_string(b[5]);
            e.dir = b[6] == 3;
            e.date = (uint16_t)(le32(b + 8) >> 16);
            e.packed = packed;
            e.size = le32(b + 16);
            e.crc = le32(b + 20);
            size_t name = b[0];
            if (name < n) e.name.assign((const char*)b + name, strnlen((const char*)b + name, n - name));
            a.entries.push_back(e);
            next += packed;
        }
        first = false;
        pos = next;
    }
}

uint32_t octal(const uint8_t* p, size_t n) {
    uint32_t v = 0;
    for (size_t i = 0; i < n && p[i] >= '0' && p[i] <= '7'; i++) v = v * 8 + (p[i] - '0');
    return v;
}

bool tarHeaderOk(const uint8_t* h) {  // the checksum: the header's bytes, its own field counted as spaces
    uint32_t sum = 0;
    for (int i = 0; i < 512; i++) sum += (i >= 148 && i < 156) ? ' ' : h[i];
    return h[0] && sum == octal(h + 148, 8);
}

void listTar(RandomFile& f, ArchiveInfo& a) {
    a.kind = "TAR";
    uint32_t pos = 0, size = f.size();
    std::unique_ptr<uint8_t[]> buf(new uint8_t[512]);
    uint8_t*                   h = buf.get();
    while (pos + 512 <= size && a.entries.size() < kMaxEntries && f.readAt(pos, h, 512) && tarHeaderOk(h)) {
        ArchiveEntry e;
        std::string  prefix = memcmp(h + 257, "ustar", 5) == 0 ? std::string((const char*)h + 345, strnlen((const char*)h + 345, 155)) : "";
        e.name = (prefix.empty() ? "" : prefix + "/") + std::string((const char*)h, strnlen((const char*)h, 100));
        if (e.name.compare(0, 2, "./") == 0) e.name.erase(0, 2);  // GNU tar's "./README.TXT"
        e.size = e.packed = octal(h + 124, 12);
        e.dir = h[156] == '5';
        e.method = "stored";
        if ((h[156] == '0' || h[156] == 0 || e.dir) && !e.name.empty()) a.entries.push_back(e);  // (links, and "./" itself, left out)
        pos += 512 + (e.size + 511) / 512 * 512;
    }
}

void listGzip(RandomFile& f, ArchiveInfo& a) {
    uint8_t h[10], tail[8];
    uint32_t size = f.size();
    if (size < 18 || !f.readAt(0, h, 10) || h[2] != 8 || !f.readAt(size - 8, tail, 8)) return;
    a.kind = "GZIP";
    ArchiveEntry e;
    e.method = "deflate";
    e.crc = le32(tail);
    e.size = le32(tail + 4);  // the original size (mod 4 GB)
    e.packed = size;
    uint32_t at = 10;
    uint8_t  flg = h[3], b[2];
    if (flg & 4) {  // extra field
        if (!f.readAt(at, b, 2)) return;
        at += 2 + le16(b);
    }
    if (flg & 8) {  // the original file name
        char c;
        while (at < size && f.readAt(at++, (uint8_t*)&c, 1) && c && e.name.size() < 255) e.name += c;
    }
    a.entries.push_back(e);
}
}  // namespace

ArchiveInfo listArchive(const std::string& name, RandomFile& f) {
    ArchiveInfo a;
    uint8_t     m[8] = {};
    if (f.size() < 4 || !f.readAt(0, m, f.size() < 8 ? 4 : 8)) return a;
    if (m[0] == 'P' && m[1] == 'K') listZip(f, a);
    else if (!memcmp(m, "Rar!\x1a\x07\x01\x00", 8)) listRar5(f, a);
    else if (!memcmp(m, "Rar!\x1a\x07\x00", 7)) listRar4(f, a);
    else if (m[0] == 0x60 && m[1] == 0xEA) listArj(f, a);
    else if (m[0] == 0x1F && m[1] == 0x8B) listGzip(f, a);
    else if (f.size() >= 512) {
        std::unique_ptr<uint8_t[]> h(new uint8_t[512]);
        if (f.readAt(0, h.get(), 512) && tarHeaderOk(h.get())) listTar(f, a);
    }
    (void)name;
    return a;
}

}  // namespace axis::bbs
