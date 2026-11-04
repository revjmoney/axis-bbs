// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
#include "platform/cyd/storage.h"

#include <SD.h>
#include <SPI.h>

#include "ff.h"

// CYD microSD slot: VSPI pins, CS on GPIO5
static SPIClass sdSpi(VSPI);
static bool     haveSd = false;
static String   summary = "internal flash (no SD card)";

bool        storageHasSd() { return haveSd; }
const char* storageSummary() { return summary.c_str(); }
const char* storageRoot() { return haveSd ? "/sd" : "/littlefs"; }

void storageInit() {
    sdSpi.begin(18, 19, 23, 5);
    // a slow first try wakes cards that dislike a fast start, then mount at the normal speed (a card
    // left at 400 kHz reads about 50 KB/s); stay slow only if the fast mount fails
    SD.begin(5, sdSpi, 400000, "/sd", 10);
    SD.end();
    haveSd = SD.begin(5, sdSpi, 20000000, "/sd", 10);
    if (!haveSd) {
        SD.end();
        haveSd = SD.begin(5, sdSpi, 400000, "/sd", 10);
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

uint64_t storageFreeBytes(bool fresh) {
    static uint64_t bytes = 0;
    static uint32_t at = 0;
    if (!haveSd) return 0;
    if (fresh || !at || millis() - at > 30000) {
        bytes = SD.totalBytes() - SD.usedBytes();
        at = millis() | 1;
    }
    return bytes;
}

// The SD library registers the card as FatFs drive "0:" (the only FatFs drive on this board).
// Unmount the filesystem, write a new FAT32 volume over the whole card, then mount it again.
bool storageFormatSd(String& result) {
    if (!haveSd) {
        result = "no SD card is mounted, nothing to format";
        return false;
    }
    const char* drv = "0:";
    DWORD       freeClusters;
    FATFS*      fs;
    if (f_getfree(drv, &freeClusters, &fs) != FR_OK) {  // make sure "0:" really is the card
        result = "could not find the card's drive";
        return false;
    }
    f_mount(NULL, drv, 0);
    const UINT workLen = 4096;
    void*      work = malloc(workLen);
    if (!work) {
        result = "not enough memory to format";
        return false;
    }
    FRESULT r = f_mkfs(drv, FM_FAT32, 0, work, workLen);
    free(work);
    SD.end();
    haveSd = false;
    storageInit();
    if (r != FR_OK) {
        result = "format failed (FatFs error " + String((int)r) + ")";
        return false;
    }
    result = String("formatted FAT32: ") + summary;
    return haveSd;
}
