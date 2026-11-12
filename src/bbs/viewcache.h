// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The ANSI viewer's SD view cache: each art pack copied once into an uncompressed ("stored") ZIP of just
// its pieces, so a caller views them straight off the card without the board's 44 KB inflate window (the
// window was what a busy board could not spare). One file per pack (about 270, not one per piece: a
// FAT folder of 12,000 files is slow to search), in the file area "viewpacks" on the main's card, never
// on a menu. A copy is named by a fingerprint of its pack's pieces (names, CRC-32s, sizes), so a pack
// that changes gets a new name, and copies no pack needs any more are deleted. A follower's viewer reads
// the main's copies over the link like any other file. The ZIPs callers download never change.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/zipreader.h"

namespace axis::bbs::viewcache {

constexpr const char* kArea = "viewpacks";  // ("viewcache" was the first try, a file per piece: left alone, see main.cpp)

// The copy's file name for a pack with these entries ("1A2B3C4D-0042.ZIP": fingerprint, piece count).
std::string cacheName(const std::vector<ZipEntry>& packEntries);
// Whether copy holds exactly the pack's viewable pieces, in order, with the same names, CRCs and sizes.
bool matches(const std::vector<ZipEntry>& packEntries, const std::vector<ZipEntry>& copyEntries);

// Makes the copies ahead of callers (the main, while nobody is online), one small step per loop turn:
// a pack opened, or up to stepBytes of a piece copied (a 1.7 MB movie in one go held the loop 7 s).
// The board's inflate window is taken only for a deflated piece, from its first step to its last, and
// only while room(minFree, minLargest) holds with it taken. space(bytes) says whether the card can take
// a copy that size. Then the copies no pack matches are deleted, one a step.
class Filler {
public:
    static constexpr size_t   kStepBytes = 16384;
    static constexpr uint32_t kFillFree = 49152, kFillLargest = 28672;  // a little under the busy-test floors (Rev)
    using Room = std::function<bool(uint32_t minFree, uint32_t minLargest)>;
    using Space = std::function<bool(uint64_t bytes)>;
    Filler(FileLibrary& lib, std::vector<std::string> areas, Room room = nullptr, Space space = nullptr,
           size_t stepBytes = kStepBytes);
    ~Filler();  // a copy part way is thrown away and the window given back
    Filler(const Filler&) = delete;
    Filler& operator=(const Filler&) = delete;
    bool step();  // false once the pass is over; a new Filler starts a new pass
    int  copied() const { return copied_; }    // packs copied this pass
    int  removed() const { return removed_; }  // stale copies deleted this pass

private:
    struct Rec {  // a piece written into the copy: for its central directory
        size_t   entry;
        uint32_t offset;
    };
    bool nextPack();
    bool startPack();       // the pack just opened: kept, skipped, or its copy begun
    bool copyStep();        // the next part of the copy
    bool finishCopy();      // the directory written and the copy kept
    void dropCopy();        // the copy part way thrown away, the window given back
    bool write(const void* p, size_t n);

    FileLibrary&                       lib_;
    std::vector<std::string>           areas_;
    Room                               room_;
    Space                              space_;
    size_t                             stepBytes_;
    bool                               listedCache_ = false;
    std::vector<std::string>           have_;  // the copies on the card when the pass began
    std::vector<bool>                  keep_;  // ...and whether a pack still matches each
    size_t                             area_ = 0, pack_ = 0, entry_ = 0, removeAt_ = 0;
    bool                               listed_ = false;
    std::vector<std::string>           packs_;
    std::unique_ptr<RandomFile>        file_;
    std::unique_ptr<ZipReader>         zip_;
    std::unique_ptr<WriteFile>         out_;  // the copy being written
    std::string                        outName_;
    uint32_t                           outBytes_ = 0;
    std::vector<Rec>                   recs_;
    std::unique_ptr<ZipReader::Stream> stream_;  // the piece being copied (declared last: it goes first)
    uint32_t                           crc_ = 0;
    bool                               ok_ = true, leased_ = false;
    bool                               openFailed_ = false;  // a pack that could not be read: no copy is deleted this pass
    int                                waitSteps_ = 0;       // after the heap said no: steps to wait before asking again
    int                                copied_ = 0, removed_ = 0;
};

}  // namespace axis::bbs::viewcache
