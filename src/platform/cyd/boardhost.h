// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The board's answers to the shared code's Host (app/host.h): millis, the hardware random number, the board's event log,
// the storage folders and the heap. The Linux axis program builds the same file over its Arduino shim.
#pragma once
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <time.h>

#include <string>

#include "app/host.h"
#include "platform/cyd/heapscope.h"
#include "platform/cyd/storage.h"

class BoardHost : public axis::app::Host {
public:
    using LogFn = void (*)(const std::string& line);
    explicit BoardHost(LogFn log) : log_(log) {}

    uint32_t nowMs() override { return millis(); }
    uint32_t unixNow() override {
        time_t now = time(nullptr);
        return now < 1700000000 ? 0 : (uint32_t)now;
    }
    uint32_t    random32() override { return esp_random(); }
    void        event(const std::string& line) override { log_(line); }
    std::string sdRoot() override { return storageRoot(); }
    bool        hasSd() override { return storageHasSd(); }
    uint64_t    sdFreeBytes() override { return storageFreeBytes(); }
    std::string flashRoot() override { return "/littlefs"; }
    bool        roomFor(uint32_t minFree, uint32_t minLargest) override {
        return heap_caps_get_free_size(MALLOC_CAP_8BIT) >= minFree && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= minLargest;
    }
    uint32_t heapMark() override { return heapScopeMark(); }
    void     heapNote(const char* where, uint32_t mark) override { heapScopeCheck(where, mark); }

private:
    LogFn log_;
};
