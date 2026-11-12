// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "heapscope.h"

#include <Arduino.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>

#include "web.h"

namespace {
void (*hook_)(const std::string&) = nullptr;
char          pending_[96];  // one held line from core 0: a newer one replaces it
volatile bool havePending_ = false;
portMUX_TYPE  mux_ = portMUX_INITIALIZER_UNLOCKED;
}  // namespace

void heapScopeHook(void (*log)(const std::string&)) { hook_ = log; }

void heapScopeFlush() {
    if (!havePending_ || !hook_) return;
    char line[sizeof pending_];
    portENTER_CRITICAL(&mux_);
    std::memcpy(line, pending_, sizeof line);
    havePending_ = false;
    portEXIT_CRITICAL(&mux_);
    hook_(line);
}

HeapScope::HeapScope(const char* name) : name_(name), before_((unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)) {}

HeapScope::~HeapScope() {
    unsigned after = (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    if (after >= before_ || !hook_) return;
    char line[sizeof pending_];
    std::snprintf(line, sizeof line, "heap low in %s: min %u->%u, largest %u, web=%d", name_, before_, after,
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), web::busy() ? 1 : 0);
    if (xPortGetCoreID() == 1) {
        hook_(line);
        return;
    }
    portENTER_CRITICAL(&mux_);
    std::memcpy(pending_, line, sizeof line);
    havePending_ = true;
    portEXIT_CRITICAL(&mux_);
}
