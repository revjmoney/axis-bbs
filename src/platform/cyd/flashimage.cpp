// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/flashimage.h"

#include <Preferences.h>

#include <memory>
#include <vector>

#include "door/imagekey.h"

using namespace axis::door;

namespace {
constexpr size_t kChunk = 4096;    // flash erase sector
constexpr size_t kWindow = 16384;  // image bytes relocated per pass over the table (LORD: 22 passes)

const esp_partition_t* doorPartition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x41, "door");
}

bool readAt(HostFs& fs, int h, uint32_t at, uint8_t* dst, size_t n) {
    if (fs.seek(h, at, 0) != (int64_t)at) return false;
    return fs.read(h, dst, (int)n) == (int)n;
}
}  // namespace

FlashImageSource::FlashImageSource(const char* doorId, bool forceRewrite) : id_(doorId), force_(forceRewrite) {}

FlashImageSource::~FlashImageSource() { unmap(); }

void FlashImageSource::unmap() {
    if (mapped_) spi_flash_munmap(handle_);
    mapped_ = false;
}

const uint8_t* FlashImageSource::image(HostFs& fs, const std::string& exe, uint16_t loadSeg, MzInfo& mz, size_t& len) {
    unmap();
    rewrote_ = false;
    const esp_partition_t* part = doorPartition();
    if (!part) {
        error_ = "no door partition";
        return nullptr;
    }
    int h = fs.open(exe, 0, false, false);
    if (h < 0) {
        error_ = "door program not found";
        return nullptr;
    }
    std::unique_ptr<uint8_t[]> buf(new uint8_t[kChunk]);
    int64_t                    fileSize = fs.seek(h, 0, 2);
    if (fileSize < 28 || !readAt(fs, h, 0, buf.get(), 28) || !parseMz(buf.get(), (size_t)fileSize, mz)) {
        fs.close(h);
        error_ = "not a DOS program";
        return nullptr;
    }
    len = (mz.imageSize + kChunk - 1) / kChunk * kChunk;
    if (len > part->size) {
        fs.close(h);
        error_ = "program too big for the door partition";
        return nullptr;
    }

    // fingerprint of the whole EXE, read in chunks
    uint32_t k = imageKeyStart();
    fs.seek(h, 0, 0);
    for (int n; (n = fs.read(h, buf.get(), kChunk)) > 0;) k = imageKeyAdd(k, buf.get(), n);
    key_ = imageKeyFinish(k, loadSeg);

    Preferences prefs;
    prefs.begin("door", true);
    uint32_t storedKey = prefs.getUInt(("k_" + id_).c_str(), 0), storedLen = prefs.getUInt(("l_" + id_).c_str(), 0);
    if (prefs.getString("cur", "") != id_.c_str()) storedKey = 0;  // the partition holds another door's image
    prefs.end();

    if (force_ || storedKey != key_ || storedLen != len) {
        rewrote_ = true;
        prefs.begin("door", false);
        // every door's key goes: there is one partition, so this image replaces whichever door was
        // there (keeping the others' keys once let LORD run on top of BBS Black Jack's code)
        prefs.clear();
        prefs.end();
        // 16 KB of image at a time: one pass over the relocation table collects just that window's
        // targets, so RAM stays small even for LORD's 14,988 relocations
        std::unique_ptr<uint8_t[]> check(new uint8_t[kChunk]);
        for (uint32_t wbase = 0; wbase < len; wbase += kWindow) {
            uint32_t              wend = wbase + kWindow;
            std::vector<uint32_t> relocs;
            for (uint32_t i = 0; i < mz.relocCount;) {
                uint32_t n = mz.relocCount - i < kChunk / 4 ? mz.relocCount - i : kChunk / 4;
                if (!readAt(fs, h, mz.relocTable + i * 4u, check.get(), n * 4)) {
                    fs.close(h);
                    error_ = "can't read the relocation table";
                    return nullptr;
                }
                for (uint32_t at : relocTargets(check.get(), n, len))
                    if (at + 1 >= wbase && at < wend) relocs.push_back(at);  // a word touching this window
                i += n;
            }
            for (uint32_t base = wbase; base < wend && base < len; base += kChunk) {
                vTaskDelay(1);  // let WiFi and the idle task run
                memset(buf.get(), 0, kChunk);
                uint32_t have = base < mz.imageSize ? (mz.imageSize - base < kChunk ? mz.imageSize - base : kChunk) : 0;
                if (have && !readAt(fs, h, mz.headerSize + base, buf.get(), have)) {
                    fs.close(h);
                    error_ = "read error";
                    return nullptr;
                }
                relocateChunk(buf.get(), base, kChunk, relocs, loadSeg, [&](uint32_t at) {
                    uint8_t b = 0;  // the other half of a word split across chunks, unrelocated
                    if (at < mz.imageSize && !readAt(fs, h, mz.headerSize + at, &b, 1)) b = 0;
                    return b;
                });
                if (esp_partition_erase_range(part, base, kChunk) != ESP_OK ||
                    esp_partition_write(part, base, buf.get(), kChunk) != ESP_OK ||
                    esp_partition_read(part, base, check.get(), kChunk) != ESP_OK ||
                    memcmp(buf.get(), check.get(), kChunk) != 0) {
                    fs.close(h);
                    error_ = "flash write failed";
                    return nullptr;
                }
            }
        }
        prefs.begin("door", false);
        prefs.putUInt(("l_" + id_).c_str(), (uint32_t)len);
        prefs.putString("cur", id_.c_str());  // which door the one partition holds
        prefs.putUInt(("k_" + id_).c_str(), key_);  // last: the image is complete and verified
        prefs.end();
    }
    fs.close(h);

    const void* ptr = nullptr;
    size_t      mapLen = (len + 0xFFFF) & ~(size_t)0xFFFF;  // the MMU maps 64 KB pages
    if (mapLen > part->size) mapLen = part->size;
    if (esp_partition_mmap(part, 0, mapLen, SPI_FLASH_MMAP_DATA, &ptr, &handle_) != ESP_OK) {
        error_ = "can't map the door partition";
        return nullptr;
    }
    mapped_ = true;
    error_ = "";
    return (const uint8_t*)ptr;
}
