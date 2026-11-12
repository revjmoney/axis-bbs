// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ANSI viewer's SD cache: each deflated piece of an art pack unpacked once into a plain file, so a
// caller views it straight off the card without the board's 44 KB inflate window (the window was what
// a busy board could not spare). The files sit in their own folder on the main's card (the file area
// "viewcache", never on a menu) named by the piece's CRC-32 and size, so a pack that changes gets new
// names and two packs holding the same piece share one file. The ZIPs callers download never change.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/zipreader.h"

namespace axis::bbs::viewcache {

constexpr const char* kArea = "viewcache";

std::string name(const ZipEntry& e);  // "1A2B3C4D-E77.ANS": CRC-32 and size in hex

// The cached copy of e, or nullptr (not there yet, or not the right size).
std::unique_ptr<RandomFile> open(FileLibrary& lib, const ZipEntry& e);

// Unpacks e into the cache. ws/wsSize/state as for ZipReader::extract (the board's lent window). The
// copy is kept only when it unpacked whole and its CRC-32 matches the ZIP's; true when the cache has it.
bool unpack(FileLibrary& lib, ZipReader& zip, const ZipEntry& e, uint8_t* ws, size_t wsSize, uint8_t* state);

// Fills the cache ahead of callers (the main, while nobody is online): one piece per step(), each with
// the board's inflate window taken and given straight back. Pieces the viewer can show (isAnsiPiece)
// and deflated ones only: a stored piece needs no window.
class Filler {
public:
    Filler(FileLibrary& lib, std::vector<std::string> areas) : lib_(lib), areas_(std::move(areas)) {}
    // One piece unpacked (or one pack looked at). False once every pack of every area has been
    // gone through; a new Filler starts a new pass.
    bool step();
    int  unpacked() const { return unpacked_; }  // pieces this pass added

private:
    bool nextPack();  // opens the next pack; false when there are none left
    FileLibrary&                lib_;
    std::vector<std::string>    areas_;
    size_t                      area_ = 0;
    std::vector<std::string>    packs_;  // the current area's ZIPs, listed when the area starts
    bool                        listed_ = false;
    size_t                      pack_ = 0;
    std::unique_ptr<RandomFile> file_;
    std::unique_ptr<ZipReader>  zip_;
    size_t                      entry_ = 0;
    int                         unpacked_ = 0;
};

}  // namespace axis::bbs::viewcache
