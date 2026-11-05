// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ANSI Pack Viewer, a door built into the firmware: browses the artpack ZIPs in a file area (the
// ANSI Packs area) and shows the pieces straight out of the archives. It runs like any door, on
// the caller's line, with a workspace the board lends it (ZipReader::kWorkspace bytes).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/zipreader.h"
#include "door/bytestream.h"

namespace axis::bbs {

class AnsiViewer {
public:
    static constexpr int kPerPage = 54;  // three columns of 18

    AnsiViewer(FileLibrary& lib, std::string areaId, std::string areaName, uint8_t* workspace, size_t workspaceSize,
               door::ByteStream& line);
    void setZipState(uint8_t* state) { state_ = state; }
    // the time, for playing ANSI movies at modem speed (without it they go as fast as the line takes)
    void setClock(std::function<uint32_t()> clock) { clock_ = std::move(clock); }  // ZipReader::kStateBytes, when the workspace is only the window
    // several file areas (the door's args "ansi,scene-acid,..."): a picker first; id and name each
    void setAreas(std::vector<std::pair<std::string, std::string>> areas) { areas_ = std::move(areas); }
    void begin();  // show the packs (or the areas, when there are several)
    bool step();   // handle waiting keys; false once the caller leaves (or hangs up)

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
    bool number(char c, int& n);  // line input of a number; true on Enter with n set (0 = empty)

    FileLibrary&                 lib_;
    std::string                  area_, areaName_;
    std::vector<std::pair<std::string, std::string>> areas_;
    uint8_t*                     ws_;
    size_t                       wsSize_;
    uint8_t*                     state_ = nullptr;
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
    // the piece playing: pulled out of the ZIP a part at a time
    std::unique_ptr<ZipReader::Stream> stream_;
    std::string                  chunk_;  // pulled but not sent yet
    size_t                       chunkPos_ = 0;
    bool                         movie_ = false, nonstop_ = false, waiting_ = false;
    int                          lines_ = 0;
    uint32_t                     baud_ = 9600, lastPump_ = 0;
    std::function<uint32_t()>    clock_;
};

bool isAnsiPiece(const std::string& name);  // .ANS .ICE .CIA .DRK .LGO .ASC .NFO .DIZ .TXT .MEM

}  // namespace axis::bbs
