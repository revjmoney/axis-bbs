// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Picks where the BBS keeps its data: the SD card when one answers, else internal flash.
#pragma once
#include <Arduino.h>

void        storageInit();       // probes the SD card once at boot
bool        storageHasSd();
const char* storageRoot();       // where files live in the VFS: "/sd", or "/littlefs" with no card
const char* storageSummary();    // e.g. "SD card 7580 MB (SDHC)" or "internal flash (no SD card)"
uint64_t    storageFreeBytes(bool fresh = false);  // the SD card's free space (0 without one); looked up every 30 s
// Sysop tool: erase the card by writing a fresh FAT32 filesystem. Destroys everything on it.
bool        storageFormatSd(String& result);
