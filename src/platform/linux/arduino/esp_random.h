// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The hardware random number generator on Linux: the kernel's (getrandom).
#pragma once
#include <cstddef>
#include <cstdint>
extern "C" uint32_t esp_random(void);
extern "C" void     esp_fill_random(void* buf, size_t len);
