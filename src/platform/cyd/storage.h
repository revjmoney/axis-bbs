// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Picks where the BBS keeps its data: the SD card when one answers, else internal flash.
#pragma once
#include <Arduino.h>

void        storageInit();       // probes the SD card once at boot
bool        storageHasSd();
const char* storageSummary();    // e.g. "SD card 7580 MB (SDHC)" or "internal flash (no SD card)"
