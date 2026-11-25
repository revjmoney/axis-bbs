// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The file areas on the SD card: /sd/files/<area id>/, each with an optional FILES.BBS list. The PC program (axispc) gives
// its own folder (<data>/sd/files) and says the card is always there.
#pragma once
#include <functional>
#include <string>
#include <utility>

#include "bbs/filelib.h"

bool storageHasSd();  // platform/cyd/storage.h (the board's card answered at boot)

class SdFileLibrary : public axis::bbs::FileLibrary {
public:
    SdFileLibrary() : SdFileLibrary("/sd/files", [] { return storageHasSd(); }) {}  // the board's card
    SdFileLibrary(std::string filesFolder, std::function<bool()> hasCard) : folder_(std::move(filesFolder)), hasCard_(std::move(hasCard)) {}
    std::vector<axis::bbs::FileEntry> list(const std::string& areaId) override;
    bool read(const std::string& areaId, const std::string& name, std::string& out, size_t max) override;
    std::unique_ptr<axis::bbs::RandomFile> open(const std::string& areaId, const std::string& name) override;
    std::unique_ptr<axis::bbs::WriteFile>  create(const std::string& areaId, const std::string& name) override;
    bool describe(const std::string& areaId, const std::string& name, const std::string& desc) override;
    bool remove(const std::string& areaId, const std::string& name) override;
    bool rename(const std::string& areaId, const std::string& from, const std::string& to) override;
    bool moveTo(const std::string& fromArea, const std::string& name, const std::string& toArea) override;

private:
    std::string folderOf(const std::string& areaId) const { return folder_ + "/" + areaId; }
    bool        hasSd() const { return hasCard_ && hasCard_(); }
    std::string           folder_;
    std::function<bool()> hasCard_;
};
