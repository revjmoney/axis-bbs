// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Rev. J. Money
// HostFs over a folder on the PC: the same POSIX code the CYD runs, so every PC test covers it.
#pragma once
#include <filesystem>

#include "platform/posix/posixhostfs.h"

namespace axis {
namespace pc {

class StdHostFs : public PosixHostFs {
public:
    explicit StdHostFs(const std::filesystem::path& root) : PosixHostFs(root.string()) {}
};

}  // namespace pc
}  // namespace axis
