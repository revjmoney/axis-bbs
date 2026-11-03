// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Reads ZIP archives (artpacks) a piece at a time: the central directory, then one entry streamed
// out through a sink. Stored and deflated entries (deflate through miniz's tinfl; on the ESP32 the
// copy in ROM) with a caller-supplied workspace, so nothing big comes from the heap. Old PKZIP
// methods (shrink, implode) report Unsupported.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace axis::bbs {

struct ZipEntry {
    std::string name;
    uint16_t    method = 0;  // 0 stored, 8 deflate
    uint16_t    flags = 0, time = 0, date = 0;
    uint32_t    crc = 0, csize = 0, usize = 0, local = 0;
};

class ZipReader {
public:
    enum class Result { Ok, Stopped, Unsupported, Corrupt, ReadError };
    using ReadAt = std::function<bool(uint32_t offset, uint8_t* buf, size_t n)>;
    using Sink = std::function<bool(const uint8_t* p, size_t n)>;  // false stops the extraction
    static const size_t kWorkspace;  // bytes extract() needs: the inflater, a 32 KB window, input
    static constexpr size_t kMaxEntries = 400;

    ZipReader(ReadAt read, uint32_t fileSize) : read_(std::move(read)), size_(fileSize) {}
    bool                          open();  // finds and reads the central directory
    const std::vector<ZipEntry>&  entries() const { return entries_; }
    Result extract(const ZipEntry& e, uint8_t* workspace, size_t workspaceSize, const Sink& sink);

private:
    ReadAt                read_;
    uint32_t              size_;
    std::vector<ZipEntry> entries_;
};

}  // namespace axis::bbs
