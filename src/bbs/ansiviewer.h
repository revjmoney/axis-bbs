// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// ANSI Pack Viewer, a door built into the firmware: browses the artpack ZIPs in a file area (the
// ANSI Packs area) and shows the pieces straight out of the archives. It runs like any door, on
// the caller's line, with a workspace the board lends it (ZipReader::kWorkspace bytes).
#pragma once
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
    void setZipState(uint8_t* state) { state_ = state; }  // ZipReader::kStateBytes, when the workspace is only the window
    void begin();  // show the packs
    bool step();   // handle waiting keys; false once the caller leaves (or hangs up)

private:
    enum class St { Packs, Pieces, Shown };
    void send(const std::string& s) { line_.write((const uint8_t*)s.data(), s.size()); }
    void showPacks();
    bool openPack(size_t i);
    void showPieces();
    void showPiece(size_t i);
    bool key(char c);
    bool number(char c, int& n);  // line input of a number; true on Enter with n set (0 = empty)

    FileLibrary&                 lib_;
    std::string                  area_, areaName_;
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
};

bool isAnsiPiece(const std::string& name);  // .ANS .ICE .CIA .DRK .LGO .ASC .NFO .DIZ .TXT .MEM

}  // namespace axis::bbs
