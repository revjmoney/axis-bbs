// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ESP-IDF's heap queries on Linux: one plain heap, reported as a fixed 64 MiB free (see Esp.h).
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "Esp.h"
#include "esp_err.h"

#define MALLOC_CAP_EXEC (1 << 0)
#define MALLOC_CAP_32BIT (1 << 1)
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_DMA (1 << 3)
#define MALLOC_CAP_SPIRAM (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)

typedef void (*esp_alloc_failed_hook_t)(size_t size, uint32_t caps, const char* function_name);

inline size_t    heap_caps_get_free_size(uint32_t) { return kHostHeapBytes; }
inline size_t    heap_caps_get_largest_free_block(uint32_t) { return kHostHeapBytes; }
inline size_t    heap_caps_get_minimum_free_size(uint32_t) { return kHostHeapBytes; }
inline size_t    heap_caps_get_total_size(uint32_t) { return kHostHeapBytes; }
inline esp_err_t heap_caps_register_failed_alloc_callback(esp_alloc_failed_hook_t) { return ESP_OK; }  // never called here
inline void*     heap_caps_malloc(size_t n, uint32_t) { return std::malloc(n); }
inline void*     heap_caps_calloc(size_t n, size_t size, uint32_t) { return std::calloc(n, size); }
inline void*     heap_caps_realloc(void* p, size_t n, uint32_t) { return std::realloc(p, n); }
inline void      heap_caps_free(void* p) { std::free(p); }
