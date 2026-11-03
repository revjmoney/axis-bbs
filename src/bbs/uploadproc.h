// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// What happens to an upload once it has arrived: a ZIP is opened, every entry is unpacked and its
// CRC-32 checked, our AXISBBS.NFO is added (replacing an older one), and a clean standard ZIP is
// written in its place (verified entries copied as they are). A damaged ZIP, or one packed with an
// old PKZIP method we can't verify, is rejected and removed. Anything that isn't a ZIP is kept as
// uploaded. Runs a step at a time so the host can show progress; the workspace is
// ZipReader::kWorkspace bytes lent by the host.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "bbs/filelib.h"
#include "bbs/zipreader.h"

namespace axis::bbs {

struct UploadInfo {  // what AXISBBS.NFO says about the board
    std::string bbsName, address, location, software;
};

class UploadProcessor {
public:
    enum class Result { Running, Rezipped, Kept, Rejected };
    enum class Stage { Open, Check, Write, Info, Finish, Done };
    static constexpr const char* kInfoName = "AXISBBS.NFO";

    UploadProcessor(FileLibrary& lib, std::string area, std::string name, UploadInfo info, uint8_t* workspace,
                    size_t workspaceSize);
    bool               step();  // false once finished
    Result             result() const { return result_; }
    Stage              stage() const { return stage_; }
    double             progress() const;  // 0..1
    const std::string& note() const { return note_; }
    size_t             entries() const { return keep_.size(); }
    size_t             entryAt() const { return at_; }
    std::string        current() const;  // the entry being worked on

private:
    bool reject(const std::string& why);
    bool writeEntry(const std::string& name, uint16_t method, uint16_t flags, uint16_t time, uint16_t date, uint32_t crc,
                    uint32_t csize, uint32_t usize);
    FileLibrary&                lib_;
    std::string                 area_, name_, temp_;
    UploadInfo                  info_;
    uint8_t*                    ws_;
    size_t                      wsSize_;
    std::unique_ptr<RandomFile> in_;
    std::unique_ptr<ZipReader>  zip_;
    std::unique_ptr<WriteFile>  out_;
    std::vector<size_t>         keep_;  // entries carried over (everything but an old AXISBBS.NFO)
    struct Written {
        std::string name;
        uint16_t    method, flags, time, date;
        uint32_t    crc, csize, usize, offset;
    };
    std::vector<Written> written_;
    uint32_t             outPos_ = 0;
    size_t               at_ = 0;
    Stage                stage_ = Stage::Open;
    Result               result_ = Result::Running;
    std::string          note_;
};

}  // namespace axis::bbs
