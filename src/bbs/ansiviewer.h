// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ANSI Pack Viewer, a door built into the firmware: browses the artpack ZIPs in a file area (the
// ANSI Packs area) and shows the pieces straight out of the archives. It runs like any door, on
// the caller's line. A workspace may be passed in, or the viewer borrows the board's one inflate
// window (bbs/inflatelease.h) while a deflated piece plays and gives it back when the piece ends.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/lightbar.h"
#include "bbs/zipreader.h"
#include "door/bytestream.h"

namespace axis::bbs {

class AnsiViewer {
public:
    static constexpr int kPerPage = 54;  // three columns of 18

    AnsiViewer(FileLibrary& lib, std::string areaId, std::string areaName, uint8_t* workspace, size_t workspaceSize,
               door::ByteStream& line);
    ~AnsiViewer();
    void setZipState(uint8_t* state) { state_ = state; }  // ZipReader::kStateBytes, when the workspace is only the window
    // the time, for playing ANSI movies at modem speed (without it they go as fast as the line takes)
    void setClock(std::function<uint32_t()> clock) { clock_ = std::move(clock); }
    // Whether the board keeps at least minFree bytes free and a minLargest block with the borrowed window taken:
    // asked right after it is taken, and on every turn a piece plays from it, so a busy board refuses the piece
    // (or stops it) with a note instead of going under the heap floor.
    void setRoom(std::function<bool(uint32_t minFree, uint32_t minLargest)> room) { room_ = std::move(room); }
    static constexpr uint32_t kPlayFree = 61440, kPlayLargest = 32768;    // playing from the window: the busy-test floors
    static constexpr uint32_t kUnpackFree = 49152, kUnpackLargest = 28672;  // a moment's unpack into the cache (Rev: a little looser)
    // The SD view cache (bbs/viewcache.h): a deflated piece is unpacked there once and then played from the card
    // with no window held. Off: pieces play straight from the ZIP as before. A piece bigger than unpackMax is
    // never unpacked while a caller waits (one in one go held the loop for seconds): until the board's idle
    // filler has it in the cache, it plays from the ZIP.
    static constexpr uint32_t kUnpackOnViewMax = 65536;
    void setCache(bool on, uint32_t unpackMax = kUnpackOnViewMax) {
        cache_ = on;
        unpackMax_ = unpackMax;
    }
    // several file areas (the door's args "ansi,scene-acid,..."): a picker first; id and name each
    void setAreas(std::vector<std::pair<std::string, std::string>> areas) { areas_ = std::move(areas); }
    void begin();  // show the packs (or the areas, when there are several)
    bool step();   // handle waiting keys; false once the caller leaves (or hangs up)
    bool idle();   // the caller paused: a lone Esc on a list counts now; false if that left the viewer

private:
    enum class St { Areas, Packs, Pieces, Playing, Shown };
    void showAreas();
    static constexpr int kPageLines = 23;
    void send(const std::string& s) { line_.write((const uint8_t*)s.data(), s.size()); }
    void showPacks();
    bool openPack(size_t i);
    void showPieces();
    std::string shownName(size_t i) const;  // a piece's name in the list (the pack's common prefix left off)
    std::string                  prefix_;   // the prefix most of the pack's pieces share ("ANSI_Star_Wars_"), or ""
    int         columns() const;            // 3 narrow columns, or 2 wide ones for long names
    int         perPage() const { return columns() * 18; }
    void showPiece(size_t i);
    void pump();                                  // send the next part of the piece playing
    void finishPiece(ZipReader::Result result);  // its footer, and on to [Enter] next ...
    uint32_t now() const { return clock_ ? clock_() : 0; }
    bool key(char c);
    bool inflateReady() const;  // a passed-in block, or our own window plus inflater state
    bool grabInflate(uint32_t minFree, uint32_t minLargest);  // borrow the board's window; false when another caller has it or no room
    size_t pull(uint8_t* out, size_t max);  // the next bytes of the piece playing: from the cached copy or the ZIP
    ZipReader::Result pulled() const;       // how that went, once pull gives 0
    void releaseInflate();      // give it back (safe to call any time)
    bool number(char c, int& n);  // line input of a number; true on Enter with n set (0 = empty)
    bool back();                  // Esc or Q on a list: up a level; false when that leaves the viewer
    void showList(const std::string& title, const std::string& help);  // the header, the lightbar list, the help line

    FileLibrary&                 lib_;
    std::string                  area_, areaName_;
    std::vector<std::pair<std::string, std::string>> areas_;
    uint8_t*                     ws_;
    size_t                       wsSize_;
    uint8_t*                     state_ = nullptr;
    bool                         leased_ = false;  // ws_ and state_ are the board's window, lent to this viewer
    door::ByteStream&            line_;
    St                           st_ = St::Packs;
    std::vector<FileEntry>       packs_;
    size_t                       pack_ = 0;
    std::unique_ptr<RandomFile>  file_;
    std::unique_ptr<ZipReader>   zip_;
    std::vector<size_t>          pieces_;  // indexes into zip_->entries() that can be shown
    size_t                       piece_ = 0;
    int                          page_ = 0;
    std::string                  typed_;
    ScrollList                   list_{4, 4, 74, 18};  // every list here is a lightbar: areas, packs, pieces
    size_t                       areaAt_ = 0;
    int                          quiet_ = 0;           // steps with no key (a lone Esc counts after a pause)
    // the piece playing: pulled out of the ZIP a part at a time
    std::unique_ptr<ZipReader::Stream> stream_;
    std::string                  chunk_;  // pulled but not sent yet
    size_t                       chunkPos_ = 0;
    bool                         movie_ = false, nonstop_ = false, waiting_ = false;
    int                          lines_ = 0;
    uint32_t                     baud_ = 9600, lastPump_ = 0;
    std::function<uint32_t()>    clock_;
    std::function<bool(uint32_t, uint32_t)> room_;
    bool                         cache_ = false;
    uint32_t                     unpackMax_ = kUnpackOnViewMax;
    std::unique_ptr<RandomFile>  cached_;  // the piece playing, from the SD view cache (no window held)
    uint32_t                     cachedAt_ = 0;
    bool                         cachedBad_ = false;
    void shortOnMemory();  // the piece is dropped: back to the list, with the note
};

bool isAnsiPiece(const std::string& name);  // .ANS .ICE .CIA .DRK .LGO .ASC .NFO .DIZ .TXT .MEM

}  // namespace axis::bbs
