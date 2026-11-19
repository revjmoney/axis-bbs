// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// Where the BBS keeps its data, on Linux: the profile's card folder (paths.h: <data>/sd solo, <data>/mirror as the
// mirror main) is the card, and it is always there; a cluster follower has none, as a cardless board. storageRoot()
// is the board's own name for it ("/sd"); fs_remap.cpp turns that into the folder wherever a path reaches the C library.
#include "platform/cyd/storage.h"

#include <SD.h>
#include <sys/stat.h>

#include <string>

#include "platform/linux/paths.h"

static String summary = "no data folder";
static bool   haveSd = false;

bool        storageHasSd() { return haveSd; }
const char* storageRoot() { return "/sd"; }
const char* storageSummary() { return summary.c_str(); }

// (@SDCHECK runs this again: it only looks at the folder, so that is safe)
void storageInit() {
    std::string dir = hostPath("/sd");
    struct stat st;
    haveSd = !dir.empty() && ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    if (haveSd)
        summary = String("data folder ") + dir.c_str() + ", " + String((uint32_t)((SD.totalBytes() - SD.usedBytes()) >> 20)) +
                  " MB free";
    else if (axisProfile() == Profile::Follower)
        summary = "no card: a cluster follower uses the main's files";
    else
        summary = String("data folder ") + (dir.empty() ? "not set" : dir.c_str()) + " has no card folder";
    Serial.printf("axis: storage: %s\n", summary.c_str());
}

uint64_t storageFreeBytes(bool fresh) {
    static uint64_t bytes = 0;
    static uint32_t at = 0;
    if (!haveSd) return 0;
    if (fresh || !at || millis() - at > 30000) {  // looked up every 30 s, as on the board
        bytes = SD.totalBytes() - SD.usedBytes();
        at = millis() | 1;
    }
    return bytes;
}

bool storageFormatSd(String& result) {
    result = "the card is a folder on this computer (" + String(hostPath("/sd").c_str()) +
             "): it is not formatted from here";
    return false;
}
