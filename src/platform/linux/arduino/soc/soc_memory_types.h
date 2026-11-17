// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ESP32 memory regions: a PC has one kind of memory.
#pragma once
inline bool esp_ptr_byte_accessible(const void*) { return true; }
inline bool esp_ptr_internal(const void*) { return true; }
inline bool esp_ptr_external_ram(const void*) { return false; }
