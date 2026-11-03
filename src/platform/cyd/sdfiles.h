// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The file areas on the SD card: /sd/files/<area id>/, each with an optional FILES.BBS list.
#pragma once
#include "bbs/filelib.h"

class SdFileLibrary : public axis::bbs::FileLibrary {
public:
    std::vector<axis::bbs::FileEntry> list(const std::string& areaId) override;
    bool read(const std::string& areaId, const std::string& name, std::string& out, size_t max) override;
};
