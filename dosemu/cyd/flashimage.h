// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Door program images cached in the `door` flash partition and served read-only from there, so a
// door's code never takes RAM. Written in 4 KB chunks straight from the EXE (it can be bigger than
// free RAM); the fingerprint goes into NVS only after the last chunk verifies, so an interrupted
// write is always redone.
#pragma once
#include <esp_partition.h>  // also brings spi_flash_mmap_handle_t (esp_spi_flash.h on IDF 4.4)

#include <string>

#include "door/imagesource.h"

class FlashImageSource : public axis::door::ImageSource {
public:
    explicit FlashImageSource(const char* doorId, bool forceRewrite = false);
    ~FlashImageSource() override;
    const uint8_t* image(axis::door::HostFs& fs, const std::string& exe, uint16_t loadSeg, axis::door::MzInfo& mz,
                         size_t& len) override;
    bool        rewrote() const { return rewrote_; }
    uint32_t    key() const { return key_; }
    const char* error() const { return error_; }
    bool        exhaustedHeap() const override { return oom_; }

private:
    void                   unmap();
    const uint8_t*         imageBody(axis::door::HostFs& fs, const std::string& exe, uint16_t loadSeg, axis::door::MzInfo& mz,
                                     size_t& len, int& openHandle);
    std::string            id_;
    bool                   force_, rewrote_ = false, oom_ = false;
    uint32_t               key_ = 0;
    const char*            error_ = "";
    spi_flash_mmap_handle_t handle_ = 0;
    bool                   mapped_ = false;
};
