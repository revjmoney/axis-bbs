// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// The file areas, on Linux. They live in the data folder's sd/files (plan task 2): until then there is no card,
// so every area is empty, as on a board without one.
#include "platform/cyd/sdfiles.h"
#include "bbs/features.h"
#if AXIS_FILES

std::vector<axis::bbs::FileEntry> SdFileLibrary::list(const std::string&) { return {}; }
bool SdFileLibrary::read(const std::string&, const std::string&, std::string& out, size_t) {
    out.clear();
    return false;
}
std::unique_ptr<axis::bbs::RandomFile> SdFileLibrary::open(const std::string&, const std::string&) { return nullptr; }
std::unique_ptr<axis::bbs::WriteFile>  SdFileLibrary::create(const std::string&, const std::string&) { return nullptr; }
bool SdFileLibrary::describe(const std::string&, const std::string&, const std::string&) { return false; }
bool SdFileLibrary::remove(const std::string&, const std::string&) { return false; }
bool SdFileLibrary::rename(const std::string&, const std::string&, const std::string&) { return false; }

#endif  // AXIS_FILES
