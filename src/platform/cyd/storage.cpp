// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/storage.h"

#include <SD.h>
#include <SPI.h>

// CYD microSD slot: VSPI pins, CS on GPIO5
static SPIClass sdSpi(VSPI);
static bool     haveSd = false;
static String   summary = "internal flash (no SD card)";

bool        storageHasSd() { return haveSd; }
const char* storageSummary() { return summary.c_str(); }

void storageInit() {
    sdSpi.begin(18, 19, 23, 5);
    // a slow first try wakes cards that dislike a fast start, then the normal speed
    for (uint32_t hz : {400000u, 20000000u}) {
        if (SD.begin(5, sdSpi, hz, "/sd", 4)) {
            haveSd = true;
            break;
        }
        SD.end();
    }
    uint8_t type = SD.cardType();
    if (haveSd) {
        const char* t = type == CARD_SDHC ? "SDHC/SDXC" : type == CARD_SD ? "SDSC" : type == CARD_MMC ? "MMC" : "?";
        summary = "SD card " + String((uint32_t)(SD.cardSize() >> 20)) + " MB (" + t + "), " +
                  String((uint32_t)(SD.usedBytes() >> 20)) + " MB used";
    } else if (type != CARD_NONE) {
        summary = "SD card answers but will not mount (not FAT16/FAT32?); using internal flash";
    } else {
        summary = "internal flash (no SD card answering)";
    }
    Serial.printf("axis: storage: %s\n", summary.c_str());
}
