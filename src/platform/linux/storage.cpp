// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Where the BBS keeps its data, on Linux. The data folder (sd/ as the card, flash/ as internal flash) is plan
// task 2: until then there is no card and the summary says so.
#include "platform/cyd/storage.h"

void        storageInit() { Serial.printf("axis: storage: %s\n", storageSummary()); }
bool        storageHasSd() { return false; }
const char* storageRoot() { return "/littlefs"; }
const char* storageSummary() { return "no data folder yet (no SD card)"; }
uint64_t    storageFreeBytes(bool fresh) {
    (void)fresh;
    return 0;
}

bool storageFormatSd(String& result) {
    result = "no SD card here: the data folder is a folder on the PC";
    return false;
}
